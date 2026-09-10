# Naming proposals for auto-named routines and RAM

Derived statically from `out/dream.asm`, `out/symbols.txt`, `docs/progress.json` (the 76
`"named": false` entries, of which 75 are 65816 code in banks `$C0`/`$C1` — `0BCC` is an
SPC700 routine and out of scope here), `docs/handler_tables.md`, `docs/dkc_crossref.md`,
`docs/data_formats.md` and `docs/NOTES.md`. Every routine below was read in full in
`out/dream.asm` (address ranges taken from `docs/progress.json` `size` fields). Confidence:

- **high** — behaviour is unambiguous from the hardware registers/addresses touched, or is
  already established by another doc file and just needs applying.
- **medium** — the mechanism is clear from the code but the game-level meaning (which
  effect, which level) is inferred rather than certain.
- **low** — the routine is dead/orphaned or its exact role is a guess from thin evidence.

## 1. Init / reset / per-mode level setup

Game mode dispatch (`game_mode_table` @ `$826A`): mode 0 = `sub_C08292`, mode 1 =
`sub_C084D7`, mode 2 = `sub_C08798`, mode 3 = `sub_C088AB`. Per `docs/data_formats.md`,
mode 3 is the title screen; modes 0-2 are levels. All four routines have the identical
shape: `stz $0BAC`(or `lda #$0018`) → `BGMODE`/`TM`/`CGWSEL`/`BG1SC`/`BG3SC`/`BG12NBA` →
seed `$7A/$7C/$7E/$80/$82/$86/$88/$98/$9A` (layer map pointers/parallax config, see RAM
section) → loop uploading metatile columns (`sub_C09FB7`+`sub_C0A0F1`, or
`sub_C08798`'s variant using bank `$C9` metatile defs) → DMA tilesets/palettes/HDMA tables
via `sub_C0A46A`/`sub_C0A483`/`sub_C0A445`/`sub_C0848C`.

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `sub_C08292` | `mode0_level_init` | medium | `game_mode_table[0]`; sets `BGMODE=1,TM=$1417,CGWSEL=$8202`, VRAM/CGRAM uploads via `sub_C0A46A`/`sub_C0A483`/`sub_C0A445`, HDMA table build at `$7F00D0-D9`/`$7F0540-49` |
| `sub_C0848C` | `dma_setup_channel_step` | medium | called repeatedly by all 4 mode-init routines with `X`=DMA-channel byte offset, `Y`=src bank\:addr, `ptr_04`=dest; writes `A1TL0,X`/`DMAP0,X`/`A1B0,X`/`DASB0,X` — one HDMA/DMA channel descriptor per call |
| `sub_C084D7` | `mode1_level_init` | medium | `game_mode_table[1]`; sets `$0BAC=$0018` (the mode-1-only row offset documented in `docs/handler_tables.md`), `CGWSEL=$2202`, distinct BG1SC/palette (`COLDATA` x3) |
| `sub_C08798` | `mode2_level_init` | high | `game_mode_table[2]`; sets `$7E/$80=$B560/$C9` — exactly the bank/pointer `docs/data_formats.md` cites as the game-mode-2 metatile table read by `sub_C09FB7`/`sub_C09E83` |
| `sub_C088AB` | `title_screen_init` | medium | `game_mode_table[3]`; mode 3 = title screen per `docs/data_formats.md`; `BGMODE=9` (mode 3 is otherwise BGMODE 1 — matches the title's documented BG-mode-3 tile format) |
| `sub_C0A4A6` | `set_bg_scroll_prep` | medium | 2-byte stub (`sep #$20`) that falls through directly into `set_bg_scroll`; called once from `mode0_level_init` before the scroll write |
| `sub_C09234` | `vram_upload_shared_tileset_c5` | medium | identical DMA parameters (`bank $C5, off $02C0, 0x0C00 words → VMADDL 0`) to the inline copy inside `mode0_level_init`; a reusable helper for the other mode-inits |

## 2. NMI handlers and per-mode scroll setters

`docs/NOTES.md` documents that NMI handlers are installed at runtime via `lda #$addr ;
jmp $A4E9` (`loc_C0A4E9`, `sta nmi_handler_ptr`). Tracing the two install sites pins down
both handlers precisely:

- `$80F4` is installed at file `0x80EE` (`lda #$80F4`), in the generic post-reset path
  before falling into the main loop — used for gameplay (modes 0-2, and by default 3).
- `$BD20` is installed at file `0xBD1A`, inside the title-screen VRAM-upload block that
  writes `data_C6002B` (the "627 raw 8bpp tiles at 0x06002B" from `docs/data_formats.md`).
  The handler body itself compares live CGRAM (`$7F0F91,X`) against `data_C6A36B`/`data_C6A36C`
  (the title's "256-colour palette at 0x06A36B") one byte per NMI — a palette fade-in.

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `nmi_handler_80F4` | `nmi_handler_gameplay` | high | installed unconditionally after `reset`/mode-switch (file 0x80EE `lda #$80F4`); dispatches `jtbl_C08272,X` (per-mode scroll) then `sub_C0AE7E` (tile DMA) |
| `nmi_handler_BD20` | `nmi_handler_title_fade` | high | installed only inside the title tile-upload block (file 0xBD1A); body walks `$7F0F91-$7F0F92,X` vs `data_C6A36B`/`data_C6A36C` (title palette), one step per call — a CGRAM fade-in |
| `sub_C08E9B` | `nmi_scroll_mode0` | high | `jtbl_C08272[0]`, called from `nmi_handler_gameplay`; writes `BG1HOFS/VOFS`, `BG2HOFS/VOFS` from `$62`/`$68`/`$0BE4-$0BE6` |
| `sub_C08F47` | `nmi_scroll_mode1` | high | `jtbl_C08272[1]`; same register set, mode-1-specific parallax math (`data_C46588` lookup, `$7F0087` etc.) |
| `sub_C09049` | `nmi_scroll_mode2` | high | `jtbl_C08272[2]`; writes `BG2HOFS/VOFS`+`BG3HOFS/VOFS` from `$62`/`$68`/`$5E` |
| `sub_C090FA` | `nmi_scroll_title` | high | `jtbl_C08272[3]` = mode 3 = title screen; same register shape as the other three, seeds `$0BE0-$0BEE` layer offsets used by the title's HDMA-driven layers |

## 3. PPU / VRAM / OAM DMA helpers

Three small, heavily-reused DMA wrappers (`sub_C0A445`, `sub_C0A46A`, `sub_C0A483`) are
called from every mode-init routine with different `A`/`X`/`Y` arguments — they are the
"upload one block" primitives the mode-inits compose into full scene setup, in the same
spirit as `docs/dkc_crossref.md`'s already-adopted DMA helpers in bank `$C1`.

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `sub_C0A445` | `dma_fill_vram_zero` | high | fixed source `data_C0A443` (a zero word, per `docs/data_formats.md`'s "zero word (VRAM fill source)"), `DMAP0=$1809` (fixed-source, word count `$0800`) — VRAM-clear helper |
| `sub_C0A46A` | `dma_upload_to_vram` | high | `A`=src addr, `X`=src bank, `Y`=word count, dest already in `VMADDL`; `DMAP0=$1801` (word, incrementing) — generic ROM→VRAM DMA, called ~20x across mode-inits |
| `sub_C0A483` | `dma_upload_to_cgram` | high | fixed src bank `$C4`, `X`=count word (`*8`→`DASL0`), `Y`=`CGADD`; `DMAP0=$2200` (byte, CGDATA) — palette upload helper |
| `sub_C09FB7` | `build_metatile_column_580` | high | `docs/data_formats.md`'s metatile blitter: reads `[$18]` (16-bit metatile index, base `$7A/$7C` + `$62`), applies `eor #$4000/#$8000` flips, writes 32-byte metatile rows into the `$0580` buffer that `vram_upload_column_580` DMAs out |
| `sub_C09E83` | `build_metatile_column_500` | high | same mechanism as `build_metatile_column_580` but keyed off `$82` (layer-2 parallax) and writing the `$0500` buffer that `vram_upload_column_500` DMAs out |
| `sub_C0A0F1` | `vram_upload_column_580` | high | sets `VMAIN=$81`, DMAs the `$0580` buffer (built by `build_metatile_column_580`) to a `VMADDL` computed from `$62`/`$98`; matches `docs/data_formats.md`'s metatile-blitter description |
| `sub_C0A148` | `vram_upload_column_500` | high | same shape, DMAs the `$0500` buffer (built by `build_metatile_column_500`) using `$68`/`$9A` |
| `sub_C09C16` | `vram_generate_particle_tile` | medium | writes a 16-row expanded 4bpp tile to `VMDATAL` at address `A` by bit-interleaving `A`/`ptr_04` through `sub_C09C28`; called from `particle_spawn_from_table` and `particle_spawn_random` with `A=$1F00` — builds a solid-fill tile for the particle sprites |
| `sub_C09C28` | `vram_write_tile_row_planes` | medium | inner helper for `vram_generate_particle_tile`: writes one bitplane row pair to `VMDATAL` then pads 7 zero words, looped 16x by the caller |
| `sub_C09C62` | `vram_stream_descriptor_dispatch` | medium | reads `data_C0B24C` ("per-mode descriptor index", `docs/data_formats.md`) keyed by camera position; on change, loads the matching `data_C0B208`-family streaming descriptor (`docs/data_formats.md`'s "VRAM/CGRAM streaming descriptors") and either starts a new stream or queues it for `cgram_upload_queue_flush`; only caller is `mode0_weather_zone_update` |
| `sub_C0A538` | `entity_build_oam_frame` | high | reads `$07C8,X` (sprite frame id) → `data_C40000`/`data_C40002` (the documented `{ptr16,bank,y-bias}` frame table), computes on-screen X/Y from `$08A8,Y`/`$08E8,Y` minus camera, culls off-screen, dispatches to the row-count-specific OAM emitters below |
| `sub_C0A757` | `oam_emit_frame_1row` | medium | one of `sub_C0A538`'s size-class dispatch targets (`cpx #$0004` branch); builds `[$26]`/`[$2A]` pointers then falls into the shared emit loop for the smallest frames |
| `sub_C0A772` | `oam_emit_frame_2row` | medium | same dispatch family, sets up 4 pointer fields (`$1C/$1E/$20/$22`) before the OAM-record loop — larger frame class than `oam_emit_frame_1row` |
| `sub_C0A8F6` | `oam_emit_frame_1row_flip` | medium | structurally identical to `oam_emit_frame_1row`, reached via the `$4C`≥`$8F` (h-flip) branch in `sub_C0A538` |
| `sub_C0A911` | `oam_emit_frame_2row_flip` | medium | structurally identical to `oam_emit_frame_2row`, h-flip branch counterpart |
| `sub_C0AAAA` | `oam_emit_frame_3row` | medium | reached from the `bit #$8000`/`#$4000` (v-flip) branches in `sub_C0A538`; tail falls into `loc_C0A6CD` (the documented shared `pea $8080;plb;plb;rtl` library stub) |
| `sub_C0AC40` | `oam_emit_frame_3row_flip` | medium | same as `oam_emit_frame_3row` but the opposite flip combination |
| `sub_C0ADE7` | `oam_hide_unused_sprites` | high | from `oam_write_ptr` (`$94`) to `$0400`, stores `#$F0FF` (Y=`$F0`, off-screen) — classic "hide the rest of OAM" loop |
| `sub_C0ADFD` | `oam_dma_upload` | high | DMAs `$0200`-`$041F` (`oam_buffer`+`oam_buffer_upper`) to `OAMDATA` (`DMAP0=$0400`), then sets `$02` (a DMA-pending bitmask, see RAM) |
| `sub_C0AE7E` | `entity_upload_pending_tiles` | high | walks a per-entity 8-byte descriptor array at `$0A8A` (size/dest/src/bank+pending-flag), DMAs each pending one to VRAM — matches `docs/data_formats.md`'s "`sub_C0AE7E` DMA[s]" note for sprite tile uploads |
| `sub_C0AE1F` | `entity_sort_draw_order` | medium | insertion-sorts the 16-entry `$09A8` index array by `$0988,Y` (a computed depth/Y key), tie-broken by `$07A8,Y` |
| `sub_C0AEB9` | `entity_render_order_reset` | medium | fills `$09A8,X = X` — resets the sorted draw-order array to identity before `entity_sort_draw_order` runs |
| `sub_C09D32` | `cgram_upload_queue_flush` | medium | loop over `$0B8A` (count) DMAing `{$0B84 len,$0B88 addr,$0B86 CGADD}` records — a queued-palette-write flush, consumed by the weather/effect code in section 6 |

## 4. Entity update / AI dispatch

`sub_C098DA` is the entity tick (called 16x per frame from the main loop, `X`=entity slot):
dispatch AI via `jtbl_C099DD,X` (indexed directly by entity type `$0708,X`, values 0,2,4,
…,16 → 9 table entries), decrement hitstun (`$0768,X`), run physics (`entity_accelerate_
velocity_x`), pick facing/state/anim id, dispatch to the animation-rate handler already
named in `docs/handler_tables.md`.

`jtbl_C099DD` entries (type value → handler):

| type | handler | proposed name | conf. |
|---|---|---|---|
| 0 | `sub_C09AF6` | `entity_animate_only` | high — `tyx;jsl anim_update;pla;rts`, no AI; likely the player (moved by input elsewhere) |
| 2 | `sub_C09A61` | `entity_apply_hit_reaction` | medium — sets a "hurt" sub-state from facing bits in `A`; entry point `loc_C09A66` is also jumped into directly by `sub_C08E8E` |
| 4 | `sub_C09AB8` | `entity_spawn_transform_a` | medium — copies parent (`$0968,X`→`Y`) position/flags into `X`, advances anim id by 2 |
| 6, 8 | `sub_C09B00` | `entity_spawn_transform_b` | medium — 3-way branch on `game_mode`/`$0BAC`: normal copy (+`$0888,X-Y`) offsets `$08E8` by `0x10`; a mode-1-only branch (`loc_C09B51`) instead decrements `$08A8` and forces anim `$0158` |
| 10 (`0x0A`) | `sub_C09B89` | `entity_spawn_transform_c` | medium — parent copy; if `$0BB6` (mode-1 row index) is set, also arms a knockback sub-state (`$07A8,X=2`) |
| 12 (`0x0C`) | `sub_C099EF` | `entity_ai_chase_player` | medium — only acts on type `$0C`; computes `abs($0828-$0828,X)` (distance to player), sets facing/state thresholds at 0x40/0x80px, arms a random delay via `sub_C0A212` (RNG) |
| 14, 16 | `sub_C09A5F` | `entity_ai_none` | high — `tyx;rts`, literal no-op; these types are animated purely by the velocity-to-rate table (`docs/handler_tables.md`) with no think logic |

Other entity-system routines:

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `sub_C098DA` | `entity_update_tick` | high | the master per-entity update described above; called from the main loop's `X=4..$A6 step 2` entity loop |
| `sub_C0A232` | `entity_accelerate_velocity_x` | high | if `$0888,X` (target velocity) is 0, decays `$0868,X` toward 0 near the terminal band; else moves `$0868,X` 1/8 of the way toward `$0888,X` each call — generic accel-toward-target used for both gravity and friction |
| `sub_C0A26F` | `entity_apply_velocity_x` | high | classic 16.8 fixed-point integrator: adds the fractional byte of `$0868,X` (velocity) into `$0848,X` (sub-pixel accumulator), carries the sign-extended integer velocity into `$0828,X` (position) |
| `sub_C0A2B9` | `entity_apply_velocity_y` | high | identical pattern for the Y axis: velocity `$0948,X`, sub-pixel accumulator `$08C8,X`, position `$08A8,X` (the same `$08A8` that `entity camera-Y-follow` code (`sub_C0A1B0`) reads for the player) |
| `orphan_C0A294` | `entity_apply_velocity_z_dead` | medium | identical pattern for a third axis (velocity `$0928,X`, accumulator `$0908,X`, position `$08E8,X`) but unreferenced by any caller — one of the four dead fragments `docs/NOTES.md` open item 1 lists |
| `sub_C0A1F3` | `entity_derive_bounce_velocity` | low | scales `abs($0868,X)` (X velocity) by `>>2`, applies a sign from `$0BAE`, stores to `$0948,X` (Y velocity) — reads as "convert horizontal speed into a vertical component" but the exact game use (recoil? bounce?) isn't confirmed |
| `sub_C09BDA` | `entity_ground_y_lookup` | medium | looks up `data_C0B2F6`/`C0B2F8`/`C0B2FA` (the documented per-mode "spawn x/y list" / "per-state Y table") by `abs($0828,X)`, writing a resolved Y-ish value to `$08A8,X`; called every tick from `entity_update_tick`, not just at spawn |
| `sub_C08E8E` | `check_pending_player_attack` | medium | `ldx $0BB4; beq rts` else jumps into `entity_apply_hit_reaction` with `A=$8E,Y=$90` — applies a queued hit to entity `$0BB4` |
| `sub_C0A1B0` | `camera_follow_player` | high | clamps player `$0828`/`$08A8` against `$86`/`$88` (level bounds), derives `$62`/`$68` (camera X/Y) and the look-ahead values `$98`(unused here)/`$9A`; skipped for mode 1 |
| `sub_C0A212` | `random_next` | high | classic 16-bit xorshift/LFSR-style PRNG over `init_magic_AA55`/`init_magic_FFFF`; called 10x from entity AI, particle spawn and animation-rate code |
| `sub_C09D6A` | `entity_init_from_table` | high | walks `data_C0B4A4` ("per-mode entity offsets"/"entity init records (18 bytes)", `docs/data_formats.md`), populating every entity SoA field (`entity_type` through `entity_anim_rate`) for each initial entity of the current `game_mode`, incrementing the active-entity count (`$A6`) |

## 5. Particle / weather effect subsystem (mode 0/1 background effects)

A cluster of routines maintains up to ~40 "particle" records in a WRAM mirror region
(`$7F0900`-`$7F0Fxx`, distinct from the entity arrays at `$0700`-`$0A80`), driven by
`random_next`, culled against the camera (`$62`/`$68`) and written into the OAM buffer via
`oam_write_ptr` (`$94`). The exact visual (rain? sparks? fireflies?) is not determinable
statically — hence medium confidence throughout — but the mechanism (spawn, integrate,
cull, emit sprite) is unambiguous.

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `sub_C09246` | `particle_table_clear` | medium | zeroes the `$7F0906,X` "active" field for all 40 slots |
| `sub_C09253` | `particle_spawn_mode0_weather` | medium | seeds inactive slots with `random_next`-derived position/velocity relative to `$0C17`/`$0C15` (the weather-zone state `mode0_weather_zone_update` maintains) |
| `sub_C092F3` | `mode1_reset_particles_and_oam` | medium | zeroes camera vars, resets the same `$0C1F-$0C2D` weather-parameter block as `mode0_weather_zone_update`, then `clear_sprite_table`+`particle_update_and_draw_mode0`+`oam_hide_unused_sprites`+`oam_dma_upload` |
| `sub_C09331` | `particle_update_and_draw_mode0` | medium | integrates `$7F09xx-$7F0Exx` particle fields and, when on-screen, writes 8-byte OAM records via `Y`=`oam_write_ptr` |
| `sub_C09227` | `mode0_particle_draw_dispatch` | medium | `jtbl_C0828A[0]`; `jmp particle_update_and_draw_mode0` |
| `sub_C094E4` | `sparkle_array_init` | medium | called from `mode2_level_init`; seeds a 39-entry `$7F0E86-$7F0E8A` array with `random_next` values — a second, smaller particle set |
| `sub_C09521` | `sparkle_update_and_draw` | medium | integrates/culls/draws the `$7F0E86-$7F0E8A` array into OAM, same shape as `particle_update_and_draw_mode0` |
| `sub_C0922A` | `mode1_particle_dispatch` | medium | `jtbl_C08282[1]`; calls `sparkle_update_and_draw` then `loc_C09679` (an OAM-count-based particle culler) |
| `sub_C09230` | `mode2_particle_dispatch` | medium | `jtbl_C08282[2]`; jumps to `loc_C097DD`, a third variant of the same integrate/cull/draw loop |
| `sub_C09233` | `particle_dispatch_noop` | high | plain `rts`; the do-nothing entry shared by the game modes/slots with no particle effect |
| `sub_C095E3` | `particle_spawn_from_table` | medium | seeds particle fields from `data_C0B26C` (the documented per-mode "entity spawn x/y list") combined with `random_next` — fixed-position particles (unlike the fully random spawns below) |
| `sub_C09781` | `particle_spawn_random` | medium | seeds 40 particle slots with fully `random_next`-derived position/velocity, no spawn table |
| `sub_C08A74` | `mode0_weather_zone_update` | low | large camera-X-driven state machine selecting among several `$0BD8`/`$0BDA`/`$0BF0-$0BFE` HDMA-colour parameter sets and `$0C00-$0C2D` fields per screen region — reads as a per-zone lighting/weather effect but the visual isn't determinable statically |
| `sub_C0B075` | `play_footstep_sound` | high | toggles between sfx `$050A`/`$0710` and `$0712`/`$050B` based on `$72` (parity flag); called from a 60-frame (`$3C`) walk-cycle counter in the main loop |
| `sub_C0B0BC` | `play_zone_transition_sound` | medium | plays sfx `$050F` then `$0611` unconditionally; called once from inside `mode0_weather_zone_update` at a specific camera-X boundary crossing |
| `sub_C091BB` | `spawn_screen_flash_effect` | low | arms a one-shot entry in the `$0B90-$0B92` effect-queue array (consumed by `cgram_upload_queue_flush`) when `$0C1B` is set; called from `nmi_handler_gameplay`'s tail |

## 6. Sound interface (bank `$C1`)

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `sub_C1815F` | `sample_uploader` | high | already identified in `docs/dkc_crossref.md` §3.2 as p4plus2's DKC2 `.sample_uploader` (byte-identical sound-interface file); just needs the `names.txt` entry added |
| `sub_C18415` | `sfx_command_dispatch` | medium | `tax;jsr write_spc_command;rtl`; the only caller is `play_sound_effect` (`sub_C0B0C5`, `docs/handler_tables.md`), packing `X`=channel:sfx-id |
| `orphan_C183F1` | `spc_stop_sequence_dead` | low | sends command `$F9` then `$FE`; `docs/dkc_crossref.md` §3.2 already notes this shape has no live caller and its structural sibling is dead in DKC2 too |

## 7. Entity record layout

Entities are a struct-of-arrays: 16 word-sized slots (`X` = slot index × 2, so `X` runs
0..30), each field a flat 32-byte (`0x20`) array, fields laid out contiguously from
`$0708` to at least `$0A68`. Columns confirmed by direct evidence:

| offset | name | conf. | meaning |
|---|---|---|---|
| `$0708` | `entity_type` | high | already used as index into `jtbl_C099DD` and the anim-rate tables (`docs/handler_tables.md`) |
| `$0728` | `entity_state` | high | already documented (`docs/handler_tables.md`) as the state/facing word |
| `$0748` | `entity_attack_timer` | low | set to a `random_next`-derived delay by `entity_ai_chase_player`; read/decremented elsewhere, not fully traced |
| `$0768` | `entity_hitstun_timer` | high | decremented once per tick in `entity_update_tick` |
| `$0788` | `entity_flags` | high | facing/orientation + collision bits (`docs/handler_tables.md`'s `facing_flag_table` writes bit 14; masks `$4000/$7000/$C000` seen in the spawn-transform handlers) |
| `$07A8` | `entity_substate` | medium | values 0/2/4 checked by `entity_sort_draw_order` and set by the spawn-transform handlers (knockback/priority?) |
| `$07C8` | `entity_frame_id` | high | sprite frame id, confirmed by `docs/data_formats.md` (`sub_C0A538`) |
| `$0828` | `entity_x` | high | world X position; the no-index form (`$0828`) is the player, read by `camera_follow_player` |
| `$0848` | `entity_x_sub` | high | sub-pixel accumulator for `entity_x` (see `entity_apply_velocity_x`) |
| `$0868` | `entity_vel_x` | high | named directly in this task's own instructions; velocity feeding both physics and the animation-rate table |
| `$0888` | `entity_vel_x_target` | high | target velocity `entity_accelerate_velocity_x` eases `entity_vel_x` toward |
| `$08A8` | `entity_y` | high | world Y position; no-index form read by `camera_follow_player` |
| `$08C8` | `entity_y_sub` | high | sub-pixel accumulator for `entity_y` |
| `$08E8` | `entity_z_dead` | medium | paired with the unreferenced `entity_apply_velocity_z_dead`; still read by `entity_build_oam_frame` (added to `entity_y` for screen position) and copied by the spawn-transform handlers, so not fully dead data even though its integrator is |
| `$0908` | `entity_z_sub_dead` | medium | sub-pixel accumulator for `entity_z_dead` (dead integrator) |
| `$0928` | `entity_vel_z_dead` | medium | velocity for `entity_z_dead` (dead integrator) |
| `$0948` | `entity_vel_y` | high | velocity feeding `entity_apply_velocity_y`; also the target of `entity_derive_bounce_velocity` |
| `$0968` | `entity_parent_index` | high | read as `Y` by every spawn-transform handler to copy another entity's fields |
| `$0988` | `entity_depth_key` | high | `entity_y` + the frame's y-bias byte (from the sprite-frame header); the sort key for `entity_sort_draw_order` |
| `$09A8` | `entity_render_order` | high | array of entity indices sorted by `entity_depth_key`; walked by `entity_build_oam_frame` |
| `$09E8` | `entity_anim_id` | high | already implied by `docs/handler_tables.md` (`anim_script_table` index) |
| `$0A68` | `entity_anim_rate` | high | named directly in this task's own instructions; consumed by `anim_update` |

Fields `$07E8`, `$0808`, `$09C8`, `$0A08`, `$0A28`, `$0A48` are read/written in the
entity code but not characterised with confidence here (each appears in fewer than 3 of
the read routines with an unambiguous role) — left for future work.

## 8. RAM (direct page / low WRAM) used in 3+ routines

| addr | proposed name | conf. | justification |
|---|---|---|---|
| `0062` | `camera_x` | high | world-space camera X; read by every scroll/metatile/entity-draw routine, written by `camera_follow_player` |
| `0068` | `camera_y` | high | world-space camera Y, same evidence |
| `0002` | `dma_pending_mask` | medium | accumulated with `ora #$xx00` across the scroll setters, consumed by `sta MDMAEN` then `stz $02`; also set by `oam_dma_upload` |
| `0096` | `entity_render_index` | high | iterator into `entity_render_order`, walked 0..30 by `entity_build_oam_frame` |
| `004C` / `004E` | `entity_screen_x` / `entity_screen_y` | high | computed on-screen position inside `entity_build_oam_frame`, consumed by all six `oam_emit_frame_*` variants |
| `0026`/`0028` | `sprite_frame_ptr` / `sprite_frame_bank` | medium | 24-bit pointer into the sprite-frame data, built in `entity_build_oam_frame` from `data_C40000`/`data_C40002` |
| `002A`/`002C` | `sprite_frame_ptr2` / `sprite_frame_bank2` | medium | `sprite_frame_ptr`+1 (byte-shifted duplicate), used by the `oam_emit_frame_*` routines' `[$2A],Y` OAM-record reads |
| `0054` | `oam_entry_ptr` | medium | 24-bit pointer built from `oam_write_ptr`, used for `[$54]` attribute-byte read-modify-write in the `oam_emit_frame_*` routines |
| `007A`/`007C` | `tilemap_a_addr` / `tilemap_a_bank` | medium | per-mode map-data pointer, set uniquely by each `modeN_level_init`, consumed by `sub_C09FB7`'s `[$18]` column read |
| `007E` | `tilemap_b_addr` | medium | second map-data base, shared by both `$1C` and `$1E` row offsets inside the column composer |
| `0080` | `metatile_data_bank` | medium | bank byte loaded via `pha;plb` before the `[$18]`/metatile-table indirect-long reads |
| `0086`/`0088` | `level_width_mask` / `level_height_mask` | medium | per-mode clamp bounds for `entity_x`/`entity_y` in `camera_follow_player` (e.g. `#$0DFF`/`#$011F` for mode 0) |
| `0098` | `layer_parallax_mode` | medium | per-mode constant (`#$FFFF` or `0`) selecting the sign/shift path in `vram_upload_column_580`/`sub_C09FB7` |
| `009A` | `camera_y_lookahead` | medium | computed per-frame in `camera_follow_player` from `entity_y`, `$88`, `$68`, `$74`; consumed by `vram_upload_column_500`/`sub_C09E83` |
| `0070`/`0072` | `walk_cycle_timer` / `walk_cycle_parity` | medium | `$70` is a decrementing counter compared to `#$3C` gating `play_footstep_sound`; `$72` toggles which of the two sfx pairs plays |
| `0074` | `camera_y_lookahead_const` | low | per-mode constant added into `camera_y_lookahead`; distinct value per `modeN_level_init` |
| `0076` | `bg2_scroll_bias` | low | added to `ptr_04` as a BG2 offset baseline in `nmi_scroll_mode0` |
| `0078` | `weather_zone_value` | low | computed by `mode0_weather_zone_update`, consumed the same frame for HDMA colour parameters |

Direct-page registers `$0018/$001A/$001C/$001E/$0020/$0022/$0024` are reused as generic
24-bit-pointer/arithmetic scratch by more than a dozen unrelated routines (metatile
column composer, OAM emitters, physics helpers) with no single fixed role — not proposed
for renaming beyond the existing informal `$18`/`$1A` scratch convention already visible
in the listing.

## 9. Paste-ready block (high + medium confidence)

```
; ---- game-mode init (docs/naming_proposals.md section 1) ----
C08292 mode0_level_init
C0848C dma_setup_channel_step
C084D7 mode1_level_init
C08798 mode2_level_init
C088AB title_screen_init
C0A4A6 set_bg_scroll_prep
C09234 vram_upload_shared_tileset_c5

; ---- NMI handlers + per-mode scroll (section 2) ----
C080F4 nmi_handler_gameplay
C0BD20 nmi_handler_title_fade
C08E9B nmi_scroll_mode0
C08F47 nmi_scroll_mode1
C09049 nmi_scroll_mode2
C090FA nmi_scroll_title

; ---- PPU/VRAM/OAM DMA helpers (section 3) ----
C0A445 dma_fill_vram_zero
C0A46A dma_upload_to_vram
C0A483 dma_upload_to_cgram
C09FB7 build_metatile_column_580
C09E83 build_metatile_column_500
C0A0F1 vram_upload_column_580
C0A148 vram_upload_column_500
C0A538 entity_build_oam_frame
C09C16 vram_generate_particle_tile
C09C28 vram_write_tile_row_planes
C09C62 vram_stream_descriptor_dispatch
C0A757 oam_emit_frame_1row
C0A772 oam_emit_frame_2row
C0A8F6 oam_emit_frame_1row_flip
C0A911 oam_emit_frame_2row_flip
C0AAAA oam_emit_frame_3row
C0AC40 oam_emit_frame_3row_flip
C0ADE7 oam_hide_unused_sprites
C0ADFD oam_dma_upload
C0AE7E entity_upload_pending_tiles
C0AE1F entity_sort_draw_order
C0AEB9 entity_render_order_reset
C09D32 cgram_upload_queue_flush

; ---- entity update / AI dispatch (section 4) ----
C09AF6 entity_animate_only m0x0
C09A61 entity_apply_hit_reaction m0x0
C09AB8 entity_spawn_transform_a m0x0
C09B00 entity_spawn_transform_b m0x0
C09B89 entity_spawn_transform_c m0x0
C099EF entity_ai_chase_player m0x0
C09A5F entity_ai_none m0x0
C098DA entity_update_tick
C0A232 entity_accelerate_velocity_x
C0A26F entity_apply_velocity_x
C0A2B9 entity_apply_velocity_y
C09BDA entity_ground_y_lookup
C08E8E check_pending_player_attack
C0A1B0 camera_follow_player
C0A212 random_next
C09D6A entity_init_from_table

; ---- particle/weather subsystem (section 5) ----
C09246 particle_table_clear
C09253 particle_spawn_mode0_weather
C092F3 mode1_reset_particles_and_oam
C09331 particle_update_and_draw_mode0
C09227 mode0_particle_draw_dispatch
C094E4 sparkle_array_init
C09521 sparkle_update_and_draw
C0922A mode1_particle_dispatch
C09230 mode2_particle_dispatch
C09233 particle_dispatch_noop
C095E3 particle_spawn_from_table
C09781 particle_spawn_random
C0B075 play_footstep_sound
C0B0BC play_zone_transition_sound

; ---- sound interface (section 6) ----
C1815F sample_uploader
C18415 sfx_command_dispatch

; ---- RAM (section 7 entity fields + section 8 DP scratch) ----
ram 0708 entity_type
ram 0728 entity_state
ram 0768 entity_hitstun_timer
ram 0788 entity_flags
ram 07C8 entity_frame_id
ram 0828 entity_x
ram 0848 entity_x_sub
ram 0868 entity_vel_x
ram 0888 entity_vel_x_target
ram 08A8 entity_y
ram 08C8 entity_y_sub
ram 0948 entity_vel_y
ram 0968 entity_parent_index
ram 0988 entity_depth_key
ram 09A8 entity_render_order
ram 09E8 entity_anim_id
ram 0A68 entity_anim_rate
ram 0062 camera_x
ram 0068 camera_y
ram 0002 dma_pending_mask
ram 0096 entity_render_index
ram 004C entity_screen_x
ram 004E entity_screen_y
ram 0026 sprite_frame_ptr
ram 0028 sprite_frame_bank
ram 002A sprite_frame_ptr2
ram 002C sprite_frame_bank2
ram 0054 oam_entry_ptr
ram 007A tilemap_a_addr
ram 007C tilemap_a_bank
ram 007E tilemap_b_addr
ram 0080 metatile_data_bank
ram 0086 level_width_mask
ram 0088 level_height_mask
ram 0098 layer_parallax_mode
ram 009A camera_y_lookahead
ram 0070 walk_cycle_timer
ram 0072 walk_cycle_parity
```

Note: `orphan_C0A294` (`entity_apply_velocity_z_dead`) is intentionally omitted from this
block — it is unreferenced (dead) code, so per the convention `docs/handler_tables.md`
uses for the other unreferenced fragments (`C099C4`, `C0B1FE`) it is listed only in the
low-confidence block below (seedable with `m0x0` if the maintainer wants it traced anyway).

## 10. Low-confidence block (not for direct merge)

Resolved (2026-09-10): the routines below are now named in `tools/names.txt`, superseding
this block's guesses where the two disagree.

- `C09206` -> `unused_stream_desc_dispatch` (this proposal's "do not seed" note is overridden
  by the 100%-naming pass; the dead-dispatcher behaviour is unchanged, only the label is new).
- `orphan_C0A294` -> `unused_entity_apply_velocity_z` (same routine as this block's
  `entity_apply_velocity_z_dead` guess, renamed to the `unused_*` convention).
- `orphan_C0A35C` -> `unused_wram_clear_full` (same routine as `wram_clear_alt`; confirmed by
  reading `reset`'s own WRAM-clear sequence in out/dream.asm rather than by the sample-directory
  cross-reference this block guessed at).
- `orphan_C183F1` -> `unused_spc_set_e7_and_play`, **not** "stop_sequence": tracing the actual
  bytes (out/dream.asm:7607-7614) shows it builds command `$F9` and calls `write_spc_command`
  twice ($F9 then $FE=play), never $FF (`cmd7_stop_to_loader`); `$F9 & 7 = 1` is
  `cmd1_set_E7` in spc/driver.asm's `NAMES` table, so this dead routine would set $E7 and play,
  not stop. This block's guess was wrong; verify against out/dream.asm and spc/driver.asm.NAMES
  before reusing an old proposal.
- `C0A1F3` -> `entity_vel_y_from_vel_x` (same routine as `entity_derive_bounce_velocity`;
  renamed for a more literal description of the arithmetic: derives entity_vel_y from
  entity_vel_x via a sign-preserving /4, called once from entity_update_tick).

```
; documented, but left unnamed/uncertain — behaviour understood, game-level meaning is not
C08A74 mode0_weather_zone_update m0x0
C091BB spawn_screen_flash_effect m0x0
ram 0748 entity_attack_timer
ram 07A8 entity_substate
ram 08E8 entity_z_dead
ram 0908 entity_z_sub_dead
ram 0928 entity_vel_z_dead
ram 0074 camera_y_lookahead_const
ram 0076 bg2_scroll_bias
ram 0078 weather_zone_value
```
