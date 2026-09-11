# Legal and provenance

This repository is a **disassembly** of the SNES prototype *Dream: Land of Giants* (codename Project Dream; Rare, c. 1995),
the build that later became *Banjo-Kazooie*. It is not a clean-room reimplementation: the
source here was produced by tracing the machine code of a leaked prototype ROM and is
therefore derived from that ROM. The repository holds the recovered *structure*: symbol
names, control flow, data-region boundaries, and analysis documents. The game's own bytes
are never committed.

## Rules

1. **No ROM, no extracted data, no build output in the repository.** `baserom/`, `data/`,
   `build/`, and `out/` are gitignored and `tools/check_no_rom.sh` refuses to commit
   anything under them or any `*.sfc`/`*.bin`/`*.zip`.
2. **Generated sources carry no literal byte runs.** Every data region is an
   `incbin` into files that `tools/extract.py` splits from *your* ROM after a SHA-1
   check: a whole named asset (`incbin "../data/<kind>/<name>.bin"`) or a half-bank
   range (`incbin "../data/NN.bin":$lo..$hi`, end exclusive). The `..` separator is
   what asar 1.91 accepts; the older `$start-$end` form is deprecated and asar 1.91
   rejects it with `(Ebroken_incbin)`, so `tools/bin/asar` has to be 1.91 or newer.
   Nothing pins that version, so a build with an older asar will fail loudly rather
   than quietly. Jump tables are symbolic (`dw label`).
3. **Assets are never redistributed.** Graphics, samples, music, level and animation data
   stay inside the user-supplied ROM. Analysis documents may quote short byte sequences as
   evidence.
4. **No leaked source, SDK, or debug symbols as inputs.** Public disassemblies of related
   titles (the Yoshifanatic1 and p4plus2 DKC projects) are used as references: read,
   cross-referenced, re-derived. Symbol names adopted from them are credited in
   `docs/dkc_crossref.md`.

## Provenance of the ROM

The ROM was released publicly on 2026-09-09 by an anonymous holder; the accompanying
release note is not included here. The project is unaffiliated with Rare, Microsoft,
or Nintendo. *Banjo-Kazooie*, *Dream: Land of Giants* and *Project Dream* are trademarks of their respective owners.
