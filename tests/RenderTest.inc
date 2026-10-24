// MicVSTSnapshot --render <folder>: MicVST's main window on the real Windows desktop, its drawing
// engine switched between the graphics card (Direct2D) and the processor (GDI) like
// Ayarlar > Arayüz çizimi does (micvst::render::setMode), while the screen - what Windows' compositor
// (DWM) really shows - is captured every few milliseconds from another thread.
//
// A marker in the window changes its colour in the same message-loop turn as the switch
// (red under the old engine, blue under the new one). After the switch:
//   ghost   = the old colour still on screen later than one refresh (50 ms) after the switch
//   flicker = old and new colours alternating (new, old, new ...)
//   flash   = neither colour in the marker, or a still part of the window (bottom bar, device row)
//             different from both the picture before and the picture after the switch
// Every switch direction is repeated; results go to <folder>/render-switch.md (+ .csv).
// Windows only (elsewhere it says so and returns 0).
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")
#endif

namespace rendertest
{
#if JUCE_WINDOWS
    struct Marker final : juce::Component
    {
        juce::Colour colour { juce::Colours::red };
        Marker() { setOpaque (true); }
        void paint (juce::Graphics& g) override { g.fillAll (colour); }
    };

    struct Window final : juce::DocumentWindow
    {
        Window() : juce::DocumentWindow ("MicVST render test", micvst::theme::background, juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
        }
        void closeButtonPressed() override {}
    };

    // what the screen shows in physical pixels (DWM's composition, Direct2D's swap chain included)
    struct Grabber
    {
        HDC screen = nullptr, mem = nullptr;
        HBITMAP bmp = nullptr;
        HGDIOBJ old = nullptr;
        uint32_t* bits = nullptr;
        int w = 0, h = 0;

        Grabber (int width, int height) : w (width), h (height)
        {
            screen = GetDC (nullptr);
            mem = CreateCompatibleDC (screen);
            BITMAPINFO bi {};
            bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = w;
            bi.bmiHeader.biHeight = -h;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            bmp = CreateDIBSection (mem, &bi, DIB_RGB_COLORS, (void**) &bits, nullptr, 0);
            old = SelectObject (mem, bmp);
        }
        ~Grabber()
        {
            SelectObject (mem, old);
            DeleteObject (bmp);
            DeleteDC (mem);
            ReleaseDC (nullptr, screen);
        }
        bool grab (int x, int y) { return BitBlt (mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT) != 0; }
        juce::Colour at (int x, int y) const
        {
            const auto p = bits[(size_t) y * (size_t) w + (size_t) x];
            return juce::Colour ((juce::uint8) ((p >> 16) & 0xff), (juce::uint8) ((p >> 8) & 0xff), (juce::uint8) (p & 0xff));
        }
        // mean colour of a rectangle (physical pixels relative to the grab)
        juce::Colour mean (juce::Rectangle<int> r) const
        {
            double s[3] {};
            int n = 0;
            for (int y = juce::jmax (0, r.getY()); y < juce::jmin (h, r.getBottom()); ++y)
                for (int x = juce::jmax (0, r.getX()); x < juce::jmin (w, r.getRight()); ++x)
                {
                    const auto c = at (x, y);
                    s[0] += c.getRed(); s[1] += c.getGreen(); s[2] += c.getBlue(); ++n;
                }
            if (n == 0) return {};
            return juce::Colour ((juce::uint8) (s[0] / n), (juce::uint8) (s[1] / n), (juce::uint8) (s[2] / n));
        }
    };

    inline int distance (juce::Colour a, juce::Colour b)
    {
        return std::abs ((int) a.getRed() - (int) b.getRed()) + std::abs ((int) a.getGreen() - (int) b.getGreen())
             + std::abs ((int) a.getBlue() - (int) b.getBlue());
    }

    // the marker's colour as the screen shows it: the same hue, its brightness possibly scaled (a CI
    // runner's display showed every colour at 81 %) - so compared by direction and brightness ratio
    inline bool sameColour (juce::Colour seen, juce::Colour ref)
    {
        const double a[] { (double) seen.getRed(), (double) seen.getGreen(), (double) seen.getBlue() };
        const double b[] { (double) ref.getRed(), (double) ref.getGreen(), (double) ref.getBlue() };
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (int i = 0; i < 3; ++i) { dot += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
        if (na < 1.0 || nb < 1.0) return false;
        const double ratio = std::sqrt (na / nb);
        return dot / std::sqrt (na * nb) > 0.985 && ratio > 0.55 && ratio < 1.25;
    }

    inline double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    struct Sample { double t; juce::Colour marker; std::vector<juce::Colour> probes; };

    struct SwitchResult
    {
        juce::String direction;
        int engineBefore = -1, engineAfter = -1;
        bool markerOkBefore = false;      // the old colour was on screen before the switch (capture works, engine draws)
        double switchMs = 0.0;            // how long setMode took
        double newColourAtMs = -1.0;      // first capture with the new colour, after the switch call
        double lastOldColourMs = -1.0;    // last capture with the old colour, after the switch returned
        int alternations = 0;             // new -> old changes (flicker)
        int flashFrames = 0;              // marker neither colour, or a still probe unlike before and after
        int captures = 0;
        bool newColourStable = false;     // the new colour at the end
        bool pass() const
        {
            return markerOkBefore && newColourStable && alternations == 0 && flashFrames == 0
                && (lastOldColourMs < 0.0 || lastOldColourMs <= 50.0);
        }
    };

    inline int run (const juce::File& dir, AudioEngine& engine, int cycles)
    {
        auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
        dir.createDirectory();

        auto stage = [] (const char* what) { std::printf ("render test: %s\n", what); std::fflush (stdout); };
        stage ("start");
        micvst::render::startSession (micvst::render::Mode::gpuOnly);
        Window win;
        auto* mc = new MainComponent (engine);
        win.setContentOwned (mc, true);
        win.centreWithSize (640, 760);
        Marker marker;
        mc->addAndMakeVisible (marker);
        marker.setBounds (8, 8, 44, 44);
        stage ("window built");
        win.setVisible (true);
        micvst::render::attach (win);
        win.toFront (true);
        pump (1500);
        stage ("window shown");

        auto* hwnd = (HWND) win.getPeer()->getNativeHandle();
        SetWindowPos (hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);   // nothing may cover it on the runner
        pump (300);

        RECT cr {};
        GetClientRect (hwnd, &cr);
        POINT origin { 0, 0 };
        ClientToScreen (hwnd, &origin);
        const int pw = cr.right - cr.left, ph = cr.bottom - cr.top;
        const double scale = (double) pw / juce::jmax (1, mc->getWidth());
        auto phys = [&] (juce::Rectangle<int> logical)
        {
            return juce::Rectangle<int> (juce::roundToInt (logical.getX() * scale), juce::roundToInt (logical.getY() * scale),
                                         juce::roundToInt (logical.getWidth() * scale), juce::roundToInt (logical.getHeight() * scale));
        };
        const auto markerPx = phys (marker.getBounds()).reduced (juce::roundToInt (6 * scale));
        // still parts of the window: bottom-right corner (settings button), left of the device row,
        // bottom-left (Ham ses button)
        const std::vector<juce::Rectangle<int>> probesPx {
            phys ({ mc->getWidth() - 60, mc->getHeight() - 40, 30, 24 }),
            phys ({ 20, mc->getHeight() - 40, 30, 24 }),
            phys ({ mc->getWidth() / 2 - 15, mc->getHeight() / 2, 30, 20 })
        };

        juce::String md;
        md << "# GPU / CPU switch of the main window: ghost, flicker, flash (screen captures)\n\n"
           << "Window client " << pw << " x " << ph << " px (scale " << juce::String (scale, 2) << "), "
           << "graphics card usable for Otomatik: " << (micvst::render::gpuUsable() ? "yes" : "no (Basic Render Driver: Direct2D still forced for this test)") << ".\n"
           << "Each switch: the marker turns from the old colour to the new one in the same message-loop turn as "
           << "micvst::render::setMode; the screen is captured from another thread every ~2 ms for 900 ms.\n"
           << "Pass: the old colour gone within 50 ms (one refresh + capture), never back (no flicker), no frame unlike "
           << "both the picture before and after (no flash).\n\n"
           << "| # | switch | engine before -> after | old colour on screen before | setMode ms | new colour first at ms | old colour last at ms | flicker (new->old) | flash frames | captures | result |\n"
           << "|---|---|---|---|---|---|---|---|---|---|---|\n";
        juce::String csv ("cycle,direction,t_ms,marker_r,marker_g,marker_b\n");

        stage ("measuring");
        const juce::Colour colours[] { juce::Colours::red, juce::Colour (0xff1e5bff), juce::Colour (0xff00c853), juce::Colour (0xffffb000) };
        int failures = 0, total = 0;
        bool d2dWorks = true;

        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            for (int dirIdx = 0; dirIdx < 2; ++dirIdx)
            {
                const bool toCpu = dirIdx == 0;   // GPU -> CPU first (the reported case), then back
                const auto oldColour = colours[(cycle * 2 + dirIdx) % 4];
                const auto newColour = colours[(cycle * 2 + dirIdx + 1) % 4];
                SwitchResult r;
                r.direction = toCpu ? "GPU -> CPU" : "CPU -> GPU";

                marker.colour = oldColour;
                marker.repaint();
                pump (700);
                r.engineBefore = win.getPeer()->getCurrentRenderingEngine();

                // the picture before (still probes) and the check that the old colour is really on screen
                std::vector<juce::Colour> before;
                {
                    Grabber g (pw, ph);
                    g.grab (origin.x, origin.y);
                    r.markerOkBefore = sameColour (g.mean (markerPx), oldColour);
                    for (auto& p : probesPx) before.push_back (g.mean (p));
                }

                std::vector<Sample> samples;
                samples.reserve (1000);
                std::atomic<bool> stop { false };
                const double t0 = nowMs();
                std::thread capturer ([&]
                {
                    Grabber g (pw, ph);
                    while (! stop.load())
                    {
                        if (g.grab (origin.x, origin.y))
                        {
                            Sample s { nowMs() - t0, g.mean (markerPx), {} };
                            for (auto& p : probesPx) s.probes.push_back (g.mean (p));
                            samples.push_back (std::move (s));
                        }
                        Sleep (1);
                    }
                });
                Sleep (60);   // a few captures of the old picture

                const double ts = nowMs() - t0;
                marker.colour = newColour;
                marker.repaint();
                micvst::render::setMode (toCpu ? micvst::render::Mode::cpuOnly : micvst::render::Mode::gpuOnly);
                const double te = nowMs() - t0;
                r.switchMs = te - ts;
                pump (900);
                stop = true;
                capturer.join();
                r.engineAfter = win.getPeer()->getCurrentRenderingEngine();

                // the picture after (still probes)
                std::vector<juce::Colour> after;
                {
                    Grabber g (pw, ph);
                    g.grab (origin.x, origin.y);
                    for (auto& p : probesPx) after.push_back (g.mean (p));
                    r.newColourStable = sameColour (g.mean (markerPx), newColour);
                }

                bool sawNew = false;
                for (auto& s : samples)
                {
                    csv << cycle << "," << r.direction << "," << juce::String (s.t, 1) << ","
                        << (int) s.marker.getRed() << "," << (int) s.marker.getGreen() << "," << (int) s.marker.getBlue() << "\n";
                    if (s.t < ts) continue;   // before the switch call
                    // (the spectrum and meters move; the marker and the still probes do not)
                    ++r.captures;
                    const bool isOld = sameColour (s.marker, oldColour), isNew = sameColour (s.marker, newColour);
                    if (isNew && ! sawNew) { sawNew = true; r.newColourAtMs = s.t - ts; }
                    if (isOld && s.t > te) r.lastOldColourMs = s.t - te;
                    if (isOld && sawNew) ++r.alternations;
                    bool flash = ! isOld && ! isNew;
                    for (size_t i = 0; i < s.probes.size(); ++i)
                        if (distance (s.probes[i], before[i]) > 45 && distance (s.probes[i], after[i]) > 45) flash = true;
                    if (flash) ++r.flashFrames;
                }
                if (! r.markerOkBefore && ! toCpu) d2dWorks = false;   // Direct2D drew nothing on this machine
                ++total;
                if (! r.pass()) ++failures;
                md << "| " << (cycle + 1) << " | " << r.direction << " | " << r.engineBefore << " -> " << r.engineAfter << " | "
                   << (r.markerOkBefore ? "yes" : "NO") << " | " << juce::String (r.switchMs, 1) << " | "
                   << (r.newColourAtMs >= 0 ? juce::String (r.newColourAtMs, 1) : juce::String ("never")) << " | "
                   << (r.lastOldColourMs >= 0 ? juce::String (r.lastOldColourMs, 1) : juce::String ("-")) << " | "
                   << r.alternations << " | " << r.flashFrames << " | " << r.captures << " | " << (r.pass() ? "PASS" : "FAIL") << " |\n";
            }
        }

        md << "\nEngines: 0 = processor (GDI), 1 = graphics card (Direct2D).\n"
           << "\n**Switches: " << total << ", failures: " << failures << "**"
           << (d2dWorks ? "" : " - Direct2D drew nothing before a switch on this machine: the GPU side is not tested here (ENGELLENDİ)") << "\n";

        // --- the spectrum and meters moving: frames, pauses, processor time, per engine and FPS ---
        stage ("animation");
        marker.setVisible (false);
        auto processCpuMs = []
        {
            FILETIME c, e, k, u;
            GetProcessTimes (GetCurrentProcess(), &c, &e, &k, &u);
            auto ms = [] (FILETIME f) { return (double) (((uint64_t) f.dwHighDateTime << 32) | f.dwLowDateTime) / 10000.0; };
            return ms (k) + ms (u);
        };
        auto measure = [&] (int ms, double& cpuPercent)
        {
            const double c0 = processCpuMs(), w0 = nowMs();
            pump (ms);
            cpuPercent = 100.0 * (processCpuMs() - c0) / (nowMs() - w0);
        };
        auto resources = [&]
        {
            PROCESS_MEMORY_COUNTERS_EX pm {};
            GetProcessMemoryInfo (GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*) &pm, sizeof (pm));
            DWORD handles = 0;
            GetProcessHandleCount (GetCurrentProcess(), &handles);
            return juce::String ((double) pm.PrivateUsage / (1024.0 * 1024.0), 1) + " MB private, "
                 + juce::String ((int) GetGuiResources (GetCurrentProcess(), GR_GDIOBJECTS)) + " GDI, "
                 + juce::String ((int) GetGuiResources (GetCurrentProcess(), GR_USEROBJECTS)) + " USER, "
                 + juce::String ((int) handles) + " handles";
        };
        const auto before = resources();
        md << "\n# Spectrum and level meters moving (engine running on test devices, synthetic speech)\n\n"
           << "Each row 5 s. Frames/s: drawn frames of the spectrum and meters (they are paced on the screen's refresh). "
           << "CPU: the whole test process (audio engine, both views, everything) in % of one core; 'hidden' = the same "
           << "with the spectrum and meters switched off, so the difference is what they cost. On a CI runner the "
           << "graphics card is Windows' software rasteriser (WARP): the GPU rows are not representative of a real card.\n\n"
           << "| engine | FPS setting | frames/s | longest pause ms | CPU % | CPU % hidden | difference |\n|---|---|---|---|---|---|---|\n";
        for (int eng = 0; eng < 2; ++eng)
        {
            micvst::render::setMode (eng == 0 ? micvst::render::Mode::cpuOnly : micvst::render::Mode::gpuOnly);
            pump (800);
            micvst::render::setMetersVisible (false);
            pump (800);
            double hidden = 0.0;
            measure (5000, hidden);
            micvst::render::setMetersVisible (true);
            for (int fps : micvst::render::fpsChoices())
            {
                micvst::render::setFps (fps);
                pump (1000);
                mc->resetVisualStats();
                double cpu = 0.0;
                const double w0 = nowMs();
                measure (5000, cpu);
                const auto st = mc->getVisualStats();
                const double fpsGot = st.frames * 1000.0 / (nowMs() - w0);
                md << "| " << (eng == 0 ? "processor (GDI)" : "graphics card (Direct2D)") << " | " << fps << " | "
                   << juce::String (fpsGot, 1) << " | " << juce::String (st.longestGapMs, 1) << " | "
                   << juce::String (cpu, 1) << " | " << juce::String (hidden, 1) << " | " << juce::String (cpu - hidden, 1) << " |\n";
                // pass: at least 90 % of the frames (a CI machine is shared), no pause longer than 3 frames
                if (fpsGot < 0.9 * fps || st.longestGapMs > 3000.0 / fps) ++failures;
            }
        }
        micvst::render::setFps (0);
        pump (500);
        md << "\nResources before: " << before << "; after: " << resources() << "\n"
           << "\n**Total failures (switches + animation): " << failures << "**\n";
        dir.getChildFile ("render-switch.md").replaceWithText (md);
        dir.getChildFile ("render-switch.csv").replaceWithText (csv);
        std::printf ("%s\n", md.toRawUTF8());

        win.setVisible (false);
        pump (200);
        return failures == 0 ? 0 : 4;
    }
#else
    inline int run (const juce::File& dir, AudioEngine&, int)
    {
        dir.createDirectory();
        dir.getChildFile ("render-switch.md").replaceWithText ("Windows only.\n");
        std::printf ("render test: Windows only\n");
        return 0;
    }
#endif
}
