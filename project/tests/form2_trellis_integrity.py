#!/usr/bin/env python3
"""Verify the source-measured Form 2 TRELLIS improvement and raw CD image."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

P = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(P / 'tools/video_codec'))
from pcfv_adpcm import read, sectors
from form2_pack import SYNC, edc, raw_mode1_sector

ap = argparse.ArgumentParser()
ap.add_argument('baseline', type=Path, help='original v55 PCFV')
ap.add_argument('enhanced', type=Path, help='Form 2 PCFV with TRELLIS replacements')
ap.add_argument('disc', type=Path, help='raw 2352 mixed-mode BIN')
ap.add_argument('--report',type=Path)
args = ap.parse_args()

old = read(args.baseline)
new = read(args.enhanced)
assert old['sector_bytes'] == 2048 and new['sector_bytes'] == 2324
assert old['paired_fields'] and new['paired_fields']
assert old['fps_num'] == new['fps_num'] == 15
assert old['fps_den'] == new['fps_den'] == 1
assert len(old['frames']) == len(new['frames']) == 3584
assert old['audio'] == new['audio']
changed = [i for i, (o,n) in enumerate(zip(old['frames'],new['frames'])) if o != n]
expected = [1400,1401,1402,1403]
assert changed == expected, (changed, expected)
for i in expected:
    record = P / 'tools/video_codec/trellis_candidates' / f'field_{i}.bin'
    assert new['frames'][i] == record.read_bytes()
for i,(o,n) in enumerate(zip(old['frames'], new['frames'])):
    assert sectors(len(n),2324) <= sectors(len(o),2048), i

raw = args.disc.read_bytes()
assert len(raw)%2352 == 0
logical = args.enhanced.read_bytes()
assert len(logical)%2324 == 0
track2_lba = (len(raw)//2352) - (len(logical)//2324)
assert track2_lba > 2
for i in (0,1,track2_lba-1):
    physical = raw[i*2352:(i+1)*2352]
    assert physical[:12] == SYNC and physical[15] == 1
    assert int.from_bytes(physical[2064:2068],'little') == edc(physical[:2064])
    assert physical == raw_mode1_sector(physical[16:2064], i)
for k in range(len(logical)//2324):
    physical=raw[(track2_lba+k)*2352:(track2_lba+k+1)*2352]
    assert physical[:12] == SYNC and physical[15] == 2
    assert physical[16:20] == physical[20:24]
    assert physical[18] & 0x20
    assert physical[24:2348] == logical[k*2324:(k+1)*2324]
    assert int.from_bytes(physical[2348:2352], 'little') == edc(physical[16:2348])

result = {
  'status':'PASS',
  'fields':len(old['frames']),
  'trellis_modified_fields':changed,
  'all_other_video_records_identical':True,
  'adpcm_sha256':hashlib.sha256(new['audio']).hexdigest(),
  'audio_identical':True,
  'physical_sector_size':2352,
  'form2_application_payload_size':2324,
  'mode1_boot_sectors':track2_lba,
  'mode2_form2_movie_sectors':len(logical)//2324,
  'all_raw_form2_EDC_valid':True,
  'tested_mode1_EDC_ECC_valid':True,
  'sector_budget_per_field_met':True,
}
if args.report:args.report.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
