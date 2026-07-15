/* V810 performance-profiling instrumentation (headless/debug builds only).
 *
 * Enable with -DV810_PROFILE (see Makefile.headless PROFILE=1). When the macro
 * is not defined every hook below compiles to nothing, so release/SDL builds are
 * byte-for-byte unaffected.
 *
 * What it measures, per run (dumped at exit, and divided by video_frames):
 *   - instruction count, total attributed cycles, CPI
 *   - per-opcode histogram: count + real cycles (incl. load-use/pairing penalties) + CPI
 *   - slow-op spotlight (MUL/MULU/DIV/DIVU)
 *   - 1 KB icache: hits / subblock-miss / tag-miss / miss-rate, and a per-address miss map
 *   - conditional-branch taken vs not-taken ratio
 *   - 2 KiB DRAM page-change penalties, split code-refill vs data (see core.c RAMLPCHECK)
 *   - hot-PC histogram (32-byte buckets) -> CSV for tools/v810_prof_symbols.py
 */
#ifndef V810_PROFILE_H
#define V810_PROFILE_H

#ifdef V810_PROFILE
#include <stdint.h>

#define V810_PROF_RAMSIZE   0x200000u                       /* main RAM window PC lives in */
#define V810_PROF_BSHIFT    5                               /* 32-byte PC buckets */
#define V810_PROF_NBUCKET   (V810_PROF_RAMSIZE >> V810_PROF_BSHIFT)
#define V810_PROF_DELTACAP  4096                             /* drop implausible cycle gaps (halt/event) */

enum { V810_PROF_HIT = 0, V810_PROF_MISS_SUB = 1, V810_PROF_MISS_TAG = 2 };

/* --- shared counters (storage in v810_profile.c) --- */
extern unsigned long long v810p_cyc, v810p_dropcyc;
extern unsigned long long v810p_op_cnt[256], v810p_op_cyc[256];
extern unsigned long long v810p_ch_hit, v810p_ch_miss_sub, v810p_ch_miss_tag;
extern unsigned long long v810p_br_taken, v810p_br_nottaken;
extern uint32_t v810p_pc_cnt[V810_PROF_NBUCKET];
extern uint64_t v810p_pc_cyc[V810_PROF_NBUCKET];
extern uint32_t v810p_pc_miss[V810_PROF_NBUCKET];
extern unsigned long long v810p_pc_other_cnt, v810p_pc_other_cyc;

/* DRAM 2 KiB page model (incremented by core.c RAMLPCHECK) */
extern int v810p_ram_ifetch, v810p_last_ifetch;
extern unsigned long long v810p_dram_acc_code, v810p_dram_acc_data;
extern unsigned long long v810p_dram_pen_code, v810p_dram_pen_data;
extern unsigned long long v810p_dram_code_after_data, v810p_dram_code_after_code;

/* per-instruction attribution state */
extern int v810p_have_prev;
extern uint32_t v810p_prev_pc, v810p_prev_op;
extern int64_t v810p_prev_ts;

void v810_prof_report(unsigned long long frames);

/* --- inline hooks --- */
static inline void v810_prof_instr(uint32_t pc, int64_t ts)
{
	if(v810p_have_prev)
	{
		int64_t d = ts - v810p_prev_ts;
		if(d < 0 || d > V810_PROF_DELTACAP)
		{
			if(d > 0) v810p_dropcyc += (unsigned long long)d;
		}
		else
		{
			v810p_cyc += (unsigned long long)d;
			v810p_op_cyc[v810p_prev_op & 0xFF] += (unsigned long long)d;
			if(v810p_prev_pc < V810_PROF_RAMSIZE)
			{
				uint32_t b = v810p_prev_pc >> V810_PROF_BSHIFT;
				v810p_pc_cnt[b]++;
				v810p_pc_cyc[b] += (uint64_t)d;
			}
			else
			{
				v810p_pc_other_cnt++;
				v810p_pc_other_cyc += (unsigned long long)d;
			}
		}
	}
	v810p_prev_pc = pc;
	v810p_prev_ts = ts;
	v810p_have_prev = 1;
}

static inline void v810_prof_opcode(uint32_t op)
{
	op &= 0xFF;
	v810p_op_cnt[op]++;
	v810p_prev_op = op;
}

static inline void v810_prof_cache(int kind, uint32_t addr)
{
	if(kind == V810_PROF_HIT)
		v810p_ch_hit++;
	else
	{
		if(kind == V810_PROF_MISS_SUB) v810p_ch_miss_sub++; else v810p_ch_miss_tag++;
		if(addr < V810_PROF_RAMSIZE) v810p_pc_miss[addr >> V810_PROF_BSHIFT]++;
	}
}

static inline void v810_prof_branch(int taken)
{
	if(taken) v810p_br_taken++; else v810p_br_nottaken++;
}

#endif /* V810_PROFILE */
#endif /* V810_PROFILE_H */
