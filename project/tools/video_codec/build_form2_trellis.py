#!/usr/bin/env python3
"""Apply four source-validated RDO fields to v55 and write a Form 2 disc payload."""
import argparse
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/video_codec'))
from pcfv_adpcm import read,write,sectors
from form2_pack import make_raw_track

parser=argparse.ArgumentParser()
parser.add_argument('--baseline',type=Path,default=ROOT/'assets/intro_480i_v55_trellis.pcfv')
parser.add_argument('--output',type=Path,default=ROOT/'assets/intro_480i_v55_form2_trellis_enhanced.pcfv')
parser.add_argument('--raw-track',type=Path,default=ROOT/'assets/intro_480i_v55_form2_trellis_enhanced_raw2352.bin')
a=parser.parse_args()
original=read(a.baseline)
assert original['sector_bytes']==2048 and len(original['frames'])==3584
fields=list(original['frames'])
for i in (1400,1401,1402,1403):
    field=Path(__file__).parent/'trellis_candidates'/f'field_{i}.bin'
    fields[i]=field.read_bytes()
assert all(sectors(len(f),2324)<=sectors(len(o),2048) for f,o in zip(fields,original['frames']))
write(a.output,fields,4,original['fps_num'],original['fps_den'],
      original['audio'],original['rate'],compact=True,paired_fields=True,form2=True)
assert read(a.output)['audio']==original['audio']
make_raw_track(a.output,a.raw_track)
print('enhanced Form 2 PCFV',a.output, 'raw CD sectors',a.raw_track)
