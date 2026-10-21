# LAME 4.0 (MP3 encoder) in MicVST

* Source: https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz
  SHA-256 3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb (the same hash as
  Homebrew's formula; checked by `.github/workflows/kaynak.yml`).
* Kept: `libmp3lame/` (encoder only), `include/lame.h`, `COPYING`, `LICENSE`, `README`.
  Removed: `mpglib_interface.c` (decoding, not used). Added: `config.h` (no autotools).
  Nothing else changed.
* Licence: GNU LGPL 2 or later (`COPYING`). MicVST's complete source, this library included, is
  public in this repository, so anyone can rebuild MicVST with a modified LAME.
