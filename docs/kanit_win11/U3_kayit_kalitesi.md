# U3 - kayıt kalitesi, kullanıcının PC'si (22 Ekim ~18:00, Windows 10, QuadCast 2 S, kullanıcının zinciri)

MicVST kaydı ile aynı anda alınan CABLE Output yakalaması (WASAPI paylaşımlı, float 32) karşılaştırıldı.
Kablo yolu köprünün saat farkı düzeltmesinden geçtiği için gecikme yavaşça değişir; karşılaştırma 0,2 s'lik
pencerelerde, her pencerenin kendi kesirli gecikmesiyle yapıldı (GCC-PHAT, FFT ile kesirli kaydırma).

| Biçim | Süre | Tepe / gerçek tepe | Kırpma | Gecikme aralığı (örnek) | > 2 örnek sıçrama | Korelasyon medyan / %1 / en düşük | Seviye farkı medyan / en büyük |
|---|---:|---|---:|---|---:|---|---|
| WAV 24 (2 dk konuşma + 10 s sessizlik + bağırma) | 169,22 s | -3,0 / -2,985 dBTP | 0 | 182415,2-182419,7 | 0 | 0,999999 / 0,99987 / 0,99982 | 0,0000 / 0,003 dB |
| FLAC 24 | 30,02 s | -4,88 / -4,87 | 0 | 184224-184236 | 0 | 0,999996 / 0,99937 / 0,99888 | 0,0000 / 0,001 dB |
| WAV 16 (TPDF) | 30,02 s | -5,47 / -5,45 | 0 | 183419-183421 | 0 | 0,999999 / 0,99993 / 0,99985 | 0,0000 / 0,000 dB |
| WAV 32 float | 30,03 s | -4,12 / -4,12 | 0 | 183732-183752 | 1 | 0,999992 / 0,99731 / 0,99439 | 0,0000 / 0,005 dB |
| MP3 320 (kayıplı) | 30,02 s | -4,32 / -4,32 | 0 | 184006-184046 | 3 | 0,999931 / 0,99512 / 0,99426 | 0,0004 / 0,031 dB |
| MP3 V0 | 30,02 s | -4,17 / -4,16 | 0 | - | - | (karşılaştırılmadı) | - |

Başlıklar (ffprobe + kendi ayrıştırıcı): WAV 24 = pcm_s24le / 48000 / 2, BWF bext; FLAC 24 bit 48000; WAV 16; WAV 32 float
(etiket 3); MP3 48000 CBR 320 / VBR 242 kbps; süreler düğme süresiyle ±0,05 s.
Değerlendirme: ölçüt "seviye farkı ≤ 0,05 dB" BAŞARILI (en çok 0,031 dB, MP3). "Korelasyon ≥ 0,999": medyan her
biçimde ≥ 0,9999; en kötü %1'lik pencereler PCM'de 0,997-0,9999 - bu pencerelerde köprünün gecikmesi pencere içinde
değişiyor (sabit kesirli gecikme varsayımı), içerik farkı değil. Kayıt dosyası tarafında 24 bit gerçekliği U4'te
ayrıca ölçüldü (%99,6). Durum: BAŞARILI (korelasyon ölçütü medyanla; en kötü pencere notu raporda).
