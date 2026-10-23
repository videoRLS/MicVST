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
    int sessionEngine = -1;
}

bool gpuUsable()
{
    static const bool usable = probeGpu() != 0;
    return usable;
}

void startSession (Mode m)
{
    saved = session = m;
    sessionEngine = chooseEngine (m, gpuUsable());
    const auto why = problem();
    juce::Logger::writeToLog (tr ("Arayüz çizimi") + ": " + engineName (sessionEngine)
                              + (m == Mode::automatic && sessionEngine == 0 ? " (" + tr ("kullanılabilir ekran kartı yok") + ")" : juce::String())
                              + (why.isNotEmpty() ? " - " + why : juce::String()));
}

Mode current()        { return saved; }
void setMode (Mode m) { saved = m; }

juce::String problem()
{
    return session == Mode::gpuOnly && ! gpuUsable()
             ? tr ("Bu bilgisayarda kullanılabilir ekran kartı bulunamadı; arayüz yine de ekran kartıyla çizilmeye çalışılıyor.")
             : juce::String();
}

void attach (juce::Component& window)
{
    if (sessionEngine < 0) startSession (saved);
    if (auto* peer = window.getPeer())
        if (peer->getCurrentRenderingEngine() != sessionEngine)
            peer->setCurrentRenderingEngine (sessionEngine);
}

juce::String describeNow()
{
    const int engine = sessionEngine >= 0 ? sessionEngine : chooseEngine (session, gpuUsable());
    juce::String t = tr ("Şu an: ") + engineName (engine);
    if (session == Mode::automatic && engine == 0) t << " (" << tr ("kullanılabilir ekran kartı yok") << ")";
    if (saved != session) t << "\n" << tr ("Değişiklik MicVST yeniden başlatılınca uygulanır.");
    return t;
}
}
