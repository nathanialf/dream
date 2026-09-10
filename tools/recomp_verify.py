#!/usr/bin/env python3
"""recomp_verify.py — the gate for the C port.

Runs `dream_harness --lockstep --hooks on --spc-hooks on` over every input
script in `recomp/harness/inputs/`, one at a time, and reports per-script
pass/fail plus the per-routine hook call counts summed across the scripts. A
routine counts as verified when every script passed *and* it was actually
entered at least once: a table that never fired proves nothing.

Both halves of the port are gated the same way and in the same run: the 65816
routines in recomp/src (reported as `hook`) and the SPC700 sound-driver routines
in recomp/spc (reported as `spchook`). The lockstep comparison covers WRAM, VRAM,
CGRAM, OAM, the SPC700's 64 KB of ARAM, the 128 DSP registers and the SPC
registers, so one pass proves both.

    python3 tools/recomp_verify.py                    # report only, exit 1 on failure
    python3 tools/recomp_verify.py --spc-hooks off    # the 65816 side alone
    python3 tools/recomp_verify.py --no-cpu           # candidate = the no-cpu machine
    python3 tools/recomp_verify.py --update           # also rewrite config/recomp.txt

`--no-cpu` runs every script with the candidate machine executing no instructions
at all on either processor: the C bodies are the program and the scheduler in
recomp/harness/snes_state.c resolves every pc hand-off through the registry (see
"Running without the CPUs" in recomp/README.md). A pc with no body is a fatal
error naming the pc and the body that handed it over, so a pass is also the
port's dead-code check. The instruction count each script executed is printed
next to its result. It cannot `--update`: the call counts it reports are the
scheduler's dispatches, which are fewer than the ordinary run's wherever the ROM
used to re-enter a routine after a yield.

`--update` writes config/recomp.txt (which tools/progress.py credits to the
`recomp` badge) with the routines that were both called and passing, from both
halves: the names are the labels in out/symbols.txt and spc/driver.asm, which is
what progress.py looks routines up by. Routines that are registered but were
never reached by any script are listed under a comment as unverified rather than
silently credited. `--spc-hooks off` refuses to update, because the SPC routines
would all look unentered.

Each script runs for 900 frames unless it carries a `# frames N` header line, in
which case N is used.
"""
from __future__ import annotations
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = ROOT / 'build' / 'recomp' / 'dream_harness'
INPUTS = ROOT / 'recomp' / 'harness' / 'inputs'
RECOMP_TXT = ROOT / 'config' / 'recomp.txt'
DEFAULT_FRAMES = 900

HOOK_LINE = re.compile(r'^hook ([0-9A-F]{6}) (\S+)\s+(\d+) calls?$')
SPC_HOOK_LINE = re.compile(r'^spchook ([0-9A-F]{4}) (\S+)\s+(\d+) calls?$')
FRAMES_HEADER = re.compile(r'^\s*#\s*frames\s+(\d+)\s*$', re.IGNORECASE)


def script_frames(path: Path) -> int:
    """A script may ask for its own length with a `# frames N` header."""
    for line in path.read_text().splitlines():
        m = FRAMES_HEADER.match(line)
        if m:
            return int(m.group(1))
    return DEFAULT_FRAMES


def run_script(path: Path, frames: int, extra: list[str],
               spc_hooks: str) -> tuple[bool, dict[str, int], dict[str, int], str]:
    cmd = [str(HARNESS), '--lockstep', '--hooks', 'on', '--spc-hooks', spc_hooks,
           '--quiet', '--frames', str(frames), '--input', str(path)] + extra
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    calls: dict[str, int] = {}
    spc_calls: dict[str, int] = {}
    detail = ''
    nocpu = ''
    for line in proc.stdout.splitlines():
        if line.startswith('no-cpu: ') and 'instructions executed' in line:
            nocpu = line.strip()
            continue
        m = HOOK_LINE.match(line.strip())
        if m:
            calls[m.group(2)] = int(m.group(3))
            continue
        m = SPC_HOOK_LINE.match(line.strip())
        if m:
            spc_calls[m.group(2)] = int(m.group(3))
        elif line.startswith('MISMATCH'):
            detail = line.strip()
    if proc.returncode != 0:
        detail = detail or (proc.stderr.strip() or 'harness error')
    return proc.returncode == 0, calls, spc_calls, detail or nocpu


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--update', action='store_true',
                    help='rewrite config/recomp.txt from the result')
    ap.add_argument('--frames', type=int, default=None,
                    help='override the frame count for every script')
    ap.add_argument('--only', default=None,
                    help='pass through to the harness: install only these routines')
    ap.add_argument('--spc-hooks', choices=('on', 'off'), default='on',
                    help='install the SPC700 routines too (default on)')
    ap.add_argument('--no-cpu', action='store_true',
                    help='run the candidate with neither CPU core executing an '
                         'instruction (the C bodies are the program)')
    args = ap.parse_args()

    if args.update and args.spc_hooks == 'off':
        print('recomp_verify: --update needs --spc-hooks on, or the SPC routines '
              'would be written out as unverified', file=sys.stderr)
        return 2

    if args.update and args.no_cpu:
        print('recomp_verify: --update writes config/recomp.txt from the ordinary '
              'run, not the --no-cpu one, whose call counts are the scheduler\'s '
              'dispatches', file=sys.stderr)
        return 2

    if args.no_cpu and args.only:
        print('recomp_verify: --no-cpu needs the whole table, so not --only',
              file=sys.stderr)
        return 2

    if not HARNESS.exists():
        print(f'recomp_verify: {HARNESS.relative_to(ROOT)} missing; run `make harness` first',
              file=sys.stderr)
        return 2
    scripts = sorted(INPUTS.glob('*.txt'))
    if not scripts:
        print('recomp_verify: no input scripts under recomp/harness/inputs', file=sys.stderr)
        return 2

    extra = ['--only', args.only] if args.only else []
    if args.no_cpu:
        extra += ['--no-cpu']
    # one table per processor: 65816 routines from recomp/src, SPC700 routines
    # from recomp/spc. They are gated identically and in the same run.
    totals: dict[str, dict[str, int]] = {'65816': {}, 'spc700': {}}
    order: dict[str, list[str]] = {'65816': [], 'spc700': []}
    failures: list[str] = []

    for path in scripts:
        frames = args.frames or script_frames(path)
        ok, calls, spc_calls, detail = run_script(path, frames, extra, args.spc_hooks)
        for side, got in (('65816', calls), ('spc700', spc_calls)):
            for name, n in got.items():
                if name not in totals[side]:
                    totals[side][name] = 0
                    order[side].append(name)
                totals[side][name] += n
        fired = sum(1 for n in calls.values() if n)
        spc_fired = sum(1 for n in spc_calls.values() if n)
        status = 'pass' if ok else 'FAIL'
        print(f'{status}  {path.name:<32} {frames:>5} frames, '
              f'{fired}/{len(calls)} routines entered'
              + (f', {spc_fired}/{len(spc_calls)} spc700' if spc_calls else '')
              + (f'\n      {detail}' if detail else ''))
        if not ok:
            failures.append(path.name)

    called: dict[str, list[str]] = {}
    uncalled: dict[str, list[str]] = {}
    for side in ('65816', 'spc700'):
        called[side] = [n for n in order[side] if totals[side][n]]
        uncalled[side] = [n for n in order[side] if not totals[side][n]]
        if not order[side]:
            continue
        print()
        print(f'{side} routine call counts across all scripts:')
        width = max(len(n) for n in order[side])
        for name in order[side]:
            mark = ' ' if totals[side][name] else '*'
            print(f'  {mark} {name:<{width}} {totals[side][name]:>8}')
        if uncalled[side]:
            print('  (* never entered by any script: not verified)')

    print()
    print(f'{len(scripts) - len(failures)}/{len(scripts)} scripts passed, '
          f'{len(called["65816"])}/{len(order["65816"])} 65816 routines and '
          f'{len(called["spc700"])}/{len(order["spc700"])} spc700 routines '
          f'entered at least once')

    if failures:
        print('FAILED: ' + ', '.join(failures))
        return 1

    if args.update:
        lines = [
            '; Routines reimplemented in C and verified in lockstep against the ROM (one label per line).',
            '; tools/progress.py credits their traced byte size to the `recomp` section.',
            ';',
            '; Regenerated by `python3 tools/recomp_verify.py --update`: every script under',
            '; recomp/harness/inputs/ passed and each routine below was entered at least once.',
            '; Both processors are listed: 65816 routines (recomp/src, named as in',
            '; out/symbols.txt) and SPC700 sound-driver routines (recomp/spc, named as in',
            '; spc/driver.asm).',
            '',
            '; --- 65816 ---',
        ]
        lines += sorted(called['65816'])
        if called['spc700']:
            lines += ['', '; --- SPC700 sound driver ---']
            lines += sorted(called['spc700'])
        for side, where in (('65816', 'recomp/src'), ('spc700', 'recomp/spc')):
            if not uncalled[side]:
                continue
            lines += [
                '',
                f'; Registered in {where} but never entered by any input script, so not',
                '; credited here. They need a script that reaches them before they can count:',
            ]
            lines += [f';   {n}' for n in sorted(uncalled[side])]
        RECOMP_TXT.write_text('\n'.join(lines) + '\n')
        total_called = len(called['65816']) + len(called['spc700'])
        total_uncalled = len(uncalled['65816']) + len(uncalled['spc700'])
        print(f'updated {RECOMP_TXT.relative_to(ROOT)} with {total_called} routines'
              + (f' ({total_uncalled} unverified)' if total_uncalled else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
