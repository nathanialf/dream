#!/usr/bin/env python3
"""Split the user's ROM into data/<half-bank>.bin files (64 x 32 KB) after verifying its SHA-1.

    python3 tools/extract.py baserom/DREAM.sfc data/

Nothing produced here is committed; data/ is gitignored.
"""
import sys, os, hashlib

EXPECTED_SHA1 = '2675d7afe886f20462337aa1ee3aa5c3135fff3a'
SIZE = 0x200000

def main():
    rom_path, outdir = sys.argv[1], sys.argv[2]
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

if __name__ == '__main__':
    main()
