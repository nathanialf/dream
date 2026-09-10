#!/usr/bin/env python3
"""Search for input scripts that maximize dream_harness static-routine coverage.

This is the search tool used to build the scripts under `recomp/harness/inputs/`.
It runs `dream_harness` with candidate button scripts, traces executed PCs, and
scores each candidate against `out/codemap.txt` / `out/symbols.txt` (the same
routine table `compare_coverage.py` reports on). It keeps a running "union"
coverage set across candidates and greedily accepts any candidate that adds at
least one previously-uncovered routine.

Usage:
    python3 recomp/harness/explore.py [--frames-probe N] [--budget-seconds N]

Requires `build/recomp/dream_harness` to already be built (`make harness`).

Strategy
--------
1. Baseline: press Start at frame 120 to leave the title (this alone reaches
   game_mode 0). `game_mode` ($A4) turns out to auto-advance (0->1->2->3->0...)
   whenever Select is pressed while no fade is in progress. This is the
   prototype's mode-cycling/debug feature mentioned in the task. `mode_cycle`
   below presses Select every 120 frames to walk through all four modes.
2. Probe a library of movement/action macros (hold a direction, jump with each
   face button, hold L/R, alternate directions, long unidirectional walks to
   reach level triggers) both from a cold start and from each of the four game
   modes (reached via the mode-cycle prefix).
3. Trace each probe, diff its routine coverage against the running union, and
   keep the script if it adds coverage. Kept scripts are the deliverable set;
   redundant probes are discarded.
4. Print a coverage table and the final cold-routine list at the end.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
HARNESS = os.path.join(REPO, 'build', 'recomp', 'dream_harness')
CODEMAP = os.path.join(REPO, 'out', 'codemap.txt')
SYMBOLS = os.path.join(REPO, 'out', 'symbols.txt')
INPUTS_DIR = os.path.join(HERE, 'inputs')

sys.path.insert(0, HERE)
import compare_coverage as cc  # noqa: E402


def load_static():
    ranges = cc.read_codemap(CODEMAP)
    syms = cc.read_symbols(SYMBOLS)
    routines = cc.routines(syms, ranges)
    return ranges, routines


def script_text(lines):
    out = []
    for frame, buttons in lines:
        out.append('%d %s' % (frame, buttons))
    return '\n'.join(out) + '\n'


def run_trace(lines, frames, tmpdir):
    """Run the harness on an in-memory script, return the set of executed
    file-offsets (folded to the $C0:0000 form compare_coverage.py uses)."""
    with tempfile.NamedTemporaryFile('w', dir=tmpdir, suffix='.txt', delete=False) as inf:
        inf.write(script_text(lines))
        inpath = inf.name
    trpath = inpath + '.trace'
    subprocess.run(
        [HARNESS, '--frames', str(frames), '--quiet',
         '--input', inpath, '--trace', trpath],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    offs, _ = cc.read_trace(trpath)
    return set(offs), inpath, trpath


def hot_names(offs, routines):
    hot = set()
    for lo, hi, name in routines:
        for o in offs:
            if lo <= o <= hi:
                hot.add(name)
                break
    return hot


# ---------------------------------------------------------------------------
# Macro library. Each macro is (name, lines, frames) where `lines` is a list
# of (frame, buttons) input-script rows and `frames` the run length. All
# macros start from a cold boot; ENTER leaves the title, MODE(n) additionally
# cycles game_mode to n via the Select debug feature discovered during this
# search (see recomp/harness/inputs/README.md).

ENTER = [(0, 'none'), (120, 'Start'), (126, 'none')]


def mode_prefix(target_mode, start_frame=240, gap=120):
    """Lines that press Select `target_mode` times, `gap` frames apart,
    starting at start_frame, to advance game_mode from 0 to target_mode.

    Each Select press advances game_mode by one (0->1->2->3->0...) once the
    fade counters $30/$32 are both back to zero; entering mode 0 itself needs
    ~114 frames to settle before the *first* Select is accepted, which is why
    start_frame is 240 (matches the existing title_start_right.txt timing),
    not the 126 where Start is released. A shorter gap than ~120 frames
    routinely drops presses, confirmed with --dump-wram against $A4."""
    lines = []
    f = start_frame
    for _ in range(target_mode):
        lines.append((f, 'Select'))
        lines.append((f + 6, 'none'))
        f += gap
    return lines, f


def probe_hold(name, buttons, mode, hold_frames=480):
    prefix, after = mode_prefix(mode)
    lines = ENTER + prefix + [(after, buttons)]
    frames = after + hold_frames
    return ('%s_m%d' % (name, mode), lines, frames)


def probe_alternate(name, seq, mode, step=90, reps=6):
    prefix, after = mode_prefix(mode)
    lines = ENTER + prefix
    f = after
    for i in range(reps):
        lines.append((f, seq[i % len(seq)]))
        f += step
    return ('%s_m%d' % (name, mode), lines, f + step)


def build_probes():
    probes = []
    # Mode cycling alone, and cycling with a long settle at the end.
    for m in (1, 2, 3, 0):
        prefix, after = mode_prefix(m)
        probes.append(('mode_cycle_to_%d' % m, ENTER + prefix, after + 300))

    directions = ['Right', 'Left', 'Up', 'Down']
    jump_buttons = ['B', 'A', 'X', 'Y']

    for mode in (0, 1, 2):
        for d in directions:
            probes.append(probe_hold('walk_%s' % d.lower(), d, mode, 600))
        for j in jump_buttons:
            probes.append(probe_hold('jump_%s' % j.lower(), j, mode, 480))
        probes.append(probe_hold('walk_right_jump', 'Right+B', mode, 900))
        probes.append(probe_hold('walk_left_jump', 'Left+B', mode, 900))
        probes.append(probe_hold('hold_l', 'L', mode, 300))
        probes.append(probe_hold('hold_r', 'R', mode, 300))
        probes.append(probe_hold('hold_lr', 'L+R', mode, 300))
        probes.append(probe_alternate('zigzag', ['Right', 'Left'], mode, step=45, reps=10))
        probes.append(probe_alternate('bounce', ['Right+B', 'Left+B'], mode, step=60, reps=8))
        probes.append(probe_alternate('updown', ['Up', 'Down'], mode, step=45, reps=8))

    # Long single-direction traverses to reach level-progression triggers
    # (weather/particle spawns keyed to camera position; see docs/handler_tables.md
    # and the entity_flags / $0C1B state-machine notes in out/dream.asm).
    for mode in (0, 1, 2):
        prefix, after = mode_prefix(mode)
        lines = ENTER + prefix + [(after, 'Right'), (after + 1800, 'Right+B')]
        probes.append(('long_walk_right_m%d' % mode, lines, after + 3600))
        lines = ENTER + prefix + [(after, 'Left'), (after + 1800, 'Left+B')]
        probes.append(('long_walk_left_m%d' % mode, lines, after + 3600))

    # Idle past the attract-mode threshold before pressing Start. The title's
    # own boot/fade sequence (loc_C0BB81..loc_C0BEB6) reads a state byte at
    # $7F1195 when Start is pressed; short idles take the loc_C0BEC0 arm, but
    # after ~2365 idle frames (binary-searched empirically, confirmed with
    # --dump-wram) it takes a different arm that calls
    # mode1_reset_particles_and_oam, reached no other way.
    probes.append(('title_attract_then_start',
                    [(0, 'none'), (2400, 'Start'), (2406, 'none'), (2500, 'Right')],
                    2900))

    # Start (pause) toggling, and Select spammed rapidly (debug feature stress).
    probes.append(('pause_toggle', ENTER + [(240, 'Start'), (246, 'none'),
                                             (400, 'Start'), (406, 'none')], 600))
    fast_select = ENTER
    f = 200
    for _ in range(8):
        fast_select = fast_select + [(f, 'Select'), (f + 3, 'none')]
        f += 20
    probes.append(('select_mash', fast_select, f + 200))

    return probes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--budget-seconds', type=float, default=1100.0,
                     help='stop launching new probes once this much CPU time has been spent')
    ap.add_argument('--keep-dir', default=None,
                     help='directory to copy accepted candidate scripts into (default: print only)')
    args = ap.parse_args()

    if not os.path.exists(HARNESS):
        sys.exit('build the harness first: make harness (expected %s)' % HARNESS)

    ranges, routines = load_static()
    total = len(routines)

    union = set()
    accepted = []  # (name, lines, frames, added_names)
    spent = 0.0

    with tempfile.TemporaryDirectory() as tmpdir:
        # Seed with the existing example script so probes are scored against
        # what it already covers.
        seed_path = os.path.join(INPUTS_DIR, 'title_start_right.txt')
        if os.path.exists(seed_path):
            t0 = time.time()
            trpath = os.path.join(tmpdir, 'seed.trace')
            subprocess.run([HARNESS, '--frames', '600', '--quiet',
                             '--input', seed_path, '--trace', trpath],
                            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            offs, _ = cc.read_trace(trpath)
            union |= hot_names(offs, routines)
            spent += time.time() - t0
            print('seed title_start_right.txt: %d/%d (%.1fs)' % (len(union), total, spent))

        for name, lines, frames in build_probes():
            if spent > args.budget_seconds:
                print('budget exhausted, stopping search early')
                break
            t0 = time.time()
            try:
                offs, inpath, trpath = run_trace(lines, frames, tmpdir)
            except subprocess.CalledProcessError as e:
                print('  %-28s FAILED (%s)' % (name, e))
                continue
            dt = time.time() - t0
            spent += dt
            names = hot_names(offs, routines)
            added = names - union
            if added:
                union |= added
                accepted.append((name, lines, frames, sorted(added)))
                print('  %-28s +%-3d new  (union %d/%d, %.1fs, %d frames)'
                      % (name, len(added), len(union), total, dt, frames))
            else:
                print('  %-28s +0        (%.1fs)' % (name, dt))

    print()
    print('== accepted candidates (%d) ==' % len(accepted))
    for name, lines, frames, added in accepted:
        print('%-28s frames=%-6d adds: %s' % (name, frames, ', '.join(added)))

    print()
    print('== union coverage: %d/%d ==' % (len(union), total))
    cold = [n for _, _, n in routines if n not in union]
    print('cold (%d): %s' % (len(cold), ', '.join(sorted(cold))))

    if args.keep_dir:
        os.makedirs(args.keep_dir, exist_ok=True)
        for name, lines, frames, added in accepted:
            path = os.path.join(args.keep_dir, name + '.txt')
            with open(path, 'w') as f:
                f.write('# frames=%d, adds: %s\n' % (frames, ', '.join(added)))
                f.write(script_text(lines))
        print('\nwrote %d candidate scripts to %s' % (len(accepted), args.keep_dir))


if __name__ == '__main__':
    main()
