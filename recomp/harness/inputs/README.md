# Coverage input scripts

These scripts were built with `recomp/harness/explore.py`, a search tool that runs
`dream_harness` against candidate button scripts, traces executed PCs, and greedily
keeps whichever scripts add previously-uncovered static routines (as reported by
`compare_coverage.py` against `out/codemap.txt`/`out/symbols.txt`, 123 routines total).

    make harness
    python3 recomp/harness/explore.py

## The mode-cycle debug feature

The search's first useful finding: `game_mode` ($A4) is not just set by level
transitions -- pressing **Select** while no screen fade is in progress (WRAM `$30`
and `$32` both zero) advances it by one, wrapping `3` back to `0`. This is visible in
`nmi_handler_gameplay` (`out/dream.asm` around `$081F5`-`$08226`): it reads the
joypad-newly-pressed/held masks, checks bit `$2000` (Select), and if the fade
counters are idle it kicks off a fade (`$32 = -256`) whose completion (back at
`$081CE`) increments and wraps `game_mode` then jumps back to the mode's own setup.
That is the "mode/level select" feature the task description hinted at, and it is
what makes `game_mode` 1 and 2 (never reached by `title_start_right.txt` alone)
reachable at all without knowing where an in-level trigger for them is.

Two timing facts, found by binary search with `--dump-wram` against `$A4`:

- The very first Select has to be pressed at least ~114 frames after entering a
  mode (mode 0's own entry fade needs to settle first); `title_start_right.txt`
  already presses Start at frame 120 and releases at 126, so frame 240 is the
  earliest reliable point.
- Two Select presses closer than ~120 frames apart: the second is silently
  dropped (the fade from the first has not finished). Closer spacing makes a
  cycling script *skip* a mode instead of visiting it -- this bit twice during
  the search (see `recomp/harness/explore.py`'s `mode_prefix` docstring).

## Scripts

| script | frames | game_mode reached | routines hit | notes |
|---|---|---|---|---|
| `title_start_right.txt` (pre-existing) | 600 | 0 | 74/123 | leaves the title via Start, then holds Right. The worked-example script from `recomp/README.md`. |
| `title_attract_then_start.txt` | 2900 | 0 | 75/123 | idles at the title past the attract-mode threshold (~frame 2365) before pressing Start, instead of immediately. |
| `mode_cycle.txt` | 900 | 0, 1, 2, 3, 0 | 93/123 | Start, then Select every 120 frames: walks `game_mode` through all four modes and back. |
| `level_walk_jump.txt` | 3700 | 0, 1, 2 | 102/123 | jump-while-walking and jump-in-place in mode 0, then Select-cycles into modes 1 and 2 and walks there too. |
| `level_long_traverse.txt` | 3840 | 0 | 86/123 | holds Right for 3600 frames (plus a jump) in mode 0 -- long enough to cross whatever camera-position thresholds gate the weather/zone-transition triggers. |
| `level_attack_enemy.txt` | 1300 | 0 | 81/123 | walks right to the type-`$0E` entity mode 0 places at x=895,y=159, then presses B (the attack button) once within its 0x48px hit range. |
| **union** | | **0, 1, 2, 3** | **111/123** | |

Per-script counts and the union were measured directly (not estimated) with:

```
python3 recomp/harness/compare_coverage.py <(dream_harness --trace ... --input recomp/harness/inputs/<script>)
```

### What each script newly contributes (in the order above)

- **title_start_right.txt** (+74): the whole non-title-specific gameplay bring-up path
  -- reset, PPU/SPC init, entity/animation update, mode-0 particle draw, sound
  interface -- everything a single short level walk touches.
- **title_attract_then_start.txt** (+1): `mode1_reset_particles_and_oam`. The title's
  boot/fade sequence (`loc_C0BB81`..`loc_C0BEB6` in `out/dream.asm`) branches on a
  state byte at `$7F1195` when Start is pressed; a short idle takes the
  `loc_C0BEC0` arm (what `title_start_right.txt` exercises), but after ~2365 idle
  frames it takes a different arm that is the *only* place calling
  `mode1_reset_particles_and_oam` (`out/dream.asm:6999`). This is presumably the
  attract-mode/demo-timeout path the task description asked to look for.
- **mode_cycle.txt** (+19): `mode1_level_init`, `mode2_level_init`, `title_screen_init`
  (the other three `game_mode_table` entries), their four `jtbl_C0827A`/`jtbl_C08282`/
  `jtbl_C0828A` NMI-scroll siblings, and the particle/sparkle machinery that only
  those modes' particle dispatch runs (`sparkle_array_init`, `sparkle_update_and_draw`,
  `particle_spawn_from_table`, `particle_spawn_random`, `random_next`,
  `vram_generate_particle_tile`, `vram_write_tile_row_planes`, `entity_animate_only`,
  `oam_emit_frame_2row_flip`, `anim_cb_sfx_0508`, `entity_ai_chase_player`).
- **level_walk_jump.txt** (+11): footstep and enemy-hit sound callbacks
  (`play_footstep_sound`, `anim_cb_hit_enemies`, `anim_cb_reset_state`,
  `set_entity_state`), the flipped 3-row sprite-frame emitter
  (`oam_emit_frame_3row_flip`, from facing left), and mode-1/mode-2 entity
  animation-rate and sound-effect variants (`anim_rate_3_16`, `anim_rate_3_8`,
  `anim_cb_sfx_0506`, `anim_cb_sfx_0606`, `anim_cb_sfx_0507`, `anim_cb_sfx_050C`) --
  the entity types (`$0708` = `$0C`/`$0E`) that use those table rows apparently
  only animate while the player is in modes 1/2, not mode 0.
- **level_long_traverse.txt** (+5): `particle_spawn_mode0_weather` and `sub_C091BB`
  (mode-0-only state-machine steps gated on camera position / an `entity_flags` +
  `$0C1B` combination set deep in `mode1_level_init`/`mode0_level_init`, see
  `out/dream.asm` around `$08B38` and `$08D14`), plus two more sound callbacks
  (`anim_cb_sfx_060E`, `anim_cb_sfx_0709`) and `play_zone_transition_sound` -- all
  reached only once the player has walked far enough into the level.
- **level_attack_enemy.txt** (+1): `entity_hit_react` and, through it,
  `set_entity_state` -- the branch inside `anim_cb_hit_enemies` that
  `level_walk_jump.txt` already reached but never took, because nothing was
  standing in range. `--dump-wram` against the entity-record columns
  (`docs/naming_proposals.md` §7: `$0708`/`$0728`/`$0828`/`$08A8` per slot)
  found the type-`$0E` entity mode 0 places at x=895,y=159 (`entity_ai_none`,
  so it never moves) and confirmed B as the attack button by reading
  `entity_apply_hit_reaction`/`check_pending_player_attack`
  (`docs/handler_tables.md` §4, §"Other pointer-table candidates"): both read
  a joypad's held/pressed words (`$8A`/`$8C` for player 1, `$8E`/`$90` for
  player 2 -- `dream_ram.h`'s `joy1_held`/`joy1_pressed`/`joy2_held`/
  `joy2_pressed`) and force `entity_state = $000C` the instant bit `$8000` (B)
  is newly pressed. Walking right to within 0x48px of the entity and pressing
  B there puts `anim_cb_hit_enemies`'s in-range branch on the executed path.

## Cold routines: 12/123, and why

10 of these are dead code or otherwise unreachable by design, not a gap in the
scripts:

| routine | why it is cold |
|---|---|
| `orphan_C09206` | dead dispatcher: `data_C0B208` (its jump table) no longer holds code addresses -- confirmed in `docs/handler_tables.md` §3. |
| `orphan_C0A294` | unreferenced fragment, no `jsr`/`jsl`/table word anywhere in the ROM points at it (`docs/NOTES.md` open item 1). |
| `orphan_C0A35C` | same as above. |
| `orphan_C183F1` | dead twin of the sound-interface code after `sub_C1803E`, shared verbatim with DKC2/Killer Instinct (`docs/NOTES.md`, toolchain evidence section). |
| `entity_clear_anim_unused` | unreferenced fragment in the animation-callback area; no script word points at it (`docs/handler_tables.md` §2). |
| `anim_rate_3_32` | one of the `anim_rate_fn_table` handler variants; `docs/handler_tables.md` §1 confirms no table word selects it. |
| `anim_rate_1_16` | same table, same conclusion. |
| `unused_vec` | the single-byte `rti` stub shared by every emulation-mode vector slot (`docs/NOTES.md` vector table); the ROM never runs in emulation mode after `reset`, so it never executes. |
| `oam_emit_frame_1row` | sibling of `oam_emit_frame_2row`/`2row_flip`/`3row`/`3row_flip`, all of which are hot, but not a coverage-budget gap: it (and its 5-byte-header path) only fires for a sprite frame id `< 4` (`docs/data_formats.md` "Caveats worth knowing"), and every one of the 96 animation scripts in `data_C41858` was decoded and none ever sets a frame field to `1`, `2` or `3` (frame `0` means "empty animation", per `docs/handler_tables.md` §2) -- confirmed directly by walking the script table with python rather than inferred from a script budget. |
| `oam_emit_frame_1row_flip` | same table, same conclusion, facing the other way. |

2 are plausibly reachable in principle but need player 2's controller, which
`dream_harness` does not model -- not a gap this batch's scripts can close:

| routine | what it needs |
|---|---|
| `anim_cb_hit_player` | animation ids `118`/`11A` (`docs/handler_tables.md` §2) on a type-`$0E`/`$10` entity: raw `entity_state` (`docs/naming_proposals.md` §7) has to be exactly `$000C`/`$000E` for that (type, state) pair to select those ids in `entity_state_anim_table` (`data_C0B7AE`, verified against the ROM's raw table bytes, not just the disassembly). Every `entity_state,X` write in `out/dream.asm` bank `$C0` was enumerated: entity AI (`entity_ai_chase_player`/`entity_ai_none`) never writes it, and `entity_hit_react` only ever writes `$10`/`$12`/`$14`/`$16`. The one write that *can* produce `$000C` is `entity_apply_hit_reaction`'s shared body (`docs/handler_tables.md` §"Other pointer-table candidates"), reached for a type-`$0E`/`$10` target only through `check_pending_player_attack` ($C0:8E8E), which reads player 2's `$8E`/`$90` (`joy2_held`/`joy2_pressed`, `dream_ram.h`). `recomp/harness/main.c`'s `machine_set_input()` hardcodes `snes->input2->currentState = 0` and the input-script format (`kButtons` in the same file) has no way to address a second controller, so no input script can ever make that bit nonzero. |
| `anim_cb_sfx_0602` | same ids `118`/`11A`, same missing controller -- see above. |

Reaching these two would need `dream_harness`/`machine_set_input()` to drive a
second controller, which is outside `recomp/harness/inputs/`'s reach; `explore.py`
already found the RAM (`$0BB4`/`$8E`/`$90`) and the exact (type, state) pair, so a
follow-up only needs the harness change, not more searching.

## Reproducing

```
make harness
for f in recomp/harness/inputs/*.txt; do
  ./build/recomp/dream_harness --frames 4000 --quiet --trace /tmp/$(basename "$f").trace --input "$f"
  echo "== $f =="
  python3 recomp/harness/compare_coverage.py /tmp/$(basename "$f").trace
done
```

(4000 frames comfortably covers every script above; each finishes long before that
via its own trailing `none`/held-button line, so running them all to frame 4000 is
harmless and simplifies the loop.)
