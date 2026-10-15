# MicVST 1.2.1 – Test Raporu

Bu rapor, `kararlilik/v1.2.0` dalındaki 1.2.1 çalışmasının testlerini belgeler: ne değişti, neyi
hangi ortamda nasıl ölçtüm, sonuç ne çıktı ve neyi **ölçemedim**. Her sonucun yanında ortamı
yazılıdır. Simülasyon, birim testi, gerçek iş parçacığı ve Windows sonuçları ayrı tutulmuştur;
biri diğerinin yerine kullanılmamıştır. Gerçek ses donanımıyla (gerçek mikrofon, VB-Cable,
gerçek Windows sürücüsü) hiçbir test yapılamadı – bu, 12. bölümde açıkça yazılıdır.

Önceki rapor: [RAPOR.md](RAPOR.md) (1.1.1 → 1.2.0). Oradaki bir model hatasının düzeltmesi
11. bölümdedir.

## Özet (16.10.2026)

- **Kod:** `kararlilik/v1.2.0` dalı, son kod commit'i `24dc9ae` (bu rapor ayrı bir commit). Forkuna
  (`videoRLS/MicVST`) gönderildi. PR #1 açık, **birleştirilmedi**; `main`'e dokunulmadı.
- **Teslim edilen `MicVST.exe`:** `24dc9ae`'nin Windows CI derlemesi (8. bölüm, SHA-256 orada).

| Alan | Sonuç | Ortam |
|---|---|---|
| Ses bütünlüğü | 53 karşılaştırma kaydında MicVST'ye ait açıklanamayan tık/atlama/tekrar/seviye sıçraması **0** (bir LSP kaydında eklentinin tepkisine bağlanan 1 bulgu, 3.2); seviye ±0,001 dB, frekans tepkisi 41 Hz–18 kHz ±0,0001 dB (48 kHz), sol/sağ birebir, sessizlik → tam 0 | W, L |
| Bozulma (kararlı durum) | tek ton −0,1 dBFS: THD+N −136 … −140 dB; 1 kHz −126 dB, 10 kHz −113 dB (43 ms pencere) | W, L |
| Bu turda bulunan tıklar | 2 kaynak (bozuk eklenti bloğu, takılan eklentiden sonra) bulundu ve düzeltildi; hatalı eklenti testinde tık 0 | W, L |
| Gerçek zamanlı çalışma | tampon 128–2048, zincir/cihaz değişiklikleri, kapanış: tık 0 (W); 20 dk uzun koşu tık 0 / boşluk 0 (W); 2 sa + 15 dk + 25 dk + 10 dk Linux koşularında açıklanamayan 0 | W, L |
| Simüle edilmiş 24 saat | MicVST'nin olaylarında tık 0, öğrenilen saat farkı gerçeğe 0,2 ppm yakın | S |
| Bellek | uzun koşularda artış yok | W, L |
| VST3 | gerçek eklentilerle yükle / çalarken değiştir / kaydet-geri yükle: tık 0, durum birebir | L (LSP), W (Airwindows) |
| Hata ayıklayıcılar | ASan/UBSan: MicVST'de bulgu yok; TSan: MicVST'de bulgu yok, gerçek LSP eklentisiyle 7 uyarı (eklentinin kendi iş parçacığı, incelenmedi) | L |
| İnternet | 0 bağlantı, 0 soket, ağ kütüphanesi yok – 6 durumda | W (+L) |
| Türkçe arayüz | bütün metinler Türkçe, taşma yok, tasarım aynı | W |

**En önemli sınır:** gerçek ses donanımıyla (senin mikrofonun, VB-Cable, gerçek WASAPI sürücüsü)
**hiçbir test yapılamadı**; bütün motor testleri sanal cihazlarla. Ayrıntı ve senin yapabileceğin
kısa kontrol: 12. ve 13. bölüm.

---

## 1. Test ortamları

| Kod | Ortam | Gerçek olan | Gerçek olmayan |
|---|---|---|---|
| **S** | Simülasyon (sanal zaman, deterministik); Linux/clang ve Windows CI/MSVC | MicVST'nin `AudioBridge` ve `InputQueue` kodu birebir | Zaman, iş parçacıkları ve cihazlar modeldir |
| **B** | Birim testleri (JUCE UnitTest, `MicVSTTests`); Linux ve Windows CI | Test edilen sınıfların kodu | – |
| **L** | Linux sanal makine (Firecracker, 2 vCPU, 8 GB RAM, Linux 6.18, clang); `MicVSTEngineTest` | MicVST'nin gerçek ses motoru (`AudioEngine`: cihaz yönetimi, giriş kuyruğu, eklenti iş parçacığı, JUCE `AudioProcessorGraph`, eklenti zinciri, köprü), JUCE 9.0.4 + 5 MicVST yaması, gerçek iş parçacıkları (cihazlar SCHED_FIFO 80, eklenti iş parçacığı SCHED_FIFO 70), gerçek VST3 eklentileri (LSP Plugins 1.2.16) | Ses cihazları sanaldır: WASAPI gibi zamanlanan iki iş parçacığı (mikrofon: 2 paketlik yakalama tamponu; kablo: 2,2 bloklu çalma tamponu). Sanal makinenin kendisi zaman zaman donuyor (hipervizör); bu donmalar kaydedildi ve ayrı sayıldı |
| **W** | GitHub Actions `windows-latest` (4 mantıksal çekirdek), Visual Studio 2026 / MSVC 19.51, Release x64 | Windows'ta derleme, birim testleri, gerçek Windows iş parçacıkları ve MMCSS ile aynı motor testi, gerçek bir Windows VST3 eklentisi (Airwindows Consolidated), `MicVST.exe`'nin çalışırken ağ/dosya denetimi, arayüz görüntüleri | CI makinesinde ses cihazı yok: motor testi orada da sanal cihazlarla çalışır |
| **G** | Gerçek donanım (senin bilgisayarın) | – | **Hiç test edilmedi** (12. bölüm) |

Ölçüm araçları depoda: `tests/EngineTestMain.cpp` (motor testi), `tests/enginetest/analyse_dump.py`
(giriş/çıkış karşılaştırması), `tests/StressMain.cpp` (simülasyon, `--day`), `tools/ci-*.ps1`
(Windows CI).

---

## 2. Bu turda koddaki değişiklikler

Ayrıntılı liste: [CHANGELOG.md](../CHANGELOG.md), 1.2.1. Kısaca (her maddenin kanıtı ilgili bölümde):

| Alan | Değişiklik | Neden |
|---|---|---|
| Ses yolu | Eklenti zinciri ayrı bir iş parçacığında (MMCSS "Pro Audio"), mikrofonun yakalama iş parçacığı yalnızca kopyalar | 1.2.0'da eklentiler yavaş kalınca Windows'un yakalama tamponu taşıyor, ses kayboluyordu (11. bölüm) |
| Saat farkı / seviye | Hız düzeltmesinin anlık kısmı ±500 ppm (0,9 cent) ile sınırlı, iki kademeli yumuşatma; 20 ms'yi aşan fazla gecikme dalga biçiminin uyduğu noktada 10 ms çapraz geçişle kırpılır | 1.2.0 bir kesintiden sonra sesi saniyelerce ±%0,5 (8,6 cent) hızlandırıp yavaşlatıyordu |
| Saat farkı öğrenme | Bloklar geç gelirken (eklentiler yetişemiyor) ve bir kesintinin toparlanma kuyruğunda saat farkı öğrenilmez | Ağır yükte öğrenilen değer ±170 ppm sapıyordu (4. bölüm) |
| Değişiklikler | Zincir değişikliği, cihaz/tampon değişikliği ve kapanış: 5 ms sönme, sessizlikte değiştirme, 5 ms açılma | 1.2.0'da 13 değişikliğin 11'inde tık vardı |
| Cihaz çıkarılınca | Başka cihaza geçilmez; seçili cihaz "(bağlı değil)" olarak kalır, takılınca kendiliğinden açılır | 1.2.0 dahili mikrofona/hoparlöre geçip orada kalıyordu (5. bölüm) |
| Bozuk eklenti çıkışı | NaN/sonsuz/> +24 dBFS blok ya da istisna fırlatan eklenti: çıkış eldeki geçerli sesle yumuşakça kısılır, geçerli ses gelince yeniden dolup açılır | Bozuk değerler kabloya ulaşıyordu; susturulan bloğun iki ucunda tık vardı (bu turda bulundu) |
| Takılan eklenti | 1,3 sn'den uzun takılmada kuyruğa sığmayan ses atılır, geçiş yumuşak | Atılan sesten sonra eski ve yeni ses yumuşatmasız birleşiyordu (tık, bu turda bulundu) |
| JUCE yamaları | 5 yama (3 WASAPI + grafikte ses iş parçacığında bellek ayırma yok + cihaz listesi değişince başka cihaz açılmaz) | 5. bölüm (gerçek zamanlı denetim) |
| Saat farkı öğrenme (2) | Bir kesintinin toparlanma kuyruğunda da öğrenilmez | Sık kesintide öğrenilen değer gereksiz geziniyordu (4.4) |
| Günlük | Kullanıcı klasörü `%USERPROFILE%` olarak yazılır; WASAPI/JUCE cihaz hataları Türkçe | Günlük paylaşılırken Windows kullanıcı adı görünmesin; günlükte İngilizce satır kalmasın |
| Arayüz | Tüm metinler Türkçe (JUCE'un kendi düğmeleri ve metin menüsü dahil) | 10. bölüm |
| Testler | Motor testi (`MicVSTEngineTest`) depoda ve Windows CI'da (gerçek bir Windows VST3 eklentisiyle); giriş/çıkış karşılaştırma analizi; simüle edilmiş gün (`MicVSTStress --day`); Windows'ta çalışma anında kapalı kutu denetimi; yeni birim testleri | Bu rapordaki ölçümler |

---

## 3. Ses kalitesi ve bütünlüğü (W, L)

### 3.1 Yöntem: giriş ile çıkışı birebir karşılaştırma

Sanal mikrofon bilinen bir sinyal verir; sanal kablo çaldığı her örneği kaydeder. Analiz
(`analyse_dump.py`) ikisini aynı zaman çizgisinde karşılaştırır:

- **Gecikme ve süreklilik:** Çıkışın her 1024 örneklik penceresi girişte bulunur (çapraz
  korelasyon, örneğin kesirine kadar). Gecikmenin ani düşmesi = ses atlandı/kayboldu, ani artması =
  araya sessizlik girdi ya da ses tekrarlandı. Eğimi = çalma hızı hatası (ppm).
- **Tık/kesinti:** Giriş, ölçülen gecikme eğrisi boyunca çıkışın zaman çizgisine taşınır (64
  dokunuşlu sinc). Her 256 örneklik parça (yarı yarıya örtüşen) tek bir kazanç çarpanıyla
  karşılaştırılır; tık gizleyebilecek bir filtre uydurulmaz. Eşiği aşan bir parça, ±0,6 örnek kayma
  ve parça içinde ±600 ppm hız payıyla yeniden denenir (yumuşak bir zaman kayması tıkı açıklayamaz). Fark, parçanın etkin değerinin
  belirli bir oranını aşarsa bulgu sayılır. Oran her koşu için o sinyaldeki yöntem tabanından
  hesaplanır (en az %2, en çok %20); duraklamalarda sinyale göre −50 dB üstü her fark sayılır.
- **Seviye, frekans tepkisi, kanal dengesi, faz, TD+N:** çok tonlu sinyalde 24 ton aynı anda
  uydurulur; tonların dışındaki her şey (bozulma + gürültü) TD+N'dir.
- **Her bulgunun nedeni:** Test, sanal kablonun geç kaldığı (sessizlik çaldığı) anları, mikrofonun
  kaybettiği paketleri (ikisi de sanal makinenin donmaları), MicVST'nin kendi olaylarını (kırpma,
  atlama, boşalma) ve planlı değişiklikleri (zincir değişikliği, program kapanışı) zamanıyla
  kaydeder. Hiçbirine denk gelmeyen bulgu **"açıklanamayan"** olarak sayılır.

**Yöntemin kendisi doğrulandı** (çıkışa bilerek hata eklenmiş kopyalar):

Temiz bir koşunun (konuşma, −6 dBFS, 20 s) kablo kaydına bilerek tek bir hata eklendi ve analiz
yeniden çalıştırıldı. Her hata, bulunması gereken yerde ve **yalnızca orada** bulundu; kayıtlı bir
olaya denk gelmediği için de doğru olarak "açıklanamayan" sayıldı. Aynı kaydın hatasız hâlinde
bulgu sayısı 0.

| Eklenen hata (t ≈ 6,3–7,0 s) | Sesin o andaki düzeyi | Analizin bulduğu |
|---|---|---|
| Tek örnekte +0,02 sıçrama (−34 dBFS) | konuşma, yerel etkin değer 0,083 (−22 dBFS) | 1 tık, doğru yerde |
| Tek örnekte +0,02 sıçrama | konuşma, yerel etkin değer 0,072 | 1 tık, doğru yerde |
| Tek örnekte +0,005 sıçrama (−46 dBFS, sinyalin %7'si) | konuşma, yerel etkin değer 0,072 | 1 tık, doğru yerde |
| 24 örnek (0,5 ms) ses atlandı | konuşma | süreklilik: 1 düşüş (22 örnek ölçüldü) |
| 5 ms sessizlik eklendi | konuşma | süreklilik: 1 artış (240 örnek) |
| 10 ms ses tekrarlandı | konuşma | süreklilik: 1 artış (480 örnek) |
| 100 ms boyunca seviye −1 dB | konuşma | 5 pencerede seviye hatası + başta ve sonda 2 tık |

Yöntemin bu kayıttaki tabanı: fark/parça etkin değeri medyan 0,0001, %99,9'luk 0,002 → eşik
%2. Yani analiz kendi başına sahte tık üretmiyor, ama sinyalin %2'sinden küçük tek örneklik
bozulmaları (konuşmada yaklaşık −56 dBFS altı) göremez; 3.2'deki "0 tık" bu duyarlılıktadır.

### 3.2 Sonuçlar (son kod, W ve L)

İki ortamda aynı ölçümler: **W** (Windows CI, `24dc9ae` derlemesi; makine sakin, test makinesi
donması yok) ve **L** (Linux sanal makinesi, son kod; bu koşu sırasında sanal makine sık sık
donuyordu – kablonun geç kalması, mikrofon paketinin kaybolması – bunlar her kayıtta sayılıp
ayrıldı). Hepsi boş zincir ve 480 örnek tampon, aksi yazılmadıysa; her kayıt 20 sn.

**Sonuç: 2 ortamda toplam 53 (W 19, L 34) kayıtta MicVST'ye ait açıklanamayan tık, atlama, tekrar,
sessizlik ya da seviye sıçraması 0** (tek istisna aşağıda, LSP zinciri).

| Ölçüm | W | L |
|---|---|---|
| Seviye (çıkış/giriş, 1024 örneklik pencereler, %1–%99) | ±0,000–0,001 dB | ±0,000–0,001 dB (L, olaysız kayıtlar) |
| Frekans tepkisi 41 Hz–18 kHz, 48 → 48 kHz | −0,0001 … 0,0000 dB | −0,0001 … 0,0000 dB |
| Frekans tepkisi 44,1 → 48 / 48 → 44,1 kHz | −0,021 … 0 dB (18 kHz'de) | −0,021 … 0 dB |
| Frekans tepkisi 96 → 48 kHz | – (yalnız L'de ölçüldü) | −0,009 … +0,007 dB |
| Sol/sağ denge, faz | 0,00000 dB, 0,0000° | 0,00000 dB, 0,0000° |
| Mono mikrofon → iki kanal | sol = sağ, birebir | sol = sağ, birebir |
| Sessizlik girince çıkış | tam 0 | tam 0 |
| Tek ton −0,1 dBFS, THD+N (1024 örnek) | medyan −139,9 dB, %95 −131,4 dB | medyan −136,5 dB, %95 −126,1 dB |
| Çok tonlu sinyal (24 ton) TD+N medyan, −3 / −40 / −0,1 dBFS | −99,7 / −99,9 / – | −90,9 / −97,3 / −94,8 dB |
| Çok tonlu, 44,1→48 / 48→44,1 / 96→48 kHz | −93,7 / −95,0 / – | −88,2 / −87,5 / −89,3 dB |
| Gecikme (ölçülen, mikrofon yakalamasından kabloda çalınana; tampon 480) | 44,7–44,8 ms | 44,8 ms |

**TD+N nasıl okunmalı:** bu ölçü tonların dışındaki her şeyi toplar – bozulma, gürültü ve
saat farkı denetleyicisinin çalma hızını oynatması (birkaç ppm ile birkaç yüz ppm arası, perdede
en çok 0,9 cent). Kararlı durumda (hız sabitken) tek tonla ölçülen değer −126 … −140 dB
(4.1, 3.2); medyan −87 … −105 dB'yi ve L'deki kötü pencereleri (%95: −50 … −79 dB) belirleyen, bir
kesintiden sonra hızın oynamasıdır, sesin bozulması değil. Bu oynama 1.2.0'daki ±%0,5'in en çok
onda biridir.

**Yüksek frekanslar:** tek tonla 5–22 kHz'de, motor çalışmaya yeni başlamışken (ilk 4–8 sn)
"THD+N" −63 … −91 dB ölçülüyor (W: 5 kHz −91, 10 kHz −81, 15 kHz −69, 18–22 kHz −75 … −86 dB); denetleyici yerleştikten sonra 10 kHz'de −106 …
−109 dB (4.1). Neden aynı: hız oynaması yüksek tonda daha büyük faz hatası demek. 22 kHz'de
seviye −0,36 dB (filtrenin kenarı, tasarım gereği).

**Gerçek zincirle (L):**

| Kayıt | Açıklanamayan bulgu | Not |
|---|---:|---|
| LSP tam zincir (kompresör, EQ ×8, limiter, gate, mikser, grafik EQ ×16, yan zincirli kompresör), çok ton −3 dBFS | 0 | seviye ±0,006 dB; ton başına +0,004 … +0,029 dB |
| LSP tam zincir, müzik | 0 | seviye −0,022 … +0,014 dB |
| LSP tam zincir, konuşma | 1 | Test makinesinde 10 ms'lik bir mikrofon paketi kayboldu (zincirin girişinde eksik ses). Sonraki 0,6 sn çıkış çevrimdışı kopyayla %0,2–0,4 içinde aynı; ardından gate/kompresör bir sonraki hece geçişinde eksik sesten dolayı farklı karar verdi ve çıkış ~0,1 sn 1,5–2,5 dB kısık kaldı (basamak yok). Eklentinin eksik girişe tepkisi; yöntemin bunu MicVST'den %100 ayırt edemediğini not ediyorum |
| Yalnızca iki EQ, konuşma | 0 | seviye −0,011 … +0,017 dB |
| Yalnızca dinamik (kompresör + limiter), konuşma | 0 | seviye ±0,000 dB (eşik %2) |
| Bütün eklentiler atlanmış (bypass), çok ton | 0 | doğrudan girişle karşılaştırma: ton başına −0,0001 … 0 dB |

Dinamik eklentili zincirlerde karşılaştırma eşiği parça etkin değerinin %17–20'sine çıkıyor
(eklentinin kendi iç durum farkları); orada yöntem daha az duyarlı.

**Bilerek bozulan durumlar (L ve W):** kablo 3 sn'de bir 60 ms takıldığında (konuşma, müzik,
gürültü, çok ton) her takılma sonrası fazla gecikme bir kez kırpıldı, tık 0; eklenti 3 sn'de bir
200 ms takıldığında yumuşak kesinti + atlama, açıklanamayan 0.

**Kırpma (gecikme fazlası atılırken) ne duyulur:** 10 ms'lik çapraz geçiş, dalga biçiminin uyduğu
noktada (L son koşu, 4 sinyal × 6 kırpma). Gürültü ve çok tonlu sinyalde geçiş penceresinde seviye
±0,32 dB içinde, yüksek frekans enerjisi +1,7 … +2,8 dB; konuşmada seviye −0,65 … +5,5 dB ve yüksek frekans +1,8 … +4,9 dB (atlanan 60 ms'nin öncesi ve sonrası farklı hece olabildiği için), müzikte −3,3 … +1,7 dB. Tık yok; çok kısa bir tını değişimi
olabilir (12. bölüm).

---

## 4. Hız düzeltmesi ve saat farkı

Mikrofon ve kablo ayrı saatlerle çalışır; aralarındaki küçük fark (ppm, milyonda bir) köprünün
çalma hızı ince ayarlanarak kapatılır. Ses bu hız farkı kadar ince/kalın olur: 1000 ppm =
1,7 cent (cent = yarım tonun yüzde biri). 1.2.0 bir kesintiden sonra saniyelerce ±%0,5
(±5000 ppm, 8,6 cent) oynuyordu; 1.2.1'de anlık düzeltme ±500 ppm (0,87 cent) ile sınırlı.

### 4.1 Kararlı çalışırken (L, W)

`MicVSTEngineTest flutter`: 40 s, kesinti yok, 997 Hz ve 10 kHz ton. Hız sapması 4096
örneklik pencerelerde (85 ms) ölçülen frekansın ortalamadan farkıdır; THD+N 2048 örneklik
pencerelerde (43 ms) tonun dışındaki her şeydir (bozulma + gürültü + hız oynaması).

| Ortam | Ton | Hız sapması RMS / en çok | THD+N medyan / en kötü |
|---|---|---|---|
| W | 997 Hz | 1,21 ppm / 18 ppm | −126,1 dB / −114,1 dB |
| W | 10 kHz | 0,14 ppm / 0,4 ppm | −112,6 dB / −98,6 dB |
| L | 997 Hz | bu koşu sanal makinenin donmalarıyla bozuldu (aynı anda kablo boşlukları); kararlı dakikalarda uzun koşudan: hız oynaması 0–17 ppm, THD+N medyan −112 … −120 dB (7.1) | |

### 4.2 Kesintiden sonra (L, W)

Kablo her 3 sn'de 60 ms takıldığında (aşırı bir test) fazla gecikme kırpılır, kalan birkaç ms
denetleyiciyle eritilir: ölçülen hız farkı en çok 503 ppm (0,87 cent; W) – anlık düzeltmenin
sınırı 500 ppm, üstündeki birkaç ppm öğrenilen saat farkı. Bu testte takılmalar 3 sn'de bir geldiği için hız neredeyse hep
sınırda kalıyor; normal kullanımda bir kesintiden sonra yalnızca birkaç saniye.

### 4.3 Simüle edilmiş gün (S, 24 saat sanal zaman)

`MicVSTStress --day 24`: MicVST'nin gerçek `AudioBridge` + `InputQueue` kodu, sanal zamanda.
Mikrofon saati ±300 ppm içinde gezinir ve 6 saatte bir yeni değere atlar; 364 kablo takılması
(5–80 ms), 98 sistem donması (2–30 ms, bütün iş parçacıkları), 229 eklenti takılması
(10–250 ms), 8 kez 30 dakikalık "video render" yükü (blokların %45'i gerçek zamanın 1,1–1,9
katı sürer). Tampon 480, güvenlik payı Otomatik.

| | Linux (clang) | Windows CI (MSVC) |
|---|---:|---:|
| Çıkışta kesinti (yumuşak geçişli) | 105 | 117 |
| Gecikme kırpması | 188 | 205 |
| Zincir yetişemedi, ses atlandı | 33 | 33 |
| Mikrofon tamponu taştı (ses Windows'ta kayboldu) | 8 (80 ms) | 8 (80 ms) |
| Kablo boşluğu (kablo sürücüsü takıldı) | 209 | 209 |
| Tık: kaçınılmaz / MicVST'nin olayında / başka | 346 / 0 / 0 | 346 / 0 / 0 |
| En büyük perde hatası | 566 ppm (0,98 cent) | 566 ppm (0,98 cent) |
| Perde hatası 100 ppm'in üstünde | 14 596 sn (günün %16,9'u) | 14 711 sn (%17,0) |
| Öğrenilen saat farkı gün sonunda (gerçek −76,3 ppm) | −76,4 ppm | −76,5 ppm |
| Gecikme: saatlik ortalamaların ortalaması / en çok | 109,6 / 206 ms | 109,6 / 208 ms |
| Güvenlik payı gün sonunda (Otomatik) | 80 ms | 80 ms |

"Kaçınılmaz tık" = kablo sürücüsü tamponundan uzun takıldığı ya da bütün sistem donup mikrofon
sesi Windows'un içinde kaybolduğu anlar; bunlar her tasarımda olur. MicVST'nin kendi
olaylarında (kesinti, kırpma, atlama) **0 tık**. İki platformun rastgele sayı üreteçleri farklı
dağıldığı için olayların sırası birebir aynı değil.

Hız 100 ppm'in üstündeyken geçen sürenin neredeyse tamamı render dönemlerinde (o saatlerde
%42–45, diğer saatlerde %1–8).

### 4.4 Sık kesintide saat farkı öğrenme (S)

Gerçek saat farkı bilinen bir senaryoda, kablo takılmaları (5–40 ms) ve donmalar (2–40 ms)
her N saniyede bir geldiğinde öğrenilen değerin 2.–20. dakikalar arasındaki aralığı
(`simlearn`, önceki köprü kodu ile karşılaştırmalı):

| Gerçek fark | Kesinti aralığı | Önce (yalnızca geç gelen blok kuralı) | Şimdi (+ toparlanma kuyruğu kuralı) |
|---:|---:|---|---|
| 0 ppm | 4 sn | −34 … +117 ppm | −3 … +89 ppm |
| 0 ppm | 7 sn | −46 … +40 ppm | −16 … +37 ppm |
| 0 ppm | 15 sn | −62 … +67 ppm | −1 … +13 ppm |
| +100 ppm | 7 sn | +48 … +160 ppm | +69 … +119 ppm |
| −200 ppm | 7 sn | −229 … −95 ppm | −197 … −102 ppm |

Öğrenilen değerdeki sapma, denetleyicinin anlık kısmıyla dengelenir; sese etkisi en çok
~100 ppm (0,17 cent) kalıcı hız farkıdır, duyulmaz. Kesintiler birkaç saniyede bir geliyorsa
sapma hâlâ ±90 ppm'e kadar çıkabiliyor (12. bölüm).

---

## 5. Gerçek zamanlı motor (W, L, S)

### 5.1 Eklentiler ayrı iş parçacığında: 1.2.0 ile karşılaştırma (W, S)

Asıl sorun (senin Topaz günlüğündeki "microphone driver gaps") 1.2.0'ın eklentileri mikrofonun
yakalama iş parçacığında çalıştırmasıydı. Aynı yük modeliyle:

**Gerçek Windows iş parçacıkları (W, Bölüm C, 40 sn, 4 çekirdeğin hepsi meşgul, render benzeri eklenti yükü):**

| Tasarım | Mikrofon tamponu taştı (kayıp ses) | Tık | En uzun yakalama çağrısı | 15 ms'den geç uyanan yakalama |
|---|---:|---:|---:|---:|
| 1.2.0 (eklentiler yakalama iş parçacığında) | 21 kez (210 ms) | 15 | 20,2 ms | 59 |
| **1.2.1** (ayrı eklenti iş parçacığı) | **0** | **0** | 2,1 ms | 0 |

**Simülasyon (S, Windows CI'da, aynı zamanlama her tasarıma):**

| Senaryo | 1.2.0 tık | 1.2.1 tık | 1.2.1 ek gecikme |
|---|---:|---:|---:|
| Çok ağır yük (eklenti süresi 0–14 ms, ort. %95) | 20 | 0 | 103 ms |
| 4 sn'de bir 25 ms takılma | 30 | 0 | 55 ms |
| Arka planda render (blokların %45'i 1,1–1,9× gerçek zaman) | 95 | 0 | 127 ms |
| Zincir gerçek zamandan yavaş (ort. %112) | 806 | 0 (115 yumuşak atlama) | 140 ms |
| 3 sn'de bir 60 ms eklenti takılması | 40 | 0 | 91 ms |
| Bütün sistem 6 sn'de bir 30 ms donuyor | 60 | 60 | 43 ms |

Son satır: bütün sistem (yakalama iş parçacığı da) donarsa ses Windows'un içinde kaybolur; hiçbir
tasarım bunu kurtaramaz. Ağır yükte gecikmenin artması bilinçli: "Otomatik" güvenlik payı her
kesintide 5 ms büyür (12. bölüm, madde 8).

### 5.2 MMCSS ve iş parçacığı önceliği (W)

- Windows CI'da eklenti iş parçacığı MMCSS "Pro Audio" önceliğini aldı (günlük satırı:
  "Eklenti iş parçacığı: MMCSS 'Pro Audio' (cihaz iş parçacıklarının bir altında)").
- 10 ms'lik bir ses iş parçacığının uyanma gecikmesi, 4 çekirdek tamamen meşgulken (Bölüm B):
  önceliksiz iş parçacığı p99 29,0 ms, en çok 40 ms; MMCSS ile p99 0,54 ms, p99,9 0,56 ms,
  en çok 0,57 ms; 2 ms'yi aşan uyanma 0 (`24dc9ae`).
- Gerçek `AudioBridge`, iki gerçek zamanlı iş parçacığı arasında, tam yükte 60 sn: 0 kesinti,
  0 tık.

### 5.3 Ses iş parçacıklarında bellek ayırma ve bekleme (L)

`MicVSTEngineTest rtprobe`: mikrofon, eklenti ve kablo iş parçacıklarının ses bölümlerinde her
`malloc/free`, kilit, bekleme ve uyku çağrısı sayıldı (20 sn'lik senaryolar, tampon 480):

| Senaryo | Bellek ayırma / bırakma | Bekleyen kilit | Koşul bekleme / uyku |
|---|---:|---:|---:|
| Boş zincir | 0 / 0 | 0 | 0 / 0 |
| Kazanç + 240 örnek gecikme + %30 yük | 0 / 0 | 0 | 0 / 0 |
| Çalarken zincir değişiklikleri | 0 / 0 | 0 | 0 / 0 |
| Güvenlik payı değişiklikleri (yeniden dolma, kırpma) | 0 / 0 | 0 | 0 / 0 |
| Gerçek LSP VST3 zinciri (kompresör, EQ ×8, limiter, gate) | 0 / 0 | 0 | 0 / 0 |

(Kilit çağrıları var – eklentilerin ve JUCE grafiğinin kendi kilitleri – ama hiçbiri beklemedi.)

### 5.4 Bu turda bulunan ve düzeltilen iki tık

1. **Bozuk ses veren ya da hata fırlatan eklenti:** MicVST o bloğu susturuyordu; susturulan blok
   önceki sesten sıfıra bir basamakla (sonradan 2 ms'lik bir rampayla, ama yine bir kırılmayla)
   başlıyordu ve sonu da öyleydi – Windows motor testinde 2–4 tık. Şimdi köprüye "giriş yok"
   denir: köprü elindeki geçerli sesle yumuşakça kısar, geçerli ses gelince yeniden dolup açar.
   Bedeli: tek bir bozuk blok için ~30–50 ms yumuşak sessizlik (önce 10 ms + tık). Kesinti
   sayılmaz, güvenlik payı büyümez.
2. **1,3 sn'den uzun takılan eklenti:** bu sürede yakalanan ses giriş kuyruğuna sığmayınca
   atılıyordu (doğru), ama kuyruk ondan sonra eski sesle yeni sesi yumuşatmasız ardı ardına
   çalıyordu – tık. Şimdi kuyruk o noktayı biliyor, bir blok o noktayı asla aşmıyor ve köprü
   orada yumuşak geçiş yapıyor.

Sonuç: hatalı eklenti testinde tık **0** (W ve L son koşu); birim testleri: geçersiz bloklar +
atlamalı devam, işaretli kesinti noktası, kuyruk taşması.

### 5.5 Değişiklikler, cihazlar, hatalar (L, son kod)

| Test | L sonucu |
|---|---|
| Tampon değişikliği ×3 ve kapanış | tık 0; 102–161 ms sessizlik |
| Cihaz çıkar/tak, sürücü takılması | başka cihaza geçmedi, takılınca açıldı |
| Hatalı eklentiler | tık 0 |
| 13 zincir değişikliği | 12'sinde tık 0; "invert ekle" penceresinde 3 tık – aynı koşuda kablo 1 kez geç kaldı ve 1 mikrofon paketi kayboldu (test makinesi). Bu test tıkları nedenine göre ayırmıyor; W'de (makine donması yok) 0/13 |
| UI iş parçacığı 3 sn kilitli, cihaz değişikliğiyle / değişikliksiz | ek boşluk yok (görülen kısa boşlukların hepsi kablonun geç kalması) |
| Tampon 128–2048 | 480 ve 2048'de tık 0; diğerlerinde tık ve boşluklar, hepsinin olduğu koşularda test makinesi donmaları sayıldı (en çok 128'de: kablo 8 kez geç, 11 mikrofon paketi kayıp). W'de hepsi 0 |
| Arka planda 2 çekirdeği de tam meşgul eden yük, konuşma ve gürültü | açıklanamayan bulgu 0, kesinti 0 |

Linux sanal makinesindeki donmalar (hipervizör) bu ortamın özelliği; MicVST'nin olaylarından ayrı
sayıldı. Gerçek zamanlılığın asıl kanıtı W (makine donması yok) ve simülasyon (S).

---

## 6. VST3 eklentileri

### 6.1 Gerçek eklentilerle yükleme, çalarken değişiklik, kaydet/geri yükle

| | Linux (L) | Windows CI (W) |
|---|---|---|
| Eklenti paketi | LSP Plugins 1.2.16 (178 eklenti) | Airwindows Consolidated (MIT lisanslı, Windows x64 VST3) – LSP'nin Windows VST3 sürümü yayımlanmıyor |
| Zincir | Compressor, Parametric EQ ×8, Limiter, Gate, Mixer ×4, Graphic EQ ×16, Sidechain Compressor | aynı eklentiden 4 tane |
| Çalarken yükleme | 7/7, 0,09 sn | 4/4, 0,03 sn |
| Atla/aç turu, 4 eklenti daha (11 / 8 eklenti) | kesinti 0 | kesinti 0 |
| Çalarken zincir değişikliği (ekle, kaldır, taşı, atla) | 22 değişiklik, ardından tık **0** (bütün koşuda 2 tık; aynı koşuda kablo 1 kez geç kaldı – test makinesi) | 16 değişiklik, ardından tık **0**, bütün koşuda tık 0 |
| Ayar dosyasına yaz → oku → yeni motorda geri yükle | 7/7 eklenti, durum verisi birebir aynı | 4/4, birebir aynı |
| Çıkış | sonlu (NaN yok), tepe −12,3 dB | sonlu, tepe −15,1 dB |

Program yeniden açıldığında zincirin geri gelmesi de bu "yaz → oku → yeni motor" adımıyla
sınandı. Aynı dosyada birden fazla eklenti bulunan paketlerde (LSP, WaveShell) her eklentinin
kimliği artık ayrı saklanıyor (1.1.1'den beri hepsi ilk eklenti olarak geri geliyordu; birim
testi var).

### 6.2 Eklentili ses, çevrimdışı işlenmiş hâliyle birebir (L)

MicVST'nin çıkışı, aynı eklentilerin aynı ayarlarla gerçek zaman dışında (dosya üzerinde) işlenmiş
hâliyle karşılaştırıldı; böylece eklentinin kendi etkisi dışarıda kalır, yalnızca MicVST'nin
eklediği bir şey varsa görünür.

Sonuçlar 3.2'deki "Gerçek zincirle" tablosunda: LSP tam zincir (çok ton, müzik, konuşma), yalnızca
EQ'lar, yalnızca dinamik eklentiler, bütün eklentiler atlanmış.

Windows'ta bu karşılaştırma Airwindows ile anlamlı sonuç vermedi: eklentinin varsayılan efekti
çevrimdışı kopyasıyla aynı çıktıyı üretmiyor (zamana bağlı iç durum), fark eklentiden geliyor.
Bütün eklentiler atlanmış (bypass) zincirde ise bulgu **0**.

### 6.3 Gecikme bildirimi ve telafi

- 240 örnek gecikme bildiren test eklentisi: ölçülen gecikme +5,0 ms arttı, gösterilen gecikme de
  aynı kadar (L, W).
- **LSP Limiter** sesi 240 örnek geciktiriyor ama 0 bildiriyor: ses doğru, gösterilen gecikme
  5 ms eksik (12. bölüm). MicVST tek bir seri zincir çalıştırdığı için iki yol arasında hizalama
  gerekmiyor; bildirilen gecikme yalnızca gösterilen değere eklenir.

### 6.4 Hatalı ve çöken eklentiler

Test eklentileriyle (L ve W):

| Durum | Sonuç |
|---|---|
| Eklenti NaN üretiyor (blok 50'den itibaren) | çıkış sonlu, susturuldu, 251 blok sayıldı; eklenti kaldırılınca ses geri geldi |
| Eklenti 1e30 üretiyor (kararsız filtre) | aynı |
| Eklenti bir kez istisna fırlatıyor | o blok susturuldu, program sürdü |
| Eklenti ses iş parçacığında 1,5 sn takılıyor | yumuşak kesinti; kuyruğa sığmayan 140 ms mikrofon sesi atıldı (14 blok, sayıldı); sonra yumuşak geçişle devam |
| Hazırlanması 400 ms süren eklenti çalarken ekleniyor | en uzun sessizlik 14 ms (değişiklik geçişi) |
| Yüklenemeyen eklenti | hata mesajı, zincir değişmedi |
| **Bu turdaki düzeltme:** susturulan bloğun iki ucunda ve takılmadan sonraki geçişte tık vardı | Linux son koşu: bütün testte tık **0**; birim testleri eklendi |

**Çöken eklenti** (erişim hatası) ses işlerken MicVST'yi de kapatır; bu test edilemez ve
çözülmedi (12. bölüm). Tarama ayrı işlemde yapıldığı için taramada çöken eklenti programı
düşürmez (birim testleri: çöken, yanıt vermeyen, bozuk sonuç veren tarama).

### 6.5 Denetlenmeyenler

- Ticari eklentiler (ör. FabFilter, Klanghelm, TDR, Waves) hiç denenmedi; bunlar Windows'a özel
  ve lisanslı. Linux'ta LSP, Windows'ta Airwindows denendi.
- Eklenti pencereleri (editörleri) testlerde açılmadı.
- Gerçek bir eklentinin çökmesi.

---

## 7. Uzun süre ve hata ayıklayıcılar

### 7.1 Gerçek zamanlı uzun koşular

| Ortam | Koşu | Süre | Tık (açıklanamayan) | Sessizlik (açıklanamayan) | Bellek (RSS) | Not |
|---|---|---:|---|---|---|---|
| L | `chaos 120` (bir önceki derleme; sonradan değişen kısımlar – bozuk eklenti, kuyruk taşması – bu testte devreye girmiyor): 20 sn'de bir zincir değişikliği (atla/aç, ekle, taşı, kaldır, güvenlik payı 40 ms ↔ Otomatik), 5 dk'da bir tampon değişikliği (iki cihaz yeniden açılır), zincir: kazanç + 240 örnek gecikme + %30 yük | 120 dk | 1000 (0) | 917 (3 → nedeni aşağıda) | 26,5 → 36,9 MB, ikinci yarıda eğim 0,02 MB/sa | Tıkların 850'si kablonun geç kalması, 147'si mikrofon paketinin kaybı (test makinesinin donmaları), 3'ü bir kesintinin ±0,4 sn yakınında (o zamanki ölçümle nedeni ayrıştırılamadı) |
| L | `chaos 15`, son kod: 20 sn'de bir zincir değişikliği, **her dakika** cihaz değişikliği | 15 dk | 83 (0): 72 kablo geç, 11 mikrofon paketi | 96 (0): 45 planlı, 6 kesinti, 45 makine | 26,8 → 36,7 MB, eğim 0,26 MB/sa | Planlı değişikliklerin yakınında tık 0 |
| L | Aynı ayarlarla yeniden üretme (20 sn'de bir hem zincir hem cihaz değişikliği), düzeltilmiş ölçüm | 25 dk | 164 (0): 136 kablo geç, 20 mikrofon paketi, 8 planlı bir değişikliğin ±0,4 sn yakınında (bu koşuda ayrıntı yazdırılmıyordu; sonraki `chaos 15`'te planlı değişikliklerin yakınında tık 0) | 164 (0) | 26,5 → 36,3 MB | 75 cihaz + 75 zincir değişikliği |
| L | `long 10`, son kod: değişiklik yok | 10 dk | 31 (0): 24 kablo geç, 7 mikrofon paketi | 16 (0) | 26,4 → 36,3 MB, eğim 0,00 MB/sa | Olaysız dakikalarda THD+N (43 ms) medyan −112 … −120 dB |
| W | `long 20`: değişiklik yok | 20 dk | **0** | **0** | 28,2 → 25,0 MB | THD+N (43 ms) dakikalık medyanlar −125 … −133 dB, en kötü pencere −116,6 dB |
| W | `chaos 10`: 20 sn'de bir değişiklik, 5. dk'da tampon değişikliği | 10 dk | **0** | 28 (0), hepsi planlı | 28,2 → 26,7 MB | |

Bellek: Linux'ta ilk 2 dakikadaki ~10 MB artış test programının kendi analiz tamponları
(önceden ayrılır, dolarken RSS'e yansır); sonra düz. Windows'ta hiç artış yok.

**2 saatlik koşudaki 3 "açıklanamayan" sessizlik:** üçü de cihaz değişikliğinden 1–5 sn sonraydı.
Yeniden üretip izledim: test programı her dakikanın sonunda birkaç saniye süren bir analiz
yapıyor ve bu sırada MicVST'nin sayaçlarını okuyamıyordu; o arada olan sıradan (yumuşak
geçişli) kesintiler saniyeler sonra not ediliyor ve sessizlikle eşleşmiyordu (cihaz
değişiklikleri de dakika sınırlarına denk geliyordu). Ölçüm aracı artık bir olayı iki okuma
arasındaki tüm aralığa bağlıyor; aynı ayarla 25 dakikalık yeniden testte (75 cihaz değişikliği +
75 zincir değişikliği) açıklanamayan **0**. Ayrıca kullanıcı arayüzü iş parçacığını her cihaz
değişikliğinden hemen sonra 3 sn kilitleyen ayrı bir test (`msgblock`) de ek bir sessizlik
üretmedi. MicVST'nin kodunda bu yüzden bir değişiklik gerekmedi.

Bu araştırma sırasında **iki gerçek tık kaynağı** bulundu ve düzeltildi (5.4).

### 7.2 Simüle edilmiş gün

4.3'te: 24 saat, MicVST'nin olaylarında 0 tık, öğrenilen saat farkı gerçeğe 0,1–0,2 ppm yakın.

### 7.3 Hata ayıklayıcılar (L)

Son kodla, JUCE'un ses/olay modülleri de araçlı derlenerek (gerçek zamanlı öncelik kapalı):

| Araç | Koşular | Sonuç |
|---|---|---|
| ASan + UBSan (bellek hatası, tanımsız davranış) | zincir değişiklikleri, cihaz değişiklikleri, çıkar/tak, hatalı eklentiler, gerçek LSP VST3, eklenti takılması kaydı, 5 dk karışık koşu | hepsi çıkış kodu 0; MicVST'de bulgu yok. VST3 koşusunda bellek sızıntısı raporu yalnızca LSP eklentisinin kendi kodunda |
| TSan (iş parçacığı yarışları) | zincir değişiklikleri, cihaz değişiklikleri, çıkar/tak, hatalı eklentiler, kablo takılması kaydı | bulgu yok |
| TSan | gerçek LSP VST3 | 7 uyarı: LSP'nin kendi iş parçacığı JUCE'un VST3 ana bilgisayar geri çağrılarını (`restartComponent`, `AsyncUpdater`) çağırıyor. MicVST'nin ses yolu sınıflarında (köprü, kuyruk, motor) bulgu yok; ama bu 7 uyarı ayrıntılı incelenmedi (zaman kalmadı) – açık madde |

---

## 8. Windows (W)

Teslim edilen `MicVST.exe` bu bölümdeki derlemedir: commit `24dc9ae`, GitHub Actions
`windows-latest`, Visual Studio 2026 (MSVC 19.51), Release x64, JUCE 9.0.4 + 5 MicVST yaması.
Dosya: 6 814 208 bayt, SHA-256 `A181E1ADE273B0848A99691E813FEC322673731305BA3C58D40BCFFB4F7DD736`.

| Adım | Sonuç |
|---|---|
| Derleme | başarılı; MicVST kodunda **0 uyarı**, JUCE'da 0 uyarı, 0 hata |
| Birim testleri (`MicVSTTests`) | hepsi geçti (Windows yol kurallarını sınayan 4 test Linux'ta doğal olarak başarısız, Windows'ta geçiyor) |
| Ağ kütüphanesi denetimi | yok (9. bölüm) |
| Çalışırken kapalı kutu denetimi, 6 durum | 0 ağ olayı, hepsi normal kapandı (9. bölüm) |
| Arayüz görüntüleri | 10 görüntü, hepsi Türkçe (10. bölüm) |
| Simüle edilmiş gün (24 sa) | MicVST'nin olaylarında 0 tık (4.3) |
| Ölçüm raporu (Bölüm A/B/C) | 5.1, 5.2 |
| Motor testi, sanal cihazlarla, gerçek Windows iş parçacıkları ve MMCSS | aşağıda |

**Motor testi (W, `24dc9ae`):**

| Test | Sonuç |
|---|---|
| Tampon 128 / 256 / 480 / 512 / 960 / 1024 / 2048, her biri 30 sn, %30 eklenti yükü | tık 0, boşluk 0, kesinti 0 (hepsinde) |
| Gecikme: ölçülen / gösterilen (tampon 128 … 2048) | 21,4 / 24,5 · 29,9 / 35,9 · 44,7 / 55,4 · 81,1 / 103,1 · 149,3 / 192,4 ms |
| 13 zincir değişikliği çalarken (atla, ekle, kaldır, taşı, mono↔stereo) | tık 0; her değişiklikte 14–15 ms yumuşak iniş |
| Tampon değişikliği ×3 ve program kapanışı | tık 0; 103–169 ms sessizlik (cihazlar yeniden açılır) |
| Cihaz çıkar/tak, sürücü takılması | başka cihaza geçmedi (hoparlöre 0 ses), takılınca kendiliğinden açıldı; takılan sürücü denetimle 1 kez yeniden açıldı |
| Hatalı eklentiler (NaN, 1e30, istisna, 1,5 sn takılma, yavaş hazırlanma, yüklenemeyen) | çıkış hep sonlu, **tık 0** (bu turdaki düzeltmeden önce 2) |
| UI iş parçacığı her cihaz değişikliğinden sonra 3 sn kilitli (6 kez) | yalnızca değişikliğin kendi sessizliği; başka boşluk 0 |
| Gerçek VST3 (Airwindows), 16 değişiklik çalarken | tık 0 (6.1) |
| Uzun koşu 20 dk | tık 0, boşluk 0; THD+N (43 ms) dakikalık medyanlar −125 … −133 dB, en kötü pencere −116,6 dB; bellek 28,2 → 25,0 MB |
| Karışık koşu 10 dk (20 sn'de bir değişiklik, 5. dk'da tampon değişikliği) | tık 0; 28 planlı değişiklik boşluğu, açıklanamayan 0; bellek 28,2 → 26,7 MB |
| Giriş/çıkış karşılaştırması, 19 kayıt | açıklanamayan bulgu 0 (Airwindows'lu kayıt hariç, 6.2) |
| Ağ, motor testinin tamamı boyunca | 0 olay |

**Windows'ta doğrulanamayanlar:** gerçek WASAPI cihazı (CI makinesinde ses kartı yok), düşük
gecikme modundan paylaşımlı moda geçiş, gerçek bir USB/Bluetooth mikrofon ve VB-Cable, uyku /
uyanma, başka bir MMCSS ses uygulamasıyla (OBS, Discord, oyun) aynı anda çalışma, ekran
ölçeklendirmesi. Hata ayıklayıcılar (ASan/TSan) Windows'ta çalıştırılmadı.

---

## 9. İnternetsiz çalışma ve gizlilik

**Sonuç:** MicVST'nin kendisi denenen hiçbir durumda ağa bağlanmadı, ağ soketi açmadı ve ağ
kütüphanesi yüklemedi. Bu, Windows'ta çalışan `MicVST.exe` üzerinde ölçüldü (W). Bir sınır var:
VST3 eklentileri MicVST'nin içinde çalışır; bir eklentinin kendisi internete çıkmak isterse MicVST
bunu kendi başına durduramaz (9.4).

### 9.1 Kaynak kod ve derlenen dosya

- MicVST'de ağ kodu yok: güncelleme denetimi ve web bağlantıları kaldırıldı
  (`kararlilik/asama-2-kapali-kutu`), JUCE'un ağ bölümleri kapalı derlenir
  (`JUCE_USE_CURL=0`, `JUCE_WEB_BROWSER=0`).
- `MicVST.exe`'nin kullandığı Windows kütüphaneleri her CI derlemesinde denetlenir
  (`tools/check-no-network.ps1`): ADVAPI32, SHCORE, CFGMGR32, COMCTL32, COMDLG32, D2D1, D3D11, DCOMP, DWMAPI, DWRITE, DXGI, GDI32, IMM32, KERNEL32, OLE32, OLEAUT32, SHELL32, SHLWAPI, USER32, WINMM. Ağ kütüphanesi (WinINet, WinHTTP,
  Winsock/WS2_32, URLMon …) **yok**; biri eklenirse CI kırmızıya döner.

### 9.2 Çalışırken: ağ (W, commit `24dc9ae`, 15.10.2026)

`tools/ci-runtime-check.ps1`, `MicVST.exe`'yi aşağıdaki durumlarda gerçekten çalıştırır ve
yaptığı her şeyi kaydeder. Program için Windows Güvenlik Duvarı'nda giden ve gelen bağlantıyı
engelleyen kural açılır; böylece en küçük deneme bile "engellendi" olayı olarak görünür.
Denetimin çalıştığı her seferinde bir kontrol programıyla (curl) kanıtlanır.

| Durum | Süre | Windows Filtering Platform olayı (bağlantı, bağlama, engelleme) | Açık TCP/UDP | Yüklenen ağ DLL'i | Process Monitor TCP/UDP işlemi | Kapanış |
|---|---|---|---|---|---|---|
| İlk açılış (ayar yok, eklenti taraması – gerçek bir VST3 eklentisi kurulu) | 25 sn | 0 | 0 | 0 (58 DLL) | 0 | normal, kod 0 |
| Normal açılış (kayıtlı ayarlar) | 25 sn | 0 | 0 | 0 (57 DLL) | 0 | normal, kod 0 |
| Tepside açılış (`--tray`, Windows ile başlat) | 25 sn | 0 | 0 | 0 | 0 | normal, kod 0 |
| Bozuk ayar dosyası | 25 sn | 0 | 0 | 0 | 0 | normal, kod 0 |
| Bozuk eklenti önbelleği (yeniden tarama) | 25 sn | 0 | 0 | 0 | 0 | normal, kod 0 |
| Olmayan cihaz / eklenti / klasörü gösteren ayarlar | 25 sn | 0 | 0 | 0 | 0 | normal, kod 0 |

Kontrol programı (curl): 6 olay → denetim çalışıyor.
Ses motoru testi (`MicVSTEngineTest.exe`, gerçek VST3 eklentisiyle birlikte, tüm testler
boyunca): 0 olay.

Linux'ta (L) aynı motor, gerçek LSP eklentileriyle `strace -e trace=%network` altında çalıştı:
tek ağ çağrısı `socketpair(AF_UNIX)` – JUCE'un kendi mesaj döngüsü için işlem içi yerel bir boru;
ağa giden hiçbir çağrı (connect, sendto, bind …) yok.

### 9.3 Dosyalar ve gizlilik (W)

- **Yazdığı yerler:** yalnızca `%APPDATA%\MicVST` (`config.xml` ayarlar, `plugin_cache.xml`
  eklenti listesi, `log.txt` günlük, tarama sırasında `plugin_scan.tmp`) ve tarama yardımcısının
  sonuç dosyası `%TEMP%\micvst_scan.xml`. Bunların dışında yalnızca Windows'un her pencereli
  program için kendisinin tuttuğu kayıtlar (BAM: programın son çalışma zamanı; bildirim ayarı).
  Belgeler, Masaüstü vb. klasörlere hiç dokunmadı.
- **Okuduğu yerler:** kendi ayar klasörü, VST3 klasörleri (`C:\Program Files\Common Files\VST3`,
  `%LOCALAPPDATA%\Programs\Common\VST3`, senin eklediğin klasörler), geçici klasör, Windows ve
  program klasörü.
- **Günlük (`log.txt`):** Windows kullanıcı adı yazılmaz (`%USERPROFILE%` olarak görünür). Cihaz
  ve eklenti adları/yolları yazılır (sorun ararken gerekli); günlüğü paylaşırken bunlar görünür.
- **Ayar dosyası:** seçili cihazlar, eklenti zinciri (eklentilerin ayarları dahil), pencere
  konumu. Hiçbir yere gönderilmez.
- Bozuk ayar dosyası ve bozuk eklenti önbelleğiyle de program açıldı, normal kapandı (9.2).

### 9.4 Sınırlar

- **Eklentiler:** Bir VST3 eklentisi MicVST'nin içinde (tararken de tarama yardımcısının
  içinde) çalışır. Lisans/güncelleme denetimi yapan bir eklenti internete çıkmaya çalışabilir; bunu
  MicVST'nin kodu engelleyemez. Tam kapalı kutu istiyorsan Windows Güvenlik Duvarı'nda
  `MicVST.exe` için giden bağlantıyı engelle (13. bölümde komut var); bu, içindeki eklentileri de
  kapsar.
- CI makinesinde ses cihazı yok: gerçek ses cihazı açıkken ağ denetimi yapılmadı. WASAPI yerel
  bir Windows arayüzüdür; ağ beklenmez, ama ölçülmedi.

---

## 10. Türkçe arayüz

**Sonuç:** Kullanıcının gördüğü bütün metinler Türkçe, Türkçe harfler doğru, tasarım ve yerleşim
değişmedi (yalnızca metinler değişti). Görsel denetim Windows'ta yapıldı (W).

Nasıl denetlendi:

| Ne | Nasıl | Sonuç |
|---|---|---|
| Ana pencere, cihaz satırları, durum ve kesinti satırı, ipuçları, eklenti seçici, klasör menüsü, tepsi menüsü, metin kutusunun sağ tık menüsü, "Nasıl kullanılır" penceresi, tarama ekranı, bağlı olmayan cihaz | `MicVSTSnapshot` gerçek bileşenleri Windows yazı tipleriyle çizer; 10 görüntünün hepsine tek tek baktım (son iki derleme `aa6a886` ve `24dc9ae`; arayüz kodu ikisinde aynı) | Hepsi Türkçe, taşan ya da kesilen metin yok, yerleşim 1.2.0 ile aynı |
| JUCE'un kendi metinleri (Tamam/İptal/Evet/Hayır düğmeleri; Kes/Kopyala/Yapıştır/Sil/Tümünü seç/Geri al/Yinele; "(seçenek yok)") | çeviri tablosu, birim testi (B) | Türkçe |
| Cihaz hataları (WASAPI/JUCE'un İngilizce iletileri, ör. "No such device") | bu turda eklendi, birim testi (B) | Günlükte Türkçe ("Cihaz bulunamadı: …"). Önceki CI'da günlükte İngilizce bir satır görülmüştü – düzeltildi |
| Eski sürümlerin kaydettiği tarama nedenleri ("unresponsive", "crashed …") | birim testi (B) | Türkçe gösterilir |
| Türkçe harfler (ç ğ ı İ ö ş ü) | UTF-8 testi (B), günlük dosyası (W) | Doğru; günlükteki çift kodlama hatası giderildi |
| Sayılar | birim testi (B) | ondalık virgül ("49,7"), yüzde "%45" |
| Kaynak koddaki metinler | arayüz kodunda çevrilmemiş metin araması | Kalan İngilizce metin yok (kod ve kütüphane adları hariç: VST3, MMCSS, WASAPI, ms, Hz) |

Görülmeyen/denetlenmeyen:

- Windows'un kendi pencereleri (klasör seçme penceresi, ileti kutusunun çerçevesi) Windows'un
  dilindedir; MicVST bunları değiştiremez. Onay pencerelerinin (ör. eklentiyi kaldırma) metinleri
  kaynakta Türkçedir ama görüntüsü alınmadı.
- Ekran ölçeklendirmesi %125/%150 ile görüntü alınmadı.

---

## 11. Düzeltme: 1.2.0 raporundaki bir model hatası

1.2.0 raporunun 7.4 bölümündeki tablo, 1.2.0'ın mikrofon yolunu **sınırsız bir kuyrukla** modelliyordu.
Gerçekte 1.2.0 eklentileri mikrofonun yakalama iş parçacığında çalıştırıyordu; Windows'un yakalama
tamponu yalnızca ~2 paket tutar. Eklentiler bir süre gerçek zamandan yavaş kalınca bu tampon taşar
ve ses kaybolur ("microphone driver gaps" – senin Topaz günlüğünde 266 kez). Gerçekçi modelle
(yakalama tamponu 2 paket) 1.2.0'ın da ağır yükte tık ürettiği görülüyor (Windows CI, Bölüm A,
senaryo 4b: 20 tık, senaryo 8: 95 tık, senaryo 9: 806 tık). 1.2.1'in ayrı eklenti iş parçacığı bu
kaybı ortadan kaldırıyor (aynı senaryolarda 0 tık). 1.2.0 raporundaki "1.2: 0 tık" satırları bu
nedenle fazla iyimserdi.

---

## 12. Çözülemeyen ve doğrulanamayan noktalar

Aşağıdakiler ya çözülmedi ya da bu ortamda doğrulanamadı. Hiçbiri "test edildi" sayılmamalı.

**Doğrulanamayanlar**

1. **Gerçek donanım (G) hiç test edilmedi.** Gerçek mikrofon, VB-Cable, gerçek WASAPI sürücüleri,
   gerçek cihazda farklı tampon/örnekleme hızları, gerçek bir ses kartıyla MMCSS, cihazı gerçekten
   çekip takma, Bluetooth/USB cihazlar, başka bir programın cihazı tek başına tutması, uyku/uyanma.
   Motor sonuçlarının hepsi sanal cihazlarla alındı (L, W). Sanal cihazlar WASAPI gibi
   zamanlanır ama gerçek bir sürücünün tuhaflıklarını içermez.
2. **Windows'ta gerçek WASAPI yolu** (MicVST'nin 3 WASAPI yaması, düşük gecikme modundan paylaşımlı
   moda geçiş) yalnızca derlendi; CI makinesinde ses cihazı olmadığı için çalıştırılamadı.
3. **24 saat** yalnızca simülasyonda (S) çalıştı. Gerçek zamanlı en uzun koşular: Linux'ta
   2 saat (20 sn'de bir değişiklik, 5 dk'da bir cihaz değişikliği), Windows'ta 20 dakika kesintisiz + 10 dakika değişikliklerle.
4. **Hata ayıklayıcılar** (ASan, UBSan, TSan) yalnızca Linux'ta çalıştı; Windows'ta çalışmadı.
5. **Kayan nokta farkları:** simülasyonun rastgele sayıları Linux ve Windows'ta farklı dağılır;
   iki platformun 24 saatlik çizelgeleri aynı değil, sonuçlar yakın ama birebir değil.

**Çözülmeyenler / bilinçli sınırlar**

6. **Çöken eklenti MicVST'yi de kapatır.** Eklentiler MicVST'nin içinde çalışır; ses işlerken
   çöken (erişim hatası) bir eklenti programı kapatır. Tarama ayrı işlemde yapılır, orada çöken
   eklenti atlanır ve kaydedilir. Ses işlemeyi de ayrı işleme taşımak büyük bir değişiklik ve ek
   gecikme demek; bu turda yapılmadı. NaN/sonsuz/aşırı değer üreten ve istisna fırlatan eklentiler
   ise yakalanıyor (5. bölüm).
7. **Gecikmesini yanlış bildiren eklentiler:** LSP Limiter sesi 240 örnek (48 kHz'de 5 ms)
   geciktiriyor ama gecikmeyi 0 bildiriyor. Ses doğru, yalnızca MicVST'nin gösterdiği gecikme o
   kadar az. MicVST bunu bilemez.
8. **"Otomatik" güvenlik payı oturum boyunca yalnızca büyür** (her kesintide +5 ms, en çok 80 ms);
   program yeniden açılınca ya da bir değer seçilince sıfırlanır. Simüle edilmiş günde ilk
   render döneminde 80 ms'ye çıktı ve gecikme günün geri kalanında ~110 ms kaldı. Bilinçli bir
   seçim (her kesinti duyulur; biraz daha gecikme daha az kötü), değiştirilmedi. Render bitince
   düşük gecikme istersen MicVST'yi yeniden aç ya da sabit bir değer seç.
8b. **Bozuk eklenti çıktısında ~30–50 ms yumuşak sessizlik:** tık yerine (5.4). Tek bir bozuk
   blok için bile köprü yeniden dolar.
9. **Kablo takıldıktan sonra kırpma:** Kablo (çıkış cihazı) bir süre takılırsa biriken fazla
   gecikme 10 ms'lik çapraz geçişle kırpılır. Tık yok, ama o 10 ms'de kısa bir tını değişimi
   ölçülüyor (3. bölüm, birleştirme ölçümü). Öbür yollar daha kötü: gecikmeyi kalıcı büyütmek ya
   da sesi saniyelerce hızlandırmak (1.2.0 böyle yapıyordu, perde 8,6 cent kayıyordu).
10. **Bir kesintiden sonra hız düzeltmesi** birkaç saniye en çok ±500 ppm (0,9 cent) sürer. Bu
    düzey genelde duyulmaz, ama sıfır değil.
11. **Sistemin tamamı donarsa** (yakalama iş parçacığı da çalışamazsa) ses her tasarımda kaybolur;
    MicVST bunu yalnızca yumuşatabilir. Linux test makinesindeki donmalar (hipervizör) bu türdendi
    ve ayrı sayıldı.
12. **Eklentilerin ağ erişimi** MicVST tarafından engellenemez (9.4).
13. **Ölçüm yönteminin sınırları:** giriş/çıkış karşılaştırması sinyalin %2'sinden küçük tek
    örneklik bozulmaları göremez (3.1); dinamik eklentili zincirlerde eşik %17–20'ye çıkar. Windows'ta
    gerçek eklentiyle (Airwindows) birebir karşılaştırma anlamlı sonuç vermedi (6.2). Linux sanal
    makinesi testler sırasında sık donduğu için L'deki bazı ölçümler (ör. son "flutter" koşusu)
    bozuldu; bunlar için W sonuçları esas alındı.
15. **TSan, gerçek LSP eklentisiyle 7 uyarı** verdi (7.3); eklentinin kendi iş parçacığından JUCE geri çağrıları – ayrıntılı incelenmedi.
14. **Linux'ta bulunan tek "açıklanamayan" bulgu** (LSP zinciri + konuşma, 3.2) büyük olasılıkla
    eklentinin kayıp girişe tepkisi; kesin olarak ayrıştırılamadı.

---

## 13. Kendi bilgisayarında kontrol (önerilen)

Benim ölçemediğim şeyleri (12. bölüm) ancak senin bilgisayarında görebiliriz. Kısa bir yol:

1. **Sürüm:** Pencerenin köşesinde `v1.2.1` yazmalı.
2. **Günlük:** `%APPDATA%\MicVST\log.txt` (Başlat → Çalıştır → `%APPDATA%\MicVST`). Açılışta
   "Eklenti iş parçacığı: MMCSS 'Pro Audio' …" satırı eklentilerin yüksek öncelik aldığını
   gösterir. "MMCSS kullanılamıyor" yazıyorsa bana bildir.
3. **Kesinti sayaçları:** Alttaki durum satırında "kesinti N" yazar; üzerine gelince ayrıntı
   çıkar (çıkışta ses kesildi, mikrofon sürücüsünde kayıp, eklenti işleme takıldı, gecikme
   düzeltmesi …). Normal kullanımda (video render yokken) 0 kalması beklenir – bu, gerçek
   donanımda henüz doğrulanmadı. Render sırasında sayılar artarsa hangi satırın arttığı bana
   yeterli bilgi verir.
4. **Ses karşılaştırması (isteğe bağlı):** Audacity'de aynı anda iki iz kaydet: mikrofonun
   kendisi ve "CABLE Output". Zincir boşken (eklentisiz) ikisi aynı ses olmalı, yalnızca birkaç
   on milisaniye kayık.
5. **Cihazı çek-tak:** Mikrofonu çıkar → cihaz "(bağlı değil)" görünmeli, başka mikrofona
   geçmemeli; tak → bir saniye içinde kendiliğinden açılmalı.
6. **Tam kapalı kutu (eklentiler dahil):** depodaki `tools/firewall-block.ps1` (teslim klasöründe de
   var), yönetici PowerShell'de:
   ```powershell
   .\firewall-block.ps1 -Exe "C:\...\MicVST.exe"    # kuralı ekler (gelen + giden engellenir)
   .\firewall-block.ps1 -Remove                      # kuralı kaldırır
   ```
   Kural `MicVST.exe`'nin yoluna bağlıdır; dosyayı taşırsan yeniden çalıştır. İnternetten
   etkinleştirme isteyen eklentileri önce kendi lisans programlarıyla etkinleştir (onlar ayrı
   programdır, kuraldan etkilenmez).
7. Bir tık/çıtırtı duyarsan saatini not et ve `log.txt`'yi gönder; günlükte o saniyedeki olaylar
   (kesinti, kırpma, cihaz hatası) görünür.
