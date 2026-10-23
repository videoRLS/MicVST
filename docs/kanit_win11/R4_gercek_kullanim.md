# R4 / U14 – Gerçek kullanım (kullanıcının PC'si)

Bilgisayar: Windows 10 Pro 22H2, HyperX QuadCast 2 S → MicVST (kullanıcının ağır zinciri: Pro-Q 4 lineer faz, Clear, …) → VB-Cable.
İzleme betiği 10 dakikada bir: süreç, bellek, tutamaç, GDI/USER, iş parçacığı, CPU süresi, öncelik, pencere yanıt veriyor mu,
log.txt'ye yeni satırlar, Windows olay günlüğünde MicVST olayları. Ham veriler: `R4/izleme.csv`, `R4/olaylar.txt`, `R4/gece_son.txt`.

PC saati bilerek 3 saat geride. Aşağıdaki saatler **PC saatidir** (gerçek = PC + 3 sa).

## Süreçler

| Süreç | Exe | Başlangıç → son görülme | Süre | Nasıl bitti |
|---|---|---|---|---|
| 10672 | `C:\MicVST\MicVST.exe` (tur #2 yapımı) | 22 Eki 18:53 → 23 Eki 01:43 | ~6 sa 55 dk | U5'te bilerek öldürüldü |
| 15024 | aynı | 01:49 → 02:25 | 36 dk | Askı kaydı 02:25:59 – Görev Yöneticisi ile kapatma izi (bulgu #13) |
| 15672 | `yeni\MicVST.exe` (titreme düzeltmesi) | 02:26 → 02:43+ | | kullanıcı kapattı, olay yok |
| 15596 | `C:\MicVST\MicVST.exe` | 02:51:58 → 02:52:31 | 33 s | Askı kaydı 02:52:31 – aynı (bulgu #13) |
| 8732, 11184, … | `yeni2\MicVST.exe` | 02:52 → 03:05 | | bilerek yapılan kapanış denemeleri (Clear çıkış olayı, bulgu #6) |
| **15868** | `yeni2\MicVST.exe` | **03:05:41 → 16:29:46** | **13 sa 24 dk kesintisiz** | Kullanıcı Görev Yöneticisi ile kapattı (son exe'ye geçerken) |
| 2520, 6460, 15884, 11560 | son exe `C:\MicVST\MicVST.exe` | 16:29 → 18:05 | | U15 (sağlık kontrolü), tepsiden kapat-aç denemeleri, #13 yeniden üretimi |

İzleme toplam: 22 Eki 19:02 → 23 Eki 18:05 (PC) = **23 sa 3 dk**, 138 örnek. (Bitişi 18:05 gerçek saat olarak düşünülmüştü; PC saati 3 sa geri olduğundan 21:05 gerçek saatte bitti.)

## Gece başlayan süreç (15868, 80 örnek, 13 sa 24 dk)

| Ölçü | İlk | En az | En çok | Son |
|---|---|---|---|---|
| Özel bellek (MB) | 248,5 | 248,4 | 252,8 | 252,7 |
| Çalışma kümesi (MB) | 238,4 | 238,4 | 244,8 | 244,3 |
| Tutamaç | 620 | 606 | 620 | 608 |
| GDI | 50 | 50 | 50 | 50 |
| USER | 46 | 45 | 47 | 47 |
| İş parçacığı | 33 | 30 | 35 | 32 |

- CPU ortalama bir çekirdeğin **%47**'si (U12 ile aynı); öncelik 80/80 örnekte **Normal**; pencere 80/80 örnekte **yanıt veriyor**.
- Bellek 13 saatte +4,3 MB, tamamı ilk saatlerde; sonra ±0,1 MB. Tutamaç / GDI / USER / iş parçacığı sabit → sızıntı yok.
- Windows olay günlüğünde bu süreç için olay **0**.

## log.txt (20 saat, bütün süreçler)

- "Köprü: çıkışta ses kesildi", "giriş bloğu atıldı", "gecikme hedefe geri çekildi": **0**
- "mikrofon sesinden … ms atlandı", "giriş kuyruğu doldu": **0**
- "Eklenti zinciri kullanılamaz ses verdi": **0**
- "İşleme: N / 100 blok gerçek zamandan uzun sürdü": 10 841 satır. Ağır zincirin ara sıra tek bir bloğu geç bitiriyor; güvenlik payı emiyor
  (sesin girişte en çok beklediği süre: 10 817 satırda 0 ms, 24 satırda 10–50 ms; güvenlik payı bunun üstünde). Saatlere göre: kullanıcı
  bilgisayarı kullanırken saatte 400–2500, gece 0–14. Yani diğer programların yükü tetikliyor; ses kesintisi doğurmadı (köprü satırı 0).
- log.txt her açılışta son 256 KB'a, çalışırken 2 MB'ı geçince yine son 256 KB'a kırpılıyor (kod). Açılıştaki kırpma ölçüldü (02:53, dosya okunur kaldı); 20 saatte dosya 1,2 MB'a çıktı, 2 MB sınırına varmadı.

## Windows olay günlüğü (20 saat)

| PC saati | Olay | Süreç / exe | Açıklama |
|---|---|---|---|
| 02:04:43 | Application Error 1000, 0xc0000005, modül bilinmiyor | 15024, eski exe | Kullanıcı tepsiden hazır ayar seçti (02:04:42 "zincir değişti") → eski zincirdeki Clear kapatılırken. **MicVST çalışmaya devam etti** (log ve izleme sonraki 20 dk boyunca normal, pencere yanıt veriyor). Bulgu #6 ile aynı imza |
| 02:25:59 | Application Hang 1002 ("Top level window is idle") | 15024, eski exe | Son log satırı 97 s önce: dil → zh. Bulgu #13: Görev Yöneticisi ile kapatmanın izi (yeniden üretildi) |
| 02:52:31 | Application Hang 1002 | 15596, eski exe | Açılıştan 33 s sonra; son log satırı 16 s önce: dil → en. Bulgu #13 (aynı) |
| 03:00–03:05 | Application Error 1000 × 10 | yeni2 (çıkış değişikliği denemesi) | Bilerek yapılan kapanış denemeleri, Clear (bulgu #6) |

03:05:41 → 16:29 (gece başlayan süreç, 13 sa 24 dk): **olay 0**; 16:29:46 Görev Yöneticisi ile kapatma kaydı (#13). Son exe ile 16:29 → 18:05: tepsiden 4 kapanışın 3ünde bilinen Clear kaydı (#6), 17:52:35 #13 yeniden üretimi.

## Son 30 s kontrol kaydı

**ENGELLENDİ.** 15:00'te betik MicVST penceresini bulamadı: pencere tepsiye kapalıydı ve betik bilerek pencereyi kendisi açmıyor (kullanıcının
oturumuna dokunmamak için). Süreç çalışıyordu (15:04 örneği). Yerine U15 provasındaki 30 s kayıt kullanılır.

## Sonuç

R4 (bu teslimde ≈ 20 sa; 72 sa – 7 gün ATLANDI, süre): son süreç ~12 sa kesintisiz, bellek / tutamaç sabit, öncelik Normal, pencere hep yanıt verdi,
ses yolunda kesinti satırı 0. İki askı kaydı eski exe ile, kullanıcının elle yaptığı denemeler sırasında. Sonradan açıklandı (bulgu #13): MicVST'yi Görev Yöneticisi → Görevi sonlandır ile kapatmak tam bu kaydı üretiyor (23 Ekim 17:52:35'te son exe ile yeniden üretildi); tepsiden Kapat ile 4 kapanışta askı 0.
