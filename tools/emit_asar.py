#!/usr/bin/env python3
"""Emit a reassemblable asar project from the trace65816 results.

    python3 tools/emit_asar.py DREAM.sfc src/ data/

Writes src/main.asm (+ per-bank files). No ROM bytes are placed in src/: every data run is an
`incbin` into a file that tools/extract.py splits out of the user's own ROM. Where a run
coincides exactly with a named asset from config/assets.txt it is `incbin "../data/<asset>"`
(whole file); where a run is a sub-range of one asset it is `incbin "../data/<asset>":$lo..$hi`
(offsets into that asset file); runs inside the code-embedded tables of 008000-00C00D and
018000-01841A (interleaved with real 65816 instructions) keep the original
`incbin "../data/<half-bank>.bin":$lo..$hi` half-bank form, and any run that still straddles
more than one asset (should not happen once asset boundaries are clipped to data-run
boundaries below) also falls back to the half-bank form.
"""
import sys, os, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace65816 as t
import gen_assets

# 65816 code lives here, interleaved with embedded data tables; leave these as half-bank ranges
# per the phase-3 spec (they are not named assets in config/assets.txt in any useful sense).
CODE_HALFBANK_RANGES = [(0x008000, 0x00C00D), (0x018000, 0x01841A)]

ASSETS = []        # sorted list of (start, end, path)
ASSET_STARTS = []  # parallel list of starts, for bisect
FALLBACKS = [0]
WHOLE = [0]
SUBRANGE = [0]

def in_code_halfbank(f):
    return any(lo <= f < hi for lo, hi in CODE_HALFBANK_RANGES)

def asset_at(f):
    """Return (start, end, path) of the named asset covering file offset f, or None."""
    if not ASSET_STARTS or in_code_halfbank(f):
        return None
    i = bisect.bisect_right(ASSET_STARTS, f) - 1
    if i < 0:
        return None
    a = ASSETS[i]
    return a if a[0] <= f < a[1] else None

def load_assets(assets_path):
    global ASSETS, ASSET_STARTS
    rows = gen_assets.parse_assets_file(assets_path)
    ASSETS = [(s, e, p) for (s, e, k, p, n) in rows]
    ASSET_STARTS = [a[0] for a in ASSETS]


def fmt_imm(f, size):
    if size == 2: return f'#${t.rd8(f+1):02X}'
    return f'#${t.rd16(f+1):04X}'

def label_at(fo):
    return t.labels.get(fo)

def sym16(fo, bank):
    """Symbolic 16-bit reference to file offset fo from code in bank (same bank), else numeric."""
    n = label_at(fo)
    return n if n else f'${fo & 0xFFFF:04X}'

def sym24(a):
    """Symbolic 24-bit reference to CPU address a (may use $80/$81 mirror bank bytes)."""
    fo = t.addr2file(a)
    n = label_at(fo) if fo is not None else None
    if n is None: return f'${a:06X}'
    canon = t.file2addr(fo)
    if canon == a: return n
    # same ROM bytes referenced through a mirror bank: keep the bank byte the ROM used
    return f'${a >> 16:02X}0000+({n}&$FFFF)'

def emit_insn(ins, bank):
    f, mn, mode, size = ins.f, ins.op, ins.mode, ins.size
    if mode == 'mvn':      # asar writes the two bank bytes in source order: opcode, dst, src
        return f'{mn} ${t.ROM[f+1]:02X},${t.ROM[f+2]:02X}', None
    if mn in ('brk', 'cop', 'wdm'):
        return f'{mn} #${t.ROM[f+1]:02X}', None
    if mode in ('imp', 'acc'): return mn, None
    if mode in ('immM', 'immX', 'imm8'):
        w = 'b' if size == 2 else 'w'
        return f'{mn}.{w} {fmt_imm(f, size)}', None
    v8 = t.rd8(f+1); v16 = t.rd16(f+1)
    def ram(v):
        return t.RAM_LABELS.get(v, f'${v:02X}')
    if mode == 'dp':   return f'{mn}.b {ram(v8)}', None
    if mode == 'dpx':  return f'{mn}.b {ram(v8)},x', None
    if mode == 'dpy':  return f'{mn}.b {ram(v8)},y', None
    if mode == 'idp':  return f'{mn}.b ({ram(v8)})', None
    if mode == 'idpx': return f'{mn}.b ({ram(v8)},x)', None
    if mode == 'idpy': return f'{mn}.b ({ram(v8)}),y', None
    if mode == 'ildp': return f'{mn}.b [{ram(v8)}]', None
    if mode == 'ildpy':return f'{mn}.b [{ram(v8)}],y', None
    if mode == 'sr':   return f'{mn}.b ${v8:02X},s', None
    if mode == 'isry': return f'{mn}.b (${v8:02X},s),y', None
    if mode in ('rel', 'rell'):
        tf = t.addr2file(ins.target)
        n = label_at(tf) if tf is not None else None
        return f'{mn} {n}' if n else f'{mn} ${ins.target & 0xFFFF:04X}', None
    if mode in ('abs', 'absx', 'absy', 'iabs', 'iabsx', 'ilabs'):
        if mn in ('jsr', 'jmp'):
            tf = t.addr2file((bank << 16) | v16)
            if mode == 'abs':   return f'{mn}.w {sym16(tf, bank)}', None
            if mode == 'iabs':  return f'{mn}.w (${v16:04X})', None
            if mode == 'iabsx': return f'{mn}.w ({sym16(tf, bank)},x)', None
            if mode == 'ilabs': return f'{mn}.w [${v16:04X}]', None
        if mn == 'jml' and mode == 'ilabs': return f'jml.w [${v16:04X}]', None
        if mn == 'pea': return f'pea.w ${v16:04X}', None
        if v16 in t.REGS: name = t.REGS[v16]
        elif v16 < 0x2000 and v16 in t.RAM_LABELS: name = t.RAM_LABELS[v16]
        elif v16 >= 0x8000:
            tf = t.addr2file((bank << 16) | v16)
            name = label_at(tf) or f'${v16:04X}'
        else: name = f'${v16:04X}'
        suf = {'absx': ',x', 'absy': ',y'}.get(mode, '')
        return f'{mn}.w {name}{suf}', None
    if mode in ('long', 'longx'):
        v24 = t.rd24(f+1)
        if mn in ('jsl', 'jml'): return f'{mn}.l {sym24(v24)}', None
        suf = ',x' if mode == 'longx' else ''
        return f'{mn}.l {sym24(v24)}{suf}', None
    raise RuntimeError(f'unhandled {mn} {mode}')

def half_name(f):
    return f'{f >> 15:02X}.bin'          # data/00.bin = file 0x000000-0x007FFF, data/01.bin = 0x008000-..., ...

def main():
    rom, srcdir = sys.argv[1], sys.argv[2]
    sys.argv = ['trace65816', rom, os.path.join(srcdir, '..', 'out')]
    t.main()
    os.makedirs(srcdir, exist_ok=True)
    assets_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config', 'assets.txt')
    if os.path.exists(assets_path):
        load_assets(assets_path)
    ROM, N = t.ROM, t.ROMSIZE
    bank_files = {}
    def out(bank):
        if bank not in bank_files:
            p = os.path.join(srcdir, f'bank_{bank:02X}.asm')
            bank_files[bank] = open(p, 'w')
            bank_files[bank].write(f'; bank ${bank:02X}  (file ${(bank & 0x3F) << 16:06X})\n')
        return bank_files[bank]

    f = 0
    last_org = None
    data_files = 0
    while f < N:
        bank = 0xC0 | (f >> 16)
        if bank not in bank_files: last_org = None      # every bank file starts with its own org
        fp = out(bank)
        if last_org != f:
            fp.write(f'\norg ${t.file2addr(f):06X}\n');
        if f in t.insns:
            ins = t.insns[f]
            n = label_at(f)
            if n: fp.write(f'\n{n}:\n')
            body, comment = emit_insn(ins, bank)
            c = f'   ; {t.file2addr(f):06X} m{ins.m}x{ins.x}' + (f' {comment}' if comment else '')
            fp.write(f'    {body:<36}{c}\n')
            f += ins.size; last_org = f
            continue
        if f in t.tables:
            n = label_at(f)
            if n: fp.write(f'\n{n}:\n')
            for e in t.tables[f]:
                fp.write(f'    dw {label_at(e) or "$%04X" % (t.file2addr(e) & 0xFFFF)}\n')
            f += 2 * len(t.tables[f]); last_org = f
            continue
        # data run: up to next instruction/table/label (and, outside the code-embedded-table
        # ranges, up to the end of the named asset covering `start` -- this keeps every run a
        # whole-file or sub-range reference into exactly one asset, never straddling two).
        start = f
        asset = asset_at(start)
        bank_end = (start | 0x7FFF) + 1      # asar refuses incbin/db runs that cross a 32K bank-half border
        run_end = min(bank_end, asset[1]) if asset else bank_end
        f += 1
        while f < run_end and f not in t.insns and f not in t.tables and f not in t.labels: f += 1
        n = label_at(start)
        if n: fp.write(f'\n{n}:\n')
        size = f - start
        if asset and asset[0] <= start and f <= asset[1]:
            astart, aend, apath = asset
            if start == astart and f == aend:
                fp.write(f'    incbin "../{apath}"' + ' ' * 24 + f'; {size} bytes (whole asset)\n')
                WHOLE[0] += 1
            else:
                lo, hi = start - astart, f - astart
                fp.write(f'    incbin "../{apath}":${lo:04X}..${hi:04X}' + ' ' * 8 + f'; {size} bytes\n')
                SUBRANGE[0] += 1
        else:
            if asset:   # run touches this asset but also spills past its end: straddle fallback
                FALLBACKS[0] += 1
            lo = start & 0x7FFF
            fp.write(f'    incbin "../data/{half_name(start)}":${lo:04X}..${lo + size:04X}      ; {size} bytes\n')
        data_files += 1
        last_org = f
    for fp in bank_files.values(): fp.close()
    with open(os.path.join(srcdir, 'main.asm'), 'w') as fp:
        fp.write('; Dream: Land of Giants (SNES prototype) - reassemblable disassembly\n')
        fp.write('; build: make (see README.md); data/ is produced by tools/extract.py from your own ROM\n')
        fp.write('hirom\n\n')
        fp.write('; RAM / register symbols\n')
        for a, n in sorted(t.RAM_LABELS.items()):
            fp.write(f'{n} = ${a:04X}\n')
        for a, n in sorted(t.REGS.items()):
            fp.write(f'{n} = ${a:04X}\n')
        fp.write('\n')
        for bank in sorted(bank_files):
            fp.write(f'incsrc "bank_{bank:02X}.asm"\n')
    print(f'banks={len(bank_files)} data_runs={data_files} '
          f'incbin_whole={WHOLE[0]} incbin_subrange={SUBRANGE[0]} incbin_fallback={FALLBACKS[0]}')

if __name__ == '__main__':
    main()
