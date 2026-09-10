# Legal and provenance

This repository is a **disassembly** of the SNES prototype *Project Dream* (Rare, c. 1995),
the build that later became *Banjo-Kazooie*. It is not a clean-room reimplementation: the
source here was produced by tracing the machine code of a leaked prototype ROM and is
therefore derived from that ROM. What we commit is the *structure* we recovered: symbol
names, control flow, data-region boundaries, and analysis documents. What we never commit
is the game's own bytes.

## Rules

1. **No ROM, no extracted data, no build output in the repository.** `baserom/`, `data/`,
   `build/`, and `out/` are gitignored and `tools/check_no_rom.sh` refuses to commit
   anything under them or any `*.sfc`/`*.bin`/`*.zip`.
2. **Generated sources carry no literal byte runs.** Every data region is an
   `incbin "../data/NN.bin":$start-$end` range into files that `tools/extract.py` splits
   from *your* ROM after a SHA-1 check. Jump tables are symbolic (`dw label`).
3. **Assets are never redistributed.** Graphics, samples, music, level and animation data
   stay inside the user-supplied ROM. Analysis documents may quote short byte sequences as
   evidence.
4. **No leaked source, SDK, or debug symbols as inputs.** Public disassemblies of related
   titles (the Yoshifanatic1 and p4plus2 DKC projects) are used the way one uses a paper:
   read, cross-referenced, re-derived. Symbol names adopted from them are credited in
   `docs/dkc_crossref.md`.

## Provenance of the ROM

The ROM was released publicly on 2026-09-09 by an anonymous holder; the accompanying
release note is not included here. The project is unaffiliated with Rare, Microsoft,
or Nintendo. *Banjo-Kazooie* and *Project Dream* are trademarks of their respective owners.
