# Dream: Land of Giants: toolchain evidence from bytes

Scope: what the bytes of `DREAM.sfc` (2 MiB HiROM) say about how Rare built it, checked
against six other Rare SNES ROMs read-only under `/primary/Games/ROMs/SNES/`:

| tag  | ROM (all headerless: size % 1024 == 0)                                   | size    | map   |
|------|--------------------------------------------------------------------------|---------|-------|
| DKC1 | Donkey Kong Country (USA) (Rev 2)                                        | 4 MiB   | HiROM |
| DKC2 | Donkey Kong Country 2 - Diddy's Kong Quest (USA) (En,Fr) (Rev 1)         | 4 MiB   | HiROM |
| DKC3 | Donkey Kong Country 3 - Dixie Kong's Double Trouble! (USA) (En,Fr)       | 4 MiB   | HiROM |
| BTBM | Battletoads in Battlemaniacs (USA)                                       | 1 MiB   | LoROM |
| BTDD | Battletoads-Double Dragon (USA)                                          | 1 MiB   | LoROM |
| KI   | Killer Instinct (USA) (Rev 1)                                            | 4 MiB   | HiROM |

Method: python3 over the raw files (16-byte window index on the other ROM, matches extended
both ways, runs of fill bytes and matches under 20 bytes discarded), `difflib` alignment for
the SPC driver, and a small recursive tracer built on `tools/trace65816.py` for the stale
image. Scripts and raw outputs are in the session scratchpad; every number below is
reproducible from the offsets given. All offsets are file offsets unless prefixed `$`
(CPU address) or "SPC $" (SPC700 address).

Each section separates **Established from bytes** (checkable) from **Inference**.

---

## 1. Shared code with other Rare titles

### 1.1 SPC700 sound driver (file 0x20000-0x20D5A)

**Established from bytes**

- DKC2 and DKC3 each contain a copy of the same loader+driver image at the start of a bank:
  DKC2 0x2E0000, DKC3 0x2D0000. The two copies are byte-identical to each other for the
  first 0xD86 bytes (they diverge only in the song/sample pointer tables that follow the
  driver).
- Dream's image aligned against DKC2's: **3290 of 3418 bytes equal (96.3 %)**. Every
  difference is one of three things:
  1. The loader's last two bytes: Dream 0x20086 = `07 1F`, DKC2 0x2E0086 = `04 D6`, KI
     0x1F4F4 = `E6 E6`. These are SPC `$055E-$055F`, adjacent to `cmd_param` (`$055D`), and
     nothing in `spc/driver.asm` references `$055E/$055F`.
  2. One 51-byte block present in Dream and absent in DKC2/DKC3: Dream 0x20982-0x209B5,
     SPC `$0E5A`, which `spc/driver.asm` names `seq_master_volume` (the `mono_flag` branch that
     averages L/R into `mvol_lr`). In DKC2 the sequence-command table entry `$11` for it is
     `0000` (DKC2 0x2E0ACD+0x22), in Dream it is `5A 0E` (0x20B00+0x22). All other 50 table
     entries are equal, or equal after the +0x33 shift below.
  3. Every remaining byte difference is a 16-bit SPC address that is exactly **0x33 = 51**
     larger in Dream, i.e. pointers and jump-table entries relocated past that insertion
     (`C8`/`95`, `CC`/`99`, `CD`/`9A`, `D8`/`A5`, `3E`/`0B`, `61`/`2E`, `9E`/`6B`,
     `09 0F`/`D6 0E`, `23 11`/`F0 10`, `2A 11`/`F7 10`; offsets in the alignment dump, e.g.
     Dream 0x2036B vs DKC2 0x2E036B).
- The `seq_cmd_table` (Dream SPC `$0FD8`, 51 entries; DKC2 SPC `$0FA5`) has the same 51
  slots with the same null entries (`$25,$28-$2A,$2D-$2F`) and the same duplicated
  `seq_echo_off` at `$30/$32` in both.
- The IPL loader stub (0x88 bytes at 0x20000, SPC `$04D8`) is byte-identical in Dream,
  DKC2 and DKC3 apart from the two bytes in item 1. KI's loader (KI 0x1F46E) differs in one
  direct-page operand byte (`D8 E8` vs Dream `D8 E9` at +0x08) and in its tail.
- KI (file 0x1F460-0x20460): 2080/3418 bytes equal (60.9 %), in ~50 runs of 12-257 bytes;
  same routines, different layout (KI driver entry and tables sit at different SPC
  addresses). DKC1 (0xAA400-0xAB400): 1625/3418 (47.5 %), the longest runs being the 114-byte
  zero remap table and DSP-init fragments (42 bytes at DKC1 0xAA5A9 = Dream 0x2030B: the
  `mov $F2,#$5C / mov $F3,#$FF / ... / mov $F2,#$6C` DSP register reset).
- BTBM/BTDD share only the 12-entry semitone table `00 02 1E 02 3F 02 61 02 85 02 AB 02 D4
  02 FF 02 2D 03 5D 03 90 03 C7 03` (Dream 0x20D3E = BTBM 0xE8C94 = BTDD 0xA2BFB = DKC1
  0xAB072, 28 bytes) plus 22-38-byte DSP-register write sequences (BTDD 0xA2B21, 0xA2AEB).

**Inference**

- Dream's driver is the DKC2/DKC3 driver plus one added sequence opcode; the +0x33
  relocation of every later pointer is what an assembler produces when 51 bytes are inserted
  in the source, so the driver was re-assembled from source for Dream, not patched.
  Direction (Dream newer or older than DKC2) is not decidable from bytes; DKC3 (1996) still
  lacks the opcode, so it was not carried back into the DKC line.
- The KI and DKC1 drivers are earlier layouts of the same code base (same routines, same
  tables, different addresses). The Battletoads driver is a different lineage that shares
  only the pitch table and DSP-init boilerplate.
- `$055E-$055F` differing per game while being unreferenced is consistent with the driver
  image being emitted with uninitialised workspace bytes (e.g. a RAM dump, or `ds` space
  with residue). Weak; the values could also be a build-specific constant.

### 1.2 65816 sound interface (file 0x18000-0x183E8, `$81:8000`)

**Established from bytes**

- `spc_ipl_upload_loader` (0x1805A) is shared verbatim: 36 bytes with DKC2 (0x358212), 33
  with DKC3 (0x328273) and KI (0x1EF7E): `c2 20 e2 10 a9 aa bb cd 40 21 d0 fb a9 d8 04 8d
  42 21 a9 cc 01 8d 40 21 aa ec 40 21 d0 fb ...` (IPL handshake with destination `$04D8`).
  The block-upload loop at 0x1807E matches 27-28 bytes in the same three.
- `sub_C1803E` at 0x1803E is `lda #$0672 ; sta $000007 ; stz $09 ; jsr spc_send_words ;
  rts`, and is immediately followed at 0x1804C by an **unreferenced 14-byte twin** that
  differs only in the immediate: `a9 e3 06 8f 07 00 00 9c 09 00 20 24 83 60`
  (`lda #$06E3`). DKC2 has exactly the same pair at 0x3581AC/0x3581B7
  (`a9 72 06 85 35 64 37 20 0d 84 60` then `a9 e3 06 85 35 64 37 20 0d 84 60`, with
  DKC2's own direct-page variables). KI has the pair at 0x1F22B/0x1F236 with `$0672` and
  `$06D9`. DKC3 (0x32847E) has only the `$0672` form.
- Whole-region alignment Dream vs DKC2 (0x357E00-0x358800): 210/1056 bytes equal; vs DKC3
  154; vs KI 129; vs DKC1 106 with no run >= 10.
- The stale build image (section 4) calls `jsl $818000` and `jsl $8183CE`, the same two
  entry points as the live build.

**Inference**

- The dead `$06E3` twin is a second driver entry point kept in the source (an older
  driver's entry, or a warm-start entry) and assembled but never called in Dream and DKC2
  alike, which means the 65816 sound-interface source file is common to Dream and DKC2 with
  only variable addresses re-targeted. KI's `$06D9` is an earlier revision of the same file.
- The interface living at the same `$81:8000/$81:83CE` in the stale and live builds shows the
  sound code was a separately located module that did not move when bank `$C0` was
  reorganised.

### 1.3 Main program (file 0x8000-0xC00D)

Byte-identical runs >= 20 bytes, longest first (trivial register-init runs marked):

| Dream routine (offset)                         | bytes | where else                                          | what it is |
|------------------------------------------------|-------|-----------------------------------------------------|------------|
| `loc_C0A6CD` 0xA6CD + `data_C0A6D3`             | 142   | DKC2 0x35A18A (142), DKC1 0x3BAA3F (137), DKC3 0x3791D0 (136) | `pea $8080 ; plb ; plb ; rtl` followed by the bit-spreading table `03 0C 30 C0 01 04 10 40 ...` (2bpp to 4bpp plane expansion) |
| `loc_C0A04B` 0xA04B                             | 124   | DKC3 0x37BC8F (124), DKC2 0x35B196 (44), DKC1 0x18E96 (33) | `asl A x5 ; adc $1E ; tay ; lda $0000,Y ; eor #$4000 ; sta $00,X ; lda $0008,Y ; ...` metatile copy with H-flip (`#$4000`), V-flip (`#$8000`, `loc_C09F39`), both (`#$C000`, `loc_C09F66`/`loc_C0A0A0`) variants each shared 32-44 bytes |
| `ppu_init` 0xA385+0xB                           | 175   | KI 0x191F8                                          | PPU register clear sequence `stz $2101 ; stz $2105 ; ... ; stz $210D ; stz $210D ...` (register init; same order and doubled scroll writes) |
| `clear_sprite_table` 0xA500                     | 49-52 | DKC1 0x389C77, DKC2 0xA456, BTBM 0xA6A5, BTDD 0xB8968 | unrolled `stz $0400 ; stz $0402 ; ...` (register-style init, but the same RAM address in five games) |
| `set_bg_scroll` 0xA4A8                          | 39    | DKC2 0x8540, DKC3 0x820D, BTBM 0x3611 (21)          | scroll register writes (trivial) |
| `sub_C0ADE7`+2 0xADE9                           | 35    | BTDD 0xBEFC3 (35), DKC1 0xA213, DKC2 0x88BE, DKC3 0x8996 (20-21) | OAM hide loop `cpx #$0400 ; beq ; lda #$F0FF ; sta $00,X ; inx x4 ; cpx #$0400 ; bne` |
| `sub_C0A445`+6 0xA44B / `sub_C0ADFD` 0xADFD / `sub_C0A46A` 0xA46A | 31/28/28 | DKC1 0xC22C, DKC2 0xB10F, DKC3 0x338093, BTBM 0x66F1, BTDD 0xBEFF4, KI 0x19369 | VRAM DMA helper: `sta $4302 ; sta $4308 ; lda #$0800 ; sta $4305 ; lda #$1809 ; sta $4300 ; sep #$20 ; stz $4304 ; lda #$01 ; sta $420B ; rep #$20`; the double store to `A1TL0` and `A2AL0` is present in all six |
| `sub_C08F47`+0x5F 0x8FA6, `sub_C09049`+0x33 0x907C | 28-29 | DKC1 0x9A35/0x36C1A7, DKC2 0xDCBC/0x35DBDE, BTBM 0xB24E (x4 form) | `cmp #$8000 ; ror A` repeated six times (sign-preserving shift right by 6) |
| `loc_C0A102`+2 0xA104                           | 26    | DKC1 0x188CA, DKC2 0x35AE63, DKC3 0x37B954         | `clc ; adc #$0100 ; lsr x3 ; and #$003F ; bit #$0020 ; clc ; beq ; adc #$03E0 ; adc #$0078 ; sta $2116` tilemap VRAM address calculation |
| `loc_C0A53D`+2 0xA53F                           | 22-23 | DKC1 0x3BA84E, DKC2 0x359F55                        | `cmp #$0400 ; bne ; sep #$20 ; lda #$07 ; sta $2100 ; rep #$20 ; pea $8080 ; plb ; plb ; rtl ; phk ; plb` |

Discarded as coincidental data: `entity_state_anim_table`+0x7B/+0xAB (zero-dominated),
`entity_state_velocity_table`+0x4E (zeros), `data_C084B5`+0xB in BTDD (`00 02 00 02 00 03`).

**Established from bytes**

- Ten distinct routines/tables of Dream's bank `$C0` exist verbatim in at least one DKC
  title, most in all three; the DMA helper and OAM clear also exist in both Battletoads games
  and KI. No 65816 run >= 20 bytes is shared with KI other than the register-init block and
  the DMA helper.
- The `pea $8080 ; plb ; plb ; rtl` stub (`f4 80 80 ab ab 6b`) appears 4 times in Dream
  (0x17AA, 0x1927 in the stale image; 0xA54D, 0xA6CD live), 2 in DKC2, 2 in DKC3, 1 in
  DKC1, 4 in BTBM, 0 in BTDD and KI.

**Inference**

- The DKC1/2/3 and Dream programs share a hand-written library (graphics upload, tile
  plane expansion, metatile blitting, OAM handling, DMA helpers). Dream is closest to DKC2
  and DKC3 (longest runs, and the same rtl-stub-plus-table layout at `loc_C0A6CD`).
  Battletoads shares only the oldest, smallest helpers; KI shares almost none of the game
  library, only the PPU/DMA boilerplate and the sound interface.

---

## 2. Padding, headers, signatures

### 2.1 Fill bytes

**Established from bytes** (single-byte runs >= 256 bytes, whole file):

| ROM   | fill totals                              | largest runs / where                                   | 64 KiB bank tails uniform |
|-------|------------------------------------------|--------------------------------------------------------|---------------------------|
| Dream | 0x55: 14944, 0x00: 256                   | 0x155E0-0x18000 (0x2A20) and 0x1EFC0-0x20000 (0x1040), both 0x55, i.e. the tails of the two 32 KiB halves of bank `$C1`; the 0x00 run is the driver's 256-byte remap table at 0x20088 | 2 (both 0x55) |
| DKC1  | 0x00: 192635, 0xFF: 424                  | bank tails, e.g. 0x1FE186-0x200000                     | 54 of 64 are 0x00 |
| DKC2  | 0x00: 84242                              | 0x3DDB26-0x3E0001 etc.                                 | 14 (0x00) |
| DKC3  | 0xFF: 4731, 0x00: 3989                   | 0x33F957-0x33FF8E (0xFF)                               | 2 (0xFF) |
| BTBM  | 0xFF: 7045, 0x00: 2321                   | 0x6FB36-0x70000 (0xFF)                                 | 14 (0xFF) |
| BTDD  | 0x04: 297                                | 0x9D6C6-0x9D7EF                                        | 2x0x00, 1x0x64, 1x0xFF |
| KI    | 0xFF: 53962, 0x00: 256                   | 0x3F9524-0x400000 (0xFF)                               | 12 (0xFF), 3 (0x00) |

- Dream contains **no 0xFF or 0x00 fill run >= 256 bytes** outside the remap table. At 64-byte
  granularity the only 0xFF run is 0x7EF6-0x7FE6 (0xF0 bytes, inside the stale image) and the
  0x00 runs (0x40-0x93 bytes, e.g. 0x14604, 0x6002B, 0x1D33B7) sit inside graphics data.
- Of Dream's 64 half-banks of 32 KiB: the two halves of bank `$C1` end in 10784 and 4160
  bytes of 0x55; six data half-banks end with a short 0x55 tail (0x68000: 2 bytes, 0x148000:
  2, 0x1C8000: 4, 0x1D8000: 18, 0x1E8000: 5, 0x1F8000: 4; the file's last four bytes are
  `55 55 55 55`); 16 end in 2-15 bytes of 0x00 (indistinguishable from graphics data); the
  remaining 39 end on non-uniform data. No half-bank has a pad longer than 18 bytes outside
  bank `$C1`.
- Bank `$C0` high half: code ends at 0xC00D and is followed immediately by a repeating
  24-byte tilemap pattern (`58 89 0F 37 8D 0F 51 8B 0F 33 8D 0F 40 8D 0F 45 8D 0F 28 8D 0F
  60 8D 0F ...`) that changes to `06 37 00 37` at 0xFFB0 and runs through the header area
  to 0xFFE0. The vectors at 0xFFE4-0xFFFF are intact.

**Inference**

- The data banks were laid out by a packing step that fills residue with 0x55 and packs
  blocks to 32 KiB boundaries (the tails are 2-18 bytes because blocks were fitted to the
  half-bank). The retail Rare titles were padded with 0x00 (DKC1/2) or 0xFF (BTBM, KI,
  DKC3). 0x55 is not the erased state of an EPROM (0xFF), so it is a tool choice, not a
  blank-chip artefact. Whether this is a different tool or a different setting of the same
  tool is not decidable.
- Bank `$C0`'s tilemap data running through 0xFFC0 means the assembler placed that data
  block right after the code with no reservation for the header, and only the vector words
  were `org`'d over the top. The retail titles reserve the header.

### 2.2 Internal header

**Established from bytes** (`$FFC0` for HiROM, `$7FC0` for LoROM; complement = checksum ^ 0xFFFF):

| ROM   | title                       | map  | type | ROM sz | RAM sz | country | maker | ver | checksum / complement | valid | 16-bit sum of file | ext header at -0x10 |
|-------|-----------------------------|------|------|--------|--------|---------|-------|-----|-----------------------|-------|--------------------|---------------------|
| Dream | (tilemap bytes `06 37 00 37 ...`) | 0x37 | 0x00 | 0x06 | 0x03 | 0x20 | 0x00 | 0x07 | 0x8500 / 0x2003 | no | 0x1333 | `06 37 00 37 ...` |
| DKC1  | `DONKEY KONG COUNTRY  `     | 0x31 | 0x02 | 0x0C | 0x01 | 0x01 | 0x33 | 0x02 | 0x2BCC / 0xD433 | yes | 0x2BCC | `018X  ` |
| DKC2  | `DIDDY'S KONG QUEST   `     | 0x31 | 0x02 | 0x0C | 0x01 | 0x01 | 0x33 | 0x01 | 0x9860 / 0x679F | yes | 0x9860 | `01ADNE` |
| DKC3  | `DONKEY KONG COUNTRY 3`     | 0x31 | 0x02 | 0x0C | 0x01 | 0x01 | 0x33 | 0x00 | 0xB28C / 0x4D73 | yes | 0xB28C | `01A3CE` |
| BTBM  | `BT IN BATTLEMANIACS  `     | 0x30 | 0x00 | 0x0A | 0x00 | 0x01 | 0x5D | 0x00 | 0x6756 / 0x98A9 | yes | 0x6756 | `FF...` |
| BTDD  | `BATTLETOADS D.D.     `     | 0x30 | 0x00 | 0x0A | 0x00 | 0x01 | 0x5D | 0x00 | 0x2843 / 0xD7BC | yes | 0x2843 | code bytes |
| KI    | `KILLER INSTINCT      `     | 0x31 | 0x00 | 0x0C | 0x00 | 0x01 | 0x33 | 0x01 | 0x757A / 0x8A85 | yes | 0x757A | `01AKLE` |

- Every retail ROM's header checksum equals the plain 16-bit sum of the file. Dream's header
  slot holds tilemap data; the "checksum" field is not related to the file sum.

**Inference**

- Checksum and header population was a mastering step (or an assembler directive) that
  this prototype never went through. The Nintendo-published titles use maker `0x33` with the
  extended `01xxxx` game code; the Tradewest-published Battletoads use `0x5D`. Nothing in the
  Dream header identifies the tool.

### 2.3 "RARE" and other ASCII in vector tables

**Established from bytes** (0x20 bytes from `$FFE0`, or `$7FE0` for LoROM):

| ROM   | bytes at $xFE0                                                                 | ASCII |
|-------|--------------------------------------------------------------------------------|-------|
| Dream | `12 20 00 04 42 A4 42 A4 42 A4 D1 A4 00 00 42 A4 52 41 52 45 42 A4 00 00 42 A4 42 A4 00 80 42 A4` | `. ..B.B.B.....B.RAREB...B.B...B.` |
| DKC1  | `44 49 44 44 59 20 03 70 00 00 76 A9 00 00 9E A9 44 4F 4E 4B 45 59 4B 4F 4E 47 00 F8 00 80 00 70` | `DIDDY .p..v.....DONKEYKONG.....p` |
| DKC2  | `44 49 44 44 59 20 03 70 00 00 BD F3 00 00 F9 F3 44 49 44 44 59 20 4B 4F 4E 47 00 F8 F7 83 00 70` | `DIDDY .p........DIDDY KONG.....p` |
| DKC3  | `44 49 58 49 45 20 03 50 00 00 45 CA 00 00 6E CA 20 20 20 20 20 20 20 20 20 20 00 F8 C4 80 00 50` | `DIXIE .P..E...n.          .....P` |
| BTBM  | `52 41 52 45 39 33 03 70 00 00 D3 FC 00 00 01 FE 42 52 45 4E 20 47 55 4E 4E 21 00 F8 00 80 00 70` | `RARE93.p........BREN GUNN!.....p` |
| BTDD  | `52 41 52 45 39 33 03 70 00 00 7E FF 00 00 98 FF 4D 41 54 54 20 31 39 39 33 21 00 F8 00 80 00 70` | `RARE93.p..~.....MATT 1993!.....p` |
| KI    | `52 41 52 45 C2 F2 03 50 E9 2A E4 F1 51 55 C2 F2 52 41 52 45 C2 F2 51 54 07 DD 00 F8 52 84 00 50` | `RARE...P.*..QU..RARE..QT....R..P` |

- All six retail ROMs write ASCII over the unused vector-table slots and, in DKC1/2/3 and
  both Battletoads, over the COP/BRK/ABORT vectors themselves (`DIDDY ` occupies `$FFE0-$FFE5`
  including the native COP vector). Dream keeps every vector functional (all to the `rti` stub
  at `$A442`, NMI `$A4D1`, RESET `$8000`) and places `RARE` only in the unused `$FFF0-$FFF3`.
- `RARE` elsewhere: Dream: none besides 0xFFF0. BTBM 0x6311 `52 41 52 45 20 31 39 39 33`
  ("RARE 1993") embedded between an `rts` (0x6310 `60`) and code (`78 E2 20 A9 01 8D 00 42`,
  `sei ; sep #$20 ; lda #$01 ; sta $4200`), i.e. a signature string in the middle of the code
  segment. DKC1/2/3 and both Battletoads also contain "RARE"/"Rare" in credits/copyright text
  (DKC1 0x1F340 `Alias/Nintendo/RARE AC`, DKC2 0x83C0 `Rareware`, 0x3ABF04 `RARE SYSTEMS`,
  DKC3 0x32A4F9 `RARE SYSTEM`, BTBM 0xA6E55 `1993 RARE LTD`, BTDD 0xBDB7A). KI: only the two
  vector-table copies.
- The BRK slot holds `03 70` (`$7003`) in DKC1, DKC2, BTBM, BTDD and `03 50` (`$5003`) in
  DKC3 and KI. Dream has a real vector there.

**Inference**

- Filling the dead vector slots with text is a house convention (present from 1993 to 1996);
  Dream follows it in its minimal form. The `$7003`/`$5003` BRK value shared across five
  titles is unexplained; it changes between the 1993-95 and 1996 builds, so it may be a
  version stamp of the tool or template that emitted the vector block, but no byte proves that.

### 2.4 Other ASCII in Dream

**Established from bytes**: scanning the full 2 MiB for printable runs >= 6 characters gives
6098 runs; after removing tilemap patterns (`D<D<D<...` at 0x10000-0x14FC0, the 0x55 run) and
graphics noise, none is a word, date, path, filename or name. Searches for `Rare`, `19[89]x`,
`.asm`, `.s`, `.obj`, `.lst`, `Copyright`, `Nintendo`, `Twycross`, `Dream`, drive-letter paths
and common Rare staff names find nothing but `RARE` at 0xFFF0 (the `.s` and `X:\` hits at
0xC6CCD, 0x11AF42, 0x19F3FC, 0x1A6576, 0xA2921 are inside graphics data with no surrounding
text). There are no listing fragments, symbol tables, or source text in the image.

---

## 3. Code idioms in the live program (from `out/dream.asm`, 6253 traced instructions)

**Established from bytes** (counts over the traced listing; sites given where few):

| idiom | count | sites / notes |
|-------|-------|---------------|
| `jsr (abs,X)` table dispatch | 6 | `$8070` `(game_mode_table,X)`, `$80FF` `(jtbl_C08272,X)`, `$815B` `(jtbl_C0827A,X)`, `$823C` `(jtbl_C0828A,X)`, `$8247` `(jtbl_C08282,X)`, `$98DF` `(jtbl_C099DD,X)` |
| `jmp (abs)` through RAM | 4 | `$A4E6` `jmp ($0000)` NMI handler; `$9219`, `$99B6`, `$B022` `jmp ($0004)` callback dispatch |
| `jmp (abs,X)` | 0 | |
| `jml` | 1 | `$A4D1` `jml $80A4D5` (NMI vector stub re-enters in bank `$80`) |
| `jml [abs]` / `jmp [abs]` | 0 | |
| `brl` / `per` | 0 | |
| `pea` | 5 | `$95F2` `pea $807F`, `$98EE` `pea $8080`, `$9D6A` `pea $8000`, `$A54D`/`$A6CD` `pea $8080` (all followed by `plb ; plb`) |
| `phk ; plb` | 5 | `$800A`, `$8049`, `$9ECD`, `$A006`, `$A553` |
| `plb` total / `phb` | 20 / 1 | `phb` only at `$8262` before the single `mvn` |
| `mvn` / `mvp` | 1 / 0 | `$8263` `mvn $00,$00` |
| `[dp],Y` / `[dp]` | 63 / 11 | 24-bit pointers in direct page (`$26`, `ptr_04`, ...) |
| `(dp),Y` / `(dp,X)` / stack-relative | 0 / 0 / 0 | |
| 24-bit absolute operands | 425 (237 `,X`) | banks referenced: `$7F` 242, `$00` 99, `$80` 36, `$C4` 19, `$C2` 11, `$C6` 10, `$7E` 6, `$C5` 2 |
| `tcd` / `phd,pld` | 3 / 0 | `$8009`, `$8048` (both `lda #$0000 ; tcd`), `$A362`; direct page is never relocated away from `$0000` |
| `tsc` / `tcs` | 3 / 4 | `$9331`-`$94E2`: stack pointer used as a scratch index and restored |
| `php` / `plp` | 0 / 0 | |
| `wdm` / `cop` / `brk` / `stp` | 0 | no debugger hooks; `wai` once at `$A4FD`, `rti` once at `$A442` |
| `rep`/`sep` | `rep #$20` 113, `sep #$20` 112, `rep #$30` 19, `sep #$30` 6, `sep #$10` 6, `rep #$10` 4 | |
| `xba` | 20 | |
| `rts`/`rtl` flag state | 100 of 100 at m0x0 | every return happens in 16-bit A and X |
| `sec`/`clc` immediately before a return | 0 | no carry-flag return convention |
| `cmp #$8000 ; ror A` | 17 pairs | sign-preserving shift right (also in DKC1/2, BTBM; section 1.3) |
| `asl A` x5 chains | 13 | index * 32 |
| NMI handler install `lda #$addr ; jmp $A4E9` | 2 | stores the handler pointer to `$0000` |
| `jsl` targets | 11 | `anim_update` 6, `spc_command` 2, `spc_init` 1, `sub_C0A538` 1, `sub_C18415` 1 |

Reset sequence at `$8000`: `clc ; xce ; sei ; rep #$30 ; cld ; lda #$0000 ; tcd ; phk ; plb ;
ldx #$01FF ; txs ; tdc ; tax ; sta $7E0000,X ; sta $7F0000,X ; inx ; inx ; bne` (`18 FB 78 C2
30 D8 A9 00 00 5B 4B AB A2 FF 01 9A`). The `18 FB 78 C2 30` opening exists in Dream (0x0 and
0x8000), DKC2 (0x35CEF0, 0x35CF63) and KI (0x846C); `ldx #$01FF ; txs` (`A2 FF 01 9A`) is in
all seven ROMs (stack at `$01FF`).

**Inference**

- The style is a 16-bit-first register discipline: routines are entered and left with
  `rep #$20/#$10` state, byte accesses are bracketed by `sep #$20 ... rep #$20` (113/112
  pairs), results are returned in registers or memory, never in the carry. No stack frames,
  no stack-relative addressing, no direct-page relocation, no `php/plp` state saving: this is
  classic hand-written 65816 in the 6502 tradition, not compiler output (no compiler of the
  period emits `jsr (table,X)`, `pea/plb/plb` bank switching or the `cmp #$8000/ror` shift).
- Bank handling via `pea $8080 ; plb ; plb` (data bank `$80`) and `phk ; plb` shows the code
  was assembled for the `$80`-mirror view of the HiROM (`jsl $818000`), with long addressing
  used for `$7F` work RAM and for data banks `$C2/$C4/$C5/$C6`.
- The 24-bit `[dp],Y` pointers with zero `(dp),Y` usage, the unrolled `stz` clears, the
  register-init boilerplate and the six-way DMA helper are the same choices found in the DKC
  titles, consistent with one in-house library and one house style across the studio rather
  than a particular assembler feature.

---

## 4. Build artefacts: the stale image at 0x0000-0x7FFF

**Established from bytes**

- The region has its own reset routine at 0x0000 (`18 FB 78 C2 30 A9 00 00 5B 4B AB A2 FF 01
  9A A2 FE 1F 74 00 CA CA 10 FA A9 00 00 85 B5 20 DB 8B ...`): same prologue as the live
  reset but it clears direct page with `stz $00,X` from `$1FFE` down, stores `$B5`, then
  `jsr $8BDB`, clears `$7F0000` and dispatches through `jsr ($814C,X)`. It calls
  `jsl $818000` (0x005D) and `jsl $8183CE` (0x0064), the live `spc_init`/`spc_command`.
- Tracing it as if loaded at `$8000` (2281 instructions, 4927 code bytes, 0x0000-0x2C8F;
  jump table at `$814C` with five entries `$942B $90F7 $936A $B5A5 $AA0A`), and aligning it to
  the live listing with address operands masked: 1065 of 2281 stale instructions fall in 112
  aligned runs of >= 5 instructions, and **1044 of those 1065 are byte-identical**; the 21
  that differ are only in address operands.
- The live-minus-stale shift is constant inside a block but differs between blocks:

  | stale range        | live counterpart                       | shift    |
  |--------------------|----------------------------------------|----------|
  | `$81EF-$82BC`      | `$9EE6-$9FA1` (`loc_C09ED1..loc_C09FA1`) | +0x1CF5..+0x1CF7 |
  | `$831A-$84AB`      | `$A01F-$A187` (`loc_C0A00A..loc_C0A156`) | +0x1D04..+0x1D05 |
  | `$8535-$85B3`      | `$8FA6-$9011` (`sub_C08F47`)             | +0xA6D..+0xA71 |
  | `$8B5F-$8CE9`      | `$A309-$A445` (`loc_C0A2E2`, `ppu_init`, `sub_C0A445`) | +0x17AA |
  | `$9237-$9343`      | `$85FD-$8711` (`loc_C085F3`, `loc_C0861D`) | **-0xC3A** (moved earlier) |
  | `$9733-$9FC2`      | `$A4D5-$AD68` (`loc_C0A4D5` .. `loc_C0AD66`) | +0xDA2..+0xDA6 |
  | `$ABB7-$ABE5`      | `$ADE9` (`sub_C0ADE7`)                   | +0x232 |

  The +0x1CF5 to +0x1D05 and +0xDA2 to +0xDA6 spreads show 16 and 4 bytes inserted inside
  those blocks between the two builds.
- 17 `jsr`/`jmp abs` call sites lie inside aligned runs; their operand movement (live minus
  stale) is +0xDA6 for 11 of them (e.g. `$9919->$A6BF`, `$99B1->$A757`), and +0x1E2F, +0x1CF7,
  +0x1D04, +0x1D05, +0x17AA, +0xDA3 for the rest. **None is off by 1 or 2 bytes.** The
  "one byte off" observation in `NOTES.md` (`jsr $ABCB` landing mid-instruction) is the old
  address of `sub_C0ADFD`, which moved by +0x232.
- Of the 177 `jsr/jmp abs` opcodes with operands in `$8000-$C00D` found anywhere in the
  stale bytes, 19 hit a live label exactly and 114 hit a live instruction boundary; that is
  what random old addresses over a reorganised code bank look like, not a fixed offset.
- The rest of the region, 0x2C90-0x7EF5, is an older copy of the animation script data now
  in bank `$C4` (`anim_script_table`, 0x41858-0x46588): 18 identical runs >= 32 bytes, e.g.
  stale 0x3391 = live 0x42BB1 (95 bytes), 0x41C0 = 0x4319E (84), with shifts around
  +0x3F820. 0x7EF6-0x7FE6 is 0xF0 bytes of 0xFF followed by 26 bytes `04 77 FF FF 6C 85 04 05
  6F 87 0C 05 77 67 76 67 3C 37 04 0D 6F 87 04 85 00 50`; there is no vector table in this
  half (0x7FFC-0x7FFD = `04 85`).
- The two Dream-only DMA/OAM helpers also exist twice in the file: `clear_sprite_table` at
  0x175B (stale) and 0xA500 (live), the DMA helper at 0xCA1 and 0xA44B, the `pea $8080` stub at
  0x17AA/0x1927 and 0xA54D/0xA6CD.

**Inference**

- 0x0000-0x7FFF is the output of an earlier assembly of the same sources in which the
  modules of bank `$C0` were in a different order (the game-mode block that is now at
  `$85F3` was after the OAM routines then; the OAM/tile routines were ~7.4 KiB earlier) and
  with small edits inside two blocks. The per-block constant shifts are the signature of an
  assembler that concatenates modules in source order: reordering or inserting a module moves
  everything after it by a constant.
- The image being left in the low half of bank `$C0` (which HiROM maps but the reset vector
  never reaches) means the ROM file was built by writing each 32 KiB block from its own
  output and the block for `$C0:0000-$7FFF` was simply not regenerated, consistent with a
  build that assembles fixed-size bank images into a pre-existing file rather than linking
  one image. The sound interface in bank `$C1` did not move between the two builds, so it
  was a separately assembled module with a fixed origin.

---

## 5. Summary

Established from bytes:

1. Dream's SPC700 driver is the DKC2/DKC3 driver re-assembled with one 51-byte sequence
   opcode added (`seq_master_volume`, SPC `$0E5A`); 96.3 % of bytes are identical and every
   other difference is a +0x33 pointer relocation. KI and DKC1 use earlier layouts of the same
   driver; Battletoads uses a different driver that shares only the pitch table.
2. The 65816 sound-interface source is shared with DKC2 down to an unreferenced 14-byte dead
   entry-point variant (`lda #$06E3`) that both builds carry; KI has the same file with
   `$06D9`.
3. Ten bank-`$C0` library routines/tables are verbatim in DKC1/2/3 (up to 142 bytes), and the
   VRAM DMA helper and OAM clear are in all six retail ROMs.
4. Dream is padded with 0x55 (only in bank `$C1` and 2-18-byte half-bank tails), the retail
   titles with 0x00 or 0xFF; Dream's header is overwritten by tilemap data and has no
   checksum, while all six retail ROMs have a valid one; `RARE` sits in the unused vector
   slot `$FFF0` as in KI, and every Rare title puts ASCII in dead vector slots.
5. The code contains no compiler patterns, no debugger hooks (`wdm/cop/brk`), no stack
   frames, no direct-page relocation, no flag-return convention, and no embedded text.
6. 0x0000-0x7FFF is an older build of the same bank-`$C0` sources with modules reordered
   (block shifts +0x1D04, +0x17AA, +0xDA6, +0xA71, +0x232, -0xC3A) plus an older copy of the
   bank-`$C4` animation data.

Inference: a single in-house assembler-based build (module concatenation, fixed-origin bank
images written into a fixed-size ROM file, 0x55 residue fill, header/checksum added only at
mastering) shared across the DKC line, with the sound driver maintained as one source tree
across DKC1, KI, DKC2, DKC3 and Dream. The bytes do not name the assembler; nothing in the
image distinguishes, for example, a Cross-Products/SNASM-style toolchain from a custom one.
