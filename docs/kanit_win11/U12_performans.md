# U12 - performans, kullanıcının zinciri (23 Ekim 04:50-05:00, Windows 10, 16 mantıksal işlemci)

| Aşama | MicVST işlemci (bir çekirdeğin %si, 5 dk ort.) | Özel bellek | Durum satırı |
|---|---:|---:|---|
| Kayıt kapalı | %45,28 | 261,4 MB | İşlemci %40-46, gecikme 286-289 ms, kesinti 1 (sabit) |
| Kayıt açık (WAV 24) | %44,81 | 275,5 MB | İşlemci %40-47, gecikme 286-290 ms, kesinti 1 (sabit) |

Kayıt açıkken işlemci artışı: -0,47 puan (ölçüt ≤ 3) - BAŞARILI. Bellek +14 MB (kaydın 30 s FIFO tamponu). Tüm makinenin işlemcisi olarak: ~%2,8.
Not: PC'deki exe (22 Ekim 20:01) "kesinti" düzeltmesinden önceki derleme; "kesinti 1" Windows'un başlangıç işareti (1.2.2'de düzeltildi, motor testi startxrun). 10 dk boyunca 1'de kaldı: gerçek kesinti 0.
Gecikme 288 ms: kullanıcının zinciri (Pro-Q 4 lineer faz 106,7 ms + diğer eklentiler) + güvenlik payı.

Ham kayıt: C:\MicVST\test\u12.txt (her 10 s bir satır).
