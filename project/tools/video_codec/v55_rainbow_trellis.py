#!/usr/bin/env python3
"""HuC6271 fixed-Huffman AC trellis rate-distortion encoder (host-side only).

No changes to hardware bitstream or native player. DP optimizes zero-run,
category and signed amplitude bits, preserving the original DC prediction.
An actual host RAINBOW decode and physical-sector limit gate all replacements.

Caveat: coefficient SSE is only a search surrogate; final decisions compare
full RGB SSE using the project's approximate host decoder.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import time
import numpy as np
from rainbow_codec import (ZIGZAG, HUFF_AC_Y, HUFF_AC_UV, encode_prepared,
                           prepare_image, analyze_stream)
from rainbow_decode import decode_stream,to_rgb
from adaptive_quant_tables import qbase
from pcfv_adpcm import read,write,sectors

FRAME_BYTES=256*480*3


def trellis_ac(original, table, baseline, ac_huffman, multiplier):
    """Exact-symbol-bit-cost, zero-run trellis for one 8x8 DCT block.

    The cost is coefficient-domain squared error + multiplier * coded bits.
    Coefficient quantization changes are allowed; DC is fixed.  All transitions
    use actual HuC6271 AC symbols incl. 0x10 ZRL and the fixed 5-bit EOB.
    """
    orig=np.asarray(original).reshape(64)
    qt=np.asarray(table).reshape(64)
    baseline=np.asarray(baseline).reshape(64)
    zz=ZIGZAG
    # extra candidates around quantized real value, allowing zero.
    choices=[None]*64
    errors=[None]*64
    for i in range(1,64):
        j=zz[i]
        c=int(orig[j]); q=int(qt[j]); initial=int(baseline[j])
        low=c//q
        candidates={0,initial,low,low+1,low-1,low+2}
        vals=sorted(x for x in candidates if x == 0 or (abs(x)<=511 and abs(x).bit_length()<=9))
        choices[i]=vals
        errors[i]=[(c-x*q)**2 for x in vals]
    # Future value of each trailing-zero run: an EOB is required unless
    # the last coefficient itself is nonzero.
    next_cost=[multiplier*5.0 if run else 0.0 for run in range(65)]
    back=[None]*64
    zrl_bits=ac_huffman[0x10][1]
    for i in range(63,0,-1):
        nxt=[float('inf')]*65
        picks=[0]*65
        vals=choices[i]
        errs=errors[i]
        nz=[(x,errs[k],abs(x).bit_length()) for k,x in enumerate(vals) if x]
        zero_err=errs[vals.index(0)]
        for run in range(i):
            winner=0
            winner_cost=zero_err+next_cost[run+1]
            extra=(run//16)*zrl_bits
            code_run=run%16
            for x,dist,cat in nz:
                symbol=code_run*16+cat
                coding=ac_huffman.get(symbol)
                if coding is None:continue
                cost=dist+multiplier*(extra+coding[1]+cat)+next_cost[0]
                if cost<winner_cost:
                    winner_cost=cost;winner=x
            nxt[run]=winner_cost
            picks[run]=winner
        next_cost=nxt
        back[i]=picks
    out=np.asarray(baseline,dtype=np.int32).reshape(64).copy()
    run=0
    for i in range(1,64):
        v=back[i][run]
        out[zz[i]]=v
        run=run+1 if not v else 0
    return out


def encode_trellis(prepared, scale, tables, lam):
    def callback(coef,qt,base,channel):
        return trellis_ac(coef,qt,base,HUFF_AC_Y if channel==0 else HUFF_AC_UV,lam)
    return encode_prepared(prepared,scale,base_tables=tables,quantizer=callback)


def field_sse(src,rec):
    out=to_rgb(*decode_stream(rec))
    d=src.astype(np.int16)-out.astype(np.int16)
    return int(np.sum(d.astype(np.int32)**2,dtype=np.int64))


def improve_field(src,old,table_names=('baseline',),lambdas=(1,2,3,4,5),
                  verbose=False, sector_bytes=2048):
    """Conservative same-sector source-measured replacement. Never increases SSE."""
    prep=prepare_image(src)
    strip_scales=[]
    decode_stream(old,strip_scales=strip_scales)
    baseline_scale=strip_scales[0] if strip_scales[0] is not None else 10
    before=field_sse(src,old)
    if sector_bytes not in (2048, 2324):
        raise ValueError("sector_bytes must be 2048 or 2324")
    # Preserve each field's allocation in original 2048-byte physical sectors.
    # At Form 2 payload size, more bits fit without spinning the disc faster.
    sector_budget=sectors(len(old), 2048)
    budget=sector_budget*sector_bytes
    best=(before,old,{'method':'baseline','scale':None,'lambda':None})
    tested=0
    # Binary search a legal scale using fast baseline encoder, then apply
    # expensive coefficient trellis only near the stream's byte budget.
    for name in table_names:
        tables=(tuple(old[4:68]),tuple(old[68:132])) if name=="baseline" else qbase(name)
        cached={}
        def plain(s):
            if s not in cached:
                try:cached[s]=encode_prepared(prep,s,base_tables=tables)
                except ValueError:cached[s]=None
            return cached[s]
        lo,hi=0,15
        while lo<hi:
            mid=(lo+hi)//2
            blob=plain(mid)
            if blob is None or len(blob)>budget:lo=mid+1
            else:hi=mid
        scales=(max(0,baseline_scale-2),max(0,baseline_scale-1),baseline_scale) if name=="baseline" else (max(0,lo-1),lo,min(15,lo+1))
        for scale in sorted(set(scales)):
            baseline=plain(scale)
            if baseline is not None and len(baseline)<=budget:
                score=field_sse(src,baseline);tested+=1
                if score<best[0]:best=(score,baseline,{'method':'plain','table':name,'scale':scale})
            for lam in lambdas:
                try:
                    candidate=encode_trellis(prep,scale,tables,lam)
                    if len(candidate)>budget:continue
                    analyze_stream(candidate,15)
                    score=field_sse(src,candidate)
                    tested+=1
                    if score<best[0]:
                        best=(score,candidate,{'method':'trellis','table':name,'scale':scale,'lambda':lam})
                except (OverflowError,ValueError):continue
    err,record,why=best
    assert err<=before and sectors(len(record),sector_bytes)<=sector_budget
    return record,{'before_sse':before,'after_sse':err,'gain_db':10*math.log10(max(1,before)/max(1,err)),
                   'tested_candidates':tested,'old_sectors':sector_budget,
                   'new_sectors':sectors(len(record),sector_bytes),'old_bytes':len(old),'new_bytes':len(record),**why}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--pictures',type=int,nargs='+',default=[700])
    p.add_argument('--tables',nargs='+',default=['baseline'])
    p.add_argument('--lambdas',nargs='+',type=float,default=[1,2,3,4,5])
    p.add_argument('--report',type=Path)
    p.add_argument('--form2',action='store_true',
                   help='2324-byte per-sector trellis budget and Form 2 PCFV output')
    args=p.parse_args()
    target_sector=2324 if args.form2 else 2048
    m=read(args.baseline)
    assert m['paired_fields'] and len(m['frames'])==3584
    changed=list(m['frames'])
    reports=[]
    start=time.monotonic()
    for picture in args.pictures:
        if not 0<=picture<1792:raise ValueError('invalid picture index')
        with args.source.open('rb') as f:
            f.seek(picture*FRAME_BYTES)
            source=np.frombuffer(f.read(FRAME_BYTES),np.uint8).reshape(480,256,3).copy()
        for par in (0,1):
            idx=picture*2+par
            changed[idx],report=improve_field(source[par::2],changed[idx],args.tables,
                                               args.lambdas,sector_bytes=target_sector)
            reports.append({'picture':picture,'parity':par,'field_index':idx,**report})
            print('field',idx,'gain',round(report['gain_db'],4),'dB','choice',report['method'],'test',report['tested_candidates'],'elapsed',round(time.monotonic()-start,1),flush=True)
    assert all(sectors(len(changed[i]),target_sector)<=sectors(len(m['frames'][i]),2048)
               for i in range(3584))
    args.output.parent.mkdir(parents=True,exist_ok=True)
    write(args.output,changed,max(sectors(len(x),target_sector) for x in changed),
          15,1,m['audio'],m['rate'],compact=True,paired_fields=True,form2=args.form2)
    check=read(args.output)
    assert check['frames']==changed and check['audio']==m['audio']
    stats={'base_sha256':hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
           'candidate_sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
           'video_sectors_before':sum(sectors(len(x)) for x in m['frames']),
           'video_sectors_after':sum(sectors(len(x),target_sector) for x in changed),
           'changed_fields':sum(changed[i]!=m['frames'][i] for i in range(3584)),
           'records_tested':reports, 'adpcm_identical':True, 'sector_bytes':target_sector,
           'status':'HOST_VALIDATED_ONLY_NOT_YET_EMULATOR_CERTIFIED'}
    (args.report or args.output.with_suffix('.json')).write_text(json.dumps(stats,indent=2))
    print(json.dumps({'changed_fields':stats['changed_fields'],'elapsed_s':round(time.monotonic()-start,1)},indent=2))

if __name__=='__main__':main()
