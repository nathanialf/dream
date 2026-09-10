#!/usr/bin/env python3
"""Turn config/assets.txt into the gallery's C table.

    gen_gallery_table.py config/assets.txt gallery_table.h

Run from CMake at configure time (recomp/app/CMakeLists.txt), which re-runs it when
config/assets.txt changes. The output holds offsets, sizes, kinds, manifest paths and
manifest notes -- no ROM bytes, ever: the gallery decodes the user's own ROM at run time
and this table only says where to look (docs/LEGAL.md).

"Unused" is not a machine-readable column in docs/data_formats.md's "referenced by" table,
so it is derived here, deterministically, from the manifest alone:

  * kinds that are unreferenced by construction -- stale, filler, unknown, sprite_frame_alt;
  * any note that says so (unreferenced / unused / stale / dead / no known reader), except
    on `code` assets, whose notes mention stale byte runs inside otherwise live code;
  * the font and picture-strip block in bank $C1 (0x010000-0x0155E0), which docs/
    data_formats.md marks "referenced by: none" row by row.

Output is a pure function of the input file (same bytes in, same bytes out).
"""

import re
import sys

# Kinds in the manifest, in the order the enum is emitted (alphabetical = stable).
KINDS = [
    'anim_script', 'anim_table', 'brr', 'code', 'entity_table', 'filler', 'hdma', 'map',
    'metatiles', 'palette', 'sfx_bank', 'song', 'spc_table', 'sprite_frame',
    'sprite_frame_alt', 'sprite_table', 'stale', 'tilemap', 'tileset_2bpp', 'tileset_4bpp',
    'tileset_8bpp', 'unknown',
]

UNUSED_KINDS = {'stale', 'filler', 'unknown', 'sprite_frame_alt'}
UNUSED_NOTE = re.compile(r'unreferenced|unused|\bstale\b|\bdead\b|no known reader', re.I)

# Bank $C1's font + picture strips: unreferenced per docs/data_formats.md, but their notes
# do not all say so and their kinds are ordinary tileset/tilemap kinds.
BANK_C1_UNUSED = (0x010000, 0x0155E0)


def c_string(s):
    out = []
    for ch in s:
        if ch in '"\\':
            out.append('\\' + ch)
        elif ch == '\t':
            out.append(' ')
        elif 0x20 <= ord(ch) < 0x7F:
            out.append(ch)
        else:
            out.append('?')          # the manifest is ASCII; keep the header ASCII too
    return '"' + ''.join(out) + '"'


def parse(path):
    assets = []
    with open(path, 'r', encoding='utf-8') as fp:
        for lineno, line in enumerate(fp, 1):
            line = line.rstrip('\n')
            if not line.strip() or line.lstrip().startswith(';'):
                continue
            body, _, note = line.partition(';')
            fields = body.split()
            if len(fields) < 4:
                raise SystemExit('%s:%d: expected "start end kind path"' % (path, lineno))
            start, end, kind, apath = fields[0], fields[1], fields[2], fields[3]
            if kind not in KINDS:
                raise SystemExit('%s:%d: unknown kind %r' % (path, lineno, kind))
            assets.append((int(start, 16), int(end, 16), kind, apath, note.strip()))
    assets.sort(key=lambda a: a[0])
    return assets


def is_unused(start, kind, note):
    if kind in UNUSED_KINDS:
        return True
    if kind != 'code' and UNUSED_NOTE.search(note):
        return True
    return BANK_C1_UNUSED[0] <= start < BANK_C1_UNUSED[1]


def main(argv):
    if len(argv) != 3:
        sys.stderr.write('usage: gen_gallery_table.py <assets.txt> <gallery_table.h>\n')
        return 2
    assets = parse(argv[1])

    notes, note_index = [], {}
    for _, _, _, _, note in assets:
        if note not in note_index:
            note_index[note] = len(notes)
            notes.append(note)

    out = []
    w = out.append
    w('/* Generated from config/assets.txt by recomp/app/gen_gallery_table.py.')
    w(' * Do not edit: change the manifest (or the generator) and re-run cmake.')
    w(' *')
    w(' * Offsets, kinds, manifest paths and manifest notes only -- no ROM bytes.')
    w(' */')
    w('#ifndef GALLERY_TABLE_H')
    w('#define GALLERY_TABLE_H')
    w('')
    w('#include <stdint.h>')
    w('')
    w('typedef enum {')
    for i, k in enumerate(KINDS):
        w('  GK_%s = %d,' % (k.upper(), i))
    w('  GK_COUNT = %d' % len(KINDS))
    w('} GalleryKind;')
    w('')
    w('static const char* const kGalleryKindName[GK_COUNT] = {')
    for k in KINDS:
        w('  %s,' % c_string(k))
    w('};')
    w('')
    w('/* The asset is not read by the live program (see the generator for the rule). */')
    w('#define GA_UNUSED 0x0001u')
    w('')
    w('typedef struct {')
    w('  uint32_t start;      /* file offset, = CPU $C0:0000 + start (HiROM) */')
    w('  uint32_t end;        /* exclusive */')
    w('  uint16_t kind;       /* GalleryKind */')
    w('  uint16_t flags;      /* GA_* */')
    w('  uint16_t note;       /* index into kGalleryNotes */')
    w('  const char* path;    /* the manifest path under data/ (never written by the app) */')
    w('} GalleryAsset;')
    w('')
    w('static const char* const kGalleryNotes[%d] = {' % len(notes))
    for n in notes:
        w('  %s,' % c_string(n))
    w('};')
    w('')
    w('static const GalleryAsset kGalleryAssets[%d] = {' % len(assets))
    for start, end, kind, apath, note in assets:
        w('  { 0x%06Xu, 0x%06Xu, GK_%s, %s, %d, %s },'
          % (start, end, kind.upper(),
             'GA_UNUSED' if is_unused(start, kind, note) else '0',
             note_index[note], c_string(apath)))
    w('};')
    w('')
    w('static const unsigned kGalleryAssetCount = %d;' % len(assets))
    w('')
    w('#endif')
    text = '\n'.join(out) + '\n'

    try:
        with open(argv[2], 'r', encoding='utf-8') as fp:
            if fp.read() == text:
                return 0                      # unchanged: do not touch the timestamp
    except OSError:
        pass
    with open(argv[2], 'w', encoding='utf-8') as fp:
        fp.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
