#!/usr/bin/env python3
"""Compare a dream_harness --trace coverage set against the static tracer's map.

    python3 recomp/harness/compare_coverage.py TRACE [--codemap out/codemap.txt]
                                                     [--symbols out/symbols.txt]
                                                     [--list-unmapped] [--list-cold]

The trace file holds one canonical `C0XXXX` address per executed instruction
(mirror banks folded onto $C0:0000 + file offset, the form used by out/dream.asm).
out/codemap.txt holds the ranges tools/trace65816.py decided are code, as file
offsets. The two answer different questions ("what ran" versus "what the static
trace believes is reachable"), so the output is the two differences:

  * executed but not in any static code range: either the static trace missed a
    routine, or the emulator ran something the tracer never saw (RAM handlers,
    the stale build image, data misread as code).
  * static routines never executed: expected in bulk, since a short scripted run
    touches a fraction of the game.
"""

import argparse
import re
import sys


def read_codemap(path):
    ranges = []
    with open(path) as f:
        for line in f:
            m = re.match(r'^([0-9A-Fa-f]{6})-([0-9A-Fa-f]{6})\s', line)
            if m:
                ranges.append((int(m.group(1), 16), int(m.group(2), 16)))
    return sorted(ranges)


def read_symbols(path):
    syms = []
    with open(path) as f:
        for line in f:
            if line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) >= 3 and re.fullmatch(r'[0-9A-Fa-f]{6}', parts[1]):
                syms.append((int(parts[1], 16), parts[2]))  # RAM symbols use '------'
    return sorted(syms)


def read_trace(path):
    offs = []
    non_rom = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                m = re.search(r'(\d+) executed PCs fell outside', line)
                if m:
                    non_rom = int(m.group(1))
                continue
            addr = int(line, 16)
            offs.append(addr - 0xC00000)
    return sorted(set(offs)), non_rom


def in_ranges(off, ranges):
    for lo, hi in ranges:
        if lo <= off <= hi:
            return True
        if off < lo:
            return False
    return False


def runs(sorted_offs):
    """Collapse a sorted offset list into (start, end) contiguous-ish runs."""
    out = []
    for off in sorted_offs:
        if out and off <= out[-1][1] + 4:
            out[-1][1] = off
        else:
            out.append([off, off])
    return [tuple(r) for r in out]


def routines(syms, ranges):
    """Routine-level units: named entries (not loc_*) inside a code range,
    extending to the next such entry or the end of the range."""
    ends = {hi for _, hi in ranges}
    entries = [(off, name) for off, name in syms
               if in_ranges(off, ranges) and not name.startswith('loc_')]
    out = []
    for i, (off, name) in enumerate(entries):
        end = None
        for lo, hi in ranges:
            if lo <= off <= hi:
                end = hi
                break
        nxt = entries[i + 1][0] - 1 if i + 1 < len(entries) else end
        out.append((off, min(nxt, end), name))
    assert ends is not None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('trace')
    ap.add_argument('--codemap', default='out/codemap.txt')
    ap.add_argument('--symbols', default='out/symbols.txt')
    ap.add_argument('--list-unmapped', action='store_true',
                    help='list every executed address outside the static code ranges')
    ap.add_argument('--list-cold', action='store_true',
                    help='list every static routine that never executed')
    args = ap.parse_args()

    ranges = read_codemap(args.codemap)
    syms = read_symbols(args.symbols)
    offs, non_rom = read_trace(args.trace)

    static_bytes = sum(hi - lo + 1 for lo, hi in ranges)
    inside = [o for o in offs if in_ranges(o, ranges)]
    outside = [o for o in offs if not in_ranges(o, ranges)]

    print('trace            %s' % args.trace)
    print('static code      %d ranges, %d bytes' % (len(ranges), static_bytes))
    print('executed PCs     %d distinct ROM addresses (%d non-ROM PCs)' % (len(offs), non_rom))
    print('  in code ranges %d' % len(inside))
    print('  outside        %d' % len(outside))

    if outside:
        print()
        print('executed outside any static code range (%d runs):' % len(runs(outside)))
        for lo, hi in runs(outside):
            print('  %06X-%06X  $C0%04X..  %d addresses'
                  % (lo, hi, lo & 0xFFFF, sum(1 for o in outside if lo <= o <= hi)))
        if args.list_unmapped:
            for o in outside:
                print('    %06X' % o)

    rs = routines(syms, ranges)
    hot, cold = [], []
    for lo, hi, name in rs:
        n = sum(1 for o in offs if lo <= o <= hi)
        (hot if n else cold).append((lo, hi, name, n))
    print()
    print('static routines  %d total: %d executed, %d never executed'
          % (len(rs), len(hot), len(cold)))
    if args.list_cold:
        for lo, hi, name, _ in cold:
            print('  cold %06X-%06X %s' % (lo, hi, name))
    return 0


if __name__ == '__main__':
    sys.exit(main())
