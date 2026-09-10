#!/usr/bin/env python3
"""assetcodec.py: lossless, byte-exact codecs between the raw asset bytes that
tools/extract.py writes under data/ (per config/assets.txt) and human-editable files.

    python3 tools/assetcodec.py decode <kind> <asset.bin> <outdir>
    python3 tools/assetcodec.py encode <kind> <editable> <asset.bin>

`decode` writes the editable form(s) into <outdir> (named after the input stem) and
prints the primary editable file; `encode` reads that primary file (plus its `.json`
sidecar, when the kind has one) and writes the asset bytes back.  Every codec here is
required to be *exact*: for every asset of a supported kind,
encode(decode(bytes)) == bytes.  tools/roundtrip_check.py enforces that over the whole
manifest and maintains config/roundtrip.txt.

Supported kinds: palette, tileset_2bpp, tileset_4bpp, tileset_8bpp, tilemap, metatiles,
map, hdma, anim_script, anim_table, sprite_table, entity_table, sprite_frame,
sprite_frame_alt, brr, song, sfx_bank, spc_table.  code/stale/filler/unknown are
deliberately not decoded (they are either program text handled by src/ and spc/, or bytes
with no recovered structure).

Only the python3 standard library is used (zlib for PNG, struct, json, wave).
Nothing this file produces is committed; build/ is gitignored like data/.
"""
from __future__ import annotations

import json
import os
import struct
import sys
import wave
import zlib

# --------------------------------------------------------------------------------------
# kind tables
# --------------------------------------------------------------------------------------

#: kind -> extension of the primary editable file (the one `encode` is handed).
PRIMARY_EXT = {
    'palette': '.json',
    'tileset_2bpp': '.png',
    'tileset_4bpp': '.png',
    'tileset_8bpp': '.png',
    'tilemap': '.json',
    'metatiles': '.json',
    'map': '.json',
    'hdma': '.json',
    'anim_script': '.json',
    'anim_table': '.json',
    'sprite_table': '.json',
    'entity_table': '.json',
    'sprite_frame': '.png',
    'sprite_frame_alt': '.png',
    'brr': '.wav',
    'song': '.json',
    'sfx_bank': '.json',
    'spc_table': '.json',
}

SKIP_KINDS = {'code', 'stale', 'filler', 'unknown'}

TILE_BPP = {'tileset_2bpp': 2, 'tileset_4bpp': 4, 'tileset_8bpp': 8}
TILE_BYTES = {2: 16, 4: 32, 8: 64}

#: documented level-map dimensions (docs/data_formats.md section 1), keyed by asset stem.
MAP_DIMS = {
    'level_map_mode0': (120, 16),
    'level_map_mode1': (40, 8),
    'level_map_mode2': (64, 24),
    'level_map_mode3': (32, 24),
}


class CodecError(Exception):
    pass


def _hex(b) -> str:
    return bytes(b).hex()


def _unhex(s) -> bytes:
    return bytes.fromhex(s or '')


def _u16(d, o) -> int:
    return d[o] | (d[o + 1] << 8)


def _s16(v) -> int:
    return v - 0x10000 if v & 0x8000 else v


def _stem(path) -> str:
    return os.path.splitext(os.path.basename(path))[0]


def _load_json(path):
    with open(path, 'r') as fp:
        return json.load(fp)


def _save_json(path, obj):
    with open(path, 'w') as fp:
        json.dump(obj, fp, indent=1, sort_keys=False)
        fp.write('\n')


# --------------------------------------------------------------------------------------
# PNG (stdlib zlib only): 8-bit indexed writer/reader + a small truecolour writer
# --------------------------------------------------------------------------------------

_PNG_SIG = b'\x89PNG\r\n\x1a\x0a'


def _chunk(tag: bytes, data: bytes) -> bytes:
    body = tag + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)


def grey_plte(maxval: int) -> bytes:
    """256-entry PLTE: a full-range grey ramp over 0..maxval, identity above it.

    The *pixel* bytes always hold the raw palette index (0-15 for 4bpp, 0-3 for 2bpp,
    0-255 for 8bpp); this table only decides what the index looks like in a viewer.
    """
    step = 255 // maxval if maxval else 255
    out = bytearray()
    for i in range(256):
        v = min(255, i * step) if i <= maxval else i
        out += bytes((v, v, v))
    return bytes(out)


def write_png_indexed(path, width, height, pixels, plte):
    raw = bytearray()
    for y in range(height):
        raw.append(0)                                   # filter type 0 (None)
        raw += pixels[y * width:(y + 1) * width]
    out = bytearray(_PNG_SIG)
    out += _chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 3, 0, 0, 0))
    out += _chunk(b'PLTE', plte)
    out += _chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    out += _chunk(b'IEND', b'')
    with open(path, 'wb') as fp:
        fp.write(out)


def write_png_rgb(path, width, height, rgb):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgb[y * width * 3:(y + 1) * width * 3]
    out = bytearray(_PNG_SIG)
    out += _chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
    out += _chunk(b'IDAT', zlib.compress(bytes(raw), 9))
    out += _chunk(b'IEND', b'')
    with open(path, 'wb') as fp:
        fp.write(out)


def read_png_indexed(path):
    """Read an 8-bit indexed (or 8-bit greyscale) PNG -> (width, height, bytearray pixels).

    All five PNG row filters are handled, so a file re-saved by an image editor still
    loads as long as it stays 8 bits per pixel and one channel.
    """
    with open(path, 'rb') as fp:
        blob = fp.read()
    if blob[:8] != _PNG_SIG:
        raise CodecError(f'{path}: not a PNG')
    pos, idat, ihdr = 8, bytearray(), None
    while pos + 8 <= len(blob):
        (length,) = struct.unpack('>I', blob[pos:pos + 4])
        tag = blob[pos + 4:pos + 8]
        data = blob[pos + 8:pos + 8 + length]
        if tag == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', data)
        elif tag == b'IDAT':
            idat += data
        elif tag == b'IEND':
            break
        pos += 12 + length
    if ihdr is None:
        raise CodecError(f'{path}: no IHDR')
    width, height, depth, ctype, comp, filt, interlace = ihdr
    if depth != 8 or ctype not in (0, 3) or interlace != 0:
        raise CodecError(f'{path}: need a non-interlaced 8-bit indexed or greyscale PNG '
                         f'(got depth={depth} colour type={ctype} interlace={interlace})')
    raw = zlib.decompress(bytes(idat))
    stride = width
    out = bytearray(width * height)
    prev = bytearray(stride)
    o = 0
    for y in range(height):
        ftype = raw[o]
        o += 1
        line = bytearray(raw[o:o + stride])
        o += stride
        if ftype == 1:
            for i in range(1, stride):
                line[i] = (line[i] + line[i - 1]) & 0xFF
        elif ftype == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:
            for i in range(stride):
                a = line[i - 1] if i else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:
            for i in range(stride):
                a = line[i - 1] if i else 0
                c = prev[i - 1] if i else 0
                b = prev[i]
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ftype != 0:
            raise CodecError(f'{path}: bad row filter {ftype}')
        out[y * width:(y + 1) * width] = line
        prev = line
    return width, height, out


# --------------------------------------------------------------------------------------
# SNES tile planes
# --------------------------------------------------------------------------------------

def tile_to_pixels(data, off, bpp):
    """One SNES tile -> 64 palette indices (row-major)."""
    px = bytearray(64)
    planes = bpp // 2
    for pair in range(planes):
        base = off + pair * 16
        lo_bit, hi_bit = pair * 2, pair * 2 + 1
        for r in range(8):
            p0 = data[base + 2 * r]
            p1 = data[base + 2 * r + 1]
            row = r * 8
            for c in range(8):
                b = 7 - c
                v = ((p0 >> b) & 1) << lo_bit
                v |= ((p1 >> b) & 1) << hi_bit
                px[row + c] |= v
    return px


def pixels_to_tile(px, bpp):
    """64 palette indices -> one SNES tile."""
    out = bytearray(TILE_BYTES[bpp])
    planes = bpp // 2
    for pair in range(planes):
        base = pair * 16
        lo_bit, hi_bit = pair * 2, pair * 2 + 1
        for r in range(8):
            p0 = p1 = 0
            row = r * 8
            for c in range(8):
                v = px[row + c]
                b = 7 - c
                p0 |= ((v >> lo_bit) & 1) << b
                p1 |= ((v >> hi_bit) & 1) << b
            out[base + 2 * r] = p0
            out[base + 2 * r + 1] = p1
    return out


def _blit(canvas, cw, x, y, px):
    for r in range(8):
        canvas[(y + r) * cw + x:(y + r) * cw + x + 8] = px[r * 8:r * 8 + 8]


def _grab(canvas, cw, x, y):
    px = bytearray(64)
    for r in range(8):
        px[r * 8:r * 8 + 8] = canvas[(y + r) * cw + x:(y + r) * cw + x + 8]
    return px


# ======================================================================================
# tileset_2bpp / tileset_4bpp / tileset_8bpp  <->  indexed PNG, 16 tiles per row
# ======================================================================================

def decode_tileset(kind, data, outdir, stem):
    bpp = TILE_BPP[kind]
    tsize = TILE_BYTES[bpp]
    ntiles = len(data) // tsize
    trailer = data[ntiles * tsize:]
    cols = 16
    rows = (ntiles + cols - 1) // cols or 1
    w, h = cols * 8, rows * 8
    canvas = bytearray(w * h)
    for i in range(ntiles):
        _blit(canvas, w, (i % cols) * 8, (i // cols) * 8, tile_to_pixels(data, i * tsize, bpp))
    png = os.path.join(outdir, stem + '.png')
    write_png_indexed(png, w, h, canvas, grey_plte((1 << bpp) - 1))
    files = [png]
    if ntiles % cols or trailer:
        side = os.path.join(outdir, stem + '.json')
        _save_json(side, {'kind': kind, 'bpp': bpp, 'tiles_per_row': cols,
                          'tile_count': ntiles, 'trailer': _hex(trailer)})
        files.append(side)
    return png, files


def encode_tileset(kind, primary, outpath):
    bpp = TILE_BPP[kind]
    tsize = TILE_BYTES[bpp]
    w, h, px = read_png_indexed(primary)
    cols = w // 8
    side = os.path.splitext(primary)[0] + '.json'
    if os.path.exists(side):
        meta = _load_json(side)
        ntiles = meta['tile_count']
        trailer = _unhex(meta.get('trailer', ''))
        cols = meta.get('tiles_per_row', cols)
    else:
        ntiles = cols * (h // 8)
        trailer = b''
    out = bytearray()
    for i in range(ntiles):
        out += pixels_to_tile(_grab(px, w, (i % cols) * 8, (i // cols) * 8), bpp)
    out += trailer
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# palette  <->  JSON rows of 5-bit RGB triples (+ a PNG swatch, for viewing only)
# ======================================================================================

def decode_palette(kind, data, outdir, stem):
    nwords = len(data) // 2
    trailer = data[nwords * 2:]
    colours, high = [], []
    for i in range(nwords):
        w = _u16(data, 2 * i)
        colours.append([w & 31, (w >> 5) & 31, (w >> 10) & 31])
        if w & 0x8000:
            high.append(i)
    rows = [colours[i:i + 16] for i in range(0, len(colours), 16)]
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'palette', 'note': '15-bit BGR words as [r,g,b], 5 bits each, '
                                               'grouped 16 per row (one CGRAM palette)',
                    'rows': rows, 'bit15_set': high, 'trailer': _hex(trailer)})
    # swatch: 16 columns x one row per palette, 8x8 px per colour
    if rows:
        cw, ch, scale = 16, len(rows), 8
        rgb = bytearray(cw * scale * ch * scale * 3)
        for ry, row in enumerate(rows):
            for cx in range(16):
                r, g, b = row[cx] if cx < len(row) else (0, 0, 0)
                px = (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)
                for dy in range(scale):
                    o = ((ry * scale + dy) * cw * scale + cx * scale) * 3
                    rgb[o:o + scale * 3] = bytes(px) * scale
        png = os.path.join(outdir, stem + '.png')
        write_png_rgb(png, cw * scale, ch * scale, rgb)
        return js, [js, png]
    return js, [js]


def encode_palette(kind, primary, outpath):
    meta = _load_json(primary)
    high = set(meta.get('bit15_set', []))
    out = bytearray()
    i = 0
    for row in meta['rows']:
        for r, g, b in row:
            w = (r & 31) | ((g & 31) << 5) | ((b & 31) << 10)
            if i in high:
                w |= 0x8000
            out += struct.pack('<H', w)
            i += 1
    out += _unhex(meta.get('trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# tilemap / metatiles / map  <->  JSON of decoded 16-bit words
# ======================================================================================

def _tm_word(w):
    return {'tile': w & 0x3FF, 'pal': (w >> 10) & 7, 'pri': (w >> 13) & 1,
            'h': (w >> 14) & 1, 'v': (w >> 15) & 1}


def _tm_pack(e):
    return ((e['tile'] & 0x3FF) | ((e['pal'] & 7) << 10) | ((e['pri'] & 1) << 13)
            | ((e['h'] & 1) << 14) | ((e['v'] & 1) << 15))


def _map_word(w):
    return {'index': w & 0x3FFF, 'h': (w >> 14) & 1, 'v': (w >> 15) & 1}


def _map_pack(e):
    return (e['index'] & 0x3FFF) | ((e['h'] & 1) << 14) | ((e['v'] & 1) << 15)


def _words(data):
    n = len(data) // 2
    return [_u16(data, 2 * i) for i in range(n)], data[n * 2:]


def decode_tilemap(kind, data, outdir, stem):
    # two manifest rows carry a path whose real structure is the *other* map kind;
    # dispatch on the asset name so the editable form matches what the bytes are.
    if stem.startswith('metatiles'):
        return _decode_metatiles(data, outdir, stem)
    words, trailer = _words(data)
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'tilemap',
                    'note': 'SNES tilemap words: tile|pal<<10|pri<<13|h<<14|v<<15',
                    'width': 32, 'entries': [_tm_word(w) for w in words],
                    'trailer': _hex(trailer)})
    return js, [js]


def _decode_metatiles(data, outdir, stem):
    words, trailer = _words(data)
    groups = [[_tm_word(w) for w in words[i:i + 16]] for i in range(0, len(words) - len(words) % 16, 16)]
    rest = words[len(groups) * 16:]
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'metatiles',
                    'note': '4x4 groups of SNES tilemap words, 32 bytes per metatile',
                    'metatiles': groups, 'tail_words': [_tm_word(w) for w in rest],
                    'trailer': _hex(trailer)})
    return js, [js]


def decode_metatiles(kind, data, outdir, stem):
    if stem.startswith('level_map'):
        return _decode_map(data, outdir, stem)
    return _decode_metatiles(data, outdir, stem)


def _decode_map(data, outdir, stem):
    words, trailer = _words(data)
    dims = MAP_DIMS.get(stem)
    js = os.path.join(outdir, stem + '.json')
    if dims and dims[0] * dims[1] == len(words):
        cols, rows = dims
        grid = [[_map_word(words[c * rows + r]) for r in range(rows)] for c in range(cols)]
        _save_json(js, {'kind': 'map', 'format': 'level_map',
                        'note': 'column-major u16 metatile indices; bit14 = hflip, bit15 = vflip',
                        'columns': cols, 'rows': rows, 'grid': grid, 'trailer': _hex(trailer)})
    else:
        _save_json(js, {'kind': 'map', 'format': 's16_table',
                        'note': 'code-referenced 16-bit table (camera/parallax/scroll curve or '
                                'per-mode parameter pair), not a level map',
                        'values': [_s16(w) for w in words], 'trailer': _hex(trailer)})
    return js, [js]


def decode_map(kind, data, outdir, stem):
    return _decode_map(data, outdir, stem)


def _encode_words(meta):
    fmt = meta.get('format') or meta.get('kind')
    out = bytearray()
    if fmt == 'level_map':
        cols, rows = meta['columns'], meta['rows']
        for c in range(cols):
            for r in range(rows):
                out += struct.pack('<H', _map_pack(meta['grid'][c][r]))
    elif fmt == 's16_table':
        for v in meta['values']:
            out += struct.pack('<h', v)
    elif fmt == 'metatiles':
        for g in meta['metatiles']:
            for e in g:
                out += struct.pack('<H', _tm_pack(e))
        for e in meta.get('tail_words', []):
            out += struct.pack('<H', _tm_pack(e))
    elif fmt == 'tilemap':
        for e in meta['entries']:
            out += struct.pack('<H', _tm_pack(e))
    else:
        raise CodecError(f'unknown word-table format {fmt!r}')
    out += _unhex(meta.get('trailer', ''))
    return bytes(out)


def encode_wordtable(kind, primary, outpath):
    with open(outpath, 'wb') as fp:
        fp.write(_encode_words(_load_json(primary)))


# ======================================================================================
# hdma  <->  JSON
# ======================================================================================

def decode_hdma(kind, data, outdir, stem):
    js = os.path.join(outdir, stem + '.json')
    entries, pos, ok = [], 0, False
    while pos + 3 <= len(data):
        lines = data[pos]
        if lines == 0:
            pos += 1
            ok = True
            break
        entries.append({'lines': lines, 'pointer': _u16(data, pos + 1)})
        pos += 3
    if ok and not stem.startswith('wave'):
        _save_json(js, {'kind': 'hdma', 'format': 'indirect',
                        'note': 'HDMA table, indirect mode ($48): 3-byte entries '
                                '{line count, 16-bit pointer}, 0 terminates',
                        'entries': entries, 'trailer': _hex(data[pos:])})
    else:
        words, trailer = _words(data)
        _save_json(js, {'kind': 'hdma', 'format': 's16_table',
                        'note': 'signed 16-bit table used as an HDMA source (wave curve)',
                        'values': [_s16(w) for w in words], 'trailer': _hex(trailer)})
    return js, [js]


def encode_hdma(kind, primary, outpath):
    meta = _load_json(primary)
    if meta.get('format') == 'indirect':
        out = bytearray()
        for e in meta['entries']:
            out += bytes((e['lines'] & 0xFF,)) + struct.pack('<H', e['pointer'])
        out += b'\x00'
        out += _unhex(meta.get('trailer', ''))
        with open(outpath, 'wb') as fp:
            fp.write(out)
    else:
        encode_wordtable(kind, primary, outpath)


# ======================================================================================
# anim_script / anim_table / sprite_table / entity_table  <->  JSON records
# ======================================================================================

ANIM_MODE = {0: 'none', 1: 'on_frame_entry', 2: 'every_tick'}


def decode_anim_script(kind, data, outdir, stem):
    n = len(data) // 8
    recs = []
    for i in range(n):
        o = i * 8
        dur = _u16(data, o + 4)
        rec = {'callback': _u16(data, o), 'mode': _u16(data, o + 2),
               'duration': dur, 'frame': _u16(data, o + 6)}
        rec['mode_name'] = ANIM_MODE.get(rec['mode'], 'unknown')
        rec['duration_name'] = {0xFFFE: 'loop_to_frame_0', 0xFFFF: 'switch_to_anim'}.get(dur, 'ticks')
        recs.append(rec)
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'anim_script',
                    'note': '8-byte records {callback:u16, mode:u16, duration:u16, frame:u16}; '
                            'mode/duration names are derived and not read back',
                    'records': recs, 'trailer': _hex(data[n * 8:])})
    return js, [js]


def encode_anim_script(kind, primary, outpath):
    meta = _load_json(primary)
    out = bytearray()
    for r in meta['records']:
        out += struct.pack('<HHHH', r['callback'], r['mode'], r['duration'], r['frame'])
    out += _unhex(meta.get('trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


def decode_anim_table(kind, data, outdir, stem):
    words, trailer = _words(data)
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'anim_table',
                    'note': 'animation id (even byte index) -> script offset in bank $C4',
                    'entries': words, 'trailer': _hex(trailer)})
    return js, [js]


def encode_anim_table(kind, primary, outpath):
    meta = _load_json(primary)
    out = bytearray()
    for w in meta['entries']:
        out += struct.pack('<H', w)
    out += _unhex(meta.get('trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


def decode_sprite_table(kind, data, outdir, stem):
    n = len(data) // 4
    recs = [{'ptr': _u16(data, 4 * i), 'bank': data[4 * i + 2],
             'y_bias': data[4 * i + 3] - 256 if data[4 * i + 3] > 127 else data[4 * i + 3]}
            for i in range(n)]
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': kind,
                    'note': '4-byte records {ptr16, bank, y_bias(signed)}; frame id = 2 * record index',
                    'records': recs, 'trailer': _hex(data[n * 4:])})
    return js, [js]


def encode_sprite_table(kind, primary, outpath):
    meta = _load_json(primary)
    out = bytearray()
    for r in meta['records']:
        out += struct.pack('<H', r['ptr']) + bytes((r['bank'] & 0xFF, r['y_bias'] & 0xFF))
    out += _unhex(meta.get('trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# sprite_frame  <->  assembled-frame PNG + JSON sidecar
# ======================================================================================
#
# Live format (docs/data_formats.md 1b, sub_C0A538 / oam_emit_frame_2row at $C0A772):
#   header  n1, n2, tile_off, unk1, unk2, ntiles1, vram_off, ntiles2|flags
#   n1+n2   2-byte OAM records {x, y}
#   tiles   (ntiles1 + (ntiles2 & 0x7F)) * 32 bytes of 4bpp tiles
#   trailer 0-40 bytes no traced code reads
# Every sprite is 16x16 (the size bit from data_C0A6D3/data_C0A6D7 is always set), and the
# emitter walks tile numbers with `tile += 2; if tile & 0x10: tile += 0x10`, i.e. a
# 16-tile-wide VRAM grid where a sprite owns tiles t, t+1, t+16, t+17.  Group 1 starts at
# tile 0, group 2 at header byte 2 (tile_off); group 1's blob occupies grid slots
# 0..ntiles1-1 and group 2's starts at grid slot vram_off (header byte 6).

TILE_SEQ_STEP = 0x10


def _next_tile(t):
    t += 2
    if t & 0x10:
        t += 0x10
    return t


def _frame_layout(data):
    """Parse the live header; raise CodecError if the frame does not chain."""
    if len(data) < 8:
        raise CodecError('short frame')
    n1, n2, tile_off, unk1, unk2, ntiles1, vram_off, flags = data[0:8]
    ntiles2 = flags & 0x7F
    noam = n1 + n2
    tiles_start = 8 + 2 * noam
    ntiles = ntiles1 + ntiles2
    tiles_end = tiles_start + 32 * ntiles
    if tiles_end > len(data):
        raise CodecError('tile blob runs past the asset')
    trailer_len = len(data) - tiles_end
    if trailer_len > 64:
        # documented live-format trailers are 2-40 bytes; anything larger means the
        # 8-byte header does not apply (the alternate-format regions).
        raise CodecError('trailer too long for the live format')
    oam = []
    t = 0
    for i in range(n1):
        oam.append((data[8 + 2 * i], data[9 + 2 * i], t))
        t = _next_tile(t)
    t = tile_off
    for i in range(n1, noam):
        oam.append((data[8 + 2 * i], data[9 + 2 * i], t))
        t = _next_tile(t)
    grid = {}
    for i in range(ntiles1):
        grid[i] = i
    for i in range(ntiles2):
        grid[vram_off + i] = ntiles1 + i
    return {'n1': n1, 'n2': n2, 'tile_off': tile_off, 'unk1': unk1, 'unk2': unk2,
            'ntiles1': ntiles1, 'vram_off': vram_off, 'flags_byte': flags,
            'ntiles2': ntiles2, 'oam': oam, 'grid': grid, 'ntiles': ntiles,
            'tiles_start': tiles_start, 'tiles_end': tiles_end}


def decode_sprite_frame(kind, data, outdir, stem):
    try:
        L = _frame_layout(data)
    except CodecError:
        # the two alternate-format regions (1CC6AA / 1F2E14) and the ROM tail: the live
        # header does not apply and their own header semantics are not established, so
        # they are kept verbatim (documented in docs/data_formats.md).
        blob = os.path.join(outdir, stem + '.raw.bin')
        with open(blob, 'wb') as fp:
            fp.write(data)
        js = os.path.join(outdir, stem + '.json')
        _save_json(js, {'kind': 'sprite_frame', 'format': 'raw',
                        'note': 'alternate-format frames, not read by the live code; header '
                                'semantics uncertain, so the bytes are kept verbatim',
                        'bytes_file': os.path.basename(blob), 'size': len(data)})
        return js, [js, blob]

    tiles = [tile_to_pixels(data, L['tiles_start'] + 32 * i, 4) for i in range(L['ntiles'])]

    # canvas bounding box over the 16x16 sprites (frame-local, x/y are raw header bytes)
    if L['oam']:
        ox = min(r[0] for r in L['oam'])
        oy = min(r[1] for r in L['oam'])
        cw = max(r[0] for r in L['oam']) + 16 - ox
        chh = max(r[1] for r in L['oam']) + 16 - oy
    else:
        ox = oy = cw = chh = 0

    rect = {}
    canvas = bytearray(cw * chh) if cw and chh else bytearray()
    claimed = bytearray(cw * chh) if cw and chh else bytearray()
    for (x, y, t) in L['oam']:
        for dy in (0, 1):
            for dx in (0, 1):
                gi = t + dx + dy * TILE_SEQ_STEP
                bi = L['grid'].get(gi)
                if bi is None:
                    continue
                px, py = x - ox + dx * 8, y - oy + dy * 8
                # sprite positions are not 8-aligned, so claim at pixel granularity:
                # the first tile to own a pixel keeps it and stays readable back.
                if any(claimed[(py + r) * cw + px + c] for r in range(8) for c in range(8)):
                    continue
                for r in range(8):
                    o = (py + r) * cw + px
                    claimed[o:o + 8] = b'\x01' * 8
                _blit(canvas, cw, px, py, tiles[bi])
                if bi not in rect:
                    rect[bi] = (px, py)

    # every tile without an authoritative rect on the canvas goes into a spill strip
    spill = [i for i in range(L['ntiles']) if i not in rect]
    gap = 1 if (chh and spill) else 0
    srows = (len(spill) + 15) // 16
    w = max(cw, 16 * 8 if spill else 0, 8)
    h = chh + gap + srows * 8
    out = bytearray(w * h)
    for y in range(chh):
        out[y * w:y * w + cw] = canvas[y * cw:(y + 1) * cw]
    for k, bi in enumerate(spill):
        px, py = (k % 16) * 8, chh + gap + (k // 16) * 8
        _blit(out, w, px, py, tiles[bi])
        rect[bi] = (px, py)

    png = os.path.join(outdir, stem + '.png')
    write_png_indexed(png, w, h, out, grey_plte(15))
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {
        'kind': 'sprite_frame', 'format': 'live',
        'note': 'PNG = the assembled frame (16x16 sprites at their OAM positions) with any '
                'tile the assembly could not place uniquely spilled into the strip below. '
                '"tiles" gives the authoritative 8x8 source rect of every VRAM tile.',
        'header': {'n1': L['n1'], 'n2': L['n2'], 'tile_off': L['tile_off'],
                   'unk1': L['unk1'], 'unk2': L['unk2'], 'ntiles1': L['ntiles1'],
                   'vram_off': L['vram_off'], 'flags_byte': L['flags_byte']},
        'canvas': {'origin_x': ox, 'origin_y': oy, 'width': cw, 'height': chh,
                   'png_width': w, 'png_height': h, 'spill_tiles': len(spill)},
        'oam': [{'x': x, 'y': y, 'tile_number': t} for (x, y, t) in L['oam']],
        'tiles': [{'i': i, 'x': rect[i][0], 'y': rect[i][1]} for i in range(L['ntiles'])],
        'trailer': _hex(data[L['tiles_end']:]),
    })
    return png, [png, js]


def encode_sprite_frame(kind, primary, outpath):
    base = primary
    for ext in ('.png', '.json'):
        if base.endswith(ext):
            base = base[:-len(ext)]
    js = base + '.json'
    meta = _load_json(js)
    if meta.get('format') == 'raw':
        blob = os.path.join(os.path.dirname(js), meta['bytes_file'])
        with open(blob, 'rb') as fp:
            data = fp.read()
        with open(outpath, 'wb') as fp:
            fp.write(data)
        return
    hdr = meta['header']
    w, h, px = read_png_indexed(base + '.png')
    out = bytearray((hdr['n1'], hdr['n2'], hdr['tile_off'], hdr['unk1'],
                     hdr['unk2'], hdr['ntiles1'], hdr['vram_off'], hdr['flags_byte']))
    for r in meta['oam']:
        out += bytes((r['x'] & 0xFF, r['y'] & 0xFF))
    for t in meta['tiles']:
        out += pixels_to_tile(_grab(px, w, t['x'], t['y']), 4)
    out += _unhex(meta.get('trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# sprite_frame_alt  <->  assembled-frame PNG + JSON sidecar
# ======================================================================================
#
# Alternate format (docs/data_formats.md 1b-alt): unreferenced by any live code or table, so
# there is no traced emitter to follow. Frame boundaries were established structurally by
# tools/gen_assets.py's parse_sprite_frame_alt_region (a header-scan chain walk: the 8 bytes
# before a maximal run of >=8 consecutive 3-byte {x,y,attr} records are a frame's header, and
# the frame runs to the next one's header), which reproduces exactly 82 frames in
# 1CC6AA-1F0000 and 31 in 1F2E14-1FFEE5: 113 total, matching the manual header-scan count.
# Each config/assets.txt sprite_frame_alt asset is already exactly one such frame (or, for
# frame_alt_tail, a partial one truncated by the ROM's end), so decode only has to re-run the
# same record-run scan locally to recover n; it always reproduces the same n the asset was
# split on, since that scan is a pure function of the frame's own bytes.
#
#   header  8 bytes; byte 3 is 0x00 in every frame observed, the rest correlate loosely with
#           the record/tile counts but not through one clean invertible formula (kept verbatim,
#           not decoded, so nothing is lost)
#   records n x 3 bytes {x, y, attr}; attr is a standard SNES OBJ low-attribute byte,
#           vhoopppN: bit7 v-flip, bit6 h-flip, bits5-4 priority, bits3-1 palette, bit0 tile
#           index bit 8. Decomposed losslessly below (the 5 fields cover all 8 bits with no
#           overlap, so decode+encode round-trips any byte value even though the corpus only
#           uses 0x1E/0x20, i.e. v=h=0 throughout: no observed frame is flipped)
#   tiles   4bpp, 32 bytes each; the tile *count* is not stored redundantly anywhere useful, so
#           it is "whatever floors evenly out of the bytes left after the header and
#           records", with 0-31 leftover bytes (rare) kept as a trailer; same convention as
#           the live format's undecoded trailer
#
# Tile-to-sprite assignment: the live format's known rule (4 tiles/16x16 sprite, `tile += 2`
# wrapping every 16) does not fit here: these frames average ~2.4 tiles per record, far
# short of 4/record, so each record is instead assigned exactly one 8x8 tile, in sequence
# (record i -> tile i); any tiles beyond the record count are unclaimed and, as in the live
# decoder, spilled into the strip below the canvas. This is a rendering guess, unvalidated by
# any code (nothing reads this format), but it does not affect round-trip exactness: the
# `tiles` array in the sidecar is authoritative, exactly like decode_sprite_frame.

ALT_ATTR_LO, ALT_ATTR_HI = 0x1C, 0x22


def _alt_record_run(data, off, limit=200):
    n = 0
    while n < limit:
        p = off + n * 3
        if p + 2 >= len(data) or not (ALT_ATTR_LO <= data[p + 2] <= ALT_ATTR_HI):
            break
        n += 1
    return n


def _alt_attr_decode(attr):
    return {'vflip': bool(attr & 0x80), 'hflip': bool(attr & 0x40),
            'priority': (attr >> 4) & 3, 'palette': (attr >> 1) & 7, 'name_bit': attr & 1}


def _alt_attr_encode(a):
    return ((0x80 if a['vflip'] else 0) | (0x40 if a['hflip'] else 0) |
            ((a['priority'] & 3) << 4) | ((a['palette'] & 7) << 1) | (a['name_bit'] & 1))


def decode_sprite_frame_alt(kind, data, outdir, stem):
    if len(data) < 8:
        raise CodecError('short alt frame')
    header = data[0:8]
    n = _alt_record_run(data, 8)
    rec_end = 8 + 3 * n
    if rec_end > len(data):
        raise CodecError('alt record run runs past the asset')
    tile_bytes = len(data) - rec_end
    ntiles = tile_bytes // 32
    tile_trailer = data[rec_end + 32 * ntiles:]
    oam = [(data[8 + 3 * i], data[8 + 3 * i + 1], data[8 + 3 * i + 2]) for i in range(n)]
    tiles_start = rec_end
    tiles = [tile_to_pixels(data, tiles_start + 32 * i, 4) for i in range(ntiles)]

    if oam:
        ox = min(r[0] for r in oam)
        oy = min(r[1] for r in oam)
        cw = max(r[0] for r in oam) + 8 - ox
        chh = max(r[1] for r in oam) + 8 - oy
    else:
        ox = oy = cw = chh = 0

    rect = {}
    canvas = bytearray(cw * chh) if cw and chh else bytearray()
    claimed = bytearray(cw * chh) if cw and chh else bytearray()
    for i, (x, y, attr) in enumerate(oam):
        if i >= ntiles:
            break
        px, py = x - ox, y - oy
        if any(claimed[(py + r) * cw + px + c] for r in range(8) for c in range(8)):
            continue
        for r in range(8):
            o = (py + r) * cw + px
            claimed[o:o + 8] = b'\x01' * 8
        _blit(canvas, cw, px, py, tiles[i])
        rect[i] = (px, py)

    spill = [i for i in range(ntiles) if i not in rect]
    gap = 1 if (chh and spill) else 0
    srows = (len(spill) + 15) // 16
    w = max(cw, 16 * 8 if spill else 0, 8)
    h = chh + gap + srows * 8
    out = bytearray(w * h)
    for y in range(chh):
        out[y * w:y * w + cw] = canvas[y * cw:(y + 1) * cw]
    for k, bi in enumerate(spill):
        px, py = (k % 16) * 8, chh + gap + (k // 16) * 8
        _blit(out, w, px, py, tiles[bi])
        rect[bi] = (px, py)

    png = os.path.join(outdir, stem + '.png')
    write_png_indexed(png, w, h, out, grey_plte(15))
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {
        'kind': 'sprite_frame_alt', 'format': 'alt',
        'note': 'PNG = a best-effort assembly (one 8x8 tile per OAM record, in order; unclaimed '
                'tiles spilled below) of a frame from the alternate sprite format, unreferenced '
                'by any live code; see docs/data_formats.md 1b-alt. "tiles" is authoritative '
                'for round-tripping regardless of the assembly guess.',
        'header': _hex(header),
        'canvas': {'origin_x': ox, 'origin_y': oy, 'width': cw, 'height': chh,
                   'png_width': w, 'png_height': h, 'spill_tiles': len(spill)},
        'oam': [dict(x=x, y=y, attr=attr, **_alt_attr_decode(attr)) for (x, y, attr) in oam],
        'tiles': [{'i': i, 'x': rect[i][0], 'y': rect[i][1]} for i in range(ntiles)],
        'tile_trailer': _hex(tile_trailer),
    })
    return png, [png, js]


def encode_sprite_frame_alt(kind, primary, outpath):
    base = primary
    for ext in ('.png', '.json'):
        if base.endswith(ext):
            base = base[:-len(ext)]
    meta = _load_json(base + '.json')
    w, h, px = read_png_indexed(base + '.png')
    out = bytearray(_unhex(meta['header']))
    for r in meta['oam']:
        attr = r['attr'] if 'attr' in r else _alt_attr_encode(r)
        out += bytes((r['x'] & 0xFF, r['y'] & 0xFF, attr & 0xFF))
    for t in meta['tiles']:
        out += pixels_to_tile(_grab(px, w, t['x'], t['y']), 4)
    out += _unhex(meta.get('tile_trailer', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# brr  <->  16-bit mono WAV @ 32000 Hz + JSON sidecar
# ======================================================================================

BRR_RATE = 32000


def _clamp16(v):
    return -32768 if v < -32768 else (32767 if v > 32767 else v)


def _brr_pred(filt, p1, p2):
    if filt == 0:
        return 0
    if filt == 1:
        return p1 + ((-p1) >> 4)
    if filt == 2:
        return p1 * 2 + ((-p1 * 3) >> 5) - p2 + (p2 >> 4)
    return p1 * 2 + ((-p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4)


def _brr_step(nib, shift):
    s = nib - 16 if nib >= 8 else nib
    if shift <= 12:
        return (s << shift) >> 1
    return -2048 if s < 0 else 0


def _brr_decode_block(block, p1, p2):
    shift, filt = block[0] >> 4, (block[0] >> 2) & 3
    out = []
    for i in range(8):
        byte = block[1 + i]
        for nib in (byte >> 4, byte & 0xF):
            v = _clamp16(_brr_step(nib, shift) + _brr_pred(filt, p1, p2))
            out.append(v)
            p2, p1 = p1, v
    return out, p1, p2


def _brr_requantise(samples, p1, p2, shift, filt):
    """Recover the 16 nibbles that produced `samples`; None if not uniquely recoverable."""
    nibs = []
    for v in samples:
        pred = _brr_pred(filt, p1, p2)
        hit = None
        for nib in range(16):
            if _clamp16(_brr_step(nib, shift) + pred) == v:
                if hit is not None:
                    return None                  # ambiguous (shift 0/13-15, or clamped)
                hit = nib
        if hit is None:
            return None
        nibs.append(hit)
        p2, p1 = p1, v
    return nibs


def decode_brr(kind, data, outdir, stem):
    loop = _u16(data, 0) if len(data) >= 2 else 0
    length = _u16(data, 2) if len(data) >= 4 else 0
    body = data[4:4 + length]
    trailing = data[4 + length:]
    nblocks = len(body) // 9
    pad = body[nblocks * 9:]

    pcm, blocks, raw_blocks = [], [], {}
    p1 = p2 = 0
    for b in range(nblocks):
        blk = body[b * 9:b * 9 + 9]
        shift, filt = blk[0] >> 4, (blk[0] >> 2) & 3
        samples, np1, np2 = _brr_decode_block(blk, p1, p2)
        nibs = _brr_requantise(samples, p1, p2, shift, filt)
        orig = []
        for byte in blk[1:]:
            orig += [byte >> 4, byte & 0xF]
        if nibs != orig:
            raw_blocks[str(b)] = _hex(blk[1:])
        blocks.append({'shift': shift, 'filter': filt,
                       'loop': (blk[0] >> 1) & 1, 'end': blk[0] & 1})
        pcm += samples
        p1, p2 = np1, np2

    wav = os.path.join(outdir, stem + '.wav')
    with wave.open(wav, 'wb') as fp:
        fp.setnchannels(1)
        fp.setsampwidth(2)
        fp.setframerate(BRR_RATE)
        fp.writeframes(struct.pack(f'<{len(pcm)}h', *pcm) if pcm else b'')
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, {'kind': 'brr',
                    'note': 'WAV holds the decoded PCM; re-encoding re-quantises it with the '
                            'per-block filter/shift below. Blocks listed in raw_nibbles were '
                            'not uniquely recoverable from the PCM (shift 0/13-15, or a '
                            'clamped sample) and keep their original nibbles.',
                    'loop_offset': loop, 'length': length, 'sample_rate': BRR_RATE,
                    'block_count': nblocks, 'sample_count': len(pcm),
                    'blocks': blocks, 'raw_nibbles': raw_blocks,
                    'pad': _hex(pad), 'trailing': _hex(trailing)})
    return wav, [wav, js]


def encode_brr(kind, primary, outpath):
    base = primary
    for ext in ('.wav', '.json'):
        if base.endswith(ext):
            base = base[:-len(ext)]
    meta = _load_json(base + '.json')
    with wave.open(base + '.wav', 'rb') as fp:
        frames = fp.readframes(fp.getnframes())
    pcm = list(struct.unpack(f'<{len(frames)//2}h', frames))
    raw_blocks = meta.get('raw_nibbles', {})
    out = bytearray(struct.pack('<HH', meta['loop_offset'], meta['length']))
    p1 = p2 = 0
    for b, spec in enumerate(meta['blocks']):
        shift, filt = spec['shift'], spec['filter']
        head = (shift << 4) | (filt << 2) | (spec['loop'] << 1) | spec['end']
        raw = raw_blocks.get(str(b))
        if raw is not None:
            blk = bytes((head,)) + _unhex(raw)
        else:
            samples = pcm[b * 16:b * 16 + 16]
            nibs = _brr_requantise(samples, p1, p2, shift, filt)
            if nibs is None:
                raise CodecError(f'block {b}: PCM does not re-quantise with shift={shift} '
                                 f'filter={filt} and no raw_nibbles fallback is stored')
            blk = bytes((head,)) + bytes(((nibs[2 * i] << 4) | nibs[2 * i + 1]) for i in range(8))
        out += blk
        _, p1, p2 = _brr_decode_block(blk, p1, p2)
    out += _unhex(meta.get('pad', ''))
    out += _unhex(meta.get('trailing', ''))
    with open(outpath, 'wb') as fp:
        fp.write(out)


# ======================================================================================
# song / sfx_bank / spc_table  <->  JSON of parsed SPC700 sequence events
# ======================================================================================
#
# Opcode set and event lengths come from spc/spc_map.txt (seq_cmd_table at $0FD8) and the
# `mov $00,#$nn` (tmp0 = total event length) in each handler of spc/driver.asm.
# name, length; length None = context dependent, 0 = control transfer handled separately.
SEQ_OPS = {
    0x00: ('end', 1), 0x01: ('instrument', 2), 0x02: ('volume', 3),
    0x03: ('jump', 3), 0x04: ('call', 4), 0x05: ('return', 1),
    0x06: ('note_length', None), 0x07: ('note_length_inline', 1),
    0x08: ('slide_up', 6), 0x09: ('slide_down', 6), 0x0A: ('slide_off', 1),
    0x0B: ('tempo', 2), 0x0C: ('tempo_add', 2),
    0x0D: ('vibrato', 4), 0x0E: ('vibrato_off', 1), 0x0F: ('vibrato_delay', 5),
    0x10: ('adsr', 3), 0x11: ('master_volume', 3), 0x12: ('finetune', 2),
    0x13: ('transpose', 2), 0x14: ('transpose_add', 2), 0x15: ('echo_setup', 4),
    0x16: ('echo_on', 1), 0x17: ('echo_off', 1), 0x18: ('fir', 9),
    0x19: ('noise_clock', 2), 0x1A: ('noise_on', 1), 0x1B: ('noise_off', 1),
    0x1C: ('set_note_e0', 2), 0x1D: ('set_note_e1', 2), 0x1E: ('volume_presets', 5),
    0x1F: ('echo_delay', 2), 0x20: ('volume_preset', 1), 0x21: ('call_once', 3),
    0x22: ('instrument_full', 8), 0x23: ('volume_mono', 2), 0x24: ('master_percent', 2),
    0x26: ('slide_up2_stale', 5), 0x27: ('slide_down2_stale', 5),
    0x2B: ('gate_on_stale', 1), 0x2C: ('gate_off_stale', 1),
    0x30: ('echo_off_stale', 1), 0x31: ('volume_preset2_stale', 1),
    0x32: ('echo_off_stale2', 1),
}
SEQ_BY_NAME = {v[0]: k for k, v in SEQ_OPS.items()}
SEQ_TRANSFER = {0x03, 0x21, 0x04}
SEQ_STOP = {0x00, 0x03, 0x05}


def _parse_seq_stream(body, start, base, note_len, gate_mode, events, targets):
    """Linear walk of one sequence stream; fills `events[offset] = record`."""
    pos = start
    while 0 <= pos < len(body):
        if pos in events:
            return
        b = body[pos]
        if b >= 0x80:                                     # note / rest / stored note
            n = 1 if note_len else (3 if gate_mode else 2)
            if pos + n > len(body):
                return
            events[pos] = {'len': n, 'rec': {'note': b, 'operands': list(body[pos + 1:pos + n])}}
            pos += n
            continue
        op = SEQ_OPS.get(b)
        if op is None:
            return                                        # null seq_cmd_table slot: stop
        name, ln = op
        if b == 0x06:
            ln = 3 if gate_mode else 2
        if pos + ln > len(body):
            return
        rec = {'cmd': name, 'code': b}
        if b in SEQ_TRANSFER:
            ao = pos + (2 if b == 0x04 else 1)
            addr = _u16(body, ao)
            if b == 0x04:
                rec['count'] = body[pos + 1]
            rec['target'] = f'${addr:04X}'
            off = addr - base
            if 0 <= off < len(body):
                targets.append((off, note_len, gate_mode))
        else:
            rec['operands'] = list(body[pos + 1:pos + ln])
        events[pos] = {'len': ln, 'rec': rec}
        if b == 0x06:
            note_len = body[pos + 1]
        elif b == 0x07:
            note_len = 0
        elif b == 0x2B:
            gate_mode = 1
        elif b == 0x2C:
            gate_mode = 0
        pos += ln
        if b in SEQ_STOP:
            return


def _segments_from_events(body, base, events):
    """Linear coverage of `body`: events where one starts, raw runs elsewhere."""
    segs, pos = [], 0
    cur = None
    while pos < len(body):
        ev = events.get(pos)
        if ev is not None and pos + ev['len'] <= len(body):
            if cur is None:
                cur = {'type': 'events', 'at': pos, 'spc': f'${base + pos:04X}', 'events': []}
                segs.append(cur)
            cur['events'].append(ev['rec'])
            pos += ev['len']
        else:
            cur = None
            run = pos
            while pos < len(body) and (pos not in events or pos + events[pos]['len'] > len(body)):
                pos += 1
            segs.append({'type': 'raw', 'at': run, 'bytes': _hex(body[run:pos])})
    return segs


def _encode_event(rec):
    if 'note' in rec:
        return bytes([rec['note'] & 0xFF] + [o & 0xFF for o in rec.get('operands', [])])
    code = rec.get('code')
    if code is None:
        code = SEQ_BY_NAME[rec['cmd']]
    if 'target' in rec:
        addr = int(rec['target'].lstrip('$'), 16)
        if code == 0x04:
            return bytes((code, rec['count'] & 0xFF, addr & 0xFF, (addr >> 8) & 0xFF))
        return bytes((code, addr & 0xFF, (addr >> 8) & 0xFF))
    return bytes([code & 0xFF] + [o & 0xFF for o in rec.get('operands', [])])


def _encode_segments(segs):
    out = bytearray()
    for s in segs:
        if s['type'] == 'raw':
            out += _unhex(s['bytes'])
        elif s['type'] == 'song_header':
            for p in s['channels']:
                out += struct.pack('<H', int(p.lstrip('$'), 16))
            out += bytes((s['tempo'] & 0xFF, s['tempo2'] & 0xFF))
        elif s['type'] == 'sfx_index':
            out += struct.pack('<H', s['count'])
            for p in s['pointers']:
                out += struct.pack('<H', int(p.lstrip('$'), 16))
        elif s['type'] == 'events':
            for rec in s['events']:
                out += _encode_event(rec)
        else:
            raise CodecError(f'unknown segment type {s["type"]!r}')
    return bytes(out)


def _decode_spc_block(data, stem, kind, mode):
    """A {dest u16, word_count u16, body} upload block: song or sfx bank 1."""
    dest, wcount = _u16(data, 0), _u16(data, 2)
    body = data[4:]
    events, targets, starts = {}, [], []
    head = []
    if mode == 'song' and len(body) >= 18:
        chans = [_u16(body, 2 * i) for i in range(8)]
        head.append({'type': 'song_header', 'at': 0,
                     'channels': [f'${c:04X}' for c in chans],
                     'tempo': body[16], 'tempo2': body[17]})
        starts = [c - dest for c in chans]
        base_body = 18
    elif mode == 'sfx' and len(body) >= 2:
        count = _u16(body, 0)
        n = min(count, max(0, (len(body) - 2) // 2))
        ptrs = [_u16(body, 2 + 2 * i) for i in range(n)]
        head.append({'type': 'sfx_index', 'at': 0, 'count': count,
                     'pointers': [f'${p:04X}' for p in ptrs]})
        starts = [p - dest for p in ptrs]
        base_body = 2 + 2 * n
    else:
        base_body = 0
    work = [(s, 0, 0) for s in starts if 0 <= s < len(body)]
    while work:
        off, nl, gm = work.pop(0)
        _parse_seq_stream(body, off, dest, nl, gm, events, targets)
        work += [t for t in targets]
        targets.clear()
    tail = _segments_from_events(body[base_body:], dest + base_body, events_shift(events, base_body))
    for s in tail:
        s['at'] += base_body
    return {'kind': kind, 'format': 'spc_block',
            'note': 'SPC700 upload block {dest, word_count, body}; body is covered exactly '
                    'once, in order, by the segments below (spc/spc_map.txt opcode table)',
            'dest': f'${dest:04X}', 'word_count': wcount,
            'segments': head + tail}


def events_shift(events, base):
    return {k - base: v for k, v in events.items() if k >= base}


def encode_spc_block(meta):
    dest = int(str(meta['dest']).lstrip('$'), 16)
    return struct.pack('<HH', dest, meta['word_count']) + _encode_segments(meta['segments'])


def _ptr24_json(kind, data, note):
    n = len(data) // 3
    return {'kind': kind, 'format': 'pointer_table_24', 'note': note,
            'pointers': [f'${data[3 * i + 2]:02X}{_u16(data, 3 * i):04X}' for i in range(n)],
            'trailer': _hex(data[n * 3:])}


def _u16_json(kind, data, note):
    words, trailer = _words(data)
    return {'kind': kind, 'format': 'u16_table', 'note': note,
            'words': [f'${w:04X}' for w in words], 'trailer': _hex(trailer)}


def _sample_lists_json(kind, data):
    words, trailer = _words(data)
    lists, cur = [], []
    for w in words:
        if w == 0xFFFF:
            lists.append(cur)
            cur = []
        else:
            cur.append(w)
    return {'kind': kind, 'format': 'sample_lists',
            'note': '$FFFF-terminated u16 sample-number lists, one per song',
            'lists': lists, 'tail': cur, 'trailer': _hex(trailer)}


SPC_LAYOUT = {
    'sample_pointer_table': 'ptr24', 'song_table': 'ptr24', 'sfx_bank2_ptrs': 'ptr24',
    'sample_pointer_tail': 'u16', 'sfx_bank2_blocks': 'u16',
    'sample_lists': 'lists', 'sfx_bank1': 'sfx_block',
}


def decode_spc_generic(kind, data, outdir, stem):
    layout = SPC_LAYOUT.get(stem, 'u16')
    if layout == 'ptr24':
        meta = _ptr24_json(kind, data, '24-bit ROM pointers, little-endian {lo16, bank}')
    elif layout == 'lists':
        meta = _sample_lists_json(kind, data)
    elif layout == 'sfx_block':
        meta = _decode_spc_block(data, stem, kind, 'sfx')
    else:
        meta = _u16_json(kind, data, '16-bit words (pointer/count table)')
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, meta)
    return js, [js]


def decode_song(kind, data, outdir, stem):
    if len(data) < 4:
        js = os.path.join(outdir, stem + '.json')
        _save_json(js, {'kind': kind, 'format': 'u16_table', 'note': 'short block',
                        'words': [], 'trailer': _hex(data)})
        return js, [js]
    js = os.path.join(outdir, stem + '.json')
    _save_json(js, _decode_spc_block(data, stem, kind, 'song'))
    return js, [js]


def encode_spc(kind, primary, outpath):
    meta = _load_json(primary)
    fmt = meta.get('format')
    if fmt == 'spc_block':
        out = encode_spc_block(meta)
    elif fmt == 'pointer_table_24':
        out = bytearray()
        for p in meta['pointers']:
            v = int(p.lstrip('$'), 16)
            out += struct.pack('<H', v & 0xFFFF) + bytes(((v >> 16) & 0xFF,))
        out += _unhex(meta.get('trailer', ''))
    elif fmt == 'sample_lists':
        out = bytearray()
        for lst in meta['lists']:
            for w in lst:
                out += struct.pack('<H', w)
            out += b'\xff\xff'
        for w in meta.get('tail', []):
            out += struct.pack('<H', w)
        out += _unhex(meta.get('trailer', ''))
    elif fmt == 'u16_table':
        out = bytearray()
        for w in meta['words']:
            out += struct.pack('<H', int(str(w).lstrip('$'), 16) if isinstance(w, str) else w)
        out += _unhex(meta.get('trailer', ''))
    else:
        raise CodecError(f'unknown SPC format {fmt!r}')
    with open(outpath, 'wb') as fp:
        fp.write(bytes(out))


# ======================================================================================
# dispatch + CLI
# ======================================================================================

DECODERS = {
    'palette': decode_palette,
    'tileset_2bpp': decode_tileset,
    'tileset_4bpp': decode_tileset,
    'tileset_8bpp': decode_tileset,
    'tilemap': decode_tilemap,
    'metatiles': decode_metatiles,
    'map': decode_map,
    'hdma': decode_hdma,
    'anim_script': decode_anim_script,
    'anim_table': decode_anim_table,
    'sprite_table': decode_sprite_table,
    'entity_table': decode_anim_table,
    'sprite_frame': decode_sprite_frame,
    'sprite_frame_alt': decode_sprite_frame_alt,
    'brr': decode_brr,
    'song': decode_song,
    'sfx_bank': decode_spc_generic,
    'spc_table': decode_spc_generic,
}

ENCODERS = {
    'palette': encode_palette,
    'tileset_2bpp': encode_tileset,
    'tileset_4bpp': encode_tileset,
    'tileset_8bpp': encode_tileset,
    'tilemap': encode_wordtable,
    'metatiles': encode_wordtable,
    'map': encode_wordtable,
    'hdma': encode_hdma,
    'anim_script': encode_anim_script,
    'anim_table': encode_anim_table,
    'sprite_table': encode_sprite_table,
    'entity_table': encode_anim_table,
    'sprite_frame': encode_sprite_frame,
    'sprite_frame_alt': encode_sprite_frame_alt,
    'brr': encode_brr,
    'song': encode_spc,
    'sfx_bank': encode_spc,
    'spc_table': encode_spc,
}

KINDS = sorted(DECODERS)


def decode_asset(kind, binpath, outdir):
    """Decode one asset file; returns (primary_editable_path, [all files written])."""
    if kind not in DECODERS:
        raise CodecError(f'no codec for kind {kind!r}')
    os.makedirs(outdir, exist_ok=True)
    with open(binpath, 'rb') as fp:
        data = fp.read()
    return DECODERS[kind](kind, data, outdir, _stem(binpath))


def encode_asset(kind, primary, outpath):
    if kind not in ENCODERS:
        raise CodecError(f'no codec for kind {kind!r}')
    d = os.path.dirname(os.path.abspath(outpath))
    if d:
        os.makedirs(d, exist_ok=True)
    ENCODERS[kind](kind, primary, outpath)


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        print('\nkinds with a codec: ' + ', '.join(KINDS))
        return 2
    cmd = argv[1]
    if cmd == 'decode' and len(argv) == 5:
        primary, files = decode_asset(argv[2], argv[3], argv[4])
        for f in files:
            print(('* ' if f == primary else '  ') + f)
        return 0
    if cmd == 'encode' and len(argv) == 5:
        encode_asset(argv[2], argv[3], argv[4])
        print(argv[4])
        return 0
    if cmd == 'kinds':
        print('\n'.join(f'{k:14s} {PRIMARY_EXT[k]}' for k in KINDS))
        return 0
    print(__doc__.strip())
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
