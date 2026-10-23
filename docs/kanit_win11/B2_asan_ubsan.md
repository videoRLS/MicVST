# B2 - ASan + UBSan (L, 23 Ekim 06:30-07:41)

Derleme: `build-asan` (RelWithDebInfo, `-fsanitize=address,undefined`, GCC, Linux konteyneri), `MicVSTTests` tümü iki parçada
(konteyner bir kez yeniden başladığı için): parça 1 Smoke … AutoGain başı (121 bölüm), parça 2 AutoGain … Cue sounds (126 bölüm, "All tests completed successfully").

- AddressSanitizer: **0** hata
- LeakSanitizer: **0** sızıntı
- UBSan: yalnız bilinen üçüncü taraf "left shift of negative value" uyarıları:
          1 build-linux/_deps/juce-src/modules/juce_audio_formats/codecs/juce_FlacAudioFormat.cpp:195:42: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:230:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:232:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:233:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:234:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:235:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:236:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:238:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:239:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:240:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:241:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:243:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:244:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:245:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:246:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/VbrTag.c:247:5: runtime error: left shift of negative value N
          1 third_party/lame-4.0/libmp3lame/bitstream.c:181:50: runtime error: left shift of 128157516 by 7 places cannot be represented in type 'int'
          1 third_party/lame-4.0/libmp3lame/quantize.c:898:43: runtime error: left shift of negative value N

Sonuç: BAŞARILI (ölçüt: bellek hatası 0, sızıntı 0, UBSan yalnız JUCE libFLAC / LAME VbrTag.c, quantize.c, bitstream.c).
