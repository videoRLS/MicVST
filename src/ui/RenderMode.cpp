#include "ui/RenderMode.h"
#include "Text.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <d3d11.h>
 #include <dxgi.h>
 #include <shellapi.h>
 #pragma comment (lib, "d3d11.lib")
 #pragma comment (lib, "shell32.lib")
#endif

using micvst::tr;

namespace micvst::render
{
juce::String toId (Mode m)
{
    return m == Mode::gpuOnly ? "gpu" : m == Mode::cpuOnly ? "cpu" : "";
}

Mode fromId (const juce::String& id)
{
    const auto t = id.trim().toLowerCase();
    return t == "gpu" ? Mode::gpuOnly : t == "cpu" ? Mode::cpuOnly : Mode::automatic;
}

int chooseEngine (Mode m, bool gpu, bool risky, bool braked)
{
    switch (m)
    {
        case Mode::cpuOnly:   return 0;
        case Mode::gpuOnly:   return 1;
        case Mode::automatic: break;
    }
    return gpu && ! risky && ! braked ? 1 : 0;
}

bool SwitchBrake::note (juce::uint32 now)
{
    times.add (now);
    while (! times.isEmpty() && now - times.getFirst() > windowMs)
        times.remove (0);
    if (times.size() > maxSwitches) on = true;
    return on;
}

namespace
{
   #if JUCE_WINDOWS
    // A Direct3D 11 hardware device with Direct2D support (BGRA), not Windows' "Microsoft Basic
    // Render Driver" (no graphics driver: software). Inside an SEH guard - a broken driver must not
    // take MicVST down; any failure means "no usable graphics card".
    static int probeGpuUnguarded()
    {
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                             D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        ID3D11Device* device = nullptr;
        D3D_FEATURE_LEVEL got {};
        HRESULT hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        levels, 4, D3D11_SDK_VERSION, &device, &got, nullptr);
        if (hr == E_INVALIDARG)   // a runtime without 11.1
            hr = D3D11CreateDevice (nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                    levels + 1, 3, D3D11_SDK_VERSION, &device, &got, nullptr);
        if (FAILED (hr) || device == nullptr) return 0;
        int usable = 1;
        IDXGIDevice* dxgi = nullptr;
        if (SUCCEEDED (device->QueryInterface (__uuidof (IDXGIDevice), (void**) &dxgi)) && dxgi != nullptr)
        {
            IDXGIAdapter* adapter = nullptr;
            if (SUCCEEDED (dxgi->GetAdapter (&adapter)) && adapter != nullptr)
            {
                DXGI_ADAPTER_DESC d {};
                if (SUCCEEDED (adapter->GetDesc (&d)) && d.VendorId == 0x1414 && d.DeviceId == 0x8c)
                    usable = 0;   // Microsoft Basic Render Driver
                adapter->Release();
            }
            dxgi->Release();
        }
        device->Release();
        return usable;
    }

    static int probeGpu()
    {
        __try { return probeGpuUnguarded(); }
        __except (1 /* EXCEPTION_EXECUTE_HANDLER */) { return 0; }
    }

    // Windows' own notice: a full-screen program (a game in exclusive full screen, or any
    // full-screen window) is in front
    static bool fullScreenProgram()
    {
        QUERY_USER_NOTIFICATION_STATE s {};
        if (FAILED (SHQueryUserNotificationState (&s))) return false;
        return s == QUNS_RUNNING_D3D_FULL_SCREEN || s == QUNS_BUSY;
    }
   #else
    static int probeGpu() { return 0; }
    static bool fullScreenProgram() { return false; }
   #endif

    juce::String engineName (int e) { return e == 1 ? tr ("ekran kartı") : tr ("işlemci"); }

    struct Controller final : private juce::Timer, public juce::DeletedAtShutdown
    {
        static Controller& get()
        {
            if (instance == nullptr) instance = new Controller();
            return *instance;
        }
        ~Controller() override { stopTimer(); instance = nullptr; }

        Mode mode = Mode::automatic;
        juce::Array<juce::Component::SafePointer<juce::Component>> windows;
        juce::uint32 riskUntil = 0;
        bool fullScreen = false;
        SwitchBrake brake;
        int lastEngine = -1;
        juce::String lastReason;

        bool risky() const
        {
            return mode == Mode::automatic && (fullScreen || (int) (riskUntil - juce::Time::getMillisecondCounter()) > 0);
        }

        juce::String reason (int engine) const
        {
            if (mode == Mode::gpuOnly)   return gpuUsable() ? juce::String() : tr ("kullanılabilir ekran kartı yok");
            if (mode == Mode::cpuOnly)   return {};
            if (engine == 1)             return {};
            if (! gpuUsable())           return tr ("kullanılabilir ekran kartı yok");
            if (brake.on)                return tr ("10 dakikada çok sık geçiş: bu oturumda işlemci");
            return tr ("tam ekran program ya da ekran değişikliği");
        }

        void apply()
        {
            for (int i = windows.size(); --i >= 0;)
                if (windows.getReference (i) == nullptr) windows.remove (i);

            int engine = chooseEngine (mode, gpuUsable(), risky(), brake.on);
            if (lastEngine >= 0 && engine != lastEngine && mode == Mode::automatic
                && brake.note (juce::Time::getMillisecondCounter()))
                engine = chooseEngine (mode, gpuUsable(), risky(), true);

            for (auto& w : windows)
                if (auto* peer = w->getPeer())
                    if (peer->getCurrentRenderingEngine() != engine)
                        peer->setCurrentRenderingEngine (engine);

            const auto why = reason (engine);
            if (engine != lastEngine || why != lastReason)
            {
                juce::Logger::writeToLog (tr ("Arayüz çizimi") + ": " + engineName (engine) + (why.isNotEmpty() ? " (" + why + ")" : juce::String()));
                lastEngine = engine;
                lastReason = why;
            }
        }

        void setMode (Mode m)
        {
            mode = m;
            if (mode == Mode::automatic) startTimer (1000);
            else                         stopTimer();
            apply();
        }

        void screensChanged()
        {
            if (mode != Mode::automatic) return;
            riskUntil = juce::Time::getMillisecondCounter() + 3000;
            apply();
        }

    private:
        static inline Controller* instance = nullptr;

        Controller() { startTimer (1000); }

        void timerCallback() override
        {
            if (mode != Mode::automatic) { stopTimer(); return; }
            const bool fs = fullScreenProgram();
            if (fullScreen && ! fs) riskUntil = juce::Time::getMillisecondCounter() + 3000;   // a fresh picture 3 s after it ended
            fullScreen = fs;
            apply();
        }
    };
}

bool gpuUsable()
{
    static const bool usable = probeGpu() != 0;
    return usable;
}

Mode current()                { return Controller::get().mode; }
void setMode (Mode m)         { Controller::get().setMode (m); }
void screensChanged()         { Controller::get().screensChanged(); }

juce::String problem()
{
    return Controller::get().mode == Mode::gpuOnly && ! gpuUsable()
             ? tr ("Bu bilgisayarda kullanılabilir ekran kartı bulunamadı; arayüz yine de ekran kartıyla çizilmeye çalışılıyor.")
             : juce::String();
}

void attach (juce::Component& window)
{
    auto& c = Controller::get();
    bool known = false;
    for (auto& w : c.windows) if (w == &window) known = true;
    if (! known) c.windows.add (&window);
    c.apply();
}

juce::String describeNow()
{
    auto& c = Controller::get();
    const int engine = c.lastEngine >= 0 ? c.lastEngine : chooseEngine (c.mode, gpuUsable(), c.risky(), c.brake.on);
    const auto why = c.reason (engine);
    return tr ("Şu an: ") + engineName (engine) + (why.isNotEmpty() ? " (" + why + ")" : juce::String());
}
}
