// Included by EngineTestMain.cpp (inside its anonymous namespace, after the Otomatik Kazanc test,
// whose helpers it uses). Mode "vst3suite": real VST3 effects in MicVST's engine.
//
// The plug-ins come from $MICVST_TEST_VST3_DIR (every *.vst3 bundle in it, recursively). Each one is
// tested on its own - loading, real processing (a measurement that fits its kind: EQ response,
// compression, de-essing, saturation, noise suppression, peak limiting), parameters changed while
// audio runs (as the plug-in's window or automation does), bypass on/off, the plug-in window opened
// and closed during playback (Windows), defaults restored, the state saved through MicVST's own
// config.xml path and restored into a new engine (same parameter values, same measured sound), a mono
// microphone - and then all together: a chain like the user's, chain edits during playback, the
// levels between the effects with Otomatik Kazanc in front (on / off) and other orders.
// Free plug-ins stand in for the user's commercial ones (same kinds of processing); the report says
// which is which. Nothing here is a listening test: every statement is a measurement.
namespace suite
{
    // ---------- live test signal: chosen by the message thread, read by the virtual microphone ----------
    enum Sig : int { silence = 0, multitone, sine997, sine300, sine6300, sine1k, noise, speech };
    std::atomic<int>    sig { silence };
    std::atomic<double> sigLevelDb { -20.0 };
    std::vector<float>  speechData;   // realistic speech, typical level -20 dBFS (set before a run starts)
    constexpr double toneFreqs[] = { 100.0, 200.0, 400.0, 1000.0, 2000.0, 3000.0, 5000.0, 7000.0, 10000.0, 15000.0 };
    constexpr int numTones = (int) (sizeof (toneFreqs) / sizeof (toneFreqs[0]));

    double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    void installSignal()
    {
        auto& cfg = vdev::config();
        const double fs = cfg.micRate;
        cfg.signal = [fs] (int, int64_t n) -> float
        {
            const double a = juce::Decibels::decibelsToGain (sigLevelDb.load (std::memory_order_relaxed));
            const double t = (double) n / fs;
            switch (sig.load (std::memory_order_relaxed))
            {
                case multitone:
                {
                    double v = 0;
                    for (int k = 0; k < numTones; ++k) v += std::sin (twoPi * toneFreqs[k] * t + 0.7 * k * k);
                    return (float) (a * v);
                }
                case sine997:  return (float) (a * std::sin (twoPi * 997.0 * t));
                case sine300:  return (float) (a * std::sin (twoPi * 300.0 * t));
                case sine6300: return (float) (a * std::sin (twoPi * 6300.0 * t));
                case sine1k:   return (float) (a * std::sin (twoPi * 1000.0 * t));
                case noise:
                {
                    uint32_t h = (uint32_t) n * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
                    return (float) (a * std::sqrt (3.0) * ((double) h / 4294967295.0 * 2.0 - 1.0));   // white, RMS = level
                }
                case speech:
                    return speechData.empty() ? 0.0f
                         : (float) (a / 0.1 * speechData[(size_t) (n % (int64_t) speechData.size())]);   // -20 dBFS typical at level -20
                default: return 0.0f;
            }
        };
    }

    void setSignal (Sig s, double levelDb) { sigLevelDb.store (levelDb); sig.store (s); }

    // ---------- analysis of the recording (cable timeline) ----------
    struct Window { size_t from = 0, to = 0; size_t size() const { return to > from ? to - from : 0; } };
    struct Phase  { juce::String name; double t0 = 0, t1 = 0; };

    Window windowOf (const Phase& p, double settle = 1.0)
    {
        const double fs = vdev::config().cableRate, t0 = vdev::recorder().outputStart.load();
        const auto& L = vdev::recorder().left;
        Window w;
        w.from = (size_t) std::max (0.0, (p.t0 + settle - t0) * fs);
        w.to   = std::min (L.size(), (size_t) std::max (0.0, (p.t1 - 0.15 - t0) * fs));
        if (w.from > w.to) w.from = w.to;
        return w;
    }

    double toneAmp (const std::vector<float>& y, Window w, double freq)
    {
        const double fs = vdev::config().cableRate;
        std::vector<double> a;
        for (size_t s = w.from; s + 8192 <= w.to && a.size() < 6; s += 8192)
            a.push_back (fitSine (y, s, 8192, freq / fs).amp);
        if (a.empty()) return 0.0;
        std::sort (a.begin(), a.end());
        return a[a.size() / 2];
    }
    double toneDb (const std::vector<float>& y, Window w, double freq, double inLevelDb)
    {
        return juce::Decibels::gainToDecibels (toneAmp (y, w, freq), -200.0) - inLevelDb;
    }
    double thdnDb (const std::vector<float>& y, Window w, double freq)
    {
        const double fs = vdev::config().cableRate;
        std::vector<double> r;
        for (size_t s = w.from; s + 4096 <= w.to && r.size() < 12; s += 4096)
            r.push_back (fitSine (y, s, 4096, freq / fs).residualDb);
        if (r.empty()) return 0.0;
        std::sort (r.begin(), r.end());
        return r[r.size() / 2];
    }
    double rmsDb (const std::vector<float>& y, Window w)
    {
        double s = 0; for (size_t i = w.from; i < w.to; ++i) s += (double) y[i] * y[i];
        return juce::Decibels::gainToDecibels (std::sqrt (s / (double) std::max<size_t> (1, w.size())), -200.0);
    }
    double peakDbOf (const std::vector<float>& y, Window w)
    {
        float m = 0; for (size_t i = w.from; i < w.to; ++i) m = std::max (m, std::abs (y[i]));
        return juce::Decibels::gainToDecibels ((double) m, -200.0);
    }
    // A stall of the test machine near a cable sample: the virtual cable woke late (device gap) or
    // the virtual microphone lost a packet before MicVST could read it - not MicVST.
    bool machineStallNear (size_t at)
    {
        const double fs = vdev::config().cableRate;
        const auto& rec = vdev::recorder();
        for (int i = 0; i < rec.gapCount.load(); ++i)
        {
            const auto& g = rec.gapEvents[(size_t) i];
            if ((int64_t) at >= g.first - (int64_t) (0.05 * fs) && (int64_t) at <= g.first + g.second + (int64_t) (0.1 * fs)) return true;
        }
        const auto& st = vdev::stats();
        for (int i = 0; i < st.lostCount.load(); ++i)
        {
            const auto m = (int64_t) ((st.lostTimes[(size_t) i] - rec.outputStart.load()) * fs);
            if ((int64_t) at >= m - (int64_t) (0.05 * fs) && (int64_t) at <= m + (int64_t) (0.4 * fs)) return true;
        }
        return false;
    }
    int machineStalls = 0;   // findings put down to the test machine (reported)
    int zeroRuns (const std::vector<float>& y, Window w, int minRun = 48)
    {
        int runs = 0, z = 0;
        size_t start = 0;
        auto count = [&] { if (z > minRun) { if (machineStallNear (start)) ++machineStalls; else ++runs; } };
        for (size_t i = w.from; i < w.to; ++i) { if (y[i] == 0.0f) { if (z++ == 0) start = i; } else { count(); z = 0; } }
        count();
        return runs;
    }
    int sineClicks (const std::vector<float>& y, Window w, double freq, juce::StringArray* where = nullptr)
    {
        const double fs = vdev::config().cableRate;
        const double amp = toneAmp (y, w, freq);
        if (amp < 1.0e-4) return 0;
        std::vector<size_t> at;
        clicks (y, w.from, w.to, amp, freq / fs, 480, &at);
        int n = 0;
        for (auto i : at)
        {
            if (machineStallNear (i)) { ++machineStalls; continue; }
            ++n;
            if (where != nullptr) where->add (juce::String ((double) i / fs, 3) + " s (" + juce::String (y[i - 1], 4) + " " + juce::String (y[i], 4) + " " + juce::String (y[i + 1], 4) + ")");
        }
        return n;
    }
    std::array<double, numTones> response (const std::vector<float>& y, Window w, double inLevelDb)
    {
        std::array<double, numTones> r {};
        for (int k = 0; k < numTones; ++k) r[(size_t) k] = toneDb (y, w, toneFreqs[k], inLevelDb);
        return r;
    }
    juce::String responseText (const std::array<double, numTones>& r)
    {
        juce::StringArray s;
        for (int k = 0; k < numTones; ++k)
            s.add ((toneFreqs[k] >= 1000.0 ? juce::String (toneFreqs[k] / 1000.0, toneFreqs[k] < 10000.0 && std::fmod (toneFreqs[k], 1000.0) != 0.0 ? 1 : 0) + "k"
                                           : juce::String (toneFreqs[k], 0)) + " " + juce::String (r[(size_t) k], 2));
        return s.joinIntoString (", ");
    }
    double maxAbsDiff (const std::array<double, numTones>& a, const std::array<double, numTones>& b)
    {
        double m = 0; for (int k = 0; k < numTones; ++k) m = std::max (m, std::abs (a[(size_t) k] - b[(size_t) k])); return m;
    }

    // ---------- parameters: by name, set by text (what the plug-in shows), as its window would ----------
    juce::AudioProcessorParameter* findParam (juce::AudioProcessor& p, const juce::String& name)
    {
        for (auto* q : p.getParameters()) if (q->getName (100).trim() == name) return q;
        for (auto* q : p.getParameters()) if (q->getName (100).trim().startsWithIgnoreCase (name)) return q;
        return nullptr;
    }
    bool isNumber (const juce::String& s) { return s.trim().isNotEmpty() && s.trim().containsOnly ("-+.0123456789"); }

    float valueForText (juce::AudioProcessorParameter& q, const juce::String& text)
    {
        auto matches = [&] (float v)
        {
            const auto t = q.getText (v, 60).trim();
            if (t.equalsIgnoreCase (text.trim())) return true;
            if (isNumber (text))
            {
                const double want = text.getDoubleValue(), got = t.getDoubleValue();
                return std::abs (got - want) <= std::max (0.02, 0.02 * std::abs (want));
            }
            return false;
        };
        if (q.isDiscrete() && q.getNumSteps() > 1 && q.getNumSteps() <= 4096)
        {
            for (int k = 0; k < q.getNumSteps(); ++k)
            {
                const float v = (float) k / (float) (q.getNumSteps() - 1);
                if (matches (v)) return v;
            }
        }
        const float v0 = q.getValueForText (text);
        if (v0 >= 0.0f && v0 <= 1.0f && matches (v0)) return v0;
        if (isNumber (text))   // monotonic continuous parameter: bisection on the shown number
        {
            const double want = text.getDoubleValue();
            const bool rising = q.getText (1.0f, 60).getDoubleValue() >= q.getText (0.0f, 60).getDoubleValue();
            float lo = 0.0f, hi = 1.0f;
            for (int it = 0; it < 40; ++it)
            {
                const float mid = 0.5f * (lo + hi);
                const double g = q.getText (mid, 60).getDoubleValue();
                if ((g < want) == rising) lo = mid; else hi = mid;
            }
            const float v = 0.5f * (lo + hi);
            if (matches (v)) return v;
        }
        return -1.0f;
    }

    struct Setting { juce::String param, text; };
    struct Applied { juce::String param, wanted, got; float before = 0.0f; bool ok = false; };

    std::vector<Applied> applySettings (juce::AudioProcessor& p, const std::vector<Setting>& settings)
    {
        std::vector<Applied> out;
        for (auto& s : settings)
        {
            Applied a; a.param = s.param; a.wanted = s.text;
            auto* q = findParam (p, s.param);
            if (q == nullptr) { a.got = "(no such parameter)"; out.push_back (a); continue; }
            const float v = valueForText (*q, s.text);
            if (v < 0.0f) { a.got = "(text not accepted; shows '" + q->getCurrentValueAsText() + "')"; out.push_back (a); continue; }
            a.before = q->getValue();
            q->beginChangeGesture();
            q->setValueNotifyingHost (v);   // what the plug-in's window / automation does
            q->endChangeGesture();
            a.got = q->getCurrentValueAsText();
            a.ok = true;
            out.push_back (a);
        }
        return out;
    }
    void restoreSettings (juce::AudioProcessor& p, const std::vector<Applied>& applied)
    {
        for (auto it = applied.rbegin(); it != applied.rend(); ++it)
            if (it->ok)
                if (auto* q = findParam (p, it->param))
                {
                    q->beginChangeGesture(); q->setValueNotifyingHost (it->before); q->endChangeGesture();
                }
    }
    juce::String appliedText (const std::vector<Applied>& a)
    {
        juce::StringArray s;
        for (auto& x : a) s.add (x.param + " = " + (x.ok ? "'" + x.got + "'" : x.got + " (wanted '" + x.wanted + "')"));
        return s.joinIntoString ("; ");
    }
    bool allApplied (const std::vector<Applied>& a) { for (auto& x : a) if (! x.ok) return false; return true; }

    std::vector<float> paramSnapshot (juce::AudioProcessor& p)
    {
        std::vector<float> v; for (auto* q : p.getParameters()) v.push_back (q->getValue()); return v;
    }

    // ---------- plug-in windows (as PluginListView::openEditor does it) ----------
    bool editorsWanted()
    {
       #if JUCE_WINDOWS
        return ! envSet ("ETEST_NO_EDITORS");
       #else
        return envSet ("ETEST_EDITORS");   // headless Linux (no display): no windows
       #endif
    }
    struct EditorWindow final : juce::DocumentWindow
    {
        explicit EditorWindow (const juce::String& n) : juce::DocumentWindow (n, juce::Colours::black, juce::DocumentWindow::closeButton) {}
        void closeButtonPressed() override { setVisible (false); }
    };
    std::unique_ptr<EditorWindow> openEditor (juce::AudioProcessor& p, juce::String& note)
    {
        if (! p.hasEditor()) { note = "no editor"; return {}; }
        const double t0 = now();
        auto* ed = p.createEditorAndMakeActive();
        if (ed == nullptr) { note = "the plug-in created no editor"; return {}; }
        auto w = std::make_unique<EditorWindow> (p.getName());
        w->setUsingNativeTitleBar (true);
        w->setContentOwned (ed, true);
        w->centreWithSize (w->getWidth(), w->getHeight());
        w->setVisible (true);
        note = "opened in " + juce::String (1000.0 * (now() - t0), 0) + " ms (" + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) + ")";
        return w;
    }

    // ---------- the plug-ins ----------
    struct Found { juce::PluginDescription desc; };

    std::vector<juce::PluginDescription> findPlugins()
    {
        std::vector<juce::PluginDescription> out;
        const auto dirName = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_VST3_DIR", {});
        if (dirName.isEmpty()) return out;
        const juce::File dir (dirName);
        juce::VST3PluginFormat fmt;
        juce::Array<juce::File> bundles;
        for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*.vst3", juce::File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            bool nested = false;   // the module inside a bundle is no bundle of its own
            for (auto p = f.getParentDirectory(); p != dir && p.getFullPathName().length() > dir.getFullPathName().length(); p = p.getParentDirectory())
                if (p.hasFileExtension ("vst3")) { nested = true; break; }
            if (! nested) bundles.addIfNotAlreadyThere (f);
        }
        bundles.sort();
        for (auto& b : bundles)
        {
            juce::OwnedArray<juce::PluginDescription> types;
            fmt.findAllTypesForFile (types, b.getFullPathName());
            for (auto* t : types) if (! t->isInstrument) out.push_back (*t);
        }
        return out;
    }

    // What each test configuration does: its kind decides the signal and the measurement.
    enum class Kind { denoise, eq, comp, deess, limiter, sat, generic };
    struct Config
    {
        juce::String label, standsFor;   // what it is, which of the user's plug-ins it stands in for
        Kind kind = Kind::generic;
        juce::PluginDescription desc;
        std::vector<Setting> active;     // the settings that make it work (applied live)
    };

    juce::String kindName (Kind k)
    {
        switch (k)
        {
            case Kind::denoise: return "noise suppression"; case Kind::eq: return "EQ"; case Kind::comp: return "compressor";
            case Kind::deess: return "de-esser"; case Kind::limiter: return "peak limiter"; case Kind::sat: return "saturation";
            default: return "other effect";
        }
    }

    std::vector<Config> buildConfigs (const std::vector<juce::PluginDescription>& found)
    {
        std::vector<Config> c;
        auto has = [&found] (const juce::String& name) -> const juce::PluginDescription*
        {
            for (auto& d : found) if (d.name.containsIgnoreCase (name)) return &d;
            return nullptr;
        };
        if (auto* d = has ("RNNoise"))
            c.push_back ({ "RNNoise", "Clear (noise removal)", Kind::denoise, *d, { { "VAD Threshold", "0.00" } } });
        if (auto* d = has ("ZL Equalizer"))
            c.push_back ({ "ZL Equalizer 2", "Pro-Q 4 / TDR VOS SlickEQ GE (EQ)", Kind::eq, *d,
                           { { "Filter Status0", "On" }, { "Filter Type0", "Peak" }, { "Freq0", "1000" }, { "Gain0", "6" }, { "Q0", "1" } } });
        if (auto* d = has ("ZL Compressor"))
        {
            c.push_back ({ "ZL Compressor", "Pro-C 3 / DC1A3 (compressor)", Kind::comp, *d,
                           { { "Compressor Style", "Clean" }, { "Threshold (dB)", "-30" }, { "Ratio", "4" }, { "Knee Width", "0" },
                             { "Attack", "1" }, { "Release", "50" } } });
            c.push_back ({ "ZL Compressor (de-esser setting: side chain high-passed at 5 kHz)", "Pro-DS (de-esser)", Kind::deess, *d,
                           { { "Compressor Style", "Clean" }, { "Threshold (dB)", "-40" }, { "Ratio", "10" }, { "Knee Width", "0" },
                             { "Attack", "1" }, { "Release", "50" },
                             { "Filter Status0", "On" }, { "Filter Type0", "High Pass" }, { "Order0", "24 dB/oct" }, { "Freq0", "5000" } } });
            c.push_back ({ "ZL Compressor (peak limiter setting, 5 ms look-ahead)", "TDR Limiter 6 GE (limiter)", Kind::limiter, *d,
                           { { "Compressor Style", "Clean" }, { "Threshold (dB)", "-8" }, { "Ratio", "100" }, { "Knee Width", "0" },
                             { "Attack", "0" }, { "Release", "50" }, { "Lookahead", "5" } } });
        }
        if (auto* d = has ("Fire"))
            c.push_back ({ "Fire (multiband saturation)", "Saturn 2 / SDRR2 (saturation)", Kind::sat, *d, { { "Drive", "60" } } });
        for (auto& d : found)   // every other plug-in: generic checks
        {
            const bool known = d.name.containsIgnoreCase ("RNNoise") || d.name.containsIgnoreCase ("ZL Equalizer")
                            || d.name.containsIgnoreCase ("ZL Compressor") || d.name.equalsIgnoreCase ("Fire");
            if (known) continue;
            juce::String stands = "-";
            if (d.name.containsIgnoreCase ("SlickEQ")) stands = "TDR VOS SlickEQ GE (free edition of the same plug-in)";
            if (d.name.containsIgnoreCase ("DC1A"))    stands = "DC1A3 (the user's own plug-in, free)";
            if (d.name.containsIgnoreCase ("Kotelnikov")) stands = "a TDR compressor (same maker)";
            c.push_back ({ d.name, stands, Kind::generic, d, {} });
        }
        return c;
    }

    // Generic configurations: up to five continuous parameters (not bypass/mix/output/gain staging)
    // moved away from their defaults - does the sound change?
    std::vector<Setting> genericSettings (juce::AudioProcessor& p)
    {
        std::vector<Setting> s;
        for (auto* q : p.getParameters())
        {
            if (s.size() >= 5) break;
            const auto n = q->getName (100);
            if (q->isDiscrete() || ! q->isAutomatable()) continue;
            if (n.containsIgnoreCase ("bypass") || n.containsIgnoreCase ("mix") || n.containsIgnoreCase ("wet") || n.containsIgnoreCase ("dry")
                 || n.containsIgnoreCase ("output") || n.containsIgnoreCase ("volume") || n.containsIgnoreCase ("trim")) continue;
            const float v = q->getValue() < 0.5f ? 0.8f : 0.2f;
            s.push_back ({ n, q->getText (v, 60) });
        }
        return s;
    }

    // signal + level of a kind's main measurement
    Sig mainSignal (Kind k)
    {
        switch (k)
        {
            case Kind::denoise: return noise;
            case Kind::comp: case Kind::limiter: return sine1k;
            case Kind::deess: return sine6300;
            case Kind::sat: return sine997;
            default: return multitone;
        }
    }
    double mainLevel (Kind k)
    {
        switch (k)
        {
            case Kind::denoise: return -40.0;
            case Kind::comp: return -10.0;
            case Kind::limiter: return -2.0;
            case Kind::deess: case Kind::sat: return -12.0;
            default: return -30.0;   // per tone (10 tones)
        }
    }
    bool sineSignal (Sig s) { return s == sine997 || s == sine300 || s == sine6300 || s == sine1k; }
    double sigFreq (Sig s) { return s == sine997 ? 997.0 : s == sine300 ? 300.0 : s == sine6300 ? 6300.0 : 1000.0; }

    // The kind's measurement of one phase, as a short text + the numbers used by the checks.
    struct Metric
    {
        juce::String text;
        double value = 0;                        // gain (dB re input) of the tone / RMS change / THD+N
        std::array<double, numTones> resp {};    // EQ / generic: response at the multitone frequencies
        double peak = -200;
    };
    Metric measure (Kind k, Sig s, double level, Window w, const std::vector<float>& y)
    {
        Metric m;
        m.peak = peakDbOf (y, w);
        if (s == multitone)
        {
            m.resp = response (y, w, level);
            m.text = "response (dB): " + responseText (m.resp);
            m.value = m.resp[3];   // 1 kHz
        }
        else if (sineSignal (s))
        {
            const double f = sigFreq (s);
            m.value = toneDb (y, w, f, level);
            const double th = thdnDb (y, w, f);
            m.text = juce::String (f, 0) + " Hz at " + juce::String (level, 0) + " dBFS: gain " + juce::String (m.value, 2) + " dB, THD+N " + juce::String (th, 1)
                   + " dB, peak " + juce::String (m.peak, 2) + " dBFS";
            if (k == Kind::sat) m.value = th;
        }
        else   // noise
        {
            m.value = rmsDb (y, w) - level;
            m.text = "noise in " + juce::String (level, 0) + " dBFS RMS -> out " + juce::String (rmsDb (y, w), 1) + " dBFS RMS (change " + juce::String (m.value, 1) + " dB)";
        }
        juce::ignoreUnused (k);
        return m;
    }

    struct Verdicts
    {
        int pass = 0, fail = 0, note = 0;
        juce::StringArray failures;
        void check (bool ok, const juce::String& what) { pr (juce::String ("  ") + (ok ? "PASS " : "FAIL ") + what); if (ok) ++pass; else { ++fail; failures.add (what); } }
        void info (const juce::String& what) { pr ("  NOTE " + what); ++note; }
    };
    Verdicts verdicts;

    juce::AudioProcessor* procAt (AudioEngine& e, int index)
    {
        const auto& es = e.getChain().entries();
        if (! juce::isPositiveAndBelow (index, (int) es.size())) return nullptr;
        auto* n = e.getGraph().getNodeForId (es[(size_t) index].node);
        return n != nullptr ? n->getProcessor() : nullptr;
    }

    void editChain (AudioEngine& e, std::function<void (PluginChain&)> f)
    {
        const AudioEngine::ChainEdit ce (e);
        f (e.getChain());
        e.rebuildGraph();
    }

    bool addVst3 (AudioEngine& e, const juce::PluginDescription& d, juce::String& err, double* loadSeconds = nullptr)
    {
        const double t0 = now();
        const bool ok = e.getChain().addPlugin (e.getFormatManager(), d, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err);
        if (loadSeconds != nullptr) *loadSeconds = now() - t0;
        const AudioEngine::ChainEdit ce (e);
        e.rebuildGraph();
        return ok;
    }

    struct Counters { uint64_t underruns = 0, skips = 0, overflows = 0, invalid = 0, graphBusy = 0, silentBlocks = 0; int xruns = 0; };
    Counters counters (AudioEngine& e)
    {
        const auto d = e.getDiagnostics();
        return { d.bridge.underruns, d.inputSkips, d.inputQueueOverflows, d.invalidBlocks, d.graphBusyBlocks, d.chainSilentBlocks, d.inputDeviceXruns };
    }
    juce::String countersDiff (const Counters& a, const Counters& b)
    {
        return "underruns " + juce::String ((int) (b.underruns - a.underruns)) + ", input skips " + juce::String ((int) (b.skips - a.skips))
             + ", queue overflows " + juce::String ((int) (b.overflows - a.overflows)) + ", invalid blocks " + juce::String ((int) (b.invalid - a.invalid))
             + ", graph-busy blocks " + juce::String ((int) (b.graphBusy - a.graphBusy)) + ", mic xruns " + juce::String (b.xruns - a.xruns);
    }
    bool countersClean (const Counters& a, const Counters& b)
    {
        return b.underruns == a.underruns && b.skips == a.skips && b.overflows == a.overflows && b.invalid == a.invalid && b.graphBusy == a.graphBusy;
    }

    //==============================================================================
    // One plug-in configuration on its own.
    void testOne (const Config& c)
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;
        pr ("");
        pr ("### " + c.label + " (" + kindName (c.kind) + "; stands for: " + c.standsFor + ")");
        pr ("plug-in: " + c.desc.name + " " + c.desc.version + " by " + c.desc.manufacturerName + " (" + c.desc.fileOrIdentifier + ")");

        const Sig s = mainSignal (c.kind);
        const double lvl = mainLevel (c.kind);
        std::vector<Phase> phases;
        auto phase = [&phases] (const juce::String& name, double seconds) { Phase p; p.name = name; p.t0 = now(); pump (seconds); p.t1 = now(); phases.push_back (p); return phases.size() - 1; };
        MicVSTState saved;
        std::vector<float> activeParams;
        std::vector<Applied> applied;
        juce::String editorNote = "not tested";
        Counters cBefore, cEditorOpen, cEditorClosed, cEnd;
        int latencyDefault = 0, latencyActive = 0, graphLatencyActive = 0, latencyZero = -1, graphLatencyZero = -1;
        double shownActive = 0, shownZero = 0;
        double loadSeconds = 0;
        juce::String structureNote;
        size_t pDefault = 0, pActive = 0, pActiveLow = 0, pActiveOther = 0, pBypass = 0, pBypassOff = 0, pEditor = 0, pEditorClosed = 0, pDefaultsAgain = 0, pZero = 0;
        bool hasLow = false, hasOther = false, hasZero = false, editorOpened = false;
        juce::String busText;
        {
            setSignal (silence, -200.0);
            EngineRun run (480, 60.0);
            auto& e = *run.engine;
            e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
            juce::String err;
            if (! addVst3 (e, c.desc, err, &loadSeconds))
            {
                verdicts.check (false, c.label + ": loads in MicVST (" + err + ")");
                return;
            }
            auto* p = procAt (e, 0);
            busText = "in " + juce::String (p->getTotalNumInputChannels()) + " / out " + juce::String (p->getTotalNumOutputChannels()) + " channels (main in: "
                    + (p->getBusCount (true) > 0 ? p->getBus (true, 0)->getCurrentLayout().getDescription() : juce::String ("none")) + ")";
            latencyDefault = p->getLatencySamples();
            pr ("loaded in " + juce::String (loadSeconds, 2) + " s; " + busText + ", latency " + juce::String (latencyDefault) + " samples, "
                + juce::String (p->getParameters().size()) + " parameters, editor " + (p->hasEditor() ? "yes" : "no"));
            verdicts.check (true, c.label + ": loads in MicVST and joins the chain");
            const auto defaults = paramSnapshot (*p);

            setSignal (s, lvl);
            pump (1.0);
            cBefore = counters (e);
            pDefault = phase ("default settings", 2.5);

            auto settings = c.kind == Kind::generic ? genericSettings (*p) : c.active;
            applied = applySettings (*p, settings);
            pr ("settings applied live: " + appliedText (applied));
            pActive = phase ("active settings", 2.5);
            latencyActive = p->getLatencySamples();
            graphLatencyActive = e.chainLatencySamples();
            shownActive = e.getDiagnostics().latencyMs;
            if (c.kind == Kind::comp) { setSignal (s, -40.0); pActiveLow = phase ("active, -40 dBFS", 2.5); hasLow = true; setSignal (s, lvl); pump (0.5); }
            if (c.kind == Kind::deess) { setSignal (sine300, lvl); pActiveOther = phase ("active, 300 Hz", 2.5); hasOther = true; setSignal (s, lvl); pump (0.5); }
            if (c.kind == Kind::eq)    // linear-phase ("Zero Phase") structure: the plug-in changes its latency while running
                if (auto* q = findParam (*p, "Filter Structure"))
                {
                    const float before = q->getValue(), v = valueForText (*q, "Zero Phase");
                    if (v >= 0.0f)
                    {
                        q->beginChangeGesture(); q->setValueNotifyingHost (v); q->endChangeGesture();
                        pZero = phase ("active, Zero Phase structure", 3.0); hasZero = true;
                        latencyZero = p->getLatencySamples();
                        graphLatencyZero = e.chainLatencySamples();
                        shownZero = e.getDiagnostics().latencyMs;
                        structureNote = "'" + q->getCurrentValueAsText() + "'";
                        q->beginChangeGesture(); q->setValueNotifyingHost (before); q->endChangeGesture();
                        pump (1.0);
                    }
                }

            editChain (e, [] (PluginChain& ch) { ch.setBypass (0, true); });
            pBypass = phase ("bypassed", 2.0);
            editChain (e, [] (PluginChain& ch) { ch.setBypass (0, false); });
            pBypassOff = phase ("bypass off again", 2.0);

            if (editorsWanted())
            {
                std::unique_ptr<EditorWindow> w;
                cEditorOpen = counters (e);
                const double t0 = now();
                w = openEditor (*p, editorNote);
                editorOpened = w != nullptr;
                pEditor = phase ("plug-in window open", 2.5);
                phases[pEditor].t0 = t0;
                w.reset();
                pEditorClosed = phase ("plug-in window closed", 1.5);
                cEditorClosed = counters (e);
            }

            saved = e.captureState();
            activeParams = paramSnapshot (*p);
            restoreSettings (*p, applied);
            pDefaultsAgain = phase ("defaults restored", 2.5);
            const auto back = paramSnapshot (*p);
            int same = 0; for (size_t i = 0; i < back.size() && i < defaults.size(); ++i) same += std::abs (back[i] - defaults[i]) < 1.0e-5f ? 1 : 0;
            pr ("parameters back at their defaults: " + juce::String (same) + "/" + juce::String ((int) defaults.size()));
            cEnd = counters (e);
            pr ("engine: " + diagLine (e));
        }

        const auto& L = vdev::recorder().left;
        const auto& R = vdev::recorder().right;
        auto m = [&] (size_t i, Sig sg = (Sig) -1, double level = 1.0e9) { return measure (c.kind, sg == (Sig) -1 ? s : sg, level > 1.0e8 ? lvl : level, windowOf (phases[i]), L); };
        const auto mDefault = m (pDefault), mActive = m (pActive), mBypass = m (pBypass), mBypassOff = m (pBypassOff), mAgain = m (pDefaultsAgain);
        pr ("| phase | measurement |");
        pr ("|---|---|");
        pr ("| default settings | " + mDefault.text + " |");
        pr ("| active settings | " + mActive.text + " |");
        Metric mLow, mOther, mZero;
        if (hasLow)   { mLow = m (pActiveLow, s, -40.0);           pr ("| active, quiet input | " + mLow.text + " |"); }
        if (hasOther) { mOther = m (pActiveOther, sine300, lvl);   pr ("| active, 300 Hz | " + mOther.text + " |"); }
        if (hasZero)  { mZero = m (pZero);                         pr ("| active, " + structureNote + " | " + mZero.text + " |"); }
        pr ("| bypassed | " + mBypass.text + " |");
        pr ("| bypass off again | " + mBypassOff.text + " |");
        Metric mEditor;
        if (pEditor != 0) { mEditor = m (pEditor); pr ("| plug-in window open | " + mEditor.text + " |"); }
        pr ("| defaults restored | " + mAgain.text + " |");

        // "the same sound": within 0.25 dB (noise suppression: 2 dB, and two residuals both below
        // -90 dBFS - inaudible, they fluctuate - count as the same)
        auto sameSound = [&c, lvl] (double a, double b)
        {
            if (c.kind == Kind::denoise) return std::abs (a - b) <= 2.0 || (a + lvl <= -90.0 && b + lvl <= -90.0);
            return std::abs (a - b) <= 0.25;
        };
        // kind-specific checks: the plug-in really processes the audio inside MicVST, and its
        // parameters (changed while audio runs) reach the sound
        verdicts.check (allApplied (applied), c.label + ": every live parameter change accepted (" + juce::String ((int) applied.size()) + " parameters)");
        switch (c.kind)
        {
            case Kind::eq:
                verdicts.check (std::abs (mActive.resp[3] - 6.0) <= 0.8 && std::abs (mActive.resp[0]) <= 0.6 && std::abs (mActive.resp[9]) <= 0.6,
                                c.label + ": bell +6 dB at 1 kHz measured " + juce::String (mActive.resp[3], 2) + " dB, 100 Hz " + juce::String (mActive.resp[0], 2)
                                + " dB, 15 kHz " + juce::String (mActive.resp[9], 2) + " dB");
                verdicts.check (maxAbsDiff (mDefault.resp, {}) <= 0.3, c.label + ": neutral at its defaults (largest deviation " + juce::String (maxAbsDiff (mDefault.resp, {}), 2) + " dB)");
                if (hasZero)
                {
                    verdicts.check (std::abs (mZero.resp[3] - 6.0) <= 0.8, c.label + ": same bell in the linear-phase structure (" + juce::String (mZero.resp[3], 2) + " dB)");
                    const double expectMs = 1000.0 * (latencyZero - latencyActive) / 48000.0;
                    verdicts.check (latencyZero > latencyActive && graphLatencyZero >= latencyZero && std::abs ((shownZero - shownActive) - expectMs) <= 15.0,
                                    c.label + ": latency change while running reaches MicVST: plug-in " + juce::String (latencyActive) + " -> " + juce::String (latencyZero)
                                    + " samples, chain " + juce::String (graphLatencyActive) + " -> " + juce::String (graphLatencyZero) + ", shown latency "
                                    + juce::String (shownActive, 1) + " -> " + juce::String (shownZero, 1) + " ms (+" + juce::String (expectMs, 1) + " expected)");
                }
                break;
            case Kind::comp:
                verdicts.check (mActive.value <= -8.0, c.label + ": -10 dBFS tone compressed by " + juce::String (-mActive.value, 1) + " dB (threshold -30, ratio 4: about 15 expected)");
                verdicts.check (std::abs (mLow.value) <= 1.0, c.label + ": -40 dBFS tone (below the threshold) unchanged: " + juce::String (mLow.value, 2) + " dB");
                break;
            case Kind::deess:
                verdicts.check (mActive.value <= -6.0, c.label + ": 6.3 kHz (sibilance) reduced by " + juce::String (-mActive.value, 1) + " dB");
                verdicts.check (std::abs (mOther.value) <= 1.5, c.label + ": 300 Hz (voice body) unchanged: " + juce::String (mOther.value, 2) + " dB");
                break;
            case Kind::limiter:
                verdicts.check (mActive.peak <= -6.5, c.label + ": -2 dBFS tone held at " + juce::String (mActive.peak, 2) + " dBFS peak (threshold -8)");
                verdicts.check (latencyActive > latencyDefault, c.label + ": look-ahead reported as latency: " + juce::String (latencyDefault) + " -> " + juce::String (latencyActive)
                                + " samples (5 ms = 240); chain latency " + juce::String (graphLatencyActive));
                break;
            case Kind::sat:
                verdicts.check (mActive.value >= mDefault.value + 20.0, c.label + ": drive adds harmonics: THD+N " + juce::String (mDefault.value, 1) + " -> " + juce::String (mActive.value, 1) + " dB");
                break;
            case Kind::denoise:
                verdicts.check (mDefault.value <= -10.0, c.label + ": steady noise reduced by " + juce::String (-mDefault.value, 1) + " dB at its defaults");
                verdicts.check ((mDefault.value + lvl < -150.0) != (mActive.value + lvl < -150.0) || std::abs (mActive.value - mDefault.value) >= 3.0,
                                c.label + ": VAD threshold change reaches the sound (output " + juce::String (mDefault.value + lvl, 1) + " -> "
                                + juce::String (mActive.value + lvl, 1) + " dBFS RMS; -200 = the voice-activity gate closed)");
                break;
            case Kind::generic:
            {
                const double change = maxAbsDiff (mActive.resp, mDefault.resp);
                if (applied.empty()) verdicts.info (c.label + ": no continuous parameter to move");
                else if (change >= 0.5) verdicts.check (true, c.label + ": the moved parameters change the sound (largest change " + juce::String (change, 2) + " dB)");
                else verdicts.info (c.label + ": the moved parameters changed the steady response by only " + juce::String (change, 2) + " dB (they may act on dynamics/time, not on a steady tone)");
                break;
            }
        }
        // bypass: MicVST takes the plug-in out of the wiring -> the dry signal
        {
            double dev = 0;
            if (s == multitone) dev = maxAbsDiff (mBypass.resp, {});
            else if (sineSignal (s)) dev = std::abs (c.kind == Kind::sat ? toneDb (L, windowOf (phases[pBypass]), 997.0, lvl) : mBypass.value);
            else dev = std::abs (mBypass.value);
            verdicts.check (dev <= (s == noise ? 0.5 : 0.1), c.label + ": bypassed = dry signal (deviation " + juce::String (dev, 3) + " dB)");
            const double back = s == multitone ? maxAbsDiff (mBypassOff.resp, mActive.resp) : std::abs (mBypassOff.value - mActive.value);
            verdicts.check (s == multitone ? back <= 0.25 : sameSound (mBypassOff.value, mActive.value),
                            c.label + ": bypass off = the active sound again (difference " + juce::String (back, 2) + " dB)");
            const double again = s == multitone ? maxAbsDiff (mAgain.resp, mDefault.resp) : std::abs (mAgain.value - mDefault.value);
            verdicts.check (s == multitone ? again <= 0.25 : sameSound (mAgain.value, mDefault.value),
                            c.label + ": defaults restored = the default sound again (difference " + juce::String (again, 2) + " dB)");
        }
        // continuity: dropouts / clicks
        {
            int clk = 0, zr = 0;
            for (auto& ph : phases)
            {
                const auto w = windowOf (ph, 0.3);
                if (sineSignal (s) && c.kind != Kind::sat) clk += sineClicks (L, w, sigFreq (s));
                if (c.kind != Kind::denoise) zr += zeroRuns (L, w);
            }
            verdicts.check (clk == 0 && zr == 0 && countersClean (cBefore, cEnd),
                            c.label + ": no click, dropout or MicVST event while it ran" + (sineSignal (s) && c.kind != Kind::sat ? "" : " (clicks not counted on this signal)")
                            + ": clicks " + juce::String (clk) + ", silent gaps " + juce::String (zr) + ", " + countersDiff (cBefore, cEnd));
        }
        if (editorsWanted())
        {
            if (! editorOpened) verdicts.info (c.label + ": plug-in window: " + editorNote);
            else
            {
                const auto w = windowOf (phases[pEditor], 0.0);
                const auto w2 = windowOf (phases[pEditorClosed], 0.0);
                int clk = 0, zr = zeroRuns (L, w) + zeroRuns (L, w2);
                if (sineSignal (s) && c.kind != Kind::sat) clk = sineClicks (L, w, sigFreq (s)) + sineClicks (L, w2, sigFreq (s));
                if (c.kind == Kind::denoise) zr = 0;
                verdicts.check (clk == 0 && zr == 0 && countersClean (cEditorOpen, cEditorClosed),
                                c.label + ": plug-in window " + editorNote + ", closed after 2.5 s: clicks " + juce::String (clk) + ", silent gaps " + juce::String (zr)
                                + ", " + countersDiff (cEditorOpen, cEditorClosed));
            }
        }
        juce::ignoreUnused (R, fs);

        // saved through config.xml and restored into a new engine (as at program start)
        {
            const auto xml = toValueTree (saved).createXml();
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("micvst_suite", ".xml");
            const bool written = xml != nullptr && xml->writeTo (file);
            MicVSTState back;
            if (auto x = juce::XmlDocument::parse (file)) back = fromValueTree (juce::ValueTree::fromXml (*x));
            file.deleteFile();
            back.inputDevice = "VMic"; back.outputDevice = "VCable"; back.bufferSize = 480;
            Metric mRestored, mMonoL, mMonoR;
            std::vector<float> restoredParams;
            juce::String restoredName;
            for (int pass = 0; pass < 2; ++pass)   // 0: stereo microphone, 1: mono microphone
            {
                cfg.micChannels = pass == 0 ? 2 : 1;
                setSignal (s, lvl);
                Phase ph;
                {
                    vdev::recorder().reset ((size_t) (14.0 * fs));
                    vdev::stats().reset();
                    auto e = std::make_unique<AudioEngine>();
                    e->getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                    e->getKnownPlugins().addType (c.desc);
                    e->applyState (back);
                    pump (0.05);
                    raiseEngineThreads();
                    if (pass == 0)
                        if (auto* p = procAt (*e, 0)) { restoredParams = paramSnapshot (*p); restoredName = p->getName(); }
                    pump (1.0);
                    ph.t0 = now(); pump (3.0); ph.t1 = now();
                    e.reset();
                }
                const auto w = windowOf (ph, 1.0);
                if (pass == 0) mRestored = measure (c.kind, s, lvl, w, vdev::recorder().left);
                else { mMonoL = measure (c.kind, s, lvl, w, vdev::recorder().left); mMonoR = measure (c.kind, s, lvl, w, vdev::recorder().right); }
            }
            cfg.micChannels = 2;
            int same = 0;
            for (size_t i = 0; i < restoredParams.size() && i < activeParams.size(); ++i) same += std::abs (restoredParams[i] - activeParams[i]) < 1.0e-5f ? 1 : 0;
            pr ("| restored from config.xml (new engine) | " + mRestored.text + " |");
            pr ("| mono microphone, left | " + mMonoL.text + " |");
            pr ("| mono microphone, right | " + mMonoR.text + " |");
            verdicts.check (written && restoredName == c.desc.name && same == (int) activeParams.size() && ! activeParams.empty(),
                            c.label + ": saved and restored through config.xml: '" + restoredName + "', parameters identical " + juce::String (same) + "/"
                            + juce::String ((int) activeParams.size()) + ", state " + juce::String (saved.plugins.isEmpty() ? 0 : (int) saved.plugins[0].state.getSize()) + " bytes");
            const double diff = s == multitone ? maxAbsDiff (mRestored.resp, mActive.resp) : std::abs (mRestored.value - mActive.value);
            verdicts.check (s == multitone ? diff <= 0.25 : sameSound (mRestored.value, mActive.value),
                            c.label + ": the restored plug-in sounds like the saved one (difference " + juce::String (diff, 2) + " dB)");
            const double lr = s == multitone ? maxAbsDiff (mMonoL.resp, mMonoR.resp) : std::abs (mMonoL.value - mMonoR.value);
            const double vsStereo = s == multitone ? maxAbsDiff (mMonoL.resp, mActive.resp) : std::abs (mMonoL.value - mActive.value);
            verdicts.check (lr <= 0.1 && (s == multitone ? vsStereo <= 0.3 : (sameSound (mMonoL.value, mActive.value) || vsStereo <= 0.3)) && mMonoR.peak > -150.0,
                            c.label + ": mono microphone: both cable channels carry the processed voice (L/R difference " + juce::String (lr, 2)
                            + " dB, vs. stereo " + juce::String (vsStereo, 2) + " dB)");
        }
    }

    //==============================================================================
    // Several plug-ins together.
    int indexOf (const std::vector<Config>& cs, Kind k)
    {
        for (size_t i = 0; i < cs.size(); ++i) if (cs[i].kind == k) return (int) i;
        return -1;
    }

    // Builds the chain in the given order of configurations (-1 = Otomatik Kazanc, -2 - k = level tap k),
    // applies each one's active settings. Returns the applied settings per chain entry.
    void buildChain (AudioEngine& e, const std::vector<Config>& cs, const std::vector<int>& order, bool autoGainOn = true)
    {
        for (int idx : order)
        {
            juce::String err;
            if (idx == -1) { e.getChain().addAutoGain (autoGainOn); continue; }
            if (idx <= -2) { e.getChain().addPlugin (e.getFormatManager(), testDesc ("TestTap", (double) (-2 - idx)), e.getProcessingSampleRate(), e.getProcessingBlockSize(), err); continue; }
            const auto& c = cs[(size_t) idx];
            if (! e.getChain().addPlugin (e.getFormatManager(), c.desc, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err))
            { pr ("  cannot add " + c.label + ": " + err); continue; }
            auto* p = procAt (e, (int) e.getChain().entries().size() - 1);
            if (p != nullptr && c.kind != Kind::generic) applySettings (*p, c.active);
        }
        const AudioEngine::ChainEdit ce (e);
        e.rebuildGraph();
    }

    juce::String orderText (const std::vector<Config>& cs, const std::vector<int>& order)
    {
        juce::StringArray s;
        for (int i : order)
        {
            if (i == -1) s.add ("Otomatik Kazanc");
            else if (i <= -2) continue;
            else s.add (cs[(size_t) i].label.upToFirstOccurrenceOf (" (", false, false) + (cs[(size_t) i].kind == Kind::deess ? " [de-esser]" : cs[(size_t) i].kind == Kind::limiter ? " [limiter]" : ""));
        }
        return s.joinIntoString (" -> ");
    }

    void chainTests (const std::vector<Config>& cs)
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;
        const int iDen = indexOf (cs, Kind::denoise), iEq = indexOf (cs, Kind::eq), iComp = indexOf (cs, Kind::comp), iDe = indexOf (cs, Kind::deess),
                  iLim = indexOf (cs, Kind::limiter), iSat = indexOf (cs, Kind::sat);
        std::vector<int> generics;
        for (size_t i = 0; i < cs.size(); ++i) if (cs[i].kind == Kind::generic) generics.push_back ((int) i);

        // ---- 1. a chain like the user's: Otomatik Kazanc first, every effect, realistic speech ----
        pr ("");
        pr ("## Chain like the user's (Otomatik Kazanc first, all effects, realistic speech at -20 dBFS typical, 40 s)");
        std::vector<int> order { -1 };
        for (int i : { iDen, iDe, iEq, iComp, iSat }) if (i >= 0) order.push_back (i);
        for (int g : generics) order.push_back (g);
        if (iDe >= 0) order.push_back (iDe);
        if (iLim >= 0) order.push_back (iLim);
        pr ("order: " + orderText (cs, order));
        double baselineShown = 0;
        { setSignal (speech, -20.0); EngineRun run (480, 6.0); pump (5.0); baselineShown = run.engine->getDiagnostics().latencyMs; }
        {
            setSignal (speech, -20.0);
            int sumLatency = 0, graphLatency = 0;
            Counters c0, c1;
            double dspAvg = 0, dspPeak = 0, shown = 0;
            juce::String diag;
            int entries = 0;
            {
                EngineRun run (480, 46.0);
                auto& e = *run.engine;
                e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                buildChain (e, cs, order);
                entries = (int) e.getChain().entries().size();
                pump (4.0);
                c0 = counters (e);
                for (int k = 0; k < 8; ++k) { pump (5.0); const auto d = e.getDiagnostics(); dspAvg += d.dspLoadPercent / 8.0; dspPeak = std::max (dspPeak, d.dspPeakPercent); }
                c1 = counters (e);
                for (int i = 0; i < entries; ++i) if (auto* p = procAt (e, i)) sumLatency += p->getLatencySamples();
                graphLatency = e.getGraph().getLatencySamples();
                shown = e.getDiagnostics().latencyMs;
                diag = diagLine (e);
            }
            const auto& L = vdev::recorder().left;
            Window w; w.from = (size_t) (8.0 * fs); w.to = std::min (L.size(), (size_t) (44.0 * fs));
            pr ("  " + diag);
            pr ("  " + juce::String (entries) + " elements; plug-in latencies add up to " + juce::String (sumLatency) + " samples, chain latency " + juce::String (graphLatency)
                + " samples (" + juce::String (graphLatency / 48.0, 1) + " ms), MicVST shows " + juce::String (shown, 1) + " ms; plug-in thread load average "
                + juce::String (dspAvg, 1) + " %, peak " + juce::String (dspPeak, 1) + " %; output peak " + juce::String (peakDbOf (L, w), 2) + " dBFS, RMS " + juce::String (rmsDb (L, w), 1) + " dBFS");
            const double addedMs = shown - baselineShown, expectMs = sumLatency / 48.0;
            verdicts.check (std::abs (addedMs - expectMs) <= 3.0, "chain: MicVST's shown latency includes the plug-ins' latencies: +" + juce::String (addedMs, 1)
                            + " ms over the empty chain (" + juce::String (baselineShown, 1) + " ms), the plug-ins report " + juce::String (sumLatency) + " samples = "
                            + juce::String (expectMs, 1) + " ms (JUCE's own graph figure: " + juce::String (graphLatency) + " samples)");
            verdicts.check (countersClean (c0, c1), "chain: 40 s of speech through " + juce::String (entries) + " elements without a MicVST event (" + countersDiff (c0, c1) + ")");
            verdicts.check (peakDbOf (L, w) < 0.0 && peakDbOf (L, w) > -60.0, "chain: the voice comes out, below full scale (peak " + juce::String (peakDbOf (L, w), 2) + " dBFS)");
        }

        // ---- 2. chain edits with real plug-ins while audio plays ----
        pr ("");
        pr ("## Chain edits during playback with real plug-ins (997 Hz -16 dBFS; one edit per 1.2 s)");
        {
            std::vector<int> ord { -1 };
            for (int i : { iEq, iComp, iDe, iLim }) if (i >= 0) ord.push_back (i);
            pr ("start: " + orderText (cs, ord));
            setSignal (sine997, -16.0);
            std::vector<std::pair<double, juce::String>> marks;
            Counters c0, c1;
            juce::String finalOrder;
            {
                EngineRun run (480, 80.0);
                auto& e = *run.engine;
                e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                buildChain (e, cs, ord);
                pump (3.0);
                c0 = counters (e);
                auto edit = [&] (const juce::String& what, std::function<void (PluginChain&)> f)
                {
                    marks.push_back ({ now(), what });
                    editChain (e, f);
                    pump (1.2);
                };
                const int n = (int) e.getChain().entries().size();
                for (int i = 0; i < n; ++i)
                {
                    edit ("bypass " + juce::String (i + 1) + " on",  [i] (PluginChain& ch) { ch.setBypass (i, true); });
                    edit ("bypass " + juce::String (i + 1) + " off", [i] (PluginChain& ch) { ch.setBypass (i, false); });
                }
                edit ("move Otomatik Kazanc to the end", [n] (PluginChain& ch) { ch.movePlugin (0, n - 1); });
                edit ("move it back to the top",         [n] (PluginChain& ch) { ch.movePlugin (n - 1, 0); });
                for (int k = 0; k < 3; ++k)
                    edit ("move the last effect to position 2", [] (PluginChain& ch) { ch.movePlugin ((int) ch.entries().size() - 1, 1); });
                if (iEq >= 0)
                {
                    edit ("add a second EQ", [&] (PluginChain& ch) { juce::String err; ch.addPlugin (e.getFormatManager(), cs[(size_t) iEq].desc, e.getProcessingSampleRate(), e.getProcessingBlockSize(), err); });
                    edit ("remove it", [] (PluginChain& ch) { ch.removePlugin ((int) ch.entries().size() - 1); });
                }
                edit ("remove effect 2", [] (PluginChain& ch) { ch.removePlugin (1); });
                pump (1.5);
                c1 = counters (e);
                finalOrder = juce::String ((int) e.getChain().entries().size()) + " elements";
            }
            const auto& L = vdev::recorder().left;
            const double t0 = vdev::recorder().outputStart.load();
            int clicked = 0, gapsAfter = 0;
            pr ("| edit | clicks in the following 1.2 s | longest silence (ms) |");
            pr ("|---|---:|---:|");
            for (auto& mk : marks)
            {
                Window w; w.from = (size_t) std::max (0.0, (mk.first - t0) * fs); w.to = std::min (L.size(), w.from + (size_t) (1.2 * fs));
                // the click limit follows the louder of the tone before and after the edit (an edit
                // that changes the level - a compressor back in - is faded, no click)
                const size_t back = (size_t) (0.6 * fs);
                const double ampBefore = w.from > back ? toneAmp (L, Window { w.from - back, w.from - (size_t) (0.1 * fs) }, 997.0) : 0.0;
                const double ampAfter = toneAmp (L, Window { std::min (w.to, w.from + back), w.to }, 997.0);
                std::vector<size_t> at;
                clicks (L, w.from, w.to, std::max ({ 1.0e-3, ampBefore, ampAfter }), 997.0 / fs, 480, &at);
                int c = 0;
                juce::StringArray where;
                for (auto i : at) { if (machineStallNear (i)) { ++machineStalls; continue; } ++c; where.add (juce::String (1000.0 * (double) (i - w.from) / fs, 1) + " ms after"); }
                int z = 0, longest = 0;
                for (size_t i = w.from; i < w.to; ++i) { if (L[i] == 0.0f) longest = std::max (longest, ++z); else z = 0; }
                clicked += c > 0 ? 1 : 0;
                gapsAfter += longest > (int) (0.1 * fs) ? 1 : 0;
                pr ("| " + mk.second + " | " + juce::String (c) + (where.isEmpty() ? juce::String() : " (" + where.joinIntoString (", ") + ")") + " | " + juce::String (1000.0 * longest / fs, 1) + " |");
            }
            verdicts.check (clicked == 0, "chain edits with real plug-ins: " + juce::String ((int) marks.size()) + " edits, followed by a click: " + juce::String (clicked));
            verdicts.check (gapsAfter == 0 && countersClean (c0, c1), "chain edits: no silence longer than 100 ms, no MicVST event (" + countersDiff (c0, c1) + ")");
        }

        // ---- 3. levels between the effects, Otomatik Kazanc first (on / off), quiet to shouting ----
        pr ("");
        pr ("## Levels between the effects with Otomatik Kazanc first (taps after each element; realistic speech, 26 s per run, measured over the last 14 s)");
        {
            std::vector<int> ord { -2 - 15, -1, -2 - 0 };   // tap 15 = microphone, tap 0 = after Otomatik Kazanc
            std::vector<juce::String> tapNames (16);
            tapNames[15] = "microphone"; tapNames[0] = "after Otomatik Kazanc";
            int tap = 1;
            for (int i : { iDen, iEq, iDe, iComp, iSat, iLim })
                if (i >= 0) { ord.push_back (i); tapNames[(size_t) tap] = "after " + cs[(size_t) i].label.upToFirstOccurrenceOf (" (", false, false)
                                                                            + (cs[(size_t) i].kind == Kind::deess ? " [de-esser]" : cs[(size_t) i].kind == Kind::limiter ? " [limiter]" : "");
                              ord.push_back (-2 - tap); ++tap; }
            pr ("order: " + orderText (cs, ord));
            struct Run { juce::String what; double typ; bool autoOn; double shoutDb; };
            const std::vector<Run> runs { { "quiet voice (-32 dBFS typical), Otomatik Kazanc on", -32.0, true, 0 },
                                          { "quiet voice (-32 dBFS typical), Otomatik Kazanc off", -32.0, false, 0 },
                                          { "normal voice (-20 dBFS typical), on", -20.0, true, 0 },
                                          { "normal voice (-20 dBFS typical), off", -20.0, false, 0 },
                                          { "loud voice (-14 dBFS typical), on", -14.0, true, 0 },
                                          { "shouting (-24 dBFS voice, +12 dB for 4 s), on", -24.0, true, 12.0 },
                                          { "shouting (-24 dBFS voice, +12 dB for 4 s), off", -24.0, false, 12.0 } };
            const auto base = speechData;
            pr ("| run | " + [&] { juce::StringArray h; for (int t : std::vector<int> { 15, 0 }) h.add (tapNames[(size_t) t]); for (int t = 1; t < tap; ++t) h.add (tapNames[(size_t) t]); return h.joinIntoString (" | "); }()
                + " | gain shown |");
            pr ("|---|" + juce::String::repeatedString ("---:|", tap + 1) + "---:|");
            int overFs = 0;
            for (auto& r : runs)
            {
                speechData = base;
                if (r.shoutDb > 0)   // a shout from 14 s to 18 s of the run (the measurement covers it)
                    for (size_t i = (size_t) (14.0 * fs); i < std::min (speechData.size(), (size_t) (18.0 * fs)); ++i) speechData[i] *= (float) juce::Decibels::decibelsToGain (r.shoutDb);
                setSignal (speech, r.typ);
                AutoGainStage::Meters m;
                {
                    EngineRun run (480, 28.0);
                    auto& e = *run.engine;
                    e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                    buildChain (e, cs, ord, r.autoOn);
                    pump (11.0);
                    taps().reset(); taps().armed = true;
                    pump (14.0);
                    taps().armed = false;
                    e.getChain().takeAutoGainMeters (autoGainIndex (e.getChain()), m);
                }
                auto cell = [] (int t)
                {
                    auto& tp = taps();
                    const auto n = tp.count[(size_t) t].load();
                    if (n == 0) return juce::String ("-");
                    return juce::String (juce::Decibels::gainToDecibels ((double) tp.peak[(size_t) t].load(), -200.0), 1) + " / "
                         + juce::String (juce::Decibels::gainToDecibels (std::sqrt (tp.sum[(size_t) t].load() / (double) n), -200.0), 1)
                         + (tp.overFullScale[(size_t) t].load() > 0 ? " (>= 0 dBFS: " + juce::String ((juce::int64) tp.overFullScale[(size_t) t].load()) + ")" : juce::String());
                };
                for (int t = 0; t < 16; ++t) overFs += (int) taps().overFullScale[(size_t) t].load();
                juce::StringArray cells; cells.add (cell (15)); cells.add (cell (0)); for (int t = 1; t < tap; ++t) cells.add (cell (t));
                pr ("| " + r.what + " | " + cells.joinIntoString (" | ") + " | " + (r.autoOn ? juce::String (m.gainDb, 2) + " dB" : juce::String ("off")) + " |");
                if (r.autoOn) verdicts.check (taps().peak[0].load() <= juce::Decibels::decibelsToGain (AutoGainStage::ceilingDb + 0.05),
                                              "Otomatik Kazanc first, " + r.what + ": the effects never get more than -10 dBFS (peak after it "
                                              + juce::String (juce::Decibels::gainToDecibels ((double) taps().peak[0].load(), -200.0), 2) + " dBFS)");
            }
            speechData = base;
            pr ("(peak / RMS in dBFS at each point)");
            verdicts.check (overFs == 0, "no sample at or above full scale anywhere in the chain in these runs (" + juce::String (overFs) + ")");
        }

        // ---- 4. other orders: the same effects, the sound differs - MicVST plays the order shown ----
        pr ("");
        pr ("## Other orders of the same effects (normal voice -20 dBFS typical, Otomatik Kazanc on; output over the last 14 s)");
        {
            std::vector<std::pair<juce::String, std::vector<int>>> orders;
            std::vector<int> a { -1 }; for (int i : { iDen, iEq, iDe, iComp, iSat, iLim }) if (i >= 0) a.push_back (i);
            orders.push_back ({ "recommended", a });
            std::vector<int> b { -1 }; for (int i : { iDen, iSat, iComp, iEq, iDe, iLim }) if (i >= 0) b.push_back (i);
            orders.push_back ({ "saturation before compressor and EQ", b });
            std::vector<int> d2 { -1 }; for (int i : { iLim, iDen, iEq, iDe, iComp, iSat }) if (i >= 0) d2.push_back (i);
            orders.push_back ({ "limiter first, saturation last", d2 });
            std::vector<int> g; for (int i : { iDen, iEq, iDe, iComp, iSat, iLim }) if (i >= 0) g.push_back (i); g.push_back (-1);
            orders.push_back ({ "Otomatik Kazanc last", g });
            pr ("| order | output peak (dBFS) | RMS (dBFS) | crest (dB) | 100-300 Hz | 1-3 kHz | 5-10 kHz (band RMS, dBFS) |");
            pr ("|---|---:|---:|---:|---:|---:|---:|");
            for (auto& [name, ord] : orders)
            {
                setSignal (speech, -20.0);
                {
                    EngineRun run (480, 28.0);
                    auto& e = *run.engine;
                    e.getFormatManager().addFormat (std::make_unique<juce::VST3PluginFormat>());
                    buildChain (e, cs, ord);
                    pump (26.0);
                }
                const auto& L = vdev::recorder().left;
                Window w; w.from = (size_t) (12.0 * fs); w.to = std::min (L.size(), (size_t) (26.0 * fs));
                // band levels: RMS through a 4th-order band-pass (two RBJ biquads)
                auto band = [&] (double lo, double hi)
                {
                    const double fc = std::sqrt (lo * hi), q = fc / (hi - lo), w0 = twoPi * fc / fs, al = std::sin (w0) / (2.0 * q);
                    const double a0 = 1.0 + al, b0 = al / a0, b2 = -al / a0, a1 = -2.0 * std::cos (w0) / a0, a2 = (1.0 - al) / a0;
                    double x1[2] = {}, x2[2] = {}, y1[2] = {}, y2[2] = {}, e = 0;
                    for (size_t i = w.from; i < w.to; ++i)
                    {
                        double v = L[i];
                        for (int st = 0; st < 2; ++st)
                        {
                            const double y = b0 * v + b2 * x2[st] - a1 * y1[st] - a2 * y2[st];
                            x2[st] = x1[st]; x1[st] = v; y2[st] = y1[st]; y1[st] = y; v = y;
                        }
                        e += v * v;
                    }
                    return juce::Decibels::gainToDecibels (std::sqrt (e / (double) std::max<size_t> (1, w.size())), -200.0);
                };
                const double pk = peakDbOf (L, w), rm = rmsDb (L, w);
                pr ("| " + name + ": " + orderText (cs, ord) + " | " + juce::String (pk, 2) + " | " + juce::String (rm, 1) + " | " + juce::String (pk - rm, 1)
                    + " | " + juce::String (band (100, 300), 1) + " | " + juce::String (band (1000, 3000), 1) + " | " + juce::String (band (5000, 10000), 1) + " |");
            }
        }
    }

    void run()
    {
        installSignal();
        {
            speech::Params p;
            p.fs = vdev::config().micRate; p.peakDb = -20.0; p.seed = 11; p.noiseDb = -65.0; p.plosiveDb = -10.0;
            speechData = speech::generate (60.0, p).x;
        }
        const auto found = findPlugins();
        pr ("## VST3 suite: real plug-ins in MicVST's engine ($MICVST_TEST_VST3_DIR = " + juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_VST3_DIR", "(not set)") + ")");
        for (auto& d : found) pr ("found: " + d.name + " " + d.version + " (" + d.manufacturerName + ") - " + d.fileOrIdentifier);
        if (found.empty()) { pr ("no plug-ins found"); return; }
        const auto cs = buildConfigs (found);
        pr ("plug-in windows: " + juce::String (editorsWanted() ? "opened and closed during playback" : "not tested here (no display)"));
        const auto only = juce::SystemStats::getEnvironmentVariable ("ETEST_SUITE_ONLY", {});   // e.g. "eq" / "chain"
        for (auto& c : cs)
            if (only.isEmpty() || kindName (c.kind).containsIgnoreCase (only) || c.label.containsIgnoreCase (only))
                testOne (c);
        if (only.isEmpty() || only == "chain")
            chainTests (cs);
        pr ("");
        pr ("## VST3 suite result: " + juce::String (verdicts.pass) + " passed, " + juce::String (verdicts.fail) + " failed, " + juce::String (verdicts.note) + " notes; "
            + juce::String (machineStalls) + " click/gap findings put down to stalls of the test machine (late virtual cable / lost microphone packet)");
        for (auto& f : verdicts.failures) pr ("  failed: " + f);
    }
}
