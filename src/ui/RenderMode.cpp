#include "ui/RenderMode.h"
#include "Text.h"
#include "ui/DarkTitleBar.h"
#include "ui/Theme.h"

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

int chooseEngine (Mode m, bool gpu)
{
    return m == Mode::cpuOnly ? 0 : m == Mode::gpuOnly ? 1 : (gpu ? 1 : 0);
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

   #else
    static int probeGpu() { return 0; }
   #endif

    juce::String engineName (int e) { return e == 1 ? tr ("ekran kartı") : tr ("işlemci"); }

    Mode saved = Mode::automatic, session = Mode::automatic;
    int sessionEngine_ = -1;
    juce::Array<juce::Component::SafePointer<juce::Component>> windows;   // MicVST's own, once shown
    int fpsSaved = 0;
    bool meters = true;
}

bool gpuUsable()
{
    static const bool usable = probeGpu() != 0;
    return usable;
}

void startSession (Mode m)
{
    saved = session = m;
    sessionEngine_ = chooseEngine (m, gpuUsable());
    const auto why = problem();
    juce::Logger::writeToLog (tr ("Arayüz çizimi") + ": " + engineName (sessionEngine_)
                              + (m == Mode::automatic && sessionEngine_ == 0 ? " (" + tr ("kullanılabilir ekran kartı yok") + ")" : juce::String())
                              + (why.isNotEmpty() ? " - " + why : juce::String()));
}

Mode current()        { return saved; }
// Yalnızca ekran kartı / Yalnızca işlemci: at once. Each of MicVST's windows gets a new native
// window (peer) whose engine is set before its first paint - switching the engine of a window that
// had already drawn left its old Direct2D picture behind. Otomatik: saved, decided at the next start.
void setMode (Mode m)
{
    saved = m;
    if (m == Mode::automatic || m == session) return;
    startSession (m);
    for (int i = windows.size(); --i >= 0;)
    {
        auto* w = windows.getReference (i).getComponent();
        if (w == nullptr) { windows.remove (i); continue; }
        auto* old = w->getPeer();
        if (old == nullptr) continue;
        const auto flags = old->getStyleFlags();
        w->removeFromDesktop();
        w->addToDesktop (flags);
        if (auto* peer = w->getPeer())
            if (peer->getCurrentRenderingEngine() != sessionEngine_)
                peer->setCurrentRenderingEngine (sessionEngine_);
        if (w->isVisible())
        {
            micvst::useDarkTitleBar (*w, micvst::theme::dark);
            w->repaint();
        }
    }
}

juce::String problem()
{
    return session == Mode::gpuOnly && ! gpuUsable()
             ? tr ("Bu bilgisayarda kullanılabilir ekran kartı bulunamadı; arayüz yine de ekran kartıyla çizilmeye çalışılıyor.")
             : juce::String();
}

void attach (juce::Component& window)
{
    if (sessionEngine_ < 0) startSession (saved);
    bool known = false;
    for (auto& w : windows) if (w == &window) known = true;
    if (! known) windows.add (&window);
    if (auto* peer = window.getPeer())
        if (peer->getCurrentRenderingEngine() != sessionEngine_)
            peer->setCurrentRenderingEngine (sessionEngine_);
}

juce::String describeNow()
{
    const int engine = sessionEngine_ >= 0 ? sessionEngine_ : chooseEngine (session, gpuUsable());
    juce::String t = tr ("Şu an: ") + engineName (engine);
    if (session == Mode::automatic && engine == 0) t << " (" << tr ("kullanılabilir ekran kartı yok") << ")";
    if (saved != session) t << "\n" << tr ("Değişiklik MicVST yeniden başlatılınca uygulanır.");
    return t;
}
int sessionEngine()   { if (sessionEngine_ < 0) startSession (saved); return sessionEngine_; }

juce::Array<int> fpsChoices()
{
    return sessionEngine() == 1 ? juce::Array<int> { 60, 30 } : juce::Array<int> { 60, 30, 20, 10 };
}

int fps()                     { return fpsChoices().contains (fpsSaved) ? fpsSaved : 30; }
int savedFps()                { return fpsSaved; }
void setFps (int f)           { fpsSaved = f == 30 ? 0 : f; }
bool metersVisible()          { return meters; }
void setMetersVisible (bool v) { meters = v; }
}
