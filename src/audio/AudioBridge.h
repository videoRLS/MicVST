#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// Lock-free bridge between two independently clocked audio devices.
//
//   microphone device thread --push()--> [ring buffer] --pull()--> virtual-cable device thread
//
// Why: a microphone and a virtual cable never share a clock. MicVST <= 1.1.1 ran both devices
// from ONE thread (JUCE's combined WASAPI device): it zero-pads the input whenever the microphone
// is a few samples behind and has no drift compensation, so clock drift produced periodic clicks
// and every scheduling stall produced an output gap PLUS a zero-padded input block. Here each
// device runs on its own thread and the two are connected by:
//
//   * a single-producer/single-consumer ring buffer (no locks, no allocation on the audio threads),
//   * a windowed-sinc resampler (THD+N < -105 dB) with a slowly varying ratio that absorbs clock
//     drift and converts between different device sample rates,
//   * a PI controller on the time-continuous fill level (timestamps interpolate between blocks),
//     which keeps a configurable safety margin (the "safety buffer") in front of the output,
//   * graceful handling of starvation: fade-out -> silence -> re-prime -> fade-in instead of
//     clicks, plus an optional automatic safety-buffer increase after each underrun.
//
// Threading contract:
//   push()                          - producer (input device callback) only
//   pull()                          - consumer (output device callback) only
//   setInputRate()/setOutputRate()  - non-realtime; never blocks the audio threads (they skip a
//                                     block while a reconfiguration is in progress)
//   setConsumerActive(), setSafety(), getStats() - any thread
// Timestamps (seconds) must come from one monotonic clock for both sides.
class AudioBridge
{
public:
    static constexpr int maxChannels    = 2;
    static constexpr int ringCapacity   = 1 << 16;   // frames (~1.36 s at 48 kHz)
    static constexpr int maxBlockFrames = 8192;      // largest block handled in one step

    AudioBridge();

    //==============================================================================
    // Configuration
    void setInputRate  (double hz);   // the producer's sample rate (input device / plug-in graph)
    void setOutputRate (double hz);   // the consumer's sample rate (output device)

    // Output side running or not. While inactive, push() is a cheap no-op (nothing is buffered).
    void setConsumerActive (bool isActive) noexcept;

    // Safety buffer = minimum margin of buffered audio kept in front of the output (ms).
    // autoMode: start at 'ms' and raise by autoStepMs after every underrun (up to autoMaxMs).
    void setSafety (double ms, bool autoMode) noexcept;
    static constexpr double autoStartMs = 10.0, autoStepMs = 5.0, autoMaxMs = 80.0, minSafetyMs = 2.0;

    //==============================================================================
    // Realtime
    void push (const float* const* data, int numChannels, int numFrames, double timeSeconds) noexcept;
    void pull (float* const* out, int numChannels, int numFrames, double timeSeconds) noexcept;

    //==============================================================================
    struct Stats
    {
        uint64_t underruns = 0;        // output ran dry (gap, faded out/in)
        uint64_t droppedBlocks = 0;    // input blocks dropped because the ring was full
        uint64_t resyncs = 0;          // latency trimmed back to the target (crossfaded skip)
        double   safetyMs = 0.0;       // current safety margin target
        double   bufferedMs = 0.0;     // audio queued in the ring (mean, time-continuous)
        double   correctionPpm = 0.0;  // current speed correction of the resampler
        double   driftPpm = 0.0;       // integrated (learned) clock drift
        bool     running = false;      // false = priming / starved / no input
    };
    Stats getStats() const noexcept;

    // Resampler filter half-length in input samples (group delay of the bridge resampler).
    int getResamplerDelayFrames() const noexcept { return halfTaps.load (std::memory_order_relaxed); }

    // For unit tests: renders numFrames output samples from a contiguous mono source with the
    // current filter (not used by the realtime paths). Returns the end position.
    double testResampleMono (const float* src, int srcLen, double startPos, double step,
                             float* dst, int numFrames) const noexcept;

private:
    //==============================================================================
    // Mirrored ring: every window of <= ringCapacity frames is contiguous in memory.
    std::array<std::vector<float>, maxChannels> ring;
    std::atomic<int64_t> writeIndex { 0 };     // producer-owned
    std::atomic<int64_t> releaseIndex { 0 };   // consumer-owned: frames before this may be overwritten
    std::atomic<int64_t> discontinuityIndex { -1 };   // producer: last input drop (audio jumps here)

    // Last push (frames written + timestamp), published with a seqlock so the consumer always
    // reads a consistent pair.
    std::atomic<uint32_t> pushSeq { 0 };
    std::atomic<int64_t>  pushEndIndex { 0 };
    std::atomic<double>   pushTime { 0.0 };
    std::atomic<int>      lastPushFrames { 480 };
    bool readLastPush (int64_t& endIndex, double& time) const noexcept;

    // Resampler (polyphase Kaiser-windowed sinc, linear interpolation between phases)
    static constexpr int phases = 256;
    static constexpr int maxHalfTaps = 128;
    std::vector<float> table;                  // (phases + 1) rows x (2 * halfTaps) taps
    std::atomic<int> halfTaps { 32 };
    void designFilter (double cutoff, int newHalfTaps);
    void updateResamplerConfig();

    // Rates / configuration
    std::atomic<double> inputRate { 48000.0 }, outputRate { 48000.0 };
    std::atomic<bool>   consumerActive { false };
    std::atomic<double> safetyMsSetting { autoStartMs };
    std::atomic<bool>   autoSafety { true };
    std::atomic<double> autoSafetyMs { autoStartMs };
    std::atomic<bool>   restartRequested { true };

    // Reconfiguration gate: push() and pull() may run at the same time (they never exclude each
    // other); a reconfiguration waits until neither is inside and keeps them out meanwhile
    // (they then skip the block instead of waiting).
    struct RealtimeGate
    {
        bool tryEnter() noexcept;
        void exit() noexcept;
        void lockForConfig() noexcept;
        void unlockForConfig() noexcept;
    private:
        std::atomic<bool> writer { false };
        std::atomic<int>  users { 0 };
        juce::SpinLock    writerMutex;
    };
    struct RealtimeScope
    {
        explicit RealtimeScope (RealtimeGate& g) noexcept : gate (g), entered (g.tryEnter()) {}
        ~RealtimeScope() noexcept { if (entered) gate.exit(); }
        RealtimeGate& gate;
        const bool entered;
    };
    RealtimeGate gate;

    // Consumer state (consumer thread only)
    enum class State { priming, running };
    State   state = State::priming;
    int64_t readIndex = 0;          // integer part of the read position
    double  frac = 0.0;             // fractional part, [0, 1)
    double  integral = 0.0;         // PI integrator = learned drift (kept across underruns)
    double  correction = 0.0;       // last applied speed correction
    double  levelEma = 0.0;         // smoothed time-continuous fill error (frames)
    bool    levelEmaValid = false;
    int     excessCount = 0;        // consecutive pulls with a large excess (resync trigger)
    double  sinceStartSeconds = 0.0; // running time since the last (re-)prime or trim
    int     fadeInRemaining = 0;
    std::array<std::vector<float>, maxChannels> scratchA, scratchB;   // preallocated

    void pullBlock (float* const* out, int numOutChannels, int numFrames, double timeSeconds) noexcept;
    int64_t framesNeeded (double startFrac, double step, int numOut) const noexcept;
    void    render (float* const* dst, int numCh, int numOut, int64_t baseIndex, double startFrac,
                    double step) const noexcept;
    double  currentSafetyMs() const noexcept;
    void    recordUnderrun() noexcept;

    // Stats (written by the RT threads, read anywhere)
    std::atomic<uint64_t> underruns { 0 }, droppedBlocks { 0 }, resyncs { 0 };
    std::atomic<double>   bufferedFrames { 0.0 }, correctionAtomic { 0.0 }, driftAtomic { 0.0 };
    std::atomic<bool>     runningAtomic { false };

    JUCE_DECLARE_NON_COPYABLE (AudioBridge)
};
