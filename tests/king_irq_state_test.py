#!/usr/bin/env python3
"""K14 as the console sees it: the interrupt controller / VDC / timer state left by K13 (DIAG9, 2026-11-01).

Hardware photo (/home/anonymous/CD/diag10, K14):
    IRQ asserted   now 000C  ack 000C  +3 frames 0004  +3 s 0004      (bit3 = VDC-A, bit2 = KING)
    mask 0031, priorities 0977 / 0053, KING SR 400E in every snapshot  (bit 14 = SCSI-phase interrupt latch)
    timer control 0006 (enabled, IRQ not enabled, zero flag SET)
    180 frames: VDC vertical-blank flag seen 0 times in A and B  (K13's setup_video writes VDC CR = BB: VBE off)
    IRQ sources seen asserted over the 180 frames: 0004 (KING only; the timer source never asserted)

What it pins in the emulator:
  * the KING SCSI-interrupt latch is NOT cleared when the player turns SCSI DMA mode off (the old "HACK(probably)"),
    so the KING IRQ stays asserted until somebody reads SCSI register 7;
  * the timer's zero flag (control D2) sets on underflow whether or not its IRQ enable (D0) is set; the IRQ line needs D0.
Runs lab page "K13 then K14 then K15 then the tests" (PCFV_LAB_ONLY=27), pressing RUN to advance.
"""
import os, struct, subprocess, sys, tempfile

PLAYER = os.environ.get('RAINBOW_PLAYER_DIR', '/mnt/file/RAINBOW_PLAYER')
TOOLKIT = os.environ.get('PCFX_TOOLKIT_ROOT', '/home/anonymous/Documents/DEV/pcfx_toolchain')
EMU = os.environ.get('PCFXEMU', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pcfx-headless'))
BIOS = os.environ.get('PCFX_BIOS', os.path.expanduser('~/.pcfxemu/pcfx.rom'))
STREAM = os.environ.get('RAINBOW_STREAM', os.path.join(PLAYER, 'stream/PCFX_8sector_stereo_form2.pcfv'))
TRACK1 = os.environ.get('RAINBOW_TRACK1', os.path.expanduser('~/Downloads/RAINBOW_FORM2_DIAG6/PCFX_RAINBOW_FORM2_DIAG6_Track01.bin'))
failures = []


def check(name, got, want):
    ok = got == want
    print('%s  HW  %s: got %s%s' % ('ok  ' if ok else 'FAIL', name, got, '' if ok else '  want %s' % (want,)))
    if not ok:
        failures.append(name)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'lab27')
        subprocess.run([sys.executable, os.path.join(PLAYER, 'native/build_audio_first.py'), '--form2', '--rainbow-lab',
                        '--stream', STREAM, '--warning', TRACK1, '--toolkit', TOOLKIT, '--define', 'PCFV_LAB_ONLY=27',
                        '--output', out], cwd=os.path.join(PLAYER, 'native'), check=True, stdout=subprocess.DEVNULL)
        nm = os.path.join(TOOLKIT, 'toolchain/v810-gcc/bin/v810-nm')
        syms = {}
        for line in subprocess.run([nm, os.path.join(PLAYER, 'native/game.elf')], capture_output=True, text=True).stdout.splitlines():
            p = line.split()
            if len(p) == 3:
                syms[p[2]] = int(p[0], 16)
        cmds = os.path.join(tmp, 'cmds.txt')
        with open(cmds, 'w') as f:                    # RUN every 400 frames until K15 is on screen (K13, K14 pages)
            for t in range(200, 2800, 400):
                f.write('%d START\n%d NONE\n' % (t, t + 12))
        ram = os.path.join(tmp, 'ram.bin')
        subprocess.run([EMU, '--bios-dir', BIOS, '--frames', '3300', '--commands', cmds, '--dump', 'ram', ram, out + '.cue'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        data = open(ram, 'rb').read()

    snaps = [struct.unpack_from('<9H', data, syms['_g_k14'] + 18 * i) for i in range(4)]   # irq[4], vdca, vdcb, king, tctl, tcnt
    vd = struct.unpack_from('<2I', data, syms['_g_k14_vd'])
    seen = struct.unpack_from('<I', data, syms['_g_k14_or'])[0]
    print('== K14 after K13 ==')
    check('IRQ asserted now/ack/+3f/+3s', [s[0] for s in snaps], [0x000C, 0x000C, 0x0004, 0x0004])
    check('IRQ mask', snaps[0][1], 0x0031)
    check('IRQ priorities', (snaps[0][2], snaps[0][3]), (0x0977, 0x0053))
    check('KING SR bit 14 (SCSI interrupt latch) stays set in every snapshot', [bool(s[6] & 0x4000) for s in snaps], [True] * 4)
    check('VDC-A SR read at "ack" shows the pending vblank flag', snaps[1][4], 0x0020)
    check('timer control zero flag D2 set, IRQ enable D0 clear', snaps[3][7] & 7, 0x6)
    check('VD frames A/B', vd, (0, 0))
    check('IRQ sources seen asserted in 180 frames', seen, 0x0004)
    if failures:
        print('FAIL king_irq_state_test: ' + ', '.join(failures))
        return 1
    print('PASS king_irq_state_test')
    return 0


if __name__ == '__main__':
    sys.exit(main())
