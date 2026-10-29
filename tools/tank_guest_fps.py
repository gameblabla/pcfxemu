#!/usr/bin/env python3
"""Measure a PC-FX application's actual completed-loop FPS from guest RAM dumps.

The sample game increments `_nframe` once after presenting each completed frame and
uses a 1000 Hz IRQ-backed counter `_zda_timer_count`.  The ELF symbol addresses
are resolved dynamically; no OCR or assumption about the overlay is needed.

Usage:
  python3 tools/tank_guest_fps.py --elf tank/mytest.elf --nm /path/to/v810-nm \
    --before ram_frame_1400.bin --after ram_frame_1700.bin

This reads guest RAM only and never changes emulator timekeeping.
"""
import argparse
import json
import pathlib
import struct
import subprocess


def symbols(elf, nm):
    output = subprocess.check_output([nm, '-n', str(elf)], text=True)
    found = {}
    for line in output.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] in ('_nframe', '_zda_timer_count'):
            found[parts[2]] = int(parts[0], 16)
    if len(found) != 2:
        raise RuntimeError('Could not resolve nframe and/or zda_timer_count from ELF; check --nm and --elf')
    return found


def u32(ram, address):
    if address < 0 or address + 4 > len(ram):
        raise ValueError('ELF symbol address outside guest RAM dump')
    return struct.unpack_from('<I', ram, address)[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', type=pathlib.Path, required=True)
    p.add_argument('--nm', default='v810-nm')
    p.add_argument('--before', type=pathlib.Path, required=True)
    p.add_argument('--after', type=pathlib.Path, required=True)
    p.add_argument('--json', action='store_true')
    args = p.parse_args()
    sym = symbols(args.elf, args.nm)
    a = args.before.read_bytes()
    b = args.after.read_bytes()
    if len(a) != len(b):
        raise ValueError('RAM dump size mismatch')
    before_frames = u32(a, sym['_nframe'])
    after_frames = u32(b, sym['_nframe'])
    before_ticks = u32(a, sym['_zda_timer_count'])
    after_ticks = u32(b, sym['_zda_timer_count'])
    frame_delta = (after_frames - before_frames) & 0xffffffff
    tick_delta = (after_ticks - before_ticks) & 0xffffffff
    if tick_delta == 0:
        raise ValueError('No guest timer ticks elapsed; cannot compute FPS')
    result = {'completed_frames': frame_delta, 'timer_ticks': tick_delta,
              'fps': round(frame_delta * 1000.0 / tick_delta, 3),
              'timer_assumed_hz': 1000,
              'frame_symbol_address': f'0x{sym["_nframe"]:x}',
              'timer_symbol_address': f'0x{sym["_zda_timer_count"]:x}'}
    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f'{frame_delta} completed frames / {tick_delta} timer ticks = {result["fps"]:.3f} FPS')

if __name__ == '__main__':
    main()
