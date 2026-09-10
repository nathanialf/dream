# DKC1/2/3 cross-reference for Dream's shared code

Scope: cross-reference the routines and RAM that `docs/toolchain_evidence.md` establishes as
byte-identical (or near-identical) between `Dream: Land of Giants` and the Rare DKC titles,
against the actual labels used in four public disassemblies, to see whether any of their
names are worth adopting. Per `docs/LEGAL.md` rule 4, every name pulled from a reference
project is credited below and in the paste-ready blocks at the end.

## Sources checked

| project | author | clone path (session scratchpad, read-only) | upstream |
|---|---|---|---|
| DKC1 disassembly | Yoshifanatic1 | `ref/Donkey-Kong-Country-1-Disassembly` | https://github.com/Yoshifanatic1/Donkey-Kong-Country-1-Disassembly |
| DKC2 disassembly | Yoshifanatic1 | `ref/Donkey-Kong-Country-2-Disassembly` | https://github.com/Yoshifanatic1/Donkey-Kong-Country-2-Disassembly |
| DKC3 disassembly | Yoshifanatic1 | `ref/Donkey-Kong-Country-3-Disassembly` | https://github.com/Yoshifanatic1/Donkey-Kong-Country-3-Disassembly |
| DKC2-disassembly | p4plus2 | `ref/DKC2-disassembly` | https://github.com/p4plus2/DKC2-disassembly |

All four are GPLv3 except p4plus2's (no LICENSE file present in the clone). Nothing from
either was copied into Dream's source or data; only short label strings and the reasoning
below are reused, per `docs/LEGAL.md`.

---

## 1. Toolchain statements in the reference projects (task 1)

I read every readme/notes file in all four clones for claims about **Rare's original**
assembler, build process, source layout, or leftover symbols in the retail ROMs (as opposed
to the disassembly authors' own reverse-engineering toolchain). Finding: **none of the four
projects makes any claim about Rare's original toolchain.** What each one says instead:

- `ref/Donkey-Kong-Country-1-Disassembly/readme.md`, `.../Donkey-Kong-Country-2-Disassembly/readme.md`,
  `.../Donkey-Kong-Country-3-Disassembly/readme.md` (Yoshifanatic1, identical boilerplate in
  all three): "It uses V1.2.0 [DKC1] / V1.1.2 [DKC2] / V1.1.3 [DKC3] of my SNES ROM
  framework (https://github.com/Yoshifanatic1/SNES-ROM-Framework) ... you'll need a
  headerless clean copy of a supported ROM version in order to extract its assets using the
  provided batch script." This describes *their* re-assembly toolchain (asar + a batch
  extractor), not Rare's. The `ROM_Map_DKC2_U1.asm` macro block (`ref/Donkey-Kong-Country-2-
  Disassembly/DKC2/RomMap/ROM_Map_DKC2_U1.asm:50-53`) records only the retail metadata
  already visible in the header/vector-table ASCII that `docs/toolchain_evidence.md` section
  2.3 also derived from bytes: `!Define_Global_DeveloperName = "Rare"`,
  `!Define_Global_ReleaseDate = "December 1995"`, `!Define_Global_LicenseeName = "Nintendo"`,
  `!Define_Global_BaseROMMD5Hash = "98458530599b9dff8a7414a7f20b777a"`. No assembler name,
  source path, or build tool of Rare's is recorded anywhere in these files.
- `ref/DKC2-disassembly/readme.md` (p4plus2): a short label/comment style guide ("Routines
  that act like game loops get names such as `run_<action>`", "Underscores only", "hard
  limit for line length is 120 chars"). This is a convention document for p4plus2's own
  hand-written labels; it says nothing about how Rare built the original ROM.
- `ref/DKC2-disassembly/notes.txt`: a scratch log of unresolved addresses, DMA trigger
  sites, and a sprite-command table; investigative notes rather than toolchain claims.
- No file in any of the four repos mentions an assembler name (SNASM, WLA, ORCA/M,
  Cross-Products, etc.), a source-file layout, or leftover debug text in the retail ROMs.
  The only "leftover text" observations anywhere are Dream's own, in
  `docs/toolchain_evidence.md` section 2.3-2.4 (the `RARE`/`DIDDY `/copyright strings baked
  into the retail vector tables and credits banks), which is Dream-project analysis, not
  something asserted by the reference projects.

Conclusion for task 1: the reference disassemblies are silent on Rare's build process. All
toolchain conclusions in `docs/toolchain_evidence.md` (module-concatenating in-house
assembler, fixed-origin bank images, per-title fill/checksum conventions) rest on Dream's own
byte-level comparison, not on anything documented by these projects.

---

## 2. SPC700 driver: Dream ↔ DKC2 label alignment (task 2)

### 2.1 Finding: neither DKC2 disassembly names the SPC700 engine

- Yoshifanatic1's `DKC2/SPC700/SPC700_Engine_DKC2.asm` (1644 lines, `base $0560` at line 3)
  and `DKC2/SPC700/InitializeSPC700.asm` (the IPL loader, `base $04D8`) label every routine
  and table purely by address: `CODE_0560`... `CODE_11CB`, `DATA_0FA5`, `DATA_1199`, etc.
  There is not one hand-chosen name in either file.
- Yoshifanatic1's `DKC2/SPC700/ARAM_Map_DKC2.asm` (the file `DKC2_LoadGameSpecificMainSPC700Files`
  includes for SPC RAM names) is **empty** (0 bytes).
- p4plus2's `DKC2-disassembly` has no SPC700 disassembly at all (only `music.txt` and
  `sound_effects.txt`, which list DKC2's own song/sfx *content* IDs; they do not apply to
  Dream, which is a different game with different songs/samples at those same driver slots).

So there are no DKC2 names to adopt for the driver itself. Dream's own tracer-derived names
in `spc/driver.asm` (`seq_instrument`, `channel_update`, `dsp_init`, `scale_volume`, the
per-slot field comments `duration[x]`, `gate[x]`, `pitch_lo[x]`, `vib_rate[x]`, etc.) are
already substantially more descriptive than anything in either reference project, and I am
**not** proposing any renames in `spc/driver.asm` (see the empty rename block at the end).

### 2.2 What the comparison is still worth: address alignment

`docs/toolchain_evidence.md` 1.1 established the relationship precisely: Dream inserted one
51-byte handler (`seq_master_volume`, SPC `$0E5A-$0E8C`) that doesn't exist in DKC2, and every
other byte difference is a pointer exactly `+0x33` larger in Dream. That gives an exact rule,
confirmed against the source files below:

- **Dream SPC addr < `$0E5A`: identical address in DKC2.**
- **Dream SPC addr in `$0E5A-$0E8C`: no DKC2 counterpart (Dream-only opcode).**
- **Dream SPC addr >= `$0E8D`: DKC2 address = Dream address − `0x33`.**

Verified directly against `SPC700_Engine_DKC2.asm`: Dream `dsp_init` ($103E) → DKC2
`CODE_100B` (line 1432); Dream `sfx_start` ($112A) → DKC2 `CODE_10F7` (line 1539); Dream
`pitch_table` ($11CC) → DKC2 `DATA_1199` (line 1628); Dream `seq_cmd_table` ($0FD8, 51
entries) → DKC2 `DATA_0FA5` (line 1379, 51 entries, same null slots at $25/$28-$2A/$2D-$2F
and the same duplicated `seq_echo_off` at $30/$32). Full opcode table:

| idx | Dream SPC | Dream label | DKC2 label | note |
|---|---|---|---|---|
| $00 | $0B18 | `seq_end` | `CODE_0B18` | |
| $01 | $0B72 | `seq_instrument` | `CODE_0B72` | |
| $02 | $0BB6 | `seq_volume` | `CODE_0BB6` | |
| $03 | $0CD7 | `seq_jump` | `CODE_0CD7` | |
| $04 | $0CE6 | `seq_call` | `CODE_0CE6` | |
| $05 | $0D34 | `seq_return` | `CODE_0D34` | |
| $06 | $0D70 | `seq_set_length` | `CODE_0D70` | |
| $07 | $0D8F | `seq_clear_length` | `CODE_0D8F` | |
| $08 | $0D9B | `seq_slide_up` | `CODE_0D9B` | |
| $09 | $0DA2 | `seq_slide_down` | `CODE_0DA2` | |
| $0A | $0DD6 | `seq_slide_off` | `CODE_0DD6` | |
| $0B | $0DEB | `seq_tempo` | `CODE_0DEB` | |
| $0C | $0DF8 | `seq_tempo_add` | `CODE_0DF8` | |
| $0D | $0E11 | `seq_vibrato` | `CODE_0E11` | |
| $0E | $0E05 | `seq_vibrato_off` | `CODE_0E05` | |
| $0F | $0E1A | `seq_vibrato_delay` | `CODE_0E1A` | |
| $10 | $0E45 | `seq_adsr` | `CODE_0E45` | |
| $11 | $0E5A | `seq_master_volume` | *(none; table entry is `dw $0000` in DKC2)* | Dream-only opcode |
| $12 | $0EA4 | `seq_finetune` | `CODE_0E71` | shift −0x33 begins here |
| $13 | $0EAE | `seq_transpose` | `CODE_0E7B` | |
| $14 | $0EBB | `seq_transpose_add` | `CODE_0E88` | |
| $15 | $0ECA | `seq_echo_setup` | `CODE_0E97` | |
| $16 | $0EF7 | `seq_echo_on` | `CODE_0EC4` | |
| $17 | $0F0F | `seq_echo_off` | `CODE_0EDC` | |
| $18 | $0F29 | `seq_fir` | `CODE_0EF6` | |
| $19 | $0F43 | `seq_noise_clock` | `CODE_0F10` | |
| $1A | $0F56 | `seq_noise_on` | `CODE_0F23` | |
| $1B | $0F67 | `seq_noise_off` | `CODE_0F34` | |
| $1C | $0E8D | `seq_set_note_E0` | `CODE_0E5A` | this is the slot Dream's new opcode displaced |
| $1D | $0E97 | `seq_set_note_E1` | `CODE_0E64` | |
| $1E | $0C83 | `seq_volume_presets` | `CODE_0C83` | (< $0E5A, unshifted) |
| $1F | $0CA0 | `seq_echo_delay` | `CODE_0CA0` | |
| $20 | $0C02 | `seq_volume_preset` | `CODE_0C02` | |
| $21 | $0CFF | `seq_call_once` | `CODE_0CFF` | |
| $22 | $0B97 | `seq_instr_full` | `CODE_0B97` | |
| $23 | $0BF0 | `seq_volume_mono` | `CODE_0BF0` | |
| $24 | $0C4E | `seq_master_percent` | `CODE_0C4E` | |
| $26 | $0F77 | `orphan_slide_up2` | `CODE_0F44` | stale in both |
| $27 | $0F81 | `orphan_slide_down2` | `CODE_0F4E` | stale in both |
| $2B | $0FAF | `orphan_gate_on` | `CODE_0F7C` | stale in both |
| $2C | $0FB9 | `orphan_gate_off` | `CODE_0F86` | stale in both |
| $31 | $0C18 | `orphan_volume_preset2` | `CODE_0C18` | stale in both |

Other subroutines/tables (all unshifted, addr < $0E5A, so Dream addr == DKC2 addr; `CODE_`/
`DATA_` labels below are DKC2's, confirmed present in `SPC700_Engine_DKC2.asm` /
`InitializeSPC700.asm`):

| Dream SPC addr | Dream label | DKC2 label |
|---|---|---|
| $04D8 | `spc_loader` | `CODE_04D8` |
| $055D | `data_055D` (`cmd_param`) | `DATA_055D` |
| $0560 | `data_0560` (sample remap) | `DATA_0560` |
| $0660 | `start_song` | `CODE_0660` |
| $0672 | `driver_entry` | `CODE_0672` |
| $0683 | `main_loop` | `CODE_0683` |
| $068C | `cmd_receive` | `CODE_068C` |
| $06A7 | `cmd_table` | `DATA_06A7` |
| $06B7/$06FA/$0702/$070A/$0712/$0739/$077B/$07DB | `cmd3_fade_and_song`/`cmd2_set_mono`/`cmd1_set_E7`/`cmd0_set_E8`/`cmd5_voice5_volume`/`cmd4_pitch_offset`/`cmd6_play`/`cmd7_stop_to_loader` | `CODE_06B7`/`CODE_06FA`/`CODE_0702`/`CODE_070A`/`CODE_0712`/`CODE_0739`/`CODE_077B`/`CODE_07DB` |
| $06EB | `dsp_step_toward_zero` | `CODE_06EB` |
| $0813 | `seq_step` | `CODE_0813` |
| $0867 | `seq_note` | `CODE_0867` |
| $09BC | `channel_update` | `CODE_09BC` |
| $0B64 | `seq_pop_x` | `CODE_0B64` |
| $0B69 | `seq_retrigger` | `CODE_0B69` |
| $0B8B | `seq_load_srcn` | `CODE_0B8B` |
| $0BC2 | `seq_read_volume` | `CODE_0BC2` |
| $0BCC | `sub_0BCC` | `CODE_0BCC` |
| $0C59 | `scale_volume` | `CODE_0C59` |
| $0D1C | `seq_push_return` | `CODE_0D1C` |
| $0E25 | `seq_read_vibrato` | `CODE_0E25` |
| $0E4E | `seq_read_adsr` | `CODE_0E4E` |
| $0FC8 | `voice_bits` | `DATA_0F95` (shifted −0x33, confirmed `db $01,$02,$04,...` at line 1376) |

No entry in either table has a DKC2 name worth adopting; they are all `CODE_`/`DATA_`
address labels. **Recommendation: keep every name in `spc/driver.asm` as-is.** The alignment
table above is offered as a navigation aid (e.g. for cross-checking future SPC700 fixes
against DKC2's copy) rather than a source of renames.

---

## 3. 65816 shared routines (task 3)

### 3.1 Bank $C0 library routines (verbatim in DKC1/2/3)

Same finding as the SPC700 engine: Yoshifanatic1's batch-produced `Routine_Macros_DKC*.asm`
label every one of these by address only (`CODE_<bank><addr>`/`DATA_<bank><addr>`, using the
`$80-$FF` mirror-bank form, e.g. DKC2 file offset `0x35xxxx` → label prefix `CODE_B5xxxx`,
confirmed against `ROM_Map_DKC2_U1.asm`'s bank macro list). I looked up every address
`docs/toolchain_evidence.md` §1.3 lists and none carries a descriptive name:

| Dream routine | bytes | reference hit(s) | verified label(s) |
|---|---|---|---|
| `loc_C0A6CD`+`data_C0A6D3` (`pea $8080;plb;plb;rtl` + 2bpp→4bpp table) | 142 | DKC2 0x35A18A | `Routine_Macros_DKC2.asm:59539 CODE_B5A18A`, table at `:59545 DATA_B5A190` (byte-identical `03 0C 30 C0 01 04 10 40...`) |
| same | 137 | DKC1 0x3BAA3F | `Routine_Macros_DKC1.asm:76532 CODE_BBAA37` / table `:76539 DATA_BBAA40` (DKC1's copy inserts one extra `stz $170D` before `rtl`, so the label lands 8 bytes earlier than the raw offset; still the same stub+table) |
| same | 136 | DKC3 0x3791D0 | not found at an exact label boundary in `Routine_Macros_DKC3.asm`; nearest labelled data is `DATA_B791D4` (4 bytes downstream); the match is mid-block, consistent with §1.3's note that this stub recurs 2× in DKC3 |
| `loc_C0A04B` (metatile copy, H-flip variant) | 124 | DKC3 0x37BC8F | `Routine_Macros_DKC3.asm:103551 CODE_B7BC8F`: byte-identical (`asl` x5, `adc $1E`, `tay`, `lda $0000,y`, `eor #$4000`...) |
| same (44-byte tail) | 44 | DKC2 0x35B196 | inside `Routine_Macros_DKC2.asm:61771 CODE_B5B18F` (DKC2's own routine uses DP `$36` where Dream uses `$1E`; same algorithm, different variable, matches §1.3's description) |
| same (33-byte tail) | 33 | DKC1 0x18E96 | inside `Routine_Macros_DKC1.asm:16193 CODE_818E8F` |
| `ppu_init` | 175 | KI 0x191F8 | Killer Instinct has no public disassembly in this session's references; not checked |
| `clear_sprite_table` | 49-52 | DKC1/DKC2/BTBM/BTDD | not individually re-verified beyond the OAM RAM-address match in §4 below |

**No adoptable names.** `docs/toolchain_evidence.md` already gives these routines good,
specific Dream names (`ppu_init`, `clear_sprite_table`, `set_bg_scroll`, the `loc_C0A04B`
flip-variant family). I'm not proposing changes to bank `$C0`.

### 3.2 Bank $C1 sound interface: p4plus2's DKC2-disassembly has real names

Unlike both Yoshifanatic1 clones, **p4plus2's `DKC2-disassembly/bank_B5.asm` is hand-written
with descriptive labels and inline comments for exactly the routines Dream's bank `$C1`
shares** (`docs/toolchain_evidence.md` §1.2). This is the one place in this whole
cross-reference where adoption is warranted. Mapping (DKC2 addresses/labels from
`ref/DKC2-disassembly/bank_B5.asm`, line numbers as read):

| Dream (bank `$C1`) | what it does | DKC2 label (p4plus2) | line | argument convention (from p4plus2's comments) |
|---|---|---|---|---|
| `spc_init` ($C18000) | IPL handshake, upload loader+driver+samples, jump to `$0672` | `.upload_spc_engine_wrapper`/`.upload_spc_engine` (called from the `upload_spc_engine` JSL stub at `$B58000`) | 249-263 | no args; entered once from reset |
| `spc_ipl_upload_loader` ($C1805A) | IPL handshake (`#$BBAA`), upload 0x88-byte loader to SPC `$04D8` | `.upload_spc_base_engine` | 335-372 | no args |
| `spc_upload_driver` ($C1809F) | stream 0x699-word driver to SPC `$0560` | `.upload_spc_sound_engine` | 403-414 | no args |
| `sub_C180C1` | build+upload the global sample directory (called once from `spc_init`) | `.upload_global_samples` | 416-434 | none; reads a fixed global-sample-map pointer |
| `sub_C1815F` | walk a sample map, build the `$7E2200`-buffered directory, upload it and the raw samples | `.sample_uploader` | 436-592 | entry: `$0E/$10`=sample-map ptr, `$02`/`$06`=directory/sample-data ARAM dest, `$0A`=running sample index (Dream: `$36`/`$3A`/`$3E`) |
| `spc_upload_block` ($C1830A) | read `{dest, count}` header from a pointer, then send | `.upload_inline_spc_block` | 594-603 | entry: `[$32]`=block ptr (Dream: `[ptr_04]`) |
| `spc_send_words` ($C18324) | dest/count handshake, send `count` words from a pointer, 24-bit-wrap aware | `.upload_spc_block` | 605-644 | entry: `$35`=SPC dest, `$37`=word count, `$32`(24-bit)=source (Dream: `$07`/`$09`/`ptr_04`); returns `A` = 2×count |
| `sub_C180FF` | single-word command send with the port0 counter handshake | `.write_spc_command` | 320-333 | entry: `X` = packed `param:cmd` word, sent as one 16-bit store to APUIO1/2 |
| `sub_C18119` | load per-song data pointer (song×6 index), upload it | `.upload_song_data` | 374-388 | entry: `A`=song number (Dream: `$48`) |
| `sub_C1813D` | load per-song sound-effect-bank pointer (song×3 index), upload it | `.upload_song_sound_effects` | 390-400 | entry: `A`/`$48`=song number |
| `sub_C18392` | load per-song sample-set pointer (song×6 index), rebuild the directory | `.upload_song_sample_set` | 662-682 | entry: `$48`=song number |
| `sub_C1803E` | send `$0672`,0-count → (re)start the driver at `driver_entry` | `.execute_spc_sound_engine` | 268-273 | no args |
| C1804C (currently unlabelled 14-byte dead twin, `lda #$06E3`) | same shape as `sub_C1803E` but targets `$06E3` | `.unused_spc_execute`; **p4plus2's own comment: "Dead code, would crash SPC engine."** | 275-280 | n/a; confirmed dead in both games |
| `spc_command` ($C183CE) | pack song number, run upload-mode + all three per-song uploads + restart + play, in one `jsl` | closest DKC2 analogue is `.play_song` (same five-step sequence: enter upload mode, `upload_song_sample_set`, `upload_song_data`, `upload_song_sound_effects`, `execute_spc_sound_engine`, then play); DKC2 splits this into 8 separate JSL entries (`queue_sound_effect`, `queue_song`, `play_queued_song`, `play_song`, `play_song_with_transition`, `transition_song`, `play_queued_sound_effect`, `play_high_priority_sound`); Dream has only the one generic entry | 177-197 | entry: `A`=song/command number |
| `orphan_C183F1` (dead, sends cmd $F9 then $FE) | n/a | no exact analogue; closest shape is the small per-command JSL trampolines `.CODE_B581C2`/`.CODE_B581CE` (themselves left unnamed by p4plus2) | 282-296 | n/a |
| `sub_C18415` (`tax; jsr sub_C180FF; rtl`, called by `play_sound_effect` at `C0B0C5`) | raw pass-through to the command-word sender for sfx playback | structurally matches `.unused_play_sound_effect` (`$B58024`); **also marked dead/superseded in DKC2**, replaced there by the ring-buffered `queue_sound_effect`/`play_queued_sound_effect` pair | 34-46 | entry: `X` = packed `channel:sfx_id` |

**Interpretation:** Dream's sound-effect call path (`play_sound_effect` → `sub_C18415` →
`sub_C180FF`, one direct command write, no queueing) matches the code DKC2 kept around as
**dead/superseded** (`.unused_play_sound_effect`) rather than the priority-ring-buffer system
DKC2 actually uses (`queue_sound_effect`/`play_queued_sound_effect`/`play_high_priority_sound`).
That is consistent with Dream being an earlier or simpler build of the same sound-interface
source file, as `docs/toolchain_evidence.md` §1.2 already inferred from the shared dead
`$06E3` twin.

`docs/toolchain_evidence.md` also notes the argument convention question for `sub_C1815F`/
`sub_C180C1`/`sub_C18119`/`sub_C1813D`; the p4plus2 comments above answer it: `A`/`$48` is
always the **song number**, and the per-routine direct-page pointers (`$0E/$10`, `$02/$06`,
`$0A`...; Dream: `$36/$38/$3A/$3C/$3E/$40/$42/$44/$48`) are private scratch registers, not
shared with the driver side.

---

## 4. RAM (task 4)

### 4.1 65816 direct page ($04-$0C, $36-$48): no address match

Dream's sound-upload scratch registers (`ptr_04`/`$06`=pointer, `$07`=`spc_dest_addr`,
`$09`=`spc_word_count`, plus the unnamed `$36/$38/$3A/$3C/$3E/$40/$42/$44/$48` used by
`sub_C180C1`/`sub_C1815F`/`sub_C18392`/`sub_C18119`/`sub_C1813D`) sit at **different**
addresses than DKC2's equivalents. p4plus2's `ram.asm` puts the corresponding scratch
registers at `$32/$34/$35/$37/$39/$02/$06/$0A/$0E/$10/$3C/$3E/$40/$42/$44` (`ref/DKC2-
disassembly/ram.asm:8-40`, `bank_B5.asm` throughout), and even there they're named
generically (`temp_32`, `temp_33`, ...; `ram.asm:29-40`), not by role. Since the code and
the address don't both match, **task 4's bar ("identical code," not just same address) is
not met here; no RAM entries proposed for this range.** (`spc_transaction = $00`,
`current_song = $1C`, `stereo_select = $1E` are DKC2's real named globals for the higher-level
music state, but Dream's own equivalents live in different places again: `spc_command`
packs the song number through `A`/`$48` rather than a persistent `current_song` byte, and
Dream has no 65816-side stereo/mono global at all; `cmd2_set_mono` sets the SPC-side
`mono_flag` directly.)

### 4.2 SPC700 zero page: genuine matches (identical code)

Because the driver is 96.3% byte-identical (`docs/toolchain_evidence.md` §1.1), every SPC
direct-page offset baked into that shared code **is** the same address doing the same job in
both games. The match is real rather than coincidental:

| SPC addr | Dream name (`spc/spc_map.txt`) | role | confirmed via |
|---|---|---|---|
| $E7 | `var_E7` | cmd-1 (`$F9`) target, no reader in traced code | `cmd1_set_E7`/DKC2 `CODE_0702`, identical bytes |
| $E8 | `var_E8` | cmd-0 (`$F8`) target, no reader in traced code | `cmd0_set_E8`/DKC2 `CODE_070A`, identical bytes |
| $E9 | `port_counter` | port0 handshake counter, shared by loader and driver | identical in `spc_loader`/DKC2 `CODE_04D8` and throughout |
| $1C/$1D | `play_flag`/`mono_flag` | driver state | `driver_init`/DKC2 `CODE_0678`, identical |
| $24+x/$34+x | `gate[x]`/`duration[x]` | per-slot sequencer state (16 slots) | `seq_step`/DKC2 `CODE_0813`, identical |
| $44+x/$54+x | `seq_ptr_lo[x]`/`seq_ptr_hi[x]` | per-slot sequence read pointer | `seq_fetch`/DKC2 `CODE_0850`(unshifted address inside `CODE_0813` block), identical |
| $64+x | `finetune[x]` | per-slot finetune | `seq_note`/DKC2 `CODE_0867`, identical |
| $74+x/$84+x | `pitch_hi[x]`/`pitch_lo[x]` | per-slot DSP pitch shadow | identical |
| $D4+x | `seq_sp[x]` | per-slot call-stack pointer | `seq_call`/DKC2 `CODE_0CE6`, identical |
| $0FC8+x | `voice_bits` table | 1<<voice lookup, twice (music/sfx) | DKC2 `DATA_0F95`, byte-identical |

Neither Yoshifanatic1's empty `ARAM_Map_DKC2.asm` nor p4plus2 (no SPC700 disassembly at all)
names any of these, so nothing is adopted from DKC2 here either. The match is still worth
recording: identical opcodes and identical operand bytes confirm that this is Rare's one
driver source shared across the family.

### 4.3 The one real adoption: OAM buffer addresses ($0200/$0400)

`docs/toolchain_evidence.md` §1.3 flags `clear_sprite_table` (`$C0A500`) as byte-identical in
DKC1, DKC2, BTBM and BTDD, "the same RAM address in five games," but doesn't name the
address. Dream's routine (`out/dream.asm:4628-4648`) zeroes `$0400-$041E` and then stores
`#$0200` into `$94` (currently misnamed `sprite_count` in `tools/names.txt`). Cross-checking
against Yoshifanatic1's `RAM_Map_DKC2.asm`:

    !RAM_DKC2_Global_OAMBuffer = $000200                         (RAM_Map_DKC2.asm:14)
    struct DKC2_Global_OAMBuffer !RAM_DKC2_Global_OAMBuffer      (RAM_Map_DKC2.asm:81-86)
        .XDisp / .YDisp / .Tile / .Prop, 4 bytes/sprite
    struct DKC2_Global_UpperOAMBuffer !RAM_DKC2_Global_OAMBuffer+$0200   (RAM_Map_DKC2.asm:88-90)
        (i.e. $000400) .Slot, 1 byte/sprite (SNES hardware "high OAM" table)

This is a justified match: DKC2's OAM buffer is at `$0200` (main table) with its 32-byte
hardware "high" table at `$0200+$0200=$0400`, exactly the range Dream's identical
`clear_sprite_table` zeroes (`$0400-$041E`, 30 of the 32 bytes touched by the unrolled `stz`
chain) and exactly the value (`#$0200`) the same routine stores as a live pointer. One more
correction, independent of DKC2 (my own reading of the code): **`$94` is not a count.**
`clear_sprite_table` stores the literal address `$0200` into it, and
`loc_C0A53D` later does `lda $94; cmp #$0400`: `$94` is a write cursor that walks the
$0200-$03FF OAM buffer as sprites are emitted, not a "sprite count." The existing name
`sprite_count` (`tools/names.txt:42`) should be corrected.

DKC2's `; $000094 = BG scroll related` (`RAM_Map_DKC2.asm:9`) is a different, unconfirmed
role at the coincidentally-same DKC2 address. I checked it and it is **not** a match
(different code, different apparent purpose), so I'm not using it to justify or contradict
the Dream finding; it's mentioned only so the discrepancy isn't silently dropped.

---

## 5. Paste-ready blocks

### 5.1 `tools/names.txt` additions/renames

```
; ---- bank $C1 sound interface: renamed from p4plus2's DKC2-disassembly bank_B5.asm
;      (https://github.com/p4plus2/DKC2-disassembly), which names and comments the
;      equivalent routines in DKC2 (see docs/dkc_crossref.md section 3.2) ----
C180C1 upload_global_samples   ; was sub_C180C1; = p4plus2 bank_B5.asm .upload_global_samples
C180FF write_spc_command       ; was sub_C180FF; = p4plus2 bank_B5.asm .write_spc_command (X = packed param:cmd word)
C18119 upload_song_data        ; was sub_C18119; = p4plus2 bank_B5.asm .upload_song_data (A = song number)
C1813D upload_song_sound_effects ; was sub_C1813D; = p4plus2 bank_B5.asm .upload_song_sound_effects (A = song number)
C1815F sample_uploader         ; was sub_C1815F; = p4plus2 bank_B5.asm .sample_uploader
C18392 upload_song_sample_set  ; was sub_C18392; = p4plus2 bank_B5.asm .upload_song_sample_set (A = song number)
C1830A upload_inline_spc_block ; was spc_upload_block; = p4plus2 bank_B5.asm .upload_inline_spc_block
C18324 upload_spc_block        ; was spc_send_words; = p4plus2 bank_B5.asm .upload_spc_block
C1803E execute_spc_sound_engine ; was sub_C1803E; = p4plus2 bank_B5.asm .execute_spc_sound_engine
data C1804C unused_spc_execute ; currently unlabelled; = p4plus2 bank_B5.asm .unused_spc_execute; dead code, would crash the SPC700 engine if reached (both games)

; ---- RAM: adopted from Yoshifanatic1's DKC2 RAM_Map_DKC2.asm
;      (https://github.com/Yoshifanatic1/Donkey-Kong-Country-2-Disassembly), justified by
;      the byte-identical clear_sprite_table shared with DKC1/DKC2/BTBM/BTDD
;      (docs/toolchain_evidence.md section 1.3) ----
ram 0200 oam_buffer            ; = RAM_Map_DKC2.asm RAM_DKC2_Global_OAMBuffer (128 x 4-byte sprite entries)
ram 0400 oam_buffer_upper      ; = RAM_Map_DKC2.asm RAM_DKC2_Global_UpperOAMBuffer (SNES hardware "high OAM" table)

; ---- RAM: independent correction (not adopted from a reference project; see
;      docs/dkc_crossref.md section 4.3 for the code reasoning) ----
ram 0094 oam_write_ptr         ; was sprite_count; holds a live pointer into oam_buffer ($0200), not a count; clear_sprite_table sets it to #$0200, loc_C0A53D compares it against #$0400
```

### 5.2 `spc/driver.asm` renames

None. Both DKC2 disassemblies checked (Yoshifanatic1's `SPC700_Engine_DKC2.asm`/
`InitializeSPC700.asm` and p4plus2's, which has no SPC700 coverage at all) leave the SPC700
engine entirely unlabelled (`CODE_xxxx`/`DATA_xxxx`, address-only) and the one RAM-name file
that could apply (`ARAM_Map_DKC2.asm`) is empty. Dream's own driver.asm names are already
more descriptive than either source, so nothing is proposed here:

```
; (no renames: see docs/dkc_crossref.md section 2 for why)
```

---

## Credits

Per `docs/LEGAL.md` rule 4: routine names in section 5.1 for `C180C1`, `C180FF`, `C18119`,
`C1813D`, `C1815F`, `C18392`, `C1830A`, `C18324`, `C1803E`, and the `C1804C` data label are
adopted from **p4plus2's `DKC2-disassembly`** (`bank_B5.asm`,
https://github.com/p4plus2/DKC2-disassembly). The `oam_buffer`/`oam_buffer_upper` RAM names
are adopted from **Yoshifanatic1's `Donkey-Kong-Country-2-Disassembly`** (`RAM_Map_DKC2.asm`,
https://github.com/Yoshifanatic1/Donkey-Kong-Country-2-Disassembly). All other names in
Dream remain as traced/curated in this project; the `oam_write_ptr` rename is this
project's own finding from reading `out/dream.asm`, not sourced from either reference
project.
