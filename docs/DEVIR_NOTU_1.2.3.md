# MicVST – Teknik devir notu (1.2.3, 24 Ekim 2026)

> **Bu notu okuyan yeni Claude oturumu için:** Bu notu yalnızca oku, kendiliğinden hiçbir işe başlama.
> Kod değiştirme, commit/push yapma, CI tetikleme, test başlatma yok. Notu okuyunca kullanıcıya kısaca
> (Türkçe) ne anladığını söyle ve **talimatını bekle**.

Bu not, 22–24 Ekim 2026 oturumunda MicVST için yapılan her şeyi, son exe'de gerçekten ne olduğunu ve
nelerin doğrulanmadığını tek yerde toplar. Doğrulama sözcükleri bilerek ayrı tutuldu:

| Etiket | Anlamı |
|---|---|
| **KULLANICI ✓** | Kullanıcı kendi bilgisayarında denedi ve sorunsuz dedi (hangi denemeyle olduğu yazılı) |
| **BİRİM ✓ (L)** | Linux'ta birim testi geçti (Windows'a özgü kod orada çalışmaz) |
| **EXE ✓** | Son exe'nin içinde olduğu dosya incelemesiyle görüldü (sürüm bilgisi, metinler, içe aktarılan DLL'ler) |
| **DERLENDİ (W)** | GitHub Actions Windows'ta hatasız derlendi; **test çalıştırılmadı** (`[hizli]` derleme) |
| **DOĞRULANMADI** | Kodda var, ama hiçbir ortamda denenmedi |

---

## 1. Tek bakışta durum

- **Teslim edilmiş, tam test edilmiş son sürüm: 1.2.2.** Dal `kararlilik/oncelik`, exe commit'i `baf4efd`,
  SHA-256 `5F70A33F5408D49DA053B7B7252803018656E517E68A240CABC44C02537D7798`, 9 987 072 bayt.
  Tam tur #3 ile test edildi. Rapor: `docs/TEST_RAPORU_WIN11_1.2.2.md`. Bu raporun son hali yalnızca
  `kararlilik/oncelik` dalında; 1.2.3 dalındaki kopya taslak.
- **Çalışılan sürüm: 1.2.3.** Dal `kararlilik/bildirim-sesi`, son commit `11ff813`. **Teslim edilmedi.**
  - Son deneme exe'si: **deneme 14**, SHA-256 `5309492837DEEC0D01B422DBB2BA2BB75123812DECD372E8BC6C74B353DB28FB`,
    10 050 560 bayt, `[hizli]` derlemesi.
  - **1.2.3 için hiçbir tam test turu tamamlanmadı.** Başlatılan 4 tam tur (`a331bdf`, `a2371a0`,
    `72bf989`, `972bc8a`) yeni değişiklikler geldiği için iptal edildi.
  - Kullanıcının son talimatı: *"uzun teste başlama, exe bana ver, ben test ederim, ona göre yönlendiririm"*.
    Tam tur, kullanıcı söyleyene kadar başlatılmaz.
- **Deneme 14 kullanıcıya verildi.** Sohbet eki olarak ve `C:\MicVST\test\deneme14\MicVST.exe` konumunda.
  **Henüz denenmedi**; sonuç bekleniyor.
- **Kullanıcının bilgisayarında asıl exe deneme 8.** `C:\MicVST\MicVST.exe` şu an deneme 8
  (SHA-256 `e3f4d74d…`, 1.2.3 ara sürümü), 1.2.2 değil. 1.2.2 exe'si
  `ci-sonuclari/kararlilik/oncelik` dalında `MicVST.exe` olarak duruyor.

---

## 2. Depo, dallar, exe'ler

| Ne | Değer |
|---|---|
| Depo | GitHub `videoRLS/MicVST` (eski adres `videoRLS/micvst` yönlendiriyor) |
| `main` | Bu çalışmada **hiç dokunulmadı**. PR / merge / release yok |
| `kararlilik/oncelik` | 1.2.2. Teslim sonrası belge commit'leri dahil son: `abeebc9` |
| `kararlilik/bildirim-sesi` | 1.2.3. `256961e`'den açıldı (1.2.2'nin ürün kodu, `baf4efd` ile aynı), son `11ff813` |
| `kararlilik/macos` | macOS çalışması, ayrı iş, bu turda dokunulmadı |
| `ci-sonuclari/kararlilik/oncelik` | 1.2.2 tam tur #3 sonuçları + `MicVST.exe` (5F70A33F…) |
| `ci-sonuclari/kararlilik/bildirim-sesi-hizli` | Son `[hizli]` derlemesi: `MicVST.exe`, `metrics.json`, `build.log`, `QUICK-BUILD-NO-TESTS.txt`. **Her yeni `[hizli]` commit'i bunun üzerine yazar** |

**1.2.2'den bu yana değişen ürün dosyaları** (`git diff --stat 256961e..11ff813 -- src cmake CMakeLists.txt`):
- Ses: `src/audio/CueSounds.{h,cpp}`
- Arayüz: `src/ui/RenderMode.{h,cpp}` (yeni), `src/ui/SettingsComponent.{h,cpp}`, `src/ui/MainComponent.cpp`,
  `src/ui/PluginListView.cpp`, `src/ui/BuiltInEffectEditor.cpp`, `src/ui/CuePanel.cpp`
- Uygulama: `src/Main.cpp`
- Ayar dosyası: `src/state/Persistence.{h,cpp}`
- Çeviriler: `src/i18n/Translations.cpp` (üretilmiş)
- Derleme: `CMakeLists.txt` (sürüm 1.2.3, yeni dosyalar)

**Ses yolu değişmedi.** `AudioEngine`, `AudioBridge`, `InputQueue`, `PluginChain`, `GraphConnections`,
`Recorder`, `fx/` ve `cmake/PatchJuce.cmake` 1.2.3'te değişmedi.

---

## 3. Kullanıcı, ortam ve çalışma kuralları

**Kullanıcı:** canlı yayın ve sosyal medya içeriği üretiyor (OBS). Türkçe, kısa yazışmayı tercih ediyor.
Exe'yi kendisi deneyip yönlendirmek istiyor.

**Bilgisayarı** (`docs/kanit_win11/test_edilen_surumler.md`, 22 Ekim envanteri):
- **Sistem:** Windows 11 (kullanıcının 24 Ekim beyanı), AMD Ryzen 7 5700X, 32 GB. 22 Ekim envanteri
  yapı 19045.7725 (Windows 10 22H2 numarası) okumuştu; kesin sürüm/yapı için `winver` sonucu bekleniyor.
- **Ekran kartı:** modeli belgelerde kayıtlı değil (envanterde yalnızca NVIDIA HD Audio var).
- **Ses donanımı:** HyperX QuadCast 2 S (USB, 48 kHz / 32 bit), VB-Audio Virtual Cable 3.3.1.7
  (24 bit / 48 kHz), Realtek HD hoparlör, kulaklık.
- **MicVST ayarları:** 48 kHz, tampon 480, güvenlik payı 80 ms.
- **PC saati:** kullanıcının tercihiyle 3 saat geride. Dosya zamanları buna göre okunmalı.
- **Zinciri:** Clear, Otomatik Kazanç, FabFilter Pro-DS ×3, Pro-Q 4 (lineer faz), Pro-C 3, SDRR2,
  Saturn 2 ×2, DC1A3, TDR VOS SlickEQ GE, TDR Limiter 6 GE.
- **Oyun:** GTA 5 oynuyor; beyaz şerit sorunu bununla ortaya çıktı.
- **Klasörler:**
  - Deneme exe'leri `C:\MicVST\test\denemeN\MicVST.exe`.
  - Test kayıtları ve betikleri `C:\MicVST\test\`.
  - 1.2.2 belgeleri `C:\MicVST\teslim_1.2.2\`.
  - Ayar yedeği `C:\MicVST\yedek\2026-10-22_2031\`.

**Kurallar** (kullanıcı koydu, devam ediyor):
1. **Dallar:** `main`'e dokunma. Kullanıcı istemedikçe PR, merge ya da release yok. Çalışma `kararlilik/...` dallarında.
2. **İnternet:** program internetsiz kalmalı. Exe'de WinINet / WinHTTP / Winsock / URLMon olmamalı;
   `tools/check-no-network.ps1` ve tam tur bunu denetler.
3. **Doğrulama dili:** test edilmeyen hiçbir şey BAŞARILI yazılmaz, mutlak garanti verilmez.
4. **Ses:** ses kalitesi her şeyden önce gelir. Arayüz değişiklikleri ses motoruna dokunmamalı.
5. **Kullanıcı ayarları:** FabFilter ve diğer ticari eklentilerin ayarlarına, kullanıcının zincirine ve
   hazır ayarlarına dokunma. Testten önce `%APPDATA%\MicVST`'yi yedekle, sonra geri koy.
6. **Onay:** geri alınamaz işlerden önce sor. `C:\MicVST\MicVST.exe`'yi sormadan değiştirme. Denemeler
   `C:\MicVST\test\denemeN\` klasörüne gider ve sohbete de eklenir.
7. **Kayıtlar:** kullanıcının kayıtlarına dokunma. Test kayıtları `C:\MicVST\test\` altına.
8. **Commit:** her şey commit edilip push edilir. Commit mesajının sonuna o oturumun kendi
   `Co-Authored-By` / `Claude-Session` satırları eklenir.
9. **Gizlilik:** kullanıcının e-posta adresi hiçbir yere gönderilmez.

---

## 4. 1.2.3'teki özellikler – ne yapıyor, nerede, doğrulama durumu

### 4.1 Bildirim sesleri (Sustur aç/kapa sesi)

**Sorun (1.2.2, bulgu #14):** hızlı aç-kapada kulaklıkta "çıt" sesi duyuluyordu. Windows'un `PlaySound`
fonksiyonu yeni sesi başlatırken eskisini dalganın ortasında kesiyordu; ölçülen basamak −13 dBFS'e kadar.

**1.2.3'te ne değişti** (`src/audio/CueSounds.{h,cpp}`):

- **Kendi karıştırıcısı (`CueMixer`).** Yeni ses, çalan sesi 10 ms'de (480 örnek) yumuşakça kısarak
  bitirir, kesmez.
- **Çalma yolu.**
  - `waveOut` (`WAVE_MAPPER`, Windows'un varsayılan çıkışı), 48 kHz, 16 bit stereo, 10 ms bloklar.
  - Cihaz yeni açıldığında 100 ms ses duraklatılmış cihaza dolduruluyor, sonra başlatılıyor
    (`waveOutRestart`). Sonrasında kuyrukta 40 ms kalıyor. Uyuyan dahili hoparlörün ilk seste
    cızırdaması bununla düzeldi.
  - Cihaz yalnızca ses çalarken ve ondan sonraki 2 saniye açık kalır.
  - Varsayılan çıkış en çok 2 saniyede bir yeniden kontrol edilir.
- **Hata durumları.**
  - Cihaz açılamazsa (başka program özel modda tutuyor, hiçbir şey takılı değil) o tek ses
    `PlaySound` ile çalınır. Bir sonraki ses cihazı yeniden dener. Hata log'a bir kez yazılır.
  - Cihaz 1 saniye ses almazsa (çalarken çıkarıldı) kapatılır ve bir sonraki seste yeniden açılır.
- **Eşit yükseklik.**
  - Her ses (hazır sesler ve kullanıcının `%APPDATA%\MicVST\Sesler` klasöründeki dosyaları)
    hazırlanırken aynı yüksekliğe getirilir.
  - Ölçüm BS.1770 K-ağırlıklı, 100 ms bloklar ve kapılama (gating) ile yapılır.
  - **%100 = −15,5 LUFS**, tepe en fazla **−3 dBFS**. Sivri bir dosya taşmak yerine biraz daha sessiz kalır.
  - Başka örnekleme hızları 48 kHz'e çevrilir (windowed-sinc).
  - Kullanıcının dosyaları değiştirilmez.
- **Ses düzeyi.**
  - Varsayılan düzey **%40** (eski %80'e göre yaklaşık 4 dB daha sessiz).
  - Hazırlanmış sesler önbellekte tutulur (seçili iki ses + son dinlenen). Kaydırıcı yalnızca yeniden
    ölçekler: adım başına yaklaşık 15 ms (eskiden ~0,4 s takılma).
- **Değişmeyen davranış:** ses yalnızca Windows'un varsayılan çıkışında çalar, asla kabloya gitmez.
  Varsayılan çıkış kablonun kendisiyse hiç çalmaz.

**Doğrulama:**

| Ne | Durum |
|---|---|
| Hazır seslerin eşitliği (0,25 dB içinde); 0 dBFS / −40 dBFS / 44,1 kHz kendi dosyalar −15,5 LUFS; saniyede 10 aç-kapada sesin kendisinden büyük basamak yok | BİRİM ✓ (L), birim grubu "Cue sounds" |
| Hızlı aç-kapada tık yok; hoparlörde ilk ses temiz; gecikme hissedilmiyor | KULLANICI ✓ ("ses düzelmiş, hoparlör kısmı da gayet iyi, gecikme yok gibi"; deneme 3 ve sonrası) |
| Ses çalarken Windows varsayılan çıkışını değiştirmek (sağ alt) | KULLANICI ✓ (hoparlöre geçince hoparlörde, kulaklığa dönünce kulaklıkta çaldı) |
| Başka programın kulaklığı özel modda kullanması, Bluetooth kulaklık, çalarken cihazı çıkarmak | DOĞRULANMADI. Kullanıcıya nasıl deneyeceği anlatıldı, sonuç bildirilmedi |
| Gerçek Windows arka ucunun 200 hızlı çalma altında CI testi | DOĞRULANMADI. Tam tur `a331bdf` iptal edildi |

### 4.2 Ekran kartı beyazlanması (GTA 5 sonrası beyaz şerit) ve "Arayüz çizimi" (GPU / CPU)

**Belirti (kullanıcının bulgusu):**
- GTA 5'ten (tam ekran) masaüstüne dönünce MicVST penceresinin altında beyaz şerit kalıyordu.
- Ekran görüntüsünde içerik başlık çubuğu yüksekliği kadar kaymış görünüyordu. Pencere kenarı çekilince düzeliyordu.
- Kullanıcıya göre sorun yalnızca iki pencerede değil, hepsinde vardı.

**Neden (çıkarım, kanıtlanmadı):**
- JUCE 9.0.4 Windows'ta pencereleri varsayılan olarak **Direct2D (ekran kartı)** ile çiziyor.
- Tam ekran oyun ekran modunu değiştirince Direct2D'nin swap chain'i pencereyle hizasız kalıyor.
- GDI (işlemci) ile çizimde sorun görülmedi.

**Denenenler (sırayla):**

| Commit | Deneme | Ne | Sonuç |
|---|---|---|---|
| `9537f42` | belirsiz (büyük olasılıkla verilmedi; hemen ardından 2ae4469 geldi) | Ekran modu değişince pencereyi 1 px yeniden boyutlama | İşe yaramadı (sonraki denemelerde beyazlık sürdü) |
| `2ae4469` | 5 | Yenileme hızı değişimi + boyut bekçisi | İşe yaramadı |
| `ab93ead` | 6 | Hemen + 0,5 s + 2 s yeniden eşitleme, saniyede 4 kontrol | Kullanıcı: "yine beyazlık oluyor" |
| `e23cf60` | 7 | Kendi pencerelerini **GDI** ile çiz | Düzeldi |
| `5e5e764` | 8 | Saniyede 4 kontrol döngüsü kullanıcı isteğiyle kaldırıldı, GDI kaldı | **KULLANICI ✓** ("güzel sorunsuz çalışıyor"). Kullanıcı "CPU'da hafif fazlalık var gibi" dedi; **ölçülmedi** |
| `4f87b4f` | 9 | 3 mod. Otomatik, tam ekran programı algılayıp (`SHQueryUserNotificationState`) çalışırken GPU↔CPU geçiyordu | **Hayalet görüntü:** canlı motor değişiminde eski Direct2D resmi kaldı, düğme sırası çift göründü (kullanıcı ekran görüntüsü) |
| `24347de` | 10 | Otomatik her açılışta **bir kez** karar verir, kapanana kadar sabit (kullanıcının yazdığı kural) | Kullanıcı bu kuralı istedi; ayrıca "sorun" bildirmedi |
| `a804376` | – | Yalnızca GPU / Yalnızca CPU, pencereyi yeniden oluşturarak anında | CI iptal edildi, **dağıtılmadı** |
| `c12c3b3` | 13 | `a804376` + efekt düzeltmesi | Kullanıcı: geçişte ve FPS değişiminde **titreme**, FPS listesi moda göre güncellenmiyor |
| `5c179ba` + `11ff813` | 14 | Aynı pencerede yalnızca motor değişimi + tüm pencereyi yeniden çizme | **DOĞRULANMADI** (bkz. aşağı) |

**Son exe'deki davranış** (`src/ui/RenderMode.{h,cpp}`, Ayarlar → Görünüm ve başlangıç → "Arayüz çizimi"):

- **Otomatik (ekran kartı öncelikli), varsayılan.**
  - Her açılışta ekran kartı bir kez denetlenir: `D3D11CreateDevice` (HARDWARE, BGRA desteği); Windows'un
    "Microsoft Basic Render Driver"ı (VendorId 0x1414, DeviceId 0x8c) sayılmaz. Denetim SEH `__try`
    korumasıyla yapılır; bozuk bir sürücü programı düşürmez.
  - Kullanılabilir kart varsa Direct2D, yoksa GDI seçilir. Seçim program kapanana kadar değişmez.
  - Seçim değiştirilirse ayar kaydedilir ve bir sonraki açılışta uygulanır. Ayarlarda "Değişiklik MicVST
    yeniden başlatılınca uygulanır." yazar.
- **Yalnızca ekran kartı:** Direct2D. Kart yoksa geri dönüş yapılmaz; Ayarlar uyarı rengiyle yazar,
  log'a da düşer.
- **Yalnızca işlemci:** GDI.
- **Anında geçiş (son exe):**
  - Yalnızca ekran kartı ↔ Yalnızca işlemci programı yeniden başlatmadan uygulanır.
  - Pencere yeniden oluşturulmaz. Her MicVST penceresinin (ana pencere, Ayarlar) çizim motoru
    `ComponentPeer::setCurrentRenderingEngine` ile değiştirilir.
  - Ardından `RedrawWindow (RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW)` ve
    `repaint()` çağrılır.
- **Kayıt ve log:** ayar `config.xml`'de `uiRenderer` (`""` = Otomatik, hiç yazılmaz; `"gpu"`; `"cpu"`).
  Log'da "Arayüz çizimi: ekran kartı / işlemci" satırı çıkar.
- **Kapsam:** eklenti (VST3) pencereleri bu ayardan etkilenmez; onlar eklentinin kendi çizimi.

**Bilinmesi gerekenler:**
- ⚠ **Canlı geçişte hayalet riski.** Deneme 14'teki canlı geçiş, deneme 9'da hayalet görüntü bırakan
  JUCE çağrısının aynısını kullanıyor. Fark yalnızca ek tam yeniden çizim. **Hayaletin tekrar
  çıkıp çıkmadığı bilinmiyor**; kullanıcı deneme 14'te bakacak.
  - Çıkarsa seçenekler:
    - (a) Pencereyi yeniden oluşturma (deneme 13; titriyor).
    - (b) GPU/CPU seçimini de Otomatik gibi bir sonraki açılışta uygulamak.
    - (c) JUCE `D2DRenderContext` / swap chain bırakılırken pencere içeriğinin nasıl temizlendiğini
      Windows'ta incelemek.
- ⚠ **Varsayılanda beyaz şerit geri gelebilir.** Ekran kartı olan bir bilgisayarda varsayılan Otomatik
  → Direct2D. **GTA sonrası beyaz şerit bu modda yeniden görülebilir.** Ayarlardaki ipucu bunu
  söylüyor: kenarı çekmek düzeltir, kalıcı çözüm "Yalnızca işlemci". Direct2D'deki şeridin kendisi için
  bir düzeltme **yok**.

**Doğrulama:**

| Ne | Durum |
|---|---|
| Kimlik/ayar dönüşümleri, motor seçimi, "GPU/CPU anında, Otomatik sonraki açılışta" mantığı | BİRİM ✓ (L), grup "Render mode" (Windows çizimi değil, yalnız mantık) |
| Mod metinleri exe'de | EXE ✓ |
| GDI ile GTA sonrası beyaz şerit yok | KULLANICI ✓ (deneme 8; o sürümde hep GDI vardı) |
| Yalnızca işlemci modunda aynı sonuç (deneme 10+) | DOĞRULANMADI açıkça (aynı GDI motoru) |
| Otomatik'in ekran kartı olmayan PC'de GDI seçmesi | Mantık BİRİM ✓; gerçek kartsız PC'de DOĞRULANMADI |
| Canlı GPU↔CPU geçişinde hayalet / titreme olmaması | **DOĞRULANMADI (deneme 14 bekleniyor)** |

### 4.3 Titreme (flicker)

- **1.2.2'de çözülen (KULLANICI ✓, 23 Ekim):** tema veya dil değişince Ayarlar penceresi bir an "hayalet"
  gibi görünüyordu. Yeni pencere artık gösterilmeden önce yerleştiriliyor ve koyulaştırılıyor; eskisi
  yenisi açılınca kapanıyor. Tema/dil değişince pencere içeriği hâlâ yeniden kuruluyor (`rebuild()`);
  bu tasarım gereği.
- **Deneme 13'te bildirilen:** FPS veya GPU/CPU değişince arayüz titriyordu. İki neden vardı:
  1. Ayarlar'daki FPS / ölçer / çizim değişiklikleri kaydetmek için `onThemeApplied` yolunu kullanıyordu.
     Bu yol LookAndFeel'i de yeniliyordu.
  2. Deneme 13'te GPU/CPU geçişi pencereyi kapatıp yeniden oluşturuyordu.
- **Deneme 14'teki düzeltme:**
  - Yeni `onPersist` yalnızca ayarı kaydeder, görünüme dokunmaz.
  - GPU/CPU geçişi pencereyi yeniden oluşturmaz.
- **Durum: DOĞRULANMADI** (deneme 14 bekleniyor).
- **Not:** LookAndFeel yenilemesinin (`NightLookAndFeel::refresh`, yalnız renk atıyor) titremenin asıl
  nedeni olduğu kanıtlanmadı. Kesin olan, deneme 13'teki pencere yeniden oluşturmanın görünür bir yanıp
  sönme yaptığı.

### 4.4 Arayüz akıcılığı (FPS)

Ayarlar → Görünüm ve başlangıç → "Arayüz akıcılığı".

- **Ne yapar:** ana penceredeki zamanlayıcının saniyedeki çalışma sayısını ayarlar
  (`MainComponent::timerCallback`). Bu zamanlayıcı şunları sürer:
  - giriş/çıkış seviye göstergeleri ve tepe değerleri;
  - Sustur / Ham ses / Kendini dinle düğmelerinin durumu;
  - Deneme kaydı'nın kayıttan çalmaya geçişi (`AudioEngine::testTick`).
- **Seçenekler:** işlemcide **60 / 30 / 20 / 10**, ekran kartında **60 / 30**; varsayılan **30**, Otomatik
  ne seçerse seçsin. Kullanıcı önce CPU'da 30/20/10 yazdı, hemen ardından 60'ı unuttuğunu söyledi;
  CPU listesi bu yüzden 60/30/20/10.
- **Anında uygulama:** değer zamanlayıcının bir sonraki çalışmasında uygulanır, yeniden başlatma gerekmez.
- **Mod değişince (deneme 14):** liste ekran kartı ↔ işlemci geçişinde anında güncellenir
  (`SettingsComponent::refreshFps`, `applyRenderer` içinden ve 250 ms'lik ayar zamanlayıcısından).
  Moddaki listede olmayan kayıtlı değer 30 olarak uygulanır ve kayıtlı değer korunur. Örnek: CPU'da 20 →
  GPU'ya geçince 30 → CPU'ya dönünce 20.
- **Kayıt:** `config.xml`'de `uiFps`; 30 yazılmaz.
- **Ses etkisi:** ses motoru bu değeri hiç okumaz. Tek yan etki: Deneme kaydı'nın kayıttan çalmaya geçişi en
  fazla 1/FPS saniye geç başlar (10 FPS'te ≤100 ms). Kaydın kendisi ses iş parçacığında örnek hassasiyetinde.
  Bu ayardan bağımsız olanlar: durum satırı (`DevicePanel`, 500 ms), zincir satırlarındaki canlı
  göstergeler (`PluginListView`, 10 Hz), Ayarlar penceresi (250 ms).

**Doğrulama:**
- Birim testi "FPS: 60 / 30 / 20 / 10 with the processor, 60 / 30 with the graphics card, default 30,
  list follows a switch": BİRİM ✓ (L). 24 Ekim'de güncel kodla yeniden derlenip çalıştırıldı.
- Metinler exe'de: EXE ✓.
- Gerçek Windows'ta listenin güncellenmesi ve titreme olmaması: **DOĞRULANMADI** (deneme 14).
- FPS'in işlemci kullanımına etkisi: **ölçülmedi**.

### 4.5 Ses seviyesi göstergeleri (göster / gizle)

- **Ayar:** Ayarlar → "Ses seviyesi göstergelerini göster" (varsayılan açık); `config.xml`'de `hideMeters="1"`.
- **Gizliyken:**
  - giriş/çıkış göstergeleri, etiketleri ve dB değerleri gizlenir; çizilmez ve güncellenmez;
  - durum satırı kartın ortasına alınır;
  - ses motoru aynı şekilde ölçmeye ve işlemeye devam eder.
- **Uygulama:** anında, zamanlayıcının bir sonraki çalışmasında.
- **Doğrulama:** varsayılan ve gizleme mantığı BİRİM ✓ (L). Gerçek pencerede kullanıcı sonucu: deneme
  11/13'te özel bir sorun bildirilmedi, açık bir "çalışıyor" da yok. **Kesin değil.**

### 4.6 Dahili efektler: ikinci açılışta boş pencere

- **Belirti (kullanıcı):** dahili bir efekt (ör. Çıkış Limiteri) açılıp kapatıldıktan sonra yeniden
  tıklanınca pencere boş geliyordu.
- **Neden (kanıtlandı):** `BuiltInEffectEditor` yok edilirken `effect.editorBeingDeleted (this)` çağrılmıyordu.
  - JUCE'de bunu çağırmak host'un işi. JUCE'nin kendi VST3 / VST / LV2 pencereleri çağırıyor.
  - Çağrılmayınca işlemci eski editörü hâlâ etkin sanıyor, `createEditorAndMakeActive()` boş dönüyordu.
- **Düzeltme:** `src/ui/BuiltInEffectEditor.cpp`, yıkıcı.
- **Ek (a804376):** penceresi açık bir efekte yeniden tıklanınca ikinci, boş pencere açılmıyor; açık olan
  öne geliyor (`PluginListView::openEditor`).
- **Kapsam:** VST3 eklentilerinde bu hata yoktu (JUCE'nin `VST3PluginWindow` yıkıcısı çağrıyı yapıyor).
  Kazanç, Otomatik Kazanç ve Mono/Stereo'nun penceresi yok (kontrolleri zincir satırında).

**Doğrulama:**
- Birim testi "Built-in effect editors" her dahili efekt için aç → kapat → tekrar aç → editör aynı boyutta mı
  diye bakıyor. Düzeltmeden önce **başarısızdı**, sonra **geçti**: BİRİM ✓ (L).
- Gerçek pencerede kullanıcı: deneme 13'te FPS sorunlarını bildirdi, efekt sorununu tekrar bildirmedi.
  Açık bir "düzeldi" yok. **Kullanıcı doğrulaması bekleniyor.**

### 4.7 Ses motoru

- **1.2.3'te ses motoruna dokunulmadı** (§2'deki diff).
- **Tek dokunuş noktası:** bildirim sesi çağrısı Sustur ile aynı ileti iş parçacığı çağrısında yapılıyor;
  1.2.2'den beri böyle.
- **Bildirim sesi akışı:** kendi iş parçacığı ve `waveOut` ile çalışır. Ses iş parçacığından bağımsızdır;
  kabloya gitmez.
- **1.2.2 ses motorunun ölçülmüş özellikleri:** `docs/TEST_RAPORU_WIN11_1.2.2.md`. 1.2.3 için yeniden
  ölçülmedi (tam tur yok).

---

## 5. Deneme exe'leri (bu oturumda verilenler)

Commit eşleşmesi, CI bitiş saatleri ile dosya saatlerinin karşılaştırılmasından çıkarıldı. SHA'lar
`/mnt/user-data/outputs/denemeN` kopyalarından alındı.

| Deneme | Commit | İçerik | SHA-256 (ilk 12) | Kullanıcı sonucu |
|---|---|---|---|---|
| 1 | `6513a9d` | Bildirim sesi: tık yok, eşit yükseklik, %40 | c303bca36322 | Hoparlöre ilk geçişte ilk ses cızırtılı (deneme 1 ya da 2; hangisi olduğu kayıtta net değil) |
| 2 | `0146b55` | Cihazı her seste yeniden dene, çıkarılmaya dayanıklılık | 844598b29bb5 | (bkz. deneme 1) |
| 3 | `e30e609` | 5×20 ms ön doldurma | 98194cd690f3 | Hızlı basışta gecikme → 10 ms bloklara geçildi |
| 4 | büyük olasılıkla `eff7062` (klasör yok, eşleşme doğrulanamadı) | 100 ms ön doldurma, sonra 40 ms | – | Bildirim sesi temiz ve hızlı (KULLANICI ✓, deneme 6 ile birlikte bildirildi) |
| 5 | `2ae4469` | Pencere yeniden eşitleme | 9e745628e7a0 | Beyazlık sürdü |
| 6 | `ab93ead` | Saniyede 4 kontrol | 42b3ae1efddc | "Yine beyazlık oluyor" |
| 7 | `e23cf60` | GDI | 9d5df4a84fa2 | – |
| 8 | `5e5e764` | GDI, döngü yok | e3f4d74d48c3 | **Sorunsuz** (şu an `C:\MicVST\MicVST.exe`) |
| 9 | `4f87b4f` | 3 mod, dinamik Otomatik | 96efe5f61b14 | Hayalet çift düğme sırası |
| 10 | `24347de` | Otomatik açılışta bir kez | be6d27f4182c | Kayıtta özel bir sonuç yok |
| 11 | `d029b20` | FPS + ölçer göster/gizle | 7f58624deda7 | Bu denemeden sonra bildirildi: efekt ikinci açılışta boş pencere (hata 1.2.2'den beri vardı) |
| 12 | `a804376` | – | – | CI iptal, verilmedi |
| 13 | `c12c3b3` | Efekt düzeltmesi + yeniden oluşturarak anında GPU/CPU | d2225e640eb4 | Titreme; FPS listesi moda göre değişmiyor |
| **14** | **`11ff813`** | Aynı pencerede canlı geçiş, FPS listesi anında, kayıtta görünüm yenilenmiyor | **5309492837de** | **Bekleniyor** |

1.2.2 teslim exe'si: `5f70a33f5408` (`baf4efd`).

---

## 6. Son exe (deneme 14) – ne kontrol edildi

Dosya: `C:\MicVST\test\deneme14\MicVST.exe` ve sohbet eki. Ayrıca
`ci-sonuclari/kararlilik/bildirim-sesi-hizli` → `MicVST.exe` (bir sonraki `[hizli]`'ye kadar).

- **SHA-256:** `5309492837DEEC0D01B422DBB2BA2BB75123812DECD372E8BC6C74B353DB28FB`; CI `metrics.json` ile aynı.
- **CI derlemesi:** `build_ok: true`, hata 0, MicVST uyarısı 0, JUCE uyarısı 0, JUCE yama işareti 3,
  JUCE 9.0.4. `QUICK-BUILD-NO-TESTS.txt`: bu derlemede **Windows'ta hiçbir test çalışmadı**.
- **PE başlığı:**
  - FileVersion / ProductVersion `1.2.3`; x64; bağlama zamanı 24.10.2026 01:29 UTC.
  - İçe aktarılan DLL'ler: ADVAPI32, CFGMGR32, COMCTL32, COMDLG32, DWrite, GDI32, IMM32, KERNEL32, OLEAUT32,
    SHELL32, SHLWAPI, USER32, WINMM, shcore, d2d1, d3d11, dcomp, dwmapi, dxgi, ole32.
  - **Ağ DLL'i yok.** Dosyada "wininet / winhttp / ws2_32 / urlmon / WSAStartup / InternetOpen" metni de yok.
  - Bu, `check-no-network.ps1`'in yerini tutmaz; çalışma anı ağ denetimi 1.2.3 için yapılmadı.
- **Metinler:** "Arayüz akıcılığı", "Yalnızca işlemci", "30 FPS (varsayılan)", "Ses seviyesi
  göstergelerini göster" exe'de var.
- **Birim testleri (Linux, `11ff813` kaynağı):** 24 Ekim'de güncel kodla yeniden derlendi.
  - "Render mode" grubu geçti.
  - Tüm gruplar (24 Ekim, `11ff813` kaynağıyla yeniden derlenmiş ikili): **251 bölüm, FAIL 0, "All tests completed successfully"** – BİRİM ✓ (L). Windows'ta 1.2.3 birim testleri çalıştırılmadı (tam tur yok).
  - Not: 24 Ekim 04:05 civarındaki ilk "geçti" sonucu eski test ikilisiyle alınmıştı (derleme bir hata
    yüzünden yarıda kalmıştı). Bu not yazılırken yeniden derlenip çalıştırıldı.

---

## 7. Tamamlanan / eksik / çözülmemiş

### Tamamlandı ve kullanıcı doğruladı
- Bildirim seslerinde hızlı aç-kapada tık yok; hoparlörde ilk ses temiz; gecikme hissedilmiyor (deneme 4+).
- Varsayılan çıkış değiştirilince bildirim sesi yeni çıkışta çalıyor.
- GDI ile GTA sonrası beyaz şerit yok (deneme 8).
- 1.2.2'nin tüm teslim kalemleri (rapor, kılavuz, arşiv). Ayrıntı `kararlilik/oncelik` dalında.
- Bulgu #13 açıklandı: Görev Yöneticisi'nden "Görevi sonlandır" Windows'ta "yanıt vermiyor" kaydı
  bırakıyor; yeniden üretildi, ürün hatası değil.

### Kodda var, kullanıcı doğrulaması bekleniyor (deneme 14)
- GPU ↔ CPU canlı geçişte titreme ve hayalet olmaması.
- FPS listesinin moda göre anında değişmesi; FPS değişiminde titreme olmaması.
- Dahili efektlerin ikinci, üçüncü… açılışta dolu gelmesi; açık efekte tıklayınca pencerenin öne gelmesi.
- Ölçer göster/gizle.

### Eksik (yapılmadı)
- **1.2.3 tam test turu.** Hiç tamamlanmadı. Kullanıcı söyleyene kadar başlatılmayacak. Tam tur ~3,5 saat.
  Ses yoluna dokunulmadığı için `[uzun]` zorunlu değil, ama bildirim sesi motoru yeni.
- **1.2.3 teslim paketi:** exe + SHA, rapor eki, kullanıcı kılavuzu güncellemesi, 1.2.3 arşiv zip'i
  (`tools/arsiv_olustur.sh`).
- **Geliştirici kılavuzu güncellemesi.** `docs/GELISTIRICI_KILAVUZU.md` 1.2.3'ü hâlâ "1.2.2 + bildirim
  sesi" diye anlatıyor. Arayüz çizimi, FPS, ölçer ve efekt düzeltmesi orada yok; bu not onları kapsıyor.
- **Kullanıcı kılavuzu.** `docs/UZUN_OMUR_KILAVUZU.md` 1.2.2 için. Yeni ayarlar ve "beyaz şerit → Yalnızca
  işlemci" önerisi eklenmedi.
- **Bildirim sesi uç durumları:** özel mod, Bluetooth, çalarken çıkarma. Gerçek donanımda denenmedi.
- **Ölçümler:** GDI'nin ve FPS seçeneklerinin işlemci kullanımı ölçülmedi.

### Çözülmemiş sorunlar / riskler
1. **Direct2D modunda GTA sonrası beyaz şerit.** Varsayılan Otomatik ekran kartını seçerse görülebilir.
   Çözüm yok; geçici yol kenarı çekmek, kalıcı yol "Yalnızca işlemci".
2. **Canlı motor değişiminde hayalet riski.** Deneme 9'da görüldü; deneme 14'te tekrar edip etmediği bilinmiyor.
3. **#6** Zincirde Clear varken kapanışta Windows olay günlüğünde "MicVST çöktü" kaydı. Çıkış kodu 0,
   kayıp yok. Eklentinin kapanışında oluyor; denenen düzeltme işe yaramadı.
4. **#12** Zorla kapatılınca `*_temp*.xml` / `*.TMP` dosyaları kalabiliyor. Öneri: açılışta eski geçici
   dosyaları sil. Yapılmadı.
5. **Kod imzası yok.** Her yeni exe'de SmartScreen uyarısı çıkabilir.
6. **Uygulama adı kararı** kullanıcıda bekliyor (eski devam notundan).

---

## 8. Programın diğer özellikleri (1.2.2 ve öncesi, değişmedi)

Ayrıntı ve ölçümler: `CHANGELOG.md`, `docs/TEST_RAPORU_WIN11_1.2.2.md`, `docs/DEVAM_NOTU.md`.

- **Yol:** mikrofon (WASAPI) → VST3 zinciri (ayrı eklenti iş parçacığı) → köprü → sanal kablo (VB-Cable).
  - Köprü iki cihaz saati arasında ppm düzeltmesi, güvenlik payı ve yumuşak geçişli atlama/ekleme yapar.
  - Cihaz kaybında yeniden açar (`ReopenBackoff`).
- **İşlem önceliği:** Normal (varsayılan) / Normalin Üstünde / Yüksek. Ses iş parçacıkları her durumda MMCSS.
- **Dahili elemanlar:**
  - Kazanç (−30…+12 dB).
  - **Otomatik Kazanç:** normal konuşma tepe ~−20 dBFS, bağırma ~−15 dBFS, kazanç 0…+6 dB, tavan −10 dBFS
    true peak, noise gate yok. Zincirdeki yerini kullanıcı belirler. Kullanıcının onayladığı ayar; değiştirme.
  - Mono ↔ Stereo.
  - **26 dahili efekt** (EQ'lar, De-esser, Kompresör, Gürültü Kapısı, Limiter, LUFS ölçer, Spektrum…).
- **Hazır ayarlar:** 15 erkek + 15 kadın fabrika ayarı, kullanıcı ayarları, `.micvstpreset` dışa/içe aktarma,
  "Önceki zincire dön".
- **Sustur ve Ham ses:**
  - Sustur 15 ms yumuşak geçişli.
  - Ham ses: A/B karşılaştırma, zincir gecikmesi kadar geciktirilmiş, 30 ms çapraz geçiş.
  - Sistem geneli kısayollar: Sustur varsayılan F9; kullanıcı Sustur'a fare tuşu atadığını söyledi. Fare tuşları desteklenir.
- **Kendini dinle ve Deneme kaydı:** kulaklık çıkışı; Deneme kaydı 10 saniye kaydedip çalar.
- **Kayıt:** WAV 16/24/32/32f (BWF, RF64), FLAC 16/24, MP3 (LAME 4.0, CBR/VBR); 44,1/48 kHz;
  kilitsiz 30 s tampon, yazıcı iş parçacığı, her saniye başlık yenileme.
- **Arayüz:** 9 tema (varsayılan Gece), 11 dil, Windows koyu başlık çubuğu.
- **Tepsi ve başlangıç:** tepsi menüsü; Windows açılışında başlatma (`--tray`).
- **Log:** `%APPDATA%\MicVST\log.txt` (2 MB'ta kırpılır). Ayarlar: `%APPDATA%\MicVST\config.xml`.
- **Kapalı kutu:** exe ağa bağlanmaz. 1.2.2'de 6 senaryoda çalışma anı ağ etkinliği 0 ölçüldü.

---

## 9. Derleme

- **Araçlar:** JUCE **9.0.4** + MicVST yamaları (`cmake/PatchJuce.cmake`, 3 yama işareti), C++17,
  CMake ≥ 3.22, MSVC (VS 2022), x64.
- **Windows'ta:**
  ```
  cmake -S . -B build -A x64
  cmake --build build --config Release --target MicVST MicVSTTests
  ```
- **İnternetsiz:** arşiv paketindeki `OKU.md` (`FETCHCONTENT_SOURCE_DIR_JUCE`, `FETCHCONTENT_FULLY_DISCONNECTED=ON`).
  1.2.2 arşivi (`MicVST_1.2.2_arsiv.zip`, ~37 MB) sohbette verildi. 1.2.3 arşivi yapılmadı.
- **Linux'ta (birim ve motor testleri):** `cmake -S . -B build-linux -G Ninja`, sonra
  `ninja MicVSTTests` ve `./MicVSTTests_artefacts/Release/MicVSTTests`. Windows'a özgü kod orada çalışmaz.
- **Hedefler:** `MicVST` (uygulama), `MicVSTTests` (birim), `MicVSTEngineTest` (motor), `MicVSTStress`
  (simülasyon), `MicVSTSnapshot` (ekran görüntüleri). `RenderMode.cpp` ilk ikisine ve Snapshot'a eklendi.
- **Sürüm:** `CMakeLists.txt` → `project(MicVST VERSION 1.2.3)`. Exe Özellikler'inde ve Ayarlar → Hakkında'da görünür.
- **Çeviriler:** Türkçe metin kodda `tr("...")` ile yazılır. Sonra sırayla:
  1. `python3 tools/i18n.py extract`
  2. `i18n/<dil>.json` dosyalarına 10 dilin çevirisini ekle (JSON: `indent=1`, anahtarlar sıralı,
     `ensure_ascii=False`)
  3. `python3 tools/i18n.py check`
  4. `python3 tools/i18n.py generate` → `src/i18n/Translations.cpp`

  Baştaki/sondaki boşluklar çevirilerde de aynı olmalı ("Şu an: " örneği). `Translations.cpp` elle düzenlenmez.

## 10. Testler ve CI

- **GitHub Actions** (`.github/workflows/kararlilik-test.yml`, yalnızca `kararlilik/**` dallarında):

  | Commit mesajında | Ne olur | Süre | Sonuç dalı |
  |---|---|---|---|
  | `[hizli]` | yalnızca exe | ~7–10 dk | `ci-sonuclari/<dal>-hizli` |
  | (normal) | tam tur | ~3,5 sa | `ci-sonuclari/<dal>` |
  | `[uzun]` | uzun koşular | ~6 sa | `ci-sonuclari/<dal>-uzun-<iş>` |
  | `[birim]` | derleme + birim testleri | ~45 dk | `ci-sonuclari/<dal>-birim` |
  | `[skip ci]` | hiçbir şey | – | – |

  Ayrıca `[kaynak]`, `[vst3tani]`, `[vst3uzun]` var.
- **Sonuç almak:** `git fetch origin ci-sonuclari/<dal>`, ardından `git show origin/...:metrics.json` /
  `MicVST.exe`. Günlük indirme 403 verebilir.
- **Bekleme yöntemi** (bu oturumda kullanılan): arka planda `gh run list -b <dal> -w kararlilik-test`
  ile commit SHA'sının işini bul, `gh run view <id> --json status` tamamlanana kadar bekle, sonuç dalını
  fetch et. Yeni commit gelince eski koşuyu `gh run cancel` ile iptal et.
- **Birim test grupları (40 grup, `--list`):** yeni olanlar "Cue sounds", "Render mode", "Built-in effect editors".
  Listesi için `MicVSTTests --list`, tek grup için `MicVSTTests "Render mode"`.
- **Motor testi modları:** quality, latency, integrity, record, cues, devices, hotplug, effects, autogain,
  vst3suite, priority, long, chaos, startxrun, recordlong…
- **Linux ortamı:** 2 çekirdekli konteyner. Motor testlerinde "makine takılması" görülebilir; son söz
  Windows tam turunun.

## 11. Belgeler

| Dosya | Ne | Dal |
|---|---|---|
| `docs/DEVIR_NOTU_1.2.3.md` | Bu not | bildirim-sesi |
| `CHANGELOG.md` | 1.2.3 dahil tüm değişiklikler (İngilizce) | bildirim-sesi |
| `docs/GELISTIRICI_KILAVUZU.md` | Kod haritası, derleme, test, kurallar (1.2.3 kısmı eksik, bkz. §7) | bildirim-sesi |
| `docs/DEVAM_NOTU.md` | 1.2.2 dönemi devam notu (eski) | ikisi de (son hali oncelik'te) |
| `docs/TEST_RAPORU_WIN11_1.2.2.md` | 1.2.2 Windows test raporu (son hali yalnızca oncelik'te) | oncelik |
| `docs/UZUN_OMUR_KILAVUZU.md` | Kullanıcı kılavuzu (1.2.2) | ikisi de |
| `docs/TEST_PROMPTU_WIN11.md` | Kapsamlı Windows test görev tanımı | ikisi de |
| `docs/kanit_win11/` | 1.2.2 kanıtları (R4 tamamı yalnızca oncelik'te) | ikisi de |

## 12. Önerilen sıradaki adımlar (kullanıcı onayıyla)

1. Kullanıcının deneme 14 sonucunu al (GPU↔CPU geçişi, FPS listesi, titreme, efekt pencereleri, ölçerler).
2. Hayalet çıkarsa: §4.2'deki seçeneklerden birini kullanıcıya sor.
3. Kullanıcı "tamam" deyince:
   - geliştirici ve kullanıcı kılavuzlarını güncelle;
   - **tam turu** başlat (normal commit);
   - sonuçlarla 1.2.3 rapor ekini yaz;
   - 1.2.3 arşivini oluştur;
   - exe + SHA'yı teslim et.

   `C:\MicVST\MicVST.exe` ancak kullanıcı onaylarsa değişir.

## 13. Yeni Claude hesabında ilk mesaj (kopyala-yapıştır)

Önce yeni hesapta GitHub bağlantısını kur ve `videoRLS/MicVST` deposuna erişim ver. Sonra şunu gönder:

```
Merhaba. MicVST adlı Windows programım üzerinde başka bir Claude hesabıyla çalışıyordum, burada kaldığım
yerden devam etmek istiyorum. Depo: GitHub videoRLS/MicVST, çalışma dalı: kararlilik/bildirim-sesi.

1. Depoyu ekle ve kararlilik/bildirim-sesi dalını aç. main dalına asla dokunma.
2. Önce yalnızca oku, sırayla:
   - docs/DEVIR_NOTU_1.2.3.md (ana kaynak, eksiksiz oku)
   - CHANGELOG.md içindeki [1.2.3] bölümü
   - docs/GELISTIRICI_KILAVUZU.md
   - Gerekirse kararlilik/oncelik dalındaki docs/TEST_RAPORU_WIN11_1.2.2.md
3. Okurken hiçbir şeyi değiştirme: kod düzenleme, commit, push, CI tetikleme, test başlatma yok.
4. Okuduktan sonra bana Türkçe ve kısa olarak şunları yaz:
   - son exe'de (deneme 14, commit 11ff813) gerçekten neler var,
   - bunlardan hangileri doğrulandı, hangileri doğrulanmadı,
   - eksik kalan işler ve çözülmemiş sorunlar.
   Notta yazmayan bir şeyi doğrulanmış gibi sunma; emin olmadığın yerde "bilinmiyor" de.
   Sonra talimatımı bekle.

Çalışma kurallarım (devir notu §3'te ayrıntılı):
- Benimle Türkçe ve kısa konuş.
- main'e dokunma; ben istemedikçe PR, merge ya da release yok.
- Program internetsiz kalmalı (exe'de ağ kütüphanesi yok).
- Ses kalitesi her şeyden önce; arayüz değişiklikleri ses motoruna dokunmamalı.
- Test edilmeyen hiçbir şeyi başarılı diye yazma, mutlak garanti verme.
- FabFilter ve diğer eklentilerimin ayarlarına, zincirime ve hazır ayarlarıma dokunma.
- Geri alınamaz işlerden önce bana sor. C:\MicVST\MicVST.exe dosyasını sormadan değiştirme.
- Deneme exe'lerini sohbete ekle; bilgisayarım bağlıysa C:\MicVST\test\denemeN\MicVST.exe olarak da koy.
- Hızlı deneme için [hizli] derlemesi kullan. Ben söylemeden uzun test (tam tur) başlatma.
- Her değişikliği commit edip push et; commit mesajının sonuna bu oturumun kendi Co-Authored-By ve
  Claude-Session satırlarını ekle.

Şu an deneme 14'ü test ediyorum; sonucunu sana yazacağım.
```
