#!/usr/bin/env python3
"""Master a 2-track PC-FX disc: MODE1 boot + Mode 2 Form 2 PCFV stream.

Requires a Form 2-compiled native player. The stream is linked as a temporary
2048-byte placeholder ONLY to stabilize the boot LBA table. The final disc
image replaces that placeholder with a separate Mode2/2352 data track.

Physical hardware compatibility needs independent testing. Track-2 access
and MODE SELECT(6) block lengths are documented emulator behavior here, not
a claim that every retail PC-FX CD-ROM accepts this mastering layout.
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

P = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(P/'tools/video_codec'))
from pcfv_adpcm import read
from form2_pack import raw_mode1_sector, raw_sector

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--stream',type=Path,default=P/'assets/intro_480i_v55_form2.pcfv')
ap.add_argument('--output-prefix',type=Path,default=P/'release/PCFX_v55_Form2_Experimental')
ap.add_argument('--toolkit',type=Path,default=Path(os.environ.get('PCFX_TOOLKIT_ROOT','/mnt/data/pcfx_toolkit')))
ap.add_argument('--skip-build',action='store_true',help='use an already-built Form 2 program')
a=ap.parse_args()
stream=a.stream.resolve()
src=read(stream)
if src['sector_bytes']!=2324 or not src['paired_fields']:
    raise SystemExit('Form 2 paired ADPCM PCFV required')
if not a.skip_build:
    env=os.environ.copy()
    env['PCFX_TOOLKIT_ROOT']=str(a.toolkit)
    subprocess.run(['make','clean'],cwd=P,check=True,env=env)
    subprocess.run(['make','program','EXTRA_CFLAGS=-DDW3_EXPERIMENTAL_PAIRED_480I -DPCFX_PCFV_FORM2'],
                   cwd=P,check=True,env=env)
P.joinpath('build').mkdir(exist_ok=True)
placeholder=P/'build/stream.pcfv'
# The raw track occupies fewer physical sectors than the legacy 2048 format.
# A placeholder of the right number of sectors keeps cdlink's LBA calculation.
physical_sectors=stream.stat().st_size//2324
placeholder.write_bytes(bytes(physical_sectors*2048))
program=P/'build/game.program.bin'
if not program.is_file() or not program.stat().st_size:
    raise SystemExit('no native V810 program available')
config=P/'build/form2_cdlink.txt'
config.write_text(
    'binary build/game.program.bin\n'
    'lbaheader build/lbas.h\n'
    'name DIGIMON WORLD 2003 FX\n'
    'maker homebrew\nmakerid HBR\ncountry 1\n'
    'version 256\ndate 20261026\nappend build/stream.pcfv\n'
)
cdlink=a.toolkit/'toolchain/bin/pcfx-cdlink-large'
for attempt in range(5):
    before=(P/'build/lbas.h').read_bytes()
    subprocess.run([str(cdlink),str(config.relative_to(P)),'build/form2_boot'],
                   cwd=P,check=True)
    if (P/'build/lbas.h').read_bytes()==before:
        break
    if a.skip_build:
        raise SystemExit('LBA header changed: rebuild native program without --skip-build')
    env=os.environ.copy();env['PCFX_TOOLKIT_ROOT']=str(a.toolkit)
    subprocess.run(['make','program','-B','EXTRA_CFLAGS=-DDW3_EXPERIMENTAL_PAIRED_480I -DPCFX_PCFV_FORM2'],
                   cwd=P,check=True,env=env)
else:
    raise SystemExit('did not converge LBA header')
match=re.search(r'#define BINARY_LBA_BUILD_STREAM_PCFV (\d+)',
                (P/'build/lbas.h').read_text())
if not match:raise SystemExit('stream LBA not found in link header')
start_lba=int(match.group(1))
boot=(P/'build/form2_boot.bin').read_bytes()
if len(boot)%2048 or start_lba*2048 > len(boot):
    raise SystemExit('cdlink output is not a valid 2048-byte-sector boot image')
output=a.output_prefix.with_suffix('.bin')
cue=a.output_prefix.with_suffix('.cue')
output.parent.mkdir(parents=True,exist_ok=True)
with output.open('wb') as out,stream.open('rb') as inp:
    for lba in range(start_lba):
        out.write(raw_mode1_sector(boot[lba*2048:(lba+1)*2048],lba))
    for i in range(physical_sectors):
        out.write(raw_sector(inp.read(2324),start_lba+i))
minute, r=divmod(start_lba,75*60)
second, frame=divmod(r,75)
cue.write_text(
    f'FILE "{output.name}" BINARY\n'
    f'  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    f'  TRACK 02 MODE2/2352\n    INDEX 01 {minute:02}:{second:02}:{frame:02}\n'
)
print(f'mixed Mode1/Mode2 image: {output}, {cue}, stream LBA={start_lba}, '
      f'{physical_sectors} Form 2 sectors; hardware unverified')
