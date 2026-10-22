#pragma once
#include "audio/BuiltInEffects.h"
#include "audio/fx/FxDsp.h"

namespace fx
{
    // LUFS meter (ITU-R BS.1770-4, EBU R128 / Tech 3341-3342). The plug-in thread K-weights the
    // audio, forms 100 ms steps and from them the momentary (400 ms) and short-term (3 s) loudness;
    // every momentary block above -70 LUFS goes into a histogram (0.1 LU bins), from which the
    // integrated loudness (absolute + relative gate) is computed at any time - for hours of audio
    // without any growing memory. The same for the short-term values -> loudness range (LRA).
    // True peak: 8x interpolation (TruePeakDetector). The audio is not changed.
    class LufsMeter final : public BuiltInEffect
    {
    public:
        explicit LufsMeter (const Info&);

        struct Readings
        {
            float momentary = -200.0f, shortTerm = -200.0f, integrated = -200.0f, range = 0.0f;
            float maxMomentary = -200.0f, maxShortTerm = -200.0f, truePeakDb = -200.0f;
            double seconds = 0.0;
            int channels = 2;
        };
        Readings read() const;           // any thread (-200 = no value yet)
        void requestReset() noexcept { resetRequested.store (true, std::memory_order_release); }

        double integratedLufs() const;
        double loudnessRange() const;
        static void kWeighting (double rate, BiquadCoeffs& shelf, BiquadCoeffs& highPass) noexcept;

        std::unique_ptr<juce::Component> createDisplay() override;

        static constexpr double histMin = -70.0, absoluteGate = -70.0;
        static constexpr int histBins = 751;   // -70.0 ... +5.0 LUFS

    private:
        void prepareEffect() override;
        void resetEffect() override;
        void processEffect (float* const* ch, int numCh, int n) noexcept override;
        void clearResults() noexcept;
        static double loudness (double energy) noexcept;
        static int binOf (double lufs) noexcept;
        static double binEnergy (int bin) noexcept;

        static constexpr int ringSize = 30;
        std::array<Biquad, 2> pre, rlb;
        std::array<TruePeakDetector, 2> tp;
        std::array<double, 2> sumSq {};
        int subLen = 4800, subCount = 0;
        std::array<double, ringSize> ring {};
        int ringPos = 0, ringFill = 0;

        std::array<std::atomic<uint32_t>, histBins> intHist {}, stHist {};
        std::atomic<float> momentary { -200.0f }, shortTerm { -200.0f }, maxMomentary { -200.0f }, maxShortTerm { -200.0f };
        std::atomic<float> truePeak { 0.0f };
        std::atomic<double> measuredSeconds { 0.0 };
        std::atomic<bool> resetRequested { false };
    };

    // Spectrum analyser: the plug-in thread copies the audio (mean of the channels) into a ring; the
    // window takes the newest 8192 samples 30 times a second (Hann window, FFT). The audio is not changed.
    class SpectrumAnalyser final : public BuiltInEffect
    {
    public:
        explicit SpectrumAnalyser (const Info&);
        int copyLatest (float* dest, int count) const noexcept;   // message thread; returns samples available
        double getSampleRateNow() const noexcept { return getRate(); }
        std::unique_ptr<juce::Component> createDisplay() override;

        static constexpr int ringSize = 1 << 15;

    private:
        void prepareEffect() override {}
        void resetEffect() override {}
        void processEffect (float* const* ch, int numCh, int n) noexcept override;

        std::array<float, ringSize> ring {};
        std::atomic<uint64_t> written { 0 };   // samples so far (64 bit: 32 bit wrapped every 24.9 h at 48 kHz)
    };
}
