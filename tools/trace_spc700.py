#!/usr/bin/env python3
"""SPC700 recursive-descent tracer / disassembler for the Dream: Land of Giants (SNES prototype) sound driver.

Standalone, no dependencies.  Usage:

    python3 tools/trace_spc700.py [baserom/DREAM.sfc] [spc/]

Builds the SPC700 RAM image from the two blocks that the 65816 side uploads
(see out/dream.asm, spc_ipl_upload_loader / spc_upload_driver):

    file 0x20000, 0x088 bytes  -> SPC $04D8  (IPL-uploaded loader)
    file 0x20088, 0x699 words  -> SPC $0560  (main driver, streamed by the loader)

then traces code from the loader entry ($04D8) and from the driver entry, emits
spc/driver.asm in asar `arch spc700` syntax and spc/spc_map.txt.  Bytes that are not
decoded as instructions (tables, the sample remap block, unreached bytes, null jump
table entries) are never written into the source: they become
`incbin "../data/04.bin":$lo..$hi` ranges into the half-bank file that
tools/extract.py splits out of the ROM (file offset f -> data/{f>>15:02X}.bin at
f & 0x7FFF; end exclusive), so the committed .asm carries no ROM bytes.

The driver entry is not a reset vector: the loader ends with `jmp ($0539+x)`
(x = 0) where $0539/$053A is the self-modified operand of `mov $xxxx+y,a` that
received the last block address.  The 65816 sends address $0672 with a word
count of 0 (sub_C1803E) which the loader treats as "jump here", so the driver
entry is $0672.  Nothing in the driver uses tcall/pcall, so the IPL-space
vectors are irrelevant.
"""
import os
import sys

# ---------------------------------------------------------------------------
# Opcode table.  Operand kinds, in byte order after the opcode:
#   dp   direct page byte            abs  16-bit address
#   imm  immediate byte              rel  signed 8-bit branch offset
#   mem  13-bit address + 3-bit index (mov1/or1/and1/eor1/not1)
#   up   pcall page byte
# The template uses {0},{1} for operands in byte order; {rel} is the resolved
# branch target label.  Two-operand encodings (dp,dp / dp,#imm) store the
# source first and the destination second, so their templates print {1},{0}.
# Syntax follows asar's arch-spc700.cpp (parenthesised indirect modes,
# (x+) for post-increment, `set1 $dp.bit`, `or1 c,!$addr.bit` for inverted).
# ---------------------------------------------------------------------------

_ALU = ['or', 'and', 'eor', 'cmp', 'adc', 'sbc']

OPS = {}


def _op(code, text, *kinds):
    assert code not in OPS, hex(code)
    OPS[code] = (text, tuple(kinds))


for i, m in enumerate(_ALU):
    b = i * 0x20
    _op(b + 0x04, m + ' a,{0}', 'dp')
    _op(b + 0x05, m + ' a,{0}', 'abs')
    _op(b + 0x06, m + ' a,(x)')
    _op(b + 0x07, m + ' a,({0}+x)', 'dp')
    _op(b + 0x08, m + ' a,#{0}', 'imm')
    _op(b + 0x09, m + ' {1},{0}', 'dp', 'dp')
    _op(b + 0x14, m + ' a,{0}+x', 'dp')
    _op(b + 0x15, m + ' a,{0}+x', 'abs')
    _op(b + 0x16, m + ' a,{0}+y', 'abs')
    _op(b + 0x17, m + ' a,({0})+y', 'dp')
    _op(b + 0x18, m + ' {1},#{0}', 'imm', 'dp')
    _op(b + 0x19, m + ' (x),(y)')

for bit in range(8):
    b = bit * 0x20
    _op(b + 0x02, 'set1 {0}.%d' % bit, 'dp')
    _op(b + 0x03, 'bbs {0}.%d,{rel}' % bit, 'dp', 'rel')
    _op(b + 0x12, 'clr1 {0}.%d' % bit, 'dp')
    _op(b + 0x13, 'bbc {0}.%d,{rel}' % bit, 'dp', 'rel')

for n in range(16):
    _op(n * 0x10 + 0x01, 'tcall %d' % n)

_branches = {0x10: 'bpl', 0x2F: 'bra', 0x30: 'bmi', 0x50: 'bvc', 0x70: 'bvs',
             0x90: 'bcc', 0xB0: 'bcs', 0xD0: 'bne', 0xF0: 'beq'}
for code, m in _branches.items():
    _op(code, m + ' {rel}', 'rel')

_op(0x00, 'nop')
_op(0x0A, 'or1 c,{0}', 'mem')
_op(0x0B, 'asl {0}', 'dp')
_op(0x0C, 'asl {0}', 'abs')
_op(0x0D, 'push p')
_op(0x0E, 'tset {0},a', 'abs')
_op(0x0F, 'brk')
_op(0x1A, 'decw {0}', 'dp')
_op(0x1B, 'asl {0}+x', 'dp')
_op(0x1C, 'asl a')
_op(0x1D, 'dec x')
_op(0x1E, 'cmp x,{0}', 'abs')
_op(0x1F, 'jmp ({0}+x)', 'abs')
_op(0x20, 'clrp')
_op(0x2A, 'or1 c,!{0}', 'mem')
_op(0x2B, 'rol {0}', 'dp')
_op(0x2C, 'rol {0}', 'abs')
_op(0x2D, 'push a')
_op(0x2E, 'cbne {0},{rel}', 'dp', 'rel')
_op(0x3A, 'incw {0}', 'dp')
_op(0x3B, 'rol {0}+x', 'dp')
_op(0x3C, 'rol a')
_op(0x3D, 'inc x')
_op(0x3E, 'cmp x,{0}', 'dp')
_op(0x3F, 'call {0}', 'abs')
_op(0x40, 'setp')
_op(0x4A, 'and1 c,{0}', 'mem')
_op(0x4B, 'lsr {0}', 'dp')
_op(0x4C, 'lsr {0}', 'abs')
_op(0x4D, 'push x')
_op(0x4E, 'tclr {0},a', 'abs')
_op(0x4F, 'pcall {0}', 'up')
_op(0x5A, 'cmpw ya,{0}', 'dp')
_op(0x5B, 'lsr {0}+x', 'dp')
_op(0x5C, 'lsr a')
_op(0x5D, 'mov x,a')
_op(0x5E, 'cmp y,{0}', 'abs')
_op(0x5F, 'jmp {0}', 'abs')
_op(0x60, 'clrc')
_op(0x6A, 'and1 c,!{0}', 'mem')
_op(0x6B, 'ror {0}', 'dp')
_op(0x6C, 'ror {0}', 'abs')
_op(0x6D, 'push y')
_op(0x6E, 'dbnz {0},{rel}', 'dp', 'rel')
_op(0x6F, 'ret')
_op(0x7A, 'addw ya,{0}', 'dp')
_op(0x7B, 'ror {0}+x', 'dp')
_op(0x7C, 'ror a')
_op(0x7D, 'mov a,x')
_op(0x7E, 'cmp y,{0}', 'dp')
_op(0x7F, 'reti')
_op(0x80, 'setc')
_op(0x8A, 'eor1 c,{0}', 'mem')
_op(0x8B, 'dec {0}', 'dp')
_op(0x8C, 'dec {0}', 'abs')
_op(0x8D, 'mov y,#{0}', 'imm')
_op(0x8E, 'pop p')
_op(0x8F, 'mov {1},#{0}', 'imm', 'dp')
_op(0x9A, 'subw ya,{0}', 'dp')
_op(0x9B, 'dec {0}+x', 'dp')
_op(0x9C, 'dec a')
_op(0x9D, 'mov x,sp')
_op(0x9E, 'div ya,x')
_op(0x9F, 'xcn a')
_op(0xA0, 'ei')
_op(0xAA, 'mov1 c,{0}', 'mem')
_op(0xAB, 'inc {0}', 'dp')
_op(0xAC, 'inc {0}', 'abs')
_op(0xAD, 'cmp y,#{0}', 'imm')
_op(0xAE, 'pop a')
_op(0xAF, 'mov (x+),a')
_op(0xBA, 'movw ya,{0}', 'dp')
_op(0xBB, 'inc {0}+x', 'dp')
_op(0xBC, 'inc a')
_op(0xBD, 'mov sp,x')
_op(0xBE, 'das a')
_op(0xBF, 'mov a,(x+)')
_op(0xC0, 'di')
_op(0xC4, 'mov {0},a', 'dp')
_op(0xC5, 'mov {0},a', 'abs')
_op(0xC6, 'mov (x),a')
_op(0xC7, 'mov ({0}+x),a', 'dp')
_op(0xC8, 'cmp x,#{0}', 'imm')
_op(0xC9, 'mov {0},x', 'abs')
_op(0xCA, 'mov1 {0},c', 'mem')
_op(0xCB, 'mov {0},y', 'dp')
_op(0xCC, 'mov {0},y', 'abs')
_op(0xCD, 'mov x,#{0}', 'imm')
_op(0xCE, 'pop x')
_op(0xCF, 'mul ya')
_op(0xD4, 'mov {0}+x,a', 'dp')
_op(0xD5, 'mov {0}+x,a', 'abs')
_op(0xD6, 'mov {0}+y,a', 'abs')
_op(0xD7, 'mov ({0})+y,a', 'dp')
_op(0xD8, 'mov {0},x', 'dp')
_op(0xD9, 'mov {0}+y,x', 'dp')
_op(0xDA, 'movw {0},ya', 'dp')
_op(0xDB, 'mov {0}+x,y', 'dp')
_op(0xDC, 'dec y')
_op(0xDD, 'mov a,y')
_op(0xDE, 'cbne {0}+x,{rel}', 'dp', 'rel')
_op(0xDF, 'daa a')
_op(0xE0, 'clrv')
_op(0xE4, 'mov a,{0}', 'dp')
_op(0xE5, 'mov a,{0}', 'abs')
_op(0xE6, 'mov a,(x)')
_op(0xE7, 'mov a,({0}+x)', 'dp')
_op(0xE8, 'mov a,#{0}', 'imm')
_op(0xE9, 'mov x,{0}', 'abs')
_op(0xEA, 'not1 {0}', 'mem')
_op(0xEB, 'mov y,{0}', 'dp')
_op(0xEC, 'mov y,{0}', 'abs')
_op(0xED, 'notc')
_op(0xEE, 'pop y')
_op(0xEF, 'sleep')
_op(0xF4, 'mov a,{0}+x', 'dp')
_op(0xF5, 'mov a,{0}+x', 'abs')
_op(0xF6, 'mov a,{0}+y', 'abs')
_op(0xF7, 'mov a,({0})+y', 'dp')
_op(0xF8, 'mov x,{0}', 'dp')
_op(0xF9, 'mov x,{0}+y', 'dp')
_op(0xFA, 'mov {1},{0}', 'dp', 'dp')
_op(0xFB, 'mov y,{0}+x', 'dp')
_op(0xFC, 'inc y')
_op(0xFD, 'mov y,a')
_op(0xFE, 'dbnz y,{rel}', 'rel')
_op(0xFF, 'stop')

assert len(OPS) == 256, len(OPS)

_KIND_LEN = {'dp': 1, 'imm': 1, 'rel': 1, 'up': 1, 'abs': 2, 'mem': 2}


def op_len(code):
    return 1 + sum(_KIND_LEN[k] for k in OPS[code][1])


# Reference instruction lengths (rows = high nibble, columns = low nibble),
# transcribed from the standard SPC700 opcode matrix.  The operand-derived
# lengths above are checked against this at import time.
_REF_LENGTHS = """
1 1 2 3 2 3 1 2 2 3 3 2 3 1 3 1
2 1 2 3 2 3 3 2 3 1 2 2 1 1 3 3
1 1 2 3 2 3 1 2 2 3 3 2 3 1 3 2
2 1 2 3 2 3 3 2 3 1 2 2 1 1 2 3
1 1 2 3 2 3 1 2 2 3 3 2 3 1 3 2
2 1 2 3 2 3 3 2 3 1 2 2 1 1 3 3
1 1 2 3 2 3 1 2 2 3 3 2 3 1 3 1
2 1 2 3 2 3 3 2 3 1 2 2 1 1 2 1
1 1 2 3 2 3 1 2 2 3 3 2 3 2 1 3
2 1 2 3 2 3 3 2 3 1 2 2 1 1 1 1
1 1 2 3 2 3 1 2 2 3 3 2 3 2 1 1
2 1 2 3 2 3 3 2 3 1 2 2 1 1 1 1
1 1 2 3 2 3 1 2 2 3 3 2 3 2 1 1
2 1 2 3 2 3 3 2 2 2 2 2 1 1 3 1
1 1 2 3 2 3 1 2 2 3 3 2 3 1 1 1
2 1 2 3 2 3 3 2 2 2 3 2 1 1 2 1
""".split()
assert len(_REF_LENGTHS) == 256
for _c in range(256):
    assert op_len(_c) == int(_REF_LENGTHS[_c]), 'length mismatch at %02X' % _c

# Flow classification.
FLOW_STOP = {0x6F, 0x7F, 0xEF, 0xFF, 0x0F}          # ret reti sleep stop brk
FLOW_JMP_ABS = 0x5F
FLOW_JMP_TABLE = 0x1F
FLOW_CALL = 0x3F
FLOW_PCALL = 0x4F
FLOW_TCALL = {n * 0x10 + 0x01 for n in range(16)}
FLOW_BRA = 0x2F
FLOW_BRANCH = set(_branches) | {0x2E, 0x6E, 0xDE, 0xFE} | \
    {b * 0x20 + 0x03 for b in range(8)} | {b * 0x20 + 0x13 for b in range(8)}

# ---------------------------------------------------------------------------
# Hardware registers and hand-curated annotations.
# ---------------------------------------------------------------------------
HWREGS = {
    0xF0: 'TEST', 0xF1: 'CONTROL', 0xF2: 'DSPADDR', 0xF3: 'DSPDATA',
    0xF4: 'CPUIO0', 0xF5: 'CPUIO1', 0xF6: 'CPUIO2', 0xF7: 'CPUIO3',
    0xF8: 'AUXIO4', 0xF9: 'AUXIO5',
    0xFA: 'T0TARGET', 0xFB: 'T1TARGET', 0xFC: 'T2TARGET',
    0xFD: 'T0OUT', 0xFE: 'T1OUT', 0xFF: 'T2OUT',
}

DSP_REGS = {
    0x0C: 'MVOLL', 0x1C: 'MVOLR', 0x2C: 'EVOLL', 0x3C: 'EVOLR',
    0x4C: 'KON', 0x5C: 'KOFF', 0x6C: 'FLG', 0x7C: 'ENDX',
    0x0D: 'EFB', 0x2D: 'PMON', 0x3D: 'NON', 0x4D: 'EON', 0x5D: 'DIR',
    0x6D: 'ESA', 0x7D: 'EDL',
}
for _v in range(8):
    for _r, _n in enumerate(['VOL_L', 'VOL_R', 'PITCH_L', 'PITCH_H', 'SRCN',
                             'ADSR1', 'ADSR2', 'GAIN', 'ENVX', 'OUTX']):
        DSP_REGS[_v * 0x10 + _r] = 'V%d_%s' % (_v, _n)
for _i in range(8):
    DSP_REGS[_i * 0x10 + 0x0F] = 'FIR%d' % _i

# Curated names.  Code names are used as labels; comments go beside the label.
NAMES = {
    0x04D8: ('spc_loader', 'IPL-uploaded loader; entered by the IPL jump to $04D8'),
    0x04F3: ('loader_reset_dsp', 'FLG=$FF EDL=0 ESA=$FF, then block loop; driver cmd 7 jumps back here'),
    0x050A: ('loader_block_loop', 'handshake: wait port0==counter, movw ya,$F5 = dest address'),
    0x0556: ('loader_jump', 'word count 0: save counter, jmp (dest)'),
    0x055D: ('cmd_param', 'scratch byte inside the uploaded loader block; port2 -> cmd_param via cmd_receive'),
    0x0560: ('sample_remap', 'uploaded by 65816 loc_C18288; 256-byte sample number -> SRCN remap table, read by seq_load_srcn'),
    0x0660: ('start_song', 'cmd 3: song number in cmd_param -> song_table[$1312] -> $E5/$E6'),
    0x0672: ('driver_entry', 'reached via loader `jmp ($0539+x)` after the 65816 sends addr $0672 with 0 words'),
    0x0678: ('driver_init', 'init DSP/channels from song header at ($E5), clear play flag'),
    0x0683: ('main_loop', 'poll port0 == counter ($E9); else fall to loc_0781 tick handling'),
    0x068C: ('cmd_receive', 'port2 -> cmd_param ($055D), port1 -> cmd; echo counter; counter++'),
    0x06A0: ('cmd_dispatch', 'cmd >= $80: (cmd & 7) -> jtab_06A7'),
    0x06A7: ('cmd_table', '8 port commands'),
    0x06B7: ('cmd3_fade_and_song', 'ramp every DSP volume toward 0 (0x7F steps) then start_song'),
    0x06EB: ('dsp_step_toward_zero', 'read DSPDATA, move 2 toward 0, write back'),
    0x06FA: ('cmd2_set_mono', 'cmd_param -> mono flag ($1D)'),
    0x0702: ('cmd1_set_E7', 'cmd_param -> $E7 (unused elsewhere in traced code)'),
    0x070A: ('cmd0_set_E8', 'cmd_param -> $E8 (unused elsewhere in traced code)'),
    0x0712: ('cmd5_voice5_volume', 'scale DSP V5 VOL_L/R by cmd_param percent'),
    0x0739: ('cmd4_pitch_offset', 'sign-extend cmd_param * 8 -> $EC/$ED (applied to SFX voice $0D); clear EON bit 5'),
    0x0773: ('play_sfx', 'cmd < $80: sfx number = cmd, channel = cmd_param -> sfx_start'),
    0x077B: ('cmd6_play', 'play flag $1C = 1, stop timers'),
    0x0781: ('tick_wait', 'if playing: T0 target = $E4, wait one tick, then step all channels'),
    0x07A9: ('channel_loop', 'x = 0..7 music voices; x|8 = sfx voice on the same DSP channel'),
    0x07DB: ('cmd7_stop_to_loader', 'param != 0: jump straight to loader; else keyoff, ~200 T1 ticks, re-init, then loader'),
    0x0813: ('seq_step', 'per-channel sequencer step: countdown, key off at gate, fetch next event'),
    0x0850: ('seq_fetch', 'read event byte at ($44/$54+x); < $80 = command via jtab_0FD8, else note'),
    0x0867: ('seq_note', 'note event: $80 = rest, $E0/$E1 = stored notes, else pitch table lookup + KON'),
    0x0983: ('seq_note_length', 'set duration $34+x / gate $24+x from $0120/$0130 or inline bytes'),
    0x09BC: ('channel_update', 'per-tick pitch slide (bit0 of $0150+x), vibrato (bit1), tremolo/volume env (bits 2-3)'),
    0x0B18: ('seq_end', 'seq cmd $00: end of track, key off voice'),
    0x0B64: ('seq_pop_x', 'drop return address, restore x from stack (helper for seq commands)'),
    0x0B69: ('seq_retrigger', 'duration=1, gate=0'),
    0x0B72: ('seq_instrument', 'seq cmd $01'),
    0x0B8B: ('seq_load_srcn', 'sample index -> sample_remap[$0560] -> SRCN ($0244+x)'),
    0x0B97: ('seq_instr_full', 'seq cmd $22: instrument, transpose, finetune, volume, ADSR'),
    0x0BB6: ('seq_volume', 'seq cmd $02'),
    0x0BC2: ('seq_read_volume', 'L,R bytes; averaged when mono flag set'),
    0x0BCC: ('seq_read_volume_r', 'reads one sequence byte into vol_r[x] ($0264+x); companion to seq_read_volume\'s vol_l store'),
    0x0BF0: ('seq_volume_mono', 'seq cmd $23: one byte -> both channels'),
    0x0C02: ('seq_volume_preset', 'seq cmd $20: volume from $04B8/$04B9'),
    0x0C18: ('orphan_volume_preset2', 'stale seq_cmd_table entry $31: copy of seq cmd $20 using $04BA/$04BB'),
    0x0C4E: ('seq_master_percent', 'seq cmd $24: $04B6 = master volume percent'),
    0x0C59: ('scale_volume', 'a = a * $04B6 / 100, clamp +/-127'),
    0x0C83: ('seq_volume_presets', 'seq cmd $1E: 4 bytes -> $04B8..$04BB'),
    0x0CA0: ('seq_echo_delay', 'seq cmd $1F: EDL, ESA = $FF - EDL*8, clear echo buffer'),
    0x0CD7: ('seq_jump', 'seq cmd $03: new pointer'),
    0x0CE6: ('seq_call', 'seq cmd $04: count, addr; push return on $0334/$03B4/$0434 stack'),
    0x0CFF: ('seq_call_once', 'seq cmd $21: call (addr) with count 1'),
    0x0D1C: ('seq_push_return', 'read target word, push count/return address'),
    0x0D34: ('seq_return', 'seq cmd $05: pop; repeat while count > 0'),
    0x0D70: ('seq_set_length', 'seq cmd $06: note length (+gate byte if $01D0+x)'),
    0x0D8F: ('seq_clear_length', 'seq cmd $07: lengths inline after notes'),
    0x0D9B: ('seq_slide_up', 'seq cmd $08: pitch slide (5 bytes)'),
    0x0DA2: ('seq_slide_down', 'seq cmd $09'),
    0x0DD6: ('seq_slide_off', 'seq cmd $0A'),
    0x0DEB: ('seq_tempo', 'seq cmd $0B: $1F = tempo (tick accumulator increment)'),
    0x0DF8: ('seq_tempo_add', 'seq cmd $0C'),
    0x0E05: ('seq_vibrato_off', 'seq cmd $0E'),
    0x0E11: ('seq_vibrato', 'seq cmd $0D: rate, speed, depth'),
    0x0E1A: ('seq_vibrato_delay', 'seq cmd $0F: delay, rate, speed, depth'),
    0x0E25: ('seq_read_vibrato', ''),
    0x0E45: ('seq_adsr', 'seq cmd $10: ADSR1, ADSR2'),
    0x0E4E: ('seq_read_adsr', ''),
    0x0E5A: ('seq_master_volume', 'seq cmd $11: MVOLL, MVOLR'),
    0x0E8D: ('seq_set_note_E0', 'seq cmd $1C: note used by event $E0'),
    0x0E97: ('seq_set_note_E1', 'seq cmd $1D: note used by event $E1'),
    0x0EA4: ('seq_finetune', 'seq cmd $12: $64+x'),
    0x0EAE: ('seq_transpose', 'seq cmd $13'),
    0x0EBB: ('seq_transpose_add', 'seq cmd $14'),
    0x0ECA: ('seq_echo_setup', 'seq cmd $15: EFB, EVOLL, EVOLR; FLG = 0 (echo on)'),
    0x0EF7: ('seq_echo_on', 'seq cmd $16: EON |= voice bit'),
    0x0F0F: ('seq_echo_off', 'seq cmd $17'),
    0x0F29: ('seq_fir', 'seq cmd $18: 8 FIR coefficients'),
    0x0F43: ('seq_noise_clock', 'seq cmd $19: FLG noise bits ($04B4)'),
    0x0F56: ('seq_noise_on', 'seq cmd $1A'),
    0x0F67: ('seq_noise_off', 'seq cmd $1B'),
    0x0F77: ('orphan_slide_up2', 'stale seq_cmd_table entry $26: slide variant (4 operand bytes)'),
    0x0F81: ('orphan_slide_down2', 'stale seq_cmd_table entry $27'),
    0x0FA9: ('seq_advance5', 'pointer += 5'),
    0x0FAF: ('orphan_gate_on', 'stale seq_cmd_table entry $2B: gate_mode $01D0+x = 1'),
    0x0FB9: ('orphan_gate_off', 'stale seq_cmd_table entry $2C: gate_mode $01D0+x = 0'),
    0x0FC8: ('voice_bits', '8 x (1 << voice), twice: index by x (0-7) or x|8'),
    0x0FD8: ('seq_cmd_table', 'sequence command opcodes $00-$24 (+ stale $25-$32)'),
    0x103E: ('dsp_init', 'DSP reset, DIR = $31 ($3100), per-voice defaults, load 8 channel pointers from song header'),
    0x1123: ('dsp_flg_20', 'FLG = $20 (echo write off)'),
    0x112A: ('sfx_start', 'a = sfx id, x = channel: pointer from $2412 (id < $60) or $2E96 (id - $60), voice x|8'),
    0x11CC: ('pitch_table', '98 words, DSP pitch per semitone, index = (note + $24 + transpose) * 2'),
    0x11CD: ('pitch_table_tail', 'remaining words of pitch_table; split into its own data run by the emitter'),
}

# RAM / variable names, shown as comments beside instructions that reference them.
RAM_NAMES = {
    0x0000: 'tmp0', 0x0001: 'tmp1', 0x0002: 'tmp2', 0x0003: 'tmp3', 0x0004: 'tmp4',
    0x000C: 'note_e0[x]', 0x0014: 'note_e1[x]',
    0x001C: 'play_flag', 0x001D: 'mono_flag', 0x001E: 'tempo_acc', 0x001F: 'tempo',
    0x0020: 'tick_music', 0x0021: 'tempo_acc2', 0x0022: 'tempo2', 0x0023: 'tick_sfx',
    0x0024: 'gate[x]', 0x0034: 'duration[x]', 0x0044: 'seq_ptr_lo[x]', 0x0054: 'seq_ptr_hi[x]',
    0x0064: 'finetune[x]', 0x0074: 'pitch_hi[x]', 0x0084: 'pitch_lo[x]', 0x0094: 'slide_count[x]',
    0x00A4: 'vib_count[x]', 0x00B4: 'vib_step[x]', 0x00C4: 'vib_delay[x]', 0x00D4: 'seq_sp[x]',
    0x00E4: 't0_target', 0x00E5: 'song_ptr', 0x00E7: 'var_E7', 0x00E8: 'var_E8',
    0x00E9: 'port_counter', 0x00EA: 'loader_count', 0x00EC: 'pitch_offset', 0x00EE: 'pitch_offset_prev',
    0x0100: 'slide_timer[x]', 0x0110: 'chan_active[x]', 0x0120: 'note_len[x]', 0x0130: 'note_gate[x]',
    0x0140: 'transpose[x]', 0x0150: 'chan_flags[x]', 0x0160: 'slide_delay[x]', 0x0170: 'slide_rate[x]',
    0x0180: 'slide_steps[x]', 0x0190: 'slide_hold[x]', 0x01A0: 'slide_wait[x]', 0x01B0: 'slide_delta[x]',
    0x01C0: 'slide_hold_ctr[x]', 0x01D0: 'gate_mode[x]', 0x01E0: 'sfx_override[x]',
    0x0200: 'vib_rate[x]', 0x0210: 'vib_speed[x]', 0x0220: 'vib_delay_init[x]', 0x0230: 'mvol_lr',
    0x0232: 'evol_lr', 0x0234: 'vib_depth[x]', 0x0244: 'srcn[x]', 0x0254: 'vol_l[x]', 0x0264: 'vol_r[x]',
    0x0274: 'adsr1[x]', 0x0284: 'adsr2[x]', 0x0294: 'echo_on[x]',
    0x02A4: 'trem_delay[x]', 0x02B4: 'trem_count[x]', 0x02C4: 'trem_rate[x]', 0x02D4: 'trem_delta[x]',
    0x02E4: 'trem_steps[x]', 0x02F4: 'trem_len[x]', 0x0314: 'sfx_vol_l[x]', 0x0324: 'sfx_vol_r[x]',
    0x0334: 'seq_stack_lo[16*x]', 0x03B4: 'seq_stack_hi', 0x0434: 'seq_stack_count',
    0x04B4: 'noise_clock', 0x04B5: 'flg_echo_off', 0x04B6: 'master_percent', 0x04B7: 'esa_value',
    0x04B8: 'vol_preset_l', 0x04B9: 'vol_preset_r', 0x04BA: 'vol_preset2_l', 0x04BB: 'vol_preset2_r',
    0x0539: 'loader_dest (self-modified operand)', 0x0542: 'loader_dest2 (self-modified operand)',
    0x053A: 'loader_dest+1', 0x0543: 'loader_dest2+1',
    0x055D: 'cmd_param', 0x0560: 'sample_remap[256] (uploaded by 65816 loc_C18288)',
    0x1300: 'song_header (uploaded per command from data_C210B9)',
    0x1312: 'song_table (word pointers, indexed by cmd 3 param)',
    0x1313: 'song_table+1',
    0x2410: 'sfx_bank1_count', 0x2412: 'sfx_bank1_ptrs (uploaded at spc_init from $C22E5C)',
    0x2E94: 'sfx_bank2_count', 0x2E96: 'sfx_bank2_ptrs (uploaded per command from data_C210EE)',
}

# Extra trace roots that are not discoverable statically, with the reason.
EXTRA_ROOTS = [
    (0x0672, 'call', 'loader jmp ($0539+x): operand self-modified to $0672 by sub_C1803E on the 65816 side'),
]

# ---------------------------------------------------------------------------
# Image loading
# ---------------------------------------------------------------------------
BLOCKS = [
    # (file offset, byte length, spc address, description)
    (0x20000, 0x88, 0x04D8, 'IPL loader (spc_ipl_upload_loader)'),
    (0x20088, 0x699 * 2, 0x0560, 'main driver (spc_upload_driver, 0x699 words)'),
]


class Image:
    def __init__(self, rom):
        self.ram = bytearray(0x10000)
        self.loaded = bytearray(0x10000)   # 1 where a block was loaded
        self.ranges = []
        for off, ln, addr, desc in BLOCKS:
            self.ram[addr:addr + ln] = rom[off:off + ln]
            for a in range(addr, addr + ln):
                self.loaded[a] = 1
            self.ranges.append((addr, addr + ln, off, desc))

    def in_image(self, a):
        return 0 <= a < 0x10000 and self.loaded[a]

    def b(self, a):
        return self.ram[a & 0xFFFF]

    def w(self, a):
        return self.ram[a & 0xFFFF] | (self.ram[(a + 1) & 0xFFFF] << 8)


# ---------------------------------------------------------------------------
# Tracer
# ---------------------------------------------------------------------------
class Insn:
    __slots__ = ('addr', 'code', 'length', 'operands', 'target', 'text')

    def __init__(self, addr, code, length, operands, target):
        self.addr = addr
        self.code = code
        self.length = length
        self.operands = operands    # list of (kind, value)
        self.target = target        # flow target address or None


class Tracer:
    def __init__(self, img):
        self.img = img
        self.insns = {}            # addr -> Insn
        self.owner = {}            # byte addr -> insn start addr
        self.labels = {}           # addr -> kind ('sub', 'loc', 'jtab', 'data')
        self.callers = {}          # addr -> set(callers)
        self.refs = {}             # data addr -> set(insn addrs) (abs/dp operands)
        self.jump_tables = {}      # table addr -> list of targets
        self.conflicts = []
        self.notes = []
        self.roots = []
        self.work = []

    # -- labels -----------------------------------------------------------
    def label(self, addr, kind, src=None):
        prio = {'data': 0, 'loc': 1, 'jtab': 2, 'sub': 3}
        cur = self.labels.get(addr)
        if cur is None or prio[kind] > prio[cur]:
            self.labels[addr] = kind
        if kind == 'sub' and src is not None:
            self.callers.setdefault(addr, set()).add(src)

    def name(self, addr):
        if addr in NAMES:
            return NAMES[addr][0]
        kind = self.labels.get(addr, 'loc')
        return '%s_%04X' % ({'sub': 'sub', 'loc': 'loc', 'jtab': 'jtab', 'data': 'data'}[kind], addr)

    # -- decoding ---------------------------------------------------------
    def decode(self, addr):
        code = self.img.b(addr)
        text, kinds = OPS[code]
        p = addr + 1
        ops = []
        for k in kinds:
            if _KIND_LEN[k] == 1:
                v = self.img.b(p)
                p += 1
            else:
                v = self.img.w(p)
                p += 2
            ops.append((k, v))
        length = p - addr
        target = None
        for k, v in ops:
            if k == 'rel':
                target = (addr + length + (v - 256 if v >= 128 else v)) & 0xFFFF
        if code in (FLOW_JMP_ABS, FLOW_CALL):
            target = ops[0][1]
        elif code == FLOW_PCALL:
            target = 0xFF00 | ops[0][1]
        elif code in FLOW_TCALL:
            target = self.img.w(0xFFC0 + (15 - (code >> 4)) * 2)
        return Insn(addr, code, length, ops, target)

    def push(self, addr, kind, src):
        if not self.img.in_image(addr):
            self.notes.append('flow target $%04X from $%04X is outside the loaded image; not followed' % (addr, src))
            return
        self.label(addr, kind, src)
        self.work.append(addr)

    def add_root(self, addr, kind, why):
        self.roots.append((addr, why))
        self.push(addr, kind, None)

    def run(self):
        while self.work:
            addr = self.work.pop()
            self.trace_from(addr)
        # Data labels recorded before the instruction that owns that byte was
        # decoded (e.g. the loader's self-modified operands) are dropped.
        for a in [a for a, k in self.labels.items() if k == 'data' and a in self.owner]:
            del self.labels[a]

    def trace_from(self, addr):
        while True:
            if addr in self.insns:
                return
            if addr in self.owner:
                self.conflicts.append('$%04X is inside instruction at $%04X; refusing to decode' % (addr, self.owner[addr]))
                return
            if not self.img.in_image(addr):
                self.notes.append('ran off the loaded image at $%04X' % addr)
                return
            ins = self.decode(addr)
            for a in range(addr, addr + ins.length):
                if a in self.owner or not self.img.in_image(a):
                    self.conflicts.append('instruction at $%04X overlaps $%04X (owned by $%04X)' % (addr, a, self.owner.get(a, -1)))
                    return
            for a in range(addr, addr + ins.length):
                self.owner[a] = addr
            self.insns[addr] = ins
            self.record_refs(ins)
            code = ins.code
            if code in FLOW_STOP:
                return
            if code == FLOW_JMP_ABS or code == FLOW_BRA:
                self.push(ins.target, 'loc', addr)
                return
            if code == FLOW_JMP_TABLE:
                self.read_jump_table(ins)
                return
            if code == FLOW_CALL or code == FLOW_PCALL or code in FLOW_TCALL:
                self.push(ins.target, 'sub', addr)
            elif code in FLOW_BRANCH:
                self.push(ins.target, 'loc', addr)
            addr += ins.length

    def record_refs(self, ins):
        for k, v in ins.operands:
            if k == 'abs':
                self.refs.setdefault(v, set()).add(ins.addr)
                if self.img.in_image(v) and v not in self.owner and v not in self.insns and ins.code not in (FLOW_CALL, FLOW_JMP_ABS, FLOW_JMP_TABLE):
                    self.label(v, 'data')
            elif k == 'mem':
                self.refs.setdefault(v & 0x1FFF, set()).add(ins.addr)

    def read_jump_table(self, ins):
        base = ins.operands[0][1]
        targets = []
        a = base
        while self.img.in_image(a + 1):
            if a in self.owner and a != base:
                break                      # ran into code
            t = self.img.w(a)
            if t == 0:
                targets.append(None)       # null entry (stale/unused opcode)
                a += 2
                continue
            if not self.img.in_image(t):
                break
            if t in self.owner and t not in self.insns:
                break                      # would land mid-instruction
            if self.img.b(t) in (0x00, 0xFF) and t not in self.insns:
                break                      # nop/stop as first opcode: implausible
            targets.append(t)
            a += 2
            if len(targets) >= 64:
                break
        while targets and targets[-1] is None:
            targets.pop()
        if not [t for t in targets if t is not None]:
            self.notes.append('jmp ($%04X+x) at $%04X: no plausible table entries (operand is probably self-modified)' % (base, ins.addr))
            return
        self.jump_tables[base] = targets
        self.label(base, 'jtab')
        for t in targets:
            if t is not None:
                self.push(t, 'loc', ins.addr)

    # -- formatting -------------------------------------------------------
    def fmt_operand(self, ins, kind, val):
        if kind == 'dp':
            if val in HWREGS:
                return '!' + HWREGS[val]
            return '$%02X' % val
        if kind == 'imm':
            return '$%02X' % val
        if kind == 'abs':
            return '$%04X' % val
        if kind == 'up':
            return '$%02X' % val
        if kind == 'mem':
            return '$%04X.%d' % (val & 0x1FFF, val >> 13)
        if kind == 'rel':
            return self.name(ins.target)
        raise ValueError(kind)

    def fmt(self, ins):
        text, kinds = OPS[ins.code]
        vals = [self.fmt_operand(ins, k, v) for k, v in ins.operands]
        d = {'rel': self.name(ins.target) if ins.target is not None else '?'}
        for i, v in enumerate(vals):
            d[str(i)] = v
        s = text
        for key, v in d.items():
            s = s.replace('{%s}' % key, v)
        if ins.code in (FLOW_CALL, FLOW_JMP_ABS) and self.img.in_image(ins.target):
            s = s.replace('$%04X' % ins.target, self.name(ins.target))
        if ins.code == FLOW_JMP_TABLE and ins.operands[0][1] in self.jump_tables:
            s = s.replace('$%04X' % ins.operands[0][1], self.name(ins.operands[0][1]))
        return s

    def comment(self, ins):
        parts = []
        # DSP register access: mov $F2,#$xx names the DSP register.
        if ins.code == 0x8F and ins.operands[1][1] == 0xF2:
            r = ins.operands[0][1]
            parts.append('DSP %s' % DSP_REGS.get(r, '$%02X' % r))
        for k, v in ins.operands:
            if k == 'abs' and v in self.labels and v != ins.target and v not in self.jump_tables:
                parts.append(self.name(v))
            if k in ('abs', 'dp') and v in RAM_NAMES and not (k == 'dp' and v in HWREGS):
                parts.append(RAM_NAMES[v])
        return '; '.join(parts)


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------
HALF_BANK = 0x8000


def half_name(f):
    """data/<NN>.bin holds file offsets NN*0x8000 .. +0x7FFF (see tools/extract.py)."""
    return '%02X.bin' % (f >> 15)


def incbin_lines(file_off, size, spc_addr, tag=''):
    """asar `incbin "../data/NN.bin":$lo..$hi` lines (hi exclusive, relative to the
    half-bank file) covering `size` ROM bytes at `file_off`, split at half-bank borders
    since asar refuses a range that crosses one.  Paths are relative to spc/driver.asm."""
    lines = []
    while size > 0:
        lo = file_off & (HALF_BANK - 1)
        n = min(size, HALF_BANK - lo)
        c = '; %d byte%s  SPC $%04X-$%04X' % (n, '' if n == 1 else 's', spc_addr, spc_addr + n - 1)
        if tag:
            c += '  ' + tag
        lines.append('    %-40s %s' % ('incbin "../data/%s":$%04X..$%04X' % (half_name(file_off), lo, lo + n), c))
        file_off += n
        spc_addr += n
        size -= n
    return lines


def emit_asm(tr, path):
    img = tr.img
    out = []
    w = out.append
    w('; Dream: Land of Giants (Rare, 1995, SNES prototype) SPC700 sound driver, traced by tools/trace_spc700.py')
    w('; Source: DREAM.sfc file 0x20000 (loader, 0x88 bytes -> $04D8) and 0x20088')
    w(';         (driver, 0x699 words -> $0560).  Data runs (tables, the sample remap')
    w(';         block, unreached bytes) are incbin ranges into data/04.bin (file')
    w(';         0x020000-0x027FFF, produced by tools/extract.py from your own ROM;')
    w(';         ranges are relative to that file, end exclusive).')
    w('; Assemble-as-image: `asar` with norom + org writes each block at its SPC address.')
    w('norom')
    w('arch spc700')
    w('')
    w('; SPC700 I/O registers')
    for a in sorted(HWREGS):
        w('!%-9s = $%02X' % (HWREGS[a], a))
    w('')
    for start, end, off, desc in img.ranges:
        w('')
        w('; ---- %s: file 0x%05X, SPC $%04X-$%04X ----' % (desc, off, start, end - 1))
        w('org $%04X' % start)
        a = start
        while a < end:
            if a in tr.labels or a in NAMES:
                if a in NAMES:
                    if tr.labels.get(a) == 'sub' or a not in tr.insns:
                        w('')
                    w('%s: ; %s' % (NAMES[a][0], NAMES[a][1]) if NAMES[a][1] else '%s:' % NAMES[a][0])
                elif tr.labels[a] == 'sub':
                    w('')
                    w('%s:' % tr.name(a))
                else:
                    w('%s:' % tr.name(a))
            if a in tr.insns:
                ins = tr.insns[a]
                c = tr.comment(ins)
                line = '    %-28s ; %04X' % (tr.fmt(ins), a)
                if c:
                    line += ' ' + c
                w(line.rstrip())
                a += ins.length
                if ins.code in FLOW_STOP or ins.code in (FLOW_JMP_ABS, FLOW_BRA, FLOW_JMP_TABLE):
                    w('')
                continue
            if a in tr.jump_tables:
                targets = tr.jump_tables[a]
                n = 0
                while n < len(targets):
                    t = targets[n]
                    if t is not None:
                        w('    dw %-25s ; %04X  [%02X]' % (tr.name(t), a, n))
                        a += 2
                        n += 1
                        continue
                    # null entries are not symbolic: pull them from the data file
                    m = n
                    while m < len(targets) and targets[m] is None:
                        m += 1
                    for line in incbin_lines(off + (a - start), 2 * (m - n), a,
                                             'null entries [%02X]-[%02X]' % (n, m - 1) if m - n > 1 else 'null entry [%02X]' % n):
                        w(line)
                    a += 2 * (m - n)
                    n = m
                continue
            # data run until next label/insn/jump table
            run_end = a + 1
            while run_end < end and run_end not in tr.insns and run_end not in tr.labels \
                    and run_end not in tr.jump_tables:
                run_end += 1
            for line in incbin_lines(off + (a - start), run_end - a, a):
                w(line)
            a = run_end
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


PROTOCOL_NOTES = """
=== SPC-side protocol (inferred; 65816 side: out/dream.asm spc_ipl_upload_loader $C1805A,
    spc_send_words $C18324, sub_C180FF $C180FF, spc_command $C183CE, sub_C1815F $C1815F) ===

Handshake (both loader and driver): port0 ($F4) carries a running counter. The SPC keeps its
expected value in $E9 (loader: x register while inside the loader, saved to $E9 on exit).
  65816: wait until APUIO0 == X; write data to APUIO1/APUIO2; X++; write X to APUIO0.
  SPC:   wait until $F4 == counter; read $F5/$F6; write counter to $F4 (ack); counter++.
The counter is not reset between loader and driver, so the same sequence number space is
used for uploads and commands.  It is 8-bit and wraps.

Loader ($04D8-$055C, entered by the IPL jump):
  1. clrp, sp=$FF, port0=0, $E9=1, clear $D000-$FFFF (echo buffer area).
  2. loader_reset_dsp ($04F3): DSP FLG=$FF (mute, echo off, reset), EDL=0, ESA=$FF,
     $04B7=$FF (esa_value).  Driver command 7 jumps back here (with the DSP muted first).
  3. loader_block_loop ($050A): one handshake reads a 16-bit DEST address (movw ya,$F5:
     A=port1=low, Y=port2=high); it is patched into the operands of the two
     `mov $xxxx+y,a` at $0538/$0541 ($0539/$053A and $0542/$0543).  A second handshake
     reads the WORD COUNT ($EA/$EB).
       count == 0  -> loader_jump ($0556): $E9 = counter, x = 0, `jmp ($0539+x)`
                      = jump to DEST.  The 65816 sends DEST=$0672 (sub_C1803E) after
                      every upload batch, which (re)starts the driver at driver_entry.
       count  > 0  -> each handshake delivers one word (port1 low byte, port2 high byte)
                      to DEST+y, DEST+y+1; the abs operand high byte is bumped when y wraps.
                      Then back to the block loop.
  Matches spc_send_words: (addr, count, count words), each with the counter handshake.

Driver command port protocol (main_loop $0683 / cmd_receive $068C):
  Polled, not interrupt driven: the loop alternates between checking port0 and, while
  play_flag ($1C) is set, waiting one timer-0 tick and stepping the 16 channel slots.
  A command is: port1 = command byte, port2 = parameter byte (-> cmd_param $055D).
    cmd < $80         : play_sfx ($0773): sfx id = cmd, channel = param (0-7) -> sfx_start.
                        id < $60 uses sfx_bank1_ptrs ($2412+2*id, count at $2410,
                        uploaded at spc_init from $C22E5C); otherwise sfx_bank2_ptrs
                        ($2E96+2*(id-$60), uploaded per song from data_C210EE).
                        The sfx runs on slot x|8 and overrides music voice x
                        (sfx_override $01E0+x) until it ends.
    cmd >= $80        : cmd & 7 selects cmd_table ($06A7):
      0 ($F8) cmd0_set_E8        $E8 = param (no other traced reader)
      1 ($F9) cmd1_set_E7        $E7 = param (no other traced reader; 65816 orphan_C183F1 sends $F9)
      2 ($FA) cmd2_set_mono      mono_flag $1D = param (volume L/R averaged)
      3 ($FB) cmd3_fade_and_song ramp all DSP volumes to 0 over 0x7F loops, then
                                 start_song: song_table[$1312 + 2*param] -> $E5/$E6,
                                 driver_init (stale 65816 bytes at $C18403 send $FB)
      4 ($FC) cmd4_pitch_offset  pitch_offset $EC/$ED = sign-extended param * 8
                                 (applied to sfx slot $0D in channel_update); EON bit5 cleared
      5 ($FD) cmd5_voice5_volume rescale DSP V5 VOL_L/VOL_R by param percent
      6 ($FE) cmd6_play          play_flag = 1, CONTROL = 0 (timers restarted at tick_wait).
                                 spc_command ends with cmd $FE.
      7 ($FF) cmd7_stop_to_loader param != 0: jump to loader_reset_dsp immediately.
                                 param == 0: KOFF all, wait ~200 timer-1 ticks (T1=$C8),
                                 FLG=$A0, EON=0, EVOL=0, dsp_init, then loader_reset_dsp.
                                 spc_command starts with cmd $FF, param 0: the driver hands
                                 control back to the loader so the 65816 can upload the new
                                 song block ($1300), sample remap ($0560), directory
                                 ($3100), samples ($3400+) and sfx bank 2 ($2E94), then it
                                 sends DEST=$0672/count 0 (restart) and cmd $FE (play).
  spc_init sequence: loader upload, driver upload ($0560), samples/dir (sub_C180C1),
  sfx bank 1 ($2410), then jump $0672.  Game then calls spc_command(1) (song 1 set).

Timing: tick_wait ($0781) programs T0TARGET = t0_target ($E4 = 100 -> 8kHz/100 = 80 Hz)
  and waits for T0OUT.  Each tick adds tempo ($1F, from song header byte 16) into
  tempo_acc ($1E); the carry (tick_music $20) decides whether the sequencer advances
  (seq_step) or only per-tick effects run (channel_update).  $21/$22/$23 do the same for
  the sfx slots (tempo2 from song header byte 17, $FF in the shipped songs).

=== Memory layout seen by the driver ===
  $0000-$0004  temporaries          $000C/$0014  stored notes for events $E0/$E1
  $001C-$0023  play flag, mono flag, tempo accumulators
  $0024-$00E3  per-slot byte arrays, 16 slots (0-7 music, 8-15 sfx): gate, duration,
               seq pointer lo/hi, finetune, pitch lo/hi, slide/vibrato counters, seq_sp
  $00E4-$00EF  t0 target, song pointer, $E7/$E8, port counter, loader count, pitch offset
  $0100-$02FF  per-slot arrays (16 bytes each): slide, note length/gate, transpose, flags,
               vibrato, SRCN, volume, ADSR, echo, tremolo
  $0334/$03B4/$0434  sequencer call stack (lo/hi/count), 8 entries per slot via seq_sp
  $04B4-$04BB  noise clock, echo-off flag, master percent, ESA, volume presets
  $04D8-$055C  loader (stays resident)    $055D  cmd_param
  $0560-$065F  sample_remap[256]: sample number in song data -> directory slot
  $0660-$11CB  driver code               $11CC-$128F  pitch_table (98 words)
  $1300        song block: 8 channel pointers, tempo, tempo2, then song_table ($1312) and
               sequence data (uploaded per spc_command from data_C210B9)
  $2410/$2412  sfx bank 1 count/pointers (spc_init, $C22E5C)   $2E94/$2E96  sfx bank 2
  $3100        sample directory (DSP DIR = $31, dsp_init $1094)   $3400+  BRR samples
  $D000-$FFFF  cleared by the loader; echo buffer at ESA = $FF - EDL*8 (seq_echo_delay)

=== Sequence data format (seq_fetch $0850 / seq_note $0867) ===
  Event byte >= $80: note.  $80 = rest (key off).  $E0/$E1 = play stored note $0C+x/$14+x.
    Other values: pitch = pitch_table[(note + $24 + transpose[x]) * 2] with finetune[x]
    ($64+x, signed, 1/256 semitone steps via mul) applied; writes VOL/PITCH/SRCN/ADSR/GAIN
    and KON for the voice unless an sfx overrides it.  Duration: from note_len[x] if set
    (seq cmd $07), else one inline byte (two bytes: duration, gate when gate_mode $01D0+x).
  Event byte < $80: command, dispatched through seq_cmd_table ($0FD8):
    $00 end of track            $01 instrument (1)         $02 volume L,R (2)
    $03 jump (addr)             $04 call (count, addr)     $05 return / loop end
    $06 note length (1 or 2)    $07 inline lengths         $08 slide up (5)
    $09 slide down (5)          $0A slide off              $0B tempo (1)
    $0C tempo add (1)           $0D vibrato (3)            $0E vibrato off
    $0F vibrato+delay (4)       $10 ADSR (2)               $11 master volume L,R (2)
    $12 finetune (1)            $13 transpose (1)          $14 transpose add (1)
    $15 echo EFB,EVOLL,EVOLR (3)  $16 echo on   $17 echo off   $18 FIR x8 (8)
    $19 noise clock (1)         $1A noise on               $1B noise off
    $1C set note for $E0 (1)    $1D set note for $E1 (1)   $1E volume presets (4)
    $1F echo delay (1)          $20 volume from preset     $21 call once (addr)
    $22 instrument full (7)     $23 volume mono (1)        $24 master percent (1)
    (operand byte counts in parentheses; handlers set tmp0 = total event length)
    $25-$32: stale entries (nulls and orphan handlers), see jump table listing.
""".strip('\n')


def emit_map(tr, path):
    img = tr.img
    out = []
    w = out.append
    w('SPC700 code map for DREAM.sfc sound driver (tools/trace_spc700.py)')
    w('')
    w('Loaded blocks:')
    for start, end, off, desc in img.ranges:
        w('  $%04X-$%04X  file 0x%05X  %s' % (start, end - 1, off, desc))
    w('')
    w('Trace roots:')
    for a, why in tr.roots:
        w('  $%04X  %s' % (a, why))
    w('')
    # code ranges
    code_bytes = sum(i.length for i in tr.insns.values())
    total = sum(e - s for s, e, _, _ in img.ranges)
    w('Bytes: %d code, %d data/unreached, %d total' % (code_bytes, total - code_bytes, total))
    w('Instructions: %d; subroutines (call targets): %d; labels: %d' % (
        len(tr.insns), sum(1 for k in tr.labels.values() if k == 'sub'), len(tr.labels)))
    w('')
    w('Code ranges:')
    starts = sorted(tr.insns)
    i = 0
    while i < len(starts):
        s = starts[i]
        e = s + tr.insns[s].length
        while i + 1 < len(starts) and starts[i + 1] == e:
            i += 1
            e = starts[i] + tr.insns[starts[i]].length
        w('  $%04X-$%04X  (%d bytes)' % (s, e - 1, e - s))
        i += 1
    w('')
    table_bytes = set()
    for base, targets in tr.jump_tables.items():
        table_bytes.update(range(base, base + 2 * len(targets)))
    w('Data / unreached ranges inside the loaded blocks (jump tables excluded):')
    for start, end, _, _ in img.ranges:
        a = start
        while a < end:
            if a in tr.owner or a in table_bytes:
                a += 1
                continue
            b = a
            while b < end and b not in tr.owner and b not in table_bytes:
                b += 1
            w('  $%04X-$%04X  (%d bytes)%s' % (a, b - 1, b - a, '  ' + tr.name(a) if a in tr.labels else ''))
            a = b
    w('')
    w('Subroutines (call targets) with callers:')
    for a in sorted(x for x, k in tr.labels.items() if k == 'sub'):
        callers = sorted(c for c in tr.callers.get(a, ()) if c is not None)
        # size: contiguous instructions until next sub label
        w('  %-14s $%04X  callers: %s' % (tr.name(a), a, ', '.join('$%04X' % c for c in callers) or '(root)'))
    w('')
    w('Jump tables (jmp (abs+x)):')
    for base, targets in sorted(tr.jump_tables.items()):
        users = sorted(i.addr for i in tr.insns.values() if i.code == FLOW_JMP_TABLE and i.operands[0][1] == base)
        w('  %s at $%04X, %d entries, used at %s' % (tr.name(base), base, len(targets), ', '.join('$%04X' % u for u in users)))
        for n, t in enumerate(targets):
            if t is None:
                w('    [$%02X] (null)' % n)
            else:
                w('    [$%02X] $%04X  %s' % (n, t, tr.name(t)))
    w('')
    w('I/O register accesses:')
    for a in sorted(tr.insns):
        ins = tr.insns[a]
        hits = [HWREGS[v] for k, v in ins.operands if k == 'dp' and v in HWREGS]
        if hits:
            w('  %04X  %-28s %s' % (a, tr.fmt(ins), tr.comment(ins)))
    w('')
    w('Absolute references to non-code addresses (variables / tables):')
    for a in sorted(tr.refs):
        if a in tr.insns:
            continue
        users = sorted(tr.refs[a])
        tag = ''
        if img.in_image(a):
            tag = ' (in image%s)' % (', inside instruction at $%04X' % tr.owner[a] if a in tr.owner else '')
        w('  $%04X%s  from %s' % (a, tag, ', '.join('$%04X' % u for u in users[:12]) + (' ...' if len(users) > 12 else '')))
    w('')
    if tr.conflicts:
        w('Conflicts:')
        for c in tr.conflicts:
            w('  ' + c)
        w('')
    if tr.notes:
        w('Notes:')
        for n in tr.notes:
            w('  ' + n)
        w('')
    w(PROTOCOL_NOTES)
    w('')
    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def main():
    rom_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'baserom', 'DREAM.sfc')
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'spc')
    with open(rom_path, 'rb') as f:
        rom = f.read()
    img = Image(rom)
    tr = Tracer(img)
    tr.add_root(0x04D8, 'sub', 'IPL jump target (spc_ipl_upload_loader writes $04D8 to APUIO2)')
    for a, kind, why in EXTRA_ROOTS:
        tr.add_root(a, kind, why)
    tr.run()
    os.makedirs(out_dir, exist_ok=True)
    emit_asm(tr, os.path.join(out_dir, 'driver.asm'))
    emit_map(tr, os.path.join(out_dir, 'spc_map.txt'))
    code_bytes = sum(i.length for i in tr.insns.values())
    total = sum(e - s for s, e, _, _ in img.ranges)
    print('instructions %d, code bytes %d, data bytes %d, subs %d, jump tables %d, conflicts %d' % (
        len(tr.insns), code_bytes, total - code_bytes,
        sum(1 for k in tr.labels.values() if k == 'sub'), len(tr.jump_tables), len(tr.conflicts)))
    for c in tr.conflicts:
        print('CONFLICT', c)
    for n in tr.notes:
        print('NOTE', n)


if __name__ == '__main__':
    main()
