#!/usr/bin/env python3
"""Turn config/assets.txt into the gallery's C table.

    gen_gallery_table.py config/assets.txt gallery_table.h

Run from CMake at configure time (recomp/app/CMakeLists.txt), which re-runs it when
config/assets.txt changes. The output holds offsets, sizes, kinds, manifest paths and
manifest notes, never ROM bytes: the gallery decodes the user's own ROM at run time
and this table only says where to look (docs/LEGAL.md).

"Unused" is not a machine-readable column in docs/data_formats.md's "referenced by" table,
so it is derived here, deterministically, from the manifest alone:

  * kinds that are unreferenced by construction: stale, filler, unknown, sprite_frame_alt;
  * any note that says so (unreferenced / unused / stale / dead / no known reader), except
    on `code` assets, whose notes mention stale byte runs inside otherwise live code;
  * the font and picture-strip block in bank $C1 (0x010000-0x0155E0), which docs/
    data_formats.md marks "referenced by: none" row by row.

The second half of the header is the palette assignment: which palette bytes each
game mode's init DMAs into which CGRAM entries, which tileset belongs to which BG of
which mode (so its tilemap words' palette bits resolve), and the landmark offsets the
gallery needs to walk the entity/animation/frame tables in the user's ROM at run time.
Those are ROM-*derived* facts (offsets, colour counts, CGRAM addresses, mode numbers,
entity types) read off the mode-init bodies and the tables docs/data_formats.md
"Palette assignment" derives, never ROM bytes. Anything that needs the bytes themselves
(the colours, the entity init records, the animation scripts) the gallery reads from the
user's own ROM.

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



# ---------------------------------------------------------------------------
# Palette assignment (docs/data_formats.md, "Palette assignment").
#
# Nothing below is read out of the ROM: every number is an address, a count or an
# index taken from the disassembled mode-init bodies (recomp/src/mode_init.c,
# recomp/src/top_level.c) and from config/assets.txt.
# ---------------------------------------------------------------------------

# Pseudo-mode for the title screen, which is not one of the four game_mode scenes.
MODE_TITLE = 4
MODE_NAMES = ['game_mode 0', 'game_mode 1', 'game_mode 2', 'game_mode 3', 'title screen']

# Every CGRAM write a scene's init performs, in the order it performs them, as
# {cgadd, colours, source file offset}. `dma_upload_to_cgram` ($C0:A483) takes
# A = source in bank $C4, X = bytes/8, Y = CGADD, so colours = X * 4.
# game_mode 1 ends with two loose CGDATA bytes at CGADD $E1 out of the main
# program at 008791 rather than a DMA; it is the one row whose source is not the
# bank-$C4 palette block.
CGRAM_UPLOADS = [
    # mode 0: mode0_level_init $C0:8292, DMAs at $8386/$8392/$839E/$83AA
    (0, 0x00, 128, 0x046DA8, 'C08386'),
    (0, 0x80, 128, 0x046C48, 'C08392'),
    (0, 0xE0,  16, 0x046D68, 'C0839E'),
    (0, 0xF0,  16, 0x046D48, 'C083AA'),
    # mode 1: mode1_level_init $C0:84D7, DMAs at $873A/$8746/$8752/$8776
    (1, 0x80, 128, 0x046C48, 'C0873A'),
    (1, 0xA0,  16, 0x046D08, 'C08746'),
    (1, 0x00, 128, 0x046EA8, 'C08752'),
    (1, 0xB0,  16, 0x046D88, 'C08776'),
    (1, 0xE1,   1, 0x008791, 'C0877D'),   # sta CGADD #$E1, then two sta CGDATA
    # mode 2: mode2_level_init $C0:8798, DMAs at $886A/$8876/$8882/$888E
    (2, 0x80, 128, 0x046C48, 'C0886A'),
    (2, 0xC0,  64, 0x046C48, 'C08876'),
    (2, 0xE0,  16, 0x046CC8, 'C08882'),
    (2, 0x00, 128, 0x046FE3, 'C0888E'),
    # mode 3: title_screen_init $C0:88AB (the fourth scene, not the title),
    # DMAs at $8992/$899E/$89AA/$89B6
    (3, 0x80, 128, 0x046C48, 'C08992'),
    (3, 0xC0,  64, 0x046C48, 'C0899E'),
    (3, 0xA0,  16, 0x047443, 'C089AA'),
    (3, 0x00, 128, 0x047343, 'C089B6'),
    # title: loc_C0BB81, the CGDATA loop at $BC2A: 512 bytes straight to CGRAM
    (MODE_TITLE, 0x00, 256, 0x06A36B, 'C0BC2A'),
]

# CGRAM entries a scene rewrites after its init, so a capture of the running game
# need not equal the constructed table there. Each row is
# {mode, first, count, what}. Derived in docs/data_formats.md; the mode-1 row is
# the HDMA table at 046FA8 (DMAP1 $03 / BBAD1 $21 = CGADD+CGDATA, armed at
# $C0:8669) and holds only the entries whose last scanline value differs from the
# uploaded one.
CGRAM_ANIMATED = [
    (0, 0x12, 1, 'palette_cycle_ramp 046B88 -> CGRAM $12 (sub_C08D6B; no script reaches it)'),
    (0, 0x70, 16, 'streaming descriptors 046E88 / 0470E3 -> CGRAM $70 (zone palettes; the '
                  'first is the same 16 colours the init uploads)'),
    (1, 0x00, 16, 'HDMA colour table 046FA8 -> CGRAM $00-$0F, per scanline'),
]

# Sprite palettes: the OBJ half of CGRAM. Every mode uploads 046C48 to CGRAM $80,
# so OBJ palette p is 046C48 + 32*p unless a later, narrower DMA overwrote it;
# CGRAM_UPLOADS above already encodes those overwrites. This row only names the
# block for the gallery's text.
SPRITE_PAL_BLOCK = 0x046C48

# Per-tileset BG assignment: {tileset asset start, mode, bg layer, VRAM word
# address the init DMAs it to, the BG's character base from BG12NBA/BG34NBA, and
# the map assets whose words carry its palette bits}.
#
# The BG number is the PPU's, read off BG12NBA and BGnSC in each init. The
# manifest's file names disagree: they call mode 0's $2000 set "bg1" where the
# register makes it BG2. Each association is confirmed by the maps' own tile indices: the
# largest index a map uses is exactly one less than the tile count of the set it
# is paired with (docs/data_formats.md, "Palette assignment").
BG_TILESETS = [
    # (tileset, mode, bg, vram, charbase, [(map start, map end, note), ...])
    (0x090000, 0, 2, 0x2000, 0x2000, [(0x09DCE0, 0x09FD80, 'metatiles_mode0')]),
    (0x098AC0, 0, 1, 0x5000, 0x5000, [(0x0AF38E, 0x0AFB8E, 'tilemap $6800'),
                                      (0x0AEB8E, 0x0AF38E, 'tilemap $7000 (streamed)')]),
    (0x086AC0, 1, 1, 0x2000, 0x2000, [(0x0A26A0, 0x0A37A0, 'metatiles_mode1'),
                                      (0x0B0000, 0x0B0800, 'tilemap $6C40')]),
    (0x08C980, 1, 2, 0x5000, 0x5000, [(0x0CAB02, 0x0CB202, 'tilemap $7000')]),
    (0x070342, 2, 2, 0x2000, 0x2000, [(0x09B560, 0x09DCE0, 'metatiles_mode2')]),
    (0x07DF82, 2, 1, 0x6000, 0x6000, [(0x0B1000, 0x0B1800, 'tilemap $5800')]),
    (0x080000, 3, 2, 0x2000, 0x2000, [(0x0A37A0, 0x0A4860, 'metatiles_mode3')]),
    (0x095AC0, 3, 1, 0x6000, 0x6000, [(0x0B3800, 0x0B4000, 'tilemap $5800')]),
    # The title's BG1 is 8bpp in BGMODE 3: the pixel byte is the CGRAM index and
    # the tilemap's palette field does not apply, so there are no map rows and the
    # character base (0, from `stz BG12NBA`) is only here for completeness.
    (0x06002B, MODE_TITLE, 1, 0x0600, 0x0000, []),
]


# Every VRAM write a scene's init performs, in the order it performs them:
# {mode, VRAM word address, source file offset (or VRAM_FILL), words, bias
# added to each word as it is written, the word a fill writes, call site}.
#
# Read off the mode-init bodies exactly as CGRAM_UPLOADS is (recomp/src/mode_init.c
# for the four scenes, recomp/src/top_level.c's title_init for the title). The
# point of having them is that a tileset is not what the game draws: several sets
# land at different VRAM addresses and the maps index across them, so the only
# arrangement in which a map word means what it says is VRAM itself.
#
# `dma_upload_to_vram` ($C0:A46A) takes A = source lo16, X = bank, Y = byte count
# and writes to the address VMADDL was set to; `dma_fill_vram_zero` ($C0:A445)
# writes $800 bytes of zero to the address in A. The title writes its tiles and
# its four tilemaps with VMDATAL loops instead, and adds $0030 to every tilemap
# word (its tiles start at VRAM $0600, which is 8bpp tile index $30).
#
# The metatile blitter's own column writes to VRAM $7800 are not here: they are
# not init uploads, they are one column per frame as the camera moves
# (recomp/src/vram_stream.c), so nothing here replays them.
VRAM_FILL = 0xFFFFFFFF

VRAM_UPLOADS = [
    # mode 0: mode0_level_init $C0:8292
    (0, 0x1600, 0x0502C0, 0x0600, 0, 0, 'C08314'),
    (0, 0x1C00, 0x0AE38E, 0x0400, 0, 0, 'C08326'),
    (0, 0x2000, 0x090000, 0x2D60, 0, 0, 'C08338'),
    (0, 0x5000, 0x098AC0, 0x1550, 0, 0, 'C0834A'),
    (0, 0x6800, 0x0AF38E, 0x0400, 0, 0, 'C0835C'),
    (0, 0x6C00, VRAM_FILL, 0x0400, 0, 0, 'C08362'),
    (0, 0x7000, 0x0AEB8E, 0x0400, 0, 0, 'C08374'),
    (0, 0x7400, VRAM_FILL, 0x0400, 0, 0, 'C0837A'),
    # mode 1: mode1_level_init $C0:84D7
    (1, 0x2000, 0x086AC0, 0x3000, 0, 0, 'C0856A'),
    (1, 0x5000, 0x08C980, 0x1B10, 0, 0, 'C0857C'),
    (1, 0x6C00, VRAM_FILL, 0x0400, 0, 0, 'C08582'),
    (1, 0x6C40, 0x0B0000, 0x0400, 0, 0, 'C08594'),
    (1, 0x7020, 0x0CAB02, 0x0380, 0, 0, 'C085A6'),
    (1, 0x7000, 0x0CAB02, 0x0380, 0, 0, 'C085B8'),
    (1, 0x7420, 0x0CA402, 0x0380, 0, 0, 'C085CA'),
    (1, 0x7400, 0x0CA402, 0x0380, 0, 0, 'C085DC'),
    (1, 0x1E00, 0x050000, 0x0100, 0, 0, 'C08767'),
    # mode 2: mode2_level_init $C0:8798
    (2, 0x2000, 0x070342, 0x37E0, 0, 0, 'C08822'),
    (2, 0x5800, 0x0B1000, 0x0400, 0, 0, 'C08834'),
    (2, 0x5C00, VRAM_FILL, 0x0400, 0, 0, 'C0883A'),
    (2, 0x6000, 0x07DF82, 0x0CF0, 0, 0, 'C0884C'),
    (2, 0x7400, 0x0B0800, 0x0400, 0, 0, 'C0885E'),
    # mode 3: title_screen_init $C0:88AB (the fourth scene, not the title)
    (3, 0x1800, VRAM_FILL, 0x0400, 0, 0, 'C0892C'),
    (3, 0x1C00, 0x0B3000, 0x0400, 0, 0, 'C0893E'),
    (3, 0x2000, 0x080000, 0x3560, 0, 0, 'C08950'),
    (3, 0x5800, 0x0B3800, 0x0400, 0, 0, 'C08962'),
    (3, 0x5C00, 0x0B2800, 0x0400, 0, 0, 'C08974'),
    (3, 0x6000, 0x095AC0, 0x1800, 0, 0, 'C08986'),
    # title: title_init (loc_C0BB81), VMDATAL loops rather than DMAs
    (MODE_TITLE, 0x0600, 0x06002B, 0x4E60, 0, 0, 'C0BC47'),
    (MODE_TITLE, 0x6000, VRAM_FILL, 0x0400, 0, 0x0030, 'C0BC60'),
    (MODE_TITLE, 0x6500, 0x069CEB, 0x01A0, 0x30, 0, 'C0BC7C'),
    (MODE_TITLE, 0x6960, 0x06A02B, 0x00E0, 0x30, 0, 'C0BC98'),
    (MODE_TITLE, 0x61C0, 0x06A1EB, 0x0040, 0x30, 0, 'C0BCB4'),
    (MODE_TITLE, 0x6DA0, 0x06A2EB, 0x0080, 0x30, 0, 'C0BCD0'),
]

# Tilesets that are OBJ, not BG: their palette comes from an OAM attribute byte
# rather than a tilemap word. {asset, mode, OBJ palette, why}.
OBJ_TILESETS = [
    (0x0502C0, 0, 7, 'particle sprites; mode1_reset_particles_and_oam ORs #$0E00 '
                     'into the attribute word at $C0:931E'),
]

# Landmarks for the run-time sprite-palette walk (gallery.c does the walking; all
# of it needs ROM bytes). Addresses are file offsets = CPU $C0:0000 + offset.
LANDMARKS = [
    ('GX_ENTITY_TABLE',   0x00B4A4, True,  'data_C0B4A4: per-mode word offset, then 18-byte init records'),
    ('GX_ENTITY_REC',           18, False, 'bytes per entity init record'),
    ('GX_STATE_ANIM',     0x00B7AE, True,  'data_C0B7AE: two-level entity_state -> anim id table'),
    ('GX_STATE_ANIM_END', 0x00B8B0, True,  'last plausible entry is at +$100; docs/handler_tables.md 4 '
                                           'estimates "about 0xB880", the word at +$102 is code bytes'),
    ('GX_ANIM_INDEX',     0x041858, True,  'data_C41858: anim id (even byte index) -> script offset in bank $C4'),
    ('GX_ANIM_COUNT',          174, False, 'entries in the animation-script index'),
    ('GX_BANK_C4',        0x040000, True,  'file offset of bank $C4'),
    ('GX_FRAME_TABLE',    0x040000, True,  'data_C40000: frame id = byte index, 4 bytes {ptr16, bank, y-bias}'),
    ('GX_FRAME_COUNT',        1558, False, 'entries in the sprite frame table'),
]

# $0BAC, added to entity_state before the level-2 lookup: $0018 in game_mode 1,
# 0 everywhere else ($C084D7 against $C08292/$C08798/$C088AE).
STATE_ROW_OFFSET = [0, 0x18, 0, 0]

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
            # A malformed offset is a manifest error like the two above it, and it
            # reads like one; int()'s own ValueError arrives as a traceback and
            # names neither the file nor the line.
            try:
                lo, hi = int(start, 16), int(end, 16)
            except ValueError:
                raise SystemExit('%s:%d: start and end must be hex offsets, got %r %r'
                                 % (path, lineno, start, end))
            assets.append((lo, hi, kind, apath, note.strip()))
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
    w(' * Offsets, kinds, manifest paths and manifest notes only. No ROM bytes.')
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

    # ---- palette assignment ------------------------------------------------
    w('/* ---- palette assignment (docs/data_formats.md, "Palette assignment") ----')
    w(' *')
    w(' * Addresses, colour counts, CGRAM addresses, mode and palette numbers. The')
    w(' * colours themselves, the entity init records and the animation scripts stay')
    w(' * in the user\'s ROM; gallery.c reads them from there at run time.')
    w(' */')
    w('')
    w('#define GAL_MODE_TITLE %d' % MODE_TITLE)
    w('#define GAL_MODE_COUNT %d' % (MODE_TITLE + 1))
    w('')
    w('static const char* const kGalleryModeName[GAL_MODE_COUNT] = {')
    for n in MODE_NAMES:
        w('  %s,' % c_string(n))
    w('};')
    w('')
    w('/* One CGRAM write a scene\'s init performs, in the order it performs them. */')
    w('typedef struct {')
    w('  uint8_t  mode;      /* 0-3, or GAL_MODE_TITLE */')
    w('  uint8_t  cgadd;     /* first CGRAM entry written */')
    w('  uint16_t colours;   /* entries written */')
    w('  uint32_t src;       /* file offset of the first 15-bit BGR word */')
    w('  const char* site;   /* the instruction that issues it */')
    w('} GalleryPalUpload;')
    w('')
    w('static const GalleryPalUpload kGalleryPalUploads[%d] = {' % len(CGRAM_UPLOADS))
    for mode, cgadd, colours, src, site in CGRAM_UPLOADS:
        w('  { %d, 0x%02Xu, %d, 0x%06Xu, %s },' % (mode, cgadd, colours, src, c_string(site)))
    w('};')
    w('static const unsigned kGalleryPalUploadCount = %d;' % len(CGRAM_UPLOADS))
    w('')
    w('/* CGRAM a scene rewrites after its init: a capture of the running game need')
    w(' * not agree with the constructed table on these entries. */')
    w('typedef struct {')
    w('  uint8_t mode;')
    w('  uint8_t first;')
    w('  uint8_t count;')
    w('  const char* what;')
    w('} GalleryPalAnimated;')
    w('')
    w('static const GalleryPalAnimated kGalleryPalAnimated[%d] = {' % len(CGRAM_ANIMATED))
    for mode, first, count, what in CGRAM_ANIMATED:
        w('  { %d, 0x%02Xu, %d, %s },' % (mode, first, count, c_string(what)))
    w('};')
    w('static const unsigned kGalleryPalAnimatedCount = %d;' % len(CGRAM_ANIMATED))
    w('')
    w('/* One VRAM write a scene\'s init performs, in the order it performs them. */')
    w('#define GAL_VRAM_FILL 0x%08Xu' % VRAM_FILL)
    w('')
    w('typedef struct {')
    w('  uint8_t  mode;')
    w('  uint16_t vram;      /* VRAM word address */')
    w('  uint32_t src;       /* file offset of the first word, or GAL_VRAM_FILL */')
    w('  uint16_t words;')
    w('  uint16_t bias;      /* added to each word written (the title\'s +$30) */')
    w('  uint16_t fill;      /* the word a fill writes */')
    w('  const char* site;')
    w('} GalleryVramUpload;')
    w('')
    w('static const GalleryVramUpload kGalleryVramUploads[%d] = {' % len(VRAM_UPLOADS))
    for mode, vram, src, words, bias, fill, site in VRAM_UPLOADS:
        w('  { %d, 0x%04Xu, 0x%08Xu, 0x%04Xu, 0x%04Xu, 0x%04Xu, %s },'
          % (mode, vram, src, words, bias, fill, c_string(site)))
    w('};')
    w('static const unsigned kGalleryVramUploadCount = %d;' % len(VRAM_UPLOADS))
    w('')

    w('/* The OBJ palette block every scene uploads to CGRAM $80. */')
    w('#define GAL_SPRITE_PAL_BLOCK 0x%06Xu' % SPRITE_PAL_BLOCK)
    w('')
    w('/* A map whose words carry a tileset\'s palette bits. */')
    w('typedef struct {')
    w('  uint32_t start;')
    w('  uint32_t end;')
    w('  const char* name;')
    w('} GalleryBgMap;')
    w('')
    maxmaps = max(len(m) for _, _, _, _, _, m in BG_TILESETS)
    w('#define GAL_BG_MAPS_MAX %d' % maxmaps)
    w('')
    w('/* A tileset a scene uploads, with the BG the PPU registers give it. */')
    w('typedef struct {')
    w('  uint32_t tileset;   /* manifest asset start */')
    w('  uint8_t  mode;')
    w('  uint8_t  bg;        /* BG number from BG12NBA/BGnSC, not the file name */')
    w('  uint8_t  nmaps;')
    w('  uint16_t vram;      /* VRAM word address the init DMAs it to */')
    w('  uint16_t charBase;  /* the BG character base the tile index counts from */')
    w('  GalleryBgMap maps[GAL_BG_MAPS_MAX];')
    w('} GalleryBgTileset;')
    w('')
    w('static const GalleryBgTileset kGalleryBgTilesets[%d] = {' % len(BG_TILESETS))
    for tileset, mode, bg, vram, base, maps in BG_TILESETS:
        rows = ', '.join('{ 0x%06Xu, 0x%06Xu, %s }' % (a, b, c_string(n)) for a, b, n in maps)
        if not rows:
            rows = '{ 0, 0, 0 }'
        w('  { 0x%06Xu, %d, %d, %d, 0x%04Xu, 0x%04Xu, { %s } },'
          % (tileset, mode, bg, len(maps), vram, base, rows))
    w('};')
    w('static const unsigned kGalleryBgTilesetCount = %d;' % len(BG_TILESETS))
    w('')
    w('/* Tilesets that are OBJ rather than BG: the palette comes from an OAM')
    w(' * attribute byte, not a tilemap word. */')
    w('typedef struct {')
    w('  uint32_t tileset;')
    w('  uint8_t  mode;')
    w('  uint8_t  pal;       /* OBJ palette 0-7 -> CGRAM $80 + 16*pal */')
    w('  const char* why;')
    w('} GalleryObjTileset;')
    w('')
    w('static const GalleryObjTileset kGalleryObjTilesets[%d] = {' % len(OBJ_TILESETS))
    for tileset, mode, pal, why in OBJ_TILESETS:
        w('  { 0x%06Xu, %d, %d, %s },' % (tileset, mode, pal, c_string(why)))
    w('};')
    w('static const unsigned kGalleryObjTilesetCount = %d;' % len(OBJ_TILESETS))
    w('')
    w('/* Landmarks for the run-time sprite-palette walk (all of it needs ROM bytes,')
    w(' * so gallery.c does the walking). */')
    for name, value, is_addr, why in LANDMARKS:
        w('#define %-20s %-12s /* %s */'
          % (name, ('0x%06Xu' % value) if is_addr else ('%du' % value), why))
    w('')
    w('/* $0BAC, added to entity_state before the level-2 anim lookup. */')
    w('static const uint16_t kGalleryStateRowOffset[4] = { %s };'
      % ', '.join('0x%02Xu' % v for v in STATE_ROW_OFFSET))
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
