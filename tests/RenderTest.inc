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
// Before that, the first switch as a user makes it (render-first.md + PNGs): the main window and the
// Settings window open, "Yalnızca işlemci" chosen in the Arayüz çizimi list - the first GPU -> CPU
// switch of these windows, then again - with the whole of both windows captured, so a white or
// foreign area anywhere (not only at the marker) is found and saved as a picture.
// Windows only (elsewhere it says so and returns 0).
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <psapi.h>
 #pragma comment (lib, "psapi.lib")
#endif
#include "ui/DarkTitleBar.h"

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

    // ---------------------------------------------------------------------------------------------
    // The first switch as a user makes it: both windows, the list's popup, whole-screen captures.
    struct ScreenFrame { double t = 0.0; std::vector<uint32_t> px; };

    inline int pxDistance (uint32_t a, uint32_t b)
    {
        return std::abs ((int) ((a >> 16) & 0xff) - (int) ((b >> 16) & 0xff)) + std::abs ((int) ((a >> 8) & 0xff) - (int) ((b >> 8) & 0xff))
             + std::abs ((int) (a & 0xff) - (int) (b & 0xff));
    }
    inline int luma (uint32_t p) { return (int) ((((p >> 16) & 0xff) * 299 + ((p >> 8) & 0xff) * 587 + (p & 0xff) * 114) / 1000); }

    inline juce::Image toImage (const std::vector<uint32_t>& px, int w, int h, const std::vector<uint8_t>* mark = nullptr)
    {
        juce::Image img (juce::Image::RGB, w, h, false);
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const size_t i = (size_t) y * (size_t) w + (size_t) x;
                const auto p = px[i];
                auto c = juce::Colour ((juce::uint8) ((p >> 16) & 0xff), (juce::uint8) ((p >> 8) & 0xff), (juce::uint8) (p & 0xff));
                if (mark != nullptr)
                    c = (*mark)[i] == 2 ? juce::Colour (0xffff00ff) : (*mark)[i] == 1 ? juce::Colour (0xff00e5ff) : c.withMultipliedBrightness (0.45f);
                bd.setPixelColour (x, y, c);
            }
        return img;
    }
    inline void savePng (const juce::Image& img, const juce::File& f)
    {
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        png.writeImageToStream (img, os);
    }
    inline juce::ComboBox* findRenderList (juce::Component& c)
    {
        if (auto* cb = dynamic_cast<juce::ComboBox*> (&c))
            for (int i = 0; i < cb->getNumItems(); ++i)
                if (cb->getItemText (i) == juce::String::fromUTF8 ("Yalnızca işlemci") || cb->getItemText (i) == "CPU only")
                    return cb;
        for (auto* child : c.getChildren())
            if (auto* found = findRenderList (*child))
                return found;
        return nullptr;
    }

    struct FirstSwitchResult
    {
        juce::String label;
        int engineBefore = -1, engineAfter = -1;
        int framesDuring = 0, anomalyFrames = 0, maxAnomaly = 0, maxBright = 0;
        double firstAnomalyMs = -1.0, lastAnomalyMs = -1.0;
        juce::Rectangle<int> worstBox;   // screen pixels
        juce::String where;
    };

    inline int firstSwitchPhase (const juce::File& dir, AudioEngine& engine)
    {
        auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
        auto stage = [] (const char* what) { std::printf ("render test (first switch): %s\n", what); std::fflush (stdout); };
        stage ("start");
        micvst::render::startSession (micvst::render::Mode::gpuOnly);

        Window win;
        win.setContentOwned (new MainComponent (engine), true);
        win.centreWithSize (640, 760);
        win.setTopLeftPosition (40, 40);
        win.setVisible (true);
        micvst::useDarkTitleBar (win, micvst::theme::dark);
        micvst::render::attach (win);
        win.toFront (true);
        pump (1200);
        win.setSize (660, 840);   // a size set while Direct2D draws (a restored or dragged size)
        pump (800);

        struct SettingsWin final : juce::DocumentWindow
        {
            SettingsWin() : juce::DocumentWindow ("Ayarlar", micvst::theme::background, juce::DocumentWindow::closeButton) { setUsingNativeTitleBar (true); }
            void closeButtonPressed() override {}
        };
        GlobalHotkeys keys;
        keys.set (GlobalHotkeys::mute, GlobalHotkeys::defaultFor (GlobalHotkeys::mute));
        SettingsComponent* sc = nullptr;
        SettingsComponent::Callbacks cb;
        cb.setHotkey = [&] (GlobalHotkeys::Action a, const GlobalHotkeys::Binding& b) { keys.set (a, b); return true; };
        cb.chooseRenderer = [&sc] (const juce::String& id)   // as MicVST (Main.cpp applyRenderer)
        {
            juce::MessageManager::callAsync ([&sc, id]
            {
                micvst::render::setMode (micvst::render::fromId (id));
                if (sc != nullptr) sc->refreshFps();
            });
        };
        SettingsWin swin;
        sc = new SettingsComponent (engine, &keys, cb);
        swin.setContentOwned (sc, true);
        swin.setResizable (true, false);
        swin.centreAroundComponent (&win, swin.getWidth(), swin.getHeight());
        swin.setVisible (true);
        micvst::useDarkTitleBar (swin, micvst::theme::dark);
        micvst::render::attach (swin);
        swin.toFront (true);
        pump (1200);
        stage ("windows shown");

        auto* list = findRenderList (*sc);
        if (list == nullptr)
        {
            dir.getChildFile ("render-first.md").replaceWithText ("# First switch\n\nArayüz çizimi list not found.\n");
            return 1;
        }
        for (auto* p = list->getParentComponent(); p != nullptr; p = p->getParentComponent())
            if (auto* vp = dynamic_cast<juce::Viewport*> (p))
            {
                vp->setViewPosition (0, juce::jmax (0, list->getY() - 120));
                break;
            }
        pump (500);

        // the area captured: both windows and room for the list's popup under them
        const auto mainHwnd = (HWND) win.getPeer()->getNativeHandle();
        const auto setHwnd = (HWND) swin.getPeer()->getNativeHandle();
        SetWindowPos (mainHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetWindowPos (setHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        pump (300);
        RECT rm {}, rs {};
        GetWindowRect (mainHwnd, &rm);
        GetWindowRect (setHwnd, &rs);
        const int vx = GetSystemMetrics (SM_XVIRTUALSCREEN), vy = GetSystemMetrics (SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics (SM_CXVIRTUALSCREEN), vh = GetSystemMetrics (SM_CYVIRTUALSCREEN);
        const int ax = juce::jmax (vx, (int) juce::jmin (rm.left, rs.left) - 8), ay = juce::jmax (vy, (int) juce::jmin (rm.top, rs.top) - 8);
        const int ar = juce::jmin (vx + vw, (int) juce::jmax (rm.right, rs.right) + 8);
        const int ab = juce::jmin (vy + vh, (int) juce::jmax (rm.bottom, rs.bottom) + 260);
        const int aw = ar - ax, ah = ab - ay;
        auto describe = [&] (juce::Rectangle<int> b)   // which window a box (screen pixels) lies in
        {
            juce::StringArray parts;
            const juce::Rectangle<int> mainR ((int) rm.left, (int) rm.top, (int) (rm.right - rm.left), (int) (rm.bottom - rm.top));
            const juce::Rectangle<int> setR ((int) rs.left, (int) rs.top, (int) (rs.right - rs.left), (int) (rs.bottom - rs.top));
            if (setR.intersects (b)) parts.add ("Ayarlar penceresi (" + juce::String (b.getX() - setR.getX()) + "," + juce::String (b.getY() - setR.getY()) + " " + juce::String (b.getWidth()) + "x" + juce::String (b.getHeight()) + " pencereye göre)");
            if (mainR.intersects (b) && ! setR.contains (b)) parts.add ("ana pencere (" + juce::String (b.getX() - mainR.getX()) + "," + juce::String (b.getY() - mainR.getY()) + " " + juce::String (b.getWidth()) + "x" + juce::String (b.getHeight()) + ")");
            if (parts.isEmpty()) parts.add ("pencerelerin dışı (açılır liste?)");
            return parts.joinIntoString (", ");
        };

        std::vector<FirstSwitchResult> results;
        auto doSwitch = [&] (int toId, bool withPopup, const juce::String& label, const juce::String& fileTag)
        {
            FirstSwitchResult r;
            r.label = label;
            r.engineBefore = win.getPeer()->getCurrentRenderingEngine();
            if (withPopup && list->isShowing())
            {
                list->showPopup();
                pump (700);
            }
            std::vector<ScreenFrame> frames;
            frames.reserve (400);
            std::atomic<bool> stop { false };
            const double t0 = nowMs();
            std::thread capturer ([&]
            {
                Grabber g (aw, ah);
                while (! stop.load())
                {
                    if (g.grab (ax, ay))
                    {
                        ScreenFrame f;
                        f.t = nowMs() - t0;
                        f.px.assign (g.bits, g.bits + (size_t) aw * (size_t) ah);
                        frames.push_back (std::move (f));
                    }
                    Sleep (8);
                }
            });
            pump (350);   // the picture before (the popup open), while the views move
            const double ts = nowMs() - t0;
            if (withPopup) juce::PopupMenu::dismissAllActiveMenus();
            list->setSelectedId (toId);   // as a click in the popup: the list's onChange, Main's callAsync
            pump (1300);
            stop = true;
            capturer.join();
            r.engineAfter = win.getPeer()->getCurrentRenderingEngine();
            if (frames.size() < 6) { r.where = "too few captures"; results.push_back (r); return; }

            // references: the last frame before the action and the last one; what moves by itself
            // (spectrum, meters, a caret) is left out: pixels that change within the frames before or
            // within the last 500 ms
            size_t lastBefore = 0;
            for (size_t i = 0; i < frames.size(); ++i) if (frames[i].t < ts) lastBefore = i;
            const auto& before = frames[lastBefore].px;
            const auto& after = frames.back().px;
            const size_t n = (size_t) aw * (size_t) ah;
            std::vector<uint8_t> moving (n, 0);
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const bool pre = frames[i].t < ts, post = frames[i].t > frames.back().t - 500.0;
                if (! pre && ! post) continue;
                const auto& ref = pre ? before : after;
                for (size_t k = 0; k < n; ++k)
                    if (pxDistance (frames[i].px[k], ref[k]) > 40) moving[k] = 1;
            }
            size_t worst = 0;
            std::vector<uint8_t> worstMark;
            for (size_t i = 0; i < frames.size(); ++i)
            {
                const auto& f = frames[i];
                if (f.t <= ts || f.t > frames.back().t - 500.0) continue;
                ++r.framesDuring;
                int anomalies = 0, bright = 0;
                juce::Rectangle<int> box;
                std::vector<uint8_t> mark (n, 0);
                for (size_t k = 0; k < n; ++k)
                {
                    if (moving[k]) continue;
                    if (pxDistance (f.px[k], before[k]) > 60 && pxDistance (f.px[k], after[k]) > 60)
                    {
                        ++anomalies;
                        const bool isBright = luma (f.px[k]) > 170;
                        if (isBright) ++bright;
                        mark[k] = isBright ? 2 : 1;
                        const juce::Rectangle<int> px1 ((int) (k % (size_t) aw) + ax, (int) (k / (size_t) aw) + ay, 1, 1);
                        box = box.isEmpty() ? px1 : box.getUnion (px1);
                    }
                }
                if (anomalies > 200)
                {
                    ++r.anomalyFrames;
                    if (r.firstAnomalyMs < 0.0) r.firstAnomalyMs = f.t - ts;
                    r.lastAnomalyMs = f.t - ts;
                }
                if (anomalies > r.maxAnomaly)
                {
                    r.maxAnomaly = anomalies;
                    r.maxBright = juce::jmax (r.maxBright, bright);
                    r.worstBox = box;
                    worst = i;
                    worstMark = std::move (mark);
                }
                r.maxBright = juce::jmax (r.maxBright, bright);
            }
            r.where = r.maxAnomaly > 200 ? describe (r.worstBox) : juce::String ("-");
            savePng (toImage (before, aw, ah), dir.getChildFile ("first-" + fileTag + "-1-before.png"));
            savePng (toImage (after, aw, ah), dir.getChildFile ("first-" + fileTag + "-4-after.png"));
            if (r.maxAnomaly > 200)
            {
                savePng (toImage (frames[worst].px, aw, ah), dir.getChildFile ("first-" + fileTag + "-2-worst.png"));
                savePng (toImage (frames[worst].px, aw, ah, &worstMark), dir.getChildFile ("first-" + fileTag + "-3-worst-marked.png"));
            }
            results.push_back (r);
            pump (600);
        };

        stage ("measuring");
        doSwitch (3, true,  "1. GPU -> CPU (the first; from the list's popup)", "a");
        doSwitch (2, true,  "2. CPU -> GPU (from the popup)", "b");
        doSwitch (3, true,  "3. GPU -> CPU (the second; from the popup)", "c");
        doSwitch (2, false, "4. CPU -> GPU (without the popup)", "d");
        doSwitch (3, false, "5. GPU -> CPU (without the popup)", "e");

        juce::String md;
        md << "# First GPU -> CPU switch as a user makes it (main window + Ayarlar, Arayüz çizimi list)\n\n"
           << "Captured area " << aw << " x " << ah << " px at " << ax << "," << ay << " (both windows + 260 px under them for the popup), every ~10 ms. "
           << "A pixel is foreign when it differs from both the picture before the choice and the final picture "
           << "(by more than 60 in R+G+B) and is not one that moves by itself (spectrum, meters). Bright = foreign and "
           << "lighter than 170/255 (white-ish; the theme is dark). Frames with more than 200 foreign pixels count.\n\n"
           << "| switch | engine before -> after | frames | frames with foreign area | most foreign px | of them bright | first .. last ms | where (worst frame) |\n"
           << "|---|---|---|---|---|---|---|---|\n";
        int failures = 0;
        for (auto& r : results)
        {
            md << "| " << r.label << " | " << r.engineBefore << " -> " << r.engineAfter << " | " << r.framesDuring << " | " << r.anomalyFrames
               << " | " << r.maxAnomaly << " | " << r.maxBright << " | "
               << (r.firstAnomalyMs >= 0 ? juce::String (r.firstAnomalyMs, 0) + " .. " + juce::String (r.lastAnomalyMs, 0) : juce::String ("-"))
               << " | " << r.where << " |\n";
            if (r.maxBright > 500) ++failures;
        }
        md << "\nPictures: first-<a..e>-1-before.png, -2-worst.png, -3-worst-marked.png (magenta = bright foreign, cyan = other foreign, the rest dimmed), -4-after.png.\n"
           << "\n**First-switch failures (a white area of more than 500 px): " << failures << "**\n";
        dir.getChildFile ("render-first.md").replaceWithText (md);
        std::printf ("%s\n", md.toRawUTF8());

        swin.setVisible (false);
        win.setVisible (false);
        pump (300);
        return failures;
    }

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
        const int firstFailures = firstSwitchPhase (dir, engine);   // first: these are the process's first switches
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
           << "\nFirst switch with the Ayarlar window (render-first.md): " << firstFailures << " failures\n"
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
