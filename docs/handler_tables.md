# Dream: Land of Giants: RAM-pointer dispatch tables (`jmp ($0004)`)

Derived statically from `DREAM.sfc` (sha1 2675d7af...) by reading `out/dream.asm` and the raw
bytes with python3; every candidate handler was re-decoded with `trace65816.decode()` at the
caller's flag state. Addresses are HiROM `$C0:0000 + file offset`. RAM names: `ptr_04` = `$04`.

There are three `jmp ($0004)` sites in the live program:

| site     | pointer source                                     | status |
|----------|----------------------------------------------------|--------|
| `$99B6`  | two-level table `data_C0B66A` (bank `$C0`)          | live, 4 distinct handlers |
| `$B022`  | animation-script records in bank `$C4` (`data_C41858`) | live, 12 distinct handlers |
| `$9219`  | `data_C0B208` (`orphan_C09206`)                     | dead: table holds no code pointers |

Every write to `ptr_04` in `0x8000-0xC00D` was reviewed (`grep 'ptr_04'` on the listing, 90 hits);
apart from the three above, `$04` is a scratch word (SPC upload source pointer in bank `$C1`,
`sta/lda/adc/ror/lsr ptr_04` arithmetic, `ptr_04,X`/`,Y` array scratch) and never reaches an
indirect jump. There are no `stx ptr_04` writes that feed a dispatch (the only one, `$B107`, stores
an entity index for a `cpy` loop). No 16-bit stores to `$0004` via absolute addressing exist.

---

## 1. `data_C0B66A`: velocity-to-animation-rate handler table

### Indexing code: `sub_C098DA` at `$9986-$99B6` (entry m0x0, X = entity index)

```
C098EE  pea $8080 ; plb                 DB = $80 for the table reads (restored by plb at $99D7)
C09994  lda $0708,X ; tay              Y = entity type * 2            (level-1 index)
C09998  lda $0728,X                    A = entity state word (bits 2+ = state, bit 1 = facing)
C0999B  adc $0BAC                      + row offset ($0BAC = $0018 in game mode 1, else 0; see $84DA)
C0999E  lsr A ; and #$FFFE             A = (v >> 2) * 2   (drop the facing bit, keep a word index)
C099A2  adc data_C0B66A,Y              + level-1 byte offset for this type
C099A5  tay
C099A6  lda data_C0B66A,Y              level-2 word = handler address (bank $C0/$80)
C099A9  beq loc_C099D4                 0 = no handler: store A(=0) as the rate
C099AB  sta ptr_04
C099AD  lda $0868,X ; bpl ; eor #$FFFF ; inc A     A = |velocity| (signed 16-bit at $0868,X)
C099B6  jmp ($0004)
```

Combined index (bytes from `$B66A`):

    B66A[ B66A[type*2] + (((state | facing) + $0BAC) >> 2) * 2 ]   with state = $0728,X, $0BAC in {0, $18}

The carry into `adc data_C0B66A,Y` is bit 0 of `($0728,X + $0BAC)`, which is always 0 because
`$0728,X` is written as `(old & $FFFC) | data_C0B654[..]` (values 0/2) and `$0BAC` is 0/`$18`.

The sibling tables `data_C0B6DC` (per-state signed velocity, `$9903`) and `data_C0B7AE`
(per-state animation id, `$998A`, `$B1F0`) use the same level-1 layout but index level 2 with the
raw byte value `($0728,X + $0BAC)`, i.e. one word per (state, facing) pair; `$B66A` uses one word per
state because the rate does not depend on facing. All three level-2 areas are laid out in 6-state
rows of `$18` bytes (velocity/anim tables) or 6 words (rate table).

### Element format

- Level 1 (`0xB66A-0xB67D`, 10 words, index = `$0708,X` = type*2): byte offset of the level-2 run
  relative to `$B66A`. Types 0,4,6,8,A,12 -> `$0000` (level 2 starts at `$B66A` itself, whose first
  word is 0 = no handler; those types never get a handler because their states are 0).
- Level 2 (`0xB67C-0xB6DB`): 16-bit code addresses in bank `$C0` (`$80` mirror), `$0000` = none.
  The runs overlap: type `$0E`'s run starts inside type `$02`'s, etc.

| type (`$0708`) | level-1 word | level-2 start | rows (6 words each, `0000 h h 0000 0000 0000`) |
|---|---|---|---|
| `$02` | `$0012` | `$B67C` | `99D2 99D1`, `99D2 99D2`, `99BA 99BA`, `99D2 99D2`, `99D2 99D2`, `99D2 99D2`, `99B9 99B9`, `99D2 99D2` |
| `$0C` | `$005A` | `$B6C4` | `99B9 99B9`, `99D2 99D2` |
| `$0E` | `$002A` | `$B694` | `99BA 99BA`, `99D2 99D2`, `99D2 99D2`, `99D2 99D2`, `99B9 99B9`, `99D2 99D2` |
| `$10` | `$0042` | `$B6AC` | `99D2 99D2`, `99D2 99D2`, `99B9 99B9`, `99D2 99D2` |
| `$00,$04-$0A,$12` | `$0000` | `$B66A` | first word 0 -> no handler |

Only states 1 and 2 of each row (`$0728` = `$04`/`$08` plus row offset) have a handler; every
other state stores rate 0.

Raw level-2 words (file `0xB67C`..`0xB6DA`):

    B67C: 0000 99D2 99D1 0000 0000 0000 0000 99D2 99D2 0000 0000 0000
    B694: 0000 99BA 99BA 0000 0000 0000 0000 99D2 99D2 0000 0000 0000
    B6AC: 0000 99D2 99D2 0000 0000 0000 0000 99D2 99D2 0000 0000 0000
    B6C4: 0000 99B9 99B9 0000 0000 0000 0000 99D2 99D2 0000 0000 0000

### Handlers (entry: m0x0, DB=$80, A = |velocity|, X = entity index, Y = table index)

All of them scale A and fall into the shared tail `loc_C099D4`, which stores the result in
`$0A68,X` (animation playback rate, 8.8 fixed; consumed at `$AF9B-$AFC0` as the per-tick frame
advance, clamped to `$0100`), then `plb ; jsl sub_C0AEC8 ; rts`.

| addr | bytes | code | result | referenced from |
|---|---|---|---|---|
| `C099B9` | `4A 4A 4A 85 18 4A 18 65 18 80 10` | lsr x3 ; sta $18 ; lsr ; clc ; adc $18 ; bra $99D4 | A*3/16 | `0xB6C6`, `0xB6C8` |
| `C099BA` | `4A 4A 85 18 4A 18 65 18 80 10` | lsr x2 ; sta $18 ; lsr ; clc ; adc $18 ; bra $99D4 | A*3/8 | `0xB696`, `0xB698` |
| `C099D1` | `4A 4A 4A` (falls into `$99D4`) | lsr x3 | A/8 | `0xB680` |
| `C099D2` | `4A 4A` (falls into `$99D4`) | lsr x2 | A/4 | 11 entries (`0xB67E`, `0xB68A`, ...) |
| `C099D4` | `9D 68 0A AB 22 C8 AE 80 60` | sta $0A68,X ; plb ; jsl sub_C0AEC8 ; rts | shared tail | branch target |

`orphan_C099B9` is therefore the first table handler; the tracer's `.db` run `0x99C4-0x99D3` holds
the remaining variants. Three of them are not referenced by any table word in the ROM
(searched the whole image for the little-endian words):

| addr | code | result | note |
|---|---|---|---|
| `C099C4` | lsr x4 ; sta $18 ; lsr ; clc ; adc $18 ; bra $99D4 | A*3/32 | unreferenced |
| `C099D0` | lsr x4 (falls into `$99D4`) | A/16 | unreferenced |
| `C099D3` | lsr (falls into `$99D4`) | A/2 | unreferenced |

They decode cleanly at m0x0 and align with the referenced entries (all 1-byte `lsr` prefixes), so
seeding them is safe but optional.

---

## 2. `$B022` dispatch: animation-script callbacks (pointers live in bank `$C4`)

### Pointer source

`sub_C0B022` is `jmp ($0004)` and is called only from `$AF79` and `$AFED` (the two `jsr sub_C0B022`;
the other `20 22 B0`/`B0 22` hits at file `0x1A36/0x1BD8/0x1D70/0x1F09` are in the stale build
image). Both callers are inside the animation player (`sub_C0AEC8` / `loc_C0AF05` / `loc_C0AF9B`)
and load `$04` straight from the script data:

```
C0AEDC  lda data_C41858,X   ; X = $09E8,X = animation id (byte index, even)
C0AEE1  sta $A0 ; lda #$00C4 ; sta $A2      ; $A0 = 24-bit script pointer in bank $C4
...
C0AF61  lda [$A0],Y          ; Y = frame*8 + 2: call mode
C0AF63  beq skip             ; 0 = no callback
C0AF65  cmp #2 ; beq call    ; 2 = call every tick this frame is shown
C0AF6A  cmp #1 ; bne skip
C0AF6F  lda $52 ; beq skip   ; 1 = call once, on the tick the frame is entered ($52 = new-frame flag)
C0AF73  dey ; dey
C0AF75  lda [$A0],Y          ; Y = frame*8 + 0: callback address
C0AF77  sta ptr_04
C0AF79  jsr sub_C0B022       ; -> jmp ($0004)
```

`$AFD5-$AFED` is the same sequence for the velocity-driven playback mode (`loc_C0AF9B`,
`Y = ($0A08,X << 3 & $0FF8) | 2`).

### Script format (`data_C41858`, bank `$C4`)

- `0x41858-0x419B3`: 174 words, animation id (even byte index, `$09E8,X`) -> script offset in
  bank `$C4`. 96 distinct scripts, `$1850` (the empty script) to `$657A`. Anim ids are what
  `data_C0B7AE` and the `+6` field of a terminator record hold.
- Script = consecutive 8-byte frame records `{ callback:u16, mode:u16, duration:u16, frame:u16 }`:
  - `callback`: 16-bit code address in bank `$C0`, only meaningful when `mode` is 1 or 2; every
    record with mode 0 has callback 0 (verified across all 96 scripts).
  - `mode`: 0 none, 1 call on frame entry, 2 call every tick.
  - `duration`: ticks; `$FFFE` = loop to frame 0 (`$AF8C`), `$FFFF` = switch to the animation id in
    `frame` (`$AF90-$AF97`); duration 0 in the first record selects the velocity-driven mode
    (`$AEF2-$AEF6`).
  - `frame`: sprite frame written to `$07C8,X`; 0 in the first record = empty animation.
- 37 scripts have no terminator: they are ended by their `$B1AE` callback, which changes
  `$09E8,X` through `sub_C0B1B1`.

### Handlers (entry: m0x0, X = entity index, Y = record offset, A = callback address, DB = caller's)

Every callback pointer found in the 96 scripts, with the anim ids that use it:

| addr | code (decoded m0x0) | proposed name | anim ids |
|---|---|---|---|
| `C0B025` | if game_mode==2 and `$0828,X` in `[$0748,$07B0)` -> `lda #$0705` else `lda #$0704`, `bra sub_C0B0C5` | `anim_cb_sfx_0704` | 02,0C,16,20,8E,98,A2,AC,110,112,14C,14E |
| `C0B052` | `lda #$0506 ; bra sub_C0B0C5` | `anim_cb_sfx_0506` | 2A,E8 |
| `C0B057` | `lda #$0606 ; bra sub_C0B0C5` | `anim_cb_sfx_0606` | 2A,E8,138,13A |
| `C0B05C` | `lda #$0602 ; bra sub_C0B0C5` | `anim_cb_sfx_0602` | 118,11A |
| `C0B061` | `lda #$0507 ; bra sub_C0B0C5` | `anim_cb_sfx_0507` | 154,156 |
| `C0B066` | `lda #$050C ; bra sub_C0B0C5` | `anim_cb_sfx_050C` | 150,152 |
| `C0B06B` | `lda #$0508 ; bra sub_C0B0C5` | `anim_cb_sfx_0508` | 150,152 |
| `C0B070` | `lda #$0709 ; bra sub_C0B0C5` | `anim_cb_sfx_0709` | 66,70,7A,84 |
| `C0B08F` | like `$B025` with `$060E`/`$060D` | `anim_cb_sfx_060E` | 7A,84 |
| `C0B0CE` | if `$0768`==0 and entity 0 (`$0828`) within `$40` px in facing direction (`bit $0788,X` / V) -> `jmp sub_C0B171` | `anim_cb_hit_player` | 118,11A |
| `C0B0FE` | `lda #$0703 ; jsr sub_C0B0C5`, then loop Y=4..`$A6` over entities of type `$0E-$10` within `$48` px in facing direction -> `jsr sub_C0B171` | `anim_cb_hit_enemies` | 3E,48,B6,C0 |
| `C0B1AE` | `lda #$0000` falling into `sub_C0B1B1` (set state 0, pick anim from `data_C0B7AE`) | `anim_cb_reset_state` | 19 scripts (3E,42,48,4C,66,70,7A,84,B6,BA,C0,C4,DE,F2,FC,106,118,11A,128,12A,130,132) |

This accounts for `orphan_C0B025`, `orphan_C0B08F`, `orphan_C0B0CE`, `orphan_C0B0FE` and the
`.db` run `0xB052-0xB074` (seven 5-byte sfx stubs, all `A9 xx xx 80 rel` to `sub_C0B0C5`).
`sub_C0B0C5` is `phx ; phy ; jsl sub_C18415 ; ply ; plx ; rts` (sound command with X/Y preserved);
`sub_C0B171` puts the target entity into state `$10/$12/$14/$16` via `sub_C0B1B1`.

Unreferenced fragment in the same area (no word in the ROM points at it; the `0x18B05` hit is inside
bank `$C1` data): `C0B1FE`: `stz $07A8,X ; stz $0808,X ; stz $0A48,X ; rts` (10 bytes, decodes
cleanly at m0x0). Optional.

---

## 3. `$9219` (`orphan_C09206`): dead dispatcher

```
C09206  clc ; adc $0BB8 ; tax
C0920B  lda data_C0B208,X ; beq rts
C09211  sta ptr_04 ; lda $0BBA ; inc $0BBA
C09219  jmp ($0004)
```

`$0BB8`/`$0BBA` are only ever cleared (`$80CF/$80D2`) and nothing calls `$9206` (the words at
`0x191BF`/`0x4E431` are in data). `data_C0B208` reads `5000 8AC0 FFC9 2C00 6800 F38E ...`: `$8AC0`
is not an instruction boundary in the live code and the rest are not ROM code addresses. The
region is now DMA/tilemap descriptor data (`0xB20E` records), so this sequencer is a leftover;
do not seed anything from it.

---

## 4. Other pointer-table candidates

- `0xA6D3-0xA756` (`data_C0A6D3` = `03 0C 30 C0` + 128 bytes): no 16-bit word in `0x8000-0xC00D`
  at any alignment. Bit-mask / lookup data, not pointers.
- `0xB1FE-0xB66A`: `0xB1FE` is the unreferenced code fragment above; `0xB208-0xB26C` are 3-byte
  DMA/HDMA descriptors; `0xB26E-0xB652` contains words like `8800/9000/A000/B000` only at odd
  offsets (VRAM/size fields). No code-pointer table.
- `0xB6DC-0xB7AD` (`data_C0B6DC`): signed velocities (`FF20 00E0 FD00 0300 ...`), no pointers.
- `0xB7AE-0xB880`: animation ids (`0110-0158` etc.) for `data_C0B7AE`; the table ends about
  `0xB880` and `0xB8B2-0xBB80` is unrelated data (even-aligned words such as `A920 A98B AF30 BFA8`
  are scattered, mostly not instruction boundaries, no stride).
- `0xC00D+`: `8958 8B51 8D40 8D28` at stride 6 are HDMA/tilemap words, not code (only `8D40`
  happens to be an instruction start).

The only ROM tables in this bank holding code addresses are `game_mode_table`/`jtbl_C08272..828A`,
`jtbl_C099DD` (all already traced) and `data_C0B66A`.

---

## 5. Lines for `tools/names.txt`

```
; ---- $B66A velocity -> animation-rate handlers (jmp ($0004) at $99B6; entry DB=$80, A=|vel|, X=entity) ----
data C0B66A anim_rate_fn_table
data C0B6DC entity_state_velocity_table
data C0B7AE entity_state_anim_table
data C0B652 facing_flag_table
data C0B654 facing_state_bits_table
C099B9 anim_rate_3_16 m0x0
C099BA anim_rate_3_8 m0x0
C099D1 anim_rate_1_8 m0x0
C099D2 anim_rate_1_4 m0x0
C099D4 anim_rate_store
; unreferenced variants in the same run (optional)
C099C4 anim_rate_3_32 m0x0
C099D0 anim_rate_1_16 m0x0
C099D3 anim_rate_1_2 m0x0

; ---- $B022 animation-script callbacks (pointers in bank $C4 scripts; entry X=entity, Y=record) ----
data C41858 anim_script_table
C0AEC8 anim_update
C0B022 anim_callback_dispatch
C0B0C5 play_sound_effect
C0B171 entity_hit_react
C0B1B1 set_entity_state
C0B025 anim_cb_sfx_0704 m0x0
C0B052 anim_cb_sfx_0506 m0x0
C0B057 anim_cb_sfx_0606 m0x0
C0B05C anim_cb_sfx_0602 m0x0
C0B061 anim_cb_sfx_0507 m0x0
C0B066 anim_cb_sfx_050C m0x0
C0B06B anim_cb_sfx_0508 m0x0
C0B070 anim_cb_sfx_0709 m0x0
C0B08F anim_cb_sfx_060E m0x0
C0B0CE anim_cb_hit_player m0x0
C0B0FE anim_cb_hit_enemies m0x0
C0B1AE anim_cb_reset_state m0x0
; unreferenced fragment (optional)
C0B1FE entity_clear_anim_unused m0x0

; ---- dead dispatcher (documented only, do not seed) ----
; C09206 seq_dispatch_dead  ; jmp ($0004) via data_C0B208, table no longer holds code
```
