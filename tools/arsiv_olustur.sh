#!/bin/bash
# R9: MicVST arşiv paketi. Kullanım: tools/arsiv_olustur.sh <çıktı.zip> <MicVST.exe> [ek dosya ...]
# İçerik: depo kaynağı (HEAD, git archive), JUCE 9.0.4 kaynağı MicVST yamaları uygulanmış (build-linux/_deps/juce-src,
# .git'siz) + yamaların farkı, exe + SHA-256, ek dosyalar (rapor, kılavuz, config yedeği, sürüm listesi), OKU.md.
set -euo pipefail
out=$(readlink -f "$1"); exe=$(readlink -f "$2"); shift 2
repo=$(git rev-parse --show-toplevel); commit=$(git -C "$repo" rev-parse HEAD)
juce="$repo/build-linux/_deps/juce-src"
[ "$(grep -c 'MicVST patch' "$juce/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp")" = 3 ] || { echo "JUCE yamalı değil"; exit 1; }
tmp=$(mktemp -d); root="$tmp/MicVST_1.2.2_arsiv"; mkdir -p "$root/kaynak" "$root/exe" "$root/belgeler"
git -C "$repo" archive --format=tar --prefix=MicVST/ HEAD | tar -x -C "$root/kaynak"
mkdir -p "$root/kaynak/JUCE-9.0.4" && tar -C "$juce" --exclude=.git -cf - . | tar -x -C "$root/kaynak/JUCE-9.0.4"
git -C "$juce" diff > "$root/kaynak/JUCE-9.0.4_MicVST_yamalari.diff"
git -C "$juce" log -1 --format="JUCE kaynağı: %H (etiket 9.0.4)" > "$root/kaynak/JUCE_SURUMU.txt"
cp "$exe" "$root/exe/MicVST.exe"; (cd "$root/exe" && sha256sum MicVST.exe > MicVST.exe.sha256)
for f in "$@"; do cp "$f" "$root/belgeler/"; done
sed "s/@COMMIT@/$commit/g; s/@SHA@/$(cut -d' ' -f1 "$root/exe/MicVST.exe.sha256")/g" "$repo/tools/arsiv_OKU.md" > "$root/OKU.md"
(cd "$tmp" && rm -f "$out" && zip -qr -9 "$out" MicVST_1.2.2_arsiv)
rm -rf "$tmp"; ls -la "$out"; sha256sum "$out"
