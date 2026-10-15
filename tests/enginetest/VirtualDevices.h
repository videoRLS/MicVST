#pragma once
// Virtual audio devices with WASAPI-like timing for the engine test (tests/EngineTestMain.cpp),
// driven by real threads: Windows - MMCSS "Pro Audio" HIGH like MicVST's patched WASAPI device
// threads; Linux - SCHED_FIFO 80.
//
// "VMic" (input): packet k (one device period) is complete at t0 + (k+1) * period, where the
// period follows the mic's own clock (config().micDriftPpm). The thread runs JUCE's input-only
// WASAPI loop (with the MicVST patch): between callbacks it moves packets from the capture
// buffer (2 whole packets, shared mode ~2.2 periods) into a reservoir and calls back once per
// packet; a packet that arrives while the capture buffer is full is LOST (one xrun per episode).
// The samples come from config().signal (channel, sample index on the mic clock).
//
// "VCable" (output): render buffer of 2.2 periods; the thread pulls a block whenever there is
// room. Late by more than the buffer lasts -> the device plays silence (counted). Everything the
// cable plays is recorded (both channels) on the cable's timeline: recorded sample m is played at
// outputStartTime + m / cableRate - one timeline across device restarts.
#include <juce_audio_devices/juce_audio_devices.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <functional>
#include <vector>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <avrt.h>
 #pragma comment (lib, "avrt.lib")
 #ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
  #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
 #endif
#else
 #include <pthread.h>
 #include <sched.h>
 #include <time.h>
#endif

namespace vdev
{
   #if JUCE_WINDOWS
    inline double now()
    {
        static const double perSecond = [] { LARGE_INTEGER f; QueryPerformanceFrequency (&f); return (double) f.QuadPart; }();
        LARGE_INTEGER c; QueryPerformanceCounter (&c);
        return (double) c.QuadPart / perSecond;
    }

    inline void sleepUntil (double t)
    {
        // high-resolution waitable timer (Windows 10 1803+): ~0.5 ms precision without timeBeginPeriod
        thread_local HANDLE timer = CreateWaitableTimerExW (nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        const double d = t - now();
        if (d <= 0.0)
            return;
        if (timer != nullptr)
        {
            LARGE_INTEGER due; due.QuadPart = -(LONGLONG) (d * 1.0e7);
            if (SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE))
            {
                WaitForSingleObject (timer, INFINITE);
                return;
            }
        }
        Sleep ((DWORD) std::ceil (d * 1000.0));
    }

    // Realtime class of a device thread: >= 80 like the patched JUCE WASAPI threads (MMCSS
    // "Pro Audio" HIGH), otherwise "Pro Audio" NORMAL (MicVST's plug-in thread).
    inline void setFifo (int prio)
    {
        DWORD index = 0;
        if (HANDLE h = AvSetMmThreadCharacteristicsW (L"Pro Audio", &index))
            AvSetMmThreadPriority (h, prio >= 80 ? AVRT_PRIORITY_HIGH : AVRT_PRIORITY_NORMAL);
    }
   #else
    inline double now()
    {
        timespec ts; clock_gettime (CLOCK_MONOTONIC, &ts);
        return (double) ts.tv_sec + 1.0e-9 * (double) ts.tv_nsec;
    }

    inline void sleepUntil (double t)
    {
        timespec ts;
        ts.tv_sec = (time_t) std::floor (t);
        ts.tv_nsec = (long) ((t - std::floor (t)) * 1.0e9);
        while (clock_nanosleep (CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
    }

    inline void setFifo (int prio)
    {
        if (std::getenv ("ETEST_NO_RT") != nullptr) return;   // sanitizer runs: a slowed-down FIFO thread could starve the machine
        sched_param p {}; p.sched_priority = prio;
        pthread_setschedparam (pthread_self(), SCHED_FIFO, &p);
    }
   #endif

    struct Config
    {
        double micRate = 48000.0, cableRate = 48000.0;
        int    micChannels = 2;            // 1 = mono microphone
        double micDriftPpm = 0.0;          // mic clock = nominal * (1 + ppm)
        juce::Array<int> bufferSizes { 128, 256, 480, 512, 960, 1024, 2048 };
        int    defaultBuffer = 480;
        std::function<float (int channel, int64_t sample)> signal =
            [] (int, int64_t n) { return (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 997.0 / 48000.0 * (double) n)); };
    };
    inline Config& config() { static Config c; return c; }

    struct Recorder
    {
        std::vector<float> left, right;
        std::atomic<int> deviceGaps { 0 };
        std::atomic<double> outputStart { 0.0 }, micStart { 0.0 };   // time of output sample 0 / mic sample 0
        std::atomic<double> micPeriodSeconds { 0.01 };
        // Where the cable played silence because its own thread was late (a stall of the test
        // machine, not MicVST): first recorded sample and length. Written by the cable thread only,
        // read after it stopped.
        std::vector<std::pair<int64_t, int64_t>> gapEvents;   // pre-sized; gapCount entries valid
        std::atomic<int> gapCount { 0 };
        // Long runs: when set, the recording is handed over block by block (on the cable thread)
        // instead of being stored; 'streamed' counts the samples (= the cable timeline).
        std::function<void (const float*, const float*, int)> sink;
        int64_t streamed = 0;
        int64_t size() const { return sink ? streamed : (int64_t) left.size(); }
        void append (const float* l, const float* r, int n)
        {
            if (sink) { sink (l, r, n); streamed += n; return; }
            for (int i = 0; i < n && left.size() < left.capacity(); ++i) { left.push_back (l[i]); right.push_back (r[i]); }
            available.store ((int64_t) left.size(), std::memory_order_release);
        }
        // Samples that may be read while the cable still records (the vectors never reallocate:
        // reserved up front) - left.size() itself must not be read concurrently.
        std::atomic<int64_t> available { 0 };
        void appendSilence (int64_t n)
        {
            static const std::vector<float> zeros (4096, 0.0f);
            while (n > 0)
            {
                const int k = (int) std::min<int64_t> (n, 4096);
                append (zeros.data(), zeros.data(), k);
                n -= k;
            }
        }
        void reset (size_t frames)
        {
            left.clear(); right.clear();
            if (! sink) { left.reserve (frames); right.reserve (frames); }
            gapEvents.assign (65536, {}); gapCount = 0;
            deviceGaps = 0; outputStart = 0.0; micStart = 0.0; streamed = 0; available = 0;
        }
    };
    inline Recorder& recorder() { static Recorder r; return r; }

    // Device presence (unplugging): absent devices vanish from the device list and their open
    // instances stop calling back (like a WASAPI stream invalidated by an unplug). "VMic2" (a
    // built-in microphone, 300 Hz tone) and "VSpeakers" (records only its peak) exist to see
    // whether the engine falls back to other devices.
    struct Presence
    {
        std::atomic<bool> mic { true }, cable { true };
        std::atomic<bool> micFrozen { false }, cableFrozen { false };   // listed, open, but no callbacks (hung driver)
        std::atomic<float> speakerPeak { 0.0f };
        std::atomic<int64_t> speakerSamples { 0 };
    };
    inline Presence& presence() { static Presence p; return p; }

    struct Stats
    {
        std::atomic<int> captureXruns { 0 }, lostPackets { 0 };
        std::atomic<int> maxCallbackUs { 0 }, captureLateWakes { 0 };
        // First mic sample of every packet the capture buffer had no room for (lost before MicVST
        // could read it: a stall of the test machine). Written by the mic thread only.
        std::vector<int64_t> lostEvents;   // pre-sized; lostCount entries valid
        std::atomic<int> lostCount { 0 };
        void reset() { captureXruns = 0; lostPackets = 0; maxCallbackUs = 0; captureLateWakes = 0; lostEvents.assign (65536, 0); lostCount = 0; }
    };
    inline Stats& stats() { static Stats s; return s; }

    class VirtualDevice final : public juce::AudioIODevice, private juce::Thread
    {
    public:
        VirtualDevice (const juce::String& name, bool isInput)
            : juce::AudioIODevice (name, "Virtual"), juce::Thread (name), input (isInput) {}
        ~VirtualDevice() override { close(); }

        juce::StringArray getOutputChannelNames() override { return input ? juce::StringArray() : juce::StringArray { "L", "R" }; }
        juce::StringArray getInputChannelNames() override
        {
            if (! input) return {};
            return config().micChannels == 1 ? juce::StringArray { "Mic" } : juce::StringArray { "L", "R" };
        }
        juce::Array<double> getAvailableSampleRates() override { return { input ? config().micRate : config().cableRate }; }
        juce::Array<int> getAvailableBufferSizes() override { return config().bufferSizes; }
        int getDefaultBufferSize() override { return config().defaultBuffer; }

        juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double sr, int bs) override
        {
            close();
            activeIns  = input ? ins : juce::BigInteger();
            if (input) activeIns.setRange (config().micChannels, 32, false);
            activeOuts = input ? juce::BigInteger() : outs;
            sampleRate = input ? config().micRate : config().cableRate;   // the device decides
            juce::ignoreUnused (sr);
            block = config().bufferSizes.contains (bs) ? bs : config().defaultBuffer;
            opened = true;
            return {};
        }
        void close() override { stop(); opened = false; }
        bool isOpen() override { return opened; }

        void start (juce::AudioIODeviceCallback* cb) override
        {
            if (! opened || cb == nullptr || playing) return;
            cb->audioDeviceAboutToStart (this);
            { const juce::ScopedLock sl (lock); callback = cb; }
            playing = true;
            startThread (juce::Thread::Priority::highest);
        }
        void stop() override
        {
            if (! playing) return;
            signalThreadShouldExit();
            stopThread (4000);
            juce::AudioIODeviceCallback* old = nullptr;
            { const juce::ScopedLock sl (lock); old = callback; callback = nullptr; }
            playing = false;
            if (old != nullptr) old->audioDeviceStopped();
        }
        bool isPlaying() override { return playing; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return block; }
        double getCurrentSampleRate() override { return sampleRate; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
        juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
        // Like WASAPI: capture latency ~ one period, render latency ~ the render buffer.
        int getOutputLatencyInSamples() override { return input ? 0 : (int) std::lround (2.2 * block); }
        int getInputLatencyInSamples() override  { return input ? block : 0; }
        int getXRunCount() const noexcept override { return input ? stats().captureXruns.load() : 0; }

    private:
        void run() override
        {
            setFifo (80);
            if (input) runInput(); else runOutput();
        }

        bool present() const
        {
            if (getName() == "VMic")   return presence().mic.load() && ! presence().micFrozen.load();
            if (getName() == "VCable") return presence().cable.load() && ! presence().cableFrozen.load();
            return true;
        }

        void callBack (const float* const* ins, int numIns, float* const* outs, int numOuts)
        {
            const double t = now();
            {
                const juce::ScopedLock sl (lock);
                if (callback != nullptr)
                    callback->audioDeviceIOCallbackWithContext (ins, numIns, outs, numOuts, block, {});
            }
            const int us = (int) ((now() - t) * 1.0e6);
            int prev = stats().maxCallbackUs.load();
            while (us > prev && ! stats().maxCallbackUs.compare_exchange_weak (prev, us)) {}
        }

        void runInput()
        {
            const auto& cfg = config();
            const double period = block / (sampleRate * (1.0 + cfg.micDriftPpm * 1.0e-6));
            const int capPackets = 2;
            int64_t r = 1; while (r < (int64_t) std::lround (2.2 * block) + block) r <<= 1;
            const int reservoirPackets = (int) (r / block);
            const double t0 = now() + 0.02;
            recorder().micStart = t0;
            recorder().micPeriodSeconds = period;
            int64_t nextArrival = 0;
            bool previousLost = false;
            std::deque<int64_t> wasapi, reservoir;
            const int nch = cfg.micChannels;
            std::vector<std::vector<float>> data ((size_t) nch, std::vector<float> ((size_t) block));
            std::vector<const float*> ptrs ((size_t) nch);

            while (! threadShouldExit())
            {
                if (reservoir.empty())
                {
                    const double due = t0 + (double) (nextArrival + 1) * period;
                    sleepUntil (due);
                    if (now() - due > 0.015) ++stats().captureLateWakes;
                }

                const double t = now() - t0;
                while ((double) (nextArrival + 1) * period <= t)
                {
                    if ((int) wasapi.size() < capPackets) { wasapi.push_back (nextArrival); previousLost = false; }
                    else
                    {
                        ++stats().lostPackets; if (! previousLost) ++stats().captureXruns; previousLost = true;
                        const int n = stats().lostCount.load (std::memory_order_relaxed);
                        if (n < (int) stats().lostEvents.size()) { stats().lostEvents[(size_t) n] = nextArrival * block; stats().lostCount.store (n + 1, std::memory_order_release); }
                    }
                    ++nextArrival;
                }
                while (! wasapi.empty() && (int) reservoir.size() < reservoirPackets)
                {
                    reservoir.push_back (wasapi.front());
                    wasapi.pop_front();
                }
                if (reservoir.empty())
                    continue;

                const int64_t k = reservoir.front();
                reservoir.pop_front();
                if (! present()) { reservoir.clear(); wasapi.clear(); continue; }   // unplugged: nothing arrives
                const bool builtIn = getName() == "VMic2";
                for (int c = 0; c < nch; ++c)
                {
                    for (int i = 0; i < block; ++i)
                        data[(size_t) c][(size_t) i] = builtIn ? (float) (0.3 * std::sin (juce::MathConstants<double>::twoPi * 300.0 / sampleRate * (double) (k * block + i)))
                                                               : cfg.signal (c, k * block + i);
                    ptrs[(size_t) c] = data[(size_t) c].data();
                }
                callBack (ptrs.data(), nch, nullptr, 0);
            }
        }

        void runSpeakers()
        {
            const double period = block / sampleRate;
            std::vector<float> l ((size_t) block), rr ((size_t) block);
            double next = now();
            while (! threadShouldExit())
            {
                next += period;
                sleepUntil (next);
                float* outs[] = { l.data(), rr.data() };
                callBack (nullptr, 0, outs, 2);
                float pk = 0; for (auto v : l) pk = std::max (pk, std::abs (v));
                if (pk > presence().speakerPeak.load()) presence().speakerPeak = pk;
                if (pk > 1.0e-4f) presence().speakerSamples += block;
            }
        }

        void runOutput()
        {
            if (getName() == "VSpeakers") { runSpeakers(); return; }
            const double period = block / sampleRate;
            const auto renderCap = (int64_t) std::lround (2.2 * block);
            const double t0 = now() + 0.02;
            auto& rec = recorder();
            // One timeline for the whole run, also across device restarts: the recording index is
            // the cable's play position. A restarted device first plays its pre-filled silence.
            if (rec.outputStart.load() == 0.0)
                rec.outputStart = t0 - (double) renderCap / sampleRate;   // pre-filled silence plays first
            const auto played = [&] { return (int64_t) std::floor ((now() - rec.outputStart.load()) * sampleRate); };
            int64_t written = (int64_t) std::floor ((t0 - rec.outputStart.load()) * sampleRate) + renderCap;
            std::vector<float> l ((size_t) block), rr ((size_t) block);
            if (rec.size() < written) rec.appendSilence (written - rec.size());
            written = rec.size();
            while (! threadShouldExit())
            {
                const int64_t needPlayed = written + block - renderCap;
                if (needPlayed > played())
                {
                    sleepUntil (rec.outputStart.load() + (double) needPlayed / sampleRate);
                    if (threadShouldExit()) break;
                }

                float* outs[] = { l.data(), rr.data() };
                if (present())
                    callBack (nullptr, 0, outs, 2);
                else
                {
                    std::fill (l.begin(), l.end(), 0.0f); std::fill (rr.begin(), rr.end(), 0.0f);   // unplugged: plays nothing
                }

                const int64_t p = played();
                if (p > written)   // late by more than the render buffer: silence was played
                {
                    ++rec.deviceGaps;
                    const int n = rec.gapCount.load (std::memory_order_relaxed);
                    if (n < (int) rec.gapEvents.size()) { rec.gapEvents[(size_t) n] = { written, p - written }; rec.gapCount.store (n + 1, std::memory_order_release); }
                    rec.appendSilence (p - written);
                    written = p;
                }
                rec.append (l.data(), rr.data(), block);
                written += block;
            }
            // Stopped: what is still in the render buffer is never played (like IAudioClient::Stop +
            // Reset) - the listener hears the signal end at the current play position.
            const auto p = played();
            if (! rec.sink && p >= 0 && (size_t) p < rec.left.size()) { rec.left.resize ((size_t) p); rec.right.resize ((size_t) p); }
            juce::ignoreUnused (period);
        }

        const bool input;
        bool opened = false, playing = false;
        juce::BigInteger activeIns, activeOuts;
        double sampleRate = 48000.0;
        int block = 480;
        juce::CriticalSection lock;
        juce::AudioIODeviceCallback* callback = nullptr;
    };

    class VirtualType;
    inline std::vector<VirtualType*>& types() { static std::vector<VirtualType*> t; return t; }

    class VirtualType final : public juce::AudioIODeviceType
    {
    public:
        VirtualType() : juce::AudioIODeviceType ("Virtual") { types().push_back (this); }
        ~VirtualType() override { auto& t = types(); t.erase (std::remove (t.begin(), t.end(), this), t.end()); }
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool wantInputNames) const override
        {
            juce::StringArray names;
            if (wantInputNames) { if (presence().mic) names.add ("VMic"); names.add ("VMic2"); }
            else                { if (presence().cable) names.add ("VCable"); names.add ("VSpeakers"); }
            return names;
        }
        void listChanged() { callDeviceChangeListeners(); }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override
        {
            return d == nullptr ? -1 : getDeviceNames (asInput).indexOf (d->getName());
        }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice (const juce::String& outputName, const juce::String& inputName) override
        {
            if (outputName.isEmpty() && getDeviceNames (true).contains (inputName))  return new VirtualDevice (inputName, true);
            if (inputName.isEmpty() && getDeviceNames (false).contains (outputName)) return new VirtualDevice (outputName, false);
            return nullptr;
        }
    };

    // Message thread: plug or unplug the microphone / the cable and notify the device managers.
    inline void setPresent (bool mic, bool cable)
    {
        presence().mic = mic;
        presence().cable = cable;
        for (auto* t : types()) t->listChanged();
    }
}
