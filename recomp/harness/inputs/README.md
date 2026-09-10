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
| **union** | | **0, 1, 2, 3** | **110/123** | |

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

## Cold routines: 13/123, and why

8 of these are dead code or otherwise unreachable by design, not a gap in the
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

5 are plausibly reachable but were not hit by any script in this budget -- they need
a specific entity encounter (an enemy of a particular type, in a particular state,
at a particular position) that these scripts' undirected movement did not line up
with:

| routine | what it needs |
|---|---|
| `anim_cb_hit_player` | animation ids `118`/`11A` (`docs/handler_tables.md` §2): the player entity (index 0) is hit by something within 40px in its facing direction. `anim_cb_hit_enemies` (ids `3E`/`48`/`B6`/`C0`) *did* execute from `level_walk_jump.txt`, so the player can land attacks, but nothing landed one back within these runs. |
| `anim_cb_sfx_0602` | same ids `118`/`11A` as `anim_cb_hit_player` -- same missing encounter. |
| `entity_hit_react` (`sub_C0B171`) | called from inside `anim_cb_hit_player`/`anim_cb_hit_enemies` only once a target entity is actually found within range; the callbacks themselves ran (so they count as "hit" for tracing) but their in-range branch was never taken in these runs. |
| `oam_emit_frame_1row` | sibling of `oam_emit_frame_2row`/`2row_flip`/`3row`/`3row_flip`, all of which are hot; needs a sprite frame that renders as a single OAM row, i.e. a specific (probably small) entity or item that was not on screen along the paths walked. |
| `oam_emit_frame_1row_flip` | same, facing the other way. |

Reaching these would need mapping which entity types/animation ids spawn where in
each level (out of scope for this search's budget); `explore.py` is reusable for
that follow-up once such positions are known -- add a probe that walks to the
specific location/frame and it will report whether the corresponding routines light
up.

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
