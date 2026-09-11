#!/usr/bin/env python3
"""Recursive-descent 65816 tracer for the Dream: Land of Giants (SNES prototype) ROM (HiROM).

Seeds from the vector table and follows jsr/jsl/jmp/jml/branches, tracking
the M/X flags through rep/sep, and reads jump tables behind `jsr (abs,X)` /
`jmp (abs,X)`. Emits:

  out/codemap.txt     code ranges (file offsets) with subroutine boundaries
  out/symbols.txt     24-bit address, file offset, label
  out/dream.mlb       Mesen2 label file
  out/dream.sym       bsnes-plus style label file
  out/dream.asm       listing: traced code disassembled, gaps as data

Usage: trace65816.py DREAM.sfc out/

The ROM is validated the way tools/extract.py validates it, by size and SHA-1, before
anything is traced. Without that a 512-byte copier header shifts every offset in the
trace by 512, `make regen` emits a src/ built on those offsets, and only the SHA-1 gate
at the end of the rebuild notices.
"""
import sys, os, struct, hashlib
from collections import deque

EXPECTED_SHA1 = '2675d7afe886f20462337aa1ee3aa5c3135fff3a'
EXPECTED_SIZE = 0x200000

ROM = None
ROMSIZE = 0
RAM_LABELS = {}   # wram/direct-page address -> name
SWEEP = [(0x8000, 0xC000), (0x18000, 0x20000)]   # code regions swept for unreferenced routines

# ---------- mapping (HiROM) ----------
def addr2file(a):
    bank = (a >> 16) & 0xFF
    off = a & 0xFFFF
    b = bank & 0x3F
    if bank & 0x40 == 0 and off < 0x8000:
        return None  # system area / WRAM mirror, not ROM
    if bank in (0x7E, 0x7F):
        return None
    f = (b << 16) | off
    return f if f < ROMSIZE else None

def file2addr(f):
    return 0xC00000 | f  # canonical HiROM address

def in_rom(a):
    return addr2file(a) is not None

# ---------- opcode table ----------
# mode -> (size, fmt). 'M' / 'X' add 1 when 16-bit.
MODES = {
 'imp':(1,'{m}'), 'acc':(1,'{m} A'),
 'immM':(2,'{m} #${v:02X}'), 'immX':(2,'{m} #${v:02X}'), 'imm8':(2,'{m} #${v:02X}'),
 'dp':(2,'{m} ${v:02X}'), 'dpx':(2,'{m} ${v:02X},X'), 'dpy':(2,'{m} ${v:02X},Y'),
 'idp':(2,'{m} (${v:02X})'), 'idpx':(2,'{m} (${v:02X},X)'), 'idpy':(2,'{m} (${v:02X}),Y'),
 'ildp':(2,'{m} [${v:02X}]'), 'ildpy':(2,'{m} [${v:02X}],Y'),
 'sr':(2,'{m} ${v:02X},S'), 'isry':(2,'{m} (${v:02X},S),Y'),
 'abs':(3,'{m} ${v:04X}'), 'absx':(3,'{m} ${v:04X},X'), 'absy':(3,'{m} ${v:04X},Y'),
 'iabs':(3,'{m} (${v:04X})'), 'iabsx':(3,'{m} (${v:04X},X)'), 'ilabs':(3,'{m} [${v:04X}]'),
 'long':(4,'{m} ${v:06X}'), 'longx':(4,'{m} ${v:06X},X'),
 'rel':(2,'{m} ${v:04X}'), 'rell':(3,'{m} ${v:04X}'),
 'mvn':(3,'{m} ${s:02X},${d:02X}'),
}
OPS = {}
def _op(code, m, mode): OPS[code] = (m, mode)
# generated from the standard 65816 matrix
T = """
00 brk imm8|01 ora idpx|02 cop imm8|03 ora sr|04 tsb dp|05 ora dp|06 asl dp|07 ora ildp|08 php imp|09 ora immM|0A asl acc|0B phd imp|0C tsb abs|0D ora abs|0E asl abs|0F ora long
10 bpl rel|11 ora idpy|12 ora idp|13 ora isry|14 trb dp|15 ora dpx|16 asl dpx|17 ora ildpy|18 clc imp|19 ora absy|1A inc acc|1B tcs imp|1C trb abs|1D ora absx|1E asl absx|1F ora longx
20 jsr abs|21 and idpx|22 jsl long|23 and sr|24 bit dp|25 and dp|26 rol dp|27 and ildp|28 plp imp|29 and immM|2A rol acc|2B pld imp|2C bit abs|2D and abs|2E rol abs|2F and long
30 bmi rel|31 and idpy|32 and idp|33 and isry|34 bit dpx|35 and dpx|36 rol dpx|37 and ildpy|38 sec imp|39 and absy|3A dec acc|3B tsc imp|3C bit absx|3D and absx|3E rol absx|3F and longx
40 rti imp|41 eor idpx|42 wdm imm8|43 eor sr|44 mvp mvn|45 eor dp|46 lsr dp|47 eor ildp|48 pha imp|49 eor immM|4A lsr acc|4B phk imp|4C jmp abs|4D eor abs|4E lsr abs|4F eor long
50 bvc rel|51 eor idpy|52 eor idp|53 eor isry|54 mvn mvn|55 eor dpx|56 lsr dpx|57 eor ildpy|58 cli imp|59 eor absy|5A phy imp|5B tcd imp|5C jml long|5D eor absx|5E lsr absx|5F eor longx
60 rts imp|61 adc idpx|62 per rell|63 adc sr|64 stz dp|65 adc dp|66 ror dp|67 adc ildp|68 pla imp|69 adc immM|6A ror acc|6B rtl imp|6C jmp iabs|6D adc abs|6E ror abs|6F adc long
70 bvs rel|71 adc idpy|72 adc idp|73 adc isry|74 stz dpx|75 adc dpx|76 ror dpx|77 adc ildpy|78 sei imp|79 adc absy|7A ply imp|7B tdc imp|7C jmp iabsx|7D adc absx|7E ror absx|7F adc longx
80 bra rel|81 sta idpx|82 brl rell|83 sta sr|84 sty dp|85 sta dp|86 stx dp|87 sta ildp|88 dey imp|89 bit immM|8A txa imp|8B phb imp|8C sty abs|8D sta abs|8E stx abs|8F sta long
90 bcc rel|91 sta idpy|92 sta idp|93 sta isry|94 sty dpx|95 sta dpx|96 stx dpy|97 sta ildpy|98 tya imp|99 sta absy|9A txs imp|9B txy imp|9C stz abs|9D sta absx|9E stz absx|9F sta longx
A0 ldy immX|A1 lda idpx|A2 ldx immX|A3 lda sr|A4 ldy dp|A5 lda dp|A6 ldx dp|A7 lda ildp|A8 tay imp|A9 lda immM|AA tax imp|AB plb imp|AC ldy abs|AD lda abs|AE ldx abs|AF lda long
B0 bcs rel|B1 lda idpy|B2 lda idp|B3 lda isry|B4 ldy dpx|B5 lda dpx|B6 ldx dpy|B7 lda ildpy|B8 clv imp|B9 lda absy|BA tsx imp|BB tyx imp|BC ldy absx|BD lda absx|BE ldx absy|BF lda longx
C0 cpy immX|C1 cmp idpx|C2 rep imm8|C3 cmp sr|C4 cpy dp|C5 cmp dp|C6 dec dp|C7 cmp ildp|C8 iny imp|C9 cmp immM|CA dex imp|CB wai imp|CC cpy abs|CD cmp abs|CE dec abs|CF cmp long
D0 bne rel|D1 cmp idpy|D2 cmp idp|D3 cmp isry|D4 pei idp|D5 cmp dpx|D6 dec dpx|D7 cmp ildpy|D8 cld imp|D9 cmp absy|DA phx imp|DB stp imp|DC jml ilabs|DD cmp absx|DE dec absx|DF cmp longx
E0 cpx immX|E1 sbc idpx|E2 sep imm8|E3 sbc sr|E4 cpx dp|E5 sbc dp|E6 inc dp|E7 sbc ildp|E8 inx imp|E9 sbc immM|EA nop imp|EB xba imp|EC cpx abs|ED sbc abs|EE inc abs|EF sbc long
F0 beq rel|F1 sbc idpy|F2 sbc idp|F3 sbc isry|F4 pea abs|F5 sbc dpx|F6 inc dpx|F7 sbc ildpy|F8 sed imp|F9 sbc absy|FA plx imp|FB xce imp|FC jsr iabsx|FD sbc absx|FE inc absx|FF sbc longx
"""
for line in T.strip().splitlines():
    for ent in line.split('|'):
        c, m, mode = ent.split()
        _op(int(c, 16), m, mode)

REGS = {
 0x2100:'INIDISP',0x2101:'OBSEL',0x2102:'OAMADDL',0x2103:'OAMADDH',0x2104:'OAMDATA',0x2105:'BGMODE',0x2106:'MOSAIC',
 0x2107:'BG1SC',0x2108:'BG2SC',0x2109:'BG3SC',0x210A:'BG4SC',0x210B:'BG12NBA',0x210C:'BG34NBA',
 0x210D:'BG1HOFS',0x210E:'BG1VOFS',0x210F:'BG2HOFS',0x2110:'BG2VOFS',0x2111:'BG3HOFS',0x2112:'BG3VOFS',0x2113:'BG4HOFS',0x2114:'BG4VOFS',
 0x2115:'VMAIN',0x2116:'VMADDL',0x2117:'VMADDH',0x2118:'VMDATAL',0x2119:'VMDATAH',0x211A:'M7SEL',
 0x211B:'M7A',0x211C:'M7B',0x211D:'M7C',0x211E:'M7D',0x211F:'M7X',0x2120:'M7Y',0x2121:'CGADD',0x2122:'CGDATA',
 0x2123:'W12SEL',0x2124:'W34SEL',0x2125:'WOBJSEL',0x2126:'WH0',0x2127:'WH1',0x2128:'WH2',0x2129:'WH3',0x212A:'WBGLOG',0x212B:'WOBJLOG',
 0x212C:'TM',0x212D:'TS',0x212E:'TMW',0x212F:'TSW',0x2130:'CGWSEL',0x2131:'CGADSUB',0x2132:'COLDATA',0x2133:'SETINI',
 0x2134:'MPYL',0x2135:'MPYM',0x2136:'MPYH',0x2137:'SLHV',0x2138:'RDOAM',0x2139:'RDVRAML',0x213A:'RDVRAMH',0x213B:'RDCGRAM',
 0x213C:'OPHCT',0x213D:'OPVCT',0x213E:'STAT77',0x213F:'STAT78',0x2140:'APUIO0',0x2141:'APUIO1',0x2142:'APUIO2',0x2143:'APUIO3',
 0x2180:'WMDATA',0x2181:'WMADDL',0x2182:'WMADDM',0x2183:'WMADDH',
 0x4016:'JOYSER0',0x4017:'JOYSER1',0x4200:'NMITIMEN',0x4201:'WRIO',0x4202:'WRMPYA',0x4203:'WRMPYB',0x4204:'WRDIVL',0x4205:'WRDIVH',0x4206:'WRDIVB',
 0x4207:'HTIMEL',0x4208:'HTIMEH',0x4209:'VTIMEL',0x420A:'VTIMEH',0x420B:'MDMAEN',0x420C:'HDMAEN',0x420D:'MEMSEL',
 0x4210:'RDNMI',0x4211:'TIMEUP',0x4212:'HVBJOY',0x4213:'RDIO',0x4214:'RDDIVL',0x4215:'RDDIVH',0x4216:'RDMPYL',0x4217:'RDMPYH',
 0x4218:'JOY1L',0x4219:'JOY1H',0x421A:'JOY2L',0x421B:'JOY2H',0x421C:'JOY3L',0x421D:'JOY3H',0x421E:'JOY4L',0x421F:'JOY4H',
}
for ch in range(8):
    b = 0x4300 + ch*0x10
    for i, n in enumerate(['DMAP','BBAD','A1TL','A1TH','A1B','DASL','DASH','DASB','A2AL','A2AH','NTRL']):
        REGS[b+i] = f'{n}{ch}'

# ---------- tracer state ----------
class Insn:
    __slots__ = ('f','size','op','mode','m','x','target','text')

insns = {}        # file offset -> Insn
covered = bytearray(ROMSIZE) if ROMSIZE else None
labels = {}       # file offset -> name
subs = set()      # file offsets of subroutine entries
locs = set()      # branch/jump targets
datarefs = {}     # file offset -> set of referencing file offsets
tables = {}       # file offset -> list of entry file offsets
conflicts = []
queue = deque()
seen_state = {}
pending_tables = []
table_bases = set()

def rd8(f): return ROM[f]
def rd16(f): return ROM[f] | (ROM[f+1] << 8)
def rd24(f): return ROM[f] | (ROM[f+1] << 8) | (ROM[f+2] << 16)

def decode(f, m, x, bank):
    """Decode one instruction at file offset f with flag state; returns Insn."""
    op = rd8(f)
    mn, mode = OPS[op]
    size, fmt = MODES[mode]
    if mode == 'immM' and not m: size += 1
    if mode == 'immX' and not x: size += 1
    if f + size > ROMSIZE:
        return None
    ins = Insn()
    ins.f, ins.size, ins.op, ins.mode, ins.m, ins.x = f, size, mn, mode, m, x
    ins.target = None
    pc = (f & 0xFFFF)  # PC within bank (HiROM: file low16 == pc)
    v = 0
    if size == 2: v = rd8(f+1)
    elif size == 3: v = rd16(f+1)
    elif size == 4: v = rd24(f+1)
    if mode == 'rel':
        d = v - 0x100 if v >= 0x80 else v
        v = (pc + 2 + d) & 0xFFFF
        ins.target = (bank << 16) | v
    elif mode == 'rell':
        d = v - 0x10000 if v >= 0x8000 else v
        v = (pc + 3 + d) & 0xFFFF
        ins.target = (bank << 16) | v
    elif mode in ('abs',) and mn in ('jsr','jmp'):
        ins.target = (bank << 16) | v
    elif mode == 'long' and mn in ('jsl','jml'):
        ins.target = v
    elif mode in ('iabsx',):
        ins.target = (bank << 16) | v   # jump table address
    if mode == 'mvn':
        ins.text = fmt.format(m=mn, s=rd8(f+1), d=rd8(f+2))
    elif mode in ('immM','immX') and size == 3:
        ins.text = f'{mn} #${v:04X}'
    else:
        ins.text = fmt.format(m=mn, v=v)
    return ins

fn_exits = {}     # function entry file offset -> set of (m,x) exit states
fn_active = set()
FLOW_END = ('rts','rtl','rti','stp')

def push(addr, m, x, kind):
    """Register a code entry. Subs are analyzed via analyze_fn; locs join the current fn."""
    f = addr2file(addr)
    if f is None: return
    if kind == 'sub':
        subs.add(f); analyze_fn(f, m, x)
    else:
        locs.add(f); queue.append((f, m, x))

def analyze_fn(entry, m, x):
    """Trace a subroutine from entry with flag state; returns its set of exit states.
    Exit states are (m, m_known, x, x_known): *_known says the routine set that flag itself
    (rep/sep/plp), so a caller only adopts flags the callee explicitly established."""
    if entry in fn_exits: return fn_exits[entry]
    if entry in fn_active: return set()      # recursion: unknown
    fn_active.add(entry)
    exits = set()
    visited = set()
    work = [(entry, m, x, False, False, ())]
    while work:
        f, m, x, mk, xk, php = work.pop()
        while True:
            if f >= ROMSIZE or (0xFFC0 <= f < 0x10000): break
            if f in visited: break
            visited.add(f)
            if f in insns:
                ins = insns[f]                       # already decoded: walk it for flag tracking only
            else:
                if covered[f]:                       # inside another instruction: flag-state conflict
                    conflicts.append((entry, f, m, x)); break
                ins = decode(f, m, x, 0xC0 | ((f >> 16) & 0x3F))
                if ins is None: break
                insns[f] = ins
                for i in range(ins.size): covered[f+i] = 1
            mn, mode = ins.op, ins.mode
            bank = 0xC0 | ((f >> 16) & 0x3F)
            if mn == 'rep':
                v = rd8(f+1)
                if v & 0x20: m, mk = 0, True
                if v & 0x10: x, xk = 0, True
            elif mn == 'sep':
                v = rd8(f+1)
                if v & 0x20: m, mk = 1, True
                if v & 0x10: x, xk = 1, True
            elif mn == 'php': php = php + ((m, x, mk, xk),)
            elif mn == 'plp' and php: (m, x, mk, xk), php = php[-1], php[:-1]
            elif mn == 'xce':
                prev = insns.get(f-1)
                if prev and prev.op == 'sec': m = x = 1; mk = xk = True
            elif mn in FLOW_END:
                exits.add((m, mk, x, xk)); break
            elif mn in ('jmp','jml','bra','brl'):
                if ins.target is not None and mode in ('abs','long','rel','rell'):
                    tf = addr2file(ins.target)
                    if tf is not None:
                        locs.add(tf); work.append((tf, m, x, mk, xk, php))
                elif mode == 'iabsx':
                    pending_tables.append((ins, m, x, bank, entry))
                break
            elif mn in ('jsr','jsl'):
                if mode in ('abs','long'):
                    tf = addr2file(ins.target)
                    if tf is not None:
                        subs.add(tf)
                        m, x, mk, xk = apply_exits(analyze_fn(tf, m, x), m, x, mk, xk)
                elif mode == 'iabsx':
                    pending_tables.append((ins, m, x, bank, entry))
            elif mode == 'rel':
                tf = addr2file(ins.target)
                if tf is not None:
                    locs.add(tf); work.append((tf, m, x, mk, xk, php))
            # data references
            if mode in ('abs','absx','absy','long','longx','iabs','ilabs') and mn not in ('jsr','jmp','jsl','jml','pea'):
                v = rd16(f+1) if mode not in ('long','longx') else rd24(f+1)
                a = v if mode in ('long','longx') else ((bank << 16) | v)
                fa = addr2file(a) if (mode in ('long','longx') or v >= 0x8000) else None
                if fa is not None:
                    datarefs.setdefault(fa, set()).add(f)
            f += ins.size
    fn_active.discard(entry)
    fn_exits[entry] = exits
    return exits

def apply_exits(exits, m, x, mk, xk):
    """Adopt a callee's flag state where every exit path explicitly set it to the same value."""
    if not exits: return m, x, mk, xk
    ms = {(em, emk) for em, emk, _, _ in exits}
    xs = {(ex, exk) for _, _, ex, exk in exits}
    if len(ms) == 1:
        em, emk = next(iter(ms))
        if emk: m, mk = em, True
    if len(xs) == 1:
        ex, exk = next(iter(xs))
        if exk: x, xk = ex, True
    return m, x, mk, xk

def trace():
    while queue:
        f, m, x = queue.popleft()
        analyze_fn(f, m, x)

BAD_OPS = {'brk','cop','wdm','stp','sed','wai'}
def plausible_code(ef, m, x, bank, limit=200):
    """Decode forward from ef; true if it reaches a flow terminator without hitting junk."""
    f = ef; n = 0; last_jsr = None
    if ROM[ef] in (0x00, 0xFF): return False
    while f < ROMSIZE and f - ef < limit and n < 40:
        if f in insns: return True            # joins known code cleanly
        for k in range(1, 4):
            if (f - k) in insns and insns[f-k].size > k: return False  # misaligned
        ins = decode(f, m, x, bank)
        if ins is None: return False
        if ins.op in BAD_OPS:
            return n >= 6 and last_jsr is not None and f - last_jsr < 16
        if ins.op in ('jsr','jsl'): last_jsr = f
        if ins.mode in ('longx','long') and ins.op not in ('jsl','jml','lda','sta','cmp','adc','sbc','and','ora','eor'): return False
        if ins.op == 'rep': v = rd8(f+1); m = 0 if v & 0x20 else m; x = 0 if v & 0x10 else x
        if ins.op == 'sep': v = rd8(f+1); m = 1 if v & 0x20 else m; x = 1 if v & 0x10 else x
        if ins.op in ('rts','rtl','rti','jmp','jml','bra','brl'): return True
        if ins.op in ('jsr','jsl') and ins.target is not None and not in_rom(ins.target): return False
        f += ins.size; n += 1
    return n >= 8

def read_table(ins, m, x, bank):
    """jsr/jmp (abs,X): read 16-bit entries while they point at plausible code."""
    tf = addr2file(ins.target)
    if tf is None: return
    if tf in tables: return
    entries = []
    for i in range(0, 64):
        e = tf + 2*i
        if e + 1 >= ROMSIZE: break
        if e in insns: break
        if i > 0 and (e in table_bases or e in subs or e in locs or e in labels): break
        v = rd16(e)
        if v in (0x0000, 0xFFFF): break
        a = (bank << 16) | v
        ef = addr2file(a)
        if ef is None: break
        if ef >= 0xFFC0 and ef < 0x10000: break
        # entries stay in the same 32K half of the bank as the table itself
        if ((ef ^ tf) & 0x8000): break
        if not plausible_code(ef, m, x, bank): break
        entries.append(ef)
    if not entries: return
    tables[tf] = entries
    for i in range(len(entries)*2): covered[tf+i] = 2
    for ef in entries:
        if ins.op == 'jsr': subs.add(ef)
        else: locs.add(ef)
        queue.append((ef, m, x))

def find_ram_dispatch_tables():
    """Handler tables behind `lda table,X/Y ; sta $dp ; ... jmp ($dp)`: read entries as subroutine pointers."""
    jmp_dps = {rd8(i.f+1) for i in insns.values() if i.op == 'jmp' and i.mode == 'iabs' and rd16(i.f+1) < 0x100}
    jmp_dps |= {rd8(i.f+1) for i in insns.values() if i.op == 'jmp' and i.mode == 'iabs'}
    for f, ins in list(insns.items()):
        if ins.op != 'lda' or ins.mode not in ('absx','absy'): continue
        nxt = insns.get(f + ins.size)
        for _ in range(3):   # allow a couple of instructions (e.g. beq) between the load and the store
            if not nxt or nxt.op == 'sta': break
            nxt = insns.get(nxt.f + nxt.size)
        if not nxt or nxt.op != 'sta' or nxt.mode not in ('dp','abs'): continue
        dp = rd8(nxt.f+1) if nxt.mode == 'dp' else rd16(nxt.f+1)
        if dp not in jmp_dps: continue
        bank = 0xC0 | ((f >> 16) & 0x3F)
        tf = addr2file((bank << 16) | rd16(f+1))
        if tf is None or tf in tables or covered[tf] == 1: continue
        entries = []
        for i in range(64):
            e = tf + 2*i
            if e + 1 >= ROMSIZE or covered[e]: break
            if i > 0 and (e in table_bases or e in subs or e in locs or e in labels): break
            v = rd16(e)
            if v in (0x0000, 0xFFFF): break
            ef = addr2file((bank << 16) | v)
            if ef is None or ((ef ^ tf) & 0x8000) or (0xFFC0 <= ef < 0x10000): break
            if not plausible_code(ef, ins.m, ins.x, bank): break
            entries.append(ef)
        if len(entries) < 2: continue
        tables[tf] = entries; table_bases.add(tf)
        labels.setdefault(tf, f'handlers_{file2addr(tf):06X}')
        for i in range(len(entries)*2): covered[tf+i] = 2
        for ef in entries:
            subs.add(ef); queue.append((ef, ins.m, ins.x))

STRICT_BAD = BAD_OPS | {'sed','cli','xce','tcs','stp','cop'}
def strict_code(f, end, m, x, bank):
    """Stricter than plausible_code: the linear run must reach rts/rtl/jmp/bra inside the gap,
    contain >= 6 instructions, and use only ordinary addressing modes."""
    n = 0
    while f < end:
        ins = decode(f, m, x, bank)
        if ins is None or ins.op in STRICT_BAD or ins.mode in ('sr','isry','mvn'): return False
        if ins.mode in ('long','longx') and ins.op not in ('jsl','jml','lda','sta','cmp'): return False
        if ins.mode in ('abs','absx','absy') and ins.op in ('lda','sta','stz','ldx','ldy','cmp','inc','dec','bit','adc','sbc','and','ora','eor'):
            v = rd16(f+1)
            if 0x2200 <= v < 0x4000 or 0x4400 <= v < 0x8000: return False   # nonsense hardware/ROM-window address
        if ins.op in ('jsr','jsl','jmp','jml') and ins.target is not None:
            tf = addr2file(ins.target)
            if tf is None or ((tf ^ f) & 0x8000 and ins.op in ('jsr','jmp')): return False
            if ins.op in ('jsr','jml','jsl') and tf not in subs and tf not in insns: return False   # must call known code
        if ins.op == 'rep': v = rd8(f+1); m = 0 if v & 0x20 else m; x = 0 if v & 0x10 else x
        if ins.op == 'sep': v = rd8(f+1); m = 1 if v & 0x20 else m; x = 1 if v & 0x10 else x
        n += 1
        if ins.op in ('rts','rtl','rti','jmp','jml','bra','brl'): return n >= 6
        f += ins.size
    return False

def orphan_sweep(regions, m=0, x=0, min_gap=24):
    """Seed unreferenced but code-looking gaps (reached at runtime through RAM pointers the
    tracer cannot resolve statically). Labelled orphan_XXXXXX so their provenance stays visible."""
    found = 0
    for lo, hi in regions:
        f = lo
        while f < hi:
            if covered[f]:
                f += 1; continue
            g = f
            while g < hi and not covered[g]: g += 1
            if g - f >= min_gap and strict_code(f, g, m, x, 0xC0 | ((f >> 16) & 0x3F)):
                labels.setdefault(f, f'orphan_{file2addr(f):06X}')
                subs.add(f); queue.append((f, m, x)); found += 1
            f = g
    return found

def trace_all():
    """Trace direct flows first, then resolve jump tables, until a fixed point."""
    while True:
        trace()
        if not pending_tables:
            if SWEEP and orphan_sweep(SWEEP): continue
            break
        for ins, m, x, bank, owner in pending_tables:
            tf = addr2file(ins.target)
            if tf is not None: table_bases.add(tf)
        batch = sorted(pending_tables, key=lambda q: addr2file(q[0].target) or 0); pending_tables.clear()
        for ins, m, x, bank, owner in batch:
            read_table(ins, m, x, bank)
        find_ram_dispatch_tables()

# ---------- naming ----------
def label_for(f):
    if f in labels: return labels[f]
    if f in subs: return f'sub_{file2addr(f):06X}'
    if f in locs: return f'loc_{file2addr(f):06X}'
    return None

def build_labels(seed):
    labels.update(seed)
    for f in list(subs) + list(locs):
        if f not in labels:
            labels[f] = label_for(f)
    for f in tables:
        labels.setdefault(f, f'jtbl_{file2addr(f):06X}')
    for f in datarefs:
        if not covered[f]:
            labels.setdefault(f, f'data_{file2addr(f):06X}')

def operand_symbol(ins):
    """Replace numeric operand with a label / register name where possible."""
    f = ins.f; mode = ins.mode; mn = ins.op
    bank = 0xC0 | ((f >> 16) & 0x3F)
    txt = ins.text
    if ins.target is not None and mode in ('abs','long','rel','rell','iabsx'):
        tf = addr2file(ins.target)
        if tf is not None and tf in labels:
            name = labels[tf]
            if mode == 'iabsx': return f'{mn} ({name},X)'
            if mode == 'long': return f'{mn} {name}'
            return f'{mn} {name}'
    if mode in ('dp','dpx','dpy','idp','idpx','idpy','ildp','ildpy'):
        v = rd8(f+1)
        if v in RAM_LABELS:
            n = RAM_LABELS[v]
            return {'dp':f'{mn} {n}','dpx':f'{mn} {n},X','dpy':f'{mn} {n},Y','idp':f'{mn} ({n})','idpx':f'{mn} ({n},X)',
                    'idpy':f'{mn} ({n}),Y','ildp':f'{mn} [{n}]','ildpy':f'{mn} [{n}],Y'}[mode]
    if mode in ('abs','absx','absy','iabs','ilabs') and mn not in ('jsr','jmp','pea'):
        v = rd16(f+1)
        if v in RAM_LABELS and v < 0x2000:
            suffix = {'absx':',X','absy':',Y'}.get(mode,'')
            return f'{mn} {RAM_LABELS[v]}{suffix}'
        if v in REGS:
            suffix = {'absx':',X','absy':',Y'}.get(mode,'')
            return f'{mn} {REGS[v]}{suffix}'
        if v >= 0x8000:
            tf = addr2file((bank << 16) | v)
            if tf is not None and tf in labels:
                suffix = {'absx':',X','absy':',Y'}.get(mode,'')
                return f'{mn} {labels[tf]}{suffix}'
    if mode in ('long','longx') and mn not in ('jsl','jml'):
        v = rd24(f+1)
        tf = addr2file(v)
        if tf is not None and tf in labels:
            suffix = ',X' if mode == 'longx' else ''
            return f'{mn} {labels[tf]}{suffix}'
    return txt

# ---------- output ----------
def ranges(pred):
    out = []; start = None
    for f in range(ROMSIZE + 1):
        on = f < ROMSIZE and pred(f)
        if on and start is None: start = f
        if not on and start is not None:
            out.append((start, f)); start = None
    return out

def write_outputs(outdir):
    os.makedirs(outdir, exist_ok=True)
    code_ranges = ranges(lambda f: covered[f] == 1)
    with open(os.path.join(outdir, 'codemap.txt'), 'w') as fp:
        fp.write('# code ranges (file offsets, HiROM addr = $C0:0000 + offset)\n')
        for a, b in code_ranges:
            fp.write(f'{a:06X}-{b-1:06X}  ${file2addr(a):06X}  {b-a:6d} bytes\n')
        fp.write(f'\n# total code bytes: {sum(b-a for a,b in code_ranges)}\n')
        fp.write(f'# subroutines: {len(subs)}  branch targets: {len(locs)}  jump tables: {len(tables)}\n')
        if conflicts:
            fp.write('\n# flag-state conflicts (path stopped because it ran into existing code mid-instruction)\n')
            for entry, f, m, x in conflicts:
                fp.write(f'fn {file2addr(entry):06X} at {file2addr(f):06X} m{m}x{x}\n')
        fp.write('\n# jump tables\n')
        for t, es in sorted(tables.items()):
            fp.write(f'{labels.get(t)} @ {file2addr(t):06X}: {len(es)} entries -> ' + ', '.join(labels.get(e, f"{file2addr(e):06X}") for e in es) + '\n')
    with open(os.path.join(outdir, 'symbols.txt'), 'w') as fp:
        fp.write('# addr    file    name\n')
        for f in sorted(labels):
            fp.write(f'{file2addr(f):06X}  {f:06X}  {labels[f]}\n')
        fp.write('# ram\n')
        for a, n in sorted(RAM_LABELS.items()):
            fp.write(f'{0x7E0000 | a:06X}  ------  {n}\n')
    with open(os.path.join(outdir, 'dream.mlb'), 'w') as fp:
        for f in sorted(labels):
            fp.write(f'SnesPrgRom:{f:X}:{labels[f]}\n')
        for a, n in sorted(REGS.items()):
            fp.write(f'SnesRegister:{a:X}:{n}\n')
        for a, n in sorted(RAM_LABELS.items()):
            fp.write(f'SnesWorkRam:{a & 0x1FFFF:X}:{n}\n')
    with open(os.path.join(outdir, 'dream.sym'), 'w') as fp:
        fp.write('[labels]\n')
        for f in sorted(labels):
            a = file2addr(f)
            fp.write(f'{a>>16:02x}:{a&0xFFFF:04x} {labels[f]}\n')
        for a, n in sorted(RAM_LABELS.items()):
            fp.write(f'7e:{a & 0xFFFF:04x} {n}\n')
    # listing
    with open(os.path.join(outdir, 'dream.asm'), 'w') as fp:
        fp.write('; Dream: Land of Giants (SNES prototype) - traced disassembly. HiROM. addr = $C0:0000 + file offset\n')
        f = 0
        while f < ROMSIZE:
            if f in insns:
                ins = insns[f]
                if f in labels: fp.write(f'\n{labels[f]}:\n')
                by = ' '.join(f'{ROM[f+i]:02X}' for i in range(ins.size))
                flags = f'm{ins.m}x{ins.x}'
                fp.write(f'  {file2addr(f):06X}  {by:<12} {operand_symbol(ins):<28} ; {flags}\n')
                f += ins.size
            else:
                # data run until next instruction or label
                start = f
                while f < ROMSIZE and f not in insns and (f == start or f not in labels):
                    f += 1
                if start in labels: fp.write(f'\n{labels[start]}:\n')
                if start in tables:
                    es = tables[start]
                    for e in es:
                        fp.write(f'  {file2addr(start):06X}  .dw {labels.get(e, f"${file2addr(e)&0xFFFF:04X}")}\n')
                    continue
                n = f - start
                if n <= 64:
                    for i in range(start, f, 16):
                        chunk = ROM[i:min(i+16, f)]
                        fp.write(f'  {file2addr(i):06X}  .db ' + ','.join(f'${b:02X}' for b in chunk) + '\n')
                else:
                    fp.write(f'  {file2addr(start):06X}  .incbin "DREAM.sfc" ${start:06X} ${n:X}   ; {n} bytes data\n')

def load_rom(path):
    """The same three checks tools/extract.py makes, in the same order."""
    rom = open(path, 'rb').read()
    if len(rom) == EXPECTED_SIZE + 512:           # copier header
        rom = rom[512:]
    if len(rom) != EXPECTED_SIZE:
        sys.exit(f'{path}: expected {EXPECTED_SIZE} bytes, got {len(rom)}')
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_SHA1:
        sys.exit(f'{path}: sha1 {sha1} does not match {EXPECTED_SHA1}')
    return rom


def reset_state():
    """Every table here is module-level, so a second main() in one process would
    compound the first run's results. tools/progress.py calls main() in-process."""
    global covered
    insns.clear(); labels.clear(); subs.clear(); locs.clear()
    datarefs.clear(); tables.clear(); fn_exits.clear(); fn_active.clear()
    seen_state.clear(); table_bases.clear()
    RAM_LABELS.clear()
    conflicts.clear(); pending_tables.clear()
    queue.clear()
    covered = None


def main(write=True):
    global ROM, ROMSIZE, covered
    if len(sys.argv) < 3:
        sys.exit(f'usage: {sys.argv[0]} <rom> <outdir> [ADDR[:name] ...]')
    rom_path, outdir = sys.argv[1], sys.argv[2]
    reset_state()
    ROM = load_rom(rom_path)
    ROMSIZE = len(ROM)
    covered = bytearray(ROMSIZE)
    vec = {
        'reset': rd16(0xFFFC), 'nmi': rd16(0xFFEA), 'irq': rd16(0xFFEE), 'brk': rd16(0xFFE6),
        'cop': rd16(0xFFE4), 'abort': rd16(0xFFE8),
        'emu_reset': rd16(0xFFFC), 'emu_nmi': rd16(0xFFFA), 'emu_irq': rd16(0xFFFE), 'emu_cop': rd16(0xFFF4), 'emu_abort': rd16(0xFFF8),
    }
    seed = {}
    def seed_addr(a, name, m=1, x=1, kind='sub'):
        f = addr2file(a)
        if f is None: return
        seed.setdefault(f, name)
        push(a, m, x, kind)
    seed_addr(0x008000 | 0xC00000, 'reset')
    seed_addr(0xC00000 | vec['nmi'], 'nmi')
    seed_addr(0xC00000 | vec['cop'], 'cop_vec')
    seed_addr(0xC00000 | vec['brk'], 'brk_vec')
    seed_addr(0xC00000 | vec['abort'], 'abort_vec')
    seed_addr(0xC00000 | vec['emu_irq'], 'emu_irq')
    seed_addr(0xC00000 | vec['emu_nmi'], 'emu_nmi')
    # secondary boot image at file 0x000000 ($C0:0000)
    # NMI handlers are installed as `lda #$addr` + `jmp $A4E9` (or `sta $00`); nmi enters them with m=0,x=0
    import re as _re
    for mm in _re.finditer(rb'\xA9(..)(\x4C\xE9\xA4|\x85\x00)', ROM[0x8000:0x20000]):
        a = int.from_bytes(mm.group(1), 'little')
        if a >= 0x8000:
            seed_addr(0xC00000 | a, f'nmi_handler_{a:04X}', m=0, x=0)
    # file 0x0000-0x7FFF is a stale build image (its jsr targets land mid-instruction in the live code); not traced
    # names file: lines "ADDR name [; comment]" for ROM, "ram ADDR name" for WRAM/direct page
    names_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'names.txt')
    if os.path.exists(names_path):
        for line in open(names_path):
            line = line.split(';')[0].strip()
            if not line: continue
            parts = line.split()
            if parts[0] == 'ram':
                RAM_LABELS[int(parts[1], 16)] = parts[2]
            elif parts[0] == 'data':
                f = addr2file(int(parts[1], 16))
                if f is not None: seed[f] = parts[2]
            else:
                a = int(parts[0], 16)
                name = parts[1] if len(parts) > 1 else f'sub_{a:06X}'
                st = [q for q in parts[2:] if q in ('m0x0','m0x1','m1x0','m1x1')]
                f = addr2file(a)
                if f is None: continue
                seed[f] = name
                if st:   # explicit entry state -> trace from here (otherwise the name is applied when reached)
                    m, x = int(st[0][1]), int(st[0][3])
                    push(a, m, x, 'sub')
    for arg in sys.argv[3:]:
        a, _, n = arg.partition(':')
        a = int(a, 16)
        seed_addr(a, n or f'sub_{a:06X}')
    trace_all()
    # second pass: jsl targets referenced from data-ish regions are not followed; fine.
    build_labels(seed)
    # progress.py --check is documented as read-only and is a gate in the pre-push hook.
    # It calls this in-process purely for the routine sizes, and rewriting out/codemap.txt,
    # out/symbols.txt, out/dream.mlb, out/dream.sym and out/dream.asm from a gate is a
    # side effect nobody asked for.
    if write:
        write_outputs(outdir)
    code = sum(1 for v in covered if v == 1)
    print(f'instructions={len(insns)} code_bytes={code} subs={len(subs)} locs={len(locs)} tables={len(tables)} labels={len(labels)}')
    for t, es in sorted(tables.items()):
        print(f'  table {file2addr(t):06X}: {len(es)} entries')

if __name__ == '__main__':
    main()
