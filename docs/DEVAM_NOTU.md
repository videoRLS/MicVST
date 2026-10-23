# MicVST – devam notu (yeni Claude oturumu için)

> **Yeni oturum için talimat: bu notu yalnızca OKU. Kendiliğinden hiçbir işe başlama.**
> Kod değiştirme, commit/push yapma, CI tetikleme, test başlatma yok. Notu okuduktan sonra kullanıcıya
> kısaca (Türkçe) ne anladığını söyle ve **kullanıcının talimatını bekle**. Kullanıcı ne derse onu yap.

Kullanıcıyla **Türkçe**, kısa konuş. Kullanıcı istemedikçe rapor/doküman yazma; iş verildiğinde testleri
sağlam yap, sonunda exe + SHA-256 + bir iki satır ver. (1.2.2 için istenen rapor: `docs/TEST_RAPORU_1.2.2.md`.)
Kullanıcı kapsamlı Windows 11 testini isterse görev tanımı hazır: **`docs/TEST_PROMPTU_WIN11.md`**.

## Depo ve dallar

- Depo: `videoRLS/MicVST` (JUCE 9.0.4, Windows mikrofon -> VST3 zinciri -> sanal kablo; arayüz Türkçe).
- Çalışma dalı: **`kararlilik/oncelik`** (`15cec52`'den açıldı). `main`'e dokunma; kullanıcı istemedikçe
  PR/merge yok.
- Önceki kararlı sürüm: `kararlilik/kazanc-kontrolu` @ `15cec52`, exe SHA-256
  `ce69214a63f68701c21eab739c932865a123c4b17d8e7eb82830198268523844` (tüm testler + 5,5 sa uzun koşular geçti).
- `kararlilik/macos`: macOS sürümü (ayrı iş, bu turda dokunulmadı).

## Bu turda yapılanlar (sürüm 1.2.2)

Ürün kodu:
1. **İşlem önceliği** (DevicePanel, "Güvenlik payı"nın altında): Normal / Normalin Üstünde / Yüksek.
   **Varsayılan Normal** (kullanıcı istedi; eskiden açılışta hep HIGH). `AudioEngine::setProcessPriority`
   -> `rt::setProcessPriority` (SetPriorityClass + GetPriorityClass ile geri okuma), config.xml'de
   `processPriority`; kutu her zaman Windows'un bildirdiği sınıfı gösterir (Görev Yöneticisi'nden
   değişirse "Başka bir seviye"); reddedilirse bir kez uyarı kutusu + log. Ses iş parçacıkları her
   seçenekte MMCSS.
2. **Mono düzeltmesi** (`GraphConnections.cpp`): mono kısım (mono mikrofon, RNNoise gibi mono eklenti)
   arkasındaki stereo eklentinin iki girişini de besler. Önce sağ kanal sessiz kalıyordu (ölçüldü:
   R -200 dB). Çıkış düğümüne mono biten zincir yine tek kanal gider (motor iki kanala kopyalar).
3. **Canlı zincir gecikmesi** (`AudioEngine::chainLatencySamples`): gösterilen gecikme ve düzenleme
   sessizliği, etkin elemanların o anki gecikme toplamını kullanır (JUCE graf gecikmesi yalnızca
   yeniden kurulunca güncelleniyordu; eklenti penceresinde lineer faza geçince eksik kalıyordu).
4. Exe sürüm bilgisi dili **Türkçe (Türkiye)** (`cmake/PatchJuce.cmake` madde 6, juce_ResourceRc.cpp).
5. **Gece teması** (kullanıcı onayladı): `src/ui/Theme.h` (NightLookAndFeel + tüm renkler),
   `src/ui/DarkTitleBar.cpp` (Windows koyu başlık çubuğu, DwmSetWindowAttribute çalışma anında).
6. Sürüm 1.2.2 (CMakeLists, CHANGELOG).

Test altyapısı (yeni):
- `MicVSTEngineTest` modları: `vst3suite` (gerçek VST3 eklentileri tek tek ve birlikte;
  `tests/enginetest/Vst3Suite.inc`), `vst3info <bundle>`, `priority` + `burn` (Windows;
  `tests/enginetest/PriorityTest.inc`), `monochain`; `dump ... suite` / `suite-wrongref` (sıra
  doğrulaması + negatif kontrol); `long` için `ETEST_LONG_VST3`, `ETEST_LONG_PRIORITY`, `ETEST_LONG_BURN`.
- Eklentiler (`tools/ci-suite-plugins.ps1`, CI indirir; MicVST'nin kendisi internete bağlanmaz):
  RNNoise 1.10, ZL Equalizer 2 1.4.1, ZL Compressor 0.6.1 (kompresör + de-esser + limiter ayarları),
  Fire 2.0.0, TDR VOS SlickEQ (ücretsiz), Airwindows Consolidated, Klanghelm DC1A3 (zip içindeki
  kurulum sessiz kurulursa). Kullanıcının ticari eklentileri (Clear, FabFilter, SDRR2, TDR GE) test
  edilemez; bunlar aynı türden ücretsiz karşılıklarıyla temsil ediliyor.
- `tools/ci-runtime-check.ps1`: gerçek MicVST.exe ile her öncelik + eski config.xml (geçti: 5e455f9 turunda
  Normal/AboveNormal/High doğru, yeniden açılışta korunuyor).
- `kararlilik-uzun.yml`: `vst3long` işi (öncelik 0/1/2, her biri 5,5 sa, gerçek eklenti zinciri, tüm
  CPU'lar başka bir programla meşgul); win11arm'a `priority` ve `monochain` eklendi.

## CI kullanımı

- Commit mesajında `[hizli]`: yalnızca exe derlenir -> `ci-sonuclari/<dal>-hizli`.
- Normal commit: tam test (~2,5 sa) -> `ci-sonuclari/<dal>`.
- `[uzun]`: ayrıca uzun koşular -> `ci-sonuclari/<dal>-uzun-<iş>` (long, chaos, app, simweek, win11arm,
  vst3long-p0/p1/p2).
- CI günlükleri indirilemeyebilir (403); sonuçları `git fetch origin ci-sonuclari/...` ile al. Üzerine
  yazılmış eski sonuçlar: `gh api "repos/videoRLS/MicVST/activity?ref=refs/heads/ci-sonuclari/kararlilik/oncelik"`
  -> `after` SHA'sını `git fetch origin <sha>` ile çek.
- Sonuç dosyaları: `engine/*.txt` (quality, flutter, autogain, vst3suite, priority, monochain, vst3info,
  long, chaos, ...), `engine/integrity.txt` (dump analizleri), `runtime/runtime-check.md`, `unit-tests.txt`,
  `metrics.json` (exe SHA), `ui/*.png` (arayüz görüntüleri).

## Durum (22.10.2026 sabah)

Teslim edilen son exe: `ecee2b6`'nin Windows CI derlemesi, SHA-256
`9ecabac2cc851245251ac059ecfb06e029816a9b2f5e9e4bcae1cc5a77b100d0`. Sonraki commit'ler yalnızca test ve belge.
Tam rapor: `docs/TEST_RAPORU_1.2.2.md`.

Bu oturumda eklenenler (önceki maddelere ek):
- **24 dahili efekt** ve **hazır ayarlar** (CHANGELOG'da) – tamamlandı.
- **Kayıt** (`src/audio/Recorder.{h,cpp}`, `Resampler.h`, `src/ui/RecordingPanel.*`): WAV 16/24/32 PCM + 32 float
  (BWF, 4 GB üstü RF64), FLAC 16/24 + sıkıştırma 0–8, MP3 LAME 4.0 (`third_party/lame-4.0`, LGPL; kaynak
  `[kaynak]` CI işiyle SHA-256 denetlenerek alındı) CBR 128–320 / VBR V0/V2/V4; 44,1/48 kHz (mikrofon daha
  hızlıysa 88,2–192); stereo/mono; klasör seçimi; ana pencerede "● Kayıt" + süre, tepside başlat/durdur,
  Ayarlar > Kayıt. Ses iş parçacığı yalnızca kilitsiz tampona (30 s) kopyalar; yazıcı iş parçacığı normalin
  üstü öncelikte. config.xml: `recordFormat` ("wav:24|48000|2"), `recordFolder`.
- İmza ("Relios tarafından geliştirilmiştir.") kaldırıldı.
- Deneme kaydı çalınırken baş/son tıkı düzeltildi (10 ms geçiş); kulaklık düzeyi hatası düzeltildi.
- Fare kısayolları, ses biçimi günlük satırı, masaüstü yardımcıları (`tools/masaustu/*.vbs`).

Testler: birim `Recorder` (her biçim/derinlik/hız, dither, taşma, çökme, 20 dk, RF64), motor `record` modu,
`tools/ci-recordings.ps1` (ffmpeg + Windows açıyor mu). CI işaretleri: `[birim]` = yalnızca derleme + birim
testleri (~45 dk, `ci-sonuclari/<dal>-birim`), `[kaynak]` = üçüncü taraf kaynak indirme, `[vst3tani]`,
`[vst3uzun]`. Linux konteyner 2 çekirdekli ve gürültülü: motor testlerinde "makine takılması" olur.

Açık konular: uygulama adı kararı (kullanıcı seçmedi); gerçek donanım testleri kullanıcıda.

## Kullanıcının kuralları / tercihleri

- Çalışan özellikleri, arayüzü ve gereksinimleri gereksiz değiştirme. Otomatik Kazanç'ta noise gate /
  kompresör yok (dahili efektlerde Kompresör ve Gürültü Kapısı kullanıcı onayıyla var).
  İnternet erişimi kapalı kalmalı (exe'de ağ kütüphanesi yok; CI bunu kontrol ediyor). Windows 11 yapısı korunmalı.
- Testleri yüzeysel geçme; doğrulanmamış hiçbir şeyi başarılı gösterme. İstediğin kadar test/deneme yap.
- Otomatik Kazanç (v9, kullanıcının seçimi): normal konuşma tepe ~-20 dBFS, bağırma ~-15 dBFS, kazanç
  0..+6 dB, tavan -10 dBFS (true peak), sessizlikte kazanç korunur, noise gate yok, **zincirdeki yeri
  kullanıcı belirler (taşınabilir)**. Metindeki eski -15/-17,5 hedeflerine geçilmeyecek (kullanıcı onayladı).
- İşlem önceliği varsayılanı Normal. Gece teması onaylı. Kendi sanal kablo sürücüsü fikri iptal (VB-Cable kalıyor).
- Kullanıcının zinciri (13 eklenti): Clear, Otomatik Kazanç, Pro-DS x3, Pro-Q 4 (lineer faz, 106,7 ms),
  Pro-C 3, SDRR2, Saturn 2 x2, DC1A3, TDR VOS SlickEQ GE, TDR Limiter 6 GE.
- Commit mesajlarının sonuna o oturumun kendi Co-Authored-By / Claude-Session satırları eklenir.

## 23 Ekim 2026 – Windows test görevi (TEST_PROMPTU_WIN11) sonu

- 16:00 kod dondurma: ürün kodu `93cb4b7` ile aynı (sonrası yalnız test araçları ve belgeler). Tam tur #3 bu
  commit'le başlatıldı; teslim edilen exe onun exe'si.
- Rapor: `docs/TEST_RAPORU_WIN11_1.2.2.md`; kılavuz: `docs/UZUN_OMUR_KILAVUZU.md`; kanıtlar: `docs/kanit_win11/`.
- Açık bulgu: Clear zincirdeyken kapanışta "MicVST.exe çöktü" olayı (çıkış kodu 0, kayıp yok). Denenen
  "süreci hemen bitir" düzeltmesi işe yaramadı ve geri alındı (çökme eklenti kapatılırken oluyor).
- Test araçları: `tools/ci-reclong-check.py` + `tools/r8_standart.py` (R3/R8), `tools/ci-startstop.ps1` (R6),
  `tools/ci-clockchange.ps1` (R5), `tools/arsiv_olustur.sh` (R9 arşiv), `MicVSTTests --list` / `Prefix*`.
