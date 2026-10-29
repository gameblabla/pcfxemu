#!/usr/bin/env python3
"""Summarize *visible changes* in the PCFX headless --frame-csv output.

Changes per emulated video frame are not necessarily complete game frames.
For interlaced output a single field can change pixels, so never call this a
measured game framerate without further source/hardware confirmation.
"""
import argparse
import csv
from statistics import mean


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', help='CSV from pcfx-headless --frame-csv')
    parser.add_argument('--from-frame', type=int, default=1200)
    parser.add_argument('--to-frame', type=int, default=None)
    parser.add_argument('--field-rate', type=float, default=60.0,
                        help='approximate video output rate (Hz); not a measured clock')
    parser.add_argument('--threshold', type=int, default=250,
                        help='minimum changed pixels considered a substantial change')
    args = parser.parse_args()
    with open(args.csv, newline='') as fh:
        rows = [r for r in csv.DictReader(fh) if int(r['frame']) >= args.from_frame
                and (args.to_frame is None or int(r['frame']) < args.to_frame)]
    if not rows:
        parser.error('no frames in requested interval')
    all_changes = [int(r['changed_pixels']) for r in rows]
    exact_changes = [int(r['changed_pixels']) > 0 for r in rows]
    major_changes = [n > args.threshold for n in all_changes]
    print(f"Frames analyzed: {len(rows)} ({rows[0]['frame']}..{rows[-1]['frame']})")
    print(f"Output-field rate assumed: {args.field_rate:g} Hz")
    print(f"Visible nonidentical frames: {sum(exact_changes)} "
          f"({args.field_rate*mean(exact_changes):.2f} per second)")
    print(f"Substantial changes: {sum(major_changes)} "
          f"({args.field_rate*mean(major_changes):.2f} per second, "
          f"more than {args.threshold} pixels)")
    print('Caution: these are output changes, NOT proven game-frame FPS.')


if __name__ == '__main__':
    main()
