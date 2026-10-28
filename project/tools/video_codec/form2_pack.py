#!/usr/bin/env python3
"""Repack a PCFV ADPCM stream for CD-XA Mode 2 Form 2 raw 2352-byte sectors.

A Form 2 track carries 2324 application bytes per physical sector, at the
ordinary 75 or 150 physical sectors/second.  This emits a standalone raw
track, NOT a replacement for the existing Mode 1 boot image. A disc builder
must master a Mode 2 track and preserve the stream's absolute LBA.
"""
import argparse
import json
from pathlib import Path
from pcfv_adpcm import read, write, sectors

SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
POLY = 0xD8018001

def _edc_table_item(x):
    for _ in range(8):
        x = (x >> 1) ^ (POLY if x & 1 else 0)
    return x


EDC_TABLE = tuple(_edc_table_item(i) for i in range(256))


def edc(data):
    result = 0
    for b in data:
        result = (result >> 8) ^ EDC_TABLE[(result ^ b) & 255]
    return result


def bcd(v):
    if v < 0 or v > 99:
        raise ValueError('MSF minute component not representable in BCD')
    return ((v // 10) << 4) | v % 10


def raw_sector(payload, lba, file_number=1, channel=0, coding=0):
    if len(payload) != 2324:
        raise ValueError('Form 2 payload must be exactly 2324 bytes')
    absolute = lba + 150
    minutes, left = divmod(absolute, 75 * 60)
    seconds, frames = divmod(left, 75)
    sub = bytes((file_number, channel, 0x28, coding))  # DATA | FORM2
    sec = bytearray(SYNC)
    sec += bytes((bcd(minutes), bcd(seconds), bcd(frames), 2))
    sec += sub + sub
    sec += payload
    sec += edc(sec[16:]).to_bytes(4, 'little')
    assert len(sec) == 2352
    return bytes(sec)


def _ecc_tables():
    f = [0]*256
    b = [0]*256
    for x in range(256):
        y = x << 1
        if x & 0x80:
            y ^= 0x11d
        y &= 255
        f[x] = y
        b[x ^ y] = x
    return f, b


ECC_F, ECC_B = _ecc_tables()


def _ecc_block(src, major_count, minor_count, major_mult, minor_inc):
    size = major_count * minor_count
    out = bytearray(major_count*2)
    for major in range(major_count):
        offset = ((major >> 1) * major_mult) + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            byte = src[offset]
            offset += minor_inc
            if offset >= size:
                offset -= size
            a ^= byte
            b ^= byte
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        out[major] = a
        out[major+major_count] = a ^ b
    return out


def _msf(lba):
    absolute = lba + 150
    minutes, rem = divmod(absolute, 75*60)
    seconds, frames = divmod(rem, 75)
    return bytes((bcd(minutes), bcd(seconds), bcd(frames)))


def raw_mode1_sector(payload, lba):
    """MODE1/2352 sector with CD-ROM EDC and P/Q Reed-Solomon ECC."""
    if len(payload) != 2048:
        raise ValueError('Mode 1 sector needs 2048 application bytes')
    sec = bytearray(SYNC + _msf(lba) + b'\x01' + payload)
    sec += edc(sec).to_bytes(4,'little')
    sec += bytes(8)
    # P parity is over 2064 bytes after sync; Q includes computed P parity.
    sec += _ecc_block(sec[12:2076],86,24,2,86)
    sec += _ecc_block(sec[12:2248],52,43,86,88)
    assert len(sec) == 2352
    return bytes(sec)


def make_raw_track(input_path, output_path, first_lba=0):
    info = read(input_path)
    if info['sector_bytes'] != 2324:
        raise ValueError('expected Form 2 logical PCFV, not 2048-byte legacy PCFV')
    length = Path(input_path).stat().st_size
    if length % 2324:
        raise ValueError('payload size is not a whole number of Form 2 sectors')
    total = length // 2324
    with Path(input_path).open('rb') as inp, Path(output_path).open('wb') as out:
        for i in range(total):
            out.write(raw_sector(inp.read(2324), first_lba + i))
    return dict(sectors=total, logical_bytes=length, raw_bytes=total * 2352,
                first_lba=first_lba)


def repack(baseline, logical_path, raw_path, start_lba=0, compact=True):
    base = read(baseline)
    if base['sector_bytes'] != 2048:
        raise ValueError('input must be the unchanged 2048-byte baseline')
    result = write(logical_path, base['frames'], base['stride'], base['fps_num'],
                   base['fps_den'], base['audio'], base['rate'],
                   compact=compact, paired_fields=base['paired_fields'], form2=True)
    result['raw'] = make_raw_track(logical_path, raw_path, start_lba)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, required=True, help='unchanged v55 PCFV')
    ap.add_argument('--output', type=Path, required=True, help='new 2324-byte logical PCFV')
    ap.add_argument('--raw-track', type=Path, required=True, help='2352-byte Mode 2 Form 2 BIN')
    ap.add_argument('--first-lba', type=int, default=0, help='absolute LBA used for MSF headers')
    ap.add_argument('--fixed-stride', action='store_true', help='retain reserved full-stride video slots')
    args = ap.parse_args()
    print(json.dumps(repack(args.input, args.output, args.raw_track,
                            args.first_lba, not args.fixed_stride), indent=2))


if __name__ == '__main__':
    main()
