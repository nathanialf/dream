#!/usr/bin/env python3
"""roundtrip_check.py: decode every asset whose kind has a codec in tools/assetcodec.py,
re-encode the editable form, and compare the bytes with the extracted asset.

    python3 tools/roundtrip_check.py                 # report per-kind pass/total
    python3 tools/roundtrip_check.py --update        # ... and rewrite config/roundtrip.txt
    python3 tools/roundtrip_check.py --kind brr -v   # one kind, list every failure

Editable files land under build/assets/<same subpath as data/>; build/ is gitignored,
so nothing produced here is ever committed (docs/LEGAL.md rule 1).  A kind counts as
round-tripping only when *every* asset of that kind reproduces its ROM bytes exactly;
config/roundtrip.txt lists exactly those kinds and is what tools/progress.py reads to
decide which data bytes count as matched. An asset whose file is not there counts as a
failure of its kind, for the same reason: a kind cannot be at 100% on assets nobody read.

Exit status: 0 all checked assets round-trip, 1 something failed or nothing was checked,
2 the arguments were refused.
"""
from __future__ import annotations

import argparse
import os
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import assetcodec  # noqa: E402

ASSETS = os.path.join(ROOT, 'config', 'assets.txt')
ROUNDTRIP = os.path.join(ROOT, 'config', 'roundtrip.txt')
OUTROOT = os.path.join(ROOT, 'build', 'assets')

HEADER = """\
; config/roundtrip.txt: asset kinds (from config/assets.txt) whose decoder + encoder pair
; in tools/assetcodec.py is verified byte-exact: the decoded, human-editable form
; re-encodes to the exact ROM bytes for every asset of that kind.  Regenerate with
;
;   python3 tools/roundtrip_check.py --update
;
; Bytes of these kinds count as matched in the data sections of tools/progress.py.
; One kind per line; a kind only appears here at 100% pass.
"""


def read_assets():
    rows = []
    with open(ASSETS) as fp:
        for line in fp:
            body = line.split(';', 1)[0].strip()
            if not body:
                continue
            parts = body.split()
            rows.append((int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]))
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--update', action='store_true', help='rewrite config/roundtrip.txt')
    ap.add_argument('--kind', action='append', help='restrict to these kinds')
    ap.add_argument('-v', '--verbose', action='store_true', help='list every failing asset')
    args = ap.parse_args()

    rows = read_assets()
    stats = {}
    failures = []
    raw_assets = []
    editable_bytes = 0
    editable_files = 0
    missing = 0
    t0 = time.time()

    for (start, end, kind, path) in rows:
        if kind not in assetcodec.DECODERS:
            continue
        if args.kind and kind not in args.kind:
            continue
        src = os.path.join(ROOT, path)
        st = stats.setdefault(kind, {'pass': 0, 'total': 0, 'bytes': 0, 'files': 0})
        if not os.path.exists(src):
            # A missing file is a failure, not an absence. It used to `continue` before
            # st['total'] += 1, so the kind's ratio never saw it: a manifest that gained
            # asset paths `make extract` had not written yet reported that kind at 100%,
            # --update promoted it into config/roundtrip.txt, and tools/progress.py
            # credited every byte of the kind as matched.
            missing += 1
            st['total'] += 1
            failures.append((kind, path, 'file missing under data/ (run `make extract`)'))
            continue
        st['total'] += 1
        rel = os.path.dirname(os.path.relpath(path, 'data'))
        outdir = os.path.join(OUTROOT, rel)
        tmp = os.path.join(OUTROOT, '.roundtrip.tmp')
        try:
            primary, files = assetcodec.decode_asset(kind, src, outdir)
            assetcodec.encode_asset(kind, primary, tmp)
            with open(src, 'rb') as fp:
                want = fp.read()
            with open(tmp, 'rb') as fp:
                got = fp.read()
            raw_form = False
            try:
                import json as _json
                with open(primary) as _fp:
                    _meta = _json.load(_fp) if primary.endswith('.json') else {}
                raw_form = _meta.get('format') == 'raw'
            except Exception:                                      # noqa: BLE001
                raw_form = False
            if want == got and raw_form:
                raw_assets.append(path)                             # exact, but not an editable form
            if want == got:
                st['pass'] += 1
            else:
                where = next((i for i in range(min(len(want), len(got))) if want[i] != got[i]),
                             min(len(want), len(got)))
                failures.append((kind, path, f'{len(want)} vs {len(got)} bytes, first diff @{where}'))
            for f in files:
                st['bytes'] += os.path.getsize(f)
                st['files'] += 1
                editable_bytes += os.path.getsize(f)
                editable_files += 1
        except Exception as exc:                                   # noqa: BLE001
            failures.append((kind, path, f'{type(exc).__name__}: {exc}'))
        finally:
            if os.path.exists(tmp):
                os.remove(tmp)

    print(f'{"kind":14s} {"pass":>6s} {"total":>6s}  {"%":>6s}  {"editable files":>14s} {"editable bytes":>15s}')
    print('-' * 70)
    perfect = []
    tot_p = tot_t = 0
    for kind in sorted(stats):
        s = stats[kind]
        if not s['total']:
            continue
        pct = 100.0 * s['pass'] / s['total']
        print(f'{kind:14s} {s["pass"]:6d} {s["total"]:6d}  {pct:5.1f}%  {s["files"]:14d} {s["bytes"]:15d}')
        tot_p += s['pass']
        tot_t += s['total']
        if s['pass'] == s['total']:
            perfect.append(kind)
    print('-' * 70)
    print(f'{"TOTAL":14s} {tot_p:6d} {tot_t:6d}  {100.0*tot_p/tot_t if tot_t else 0:5.1f}%  '
          f'{editable_files:14d} {editable_bytes:15d}')
    if raw_assets:
        print(f'note: {len(raw_assets)} assets decode to a raw form (exact but not editable): ' + ', '.join(raw_assets))
    if missing:
        print(f'note: {missing} asset files missing under data/ (run `make extract` first)')
    if tot_t == 0:
        print('roundtrip: no asset was checked; nothing here proves anything')
    print(f'elapsed {time.time() - t0:.1f}s')

    if failures:
        print(f'\n{len(failures)} failing assets:')
        shown = failures if args.verbose else failures[:20]
        for (kind, path, why) in shown:
            print(f'  {kind:14s} {path}  {why}')
        if len(shown) < len(failures):
            print(f'  ... and {len(failures) - len(shown)} more (-v to list all)')

    if args.update:
        if args.kind:
            print('\nrefusing --update with --kind (it would drop the kinds not checked)')
            return 2     # a usage refusal, not a failing round trip
        with open(ROUNDTRIP, 'w') as fp:
            fp.write(HEADER)
            for kind in perfect:
                fp.write(kind + '\n')
            if raw_assets:
                fp.write('; assets whose editable form is a raw byte dump: exact, but not counted as round-tripping\n')
                for path in raw_assets:
                    fp.write(f'raw {path}\n')
        print(f'\nwrote {ROUNDTRIP}: {len(perfect)} kinds at 100% '
              f'({", ".join(perfect) if perfect else "none"})')
    return 1 if (failures or tot_t == 0) else 0


if __name__ == '__main__':
    sys.exit(main())
