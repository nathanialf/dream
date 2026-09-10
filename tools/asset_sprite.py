#!/usr/bin/env python3
"""Decode a sprite frame asset (data/sprites/frame_NNNN.bin) to prove the boundaries
config/assets.txt derived from the sprite frame table: prints the header fields and OAM
records, and renders the 4bpp tiles (palette index -> greyscale) to a PPM grid, plus an
ASCII preview of the first few tiles on stdout (` .:-=+*#%@ABCDEF` for index 0-15, matching
docs/data_formats.md).

    python3 tools/asset_sprite.py data/sprites/frame_0100.bin build/frame_0100.ppm

Nothing here is committed: run this yourself and inspect build/*.ppm.
"""
import sys

RAMP = ' .:-=+*#%@ABCDEF'


def decode_tile(data, off):
    """One 32-byte SNES 4bpp tile -> 8x8 list of pixel values 0-15."""
    rows = []
    for r in range(8):
        p0 = data[off + 2 * r]
        p1 = data[off + 2 * r + 1]
        p2 = data[off + 16 + 2 * r]
        p3 = data[off + 16 + 2 * r + 1]
        row = []
        for c in range(8):
            bit = 7 - c
            v = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1) | (((p2 >> bit) & 1) << 2) | (((p3 >> bit) & 1) << 3)
            row.append(v)
        rows.append(row)
    return rows


def write_ppm_grid(path, tiles, cols):
    rows = (len(tiles) + cols - 1) // cols
    w, h = cols * 8, rows * 8
    buf = bytearray(w * h)
    for i, tile in enumerate(tiles):
        tx, ty = (i % cols) * 8, (i // cols) * 8
        for r in range(8):
            for c in range(8):
                buf[(ty + r) * w + (tx + c)] = tile[r][c] * 17
    with open(path, 'wb') as fp:
        fp.write(f'P5\n{w} {h}\n255\n'.encode())
        fp.write(bytes(buf))
    return w, h


def main():
    in_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else None
    data = open(in_path, 'rb').read()

    n1, n2, tile_off, unk1, unk2, ntiles1, vram_off, flags_byte = data[0:8]
    ntiles2 = flags_byte & 0x7F
    ntiles = ntiles1 + ntiles2
    noam = n1 + n2

    print(f'{in_path}: {len(data)} bytes')
    print(f'  header: n1={n1} n2={n2} tile_off={tile_off:#04x} unk1={unk1:#04x} unk2={unk2:#04x} '
          f'ntiles1={ntiles1} vram_off={vram_off:#04x} flags_byte={flags_byte:#04x} '
          f'(ntiles2={ntiles2}) -> {noam} OAM records, {ntiles} tiles')

    oam_start = 8
    records = []
    for i in range(noam):
        o = oam_start + i * 2
        if o + 2 > len(data):
            break
        x, y = data[o], data[o + 1]
        records.append((x, y))
    print(f'  OAM records ({len(records)}):', ' '.join(f'({x},{y})' for x, y in records))

    tiles_start = oam_start + noam * 2
    tiles = []
    for i in range(ntiles):
        o = tiles_start + i * 32
        if o + 32 > len(data):
            print(f'  warning: tile {i} would read past end of file ({o + 32} > {len(data)}); stopping')
            break
        tiles.append(decode_tile(data, o))
    trailer = len(data) - (tiles_start + len(tiles) * 32)
    print(f'  decoded {len(tiles)} tiles ({len(tiles) * 32} bytes); {trailer} trailer bytes left over')

    for i, tile in enumerate(tiles[:2]):
        print(f'  tile {i} ASCII preview:')
        for row in tile:
            print('    ' + ''.join(RAMP[v] for v in row))

    if out_path and tiles:
        cols = min(16, len(tiles))
        w, h = write_ppm_grid(out_path, tiles, cols)
        print(f'  wrote {w}x{h} PPM grid ({cols} cols x {len(tiles)} tiles) to {out_path}')


if __name__ == '__main__':
    main()
