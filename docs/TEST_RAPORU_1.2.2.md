# MicVST 1.2.2 – Test Raporu

Bu rapor `kararlilik/oncelik` dalındaki 1.2.2 çalışmasının testlerini belgeler: ne değişti, neyi
hangi ortamda nasıl ölçtüm, sonuç ne çıktı ve neyi **ölçemedim**. Her sonucun yanında ortamı
yazılıdır; ortamlar birbirinin yerine kullanılmamıştır. Gerçek ses donanımıyla (QuadCast, VB-Cable,
gerçek Windows ses sürücüsü) hiçbir test yapılamadı – bunlar **ENGELLENDİ** olarak işaretlidir.

Durumlar: **BAŞARILI** (test edildi, geçti) · **BAŞARISIZ** (test edildi, geçmedi) · **ATLANDI**
(bilerek çalıştırılmadı) · **ENGELLENDİ** (bu ortamda yapılamaz). Kesin garanti verilmez: testler
gösterilen koşullarda geçmiştir.

Önceki rapor: [TEST_RAPORU_1.2.1.md](TEST_RAPORU_1.2.1.md).

## Ortamlar

| Kısa ad | Ortam |
|---|---|
| **W** | GitHub Actions `windows-latest` (Windows Server 2025, x64, 4 çekirdek, ses aygıtı yok), MSVC 19.51 |
| **A** | GitHub Actions `windows-11-arm` (Windows 11 Enterprise, ARM64) |
| **L** | Linux konteyner (2 çekirdek, gürültülü: sanal aygıt iş parçacıkları zaman zaman gecikir) |
| **S** | Simülasyon (sanal zaman, deterministik) |
| **San** | L üzerinde AddressSanitizer + UndefinedBehaviorSanitizer derlemesi |

Sanal aygıtlar: motor testleri (`MicVSTEngineTest`) gerçek `AudioEngine`'i, gerçek iş parçacıklarıyla,
WASAPI benzeri sanal mikrofon / kablo / kulaklıkla çalıştırır (`tests/enginetest/VirtualDevices.h`).

## Özet

- **Kod:** `kararlilik/oncelik`, son kod commit'i `ecee2b6`. `main`'e dokunulmadı, PR/merge yok.
- **Teslim edilen `MicVST.exe`:** `ecee2b6`'nin Windows CI derlemesi, SHA-256 `{{SHA}}`.

| Alan | Durum | Ortam |
|---|---|---|
| Kayıt (WAV/FLAC/MP3) – biçimler, bit derinlikleri, hızlar, doğrulama | BAŞARILI | W, L |
| Kayıt – başka programlarda açılma (ffmpeg, Windows'un kendisi) | BAŞARILI | W, L |
| Kayıt – kabloya etkisi (tık, seviye, gecikme, kesinti) | BAŞARILI | W, L |
| Kayıt – bellek / sızıntı / hata ayıklayıcı | BAŞARILI | L, San |
| Kayıt – gerçek QuadCast + VB-Cable ile | ENGELLENDİ | – |
| Deneme kaydı çalınırken tık | BAŞARISIZ → düzeltildi → BAŞARILI | W, L |
| Kulaklık ses düzeyi hatası | BAŞARISIZ → düzeltildi → BAŞARILI | L, W |
| Otomatik Kazanç (düzeltilmiş tanım) | BAŞARILI | W, A, L, S |
| Kısayollar (klavye + fare, gerçek MicVST.exe) | BAŞARILI | W |
| Masaüstü yardımcıları (simge, öncelik kısayolları) | BAŞARILI (test aracı düzeltildi) | W |
| İnternetsiz çalışma (kapalı kutu) | BAŞARILI | W |
| Uzun koşular (5,5 sa ×3, Win11 ARM 60 dk, 168 sa simülasyon, 24 sa Otomatik Kazanç) | BAŞARILI | W, A, S |
| VST3 tarayıcı (gerçek eklentiler, 240 tarama) | BAŞARILI | W |
| Win11 x64 istemci, uyku/uyanma, ticari eklentiler, dinleme testi | ENGELLENDİ | – |

## 1. Bu turda yapılan değişiklikler

1. **Kayıt özelliği** (yeni; bölüm 2).
2. **İmza kaldırıldı:** "Relios tarafından geliştirilmiştir." ana pencerenin altından ve
   Ayarlar > Hakkında'dan silindi (10 dilin çevirisi dahil).
3. **Deneme kaydı tıkı düzeltildi** (bölüm 4.1).
4. **Kulaklık ses düzeyi hatası düzeltildi** (bölüm 4.2).
5. **Fare kısayolları** (orta tuş, yan tuşlar, Ctrl/Shift/Alt + tekerlek), **ses biçimi günlük
   satırı**, masaüstü yardımcıları (önceki ara teslimlerde verildi).

## 2. Kayıt özelliği

### 2.1 Seçenekler ve neden bunlar

Profesyonel programların (REAPER, Adobe Audition, Audacity) sunduğuna bakıldı; 32 bit kayan noktalı
bir konuşma sinyaline hiçbir şey katmayanlar konmadı.

| Biçim | Sunulan | Bilerek konmayan (neden) |
|---|---|---|
| WAV | 16 / **24 (varsayılan)** / 32 bit PCM, 32 bit float; Broadcast WAV (bext: MicVST, tarih-saat); 4 GB'ı geçince kendiliğinden RF64 | 8 bit (≈48 dB dinamik, gürültülü), 64 bit float (kaynak 32 bit float) |
| FLAC | 16 / 24 bit, sıkıştırma 0–8 (hepsi kayıpsız) | 32 bit (JUCE'un FLAC yazıcısı 24 bit ile sınırlı; çoğu oynatıcı açamıyor) |
| MP3 | LAME 4.0 (programa gömülü): CBR 128 / 160 / 192 / 224 / 256 / **320**, VBR V0 / V2 / V4 | 128 kbps altı (kalite önceliği); ABR (VBR'nin daha zayıf hâli) |
| Hız | 44,1 ve **48 kHz** her zaman; 88,2 / 96 / 176,4 / 192 kHz yalnızca mikrofon o hızda çalışıyorsa; MP3: 44,1 / 48 | 48 kHz sesi 96 kHz'e yükseltmek (kaliteye bir şey katmaz) |
| Kanal | Stereo, Mono ((L+R)/2) | – |

- **Varsayılan:** WAV 24 bit, 48 kHz, stereo, `Belgeler\MicVST Kayıtları`. Ayarlar `config.xml`'de
  saklanır (`recordFormat`, `recordFolder`).
- **Dosya adı:** `MicVST_YYYY-AA-GG_SS-DD-ss` (+ `_2`, `_3` …); var olan dosyanın üzerine asla yazılmaz.
- **Kütüphaneler:** WAV ve FLAC JUCE'un kendi kodu (libFLAC 1.5.0, BSD); MP3 LAME 4.0 (Temmuz 2026,
  LGPL; kaynak `third_party/lame-4.0`, SHA-256 Homebrew'daki ile aynı, CI'da denetlendi). İnternet
  kütüphanesi yok.
- **Kaydedilen ses:** kabloya giden işlenmiş ses (karşı tarafın duyduğu), dijital kopya: ses kartından
  geri kayıt yapılmaz.

### 2.2 Gerçek zamanlı güvenlik (tasarım)

- Ses iş parçacığı yalnızca kilitsiz bir tampona kopyalar (tahsis, bekleme, disk, kodlama yok).
- Ayrı bir yazıcı iş parçacığı (normalin üstü öncelik, ses iş parçacıklarının çok altında) dönüştürür,
  kodlar, yazar. 30 saniyelik tampon diskin takılmasını karşılar; taşarsa kayıp sayılır ve bildirilir.
- WAV başlığı her saniye yenilenir: program çökerse dosya son saniyeye kadar geçerlidir.
- Disk dolarsa (30 MB altı) ya da yazma hatası olursa kayıt durur, dosya kapatılır, mesaj gösterilir.
- Durdurunca dosya yeniden okunup denetlenir (biçim, hız, kanal, tam uzunluk; MP3: çerçeveler + LAME etiketi).

### 2.3 Sonuçlar

**Birim testleri (W, L) – BAŞARILI**

| Test | Sonuç |
|---|---|
| WAV 16/24/32 PCM, 32 float; FLAC 16/24 (sıkıştırma 0, 5, 8) geri okuma | 24/32 bit: en çok 0,500 LSB (doğru yuvarlama); 32 float: 0 (birebir); 16 bit: ≤1,49 LSB (TPDF dither) |
| 16 bit dither | hata ortalaması 0,001 LSB, rms 0,498 LSB (TPDF beklenen 0,5) |
| Taşma (+3,5 dBFS) | tam ölçekte kesilir, işaret dönmesi (wrap-around) 0 |
| Hız dönüştürme 48→44,1, 96→48, 96→88,2, 44,1→48 | sinyal/hata 136–148 dB, uzunluk birebir, zaman kayması yok |
| Dönüştürücü | geçirme bandı düz (20 kHz'e kadar), durdurma bandı ≥ 140 dB |
| MP3 CBR 128–320, VBR V0/V2/V4, stereo/mono, 44,1/48 kHz | 11 ayarın hepsi: örnek sayısı birebir (LAME etiketi), CBR bit hızı doğru, çözülünce doğru kanal ve sıra |
| Çökme anı | dosya son saniyeye kadar geçerli, içerik 0,5 LSB içinde |
| Tampon taşması | kayıp sayılır; yazılan + kayıp = gönderilen |
| Hatalar | ses yokken, klasör oluşturulamazken, ikinci başlatmada anlaşılır hata |
| 12 kez başlat/durdur | 12 dosya, hepsi sağlam, adlar farklı |
| 20 dk kayıt (30× hızda): WAV 24, FLAC 24, MP3 V0 | kayıp 0, doğrulandı |
| 4 GB üstü (RF64) | W: 4,25 GB, L: 4,7 GB; geri okundu; ffmpeg 3072 s'yi birebir ve hatasız açtı (L) |
| Ayarların kalıcılığı | config.xml'e yazılıp geri okununca aynı (birim + motor testi) |

Yazıcının hızı (bir çekirdek, W): WAV 24 bit 1608× gerçek zaman (%0,06 çekirdek), FLAC 5: 98×
(%1,0), MP3 320 CBR: 27–34× (%3–3,7), MP3 V0: 117× (%0,85).

**Başka programlarda açılma (W) – BAŞARILI:** 24 test dosyası (her biçim, bit derinliği, hız):
ffmpeg 9.0.2 hepsini hatasız, beklenen örnek sayısıyla çözdü; Windows'un kendi özellik okuyucuları
24 dosyanın 24'ünde süre ve bit hızını gösterdi.

**Motor testi `record` (W, L) – BAŞARILI** (gerçek motor, sanal aygıtlar, 997 Hz −20 dBFS, Kazanç −6 dB)

| Ölçüm | W ({{WREC}}) | L |
|---|---|---|
| Kabloda tık, kayıt yok / var (başlat-durdur dahil) | 0 / 0 | 0 / 0 |
| Kablo seviyesi kayıt yok / var | −26,00 / −26,00 dB | −26,00 / −26,00 dB |
| Kabloda taşma/atlama | 0 | 0 |
| Gösterilen gecikme yok / var | 55,4 / 55,4 ms | 55,4 / 55,5 ms |
| Dosya içeriği (32 float) | seviye −26,000 dB, THD+N −149,3 dB, tık 0, boşluk 0 | aynı |
| 30 başlat/durdur, 10 farklı biçimde | 30/30 doğrulandı, kabloda tık 0 | 30/30 |
| İşlem CPU'su, kayıtsız → WAV kaydı | %1,30 → %2,08 çekirdek | %3,7 → %3,9 |
| En ağır ayarlar (44,1'e dönüştürerek MP3 320 / FLAC 8) | %3,7 / %3,9 | %6,6 / %6,1 |
| Bellek, 30 kayıt sonrası | 30 → 35 MB (sızıntı yok) | 27 → 38 MB |

Not (L): 2 çekirdekli konteynerde sanal aygıtlar zaman zaman gecikir ("makine takılması"). Bu
takılmalar kayıt olmayan bölümlerde de oldu; tıklar hiçbir zaman başlat/durdur anında değildi (en
yakını 94 ms uzakta). Takılma olan bölümlerde karşılaştırma ölçülmüş ama sayılmamıştır.

**Hata ayıklayıcılar (San) – BAŞARILI:** kayıt birim testleri ve motor testi: bellek hatası 0,
sızıntı 0. UBSan yalnızca üçüncü taraf kodunda (JUCE'un libFLAC'ı, LAME) negatif sayıların bit
kaydırmasını bildirdi; MSVC ve GCC'de tanımlı davranış, sonuçlar doğru. San altında program ~10×
yavaşladığından 30× hızlı 20 dk testinde ve bellek ölçümünde beklenen sapmalar oldu (hata değil).

## 3. Kayıt geliştirilirken bulunan ve düzeltilen sorunlar

| # | Önem | Sorun | Düzeltme | Yeniden test |
|---|---|---|---|---|
| K1 | Orta | JUCE'un tamsayı yazıcısı örnekleri yuvarlamak yerine **kesiyordu**: 1 LSB'ye kadar hata ve −0,5 LSB kayma | yuvarlama MicVST'de; JUCE'a tam sayılar veriliyor | 24/32 bit: 0,500 LSB (W, L) |
| K2 | Orta | Yazıcı "düşük" öncelikteydi: Windows'ta verimlilik çekirdeği + düşük öncelik; FLAC 68× (Linux'ta ~300×); 30× stres testinde kayıp; yoğun yükte gerçek kayıpta da ses kaybettirebilirdi | normalin üstü öncelik, tampon 10 → 30 s | 20 dk testleri W'de kayıpsız; FLAC 98× |

## 4. Diğer bulunan ve düzeltilen hatalar (önem sırasıyla)

### 4.1 Deneme kaydı çalınırken başta ve sonda tık – Orta

- **Bulgu (W):** 10 saniyelik çalmada her saniye −38,00 dB, son saniye −37,28 dB (takılma yokken).
- **Neden:** kayıt çalındıktan sonra ses aniden sessizliğe geçiyordu (ve aniden başlıyordu): dalganın
  ortasında kesilen ses tıktır; kulaklık tarafındaki örnek hızı dönüştürücüsü bunu +0,7 dB taşmaya çevirir.
- **Düzeltme:** çalmanın başına ve sonuna 10 ms yumuşak geçiş.
- **Kanıt (L, sanal kulaklık yeni ölçüm: en büyük ani örnek sıçraması):** düzeltmesiz 0,0127 (sesin
  tam genliği = kesik), düzeltmeli 0,000214 (pürüzsüz sinüsün kendisi); son saniye −38,00 dB.
  Test artık bunu denetliyor. W: {{WFX}}

### 4.2 Kulaklık ses düzeyi hatası – Orta

- **Bulgu:** "Kendini dinle" düzeyi −12 dB'ye alınınca kulaklıkta −42,6 dB ölçülüyordu (beklenen −38).
- **Neden:** açma/kapama geçişinin yükseltilmiş kosinüs eğrisi düzeye de uygulanıyordu.
- **Düzeltme:** geçiş ve düzey ayrı; düzey 10 ms yumuşatmayla. **Yeniden test:** −38,00 dB (W, L).

### 4.3 VST3 uzun koşuda test aracının çökmesi (0xC0000409) – Düşük (yalnızca test aracı)

- **Bulgu (W):** gerçek VST3 eklentili uzun koşu üç öncelikte de ~11 s'de çöktü.
- **Neden:** test aracı eklentileri **kendi içinde** taradı; ücretsiz TDR VOS SlickEQ 1.3.9 tam CPU
  yükü altında kendi tarama kodunda çöktü. MicVST eklentileri ayrı bir süreçte tarar.
- **Kanıt:** MicVST'nin gerçek tarayıcısı 6 eklenti × 20 tarama × (boşta + tam yük) = **240/240**
  başarılı (W). Yük, zincir kurulduktan sonra başlatılınca 3 dk koşu geçti.
- **Durum:** düzeltilmiş 5,5 saatlik VST3 koşuları (3 öncelik) çıkış kodu 0 ile bitti – BAŞARILI (bölüm 7).

### 4.4 Test araçlarındaki hatalar (ürün değil)

- Masaüstü yardımcıları testi: runner'ın `%TEMP%`'i kısa 8.3 yol (`RUNNER~1`), Windows uzun yolu
  bildiriyor → betik doğru çalıştığı hâlde 4 denetim başarısız görünüyordu. Uzun yolla düzeltildi; W: 0 hata.
- 4 GB testi diskten hızlı veri basıyordu (75 MB/s); artık diskin aldığı hızda.
- Kayıt motor testinde kablo THD+N karşılaştırması sabit 3 dB yerine kablonun kayıtsız dalgalanmasıyla
  (90. yüzdelik + 1 dB) yapılıyor: kayıt olmadan da 8–10 dB dalgalanıyor.

## 5. Ses biçimi: 32 bit QuadCast girişi, 24 bit VB-Cable çıkışı, 48 kHz

- MicVST, WASAPI paylaşımlı modda Windows ile **32 bit kayan nokta** konuşur; iç işlem de 32 bit
  kayan noktadır. Aygıtın kendi biçimine (QuadCast 32 bit, VB-Cable 24 bit PCM) dönüştürmeyi Windows'un
  ses motoru yapar. Bu yüzden 32 → 24 bit dönüşüm MicVST'de değil Windows'tadır.
- MicVST açılışta günlüğe şu satırı yazar (yeni):
  `Ses biçimi: MicVST akışı ve iç işlem 32 bit kayan nokta; Windows aygıt biçimi: giriş … | çıkış …`
  – Windows'un aygıt için ayarlı biçimini (bit, hız, kanal) gösterir. Birim testleri bu bilgiyi okuyan
  kodu (WAVEFORMATEX / EXTENSIBLE) doğrular (W, L).
- **ENGELLENDİ:** CI makinelerinde ses aygıtı yok (günlük: "giriş (yok) | çıkış (yok)"). Kendi
  bilgisayarınızda günlükteki satır QuadCast'i 32 bit, VB-Cable'ı 24 bit 48000 Hz göstermelidir.
- Kayıt dosyası 48 kHz (varsayılan) ve 24 bit PCM'dir; 32 float kaynak doğru yuvarlanır (K1).

## 6. Otomatik Kazanç (düzeltilmiş tanım)

Tanım: normal konuşmada tepe ≈ −20 dBFS, bağırırken ≈ −15 dBFS; kazanç 0 … +6 dB (hiç kısmaz);
hiçbir tepe −10 dBFS'yi geçmez (3 ms ileri bakışlı yumuşak sınırlayıcı); sessizlikte son kazanç
korunur, gürültü kapısı yok; zincirde yeri değiştirilebilir; kapatınca 0 dB'ye yumuşakça döner.

- Birim testleri (W, L, A): konuşma düzeyleri, bağırma, ani bağırmanın ilk 0,5 s'si (−10 dBFS üstü
  örnek 0, gerçek tepe < −9,7 dB), sessizlikte koruma, açma/kapama, kazanç sınırları – BAŞARILI.
- 24 saatlik dayanıklılık (S): 8 milyon blok, −10 dBFS üstü örnek 0, geçersiz örnek 0, sınır dışı kazanç 0.
- Motor testi `autogain` (W, A): BAŞARILI.

## 7. Uzun süre ve dayanıklılık

| Koşu | Sonuç | Ortam |
|---|---|---|
| Motor, 330 dk | tık 2 / boşluk 1, **açıklanamayan 0**; THD+N en kötü −103 dB; bellek eğimi 0,00 MB/sa | W |
| Kaos (rastgele aygıt/zincir/yük olayları), 330 dk | açıklanamayan tık 0, açıklanamayan boşluk 0; bellek 0,01 MB/sa | W |
| Gerçek MicVST.exe, 330 dk | özel bellek −0,03 MB/sa; tutamaç/GDI sabit; temiz kapanış | W |
| Windows 11 ARM64, 60 dk | tık 0, boşluk 0, THD+N −112 dB | A |
| 168 saat (1 hafta) simülasyon | MicVST'ye bağlı tık 0 | S |
| VST3 eklentili 330 dk, tam CPU yükü, 3 öncelik (Normal / Normalin Üstünde / Yüksek); zincir: Otomatik Kazanç → ZL Equalizer 2 → ZL Compressor (de-esser) → ZL Compressor → Fire → ZL Compressor (limiter) | her öncelikte açıklanamayan tık 0, boşluk 0; bellek eğimi 0,00–0,01 MB/sa; çıkış kodu 0 (THD+N −40,7 dB: Fire satürasyon eklentisinin kendi bozulması) | W |

## 8. İnternetsiz çalışma

- Exe'nin içe aktardığı DLL'ler arasında ağ kütüphanesi yok (WinINet, WinHTTP, Winsock, URLMon) – W.
- Çalışırken ağ, dosya ve DLL denetimi (gerçek MicVST.exe, 6 senaryo) – W, BAŞARILI.
- LAME dahil yeni kod hiçbir ağ işlevi kullanmaz.

## 9. Bağımlılıklar

| Bileşen | Kullanılan | En yeni | Karar |
|---|---|---|---|
| JUCE | 9.0.4 | 9.0.4 | güncel |
| VST3 SDK (JUCE içinde) | 3.8.0 | 3.8.1 | yama gerekmedi (barındırma için fark yok) |
| libFLAC (JUCE içinde) | 1.5.0 | – | güncel |
| LAME | 4.0 | 4.0 | yeni eklendi (güvenlik düzeltmeli sürüm) |
| MSVC | 19.51 | – | CI'ın sürümü |

## 10. Test edilemeyenler (ENGELLENDİ)

- Gerçek QuadCast 2 / VB-Cable / Windows ses sürücüsü yolu (CI'da ses aygıtı yok).
- Windows 11 x64 istemci (CI'da yalnızca Server 2025 x64 ve Win11 ARM64 var).
- Uyku/uyanma, USB çıkar/tak (gerçek donanım), kullanıcının ticari eklentileri (FabFilter vb.).
- Dinleme testi (kulakla değerlendirme).

## 11. Kendi bilgisayarınızda kontrol (önerilen)

1. MicVST'yi açın, `log.txt`'de **Ses biçimi** satırına bakın: giriş QuadCast 32 bit, çıkış VB-Cable 24 bit 48000 Hz.
2. **● Kayıt** ile 30 saniye konuşun, durdurun: "Kaydedildi" notu çıkmalı; dosya
   `Belgeler\MicVST Kayıtları` içinde. Windows Medya Oynatıcı / VLC ile açın.
3. Kayıt sürerken Discord/OBS'de sesinizi dinletin: kayıt açık/kapalı fark olmamalı.
4. Ayarlar > Kayıt'ta FLAC ve MP3'ü deneyin; MicVST'yi kapatıp açınca seçim korunmalı.
5. Deneme kaydını kulaklıkla dinleyin: sonunda tık olmamalı.
