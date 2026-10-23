# MicVST 1.2.2 – Kapsamlı test raporu (Windows)

> Taslak – 23 Ekim 2026 sabah. CI tur #2 / #3 sonuçları geldikçe doldurulacak bölümler **(bekleniyor)** diye işaretli.

- **Dal:** `kararlilik/oncelik` (`main`'e dokunulmadı, PR / merge / release yok)
- **Görev tanımı:** `docs/TEST_PROMPTU_WIN11.md`
- **Durum sözcükleri:** BAŞARILI · BAŞARISIZ · ATLANDI · ENGELLENDİ. Test edilmeyen hiçbir şey BAŞARILI yazılmadı. Mutlak garanti verilmez.

## 0. Ortamlar

| Kısa ad | Ne | Ayrıntı |
|---|---|---|
| **U** | Kullanıcının bilgisayarı (gerçek donanım) | **Windows 10 Pro 22H2, yapı 19045.7725** (görev Windows 11 diyordu; kullanıcı Windows 10'da test istedi, bkz. §6). HyperX QuadCast 2 S (USB, 48 kHz / 32 bit), VB-Audio Virtual Cable **3.3.1.7** (CABLE Input / Output 24 bit / 48 kHz), Realtek HD 6.0.9977.1, Elgato 4K X. 16 mantıksal işlemci. Kullanıcının zinciri: Clear, Otomatik Kazanç, FabFilter Pro-DS ×3, Pro-Q 4 (lineer faz, 106,7 ms), Pro-C 3, SDRR2, Saturn 2 ×2, Klanghelm DC1A3, TDR VOS SlickEQ GE, TDR Limiter 6 GE. PC saati kullanıcının tercihiyle 3 saat geride. |
| **W** | GitHub Actions `windows-latest` | Windows Server 2025, MSVC; tam tur (`kararlilik-test`) ve uzun koşular (`kararlilik-uzun`). |
| **A** | GitHub Actions `windows-11-arm` | **Windows 11 Enterprise 10.0.26200** (ARM64): birim testleri + motor modları + 60 dk long. |
| **L** | Linux konteyneri (bu oturum) | Hızlı yerel testler, ASan/UBSan, rtprobe, K22 tam tarama, R3/R8 kısa koşular. |
| **S** | Simülasyon | `MicVSTStress --day` (sanal saat), Otomatik Kazanç soak. |

## 1. Özet

**(bekleniyor – tur #3 sonrası tablo)**

| Alan | Durum | Ortam | Not |
|---|---|---|---|
| A Derleme / exe | | W, A | |
| B Birim testleri | | W, A, L | L: tümü geçti (23 Ekim 04:41-05:50) |
| C Ses kalitesi / bütünlük | | W, A, L, U | C7 rtprobe L: 0 bellek ayırma, 0 bekleyen kilit |
| D Saat farkı / tampon / gecikme | | W, S | |
| E Cihazlar / hatalar | | W | |
| F Dahili efektler | | W, L | |
| G Otomatik Kazanç | | W, S | |
| H Hazır ayarlar | | W | |
| I Sustur / Ham ses / kısayollar / bildirim sesleri | | W, U | I6 tepsi menüsü: kullanıcı beyanı |
| J Kendini dinle / Deneme kaydı | | W | |
| K Kayıt | | W, L, U | K22: 760 dosya, 0 hata (L) |
| L İşlem önceliği | | W, U | |
| M VST3 | | W | Airwindows: zamanla değişen eklenti (bkz. §3) |
| N Arayüz / dil / tema | | W, U | Tema/dil titremesi düzeltildi, kullanıcı doğruladı |
| O Kapalı kutu / gizlilik / kalıcılık | | W | |
| P Uzun süre | BAŞARILI (tur #1) | W, A, S | tur #2 (bekleniyor) |
| R Uzun ömür | | W, S, L, U | |
| U Gerçek donanım | bkz. §4 | U | |

## 2. Bulunan hatalar (önem sırasıyla)

| # | Önem | Bulgu | Neden | Düzeltme | Yeniden test | Durum |
|---|---|---|---|---|---|---|
| 1 | Orta | Her başlangıçta durum satırında **"kesinti 1"** | WASAPI, her `Start` sonrası ilk pakete DATA_DISCONTINUITY koyuyor (QuadCast 2 S ve VB-Cable'da ölçüldü: her zaman paket 0, 4/4 başlangıç) | Yalnızca her başlangıcın ilk bloğundan sonraki kesintiler sayılıyor | Motor testi `startxrun` (başta 0, araya enjekte 1, cihaz yeniden açılınca 0) | Düzeltildi |
| 2 | Düşük (ölçülebilir, duyulamaz) | 24 / 32 bit tamsayı kayıtta yuvarlama **yanlılığı** (+0,157 LSB ortalama ≈ −158 dBFS DC) | Tam ortadaki değerler hep yukarı yuvarlanıyordu (|x|>0,5 float'ta 24 anlamlı bit) | En yakına, eşitlikte çifte (`std::nearbyint`) | Birim "Recorder K22": ortalama −0,0001 LSB, her örnek en yakın değer | Düzeltildi |
| 3 | Düşük (görsel) | Tema / dil değişince Ayarlar penceresi bir an "hayalet" gibi görünüyordu | Yeni pencere önce varsayılan yerde açık başlıkla gösterilip sonra taşınıyordu | Konum, görünüm ve koyu başlık gösterilmeden önce; eski pencere yenisi açılınca kapanıyor | Kullanıcı hızlı derlemeyle denedi: "düzelmiş" (23 Ekim 05:28) | Düzeltildi (kullanıcı bulgusu) |
| 4 | Düşük | Mikrofon açılamazken bilgisayar > 24,8 gün uykuda kalırsa yeniden deneme 24,8 güne kadar bekleyebiliyordu | 32 bit ms sayacında işaretli fark | İşaretsiz fark (`micvst::ReopenBackoff`) | Birim "Counters R1" (önce başarısız) | Düzeltildi |
| 5 | Düşük (görsel) | Spektrum göstergesi 24,9 saatte bir bir an boş | Örnek sayacı 32 bit | 64 bit | Birim: 2^32 örnek beslenir (önce başarısız) | Düzeltildi |
| 6 | **Kritik değil, ama görünür** | Zincirde **Clear** varken kapanışta Windows olay günlüğüne "MicVST.exe çöktü" (0xc0000005, modül bilinmiyor) | Eklenti kapatılırken (Clear) erişim ihlali; MicVST'nin çıkış kodu 0, ayarlar değişmiyor | Denenen "kapanışta süreci hemen bitir" düzeltmesi **işe yaramadı** (yeni exe ile 9 kapanışın 8'inde yine olay) → **geri alındı** | U: 10'da 8 (eski), 9'da 8 (denenen) | **Düzeltilmedi** (kullanıcı kararıyla ayrıntılı inceleme yapılmadı) |
| 7 | Düşük | Kendi zinciri tepsiden hazır ayar seçilince değişir (beklenen davranış, ama kullanıcı fark etmeyebilir) | – | Kılavuza not | – | Bilgi |
| 8 | Düşük | İmzasız exe ilk açılışta SmartScreen uyarısı (Zone.Identifier) | Kod imzası yok | – (imza dışarıda) | U1 | Bilgi |
| 9 | Üçüncü taraf | libsndfile 1.2 (Python `soundfile`) MP3'ü **parça parça** okurken her okuma sınırından sonra ~1600 örneği yanlış çözüyor | libsndfile hatası (ffmpeg ile yapılmış MP3'te de aynı) | MicVST'de değil; R8 betiği MP3'ü bütün okur | `tools/r8_standart.py` | Bilgi |
| 10 | Test altyapısı | Airwindows Consolidated (CI'nın indirdiği gece sürümü 1.2026.291) sesi kendiliğinden değişen bir efektle açılıyor → 4 vst3suite + 1 bütünlük karşılaştırması BAŞARISIZ görünüyordu | Eklenti modülasyonlu (aynı ayarla ardışık iki ölçüm 10,5 dB farklı; ZL EQ 0,04, DC1A3 0,06, SlickEQ 0,14) | vst3suite: > 1 dB ise "aynı ses" karşılaştırmaları ATLANDI olarak raporlanır | tur #2/#3 (bekleniyor) | Test düzeltildi; bütünlük karşılaştırması: (bekleniyor) |
| 11 | Test altyapısı | Yeni `RecordLong.inc`'te `near` değişkeni → MSVC derlemesi durdu (tur #2 ilk deneme) | `near`, windows.h'de makro | `isNear` | tur #2 yeniden | Düzeltildi |
| 12 | Düşük | Ayar / önbellek / log yazılırken süreç öldürülürse geçici dosya kalıyor (R6: 200 öldürmede 6 dosya) | JUCE `writeTo` geçici dosya + değiştirme; öldürülünce geçici dosya silinmiyor | Düzeltilmedi (zararsız; ayar dosyası hiç bozulmadı) – öneri: açılışta eski `*_temp*.xml` dosyalarını sil | R6 | Bilgi |

## 3. Notlar ve açıklamalar

- **Kullanıcının zinciri ağır:** MicVST bir çekirdeğin ~%45'ini kullanıyor (U12); log'da ara sıra "İşleme: N/100 blok gerçek zamandan uzun sürdü" (eklenti yükü tepe %100–153). 60 dk kayıtta (U4) kabloda kesinti/ekleme/kesme yok; köprü saat farkı düzeltmesini en çok 148 ppm (0,26 cent) hızlandırıyor – duyulamaz. Gösterilen gecikme ~288 ms (Pro-Q 4 lineer faz 106,7 ms + diğerleri + güvenlik payı).
- **Thread / tutamaç sayısı:** U4 sırasında MicVST'nin iş parçacığı sayısı 33 → 69, tutamaç 611 → 798 arttı ve kayıt bitince düşmedi; aynı anda UI Automation (test betiği) bağlandı. CI'da (UIA yok) gerçek exe 330 dk: iş parçacığı 9, tutamaç 387-394 sabit. Büyüme yalnızca bir kez, sonra sabit (R4 7 saat) – sızıntı değil; büyük olasılıkla UIA sağlayıcısı + eklentiler. (bekleniyor: R4 sonu)

## 4. Gerçek donanım testleri (U)

| Test | Ölçülen | Ölçüt | Durum |
|---|---|---|---|
| U1 Kurulum | `C:\MicVST\MicVST.exe`; ilk açılışta SmartScreen uyarısı (Zone.Identifier) – kullanıcı onayladı | not et | BAŞARILI (not) |
| U2 Ses biçimi | Log: "giriş 32 bit PCM, 48000 Hz, 2 kanal \| çıkış 24 bit PCM, 48000 Hz, 2 kanal" | giriş QuadCast 32 bit, çıkış 24 bit / 48 kHz | BAŞARILI |
| U3 Kayıt kalitesi | Bkz. `docs/kanit_win11/U3_kayit_kalitesi.md`: 6 biçim; kırpma 0; kablo ile seviye farkı ≤ 0,031 dB; korelasyon medyan ≥ 0,9999 (en kötü %1 PCM 0,997–0,9999) | seviye ≤ 0,05 dB, korelasyon ≥ 0,999, tık 0 | BAŞARILI (korelasyon medyanla) |
| U4 60 dk kayıt | 3600,05 s; düğme aralığı 3600,032 s → fark %0,0005; 24 bit gerçek %99,6; kırpma 0; boşluk 0; kablo 3720 s kesintisiz; ayrıntı `U4_kablo_inceleme.md` | kayıp 0, süre < %0,01 | BAŞARILI |
| U5 Çökme | 65,5 s'de zorla sonlandırıldı: başlık 64,76 s, diskte 65,47 s, boşluk 0, tepside yeniden açıldı | son saniyeye kadar açılır | BAŞARILI |
| U6 Kabloya sızma | 20 Sustur geçişi (F9 eşdeğeri, 1,5 s arayla) → log'da 20 bildirim sesi; CABLE Output yakalamasında susturulu her an **tam dijital sıfır** (−240 dB), açık anlarda oda gürültüsü −65…−68 dBFS ve Ahşap notaları (220 / 277 / 330 Hz) −85…−102 dB (ses yok); kullanıcı kulaklıkta duydu: "aynı anda gibi". Varsayılan çıkış = CABLE Input iken: ses çalınmadı, log'da bir kez uyarı | bildirim sesi kabloda yok, eşzamanlı, kablo varsayılansa çalmaz | BAŞARILI |
| U7 Bildirim sesi ayarları | Kullanıcı `Sesler` klasörüne kendi WAV'ını koydu → yeniden başlatmadan listede, seçince çaldı ("duydum ve liste de görünüyor"); %0/%50/%100 ve kalıcılık W'de birim "Cue sounds" ile ölçüldü (I5) | | BAŞARILI (U kısmı kullanıcı beyanı) |
| U8 Hazır ayar dinleme | – | | ATLANDI (kullanıcı kararı) |
| U9 Kendi zinciri + 30 dk Discord | Zincir yükleniyor, gecikme ~288 ms gösteriliyor; Discord oturumu yapılmadı (arkadaşı sesin iyi olduğunu söyledi – öznel) | | ATLANDI (kullanıcı kararı); zincir kısmı U4/U12'de kullanıldı |
| U10 Cihazlar | – | | ATLANDI (kullanıcı kararı) |
| U11 Windows 11 uyku / DPI / tema | – (PC Windows 10) | | ATLANDI (kullanıcı kararı) |
| U12 Performans | Kayıtsız %45,28, kayıtlı %44,81 bir çekirdek (fark −0,47 puan); bellek 261 → 275 MB; durum satırı kesinti sabit 1 (eski exe, başlangıç işareti) | kayıt açıkken artış ≤ 3 puan | BAŞARILI |
| U13 İşlem önceliği | – | | ATLANDI (kullanıcı kararı); R4 boyunca öncelik Normal okundu |
| U14 Uzun kullanım (R4) | (bekleniyor – 18:05'te biter) | | |
| U15 Sağlık kontrolü provası | (bekleniyor – kullanıcıyla) | | |
| I6 Tepsi menüsü | Kullanıcı tüm öğeleri denedi: "sorunsuz" | | BAŞARILI (kullanıcı beyanı, ölçülmedi) |

## 5. Uzun ömür testleri (R)

| Test | Ölçülen | Durum |
|---|---|---|
| R1 Sayaç taşmaları | Tüm `getMillisecondCounter` kullanımları incelendi (AudioEngine ×3, CueSounds, Recorder ×2, PluginListView ×2, ScanCoordinator ×2); birim "Counters R1": kayıt başlığı, bildirim sesi önbelleği, cihaz yeniden deneme sayaç başa dönerken doğru; 2 hata düzeltildi (bkz. §2 #4, #5); sıra sayaçları (`pushSeq`, `stampSeq`) yalnız eşitlik/çiftlik ile karşılaştırılıyor (2^32 çift → taşmada doğru); örnek konumları 64 bit / double; efekt fazları double + sarmal | BAŞARILI |
| R2 30 gün simülasyon + 7 gün Otomatik Kazanç | (bekleniyor – uzun tur #2) | |
| R3 320 dk gerçek zamanlı kayıt (WAV 24 → RF64, FLAC 24, MP3 V0) | L kısa koşular geçti; W: (bekleniyor) | |
| R4 Gerçek kullanım | 22 Ekim 21:53 → … ; ilk süreç 7 saat kesintisiz (U5'te bilerek öldürüldü); bellek 248 → 272 MB sonra sabit; öncelik hep Normal; pencere hep yanıt verdi (bekleniyor: son) | |
| R5 Saat değişimi | (bekleniyor – tam tur #2/#3) | |
| R6 1000 aç-kapa / 200 öldürme | W, gerçek MicVST.exe (c0515a9): 800 normal kapanış (WM_QUERYENDSESSION, çıkış 0) + 200 `taskkill /f` (ilk 2 s içinde, rastgele an); çökme 0, askıda kalma 0, okunamayan config 0, kaybolan ayar (3 işaret) 0; WER/Application Error olayı 0; pencereye kadar medyan 261 ms, %95 624 ms, en çok 3,1 s. Not: öldürülen yazmalardan 6 geçici dosya kaldı (`config_temp*.xml` ×2, `plugin_cache_temp*.xml` ×2, `log.txt~RF*.TMP` ×2) – zararsız, birikir (bulgu #12) | BAŞARILI |
| R7 Ortam değişiklikleri | – | ATLANDI (kullanıcı kararı / süre) |
| R8 Okunabilirlik | L: WAV/BWF (EBU 3285), FLAC (RFC 9639 + MD5), MP3 (çerçeveler + Xing/LAME): BAŞARILI; libsndfile + ffmpeg bit-exact; MP3 ffmpeg + mpg123 + libsndfile aynı (< 1e-4); RF64 (EBU 3306): W (bekleniyor) | |
| R9 Arşiv + ağsız derleme | W `offline` işi (c0515a9): JUCE 9.0.4 arşiv klasöründen, `PatchJuce.cmake` bir kez uygulandı, ikinci çalıştırmada "already applied" (tekrarlanabilir); `FETCHCONTENT_FULLY_DISCONNECTED=ON` + ölü proxy ile derleme: build_ok, hata 0, uyarı 8 (temel), `juce_patch_markers: 3`; configure günlüğünde indirme yok; birim testleri "All tests completed successfully"; motor modları quality / latency / devices / record (20 PASS) / startxrun çıkış 0. Arşiv paketi: `tools/arsiv_olustur.sh` (deneme: 37 MB, 6329 dosya) | BAŞARILI (paketin kendisi teslimde) |
| R10 Güvenlik yazılımı | (bekleniyor) | |
| R11 Windows sürümleri | U: Windows 10 Pro 22H2 19045.7725; A: Windows 11 Enterprise 26200; W: Server 2025 | bilgi |

## 6. Windows 10 hakkında

Görev Windows 11 x64 içindi; kullanıcının ana bilgisayarı Windows 10 Pro 22H2 ve kullanıcı testlerin orada yapılmasını istedi. Windows 11'e özgü U11 maddeleri yapılmadı. Windows 11 (ARM64, 26200) CI'da (A) test edildi. Microsoft, Windows 10 ev kullanıcıları için güvenlik güncellemelerini (ESU) 12 Ekim 2027'ye kadar uzattı.

## 7. Uzun ömür değerlendirmesi

(bekleniyor)

## 8. Kullanıcının yapması gerekenler

(bekleniyor – kılavuzla birlikte)
