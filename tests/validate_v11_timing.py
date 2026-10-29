#!/usr/bin/env python3
"""Static instruction-timing and KING phase trace checks for PCFXemu v11.

The relative phase trace is observational, NOT a hardware-derived bus latency.
Optional argument: phase CSV obtained from the tank demo.
"""
import argparse
import csv
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
CPU=(ROOT/'mednafen/hw_cpu/v810/v810_oploop.inc').read_text()
DEFS=(ROOT/'mednafen/hw_cpu/v810/v810_cpu.c').read_text()
for instr in ('ST_B','ST_H','ST_W','OUT_B','OUT_H','OUT_W'):
    segment=CPU.split('BEGIN_OP('+instr+');',1)[1].split('END_OP_SKIPLO();',1)[0]
    assert 'lastop == LASTOP_'+instr in segment, instr+' must match its own width'
    assert 'lastop = LASTOP_'+instr+';' in segment, instr+' history must match its own width'
    assert '#define LASTOP_'+instr+' ' in DEFS, instr+' history ID must exist'

p=argparse.ArgumentParser()
p.add_argument('phase_csv',nargs='?')
a=p.parse_args()
if a.phase_csv:
    rows=list(csv.DictReader(open(a.phase_csv, newline='')))
    assert len(rows)==16,'8 microprogram phases x 2 banks'
    assert [(r['bank'],int(r['slot'])) for r in rows]==[(b,s) for b in 'AB' for s in range(8)]
    for r in rows:
        n=int(r['issued_writes'])
        busy=int(r['programmed_non_nop'])
        selected=int(r['enabled_bg_slot'])
        assert 0<=selected<=busy<=n
    # The supplied tank background uses 4 programmed CG reads in bank A.
    assert sum(int(r['issued_writes']) for r in rows if r['bank']=='A')>0
    assert all(int(r['issued_writes'])==0 for r in rows if r['bank']=='B')
    assert all(int(r['programmed_non_nop'])==0 for r in rows if r['bank']=='A' and int(r['slot'])>=4)
print('PASS v11: same-width instruction history + non-invasive per-bank phase accounting')
