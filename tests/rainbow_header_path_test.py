#!/usr/bin/env python3
"""RAINBOW player boot-path regression in the emulator (DIAG9 labs K13 and K14, plus a plain PLAY).

K13 runs the player's own header path -- SCSI READ of the 57 header+index sectors, KING DMA into KRAM, CPU copy-out,
CRC32 against the value the build script computed from the stream, parse_pcfv_header() -- in four KING states and the
test requires every one to parse (reason code 0) with the expected CRC.  On a console the same page printed BAD STREAM
HEADER after the KING labs (DIAG7/DIAG8); the emulator never failed, so this test pins the healthy behaviour and the
format of the evidence (the on-screen "#n a b / crc" lines are what to compare with the console photo).

K14 (read-only interrupt / VDC / timer state): the emulator keeps the VDC-A vertical-blank flag and its IRQ asserted
every frame.  Console behaviour is UNKNOWN until DIAG9 K14 is photographed: pinned as EMU.

PLAY: the disc plays (a non-trivial picture on screen) -- guards the CD read pump catch-up fix (a stalled initiator used to
leave the drive in DATA IN forever, for some code layouts).
"""
import os, struct, subprocess, sys, tempfile, zlib

PLAYER = os.environ.get('RAINBOW_PLAYER_DIR', '/mnt/file/RAINBOW_PLAYER')
TOOLKIT = os.environ.get('PCFX_TOOLKIT_ROOT', '/home/anonymous/Documents/DEV/pcfx_toolchain')
EMU = os.environ.get('PCFXEMU', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'pcfx-headless'))
BIOS = os.environ.get('PCFX_BIOS', os.path.expanduser('~/.pcfxemu/pcfx.rom'))
STREAM = os.environ.get('RAINBOW_STREAM', os.path.join(PLAYER, 'stream/PCFX_8sector_stereo_form2.pcfv'))
TRACK1 = os.environ.get('RAINBOW_TRACK1', os.path.expanduser('~/Downloads/RAINBOW_FORM2_DIAG6/PCFX_RAINBOW_FORM2_DIAG6_Track01.bin'))
failures = []


def build(out, *defs):
    cmd = [sys.executable, os.path.join(PLAYER, 'native/build_audio_first.py'), '--form2', '--rainbow-lab', '--stream', STREAM,
           '--warning', TRACK1, '--toolkit', TOOLKIT, '--output', out]
    for d in defs:
        cmd += ['--define', d]
    subprocess.run(cmd, cwd=os.path.join(PLAYER, 'native'), check=True, stdout=subprocess.DEVNULL)


def symbols():
    nm = os.path.join(TOOLKIT, 'toolchain/v810-gcc/bin/v810-nm')
    syms = {}
    for line in subprocess.run([nm, os.path.join(PLAYER, 'native/game.elf')], capture_output=True, text=True).stdout.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def run(cue, frames, ram=None, shot=None, commands=None):
    cmd = [EMU, '--bios-dir', BIOS, '--frames', str(frames)]
    if ram: cmd += ['--dump', 'ram', ram]
    if shot: cmd += ['--screenshot', shot]
    if commands: cmd += ['--commands', commands]
    subprocess.run(cmd + [cue], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def expect(name, ok, detail=''):
    print('%s  %s %s' % ('ok  ' if ok else 'FAIL', name, detail))
    if not ok:
        failures.append(name)


def main():
    stream = open(STREAM, 'rb').read()
    hdr_bytes = 64 + struct.unpack_from('<I', stream, 20)[0]
    want_crc = zlib.crc32(stream[:hdr_bytes]) & 0xFFFFFFFF
    with tempfile.TemporaryDirectory() as tmp:
        # ---- K13: header path in four KING states --------------------------------------------------------------------
        build(os.path.join(tmp, 'k13'), 'PCFV_LAB_ONLY=24')
        syms = symbols()
        run(os.path.join(tmp, 'k13.cue'), 3600, ram=os.path.join(tmp, 'k13.ram'))
        ram = open(os.path.join(tmp, 'k13.ram'), 'rb').read()
        names = ['quiet KING', 'text-screen KING', 'PLAY boot as-is', 'PLAY boot+quiesce']
        for c in range(4):
            ran, ok, fail, reads, a, b, crc0, diffw, difff = struct.unpack_from('<4B5I', ram, syms['_g_k13'] + 24 * c)
            expect('K13 C%d %s: parsed' % (c, names[c]), ran == 1 and ok == 1 and fail == 0,
                   '(ran %d ok %d fail #%d)' % (ran, ok, fail))
            expect('K13 C%d crc' % c, crc0 == want_crc and reads == 1, '(crc %08X want %08X, %d CPU read(s))' % (crc0, want_crc, reads))

        # ---- K14: IRQ / VDC / timer (EMU pins) ---------------------------------------------------------------------------
        build(os.path.join(tmp, 'k14'), 'PCFV_LAB_ONLY=25')
        syms = symbols()
        run(os.path.join(tmp, 'k14.cue'), 3600, ram=os.path.join(tmp, 'k14.ram'))
        ram = open(os.path.join(tmp, 'k14.ram'), 'rb').read()
        vd_a, vd_b = struct.unpack_from('<2I', ram, syms['_g_k14_vd'])
        irq_or, frames = struct.unpack_from('<2I', ram, syms['_g_k14_or'])
        expect('K14 EMU: VDC-A vertical-blank flag set in every one of 180 frames', vd_a == 180 and frames == 180,
               '(A %d, B %d)  [console: UNKNOWN]' % (vd_a, vd_b))
        expect('K14 EMU: only VDC-A (src 4) ever asserted', irq_or == 0x0008, '(0x%04X)' % irq_or)

        # ---- PLAY: the disc plays ---------------------------------------------------------------------------------------------
        build(os.path.join(tmp, 'play'))
        cmds = os.path.join(tmp, 'play.txt')
        open(cmds, 'w').write('2700 START\n2720 NONE\n')
        shot = os.path.join(tmp, 'play.png')
        run(os.path.join(tmp, 'play.cue'), 5200, shot=shot, commands=cmds)
        raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', shot, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], capture_output=True).stdout
        colours = len(set(raw[i:i + 3] for i in range(0, len(raw), 3 * 7)))
        expect('PLAY: video on screen (not the failure screen)', colours > 500, '(%d distinct colours)' % colours)
    if failures:
        print('FAIL rainbow_header_path_test: ' + ', '.join(failures))
        return 1
    print('PASS rainbow_header_path_test')
    return 0


if __name__ == '__main__':
    sys.exit(main())
