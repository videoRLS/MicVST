# C2 - 997 Hz −6 dBFS THD+N'nin turdan tura değişmesi: gerileme mi? (23 Ekim 14:45-15:20)

`quality` modundaki 997 Hz −6 dBFS THD+N medyanı (köprü dahil, kablo kaydı):

| Koşu | Kod | Ortam | THD+N medyan |
|---|---|---|---|
| Görevdeki temel değer | 62ff06b | W | −110,6 dB |
| Tam tur #1 | 1be456e (gece değişikliklerinden önce) | W | −108,2 dB |
| Tam tur #2 | c0515a9 | W | −101,8 dB |
| Ek tur #2b | 93cb4b7 | W | −98,7 dB |
| Uzun tur #1, win11arm | 1be456e | A | **−98,2 dB** |
| Uzun tur #2, win11arm | c0515a9 | A | −99,9 dB |
| Yerel A/B (aynı makine, art arda) | 1be456e | L | −96,5 / −96,0 dB |
| Yerel A/B (aynı makine, art arda) | HEAD | L | −88,4 / −117,4 dB |

Aynı ölçüm **eski kodla da** −96 ile −108 dB arasında dağılıyor (A'da eski kod −98,2). Ölçüm, köprünün saat farkı
düzeltmesinin anlık dalgalanmasına (flutter) duyarlı; o da makinenin zamanlamasına bağlı (yerelde flutter RMS
eski kodla 1,6-298 ppm, yeni kodla 1,6-317 ppm). Uzun koşularda aynı donanım sınıfında fark yok:

| 330 dk long | THD+N medyan / dakika | en kötü 43 ms pencere |
|---|---|---|
| Uzun tur #1 (1be456e) | −132,7 … −120,3 dB | −102,7 dB |
| Uzun tur #2 (c0515a9) | −133,9 … −122,1 dB | −102,3 dB |

1be456e → c0515a9 arasında ses yoluna dokunan değişiklik yok (yalnız kesinti sayacının başlangıç tabanı, kayıt
yuvarlaması, zamanlayıcı sayaçları, spektrum sayacı, arayüz). Sonuç: **gerileme kanıtı yok**; kısa `quality`
testinin −105 dB ölçütü bu ortamlarda tekrarlanabilir değil (temel değer de tek koşudan). Ölçüt raporda "karşılanmadı
(ortama duyarlı, gerileme değil)" diye yazılır.
