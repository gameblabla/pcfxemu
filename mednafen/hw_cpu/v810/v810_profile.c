/* V810 performance-profiling instrumentation — storage + end-of-run report.
 * Compiles to an empty translation unit unless -DV810_PROFILE is set. */
#include "v810_profile.h"

#ifdef V810_PROFILE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned long long v810p_cyc, v810p_dropcyc;
unsigned long long v810p_op_cnt[256], v810p_op_cyc[256];
unsigned long long v810p_ch_hit, v810p_ch_miss_sub, v810p_ch_miss_tag;
unsigned long long v810p_br_taken, v810p_br_nottaken;
unsigned long long v810p_flag_stall, v810p_flag_noflag;
uint32_t v810p_pc_cnt[V810_PROF_NBUCKET];
uint64_t v810p_pc_cyc[V810_PROF_NBUCKET];
uint32_t v810p_pc_miss[V810_PROF_NBUCKET];
unsigned long long v810p_pc_other_cnt, v810p_pc_other_cyc;

int v810p_ram_ifetch, v810p_last_ifetch;
unsigned long long v810p_dram_acc_code, v810p_dram_acc_data;
unsigned long long v810p_dram_pen_code, v810p_dram_pen_data;
unsigned long long v810p_dram_code_after_data, v810p_dram_code_after_code;

int v810p_have_prev;
uint32_t v810p_prev_pc, v810p_prev_op;
int64_t v810p_prev_ts;

static const char *const v810_op_names[256] = {
  "MOV", "MOV", "ADD", "ADD", "SUB", "SUB", "CMP", "CMP",
  "SHL", "SHL", "SHR", "SHR", "JMP", "JMP", "SAR", "SAR",
  "MUL", "MUL", "DIV", "DIV", "MULU", "MULU", "DIVU", "DIVU",
  "OR", "OR", "AND", "AND", "XOR", "XOR", "NOT", "NOT",
  "MOV_I", "MOV_I", "ADD_I", "ADD_I", "SETF", "SETF", "CMP_I", "CMP_I",
  "SHL_I", "SHL_I", "SHR_I", "SHR_I", "EI", "EI", "SAR_I", "SAR_I",
  "TRAP", "TRAP", "RETI", "RETI", "HALT", "HALT", "INVALID", "INVALID",
  "LDSR", "LDSR", "STSR", "STSR", "DI", "DI", "BSTR", "BSTR",
  "BV", "BL", "BE", "BNH", "BN", "BR", "BLT", "BLE",
  "BNV", "BNL", "BNE", "BH", "BP", "NOP", "BGE", "BGT",
  "MOVEA", "MOVEA", "ADDI", "ADDI", "JR", "JR", "JAL", "JAL",
  "ORI", "ORI", "ANDI", "ANDI", "XORI", "XORI", "MOVHI", "MOVHI",
  "LD_B", "LD_B", "LD_H", "LD_H", "INVALID", "INVALID", "LD_W", "LD_W",
  "ST_B", "ST_B", "ST_H", "ST_H", "INVALID", "INVALID", "ST_W", "ST_W",
  "IN_B", "IN_B", "IN_H", "IN_H", "CAXI", "CAXI", "IN_W", "IN_W",
  "OUT_B", "OUT_B", "OUT_H", "OUT_H", "FPP", "FPP", "OUT_W", "OUT_W",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID",
  "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INVALID", "INT_HANDLER",
};

/* Aggregate the 256 opcode slots down to distinct mnemonics for the report. */
typedef struct { const char *name; unsigned long long cnt, cyc; } opagg_t;

static int opagg_cmp(const void *a, const void *b)
{
	unsigned long long x = ((const opagg_t*)a)->cyc, y = ((const opagg_t*)b)->cyc;
	return (x < y) - (x > y);
}

static double pct(unsigned long long x, unsigned long long tot)
{
	return tot ? 100.0 * (double)x / (double)tot : 0.0;
}

void v810_prof_report(unsigned long long frames)
{
	const double CYC_PER_FIELD = 21477000.0 / 60.0;   /* V810 21.477 MHz, 60 fields/s */
	double f = frames ? (double)frames : 1.0;

	unsigned long long instr = 0;
	for(int i = 0; i < 256; i++) instr += v810p_op_cnt[i];

	unsigned long long ch_miss = v810p_ch_miss_sub + v810p_ch_miss_tag;
	unsigned long long ch_acc  = v810p_ch_hit + ch_miss;
	unsigned long long br_tot  = v810p_br_taken + v810p_br_nottaken;
	unsigned long long dram_ev = v810p_dram_pen_code + v810p_dram_pen_data;

	fprintf(stderr, "\n");
	fprintf(stderr, "################  V810 PROFILE  (video_frames=%llu)  ################\n", frames);
	fprintf(stderr, "Instructions      : %llu  (%.0f / field)\n", instr, instr / f);
	fprintf(stderr, "Cycles attributed : %llu  (%.0f / field = %.2f%% of a field)   dropped=%llu\n",
		v810p_cyc, v810p_cyc / f, 100.0 * (v810p_cyc / f) / CYC_PER_FIELD, v810p_dropcyc);
	fprintf(stderr, "CPI (mean)        : %.3f cyc/instr\n", instr ? (double)v810p_cyc / (double)instr : 0.0);

	/* ---- icache ---- */
	fprintf(stderr, "\n-- 1 KB icache --\n");
	fprintf(stderr, "hits=%llu  miss(subblock)=%llu  miss(tag)=%llu  total=%llu\n",
		v810p_ch_hit, v810p_ch_miss_sub, v810p_ch_miss_tag, ch_acc);
	fprintf(stderr, "miss-rate=%.3f%%   misses/field=%.0f   fixed miss cost=%.0f cyc/field (+2 each, DRAM refill extra)\n",
		pct(ch_miss, ch_acc), ch_miss / f, ch_miss * 2.0 / f);

	/* ---- branches ---- */
	fprintf(stderr, "\n-- conditional branches --\n");
	fprintf(stderr, "taken=%llu (%.1f%%)  not-taken=%llu (%.1f%%)  total/field=%.0f\n",
		v810p_br_taken, pct(v810p_br_taken, br_tot),
		v810p_br_nottaken, pct(v810p_br_nottaken, br_tot), br_tot / f);
	fprintf(stderr, "branch cycles/field=%.0f (taken 3, not 1)\n",
		(v810p_br_taken * 3.0 + v810p_br_nottaken * 1.0) / f);

	/* ---- flag-use pipeline stalls ---- */
	{
		unsigned long long readers = v810p_flag_stall + v810p_flag_noflag;
		fprintf(stderr, "\n-- flag-use stalls (+2 when a Bcc/SETF/STSR follows a flag-writing op) --\n");
		if(!readers)
			fprintf(stderr, "(model off: built with -DV810_NO_FLAG_STALL)\n");
		else
			fprintf(stderr, "flag-readers/field=%.0f  stalled=%.0f (%.1f%%)  dodged=%.0f\n"
				"stall cost=%.0f cyc/field (%.2f%% of a field) — hoisting a flag-neutral op\n"
				"between compare and branch reclaims 2 cyc each\n",
				readers / f, v810p_flag_stall / f, pct(v810p_flag_stall, readers),
				v810p_flag_noflag / f, v810p_flag_stall * 2.0 / f,
				100.0 * (v810p_flag_stall * 2.0 / f) / CYC_PER_FIELD);
	}

	/* ---- slow ops ---- */
	fprintf(stderr, "\n-- slow ops --\n");
	{
		struct { const char *n; int a, b; } S[] = {
			{"MUL", 16, 17}, {"MULU", 20, 21}, {"DIV", 18, 19}, {"DIVU", 22, 23} };
		for(unsigned k = 0; k < sizeof(S)/sizeof(S[0]); k++)
		{
			unsigned long long c = v810p_op_cnt[S[k].a] + v810p_op_cnt[S[k].b];
			unsigned long long y = v810p_op_cyc[S[k].a] + v810p_op_cyc[S[k].b];
			fprintf(stderr, "%-5s count=%llu (%.0f/field)  cycles=%llu (%.0f/field, %.2f%%)\n",
				S[k].n, c, c / f, y, y / f, 100.0 * (y / f) / CYC_PER_FIELD);
		}
	}

	/* ---- opcode histogram (aggregated by mnemonic, by cycles) ---- */
	fprintf(stderr, "\n-- opcodes by cycles (top 24) --\n");
	fprintf(stderr, "%-10s %12s %6s %14s %6s %6s\n", "op", "count", "%cnt", "cycles", "%cyc", "CPI");
	{
		opagg_t agg[256]; int n = 0;
		for(int i = 0; i < 256; i++)
		{
			if(!v810p_op_cnt[i] && !v810p_op_cyc[i]) continue;
			const char *nm = v810_op_names[i];
			int j; for(j = 0; j < n; j++) if(agg[j].name == nm || !strcmp(agg[j].name, nm)) break;
			if(j == n) { agg[n].name = nm; agg[n].cnt = 0; agg[n].cyc = 0; n++; }
			agg[j].cnt += v810p_op_cnt[i];
			agg[j].cyc += v810p_op_cyc[i];
		}
		qsort(agg, n, sizeof(agg[0]), opagg_cmp);
		int lim = n < 24 ? n : 24;
		for(int j = 0; j < lim; j++)
			fprintf(stderr, "%-10s %12llu %5.1f%% %14llu %5.1f%% %6.2f\n",
				agg[j].name, agg[j].cnt, pct(agg[j].cnt, instr),
				agg[j].cyc, pct(agg[j].cyc, v810p_cyc),
				agg[j].cnt ? (double)agg[j].cyc / (double)agg[j].cnt : 0.0);
	}

	/* ---- DRAM 2 KiB page penalties ---- */
	fprintf(stderr, "\n-- 2 KiB DRAM page penalties (+3 cyc each) --\n");
	fprintf(stderr, "penalty cyc/field=%.0f (%.2f%% of a field)   code-refill=%.0f  data=%.0f\n",
		dram_ev * 3.0 / f, 100.0 * (dram_ev * 3.0 / f) / CYC_PER_FIELD,
		v810p_dram_pen_code * 3.0 / f, v810p_dram_pen_data * 3.0 / f);
	fprintf(stderr, "code-refill page-changes: after-DATA=%llu (ping-pong, unfixable)  after-CODE=%llu (align-addressable)\n",
		v810p_dram_code_after_data, v810p_dram_code_after_code);

	/* ---- hot PC buckets ---- */
	fprintf(stderr, "\n-- hot code (top 16 of %d-byte buckets, by cycles) --\n", 1 << V810_PROF_BSHIFT);
	fprintf(stderr, "%-10s %12s %14s %12s\n", "addr", "count", "cycles", "icache-miss");
	{
		/* selection sort for top 16 to avoid sorting the whole 64K array */
		char used[16] = {0}; int top[16];
		for(int r = 0; r < 16; r++)
		{
			long best = -1; uint64_t bc = 0;
			for(uint32_t b = 0; b < V810_PROF_NBUCKET; b++)
			{
				int skip = 0;
				for(int q = 0; q < r; q++) if(top[q] == (int)b) { skip = 1; break; }
				if(skip) continue;
				if(v810p_pc_cyc[b] > bc) { bc = v810p_pc_cyc[b]; best = b; }
			}
			if(best < 0) { top[r] = -1; continue; }
			top[r] = (int)best; used[r] = 1;
			fprintf(stderr, "0x%06x   %12u %14llu %12u\n",
				(unsigned)(best << V810_PROF_BSHIFT), v810p_pc_cnt[best],
				(unsigned long long)v810p_pc_cyc[best], v810p_pc_miss[best]);
		}
		(void)used;
	}
	if(v810p_pc_other_cnt)
		fprintf(stderr, "(non-RAM PC e.g. BIOS: count=%llu cycles=%llu)\n",
			v810p_pc_other_cnt, v810p_pc_other_cyc);

	/* ---- optional CSV for the symbol post-processor ---- */
	{
		const char *path = getenv("V810_PROF_OUT");
		if(path)
		{
			FILE *fp = fopen(path, "w");
			if(fp)
			{
				fprintf(fp, "addr,count,cycles,misses\n");
				for(uint32_t b = 0; b < V810_PROF_NBUCKET; b++)
					if(v810p_pc_cnt[b] || v810p_pc_miss[b])
						fprintf(fp, "0x%06x,%u,%llu,%u\n",
							(unsigned)(b << V810_PROF_BSHIFT), v810p_pc_cnt[b],
							(unsigned long long)v810p_pc_cyc[b], v810p_pc_miss[b]);
				fclose(fp);
				fprintf(stderr, "\nwrote PC histogram CSV -> %s (map to symbols: tools/v810_prof_symbols.py)\n", path);
			}
		}
	}
	fprintf(stderr, "####################################################################\n");
}

#endif /* V810_PROFILE */
