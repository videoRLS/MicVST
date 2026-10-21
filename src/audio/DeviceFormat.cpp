#include "audio/DeviceFormat.h"
#include "Text.h"

namespace devfmt
{
namespace
{
    int u16 (const juce::uint8* p) { return p[0] | (p[1] << 8); }
    int u32 (const juce::uint8* p) { return (int) ((juce::uint32) p[0] | ((juce::uint32) p[1] << 8) | ((juce::uint32) p[2] << 16) | ((juce::uint32) p[3] << 24)); }
}

Format parseWaveFormat (const void* data, size_t size)
{
    Format f;
    if (data == nullptr || size < 16) return f;
    const auto* p = static_cast<const juce::uint8*> (data);
    const int tag = u16 (p);
    f.channels = u16 (p + 2);
    f.sampleRate = u32 (p + 4);
    f.containerBits = u16 (p + 14);
    f.validBits = f.containerBits;
    constexpr int pcm = 1, ieeeFloat = 3, extensible = 0xFFFE;
    if (tag == pcm) f.isFloat = false;
    else if (tag == ieeeFloat) f.isFloat = true;
    else if (tag == extensible)
    {
        // WAVEFORMATEX (18 bytes) + wValidBitsPerSample (2) + dwChannelMask (4) + SubFormat GUID (16)
        if (size < 40 || u16 (p + 16) < 22) return f;
        const int vb = u16 (p + 18);
        if (vb > 0) f.validBits = vb;
        // SubFormat: the first 4 bytes of KSDATAFORMAT_SUBTYPE_PCM / _IEEE_FLOAT are the format tag
        const int sub = u32 (p + 24);
        static const juce::uint8 baseGuidTail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
        if (std::memcmp (p + 28, baseGuidTail, 12) != 0) return f;
        if (sub == pcm) f.isFloat = false;
        else if (sub == ieeeFloat) f.isFloat = true;
        else return f;
    }
    else return f;
    f.valid = f.channels > 0 && f.sampleRate > 0 && f.containerBits > 0 && f.validBits <= f.containerBits;
    return f;
}

juce::String describe (const Format& f)
{
    if (! f.valid) return micvst::tr ("bilinmiyor");
    juce::String s = juce::String (f.validBits) + " bit " + (f.isFloat ? micvst::tr ("kayan nokta") : juce::String ("PCM"));
    if (! f.isFloat && f.validBits != f.containerBits) s << " (" << f.containerBits << micvst::tr (" bit içinde") << ")";
    s << ", " << f.sampleRate << " Hz, " << f.channels << micvst::tr (" kanal");
    return s;
}
}

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <propsys.h>
 #include <propidl.h>

namespace devfmt
{
Format windowsEndpointFormat (const juce::String& friendlyName, bool capture)
{
    Format result;
    if (friendlyName.isEmpty()) return result;
    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED (init);   // S_OK or S_FALSE: balanced by CoUninitialize; RPC_E_CHANGED_MODE: COM is already up
    // {a45c254e-df1c-4efd-8020-67d146a850e0}, 14: PKEY_Device_FriendlyName
    static const PROPERTYKEY keyName { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };
    // {f19f064d-082c-4e27-bc73-6882a1bb8e4c}, 0: PKEY_AudioEngine_DeviceFormat
    static const PROPERTYKEY keyFormat { { 0xf19f064d, 0x082c, 0x4e27, { 0xbc, 0x73, 0x68, 0x82, 0xa1, 0xbb, 0x8e, 0x4c } }, 0 };
    IMMDeviceEnumerator* en = nullptr;
    if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)) && en != nullptr)
    {
        IMMDeviceCollection* list = nullptr;
        if (SUCCEEDED (en->EnumAudioEndpoints (capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, &list)) && list != nullptr)
        {
            UINT n = 0;
            list->GetCount (&n);
            for (UINT i = 0; i < n && ! result.valid; ++i)
            {
                IMMDevice* dev = nullptr;
                if (FAILED (list->Item (i, &dev)) || dev == nullptr) continue;
                IPropertyStore* store = nullptr;
                if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &store)) && store != nullptr)
                {
                    PROPVARIANT name; PropVariantInit (&name);
                    if (SUCCEEDED (store->GetValue (keyName, &name)) && name.vt == VT_LPWSTR
                        && juce::String (name.pwszVal) == friendlyName)
                    {
                        PROPVARIANT fmt; PropVariantInit (&fmt);
                        if (SUCCEEDED (store->GetValue (keyFormat, &fmt)) && fmt.vt == VT_BLOB)
                            result = parseWaveFormat (fmt.blob.pBlobData, fmt.blob.cbSize);
                        PropVariantClear (&fmt);
                    }
                    PropVariantClear (&name);
                    store->Release();
                }
                dev->Release();
            }
            list->Release();
        }
        en->Release();
    }
    if (uninit) CoUninitialize();
    return result;
}
}
#else
namespace devfmt
{
Format windowsEndpointFormat (const juce::String&, bool) { return {}; }
}
#endif
