#pragma once
#include <juce_dsp/juce_dsp.h>
#include "audio/Metering.h"
#include <array>
#include <vector>

// The analysis behind the main window's spectrum and level meters - interface only. It reads the
// meters' read-only sample copies (SampleTap) on the message thread, once per drawn frame; the
// audio engine never waits for it and never sees it.
//
// Everything moves in real time, not per frame: the same curves at 10, 30 or 60 FPS, and an
// uneven frame (a busy message thread, a 144 Hz screen) moves the picture by exactly the time
// that passed - no jumps, no speed-ups.
namespace micvst::viz
{
    // exponential approach toward a target, rise and fall with their own time constants (s)
    inline float approach (float current, float target, double dt, double tauUp, double tauDown) noexcept
    {
        const double tau = target > current ? tauUp : tauDown;
        if (tau <= 0.0 || dt <= 0.0) return dt <= 0.0 ? current : target;
        return (float) (target + (current - target) * std::exp (-dt / tau));
    }

    constexpr float floorDb = -140.0f;

    // Spectrum of the newest samples: Hann window, FFT of about 12 Hz resolution, then one value per
    // point of a logarithmic frequency axis - the loudest bin within a sixth of an octave around
    // the point (between bins where the band is narrower than a bin). Calibrated: a sine of
    // amplitude A reads 20 log10 A dBFS (within the window's 1.4 dB scalloping).
    class SpectrumAnalyser
    {
    public:
        static int orderFor (double sampleRate);   // 44.1 / 48 kHz: 4096, 96: 8192, 192: 16384
        void prepare (double sampleRate, int numPoints, double fMin = 20.0, double fMax = 20000.0);
        bool isPrepared() const noexcept       { return size > 0; }
        int fftSize() const noexcept           { return size; }
        double rate() const noexcept           { return fs; }
        int numPoints() const noexcept         { return (int) freqs.size(); }
        double frequencyOf (int point) const   { return freqs[(size_t) point]; }
        // exactly fftSize() mono samples -> dBFS per point
        const std::vector<float>& analyse (const float* mono);

    private:
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window, work, power, levels, frac;
        std::vector<double> freqs;
        std::vector<int> lo, hi;                 // bins of a point's band; lo > hi: interpolate at frac
        double fs = 0.0, scale = 1.0;
        int size = 0;
    };

    // One channel of a level meter (dBFS), in real time:
    //   peak  rises at once, falls 20 dB/s
    //   hold  the highest peak, held 1.5 s, then falls 20 dB/s
    //   rms   300 ms energy average
    //   clip  a sample at or above full scale (it is cut in the cable's 24-bit format), shown 3 s
    struct ChannelLevel
    {
        float peakDb = floorDb, holdDb = floorDb, rmsDb = floorDb;
        double meanSquare = 0.0, holdUntil = 0.0, clipUntil = 0.0;
        void add (const float* x, int n, double sampleRate, double now);   // new samples
        void fall (double dt, double now);                                  // time without samples
        static constexpr double fallDbPerSecond = 20.0, holdSeconds = 1.5, clipSeconds = 3.0, rmsSeconds = 0.3;
    };

    // What the main window draws: input and output (0 / 1), the spectrum smoothed per point and the
    // left / right levels. update() once per drawn frame.
    class Feed
    {
    public:
        enum { input = 0, output = 1 };
        void setPoints (int numPoints);                       // the spectrum's width in points
        void update (const SampleTap& in, const SampleTap& out, double sampleRate, double timeSec);
        void reset();

        const std::vector<float>& spectrum (int which) const  { return side[(size_t) which].shown; }
        const ChannelLevel& level (int which, int channel) const { return side[(size_t) which].ch[(size_t) channel]; }
        const SpectrumAnalyser& analyser() const              { return analyserState; }
        double frequencyOf (int point) const                  { return analyserState.frequencyOf (point); }
        int numPoints() const                                 { return analyserState.numPoints(); }
        bool hasSignal() const                                { return running; }
        double now() const                                    { return lastTime; }   // the last update's time
        // nothing moves any more: no signal, the spectrum at the bottom, the meters down (no redraw needed)
        bool settled() const;

        // spectrum motion: rise / fall time constants (s). The spectrum is analysed in fixed steps
        // of audio time (1/60 s), however many frames are drawn: at 10 FPS a frame takes six steps,
        // so the curve is the same at any FPS and frames only decide how often it is shown.
        static constexpr double riseSeconds = 0.035, fallSeconds = 0.28, stepSeconds = 1.0 / 60.0;
        static constexpr int maxStepsPerFrame = 15;   // further behind (a stall): jump to the newest
        // no new samples for this long: the engine is stopped, everything falls to silence
        static constexpr double staleSeconds = 0.12;

    private:
        struct Side
        {
            uint64_t readUpTo = 0, analysedUpTo = 0;
            double lastNewAt = -1.0e9;
            std::vector<float> shown;
            std::array<ChannelLevel, 2> ch;
        };
        void updateSide (Side&, const SampleTap&, double now, double dt);

        std::array<Side, 2> side;
        SpectrumAnalyser analyserState;
        std::vector<float> l, r, mono;
        int points = 0;
        double rate = 0.0, lastTime = -1.0;
        bool running = false;
    };
}
