# Dream: Land of Giants (SNES prototype) decompilation notes

ROM: `DREAM.sfc` / `DREAM.bin` (identical, 2 MiB), from the 2026-09-09 public release
(`Release Info.txt`). No symbol map, debug strings, or source paths exist in the image or
publicly; everything below was derived statically with `tools/trace65816.py`.

    sha1 2675d7afe886f20462337aa1ee3aa5c3135fff3a
    md5  8c0eedf8bd3d59bef20e7ba62d633087

## Mapping

HiROM. File offset `f` is CPU address `$C0:0000 + f`; the high half of each bank is also
visible at `$00:8000`/`$80:8000` style addresses (the code uses `$80xxxx`/`$81xxxx` for
`jsl`). The internal header at `0xFFC0` is overwritten by tilemap data (`06 37 00 37`...),
so emulators may mis-detect the map mode. The vector table at `0xFFE4` is intact:

| vector        | value  | note                                    |
|---------------|--------|-----------------------------------------|
| RESET         | $8000  | `reset`                                 |
| NMI           | $A4D1  | `nmi` -> `jmp ($0000)` (handler in RAM) |
| COP/BRK/ABORT/IRQ, all emulation vectors | $A442 | single `rti` stub |
| unused emulation-mode slots (`0xFFF0..0xFFF3`) | "RARE" | ASCII signature (native IRQ at `0xFFEE` = $A442) |

## Layout

| file range        | contents |
|-------------------|----------|
| 000000-007FFF     | stale build image: a second boot routine + code from an older build. Its `jsr` targets land one byte off inside live routines (e.g. `$ABCB` is mid-instruction), so it is not traced. Labelled `stale_build_image`. |
| 008000-00C00D     | main program (bank `$C0` high half, ~14.6 KB traced, 104 subroutines) |
| 00C00D-00FFFF     | data: HDMA/tilemap tables, `06 37 00 37` fill, vectors at FFE4 |
| 018000-0183E8     | sound driver interface (`$81:8000`, `jsl` entry points) |
| 020000-020087     | SPC700 IPL loader (0x88 bytes, uploaded to SPC `$04D8`) |
| 020088-020D5A     | SPC700 driver, 0x699 words uploaded to SPC `$0560` |
| 020DB9-           | pointer table for sample/song blocks used by `sub_C180C1` / `sub_C1815F` |
| 020000-1FFFFF     | graphics and level data; nothing else executes |

`snes2asm` (linear disassembler) was tried first; it treats all 2 MiB as code and found
only 424 labels, so its output was discarded in favour of the tracer.

## Program structure

- `reset` clears WRAM, calls `spc_init` and `spc_command`, `ppu_init`, then falls into the
  main loop. Game state is `game_mode` (`$A4`), dispatched through `game_mode_table`
  (`$826A`, 4 entries: `sub_C08292`, `sub_C084D7`, `sub_C08798`, `sub_C088AB`) plus
  four sibling tables at `$8272/$827A/$8282/$828A` indexed the same way.
- NMI handlers are installed by `lda #$addr ; jmp $A4E9` (stores to `$0000`); the
  tracer seeds them as `nmi_handler_80F4` and `nmi_handler_BD20`.
- Entity animation rate: `sub_C098DA` dispatches through `jmp ($0004)` at `$99B6` using
  the two-level `anim_rate_fn_table` at `$B66A` (indexed by entity type and state); the
  handlers scale velocity into an animation playback rate (`anim_rate_*`).
- Animation scripts live in bank `$C4` (`data_C41858`, 8-byte records `{callback, mode,
  duration, frame}`); `anim_update` (`$AEC8`) loads the callback into `ptr_04` and
  `anim_callback_dispatch` (`$B022`) jumps to it. Callbacks are the `anim_cb_*` routines
  (sound-effect pickers, hit checks, state reset). Full derivation in
  `docs/handler_tables.md`. A third dispatcher at `$9206` reads a table that no longer holds
  code and is dead.
- Sound: `spc_init` (`$81:8000`) does the IPL handshake, uploads the loader from
  `$C2:0000`, then streams the driver with a custom protocol (`upload_spc_block`).
  `spc_command` (`$81:83CE`) is the runtime entry, called with A = command.

## Dynamic findings (harness)

- The main loop runs inside NMI: after init the CPU parks on `wai` at `loc_C0A4FD` and all
  per-frame work happens in the handler reached through `jmp ($0000)`.
- Execution uses the `$80/$81` mirror banks, never `$C0/$C1` directly.
- Pressing **Select** while no fade is in progress (`$30` and `$32` zero) advances
  `game_mode` 0 -> 1 -> 2 -> 3 -> 0 (in `nmi_handler_gameplay`), a leftover debug/attract
  mode cycle. It is how modes 1-3 are reached in `recomp/harness/inputs/`.
- Coverage: the static trace is a strict superset of executed code; the input scripts
  reach 111 of 123 routines; the rest is dead code or needs player 2 (below).
- **B** is the attack button (`entity_apply_hit_reaction` forces `entity_state = $000C` on a
  new press). `check_pending_player_attack` reads **player 2's** controller and applies the
  same logic to enemies: a second pad drives enemy attacks, a debug/test feature of the
  prototype and the only path to `anim_cb_hit_player` / `anim_cb_sfx_0602`.
- The three unreferenced picture strips in bank `$C1` (`010000`, `012800`, `013300`) decode,
  at tile base `$44`, to the captions STRIKE, OUT, PITCH and HIT B. Rare shipped *Ken Griffey
  Jr.'s Winning Run* on SNES in 1996; these look like leftovers of a baseball project on the
  same development cartridge (inference from the text alone, no code references them).
- The empty song slots 3-7 cannot be started: `spc_command` with those numbers never returns.
- The 1-row OAM emitters need sprite frame ids 1-3, which no animation script emits: dead.
- `$8BDB` (formerly labelled `ppu_regs_default`) is the fall-through second half of
  `mode0_camera_zone_update`, not a routine; `$8152`/`$814C` labels were stale-image artefacts.

## Data formats

Full region table (112 regions covering all 2 MiB, with ASCII tile renders and evidence):
`docs/data_formats.md`. No compression anywhere: every upload is raw DMA or a `VMDATA` loop.

| share | format |
|-------|--------|
| 56.1% | sprite frames: `{ptr16, bank, y-bias}` table at `0x040000` (1556 frames); each frame = 8-byte header, `{x,y}` OAM records, raw 4bpp tiles; banks `$CA-$DC` |
| 9.5%  | alternate sprite-frame format (`{x,y,attr}` records) at `0x1CC6AA-0x1FFEE5`, unreferenced by live code |
| ~10%  | level scenes: raw 4bpp BG tilesets (`$C7-$CA`), 4x4-word metatiles, column-major u16 maps with flip bits, 2 KB tilemaps, palettes and HDMA colour tables in `$C4` |
| title | BG mode 3: 627 raw 8bpp tiles at `0x06002B`, 256-colour palette at `0x06A36B`, INIDISP HDMA table at `0x00C00D`; header text "FRAME 1"/"FRAME 3" is the only ASCII in the image |
| 3.5%  | 51 BRR samples `{loop, len, data}` at `0x023095-0x035163`, bounds from the SPC pointer table |
| 0.4%  | SPC songs `0x02119F-0x022E5C`, sfx banks `0x022E5C-0x023081` |
| 1.2%  | animation scripts + frame table |
| 1.0%  | code (65816 + SPC700) |
| 9.8%  | stale image + exact shifted duplicates of live data (e.g. `0x01841A`, `0x050EC0-0x05F0E1`, `0x1F0000`) |
| 1.5%  | filler: `06 37 00 37` tilemap words at `0x00C054-0x00FFD4` (read by nothing), `0x55` in bank `$C1` |
| 3.1%  | unknown slivers, mostly tile-like and unreferenced |

Also found: an unreferenced 96-glyph ASCII 2bpp font at `0x014FE0-0x0155E0`.

## Toolchain evidence

Full write-up with offsets: `docs/toolchain_evidence.md`. Established from bytes:

- No compiler, assembler name, date, path, or text of any kind is embedded. Hand-written
  65816 (no `php/plp`, no stack-relative modes, no relocated direct page, all returns in
  16-bit mode).
- The SPC700 driver is the DKC2/DKC3 driver: 96.3% byte-identical, the only real
  difference being one added sequence opcode (`seq_master_volume`, file `0x20982`), with
  every other differing byte a pointer shifted by +0x33 from that insertion. Killer
  Instinct carries the same code at a different layout, DKC1 an earlier revision. The
  65816 sound-interface file is also shared: DKC2 and KI have the same live routine plus
  the same dead twin after `sub_C1803E`.
- Ten bank `$C0` routines are verbatim in DKC1/2/3 (2bpp-to-4bpp table, metatile blitter,
  VRAM address calc, OAM hide loop, VRAM DMA helper), so Dream uses Rare's DKC-era
  65816 library.
- Fill byte is `0x55` (bank `$C1` tails); DKC1/2 pad with `0x00`, DKC3/Battletoads/KI with
  `0xFF`. No checksum, header overwritten by tilemap data: this image never went through
  the mastering step every retail Rare ROM shows.
- The stale image at `0x0000-0x7FFF` is an older assembly of the same sources with modules
  reordered (1044 of 1065 aligned instructions identical, per-module shifts of +0x1D04,
  +0x17AA, +0xDA6, +0xA71, +0x232, -0xC3A), followed by an older copy of the bank `$C4`
  animation script table. That implies a module-concatenating in-house assembler with
  fixed-origin bank images.

## SPC700 sound driver

`spc/driver.asm` is a full disassembly of the loader (`$04D8`) and driver (`$0560`,
entry `$0672`), produced by `tools/trace_spc700.py` and reassembled byte-exact by
`make spc`. `spc/spc_map.txt` documents the port protocol, command set, sequence format
and tables. Summary:

- Port0 is a running handshake counter; the SPC waits for it, reads port1/port2, echoes it.
  The loader accepts `(dest, count, data...)` blocks; a zero count jumps to `dest`.
- Commands (port1): `< $80` plays sound effect id on channel = port2; `>= $80` uses
  `cmd & 7`: 0/1 set globals, 2 mono, 3 fade + start song (`song_table $1312`),
  4 pitch offset, 5 voice-5 volume, 6 (`$FE`) play, 7 (`$FF`) stop and return to the
  loader for uploads. That matches `spc_command` on the 65816 side.
- DSP `DIR = $31`: sample directory at `$3100`, samples from `$3400`, built by the 65816 in
  `$7E2000` (`sub_C1815F`). The 256-byte block at `$0560` is a sample remap table.
- Sequence data: events `>= $80` are notes, `< $80` are opcodes via `seq_cmd_table $0FD8`
  (37 live opcodes; entries `$25-$32` point at stale handlers).

## Building (matching)

    make            # assembles src/ + data/ into build/dream.sfc, checks the sha1, checks the SPC driver
    make regen      # re-runs the tracer and regenerates src/ and data/ from DREAM.sfc

`build/dream.sfc` is byte-identical to the original (verified 2026-09-10). The assembler is
asar 1.91 built from source into `tools/bin/asar` (see `.gitignore`; rebuild with
`cmake ../asar/src/asar -DASAR_GEN_EXE=ON -DASAR_GEN_DLL=OFF -DASAR_GEN_LIB=OFF`).
`src/main.asm` sets `hirom`, defines RAM/register symbols, and includes one file per bank;
code is emitted with explicit `.b/.w/.l` widths, jump tables as `dw label`, and data as
`incbin "../data/<offset>.bin"` (runs under 64 bytes inline as `db`). References through
the `$80/$81` mirror banks keep the ROM's bank byte via `$800000+(label&$FFFF)`.

Workflow: improve labels in `tools/names.txt` (or the tracer), `make regen`, `make`.
Hand edits to `src/` are lost on `regen`, so the intended path is to keep enriching
`names.txt` until the generated source is good enough to freeze, then stop regenerating.

## Tooling

    python3 tools/trace65816.py DREAM.sfc out/
    python3 tools/emit_asar.py DREAM.sfc src data

- `tools/names.txt` — curated labels (ROM code/data and RAM). Code entries only name a
  routine; add `m0x0` etc. after the name to force tracing from an address that is not
  otherwise reachable.
- `out/codemap.txt` — code ranges, jump tables, flag-state conflicts.
- `out/symbols.txt` — plain address/offset/name list.
- `out/dream.mlb` — Mesen2 labels (ROM, registers, WRAM).
- `out/dream.sym` — bsnes-plus style labels.
- `out/dream.asm` — listing: traced code disassembled with symbols, gaps as `.incbin`.

The tracer follows `jsr/jsl/jmp/jml`/branches, tracks M/X through `rep/sep/php/plp` and
through subroutine return states (only flags a callee explicitly sets are adopted by the
caller), validates jump-table entries, refuses to decode into the interior of an existing
instruction, and finally sweeps unreferenced gaps in the code region for routines reached
via RAM pointers.

## Open items

1. Four small unreferenced fragments remain (`orphan_C0A294`, `orphan_C0A35C`,
   `entity_clear_anim_unused`, the dead `$9206` dispatcher); everything else in the code
   bank is reached from a vector, a table, or an animation script.
2. Split `data/` along the region table in `docs/data_formats.md` (sprite frames, BRR,
   songs, tilesets, maps, palettes) so assets become editable files instead of blobs.
3. Dynamic confirmation: a trace log from Mesen2/bsnes-plus would settle the handler
   tables quickly. Headless Mesen2 needs an X display; a rootless Xvfb was set up in
   `/tmp/claude-1005/.../scratchpad/xvfb` but the test runner never produced output.
