# MicVST 1.2.2 – Windows 11 Kapsamlı Test Görevi (Claude için tam prompt)

> Bu dosya, MicVST'nin güncel sürümünü Windows 11 için A'dan Z'ye test edecek **yeni bir Claude oturumu**
> için yazıldı. Oturum GitHub'a bağlıdır (depo: `videoRLS/MicVST`). Önce bu dosyanın tamamını oku, sonra
> 0. bölümdeki kurallara uyarak 9. bölümdeki sırayla çalış.
>
> **Kullanıcının amacı:** programı **yıllarca** güvenle kullanmak. Onun için en önemli iki şey **ses kalitesi**
> ve **kararlılık**.
>
> **TESLİM: en geç 23 Ekim 2026, 23:59 (İstanbul saati, UTC+3) – hedef 22:00.** Görev 22 Ekim akşamı
> başlıyor; elinde yaklaşık 27 saat var. Zaman planı **bölüm 0.1**'de; ona uy.

---

## 0. Rolün ve kesin kurallar

1. Kullanıcıyla **Türkçe**, kısa ve net konuş. Ara durumlarda iki üç cümle yeter; sonuçta: ne geçti, ne
   kaldı, ne düzeltildi, exe + SHA-256.
2. **`main` dalına dokunma. PR açma, birleştirme (merge), sürüm (release) yayınlama yok** – kullanıcı açıkça
   istemedikçe. Çalışma dalı: **`kararlilik/oncelik`**.
3. **Çalışan özellikleri, arayüzü ve davranışı gereksiz değiştirme.** Yalnızca test ekle; bir hata bulursan
   en küçük düzeltmeyi yap, nedenini ölç, yeniden test et.
4. **Program tamamen çevrimdışı kalmalı:** exe'ye hiçbir ağ kütüphanesi (WinINet, WinHTTP, Winsock, URLMon…)
   girmeyecek. CI bunu her turda denetler; bozulursa iş durur.
5. **Test edilmeyen hiçbir şeyi BAŞARILI gösterme.** Durumlar yalnızca: **BAŞARILI** (test edildi, geçti),
   **BAŞARISIZ** (test edildi, geçmedi), **ATLANDI** (bilerek koşulmadı, nedeni yazılı), **ENGELLENDİ**
   (bu ortamda yapılamaz, nedeni yazılı). Mutlak garanti verme ("asla bozulmaz" yok; "şu koşullarda geçti" var).
6. **Ses kalitesi her şeyden önce gelir.** Kayıt (özellikle **WAV 24 bit**, ayrıca 16/32 bit, 32 bit float,
   FLAC, MP3) bit düzeyinde doğrulanacak; tek bir tık, kayıp örnek, kesik, yanlış başlık kabul edilmez.
7. Kullanıcının bilgisayarına bağlısın ve tam erişim iznin var (bölüm 7.0; program `C:\MicVST\`). Orada bir
   şey çalıştırmadan önce **`%APPDATA%\MicVST` klasörünü yedekle** (config.xml, plugin_cache.xml, log.txt, Sesler\\). **FabFilter ve diğer ticari eklentilerin
   ayarlarını değiştirme.** Testten sonra yedeği geri yükle. Geri alınamaz bir işlemden önce kullanıcıya sor.
8. Commit mesajlarının sonuna **kendi oturumunun** Co-Authored-By / Claude-Session satırlarını ekle (bu
   oturumunkileri değil). Bir "stop hook" commit edilmemiş değişiklik bırakmana izin vermez: ara işleri
   `[skip ci]` ile commit edip gönder.
9. Önceki raporu bozma: yeni rapor ayrı dosya (`docs/TEST_RAPORU_WIN11_1.2.2.md`).
10. **Teslim tarihi var (bölüm 0.1), ama ucuz testlerde örnekleme yapma.** "Birkaçını denedim" yok: 30 hazır
    ayarın hepsi, 26 efektin hepsi, kaydın her biçim × bit × sıkıştırma × hız × kanal birleşimi (K22). Zamanlamaya
    bağlı ya da rastgele testleri farklı tohumlarla tekrarla: **tam tur 3 kez, `[uzun]` 2 kez** (0.1'deki plana
    göre). Tek bir başarısızlık bile "rastgele oldu" diye geçilmez; nedeni bulunur, açıklanır ya da düzeltilir.
    Süre yetmediği için yapılamayan her şey raporda **ATLANDI (süre)** olarak yazılır, asla BAŞARILI değil.
11. **Uzun ömür (bölüm 5-R) zorunludur.** "Ömür boyu sorunsuz" garantisi verilemez ve verme; görevin, zamanla
    çıkabilecek her hata türünü bugünden aramak, bulunamayanları açıkça yazmak ve kullanıcıya bir bakım yolu
    (arşiv + kılavuz + sağlık kontrolü) bırakmak.

## 0.1 Teslim tarihi ve zaman planı (İstanbul saati, UTC+3)

**Kesin sınır: 23 Ekim 2026, 23:59.** Rapor, exe, SHA-256, arşiv ve kılavuz bu saatten önce kullanıcıda olacak;
hedef **22:00** (yedek pay). Saati her zaman `mcp__claude_ai__current_time` ile ya da bilgisayarda `Get-Date` ile kontrol et.

Bilinen süreler:

| İş | Süre |
|---|---|
| Tam tur | ~2,5–3 sa |
| `[uzun]` | ~6 sa (işler paralel; long, chaos, app ve vst3long 330 dk) |
| `[birim]` | ~45 dk |
| simweek 168 sa | 30 dk (bu yüzden 720 sa ≈ 2 sa) |

CI iş akışlarında `concurrency` yok: koşular birbirini iptal etmez ve paralel çalışır. Kod değiştirmeden tur
başlatmak için:
- `gh workflow run kararlilik-test.yml --ref kararlilik/oncelik` (tam tur; ek olarak "baseline" işi orijinal v1.1.1'i derleyip `ci-sonuclari/baseline` dalına yazar, zararsız);
- `gh workflow run kararlilik-uzun.yml --ref kararlilik/oncelik` (uzun).

`[uzun]` içeren bir commit hem tam turu hem uzun koşuları başlatır.

| Zaman | Ne yapılır |
|---|---|
| **22 Ekim, ilk 1 saat** | Bu dosyayı ve ilgili belgeleri oku. Bilgisayarda SHA, yedek ve envanter (7.0). **Hemen, kod değiştirmeden** iki CI turunu başlat: tam tur #1 ve `[uzun]` #1 (`gh workflow run`; ~02:00 ve ~04:00'te biter). Kullanıcıya planı 3–4 cümleyle söyle ve **bu gece ile yarın hangi saatlerde müsait olduğunu sor** (konuşma, dinleme, USB işleri için). |
| **22 Ekim akşamı – ~23:30 (kullanıcı varken)** | Önce en önemlisi: **U2, U3 (kayıt kalitesi: bütün biçimler)**, sonra U6, U7, U8, U10, U13. Kullanıcı yatmadan önce **R4 uzun kullanım testini başlat** (izleme betiği + bilgisayarın uykuya geçmemesi; kullanıcıya sor). |
| **Gece (~23:30 – 08:00)** | Konteynerde: R1 kod denetimi ve taşma testleri; K22, K14, R5, R6, R3 işi, R2 (720 sa), R9 arşiv + ağsız derleme işi, I6, `tools/analiz_kayit.py`. L'de derle ve koştur, ASan/UBSan. CI #1 sonuçlarını oku. Yeni testleri **en geç 07:00'de** `[uzun]` etiketli tek commit ile gönder: tam tur #2 (~10:00'da biter) ve `[uzun]` #2 (~13:00'te biter). Kullanıcı uyurken bekleme sırasında kendine 30–60 dk'lık hatırlatma kur (send_later); konteyner yeniden başlarsa iş kaybolmasın. |
| **23 Ekim 09:00 – 16:00 (kullanıcı müsait oldukça)** | U4 (60 dk kayıt), U5, U9 (30 dk Discord), U11 (uyku/uyanma, DPI, tema), U12. R7'nin geri alınabilir kısımları (USB yuvası, varsayılan cihaz). CI #2 sonuçları. Bulunan hataların düzeltilmesi. |
| **23 Ekim 16:00 – KOD DONDURMA** | Bu saatten sonra ürün kodu değişmez. Son kodla **tam tur #3**'ü başlat (~19:00'da biter). Ses yolunu değiştiren bir düzeltme 15:00'ten sonra yapıldıysa yeni `[uzun]` yetişmez: raporda uzun koşuların hangi exe ile yapıldığını ve aradaki farkı açıkça yaz. 16:00'dan sonra bulunan kritik hata düzeltilmez; ayrıntısıyla raporlanır ve son doğrulanmış exe teslim edilir. |
| **23 Ekim ~18:00** | R4'ü durdur, verileri topla, analiz et. Kılavuzun taslağıyla **U15 sağlık kontrolü provası.** |
| **23 Ekim 19:00 – 22:00** | Tam tur #3 sonuçları. Kılavuz, arşiv zip'i ve rapor. Exe, SHA, rapor, kılavuz ve arşivi kullanıcıya gönder. |

**Bu teslimde kısalanlar (raporda ATLANDI (süre) / kısmi olarak yaz):**
- R4: 72 sa – 7 gün yerine ~18–20 sa. Kullanıcı isterse teslimden sonra MicVST'yi açık bırakır; izleme betiği çalışmaya devam eder, log daha sonra analiz edilir.
- "Farklı günlerde 3 tam tur" yerine 24 saat içinde 3 tam tur.
- R7'nin geri alınamaz kısımları (VB-Cable yeniden kurulumu, eklenti kaldırma) yalnızca kullanıcı açıkça isterse yapılır.

**Süre yetmezse öncelik sırası** (üstteki önce bitirilir):
1. K + U3 (kayıt kalitesi);
2. C (ses kalitesi ve bütünlük);
3. P ve `[uzun]` (kararlılık);
4. I (Sustur, kısayollar, bildirim sesleri);
5. kalan U testleri ve R1;
6. kalan R testleri;
7. arşiv ve kılavuz;
8. N (görünüm).

Rapor hiçbir koşulda atlanmaz.

**Kullanıcıya durum bildirimi:** başlangıçta, ~23:30, 09:00, 14:00, 19:00 ve teslimde; 2–3 cümle (ne bitti,
ne sürüyor, bir sorun var mı, kullanıcıdan ne lazım).

---

## 1. Proje ve depo bilgileri

| Bilgi | Değer |
|---|---|
| Depo | `videoRLS/MicVST` (fork; orijinal geliştiricinin 1.0–1.1.1 sürümleri üzerine 1.2.0 / 1.2.1 / 1.2.2) |
| Dal | `kararlilik/oncelik` |
| Son ürün kodu | `62ff06b` (sonraki commit'ler yalnızca belge/test: `0f7c5fe` ve sonrası) |
| Test edilecek exe | `62ff06b`'nin Windows CI derlemesi, **SHA-256 `d079483bcfffee393250d1ffba1ae871093266efa0250841d96288ac0424ae15`**, 9 986 048 bayt |
| Exe'yi alma | `git fetch origin ci-sonuclari/kararlilik/oncelik && git show FETCH_HEAD:MicVST.exe > MicVST.exe` (ve `metrics.json` içindeki SHA ile karşılaştır). Daha yeni bir tam tur o dalın üzerine yazar; o zaman ilgili commit'in sonucunu al. |
| Dil / çatı | C++17, JUCE 9.0.4 (+ `cmake/PatchJuce.cmake` ile 5 küçük yama), MSVC 19.51 (CI), statik bağlı tek exe |
| Üçüncü taraf | LAME 4.0 (`third_party/lame-4.0`, LGPL, SHA-256 denetimli), libFLAC 1.5.0 (JUCE içinde), VST3 SDK 3.8.0 (JUCE içinde) |
| Ayar dosyası | `%APPDATA%\MicVST\config.xml` (+ `plugin_cache.xml`, `log.txt`, `Sesler\`) |
| Önce oku | `docs/DEVAM_NOTU.md`, `CHANGELOG.md` (1.2.2 bölümü), `docs/TEST_RAPORU_1.2.2.md`, bu dosya |

### 1.1 Kaynak kodda nerede ne var

| Alan | Dosyalar |
|---|---|
| Ses motoru | `src/audio/AudioEngine.*`, `AudioBridge.*` (güvenlik tamponu + saat farkı), `InputQueue.h`, `RealtimeSupport.*`, `GraphConnections.*`, `MonitorOutput.*` (Kendini dinle) |
| Kayıt | `src/audio/Recorder.*`, `Resampler.h`, `src/ui/RecordingPanel.*` |
| Bildirim sesleri | `src/audio/CueSounds.*`, `resources/sounds/*.flac` (16 ses), `src/ui/CuePanel.*` |
| Efektler | `src/audio/BuiltInEffects.*`, `src/audio/fx/*`, `BuiltInGain.*`, `BuiltInAutoGain.*` |
| Hazır ayarlar | `src/state/Presets.*` |
| Kalıcılık | `src/state/Persistence.*` |
| Arayüz | `src/ui/MainComponent.*`, `SettingsComponent.*`, `DevicePanel.*`, `PluginListView.*`, `HotkeyDialog.*`, `GlobalHotkeys.*`, `TrayIcon.*`, `Theme.h` |
| Çeviri | `i18n/src/<dil>.txt`, `tools/i18n.py`, `tools/i18n_src.py`, `src/i18n/Translations.cpp` (582 metin × 10 dil + Türkçe) |
| Aygıt biçimi günlüğü | `src/audio/DeviceFormat.*` |

### 1.2 Derleme (Linux konteynerinde, hızlı yerel testler için)

Depoyu al: `git clone https://github.com/videoRLS/MicVST.git && cd MicVST && git checkout kararlilik/oncelik`.
Yeni bir makinede eksikse paketler: `apt-get install -y cmake ninja-build g++ pkg-config libfreetype-dev libfontconfig1-dev libx11-dev libxext-dev ffmpeg flac fonts-noto-core fonts-noto-cjk` ve `pip install --break-system-packages numpy scipy`. JUCE 9.0.4 ilk yapılandırmada GitHub'dan otomatik indirilir (FetchContent); LAME depoda hazırdır.

```
cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-DJUCE_ALSA=0 -DJUCE_JACK=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XINPUT=0 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0"
ninja -C build-linux MicVSTTests MicVSTEngineTest MicVSTSnapshot MicVSTStress
```
ASan/UBSan: aynı bayraklara `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -fsanitize-recover=shift-base,shift-exponent` (C ve C++), `RelWithDebInfo`. Konteyner 2 çekirdekli ve gürültülüdür: motor testlerinde "machine hiccups" (sanal aygıt gecikmesi) görülür; testler bunu ayrı sayar.

### 1.3 Test programları

| Program | Kullanım |
|---|---|
| `MicVSTTests [ad ...]` | birim testleri (234 bölüm). Ada göre: `"Recorder"`, `"Cue sounds"`, `"AutoGain"`, `"Presets"`, `"Shortcuts (keys and mouse buttons)"` … Ortam değişkeni `MICVST_TEST_RF64=1` 4 GB üstü testini açar; `MICVST_RECORDER_KEEP=<klasör>` test kayıtlarını saklar |
| `MicVSTEngineTest <mod>` | gerçek `AudioEngine`, gerçek iş parçacıkları, WASAPI benzeri sanal mikrofon/kablo/kulaklık. Modlar: `quality latency flutter mono monochain buffers edits devices hotplug openfail faults msgblock gain autogain effects record cues priority vst3 vst3suite vst3info long chaos dump clickpos rtprobe trace burn` |
| `MicVSTStress` | ses yolu simülasyonu + gerçek Windows iş parçacıkları tam yük altında; `--day 24` 24 saatlik sanal gün |
| `MicVSTSnapshot <klasör>` | arayüz görüntüleri: 11 dil × 9 tema, Ayarlar bölümleri, efekt pencereleri |
| `tools/ci-*.ps1` | CI betikleri: derleme, motor testleri, çalışma anı kapalı kutu, kısayollar (gerçek exe), masaüstü yardımcıları, kayıt dosyalarının başka programlarda açılması, uygulama dayanıklılığı |

### 1.4 CI (GitHub Actions) – nasıl tetiklenir, sonuç nerede

| Commit mesajında | Ne çalışır | Süre | Sonuç dalı |
|---|---|---|---|
| (normal) | **tam tur** `kararlilik-test.yml`: derleme, birim, kayıt dosyaları (ffmpeg + Windows), kapalı kutu, stres, 24 sa simülasyon, arayüz, tüm motor testleri + bütünlük analizleri, çalışma anı kapalı kutu, gerçek exe kısayol/bildirim sesi, masaüstü yardımcıları | ~2,5–3 sa | `ci-sonuclari/kararlilik/oncelik` |
| `[birim]` | yalnızca derleme + birim + kayıt dosyaları | ~45 dk | `…-birim` |
| `[hizli]` | yalnızca exe | ~20 dk | `…-hizli` |
| `[uzun]` | `kararlilik-uzun.yml`: long / chaos (330 dk), app (gerçek exe 330 dk), simweek (168 sa sanal + 24 sa Otomatik Kazanç), win11arm (ARM64 Windows 11: birim + motor + 60 dk), vst3long p0/p1/p2 (gerçek VST3 zinciri, tam CPU yükü, 330 dk × 3 öncelik) | ~6 sa | `…-uzun-<iş>` |
| `[vst3uzun]` | yalnızca vst3long | ~5,5 sa | `…-uzun-vst3long-p*` |
| `[vst3tani]` | gerçek tarayıcı 20× eklenti, boşta ve tam yükte | ~1 sa | `…-vst3tani` |
| `[skip ci]` | hiçbir şey | – | – |

- Çalışma durumu: `gh run list -R videoRLS/MicVST --commit <sha>`; adımlar: `gh api repos/videoRLS/MicVST/actions/runs/<id>/jobs`.
- **CI iş günlükleri indirilemez** (blob sunucusu kapalı). Sonuçları her zaman sonuç dalından al: `git fetch origin ci-sonuclari/... && git show FETCH_HEAD:<dosya>`.
- Önemli sonuç dosyaları: `unit-tests.txt`, `engine/<mod>.txt`, `engine/integrity.txt`, `recordings-check.md`, `hotkeys/hotkeys.md`, `desktop/desktop-scripts.md`, `runtime/runtime-check.md`, `network-check.txt`, `imports.txt`, `measurements.md`, `day-simulation.md`, `metrics.json` (exe SHA), `ui/*.png`, `MicVST.exe`.
- CI makineleri: `windows-latest` = **Windows Server 2025 x64, ses aygıtı YOK**; `windows-11-arm` = **Windows 11 Enterprise ARM64**. Gerçek Windows 11 x64 masaüstü + gerçek ses donanımı yalnızca kullanıcının bilgisayarıdır (bölüm 7).
- Konteyner yeniden başlarsa arka plan beklemeleri kaybolur: uzun beklemeler için kendine zamanlanmış hatırlatma kur (send_later), CI'yi `gh` ile kontrol et.

---

## 2. Test ortamları

| Kısa ad | Ortam | Neyi test edebilir |
|---|---|---|
| **L** | Linux konteyner | birim, motor (sanal aygıt), ASan/UBSan, arayüz görüntüleri, dosya analizi (ffmpeg, numpy, scipy) |
| **W** | CI Windows Server 2025 x64 | MSVC derlemesi, birim, motor (MMCSS, gerçek Windows iş parçacıkları), gerçek MicVST.exe (kısayollar, tepsi, kapalı kutu, dayanıklılık), Windows'un kendi dosya özellik okuyucuları; **ses aygıtı yok** |
| **A** | CI Windows 11 ARM64 | Windows 11 üzerinde birim + motor + 60 dk (ARM64 derlemesi) |
| **S** | simülasyon | 24 sa / 168 sa sanal zaman, deterministik |
| **U** | **kullanıcının Windows 11 x64 bilgisayarı** (HyperX QuadCast 2 S 32 bit, VB-Cable 24 bit 48 kHz, kendi VST3'leri). **Bu oturum bilgisayara bağlı ve kullanıcı tam erişim izni verdi.** Program: **`C:\MicVST\MicVST.exe`** | gerçek donanım, gerçek sürücü, gerçek dinleme – testlerin çoğunu doğrudan bilgisayarda kendin yaparsın (bölüm 7.0) |

---

## 3. Kapsam: güncel sürümde GERÇEKTEN olan özellikler (yalnızca bunlar test edilir)

**Ses motoru:** mikrofon ve kablo ayrı gerçek zamanlı WASAPI iş parçacıkları; eklenti zinciri kendi MMCSS "Pro Audio" iş parçacığında, kilitsiz giriş kuyruğu; güvenlik tamponu (Otomatik 10→80 ms ya da sabit 5–80 ms) + saat farkı düzeltmesi (±%0,05 sınır, ±1000 ppm öğrenme); zincir gerçek zamanı yakalayamazsa 120 ms'den sonra dalga uyumlu çapraz geçişli atlama; zincir düzenlemede, kapanışta, cihaz/tampon değişiminde tıksız sönme; bozuk (NaN/sonsuz/>+24 dBFS) eklenti çıkışını susturma; 1,3 s'den uzun takılmada yumuşak geçiş; seçili cihazın yerinde kalması ("bağlı değil") ve takılınca ~1 s'de yeniden açılma; açılamayan cihaza 1/2/4/8/10 s aralıklı deneme; farklı mikrofon/kablo hızları; mono parça sonrası stereo eklentinin iki girişini besleme; canlı zincir gecikmesi; ses iş parçacıklarında tahsis/kilit yok; denormal koruması.

**Arayüz:** ana pencere 640×600 (seviye kartı + tepe değerleri + durum satırı, büyük Sustur düğmesi, Mikrofon/Kablo, hazır ayar satırı + "+ Eklenti", zincir listesi: sürükle-sırala, aç/kapa, gecikme, sil; alt çubuk: Ham ses, Kendini dinle, Deneme kaydı, **● Kayıt** (süre gösterir), ?, Ayarlar; VB-Cable yoksa uyarı). Ayarlar: **Ses** (Tampon, Güvenlik payı, Kulaklık + düzey, İşlem önceliği), **Kayıt**, **Kısayollar** (+ **Bildirim sesleri**), **Görünüm ve başlangıç** (Dil, Tema, Windows açılışında başlat), **Eklentiler** (VST3 klasörleri, tarama, sıfırlama), **Hakkında** (sürüm, çevrimdışı notu, Kullanım). 9 tema, 11 dil (Arapça sağdan sola), yeni simge, açık/koyu tepsi simgesi, susturunca tepside kırmızı nokta, Türkçe exe sürüm bilgisi, **geliştirici imzası yok**.

**Susturma ve kısayollar:** Sustur (15 ms sönme), Ham ses (zincir gecikmesiyle hizalı, 30 ms eşit güçlü geçiş); sistem geneli kısayollar (Sustur F9 açık, Ham ses F10 kapalı), fare kısayolları (orta, yan 4/5, Ctrl/Alt/Shift + tekerlek), aynı kısayol iki işe verilemez, kalıcı.

**Bildirim sesleri:** Sustur değişince aynı çağrıda, değişim başına bir kez, açılışta değil; yalnızca Windows'un varsayılan çıkışında (PlaySound, bellekten, eşzamansız); varsayılan çıkış kablo ise çalmaz; 16 gömülü ses (Ahşap, Kadife, Damla, Tok, Kristal, Esinti, Yankı – açılma/kapanma; Kıvılcım – açılma, açılma 2); varsayılan **Ahşap**; `%APPDATA%\MicVST\Sesler` içindeki WAV/FLAC/MP3/AIFF/OGG (≤ 10 s) kendiliğinden listelenir; açılma/kapanma ayrı seçim, ▶ dinle, ses düzeyi (varsayılan %80), Varsayılana dön, Ses klasörünü aç; tepside "Açma/kapama sesi"; config.xml `cueSounds`.

**Dahili efektler (26):** Low-Cut, High-Cut, Gövde, Çamur Temizleyici, Genizlik Azaltıcı, Netlik, Hava, Yakınlık, Tilt EQ, Parametrik EQ, Uğultu Giderici, DC Kayması Giderici, De-esser, Kompresör, Gürültü Kapısı, Sıcak Saturasyon, Telefon/Radyo/Megafon, Oda Yankısı, Gecikme/Eko, Chorus/Stereo Genişletici, Çıkış Limiteri, Sessize Alma, Ses Gecikmesi, Kanal Araçları, LUFS Ölçer, Spektrum Analizörü; **ve** Kazanç (−30…+12 dB), Otomatik Kazanç, Mono→Stereo, Stereo→Mono.

**Otomatik Kazanç tanımı (kullanıcının düzelttiği hâli – esas budur):** normal konuşma tepe ≈ **−20 dBFS**, bağırma ≈ **−15 dBFS**; kazanç **0…+6 dB** (hiç kısmaz), yavaş ve gerektiği kadar; **hiçbir tepe −10 dBFS'yi geçmez** (3 ms ileri bakışlı yumuşak sınırlayıcı, gerçek tepe); sessizlikte son kazanç korunur, **noise gate yok, ses hiç kesilmez**; zincirde yeri değiştirilebilir; kapatınca kazanç yumuşakça 0 dB'ye döner; gösterge: uygulanan kazanç (giriş tepe → çıkış tepe, dBFS); beyaz = konuşma algılanıyor, gri = kazanç korunuyor ya da kapalı.

**Hazır ayarlar:** 30 fabrika (Erkek 15 + Kadın 15: Dengeli Profesyonel, Podcast/Sohbet, Radyo Spikeri, Haber Sunucusu, Seslendirme (Belgesel / Anlatıcı), Sesli Kitap, Sinema Sesi, Film Diyaloğu, Kristal Netlik, Yumuşak ve Pürüzsüz, Derin ve Sıcak / Sıcak ve Dolgun, Şarkı/Vokal, Oyun/Canlı Yayın, Toplantı/Ders, ASMR/Fısıltı); hepsi aynı eleman sırası (geçiş yumuşak), eşit ses yüksekliği; kendi ayarlar (VST3 durumlarıyla), üzerine yaz / yeniden adlandır / sil, "Önceki zincire dön", `.micvstpreset` dışa/içe aktarma.

**Kendini dinle / Deneme kaydı:** işlenmiş ses kulaklık cihazında (ayrı iş parçacığı + köprü, gerektiğinde açılır, her açılışta kapalı), düzey tam dB; Deneme kaydı 10 s kayıt + kulaklıkta çalma (10 ms giriş/çıkış geçişi), kablo canlı kalır.

**Kayıt:** WAV 16/24 (varsayılan)/32 bit PCM + 32 bit float (BWF, 4 GB üstü RF64); FLAC 16/24 + sıkıştırma 0–8; MP3 LAME 4.0 CBR 128/160/192/224/256/320, VBR V0/V2/V4; 44,1/48 kHz (mikrofon daha hızlıysa 88,2–192); stereo/mono; klasör seçimi (varsayılan `Belgeler\MicVST Kayıtları`), tarih-saat adı, üzerine yazmama; ses iş parçacığı yalnızca 30 s kilitsiz tampona kopyalar, yazıcı normalin üstü öncelikte; WAV başlığı her saniye yenilenir; disk dolu / yazma hatasında durup mesaj; bitince geri okuyup doğrulama; ana pencere düğmesi + tepsi; ayarlar kalıcı.

**İşlem önceliği:** Normal (varsayılan) / Normalin Üstünde / Yüksek; uygulanır, Windows'tan geri okunur, kaydedilir.

**VST3:** süreç dışı tarama (çöken eklenti programı düşürmez), 120 s zaman aşımı, Atla, Atlananları yeniden dene, önbellek, çok eklentili dosyalarda kimlik korunur, durum kaydederken ses durmaz, gecikme bildirimi, aramalı seçici.

**Kapalı kutu ve günlük:** ağ kodu yok; çalışma anında ağ/dosya/DLL denetimi; günlükte `%USERPROFILE%` maskesi, 2 MB'ta kırpma; "Ses biçimi" satırı (MicVST 32 bit float + Windows aygıt biçimleri); Sustur (bildirim sesi adıyla), Ham ses, kayıt başlangıç/bitiş satırları; config.xml yalnızca değişince yazılır; 1.1.1 config'i yüklenir.

**Depodaki yardımcı araçlar (exe'nin parçası değil ama güncel depoda):** `tools/masaustu/MicVST_Oncelik.vbs` (3 öncelik kısayolu), `MicVST_Simgeleri.vbs` (kullanıcının 4 kısayoluna MicVST simgesi), `tools/firewall-block.ps1`.

### 3.1 KAPSAM DIŞI (güncel sürümde YOK – mevcutmuş gibi test etme)

- macOS sürümü (`kararlilik/macos` dalı, birleştirilmedi).
- "Kişisel 1–4" hazır ayarları (Sinema Anlatıcı, Doğal Stüdyo, Kristal Netlik, Kadife Sıcak) – ayrı dosya olarak verildi, programda hazır gelmez.
- Kullanıcının VST3 zincirinin "Tok/Yumuşak" config dosyaları – ayrı dosyalar.
- Ses denemeleri (Yumuşak/Cam/Kabarcık/Tık/Retro, tok A/B/C, belirgin 1–3) – programda yalnızca 16 seçilmiş ses var.
- Önerilmiş ama yapılmamış: bas-konuş, "susturulmuşken konuşuyorsun" uyarısı, kayıt kısayolu, son 30 sn kaydı, hazır ayar kısayolları, ekranda susturma göstergesi, tepside kayıt işareti.
- Eski hâller: Windows Media Foundation MP3 (yerine LAME 4.0), düşük öncelikli kayıt yazıcısı ve 10 s tampon (yerine normalin üstü + 30 s), 1.2.1'deki sabit HIGH öncelik (yerine seçimli, varsayılan Normal), alt çubukta tema/dil (Ayarlar'a taşındı), imza.

---

## 4. Ölçülmüş temel değerler (62ff06b, Windows CI) – gerilemeyi bunlara göre değerlendir

| Ölçüm | Temel değer | Kabul ölçütü |
|---|---|---|
| Boş zincir seviye (L/R) | −0,0000 dB | \|fark\| ≤ 0,001 dB, L = R |
| Bütünlük (giriş ↔ kablo, 1024'lük pencere seviyesi) | medyan −0,0000 dB, p1..p99 ±0,000 | 0,5 dB'den fazla sapan pencere **0** |
| Bütünlük: tık / atlama / tekrar / gecikme sıçraması | 0 / 0 / 0 / 0 | **0 açıklanamayan bulgu** (tüm `dump_*` kayıtları) |
| THD+N 997 Hz −6 dBFS (köprü dahil, 8192'lik pencere) | −110,6 dB (en kötü −101,7) | medyan ≤ −105 dB |
| Frekans yanıtı 20 Hz–20 kHz | 0,000 dB | ±0,01 dB |
| Hız sapması (flutter) | RMS 0,33 ppm, tepe 0,8 ppm | RMS ≤ 2 ppm, tepe ≤ 5 ppm |
| Gecikme 480 tampon | ölçülen 44,8 ms, gösterilen 55,4 ms | gösterilen ≥ ölçülen; fark ≤ 2 blok |
| Gecikme 128 / 256 / 1024 / 2048 | 21,3 / 29,9 / 81,1 / 149,4 ms | ±1 ms içinde aynı |
| Tampon 128–2048 (30 s, %30 yük) | tık 0, boşluk 0 | 0 / 0 |
| Zincir düzenleme | tık 0 | 0 |
| 20 dk uzun koşu | açıklanamayan tık 0, boşluk 0, THD+N en kötü −115 dB | 0 / 0; bellek eğimi ≤ +1 MB/sa |
| 330 dk long / chaos | açıklanamayan 0 | 0 |
| Gerçek exe 330 dk | özel bellek −0,03 MB/sa, tutamaç/GDI sabit | eğim ≤ +1 MB/sa; tutamaç artışı ≤ 50 |
| VST3 zinciri 330 dk × 3 öncelik, tam yük | açıklanamayan 0, çıkış 0 | 0 |
| Win11 ARM64 60 dk | tık 0, boşluk 0, THD+N −112 dB | 0 / 0 |
| 168 sa simülasyon | MicVST'ye bağlı tık 0 | 0 |
| Kayıt motor testi | kabloda tık 0 (kayıtla/kayıtsız), seviye −26,00 / −26,00 dB, gecikme 55,4 / 55,4 ms, dosya THD+N −149,3 dB | aynısı |
| Kayıt CPU (bir çekirdek, W) | WAV 24 %0,06, FLAC 5 %1,0, MP3 320 %3–3,7 | ≤ %5 |
| Bildirim sesi: susturma → Windows'a | ≤ 0,046 ms | ≤ 5 ms; değişim başına tam 1 |
| VST3 tarayıcı | 240/240 | 240/240 |

---

## 5. Test planı (A'dan Z'ye) – her madde ölçülebilir

Biçim: **Kimlik – Ne – Nasıl (ortam, araç) – Geçme ölçütü.** Var olan test yoksa yaz (birim, motor modu ya da `tools/ci-*.ps1`), CI'ya bağla, sonucu sonuç dalından oku.

### A. Derleme ve exe bütünlüğü
- **A1** MSVC derlemesi hatasız (W); `metrics.json`: `build_ok: true`, `errors: 0`, `warnings_micvst` ≤ 8 (temel), `warnings_juce: 0`, `juce_patch_markers: 3`. **A2** Exe SHA-256 metrics ile aynı; boyut ~10 MB. **A3** İçe aktarılan DLL listesi (`imports.txt`) ağ kütüphanesi içermez. **A4** Sürüm bilgisi: 1.2.2, dil Türkçe (Türkiye). **A5** Exe tek başına çalışır (VC++ Redistributable gerekmez): temiz klasörden başlat (W runtime-check). **A6** ARM64 derlemesi (A) hatasız.

### B. Birim testleri
- **B1** `MicVSTTests` W, A ve L'de: "All tests completed successfully", 234 bölüm, 0 hata. **B2** ASan/UBSan (L): bellek hatası 0, sızıntı 0; UBSan yalnızca bilinen üçüncü taraf bit kaydırma uyarıları (JUCE libFLAC `juce_FlacAudioFormat.cpp`, LAME `VbrTag.c`, `quantize.c`, `bitstream.c`) – başka bir bulgu BAŞARISIZ.

### C. Ses kalitesi ve bütünlük (en önemli bölüm)
- **C1** Boş zincirde bit düzeyi: Kazanç 0 dB elemanı **bit-exact** geçirir (birim). **C2** `quality` (W, A): bölüm 4 ölçütleri. **C3** `flutter`, `mono`, `monochain`: mono mikrofon → iki kanal eşit (L−R = 0), mono eklenti sonrası stereo eklentinin iki girişi beslenir (R ≠ −200 dB). **C4** Bütünlük analizleri (`dump_*`, `engine/integrity.txt`): gürültü −12/−60 dBFS, çok ton −3/−40 dBFS, sinüs −0,1 dBFS, ses (konuşma), müzik, sessizlik, mono gürültü, tampon 128/2048, 44,1→48 ve 48→44,1, test gecikmesi, kablo takılması, eklenti takılması, gerçek VST3 ve devre dışı VST3, konuşma −40/−20 dBFS, ve süit (doğru/yanlış referans – yanlış referans **bulgu üretmeli**, negatif kontrol): her birinde **0 açıklanamayan bulgu**, sessizlik → çıkış tam 0, örnek ≥ 1,0 sayısı 0, NaN 0. **C5** Farklı hızlar: mikrofon 44,1 / kablo 48 ve tersi – seviye ±0,01 dB, tık 0. **C6** Tıksız kenarlar: program kapanışı, cihaz değişimi, tampon değişimi, güvenlik payı değişimi, Sustur, Ham ses, zincir düzenleme (ekle/sil/taşı/devre dışı), hazır ayar geçişi, efekt parametresi uç değer sıçramaları – hepsinde kablo kaydında ikinci fark dedektörü (bölüm 6.1) ile **0 tık**. **C7** Ses iş parçacığında tahsis/kilit (`rtprobe`, L): 0.

### D. Saat farkı, tampon, gecikme
- **D1** `latency`: ölçülen ↔ gösterilen (bölüm 4). **D2** `buffers` 128/256/480/512/960/1024/2048: tık 0, boşluk 0. **D3** Saat farkı ±60/±300/±1000 ppm (stres + simülasyon): öğrenilen değer gerçeğe ≤ 2 ppm, hız değişimi ≤ ±%0,05, tık 0. **D4** Güvenlik payı Otomatik ve sabit 5/10/20/40/80 ms: değişim anında etkili, tık 0. **D5** Zincir gerçek zamandan yavaş (ortalama %112) – atlama çapraz geçişli, gecikme sınırlı (≤ ~150 ms), tık 0.

### E. Cihazlar ve hatalar
- **E1** `devices`, `hotplug` (çıkar/tak), `openfail` (açılamayan cihaz: 1/2/4/8/10 s deneme, günlük taşmaz, ≤ 10 s'de açılır), `faults` (bozuk eklenti çıkışı susturulur, yavaş/yüklenemeyen eklenti), `msgblock` (arayüz 3 s kilitli): tık 0; seçili cihaz yerinde kalır; config.xml gereksiz yazılmaz.

### F. Dahili efektler (26 + 4)
- **F1** Her efekt: tasarım hedefine göre ölçülmüş frekans yanıtı / dinamik eğri / gecikme (birim "Effects / …" bölümleri). **F2** Her parametrenin min↔maks sıçraması ve her seçim değişimi: tık yok. **F3** Bozuk giriş (NaN, +24 dBFS): çıkış sonlu. **F4** Mono/stereo, kaydet/geri yükle, gecikme bildirimi doğru. **F5** `effects` motor modu: 26'sı birden zincirde, canlı rastgele parametre (4/s), geri yükleme: tık 0. **F6** LUFS ölçer EBU Tech 3341/3342 vakaları. **F7** Gürültü kapısı 3 ms ileri bakış: kelime başı kesilmez (ilk 5 ms enerjisi korunur).

### G. Otomatik Kazanç (tanım bölüm 3)
- **G1** 16 ses × 160 s: normal konuşma tepe −20 ± 1,5 dBFS (ulaşılabildiğinde), bağırma ≈ −15 dBFS. **G2** Kazanç her zaman 0…+6 dB (sayaç: sınır dışı 0). **G3** −10 dBFS üstü örnek **0**, gerçek tepe < −9,7 dBFS (ani bağırmanın ilk 0,5 s'si dahil). **G4** Sessizlikte kazanç değişmez (fark < 0,01 dB), çıkış kesilmez (noise gate yok). **G5** Kapatınca 0 dB'ye 12 dB/s ile döner, sonra 3 ms gecikmeyle bit-exact. **G6** Zincirde her konumda çalışır, yalnızca bir tane eklenebilir. **G7** 24 sa dayanıklılık (S): −10 üstü 0, geçersiz örnek 0. **G8** Gösterge: beyaz/gri durumları ve "giriş → çıkış" değerleri doğru (birim/arayüz).

### H. Hazır ayarlar
- **H1** 30 fabrika ayarı yüklenir; hepsi aynı eleman sırası; ses yüksekliği farkı ≤ 0,1 LU (sentetik konuşma). **H2** Herhangi ikisi arasında geçiş: tık 0 (motor). **H3** Kendi ayar: kaydet/üzerine yaz/yeniden adlandır/sil; VST3 durumu dahil geri gelir. **H4** `.micvstpreset` dışa → içe aktar: zincir birebir. **H5** "Önceki zincire dön" son seçimi geri alır.

### I. Susturma, Ham ses, kısayollar, bildirim sesleri
- **I1** Sustur: kabloda 15 ms sönme (ölçülen süre 12–18 ms), tık 0; mikrofon ölçeri sesi göstermeye devam eder. **I2** Ham ses: zincir gecikmesiyle hizalı (kayma ≤ 1 örnek), 30 ms eşit güçlü geçiş (seviye çukuru ≤ 0,5 dB), tık 0. **I3** Gerçek exe (W, `ci-hotkeys.ps1`): F9, orta tuş, yan tuşlar, Ctrl+tekerlek, bağlanmamış tuş hiçbir şey yapmaz, düz tekerlek hiçbir şey yapmaz, yeniden başlatınca korunur, kapatılınca çalışmaz – 0 FAIL. **I4** Aynı kısayol iki işe verilemez. **I5** Bildirim sesleri (`cues` motor modu + "Cue sounds" birim + gerçek exe günlüğü): 40 değişimde 40 ses, tekrar eden çağrıda 0, açılışta 0, susturma → Windows ≤ 5 ms, kapanma/açılma sırası doğru, kapalıyken 0, varsayılan çıkış = kablo iken 0 ve günlükte bir kez uyarı, 16 gömülü sesin hepsi yüklenir (48 kHz stereo, baş/son < 1e-4), klasör: yeni dosya yeniden başlatmadan görünür, bozuk/10 s'den uzun atlanır ve sayılır, silinen seçili dosya → varsayılan çalar, ses düzeyi %50 = −6,02 dB (±0,1), %0 = sessiz, ayarlar config.xml'e yazılıp geri okunur, varsayılan hiçbir şey yazmaz. **I6** Tepsi menüsü: Sustur, Açma/kapama sesi, Kaydı başlat/durdur, Hazır ayar, Windows açılışında başlat, Kapat – hepsi çalışır (W'de UI otomasyonu ya da U'da elle).

### J. Kendini dinle ve Deneme kaydı
- **J1** Kulaklık düzeyi 0/−12/−20/−40 dB → ölçülen aynı (±0,05 dB). **J2** Kapalıyken kulaklıkta 0 örnek; kapatınca cihaz kapanır. **J3** Deneme kaydı: kayıt → çalma → bitiş; çalma sırasında kablo seviyesi değişmez (±0,05 dB); her saniyenin tepesi eşit (±0,05 dB); en büyük örnek sıçraması ≤ düzgün sinüsün 3 katı (başta/sonda tık yok).

### K. Kayıt (KALİTE KRİTİK)
- **K1 Biçim doğruluğu** (birim + `MICVST_RECORDER_KEEP` + `ci-recordings.ps1`): her biçim/bit/hız/kanal için başlık (biçim etiketi, bit, hız, kanal, uzunluk) beyan edilenle **birebir**; ffmpeg çözme hatası 0; Windows özellik okuyucusu süre ve bit hızını gösterir.
- **K2 WAV 24 bit PCM:** her örnek kaynak float değerinden en çok **0,500 LSB** sapar (doğru yuvarlama), hata ortalaması |ort| < 0,01 LSB (kesme yanlılığı yok), taşma tam ölçekte kırpılır (işaret dönmesi 0). **K3 WAV 32 bit float:** kaynakla **bit-exact**. **K4 WAV 32 bit PCM:** ≤ 0,5 LSB. **K5 WAV/FLAC 16 bit:** TPDF dither: hata rms 0,50 ± 0,05 LSB, ortalama |ort| < 0,02 LSB, en büyük ≤ 1,5 LSB. **K6 FLAC 16/24 (sıkıştırma 0/5/8):** çözülünce aynı bit derinliğindeki WAV ile bit-exact; STREAMINFO MD5 doğrulanır (`flac -t` ya da ffmpeg). **K7 MP3:** 11 ayar (CBR 128…320, VBR V0/V2/V4, stereo/mono, 44,1/48): LAME etiketindeki örnek sayısı = kaydedilen (gapless), ffmpeg çözünce **tam uzunluk**, CBR'de her çerçeve beyan edilen kbps, kanal sırası doğru. **K8 Hız dönüştürme:** 48→44,1, 96→48, 96→88,2, 44,1→48 (ve birim testindeki diğer çiftler): sinyal/hata **> 130 dB**; 100 Hz / 1 kHz / 10 kHz / 19,9 kHz sinüslerinde ideal sonuca göre hata **< −135 dB** (geçirme bandı düz, faz/zaman kayması 0), alt hızın yarısının üstündeki ton (durdurma bandı) **< −135 dB**; uzunluk birebir, zaman kayması 0. Mikrofon 48 kHz iken 96 seçili → 48 kHz yazılır (yükseltme yok). **K9 Mono:** (L+R)/2; mono mikrofonda birebir. **K10 BWF:** bext originator "MicVST", tarih/saat. **K11 RF64:** > 4 GB (192 kHz float, disk hızında): başlık "RF64", geri okunur, ffmpeg tam süreyi verir, kayıp 0. **K12 Çökme:** kayıt sürerken anlık kopya (ya da gerçek exe'yi öldürme, U): dosya son saniyeye kadar geçerli, içerik ≤ 0,5 LSB. **K13 Taşma:** 30 s'den uzun disk takılması: kayıp sayılır, yazılan + kayıp = gönderilen, dosya tutarlı. **K14 Hatalar:** ses yokken ("Ses çalışmıyor…"), klasör oluşturulamazken, ikinci başlatmada ("Kayıt zaten sürüyor"), yazılamayan klasör seçiminde, **diskte 100 MB'tan az boş yer varken** (kayıt hiç başlamaz, mesaj) anlaşılır Türkçe mesaj. **Yeni test (yaz):** küçük sanal disk (W: `diskpart create vdisk` ~250 MB, NTFS – 100 MB sınırının üstünde başlasın) → doldurana kadar WAV 32 float 192 kHz kaydet: kayıt kendiliğinden durur, mesaj gelir, dosya geçerli ve son yazılan saniyeye kadar okunur (≤ 0,5 LSB); kayıt sırasında diski ayır: hata mesajı, program çökmez, kabloda tık 0. **K15 Adlandırma:** `MicVST_YYYY-AA-GG_SS-DD-ss`, aynı saniyede `_2`, `_3`; asla üzerine yazmaz. **K16 Uzun kayıt:** 20 dk (10× hızla) WAV 24 / FLAC 24 / MP3 V0: kayıp 0, geri okuma birebir. **K17 Kabloya etki** (`record` modu): kayıt açık/kapalı kabloda tık 0, seviye aynı (±0,01 dB), THD+N kayıtsız dalgalanmanın 90. yüzdeliği + 1 dB'den kötü değil, gecikme aynı (±1 ms), taşma 0; dosya içeriği seviye −26,000 dB, THD+N < −120 dB, L=R. **K18 Başlat/durdur:** 30 kez her biçimde, ses çalarken: hepsi doğrulanır, kabloda tık 0, bellek sızıntısı yok (30 kayıtta < 30 MB). **K19 Hız değişimi:** kayıt sırasında mikrofon hızı değişirse kayıt o noktada biter ve mesaj gelir. **K20 Kalıcılık:** biçim/klasör yeniden başlatınca korunur. **K21 CPU:** bölüm 4. **K22 Tam birleşim taraması (yeni, yaz):** arayüzün sunduğu **her** birleşim – WAV 16/24/32/32f × FLAC 16/24 × sıkıştırma 0–8 × MP3 9 ayar × izinli her hız (44,1/48 ve mikrofon hızına göre 88,2/96/176,4/192) × stereo/mono – ve her biri için kaynak (işlem) hızı 44,1 / 48 / 96 / 192 kHz; her dosya 10 s: K1–K9 ölçütleri (başlık, LSB hatası, dither, FLAC MD5/bit-exact, MP3 uzunluk, hız dönüştürme SNR). Arayüzün **sunmadığı** birleşim (ör. MP3 96 kHz) seçilemiyor ve config'e elle yazılsa bile güvenli bir değere düşüyor.

### L. İşlem önceliği
- **L1** `priority` (W): her sınıf uygulanır ve Windows'tan aynı okunur; tam CPU yükünde ses temiz (tık 0); diğer programlara etkisi raporlanır. **L2** Gerçek exe (runtime-check): eski config → Normal; 0/1/2 → Normal/AboveNormal/High; yeniden açılışta korunur. **L3** Masaüstü `MicVST_Oncelik.vbs` 3 kısayolu (W): hedef, argüman, simge doğru; bildirim penceresi çıkmaz.

### M. VST3
- **M1** `vst3`, `vst3suite` (ücretsiz eklentiler: RNNoise, ZL Equalizer 2, ZL Compressor, Fire, TDR VOS SlickEQ, Airwindows Consolidated, DC1A3): tek tek ve birlikte, çalarken ekle/kaldır/taşı/devre dışı, kaydet/geri yükle (durum blobu aynı), gecikme bildirimi = ölçülen; tık 0. **M2** `[vst3tani]`: gerçek tarayıcı 6 eklenti × 20 × (boşta, tam yük) = 240/240. **M3** Çöken / askıda kalan / bozuk çıkış veren eklenti programı düşürmez (tarama süreç dışı). **M4** Çok eklentili dosya (WaveShell türü) kimliği yeniden başlatınca korunur. **M5** `[vst3uzun]` 330 dk × 3 öncelik: açıklanamayan 0, bellek eğimi ≈ 0.

### N. Arayüz, dil, tema
- **N1** `MicVSTSnapshot`: 11 dil × 9 tema; hiçbir metin kesilmez/taşmaz (görüntüleri incele), Arapça sağdan sola; Ayarlar bölüm görünümü yeniden boyutlandırma ve yeniden kurulumda korunur (0 hata). **N2** Her dilde 582 metin eksiksiz (`tools/i18n.py check`). **N3** Kayıt paneli (WAV/FLAC/MP3), Bildirim sesleri paneli (açık/kapalı) her dilde düzgün. **N4** İmza hiçbir yerde yok (exe içinde "Relios" dizesi 0). **N5** Windows 11'de DPI %100/125/150/175, açık/koyu mod, başlık çubuğu rengi (U).

### O. Kapalı kutu, gizlilik, kalıcılık
- **O1** `check-no-network.ps1` (W): ağ kütüphanesi yok. **O2** `ci-runtime-check.ps1` (W): 6 senaryoda (ilk açılış, normal, bozuk config, bozuk önbellek, eksik cihaz/eklenti/klasör, tepside başlangıç) ağ bağlantısı/bind 0 (WFP denetimi + kontrol süreci curl ile kanıtlı), yazılan dosyalar yalnızca beklenen yerler, Windows kapanış isteğinde temiz çıkış (kod 0). **O3** Günlük kullanıcı adını göstermez (`%USERPROFILE%`), 2 MB'ta kırpılır. **O4** config.xml yalnızca değişince yazılır; 1.1.1 config'i yüklenir; bozuk config/önbellek kurtarılır. **O5** `MicVST_Simgeleri.vbs` (W): 4 kısayolun simgesi MicVST.exe, hedefleri değişmez, başka kısayola dokunmaz.

### P. Uzun süre ve dayanıklılık (`[uzun]`)
- **P1** long 330 dk, chaos 330 dk, app (gerçek exe) 330 dk, vst3long p0/p1/p2 330 dk, simweek 168 sa + Otomatik Kazanç 24 sa, win11arm 60 dk: bölüm 4 ölçütleri. **P2** Kayıt dayanıklılığı (yeni, önerilen): gerçek zamanlı 60 dk WAV 24 kayıt motor testi içinde (`record` moduna süre parametresi ekleyebilirsin) – kayıp 0, süre hatası < %0,01, bellek sabit.

### Q. Gerileme listesi (daha önce düzeltilmiş hatalar – her biri yeniden doğrulanır)
Sonsuz yeniden bağlanma döngüsü (açılamayan cihaz) · config.xml'in boş yere yazılması · log.txt'nin sınırsız büyümesi · bozuk eklenti çıkışının kabloya ulaşması · takılan eklentiden sonra tık · zincir düzenlemede tık (1.2.0: 13'te 11) · gecikmeli zincirde fade-in tıkı · kapanış/cihaz değişiminde tık · çok eklentili VST3 kimlik kaybı · günlükte çift UTF-8 · İngilizce cihaz hataları · mono zincirin yalnızca sol kanala gitmesi · mono→stereo eklentide sağ kanalın sessiz kalması · canlı gecikmenin eksik gösterilmesi · kulaklık düzeyinin yanlış eğri izlemesi (−12 dB → −16,6) · Deneme kaydı başı/sonunda tık · JUCE tamsayı yazıcısının kesmesi (kayıtta 1 LSB / −0,5 LSB yanlılık) · Windows'ta kayıt yazıcısının yavaş kalması · test aracının eklentiyi süreç içinde tarayıp çökmesi (gerçek tarayıcı 240/240).

### R. Uzun ömür – yıllarca kullanım (zorunlu)
- **R1 Zaman sayaçlarının taşması (kod denetimi + test).** Windows'ta `juce::Time::getMillisecondCounter()` 32 bittir ve bilgisayar açık kaldıkça **49,7 günde bir başa döner**. Windows "Hızlı başlatma" açıkken kapat-aç çalışma süresini sıfırlamaz, yani bu kullanıcıda gerçekten olur. Kullanıldığı yerler (en az): `src/audio/AudioEngine.cpp` (cihaz yeniden açma beklemesi `(juce::int32) (nowMs - b.nextMs) >= 0`, denetim yeniden başlatma beklemesi `nowMs - lastRestartMs > backoffMs`, Deneme kaydı çalma sonu `playbackEndMs`), `src/audio/CueSounds.cpp` (`outputIsCable`, 2 s önbellek, `defaultCheckedAt == 0` işareti), `src/audio/Recorder.cpp` (yazıcı döngüsü: 1 s başlık yenileme, 5 s boş yer denetimi), `src/ui/PluginListView.cpp`, `src/audio/ScanCoordinator.cpp` (120 s zaman aşımı). İlk bakışta hepsi işaretsiz fark kullanıyor ve taşmaya dayanıklı görünüyor – **bunu kanıtla:** her ifadeyi tek tek incele ve sayacın `0xFFFFFF00`'den başlayıp başa döndüğü bir birim testi yaz (gerekirse saati en küçük değişiklikle enjekte edilebilir yap). Ayrıca tüm `src/` içinde 32 bitlik sayaçları tara: örnek/çerçeve sayan `int` 48 kHz'de **12,4 saatte**, 192 kHz'de **3,1 saatte** taşar. Sıra sayaçları (`AudioBridge::pushSeq`, `InputQueue::stampSeq`, `uint32_t`) taşınca karşılaştırmaların doğru kaldığını göster. Uzun süre biriken `float` zaman/konum/faz değerlerinde hassasiyet kaybını kontrol et (30 gün × 48 000 = 1,2·10¹¹ örnek: double'da tam, float'ta değil). Ölçüt: hiçbir taşma davranışı değiştirmez (testle kanıtlı) ya da en küçük değişiklikle düzeltilir.
- **R2 Çok uzun simülasyon (S):** `MicVSTStress --day 720` (30 sanal gün; 168 sa CI'da 1786 s sürdü, 720 sa ≈ 2 sa – `ci-run-timed.ps1` zaman aşımını buna göre ayarla) ve Otomatik Kazanç 7 gün. Ölçüt: MicVST'ye bağlı tık 0, saat farkı öğrenmesi kararlı (sapma ≤ 2 ppm), bellek ve sayaçlar sınırlı.
- **R3 Gerçek zamanlı çok saatlik kayıt (W, yeni `[uzun]` işi):** 330 dk gerçek zamanlı WAV 24 / 48 kHz / stereo kayıt. Bu kayıt ≈ 4 sa 8 dk'da **4 GB sınırını gerçek zamanda aşar ve RF64'e geçer.** Ölçüt: kayıp 0; başlık RF64; ffmpeg ve Python `soundfile` tam süreyi okur; içerik ≤ 0,5 LSB; dosya süresi ile örnek sayısı birebir; yazıcının belleği sabit; kabloda tık 0. FLAC 24 ve MP3 V0 ile 330 dk daha: uzunluk ve içerik doğruluğu.
- **R4 Çok günlük gerçek kullanım (U):** bilgisayar açık, MicVST kesintisiz çalışır. Bu teslimde süre: 22 Ekim gecesinden 23 Ekim ~18:00'e kadar (≈ 18–20 sa). 72 sa ve üstü ATLANDI (süre); kullanıcı isterse teslimden sonra sürdürür (0.1). gerçek mikrofon ve kablo, arada Discord ve normal kullanım olur. Her saat şunları toplayan bir PowerShell betiği yaz; kullanıcı Görev Zamanlayıcı ile çalıştırır:
  - MicVST'nin özel belleği, tutamaç / GDI / USER nesne sayısı ve iş parçacığı sayısı;
  - işlemci süresi;
  - log.txt boyutu ve yeni satırları.

  Ölçütler:
  - bellek artışı ≤ 10 MB/gün;
  - tutamaç, GDI ve iş parçacığı sayıları sabit (±%5);
  - log'daki her olayın açıklaması var (cihaz olayı vb.);
  - durum satırındaki "kesinti" sayısının her artışı açıklanmış;
  - program yanıt veriyor;
  - sonda 30 s WAV 24 kaydı K2 ölçütlerini ve 6.3'teki analizi geçiyor.
- **R5 Sistem saatinin değişmesi (W, U):** MicVST çalışırken ve kayıt sürerken saati 1 saat ileri ve geri, 1 gün ileri ve geri al; ayrıca saat dilimini değiştir (`Set-Date`, `tzutil /s`). Ölçüt: ses kesilmez ve tık 0; kayıt süresi örnek sayısıyla tutarlı (duvar saatine göre değil); dosya adı çakışmaz, üzerine yazılmaz; zamanlayıcılar (yeniden deneme, 2 s önbellek, başlık yenileme) takılmaz.
- **R6 Bin kez aç-kapa, ayar dayanıklılığı (W, gerçek exe):** exe'yi 1000 kez başlatıp kapat; bunların 200'ünde ayar yazılırken `taskkill /f` ile öldür.
  - Her açılışta config.xml geçerli olmalı: eski ya da yeni hâli olur, asla yarım kalmaz. Son değişiklik dışında hiçbir ayar kaybolmaz.
  - plugin_cache.xml geçerli kalır, log 2 MB'ı aşmaz, `%APPDATA%\MicVST` boyutu sınırlı kalır.
  - Ayrıca 10 000 Sustur değişimi, her biri doğrulanan 1 000 kayıt başlat/durdur ve 1 000 hazır ayar geçişi yap. Ölçüt: bellek ve tutamaçlar sabit, kabloda tık 0.
- **R7 Ortam değişiklikleri (U, kullanıcının onayıyla, önce yedek):** eklentiyi güncelle, kaldır ya da klasörünü taşı; VB-Cable'ı yeniden kur; QuadCast'i başka bir USB yuvasına tak; Windows'un varsayılan cihazlarını değiştir. Bunların olacağı kesin; o anki davranışı ölç ve yaz. Ölçüt: program çökmez; zincirin kalanı ses vermeye devam eder; cihaz aynı adla geri gelince kendiliğinden bağlanır. Eklenti geri gelince ayarlarının dönüp dönmediğini ölç ve yaz. **Yeni özellik ekleme; gerekiyorsa kullanıcıya öner.**
- **R8 Kayıtların yıllar sonra da açılabilmesi:** her biçimi en az iki bağımsız okuyucuyla aç: ffmpeg, Python `soundfile` (libsndfile), FLAC için `flac -t`, MP3 için ffmpeg dışında ikinci bir çözücü (`mpg123` ya da başka bir bağımsız çözücü). Hepsinde örnek sayısı ve içerik aynı olmalı. Standartlara uygunluk denetlenir:

  | Biçim | Standart |
  |---|---|
  | WAV/BWF | EBU Tech 3285 (bext) |
  | RF64 | EBU Tech 3306 (ds64) |
  | FLAC | RFC 9639 (STREAMINFO, MD5) |
  | MP3 | ISO/IEC 11172-3 (çerçeve başlıkları), Xing/LAME etiketi |
- **R9 Yeniden üretilebilirlik ve arşiv:** JUCE ilk derlemede GitHub'dan `GIT_TAG 9.0.4` ile iner ve yıllar sonra erişilemeyebilir. Bir **arşiv paketi** hazırla:
  - depo kaynağı (test edilen commit) ve JUCE 9.0.4 kaynağı. **Dikkat:** `FETCHCONTENT_SOURCE_DIR_JUCE` verildiğinde CMake `PATCH_COMMAND`'ı **çalıştırmaz**; MicVST'nin 5 gerçek zamanlı JUCE yaması uygulanmadan derlenir. Bu yüzden arşivdeki JUCE'nin içinde bir kez `cmake -P <depo>/cmake/PatchJuce.cmake` çalıştır (betik tekrarlanabilir; her parça bir kez eşleşmezse hata verir). Arşivden derlenmiş exe'de yamaların varlığını kanıtla: `metrics.json` içinde `juce_patch_markers: 3` ve motor testleri temel değerlerde. Arşive bir `OKU.md` koy: hangi komutlarla, hangi sırayla derleneceği;
  - exe ve SHA-256, test raporu, uzun ömür kılavuzu;
  - kullanıcının config yedeği;
  - test edilen Windows, VB-Cable ve eklenti sürümlerinin listesi.

  Sonra **ağsız derleme kanıtı:** CI'da JUCE'yi arşivden derle (`-DFETCHCONTENT_SOURCE_DIR_JUCE=<klasör>` ve `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`); birim testleri geçmeli. Exe'nin bayt bayt aynı olması beklenmez (MSVC zaman damgası), davranışın aynı olması beklenir. Paketi kullanıcıya zip olarak gönder; GitHub Release yayınlama yok.
- **R10 Güvenlik yazılımı:** exe imzasız. Windows Defender ile tara (W: `Start-MpScan -ScanType CustomScan -ScanPath`); sonucu ve SmartScreen davranışını yaz. Kullanıcıya gereksiz Defender istisnası ekletme.
- **R11 Doğrulanan Windows sürümleri:** U'daki Windows 11 sürüm ve yapı numarasını (`winver`, `[Environment]::OSVersion`) rapora yaz. W (Server 2025) ve A (Windows 11 ARM64) sonuçlarıyla birlikte "hangi Windows'ta, hangi sürücülerle doğrulandı" tablosu hazırla.

---

## 6. Ölçüm yöntemleri (aynı yöntemi kullan, sonuçlar karşılaştırılabilsin)

### 6.1 Tık / süreksizlik dedektörü
Kaynak sinyal biliniyorsa: kaynakla hizala (çapraz korelasyon), artığı 256 örneklik parçalarda incele; artık > parça RMS'sinin dedektör tabanına göre eşiği (ölçülen p99'dan türetilen, tipik %2–2,4) ve > −60 dB (sessizlikte > −50 dB) ise bulgu. Kaynak tek tonsa: ikinci fark `|x[n+1]−2x[n]+x[n−1]| > 3·A·(2πf/fs)² + 2·10⁻⁴` → tık; 480 örnek içindekiler tek olay.

### 6.2 THD+N
8192 örneklik pencerelerde 3 parametreli sinüs uydurma (frekans köprünün ppm düzeltmesine göre ±%0,06 aralıkta aranır), artık/sinyal dB; medyan ve en kötü pencere raporlanır.

### 6.3 Kayıt dosyası analizi (numpy/scipy/ffmpeg ile)
- Başlık: RIFF/RF64, `fmt` (etiket 1 = PCM, 3 = float, 0xFFFE = EXTENSIBLE), bit, hız, kanal, `data` uzunluğu, `bext`.
- **24 bit gerçekten 24 bit mi:** konuşma sırasında örneklerin alt 8 bitinin sıfır olmama oranı > %90 (16 bitten büyütülmüş dosyada ≈ %0). 32 float'ta değerlerin 2⁻²³ ızgarasına düşmeme oranı.
- Kırpma: |x| ≥ 0,9999 örnek sayısı; gerçek tepe (4× aşırı örnekleme); LUFS (ITU-R BS.1770); DC (ortalama); gürültü tabanı (sessiz bölüm RMS); boşluk (≥ 24 ardışık tam sıfır, konuşma içinde); tık (6.1); süre = örnek sayısı / hız ile duvar saati farkı.
- İki kaydı karşılaştırma (MicVST kaydı ↔ başka programın CABLE Output kaydı): hizala, seviye farkı, korelasyon, artık spektrumu.

---

## 7. Kullanıcının Windows 11 x64 bilgisayarında testler (U) – gerçek donanım

### 7.0 Bilgisayar erişimi (kullanıcı tam erişim izni verdi)

- Oturum, kullanıcının bilgisayarına Claude masaüstü uygulaması üzerinden bağlıdır. Araçlar uzak cihaz araçlarıdır (`mcp__remote-devices__*`): bilgisayarda komut çalıştırma, dosya araçları ve ekran kontrolü. İlk iş, bu araçların çalıştığını basit bir komutla doğrula (ör. `Get-ComputerInfo | Select OsName, OsVersion, OsBuildNumber`). Araç yoksa ya da bilgisayara ulaşılamıyorsa kullanıcıya söyle: bilgisayar uyanık ve çevrimiçi olmalı, masaüstü uygulaması açık olmalı ve sohbet "bu bilgisayara bağla" ile bağlanmış olmalı. O sırada konteynerde yapılabilecek işlere devam et.
- **Program yeri: `C:\MicVST\`** (kullanıcı son exe'yi buraya koyar). İlk iş:
  1. `Get-FileHash C:\MicVST\MicVST.exe -Algorithm SHA256` sonucu bölüm 1'deki SHA ile aynı mı?
  2. `%APPDATA%\MicVST` klasörünün tamamını `C:\MicVST\yedek\<tarih-saat>\` altına kopyala.
  3. Bilgisayarın envanterini rapora yaz: Windows sürümü ve yapısı; ses aygıtları ve biçimleri; VB-Cable sürümü; QuadCast sürücüsü/donanım yazılımı; VST3 klasörleri ve eklenti sürümleri; CPU, RAM, güç planı; Hızlı başlatma açık mı (R1 için önemli); çalışma süresi (`(Get-CimInstance Win32_OperatingSystem).LastBootUpTime`).
- **Kapsam:** kullanıcı bilgisayarda her şeye göz atabileceğini söyledi. Bu izni testin gerektirdiği her şey için kullan: MicVST klasörleri, log ve config, VST3 klasörleri, ses ayarları, Olay Görüntüleyicisi (Uygulama/Sistem: MicVST, ses ve USB olayları), Audacity/OBS/Discord ayarları, Güvenilirlik İzleyicisi (çökme kayıtları). Testle ilgisi olmayan kişisel dosyaları açma.
- **Kendin yapabileceklerin:** exe'yi başlatıp kapatmak; ayarları config.xml üzerinden ya da arayüzden değiştirmek; ekranı izlemek ve tıklamak; MicVST ile kayıt almak; CABLE Output'u aynı anda kaydetmek; dosyaları konteynere alıp 6.3 analizini yapmak; PowerShell ile bellek, tutamaç ve CPU izlemek (R4); Görev Zamanlayıcı görevi kurmak; saat ve saat dilimi değişikliği (R5, sonra geri al); Defender taraması.
  - CABLE Output kaydı için bilgisayarda ffmpeg yoksa önce kullanıcıya sor, sonra `winget install Gyan.FFmpeg` ile kur. Kayıt komutu: `ffmpeg -f dshow -i audio="CABLE Output (VB-Audio Virtual Cable)" -c:a pcm_s24le …`. Aygıt adını önce `ffmpeg -list_devices true -f dshow -i dummy` ile al.
  - Python gerekiyorsa aynı şekilde sorup kur ya da analizi konteynerde yap.
- **Kullanıcının fiziksel olarak yapması gerekenler** (her seferinde kısa ve net iste, ne zaman başlayacağını söyle):
  - mikrofona konuşmak ya da bağırmak;
  - kulaklıktan dinleyip onaylamak (bildirim sesleri, hazır ayarlar, Deneme kaydı);
  - USB'yi çıkarıp takmak;
  - uyku/uyanma (uyku sırasında bağlantı kopar; uyanınca kontrol et);
  - Discord'da karşı taraftan geri bildirim almak.
- **Sınırlar (tam erişime rağmen):**
  - **FabFilter ve diğer ticari eklentilerin ayarlarına dokunma.** Kullanıcının kendi zincirini ve hazır ayarlarını değiştirme; test için ayrı bir hazır ayar oluştur, sonra sil.
  - VB-Cable, ses sürücüsü ya da Windows ayarı (varsayılan aygıt, biçim, güç planı, Hızlı başlatma) değiştireceksen önce eski değeri kaydet, sonra geri al.
  - Kayıt klasöründeki kullanıcı kayıtlarına dokunma; test kayıtlarını `C:\MicVST\test\` altına yap.
  - Silme işlemi bilgisayarda varsayılan olarak engellidir; gerekirse izin iste ya da `_to_delete\` klasörüne taşı.
  - Geri alınamaz her işlemden (sürücü kaldırma/kurma, eklenti kaldırma – R7) önce kullanıcıya sor.
  - Çok günlük testlerde (R4) bilgisayarın uykuya geçmemesi gerekir. Güç ayarını değiştirmeden önce sor ve test bitince geri al.
- **Yeni exe kurulumu:** bir düzeltmeden sonra yeni exe derlenirse:
  1. Önce eskisini `C:\MicVST\eski\MicVST_<sha8>.exe` olarak sakla.
  2. Programı kapat, yeni exe'yi kopyala, SHA'yı doğrula, yeniden başlat.
  3. Hangi exe'nin hangi testi geçtiğini rapora yaz.

### 7.1 Testler

Önce **0.7'deki yedek kuralı.** Bir test bilgisayarda yapılamıyorsa kullanıcıya **adım adım, kısa** yaptır; dosyaları (log.txt, kayıtlar, config.xml) iste ve 6.3 ile analiz et.

- **U1 Kurulum:** exe `C:\MicVST\MicVST.exe`; SHA-256'yı `Get-FileHash` ile doğrula. İlk açılışta SmartScreen uyarısı (imzasız exe) çıkarsa not et. Windows açılışında başlatma açıksa kayıt defterindeki yol `C:\MicVST\MicVST.exe` olmalı (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).
- **U2 Ses biçimi:** log.txt'deki satır: `Ses biçimi: MicVST akışı ve iç işlem 32 bit kayan nokta; Windows aygıt biçimi: giriş … | çıkış …` → giriş QuadCast **32 bit**, çıkış VB-Cable **24 bit PCM, 48000 Hz** olmalı. Değilse kullanıcıya Windows Ses ayarlarından nasıl düzelteceğini anlat.
- **U3 KAYIT KALİTESİ (kritik):** Varsayılan (WAV 24 bit, 48 kHz, stereo) ile 2 dk konuşma + 10 s sessizlik + bir bağırma. Aynı anda Audacity (ya da OBS) ile **CABLE Output**'u 24 bit 48 kHz kaydettir. İki dosyayı al: (a) MicVST dosyası: başlık 24/48000/2, gerçekten 24 bit (6.3), kırpma 0, Otomatik Kazanç açıksa hiçbir örnek > −10 dBFS, boşluk 0, tık 0, süre = kayıt süresi (±0,1 s), MicVST'nin "Kaydedildi" notu ve günlükte "dosya doğrulandı". (b) Karşılaştırma: hizalanınca seviye farkı ≤ 0,05 dB, korelasyon ≥ 0,999, hiçbirinde tık yok. Sonra aynısını **FLAC 24**, **WAV 32 float**, **WAV 16**, **MP3 320** ve **MP3 V0** ile tekrarla (her biri 30 s): başlıklar doğru, Windows Gezgini ve bir oynatıcıda (Medya Oynatıcı / VLC) açılıyor.
- **U4 Uzun kayıt:** 60 dk WAV 24 (arka planda normal kullanım): kayıp 0, süre hatası < %0,01, dosya ~1 GB açılıyor; kayıt sırasında Discord'da ses temiz (kullanıcı onayı).
- **U5 Çökme:** kayıt sürerken Görev Yöneticisi'nden MicVST'yi sonlandır → dosya son saniyeye kadar açılıyor.
- **U6 Kabloya hiçbir şey sızmıyor:** CABLE Output'u Audacity'de kaydederken F9 ile 10 kez aç/kapa → kayıtta **bildirim sesi yok** (yalnızca 15 ms sönme/açılma; spektrumda Ahşap notaları La3/Do♯4/Mi4 = 220/277/330 Hz ve üst tonları yok), tık 0. Kulaklıkta Ahşap sesleri duyuluyor (kullanıcı onayı); ses tuşa basmakla eşzamanlı algılanıyor (10 denemede gecikme fark edilmiyor). Windows varsayılan çıkışını CABLE Input yap → ses çalmıyor, günlükte uyarı bir kez.
- **U7 Bildirim sesi ayarları:** Ayarlar > Kısayollar > Bildirim sesleri: farklı sesler, ▶, %0/%50/%100, kendi WAV dosyasını `Sesler` klasörüne koy → yeniden başlatmadan listede, seç → çalıyor; programı kapat-aç → seçimler duruyor; tepsiden "Açma/kapama sesi" kapat → sessiz.
- **U8 Ses kalitesi dinleme:** 30 fabrika hazır ayarından en az 6'sı (Erkek: Dengeli Profesyonel, Podcast, Sinema Sesi; Kadın eşleri) ile kısa kayıt; geçişlerde tık yok; ses yükseklikleri yakın (LUFS farkı ≤ 1 LU gerçek seste).
- **U9 Kullanıcının kendi zinciri** (Clear, Otomatik Kazanç, Pro-DS ×3, Pro-Q 4 lineer faz 106,7 ms, Pro-C 3, SDRR2, Saturn 2 ×2, DC1A3, TDR VOS SlickEQ GE, TDR Limiter 6 GE): yüklenir, gösterilen gecikme ≈ eklentilerin toplamı + tampon, 30 dk Discord'da tık yok, kaydet-kapat-aç sonrası ayarlar aynı. **Ayarlarına dokunma.**
- **U10 Cihazlar:** QuadCast USB çıkar → "bağlı değil", tak → ≤ 2 s'de ses geri; Windows Ses ayarlarında mikrofon hızını 48→96→44,1 kHz yap → ses devam eder, kayıt sürüyorsa "örnekleme hızı değişti" mesajıyla biter, Kayıt panelinde 96 kHz'de 88,2/96 seçenekleri görünür; başka bir uygulama VB-Cable'ı özel modda tutarsa davranış (hata mesajı, tekrar deneme).
- **U11 Windows 11:** uyku → uyanma: ≤ 5 s'de ses geri, günlük temiz; Windows açılışında başlat → tepside sessiz başlıyor; Odak/Oyun modu açıkken kısayollar çalışıyor; DPI %125/150/175'te arayüz düzgün; açık/koyu modda tepsi simgesi doğru.
- **U12 Performans:** 10 dk normal kullanım (zinciri ile): Görev Yöneticisi CPU ve bellek; MicVST durum satırındaki "işlemci %" ve "kesinti 0". Kayıt açıkken CPU artışı ≤ %3 (bir çekirdeğin).
- **U13 İşlem önceliği:** üç masaüstü kısayolu ve Ayarlar'daki seçim: Görev Yöneticisi > Ayrıntılar'da öncelik aynı.
- **U14 Uzun kullanım:** R4 (bu teslimde ≈ 18–20 sa) ve R7 (ortam değişiklikleri; geri alınamaz kısımlar yalnızca kullanıcı isterse) burada yapılır.
- **U15 Sağlık kontrolü provası:** teslim edilecek `docs/UZUN_OMUR_KILAVUZU.md` (bölüm 9) içindeki 10 dakikalık sağlık kontrolünü kullanıcıyla bir kez birlikte yap. Kullanıcının bunu tek başına, teknik bilgi gerekmeden yapabildiğini doğrula; anlaşılmayan adımı sadeleştir.

---

## 8. Hata bulunca

1. Önem: **Kritik** (ses kaybı/tık/bozuk kayıt/çökme/ağ erişimi) · **Yüksek** (yanlış ses seviyesi, ayar kaybı) · **Orta** · **Düşük** (görünüm).
2. Yeniden üret, kök nedeni ölç (öncesi/sonrası sayılarla), en küçük düzeltme, birim/motor testi ekle (hatayı yakalayan test önce **başarısız** olmalı), düzeltmeden sonra geçmeli.
3. Çeviri gerekiyorsa: `cp i18n/keys.json eski.json && python3 tools/i18n.py extract && python3 tools/i18n_src.py renumber eski.json`, yeni `@@n` girişlerini 10 dile ekle, `python3 tools/i18n_src.py && python3 tools/i18n.py check && python3 tools/i18n.py generate`.
4. `CHANGELOG.md` 1.2.2 bölümüne bir satır.
5. Önce `[birim]` ya da yerel test, sonra normal commit ile **tam tur**; gerekiyorsa `[uzun]`.
6. Ses yolunu etkileyen her düzeltmeden sonra C, K, I bölümleri yeniden.

---

## 9. Çalışma sırası

1. Oku: bu dosya, `docs/DEVAM_NOTU.md`, `CHANGELOG.md`, `docs/TEST_RAPORU_1.2.2.md`. Kullanıcıya 3–4 cümleyle planı söyle.
2. Exe'yi al ve SHA'yı doğrula (bölüm 1).
3. L'de derle; birim + ASan/UBSan + `cues`/`record`/`quality` hızlı kontrol.
4. R1 kod denetimini yap (sayaç taşmaları), sonra eksik testleri yaz: K14 (disk dolu / çıkarma), K22 (tam birleşim taraması), P2 (60 dk kayıt), R1 (taşma testleri), R3 (330 dk gerçek zamanlı kayıt işi), R5 (saat değişimi), R6 (1000 aç-kapa), I6 (tepsi otomasyonu), 6.3 analiz betiği `tools/analiz_kayit.py`, R4 izleme betiği. Her biri önce yerelde, sonra `[skip ci]` ile commit.
5. Tam tur (W): **3 kez** – #1 hemen (kod değişmeden), #2 yeni testlerle, #3 kod dondurmadan sonra (0.1). Sonuçları sonuç dalından oku.
6. `[uzun]` (W, A, S): **2 kez** – #1 hemen, #2 yeni testlerle (R2 30 gün simülasyon, R3, R6, R9 dahil).
7. Kullanıcıyla U testleri (bölüm 7), R4 ve R7 dahil.
8. Bulunanları düzelt, yeniden test et (bölüm 8). Ses yolunu etkileyen her düzeltmeden sonra tam turu baştan yap; `[uzun]` ancak 23 Ekim 15:00'ten önce başlarsa yetişir. Kod dondurma: 23 Ekim 16:00 (0.1).
9. **Uzun ömür kılavuzu** `docs/UZUN_OMUR_KILAVUZU.md`. Türkçe, kısa ve teknik olmayan bir dille, kullanıcının tek başına uygulayacağı biçimde yaz:
   - neyi, nereye yedeklemeli: exe ve SHA-256, `%APPDATA%\MicVST\config.xml`, `Sesler\`, kendi hazır ayarlar (`.micvstpreset` dışa aktarımı), arşiv paketi;
   - **10 dakikalık sağlık kontrolü** (Windows büyük güncellemesinden, ses sürücüsü, VB-Cable ya da eklenti güncellemesinden sonra ve ayda bir):
     - log'daki "Ses biçimi" satırı;
     - durum satırında "kesinti 0";
     - 30 s WAV 24 kayıt ve kulaklıkla dinleme;
     - F9 ve bildirim sesi;
     - Discord'da karşı tarafla kısa deneme;
     - isterse kaydı ve log.txt'yi bir Claude oturumuna analiz ettirmek için hazır bir cümle;
   - bir şey bozulursa geri dönüş: yedek config ve önceki exe;
   - yapılmaması gerekenler: VB-Cable biçimini 24 bit / 48 kHz dışına almak, Windows "ses geliştirmeleri"ni kablo için açmak vb. – yalnızca gerçekten ölçülmüş olanları yaz.
10. **Arşiv paketi** (R9) ve ağsız derleme kanıtı.
11. Rapor: `docs/TEST_RAPORU_WIN11_1.2.2.md` (Türkçe):
    - özet tablo (alan, durum, ortam);
    - her test kimliği için ölçülen değer, ölçüt ve durum;
    - bulunan hatalar önem sırasıyla (neden, düzeltme, yeniden test);
    - ENGELLENEN / ATLANAN testler ve nedenleri;
    - **"Uzun ömür değerlendirmesi":** hangi koşullarda ne kadar süre doğrulandı; kalan riskler (Windows büyük güncellemeleri, sürücü / eklenti / donanım değişimi) ve her birinde ne yapılacağı. Mutlak garanti dili kullanma;
    - kullanıcının yapması gerekenler.

    Exe, SHA-256, arşiv zip'i ve kılavuzla birlikte kullanıcıya gönder.
