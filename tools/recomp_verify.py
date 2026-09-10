#!/usr/bin/env python3
"""recomp_verify.py — the gate for the C port.

Runs `dream_harness --lockstep --hooks on` over every input script in
`recomp/harness/inputs/`, one at a time, and reports per-script pass/fail plus
the per-routine hook call counts summed across the scripts. A routine counts as
verified when every script passed *and* it was actually entered at least once:
a table that never fired proves nothing.

    python3 tools/recomp_verify.py               # report only, exit 1 on failure
    python3 tools/recomp_verify.py --update      # also rewrite config/recomp.txt

`--update` writes config/recomp.txt (which tools/progress.py credits to the
`recomp` badge) with the routines that were both called and passing. Routines
that are registered but were never reached by any script are listed under a
comment as unverified rather than silently credited.

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
FRAMES_HEADER = re.compile(r'^\s*#\s*frames\s+(\d+)\s*$', re.IGNORECASE)


def script_frames(path: Path) -> int:
    """A script may ask for its own length with a `# frames N` header."""
    for line in path.read_text().splitlines():
        m = FRAMES_HEADER.match(line)
        if m:
            return int(m.group(1))
    return DEFAULT_FRAMES


def run_script(path: Path, frames: int, extra: list[str]) -> tuple[bool, dict[str, int], str]:
    cmd = [str(HARNESS), '--lockstep', '--hooks', 'on', '--quiet',
           '--frames', str(frames), '--input', str(path)] + extra
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    calls: dict[str, int] = {}
    detail = ''
    for line in proc.stdout.splitlines():
        m = HOOK_LINE.match(line.strip())
        if m:
            calls[m.group(2)] = int(m.group(3))
        elif line.startswith('MISMATCH'):
            detail = line.strip()
    if proc.returncode == 2:
        detail = detail or (proc.stderr.strip() or 'harness error')
    return proc.returncode == 0, calls, detail


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--update', action='store_true',
                    help='rewrite config/recomp.txt from the result')
    ap.add_argument('--frames', type=int, default=None,
                    help='override the frame count for every script')
    ap.add_argument('--only', default=None,
                    help='pass through to the harness: install only these routines')
    args = ap.parse_args()

    if not HARNESS.exists():
        print(f'recomp_verify: {HARNESS.relative_to(ROOT)} missing; run `make harness` first',
              file=sys.stderr)
        return 2
    scripts = sorted(INPUTS.glob('*.txt'))
    if not scripts:
        print('recomp_verify: no input scripts under recomp/harness/inputs', file=sys.stderr)
        return 2

    extra = ['--only', args.only] if args.only else []
    totals: dict[str, int] = {}
    order: list[str] = []
    failures: list[str] = []

    for path in scripts:
        frames = args.frames or script_frames(path)
        ok, calls, detail = run_script(path, frames, extra)
        for name, n in calls.items():
            if name not in totals:
                totals[name] = 0
                order.append(name)
            totals[name] += n
        fired = sum(1 for n in calls.values() if n)
        status = 'pass' if ok else 'FAIL'
        print(f'{status}  {path.name:<32} {frames:>5} frames, '
              f'{fired}/{len(calls)} routines entered'
              + (f'\n      {detail}' if detail else ''))
        if not ok:
            failures.append(path.name)

    print()
    print('routine call counts across all scripts:')
    width = max((len(n) for n in order), default=0)
    called = [n for n in order if totals[n]]
    uncalled = [n for n in order if not totals[n]]
    for name in order:
        mark = ' ' if totals[name] else '*'
        print(f'  {mark} {name:<{width}} {totals[name]:>8}')
    if uncalled:
        print('  (* never entered by any script: not verified)')
    print()
    print(f'{len(scripts) - len(failures)}/{len(scripts)} scripts passed, '
          f'{len(called)}/{len(order)} routines entered at least once')

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
            '',
        ]
        lines += sorted(called)
        if uncalled:
            lines += [
                '',
                '; Registered in recomp/src but never entered by any input script, so not',
                '; credited here. They need a script that reaches them before they can count:',
            ]
            lines += [f';   {n}' for n in sorted(uncalled)]
        RECOMP_TXT.write_text('\n'.join(lines) + '\n')
        print(f'updated {RECOMP_TXT.relative_to(ROOT)} with {len(called)} routines'
              + (f' ({len(uncalled)} unverified)' if uncalled else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
