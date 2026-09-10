#!/usr/bin/env python3
"""progress.py — regenerate README.md badges, docs/PROGRESS.md and docs/progress.json.

The ROM has no ELF, so "sections" are the ROM's own regions as declared in
config/regions.txt (class per byte range). Two kinds of progress:

  code sections (code, sound_iface, spc700)
      matched = bytes that belong to a routine carrying a human-chosen name, plus
      data tables inside the region that start at a human-named label.
      Auto names (sub_/loc_/orphan_/nmi_handler_/jtbl_/data_ + hex) do not count.
  recomp
      matched = traced bytes of 65816/SPC700 routines listed in config/recomp.txt, i.e.
      reimplemented in C and passing the lockstep check against the ROM. Total = all
      traced code bytes. This is the project's end target.
  data sections (sprites, tiles, maps, palettes, brr, music, anim, stale, filler, unknown)
      Three levels per byte, from config/assets.txt (named per-asset extraction) and
      config/roundtrip.txt (asset kinds whose editable form re-encodes to the ROM bytes):
        identified  = in a region with a known class (config/regions.txt)
        extracted   = inside a named asset of a real kind (not unknown)   -> reported as `extracted`
        round-trips = the asset kind is listed in config/roundtrip.txt     -> reported as `matched`
      stale, filler and unknown have nothing to round-trip, so for them matched = extracted.

The round trip is byte-identical by construction (make check), so this measures
understanding, not reproduction.

Needs baserom/DREAM.sfc (the tracer runs in-process). Usage:
    python3 tools/progress.py            # rewrite README.md / docs/PROGRESS.md / docs/progress.json
    python3 tools/progress.py --check    # exit 1 if the files would change
"""
from __future__ import annotations
import sys, os, re, json, datetime
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import trace65816 as t  # noqa: E402

ROM = ROOT / 'baserom' / 'DREAM.sfc'
REGIONS = ROOT / 'config' / 'regions.txt'
README = ROOT / 'README.md'
PROGRESS_MD = ROOT / 'docs' / 'PROGRESS.md'
PROGRESS_JSON = ROOT / 'docs' / 'progress.json'
SPC_ASM = ROOT / 'spc' / 'driver.asm'
SHA1 = '2675d7afe886f20462337aa1ee3aa5c3135fff3a'

SECTION_ORDER = ['recomp', 'code', 'sound_iface', 'spc700', 'sprites', 'tiles', 'maps', 'palettes',
                 'brr', 'music', 'anim', 'stale', 'filler', 'unknown']
CODE_KIND = {'code', 'sound_iface', 'spc700'}
RECOMP = ROOT / 'config' / 'recomp.txt'
ASSETS = ROOT / 'config' / 'assets.txt'
ROUNDTRIP = ROOT / 'config' / 'roundtrip.txt'
KIND_CLASS = {'sprite_frame': 'sprites', 'sprite_frame_alt': 'sprites', 'sprite_table': 'sprites', 'tileset_4bpp': 'tiles', 'tileset_8bpp': 'tiles',
              'tileset_2bpp': 'tiles', 'tilemap': 'maps', 'metatiles': 'maps', 'map': 'maps', 'hdma': 'maps',
              'palette': 'palettes', 'brr': 'brr', 'song': 'music', 'sfx_bank': 'music', 'spc_table': 'music',
              'anim_script': 'anim', 'anim_table': 'anim', 'stale': 'stale', 'filler': 'filler', 'unknown': 'unknown'}
LEVEL2_ONLY = {'stale', 'filler', 'unknown'}
AUTO = re.compile(r'^(sub|loc|orphan|nmi_handler|jtbl|data|handlers|null|unk)_[0-9A-Fa-f]{4,6}$')

def is_named(label: str | None) -> bool:
    return bool(label) and not AUTO.match(label)

def read_manifest(path):
    rows = []
    if not path.exists(): return rows
    for line in path.read_text().splitlines():
        body = line.split(';', 1)[0].strip()
        if not body: continue
        rows.append(body.split())
    return rows

def read_regions():
    regions = []
    for line in REGIONS.read_text().splitlines():
        body = line.split(';', 1)[0].strip()
        if not body: continue
        parts = body.split()
        start, end, cls, slug = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        desc = line.split(';', 1)[1].strip() if ';' in line else ''
        regions.append({'start': start, 'end': end, 'class': cls, 'name': slug, 'desc': desc})
    regions.sort(key=lambda r: r['start'])
    return regions

def run_tracer():
    sys.argv = ['trace65816', str(ROM), str(ROOT / 'out')]
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        t.main()

def class_at(regions, f):
    for r in regions:
        if r['start'] <= f < r['end']: return r['class']
    return 'unknown'

def routines_65816(regions):
    """Group traced instructions/tables into routines owned by the nearest preceding routine label."""
    entries = sorted(f for f in t.labels if f in t.insns and not t.labels[f].startswith(('loc_',)))
    entries_set = set(entries)
    owner = {}
    cur = None
    for f in sorted(t.insns):
        if f in entries_set: cur = f
        if cur is None: continue
        owner[f] = cur
    sizes = {}
    for f, ins in t.insns.items():
        if f in owner: sizes[owner[f]] = sizes.get(owner[f], 0) + ins.size
    for tf, es in t.tables.items():
        # a jump table belongs to the routine that precedes it
        prev = max((e for e in entries if e < tf), default=None)
        if prev is not None: sizes[prev] = sizes.get(prev, 0) + 2 * len(es)
    out = []
    for f in entries:
        name = t.labels[f]
        out.append({'name': name, 'addr': f'{t.file2addr(f):06X}', 'size': sizes.get(f, 0),
                    'section': class_at(regions, f), 'named': is_named(name)})
    return out

def named_data_bytes(regions):
    """Bytes of data runs (not instructions/tables) inside code regions that start at a human-named label."""
    per = {}
    covered = t.covered
    N = t.ROMSIZE
    f = 0
    while f < N:
        if covered[f]:
            f += 1; continue
        start = f
        while f < N and not covered[f] and (f == start or f not in t.labels): f += 1
        cls = class_at(regions, start)
        if cls in ('code', 'sound_iface') and is_named(t.labels.get(start)):
            per[cls] = per.get(cls, 0) + (f - start)
    return per

def routines_spc700():
    """Parse spc/driver.asm: labels + instruction address comments -> routine sizes."""
    lines = SPC_ASM.read_text().splitlines()
    items = []  # (addr, kind, label)
    cur_label = None
    named_data = [0]
    for ln in lines:
        m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):', ln)
        if m:
            cur_label = m.group(1); continue
        m = re.match(r'^\s+\S.*;\s*([0-9A-F]{4})\b', ln)
        if m and not ln.lstrip().startswith(('incbin', 'dw', 'db', ';')):
            items.append((int(m.group(1), 16), 'i', cur_label)); cur_label = None; continue
        m = re.search(r'incbin .*SPC \$([0-9A-F]{4})-\$([0-9A-F]{4})', ln)
        if m:
            a, b = int(m.group(1), 16), int(m.group(2), 16) + 1
            items.append((a, 'd', cur_label)); items.append((b, 'end', None))
            if is_named(cur_label): named_data[0] += b - a
            cur_label = None; continue
        m = re.match(r'^\s+dw\s', ln)
        if m and cur_label:
            items.append((None, 'dw', cur_label)); cur_label = None
    items = [i for i in items if i[0] is not None]
    items.sort(key=lambda x: x[0])
    routines = []
    cur = None
    code_total = 0
    for idx, (addr, kind, label) in enumerate(items):
        nxt = items[idx + 1][0] if idx + 1 < len(items) else addr
        if kind == 'i':
            size = max(0, nxt - addr)
            code_total += size
            if label and not label.startswith('loc_'):
                cur = {'name': label, 'addr': f'{addr:04X}', 'size': 0, 'section': 'spc700', 'named': is_named(label)}
                routines.append(cur)
            if cur: cur['size'] += size
        elif kind == 'd':
            cur = None
    return routines, code_total, named_data[0]

def compute():
    regions = read_regions()
    run_tracer()
    routines = routines_65816(regions)
    spc_routines, spc_code_total, spc_named_data = routines_spc700()
    ndata = named_data_bytes(regions)
    sections = {s: {'name': s, 'kind': 'code' if s in CODE_KIND else 'data', 'total': 0, 'matched': 0,
                    'count_total': 0, 'count_matched': 0} for s in SECTION_ORDER}
    for r in regions:
        sections[r['class']]['total'] += r['end'] - r['start']
    done = set()
    if RECOMP.exists():
        done = {ln.split(';')[0].strip() for ln in RECOMP.read_text().splitlines() if ln.split(';')[0].strip()}
    rc = sections['recomp']
    for rt in routines + spc_routines:
        rc['total'] += rt['size']; rc['count_total'] += 1
        if rt['name'] in done:
            rc['matched'] += rt['size']; rc['count_matched'] += 1
    sections['recomp']['kind'] = 'code'
    for rt in routines:
        s = sections[rt['section']] if rt['section'] in CODE_KIND else None
        if s is None: continue
        s['count_total'] += 1
        if rt['named']:
            s['count_matched'] += 1; s['matched'] += rt['size']
    for cls, b in ndata.items():
        sections[cls]['matched'] += b
    s = sections['spc700']
    s['matched'] += spc_named_data
    for rt in spc_routines:
        s['count_total'] += 1
        if rt['named']:
            s['count_matched'] += 1; s['matched'] += rt['size']
    for sec in sections.values(): sec.setdefault('extracted', 0)
    rt_rows = read_manifest(ROUNDTRIP)
    roundtrip = {row[0] for row in rt_rows if row[0] != 'raw'}
    raw_paths = {row[1] for row in rt_rows if row[0] == 'raw' and len(row) > 1}
    for row in read_manifest(ASSETS):
        a, b, kind = int(row[0], 16), int(row[1], 16), row[2]
        if len(row) > 3 and row[3] in raw_paths:
            cls = KIND_CLASS.get(kind)
            if cls: sections[cls]['count_total'] += 1; sections[cls]['extracted'] += b - a
            continue
        cls = KIND_CLASS.get(kind)
        if cls is None: continue
        sec = sections[cls]
        sec['count_total'] += 1
        if kind != 'unknown':
            sec['extracted'] += b - a
        if kind in roundtrip or (cls in LEVEL2_ONLY and kind != 'unknown'):
            sec['matched'] += b - a; sec['count_matched'] += 1
    region_rows = []
    for r in regions:
        if r['class'] in CODE_KIND: continue
        named = is_named(t.labels.get(r['start']))
        region_rows.append({'name': r['name'], 'start': f'{r["start"]:06X}', 'end': f'{r["end"]:06X}',
                            'size': r['end'] - r['start'], 'class': r['class'], 'named': named, 'desc': r['desc']})
    for sec in sections.values():
        sec['matched'] = min(sec['matched'], sec['total'])
    return {
        'target': 'dream', 'sha1': SHA1,
        'generated': datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
        'sections': [sections[s] for s in SECTION_ORDER],
        'routines': routines + spc_routines,
        'regions': region_rows,
    }

def pct(m, tot):
    return 100.0 * m / tot if tot else 0.0

def badge_color(p):
    if p >= 100: return 'brightgreen'
    if p >= 75: return 'green'
    if p >= 50: return 'yellowgreen'
    if p >= 25: return 'yellow'
    if p > 0: return 'orange'
    return 'red'

def badges(data):
    out = []
    for s in data['sections']:
        p = pct(s['matched'], s['total'])
        label = quote(s['name'].replace('_', ' '))
        out.append(f'![{s["name"]} progress](https://img.shields.io/badge/{label}-{p:.2f}%20%25-{badge_color(p)}.svg)')
    return '\n'.join(out)

def table(data):
    rows = ['| Section | Kind | Matched bytes | Total bytes | % | Items | Extracted bytes |', '| --- | --- | ---: | ---: | ---: | ---: | ---: |']
    for s in data['sections']:
        ex = s.get('extracted', '')
        rows.append(f'| `{s["name"]}` | {s["kind"]} | {s["matched"]} | {s["total"]} | {pct(s["matched"], s["total"]):.2f} % | {s["count_matched"]}/{s["count_total"]} | {ex} |')
    return '\n'.join(rows)

def splice(path: Path, block: str):
    text = path.read_text() if path.exists() else ''
    begin, end = '<!-- progress:begin -->', '<!-- progress:end -->'
    if begin in text and end in text:
        pre, rest = text.split(begin, 1)
        _, post = rest.split(end, 1)
        new = f'{pre}{begin}\n{block}\n{end}{post}'
    else:
        new = f'{text.rstrip()}\n\n{begin}\n{block}\n{end}\n'
    return new

def main():
    check = '--check' in sys.argv
    if not ROM.exists():
        sys.exit('progress.py: baserom/DREAM.sfc missing; cannot compute progress')
    data = compute()
    readme_new = splice(README, badges(data))
    explain = ('`recomp` counts traced code bytes reimplemented in C and passing the lockstep gate. Code '
               'sections count bytes inside routines that carry a human-chosen name. Data sections count '
               'bytes of assets whose kind round-trips through an editable form (`config/roundtrip.txt`); '
               '"Extracted" is the weaker level, bytes split into a named asset file by `tools/extract.py`. '
               '`stale`, `filler` and `unknown` have nothing to round-trip, so their matched figure is the '
               'extracted one. The rebuild itself '
               'is byte-identical on every commit (`make check`), so these figures measure how much of '
               'the ROM is understood, not reproduced.')
    progress_new = splice(PROGRESS_MD, table(data) + '\n\n' + explain)
    if not PROGRESS_MD.exists():
        progress_new = '# Progress\n\nAuto-regenerated by `tools/progress.py`.\n' + progress_new
    js = json.dumps(data, separators=(',', ':'))
    old_json = PROGRESS_JSON.read_text() if PROGRESS_JSON.exists() else ''
    def same_json(a, b):
        try:
            da, db = json.loads(a), json.loads(b)
        except Exception:
            return False
        da.pop('generated', None); db.pop('generated', None)
        return da == db
    changed = (readme_new != (README.read_text() if README.exists() else '') or
               progress_new != (PROGRESS_MD.read_text() if PROGRESS_MD.exists() else '') or
               not same_json(js, old_json))
    if check:
        print('progress: up to date' if not changed else 'progress: STALE')
        sys.exit(1 if changed else 0)
    if changed:
        README.write_text(readme_new)
        PROGRESS_MD.write_text(progress_new)
        PROGRESS_JSON.write_text(js + '\n')
    for s in data['sections']:
        print(f'{s["name"]:<12} {pct(s["matched"], s["total"]):6.2f} %  ({s["matched"]}/{s["total"]} bytes, {s["count_matched"]}/{s["count_total"]})')

if __name__ == '__main__':
    main()
