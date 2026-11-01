#!/usr/bin/env python3
"""3D tank speed-test regression (DIAG11 button V / PCFV_LAB_ONLY=28), pinned to a real console's photos (2026-11-01,
/home/anonymous/CD/diag12).  Runs the player's own lab (the tank demo's renderer, rows A..L) in pcfx-headless and compares
ms/frame per row and per stage.  Every expectation is a HARDWARE measurement; the fit is TENTATIVE (see v810_cpu.c, core.c).
Rows whose emulator error is known are listed in GAPS and only reported.
Usage: tests/run_tank_speed_lab_test.sh"""
import os, struct, subprocess, sys, tempfile

PLAYER = os.environ.get('RAINBOW_PLAYER_DIR', '/mnt/file/RAINBOW_PLAYER')
TOOLKIT = os.environ.get('PCFX_TOOLKIT_ROOT', '/home/anonymous/Documents/DEV/pcfx_toolchain')
EMU = os.environ.get('PCFXEMU', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pcfx-headless'))
BIOS = os.environ.get('PCFX_BIOS', os.path.expanduser('~/.pcfxemu/pcfx.rom'))
STREAM = os.environ.get('RAINBOW_STREAM', os.path.join(PLAYER, 'stream/PCFX_8sector_stereo_form2.pcfv'))
TRACK1 = os.environ.get('RAINBOW_TRACK1', os.path.expanduser('~/Downloads/RAINBOW_FORM2_DIAG6/PCFX_RAINBOW_FORM2_DIAG6_Track01.bin'))

# row: (total, clear, xform, raster) ms per frame on the console; None = not photographed
HW = {'A': (31.99, 21.5, 1.9, 8.5), 'B': (32.00, None, None, None), 'C': (37.70, 21.7, 3.3, 12.6),
      'D': (27.93, 17.4, 1.9, 8.5), 'E': (31.98, None, None, None), 'F': (32.61, 21.5, 1.9, 9.1),
      'G': (16.10, 4.2, 1.9, 9.9), 'H': (26.85, 9.8, 3.2, 13.7), 'I': (21.47, None, None, None),
      'J': (17.45, None, None, None), 'K': (1.96, None, None, None), 'L': (10.81, None, None, None)}
TOL = 0.05
# KING write cost is on a 4-clk dot grid: dense with 4 BG slots busy is 684 ns here vs 714.7 on HW (K2), so rows with a
# KRAM-bound clear are ~3-4 % fast.  G clear (cached ST.H RAM loop, 2.4 clk vs 2.3) is 5 % fast: same gap as K11 'RAM st.h'.
# Reported, not failed, up to this bound.
GAPS = {('G', 1): .06, ('A', 0): .045, ('A', 1): .045, ('B', 0): .045, ('E', 0): .045, ('F', 0): .045, ('I', 0): .045, ('C', 0): .045}
NAMES = 'ABCDEFGHIJKL'

tmp = tempfile.mkdtemp()
out = os.path.join(tmp, 'lab')
subprocess.run([sys.executable, os.path.join(PLAYER, 'native/build_audio_first.py'), '--form2', '--rainbow-lab', '--stream', STREAM,
                '--warning', TRACK1, '--toolkit', TOOLKIT, '--define', 'PCFV_LAB_ONLY=28', '--output', out],
               cwd=os.path.join(PLAYER, 'native'), check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
syms = {}
for line in subprocess.run([os.path.join(TOOLKIT, 'toolchain/v810-gcc/bin/v810-nm'), os.path.join(PLAYER, 'native/game.elf')],
                           capture_output=True, text=True).stdout.splitlines():
    p = line.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
ram = os.path.join(tmp, 'ram.bin')
subprocess.run([EMU, '--bios-dir', BIOS, '--frames', '1500', '--dump', 'ram', ram, out + '.cue'], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
data = open(ram, 'rb').read()
base = syms['_g_tank_res']
fail, gaps = [], []
for i, n in enumerate(NAMES):
    st = struct.unpack_from('<4I', data, base + 20 * i)
    ms = [t * 0.6984 / 1000 / 12 for t in st]
    got = (sum(ms), ms[0], ms[1], ms[2])
    for j, (g, w) in enumerate(zip(got, HW[n])):
        if w is None: continue
        err = abs(g - w) / w
        tol = TOL if w > 3 else 0.12            # tiny stage values are photographed to 0.1 ms
        gap = GAPS.get((n, j))
        ok = err <= tol
        tag = 'ok  ' if ok else ('GAP ' if gap and err <= gap else 'FAIL')
        print('%s  row %s %-6s: %.2f ms (HW %.2f, %+.1f%%)' % (tag, n, ('total', 'clear', 'xform', 'raster')[j], g, w, 100 * (g - w) / w))
        if tag == 'FAIL': fail.append((n, j))
        elif tag == 'GAP ': gaps.append((n, j))
if gaps: print('KNOWN GAPS (KING dot-grid, KRAM-bound clear):', gaps)
print('FAIL tank_speed_lab_test %s' % fail if fail else 'PASS tank_speed_lab_test')
sys.exit(1 if fail else 0)
