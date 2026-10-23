# MicVST 1.2.2 – Uzun ömür kılavuzu

Bu kılavuz, MicVST'yi yıllarca sorunsuz kullanabilmen için kendi başına yapabileceğin işleri anlatır:
neyi yedekleyeceğin, ara sıra yapacağın 10 dakikalık kontrol, bir şey bozulursa nasıl geri döneceğin
ve kaçınman gerekenler. Burada yazan her şey 22–23 Ekim 2026 testlerinde senin bilgisayarında
(Windows 10 Pro 22H2, HyperX QuadCast 2 S, VB-Cable 3.3.1.7) ya da GitHub'daki test makinelerinde
**ölçüldü**. Ölçülmemiş bir şeyi tavsiye olarak yazmadım.

---

## 1. Neyi, nereye yedeklemeli

| Ne | Nerede | Ne zaman |
|---|---|---|
| **MicVST.exe** ve SHA-256'sı | `C:\MicVST\MicVST.exe` | Her yeni sürümde. Bir önceki sürümü de `MicVST_onceki.exe` adıyla sakla. |
| **Ayarların ve zincirin** | `%APPDATA%\MicVST\config.xml` | Zincirde ya da ayarlarda önemli bir değişiklikten sonra. |
| **Kendi bildirim seslerin** | `%APPDATA%\MicVST\Sesler\` | Yeni ses eklediğinde. |
| **Kendi hazır ayarların** | MicVST'de hazır ayar menüsü → **"Hazır ayarı dosyaya aktar"** (`.micvstpreset`) | Her hazır ayarı kaydettiğinde. |
| **Arşiv paketi** | `MicVST_1.2.2_arsiv.zip` (teslimde verildi) | Bir kez; dokunma. Yıllar sonra MicVST'yi yeniden derlemek için gereken her şey içinde. |
| Eklenti kurulumları ve lisansları | FabFilter, Clear, Klanghelm, TDR, Waves… hesapların | Eklenti güncellemeden önce, çalışan sürümün kurulum dosyasını sakla. |
| VB-Cable kurulumu | VB-Audio sitesi, **sürüm 3.3.1.7** (test edilen) | Bir kez. |

`%APPDATA%\MicVST` klasörünü açmak için: **Win + R** → `%APPDATA%\MicVST` → Enter.

**En az iki yer kullan:** biri harici disk ya da USB bellek, biri bulut klasörü.

**Exe'nin bozulmadığını kontrol etmek:** PowerShell'de

```
Get-FileHash C:\MicVST\MicVST.exe
```

Çıkan uzun sayı, sana verilen SHA-256 ile birebir aynı olmalı.

---

## 2. 10 dakikalık sağlık kontrolü

**Ne zaman:**
- Büyük bir Windows güncellemesinden sonra
- Ses sürücüsü, VB-Cable ya da bir eklenti güncellemesinden sonra
- Ayda bir

1. **Durum satırı (1 dk).** MicVST penceresinin altındaki satırda **"Çalışıyor · gecikme … · kesinti 0"** görmelisin.
   - "kesinti" sayısı kullanırken artıyorsa mikrofondan ses kaybolmuş demektir. 4. adımdaki kaydı dinle.
   - Gecikme senin zincirinle yaklaşık 290 ms (testte ölçüldü). Büyük ölçüde Pro-Q 4'ün lineer faz modundan geliyor. Bundan çok farklıysa bir şey değişmiştir.
2. **Ses biçimi (1 dk).** `%APPDATA%\MicVST\log.txt` dosyasını Not Defteri ile aç. En alttaki **"Ses biçimi"** satırını bul. Test edilen düzen buydu:
   ```
   Ses biçimi: MicVST akışı ve iç işlem 32 bit kayan nokta; Windows aygıt biçimi: giriş 32 bit PCM, 48000 Hz, 2 kanal | çıkış 24 bit PCM, 48000 Hz, 2 kanal
   ```
   Farklıysa Windows bir aygıtın biçimini değiştirmiştir. MicVST farklı biçimlerde de çalışıyor (44,1 ↔ 48 kHz dönüşümünde seviye farkı 0,01 dB'den az ölçüldü). Yine de test edilen düzene dönmek için: Ses Denetim Masası → Kayıt → **CABLE Output** → Özellikler → Gelişmiş → **24 bit, 48000 Hz**. Aynısını Kayıttan Yürütme → **CABLE Input** için de yap.
3. **Sustur ve bildirim sesi (1 dk).** **Sustur kısayoluna** bas (varsayılan F9; Ayarlar'da değiştirdiysen kendininkine): "kapanma" sesini duymalısın. Tekrar bas: "açılma" sesini duymalısın. Susturulmuşken mikrofon ölçeri hareket etmeye devam eder, bu normal.
4. **30 saniyelik kayıt (3 dk).**
   - Ayarlar → Kayıt: **WAV 24 bit, 48 kHz** (varsayılan).
   - **● Kayıt**'a bas, 30 saniye normal konuş, **■**'e bas.
   - Dosya: `Belgeler\MicVST Kayıtları`. Kulaklıkla dinle: tık, cızırtı, robot sesi, kopma olmamalı.
5. **Discord'da kısa deneme (3 dk).** Bir arkadaşınla konuş ya da Discord'un Ses ve Görüntü ayarlarındaki mikrofon testini kullan. Giriş aygıtı **CABLE Output** olmalı.
6. **İstersen Claude'a kontrol ettir.** 4. adımdaki WAV dosyasını ve `log.txt`'yi bir Claude sohbetine ekle ve şunu yaz:

   > Ekteki MicVST 1.2.2 kaydını (WAV 24 bit) ve log.txt dosyasını incele. Kayıtta tık, boşluk ya da kırpma var mı?
   > log.txt'de "Köprü", "kesinti", "Denetim", "gerçek zamandan uzun sürdü" ve "Hata" geçen satırlar var mı, varsa ne zaman?
   > Son "Ses biçimi" satırı 24 bit / 48 kHz mi? Kısa ve Türkçe cevap ver; sorun varsa ne yapmam gerektiğini söyle.

Altısı da yolundaysa kontrol bitti.

---

## 3. Bir şey bozulursa: geri dönüş

1. MicVST'yi kapat: tepsi simgesine sağ tıkla → **Kapat**.
2. **Ayarları geri koy:** yedeğindeki `config.xml`'i `%APPDATA%\MicVST\` içine kopyala, üzerine yaz.
3. **Önceki exe'ye dön:** `MicVST_onceki.exe`'yi `C:\MicVST\MicVST.exe` olarak kopyala ve SHA-256'sını kontrol et (bölüm 1).
4. MicVST'yi aç ve 2. bölümdeki kontrolü yap.

Bilmen iyi olur (ölçüldü):
- **Bir eklenti eksik olsa da MicVST açılır.** Ayarlarda olup bilgisayarda bulunamayan eklentiyle açılış test edildi.
- **Mikrofon USB'den çıkıp aynı adla geri gelirse MicVST kendiliğinden yeniden bağlanır.** Başka bir mikrofona ya da hoparlöre asla geçmez.
- **Kayıt sürerken MicVST zorla kapanırsa** (elektrik, Görev Yöneticisi), kayıt kapanmadan önceki son 1 saniyeye kadar açılabilir. Testte 65,5 s'lik kaydın 64,8 s'si açıldı.
- **Ayar dosyası güvenle yazılır.** Test makinesinde MicVST 1000 kez açılıp kapatıldı, bunun 200'ünde açılırken zorla öldürüldü: ayar dosyası bir kez bile bozulmadı, hiçbir ayar kaybolmadı. Arkada en çok birkaç küçük `*_temp*.xml` / `*.TMP` dosyası kalabiliyor; zararsız, silebilirsin.
- **MicVST'yi kapatmak için tepsideki simgeye sağ tıkla → Kapat.** Pencerenin X'i MicVST'yi kapatmaz, tepsiye gizler. Görev Yöneticisi'nden "Görevi sonlandır" dersen MicVST zorla kapatılır ve Windows olay günlüğüne "MicVST yanıt vermiyordu" diye yazar. Bu bir arıza değil, testte bu yolla yeniden üretildi. Ayarlar da bozulmaz: zorla kapatma yukarıda ölçüldü.

---

## 4. Yapmaman gerekenler (ölçülenler)

- **Sanal kabloyu (CABLE Input) Windows'un varsayılan çıkışı yapma.**
  - Bilgisayarın bütün sesleri (bildirimler, YouTube, oyun) yayına ve Discord'a gider.
  - MicVST bunu fark eder ve bildirim seslerini çalmaz, karşı taraf duymasın diye. Log'a "Bildirim sesi çalınmadı: Windows'un varsayılan çıkışı sanal kablo" yazar (ölçüldü).
  - Varsayılan çıkış kulaklığın ya da hoparlörün olmalı.
- **Tepsi menüsünden hazır ayar seçmenin zinciri değiştirdiğini unutma.**
  - Seçilen hazır ayar o anki zincirin yerine geçer (23 Ekim'de denerken oldu).
  - Kendi zincirini her zaman bir hazır ayar olarak kaydet ve dosyaya aktar.
- **Zincir çok ağırlaşırsa:** log'da sık sık "İşleme: … blok gerçek zamandan uzun sürdü" satırı görür ya da kayıtta tık duyarsan:
  - Senin **Güvenlik payı** ayarın **80 ms** (Ayarlar → Ses). Bütün testler bu ayarla yapıldı ve ses kesintisi ölçülmedi; düşürme.
  - Zinciri hafifletmek en etkili yol: örneğin Pro-Q 4'ü "Zero latency" / "Natural phase" moduna al (gecikme de ~100 ms azalır).
  - Bugünkü zincirinle bu satırlar ara sıra çıkıyor, çoğu sen bilgisayarda başka iş yaparken. 20 saatlik izlemede ve 60 dakikalık kayıtta ses kesintisi 0 ölçüldü; hız en çok 0,26 cent değişti (duyulmaz).
- **Kapanışta "MicVST çöktü" kaydı:**
  - Zincirde Clear varken MicVST kapanırken Windows olay günlüğüne bu kayıt düşebiliyor (10 kapanışın 8'inde ölçüldü). Tepsiden hazır ayar seçip Clear'ı zincirden çıkarınca da bir kez düştü; MicVST çalışmaya devam etti.
  - Ayarlar ve kayıtlar bundan etkilenmiyor (ölçüldü). Görmezden gelebilirsin.
  - Sorun Clear'ın kapanışında; Clear güncellendiğinde kendiliğinden düzelebilir.

---

## 5. Windows hakkında

- Bilgisayarın **Windows 10 Pro 22H2**. Microsoft, ev kullanıcıları için güvenlik güncellemelerini (ESU programı) **12 Ekim 2027**'ye kadar uzattı. Kayıt Windows Update'ten yapılıyor; bazı yolları ücretsiz.
- MicVST 1.2.2, GitHub'da Windows 11 (ARM) ve Windows Server 2025 üzerinde de test edildi.
- Windows 11'e geçersen, geçtikten sonra 2. bölümdeki kontrolü bir kez yap.

---

## 6. Hızlı özet

- Yedekle: exe + SHA, `config.xml`, `Sesler`, hazır ayar dosyaları, arşiv zip'i. En az iki yere.
- Güncellemeden sonra ve ayda bir: durum satırı, Ses biçimi satırı, Sustur kısayolu, 30 s kayıt, Discord.
- Bozulursa: kapat → `config.xml`'i geri koy → gerekirse önceki exe → kontrol.
- Kabloyu varsayılan çıkış yapma. Kendi zincirini hazır ayar olarak sakla.
