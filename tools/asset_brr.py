#!/usr/bin/env python3
"""Decode a BRR sample asset (data/brr/sample_NN.bin, {loop_offset u16, length u16, BRR blocks})
to a 16-bit mono PCM WAV, to prove the boundaries config/assets.txt derived for the BRR chain.

    python3 tools/asset_brr.py data/brr/sample_00.bin build/sample_00.wav

Standard SNES BRR decode (4 filters, 9-byte blocks: 1 header + 16 x 4-bit nibbles), per the
widely documented DSP algorithm. Output is raw decoded PCM at the nominal APU sample rate
(32000 Hz); no per-note pitch scaling is applied, so this is a preview, not a game-accurate
mix. Nothing here is committed: run this yourself and inspect build/*.wav.
"""
import sys, struct

SAMPLE_RATE = 32000


def clamp16(v):
    if v > 32767: return 32767
    if v < -32768: return -32768
    return v


def decode_brr(data):
    samples = []
    p1 = p2 = 0
    pos = 0
    loop_flag = False
    while pos + 9 <= len(data):
        header = data[pos]
        shift = header >> 4
        filt = (header >> 2) & 3
        loop = (header >> 1) & 1
        end = header & 1
        for i in range(8):
            byte = data[pos + 1 + i]
            for nib in (byte >> 4, byte & 0xF):
                s = nib - 16 if nib >= 8 else nib
                if shift <= 12:
                    s = (s << shift) >> 1
                else:
                    s = -2048 if s < 0 else 0
                if filt == 0:
                    pred = 0
                elif filt == 1:
                    pred = p1 + ((-p1) >> 4)
                elif filt == 2:
                    pred = p1 * 2 + ((-p1 * 3) >> 5) - p2 + (p2 >> 4)
                else:
                    pred = p1 * 2 + ((-p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4)
                sample = clamp16(s + pred)
                samples.append(sample)
                p2 = p1
                p1 = sample
        pos += 9
        if loop:
            loop_flag = True
        if end:
            break
    return samples, pos, loop_flag


def write_wav(path, samples, rate=SAMPLE_RATE):
    data = struct.pack(f'<{len(samples)}h', *samples)
    with open(path, 'wb') as fp:
        fp.write(b'RIFF')
        fp.write(struct.pack('<I', 36 + len(data)))
        fp.write(b'WAVEfmt ')
        fp.write(struct.pack('<IHHIIHH', 16, 1, 1, rate, rate * 2, 2, 16))
        fp.write(b'data')
        fp.write(struct.pack('<I', len(data)))
        fp.write(data)


def main():
    in_path, out_path = sys.argv[1], sys.argv[2]
    raw = open(in_path, 'rb').read()
    loop_offset = raw[0] | (raw[1] << 8)
    length = raw[2] | (raw[3] << 8)
    blocks = raw[4:4 + length]
    samples, consumed, loop_flag = decode_brr(blocks)
    write_wav(out_path, samples, SAMPLE_RATE)
    dur = len(samples) / SAMPLE_RATE
    print(f'{in_path}: loop_offset={loop_offset} declared_len={length} block_bytes_consumed={consumed} '
          f'blocks={consumed // 9} samples={len(samples)} duration={dur:.3f}s loop_flag_seen={loop_flag} '
          f'-> {out_path} ({SAMPLE_RATE} Hz, 16-bit mono)')


if __name__ == '__main__':
    main()
