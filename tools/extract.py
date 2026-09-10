#!/usr/bin/env python3
"""Split the user's ROM into data/<half-bank>.bin files (64 x 32 KB) after verifying its SHA-1,
then write the named per-asset files listed in config/assets.txt.

    python3 tools/extract.py baserom/DREAM.sfc data/

Nothing produced here is committed; data/ is gitignored (both the half-bank files, which src/
still depends on, and the per-asset files under data/<kind-dir>/, which are the phase-3 named
extraction: config/assets.txt is checked in as derived text, the bytes it names are not).
"""
import sys, os, hashlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_assets

EXPECTED_SHA1 = '2675d7afe886f20462337aa1ee3aa5c3135fff3a'
SIZE = 0x200000

def write_asset_files(rom, outdir, assets_path):
    assets = gen_assets.parse_assets_file(assets_path)
    gen_assets.verify_assets(assets)
    root = os.path.normpath(os.path.join(os.path.dirname(assets_path), '..'))
    for (start, end, kind, path, note) in assets:
        full = os.path.join(root, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as fp:
            fp.write(rom[start:end])
    print(f'extracted {len(assets)} named assets under data/')

def main():
    rom_path, outdir = sys.argv[1], sys.argv[2]
    root = os.path.dirname(os.path.abspath(__file__)) + '/..'
    assets_path = os.path.join(root, 'config', 'assets.txt')
    rom = open(rom_path, 'rb').read()
    if len(rom) == SIZE + 512:            # copier header
        rom = rom[512:]
    if len(rom) != SIZE:
        sys.exit(f'{rom_path}: expected {SIZE} bytes, got {len(rom)}')
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_SHA1:
        sys.exit(f'{rom_path}: sha1 {sha1} does not match {EXPECTED_SHA1}')
    os.makedirs(outdir, exist_ok=True)
    for i in range(SIZE // 0x8000):
        with open(os.path.join(outdir, f'{i:02X}.bin'), 'wb') as fp:
            fp.write(rom[i * 0x8000:(i + 1) * 0x8000])
    print(f'extracted {SIZE // 0x8000} half-bank files to {outdir}')
    if os.path.exists(assets_path):
        write_asset_files(rom, outdir, assets_path)
    else:
        print(f'note: {assets_path} not found, skipping named-asset extraction')

if __name__ == '__main__':
    main()
