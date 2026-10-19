// MicVSTEngineTest: MicVST's real AudioEngine (device handling, InputQueue, plug-in thread,
// AudioProcessorGraph, PluginChain, AudioBridge) between virtual devices with WASAPI-like timing
// (tests/enginetest/VirtualDevices.h), with real threads - on Windows (CI, MMCSS) and Linux.
// Everything the virtual cable plays is recorded and analysed. For comparisons the same file
// also builds against the 1.2.0 engine (MICVST_NEW_ENGINE=0, outside CMake).
//
// Usage: MicVSTEngineTest <mode> ...   (modes: see main(); results on stdout, dumps and traces in
// $MICVST_TEST_OUT, default ./engine-test). VST3 tests use $MICVST_TEST_VST3 (default: the LSP
// plug-ins bundle in the standard VST3 folder).
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/AudioEngine.h"
#include "audio/GainStage.h"
#include "SpeechSynth.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <limits>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <map>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")
#else
 #include <dirent.h>
 #include <unistd.h>
 #include <sys/resource.h>
 #include <sys/syscall.h>
#endif

#ifndef MICVST_NEW_ENGINE
 #define MICVST_NEW_ENGINE 1
#endif
#if MICVST_RT_PROBE
 #include "enginetest/rtprobe.h"
#endif

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;
   #if ! JUCE_WINDOWS
    pid_t mainTid = 0;
   #endif

    void pr (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }
    bool envSet (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}).isNotEmpty(); }

    double threadCpuMs()
    {
       #if JUCE_WINDOWS
        return juce::Time::getMillisecondCounterHiRes();   // wall time (thread CPU time is too coarse there)
       #else
        timespec ts; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);
        return 1000.0 * (double) ts.tv_sec + 1.0e-6 * (double) ts.tv_nsec;
       #endif
    }

    //==============================================================================
    // Test "plug-ins"
    struct TestProcessor : juce::AudioPluginInstance
    {
        enum class Kind { gain, invert, delay, load, nan, huge, hang, thrower, slowPrepare, stall, stepGain };
        TestProcessor (Kind k, double p)
            : AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                    .withOutput ("Out", juce::AudioChannelSet::stereo())),
              kind (k), param (p)
        {
            if (kind == Kind::delay) setLatencySamples ((int) param);
        }
        const juce::String getName() const override
        {
            switch (kind)
            {
                case Kind::gain: return "TestGain"; case Kind::invert: return "TestInvert"; case Kind::delay: return "TestDelay";
                case Kind::load: return "TestLoad"; case Kind::nan: return "TestNaN"; case Kind::huge: return "TestHuge";
                case Kind::hang: return "TestHang"; case Kind::thrower: return "TestThrow"; case Kind::slowPrepare: return "TestSlowPrepare";
                case Kind::stall: return "TestStall"; case Kind::stepGain: return "TestStepGain";
            }
            return "Test";
        }
        void fillInPluginDescription (juce::PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "TestFormat"; d.fileOrIdentifier = getName(); }
        void prepareToPlay (double sr, int bs) override
        {
            blockMs = 1000.0 * bs / sr;
            blocks = 0;
            if (kind == Kind::slowPrepare) juce::Thread::sleep ((int) param);
            if (kind == Kind::delay) { line.assign ((size_t) (2 * 8192), 0.0f); lineR.assign (line.size(), 0.0f); pos = 0; }
        }
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
        {
            const int n = b.getNumSamples();
            switch (kind)
            {
                case Kind::gain:   b.applyGain ((float) param); break;
                case Kind::invert: b.applyGain (-1.0f); break;
                case Kind::delay:
                {
                    const int d = (int) param, len = (int) line.size();
                    for (int i = 0; i < n; ++i)
                    {
                        line[(size_t) pos] = b.getSample (0, i);
                        lineR[(size_t) pos] = b.getNumChannels() > 1 ? b.getSample (1, i) : 0.0f;
                        const int rd = (pos - d + len) % len;
                        b.setSample (0, i, line[(size_t) rd]);
                        if (b.getNumChannels() > 1) b.setSample (1, i, lineR[(size_t) rd]);
                        pos = (pos + 1) % len;
                    }
                    break;
                }
                case Kind::load:
                {
                    const double until = threadCpuMs() + param * blockMs;
                    volatile double sink = 0;
                    while (threadCpuMs() < until) sink = sink + 1.0;
                    break;
                }
                case Kind::nan:     // param = first bad block, then every block (an unstable filter)
                    if (blocks >= (int) param) for (int c = 0; c < b.getNumChannels(); ++c) b.setSample (c, n / 2, std::numeric_limits<float>::quiet_NaN());
                    break;
                case Kind::huge:
                    if (blocks >= (int) param) b.applyGain (1.0e30f);
                    break;
                case Kind::hang:    // param = block that hangs 1.5 s once
                    if (blocks == (int) param) juce::Thread::sleep (1500);
                    break;
                case Kind::thrower: // param = block that throws once
                    if (blocks == (int) param) { ++blocks; throw std::runtime_error ("test plug-in failure"); }
                    break;
                case Kind::slowPrepare: break;
                case Kind::stall:   // param = ms: the plug-in stalls that long once every 3 s (from block 200 on)
                    if (blocks >= 200 && (int) ((blocks - 200) * blockMs) / 3000 != (int) ((blocks - 199) * blockMs) / 3000)
                        juce::Thread::sleep ((int) param);
                    break;
                case Kind::stepGain:   // control for the Kazanç test: abrupt -30 dB <-> +12 dB every param blocks
                    b.applyGain ((blocks / juce::jmax (1, (int) param)) % 2 == 0 ? 0.0316228f : 3.9810717f);
                    break;
            }
            ++blocks;
        }
        void reset() override { std::fill (line.begin(), line.end(), 0.0f); std::fill (lineR.begin(), lineR.end(), 0.0f); }
        double getTailLengthSeconds() const override { return 0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock& m) override { m.append (&param, sizeof (param)); }
        void setStateInformation (const void* d, int s) override { if (s == (int) sizeof (param)) std::memcpy (&param, d, sizeof (param)); }

        Kind kind; double param; double blockMs = 10.0; int blocks = 0;
        std::vector<float> line, lineR; int pos = 0;
    };

    struct TestFormat final : juce::AudioPluginFormat
    {
        juce::String getName() const override { return "TestFormat"; }
        void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&, const juce::String&) override {}
        bool fileMightContainThisPluginType (const juce::String& f) override { return f.startsWith ("Test"); }
        juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
        bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
        bool doesPluginStillExist (const juce::PluginDescription&) override { return true; }
        bool canScanForPlugins() const override { return false; }
        bool isTrivialToScan() const override { return true; }
        juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
        juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
        bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }
        void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback cb) override
        {
            using K = TestProcessor::Kind;
            const auto id = d.fileOrIdentifier;
            const double p = d.version.getDoubleValue();
            if (id == "TestGain")   { cb (std::make_unique<TestProcessor> (K::gain, p > 0 ? p : 0.5), {}); return; }
            if (id == "TestInvert") { cb (std::make_unique<TestProcessor> (K::invert, 0), {}); return; }
            if (id == "TestDelay")  { cb (std::make_unique<TestProcessor> (K::delay, p > 0 ? p : 240), {}); return; }
            if (id == "TestLoad")   { cb (std::make_unique<TestProcessor> (K::load, p), {}); return; }
            if (id == "TestNaN")    { cb (std::make_unique<TestProcessor> (K::nan, p), {}); return; }
            if (id == "TestHuge")   { cb (std::make_unique<TestProcessor> (K::huge, p), {}); return; }
            if (id == "TestHang")   { cb (std::make_unique<TestProcessor> (K::hang, p), {}); return; }
            if (id == "TestThrow")  { cb (std::make_unique<TestProcessor> (K::thrower, p), {}); return; }
            if (id == "TestSlowPrepare") { cb (std::make_unique<TestProcessor> (K::slowPrepare, p), {}); return; }
            if (id == "TestStall")  { cb (std::make_unique<TestProcessor> (K::stall, p), {}); return; }
            if (id == "TestStepGain") { cb (std::make_unique<TestProcessor> (K::stepGain, p > 0 ? p : 5), {}); return; }
            cb (nullptr, "unknown test plug-in");
        }
    };

    juce::PluginDescription testDesc (const char* id, double param = 0.0)
    {
        juce::PluginDescription d;
        d.name = id; d.fileOrIdentifier = id; d.pluginFormatName = "TestFormat"; d.version = juce::String (param);
        return d;
    }

    //==============================================================================
    // Linux: every SCHED_OTHER thread except the main one -> SCHED_FIFO 70 (= MicVST's plug-in
    // thread at MMCSS "Pro Audio" NORMAL; the device threads are FIFO 80 already). Windows: the
    // engine's plug-in thread enters MMCSS itself, the device threads too (VirtualDevices.h).
    void raiseEngineThreads()
    {
       #if ! JUCE_WINDOWS
        if (envSet ("ETEST_NO_RT")) return;   // sanitizer runs (see VirtualDevices.h)
        if (DIR* d = opendir ("/proc/self/task"))
        {
            while (auto* e = readdir (d))
            {
                const int tid = atoi (e->d_name);
                if (tid <= 0 || tid == mainTid) continue;
                if (sched_getscheduler (tid) == SCHED_OTHER)
                {
                    sched_param p {}; p.sched_priority = 70;
                    sched_setscheduler (tid, SCHED_FIFO, &p);
                }
            }
            closedir (d);
        }
       #endif
    }

    juce::File outDir()
    {
        const auto env = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_OUT", {});
        auto d = env.isNotEmpty() ? juce::File (env) : juce::File::getCurrentWorkingDirectory().getChildFile ("engine-test");
        d.createDirectory();
        return d;
    }

    juce::String vst3Bundle()
    {
        if (const auto env = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_VST3", {}); env.isNotEmpty()) return env;
       #if JUCE_WINDOWS
        return "C:\\Program Files\\Common Files\\VST3\\lsp-plugins.vst3";
       #else
        return "/usr/lib/vst3/lsp-plugins.vst3";
       #endif
    }

    void pump (double seconds) { juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000.0)); }

    // every line the engine logs (the test's logger counts and keeps them)
    std::atomic<int> logLineCount { 0 };
    std::mutex logLinesMutex;
    juce::StringArray keptLogLines;

    struct EngineRun
    {
        EngineRun (int bufferSize, double seconds, double preferredRate = 48000.0)
        {
            vdev::recorder().reset ((size_t) ((seconds + 4.0) * vdev::config().cableRate));
            vdev::stats().reset();
            engine = std::make_unique<AudioEngine>();
            engine->getFormatManager().addFormat (std::make_unique<TestFormat>());
            MicVSTState st;
            st.inputDevice = "VMic";
            st.outputDevice = "VCable";
            st.bufferSize = bufferSize;
            st.sampleRate = preferredRate;
            engine->applyState (st);
            pump (0.05);
            raiseEngineThreads();
        }
        ~EngineRun() { engine.reset(); }

        bool add (const juce::PluginDescription& d)
        {
            juce::String err;
            const bool ok = engine->getChain().addPlugin (engine->getFormatManager(), d, engine->getProcessingSampleRate(),
                                                          engine->getProcessingBlockSize(), err);
            if (! ok) pr ("  add failed: " + d.name + " " + err);
           #if MICVST_NEW_ENGINE
            const AudioEngine::ChainEdit edit (*engine);
           #endif
            engine->rebuildGraph();
            return ok;
        }

        std::unique_ptr<AudioEngine> engine;
    };

    //==============================================================================
    // Analysis
    struct Fit { double amp = 0, freq = 0, residualDb = 0, dc = 0; };

    // 3-parameter least-squares sine fit at a fixed frequency (cycles/sample).
    Fit fit3 (const float* x, int n, double f)
    {
        double m[3][3] = {}, v[3] = {};
        for (int i = 0; i < n; ++i)
        {
            const double ph = twoPi * f * i;
            const double b[3] = { std::sin (ph), std::cos (ph), 1.0 };
            for (int r = 0; r < 3; ++r) { v[r] += b[r] * x[i]; for (int c = 0; c < 3; ++c) m[r][c] += b[r] * b[c]; }
        }
        for (int col = 0; col < 3; ++col)
            for (int row = col + 1; row < 3; ++row)
            {
                const double k = m[row][col] / m[col][col];
                for (int c = col; c < 3; ++c) m[row][c] -= k * m[col][c];
                v[row] -= k * v[col];
            }
        double s[3];
        for (int row = 2; row >= 0; --row) { double a = v[row]; for (int c = row + 1; c < 3; ++c) a -= m[row][c] * s[c]; s[row] = a / m[row][row]; }
        double res = 0, sig = 0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = twoPi * f * i;
            const double t = s[0] * std::sin (ph) + s[1] * std::cos (ph);
            const double e = x[i] - t - s[2];
            res += e * e; sig += t * t;
        }
        Fit r; r.amp = std::sqrt (s[0] * s[0] + s[1] * s[1]); r.freq = f; r.dc = s[2];
        r.residualDb = 10.0 * std::log10 ((res + 1e-30) / (sig + 1e-30));
        return r;
    }

    // Frequency-refined fit: the bridge resamples with a slowly varying ratio (clock drift
    // compensation), so the exact output frequency differs by a few ppm from the nominal one.
    Fit fitSine (const std::vector<float>& y, size_t from, int n, double nominalCyclesPerSample)
    {
        // Search range: the bridge changes the speed by at most ~600 ppm, and the range must stay
        // inside the main lobe of the window (+-0.45 bin): a wider range has side-lobe minima where
        // the golden section can end up (that read as -30..-80 dB "THD+N" for high tones and long
        // windows - a fit error, not the audio).
        const double span = std::max (nominalCyclesPerSample * 0.0006, std::min (nominalCyclesPerSample * 0.006, 0.45 / (double) n));
        // coarse grid first (finds the main lobe), then golden section around its best point
        Fit best = fit3 (y.data() + from, n, nominalCyclesPerSample);
        double centre = nominalCyclesPerSample;
        const double step = span / 10.0;
        for (int k = -10; k <= 10; ++k)
        {
            const auto f = fit3 (y.data() + from, n, nominalCyclesPerSample + k * step);
            if (f.residualDb < best.residualDb) { best = f; centre = nominalCyclesPerSample + k * step; }
        }
        double lo = centre - step, hi = centre + step;
        for (int it = 0; it < 40; ++it)   // golden-section on the residual
        {
            const double a = hi - (hi - lo) * 0.618, b = lo + (hi - lo) * 0.618;
            const auto fa = fit3 (y.data() + from, n, a), fb = fit3 (y.data() + from, n, b);
            if (fa.residualDb < fb.residualDb) { hi = b; if (fa.residualDb < best.residualDb) best = fa; }
            else                               { lo = a; if (fb.residualDb < best.residualDb) best = fb; }
        }
        return best;
    }

    struct Discontinuities { int events = 0, samples = 0; };

    Discontinuities clicks (const std::vector<float>& y, size_t from, size_t to, double amp, double cyclesPerSample, size_t spacing = 480,
                            std::vector<size_t>* where = nullptr)
    {
        Discontinuities d;
        const double limit = 3.0 * amp * std::pow (twoPi * cyclesPerSample, 2.0) + 2.0e-4;
        size_t last = 0;
        for (size_t i = std::max<size_t> (from, 1); i + 1 < std::min (to, y.size()); ++i)
            if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit)
            {
                if (d.samples == 0 || i - last > spacing) { ++d.events; if (where != nullptr) where->push_back (i); }
                last = i; ++d.samples;
            }
        return d;
    }

    struct Silences { int count = 0; double ms = 0; };
    Silences silences (const std::vector<float>& y, size_t from, double rate, int minRun = 24)
    {
        Silences s; int zeros = 0;
        for (size_t i = from; i < y.size(); ++i)
        {
            if (y[i] == 0.0f) ++zeros;
            else { if (zeros > minRun) { ++s.count; s.ms += 1000.0 * zeros / rate; } zeros = 0; }
        }
        return s;
    }

    size_t firstAudio (const std::vector<float>& y, float threshold = 1.0e-6f)
    {
        size_t i = 0;
        while (i < y.size() && std::abs (y[i]) < threshold) ++i;
        return i;
    }

    juce::String db (double v, int dec = 1) { return juce::String (v, dec) + " dB"; }

    long rssKb()
    {
       #if JUCE_WINDOWS
        PROCESS_MEMORY_COUNTERS pmc {};
        return GetProcessMemoryInfo (GetCurrentProcess(), &pmc, sizeof (pmc)) ? (long) (pmc.WorkingSetSize / 1024) : 0;
       #else
        long pages = 0, rss = 0;
        if (FILE* f = std::fopen ("/proc/self/statm", "r")) { if (std::fscanf (f, "%ld %ld", &pages, &rss) != 2) rss = 0; std::fclose (f); }
        return rss * (sysconf (_SC_PAGESIZE) / 1024);
       #endif
    }

    double processCpuSeconds()
    {
       #if JUCE_WINDOWS
        FILETIME c, e, k, u;
        if (! GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
        auto secs = [] (FILETIME f) { return (double) (((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime) * 1.0e-7; };
        return secs (k) + secs (u);
       #else
        rusage ru {}; getrusage (RUSAGE_SELF, &ru);
        return (double) ru.ru_utime.tv_sec + 1e-6 * (double) ru.ru_utime.tv_usec + (double) ru.ru_stime.tv_sec + 1e-6 * (double) ru.ru_stime.tv_usec;
       #endif
    }

    juce::String diagLine (AudioEngine& e)
    {
        const auto d = e.getDiagnostics();
        juce::String s;
        s << "underruns " << (int) d.bridge.underruns << ", trims " << (int) d.bridge.resyncs << ", ring drops " << (int) d.bridge.droppedBlocks
          << ", mic xruns " << d.inputDeviceXruns << " (lost packets " << vdev::stats().lostPackets.load() << ")"
         #if MICVST_NEW_ENGINE
          << ", skips " << (int) d.inputSkips << ", queue overflows " << (int) d.inputQueueOverflows
         #endif
          << ", device gaps " << vdev::recorder().deviceGaps.load()
          << ", safety " << juce::roundToInt (d.bridge.safetyMs) << " ms, drift " << juce::String (d.bridge.driftPpm, 1) << " ppm"
          << ", shown latency " << juce::String (d.latencyMs, 1) << " ms, cpu " << juce::roundToInt (d.dspLoadPercent) << " %";
        return s;
    }

    //==============================================================================
    // Quality: passthrough measurements
    void sineConfig (double freqHz, double amp, double micRate)
    {
        vdev::config().signal = [freqHz, amp, micRate] (int, int64_t n) { return (float) (amp * std::sin (twoPi * freqHz / micRate * (double) n)); };
    }

    struct SineResult { double gainDbL = 0, gainDbR = 0, thdnL = 0, thdnR = 0, thdnWorstL = 0, freqRatio = 0; int windows = 0, badWindows = 0;
                        Discontinuities clk; Silences gaps; };

    // setup: builds the chain before the measurement runs; outputGain: the chain's gain (the click
    // detector's limit scales with the output amplitude).
    SineResult measureSine (double freqHz, double amp, double seconds, int buffer, double analyseFrom = 4.0,
                            std::function<void (EngineRun&)> setup = {}, double outputGain = 1.0)
    {
        auto& cfg = vdev::config();
        sineConfig (freqHz, amp, cfg.micRate);
        SineResult r;
        {
            EngineRun run (buffer, seconds);
            if (setup) setup (run);
            pump (seconds);
            pr ("    " + diagLine (*run.engine));
        }
        const auto& L = vdev::recorder().left;
        const auto& R = vdev::recorder().right;
        const double fs = cfg.cableRate;
        const size_t from = (size_t) (analyseFrom * fs);
        const double cps = freqHz / fs;
        // Short windows (8192 samples): the bridge's drift correction varies the resampling ratio
        // by a few ppm over seconds - a slow, inaudible phase wander that a single fit over
        // seconds would count as "noise". Windows containing a dropout are skipped (counted).
        const int win = 8192;
        std::vector<double> gl, gr, tl, tr;
        r.badWindows = 0;
        for (size_t s0 = from; s0 + (size_t) win < L.size() && gl.size() < 24; s0 += (size_t) win * 2)
        {
            int zeros = 0, longest = 0;
            for (size_t i = s0; i < s0 + (size_t) win; ++i) { if (L[i] == 0.0f) longest = std::max (longest, ++zeros); else zeros = 0; }
            if (longest > 8 && amp > 0.01) { ++r.badWindows; continue; }
            const auto fl = fitSine (L, s0, win, cps), fr = fitSine (R, s0, win, cps);
            gl.push_back (juce::Decibels::gainToDecibels (fl.amp / amp, -200.0));
            gr.push_back (juce::Decibels::gainToDecibels (fr.amp / amp, -200.0));
            tl.push_back (fl.residualDb); tr.push_back (fr.residualDb);
        }
        auto median = [] (std::vector<double> v) { if (v.empty()) return 0.0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; };
        auto worst  = [] (const std::vector<double>& v) { double w = -500; for (auto x : v) w = std::max (w, x); return w; };
        r.gainDbL = median (gl); r.gainDbR = median (gr);
        r.thdnL = median (tl); r.thdnR = median (tr);
        r.thdnWorstL = worst (tl);
        r.windows = (int) gl.size();
        const size_t start = firstAudio (L) + (size_t) (0.05 * fs);
        r.clk = clicks (L, start, L.size(), amp * outputGain, cps);
        r.gaps = silences (L, start, fs);
        return r;
    }

    void quality()
    {
        auto& cfg = vdev::config();
        pr ("## Quality: passthrough (empty chain), stereo mic, 48 kHz / 48 kHz, buffer 480");
        pr ("| Signal | Gain L / R | THD+N L / R | Clicks | Gaps |");
        pr ("|---|---:|---:|---:|---:|");
        for (double amp : { 0.5, 0.1, 0.001 })
        {
            const auto r = measureSine (997.0, amp, 10.0, 480);
            pr ("| 997 Hz, " + db (juce::Decibels::gainToDecibels (amp), 0) + "FS | " + db (r.gainDbL, 4) + " / " + db (r.gainDbR, 4)
                + " | " + db (r.thdnL) + " / " + db (r.thdnR) + " (worst window " + db (r.thdnWorstL) + ", " + juce::String (r.windows)
                + " windows" + (r.badWindows > 0 ? ", " + juce::String (r.badWindows) + " with a dropout" : juce::String()) + ") | "
                + juce::String (r.clk.events) + " | " + juce::String (r.gaps.count) + " |");
        }

        pr ("");
        pr ("## Frequency response (48/48 kHz, -6 dBFS tones)");
        pr ("| Frequency | Gain L | THD+N L |");
        pr ("|---:|---:|---:|");
        for (double f : { 20.0, 50.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 19000.0, 20000.0, 21000.0, 22000.0 })
        {
            const auto r = measureSine (f, 0.5, 8.0, 480, 4.0);
            pr ("| " + juce::String (f, 0) + " Hz | " + db (r.gainDbL, 3) + " | " + db (r.thdnL) + " (worst " + db (r.thdnWorstL) + ") |");
        }

        pr ("");
        pr ("## Silence in -> output");
        {
            cfg.signal = [] (int, int64_t) { return 0.0f; };
            { EngineRun run (480, 6.0); pump (6.0); }
            float mx = 0; bool finite = true;
            for (auto v : vdev::recorder().left)  { mx = std::max (mx, std::abs (v)); finite = finite && std::isfinite (v); }
            for (auto v : vdev::recorder().right) { mx = std::max (mx, std::abs (v)); finite = finite && std::isfinite (v); }
            pr ("max |output| = " + juce::String (mx, 12) + (finite ? " (all finite)" : " (NON-FINITE VALUES!)"));
        }

        pr ("");
        pr ("## Channels");
        {
            cfg.micChannels = 1;
            const auto r = measureSine (997.0, 0.5, 8.0, 480);
            pr ("mono mic -> cable: gain L " + db (r.gainDbL, 4) + ", R " + db (r.gainDbR, 4) + ", THD+N L " + db (r.thdnL) + ", R " + db (r.thdnR)
                + ", windows " + juce::String (r.windows) + " (+" + juce::String (r.badWindows) + " with dropouts), clicks " + juce::String (r.clk.events));
            // where does the mono output go wrong? share of exact-zero samples and a few block peaks
            const auto& L = vdev::recorder().left;
            size_t zeros = 0; for (size_t i = 4 * 48000; i < L.size(); ++i) if (L[i] == 0.0f) ++zeros;
            pr ("  mono output: zero samples " + juce::String (100.0 * (double) zeros / (double) (L.size() - 4 * 48000), 1) + " %");
            cfg.micChannels = 2;
        }
        {
            cfg.signal = [] (int ch, int64_t n) { return (float) (0.4 * std::sin (twoPi * (ch == 0 ? 440.0 : 1250.0) / 48000.0 * (double) n)); };
            { EngineRun run (480, 8.0); pump (8.0); }
            const auto& L = vdev::recorder().left; const auto& R = vdev::recorder().right;
            const size_t from = 4 * 48000; const int n = 3 * 48000;
            const auto lMain = fitSine (L, from, n, 440.0 / 48000.0), lCross = fit3 (L.data() + from, n, 1250.0 / 48000.0);
            const auto rMain = fitSine (R, from, n, 1250.0 / 48000.0), rCross = fit3 (R.data() + from, n, 440.0 / 48000.0);
            pr ("stereo mic L=440 Hz, R=1250 Hz: L keeps 440 Hz at " + db (juce::Decibels::gainToDecibels (lMain.amp / 0.4), 4)
                + ", 1250 Hz leaking into L " + db (juce::Decibels::gainToDecibels (lCross.amp / 0.4, -200.0))
                + "; R keeps 1250 Hz at " + db (juce::Decibels::gainToDecibels (rMain.amp / 0.4), 4)
                + ", 440 Hz leaking into R " + db (juce::Decibels::gainToDecibels (rCross.amp / 0.4, -200.0)));
        }

        pr ("");
        pr ("## Sample-rate combinations (mic -> cable), 997 Hz and 15 kHz at -6 dBFS");
        pr ("| Mic | Cable | 997 Hz gain / THD+N | 15 kHz gain / THD+N | Clicks | Gaps |");
        pr ("|---:|---:|---:|---:|---:|---:|");
        for (auto rates : { std::pair<double, double> { 44100, 48000 }, { 48000, 44100 }, { 96000, 48000 }, { 48000, 96000 }, { 44100, 44100 } })
        {
            cfg.micRate = rates.first; cfg.cableRate = rates.second;
            const int buf = rates.first == 44100 ? 441 : rates.first == 96000 ? 960 : 480;
            cfg.bufferSizes = { buf }; cfg.defaultBuffer = buf;
            const auto a = measureSine (997.0, 0.5, 8.0, buf);
            const auto b = measureSine (15000.0, 0.5, 8.0, buf);
            pr ("| " + juce::String (rates.first, 0) + " | " + juce::String (rates.second, 0) + " | " + db (a.gainDbL, 3) + " / " + db (a.thdnL)
                + " | " + db (b.gainDbL, 3) + " / " + db (b.thdnL) + " | " + juce::String (a.clk.events + b.clk.events)
                + " | " + juce::String (a.gaps.count + b.gaps.count) + " |");
        }
        cfg.micRate = cfg.cableRate = 48000; cfg.bufferSizes = { 128, 256, 480, 512, 960, 1024, 2048 }; cfg.defaultBuffer = 480;

        pr ("");
        pr ("## Mic clock drift (60 s, 997 Hz -6 dBFS)");
        for (double ppm : { -150.0, 150.0 })
        {
            cfg.micDriftPpm = ppm;
            const auto r = measureSine (997.0, 0.5, 60.0, 480, 40.0);
            pr ("drift " + juce::String (ppm, 0) + " ppm: THD+N " + db (r.thdnL) + ", clicks " + juce::String (r.clk.events)
                + ", gaps " + juce::String (r.gaps.count));
        }
        cfg.micDriftPpm = 0;
    }

    //==============================================================================
    // Latency: impulse at a known mic sample; where does it appear on the cable timeline?
    void latency()
    {
        auto& cfg = vdev::config();
        pr ("## Latency: measured (impulse, virtual device timeline) vs. shown by MicVST");
        pr ("| Buffer | Measured | Shown | Shown - measured |");
        pr ("|---:|---:|---:|---:|");
        for (int buf : { 128, 256, 480, 1024, 2048 })
        {
            cfg.signal = [] (int, int64_t n) { return (n % 48000 == 24000) ? 0.9f : 0.0f; };   // one impulse per second
            double shown = 0;
            {
                EngineRun run (buf, 12.0);
                pump (11.0);
                shown = run.engine->getDiagnostics().latencyMs;
            }
            const auto& L = vdev::recorder().left;
            const double fs = cfg.cableRate;
            // the last impulse found: its mic sample index k*48000+24000; its cable index = peak
            int64_t peak = -1; float best = 0.2f;
            for (size_t i = (size_t) (8 * fs); i < L.size(); ++i) if (std::abs (L[i]) > best) { best = std::abs (L[i]); peak = (int64_t) i; }
            if (peak < 0) { pr ("| " + juce::String (buf) + " | no impulse found | | |"); continue; }
            const double playTime = vdev::recorder().outputStart.load() + (double) peak / fs;
            // which impulse? the one whose capture time is closest before playTime
            const double micStart = vdev::recorder().micStart.load();
            double measured = 1e9;
            for (int64_t k = 0; k < 20; ++k)
            {
                const double captureTime = micStart + (double) (k * 48000 + 24000) / cfg.micRate;
                const double l = playTime - captureTime;
                if (l > 0 && l < measured) measured = l;
            }
            pr ("| " + juce::String (buf) + " | " + juce::String (1000.0 * measured, 1) + " ms | " + juce::String (shown, 1) + " ms | "
                + juce::String (shown - 1000.0 * measured, 1) + " ms |");
        }
    }

    //==============================================================================
    // Buffer sizes: stability + latency for each device buffer
    void buffers()
    {
        auto& cfg = vdev::config();
        pr ("## Buffer sizes (30 s each, 997 Hz -6 dBFS, light plug-in load 30 %)");
        pr ("| Buffer | Clicks | Gaps | Diagnostics |");
        pr ("|---:|---:|---:|---|");
        for (int buf : { 128, 256, 480, 512, 960, 1024, 2048 })
        {
            sineConfig (997.0, 0.5, 48000);
            juce::String diag;
            {
                EngineRun run (buf, 30.0);
                run.add (testDesc ("TestLoad", 0.3));
                pump (30.0);
                diag = diagLine (*run.engine);
            }
            const auto& L = vdev::recorder().left;
            const size_t start = firstAudio (L) + 4800;
            const auto c = clicks (L, start, L.size(), 0.5, 997.0 / 48000.0);
            const auto g = silences (L, start, 48000);
            pr ("| " + juce::String (buf) + " | " + juce::String (c.events) + " | " + juce::String (g.count) + " | " + diag + " |");
        }
        juce::ignoreUnused (cfg);
    }

    //==============================================================================
    // Chain edits while audio plays: every edit must be inaudible as a click.
    void edits()
    {
        auto& cfg = vdev::config();
        sineConfig (997.0, 0.5, 48000);
        pr ("## Chain edits during playback (997 Hz -6 dBFS; one edit per second)");
        const double total = 22.0;
        std::vector<std::pair<double, juce::String>> marks;
        {
            EngineRun run (480, total);
            auto& e = *run.engine;
            run.add (testDesc ("TestGain", 0.5));
            pump (3.0);
            auto edit = [&] (const juce::String& what, std::function<void()> f)
            {
                marks.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, what });
                {
                   #if MICVST_NEW_ENGINE
                    const AudioEngine::ChainEdit ce (e);
                   #endif
                    f();
                    e.rebuildGraph();
                }
                pump (1.0);
            };
            juce::String err;
            auto addPlugin = [&] (const char* id, double p) { e.getChain().addPlugin (e.getFormatManager(), testDesc (id, p), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err); };
            edit ("bypass gain on",        [&] { e.getChain().setBypass (0, true); });
            edit ("bypass gain off",       [&] { e.getChain().setBypass (0, false); });
            edit ("add invert",            [&] { addPlugin ("TestInvert", 0); });
            edit ("remove invert",         [&] { e.getChain().removePlugin (1); });
            edit ("add delay 240 smp",     [&] { addPlugin ("TestDelay", 240); });
            edit ("bypass delay on",       [&] { e.getChain().setBypass (1, true); });
            edit ("bypass delay off",      [&] { e.getChain().setBypass (1, false); });
            edit ("move delay to front",   [&] { e.getChain().movePlugin (1, 0); });
            edit ("add mono->stereo",      [&] { e.getChain().addMonoToStereo(); });
            edit ("add stereo->mono",      [&] { e.getChain().addStereoToMono(); });
            edit ("remove stereo->mono",   [&] { e.getChain().removePlugin (3); });
            edit ("remove gain",           [&] { e.getChain().removePlugin (1); });
            edit ("remove all",            [&] { while (! e.getChain().entries().empty()) e.getChain().removePlugin (0); });
            pump (total - 3.0 - 13.0 - 1.0);
            pr ("  " + diagLine (e));
        }
        const auto& L = vdev::recorder().left;
        const double fs = 48000.0;
        const double t0 = vdev::recorder().outputStart.load();
        int totalClicks = 0;
        pr ("| Edit | Clicks within the following second (cable timeline, incl. latency) | Gap (ms) |");
        pr ("|---|---:|---:|");
        for (auto& m : marks)
        {
            const auto from = (size_t) std::max (0.0, (m.first - t0) * fs);
            const auto to   = std::min (L.size(), from + (size_t) fs);
            const auto c = clicks (L, from, to, 0.5, 997.0 / fs);
            int zeros = 0, longest = 0;
            for (size_t i = from; i < to; ++i) { if (std::abs (L[i]) < 1e-6f) longest = std::max (longest, ++zeros); else zeros = 0; }
            totalClicks += c.events;
            pr ("| " + m.second + " | " + juce::String (c.events) + " | " + juce::String (1000.0 * longest / fs, 1) + " |");
        }
        const size_t start = firstAudio (L) + 4800;
        pr ("total click events in the whole run: " + juce::String (clicks (L, start, L.size(), 0.5, 997.0 / fs).events)
            + " (in edit windows: " + juce::String (totalClicks) + ")");
        juce::ignoreUnused (cfg);
    }

    //==============================================================================
    // Real VST3 plug-ins (LSP, Linux builds of the VST3 SDK host path JUCE uses on Windows too)
    void vst3()
    {
        pr ("## VST3 plug-ins (" + vst3Bundle() + ")");
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> types;
        fmt.findAllTypesForFile (types, vst3Bundle());
        pr ("types in the bundle: " + juce::String (types.size()));

        auto find = [&] (const juce::String& needle) -> const juce::PluginDescription*
        {
            for (auto* t : types) if (t->name.equalsIgnoreCase (needle)) return t;
            for (auto* t : types) if (t->name.containsIgnoreCase (needle)) return t;
            return nullptr;
        };

        juce::StringArray wanted { "Compressor Stereo", "Parametric Equalizer x8 Stereo", "Limiter Stereo", "Gate Stereo",
                                   "Dynamic Processor Stereo", "Mixer x4 Stereo", "Graphic Equalizer x16 Stereo", "Sidechain Compressor Stereo" };
        juce::Array<juce::PluginDescription> chain;
        for (auto& w : wanted)
            if (auto* d = find (w)) chain.add (*d);
        if (chain.size() < 2)   // another bundle (CI on Windows): its first effects, one plug-in several times if it is alone
        {
            chain.clear();
            for (auto* t : types)
                if (! t->isInstrument && chain.size() < 7) chain.add (*t);
            for (int i = 0; chain.size() > 0 && chain.size() < 4; ++i) chain.add (chain[i]);
        }
        juce::String names; for (auto& d : chain) names << d.name << ", ";
        pr ("chain: " + names.dropLastCharacters (2));

        sineConfig (997.0, 0.25, 48000);
        const double total = 46.0;
        juce::String report;
        std::vector<std::pair<double, juce::String>> editMarks;
        {
            EngineRun run (480, total);
            auto& e = *run.engine;
            e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            int loaded = 0;
            for (auto& d : chain)
            {
                juce::String err;
                if (e.getChain().addPlugin (e.getFormatManager(), d, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err)) ++loaded;
                else pr ("  load failed: " + d.name + " - " + err);
            }
            {
               #if MICVST_NEW_ENGINE
                const AudioEngine::ChainEdit ce (e);
               #endif
                e.rebuildGraph();
            }
            pr ("loaded " + juce::String (loaded) + "/" + juce::String (chain.size()) + " in "
                + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + " s; graph latency "
                + juce::String (e.getGraph().getLatencySamples()) + " samples");
            for (size_t i = 0; i < e.getChain().entries().size(); ++i)
                if (auto* n = e.getGraph().getNodeForId (e.getChain().entries()[i].node))
                    pr ("  " + n->getProcessor()->getName() + ": in " + juce::String (n->getProcessor()->getTotalNumInputChannels())
                        + " / out " + juce::String (n->getProcessor()->getTotalNumOutputChannels()) + ", latency "
                        + juce::String (n->getProcessor()->getLatencySamples()) + ", params " + juce::String (n->getProcessor()->getParameters().size()));
            pump (8.0);
            pr ("  after 8 s: " + diagLine (e));

            // state round trip: save every plug-in's state, restore it into a fresh engine chain
            const auto state = e.captureState();
            int stateBytes = 0; for (auto& p : state.plugins) stateBytes += (int) p.state.getSize();
            pr ("  saved chain state: " + juce::String (state.plugins.size()) + " plug-ins, " + juce::String (stateBytes) + " bytes");

            // bypass every plug-in once, then all back; then move plug-ins around
            auto mark = [&] (const juce::String& what) { editMarks.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, what }); };
            for (int i = 0; i < (int) e.getChain().entries().size(); ++i)
            {
                mark ("bypass " + juce::String (i + 1));
                {
                   #if MICVST_NEW_ENGINE
                    const AudioEngine::ChainEdit ce (e);
                   #endif
                    e.getChain().setBypass (i, true); e.rebuildGraph(); }
                pump (0.4);
                mark ("bypass off " + juce::String (i + 1));
                {
                   #if MICVST_NEW_ENGINE
                    const AudioEngine::ChainEdit ce (e);
                   #endif
                    e.getChain().setBypass (i, false); e.rebuildGraph(); }
                pump (0.4);
            }
            for (int i = 0; i < 3; ++i)
            {
                mark ("move last to first");
                {
                   #if MICVST_NEW_ENGINE
                    const AudioEngine::ChainEdit ce (e);
                   #endif
                    e.getChain().movePlugin ((int) e.getChain().entries().size() - 1, 0); e.rebuildGraph(); }
                pump (0.4);
            }
            pr ("  after bypass round: " + diagLine (e));

            // 4 more instances of the compressor (heavier chain), then remove them
            mark ("add 4 compressors");
            for (int k = 0; k < 4 && ! chain.isEmpty(); ++k)
            {
                juce::String err;
                e.getChain().addPlugin (e.getFormatManager(), chain[0], e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
            }
            {
               #if MICVST_NEW_ENGINE
                const AudioEngine::ChainEdit ce (e);
               #endif
                e.rebuildGraph();
            }
            pump (4.0);
            pr ("  with 4 extra compressors (" + juce::String ((int) e.getChain().entries().size()) + " plug-ins): " + diagLine (e));
            for (int k = 0; k < 4; ++k)
            {
                mark ("remove a compressor");
               #if MICVST_NEW_ENGINE
                const AudioEngine::ChainEdit ce (e);
               #endif
                e.getChain().removePlugin ((int) e.getChain().entries().size() - 1);
                e.rebuildGraph();
            }
            pump (total - (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 - 6.0);

            // restore the saved state into a second engine (as at program start): through a
            // settings file written and read with MicVST's own serialisation
            {
                const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("micvst_enginetest_config", ".xml");
                bool written = false;
                if (auto xml = toValueTree (state).createXml()) written = xml->writeTo (file);
                MicVSTState fromFile;
                if (auto xml = juce::XmlDocument::parse (file)) fromFile = fromValueTree (juce::ValueTree::fromXml (*xml));
                pr ("  settings file: " + juce::String (written ? "written" : "NOT written") + ", " + juce::String (file.getSize()) + " bytes, read back "
                    + juce::String (fromFile.plugins.size()) + " plug-ins");
                file.deleteFile();
                AudioEngine second;
                second.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                for (auto& d : chain) second.getKnownPlugins().addType (d);
                MicVSTState s2 = fromFile;
                s2.inputDevice = {}; s2.outputDevice = {};   // devices of the first engine stay in use
                second.applyState (s2);
                const auto again = second.captureState();
                juce::String restoredNames;
                for (auto& en : second.getChain().entries())
                    if (auto* n = second.getGraph().getNodeForId (en.node)) restoredNames << n->getProcessor()->getName() << ", ";
                pr ("  restored plug-ins: " + restoredNames.dropLastCharacters (2));
                int same = 0;
                for (int i = 0; i < juce::jmin (again.plugins.size(), state.plugins.size()); ++i)
                    if (again.plugins[i].state == state.plugins[i].state) ++same;
                pr ("  restore into a new engine: " + juce::String (again.plugins.size()) + " plug-ins, identical state blobs: "
                    + juce::String (same) + "/" + juce::String (state.plugins.size()));
            }
            report = diagLine (e);
        }
        const auto& L = vdev::recorder().left;
        const size_t start = firstAudio (L) + 9600;
        bool finite = true; float mx = 0;
        for (size_t i = start; i < L.size(); ++i) { finite = finite && std::isfinite (L[i]); mx = std::max (mx, std::abs (L[i])); }
        pr ("  end: " + report);
        pr ("  output finite: " + juce::String (finite ? "yes" : "NO") + ", peak " + db (juce::Decibels::gainToDecibels (mx)));
        // clicks around the edits (the plug-ins at their default settings keep the tone a tone)
        const double fs = vdev::config().cableRate, t0 = vdev::recorder().outputStart.load();
        int clicked = 0;
        for (auto& m : editMarks)
        {
            const auto a = (size_t) std::max (0.0, (m.first - t0) * fs), b = std::min (L.size(), a + (size_t) fs);
            if (clicks (L, a, b, mx, 997.0 / fs).events > 0) { ++clicked; pr ("  click after: " + m.second); }
        }
        pr ("  edits during playback with real plug-ins: " + juce::String ((int) editMarks.size()) + ", followed by a click within 1 s: " + juce::String (clicked)
            + "; click events in the whole run: " + juce::String (clicks (L, start, L.size(), mx, 997.0 / fs).events));
    }

    //==============================================================================
    // Long run with the recording analysed minute by minute instead of kept (hours of audio):
    // 997 Hz -6 dBFS through gain 0.5 + delay 240 + load 30 %. Per minute: clicks (second
    // difference), silent gaps, THD+N and level of a 2 s slice, RSS, process CPU, MicVST's
    // counters. Every click/gap is attributed: cable late or mic packet lost (stalls of the test
    // machine), a MicVST event (underrun, trim, skip, ...), a planned change (chaos mode).
    // chaos: every 20 s one change while audio plays (bypass, add/move/remove a plug-in, safety
    // buffer fixed 40 ms <-> Auto), every 5 min a device buffer change (both devices reopen).
    void longRun (double minutes, bool chaos)
    {
        auto& cfg = vdev::config();
        sineConfig (997.0, 0.5, 48000);
        const double fs = cfg.cableRate, seconds = minutes * 60.0;
        const double limit = 3.0 * 0.25 * std::pow (twoPi * 997.0 / fs, 2.0) + 2.0e-4;
        auto& rec = vdev::recorder();
        std::mutex lock;
        std::vector<float> pending, work;
        pending.reserve (1 << 23); work.reserve (1 << 23);
        rec.sink = [&] (const float* l, const float*, int n)
        {
            const std::lock_guard<std::mutex> g (lock);
            if (pending.size() + (size_t) n <= pending.capacity()) pending.insert (pending.end(), l, l + n);
        };
        // MicVST events and planned changes on the cable timeline. A counter seen changed at a poll
        // changed somewhere since the previous poll: [from, at] (the main thread can be busy with the
        // per-minute analysis for seconds - the event is then not pinned to the late poll's time).
        struct Mark { int64_t at; juce::String what; int64_t from = -1; int64_t lo() const { return from >= 0 ? from : at; } };
        std::vector<Mark> marks;
        int64_t analysed = 0;                 // cable samples analysed so far
        float p1 = 0, p2 = 0;                 // last two samples of the previous chunk
        bool audioStarted = false; int64_t skipUntil = 0;
        int zeros = 0; int64_t zeroStart = 0, lastClick = -1000000;
        struct Totals { int clicks = 0, clicksUnexplained = 0, gaps = 0, gapsUnexplained = 0; double gapMs = 0; } tot;
        std::map<juce::String, int> causes;
        double worstThd = -200.0;
        std::vector<std::pair<double, double>> rssSeries;

        auto cableIndex = [&] (double t) { return (int64_t) ((t - rec.outputStart.load()) * fs); };
        auto causeOf = [&] (int64_t at) -> juce::String
        {
            for (int i = 0; i < rec.gapCount.load (std::memory_order_acquire); ++i)
            {
                const auto& g = rec.gapEvents[(size_t) i];
                if (at >= g.first - (int64_t) (0.02 * fs) && at <= g.first + g.second + (int64_t) (0.06 * fs)) return "cable late (test machine)";
            }
            const auto& st = vdev::stats();
            const double latency = 0.06 * fs;
            for (int i = 0; i < st.lostCount.load (std::memory_order_acquire); ++i)
            {
                // by capture time: the mic's sample index restarts when the devices are reopened
                const auto m = (int64_t) ((st.lostTimes[(size_t) i] - rec.outputStart.load()) * fs + latency);
                if (at >= m - (int64_t) (0.06 * fs) && at <= m + (int64_t) (0.2 * fs)) return "mic packet lost (test machine)";
            }
            for (auto& mk : marks)
                if (at >= mk.lo() - (int64_t) (0.4 * fs) && at <= mk.at + (int64_t) (0.4 * fs)) return mk.what;
            return {};
        };
        // For an unexplained finding: what was nearest (to find causes the test does not record)
        std::vector<int64_t> deviceChanges;   // cable index after each planned device change
        juce::String lastChangeKind = "none";
        auto context = [&] (int64_t at) -> juce::String
        {
            auto sec = [&] (int64_t d) { return juce::String ((double) d / fs, 3) + " s"; };
            juce::String r;
            int64_t lastDev = -1;
            for (auto d : deviceChanges) if (d <= at) lastDev = d;
            if (lastDev >= 0) r << "; " << sec (at - lastDev) << " after a device change";
            r << "; last chain change: " << lastChangeKind;
            const Mark* best = nullptr;
            auto dist = [at] (const Mark& mk) { return at < mk.lo() ? mk.lo() - at : (at > mk.at ? at - mk.at : (int64_t) 0); };
            auto describeMark = [&] (const Mark& mk) { return mk.what + " at " + sec (mk.lo() - at) + (mk.from >= 0 ? " .. " + sec (mk.at - at) : juce::String()); };
            juce::StringArray close;
            for (auto& mk : marks)
            {
                if (best == nullptr || dist (mk) < dist (*best)) best = &mk;
                if (dist (mk) <= (int64_t) (0.5 * fs)) close.add (describeMark (mk));
            }
            if (! close.isEmpty())   r << "; events within 0.5 s: " << close.joinIntoString (", ");
            else if (best != nullptr) r << "; nearest event: " << describeMark (*best);
            int64_t g = INT64_MAX;
            for (int i = 0; i < rec.gapCount.load (std::memory_order_acquire); ++i)
                if (std::abs (rec.gapEvents[(size_t) i].first - at) < std::abs (g)) g = rec.gapEvents[(size_t) i].first - at;
            if (g != INT64_MAX) r << "; nearest cable-late at " << sec (g);
            const auto& st = vdev::stats();
            int64_t lp = INT64_MAX;
            for (int i = 0; i < st.lostCount.load (std::memory_order_acquire); ++i)
            {
                const auto m = (int64_t) ((st.lostTimes[(size_t) i] - rec.outputStart.load()) * fs + 0.06 * fs) - at;
                if (std::abs (m) < std::abs (lp)) lp = m;
            }
            if (lp != INT64_MAX) r << "; nearest mic packet lost at " << sec (lp);
            return r;
        };
        // any recorded event in [a, b] (cable timeline)
        auto eventIn = [&] (int64_t a, int64_t b)
        {
            for (int i = 0; i < rec.gapCount.load (std::memory_order_acquire); ++i)
            {
                const auto& g = rec.gapEvents[(size_t) i];
                if (g.first + g.second >= a && g.first <= b) return true;
            }
            const auto& st = vdev::stats();
            for (int i = 0; i < st.lostCount.load (std::memory_order_acquire); ++i)
            {
                const auto m = (int64_t) ((st.lostTimes[(size_t) i] - rec.outputStart.load()) * fs + 0.06 * fs);
                if (m >= a && m <= b) return true;
            }
            for (auto& mk : marks)
                if (mk.at >= a && mk.lo() <= b) return true;
            return false;
        };

        int minuteClicks = 0, minuteClicksU = 0, minuteGaps = 0, minuteGapsU = 0;
        auto analyseChunk = [&] (const std::vector<float>& y)
        {
            for (size_t i = 0; i < y.size(); ++i)
            {
                const int64_t at = analysed + (int64_t) i;
                const float v = y[i];
                if (! audioStarted) { if (std::abs (v) > 1.0e-6f) { audioStarted = true; skipUntil = at + (int64_t) (0.2 * fs); } p2 = p1; p1 = v; continue; }
                if (at > skipUntil)
                {
                    if (std::abs ((double) v - 2.0 * p1 + p2) > limit)
                    {
                        if (at - lastClick > 480)
                        {
                            ++tot.clicks; ++minuteClicks;
                            const auto c = causeOf (at);
                            if (c.isEmpty()) { ++tot.clicksUnexplained; ++minuteClicksU; pr ("  unexplained click at " + juce::String ((double) at / fs, 3) + " s" + context (at)); }
                            else
                            {
                                ++causes["click: " + c];
                                if (c == "planned change")   // must be click-free (fades): show where exactly
                                    pr ("  click at " + juce::String ((double) at / fs, 3) + " s near a planned change" + context (at));
                            }
                        }
                        lastClick = at;
                    }
                    if (v == 0.0f) { if (zeros++ == 0) zeroStart = at; }
                    else
                    {
                        if (zeros > 48)
                        {
                            ++tot.gaps; ++minuteGaps; tot.gapMs += 1000.0 * zeros / fs;
                            const auto c = causeOf (zeroStart);
                            if (c.isEmpty()) { ++tot.gapsUnexplained; ++minuteGapsU; pr ("  unexplained gap at " + juce::String ((double) zeroStart / fs, 3) + " s, " + juce::String (1000.0 * zeros / fs, 1) + " ms" + context (zeroStart)); }
                            else
                            {
                                ++causes["gap: " + c];
                                if (c.contains ("silence") || c.contains ("graph busy") || c.contains ("restarted") || c.contains ("invalid"))
                                    pr ("  gap at " + juce::String ((double) zeroStart / fs, 3) + " s, " + juce::String (1000.0 * zeros / fs, 1) + " ms: " + c + context (zeroStart));
                            }
                        }
                        zeros = 0;
                    }
                }
                p2 = p1; p1 = v;
            }
            analysed += (int64_t) y.size();
        };

        const bool withAutoGain = envSet ("ETEST_LONG_AUTOGAIN");
        pr ("## Long run" + juce::String (chaos ? " with changes during playback" : "") + ": " + juce::String (minutes, 0)
            + " min real time, chain gain 0.5 + delay 240 smp + load 30 %" + (withAutoGain ? juce::String (" + Otomatik Kazanç") : juce::String())
            + ", 997 Hz -6 dBFS, buffer 480, streamed analysis");
        pr ("| t (min) | RSS (MB) | process CPU (%) | clicks (unexplained) | gaps (unexplained) | THD+N in 43 ms windows, median / worst (event-free 2 s slices) | speed p-p within a slice (ppm) | level (dBFS) | MicVST |");
        pr ("|---:|---:|---:|---:|---:|---:|---:|---:|---|");
        {
            EngineRun run (480, seconds);
            auto& e = *run.engine;
            run.add (testDesc ("TestGain", 0.5));
            run.add (testDesc ("TestDelay", 240));
            run.add (testDesc ("TestLoad", 0.3));
            if (withAutoGain)   // ETEST_LONG_AUTOGAIN: the built-in Otomatik Kazanç at the end of the chain
            {
                e.getChain().addAutoGain (true);
                const AudioEngine::ChainEdit edit (e);
                e.rebuildGraph();
            }
            const double start = juce::Time::getMillisecondCounterHiRes() * 0.001;
            // ETEST_CHANGE_EVERY / ETEST_DEVICE_EVERY (s): other intervals for the chaos mode
            const double changeEvery = juce::jmax (2.0, juce::SystemStats::getEnvironmentVariable ("ETEST_CHANGE_EVERY", "20").getDoubleValue());
            const double deviceEvery = juce::jmax (5.0, juce::SystemStats::getEnvironmentVariable ("ETEST_DEVICE_EVERY", "300").getDoubleValue());
            double lastCpu = processCpuSeconds(), lastT = start, nextMinute = start + 60.0, nextChange = start + changeEvery, nextDevice = start + deviceEvery;
            double lastPoll = start;
            auto d0 = e.getDiagnostics();
            int change = 0;
            juce::String err;
            while (juce::Time::getMillisecondCounterHiRes() * 0.001 - start < seconds)
            {
                pump (0.1);
                const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
                const auto d1 = e.getDiagnostics();
                auto note = [&] (const char* kind, uint64_t a, uint64_t b) { if (b > a) marks.push_back ({ cableIndex (t), juce::String ("MicVST ") + kind, cableIndex (lastPoll) }); };
                note ("underrun", d0.bridge.underruns, d1.bridge.underruns);
                note ("trim", d0.bridge.resyncs, d1.bridge.resyncs);
                note ("ring drop", d0.bridge.droppedBlocks, d1.bridge.droppedBlocks);
               #if MICVST_NEW_ENGINE
                note ("input skip", d0.inputSkips, d1.inputSkips);
                note ("queue overflow", d0.inputQueueOverflows, d1.inputQueueOverflows);
               #endif
                note ("device reopened by the watchdog", d0.watchdogRestarts, d1.watchdogRestarts);
                note ("graph busy (silent block)", d0.graphBusyBlocks, d1.graphBusyBlocks);
                note ("chain added silence to a block", d0.chainSilentBlocks, d1.chainSilentBlocks);
                note ("invalid plug-in output", d0.invalidBlocks, d1.invalidBlocks);
                d0 = d1;
                lastPoll = t;

                if (chaos && t >= nextChange)
                {
                    nextChange += changeEvery;
                    static const char* const kinds[] = { "bypass on", "bypass off", "add plug-in", "move plug-in", "remove plug-in", "safety 40 ms / Auto" };
                    lastChangeKind = kinds[change % 6];
                    marks.push_back ({ cableIndex (t), "planned change" });
                    switch (change++ % 6)
                    {
                        case 0: { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (0, true); e.rebuildGraph(); } break;
                        case 1: { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (0, false); e.rebuildGraph(); } break;
                        case 2: e.getChain().addPlugin (e.getFormatManager(), testDesc ("TestGain", 1.0), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
                                { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); } break;
                        case 3: { const AudioEngine::ChainEdit ce (e); e.getChain().movePlugin ((int) e.getChain().entries().size() - 1, 0); e.rebuildGraph(); } break;
                        case 4: { const AudioEngine::ChainEdit ce (e); e.getChain().removePlugin (0); e.rebuildGraph(); } break;
                        case 5: e.setSafetyBufferMs ((change / 6) % 2 == 0 ? 40 : 0); break;   // fixed 40 ms <-> Auto
                    }
                    marks.back().from = marks.back().at;   // the whole change: start .. end
                    marks.back().at = cableIndex (juce::Time::getMillisecondCounterHiRes() * 0.001);
                }
                if (chaos && t >= nextDevice)
                {
                    nextDevice += deviceEvery;
                    const auto from = cableIndex (t);
                    static const int sizes[] = { 960, 256, 480 };
                    e.setDeviceConfig ("VMic", "VCable", 48000.0, sizes[(int) std::lround ((t - start) / deviceEvery) % 3]);
                    raiseEngineThreads();
                    marks.push_back ({ cableIndex (juce::Time::getMillisecondCounterHiRes() * 0.001), "planned change", from });
                    deviceChanges.push_back (marks.back().at);
                }

                if (t >= nextMinute)
                {
                    nextMinute += 60.0;
                    { const std::lock_guard<std::mutex> g (lock); std::swap (pending, work); }
                    // THD+N and level: median of up to five 2 s slices of this minute that hold no
                    // recorded event (stall of the test machine, MicVST event, planned change):
                    // such a slice measures the event, not the audio path. No clean slice: "-".
                    // Distortion: THD+N in 2048-sample windows (43 ms: the slow speed changes of
                    // the clock control do not count as distortion there - a 2 s fit would read
                    // them as -20..-50 dB "THD+N"); speed: fitted frequency in 4096-sample windows.
                    // Only from 2 s slices without any recorded event (up to five per minute).
                    double thd = 0, thdWorst = 0, level = -200, speedPp = 0;
                    int clean = 0;
                    if (work.size() > (size_t) (12 * fs))
                    {
                        std::vector<double> th, lv, sp;
                        const auto len = (size_t) (2 * fs), margin = (size_t) (0.5 * fs);
                        const double cps = 997.0 / fs;
                        for (size_t from = analysed == 0 ? (size_t) (2 * fs) : margin; from + len + margin <= work.size() && clean < 5; from += len / 2)
                        {
                            if (eventIn (analysed + (int64_t) (from - margin), analysed + (int64_t) (from + len + margin))) continue;
                            for (size_t w = 0; w + 2048 <= len; w += 2048)
                                th.push_back (fitSine (work, from + w, 2048, cps).residualDb);
                            std::vector<double> ppm;
                            for (size_t w = 0; w + 4096 <= len; w += 4096)
                            {
                                const auto f = fitSine (work, from + w, 4096, cps);
                                ppm.push_back ((f.freq / cps - 1.0) * 1e6);
                                lv.push_back (20.0 * std::log10 (std::max (f.amp, 1e-12)));
                            }
                            std::sort (ppm.begin(), ppm.end());
                            sp.push_back (ppm.back() - ppm.front());
                            ++clean;
                            from += len / 2;   // slices do not overlap
                        }
                        if (clean > 0)
                        {
                            std::sort (th.begin(), th.end()); std::sort (lv.begin(), lv.end());
                            thd = th[th.size() / 2]; thdWorst = th.back(); level = lv[lv.size() / 2];
                            speedPp = *std::max_element (sp.begin(), sp.end());
                            if (audioStarted) worstThd = std::max (worstThd, thdWorst);
                        }
                    }
                    minuteClicks = minuteClicksU = minuteGaps = minuteGapsU = 0;
                    analyseChunk (work);
                    work.clear();
                    const double cpu = processCpuSeconds();
                    const double rss = rssKb() / 1024.0;
                    rssSeries.push_back ({ (t - start) / 3600.0, rss });
                    pr ("| " + juce::String ((t - start) / 60.0, 1) + " | " + juce::String (rss, 1) + " | " + juce::String (100.0 * (cpu - lastCpu) / (t - lastT), 1)
                        + " | " + juce::String (minuteClicks) + " (" + juce::String (minuteClicksU) + ") | " + juce::String (minuteGaps) + " (" + juce::String (minuteGapsU)
                        + ") | " + (clean > 0 ? db (thd) + " / " + db (thdWorst) + " (" + juce::String (clean) + ")" : juce::String ("-"))
                        + " | " + (clean > 0 ? juce::String (juce::roundToInt (speedPp)) : juce::String ("-")) + " | " + (clean > 0 ? juce::String (level, 3) : juce::String ("-"))
                        + " | " + diagLine (e) + " |");
                    lastCpu = cpu; lastT = t;
                }
            }
        }
        { const std::lock_guard<std::mutex> g (lock); std::swap (pending, work); }
        rec.sink = nullptr;
        analyseChunk (work);
        // RSS trend: least-squares slope over the second half (after warm-up)
        double slope = 0;
        if (rssSeries.size() >= 4)
        {
            const size_t h = rssSeries.size() / 2;
            double sx = 0, sy = 0, sxx = 0, sxy = 0; const double n = (double) (rssSeries.size() - h);
            for (size_t i = h; i < rssSeries.size(); ++i) { const auto [x, y] = rssSeries[i]; sx += x; sy += y; sxx += x * x; sxy += x * y; }
            slope = (n * sxy - sx * sy) / std::max (1e-12, n * sxx - sx * sx);
        }
        pr ("recorded " + juce::String ((double) analysed / fs / 60.0, 1) + " min: clicks " + juce::String (tot.clicks) + " (unexplained " + juce::String (tot.clicksUnexplained)
            + "), gaps " + juce::String (tot.gaps) + " (" + juce::String (juce::roundToInt (tot.gapMs)) + " ms; unexplained " + juce::String (tot.gapsUnexplained) + "), worst THD+N in a 43 ms window of an event-free slice " + db (worstThd)
            + ", RSS " + (rssSeries.empty() ? juce::String ("?") : juce::String (rssSeries.front().second, 1) + " -> " + juce::String (rssSeries.back().second, 1)) + " MB (slope over the second half "
            + juce::String (slope, 2) + " MB/h)");
        for (auto& [k, v] : causes) pr ("  " + k + ": " + juce::String (v));
    }
}

#if MICVST_NEW_ENGINE
namespace
{
    //==============================================================================
    // Built-in "Kazanç" (gain): accuracy and transparency, the slider moved while audio runs (with
    // a control that steps the gain abruptly, to show that the detector would see it), mono/stereo
    // wiring, chain edits, restoring a saved chain, a different microphone rate and a long run.
    void gainTest (double liveSeconds)
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;
        const double toneHz = 997.0;
        auto addGain = [] (EngineRun& r, float db)
        {
            r.engine->getChain().addGain (db);
            const AudioEngine::ChainEdit edit (*r.engine);
            r.engine->rebuildGraph();
        };
        auto row = [] (const juce::String& what, const SineResult& r, double expectedDb)
        {
            pr ("| " + what + " | " + db (r.gainDbL, 3) + " / " + db (r.gainDbR, 3) + " | " + db (expectedDb, 3)
                + " | " + db (r.thdnL) + " / " + db (r.thdnR) + " | " + juce::String (r.clk.events) + " | " + juce::String (r.gaps.count) + " |");
        };

        pr ("## Kazanç: level and THD+N (997 Hz, -20 dBFS in, 48/48 kHz, buffer 480)");
        pr ("| Chain | Gain L / R | Expected | THD+N L / R | Clicks | Gaps |");
        pr ("|---|---:|---:|---:|---:|---:|");
        const double amp = 0.1;
        row ("empty chain (reference)", measureSine (toneHz, amp, 10.0, 480), 0.0);
        for (float g : { 0.0f, 6.0f, 12.0f, -30.0f, 3.3f })
            row ("Kazanç " + juce::String (g, 1) + " dB",
                 measureSine (toneHz, amp, 10.0, 480, 4.0, [&addGain, g] (EngineRun& r) { addGain (r, g); }, GainStage::dbToGain (g)),
                 (double) g);
        row ("full-scale tone, Kazanç +12 dB (output +12 dBFS: float, not clipped in MicVST)",
             measureSine (toneHz, 1.0, 10.0, 480, 4.0, [&addGain] (EngineRun& r) { addGain (r, 12.0f); }, 4.0), 12.0);

        pr ("");
        pr ("## Kazanç: channels");
        pr ("| Microphone / chain | Gain L / R | Expected | THD+N L / R | Clicks | Gaps |");
        pr ("|---|---:|---:|---:|---:|---:|");
        cfg.micChannels = 1;
        row ("mono mic -> Kazanç +6", measureSine (toneHz, amp, 8.0, 480, 4.0, [&addGain] (EngineRun& r) { addGain (r, 6.0f); }, 2.0), 6.0);
        row ("mono mic -> Kazanç +6 -> Mono->Stereo -> Kazanç -6",
             measureSine (toneHz, amp, 8.0, 480, 4.0, [&addGain] (EngineRun& r)
             {
                 addGain (r, 6.0f);
                 r.engine->getChain().addMonoToStereo();
                 addGain (r, -6.0f);
             }), 0.0);
        cfg.micChannels = 2;
        row ("stereo mic -> Stereo->Mono -> Kazanç +12",
             measureSine (toneHz, amp, 8.0, 480, 4.0, [&addGain] (EngineRun& r)
             {
                 r.engine->getChain().addStereoToMono();
                 addGain (r, 12.0f);
             }, 4.0), 12.0);

        // --- the slider moved while audio runs ---------------------------------------------------
        auto live = [&] (const juce::String& what, double seconds, bool control, bool load)
        {
            sineConfig (toneHz, amp, cfg.micRate);
            double underruns = 0, skips = 0;
            long rssBefore = 0, rssAfter = 0;
            int changes = 0;
            {
                EngineRun run (480, seconds + 2.0);
                if (load) run.add (testDesc ("TestLoad", 0.3));
                if (control) run.add (testDesc ("TestStepGain", 5));
                else         addGain (run, 0.0f);
                const int gainIndex = (int) run.engine->getChain().entries().size() - 1;
                pump (1.0);
                rssBefore = rssKb();
                std::mt19937 rng (17);
                std::uniform_real_distribution<float> value (GainStage::minDb, GainStage::maxDb);
                std::uniform_real_distribution<double> pause (0.015, 0.08);   // a dragged slider: 12..65 values/s
                const auto end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
                while (juce::Time::getMillisecondCounterHiRes() < end)
                {
                    if (! control) run.engine->getChain().setGainDb (gainIndex, value (rng));
                    ++changes;
                    pump (pause (rng));
                }
                if (! control) run.engine->getChain().setGainDb (gainIndex, 6.0f);   // final value, held
                pump (1.0);
                rssAfter = rssKb();
                const auto d = run.engine->getDiagnostics();
                underruns = (double) d.bridge.underruns; skips = (double) d.inputSkips;
                pr ("    " + diagLine (*run.engine));
            }
            const auto& L = vdev::recorder().left;
            const double cps = toneHz / fs;
            const size_t start = firstAudio (L) + (size_t) (0.05 * fs);
            std::vector<size_t> where;
            const auto clk = clicks (L, start, L.size(), amp * 4.0, cps, 480, &where);
            const auto gaps = silences (L, start, fs);
            juce::String held;
            if (! control && L.size() > (size_t) (0.6 * fs))
            {
                const auto f = fitSine (L, L.size() - (size_t) (0.5 * fs), 8192, cps);
                held = ", level after the last move " + db (juce::Decibels::gainToDecibels (f.amp / amp), 3) + " (expected +6.000 dB)";
            }
            pr ("| " + what + " | " + juce::String (changes) + " | " + juce::String (clk.events) + " | " + juce::String (gaps.count)
                + " | " + juce::String ((int) underruns) + " / " + juce::String ((int) skips) + " | "
                + juce::String ((rssAfter - rssBefore) / 1024.0, 1) + " MB |" + held);
            for (size_t k = 0; k < std::min<size_t> (where.size(), 5); ++k)
                pr ("    click at " + juce::String ((double) where[k] / fs, 3) + " s");
        };

        pr ("");
        pr ("## Kazanç: slider moved during playback (new random value -30..+12 dB every 15-80 ms)");
        pr ("| Run | Changes | Clicks | Gaps | Underruns / skips | RSS change |");
        pr ("|---|---:|---:|---:|---:|---:|");
        live ("control: abrupt steps -30 <-> +12 dB every 5 blocks (no ramp)", 6.0, true, false);
        live ("Kazanç, " + juce::String (liveSeconds, 0) + " s", liveSeconds, false, false);
        live ("Kazanç + 30 % plug-in load, " + juce::String (liveSeconds, 0) + " s", liveSeconds, false, true);
        {
            const auto oldMic = cfg.micRate;
            cfg.micRate = 44100.0;   // microphone 44.1 kHz, cable 48 kHz (the bridge resamples)
            live ("Kazanç, microphone 44.1 kHz -> cable 48 kHz, 20 s", 20.0, false, false);
            cfg.micRate = oldMic;
        }
        cfg.micChannels = 1;
        live ("Kazanç, mono microphone, 20 s", 20.0, false, false);
        cfg.micChannels = 2;

        // --- chain edits with a Kazanç in the chain ----------------------------------------------
        pr ("");
        pr ("## Kazanç: chain edits during playback (add, bypass, move, remove; 6 rounds)");
        {
            sineConfig (toneHz, amp, cfg.micRate);
            {
                EngineRun run (480, 30.0);
                run.add (testDesc ("TestDelay", 100));
                pump (1.0);
                auto& e = *run.engine;
                for (int i = 0; i < 6; ++i)
                {
                    e.getChain().addGain (6.0f); { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); } pump (0.5);
                    { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (1, true);  e.rebuildGraph(); } pump (0.5);
                    { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (1, false); e.rebuildGraph(); } pump (0.5);
                    { const AudioEngine::ChainEdit ce (e); e.getChain().movePlugin (1, 0);    e.rebuildGraph(); } pump (0.5);
                    { const AudioEngine::ChainEdit ce (e); e.getChain().removePlugin (0);     e.rebuildGraph(); } pump (0.5);
                }
                pr ("    " + diagLine (e));
            }
            const auto& L = vdev::recorder().left;
            const size_t start = firstAudio (L) + (size_t) (0.05 * fs);
            const auto clk = clicks (L, start, L.size(), amp * 2.0, toneHz / fs);
            const auto gaps = silences (L, start, fs);
            pr ("clicks " + juce::String (clk.events) + ", gaps (exact-zero runs, incl. the 5 ms edit fades' silent block) " + juce::String (gaps.count)
                + " (" + juce::String (gaps.ms, 1) + " ms)");
        }

        // --- saved chain restored ----------------------------------------------------------------
        pr ("");
        pr ("## Kazanç: value survives captureState -> applyState (config.xml round trip)");
        {
            MicVSTState saved;
            {
                EngineRun run (480, 2.0);
                addGain (run, 0.0f);
                run.engine->getChain().setGainDb (0, 7.5f);
                pump (0.5);
                saved = run.engine->captureState();
            }
            const auto xml = toValueTree (saved).createXml();
            const auto back = fromValueTree (juce::ValueTree::fromXml (*xml));
            const auto r = measureSine (toneHz, amp, 8.0, 480, 4.0, [back] (EngineRun& run)
            {
                MicVSTState st = back;
                st.inputDevice = "VMic"; st.outputDevice = "VCable"; st.bufferSize = 480;
                run.engine->applyState (st);
                pump (0.05);
                raiseEngineThreads();
            }, GainStage::dbToGain (7.5));
            pr ("restored chain: " + juce::String (back.plugins.size()) + " element(s), '" + (back.plugins.isEmpty() ? juce::String() : back.plugins[0].fileOrId)
                + "'; measured gain L " + db (r.gainDbL, 3) + " / R " + db (r.gainDbR, 3) + " (expected +7.500 dB), clicks " + juce::String (r.clk.events));
        }
    }

    //==============================================================================
    // Built-in "Otomatik Kazanç" (auto gain), checks through the real engine: typical speech near
    // -20 dBFS (gain 0 ... +6 dB), shouting near -15 dBFS, a sudden rise and the way back, long
    // silence (last gain kept, no gate) and speaking again, never above -10 dBFS, the 3 ms look-ahead
    // latency, free position in the chain, on/off, the manual Kazanç unchanged with auto gain off,
    // save/restore, realistic synthetic speech (tests/SpeechSynth.h).
    // Simple test signal (clicks are counted on it): speech-like phrases (2.5 s of 150 ms 300 Hz
    // syllables every 250 ms, then 1 s pause) - every syllable at the same level.
    float voiceSample (double t, double peakAmp)
    {
        const double inPhrase = std::fmod (t, 3.5);
        double env = 0.0;
        if (inPhrase < 2.5)
            if (const double s = std::fmod (inPhrase, 0.25); s < 0.15) env = 0.5 - 0.5 * std::cos (twoPi * s / 0.15);
        return (float) (peakAmp * env * std::sin (twoPi * 300.0 * t));
    }

    // Voice whose peak level changes at given times: segments = { { from second, peak dBFS }, ... }
    // (-200 = no voice); plus white noise of noiseDb RMS all the time (-200 = none).
    void voiceConfig (std::vector<std::pair<double, double>> segments, double noiseDb = -200.0)
    {
        auto& cfg = vdev::config();
        const double rate = cfg.micRate;
        const double n = noiseDb > -150.0 ? juce::Decibels::decibelsToGain (noiseDb) * std::sqrt (3.0) : 0.0;
        for (auto& s : segments) s.second = s.second > -150.0 ? juce::Decibels::decibelsToGain (s.second) : 0.0;
        cfg.signal = [segments, n, rate] (int, int64_t i)
        {
            const double t = (double) i / rate;
            double a = 0.0;
            for (auto& s : segments) if (t >= s.first) a = s.second;
            float v = a > 0.0 ? voiceSample (t, a) : 0.0f;
            if (n > 0.0)
            {
                uint32_t h = (uint32_t) i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                v += (float) (n * ((double) h / 4294967295.0 * 2.0 - 1.0));
            }
            return v;
        };
    }
    void voiceConfig (double peakDb) { voiceConfig ({ { 0.0, peakDb } }); }

    // Realistic synthetic speech (SpeechSynth.h) at the given typical level, looped.
    void talkConfig (double typicalDb, unsigned seed, double noiseDb = -65.0, double seconds = 60.0)
    {
        auto& cfg = vdev::config();
        speech::Params p;
        p.fs = cfg.micRate; p.peakDb = typicalDb; p.seed = seed; p.noiseDb = noiseDb; p.plosiveDb = -10.0;
        auto data = std::make_shared<std::vector<float>> (speech::generate (seconds, p).x);
        cfg.signal = [data] (int, int64_t i) { return (*data)[(size_t) (i % (int64_t) data->size())]; };
    }

    juce::String chainOrder (PluginChain& c)
    {
        juce::String s;
        for (int i = 0; i < (int) c.entries().size(); ++i)
            s << (i > 0 ? ", " : "") << (c.isAutoGain (i) ? "Otomatik Kazanc" : c.isGain (i) ? "Kazanc" : "other");
        return s;
    }

    int autoGainIndex (PluginChain& c)
    {
        for (int i = 0; i < (int) c.entries().size(); ++i) if (c.isAutoGain (i)) return i;
        return -1;
    }

    void autoGainTest()
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;
        const double N = AutoGainStage::normalPeakDb, S = AutoGainStage::shoutPeakDb, C = AutoGainStage::ceilingDb;
        auto addAuto = [] (EngineRun& r, bool on)
        {
            r.engine->getChain().addAutoGain (on);
            const AudioEngine::ChainEdit edit (*r.engine);
            r.engine->rebuildGraph();
        };
        auto edit = [] (EngineRun& r, std::function<void (PluginChain&)> f)
        {
            const AudioEngine::ChainEdit ce (*r.engine);
            f (r.engine->getChain());
            r.engine->rebuildGraph();
        };
        auto peakDbOf = [&fs] (double fromSec, double toSec)
        {
            const auto& L = vdev::recorder().left;
            float m = 0.0f;
            for (size_t i = (size_t) (fromSec * fs); i < std::min ((size_t) (toSec * fs), L.size()); ++i) m = std::max (m, std::abs (L[i]));
            return juce::Decibels::gainToDecibels ((double) m, -200.0);
        };
        auto meters = [] (EngineRun& r)
        {
            AutoGainStage::Meters m;
            auto& c = r.engine->getChain();
            c.takeAutoGainMeters (autoGainIndex (c), m);
            return m;
        };
        // Loudest output of the runs where Otomatik Kazanç is the last gain stage, from 0.3 s after the
        // first audio on (the test starts the engine with an empty chain and inserts the element a few
        // milliseconds later - the audio before that never went through it). Must stay <= -10 dBFS.
        double loudest = -200.0;
        auto clicksInRun = [&fs, &loudest, &peakDbOf] (bool countLoudest)
        {
            const auto& L = vdev::recorder().left;
            const double top = peakDbOf (0.0, 1.0e9);
            if (countLoudest) loudest = std::max (loudest, peakDbOf ((double) firstAudio (L) / fs + 0.3, 1.0e9));
            // from 50 ms after the first audio to 50 ms before the last: the engine's own fade when the
            // test run is shut down (often in the middle of a syllable) is not a click of the element
            size_t last = L.size();
            while (last > 0 && std::abs (L[last - 1]) < 1.0e-6f) --last;
            const size_t start = firstAudio (L) + (size_t) (0.05 * fs), end = last > (size_t) (0.05 * fs) ? last - (size_t) (0.05 * fs) : 0;
            return clicks (L, start, end, juce::Decibels::decibelsToGain (top), 300.0 / fs).events;
        };

        pr ("## Otomatik Kazanç: normal speech at different microphone levels (13 s, output analysed from 6.8 s)");
        pr ("| Chain | Input peaks | Output peaks | Aim | Gain shown in the row | Gain measured (out - in) | L = R | Clicks |");
        pr ("|---|---:|---:|---:|---:|---:|---:|---:|");
        auto speechRow = [&] (const juce::String& what, double peakDb, std::function<void (EngineRun&)> setup, double aimDb, double inputToAuto,
                              bool autoIsLast = true)
        {
            voiceConfig (peakDb);
            AutoGainStage::Meters m;
            bool withAuto = false;
            {
                EngineRun run (480, 13.0);
                setup (run);
                withAuto = autoGainIndex (run.engine->getChain()) >= 0;
                pump (13.0);
                if (withAuto) m = meters (run);
                pr ("    " + diagLine (*run.engine));
            }
            const auto& L = vdev::recorder().left;
            const auto& R = vdev::recorder().right;
            const double out = peakDbOf (6.8, 1.0e9);
            bool same = true;
            for (size_t i = 0; i < std::min (L.size(), R.size()); ++i) same = same && juce::exactlyEqual (L[i], R[i]);
            const int clk = clicksInRun (withAuto && autoIsLast);
            pr ("| " + what + " | " + db (peakDb, 2) + "FS | " + db (out, 2) + "FS | " + db (aimDb, 1) + "FS | "
                + (withAuto ? db (m.gainDb, 2) : juce::String ("-")) + " | " + db (out - inputToAuto, 2) + " | " + (same ? "yes" : "NO")
                + " | " + juce::String (clk) + " |");
        };
        auto autoOnly = [&addAuto] (EngineRun& r) { addAuto (r, true); };
        speechRow ("empty chain (reference)", -20.0, [] (EngineRun&) {}, -20.0, -20.0);
        speechRow ("Otomatik Kazanç, voice at -26 dBFS (needs +6 dB)", -26.0, autoOnly, N, -26.0);
        speechRow ("Otomatik Kazanç, voice at -23 dBFS (needs +3 dB)", -23.0, autoOnly, N, -23.0);
        speechRow ("Otomatik Kazanç, voice already at -20 dBFS", -20.0, autoOnly, N, -20.0);
        speechRow ("Otomatik Kazanç, very quiet voice -30 dBFS (boost stops at +6 dB: expected -24)", -30.0, autoOnly, -30.0 + AutoGainStage::maxBoostDb, -30.0);
        speechRow ("Otomatik Kazanç, voice at -15 dBFS (above the aim: no reduction, stays -15)", -15.0, autoOnly, -15.0, -15.0);
        speechRow ("Otomatik Kazanç, hot microphone -2 dBFS (gain stays 0 dB, the ceiling holds -10)", -2.0, autoOnly, C, -2.0);

        pr ("");
        pr ("## Otomatik Kazanç: realistic synthetic speech (SpeechSynth, 45 s, output analysed from 15 s; typical = level of the loudest 20 % of the voiced 10 ms windows)");
        pr ("| Input typical | Output typical | Expected | Loudest output | Gain shown at the end |");
        pr ("|---:|---:|---:|---:|---:|");
        for (double in : { -30.0, -26.0, -23.0, -20.0, -16.0 })
        {
            talkConfig (in, 7);
            AutoGainStage::Meters m;
            {
                EngineRun run (480, 45.0);
                addAuto (run, true);
                pump (45.0);
                m = meters (run);
            }
            const auto& L = vdev::recorder().left;
            const auto from = std::min (L.size(), (size_t) (15.0 * fs));
            const double outTyp = speech::typicalLevel (L.data() + from, L.size() - from, fs);
            const double expected = juce::jlimit (in, in + AutoGainStage::maxBoostDb, N);
            loudest = std::max (loudest, peakDbOf (15.0, 1.0e9));
            pr ("| " + db (in, 1) + "FS | " + db (outTyp, 2) + "FS | " + db (expected, 1) + "FS (+-1) | " + db (peakDbOf (15.0, 1.0e9), 2) + "FS | " + db (m.gainDb, 2) + " |");
        }

        pr ("");
        pr ("## Otomatik Kazanç: shouting and back (voice -24 dBFS, from 14 s on 10 dB louder at -14 dBFS, from 24.5 s on -24 dBFS again)");
        {
            voiceConfig ({ { 0.0, -24.0 }, { 14.0, -14.0 }, { 24.5, -24.0 } });
            {
                EngineRun run (480, 35.0);
                addAuto (run, true);
                pump (35.0);
                pr ("    " + diagLine (*run.engine));
            }
            const int clk = clicksInRun (true);
            pr ("output peaks: normal phrase before " + db (peakDbOf (10.4, 13.2), 2) + "FS (aim " + db (N, 1) + "FS); first 0.5 s of shouting "
                + db (peakDbOf (13.9, 14.6), 2) + "FS; last shouting phrase " + db (peakDbOf (20.9, 23.7), 2) + "FS (aim " + db (S, 1) + "FS)");
            pr ("normal phrases after the shouting: " + db (peakDbOf (24.4, 27.2), 2) + "FS / " + db (peakDbOf (27.9, 30.7), 2) + "FS / "
                + db (peakDbOf (31.4, 34.2), 2) + "FS; clicks " + juce::String (clk));
        }

        pr ("");
        pr ("## Otomatik Kazanç: long silence and speaking again (voice -20 dBFS until 9.6 s, silence, from 21 s on 3 dB louder; white noise -70 dBFS RMS all the time)");
        {
            voiceConfig ({ { 0.0, -20.0 }, { 9.6, -200.0 }, { 21.0, -17.0 } }, -70.0);
            AutoGainStage::Meters a, b, c, d, e;
            {
                EngineRun run (480, 34.0);
                addAuto (run, true);
                pump (9.0);  a = meters (run);
                pump (1.5);  b = meters (run);
                pump (10.0); c = meters (run);
                pump (1.0);  d = meters (run);
                pump (12.0); e = meters (run);
                pr ("    " + diagLine (*run.engine));
            }
            const auto& L = vdev::recorder().left;
            double s2 = 0.0; size_t n = 0;
            for (size_t i = (size_t) (12.0 * fs); i < std::min (L.size(), (size_t) (20.0 * fs)); ++i, ++n) s2 += (double) L[i] * L[i];
            const double noiseOut = juce::Decibels::gainToDecibels (std::sqrt (n > 0 ? s2 / (double) n : 0.0), -200.0);
            clicksInRun (true);
            pr ("gain shown: 9.0 s (speaking) " + db (a.gainDb, 4) + ", 10.5 s (silence) " + db (b.gainDb, 4) + ", 20.5 s (silence) " + db (c.gainDb, 4)
                + ", 21.5 s (speaking again) " + db (d.gainDb, 4) + ", 33.5 s " + db (e.gainDb, 4));
            pr ("noise in the output during the silence (12..20 s): " + db (noiseOut, 2) + "FS RMS = input -70 dBFS + kept gain " + db (c.gainDb, 2)
                + " (no gate); last phrase " + db (peakDbOf (31.4, 34.2), 2) + "FS (aim " + db (N, 1) + "FS)");
        }

        pr ("");
        pr ("## Otomatik Kazanç: a full-scale phrase right after quiet speech (voice -25 dBFS, from 14 s on 0 dBFS)");
        {
            voiceConfig ({ { 0.0, -25.0 }, { 14.0, 0.0 } });
            AutoGainStage::Meters before;
            {
                EngineRun run (480, 18.0);
                addAuto (run, true);
                pump (13.5); before = meters (run);
                pump (4.5);
                pr ("    " + diagLine (*run.engine));
            }
            clicksInRun (true);
            pr ("gain before " + db (before.gainDb, 2) + "; output peak of the 0 dBFS phrase " + db (peakDbOf (13.9, 18.0), 3) + "FS (limit " + db (C, 1) + "FS)");
        }

        pr ("");
        pr ("## Otomatik Kazanç: position in the chain is free");
        {
            voiceConfig (-20.0);
            EngineRun run (480, 22.0);
            run.engine->getChain().addGain (6.0f);
            run.add (testDesc ("TestDelay", 10));
            addAuto (run, true);
            auto& c = run.engine->getChain();
            pr ("added to a chain Kazanc, other: " + chainOrder (c));
            pump (6.0);
            edit (run, [] (PluginChain& ch) { ch.movePlugin (2, 0); });  pr ("moved to the top: " + chainOrder (c));                pump (3.0);
            edit (run, [] (PluginChain& ch) { ch.movePlugin (0, 1); });  pr ("moved behind the Kazanc: " + chainOrder (c));         pump (3.0);
            edit (run, [] (PluginChain& ch) { ch.movePlugin (2, 0); });  pr ("another element moved in front: " + chainOrder (c)); pump (3.0);
            edit (run, [] (PluginChain& ch) { ch.movePlugin (1, 2); });  pr ("moved to the end again: " + chainOrder (c));         pump (6.0);
            pr ("second Otomatik Kazanç refused: " + juce::String (c.addAutoGain() ? "NO (added)" : "yes"));
            pr ("    " + diagLine (*run.engine));
        }
        pr ("clicks while audio ran through 4 moves: " + juce::String (clicksInRun (false)));   // ends with Kazanc +6 after it
        pr ("| Chain | Input peaks | Output peaks | Aim | Gain shown in the row | Gain measured (out - input of the auto gain) | L = R | Clicks |");
        pr ("|---|---:|---:|---:|---:|---:|---:|---:|");
        speechRow ("Kazanç +3 -> Otomatik Kazanç (auto gain behind the manual gain)", -20.0,
                   [&addAuto] (EngineRun& r) { r.engine->getChain().addGain (3.0f); addAuto (r, true); }, N, -17.0);
        speechRow ("Otomatik Kazanç -> Kazanç +6 (manual gain after it)", -20.0,
                   [&addAuto, &edit] (EngineRun& r) { r.engine->getChain().addGain (6.0f); addAuto (r, true); edit (r, [] (PluginChain& ch) { ch.movePlugin (1, 0); }); },
                   N + 6.0, -20.0, false);

        pr ("");
        pr ("## Otomatik Kazanç: latency (the limiter's 3 ms look-ahead)");
        {
            // the chain's latency as the engine adds it to the shown latency (graph latency); the shown
            // total also contains the bridge's fill level, which moves a little from run to run
            voiceConfig (-20.0);
            int without = 0, with = 0, withOff = 0, reported = 0;
            { EngineRun run (480, 2.0); pump (1.0); without = run.engine->getGraph().getLatencySamples(); }
            {
                EngineRun run (480, 3.0);
                addAuto (run, true);
                pump (1.0);
                with = run.engine->getGraph().getLatencySamples();
                if (auto* node = run.engine->getGraph().getNodeForId (run.engine->getChain().entries()[0].node))
                    reported = node->getProcessor()->getLatencySamples();
                run.engine->getChain().setAutoGainEnabled (0, false);
                pump (1.0);
                withOff = run.engine->getGraph().getLatencySamples();
            }
            pr ("chain latency: empty " + juce::String (without) + " samples, with Otomatik Kazanç " + juce::String (with) + " samples (+"
                + juce::String ((with - without) / 48.0, 2) + " ms, expected +3.00), switched off " + juce::String (withOff)
                + " samples (unchanged); the element reports " + juce::String (reported) + " samples (expected 144 at 48 kHz)");
        }

        pr ("");
        pr ("## Otomatik Kazanç off: the manual Kazanç is unchanged (997 Hz, -20 dBFS in)");
        pr ("| Chain | Gain L / R | Expected | THD+N L / R | Clicks | Gaps |");
        pr ("|---|---:|---:|---:|---:|---:|");
        auto row = [] (const juce::String& what, const SineResult& r, double expectedDb)
        {
            pr ("| " + what + " | " + db (r.gainDbL, 3) + " / " + db (r.gainDbR, 3) + " | " + db (expectedDb, 3)
                + " | " + db (r.thdnL) + " / " + db (r.thdnR) + " | " + juce::String (r.clk.events) + " | " + juce::String (r.gaps.count) + " |");
        };
        const double amp = 0.1;
        row ("Kazanç +6 (reference)", measureSine (997.0, amp, 8.0, 480, 4.0, [] (EngineRun& r)
             { r.engine->getChain().addGain (6.0f); const AudioEngine::ChainEdit e (*r.engine); r.engine->rebuildGraph(); }, 2.0), 6.0);
        row ("Kazanç +6 -> Otomatik Kazanç (off)", measureSine (997.0, amp, 8.0, 480, 4.0, [&addAuto] (EngineRun& r)
             { r.engine->getChain().addGain (6.0f); addAuto (r, false); }, 2.0), 6.0);
        row ("Otomatik Kazanç (off) alone", measureSine (997.0, amp, 8.0, 480, 4.0, [&addAuto] (EngineRun& r) { addAuto (r, false); }), 0.0);

        pr ("");
        pr ("## Otomatik Kazanç: saved and restored (captureState -> config.xml -> applyState), position and on/off");
        for (bool on : { false, true })
        {
            voiceConfig (-20.0);
            MicVSTState saved;
            {
                EngineRun run (480, 2.0);
                run.engine->getChain().addGain (6.0f);
                addAuto (run, on);   // at the end: Kazanc, Otomatik Kazanc
                pump (0.5);
                saved = run.engine->captureState();
            }
            const auto xml = toValueTree (saved).createXml();
            const auto back = fromValueTree (juce::ValueTree::fromXml (*xml));
            EngineRun run (480, 2.0);
            MicVSTState st = back;
            st.inputDevice = "VMic"; st.outputDevice = "VCable"; st.bufferSize = 480;
            run.engine->applyState (st);
            pump (0.5);
            auto& c = run.engine->getChain();
            const int ai = autoGainIndex (c);
            pr ("saved Kazanc, Otomatik Kazanc (" + juce::String (on ? "on" : "off") + ") -> restored: " + chainOrder (c)
                + "; Otomatik Kazanç " + (ai >= 0 ? (c.isAutoGainEnabled (ai) ? "on" : "off") : "missing")
                + "; Kazanç " + (c.isGain (0) ? db (c.getGainDb (0), 1) : juce::String ("missing")));
        }

        pr ("");
        pr ("## Otomatik Kazanç: switched off and on during speech (voice -20 dBFS, 4 x off 2 s / on 2 s)");
        {
            voiceConfig (-20.0);
            AutoGainStage::Meters offState;
            {
                EngineRun run (480, 24.0);
                addAuto (run, true);
                pump (6.0);
                for (int k = 0; k < 4; ++k)
                {
                    run.engine->getChain().setAutoGainEnabled (0, false); pump (2.0);
                    if (k == 3) offState = meters (run);
                    run.engine->getChain().setAutoGainEnabled (0, true);  pump (2.0);
                }
                pr ("    " + diagLine (*run.engine));
            }
            pr ("clicks " + juce::String (clicksInRun (true)) + "; gain shown 2 s after switching off: " + db (offState.gainDb, 6));
        }

        pr ("");
        pr ("loudest output sample in the runs where Otomatik Kazanç is the last gain stage: " + db (loudest, 3) + "FS (must stay at or below "
            + db (C, 1) + "FS; the bridge's resampler after the chain may add a few thousandths of a dB)");
    }
}
#endif

#if MICVST_NEW_ENGINE
 #define NEWF(x) (x)
#else
 #define NEWF(x) 0
#endif
namespace
{
    //==============================================================================
    // Trace: time series of the bridge controller, the device counters and scheduling stalls of
    // the machine itself (one detector thread per CPU, SCHED_FIFO 95, 1 ms period), plus the
    // speed/THD+N of the recorded output - written as CSV for offline correlation.
   #if ! JUCE_WINDOWS
    void trace (double seconds, double freqHz, double load, const juce::String& prefix)
    {
        sineConfig (freqHz, 0.5, 48000);
        struct Stall { double t, lateMs; int cpu; };
        struct Sample { double t, corr, drift, buffered, safety; uint64_t under, resync, drops, skips, overflows; int xruns, lost, gaps, lateWakes; double backlog; };
        std::vector<Stall> stalls; std::mutex stallLock;
        std::vector<Sample> samples; samples.reserve ((size_t) (seconds * 210));
        std::atomic<bool> stop { false };

        auto detector = [&] (int cpu)
        {
            cpu_set_t set; CPU_ZERO (&set); CPU_SET (cpu, &set);
            pthread_setaffinity_np (pthread_self(), sizeof (set), &set);
            vdev::setFifo (95);
            double next = vdev::now() + 0.001;
            while (! stop.load())
            {
                vdev::sleepUntil (next);
                const double late = vdev::now() - next;
                if (late > 0.0015) { const std::lock_guard<std::mutex> g (stallLock); stalls.push_back ({ next, late * 1000.0, cpu }); }
                next = std::max (next + 0.001, vdev::now() + 0.0002);
            }
        };
        const int cpus = (int) std::thread::hardware_concurrency();
        std::vector<std::thread> detectors;
        for (int c = 0; c < cpus; ++c) detectors.emplace_back (detector, c);

        {
            EngineRun run (480, seconds);
            if (load > 0.0) run.add (testDesc ("TestLoad", load));
            auto& e = *run.engine;
            std::thread sampler ([&]
            {
                vdev::setFifo (75);
                double next = vdev::now();
                while (! stop.load())
                {
                    next += 0.005;
                    vdev::sleepUntil (next);
                    const auto d = e.getDiagnostics();
                    Sample s { vdev::now(), d.bridge.correctionPpm, d.bridge.driftPpm, d.bridge.bufferedMs, d.bridge.safetyMs,
                               d.bridge.underruns, d.bridge.resyncs, d.bridge.droppedBlocks, NEWF (d.inputSkips), NEWF (d.inputQueueOverflows),
                               d.inputDeviceXruns, vdev::stats().lostPackets.load(), vdev::recorder().deviceGaps.load(),
                               vdev::stats().captureLateWakes.load(), (double) NEWF (d.backlogMs) };
                    if (samples.size() < samples.capacity()) samples.push_back (s);
                }
            });
            pump (seconds);
            stop = true;
            sampler.join();
            pr ("  " + diagLine (e));
        }
        stop = true;
        for (auto& t : detectors) t.join();

        const auto dir = outDir();
        dir.createDirectory();
        {
            juce::String csv ("t,corr,drift,buffered,safety,underruns,resyncs,drops,skips,overflows,xruns,lost,gaps,latewakes,backlog\n");
            for (auto& s : samples)
                csv << juce::String (s.t, 4) << "," << juce::String (s.corr, 1) << "," << juce::String (s.drift, 1) << "," << juce::String (s.buffered, 3)
                    << "," << juce::String (s.safety, 1) << "," << (int) s.under << "," << (int) s.resync << "," << (int) s.drops << "," << (int) s.skips
                    << "," << (int) s.overflows << "," << s.xruns << "," << s.lost << "," << s.gaps << "," << s.lateWakes << "," << juce::String (s.backlog, 2) << "\n";
            dir.getChildFile (prefix + "_diag.csv").replaceWithText (csv);
        }
        {
            juce::String csv ("t,cpu,late_ms\n");
            for (auto& s : stalls) csv << juce::String (s.t, 4) << "," << s.cpu << "," << juce::String (s.lateMs, 3) << "\n";
            dir.getChildFile (prefix + "_stalls.csv").replaceWithText (csv);
        }
        {
            const auto& L = vdev::recorder().left;
            const double fs = 48000.0, cps = freqHz / fs, t0 = vdev::recorder().outputStart.load();
            juce::String csv ("t,freqdev_ppm,thdn_db,zeros\n");
            const size_t from = firstAudio (L) + 4800;
            std::vector<double> f;
            for (size_t s0 = from; s0 + 4096 < L.size(); s0 += 2048)
                f.push_back ((fitSine (L, s0, 4096, cps).freq / cps - 1.0) * 1e6);
            std::vector<double> sorted = f; std::sort (sorted.begin(), sorted.end());
            const double med = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
            size_t k = 0;
            for (size_t s0 = from; s0 + 4096 < L.size(); s0 += 2048, ++k)
            {
                int zeros = 0; for (size_t i = s0; i < s0 + 2048; ++i) if (L[i] == 0.0f) ++zeros;
                csv << juce::String (t0 + (double) (s0 + 1024) / fs, 4) << "," << juce::String (f[k] - med, 1) << ","
                    << juce::String (fitSine (L, s0, 2048, cps).residualDb, 1) << "," << zeros << "\n";
            }
            dir.getChildFile (prefix + "_win.csv").replaceWithText (csv);
            pr ("  trace written: " + dir.getChildFile (prefix + "_*.csv").getFullPathName() + " (" + juce::String ((int) samples.size()) + " samples, "
                + juce::String ((int) stalls.size()) + " stalls > 1.5 ms)");
        }
    }
   #endif
}

int main (int argc, char** argv)
{
   #if ! JUCE_WINDOWS
    mainTid = (pid_t) syscall (SYS_gettid);
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    struct QuietLogger final : juce::Logger
    {
        void logMessage (const juce::String& m) override
        {
            ++logLineCount;
            {
                const std::lock_guard<std::mutex> g (logLinesMutex);
                if (keptLogLines.size() < 20000) keptLogLines.add (m);
            }
            // with the cable-timeline time when a recording runs (to line log lines up with findings)
            const double t0 = vdev::recorder().outputStart.load();
            if (envSet ("ETEST_LOG"))
            {
                if (t0 > 0.0) std::printf ("  [log %.3f s] %s\n", juce::Time::getMillisecondCounterHiRes() * 0.001 - t0, m.toRawUTF8());
                else          std::printf ("  [log] %s\n", m.toRawUTF8());
                std::fflush (stdout);
            }
        }
    } logger;
    juce::Logger::setCurrentLogger (&logger);

    const juce::String mode (argc > 1 ? argv[1] : "quality");
    pr ("# MicVST engine test (" + juce::String (MICVST_ENGINE_LABEL) + ") - " + mode);
    if (mode == "quality") quality();
    else if (mode == "latency") latency();
    else if (mode == "flutter")
    {
        // Speed stability of the bridge: fitted frequency in 4096-sample windows -> deviation
        // from the median in ppm (= flutter); THD+N in 2048-sample windows (phase wander negligible).
        for (double f : { 997.0, 10000.0 })
        {
            sineConfig (f, 0.5, 48000);
            std::vector<double> corr;
            {
                EngineRun run (480, 40.0);
                for (int i = 0; i < 400; ++i)
                {
                    pump (0.1);
                    if (i > 100) corr.push_back (run.engine->getDiagnostics().bridge.correctionPpm);
                }
            }
            const auto& L = vdev::recorder().left;
            std::vector<double> ppm, thd;
            const double cps = f / 48000.0;
            for (size_t s0 = 12 * 48000; s0 + 4096 < L.size(); s0 += 4096)
            {
                const auto fit = fitSine (L, s0, 4096, cps);
                ppm.push_back ((fit.freq / cps - 1.0) * 1e6);
                thd.push_back (fitSine (L, s0, 2048, cps).residualDb);
            }
            std::vector<double> sorted = ppm; std::sort (sorted.begin(), sorted.end());
            const double med = sorted[sorted.size() / 2];
            double rms = 0, peak = 0; for (auto v : ppm) { rms += (v - med) * (v - med); peak = std::max (peak, std::abs (v - med)); }
            rms = std::sqrt (rms / (double) ppm.size());
            std::sort (thd.begin(), thd.end());
            double cm = 0, cs = 0; for (auto c : corr) cm += c; cm /= (double) corr.size(); for (auto c : corr) cs += (c - cm) * (c - cm); cs = std::sqrt (cs / (double) corr.size());
            pr ("tone " + juce::String (f, 0) + " Hz: speed deviation (flutter) RMS " + juce::String (rms, 2) + " ppm, peak " + juce::String (peak, 1)
                + " ppm; THD+N (2048-sample windows) median " + db (thd[thd.size() / 2]) + ", worst " + db (thd.back())
                + "; bridge correction mean " + juce::String (cm, 1) + " ppm, std " + juce::String (cs, 1) + " ppm");
        }
    }
    else if (mode == "mono")
    {
        vdev::config().micChannels = 1;
        const auto r = measureSine (997.0, 0.5, 8.0, 480);
        pr ("mono: gain L " + db (r.gainDbL, 4) + " R " + db (r.gainDbR, 4) + " THD+N " + db (r.thdnL) + " windows " + juce::String (r.windows)
            + " bad " + juce::String (r.badWindows) + " clicks " + juce::String (r.clk.events));
        const auto& L = vdev::recorder().left;
        for (size_t b = 5 * 48000; b < 5 * 48000 + 480 * 12; b += 480)
        {
            float mx = 0; for (size_t i = b; i < b + 480; ++i) mx = std::max (mx, std::abs (L[i]));
            std::printf ("  block at %zu: peak %.4f first %.4f\n", b, mx, L[b]);
        }
    }
    else if (mode == "buffers") buffers();
    else if (mode == "edits") edits();
    else if (mode == "vst3") vst3();
   #if MICVST_NEW_ENGINE
    else if (mode == "gain") gainTest (argc > 2 ? std::atof (argv[2]) : 20.0);   // seconds per live-slider run
    else if (mode == "autogain") autoGainTest();
   #endif
    else if (mode == "long") longRun (argc > 2 ? std::atof (argv[2]) : 10.0, false);
    else if (mode == "chaos") longRun (argc > 2 ? std::atof (argv[2]) : 10.0, true);
   #if MICVST_RT_PROBE
    else if (mode == "rtprobe")
    {
        // Allocations and blocking calls inside the audio sections (capture callback, plug-in
        // block, output callback) while audio runs. Armed after a 2 s warm-up.
        sineConfig (997.0, 0.5, 48000);
        auto report = [] (const juce::String& what)
        {
            juce::String line ("| " + what + " |");
            for (int k = 0; k < (int) rtprobe::Kind::count; ++k)
                line << " " << (int) rtprobe::count ((rtprobe::Kind) k) << " |";
            line << " " << (int) rtprobe::contendedLocks() << " (max " << juce::String (rtprobe::maxLockWaitUs(), 0) << " us) |";
            pr (line);
        };
        juce::String head ("| Scenario |"), sep ("|---|");
        for (int k = 0; k < (int) rtprobe::Kind::count; ++k) { head << " " << rtprobe::name ((rtprobe::Kind) k) << " |"; sep << "---:|"; }
        head << " locks that waited |"; sep << "---:|";
        pr ("## Realtime probe: calls inside the audio sections (20 s each, buffer 480)");
        pr (head); pr (sep);
        auto scenario = [&] (const juce::String& what, std::function<void (EngineRun&)> setup, std::function<void (EngineRun&)> during)
        {
            EngineRun run (480, 30.0);
            setup (run);
            pump (2.0);
            rtprobe::arm();
            during (run);
            rtprobe::disarm();
            report (what);
            if (envSet ("PROBE_STACKS")) rtprobe::printSamples (2);
        };
        scenario ("empty chain", [] (EngineRun&) {}, [] (EngineRun&) { pump (20.0); });
        scenario ("gain + delay 240 + load 30 %", [] (EngineRun& r) { r.add (testDesc ("TestGain", 0.5)); r.add (testDesc ("TestDelay", 240)); r.add (testDesc ("TestLoad", 0.3)); },
                  [] (EngineRun&) { pump (20.0); });
        scenario ("edits during playback (bypass, add, remove, move)", [] (EngineRun& r) { r.add (testDesc ("TestGain", 0.5)); },
                  [] (EngineRun& r)
                  {
                      auto& e = *r.engine; juce::String err;
                      for (int i = 0; i < 6; ++i)
                      {
                          { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (0, true); e.rebuildGraph(); } pump (0.5);
                          { const AudioEngine::ChainEdit ce (e); e.getChain().setBypass (0, false); e.rebuildGraph(); } pump (0.5);
                          e.getChain().addPlugin (e.getFormatManager(), testDesc ("TestDelay", 100), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
                          { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); } pump (0.5);
                          { const AudioEngine::ChainEdit ce (e); e.getChain().movePlugin (1, 0); e.rebuildGraph(); } pump (0.5);
                          { const AudioEngine::ChainEdit ce (e); e.getChain().removePlugin (0); e.rebuildGraph(); } pump (0.5);
                      }
                  });
        scenario ("Kazanç: slider moved during playback (400 values)",
                  [] (EngineRun& r) { r.engine->getChain().addGain (0.0f); const AudioEngine::ChainEdit ce (*r.engine); r.engine->rebuildGraph(); },
                  [] (EngineRun& r)
                  {
                      std::mt19937 rng (5); std::uniform_real_distribution<float> v (GainStage::minDb, GainStage::maxDb);
                      for (int i = 0; i < 400; ++i) { r.engine->getChain().setGainDb (0, v (rng)); pump (0.05); }
                  });
        scenario ("safety buffer changes (refill, trim)", [] (EngineRun&) {},
                  [] (EngineRun& r) { for (int i = 0; i < 4; ++i) { r.engine->setSafetyBufferMs (40); pump (2.5); r.engine->setSafetyBufferMs (10); pump (2.5); } });
        {
            juce::VST3PluginFormat fmt;
            juce::OwnedArray<juce::PluginDescription> types;
            fmt.findAllTypesForFile (types, vst3Bundle());
            juce::Array<juce::PluginDescription> chain;
            for (auto* t : types)
                for (auto w : { "Compressor Stereo", "Parametric Equalizer x8 Stereo", "Limiter Stereo", "Gate Stereo" })
                    if (t->name == w) chain.add (*t);
            scenario ("LSP VST3: compressor, EQ x8, limiter, gate", [chain] (EngineRun& r)
                      {
                          auto& e = *r.engine; e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                          for (auto& d : chain) { juce::String err; e.getChain().addPlugin (e.getFormatManager(), d, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err); }
                          const AudioEngine::ChainEdit ce (e); e.rebuildGraph();
                      },
                      [] (EngineRun&) { pump (20.0); });
        }
    }
   #endif
   #if MICVST_NEW_ENGINE
    else if (mode == "msgblock")
    {
        // The message (UI) thread blocked for a while - with or without a device change just before
        // (a plug-in editor or a slow system can do that). Every exact-zero run on the cable is listed
        // with its time relative to the change and to the moment the thread was free again.
        const bool withDevice = argc > 2 && juce::String (argv[2]).getIntValue() != 0;
        const int blockMs = argc > 3 ? juce::String (argv[3]).getIntValue() : 3000;
        const int rounds = argc > 4 ? juce::String (argv[4]).getIntValue() : 9;
        auto& cfg = vdev::config();
        sineConfig (997.0, 0.5, 48000);
        const double fs = cfg.cableRate;
        pr ("## Message thread blocked " + juce::String (blockMs) + " ms" + (withDevice ? " right after a device buffer change" : " (no device change)")
            + ", " + juce::String (rounds) + " rounds; chain gain 0.5 + delay 240 smp + load 30 %, 997 Hz");
        struct Ev { double change, changed, freed; };
        std::vector<Ev> evs;
        {
            EngineRun run (480, rounds * (6.0 + blockMs / 1000.0) + 10.0);
            auto& e = *run.engine;
            run.add (testDesc ("TestGain", 0.5));
            run.add (testDesc ("TestDelay", 240));
            run.add (testDesc ("TestLoad", 0.3));
            pump (3.0);
            static const int sizes[] = { 960, 256, 480 };
            for (int i = 0; i < rounds; ++i)
            {
                Ev ev {};
                ev.change = juce::Time::getMillisecondCounterHiRes() * 0.001;
                if (withDevice) { e.setDeviceConfig ("VMic", "VCable", 48000.0, sizes[i % 3]); raiseEngineThreads(); }
                ev.changed = juce::Time::getMillisecondCounterHiRes() * 0.001;
                juce::Thread::sleep (blockMs);   // the message thread does not run
                ev.freed = juce::Time::getMillisecondCounterHiRes() * 0.001;
                evs.push_back (ev);
                pump (6.0);
            }
            pr ("  " + diagLine (e) + ", graph busy " + juce::String ((int) e.getDiagnostics().graphBusyBlocks)
                + ", chain silence " + juce::String ((int) e.getDiagnostics().chainSilentBlocks) + ", bridge starts " + juce::String ((int) e.getDiagnostics().bridge.starts));
        }
        const auto& L = vdev::recorder().left;
        const auto& rec = vdev::recorder();
        const double t0 = rec.outputStart.load();
        const size_t from = firstAudio (L) + 4800;
        int zeros = 0, listed = 0;
        for (size_t i = from; i < L.size(); ++i)
        {
            if (L[i] == 0.0f) { ++zeros; continue; }
            if (zeros > 48)
            {
                const size_t s0 = i - (size_t) zeros;
                const double at = t0 + (double) s0 / fs;
                juce::String rel = "before the first round";
                for (auto& ev : evs)
                    if (ev.change <= at + 0.5) rel = "change +" + juce::String (at - ev.change, 3) + " s, changed +" + juce::String (at - ev.changed, 3)
                                                     + " s, thread free again " + juce::String (at - ev.freed, 3) + " s";
                juce::String cause;
                for (int g = 0; g < rec.gapCount.load(); ++g)
                    if ((int64_t) s0 >= rec.gapEvents[(size_t) g].first - 960 && (int64_t) s0 <= rec.gapEvents[(size_t) g].first + rec.gapEvents[(size_t) g].second + 2880) cause = " [cable late]";
                pr ("  zeros at " + juce::String ((double) s0 / fs, 3) + " s: " + juce::String (1000.0 * zeros / fs, 1) + " ms; " + rel + cause);
                ++listed;
            }
            zeros = 0;
        }
        pr ("  zero runs > 1 ms: " + juce::String (listed) + "; cable late " + juce::String (rec.gapCount.load()) + " times; mic packets lost " + juce::String (vdev::stats().lostCount.load()));
    }
    else if (mode == "faults")
    {
        // Faulty plug-ins while audio plays. Output analysed per phase on the cable timeline.
        auto analyse = [] (double fromT, double toT, const juce::String& what)
        {
            const auto& L = vdev::recorder().left;
            const double fs = 48000.0, t0 = vdev::recorder().outputStart.load();
            const auto avail = (size_t) vdev::recorder().available.load (std::memory_order_acquire);   // the cable still records
            const auto a = (size_t) std::max (0.0, (fromT - t0) * fs), b = std::min (avail, (size_t) std::max (0.0, (toT - t0) * fs));
            bool finite = true; float peak = 0; size_t zeros = 0;
            for (size_t i = a; i < b; ++i) { finite = finite && std::isfinite (L[i]); peak = std::max (peak, std::abs (L[i])); if (L[i] == 0.0f) ++zeros; }
            pr ("| " + what + " | " + (finite ? juce::String ("yes") : juce::String ("NO")) + " | " + juce::String (peak, 3) + " | "
                + juce::String (b > a ? 100.0 * (double) zeros / (double) (b - a) : 0.0, 1) + " % |");
        };
        auto now = [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; };
        std::vector<std::pair<double, juce::String>> phases;   // start time (s, now() clock) and name: where the clicks are
        auto phase = [&] (const juce::String& what) { phases.push_back ({ now(), what }); };
        pr ("## Faulty plug-ins during playback (997 Hz -6 dBFS)");
        pr ("| Phase | Output finite | Peak | Silent samples |");
        pr ("|---|---|---:|---:|");
        sineConfig (997.0, 0.5, 48000);
        {
            EngineRun run (480, 60.0);
            auto& e = *run.engine;
            juce::String err;
            auto addP = [&] (const char* id, double p) { e.getChain().addPlugin (e.getFormatManager(), testDesc (id, p), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
                                                         const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); };
            auto removeLast = [&] { const AudioEngine::ChainEdit ce (e); e.getChain().removePlugin ((int) e.getChain().entries().size() - 1); e.rebuildGraph(); };
            pump (3.0);
            double t = now(); pump (2.0); analyse (t, now(), "normal");

            phase ("NaN plug-in added"); t = now(); addP ("TestNaN", 50); pump (3.0); analyse (t + 0.8, now(), "plug-in outputs NaN from block 50 on");
            auto d = e.getDiagnostics(); pr ("  invalid blocks counted: " + juce::String ((int) d.invalidBlocks));
            phase ("NaN plug-in removed"); removeLast(); t = now(); pump (2.0); analyse (t + 0.2, now(), "NaN plug-in removed");

            phase ("huge plug-in added"); t = now(); addP ("TestHuge", 50); pump (3.0); analyse (t + 0.8, now(), "plug-in outputs 1e30 (unstable)");
            phase ("huge plug-in removed"); removeLast(); t = now(); pump (2.0); analyse (t + 0.2, now(), "huge plug-in removed");

            phase ("throwing plug-in added"); t = now(); addP ("TestThrow", 100); pump (3.0); analyse (t + 0.3, now(), "plug-in throws an exception once");
            removeLast(); pump (1.0);

            const auto before = e.getDiagnostics();
            phase ("hanging plug-in added"); t = now(); addP ("TestHang", 100); pump (6.0);
            const auto after = e.getDiagnostics();
            analyse (t + 4.0, now(), "after a 1.5 s hang inside the plug-in (last 2 s)");
            pr ("  hang: underruns +" + juce::String ((int) (after.bridge.underruns - before.bridge.underruns)) + ", queue overflows +"
                + juce::String ((int) (after.inputQueueOverflows - before.inputQueueOverflows)) + ", skips +" + juce::String ((int) (after.inputSkips - before.inputSkips)));
            removeLast(); pump (1.0);

            // a plug-in whose preparation takes 400 ms, added during playback
            phase ("slow-prepare plug-in added"); t = now(); addP ("TestSlowPrepare", 400);
            const double addSeconds = now() - t;
            pump (2.0);
            {
                const auto& L = vdev::recorder().left; const double fs = 48000.0, t0 = vdev::recorder().outputStart.load();
                const auto a = (size_t) ((t - t0) * fs), b = std::min ((size_t) vdev::recorder().available.load (std::memory_order_acquire), (size_t) ((now() - t0) * fs));
                int zeros = 0, longest = 0; for (size_t i = a; i < b; ++i) { if (std::abs (L[i]) < 1e-6f) longest = std::max (longest, ++zeros); else zeros = 0; }
                pr ("  slow plug-in (prepare 400 ms) added: the add call took " + juce::String (addSeconds * 1000.0, 0) + " ms, longest silence "
                    + juce::String (1000.0 * longest / fs, 1) + " ms");
            }
            removeLast(); pump (1.0);

            // unknown plug-in: load fails, nothing changes
            const bool ok = e.getChain().addPlugin (e.getFormatManager(), testDesc ("TestMissing", 0), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
            pr ("  plug-in that cannot be loaded: addPlugin " + juce::String (ok ? "succeeded (?)" : "failed as expected") + " (\"" + err + "\"), chain size "
                + juce::String ((int) e.getChain().entries().size()));
            t = now(); pump (2.0); analyse (t, now(), "after the failed load");
            pr ("  " + diagLine (e));
        }
        const auto& L = vdev::recorder().left;
        const size_t start = firstAudio (L) + 4800;
        std::vector<size_t> where;
        pr ("whole run: click events " + juce::String (clicks (L, start, L.size(), 0.5, 997.0 / 48000.0, 480, &where).events));
        const double t0 = vdev::recorder().outputStart.load();
        for (auto i : where)
        {
            const double at = t0 + (double) i / 48000.0;
            juce::String in = "before the first phase";
            for (auto& [ts, what] : phases) if (ts <= at) in = what + " +" + juce::String (at - ts, 3) + " s";
            // the test machine's own stalls (not MicVST): the cable played silence / a mic packet was lost
            juce::String machine;
            const auto& rec = vdev::recorder();
            for (int g = 0; g < rec.gapCount.load(); ++g)
                if ((int64_t) i >= rec.gapEvents[(size_t) g].first - 960 && (int64_t) i <= rec.gapEvents[(size_t) g].first + rec.gapEvents[(size_t) g].second + 2880)
                    machine = " [cable late: test machine stall]";
            for (int k = 0; k < vdev::stats().lostCount.load(); ++k)
            {
                const double lt = vdev::stats().lostTimes[(size_t) k] - t0 + 0.06;
                if (std::abs ((double) i / 48000.0 - lt) < 0.25) machine = " [mic packet lost: test machine stall]";
            }
            pr ("  click at " + juce::String ((double) i / 48000.0, 3) + " s (cable timeline): " + in + machine);
        }
    }
   #endif
    else if (mode == "openfail")
    {
        // A device that is listed but cannot be opened (another program holds it exclusively, a
        // driver error): how often does MicVST retry, how much does it log and how often does it ask
        // the app to save config.xml (onDeviceChanged), does it come back by itself once the device
        // can be opened again?
        sineConfig (997.0, 0.5, 48000);
        auto openNow = [] (AudioEngine& e, bool input)
        {
            auto* d = (input ? e.getInputDeviceManager() : e.getOutputDeviceManager()).getCurrentAudioDevice();
            return d != nullptr && d->isOpen();
        };
        for (bool input : { false, true })
        {
            const juce::String which (input ? "microphone" : "cable");
            int changed = 0;
            EngineRun run (480, 70.0);
            auto& e = *run.engine;
            e.onDeviceChanged = [&changed] { ++changed; };
            pump (2.0);
            (input ? vdev::presence().micOpenFails : vdev::presence().cableOpenFails) = true;
            if (input) vdev::setPresent (false, true); else vdev::setPresent (true, false);
            pump (1.0);
            const int log0 = logLineCount.load(), ch0 = changed, at0 = vdev::presence().openAttempts.load();
            { const std::lock_guard<std::mutex> g (logLinesMutex); keptLogLines.clear(); }
            vdev::setPresent (true, true);   // listed again - but it cannot be opened
            pump (30.0);
            const int log1 = logLineCount.load(), ch1 = changed, at1 = vdev::presence().openAttempts.load();
            pr ("  " + which + " listed but cannot be opened, 30 s: open attempts " + juce::String (at1 - at0) + ", log lines " + juce::String (log1 - log0)
                + ", device-change callbacks (MicVST saves config.xml on each) " + juce::String (ch1 - ch0) + ", device open now: " + (openNow (e, input) ? "yes" : "no"));
            {
                const std::lock_guard<std::mutex> g (logLinesMutex);
                std::map<juce::String, int> kinds;
                for (auto& l : keptLogLines) ++kinds[l.substring (0, 48)];
                for (auto& k : kinds) pr ("      " + juce::String (k.second) + " x '" + k.first + "...'");
            }
            (input ? vdev::presence().micOpenFails : vdev::presence().cableOpenFails) = false;
            const double t0 = juce::Time::getMillisecondCounterHiRes() * 0.001;
            double back = -1.0;
            while (juce::Time::getMillisecondCounterHiRes() * 0.001 - t0 < 35.0)
            {
                pump (0.1);
                if (openNow (e, input)) { back = juce::Time::getMillisecondCounterHiRes() * 0.001 - t0; break; }
            }
            pump (1.0);
            pr ("  " + which + " can be opened again: open after " + (back >= 0.0 ? juce::String (back, 1) + " s" : juce::String ("NOT within 35 s"))
                + "; " + diagLine (e));
        }
        vdev::presence().micOpenFails = false; vdev::presence().cableOpenFails = false;
        vdev::setPresent (true, true);
    }
    else if (mode == "hotplug")
    {
        // Microphone and cable unplugged and plugged in again while MicVST runs. Which device does
        // the engine use meanwhile, what reaches the cable (997 Hz = our mic, 300 Hz = the built-in
        // "VMic2"), does anything reach the speakers, and does it come back by itself?
        sineConfig (997.0, 0.5, 48000);
        std::vector<std::pair<double, juce::String>> marks;
        auto now = [] { return juce::Time::getMillisecondCounterHiRes() * 0.001; };
        auto names = [] (AudioEngine& e)
        {
            auto* in = e.getInputDeviceManager().getCurrentAudioDevice();
            auto* out = e.getOutputDeviceManager().getCurrentAudioDevice();
            return "input " + (in != nullptr ? in->getName() : juce::String ("(none)")) + ", output " + (out != nullptr ? out->getName() : juce::String ("(none)"));
        };
        {
            EngineRun run (480, 70.0);
            auto& e = *run.engine;
            pump (3.0);
            marks.push_back ({ now(), "start" });
            pr ("  start: " + names (e));
            vdev::setPresent (false, true); marks.push_back ({ now(), "mic unplugged" }); pump (5.0); raiseEngineThreads();
            pr ("  mic unplugged (5 s): " + names (e));
            vdev::setPresent (true, true);  marks.push_back ({ now(), "mic plugged in again" }); pump (6.0); raiseEngineThreads();
            pr ("  mic back (6 s): " + names (e));
            vdev::setPresent (true, false); marks.push_back ({ now(), "cable unplugged" }); pump (5.0); raiseEngineThreads();
            pr ("  cable unplugged (5 s): " + names (e) + ", speakers peak " + juce::String (vdev::presence().speakerPeak.load(), 3)
                + " (" + juce::String ((double) vdev::presence().speakerSamples.load() / 48000.0, 1) + " s of audio)");
            vdev::setPresent (true, true);  marks.push_back ({ now(), "cable plugged in again" }); pump (6.0); raiseEngineThreads();
            pr ("  cable back (6 s): " + names (e));
            const auto w0 = e.getDiagnostics().watchdogRestarts;
            vdev::presence().micFrozen = true;  marks.push_back ({ now(), "mic driver hangs (no callbacks)" }); pump (5.0); raiseEngineThreads();
            vdev::presence().micFrozen = false; marks.push_back ({ now(), "mic driver responds again" }); pump (5.0); raiseEngineThreads();
            vdev::presence().cableFrozen = true;  marks.push_back ({ now(), "cable driver hangs" }); pump (5.0); raiseEngineThreads();
            vdev::presence().cableFrozen = false; marks.push_back ({ now(), "cable driver responds again" }); pump (6.0); raiseEngineThreads();
            pr ("  after the hangs: " + names (e) + ", watchdog reopened a device " + juce::String ((int) (e.getDiagnostics().watchdogRestarts - w0)) + " times");
            marks.push_back ({ now(), "end" });
            pr ("  " + diagLine (e));
        }
        const auto& L = vdev::recorder().left;
        const double fs = 48000.0, t0 = vdev::recorder().outputStart.load();
        pr ("| Second (cable timeline) | 997 Hz (our mic) | 300 Hz (built-in mic) | Event |");
        pr ("|---:|---:|---:|---|");
        size_t mi = 0;
        for (double t = marks.front().first - 1.0; t < marks.back().first; t += 1.0)
        {
            const auto a = (size_t) std::max (0.0, (t - t0) * fs);
            if (a + 24000 >= L.size()) break;
            const auto f1 = fit3 (L.data() + a, 24000, 997.0 / fs), f2 = fit3 (L.data() + a, 24000, 300.0 / fs);
            juce::String ev;
            while (mi < marks.size() && marks[mi].first <= t + 1.0) { ev << marks[mi].second << " "; ++mi; }
            pr ("| " + juce::String (t - marks.front().first, 0) + " | " + juce::String (f1.amp, 3) + " | " + juce::String (f2.amp, 3) + " | " + ev + "|");
        }
        vdev::setPresent (true, true);
    }
    else if (mode == "dump")   // dump <name> <signal> <level dBFS> <seconds> <chain> [micChannels] [buffer]
    {
        // Runs one scenario and writes the exact input and the recorded cable output (float32,
        // interleaved L/R) plus timing metadata for the offline analysis (analyse_dump.py).
        const juce::String name (argc > 2 ? argv[2] : "x"), signal (argc > 3 ? argv[3] : "noise"), chain (argc > 6 ? argv[6] : "empty");
        const double levelDb = argc > 4 ? std::atof (argv[4]) : -12.0, seconds = argc > 5 ? std::atof (argv[5]) : 20.0;
        const int micChannels = argc > 7 ? std::atoi (argv[7]) : 2, buffer = argc > 8 ? std::atoi (argv[8]) : 480;
        auto& cfg = vdev::config();
        cfg.micChannels = micChannels;
        if (argc > 10) { cfg.micRate = std::atof (argv[9]); cfg.cableRate = std::atof (argv[10]); }
        const double fs = cfg.micRate;
        const size_t len = (size_t) ((seconds + 30.0) * fs);   // loading real plug-ins can take seconds before the timed part starts
        auto in = std::make_shared<std::array<std::vector<float>, 2>>();
        (*in)[0].assign (len, 0.0f); (*in)[1].assign (len, 0.0f);
        std::mt19937 rng (12345);
        std::normal_distribution<double> gauss (0.0, 1.0);
        const double amp = juce::Decibels::decibelsToGain (levelDb);

        auto lowpass = [&] (std::vector<float>& x, double cutHz)
        {
            const int taps = 127; std::vector<double> h ((size_t) taps); double sum = 0;
            for (int i = 0; i < taps; ++i)
            {
                const double t = i - (taps - 1) / 2.0, w = 0.42 - 0.5 * std::cos (twoPi * i / (taps - 1)) + 0.08 * std::cos (2 * twoPi * i / (taps - 1));
                h[(size_t) i] = (t == 0 ? 2 * cutHz / fs : std::sin (twoPi * cutHz / fs * t) / (juce::MathConstants<double>::pi * t)) * w; sum += h[(size_t) i];
            }
            std::vector<float> y (x.size(), 0.0f);
            for (size_t n = (size_t) taps; n < x.size(); ++n) { double a = 0; for (int i = 0; i < taps; ++i) a += h[(size_t) i] / sum * x[n - (size_t) i]; y[n] = (float) a; }
            x.swap (y);
        };
        auto normalisePeak = [&] (std::vector<float>& x) { float pk = 0; for (auto v : x) pk = std::max (pk, std::abs (v)); if (pk > 0) for (auto& v : x) v = (float) (v / pk * amp); };

        if (signal == "noise")   // band-limited (18 kHz) Gaussian noise, independent L/R, RMS = level - 10 dB
        {
            for (int c = 0; c < 2; ++c)
            {
                auto& x = (*in)[(size_t) c];
                for (auto& v : x) v = (float) gauss (rng);
                lowpass (x, 18000.0);
                double rms = 0; for (auto v : x) rms += (double) v * v; rms = std::sqrt (rms / (double) x.size());
                for (auto& v : x) v = (float) (v / rms * amp * 0.316);
            }
        }
        else if (signal == "multitone")   // 24 log-spaced tones 40 Hz - 18 kHz, Schroeder phases, same on L and R
        {
            for (size_t n = 0; n < len; ++n)
            {
                double v = 0;
                for (int k = 0; k < 24; ++k)
                {
                    const double f = std::round (40.0 * std::pow (18000.0 / 40.0, k / 23.0) / 1.5) * 1.5 + 0.37;   // off the bin grid
                    v += std::sin (twoPi * f / fs * (double) n + juce::MathConstants<double>::pi * k * (k + 1) / 24.0);
                }
                (*in)[0][n] = (*in)[1][n] = (float) v;
            }
            normalisePeak ((*in)[0]); (*in)[1] = (*in)[0];
        }
        else if (signal == "sine")
        {
            for (size_t n = 0; n < len; ++n) (*in)[0][n] = (*in)[1][n] = (float) (amp * std::sin (twoPi * 997.0 / fs * (double) n));
        }
        else if (signal == "voice")   // synthetic speech: glottal pulses (f0 95-230 Hz gliding) through moving formants, fricatives, pauses
        {
            const double formants[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
            double phase = 0, y1[3][2] = {}, env = 0;
            std::uniform_real_distribution<double> u (0.0, 1.0);
            for (size_t n = 0; n < len; ++n)
            {
                const double t = (double) n / fs;
                const int syll = (int) (t / 0.18);
                const bool pause = (syll % 9) == 8, fric = (syll % 7) == 3;
                const double f0 = 140.0 + 70.0 * std::sin (twoPi * 0.31 * t) + 20.0 * std::sin (twoPi * 2.3 * t);
                phase += f0 / fs;
                double src = 0.0;
                if (phase >= 1.0) { phase -= 1.0; src = 1.0; }
                if (fric) src = 0.3 * (u (rng) - 0.5);
                const auto& F = formants[syll % 5];
                double v = 0;
                for (int k = 0; k < 3; ++k)   // resonators
                {
                    const double r = std::exp (-juce::MathConstants<double>::pi * 90.0 / fs), w = twoPi * F[k] / fs;
                    const double yk = src * (1 - r) + 2 * r * std::cos (w) * y1[k][0] - r * r * y1[k][1];
                    y1[k][1] = y1[k][0]; y1[k][0] = yk; v += yk / (k + 1);
                }
                env += ((pause ? 0.0 : 1.0) - env) * 0.002;
                (*in)[0][n] = (*in)[1][n] = (float) (v * env);
            }
            normalisePeak ((*in)[0]); (*in)[1] = (*in)[0];
        }
        else if (signal == "music")   // decaying harmonic chords + kick and hi-hat transients, stereo
        {
            const double chords[4][3] = { { 261.63, 329.63, 392.0 }, { 220.0, 261.63, 329.63 }, { 174.61, 220.0, 261.63 }, { 196.0, 246.94, 293.66 } };
            std::uniform_real_distribution<double> u (-1.0, 1.0);
            for (size_t n = 0; n < len; ++n)
            {
                const double t = (double) n / fs, tb = std::fmod (t, 0.5), tc = std::fmod (t, 2.0);
                const auto& c = chords[(int) (t / 2.0) % 4];
                double l = 0, r = 0;
                for (int k = 0; k < 3; ++k)
                    for (int h = 1; h <= 6; ++h)
                    {
                        const double a = std::exp (-tc * (1.0 + h)) / h, v = a * std::sin (twoPi * c[k] * h * t);
                        l += v * (k == 0 ? 1.0 : 0.6); r += v * (k == 2 ? 1.0 : 0.6);
                    }
                const double kick = std::exp (-tb * 30.0) * std::sin (twoPi * (50.0 + 80.0 * std::exp (-tb * 40.0)) * tb);
                const double hat = std::fmod (t + 0.25, 0.5) < 0.03 ? std::exp (-std::fmod (t + 0.25, 0.5) * 150.0) * u (rng) : 0.0;
                (*in)[0][n] = (float) (l * 0.3 + kick + 0.3 * hat); (*in)[1][n] = (float) (r * 0.3 + kick + 0.3 * hat);
            }
            float pk = 0; for (int c = 0; c < 2; ++c) for (auto v : (*in)[(size_t) c]) pk = std::max (pk, std::abs (v));
            for (int c = 0; c < 2; ++c) for (auto& v : (*in)[(size_t) c]) v = (float) (v / pk * amp);
        }
        // "silence": zeros

        cfg.signal = [in] (int ch, int64_t n) { const auto& x = (*in)[(size_t) juce::jmin (ch, 1)]; return n >= 0 && (size_t) n < x.size() ? x[(size_t) n] : 0.0f; };
        cfg.bufferSizes = { 128, 256, 441, 480, 512, 882, 960, 1024, 2048 };
        juce::String diag; double shownLatency = 0, chainEditAt = 0, stopAt = 0;
        int chainLatency = 0;
        juce::Array<juce::PluginDescription> refDescs;   // real plug-ins in the chain, in order
        MicVSTState refState;
        std::vector<std::pair<double, juce::String>> engineEvents;
        std::vector<std::pair<double, double>> injected;   // test-made disturbances: time, length (s)
        {
            EngineRun run (buffer, seconds);
            auto& e = *run.engine;
            if (chain == "testgain") run.add (testDesc ("TestGain", 1.0));
            if (chain == "pluginstall") run.add (testDesc ("TestStall", 200.0));   // 200 ms every 3 s -> underrun + input skip
            if (chain == "testdelay240") run.add (testDesc ("TestDelay", 240.0));   // reports 240 samples latency
            if (chain.startsWith ("lsp") || chain.startsWith ("vst3"))   // real VST3 plug-ins ("...-bypassed": all bypassed)
            {
                e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                juce::VST3PluginFormat fmt; juce::OwnedArray<juce::PluginDescription> types;
                fmt.findAllTypesForFile (types, vst3Bundle());
                // lsp: EQ x8, graphic EQ x16, compressor, limiter; lsp-eq: only the two (linear) EQs;
                // lsp-dyn: only compressor + limiter
                juce::StringArray names { "Parametric Equalizer x8 Stereo", "Graphic Equalizer x16 Stereo", "Compressor Stereo", "Limiter Stereo" };
                if (chain.startsWith ("lsp-eq"))  names = { "Parametric Equalizer x8 Stereo", "Graphic Equalizer x16 Stereo" };
                if (chain.startsWith ("lsp-dyn")) names = { "Compressor Stereo", "Limiter Stereo" };
                for (auto& w : names)
                    for (auto* t : types) if (t->name == w) { juce::String err; if (e.getChain().addPlugin (e.getFormatManager(), *t, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err)) refDescs.add (*t); }
                if (e.getChain().entries().empty())   // another bundle: up to 4 of its effects (one effect several times if alone)
                {
                    juce::Array<juce::PluginDescription> pick;
                    for (auto* t : types) if (! t->isInstrument && pick.size() < 4) pick.add (*t);
                    for (int i = 0; pick.size() > 0 && pick.size() < 4; ++i) pick.add (pick[i]);
                    for (auto& d : pick) { juce::String err; if (e.getChain().addPlugin (e.getFormatManager(), d, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err)) refDescs.add (d); }
                }
                pr ("  VST3 chain: " + juce::String ((int) e.getChain().entries().size()) + " plug-ins from " + vst3Bundle());
               #if MICVST_NEW_ENGINE
                const AudioEngine::ChainEdit ce (e);
               #endif
                if (chain.endsWith ("-bypassed")) for (int i = 0; i < (int) e.getChain().entries().size(); ++i) e.getChain().setBypass (i, true);
                e.rebuildGraph();
                chainEditAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
            }
            // MicVST's own events (trims, skips, underruns, ...) with their time, every 50 ms, so
            // the analysis can tell them apart from stalls of the test machine.
            auto d0 = e.getDiagnostics();
            const double begin = juce::Time::getMillisecondCounterHiRes() * 0.001, end = begin + seconds;
            double nextStall = begin + 3.0;
            while (juce::Time::getMillisecondCounterHiRes() * 0.001 < end)
            {
                if (chain == "cablestall" && juce::Time::getMillisecondCounterHiRes() * 0.001 >= nextStall)
                {
                    // the cable's driver does not call for 60 ms (-> MicVST trims the excess afterwards)
                    injected.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, 0.060 });
                    vdev::presence().cableFrozen = true;
                    juce::Thread::sleep (60);
                    vdev::presence().cableFrozen = false;
                    nextStall += 3.0;
                }
                pump (0.05);
                const auto d1 = e.getDiagnostics();
                const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
                auto note = [&] (const char* kind, uint64_t a, uint64_t b) { if (b > a) engineEvents.push_back ({ t, juce::String (kind) + " " + juce::String ((int) (b - a)) }); };
                note ("underrun", d0.bridge.underruns, d1.bridge.underruns);
                note ("trim", d0.bridge.resyncs, d1.bridge.resyncs);
                note ("ring-drop", d0.bridge.droppedBlocks, d1.bridge.droppedBlocks);
                note ("input-skip", d0.inputSkips, d1.inputSkips);
                note ("queue-overflow", d0.inputQueueOverflows, d1.inputQueueOverflows);
               #if MICVST_NEW_ENGINE
                note ("invalid", d0.invalidBlocks, d1.invalidBlocks);
               #endif
                d0 = d1;
            }
            diag = diagLine (e);
            shownLatency = e.getDiagnostics().latencyMs;
            chainLatency = e.getGraph().getLatencySamples();
            refState = e.captureState();
            stopAt = juce::Time::getMillisecondCounterHiRes() * 0.001;   // engine destroyed next: fade-out
        }

        // Reference for chains of real plug-ins (compressors, limiters change the sound on
        // purpose): the same plug-ins with the same state, run offline over the exact input in the
        // same block size = what MicVST's chain must hand to the bridge. The analysis compares the
        // cable with it instead of the raw input (from 1 s after the chain was switched in, when
        // the plug-ins' envelopes have caught up with the offline ones).
        std::array<std::vector<float>, 2> ref;
        bool haveRef = false;
        if (! refDescs.isEmpty() && ! chain.endsWith ("-bypassed"))
        {
            juce::AudioPluginFormatManager fm;
            fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
            std::vector<std::unique_ptr<juce::AudioPluginInstance>> procs;
            int maxCh = 2;
            bool ok = true;
            for (int i = 0; i < refDescs.size() && ok; ++i)
            {
                juce::String err;
                auto p = fm.createPluginInstance (refDescs[i], fs, buffer, err);
                if (p == nullptr) { ok = false; pr ("  reference: cannot create " + refDescs[i].name + " - " + err); break; }
                if (i < refState.plugins.size() && refState.plugins[i].state.getSize() > 0)
                    p->setStateInformation (refState.plugins[i].state.getData(), (int) refState.plugins[i].state.getSize());
                p->prepareToPlay (fs, buffer);
                maxCh = std::max ({ maxCh, p->getTotalNumInputChannels(), p->getTotalNumOutputChannels() });
                procs.push_back (std::move (p));
            }
            if (ok)
            {
                const juce::ScopedNoDenormals noDenormals;   // as on MicVST's plug-in thread (flush-to-zero changes quiet passages)
                juce::AudioBuffer<float> b (maxCh, buffer);
                juce::MidiBuffer midi;
                ref[0].assign (len, 0.0f); ref[1].assign (len, 0.0f);
                for (size_t s0 = 0; s0 + (size_t) buffer <= len; s0 += (size_t) buffer)
                {
                    b.clear();
                    b.copyFrom (0, 0, (*in)[0].data() + s0, buffer);
                    b.copyFrom (1, 0, (*in)[1].data() + s0, buffer);
                    for (auto& p : procs) { midi.clear(); p->processBlock (b, midi); }
                    std::memcpy (ref[0].data() + s0, b.getReadPointer (0), sizeof (float) * (size_t) buffer);
                    std::memcpy (ref[1].data() + s0, b.getReadPointer (1), sizeof (float) * (size_t) buffer);
                }
                for (auto& p : procs) p->releaseResources();
                haveRef = true;
            }
        }
        const auto dir = outDir();
        dir.createDirectory();
        auto writeStereo = [] (const juce::File& f, const std::vector<float>& l, const std::vector<float>& r, size_t n)
        {
            std::vector<float> inter (2 * n);
            for (size_t i = 0; i < n; ++i) { inter[2 * i] = l[i]; inter[2 * i + 1] = r[i]; }
            f.replaceWithData (inter.data(), inter.size() * sizeof (float));
        };
        writeStereo (dir.getChildFile ("dump_" + name + "_in.f32"), (*in)[0], (*in)[1], len);
        if (haveRef) writeStereo (dir.getChildFile ("dump_" + name + "_ref.f32"), ref[0], ref[1], len);
        else dir.getChildFile ("dump_" + name + "_ref.f32").deleteFile();
        const auto& rec = vdev::recorder();
        writeStereo (dir.getChildFile ("dump_" + name + "_out.f32"), rec.left, rec.right, rec.left.size());
        juce::String meta;
        meta << "{\"name\":\"" << name << "\",\"signal\":\"" << signal << "\",\"level\":" << levelDb << ",\"seconds\":" << seconds
             << ",\"chain\":\"" << chain << "\",\"micChannels\":" << micChannels << ",\"buffer\":" << buffer
             << ",\"micRate\":" << cfg.micRate << ",\"cableRate\":" << cfg.cableRate
             << ",\"micStart\":" << juce::String (rec.micStart.load(), 6) << ",\"outputStart\":" << juce::String (rec.outputStart.load(), 6)
             << ",\"shownLatencyMs\":" << shownLatency << ",\"chainLatencySamples\":" << chainLatency
             << ",\"reference\":" << (haveRef ? "true" : "false") << ",\"diag\":\"" << diag << "\"";
        // events on the cable timeline (recorded sample index) / mic timeline (input sample index)
        const double t0 = rec.outputStart.load();
        meta << ",\"deviceGaps\":[";
        for (int i = 0; i < rec.gapCount.load(); ++i) meta << (i ? "," : "") << "[" << (juce::int64) rec.gapEvents[(size_t) i].first << "," << (juce::int64) rec.gapEvents[(size_t) i].second << "]";
        meta << "],\"lostMicPackets\":[";
        const auto& lost = vdev::stats().lostEvents;
        for (int i = 0; i < vdev::stats().lostCount.load(); ++i) meta << (i ? "," : "") << (juce::int64) lost[(size_t) i];
        meta << "],\"micPacket\":" << buffer << ",\"engineEvents\":[";
        for (size_t i = 0; i < engineEvents.size(); ++i)
            meta << (i ? "," : "") << "[" << (juce::int64) ((engineEvents[i].first - t0) * cfg.cableRate) << ",\"" << engineEvents[i].second << "\"]";
        meta << "],\"chainEdit\":" << (chainEditAt > 0 ? (juce::int64) ((chainEditAt - t0) * cfg.cableRate) : (juce::int64) -1)
             << ",\"stop\":" << (juce::int64) ((stopAt - t0) * cfg.cableRate) << ",\"injected\":[";
        for (size_t i = 0; i < injected.size(); ++i)
            meta << (i ? "," : "") << "[" << (juce::int64) ((injected[i].first - t0) * cfg.cableRate) << "," << (juce::int64) (injected[i].second * cfg.cableRate) << "]";
        meta << "]}";
        dir.getChildFile ("dump_" + name + ".json").replaceWithText (meta);
        pr ("  " + diag);
        pr ("  written: dump_" + name + "_{in,out}.f32 + .json");
    }
    else if (mode == "devices")
    {
        // Device changes while audio plays: buffer size switches (both devices reopen), then the
        // engine is destroyed (program exit). Clicks on the cable timeline around each change.
        sineConfig (997.0, 0.5, 48000);
        std::vector<std::pair<double, juce::String>> marks;
        {
            EngineRun run (480, 16.0);
            auto& e = *run.engine;
            pump (3.0);
            for (int bs : { 256, 960, 480 })
            {
                marks.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, "buffer -> " + juce::String (bs) });
                e.setDeviceConfig ("VMic", "VCable", 48000.0, bs);
                raiseEngineThreads();
                pump (3.0);
            }
            marks.push_back ({ juce::Time::getMillisecondCounterHiRes() * 0.001, "program exit" });
            pr ("  " + diagLine (e));
        }
        // after the last device stopped, the cable delivers silence to whoever listens
        for (int i = 0; i < 4800; ++i) { vdev::recorder().left.push_back (0.0f); vdev::recorder().right.push_back (0.0f); }
        const auto& L = vdev::recorder().left;
        const double fs = 48000.0, t0 = vdev::recorder().outputStart.load();
        const double lim = 3.0 * 0.5 * std::pow (twoPi * 997.0 / fs, 2.0) + 2.0e-4;
        pr ("| Change | Clicks within 1 s after | Longest silence (ms) |");
        pr ("|---|---:|---:|");
        for (auto& m : marks)
        {
            const auto from = (size_t) std::max (0.0, (m.first - t0) * fs - 0.1 * fs);
            const auto to = std::min (L.size(), from + (size_t) (1.1 * fs));
            int clicks = 0, zeros = 0, longest = 0;
            for (size_t i = std::max<size_t> (from, 1); i + 1 < to; ++i)
            {
                if (std::abs ((double) L[i + 1] - 2.0 * L[i] + L[i - 1]) > lim) ++clicks;
                if (std::abs (L[i]) < 1e-6f) longest = std::max (longest, ++zeros); else zeros = 0;
            }
            pr ("| " + m.second + " | " + juce::String (clicks) + " | " + juce::String (1000.0 * longest / fs, 1) + " |");
        }
    }
    else if (mode == "clickpos")
    {
        sineConfig (997.0, 0.5, 48000);
        { EngineRun run (480, 10.0); pump (10.0); pr ("  " + diagLine (*run.engine)); }
        const auto& L = vdev::recorder().left;
        const double lim = 3.0 * 0.5 * std::pow (twoPi * 997.0 / 48000.0, 2.0) + 2.0e-4;
        const size_t start = firstAudio (L);
        pr ("  recording " + juce::String ((int) L.size()) + " samples, first audio at " + juce::String ((int) start));
        for (size_t i = start + 1; i + 1 < L.size(); ++i)
            if (std::abs ((double) L[i + 1] - 2.0 * L[i] + L[i - 1]) > lim)
                std::printf ("  click at %zu (%.4f s, %zu before the end): %.5f %.5f %.5f\n", i, (double) i / 48000.0, L.size() - i, L[i - 1], L[i], L[i + 1]);
    }
   #if ! JUCE_WINDOWS
    else if (mode == "trace")   // trace <seconds> <freq> <load> <prefix>
        trace (argc > 2 ? std::atof (argv[2]) : 40.0, argc > 3 ? std::atof (argv[3]) : 997.0,
               argc > 4 ? std::atof (argv[4]) : 0.0, argc > 5 ? juce::String (argv[5]) : juce::String ("trace"));
   #endif

    juce::Logger::setCurrentLogger (nullptr);
    return 0;
}
