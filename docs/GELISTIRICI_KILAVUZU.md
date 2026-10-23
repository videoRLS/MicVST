# MicVST – Geliştirici kılavuzu

Bu kılavuz, MicVST'yi ileride kendin (ya da Claude gibi bir yardımcıyla) geliştirmek ya da bir hatasını
düzeltmek için yazıldı: kod nerede, nasıl derlenir, nasıl test edilir, neye dikkat etmek gerekir.
Kullanıcı kılavuzu ayrı: `docs/UZUN_OMUR_KILAVUZU.md`. Test sonuçları: `docs/TEST_RAPORU_WIN11_1.2.2.md`.

---

## 1. Kaynak nerede

- GitHub: `videoRLS/MicVST`. Teslim edilen sürümler:
  - **1.2.2**: dal `kararlilik/oncelik` (exe commit `baf4efd`)
  - **1.2.3**: dal `kararlilik/bildirim-sesi` (1.2.2 + bildirim sesi düzeltmesi)
  - `main`'e bu çalışmada dokunulmadı.
- İnternetsiz kopya: arşiv paketi `MicVST_<sürüm>_arsiv.zip`. İçinde MicVST kaynağı, MicVST yamaları
  uygulanmış JUCE 9.0.4 kaynağı, exe ve SHA-256'sı ve belgeler var. Paketin içindeki `OKU.md` internetsiz
  derlemeyi anlatır.

## 2. Kod haritası

| Klasör / dosya | Ne yapar |
|---|---|
| `src/Main.cpp` | Uygulama: açılış/kapanış, tepsi, pencereler, log dosyası (`%APPDATA%\MicVST\log.txt`, 2 MB'ta kırpılır), tema/dil değişince pencereleri yeniden kurma |
| `src/audio/AudioEngine.*` | Ses motoru: mikrofon (WASAPI) → eklenti iş parçacığı → köprü → kablo. Sustur (15 ms yumuşak geçiş), cihaz kaybında yeniden açma (`ReopenBackoff`), durum satırı sayaçları, log'daki "İşleme / Köprü" satırları |
| `src/audio/AudioBridge.*`, `InputQueue.h`, `Resampler.h`, `SpliceAlign.h` | İki cihaz saati arasındaki köprü: saat farkı düzeltmesi (±ppm), güvenlik payı, yumuşak geçişli atlama/ekleme |
| `src/audio/PluginChain.*`, `GraphConnections.*`, `ScanCoordinator.*` | VST3 zinciri, bağlantılar, eklenti tarama |
| `src/audio/Recorder.*` | Kayıt: WAV/BWF/RF64, FLAC, MP3 (LAME 4.0, `third_party/`); yazıcı iş parçacığı, her saniye başlık yenileme |
| `src/audio/CueSounds.*` | Bildirim sesleri. 1.2.3: `CueMixer` (yumuşak geçiş), `levelled()` (eşit yükseklik), Windows'ta waveOut |
| `src/audio/fx/`, `BuiltIn*` | Dahili efektler, Otomatik Kazanç |
| `src/audio/MsCounter.h` | 32 bit ms sayacı (49,7 günde başa döner). Farklar hep işaretsiz alınır. Testler için sayaca ofset verilebilir |
| `src/state/` | `config.xml` okuma/yazma, hazır ayarlar, eklenti önbelleği, otomatik başlatma |
| `src/ui/` | Arayüz: ana pencere, Ayarlar, bildirim sesi paneli, kısayollar, tepsi, temalar |
| `src/i18n/Translations.cpp` | **Elle düzenleme.** `tools/i18n.py` üretir (bkz. §5) |
| `cmake/PatchJuce.cmake` | JUCE'ye MicVST'nin gerçek zamanlı yamaları (WASAPI). Tekrar çalıştırılabilir |
| `resources/sounds/` | 16 hazır bildirim sesi (FLAC, exe'nin içine gömülür) |
| `tests/` | Birim testleri (`*Tests.cpp`), motor testi (`EngineTestMain.cpp` + `enginetest/*.inc`), stres/simülasyon (`StressMain.cpp`), ekran görüntüleri (`SnapshotMain.cpp`) |
| `tools/` | CI betikleri (`ci-*.ps1`), kayıt analizi (`analiz_kayit.py`), arşiv (`arsiv_olustur.sh`), çeviri (`i18n.py`) |
| `.github/workflows/` | `kararlilik-test.yml` (tam tur), `kararlilik-uzun.yml` (uzun koşular), diğerleri `main` içindir |

## 3. Derleme (Windows)

Gerekenler: Windows 10/11 x64, Visual Studio 2022 ("C++ ile masaüstü geliştirme"), CMake 3.22+ (VS ile gelir).

İnternet varken (JUCE'yi kendisi indirir ve yamalar):

```
cmake -S . -B build -A x64
cmake --build build --config Release --target MicVST MicVSTTests
```

İnternet yokken: arşiv paketindeki `OKU.md`'yi izle (`-DFETCHCONTENT_SOURCE_DIR_JUCE=...` ve `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`).

Çıktılar: `build\MicVST_artefacts\Release\MicVST.exe`, `build\MicVSTTests_artefacts\Release\MicVSTTests.exe`.

Sürüm numarası: `CMakeLists.txt` → `project(MicVST VERSION x.y.z)`. Exe'nin Özellikler'inde ve Ayarlar → Hakkında'da görünür.
Her yeni sürümde değiştir, `CHANGELOG.md`'ye yaz.

## 4. Test

**Birim testleri:**
- Hepsi: `MicVSTTests.exe`. Sonunda "All tests completed successfully" yazmalı.
- Listesi: `MicVSTTests.exe --list`
- Bir grup: `MicVSTTests.exe "Cue sounds*"`

**Motor testi** (gerçek ses motoru, sanal cihazlar): `MicVSTEngineTest.exe <mod>`
- Başlıca modlar: `quality`, `latency`, `integrity`, `record`, `cues`, `devices`, `hotplug`, `effects`, `autogain`,
  `vst3suite`, `priority`, `long`, `chaos`, `startxrun`, `recordlong`.
- Her biri satır satır PASS / FAIL yazar.

**Simülasyon:** `MicVSTStress.exe sonuc.md` (ses yolu ölçümleri), `MicVSTStress.exe gun.md --day 24` (24 saatlik sanal gün).

**Linux'ta (hızlı deneme):** Birim ve motor testleri Linux'ta da derlenir (`cmake -S . -B build-linux -G Ninja`).
Windows'a özgü kısımlar orada çalışmaz. Son söz her zaman Windows'taki tam turundur.

**GitHub Actions** (yalnızca `kararlilik/**` dallarında):

| Commit mesajında | Ne olur | Süre | Sonuç dalı |
|---|---|---|---|
| (normal) | Tam tur: derleme, birim, motor testleri, bütünlük, kayıt, kapalı kutu, kısayollar, saat değişimi, Defender | ~3,5 sa | `ci-sonuclari/<dal>` |
| `[hizli]` | Yalnızca exe | ~10 dk | `ci-sonuclari/<dal>-hizli` |
| `[uzun]` | Uzun koşular: 330 dk long/chaos/gerçek exe, 30 gün simülasyon, 320 dk kayıt, 1000 aç-kapa, internetsiz derleme, Windows 11 ARM | ~6 sa | `ci-sonuclari/<dal>-uzun-<iş>` |
| `[skip ci]` | Hiçbir şey (belge değişiklikleri) | – | – |

Sonuçları almak: `git fetch origin ci-sonuclari/<dal>`. Exe de `MicVST.exe` olarak oradadır.

## 5. Kurallar (bunlar yüzünden bugün sorunsuz)

1. **Önce başarısız olan testi yaz, sonra düzelt.** Test düzeltmeden önce kırmızı, sonra yeşil olmalı.
2. **Ses yoluna dokunan her değişiklikten sonra** (`AudioEngine`, `AudioBridge`, `InputQueue`, `PluginChain`, `Recorder`, `fx`):
   tam tur ve `[uzun]`. Arayüz ya da bildirim sesi gibi ses yolu dışı değişiklikler için tam tur yeterli.
3. **Ses iş parçacığında bellek ayırma, kilit, dosya ya da log yok.** Motor testinin `rtprobe` senaryoları bunu ölçer.
4. **Kapalı kutu:** Exe ağ kütüphanesi kullanmaz (WinINet/WinHTTP/Winsock/URLMon). `tools/check-no-network.ps1` ve tam tur denetler.
   İnternet gerektiren bir özellik ekleme.
5. **Zaman:** `getMillisecondCounter` 49,7 günde başa döner. Farkları işaretsiz al (`MsCounter.h`), duvar saatini süre ölçmek için kullanma.
6. **Çeviriler:** Türkçe metni kodda `tr ("...")` ile yaz. Sonra sırayla çalıştır:
   - `python3 tools/i18n.py extract`
   - `i18n/<dil>.json` dosyalarına çevirileri ekle
   - `python3 tools/i18n.py check`
   - `python3 tools/i18n.py generate`
7. **Ayar dosyası:** Yeni ayar eklerken eski `config.xml` dosyaları okunabilmeli (`Persistence.cpp`). Örnek: `cueSounds` varsayılandayken dosyaya hiç yazılmıyor; eski dosyalar olduğu gibi kalıyor.
8. **Kullanıcının zinciri ve eklentileri:** Testlerde kullanıcının ayarlarını değiştirme. `%APPDATA%\MicVST`'yi önce yedekle, sonra geri koy.

## 6. Bilinen açık konular

| # | Konu | Not |
|---|---|---|
| 6 | Zincirde Clear varken kapanışta Windows olay günlüğüne "MicVST çöktü" kaydı | Eklentinin kapanışında. Zararsız. "Süreci hemen bitir" denendi, işe yaramadı |
| 12 | Zorla kapatılınca `*_temp*.xml` / `*.TMP` dosyaları kalabiliyor | Öneri: açılışta eski geçici dosyaları sil |
| 13 | Görev Yöneticisi ile kapatınca Windows "yanıt vermiyordu" yazıyor | Ürün hatası değil (pencere tepsiye gizlenir) |
| 14 | 1.2.2'de hızlı aç-kapada kulaklıkta bildirim sesi tıkı | 1.2.3'te düzeltildi (`CueMixer`) |

## 7. Claude ile geliştirirken

Yeni bir oturumda bu kılavuzu, test raporunu ve sorunu (log satırları, kayıt) ver. İste ki:
- ayrı bir dalda çalışsın (`kararlilik/...`), `main`'e dokunmasın;
- önce sorunu gösteren testi yazsın, sonra düzeltsin;
- tam turu çalıştırıp sonucu göstersin; ses yoluna dokunduysa `[uzun]`'u da.
