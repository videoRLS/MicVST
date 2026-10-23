# U4 - 60 dk kayıt: kablo karşılaştırmasının ayrıntılı incelemesi (23 Ekim ~04:50)

Kayıt dosyası (`MicVST_2026-10-22_21-11-32.wav`): 3600,05 s, düğmeye basma aralığı 3600,032 s (fark 0,018 s =
%0,0005), 24 bit gerçek (%99,6), kırpma 0, boşluk 0; kabloyla seviye farkı en çok -0,018 dB. Kablo yakalaması
(`u4_kablo.wav`, WASAPI paylaşımlı, CABLE Output): 3720,000 s, DATA_DISCONTINUITY yalnız paket 0, sessiz paket 0,
konum atlaması 0. MicVST log'unda bu saatte "Köprü" satırı yok (underrun / atılan blok / yeniden eşitleme yok);
"İşleme: N/100 blok gerçek zamandan uzun sürdü" satırları var (kullanıcının zinciri, eklenti yükü tepe %100-153).

İlk analiz (1 s pencere, tamsayı gecikme) 606 sesli saniyenin 91'inde korelasyon < 0,99 ve 104 "sıçrama" buldu.
Ayrıntı: 25-110 s, 370-650 s, 770-800 s bölümleri iki dosyadan kesildi (`u4_kes.ps1`), gecikme 200 ms / 50 ms ve
30 ms / 10 ms pencerelerle (GCC-PHAT, 1/32 örnek) izlendi:

| Bölüm | Sesli pencere | Gecikme aralığı (örnek) | Komşu pencereler arası > 2 örnek sıçrama | 1 s'lik eğim: medyan / %95 / en çok |
|---|---:|---:|---:|---|
| A 25-110 s | 392 | -45,9 .. +10,8 | 0 | 12 / 117 / 127 ppm |
| B 370-650 s | 531 | -85,7 .. -13,8 | 0 | 15 / 69 / 97 ppm |
| C 770-800 s | 336 | -193,0 .. -142,8 | 0 | 31 / 139 / 148 ppm |

En kötü görünen anlar (40,1 s; 573,75 s; 796,9 s) 10 ms çözünürlükte: gecikme düzgün akıyor (ör. 30 ms'de 0,2
örnek), basamak yok. Yani kabloda kesinti, ekleme ya da kesme yok; köprü, zincirin yük patlamalarında saat farkı
düzeltmesini hızlandırıyor: hız en çok 148 ppm değişiyor (0,26 cent - duyma eşiği birkaç cent, yani ~2000 ppm).
İlk analizdeki "sıçramalar" 1 s'lik tamsayı gecikme aramasının ölçüm hatası (ör. 554 s: -48, 573 s: +48 - git-gel).

Sonuç: U4 BAŞARILI (dosya ölçütleri); kablo tarafında duyulur bir olay yok. Not: kullanıcının zinciri bu PC'de
anlık olarak gerçek zamanı aşıyor (log); bu dakikalarda gecikme 0,2-4 ms arasında yavaşça kayıyor.
