# MicVST 1.2.2 – Kapsamlı test raporu (Windows)

> Teslim – 23 Ekim 2026 akşam. Teslim edilen exe: tam tur #3'ün exe'si (commit `baf4efd`), SHA-256 `5F70A33F5408D49DA053B7B7252803018656E517E68A240CABC44C02537D7798`, 9 987 072 bayt. Kod dondurma 16:00; sonrasında yalnız belge ve test betikleri değişti.

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

Son kod (`baf4efd`) üzerinde tam tur #3 (W, 23 Ekim 12:56–16:28 UTC) ve uzun koşular (tur #2, `c0515a9`: ürün kodu teslim edilenle aynı; fark yalnız test programına eklenen `MonotonicPump.cpp`).

| Alan | Durum | Ortam | Not |
|---|---|---|---|
| A Derleme / exe | BAŞARILI | W, A | build_ok, hata 0, uyarı 8 (temel, değişmedi), JUCE uyarısı 0, yama işareti 3; A (Windows 11 ARM64) derleme + birim testleri |
| B Birim testleri | BAŞARILI | W, A, L | W: 243 bölüm "All tests completed successfully"; A: aynı, 833 s; L: ASan/UBSan 0 hata, 0 sızıntı (B2) |
| C Ses kalitesi / bütünlük | BAŞARILI (1 ATLANDI) | W, A, L, U | Bütünlük 24 döküm: 21'inde açıklanamayan 0; `s_suite_wrongref` 541 = negatif kontrol (bulmalı ✓); `i_mt_vst3` 395 = Airwindows zamanla değişen eklenti, çevrimdışı referansla karşılaştırılamaz → ATLANDI (bulgu #10); `i_voice_pluginstall` artık 0. Sinüs −0,1 dBFS: seviye 0,0000 dB, THD+N medyan −134,5 dB, süreksizlik 0; sessizlik girişi → çıkış tam 0. C7 rtprobe (L): ses iş parçacığında bellek ayırma 0, bekleyen kilit 0. U3/U4: gerçek kayıtlar kusursuz |
| D Saat farkı / tampon / gecikme | BAŞARILI | W, S | quality: ±150 ppm THD+N ≤ −102 dB, tık 0; ölçümler bölümü: 1.2.1 tasarımı 11 senaryonun 10'unda tık 0 (6. senaryo: bütün iş parçacıklarını donduran sistem – kaçınılmaz, eski tasarımlarla aynı); simüle gün: MicVST kaynaklı tık 0 |
| E Cihazlar / hatalar | BAŞARILI | W | devices, hotplug, openfail, faults, msgblock: FAIL 0; bozuk ayar / önbellek, eksik cihaz-eklenti-klasör ile açılış: çalışıyor (runtime) |
| F Dahili efektler | BAŞARILI | W, L | effects: kanal-nötr zincirler L/R 0,00 dB, tık 0; birim 52 bölüm |
| G Otomatik Kazanç | BAŞARILI | W, S | autogain: tık 0, kapatınca 2 s sonra kazanç 0,000000 dB; R2: 168 sa soak, sınır dışı 0 |
| H Hazır ayarlar | BAŞARILI | W | birim "Presets" 7 bölüm; U8 (dinleme) ATLANDI (kullanıcı kararı) |
| I Sustur / Ham ses / kısayollar / bildirim sesleri | BAŞARILI (not) | W, U | cues: 10 PASS (kabloda Sustur tık 0, varsayılan çıkış kabloysa ses yok); kısayollar (gerçek exe) 20 PASS; U6/U7. I6 tepsi: kullanıcı beyanı. Not: kulaklıkta hızlı aç-kapada bildirim sesinde tık (bulgu #14) |
| J Kendini dinle / Deneme kaydı | BAŞARILI | W | motor testleri (gecikme, edits) FAIL 0 |
| K Kayıt | BAŞARILI | W, L, U | record 20 PASS; K14 (disk dolu / çıkarma) ve K22 (760 dosya, 0 hata, 17,6 dk) geçti; 24/24 dosya ffmpeg ve Windows ile açıldı; R3 320 dk; U4 60 dk; U5 zorla kapatma |
| L İşlem önceliği | BAŞARILI | W, U | priority 16 PASS, öncelik denetimi hatası 0; R4: 20 sa Normal okundu. U13 ATLANDI |
| M VST3 | BAŞARILI (not) | W | vst3suite 116 PASS, FAIL 0 (Airwindows "aynı ses" karşılaştırmaları ATLANDI: zamanla değişen eklenti); gerçek eklenti penceresi açma/kapama tık 0 |
| N Arayüz / dil / tema | BAŞARILI | W, U | 202 ekran görüntüsü / metin (Türkçe yazı tipleri); tema/dil titremesi düzeltildi, kullanıcı doğruladı; U15'te son exe ile 6 dil + 5 tema değişimi: askı yok |
| O Kapalı kutu / gizlilik / kalıcılık | BAŞARILI | W | exe'de ağ kütüphanesi yok (WinINet/WinHTTP/Winsock/URLMon); 6 senaryoda ağ etkinliği 0 (WFP 0, soket 0, ProcMon 0); masaüstü betikleri ve kısayollar geçti; R6 ayar dosyası 1000 aç-kapa / 200 öldürmede bozulmadı |
| P Uzun süre | BAŞARILI | W, A, S | tur #2 uzun: long 330 dk tık 0 / boşluk 0, bellek eğimi 0,00 MB/sa; chaos 330 dk açıklanamayan 0; gerçek VST3 zinciri 330 dk × 3 öncelik açıklanamayan 0; gerçek exe 330 dk tutamaç 390–406, özel bellek eğimi 0,061 MB/sa, temiz kapanış; A (Win 11 ARM) 60 dk tık 0 |
| R Uzun ömür | BAŞARILI (R7 ATLANDI) | W, S, L, U | bkz. §5 |
| U Gerçek donanım | bkz. §4 | U | |

### Tam tur #3 (W, baf4efd – teslim edilen kod)

- Derleme ve birim testleri: yukarıda. Exe SHA-256 = teslim edilen.
- R5 (saat değişimi, düzeltilmiş test pompasıyla): kayıt 420,000 s, geçen 420,004 s, kabloda tık 0, düşen kare 0, dosya doğrulandı.
- R10: Defender (imza 23 Ekim) taraması tespit 0.
- Motor testleri, bütünlük, kapalı kutu, kısayollar, masaüstü betikleri: yukarıdaki tabloda; FAIL 0.

### Tam tur #2 (W, c0515a9, 23 Ekim 02:16–06:00 UTC) – ara sonuç

- Derleme: build_ok, hata 0, uyarı 8 (temel), JUCE uyarısı 0, yama işareti 3; exe 9 987 072 bayt.
- Birim testleri: 243 bölüm, "All tests completed successfully".
- Kayıt: diğer programlarla açılış (ffmpeg hata 0; Windows 24/24 dosyada uzunluk gösterdi), K14 (260 MB VHD: dolunca kendiliğinden durma, < 100 MB'ta başlamama, kayıt sürerken disk çıkarılınca hata mesajı – askıda kalma yok) ve K22 (760 dosya, 0 hata, 17,6 dk): BAŞARILI.
- Motor testleri: tüm modlar FAIL 0, çıkış 0; vst3suite artık Airwindows'ta "ATLANDI (zamanla değişen)" diyor.
- Bütünlük (24 döküm): 21'inde açıklanamayan 0; `s_suite_wrongref` 539 (**negatif kontrol, bulgu üretmesi beklenir** ✓); `i_mt_vst3` 405 (Airwindows, zamanla değişen eklenti – çevrimdışı referansla karşılaştırılamaz); `i_voice_pluginstall` 1 – **açıklandı**: kasıtlı 200 ms eklenti takılması senaryosunda, input-skip eklemesinin kendisi (tık 8,525 s; analizin bu ekleme için tahmin ettiği yer 8,47–8,48 s, açıklama penceresinin dışında). Aynı senaryo yerelde (L) çalıştırıldı: ekleme 8,52 s'de bulundu, tık ona bağlandı, açıklanamayan 0. Ek tur #2b'de aynı yerde aynı sonuç (belirlenimci). Ürün davranışı tasarım gereği: 200 ms takılmada çapraz geçişli atlama.
- Kapalı kutu: ağ kütüphanesi yok; çalışma anında 6 senaryoda ağ etkinliği 0 (WFP 0, soket 0, ProcMon 0); kısayollar (gerçek exe) ve masaüstü betikleri: tüm denetimler geçti.
- **R5 (saat değişimi): ENGELLENDİ** – test programı askıda kaldı. Neden ürün değil test düzeneği: motor testinin mesaj pompası JUCE `runDispatchLoopUntil` kullanıyordu ve bu, süreyi duvar saatiyle ölçüyor (saat 1 gün geri alınınca 1 gün bekler). İlk 3 dakikada (+1 sa, −1 sa sonrası) kayıt süresi = geçen süre, olay 0. MicVST'nin kendisi bu çağrıyı kullanmıyor (sonsuz döngü + monoton zamanlayıcılar; duvar saati yalnız dosya adı, log ve BWF tarihi). Düzeltme: pompa performans sayacıyla (`MonotonicPump.cpp`); tur #3'te yeniden ölçülecek.

## 2. Bulunan hatalar (önem sırasıyla)

| # | Önem | Bulgu | Neden | Düzeltme | Yeniden test | Durum |
|---|---|---|---|---|---|---|
| 1 | Orta | Her başlangıçta durum satırında **"kesinti 1"** | WASAPI, her `Start` sonrası ilk pakete DATA_DISCONTINUITY koyuyor (QuadCast 2 S ve VB-Cable'da ölçüldü: her zaman paket 0, 4/4 başlangıç) | Yalnızca her başlangıcın ilk bloğundan sonraki kesintiler sayılıyor | Motor testi `startxrun` (başta 0, araya enjekte 1, cihaz yeniden açılınca 0) | Düzeltildi |
| 2 | Düşük (ölçülebilir, duyulamaz) | 24 / 32 bit tamsayı kayıtta yuvarlama **yanlılığı** (+0,157 LSB ortalama ≈ −158 dBFS DC) | Tam ortadaki değerler hep yukarı yuvarlanıyordu (|x|>0,5 float'ta 24 anlamlı bit) | En yakına, eşitlikte çifte (`std::nearbyint`) | Birim "Recorder K22": ortalama −0,0001 LSB, her örnek en yakın değer | Düzeltildi |
| 3 | Düşük (görsel) | Tema / dil değişince Ayarlar penceresi bir an "hayalet" gibi görünüyordu | Yeni pencere önce varsayılan yerde açık başlıkla gösterilip sonra taşınıyordu | Konum, görünüm ve koyu başlık gösterilmeden önce; eski pencere yenisi açılınca kapanıyor | Kullanıcı hızlı derlemeyle denedi: "düzelmiş" (23 Ekim 05:28) | Düzeltildi (kullanıcı bulgusu) |
| 4 | Düşük | Mikrofon açılamazken bilgisayar > 24,8 gün uykuda kalırsa yeniden deneme 24,8 güne kadar bekleyebiliyordu | 32 bit ms sayacında işaretli fark | İşaretsiz fark (`micvst::ReopenBackoff`) | Birim "Counters R1" (önce başarısız) | Düzeltildi |
| 5 | Düşük (görsel) | Spektrum göstergesi 24,9 saatte bir bir an boş | Örnek sayacı 32 bit | 64 bit | Birim: 2^32 örnek beslenir (önce başarısız) | Düzeltildi |
| 6 | **Kritik değil, ama görünür** | Zincirde **Clear** varken kapanışta Windows olay günlüğüne "MicVST.exe çöktü" (0xc0000005, modül bilinmiyor). R4'te aynı imza bir kez de tepsiden hazır ayar seçilince (Clear zincirden çıkarken) düştü; MicVST çalışmaya ve ses vermeye devam etti | Eklenti kapatılırken (Clear) erişim ihlali; MicVST'nin çıkış kodu 0, ayarlar değişmiyor | Denenen "kapanışta süreci hemen bitir" düzeltmesi **işe yaramadı** (yeni exe ile 9 kapanışın 8'inde yine olay) → **geri alındı** | U: 10'da 8 (eski), 9'da 8 (denenen) | **Düzeltilmedi** (kullanıcı kararıyla ayrıntılı inceleme yapılmadı) |
| 7 | Düşük | Kendi zinciri tepsiden hazır ayar seçilince değişir (beklenen davranış, ama kullanıcı fark etmeyebilir) | – | Kılavuza not | – | Bilgi |
| 8 | Düşük | İmzasız exe ilk açılışta SmartScreen uyarısı (Zone.Identifier) | Kod imzası yok | – (imza dışarıda) | U1 | Bilgi |
| 9 | Üçüncü taraf | libsndfile 1.2 (Python `soundfile`) MP3'ü **parça parça** okurken her okuma sınırından sonra ~1600 örneği yanlış çözüyor | libsndfile hatası (ffmpeg ile yapılmış MP3'te de aynı) | MicVST'de değil; R8 betiği MP3'ü bütün okur | `tools/r8_standart.py` | Bilgi |
| 10 | Test altyapısı | Airwindows Consolidated (CI'nın indirdiği gece sürümü 1.2026.291) sesi kendiliğinden değişen bir efektle açılıyor → 4 vst3suite + 1 bütünlük karşılaştırması BAŞARISIZ görünüyordu | Eklenti modülasyonlu (aynı ayarla ardışık iki ölçüm 10,5 dB farklı; ZL EQ 0,04, DC1A3 0,06, SlickEQ 0,14) | vst3suite: > 1 dB ise "aynı ses" karşılaştırmaları ATLANDI olarak raporlanır | tur #2 ve #3: vst3suite FAIL 0 | Test düzeltildi; bütünlükte `i_mt_vst3` (395) çevrimdışı referansla karşılaştırılamaz → ATLANDI; aynı zincir atlanmış (bypass) hâlde açıklanamayan 0 |
| 11 | Test altyapısı | Yeni `RecordLong.inc`'te `near` değişkeni → MSVC derlemesi durdu (tur #2 ilk deneme) | `near`, windows.h'de makro | `isNear` | tur #2 yeniden | Düzeltildi |
| 13 | Açıklandı – ürün hatası değil | R4'te Windows 2 kez "MicVST yanıt vermiyordu ve kapatıldı" (Application Hang 1002, "Top level window is idle") kaydetti: 02:25:59 ve 02:52:31 (PC saati), ikisi de eski exe | MicVST'yi **Görev Yöneticisi → Görevi sonlandır** ile kapatmak bu kaydı üretiyor: Görev Yöneticisi önce pencereyi kapatmayı dener, MicVST tasarım gereği tepsiye gizlenir, süreç bitmeyince Görev Yöneticisi onu zorla bitirir ve Windows bunu "askı" diye yazar | Gerekmez | Kullanıcının PC'sinde son exe ile yeniden üretildi (23 Ekim 17:52:35 PC saati): yanıt veren MicVST Görev Yöneticisi'nden sonlandırıldı → aynı olay, aynı askı türü. 16:29:46'daki olay da kullanıcının Görev Yöneticisi ile kapattığı ana denk. Tepsiden Kapat ile son exe'de 4 kapanış: askı 0. R4 son süreç 12 sa: askı 0 | Kapandı (açıklandı) |
| 14 | Düşük (yalnız kulaklık) | Sustur hızlı açılıp kapatılınca (saniyede 2–4) **kulaklıkta** bildirim sesinde "çıt" (kullanıcı, U15) | Sesler Windows `PlaySound` ile çalınıyor; yeni ses eskisini dalganın ortasında kesiyor. Ahşap 1,3 s sürüyor: ilk 0,5 s içinde kesilince −13…−30 dBFS'lik ani düşüş (dosyadan hesaplandı) | 1.2.2'de yok (kod dondurma). Geçici: kısa ses (Kıvılcım 0,29 s / Kristal 0,10 s) + %40–50. Ayrı dalda deneme (`kararlilik/bildirim-sesi`): sesleri MicVST kendisi çalıyor, eski ses 10 ms'de kısılıyor; tüm sesler aynı yükseklikte | Kabloya etkisi yok: cues "Sustur on the cable: no clicks" PASS, U6. Deneme dalı birim testi: saniyede 10 aç-kapada en büyük sıçrama 912 (seslerin kendisi 1319; kesme 6048) | 1.2.2'de açık; düzeltme deneme dalında |
| 12 | Düşük | Ayar / önbellek / log yazılırken süreç öldürülürse geçici dosya kalıyor (R6: 200 öldürmede 6 dosya) | JUCE `writeTo` geçici dosya + değiştirme; öldürülünce geçici dosya silinmiyor | Düzeltilmedi (zararsız; ayar dosyası hiç bozulmadı) – öneri: açılışta eski `*_temp*.xml` dosyalarını sil | R6 | Bilgi |

## 3. Notlar ve açıklamalar

- **Kullanıcının zinciri ağır:** MicVST bir çekirdeğin ~%45'ini kullanıyor (U12); log'da ara sıra "İşleme: N/100 blok gerçek zamandan uzun sürdü" (eklenti yükü tepe %100–153). 60 dk kayıtta (U4) kabloda kesinti/ekleme/kesme yok; köprü saat farkı düzeltmesini en çok 148 ppm (0,26 cent) hızlandırıyor – duyulamaz. Gösterilen gecikme ~288 ms (Pro-Q 4 lineer faz 106,7 ms + diğerleri + güvenlik payı).
- **Thread / tutamaç sayısı:** U4 sırasında MicVST'nin iş parçacığı sayısı 33 → 69, tutamaç 611 → 798 arttı ve kayıt bitince düşmedi; aynı anda UI Automation (test betiği) bağlandı. CI'da (UIA yok) gerçek exe 330 dk: iş parçacığı 9, tutamaç 387-394 sabit. Büyüme yalnızca bir kez, sonra sabit – sızıntı değil; büyük olasılıkla UIA sağlayıcısı + eklentiler. R4 son süreci (UIA bağlanmadan, ~12 sa): tutamaç 606–620, iş parçacığı 30–35, sabit.

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
| U14 Uzun kullanım (R4) | Bkz. R4 ve `docs/kanit_win11/R4_gercek_kullanim.md`: 20 sa izleme; son süreç ~12 sa kesintisiz, bellek +4 MB sonra sabit, tutamaç/GDI/USER/iş parçacığı sabit, öncelik Normal, pencere hep yanıt verdi, log'da kesinti satırı 0. Son 30 s kontrol kaydı alınamadı (pencere tepsideydi) – U15 kaydı yerine geçer | sızıntı yok, çökme/askı yok | BAŞARILI (son süreç); eski exe'de 2 askı kaydı → bulgu #13 (Görev Yöneticisi ile kapatmanın izi, açıklandı) |
| U15 Sağlık kontrolü provası | Son exe (`5F70A33F…`) ile, kullanıcı kılavuzu izleyerek: açılış, "Ses biçimi" satırı (giriş 32 bit, çıkış 24 bit, 48 kHz – test edilenle aynı), Sustur sesi duyuldu, 6 kayıt (1,6–24,6 s) "dosya doğrulandı"; 24,6 s ve 19,2 s kayıtlar incelendi: boşluk 0, kırpma 0, 24 bit gerçek %99,6, tık adayları yalnız sessiz anlarda −50 dB (düğme tıkları / ünsüzler). 6 dil + 5 tema değişimi: askı yok. Tepsiden Kapat → aç 4 kez: askı 0 (3 kapanışta bilinen Clear kaydı, #6). Kullanıcı: "herşey çalışıyor". Kulaklıkta hızlı aç-kapada bildirim sesinde tık → bulgu #14 | kılavuz uygulanabilir | BAŞARILI |
| I6 Tepsi menüsü | Kullanıcı tüm öğeleri denedi: "sorunsuz" | | BAŞARILI (kullanıcı beyanı, ölçülmedi) |

## 5. Uzun ömür testleri (R)

| Test | Ölçülen | Durum |
|---|---|---|
| R1 Sayaç taşmaları | Tüm `getMillisecondCounter` kullanımları incelendi (AudioEngine ×3, CueSounds, Recorder ×2, PluginListView ×2, ScanCoordinator ×2); birim "Counters R1": kayıt başlığı, bildirim sesi önbelleği, cihaz yeniden deneme sayaç başa dönerken doğru; 2 hata düzeltildi (bkz. §2 #4, #5); sıra sayaçları (`pushSeq`, `stampSeq`) yalnız eşitlik/çiftlik ile karşılaştırılıyor (2^32 çift → taşmada doğru); örnek konumları 64 bit / double; efekt fazları double + sarmal | BAŞARILI |
| R2 30 gün simülasyon + 7 gün Otomatik Kazanç | S (uzun tur #2): 720 sanal saat, mikrofon saati ±300 ppm rastgele yürüyüş + 6 saatte bir sıçrama, binlerce kablo / sistem / eklenti takılması: **MicVST kaynaklı tık 0, diğer 0** (11 560 tık yalnız kasıtlı takılmalardan); öğrenilen saat farkı −165,9 ppm (gerçek −165,6 → hata 0,3 ppm, ölçüt ≤ 2); bellek tepe 11 MB; 2 sa 5 dk sürdü. Otomatik Kazanç 168 sa ses (57 milyon blok, 11 333 bozuk blok, 2 795 aç/kapa): −10 dBFS üstü örnek 0, geçersiz örnek 0, kazanç 0…+6 dB dışı 0, iç sayaç denetimi hatası 0 | BAŞARILI |
| R3 320 dk gerçek zamanlı kayıt (WAV 24 → RF64, FLAC 24, MP3 V0) | W (uzun tur #2), her biçim 320 dk = 921 600 000 kare: kayıp 0, düşen kare 0, süre farkı 0,009 / 0,017 / 0,081 s (%0,00005 / 0,00009 / 0,00042); bellek eğimi −0,09 / −0,18 / −0,16 MB/sa; kabloda açıklanamayan tık 0. WAV 5,15 GiB → **RF64** (4 GB sınırını ~4 sa 8 dk'da gerçek zamanda geçti); WAV ve FLAC'ta **921,6 milyon karenin her biri mikrofonun örneğiyle birebir aynı** (fark 0, zaman kayması 0); MP3: LAME etiketi uzunluğu = kaydedilen, her saniyede ton (kalıntı < −40 dB) | BAŞARILI (330 yerine 320 dk: GitHub 6 sa iş sınırı) |
| R4 Gerçek kullanım | 22 Ekim 21:53 → 23 Ekim 18:04 (gerçek saat) = **20 sa** izleme, 121 örnek. İlk süreç ~6 sa 55 dk (U5'te bilerek öldürüldü); **son süreç ≥ 11 sa 59 dk kesintisiz** (hâlâ çalışıyor): özel bellek 248,5 → 252,7 MB (artış ilk saatlerde, sonra ±0,1), tutamaç 606–620, GDI 50, USER 45–47, iş parçacığı 30–35, CPU bir çekirdeğin %47'si, öncelik 72/72 Normal, pencere 72/72 yanıt verdi, olay günlüğünde olay 0. log.txt 20 sa: köprü kesintisi / atılan blok / atlanan ses / bozuk blok satırı **0**; "blok gerçek zamandan uzun sürdü" 10 841 satır (güvenlik payı emdi; çoğu kullanıcı bilgisayarı kullanırken, gece ~0). Olay günlüğü: Clear'lı hazır ayar değişiminde 1 çökme kaydı (MicVST çalışmaya devam etti, bulgu #6), 2 askı kaydı (bulgu #13: Görev Yöneticisi ile kapatma, açıklandı). Son 30 s kontrol kaydı: ENGELLENDİ (pencere tepsideydi). 72 sa – 7 gün: ATLANDI (süre) | BAŞARILI (≈ 20 sa; kısıtlar yanda) |
| R5 Saat değişimi | W (ek tur #2b, 93cb4b7): 7 dk WAV 24 kayıt sürerken saat +1 sa, −1 sa, +1 gün, −1 gün, saat dilimi Tokyo ve geri: kabloda tık 0; kayıt 420,000 s, geçen süre 420,004 s (fark %0,001 – süre örnek sayısıyla tutarlı, duvar saatinden bağımsız); 20 160 000 karenin her biri mikrofonla birebir; dosya doğrulandı, düşen kare 0; adı başlangıç saatinden, üzerine yazma yok (birim: var olan ad → `_2`). Tur #2'deki askıda kalma test düzeneğindeydi (bkz. tur #2 notu), düzeltildi | BAŞARILI |
| R6 1000 aç-kapa / 200 öldürme | W, gerçek MicVST.exe (c0515a9): 800 normal kapanış (WM_QUERYENDSESSION, çıkış 0) + 200 `taskkill /f` (ilk 2 s içinde, rastgele an); çökme 0, askıda kalma 0, okunamayan config 0, kaybolan ayar (3 işaret) 0; WER/Application Error olayı 0; pencereye kadar medyan 261 ms, %95 624 ms, en çok 3,1 s. Not: öldürülen yazmalardan 6 geçici dosya kaldı (`config_temp*.xml` ×2, `plugin_cache_temp*.xml` ×2, `log.txt~RF*.TMP` ×2) – zararsız, birikir (bulgu #12) | BAŞARILI |
| R7 Ortam değişiklikleri | – | ATLANDI (kullanıcı kararı / süre) |
| R8 Okunabilirlik | L (kısa dosyalar) ve W (320 dk dosyalar): WAV/BWF EBU 3285 (bext 602+, tarih/saat biçimi, sürüm, ayrılmış baytlar 0) ✓; RF64 EBU 3306 (boyut alanları 0xFFFFFFFF, ds64 ilk, RIFF ve data boyutları birebir; sampleCount 0 – PCM'de fact öbeği yok, kural gereği) ✓; FLAC RFC 9639 (STREAMINFO toplam örnek = kaydedilen, çözülen sesin MD5'i = STREAMINFO MD5) ✓; MP3 ISO 11172-3 (800 002 çerçevenin hepsi geçerli, uçtan uca bitişik) + Xing/LAME uzunluğu birebir ✓; okuyucular: libsndfile + ffmpeg PCM'de bit-exact, MP3 ffmpeg + mpg123 + libsndfile aynı (< 1e-4). Not: libsndfile 1.2 MP3'ü parça parça okurken hatalı (üçüncü taraf, bulgu #9) | BAŞARILI |
| R9 Arşiv + ağsız derleme | W `offline` işi (c0515a9): JUCE 9.0.4 arşiv klasöründen, `PatchJuce.cmake` bir kez uygulandı, ikinci çalıştırmada "already applied" (tekrarlanabilir); `FETCHCONTENT_FULLY_DISCONNECTED=ON` + ölü proxy ile derleme: build_ok, hata 0, uyarı 8 (temel), `juce_patch_markers: 3`; configure günlüğünde indirme yok; birim testleri "All tests completed successfully"; motor modları quality / latency / devices / record (20 PASS) / startxrun çıkış 0. Arşiv paketi: `tools/arsiv_olustur.sh` (deneme: 37 MB, 6329 dosya) | BAŞARILI (paketin kendisi teslimde) |
| R10 Güvenlik yazılımı | W: Windows Defender (imza 1.461.160.0, 22 Ekim) ile exe'ye özel tarama: tespit 0 (koşucuda gerçek zamanlı koruma kapalı – yalnız istek üzerine tarama). U: ilk açılışta SmartScreen "tanınmayan uygulama" uyarısı (imzasız exe, internetten inmiş dosya – Zone.Identifier); kullanıcı "Yine de çalıştır" dedi. Defender istisnası eklenmedi, gerekmedi | BAŞARILI (not: imza yok) |
| R11 Windows sürümleri | U: Windows 10 Pro 22H2 19045.7725; A: Windows 11 Enterprise 26200; W: Server 2025 | bilgi |

## 6. Windows 10 hakkında

Görev Windows 11 x64 içindi; kullanıcının ana bilgisayarı Windows 10 Pro 22H2 ve kullanıcı testlerin orada yapılmasını istedi. Windows 11'e özgü U11 maddeleri yapılmadı. Windows 11 (ARM64, 26200) CI'da (A) test edildi. Microsoft, Windows 10 ev kullanıcıları için güvenlik güncellemelerini (ESU) 12 Ekim 2027'ye kadar uzattı.

## 7. Uzun ömür değerlendirmesi

**Hangi koşullarda, ne kadar süre doğrulandı** (bu teslimdeki ölçümler; mutlak garanti değildir):

| Koşul | Süre / kapsam | Sonuç |
|---|---|---|
| Kullanıcının PC'si, gerçek mikrofon + VB-Cable + kendi ağır zinciri | R4: 20 sa izleme; son süreç ~12 sa, ilk süreç ~7 sa kesintisiz; U4 60 dk kayıt | Bellek ve tutamaç sabit, log'da ses kesintisi 0, kayıt kusursuz; askı yok (2 kayıt Görev Yöneticisi ile kapatmanın izi, bulgu #13) |
| Gerçek motor + sanal cihazlar, Otomatik Kazanç | 330 dk × 2 (long, chaos – zincir düzenlemeleri ve cihaz değişimleri dahil) | Açıklanamayan tık 0, boşluk 0, bellek eğimi 0 |
| Gerçek VST3 zinciri, tüm CPU'lar başka programla meşgul | 330 dk × 3 öncelik | Açıklanamayan tık 0, bellek eğimi ≈ 0 |
| Gerçek MicVST.exe penceresi açık | 330 dk | Bellek, tutamaç, GDI/USER, iş parçacığı sabit; kapanış temiz |
| Simülasyon (köprü, saat kayması ±300 ppm, takılmalar) | 1 hafta (tur #1), 30 gün (tur #2) | MicVST kaynaklı tık 0; saat farkı hatası 0,3 ppm |
| Otomatik Kazanç soak | 24 sa (tur #1), 7 gün (tur #2) | −10 dBFS üstü örnek 0, geçersiz örnek 0 |
| Gerçek zamanlı uzun kayıt | 320 dk WAV 24 (4 GB'ı geçip RF64), FLAC 24, MP3 V0 | Kayıp 0; WAV/FLAC her kare mikrofonla birebir |
| 1000 aç-kapa, 200 öldürme | 21 dk | Ayarlar hiç bozulmadı |
| 49,7 gün sayaç dönüşü | Birim testiyle (sayaç dönüşün hemen öncesinden başlatılarak) | Zamanlayıcılar doğru; 2 küçük hata düzeltildi |
| Saat / saat dilimi değişimi | Kayıt sürerken ±1 sa, ±1 gün, saat dilimi (tur #2b ve #3) | Tık 0, süre duvar saatinden bağımsız |
| Ağsız yeniden derleme | Arşivdeki JUCE ile | BAŞARILI |

**Kalan riskler ve her birinde ne yapılır** (ayrıntı: `docs/UZUN_OMUR_KILAVUZU.md`):

- **Windows büyük güncellemesi / Windows 11'e geçiş:** Ses aygıtı biçimleri ya da varsayılan aygıtlar değişebilir. → 10 dakikalık sağlık kontrolü ("Ses biçimi" satırı, kesinti, 30 s kayıt). Windows 10 ESU 12 Ekim 2027'de bitiyor.
- **Ses sürücüsü / VB-Cable güncellemesi:** Kablonun biçimi değişebilir. MicVST farklı hızlarda da doğru çalışıyor (44,1 ↔ 48 kHz seviye farkı < 0,01 dB, C5). → Sağlık kontrolü; test edilen düzen 24 bit / 48 kHz. Kurulum dosyası 3.3.1.7'yi sakla.
- **Eklenti güncellemesi / kaldırılması:** Eksik eklentiyle MicVST açılır (O2). Kapanıştaki Clear olayı Clear güncellemesiyle değişebilir. → Güncellemeden önce çalışan sürümün kurulumunu sakla; sonra sağlık kontrolü.
- **Donanım değişimi (mikrofon, USB yuvası):** Aynı adla geri gelen cihaz kendiliğinden bağlanıyor (E1, W). Gerçek USB çıkar-tak bu teslimde kullanıcı kararıyla yapılmadı (U10 ATLANDI).
- **Ağır zincir + başka yüklü programlar:** Ara sıra gerçek zamanı aşan bloklar ölçüldü. Köprü bunları kullanıcının 80 ms güvenlik payıyla emiyor (U4, R4: ses kesintisi 0). Güvenlik payını düşürmemek gerekir; sık olursa zinciri hafifletmek (ör. Pro-Q 4 doğal faz).
- **Kod imzası yok:** SmartScreen uyarısı her yeni exe'de çıkabilir. Exe'yi SHA-256 ile doğrula.

## 8. Kullanıcının yapması gerekenler

1. Teslim edilen exe'yi `C:\MicVST\MicVST.exe` yap. Öncekini `MicVST_onceki.exe` olarak sakla. SHA-256'yı `Get-FileHash` ile doğrula.
2. Kılavuzdaki yedekleri al: exe + SHA, `config.xml`, `Sesler`, hazır ayar dosyaları, arşiv zip'i. En az iki yere.
3. Kendi zincirini hazır ayar olarak kaydet ve `.micvstpreset` olarak dışa aktar.
4. Büyük Windows / sürücü / VB-Cable / eklenti güncellemesinden sonra ve ayda bir 10 dakikalık sağlık kontrolü.
5. Windows 10 ESU kaydını kontrol et (güvenlik güncellemeleri 12 Ekim 2027'ye kadar).
