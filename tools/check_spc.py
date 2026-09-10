#!/usr/bin/env python3
"""Compare the assembled SPC RAM image with the loader/driver blocks in the ROM data files."""
import sys, glob, os
img = open(sys.argv[1], 'rb').read()
# file 0x20000.. lives in data/020000.bin (bank $C2 low half) after `make regen`
p = os.path.join(os.path.dirname(__file__), '..', 'data', '04.bin')   # file 0x020000-0x027FFF
if not os.path.exists(p): sys.exit('data/04.bin missing: run make extract')
src = open(p, 'rb').read()
loader, driver = src[0:0x88], src[0x88:0x88 + 0x699 * 2]
ok = img[0x4D8:0x4D8 + 0x88] == loader and img[0x560:0x560 + len(driver)] == driver
print('spc loader+driver:', 'OK' if ok else 'MISMATCH')
sys.exit(0 if ok else 1)
