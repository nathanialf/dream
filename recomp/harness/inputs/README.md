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
| `p2_enemy_attack.txt` | 2000 | 0, 1, 2 | 98/120 | Select-cycles to mode 2, walks to that mode's type-`$0E` entity (spawn x=813,y=120), then presses B on **player 2's pad** so the entity itself attacks, hitting the player. See "A second controller as a debug/test feature" below. |
| **union** | | **0, 1, 2, 3** | **110/120** | |

(`title_start_right.txt` through `level_attack_enemy.txt`'s figures above are as originally measured, against an `/123` denominator; `compare_coverage.py` run against this checkout's current `out/codemap.txt`/`out/symbols.txt` now reports 120 static routines, not 123, for reasons this pass did not chase down. `p2_enemy_attack.txt`'s row and the union figure were both measured directly against that current `/120` tool, the same way as the command below.)

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
- **p2_enemy_attack.txt** (+2): `anim_cb_hit_player` and `anim_cb_sfx_0602`,
  the two routines the previous batch left cold for want of a second
  controller. See the section below.

## A second controller as a debug/test feature

`anim_cb_hit_player` ($C0:B0CE) and `anim_cb_sfx_0602` ($C0:B05C) share the
same gate as `entity_hit_react`/`anim_cb_hit_enemies` above, but mirrored onto
player 2: `check_pending_player_attack` ($C0:8E8E) is `jtbl_C0827A`'s
game-mode-1/2/3 slot (mode 0's slot is `mode0_camera_zone_update` instead), and
when `$0BB4` (`player_attack_flag`, set once per level load to the first live
entity of type `$0E`-`$11`) is nonzero it feeds player 2's held/pressed words
(`$8E`/`$90`) into `entity_apply_hit_reaction`'s shared body ($C0:9A66) with
*that entity* as the target -- the exact state machine the player's own entity
runs off `$8A`/`$8C` (`recomp/src/entities.c`'s `entity_hit_reaction_body`).
A type-`$0E`-`$11` entity has no attack AI of its own -- the one in every level
this batch has found is `entity_ai_none`, confirmed against `out/dream.asm`
and `docs/naming_proposals.md` section 4/8E8E -- so the only way it ever
reaches an attacking state (`entity_state` = `$000C`/`$000E`, selecting anim
id 118/11A per `entity_state_anim_table`) is a second pad acting on its
behalf. Nothing in the ROM's own AI or attract-mode logic ever touches
`joy2_held`/`joy2_pressed`; this reads as a leftover developer/QA hook for
puppeting an enemy's attack on demand while testing hit reactions, not a
gameplay feature -- there is no in-game way to reach it with one controller.

Reaching it also needs `game_mode`'s animation "row offset":
`entity_update_tick`'s state -> `entity_anim_id` lookup ($C0:9983-9991,
`recomp/src/entities_ai.c`) adds `$0BAC` (`state_row_offset`: `$0018` in game
mode 1, `$0000` in every other mode) to `entity_state` before indexing
`entity_state_anim_table`, so the same (type, state) picks a different table
row in mode 1 than in mode 0/2/3. Empirically, driving mode 1's own
type-`$0E` entity (spawn x=768,y=20) this way sets its state to `$000C` for
exactly one frame and then resets to `$0000` without ever entering
`anim_cb_hit_player` -- the `+$18` row lands on some other, short-lived
animation. Mode 2 keeps the row offset at 0 (like mode 0) while still routing
`jtbl_C0827A` to `check_pending_player_attack`, so `p2_enemy_attack.txt` uses
mode 2's own type-`$0E` entity (spawn x=813,y=120) instead. `entity_apply_hit_reaction`
sets that entity's state to exactly `$000C`; `entity_update_tick`'s own per-tick
facing/ground-probe step (the same `and #$FFFC ; ora facing_state_bits_table,Y`
every entity gets every frame) then ORs this entity's steady facing sub-bits
(`$0002`, the same low bits its idle state already carried) back in, landing on
`$000E` and holding it there -- confirmed directly in WRAM, along with
`anim_cb_hit_player`'s own hit landing (the player's `entity_state` becomes
`$0010`, the hurt state `entity_hit_react` writes).

None of this was reachable before `recomp/harness/main.c` grew a second input
column (`frame Buttons | Buttons2`, `recomp/README.md`): `machine_set_input()`
used to hardcode `snes->input2->currentState = 0` and the old script format
had no way to address a second controller, so `$8E`/`$90` could never be
nonzero. `p2_enemy_attack.txt` is the script that exercises the new column.

## Cold routines: 10/120, and why

All 10 are dead code or otherwise unreachable by design, not a gap in the
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

The 2 that used to sit here, `anim_cb_hit_player` and `anim_cb_sfx_0602`, needed
player 2's controller and are covered now by `p2_enemy_attack.txt` -- see "A
second controller as a debug/test feature" above.

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
