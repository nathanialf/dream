# dream-land-of-giants

A matching disassembly of the **Dream: Land of Giants** prototype (codename Project Dream), the SNES game Rare built around 1995
that later became *Banjo-Kazooie*. `make` rebuilds the 2 MiB ROM byte-for-byte from
the sources here plus data extracted from your own copy of the ROM.

> [!IMPORTANT]
> This project is unaffiliated with Rare, Microsoft, or Nintendo. No ROM data or
> extracted assets ship with this repository; you must supply your own ROM to build.
> This is a disassembly, not a clean-room reimplementation. Read
> [`docs/LEGAL.md`](docs/LEGAL.md) before contributing.

## Status

| target | state |
|---|---|
| 65816 program (`src/`, bank `$C0` high half + sound interface in `$C1`) | byte-identical rebuild, 119 subroutines labelled, 6 jump tables + 2 RAM-dispatch tables resolved |
| SPC700 sound driver (`spc/driver.asm`) | byte-identical rebuild, command/port protocol and sequence format documented |
| data (`data/`, 98% of the ROM) | region map with formats in `docs/data_formats.md`; still `incbin` ranges, not yet split into assets |

The ROM is HiROM with an intact vector table but a header overwritten by tilemap data,
no checksum, and a stale older build of the same program sitting in the first 32 KB.
The sound driver is the DKC2/DKC3 driver (96% identical) and ten library routines are
verbatim from the DKC games. Details and evidence: [`docs/NOTES.md`](docs/NOTES.md),
[`docs/toolchain_evidence.md`](docs/toolchain_evidence.md),
[`docs/data_formats.md`](docs/data_formats.md), [`docs/handler_tables.md`](docs/handler_tables.md).

## Quickstart

```sh
git clone https://github.com/RPGHacker/asar && cd asar && mkdir build && cd build && \
  cmake ../src/asar -DCMAKE_BUILD_TYPE=Release -DASAR_GEN_EXE=ON -DASAR_GEN_DLL=OFF -DASAR_GEN_LIB=OFF && \
  make && mkdir -p ../../tools/bin && cp bin/asar ../../tools/bin/ && cd ../..
cp /path/to/DREAM.sfc baserom/        # sha1 2675d7afe886f20462337aa1ee3aa5c3135fff3a
make                                  # extract -> assemble -> verify (ROM and SPC700 driver)
```

`tools/extract.py` refuses a ROM whose SHA-1 does not match. Everything under
`baserom/`, `data/`, `build/`, and `out/` is gitignored; `tools/check_no_rom.sh`
(run by the pre-commit hook) refuses to commit ROM-derived files or inline byte runs.

## Workflow

`src/` and `spc/driver.asm` are generated:

1. `tools/trace65816.py` traces the 65816 code from the vectors, tracking M/X flags,
   jump tables, RAM-dispatch tables, and subroutine return states.
2. `tools/names.txt` supplies curated labels (ROM code, data, RAM).
3. `tools/emit_asar.py` writes asar source with symbolic references and `incbin`
   ranges for data.

To improve the disassembly, edit `tools/names.txt` (or the tracer), then
`make regen && make`. Hand edits to `src/` are discarded by `regen`; the plan is to keep
enriching the generators until the output is worth freezing. Symbol files for
emulator debuggers (`out/dream.mlb` for Mesen2, `out/dream.sym` for bsnes-plus) are
produced by the tracer.

## Layout

```
baserom/   your ROM (gitignored)          docs/     notes, legal, analysis reports
data/      extracted half-banks (ignored) spc/      SPC700 driver disassembly
src/       generated asar source          tools/    tracer, emitter, extract, checks
```

## License

MIT for everything authored here. The game and its data remain the property of their
respective owners.
