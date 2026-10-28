#!/usr/bin/env python3
"""Host transport checks against unchanged v55: no emulator timing claims."""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

P = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(P / 'tools/video_codec'))
from pcfv_adpcm import read, sectors, HEADER, ENTRY
from form2_pack import SYNC, edc

ap = argparse.ArgumentParser()
ap.add_argument('baseline', type=Path)
ap.add_argument('form2', type=Path)
ap.add_argument('raw_track', type=Path)
ap.add_argument('--report', type=Path)
a = ap.parse_args()

old = read(a.baseline)
new = read(a.form2)
assert old['sector_bytes'] == 2048 and new['sector_bytes'] == 2324
assert old['paired_fields'] and new['paired_fields']
assert len(old['frames']) == len(new['frames']) == 3584
assert old['audio'] == new['audio'], 'ADPCM changed'
assert old['fps_num'] == new['fps_num'] and old['fps_den'] == new['fps_den']
assert old['rate'] == new['rate'] and old['stride'] == new['stride']
changed = [i for i, (x,y) in enumerate(zip(old['frames'],new['frames'])) if x != y]
# Allowed for optimized builds, but transport-only repacks must be byte-exact.
assert not changed, f'compressed video changed at {changed[:10]}'
assert all(sectors(len(x),2324) <= sectors(len(x),2048) for x in new['frames'])

raw = a.raw_track.read_bytes()
logical = a.form2.read_bytes()
assert len(raw) == len(logical) // 2324 * 2352
assert len(raw) % 2352 == 0 and len(logical) % 2324 == 0
for i in range(len(raw) // 2352):
    sec = raw[i*2352:(i+1)*2352]
    assert sec[:12] == SYNC and sec[15] == 2
    assert sec[16:20] == sec[20:24] and sec[18] & 0x20
    assert sec[24:2348] == logical[i*2324:(i+1)*2324]
    assert int.from_bytes(sec[2348:], 'little') == edc(sec[16:2348])
# Check actual host RAINBOW reconstructions, not only compressed CRCs.
from rainbow_decode import decode_stream
import numpy as np
samples = [0, 1, 2, 3, 300, 805, 806, 925, 926, 1400, 1401, 3582, 3583]
for idx in samples:
    x = decode_stream(old['frames'][idx])
    y = decode_stream(new['frames'][idx])
    assert len(x) == len(y)
    assert all(np.array_equal(u,v) for u,v in zip(x,y)), f'RAINBOW mismatch field {idx}'
duration = len(old['frames']) / 2 * old['fps_den'] / old['fps_num']
base_sectors = a.baseline.stat().st_size // 2048
form2_sectors = a.form2.stat().st_size // 2324
report = {
    'status': 'HOST_TRANSPORT_PASS_NOT_CONSOLE_VERIFIED',
    'fields': len(old['frames']),
    'decoded_fields_compared': samples,
    'decoded_frame_samples_pixel_identical': True,
    'audio_bytes_identical': True,
    'adpcm_sha256': hashlib.sha256(old['audio']).hexdigest(),
    'video_records_identical': True,
    'clip_seconds': duration,
    'sector_size_old': 2048,
    'sector_size_form2': 2324,
    'sectors_old': base_sectors,
    'sectors_form2': form2_sectors,
    'sectors_saved': base_sectors - form2_sectors,
    'sustained_logical_disc_sectors_per_second_old': base_sectors / duration,
    'sustained_logical_disc_sectors_per_second_form2': form2_sectors / duration,
    'two_x_physical_capacity_sectors_per_second': 150,
    'decoded_frame_drop_count_emulator': None,
    'audio_output_sync_emulator': None,
    'hardware_validated': False
}
if a.report:
    a.report.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
