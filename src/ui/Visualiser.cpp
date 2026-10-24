#include "ui/Visualiser.h"

namespace micvst::viz
{
namespace
{
    inline float toDb (double power) noexcept
    {
        return power > 1.0e-14 ? (float) (10.0 * std::log10 (power)) : floorDb;
    }
}

//==============================================================================
int SpectrumAnalyser::orderFor (double sampleRate)
{
    if (sampleRate <= 0.0) return 12;
    return juce::jlimit (11, 14, (int) std::round (std::log2 (sampleRate / 11.72)));
}

void SpectrumAnalyser::prepare (double sampleRate, int numPoints, double fMin, double fMax)
{
    numPoints = juce::jmax (2, numPoints);
    const int order = orderFor (sampleRate);
    if (juce::exactlyEqual (sampleRate, fs) && (1 << order) == size && numPoints == (int) freqs.size()) return;

    fs = sampleRate;
    size = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) size);
    for (int i = 0; i < size; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / size));
    scale = 2.0 / (0.5 * size);   // Hann: sum of the window = N / 2; a sine's bin = A * N / 4
    work.assign ((size_t) size * 2, 0.0f);
    power.assign ((size_t) size / 2 + 1, 0.0f);

    const double top = juce::jmin (fMax, 0.49 * fs);
    const double bottom = juce::jmin (fMin, top * 0.5);
    freqs.resize ((size_t) numPoints);
    lo.resize ((size_t) numPoints);
    hi.resize ((size_t) numPoints);
    frac.resize ((size_t) numPoints);
    levels.assign ((size_t) numPoints, floorDb);
    const double band = std::pow (2.0, 1.0 / 12.0);   // half of a sixth of an octave
    const double binsPerHz = size / fs;
    for (int p = 0; p < numPoints; ++p)
    {
        const double f = bottom * std::pow (top / bottom, (double) p / (numPoints - 1));
        freqs[(size_t) p] = f;
        const int a = juce::jlimit (1, size / 2, (int) std::ceil (f / band * binsPerHz));
        const int b = juce::jlimit (1, size / 2, (int) std::floor (f * band * binsPerHz));
        lo[(size_t) p] = a;
        hi[(size_t) p] = b;
        frac[(size_t) p] = (float) juce::jlimit (1.0, size / 2.0 - 1.0, f * binsPerHz);
    }
}

const std::vector<float>& SpectrumAnalyser::analyse (const float* mono)
{
    for (int i = 0; i < size; ++i)
    {
        const float x = mono[i];
        work[(size_t) i] = std::isfinite (x) ? x * window[(size_t) i] : 0.0f;   // a broken sample must not poison the picture
    }
    std::fill (work.begin() + size, work.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (work.data(), true);
    for (int k = 0; k <= size / 2; ++k)
    {
        const double m = work[(size_t) k] * scale;
        power[(size_t) k] = (float) (m * m);
    }
    for (size_t p = 0; p < freqs.size(); ++p)
    {
        double pw;
        if (lo[p] <= hi[p])
        {
            float m = 0.0f;
            for (int k = lo[p]; k <= hi[p]; ++k) m = juce::jmax (m, power[(size_t) k]);
            pw = m;
        }
        else
        {
            const int k0 = (int) frac[p];
            const double t = frac[p] - k0;
            pw = power[(size_t) k0] * (1.0 - t) + power[(size_t) k0 + 1] * t;
        }
        levels[p] = toDb (pw);
    }
    return levels;
}

//==============================================================================
void ChannelLevel::add (const float* x, int n, double sampleRate, double now)
{
    if (n <= 0 || sampleRate <= 0.0) return;
    float pk = 0.0f;
    double ss = 0.0;
    int counted = 0;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::abs (x[i]);
        if (! (a < 1.0e6f)) continue;   // NaN / infinite: not a level
        pk = juce::jmax (pk, a);
        ss += (double) x[i] * x[i];
        ++counted;
    }
    const double dur = n / sampleRate;
    const float pkDb = pk > 1.0e-7f ? juce::Decibels::gainToDecibels (pk) : floorDb;

    peakDb = juce::jmax (pkDb, juce::jmax (floorDb, peakDb - (float) (fallDbPerSecond * dur)));
    if (pkDb >= holdDb)
    {
        holdDb = pkDb;
        holdUntil = now + holdSeconds;
    }
    else if (now > holdUntil)
        holdDb = juce::jmax (peakDb, holdDb - (float) (fallDbPerSecond * dur));

    const double a = std::exp (-dur / rmsSeconds);
    meanSquare = meanSquare * a + (counted > 0 ? ss / counted : 0.0) * (1.0 - a);
    rmsDb = toDb (meanSquare);
    if (pk >= 1.0f) clipUntil = now + clipSeconds;
}

void ChannelLevel::fall (double dt, double now)
{
    if (dt <= 0.0) return;
    peakDb = juce::jmax (floorDb, peakDb - (float) (fallDbPerSecond * dt));
    if (now > holdUntil)
        holdDb = juce::jmax (peakDb, holdDb - (float) (fallDbPerSecond * dt));
    meanSquare *= std::exp (-dt / rmsSeconds);
    rmsDb = toDb (meanSquare);
}

//==============================================================================
void Feed::setPoints (int numPoints)
{
    points = juce::jmax (0, numPoints);
}

void Feed::reset()
{
    for (auto& s : side) s = Side();
    lastTime = -1.0;
    running = false;
}

void Feed::update (const SampleTap& in, const SampleTap& out, double sampleRate, double timeSec)
{
    const double dt = lastTime < 0.0 ? 0.0 : juce::jlimit (0.0, 0.25, timeSec - lastTime);
    lastTime = timeSec;

    if (sampleRate > 0.0 && points >= 2
        && (! juce::exactlyEqual (sampleRate, rate) || ! analyserState.isPrepared() || analyserState.numPoints() != points))
    {
        rate = sampleRate;
        analyserState.prepare (rate, points);
        for (auto& s : side) s.shown.assign ((size_t) points, floorDb);
        mono.resize ((size_t) analyserState.fftSize());
    }
    if (sampleRate <= 0.0) rate = 0.0;

    updateSide (side[input], in, timeSec, dt);
    updateSide (side[output], out, timeSec, dt);
    running = timeSec - juce::jmax (side[0].lastNewAt, side[1].lastNewAt) <= staleSeconds;
}

bool Feed::settled() const
{
    if (running) return false;
    for (auto& s : side)
    {
        for (auto v : s.shown) if (v > floorDb + 8.0f) return false;
        for (auto& c : s.ch) if (c.holdDb > -100.0f || c.rmsDb > -100.0f || c.clipUntil > lastTime) return false;
    }
    return true;
}

void Feed::updateSide (Side& s, const SampleTap& tap, double now, double dt)
{
    const uint64_t total = tap.total();
    if (total < s.readUpTo) s.readUpTo = 0;
    const uint64_t pending = total - s.readUpTo;
    if (pending > 0 && rate > 0.0) s.lastNewAt = now;
    const bool stale = rate <= 0.0 || now - s.lastNewAt > staleSeconds;

    // level meters: every new sample (a long stall: the newest that are still there)
    if (pending > 0 && rate > 0.0)
    {
        const int n = (int) juce::jmin<uint64_t> (pending, (uint64_t) SampleTap::maxRead);
        l.resize ((size_t) n);
        r.resize ((size_t) n);
        if (tap.read (total, l.data(), r.data(), n))
        {
            s.ch[0].add (l.data(), n, rate, now);
            s.ch[1].add (r.data(), n, rate, now);
        }
        s.readUpTo = total;
    }
    else if (stale)
    {
        s.ch[0].fall (dt, now);
        s.ch[1].fall (dt, now);
    }

    // spectrum: analysed in fixed steps of audio time (the FFT windows of successive steps overlap:
    // smooth), each step smoothed with the step's own duration - independent of the FPS
    if (! analyserState.isPrepared() || s.shown.size() != (size_t) analyserState.numPoints()) return;
    const int fftN = analyserState.fftSize();
    if (stale)
    {
        for (auto& v : s.shown) v = approach (v, floorDb, dt, riseSeconds, fallSeconds);
        s.analysedUpTo = total;
        return;
    }
    const auto hop = (uint64_t) juce::jmax (1, juce::roundToInt (rate * stepSeconds));
    if (s.analysedUpTo > total || total - s.analysedUpTo > hop * (uint64_t) maxStepsPerFrame || s.analysedUpTo % hop != 0)
    {
        // first frame, far behind, or another rate: the newest step only, on the fixed grid of
        // audio positions (multiples of the step) - the same positions whatever the FPS
        const uint64_t grid = (total / hop) * hop;
        s.analysedUpTo = grid >= hop ? grid - hop : 0;
    }
    const double stepDt = (double) hop / rate;
    while (s.analysedUpTo + hop <= total)
    {
        s.analysedUpTo += hop;
        const uint64_t end = s.analysedUpTo;
        if (end < (uint64_t) fftN) continue;   // the first moments after a start
        l.resize ((size_t) fftN);
        r.resize ((size_t) fftN);
        if (! tap.read (end, l.data(), r.data(), fftN)) continue;   // (overwritten meanwhile: skip this step)
        for (int i = 0; i < fftN; ++i) mono[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        const auto& lv = analyserState.analyse (mono.data());
        const size_t np = lv.size();
        for (size_t p = 0; p < np; ++p)
        {
            // a light smoothing across neighbouring points: no single-point spikes, no jagged top
            const float a = lv[p > 0 ? p - 1 : p], b = lv[p], c = lv[p + 1 < np ? p + 1 : p];
            const float target = 0.25f * a + 0.5f * b + 0.25f * c;
            s.shown[p] = approach (s.shown[p], target, stepDt, riseSeconds, fallSeconds);
        }
    }
}
}
