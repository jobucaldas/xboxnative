- qrcodegen.cpp/.hpp: Nayuki QR Code generator library, MIT License, Copyright (c) Project Nayuki (https://www.nayuki.io/page/qr-code-generator-library). License text is in the file headers.
- font8x8_basic.h: dhepper/font8x8, Public Domain.
- vendor/zlib: zlib 1.2.11 (inflate subset), zlib license, (C) Jean-loup Gailly and Mark Adler.
- vendor/lzma: LZMA SDK LzmaDec (2023-04-02), Igor Pavlov, public domain.
- vendor/zstd: zstd 1.5.7 single-file decoder (zstddeclib.c, built with build/single_file_libs/combine.py), Meta, BSD-3-Clause option of the dual license.
- stb_truetype.h: Sean Barrett / RAD Game Tools, public domain or MIT (dual), pinned in deps.pin (`stb-truetype`,
  `stb-license`), fetched by src/tools/fetch-ui-assets.sh into git-ignored out/ui-assets/; never committed.
- Selawik 1.01 (selawk.ttf, selawksb.ttf, selawkb.ttf): Microsoft Corporation, SIL Open Font License 1.1, pinned
  (`selawik-release`, `selawik-license`), fetched and SHA-256 verified by src/tools/fetch-ui-assets.sh. The launcher
  packages the fonts and their licences under layout/fonts/; the TTFs are never committed.
