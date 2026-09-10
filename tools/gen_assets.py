#!/usr/bin/env python3
"""Build config/assets.txt, the named per-asset manifest, from the ROM plus config/regions.txt.

    python3 tools/gen_assets.py generate baserom/DREAM.sfc config/regions.txt config/assets.txt
    python3 tools/gen_assets.py verify config/assets.txt [size]

`generate` re-derives asset boundaries from the ROM's own code-driven structures (the sprite
frame table at file 0x040000, the animation script index at 0x041858, the song table at
0x0210B9, and the BRR sample chain at 0x023095-0x035163) and subdivides the corresponding
regions.txt rows into one asset per frame/script/sample/song; every other row in regions.txt
becomes a single asset. `verify` checks that a manifest covers 0x000000-0x200000 exactly once,
with no gaps or overlaps, independent of the ROM.

Manifest line format: `<start6hex> <end6hex> <kind> <path>  ; note`. Nothing here is committed:
config/assets.txt is checked in (it is derived text, not ROM bytes), but the data/ files it
names are gitignored, same as the half-bank files.
"""
import re, sys, os

ROM_SIZE = 0x200000

KINDS = {
    'sprite_frame', 'sprite_frame_alt', 'sprite_table', 'tileset_4bpp', 'tileset_8bpp',
    'tileset_2bpp', 'tilemap', 'metatiles', 'map', 'palette', 'hdma', 'brr', 'song', 'sfx_bank',
    'spc_table', 'anim_script', 'anim_table', 'entity_table', 'code', 'stale', 'filler', 'unknown',
}

LINE_RE = re.compile(
    r'^([0-9a-fA-F]{6})\s+([0-9a-fA-F]{6})\s+(\S+)\s+(\S+)(?:\s*;\s*(.*))?$'
)


def rd8(rom, f): return rom[f]
def rd16(rom, f): return rom[f] | (rom[f + 1] << 8)


def parse_regions(path):
    """Yield (start, end, cls, slug, note) tuples in file order."""
    rows = []
    for line in open(path):
        line = line.rstrip('\n')
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        m = LINE_RE.match(line.strip())
        if not m:
            continue
        start, end, cls, slug = int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)
        note = (m.group(5) or '').strip()
        rows.append((start, end, cls, slug, note))
    return rows


# ---------- structural parsers (code-driven boundary derivation) ----------

def parse_sprite_frame_table(rom, start, end):
    """040000-041858: 1558 x {ptr16, bank, y-bias}. Returns sorted list of distinct non-null
    frame file offsets that fall inside one of the 9 live sprite-frame mega-regions (entry 0
    is a dummy: bank $C4 ptr 0 aliases the table's own start and is not real frame data)."""
    n = (end - start) // 4
    addrs = []
    for i in range(n):
        e = start + i * 4
        ptr = rd16(rom, e)
        bank = rd8(rom, e + 2)
        if bank == 0:
            continue  # true null terminator entries
        fo = ((bank & 0x3F) << 16) | ptr
        addrs.append((i, fo))
    return addrs


ALT_ATTR_LO, ALT_ATTR_HI = 0x1C, 0x22  # observed range of the alt-format {x,y,attr} 3rd byte


def _alt_record_run(rom, f, limit=200):
    """Count consecutive 3-byte {x,y,attr} records starting at f, attr in [ALT_ATTR_LO,HI]."""
    n = 0
    while n < limit:
        p = f + n * 3
        if not (ALT_ATTR_LO <= rom[p + 2] <= ALT_ATTR_HI):
            break
        n += 1
    return n


def parse_sprite_frame_alt_region(rom, start, end, thresh=8):
    """Chain-walk one alternate-format sprite-frame region (docs/data_formats.md 1b, alt
    format): unlike the live format there is no frame table pointing into these bytes, so
    frame boundaries are found by scanning for maximal runs (>=thresh records) of 3-byte
    {x, y, attr} OAM records with attr in 0x1C-0x22; the 8 bytes immediately before a run's
    first record are that frame's header, and each frame runs up to the next one's header
    (folding in its own 4bpp tile data plus any short undecoded trailer; same convention as
    the live format's trailer). Returns a sorted list of (start, end) frame spans covering
    [start, end) exactly. Validated against baserom/DREAM.sfc: reproduces exactly 82 frames in
    1CC6AA-1F0000 and 31 in 1F2E14-1FFEE5, matching the header-scan count in the docs."""
    anchors = []
    f = start
    while f < end - 3:
        if ALT_ATTR_LO <= rom[f + 2] <= ALT_ATTR_HI:
            n = _alt_record_run(rom, f, limit=200)
            if n >= thresh:
                anchors.append(f - 8)
                f += n * 3
                continue
        f += 1
    assert anchors and anchors[0] == start, \
        f'alt sprite-frame chain must start exactly at {start:06X} (first anchor {anchors[0] if anchors else None})'
    frames = []
    for i, a in enumerate(anchors):
        nxt = anchors[i + 1] if i + 1 < len(anchors) else end
        frames.append((a, nxt))
    return frames


def sprite_frame_len(rom, fo):
    """8-byte header {n1, n2, tile_off, ?, ?, ntiles1, vram_off, ntiles2|flags}, n1+n2 2-byte
    OAM records, (ntiles1+ntiles2)*32 bytes of 4bpp tiles. Validated against docs/data_formats.md
    1b: this formula abuts exactly for the large majority of the 1555 live frames and leaves a
    2-40 byte unread trailer for the rest (folded into the frame asset below), matching the
    documented '970 abut exactly ... 2-40 byte trailers' note with zero overlaps."""
    n1 = rd8(rom, fo)
    n2 = rd8(rom, fo + 1)
    ntiles1 = rd8(rom, fo + 5)
    ntiles2 = rd8(rom, fo + 7) & 0x7F
    return 8 + (n1 + n2) * 2 + (ntiles1 + ntiles2) * 32


def split_sprite_region(rom, region_start, region_end, frame_addrs_in_region, start_index):
    """Slice one live-format sprite-frame region into per-frame assets. Any bytes between one
    frame's decoded length and the next frame's start (or the region end) are folded into the
    preceding frame's file as an undecoded trailer (2-40 bytes; sub_C0A538 never reads them)."""
    assets = []
    block = sorted(frame_addrs_in_region)
    for i, a in enumerate(block):
        nxt = block[i + 1] if i + 1 < len(block) else region_end
        idx = start_index + i
        note = f'sprite frame table entry(ies) -> file offset {a:06X}'
        trailer = nxt - a - sprite_frame_len(rom, a)
        if trailer:
            note += f'; {trailer}-byte undecoded trailer folded in'
        assets.append((a, nxt, 'sprite_frame', f'data/sprites/frame_{idx:04d}.bin', note))
    return assets


def parse_anim_script_offsets(rom, idx_start, idx_end):
    n = (idx_end - idx_start) // 2
    offs = set()
    for i in range(n):
        v = rd16(rom, idx_start + i * 2)
        offs.add(0x040000 | v)
    return sorted(offs)


def parse_song_table(rom):
    """0210B9: 16 x {song_block_ptr24, sample_list_ptr24}; only the first 8 (songs 0-7) are used.
    Returns the 8 song block start file offsets in order."""
    TABLE = 0x0210B9
    starts = []
    for i in range(8):
        e = TABLE + i * 6
        a = rom[e] | (rom[e + 1] << 8) | (rom[e + 2] << 16)
        bank = (a >> 16) & 0xFF
        off = a & 0xFFFF
        starts.append(((bank & 0x3F) << 16) | off)
    return starts


def parse_brr_chain(rom, start, end):
    """023095-035163: sequential {loop_offset u16, length u16, BRR blocks} records; length is the
    byte count of the BRR block data that follows the 4-byte header. Chains exactly from start to
    end with no gaps (validated: 51 records land exactly on `end`)."""
    recs = []
    f = start
    while f < end:
        length = rd16(rom, f + 2)
        rec_len = 4 + length
        recs.append((f, f + rec_len))
        f += rec_len
    assert f == end, f'BRR chain did not land exactly on {end:06X} (stopped at {f:06X})'
    return recs


# ---------- asset construction ----------

def classify_generic(cls, slug, note):
    """Fallback kind for regions.txt rows that are not subdivided further; keys off the
    regions.txt class (itself code-driven per docs/data_formats.md) plus note keywords for the
    finer-grained kinds the manifest distinguishes (tile bit depth, tilemap vs metatiles vs hdma
    vs level map)."""
    nl = note.lower()
    if cls in ('code', 'sound_iface', 'spc700'):
        return 'code', 'misc' if cls != 'spc700' else 'spc'
    if cls == 'sprites':
        return 'sprite_frame', 'sprites'
    if cls == 'tiles':
        if '8bpp' in nl:
            return 'tileset_8bpp', 'gfx'
        if '2bpp' in nl or '1bpp' in nl:
            return 'tileset_2bpp', 'gfx'
        return 'tileset_4bpp', 'gfx'
    if cls == 'maps':
        if 'hdma' in nl:
            return 'hdma', 'hdma'
        if 'tilemap' in nl:
            return 'tilemap', 'maps'
        if 'metatile' in nl:
            return 'metatiles', 'maps'
        return 'map', 'maps'
    if cls == 'palettes':
        return 'palette', 'palettes'
    if cls == 'brr':
        return 'brr', 'brr'
    if cls == 'music':
        if 'sfx bank' in nl and 'filler' not in nl and 'placeholder' not in nl:
            return 'sfx_bank', 'music'
        if 'filler' in nl or 'placeholder' in nl:
            return 'filler', 'music'
        return 'spc_table', 'music'
    if cls == 'anim':
        if 'index' in slug:
            return 'anim_table', 'anim'
        return 'anim_script', 'anim'
    if cls == 'stale':
        return 'stale', 'stale'
    if cls == 'filler':
        return 'filler', 'filler'
    if cls == 'unknown':
        return 'unknown', 'unknown'
    raise ValueError(f'unhandled regions.txt class {cls!r}')


SPRITE_REGION_SLUGS = {
    'sprite_frames_c7a', 'sprite_frames_c7b', 'sprite_frames_c8', 'sprite_frames_ca',
    'sprite_frames_ca2', 'sprite_frames_cb', 'sprite_frames_cb2', 'sprite_frames_cc',
    'sprite_frames_ce',
}

# The two alternate-format sprite-frame mega-regions (unreferenced by any table): split by
# parse_sprite_frame_alt_region's header-scan chain walk into one asset per detected frame,
# using a path prefix distinct from the live-format frame_NNNN.bin series above so that the
# 1555 already-established live-frame paths (and their numbering) are left untouched.
SPRITE_ALT_REGION_SLUGS = {'sprite_frames_alt_dc', 'sprite_frames_alt_ef'}


def build_assets(rom, rows):
    assets = []  # (start, end, kind, path, note)

    # pre-parse the structures shared across rows
    all_frame_entries = None
    anim_offsets = None
    song_starts = None

    frame_counter = 0
    alt_frame_counter = 0
    for (start, end, cls, slug, note) in rows:
        if slug in SPRITE_ALT_REGION_SLUGS:
            region_frames = parse_sprite_frame_alt_region(rom, start, end)
            for (a, b) in region_frames:
                assets.append((a, b, 'sprite_frame_alt',
                                f'data/sprites/frame_alt_{alt_frame_counter:04d}.bin',
                                f'alternate-format sprite frame (header scan) -> file offset {a:06X}'))
                alt_frame_counter += 1
            continue
        if slug == 'sprite_frame_tail':
            # 283-byte partial/truncated alt-format frame at the ROM's end (header +
            # a handful of OAM records + a partial tile blob, cut off by the 0x200000 edge);
            # genuinely partial, but it is the *same* structure, so it gets the same kind and
            # keeps its existing path (nothing to split it into).
            assets.append((start, end, 'sprite_frame_alt', f'data/sprites/{slug}.bin',
                            note + '; partial alt-format frame (truncated by end of ROM)'))
            continue
        if slug == 'sprite_frame_table':
            assets.append((start, end, 'sprite_table', 'data/sprites/frame_table.bin',
                            note + '; 1558 x 4-byte {ptr16,bank,y_bias} records'))
            all_frame_entries = parse_sprite_frame_table(rom, start, end)
            continue
        if slug in SPRITE_REGION_SLUGS:
            assert all_frame_entries is not None, 'sprite_frame_table row must precede sprite_frames_* rows'
            in_region = [fo for (_, fo) in all_frame_entries if start <= fo < end]
            region_assets = split_sprite_region(rom, start, end, in_region, frame_counter)
            frame_counter += len(region_assets)
            assets.extend(region_assets)
            continue
        if slug == 'anim_script_index':
            assets.append((start, end, 'anim_table', 'data/anim/script_index.bin',
                            note + '; anim id -> script file offset'))
            anim_offsets = parse_anim_script_offsets(rom, start, end)
            continue
        if slug == 'anim_scripts':
            assert anim_offsets is not None
            in_region = [o for o in anim_offsets if start <= o < end]
            assert in_region and in_region[0] == start, \
                'anim script chain must start exactly at the anim_scripts region start'
            for i, a in enumerate(in_region):
                nxt = in_region[i + 1] if i + 1 < len(in_region) else end
                assets.append((a, nxt, 'anim_script', f'data/anim/script_{i:03d}.bin',
                                'animation script: 8-byte {callback,mode,duration,frame} records'))
            continue
        if slug == 'brr_samples':
            recs = parse_brr_chain(rom, start, end)
            for i, (a, b) in enumerate(recs):
                assets.append((a, b, 'brr', f'data/brr/sample_{i:02d}.bin',
                                'BRR sample record {loop_offset u16, length u16, blocks}'))
            continue
        if slug == 'song_blocks':
            song_starts = parse_song_table(rom)
            assert song_starts[0] == start, 'song table entry 0 must match the song_blocks row start'
            for i, a in enumerate(song_starts):
                nxt = song_starts[i + 1] if i + 1 < len(song_starts) else end
                extra = '' if i < 3 else '; empty song slot'
                assets.append((a, nxt, 'song', f'data/music/song_{i:02d}.bin',
                                'song block {dest u16, word_count u16, data}' + extra))
            continue
        # generic single-asset row
        kind, dirname = classify_generic(cls, slug, note)
        assets.append((start, end, kind, f'data/{dirname}/{slug}.bin', note))

    assets.sort(key=lambda a: a[0])
    return assets


def verify_assets(assets, size=ROM_SIZE):
    prev_end = 0
    paths = set()
    for (start, end, kind, path, note) in assets:
        if kind not in KINDS:
            raise ValueError(f'unknown kind {kind!r} for {path}')
        if start != prev_end:
            raise ValueError(f'coverage gap/overlap: expected {prev_end:06X}, got {start:06X} ({path})')
        if end <= start:
            raise ValueError(f'empty or negative-length asset {path}: {start:06X}-{end:06X}')
        if path in paths:
            raise ValueError(f'duplicate asset path {path}')
        paths.add(path)
        prev_end = end
    if prev_end != size:
        raise ValueError(f'coverage ends at {prev_end:06X}, expected {size:06X}')
    return True


def write_assets(path, assets):
    with open(path, 'w') as fp:
        fp.write(
            '; config/assets.txt: machine-readable per-asset manifest for DREAM.sfc, generated by\n'
            '; tools/gen_assets.py from config/regions.txt and the ROM\'s own code-driven structures\n'
            '; (the sprite frame table at 0x040000, the animation script index at 0x041858, the song\n'
            '; table at 0x0210B9, and the BRR sample chain at 0x023095-0x035163). Regenerate with:\n'
            ';\n'
            ';   python3 tools/gen_assets.py generate baserom/DREAM.sfc config/regions.txt config/assets.txt\n'
            ';\n'
            '; Format: one line per asset, fields separated by whitespace, comment after \';\':\n'
            ';\n'
            ';   <start6hex> <end6hex> <kind> <path>    ; free-text note\n'
            ';\n'
            '; start/end are 6 hex digit file offsets (CPU $C0:0000 + offset, HiROM), end exclusive.\n'
            '; Assets are sorted by start, contiguous and non-overlapping over the whole 2 MiB image\n'
            '; (000000-200000): every byte belongs to exactly one asset. path is the file under data/\n'
            '; that tools/extract.py writes the asset to (gitignored, like the half-bank files); it is\n'
            '; never committed. kind is one of: ' + ', '.join(sorted(KINDS)) + '.\n'
            ';\n'
            '; See docs/data_formats.md "Asset manifest" section for the derivation of each kind.\n\n'
        )
        for (start, end, kind, apath, note) in assets:
            note = note.replace('\n', ' ').strip()
            fp.write(f'{start:06x} {end:06x} {kind:<12} {apath:<32} ; {note}\n')


def cmd_generate(argv):
    rom_path, regions_path, out_path = argv
    rom = open(rom_path, 'rb').read()
    if len(rom) == ROM_SIZE + 512:
        rom = rom[512:]
    if len(rom) != ROM_SIZE:
        sys.exit(f'{rom_path}: expected {ROM_SIZE} bytes, got {len(rom)}')
    rows = parse_regions(regions_path)
    assets = build_assets(rom, rows)
    verify_assets(assets)
    write_assets(out_path, assets)
    by_kind = {}
    for (s, e, kind, *_r) in assets:
        d = by_kind.setdefault(kind, [0, 0])
        d[0] += 1
        d[1] += e - s
    print(f'wrote {len(assets)} assets to {out_path}')
    for kind in sorted(by_kind):
        n, nbytes = by_kind[kind]
        print(f'  {kind:<14} {n:5d} assets  {nbytes:9d} bytes')


def parse_assets_file(path):
    assets = []
    for line in open(path):
        s = line.split(';', 1)[0].strip()
        if not s:
            continue
        parts = s.split()
        start, end, kind, apath = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        assets.append((start, end, kind, apath, ''))
    assets.sort(key=lambda a: a[0])
    return assets


def cmd_verify(argv):
    path = argv[0]
    size = int(argv[1], 0) if len(argv) > 1 else ROM_SIZE
    assets = parse_assets_file(path)
    verify_assets(assets, size)
    print(f'OK: {len(assets)} assets cover 0x{size:06X} bytes exactly, no gaps or overlaps')


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ('generate', 'verify'):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'generate':
        cmd_generate(sys.argv[2:])
    else:
        cmd_verify(sys.argv[2:])


if __name__ == '__main__':
    main()
