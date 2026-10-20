// Meters: LUFS Ölçer (ITU-R BS.1770-4 / EBU R128) and Spektrum Analizörü. Both leave the audio
// untouched (bit-exact); the plug-in thread only measures, the windows read the results lock-free.
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FxDsp.h"
#include "audio/fx/MeterEffects.h"
#include "ui/Theme.h"
#include <juce_dsp/juce_dsp.h>

namespace fx
{
//==============================================================================
LufsMeter::LufsMeter (const Info& i) : BuiltInEffect (i) {}

void LufsMeter::kWeighting (double rate, BiquadCoeffs& shelf, BiquadCoeffs& highPass) noexcept
{
    // BS.1770 pre-filter and RLB high-pass for any rate (the bilinear designs used by libebur128;
    // at 48 kHz they give the coefficients printed in the standard).
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / rate), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                  2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / rate), a0 = 1.0 + K / Q + K * K;
        highPass = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
    }
}

void LufsMeter::prepareEffect()
{
    BiquadCoeffs s, h;
    kWeighting (getRate(), s, h);
    for (auto& f : pre) f.k = s;
    for (auto& f : rlb) f.k = h;
    subLen = std::max (1, (int) std::lround (0.1 * getRate()));
}

void LufsMeter::resetEffect()
{
    for (auto& f : pre) f.reset();
    for (auto& f : rlb) f.reset();
    for (auto& t : tp) t.reset();
    sumSq.fill (0.0);
    subCount = 0;
    ring.fill (0.0);
    ringPos = ringFill = 0;
}

void LufsMeter::clearResults() noexcept
{
    for (auto& b : intHist) b.store (0, std::memory_order_relaxed);
    for (auto& b : stHist) b.store (0, std::memory_order_relaxed);
    momentary.store (-200.0f); shortTerm.store (-200.0f);
    maxMomentary.store (-200.0f); maxShortTerm.store (-200.0f);
    truePeak.store (0.0f);
    measuredSeconds.store (0.0);
}

double LufsMeter::loudness (double energy) noexcept { return energy > 0.0 ? -0.691 + 10.0 * std::log10 (energy) : -200.0; }

int LufsMeter::binOf (double lufs) noexcept
{
    return std::clamp ((int) std::lround ((lufs - histMin) * 10.0), 0, histBins - 1);
}

double LufsMeter::binEnergy (int bin) noexcept { return std::pow (10.0, (histMin + bin * 0.1 + 0.691) / 10.0); }

void LufsMeter::processEffect (float* const* ch, int numCh, int n) noexcept
{
    if (resetRequested.exchange (false, std::memory_order_acquire))
    {
        clearResults();
        resetEffect();
    }

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < numCh; ++c)
        {
            const double y = rlb[(size_t) c].process (pre[(size_t) c].process (ch[c][i]));
            sumSq[(size_t) c] += y * y;
            peak = std::max (peak, (float) tp[(size_t) c].push (ch[c][i]));
        }
        if (++subCount < subLen) continue;

        // a 100 ms step: sum over the channels (left and right weight 1)
        double e = 0.0;
        for (int c = 0; c < numCh; ++c) e += sumSq[(size_t) c] / subLen;
        sumSq.fill (0.0);
        subCount = 0;
        ring[(size_t) ringPos] = e;
        ringPos = (ringPos + 1) % ringSize;
        ringFill = std::min (ringFill + 1, ringSize);
        measuredSeconds.store (measuredSeconds.load (std::memory_order_relaxed) + 0.1, std::memory_order_relaxed);

        auto meanOfLast = [this] (int count)
        {
            double s = 0.0;
            for (int k = 1; k <= count; ++k) s += ring[(size_t) ((ringPos - k + ringSize) % ringSize)];
            return s / count;
        };
        if (ringFill >= 4)   // momentary: 400 ms, every 100 ms (75 % overlap) - also the gating blocks
        {
            const double l = loudness (meanOfLast (4));
            momentary.store ((float) l, std::memory_order_relaxed);
            if (l > maxMomentary.load (std::memory_order_relaxed)) maxMomentary.store ((float) l, std::memory_order_relaxed);
            if (l > absoluteGate) intHist[(size_t) binOf (l)].fetch_add (1, std::memory_order_relaxed);
        }
        if (ringFill >= 30)  // short-term: 3 s
        {
            const double l = loudness (meanOfLast (30));
            shortTerm.store ((float) l, std::memory_order_relaxed);
            if (l > maxShortTerm.load (std::memory_order_relaxed)) maxShortTerm.store ((float) l, std::memory_order_relaxed);
            if (l > absoluteGate) stHist[(size_t) binOf (l)].fetch_add (1, std::memory_order_relaxed);
        }
    }
    for (auto& f : pre) f.flush();
    for (auto& f : rlb) f.flush();
    storeMax (truePeak, peak);
}

double LufsMeter::integratedLufs() const
{
    // BS.1770-4: blocks above -70 LUFS; relative gate 10 LU under their mean; mean of the rest.
    double sum = 0.0, count = 0.0;
    for (int b = 0; b < histBins; ++b)
        if (const auto k = intHist[(size_t) b].load (std::memory_order_relaxed)) { sum += k * binEnergy (b); count += k; }
    if (count <= 0.0) return -200.0;
    const double relGate = loudness (sum / count) - 10.0;
    sum = count = 0.0;
    for (int b = binOf (relGate); b < histBins; ++b)
        if (histMin + b * 0.1 < relGate - 1.0e-9) continue;
        else if (const auto k = intHist[(size_t) b].load (std::memory_order_relaxed)) { sum += k * binEnergy (b); count += k; }
    return count > 0.0 ? loudness (sum / count) : -200.0;
}

double LufsMeter::loudnessRange() const
{
    // EBU Tech 3342: short-term values above -70 LUFS, relative gate 20 LU under their mean;
    // LRA = 95th - 10th percentile of the rest.
    double sum = 0.0, count = 0.0;
    for (int b = 0; b < histBins; ++b)
        if (const auto k = stHist[(size_t) b].load (std::memory_order_relaxed)) { sum += k * binEnergy (b); count += k; }
    if (count <= 0.0) return 0.0;
    const double relGate = loudness (sum / count) - 20.0;
    std::vector<std::pair<int, uint32_t>> kept;
    double total = 0.0;
    for (int b = 0; b < histBins; ++b)
        if (histMin + b * 0.1 >= relGate - 1.0e-9)
            if (const auto k = stHist[(size_t) b].load (std::memory_order_relaxed)) { kept.push_back ({ b, k }); total += k; }
    if (total <= 0.0) return 0.0;
    auto percentile = [&] (double p)
    {
        const double target = p * (total - 1.0);
        double acc = 0.0;
        for (auto& [b, k] : kept)
        {
            acc += k;
            if (acc - 1.0 >= target) return histMin + b * 0.1;
        }
        return histMin + kept.back().first * 0.1;
    };
    return std::max (0.0, percentile (0.95) - percentile (0.10));
}

LufsMeter::Readings LufsMeter::read() const
{
    Readings r;
    r.momentary = momentary.load (std::memory_order_relaxed);
    r.shortTerm = shortTerm.load (std::memory_order_relaxed);
    r.integrated = (float) integratedLufs();
    r.range = (float) loudnessRange();
    r.maxMomentary = maxMomentary.load (std::memory_order_relaxed);
    r.maxShortTerm = maxShortTerm.load (std::memory_order_relaxed);
    r.truePeakDb = (float) gainToDb (truePeak.load (std::memory_order_relaxed));
    r.seconds = measuredSeconds.load (std::memory_order_relaxed);
    r.channels = getActiveChannels();
    return r;
}

namespace
{
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    juce::String lufsText (float v, const char* unit = " LUFS")
    {
        if (v < -150.0f) return u8 ("—");
        return juce::String (v, 1).replaceCharacter ('.', ',') + u8 (unit);
    }

    //==============================================================================
    class LufsDisplay final : public juce::Component, private juce::Timer
    {
    public:
        explicit LufsDisplay (LufsMeter& m) : meter (m)
        {
            resetBtn.setButtonText (u8 ("Sıfırla"));
            resetBtn.setTooltip (u8 ("Tümleşik ölçümü, en yüksek değerleri ve gerçek tepeyi sıfırlar"));
            resetBtn.onClick = [this] { meter.requestReset(); history.fill (-200.0f); };
            addAndMakeVisible (resetBtn);
            history.fill (-200.0f);
            setSize (436, 250);
            startTimerHz (10);
        }

        void resized() override { resetBtn.setBounds (getWidth() - 90, 0, 90, 24); }

        void paint (juce::Graphics& g) override
        {
            using namespace micvst::theme;
            const auto r = meter.read();
            auto area = getLocalBounds();
            auto top = area.removeFromTop (110);

            auto cell = [&] (juce::Rectangle<int> b, const juce::String& label, const juce::String& value, bool big)
            {
                g.setColour (textDim);
                g.setFont (juce::Font (juce::FontOptions (12.5f)));
                g.drawText (label, b.removeFromTop (16), juce::Justification::centredLeft);
                g.setColour (textBright);
                g.setFont (juce::Font (juce::FontOptions (big ? 30.0f : 17.0f, big ? juce::Font::bold : juce::Font::plain)));
                g.drawText (value, b.removeFromTop (big ? 36 : 22), juce::Justification::centredLeft);
            };
            auto left = top.removeFromLeft (200);
            cell (left.removeFromTop (54), u8 ("Kısa süreli (3 s)"), lufsText (r.shortTerm), true);
            cell (left, u8 ("Anlık (400 ms)"), lufsText (r.momentary), false);
            top.removeFromTop (28);   // under the reset button
            auto c1 = top.removeFromLeft (118), c2 = top;
            cell (c1.removeFromTop (40), u8 ("Tümleşik"), lufsText (r.integrated), false);
            cell (c1, u8 ("Aralık (LRA)"), r.range > 0.0f ? lufsText (r.range, " LU") : u8 ("—"), false);
            cell (c2.removeFromTop (40), u8 ("Gerçek tepe"), r.truePeakDb < -150.0f ? u8 ("—") : lufsText (r.truePeakDb, " dBTP"), false);
            {
                const int s = (int) r.seconds;
                cell (c2, u8 ("Ölçülen süre"), juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2), false);
            }

            // short-term history (last 60 s), -50 ... -5 LUFS
            area.removeFromTop (8);
            auto graph = area.removeFromTop (110).toFloat();
            g.setColour (meterBack);
            g.fillRect (graph);
            const float lo = -50.0f, hi = -5.0f;
            auto yOf = [&] (float v) { return graph.getBottom() - (std::clamp (v, lo, hi) - lo) / (hi - lo) * graph.getHeight(); };
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            for (float v : { -40.0f, -30.0f, -23.0f, -16.0f, -10.0f })
            {
                g.setColour (outline);
                g.drawHorizontalLine ((int) yOf (v), graph.getX(), graph.getRight());
                g.setColour (textDim);
                g.drawText (juce::String ((int) v), (int) graph.getX() + 3, (int) yOf (v) - 12, 40, 12, juce::Justification::centredLeft);
            }
            juce::Path p;
            bool started = false;
            for (int k = 0; k < historySize; ++k)
            {
                const float v = history[(size_t) ((historyPos + k) % historySize)];
                const float x = graph.getX() + graph.getWidth() * (float) k / (historySize - 1);
                if (v < -150.0f) { started = false; continue; }
                if (! started) { p.startNewSubPath (x, yOf (v)); started = true; }
                else p.lineTo (x, yOf (v));
            }
            g.setColour (accent);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            g.setColour (textDim);
            g.drawText (u8 ("son 60 s (kısa süreli)"), graph.reduced (4).toNearestInt(), juce::Justification::bottomRight);
            if (r.channels == 1)
                g.drawText (u8 ("mono"), graph.reduced (4).toNearestInt(), juce::Justification::topRight);
        }

    private:
        void timerCallback() override
        {
            history[(size_t) historyPos] = meter.read().shortTerm;
            historyPos = (historyPos + 1) % historySize;
            repaint();
        }

        static constexpr int historySize = 600;
        LufsMeter& meter;
        juce::TextButton resetBtn;
        std::array<float, historySize> history {};
        int historyPos = 0;
    };
}

std::unique_ptr<juce::Component> LufsMeter::createDisplay() { return std::make_unique<LufsDisplay> (*this); }

//==============================================================================
SpectrumAnalyser::SpectrumAnalyser (const Info& i) : BuiltInEffect (i) {}

void SpectrumAnalyser::processEffect (float* const* ch, int numCh, int n) noexcept
{
    uint32_t w = written.load (std::memory_order_relaxed);
    const float k = numCh > 1 ? 0.5f : 1.0f;
    for (int i = 0; i < n; ++i, ++w)
    {
        float v = ch[0][i];
        if (numCh > 1) v += ch[1][i];
        ring[(size_t) (w & (ringSize - 1))] = v * k;
    }
    written.store (w, std::memory_order_release);
}

int SpectrumAnalyser::copyLatest (float* dest, int count) const noexcept
{
    const uint32_t w = written.load (std::memory_order_acquire);
    const int avail = (int) std::min<uint32_t> (w, (uint32_t) count);
    for (int k = 0; k < count; ++k)
        dest[k] = k < count - avail ? 0.0f : ring[(size_t) ((w - (uint32_t) (count - k)) & (ringSize - 1))];
    return avail;
}

namespace
{
    class SpectrumDisplay final : public juce::Component, private juce::Timer
    {
    public:
        static constexpr int order = 13, size = 1 << order;

        explicit SpectrumDisplay (SpectrumAnalyser& a) : analyser (a), fft (order)
        {
            for (int i = 0; i < size; ++i)
            {
                window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (size - 1));
                windowSum += window[(size_t) i];
            }
            shown.fill (-120.0f);
            peaks.fill (-120.0f);
            setSize (436, 240);
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            using namespace micvst::theme;
            auto b = getLocalBounds().toFloat();
            g.setColour (meterBack);
            g.fillRect (b);
            const float lo = -100.0f, hi = 0.0f;
            const double fLo = 20.0, fHi = std::min (20000.0, analyser.getSampleRateNow() * 0.5);
            auto xOf = [&] (double f) { return (float) (b.getX() + b.getWidth() * std::log (f / fLo) / std::log (fHi / fLo)); };
            auto yOf = [&] (float db) { return b.getY() + (hi - std::clamp (db, lo, hi)) / (hi - lo) * b.getHeight(); };

            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                g.setColour (outline);
                g.drawVerticalLine ((int) xOf (f), b.getY(), b.getBottom());
                g.setColour (textDim);
                g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f),
                            (int) xOf (f) + 2, (int) b.getBottom() - 14, 30, 12, juce::Justification::centredLeft);
            }
            for (float db : { -20.0f, -40.0f, -60.0f, -80.0f })
            {
                g.setColour (outline);
                g.drawHorizontalLine ((int) yOf (db), b.getX(), b.getRight());
                g.setColour (textDim);
                g.drawText (juce::String ((int) db) + " dB", (int) b.getX() + 3, (int) yOf (db) - 12, 50, 12, juce::Justification::centredLeft);
            }

            const double rate = analyser.getSampleRateNow();
            auto curve = [&] (const std::array<float, size / 2 + 1>& v)
            {
                juce::Path p;
                const int points = std::max (2, (int) b.getWidth());
                for (int k = 0; k < points; ++k)
                {
                    const double f0 = fLo * std::pow (fHi / fLo, (double) k / points);
                    const double f1 = fLo * std::pow (fHi / fLo, (double) (k + 1) / points);
                    int b0 = (int) std::floor (f0 / rate * size), b1 = (int) std::ceil (f1 / rate * size);
                    b0 = std::clamp (b0, 1, size / 2); b1 = std::clamp (b1, b0, size / 2);
                    float m = -200.0f;
                    for (int j = b0; j <= b1; ++j) m = std::max (m, v[(size_t) j]);
                    const float x = b.getX() + (float) k, y = yOf (m);
                    if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                return p;
            };
            g.setColour (textDim.withAlpha (0.6f));
            g.strokePath (curve (peaks), juce::PathStrokeType (1.0f));
            auto fillPath = curve (shown);
            g.setColour (accent);
            g.strokePath (fillPath, juce::PathStrokeType (1.5f));
            fillPath.lineTo (b.getRight(), b.getBottom());
            fillPath.lineTo (b.getX(), b.getBottom());
            fillPath.closeSubPath();
            g.setColour (accent.withAlpha (0.18f));
            g.fillPath (fillPath);
        }

    private:
        void timerCallback() override
        {
            analyser.copyLatest (fftData.data(), size);
            for (int i = 0; i < size; ++i) fftData[(size_t) i] *= window[(size_t) i];
            std::fill (fftData.begin() + size, fftData.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
            const float norm = 2.0f / windowSum;
            for (int k = 0; k <= size / 2; ++k)
            {
                const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] * norm, -200.0f);
                auto& s = shown[(size_t) k];
                s = db > s ? db : std::max (db, s - 1.5f);            // falls 45 dB/s
                auto& p = peaks[(size_t) k];
                p = db > p ? db : std::max (db, p - 0.15f);           // peak line falls 4.5 dB/s
            }
            repaint();
        }

        SpectrumAnalyser& analyser;
        juce::dsp::FFT fft;
        std::array<float, size> window {};
        float windowSum = 0.0f;
        std::array<float, 2 * size> fftData {};
        std::array<float, size / 2 + 1> shown {}, peaks {};
    };
}

std::unique_ptr<juce::Component> SpectrumAnalyser::createDisplay() { return std::make_unique<SpectrumDisplay> (*this); }

std::unique_ptr<BuiltInEffect> createLufsMeter (const BuiltInEffect::Info& i) { return std::make_unique<LufsMeter> (i); }
std::unique_ptr<BuiltInEffect> createSpectrum  (const BuiltInEffect::Info& i) { return std::make_unique<SpectrumAnalyser> (i); }
}
