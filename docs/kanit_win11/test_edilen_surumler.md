# MicVST 1.2.2 – test edilen sürümler (22–23 Ekim 2026)

Yıllar sonra "o zaman neyle çalışıyordu?" sorusunun cevabı. Kaynak: kullanıcının bilgisayarındaki envanter (22 Ekim) ve CI betikleri.

## MicVST

| Ne | Sürüm |
|---|---|
| MicVST | 1.2.2, dal `kararlilik/oncelik`; teslim exe'sinin commit'i ve SHA-256'sı arşivin `OKU.md` dosyasında |
| JUCE | 9.0.4 + MicVST'nin gerçek zamanlı yamaları (`cmake/PatchJuce.cmake`) |
| LAME (MP3) | 4.0 (`third_party/lame-4.0`) |
| Derleyici | MSVC (Visual Studio 2022, GitHub `windows-latest`), CMake |

## Kullanıcının bilgisayarı (U)

| Ne | Sürüm |
|---|---|
| Windows | Windows 10 Pro 22H2, yapı 19045.7725 (dil tr-TR) |
| İşlemci / bellek | AMD Ryzen 7 5700X (8 çekirdek / 16 iş parçacığı), 32 GB; anakart MSI MS-7C95 |
| Mikrofon | HyperX QuadCast 2 S (USB Audio 2.0, Windows sürücüsü usbaudio2 10.0.19041.7725), Windows biçimi 32 bit PCM / 48 kHz / 2 kanal |
| Sanal kablo | VB-Audio Virtual Cable 3.3.1.7 (sürücü tarihi 10.07.2024), CABLE Input / Output 24 bit / 48 kHz |
| Diğer ses | Realtek High Definition Audio 6.0.9977.1, NVIDIA High Definition Audio 1.4.6.3 |
| MicVST ayarları | 48 kHz, tampon 480, **güvenlik payı 80 ms**, kayıt WAV 24 bit / 48 kHz, öncelik Normal |

### Kullanıcının zinciri (sırayla)

| Eklenti | Sürüm |
|---|---|
| Clear | sürüm bilgisi yok (dosya 28.03.2025, 260 096 B) |
| FabFilter Pro-DS | 1.32 (1.3.2.35) |
| FabFilter Pro-DS | 1.32 |
| FabFilter Pro-C 3 | 3.02 (3.0.2.54) |
| FabFilter Pro-Q 4 | 4.13 (4.1.3.14), lineer faz |
| Klanghelm SDRR2 | 2.4.1 |
| FabFilter Saturn 2 | 2.13 (2.1.3.15) |
| Klanghelm DC1A3 | 3.5.0 |
| TDR VOS SlickEQ GE | 1.3.7 |
| FabFilter Saturn 2 | 2.13 |
| TDR Limiter 6 GE | 1.2.4 |
| FabFilter Pro-DS | 1.32 |

Bilgisayarda olup zincirde olmayan VST3'ler: diğer FabFilter eklentileri, Goyo 0.9.4, TDR Kotelnikov GE 1.6.4, TDR Nova GE 2.1.5,
Waves WaveShell 17.1 / 17.2 / 17.4, Youlean Loudness Meter 2, oeksound soothe2 1.1.2.

## Test makineleri

| Kısa ad | Sistem |
|---|---|
| W | GitHub Actions `windows-latest`: Windows Server 2025 (x64) |
| A | GitHub Actions `windows-11-arm`: Windows 11 Enterprise 10.0.26200 (ARM64) |
| L | Linux konteyneri (GCC, ASan/UBSan) |

CI'da kullanılan ücretsiz eklentiler (`tools/ci-suite-plugins.ps1`): ZL Equalizer 2 1.4.1, ZL Compressor 0.6.1, Fire 2.0.0,
RNNoise (noise-suppression-for-voice) 1.10, Klanghelm DC1A3, TDR VOS SlickEQ 1.3.9, LSP Plugins (son sürüm), Airwindows Consolidated (gece sürümü 1.2026.291).
