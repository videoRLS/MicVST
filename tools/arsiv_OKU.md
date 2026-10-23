# MicVST 1.2.2 – arşiv paketi

Bu paket, MicVST 1.2.2'yi yıllar sonra internete ihtiyaç duymadan yeniden derleyebilmen ve
test edilen hâline geri dönebilmen için hazırlandı.

- Kaynak commit: `@COMMIT@` (GitHub: `videoRLS/MicVST`, dal `kararlilik/oncelik`)
- Teslim edilen exe'nin SHA-256'sı: `@SHA@`

## İçerik

| Klasör / dosya | Ne |
|---|---|
| `exe/MicVST.exe`, `exe/MicVST.exe.sha256` | Test edilen program ve özeti. PowerShell: `Get-FileHash exe\MicVST.exe` aynı sayıyı vermeli. |
| `kaynak/MicVST/` | MicVST'nin kaynak kodu (yukarıdaki commit). LAME 4.0 (MP3) `third_party/` içinde. |
| `kaynak/JUCE-9.0.4/` | JUCE 9.0.4 kaynağı, **MicVST'nin 5 gerçek zamanlı yaması uygulanmış hâlde** (`cmake/PatchJuce.cmake` bir kez çalıştırıldı). |
| `kaynak/JUCE-9.0.4_MicVST_yamalari.diff` | Yamaların özgün JUCE 9.0.4'e göre farkı (inceleme için). |
| `belgeler/` | Test raporu, uzun ömür kılavuzu, ayar yedeği (`config.xml`), test edilen sürümler listesi. |

## Yeniden derleme (Windows, internet gerekmez)

Gerekenler: Windows 10/11 x64, **Visual Studio 2022** (C++ masaüstü geliştirme), **CMake 3.22+** (Visual Studio ile gelir).

Bu paketi örneğin `C:\arsiv\MicVST_1.2.2_arsiv` olarak açtıysan, "x64 Native Tools Command Prompt for VS 2022"de:

```
cd C:\arsiv\MicVST_1.2.2_arsiv\kaynak
cmake -S MicVST -B build -A x64 -DFETCHCONTENT_SOURCE_DIR_JUCE=C:/arsiv/MicVST_1.2.2_arsiv/kaynak/JUCE-9.0.4 -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build --config Release --target MicVST
```

Çıkan exe: `build\MicVST_artefacts\Release\MicVST.exe`.

**Önemli:**
- `FETCHCONTENT_SOURCE_DIR_JUCE` verildiğinde CMake JUCE'ye yamaları **kendisi uygulamaz**. Bu yüzden paketteki JUCE zaten yamalı geliyor.
- Başka bir JUCE kopyası kullanırsan, önce onun içinde bir kez `cmake -P <MicVST>/cmake/PatchJuce.cmake` çalıştır. Betik tekrarlanabilir; bir parça eşleşmezse hata verir.
- Yamaların yerinde olduğunu doğrula: `kaynak\JUCE-9.0.4\modules\juce_audio_devices\native\juce_WASAPI_windows.cpp` içinde "MicVST patch" **3 kez** geçmeli. PowerShell: `(Select-String -Path <dosya> -Pattern "MicVST patch").Count`.

Yeni derlenen exe bayt bayt aynı olmaz, çünkü MSVC zaman damgası yazar. Davranış aynıdır. Testler de aynı yolla derlenir: `--target MicVSTTests` → `build\MicVSTTests_artefacts\Release\MicVSTTests.exe`, "All tests completed successfully" yazmalı.

Bu yöntem 23 Ekim 2026'da GitHub Actions'ta (Windows Server 2025, ağ kapalı ayarlarla) denendi: `kararlilik-uzun.yml` → `offline` işi. Sonuç test raporunda.

## Not (kod değişikliği yapacak kişi için)

Test raporundaki bulgular ve `CHANGELOG.md` okunmadan ses yoluna dokunma. Her değişiklikten sonra tam test turunu çalıştır (`.github/workflows/kararlilik-test.yml`).
