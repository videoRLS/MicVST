# MicVST – devam notu (yeni Claude oturumu için)

Yeni oturumda ilk iş: bu dosyayı baştan sona oku, sonra "Bekleyen işler"den devam et.
Kullanıcıyla **Türkçe**, kısa konuş. **Rapor/doküman yazma** (kullanıcı okumuyor); yalnızca testleri
sağlam yap, sonunda exe + SHA-256 + bir iki satır ver.

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

## Durum: testler durduruldu (kullanıcı kararı, 20.10 22:30)

Kullanıcı, yarım kalan test işlerini durdurdu (çalışan CI turları iptal edildi). Bundan sonraki iş: bu dalın
şimdiki hâlinin (öncelik seçeneği, mono düzeltmesi, canlı gecikme, Türkçe sürüm bilgisi, gece teması,
sıra doğrulaması) **üstüne dahili efektleri eklemek**. Tam testler (normal commit + `[uzun]`) efektler
bittikten sonra hep birlikte koşulacak; teslim ondan sonra (exe + SHA-256, rapor yok).

Durdurulmadan önce bilinenler:
- Gerçek MicVST.exe ile öncelik kontrolü geçti (5e455f9): Normal / AboveNormal / High doğru, yeniden açılışta korunuyor.
- Sıra doğrulaması (yerel, b54f39f): ZL Equalizer 2 -> Fire zincirinde kablo = çevrimdışı zincir (artık medyan
  -80 dB, 0 bulgu); ters sıra negatif kontrolü 510 bulgu.
- Linux'ta vst3suite 73/77 geçti; kalan 4 hata test toleransıydı, düzeltildi, Windows'ta henüz koşmadı.
- `7b12d32` tam turu iptal edildi (sonucu yok). Windows'ta `vst3suite` / `priority` / `monochain` henüz hiç tam koşmadı.

## Sıradaki iş: dahili efektler (kullanıcı listeyi onayladı)

Hepsi eklenti listesinde **Dahili** başlığı altında, zincire eklenip taşınabilen, devre dışı bırakılabilen,
ayarları config.xml'de saklanan ayrı elemanlar. De-esser ve Çıkış Limiteri gibi dinamik işlemciler kullanıcı
onayıyla serbest; **gürültü kapısı ve kompresör hâlâ yok**. Varsayılan olarak hiçbiri zincire kendiliğinden girmez.

Ton ve EQ:
1. Low-Cut (Alçak Kesici): 60-100 Hz altı (12/24 dB/oktav), uğultu, titreşim, "p" alt ucu.
2. High-Cut (Yüksek Kesici): 14-18 kHz üstü, cızırtı/tıslama.
3. Gövde / Sıcaklık: 120-250 Hz yükseltme (tok ses).
4. Çamur Temizleyici: 300-500 Hz kısma (boğukluk, "kutu sesi").
5. Genizlik Azaltıcı: 0,8-1,3 kHz kısma (genizden gelen sert orta bölge).
6. Netlik (Presence): 3-5 kHz yükseltme (anlaşılırlık).
7. Hava (Air): 10-12 kHz üstü yükseltme (parlaklık).
8. Yakınlık (Proximity): yakın mikrofon tokluğu ekle / fazlasını azalt.
9. Tilt EQ (Ton Dengesi): tek kaydırıcı, sıcak <-> parlak.
10. Parametrik EQ: birkaç bant (frekans, kazanç, Q).
Temizlik:
11. Uğultu Giderici (50 Hz): 50/100/150/200... Hz dar çentikler (60 Hz seçeneği).
12. DC Kayması Giderici.
Renk ve ortam:
13. Sıcak Saturasyon (Tüp / Bant): hafif harmonik, aşırı örneklemeli.
14. Telefon / Radyo / Megafon filtresi.
15. Oda Yankısı (basit; juce::dsp::Reverb).
16. Gecikme / Eko.
17. Chorus / Stereo Genişletici.
Seviye ve koruma:
18. Çıkış Limiteri (true peak; Otomatik Kazanç'taki test edilmiş limiter kodu, ileri bakışlı, tavan ayarlı).
19. Sessize Alma (Mute): tıksız susturma.
20. Otomatik Kazanç (var), 21. Kazanç (var).
Zaman ve kanal:
22. Ses Gecikmesi (senkron): ms cinsinden gecikme, görüntüyle hizalama.
23. Yardımcılar: faz çevirme, sol/sağ kanal seçimi, sağ-sol denge.
24. Mono -> Stereo / Stereo -> Mono (var).
Ölçüm:
25. LUFS ölçer (ITU-R BS.1770, gerçek tepe).
26. Spektrum Analizörü.
(Ayrıca listede istenirse: De-esser - kullanıcı onayladı ama "kolay/emin" listesinde değil, orta zorluk.)

Önerilen uygulama yolu:
- `src/audio/BuiltInEffects.{h,cpp}`: her efekt bir `juce::AudioProcessor`, parametreleri
  `AudioParameterFloat/Choice/Bool` (kimlikler sabit, Türkçe adlar `tr()` ile), durum = parametrelerin XML'i.
  Kimlikler `builtin:lowcut`, `builtin:highcut`, `builtin:body`, ... (`PluginChain` ve `AudioEngine::restoreChain`
  bir fabrika üzerinden kurar; eski config.xml'ler etkilenmez).
- DSP: `juce_dsp` (IIR filtreler, Oversampling, Reverb, Chorus, DelayLine); parametre değişimleri yumuşatılarak
  (katsayılar küçük bloklarda güncellenir) - tık yok. Gecikme ekleyenler (limiter ileri bakışı, senkron gecikmesi)
  `setLatencySamples` ile bildirir (MicVST gösterilen gecikmeye ve düzenleme sessizliğine katar).
- Arayüz: çift tıkla açılan basit ayar penceresi (`juce::GenericAudioProcessorEditor` gece temasıyla), satırda ad +
  gecikme + devre dışı; ölçerler (LUFS, spektrum) kendi küçük pencerelerinde.
- Testler: her efekt için birim testi (frekans cevabı/kazanç hedefleri, THD, gecikme, mono/stereo, durum gidiş-dönüş,
  NaN/aşırı giriş, parametre değişiminde tık yok), motor testinde çalışırken ekle/taşı/kaldır, arayüz görüntüleri;
  sonra tam tur + `[uzun]`.

## Kullanıcının kuralları / tercihleri

- Çalışan özellikleri, arayüzü ve gereksinimleri gereksiz değiştirme. **Noise gate veya compressor ekleme.**
  İnternet erişimi kapalı kalmalı (exe'de ağ kütüphanesi yok; CI bunu kontrol ediyor). Windows 11 yapısı korunmalı.
- Testleri yüzeysel geçme; doğrulanmamış hiçbir şeyi başarılı gösterme. İstediğin kadar test/deneme yap.
- Otomatik Kazanç (v9, kullanıcının seçimi): normal konuşma tepe ~-20 dBFS, bağırma ~-15 dBFS, kazanç
  0..+6 dB, tavan -10 dBFS (true peak), sessizlikte kazanç korunur, noise gate yok, **zincirdeki yeri
  kullanıcı belirler (taşınabilir)**. Metindeki eski -15/-17,5 hedeflerine geçilmeyecek (kullanıcı onayladı).
- İşlem önceliği varsayılanı Normal. Gece teması onaylı. Kendi sanal kablo sürücüsü fikri iptal (VB-Cable kalıyor).
- Kullanıcının zinciri (13 eklenti): Clear, Otomatik Kazanç, Pro-DS x3, Pro-Q 4 (lineer faz, 106,7 ms),
  Pro-C 3, SDRR2, Saturn 2 x2, DC1A3, TDR VOS SlickEQ GE, TDR Limiter 6 GE.
- Commit mesajlarının sonuna o oturumun kendi Co-Authored-By / Claude-Session satırları eklenir.
