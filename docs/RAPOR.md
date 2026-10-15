# MicVST – Ses Kalitesi ve Kararlılık Raporu (v1.1.1 → v1.2.0)

Bu rapor `videoRLS/MicVST` deposundaki çalışmayı belgeler. Her ölçüm değeri nerede ve nasıl
alındığıyla birlikte verilmiştir. Gerçek donanımda (gerçek mikrofon, VB-Cable, gerçek oyun yükü)
yapılamayan testler **9. bölümde** açıkça listelenmiştir; bunlar "yapıldı" diye gösterilmemiştir.

**Uzak depodaki dallar** (hepsi gerçekten gönderildi, `main` dalına dokunulmadı):

| Dal | İçerik |
|---|---|
| `yedek/orijinal-v1.1.1` | Başlangıç noktasının yedeği (`5b1403f`, değiştirilmedi) |
| `kararlilik/asama-1-bagimliliklar` | Aşama 1: yalnızca bağımlılık güncellemesi, orijinal kod (`a7fcda4`) |
| `kararlilik/asama-2-kapali-kutu` | Aşama 2: + MicVST'nin tüm ağ kodu kaldırıldı (`04c7eb3`) |
| `kararlilik/v1.2.0` | Aşama 3 ve sonrası: yeni ses motoru, gerçek zamanlı düzeltmeler, testler, belgeler |
| `ci-sonuclari/baseline`, `ci-sonuclari/kararlilik/*` | GitHub Actions (Windows) çıktıları: ölçümler, günlükler, test çıktısı, exe |

Değişiklikler `kararlilik/v1.2.0` → `main` Pull Request'i ile incelemeye sunuldu ve birleştirilmedi.

---

## 1. Başlangıç durumu ve mimari (v1.1.1)

**Kaynak:** `videoRLS/MicVST`, `philipz794/MicVST`'nin fork'u. Başlangıç commit'i `5b1403f`
(v1.1.1, upstream `main` ile aynı).

**Gönderilen MicVST.exe** (PE başlığı ve sürüm kaynağı incelendi): sürüm 1.1.1, derleme tarihi
2026-08-02, x64, statik MSVC çalışma zamanı, **JUCE v8.0.13**. İçe aktarılan DLL'ler arasında
`WININET.dll` (13 fonksiyon: `InternetOpenW`, `HttpOpenRequestW`, `InternetReadFile` …) ve
`WS2_32.dll` (26 fonksiyon: `socket`, `getaddrinfo`, `sendto` …) var. Exe içinde
`api.github.com/repos/philipz794/MicVST/releases/latest` dizesi bulunuyor, yani güncelleme
denetimi kodu exe'de mevcut.

**Mimari (1.1.1):**

- **Cihaz:** Tek bir JUCE `AudioDeviceManager` ve **tek bir "birleşik" WASAPI cihazı**: giriş =
  mikrofon, çıkış = sanal kablo (VB-Cable). Tercih edilen tür "Windows Audio (Low Latency Mode)",
  bu tür yoksa "Windows Audio" (paylaşımlı).
- **Tek iş parçacığı:** JUCE'un WASAPI arka ucu birleşik cihazı tek bir iş parçacığında
  çalıştırır. Bu iş parçacığının temposunu **çıkış cihazı (kablo)** belirler.
- **Sıfır doldurma, drift telafisi yok:** Mikrofondan gelen veri bir ara depoya ("reservoir")
  yazılır. Her blokta `copyBuffersFromReservoir()` mevcut veriyi alır ve **eksik kalan kısmı sıfırla
  (sessizlik) doldurur**. İki cihazın saat farkı hiçbir yerde telafi edilmez.
- **Ses geri çağrısı:** `AudioEngine` → `AudioProcessorPlayer` → `AudioProcessorGraph`
  (giriş düğümü → VST3 zinciri → çıkış düğümü). Eklenti zinciri, kabloya yazan aynı iş
  parçacığında hesaplanır; arada ek tampon yoktur.
- **Eklenti taraması:** Ayrı alt süreçlerde çalışır (`MicVST.exe --scan`), zaman aşımı ve önbellekle.
- **Ayarlar:** `%APPDATA%\MicVST\config.xml` dosyasında tutulur.
- **Ağ:** `UpdateChecker` (WinINet ile GitHub API isteği, isteğe bağlı) ve arayüzde tıklanabilir
  web bağlantıları vardı.
- **Testler ve CI:** `MicVSTTests`; CI dosyaları `build.yml` ve `release.yml`.

## 2. Bulunan hatalar ve kök nedenleri

Önem: **Y** = yük altında duyulur çıtırtı/kesinti, **O** = belirli durumlarda, **D** = düşük.

| # | Önem | Sorun | Kök neden (kanıt) |
|---|---|---|---|
| 1 | Y | Saat kayması (drift) → düzenli tık/kesinti | Birleşik cihazda mikrofon ve kablo ayrı saatlerle çalışır. JUCE eksik girişi sıfırla doldurur, fazlası birikir ya da taşar. Telafi yok (`juce_WASAPI_windows.cpp`, `copyBuffersFromReservoir`). |
| 2 | Y | İşlem süresi dalgalanması doğrudan çıkışa gider | Eklenti zinciri kabloya yazan iş parçacığında hesaplanır. Bir blok geç kalırsa kablo tamponu boşalır; ara tampon yok. |
| 3 | Y | Öncelik terslemesi | `captureState()` her eklentinin *callback lock*'unu tutarken `getStateInformation()` çağırıyordu. Graf her blokta bu kilidi alır, bu yüzden ses iş parçacığı normal öncelikli arayüz iş parçacığını bekler. Kayıt her cihaz ve zincir değişikliğinde çalışır. |
| 4 | O | Denormal sayılar → CPU sıçraması | Ses iş parçacığında `ScopedNoDenormals` yok (`AudioProcessorPlayer` da koymuyor). |
| 5 | O | Windows zamanlaması | Süreç NORMAL öncelikteydi. Windows 11 tepside çalışan bir süreci EcoQoS ile kısabilir. MMCSS "Pro Audio" önceliği NORMAL'di. |
| 6 | O | Zincir değişikliğinde kısa sessizlik | `rebuildConnections()` her bağlantı silme ve eklemede grafı senkron olarak yeniden kuruyordu. Ses iş parçacığı bağlantısız ara durumları çalabiliyordu. |
| 7 | D | Bypass'taki eklentiler yine hesaplanıyordu | Bağlantısı kesilen düğüm her blokta sessizlikle işleniyordu (boşa CPU). |
| 8 | O | JUCE WASAPI iç sorunları | (a) Oturum etkin değilken MMCSS önceliğinde boş döngü (busy-spin) dönüyordu. (b) Yalnız-giriş cihazında olay başına tek blok işleniyordu; takılmadan sonra kalıcı gecikme birikiyordu. 1 sn zaman aşımında iş parçacığı **tamamen sonlanıyor**, ses sessizce kesiliyordu. |
| 9 | O | Mono biten zincir yalnızca sol kanala gidiyordu | Çıkışa tek kanal bağlanıyordu. Örneğin mono mikrofon + boş zincir: stereo dinleyen uygulamada ses tek taraflı, (L+R)/2 yapanlarda −6 dB. |
| 10 | O | 44,1 kHz mikrofon + 48 kHz kablo açılmıyordu | Düşük gecikme modunda (SRC yok) birleşik cihaz ortak örnekleme hızı ister: "The input and output devices don't share a common sample rate!" |
| 11 | D | Kayıt defteri saniyede 30 kez okunuyordu | Otomatik başlatma onay kutusu için arayüz zamanlayıcısı. |
| 12 | D | Eklenti penceresi kapanınca ayar kaydedilmiyordu | Parametreler yalnızca gizle/çık sırasında yazılıyordu; çökme ya da elektrik kesintisinde kaybolabiliyordu. |
| 13 | — | Ağ erişimi ("kapalı kutu" isteği) | WinINet ile güncelleme denetimi ve tıklanabilir web bağlantıları. |

## 3. Ses kalitesi analizi

**1.1.1'in sinyal yolu normal durumda şeffaf.** Yol 32-bit float'tır ve MicVST kazancı
değiştirmez; ton ve renk eklentilerden gelir. Kalite kaybı yavaş bir bozulma değil,
**kesintilerdir**: sıfır doldurma (#1), geç kalan bloklar (#2–#6) ve sert kesmeler duyulur
tık/çıtırtı üretir. Diğer iki sorun:

- **Mono zincir (#9):** Ses yalnız sol kanaldaydı; 1.2'de her iki kanala gider.
- **Farklı örnekleme hızları (#10):** 1.1.1'de düşük gecikme modunda hiç açılmıyordu.

**1.2'deki yeniden örnekleyici.** Köprü, drift telafisi için her zaman bundan geçer (oran ≈ 1 ± birkaç ppm).
Ölçümler birim testlerinden geliyor ve Windows/MSVC (CI) ile Linux/clang'da birebir aynı:

| Test | Sonuç |
|---|---|
| 100 Hz – 18 kHz kazanç sapması | ≤ 0,0001 dB |
| THD+N (48 kHz, drift oranında) | −128,5 dB (100 Hz) … −107,7 dB (18 kHz) |
| 44,1 → 48 kHz, 1 kHz THD+N | −118,5 dB |
| 96 → 48 kHz, 30 kHz bileşen (alias) | −114,3 dB bastırma |

İyi bir USB mikrofonun gerçek dinamik aralığı ~100–110 dB'dir; yeniden örnekleyicinin katkısı
mikrofonun kendi gürültüsünün altında kalır. Drift telafisinin perde etkisi 100 ppm için 0,17
cent'tir, yani duyulmaz.

**Bilinen davranış:** Büyük bir aksamadan sonra (ör. çıkışın takılması) köprü, tampon seviyesini
geri getirmek için birkaç saniye boyunca oynatma hızını en fazla ±%0,5 değiştirebilir (en fazla
~8,6 cent, tipik olarak çok daha az). 40 ms'yi aşan fazlalık ise çapraz geçişle (crossfade) bir anda
kırpılır. Konuşmada bu fark edilmez.

**Boşalma ve gecikme:**

- **Boşalma olursa:** Güvenlik tamponu yetmediğinde 2 ms'lik fade-out ve fade-in yapılır; sert tık
  olmaz.
- **Gecikme bedeli:** Güvenlik tamponu (Auto: 10 ms'den başlar) yüzünden gecikme 1.1.1'e göre
  yaklaşık 10–20 ms artar. Simülasyonda 1.1.1'in kendi gecikmesi 32–42 ms arasında değişiyordu,
  1.2'de ~50 ms (bkz. 7.4). Konuşmada fark edilmez; "Safety" satırından 5 ms seçilebilir.

## 4. Çıtırtı ve kesintilerin nedenleri (etki sırasıyla)

1. **Saat kayması (#1).** En sinsi neden; yük olmasa bile oluşur. Mikrofon ve VB-Cable tipik
   olarak 10–100 ppm farklı hızda çalışır. 60 ppm'de her ~2,8 dakikada bir 10 ms'lik blok eksik
   (sıfır) ya da fazla kalır. Simülasyonda 600 sn'de 3 kesinti ve 12 tık oluştu (7.4).
2. **Yük altında geç kalan blok (#2, #5).** Ses iş parçacığı birkaç ms geç uyanırsa ya da eklenti
   zinciri bir blokta gerçek zamandan uzun sürerse, 1.1.1'de bu doğrudan kablo tamponunu boşaltır.
   Simülasyonda 25 ms'lik takılmalarda 94 tık, ortalama %95 yükte 11 214 tık oluştu.
3. **Öncelik terslemesi (#3).** Her ayar kaydında ses iş parçacığı arayüz iş parçacığını bekler.
   Büyük eklenti durumlarında bu onlarca ms sürebilir.
4. **Denormal (#4) ve gereksiz CPU (#7).** Yük zirvelerini büyütür.
5. **Zincir değişikliği (#6) ve JUCE busy-spin / iş parçacığı ölümü (#8).**
6. **Sistem kaynaklı nedenler.** Uygulamanın çözemeyeceği şeyler: sürücülerin DPC/ISR gecikmeleri,
   USB güç tasarrufu, CPU park/C-state. Giriş ve çıkış iş parçacıkları birlikte donarsa her tasarımda
   duyulur (7.4, senaryo 6). Teşhis için LatencyMon önerilir.

## 5. Bağımlılıklar (eski → yeni)

Sürümler resmî kaynaklardan doğrulandı: JUCE GitHub etiketleri ve `CHANGE_LIST.md`, Steinberg
`vst3sdk` deposu ve ilgili GitHub Actions depoları.

| Bileşen | Eski | Yeni | Not |
|---|---|---|---|
| JUCE | 8.0.13 | **9.0.4** (12.10.2026, en güncel kararlı) | 8.0.14–9.0.4 arasındaki değişiklikler: WASAPI start/stop yarışı düzeltmesi (bayraklar artık kilit altında yeniden okunuyor; kaynak farkıyla doğrulandı), VST3 hosting düzeltmeleri (9.0.2), MessageManagerLock düzeltmeleri, bozuk String/AudioFormat girişine karşı koruma, Windows ölçekleme düzeltmeleri. |
| JUCE WASAPI yaması | – | `cmake/PatchJuce.cmake` | 3 küçük gerçek zamanlı düzeltme (bkz. 6. bölüm). CI günlüğünde "PatchJuce: applied …" ve 3 yama işareti görüldü. JUCE değişirse yapılandırma bilerek hata verir. |
| VST3 SDK | 3.8.0 (JUCE içinde) | **3.8.0 (bilerek değiştirilmedi)** | En güncel sürüm 3.8.1 (build 84). `pluginterfaces` farkı yalnızca MIDI 2.0, transport ve Linux/Wayland ile ilgili; Windows'ta eklenti barındırma için gereken bir düzeltme yok. JUCE'un test ettiği sürümü değiştirmek kazanç getirmez, risk getirir. |
| actions/checkout | v4 | v7 | Node 24 |
| actions/upload-artifact | v4 | v7 | Node 24 |
| softprops/action-gh-release | v2 | v3 | Node 24 |
| CMake (en az) | 3.22 | 3.22 | Değişmedi |

**Lisans notu (bir avukat değilim):**

- JUCE modülleri **AGPLv3 veya ticari JUCE lisansı** ile çift lisanslıdır (JUCE 8'den beri;
  README'de hâlâ "GPL" yazıyor).
- MicVST'nin GPLv3 kodu AGPLv3 ile birleştirilebilir (GPLv3 §13). Açık kaynak olarak dağıtıldığı
  sürece AGPLv3 yolu geçerlidir; yerel masaüstü uygulamasında pratik bir fark yok.
- MicVST bir gün kapalı kaynak ya da ticari olarak dağıtılmak istenirse ticari JUCE lisansı
  gerekir.
- README'deki cümleyi düzeltmek depo sahibinin kararıdır; dokunmadım.

## 6. Kod değişiklikleri ve gerekçeleri

**Aşama 1 – bağımlılıklar** (`a7fcda4`):

- **Değişenler:** Yalnızca JUCE 9.0.4, Actions sürümleri ve CI altyapısı
  (`.github/workflows/kararlilik-test.yml`, `tools/ci-*.ps1`). Orijinal kod değişmedi.
- **Sonuç:** Güncelleme tek başına derlendi ve testleri geçti.

**Aşama 2 – kapalı kutu** (`04c7eb3`):

- **Güncelleme denetimi kaldırıldı:** `src/net/UpdateChecker.h` silindi. Açılış onay penceresi,
  onay kutusu, "güncelleme var" balonu ve ilgili ayar alanları da kaldırıldı.
- **Eski ayar dosyaları:** Eski `config.xml` sorunsuz yüklenir; eski anahtarlar yok sayılır ve
  artık yazılmaz (birim testi eklendi).
- **Bağlantılar ve derleme ayarları:** Tıklanabilir web bağlantısı kalmadı. `JUCE_USE_WIN_WEBVIEW2=0`
  eklendi; sürüm numarası artık CMake'ten geliyor.
- **CI kontrolü:** `tools/check-no-network.ps1`, exe WinINet/WinHTTP/Winsock/URLMon/IPHLPAPI/DNSAPI/…
  içe aktarıyorsa derlemeyi başarısız sayar.
- **Sonuç:** Bu aşamada kontrol **başarısız oldu**. Neden ve çözümü aşağıda (`e59cd3c`).

**Aşama 3 – ses motoru ve gerçek zamanlı düzeltmeler** (`d777e2c` ve sonrası):

| Değişiklik | Dosya | Çözdüğü sorun |
|---|---|---|
| Mikrofon = yalnız-giriş, kablo = yalnız-çıkış WASAPI cihazı; her biri kendi MMCSS iş parçacığında | `AudioEngine.*` | #1, #2, #10 |
| `AudioBridge`: kilitsiz SPSC halka tampon + polifaz Kaiser-sinc yeniden örnekleyici + dolum seviyesine PI denetleyici (drift'i öğrenir) + uyarlanır güvenlik tamponu (Auto: 10 ms, her boşalmada +5 ms, en çok 80 ms) + boşalmada fade + aşırı birikmede çapraz geçişli kırpma | `AudioBridge.*` | #1, #2, #10 |
| `captureState()` callback kilidi olmadan çalışıyor (VST3'te `getState()` arayüz iş parçacığı çağrısıdır ve `process()` sırasında izinlidir; JUCE'un kendisi de böyle kaydeder) | `AudioEngine.cpp` | #3 |
| Her iki ses iş parçacığında `ScopedNoDenormals` | `AudioEngine.cpp` | #4 |
| Süreç "normalin üstü" + EcoQoS/zamanlayıcı kısıtlaması kapalı; ses iş parçacıkları güç kısıtlamasından çıkarıldı. API çalışma anında çözülür, desteklenmeyen sistemde "n/a" yazar. | `RealtimeSupport.*`, `Main.cpp` | #5 |
| Tarama alt süreçleri "normalin altı" önceliğinde. Bilerek EcoQoS kullanılmıyor: verimlilik çekirdeklerinde büyük kabuk eklentiler (WaveShell) 120 sn zaman aşımına takılabilir. | `RealtimeSupport.cpp` | Oyun/OBS'yi rahatlatır |
| Zincir yeniden kurulumu `UpdateKind::none` + tek `rebuild()` | `PluginChain.cpp` | #6 |
| Bypass'taki eklenti askıya alınır; yeniden açılınca durumu sıfırlanır (eski reverb/delay kuyruğu çalmaz) | `PluginChain.cpp` | #7 |
| JUCE yaması: MMCSS HIGH; etkin olmayan oturumda `Sleep(1)`; yalnız-giriş döngüsü birikmeyi hemen işler ve zaman aşımında beklemeye devam eder | `cmake/PatchJuce.cmake` | #5, #8 |
| Düşük gecikme modu bir cihazı açamazsa paylaşımlı moda geri dönülür (1.1.1 sessizce "Idle" kalıyordu) | `AudioEngine.cpp` | Sağlamlık |
| Mono biten zincir her iki kablo kanalına gider | `PluginChain.*`, `AudioEngine.cpp` | #9 |
| Kayıt defteri saniyede 1 kez okunur; eklenti penceresi kapanınca ayarlar kaydedilir | `MainComponent.cpp`, `PluginListView.cpp` | #11, #12 |
| Bekçi (watchdog): 3 sn boyunca geri çağrı gelmeyen cihaz yeniden açılır (10–60 sn geri çekilmeyle) | `AudioEngine.cpp` | Cihaz takılması |
| Arayüz: "Safety" satırı ve ikinci durum satırı (zincirin CPU yükü/tepe, güvenlik tamponu, drift, kesinti sayacı; kesinti olunca turuncu olur, ipucunda nedenler dökülür) | `DevicePanel.*` | Teşhis |
| **Kapalı kutu düzeltmesi:** MSVC'de `/Gy` + `/OPT:REF /OPT:ICF` (bkz. 7.3) | `CMakeLists.txt` | #13 |
| Testler: `BridgeTests` (16 senaryo) ve `MicVSTStress` (eski/yeni tasarım aynı senaryolarda; Windows'ta gerçek iş parçacıklarıyla) | `tests/*` | Doğrulama |

**Geliştirme sırasında yeni kodda bulup düzelttiğim hatalar** (şeffaflık için; her biri için test
eklendi):

- **Fade-in kaybı:** Yeniden başlatmanın hemen ardından gelen bir boşalmada fade-in kazancı
  uygulanmıyordu (çok küçük çıkış periyotlarında olasıydı).
- **Yanlış drift öğrenimi:** Sık açlıkta takılma sonrası toparlanma "drift" olarak öğreniliyordu
  (~300 ppm). Öğrenme artık yeniden başlatmadan sonraki 1,5 sn boyunca duruyor.
- **Kalıcı sessizlik (kilitlenme):** Çıkış, bir boşalmanın hemen ardından 1,36 sn'den uzun
  donarsa köprü kalıcı sessizlikte kalabiliyordu. Bu hata, gerçek motorun Linux'taki uçtan uca
  denemesinde (7.7) ortaya çıktı.
- **Atılan blokta tık:** Halka taşınca atılan giriş bloğunun yerinde tık oluşabiliyordu. Artık
  orada fade-out yapılıp yeniden başlatılıyor.

Orijinal koddaki Almanca yorumlar ve günlük metinleri, değişmeyen kısımlarda aynen korundu.

## 7. Derleme, performans, kalite ve stres test sonuçları

**Ortam:** GitHub Actions `windows-latest`, MSVC, Release x64. Gerçek Windows'ta derleme ve
çalıştırma, ancak **ses cihazı yok** (bkz. 9). Tüm çıktılar `ci-sonuclari/*` dallarında.

### 7.1 Derleme ve birim testleri

| Durum | Commit | JUCE | Derleme | Hata/uyarı (MicVST kodu) | Birim testleri | Exe boyutu | Ağ kütüphanesi içe aktarımı |
|---|---|---|---|---|---|---|---|
| v1.1.1 (orijinal) | `5b1403f` | 8.0.13 | ✔ | 0 / 0 | ✔ çıkış kodu 0 ¹ | 9 104 896 B | WININET, WS2_32 |
| Aşama 1 | `a7fcda4` | 9.0.4 | ✔ | 0 / 0 | ✔ çıkış kodu 0 | 11 490 304 B | WININET, WS2_32 (beklenen) |
| Aşama 2 | `04c7eb3` | 9.0.4 | ✔ | 0 / 0 | ✔ çıkış kodu 0 | 11 476 480 B | WININET, WS2_32 ✘ |
| Aşama 3 (ilk) | `d777e2c` | 9.0.4 + yama | ✔ | 0 / 0 | ✔ çıkış kodu 0 | 11 504 640 B | WININET, WS2_32 ✘ |
| Bağlayıcı düzeltmesi | `e59cd3c` (CI: `52f433d`) | 9.0.4 + yama | ✔ | 0 / 0 | ✔ 56/56 | 6 768 128 B | **yok ✔** |
| **Son kod** | `aa0aae4` | 9.0.4 + yama (3 işaret) | ✔ | **0 / 0** | ✔ **59/59** | 6 772 224 B | **yok ✔** |

Son kod commit'i `aa0aae4`; ardından gelen commit yalnızca bu raporu ekler, kodu değiştirmez.
Son exe'nin SHA-256 değeri: `6188558E4FE6CF8C4F90F87DC8FE783CE77FB719795CE9C160DB85F1CD9654F8`.

¹ 1.1.1'in test çalıştırıcısı Windows'ta çıktıyı yalnızca hata ayıklayıcıya yazıyordu, bu yüzden
ayrıntılar görünmüyor; 1.2'de testler stdout'a yazıyor. Derleme süreleri paylaşımlı CI
makinelerinde ±%30 oynadığı için (414–657 sn) karşılaştırma ölçütü olarak kullanılmadı.

### 7.2 Kapalı kutu – nasıl doğrulandı

1. **Statik kontrol (her CI derlemesinde):** Son exe'nin içe aktardığı DLL'ler ADVAPI32,
   shcore-scaling, CFGMGR32, COMCTL32, COMDLG32, D2D1, D3D11, DCOMP, DWMAPI, DWRITE, DXGI, GDI32,
   IMM32, KERNEL32, OLE32, OLEAUT32, SHELL32, SHLWAPI, USER32 ve WINMM. Hiçbiri ağ kütüphanesi değil.
2. **Aşama 2'de neden başarısız oldu?** Exe'yi disassemble ettim (capstone; .pdata'dan 33 218
   fonksiyon, RTTI ile 1 713 vtable sınıf adı). WinINet ve Winsock çağıran fonksiyonlar JUCE'un
   `WebInputStream`, `URLInputSource`, `InterprocessConnectionServer` ve soket sınıflarına ait.
   Kodda hiçbir yerden çağrılmıyorlar; tek referansları kendi .pdata kayıtları, yani **ölü kod**.
   Nedeni şu: JUCE'un önerilen ayarı `/Ox`, CMake'in `/O2`'sinin yerine geçiyor ve `/Ox`, `/O2`'den
   farklı olarak `/Gy` içermiyor. Bu yüzden bağlayıcı bu kodu atamıyordu. `/Gy` + `/OPT:REF`
   eklenince ağ içe aktarımları kayboldu; exe 11,5 MB'tan 6,8 MB'a indi.
3. **Çalışma zamanı kontrolü (Wine 9, süreç bazında `+loaddll`):** Orijinal 1.1.1 açılışta
   `WININET.dll` ve `ws2_32.dll` yükledi. 1.2.0 sürecine 25 sn boyunca **hiçbir ağ DLL'i
   yüklenmedi**. Wine'ın görülen `ws2_32` yüklemeleri kendi sistem süreçlerine aitti (wineboot,
   winedevice).
4. **Sınır:** VST3 eklentileri MicVST'nin içinde çalışır ve kendileri ağa çıkabilir. Bunu yalnızca
   `tools/firewall-block.ps1` (Windows Güvenlik Duvarı kuralı) kesin olarak önler.

### 7.3 Ses kalitesi

Değerler 3. bölümdeki tabloda; Windows/MSVC ve Linux/clang'da birebir aynı.

### 7.4 Eski ve yeni tasarım: aynı senaryolar (MicVSTStress, Bölüm A)

**Yöntem:**

- **Sinyal:** Her iki tasarımda aynı giriş sinyali kullanılır (997 Hz sinüs), zamanlama
  deterministiktir.
- **1.1.1 modeli:** Tek iş parçacığı, sıfır doldurma ve 2,2 periyotluk render tamponu.
- **1.2:** Simüle edilmiş değil, gerçek `AudioBridge` kodu çalışır.
- **"Duyulur hata":** Dalga biçiminin sıçradığı örneklerdir; aynı dedektör her iki tasarıma
  uygulanır.

Sonuçlar Windows/MSVC'de ve Linux'ta aynı:

| Senaryo | 1.1.1 duyulur hata (boşluk) | 1.2 duyulur hata (boşluk) | Ortalama ek gecikme 1.1.1 → 1.2 |
|---|---:|---:|---:|
| 1. Normal (drift 0, jitter 0–1 ms) | 0 (0) | **0** (0) | 41,8 → 49,7 ms |
| 2. Mikrofon 60 ppm yavaş (600 sn) | 12 (3) | **0** (0) | 32,0 → 49,7 ms |
| 3. Mikrofon 60 ppm hızlı (1200 sn) | 4 (2) | **0** (0) | 61,9 → 49,7 ms |
| 4. Ağır oyun: eklenti süresi 0–9 ms dalgalı, +40 ppm | 0 (0) | **0** (0) | 31,8 → 49,8 ms |
| 4b. Çok ağır yük: 0–14 ms dalgalı (ortalama %95), +40 ppm | **11 214** (3 470) | **0** (8 fade'li boşluk, güvenlik tamponu 50 ms'ye çıktı) | 74,9 → 86,4 ms |
| 5. Her 4 sn'de 25 ms takılma | 94 (57) | **0** (2 fade'li boşluk) | 70,3 → 59,3 ms |
| 6. Sistem donması (DPC fırtınası), 30 ms, iki iş parçacığı birden | 80 (38) | 80 (20) – uygulama çözemez | 64,0 → 63,6 ms |
| 7. Mikrofon 44,1 kHz, kablo 48 kHz | Cihaz açılmıyor | **0** (0) | – → 49,8 ms |

### 7.5 Gerçek Windows iş parçacıkları, tam CPU yükü altında (MicVSTStress, Bölüm B)

**Ortam:** GitHub runner, 4 mantıksal CPU, 4 normal öncelikli yük iş parçacığı. Tablo, 10 ms
periyotlu bir ses iş parçacığının uyanma gecikmesini (ms) gösterir:

| İş parçacığı ayarı | p50 | p99 | p99,9 | maks | > 5 ms |
|---|---:|---:|---:|---:|---:|
| Boş sistem, düz iş parçacığı | 0,264 | 0,527 | 0,594 | 1,039 | 0 |
| Yük altında, düz iş parçacığı (en kötü durum) | 9,192 | 29,013 | 40,066 | 51,687 | 2 066 / 3 000 |
| Yük altında, MMCSS NORMAL (1.1.1 / JUCE varsayılanı) | 0,277 | 0,536 | 0,565 | 0,719 | 0 |
| **Yük altında, MMCSS HIGH + güç kısıtlaması kapalı (1.2)** | 0,277 | 0,541 | 0,617 | 0,635 | 0 |

**Yorum (dürüstçe):**

- **MMCSS'nin etkisi:** Yük altında öncelik vermek şart; düz bir iş parçacığı onlarca ms geç
  kalıyor.
- **NORMAL ve HIGH farkı:** Bu testte görülmüyor, çünkü yapay yük normal öncelikli. HIGH yalnızca
  başka MMCSS iş parçacıkları (oyun/OBS/Discord sesi) yarıştığında fark eder.
- **Kazancın kaynağı:** 1.1.1 de zaten MMCSS kullanıyordu. Bu makinede çıtırtıları gideren şey
  öncelik değil, 7.4'teki köprü tasarımı.

**Gerçek `AudioBridge`, iki gerçek zamanlı iş parçacığı arasında, tam yükte, +70 ppm saat farkıyla
60 sn:** 0 boşalma, 0 tık. Öğrenilen drift 57,0 ppm (gerçek +70; öğrenme sürüyor, seviyeyi P terimi zaten tutuyor). Güvenlik tamponu 10 ms'de kaldı.

### 7.6 Ek uçtan uca denemeler (sınırlarıyla)

- **Linux, gerçek 1.2 motoru** (`AudioEngine` + `AudioBridge` + JUCE graf; ALSA → PulseAudio;
  sinüs kaynağı mikrofon, null-sink kablo):
  - **Doğrulanan:** Cihazlar açıldı, iki iş parçacığı çalıştı, sinyal kabloya ulaştı.
  - **Sınır:** PulseAudio'nun ALSA çıkışı veriyi ~2 sn'lik patlamalar halinde tükettiği için
    (Windows WASAPI'de böyle bir davranış yok) ses kalitesi ölçümü olarak geçerli değil.
  - **Kazanç:** Bu deneme 6. bölümdeki kilitlenme hatasını buldurdu; hata düzeltildi ve teste
    bağlandı.
- **Wine 9 altında CI'da derlenen Windows exe:**
  - **Doğrulanan:** Program açıldı. Düşük gecikme modu Wine'da desteklenmediği için yeni
    paylaşımlı-mod geri dönüşü devreye girdi ve iki cihaz açıldı. JUCE yaması etkindi
    (`AvSetMmThreadPriority(…, 1)` = HIGH). Render iş parçacığı 10 ms'de bir düzenli çalıştı.
    Süreçte ağ DLL'i yüklenmedi (7.2).
  - **Sınır:** Wine'ın yakalama (capture) akışı hiç veri vermedi (`GetBuffer` hep boş döndü).
    Ayrıca pencere Wine'da görünmedi (1.1.1'de de aynı). Bu yüzden Wine'da ses akışı
    doğrulanamadı. Bunlar Wine'ın sınırları; Windows davranışı hakkında bir şey söylemez.
- **Arayüz düzeni:** Gerçek `MainComponent`, Linux'ta JUCE'un yazılım çizicisiyle 584×521
  boyutunda (pencerenin en küçük iç alanı) görüntüye çizildi. Safety satırı ve iki durum satırı
  çakışmadan sığıyor. Yazı tipi Windows'tan farklı, yerleşim aynı.

## 8. Başlangıç (v1.1.1) ile son durumun karşılaştırması

| Konu | v1.1.1 | v1.2.0 | Kanıt |
|---|---|---|---|
| Saat kayması (drift) | Telafi yok → düzenli tık | Öğrenilip telafi ediliyor (±300 ppm test edildi) | 7.4 (2, 3), BridgeTests |
| Yük altında takılmalar | 25 ms takılma → 94 tık | 0 tık, 2 fade'li boşluk; Auto tampon kendini büyütüyor | 7.4 (4b, 5) |
| Çok ağır yük (%95) | 11 214 tık | 0 tık | 7.4 (4b) |
| Gerçek iş parçacıkları, tam yük, 60 sn | – | 0 boşalma, 0 tık | 7.5 |
| Sistem donması (iki iş parçacığı) | 80 | 80 (çözülemez, bkz. 9) | 7.4 (6) |
| Gecikme | ~32–62 ms (drift'e göre kayıyordu) | ~50 ms sabit (Auto); 5 ms seçeneğiyle daha az | 7.4 |
| 44,1/48 kHz karışık | Açılmıyor | Çalışıyor | 7.4 (7), BridgeTests |
| Mono zincir | Yalnız sol kanal | İki kanal | Kod, 6. bölüm |
| Ağ erişimi | WinINet + Winsock içe aktarılıyor ve yükleniyor | Yok (statik ve çalışma zamanında doğrulandı) | 7.2 |
| Exe boyutu | 9,1 MB | 6,8 MB (JUCE 9 daha büyük ama ölü kod artık atılıyor) | 7.1 |
| Derleme uyarıları (MicVST kodu) | 0 | 0 | 7.1 |
| Birim testleri | 42 durum, geçiyor | 59 durum, hepsi geçiyor | 7.1 |
| Teşhis | Yalnız durum satırı | CPU yükü, güvenlik tamponu, drift, kesinti sayacı ve nedenleri | Arayüz |

## 9. Çözülemeyen veya doğrulanamayan noktalar

**Doğrulanamayanlar.** Bunlar yapılmadı ve yapılmış gibi gösterilmedi:

1. **Gerçek donanım testi yapılmadı.** Gerçek mikrofon + VB-Cable + ağır oyun + OBS/Discord ile
   uzun deneme yapılamadı. GitHub'ın Windows makinelerinde ses cihazı yok; bu ortamda Windows ve
   ses donanımı da yok. Yeni WASAPI cihaz kodu (iki ayrı cihaz, JUCE yamasının çalışma anındaki
   etkisi, watchdog, paylaşımlı mod geri dönüşü) Windows'ta derlendi, ama gerçek bir ses cihazıyla
   çalıştırılmadı. Köprünün kendisi simülasyonda ve gerçek Windows iş parçacıklarıyla test edildi
   (7.4, 7.5).
2. **Kendi sistemindeki gerçek kazanç ölçülmedi.** Hangi nedenin baskın olduğu (drift mi, eklenti
   yükü mü, DPC mi) cihaza ve sürücüye bağlı. 10. bölümdeki test bunu senin sisteminde ölçer.
3. **Arayüz Windows'ta görsel olarak kontrol edilmedi.** Yalnızca Linux çizimiyle kontrol edildi
   (7.6).

**Çözülemeyenler:**

4. **Sistem donmaları** (DPC/ISR fırtınası, iki iş parçacığını birden durduran sürücü sorunları)
   uygulama tarafından çözülemez. Teşhis: LatencyMon; çözüm: sürücü/BIOS/güç ayarları.
5. **Eklentilerin ağ erişimi:** MicVST'nin kendisi ağa çıkmaz, ama eklentiler süreç içinde çalışır.
   Tam kapalı kutu için güvenlik duvarı kuralı gerekir. Çevrimiçi aktivasyon isteyen eklentiler
   önceden etkinleştirilmelidir.

**Bilinen davranışlar ve yan etkiler:**

6. **Gecikme artışı:** Auto modunda ~10–20 ms ek gecikme var.
7. **Drift göstergesi:** Bir tahmindir; bir kesintiden sonra geçici olarak sapabilir. Ses
   kalitesini etkilemez.
8. **1.1.1'e geri dönerken:** 1.2'nin yazdığı `config.xml` 1.1.1'de de açılır, ancak 1.1.1
   güncelleme onayını bir kez yeniden sorar.
9. **README lisans cümlesi:** "JUCE … GPL" yazıyor; doğrusu AGPLv3/ticari (5. bölüm). Depo sahibinin
   kararına bırakıldı.

## 10. Derleme, kurulum ve test

### Hazır exe

- **Yeni exe:** `ci-sonuclari/kararlilik/v1.2.0` dalındaki `MicVST.exe`. GitHub Actions bunu bu
  daldaki kodla Windows'ta derledi; SHA-256 değeri `metrics.json` dosyasında.
- **Karşılaştırma için orijinal:** `ci-sonuclari/baseline` dalındaki `MicVST.exe` (1.1.1).

### Kaynaktan derleme (Windows, Visual Studio 2022 "Desktop development with C++")

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target MicVST MicVSTTests MicVSTStress
.\build\MicVSTTests_artefacts\Release\MicVSTTests.exe              # birim testleri (çıkış kodu 0 = tamam)
.\build\MicVSTStress_artefacts\Release\MicVSTStress.exe rapor.md   # ölçümler (Bölüm A + B)
.\tools\check-no-network.ps1 build\MicVST_artefacts\Release\MicVST.exe
```

### Kurulum

1. MicVST'yi tepsiden kapat (sağ tık → Quit).
2. Yeni `MicVST.exe`'yi eskisinin yerine koy. Ayarlar (`%APPDATA%\MicVST\config.xml`) ve eklenti
   önbelleği aynen kullanılır.
3. **İsteğe bağlı, tam kapalı kutu için** (yönetici PowerShell):
   `.\tools\firewall-block.ps1 -Exe "C:\…\MicVST.exe"`

### Kendi sisteminde A/B testi (önerilen)

1. Her zamanki zincirini kur (Pro-Q 4, soothe2 vb.). "Safety" ayarını **Auto** bırak.
2. **Ağır yük oluştur:** Oyunu aç, OBS'de kayıt/yayın başlat (CPU ve GPU dolsun).
3. **İzle:** Alt durum satırındaki `dropouts` sayacını 30 dakika izle. Turuncuya dönerse imleci
   üzerine getir; ipucu nedeni gösterir:
   - *Output ran dry:* Güvenlik tamponu yetmedi; Auto kendini büyütür.
   - *Plug-in chain slower than real time:* Zincir bir blokta gerçek zamandan uzun sürdü.
     `CPU peak` değerine bak; zinciri hafiflet ya da buffer'ı büyüt.
   - *Microphone driver gaps:* Sürücü veya DPC sorunu; LatencyMon ile kontrol et.
4. **Kaydet ve dinle:** OBS'de mikrofon olarak "CABLE Output" kaydet, sonra tıklara bak. Sürekli
   bir ton (ör. "aaaa" ya da test sinyali) tıkları kolay duyurur.
5. **Karşılaştır:** Aynı koşullarda 1.1.1 ile tekrarla.
6. **Günlük:** `%APPDATA%\MicVST\log.txt` dosyasında süreç ayarları ("Process: priority above
   normal ok …"), cihaz açılışı ve her köprü olayı yazılı.

### CI'yı yeniden çalıştırma

`kararlilik/**` dallarına her gönderimde `kararlilik-test` iş akışı çalışır. Orijinal 1.1.1'i ve
gönderilen dalı Windows'ta derler; testleri, kapalı kutu kontrolünü ve ölçümleri çalıştırıp
sonuçları `ci-sonuclari/*` dallarına yazar.
