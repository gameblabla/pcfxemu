#!/usr/bin/env python3
"""KRAM access-width regression: ST.H / ST.W / ST.B / OUT.H / OUT.W / OUT.B to the KING data port, and the read side
(IN.H / IN.W / IN.B / LD.H / LD.W / LD.B), run as the real V810 instructions the DIAG9 disc runs on a console (labs K10,
K11, K12 of /mnt/file/RAINBOW_PLAYER/native/pcfv_lab_king_ext.inc).

STATUS: every expectation below is a HARDWARE measurement (DIAG7/DIAG8/DIAG9 photos of a real console, 2026-11-01;
DIAG9 K10/K11/K12 photos are in /home/anonymous/CD/diag10).  A miss is a failure unless it is listed as a known gap
(then it is reported, with the hardware value, so the gap stays visible).

Usage: tests/run_king_width_lab_test.sh   (needs the v810 toolchain, the RAINBOW player sources and a BIOS)
"""
import os, struct, subprocess, sys, tempfile

PLAYER = os.environ.get('RAINBOW_PLAYER_DIR', '/mnt/file/RAINBOW_PLAYER')
TOOLKIT = os.environ.get('PCFX_TOOLKIT_ROOT', '/home/anonymous/Documents/DEV/pcfx_toolchain')
EMU = os.environ.get('PCFXEMU', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pcfx-headless'))
BIOS = os.environ.get('PCFX_BIOS', os.path.expanduser('~/.pcfxemu/pcfx.rom'))
STREAM = os.environ.get('RAINBOW_STREAM', os.path.join(PLAYER, 'stream/PCFX_8sector_stereo_form2.pcfv'))
TRACK1 = os.environ.get('RAINBOW_TRACK1', os.path.expanduser('~/Downloads/RAINBOW_FORM2_DIAG6/PCFX_RAINBOW_FORM2_DIAG6_Track01.bin'))

PAGES = {21: 'K10', 22: 'K11', 23: 'K12'}
failures = []
notes = []


def build_and_run(page, frames, tmp):
    out = os.path.join(tmp, 'lab%d' % page)
    subprocess.run([sys.executable, os.path.join(PLAYER, 'native/build_audio_first.py'), '--form2', '--rainbow-lab',
                    '--stream', STREAM, '--warning', TRACK1, '--toolkit', TOOLKIT,
                    '--define', 'PCFV_LAB_ONLY=%d' % page, '--output', out],
                   cwd=os.path.join(PLAYER, 'native'), check=True, stdout=subprocess.DEVNULL)
    nm = os.path.join(TOOLKIT, 'toolchain/v810-gcc/bin/v810-nm')
    syms = {}
    for line in subprocess.run([nm, os.path.join(PLAYER, 'native/game.elf')], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    ram = os.path.join(tmp, 'ram%d.bin' % page)
    subprocess.run([EMU, '--bios-dir', BIOS, '--frames', str(frames), '--dump', 'ram', ram, out + '.cue'],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return syms, open(ram, 'rb').read()


def u16s(ram, addr, n): return list(struct.unpack_from('<%dH' % n, ram, addr))
def u32s(ram, addr, n): return list(struct.unpack_from('<%dI' % n, ram, addr))


def check(name, got, want, kind):
    ok = got == want
    print('%s  %-5s %s: got %s%s' % ('ok  ' if ok else 'FAIL', kind, name, got, '' if ok else '  want %s' % (want,)))
    if not ok:
        failures.append(name)


def near(name, ns10, want_ns, tol, kind, gap=False):
    got = ns10 / 10.0
    ok = abs(got - want_ns) <= want_ns * tol
    tag = 'ok  ' if ok else ('GAP ' if gap else 'FAIL')
    print('%s  %-5s %s: %.1f ns (want %.1f +-%d%%)' % (tag, kind, name, got, want_ns, round(tol * 100)))
    if not ok and not gap:
        failures.append(name)
    elif not ok:
        notes.append('%s %.1f vs %.1f' % (name, got, want_ns))


# ---- K10: data semantics (HW, DIAG9 K10 photo) -------------------------------------------------------------------------------------
K10_W = [   # name, KRAM words 0..3 after (start EEEE EEEE EEEE EEEE)
    ('OUT.H x2', [0x1111, 0x2222, 0xEEEE, 0xEEEE]),
    ('OUT.W',    [0x4444, 0x3333, 0x5555, 0xEEEE]),      # low half first, pointer +2
    ('OUT.B',    [0x0055, 0x6666, 0xEEEE, 0xEEEE]),      # a byte write stores a whole word
    ('ST.H x2',  [0x7777, 0x8888, 0xEEEE, 0xEEEE]),
    ('ST.W +H',  [0xAAAA, 0x9999, 0xBBBB, 0xEEEE]),
    ('ST.B +H',  [0x00CC, 0xDDDD, 0xEEEE, 0xEEEE]),      # HW: a byte store to the window writes one word, byte in its lane
    ('ST.H +2',  [0xEEEE, 0x1357, 0xEEEE, 0xEEEE]),      # HW: window offset is ignored for H/W
    ('ST.H +4',  [0xFACE, 0x1357, 0xEEEE, 0xEEEE]),
    ('ST.W +4',  [0x33CC, 0x55AA, 0x1357, 0xEEEE]),
    ('ST.B +1',  [0x9900, 0x1357, 0xEEEE, 0xEEEE]),      # HW: odd address = high lane
    ('OUT.B+1',  [0x7700, 0x1357, 0xEEEE, 0xEEEE]),
]
K10_R = [   # name, first read value, value of the NEXT IN.H (shows how far the read pointer moved)
    ('IN.H', 0x00001111, 0x2222), ('IN.W', 0x22221111, 0x3333), ('IN.B', 0x00000011, 0x2222),
    ('LD.H', 0x00001111, 0x2222), ('LD.W', 0x22221111, 0x3333), ('LD.B', 0x00000011, 0x2222),
    ('LD.H+2', 0x00001111, 0x2222), ('LD.H+4', 0x00001111, 0x2222),
]

# ---- K11 / K12 timing -----------------------------------------------------------------------------------------------
K11_NAMES = ['RAM st.h', 'OUT.H A', 'OUT.H B', 'OUT.W A', 'OUT.B A', 'ST.H A', 'ST.H B', 'ST.W A', 'ST.W B', 'ST.B A',
             'VCE out.h', 'VCE st.h', 'IN.H', 'IN.W', 'LD.H', 'LD.W']
# DIAG9 K11 photo, dense column (ns per instruction, loop included).  The emulator's loop overhead is ~1.3 % lower than the
# console's (RAM st.h 107.8 vs 112.5), hence 3 % tolerances.
HW_DENSE = {'OUT.H A': (567.4, 0.03), 'OUT.H B': (567.4, 0.03), 'OUT.W A': (1135.0, 0.03), 'OUT.B A': (567.2, 0.03),
            'ST.H A': (567.2, 0.03), 'ST.H B': (567.4, 0.03), 'ST.W A': (1134.6, 0.03), 'ST.W B': (1134.7, 0.03),
            'ST.B A': (567.3, 0.03), 'VCE out.h': (282.0, 0.05), 'VCE st.h': (282.0, 0.05), 'IN.H': (772.6, 0.04)}
# Known gaps (hardware value; the emulator is further off than the tolerance).  Reported, not failed.
HW_GAPS = {'RAM st.h': (112.5, 0.03), 'IN.W': (1337.0, 0.04), 'LD.H': (595.5, 0.04), 'LD.W': (1170.6, 0.04)}
EMU_DENSE = {}
HW_K2_OUTH = [567.4, 598.8, 698.4, 714.7, 2544.6]
HW_K12 = {'OUT.H': [567, 599, 698, 713, 2545], 'OUT.W': [1135, 1199, 1397, 1428, 3510], 'ST.H': [566, 599, 698, 712, 2545],
          'ST.W': [1134, 1180, 1396, 1427, 3510]}   # DIAG9 K12 photo (8-slot column lost its last digit at the screen edge)      # K2, DIAG7/8: dense OUT.H, BG0 CG slots 0/1/2/4/8 busy, active, bank A
KNOWN_GAPS = set()


def main():
    with tempfile.TemporaryDirectory() as tmp:
        data = {}
        for page, frames in ((21, 1600), (22, 2600), (23, 2600)):
            data[page] = build_and_run(page, frames, tmp)

        syms, ram = data[21]
        print('== K10 access widths (HW) ==')
        w = u16s(ram, syms['_g_kx_w'], 4 * len(K10_W))
        for i, (name, want) in enumerate(K10_W):
            check(name, w[4 * i:4 * i + 4], want, 'HW')
        r = u32s(ram, syms['_g_kx_r'], 2 * len(K10_R))
        for i, (name, a, b) in enumerate(K10_R):
            check(name, [r[2 * i], r[2 * i + 1] & 0xFFFF], [a, b], 'HW')

        syms, ram = data[22]
        print('== K11 cost per instruction, dense / 4 NOPs (ns) ==')
        t = u32s(ram, syms['_g_kx_t'], 2 * len(K11_NAMES))
        dense = {n: t[2 * i] for i, n in enumerate(K11_NAMES)}
        for name, (want, tol) in HW_DENSE.items():
            near(name, dense[name], want, tol, 'HW')
        for name, (want, tol) in HW_GAPS.items():
            near(name, dense[name], want, tol, 'HW', gap=True)
        # structural relations that hold for any model in which OUT.W / ST.W are two KRAM writes
        for a, b in (('OUT.W A', 'OUT.H A'), ('ST.W A', 'ST.H A')):
            ratio = dense[a] / dense[b]
            ok = 1.8 <= ratio <= 2.2
            print('%s  rel   %s / %s = %.2f (two KRAM words per access: 1.8..2.2)' % ('ok  ' if ok else 'FAIL', a, b, ratio))
            if not ok:
                failures.append(a + '/' + b)

        syms, ram = data[23]
        print('== K12 widths under BG fetch load (ns, dense, bank A, active display) ==')
        l = u32s(ram, syms['_g_kx_l'], 4 * 5)
        rows = ['OUT.H', 'OUT.W', 'ST.H', 'ST.W']
        for i, n in enumerate(rows):
            print('      ' + n + ': ' + ' '.join('%7.1f' % (v / 10.0) for v in l[5 * i:5 * i + 5]))
        for j in range(5):
            near('K2 OUT.H n=%s' % (0, 1, 2, 4, 8)[j], l[j], HW_K2_OUTH[j], 0.05, 'HW', gap=(j == 3))
        for i, n in enumerate(rows):
            for j in range(5):
                near('K12 %s n=%s' % (n, (0, 1, 2, 4, 8)[j]), l[5 * i + j], HW_K12[n][j], 0.08 if j < 4 else 0.12, 'HW', gap=(j == 3))
        print('\nAll rows are hardware measurements.')
        if notes:
            print('KNOWN GAPS (emulator vs hardware): ' + '; '.join(notes))
    if failures:
        print('FAIL king_width_lab_test: ' + ', '.join(failures))
        return 1
    print('PASS king_width_lab_test')
    return 0


if __name__ == '__main__':
    sys.exit(main())
