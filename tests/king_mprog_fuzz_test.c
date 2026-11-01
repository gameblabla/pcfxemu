/* KING microprogram fuzzer: emulator (king.c DrawBG) vs. an independent model of HuC6272 manual 3.6.7.
 *
 * STATUS: TENTATIVE.  The reference model encodes what the manual says plus the one behaviour confirmed on hardware
 * (BAT->CG latency, Super Mario Bros PC-FX commit 3d7fca35).  Everything the manual calls "display is not correct" is
 * treated as UNDEFINED: those programs are still run (crash / determinism / isolation checks) but their pixels are not
 * compared.  Divergences found here are findings to put in front of real hardware (see the KING labs of the DIAG7
 * disc), not proof that the emulator or the model is right.
 *
 * What is checked, for every random microprogram on BG1 (BAT-mode and direct-mode, all five colour modes):
 *   1. SPEC     valid programs render exactly what the model says (including the two-cycle BAT->CG window).
 *   2. BANK     rewriting every slot of the bank that the plane does NOT use cannot change a pixel.
 *   3. PLANE    rewriting every slot that belongs to other planes cannot change a pixel.
 *   4. SAFE     any program (valid or not) renders deterministically and never touches pixels outside the line.
 * Plus the explicit Mario HUD case, and a census of affine planes drawn without any microprogram fetches (spec: the
 * rotated plane needs 8 CG accesses; the emulator draws it from the base registers regardless).
 *
 * Build/run: tests/run_king_mprog_fuzz_test.sh [iterations] [seed]   (use -fsanitize=address,undefined) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mednafen/pcfx/king.c"
void SoundBox_SetKINGADPCMControl(uint32 value) {(void)value;}

#define LINE_PIXELS (8 + 256 + 8 + 8)
#define GUARD 0xDEADBEEFu

static uint64_t rng_state;
static uint32_t rnd(void)
{
 rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
 return (uint32_t)(rng_state >> 16);
}
static uint32_t rn(uint32_t n) { return n ? rnd() % n : 0; }

/* ---- model of one plane's schedule ------------------------------------------------------------------------- */
static uint64_t cross_bank_undefined;

typedef struct {
 int bat_mode;			/* bgmode & 8 */
 int colour;			/* bgmode & 7: 1=4c 2=16c 3=256c 4=64K 5=16M */
 int need;			/* CG words per cell row */
 int plane;
 int bank_bat, bank_cg;		/* 0 = A (slots 0-7), 1 = B (slots 8-15) */
} model_cfg;

typedef struct {
 int valid;			/* every manual rule satisfied for this plane */
 int ncg, nbat;
 int cg_cycle[8], cg_off[8], cg_ofbat[8];
 int bat_cycle;
 int late[8];			/* CG read inside the two-cycle latency -> cell to the left */
} model_sched;

static const int k_need[8] = { 0, 1, 2, 4, 8, 8, 0, 0 };

static void model_analyse(const model_cfg *c, const uint16 *mp, model_sched *s)
{
 memset(s, 0, sizeof(*s));
 s->bat_cycle = -1;
 s->valid = 1;
 for(int x = 0; x < 8; x++)
 {
  const uint16 w = mp[c->bank_cg * 8 + x];
  if(!(w & 0x100) && ((w >> 6) & 3) == (unsigned)c->plane && !(w & 0x10))
  {
   if(s->ncg < 8)
   {
    s->cg_cycle[s->ncg] = x; s->cg_off[s->ncg] = w & 7; s->cg_ofbat[s->ncg] = !!(w & 8);
   }
   s->ncg++;
   if(w & 0x20) s->valid = 0;			/* rotation flag on a non-rotated plane */
  }
  const uint16 b = mp[c->bank_bat * 8 + x];
  if(!(b & 0x100) && ((b >> 6) & 3) == (unsigned)c->plane && (b & 0x10))
  {
   if(!s->nbat) s->bat_cycle = x;
   s->nbat++;
   if(b & 0x20) s->valid = 0;
  }
 }
 if(s->ncg != c->need) s->valid = 0;
 if(c->bat_mode) { if(s->nbat != 1) s->valid = 0; } else if(s->nbat) s->valid = 0;
 for(int i = 0; i < s->ncg && i < 8; i++)
 {
  if(s->cg_off[i] >= c->need) s->valid = 0;
  if(i && s->cg_off[i] <= s->cg_off[i - 1]) s->valid = 0;	/* must ascend, no repeats */
  if(s->cg_ofbat[i] != (c->bat_mode ? 1 : 0)) s->valid = 0;
  if(c->bat_mode && s->nbat == 1 && s->cg_cycle[i] < ((s->bat_cycle + 2) & 7)) s->late[i] = 1;
 }
 /* The manual does not say which cycle numbers count when the BAT and its CG live in different banks: the model has no
  * opinion (the emulator applies the same cycle rule).  DIAG7 lab K5 measures this on hardware. */
 if(c->bat_mode && c->bank_bat != c->bank_cg) s->valid = 0, cross_bank_undefined++;
}

/* ---- scene -------------------------------------------------------------------------------------------------- */
#define PBASE 0x0u			/* palette: entry i -> unique colour */
static uint32 pal_colour(unsigned i) { return 0x00200000u + i * 0x00010403u; }

static void scene_reset(void)
{
 memset(king, 0, sizeof(*king));
 king->BG0FetchRehomed = TRUE;
 king->MPROGControl = 1;
 king->priority = 0x0000;
 for(unsigned i = 0; i < 1024; i++) vce_rendercache.palette_table_cache[i] = pal_colour(i & 0x1FF);
 memset(vce_rendercache.palette_offset, 0, sizeof(vce_rendercache.palette_offset));
 BGLayerDisable = 0;
}

static void kram_randomise(void)
{
 for(int p = 0; p < 2; p++)
  for(unsigned i = 0; i < 262144; i++) king->KRAM[p][i] = (uint16)rnd();
}

/* Place BAT and CG of BG1 so the model and emulator read identical words. */
static void scene_plane(const model_cfg *c, int cg_word_base, int bat_word_base)
{
 const int n = 1;
 king->bgmode = (uint16)((c->bat_mode | c->colour) << (n * 4));
 king->BGSize[n] = 0x88;			/* 256x256 dots: 32x32 cells */
 king->BGBATAddr[n] = (uint16)(bat_word_base / 1024);
 king->BGCGAddr[n] = (uint16)(cg_word_base / 1024);
 king->BGXScroll[n] = 0; king->BGYScroll[n] = 0;
 king->BGScrollMode = 0;
 king->PageSetting = 0;
 king->priority = (uint16)(1 << (3 * n));	/* BG1 priority 1 */
}

static uint32 model_pixel_from_words(const model_cfg *c, const uint16 *w, int p, unsigned pbn, uint32 layer_or, int *drawn)
{
 *drawn = 0;
 switch(c->colour)
 {
  case 1: { unsigned v = (w[0] >> (14 - 2 * p)) & 3; if(v) { *drawn = 1; return pal_colour((pbn << 2) + v) | layer_or; } } break;
  case 2: { unsigned v = (w[p >> 2] >> (12 - 4 * (p & 3))) & 0xF; if(v) { *drawn = 1; return pal_colour((pbn << 4) + v) | layer_or; } } break;
  case 3: { unsigned v = (w[p >> 1] >> ((p & 1) ? 0 : 8)) & 0xFF; if(v) { *drawn = 1; return pal_colour(v) | layer_or; } } break;
  case 4: if(w[p] & 0xFF00) { *drawn = 1; return ((w[p] & 0x00F0) << 8) | ((w[p] & 0x000F) << 4) | ((w[p] & 0xFF00) << 8) | layer_or; } break;
  case 5: {
   const uint16 hi = w[(p & ~1)], lo = w[(p & ~1) + 1];
   const unsigned sel = (p & 1) ? (hi & 0xFF) : (hi >> 8);
   if(sel) { *drawn = 1; return (((p & 1) ? (hi & 0x00FF) << 16 : (hi & 0xFF00) << 8)) | (lo & 0xFF00) | (lo & 0xFF) | layer_or; }
  } break;
 }
 return 0;
}

/* Expected pixels of cell `cell` on line `y`, or -1 for "model has no opinion".  out[8], drawn[8]. */
static void model_cell(const model_cfg *c, const model_sched *s, int y, int cell, int cg_base, int bat_base,
                       uint32 *out, int *drawn)
{
 const int ysmall = y & 7, bat_y = y >> 3;
 const uint16 *bat_pg = &king->KRAM[0][bat_base & 0x20000];
 const uint16 *cg_pg = &king->KRAM[0][cg_base & 0x20000];
 const uint32 layer_or = (uint32)(LAYER_BG0 + 1) << 28;
 uint16 words[8] = { 0 };
 unsigned pbn = 0;

 if(c->bat_mode)
 {
  const int bi = (bat_base + cell + bat_y * 32) & 0x1FFFF;
  uint16 bat = bat_pg[bi];
  const unsigned tile = bat & 0xFFF;
  pbn = bat >> 12;
  const int lcell = cell ? cell - 1 : cell;
  const uint16 lbat = bat_pg[(bat_base + lcell + bat_y * 32) & 0x1FFFF];
  for(int k = 0; k < s->ncg && k < c->need; k++)
  {
   unsigned t = (s->late[k] ? (lbat & 0xFFF) : tile);
   words[k] = cg_pg[(cg_base + t * 8 * c->need + ysmall * c->need + s->cg_off[k]) & 0x1FFFF];
  }
 }
 else
 {
  for(int k = 0; k < s->ncg && k < c->need; k++)
   words[k] = cg_pg[(cg_base + cell * c->need + y * 32 * c->need + s->cg_off[k]) & 0x1FFFF];
 }
 for(int p = 0; p < 8; p++) out[p] = model_pixel_from_words(c, words, p, pbn, layer_or, &drawn[p]);
}

static void render_line(int y, uint32 *line)
{
 for(int i = 0; i < LINE_PIXELS; i++) line[i] = GUARD;
 memset(line + 8, 0, 256 * sizeof(uint32));
 fx_vce.raster_counter = 22 + y;
 DrawBG(line, 1);
}

static uint64_t checked_cells, undefined_cells, late_cells, spec_fail, bank_fail, plane_fail, safe_fail;
static uint64_t affine_no_fetch_drawn, affine_cases, affine_valid_drawn;

static void build_valid_program(const model_cfg *c, uint16 *mp)
{
 /* Fill with NOP, then place a legal schedule for BG1 in its bank(s); sprinkle other planes into free slots. */
 for(int i = 0; i < 16; i++) mp[i] = 0x100;
 int used[16] = { 0 };
 int bat_c = -1;
 if(c->bat_mode)
 {
  bat_c = (int)rn(8);
  mp[c->bank_bat * 8 + bat_c] = (uint16)((1 << 6) | 0x10);
  used[c->bank_bat * 8 + bat_c] = 1;
 }
 /* CG slots: ascending offsets 0..need-1 assigned to ascending free cycles, legal or (sometimes) too early */
 int start = 0;
 if(c->bat_mode) start = (rn(5) == 0) ? 0 : ((bat_c + 2) & 7);	/* 1 in 5: deliberately inside the latency */
 int cycles[8], nc = 0;
 for(int x = 0; x < 8 && nc < c->need; x++)
 {
  const int slot = c->bank_cg * 8 + x;
  if(x < start || used[slot]) continue;
  cycles[nc++] = x;
 }
 if(nc < c->need)		/* not enough room after the window: restart from cycle 0 (may be early / unusable) */
 {
  nc = 0;
  for(int x = 0; x < 8 && nc < c->need; x++) if(!used[c->bank_cg * 8 + x]) cycles[nc++] = x;
 }
 for(int k = 0; k < nc; k++)
 {
  mp[c->bank_cg * 8 + cycles[k]] = (uint16)((1 << 6) | (c->bat_mode ? 8 : 0) | k);
  used[c->bank_cg * 8 + cycles[k]] = 1;
 }
 for(int i = 0; i < 16; i++)		/* other planes in what is left */
  if(!used[i] && rn(2)) mp[i] = (uint16)((rn(4) == 1 ? 0 : 2 + rn(2)) << 6 | rn(0x40));
}

static void run_case(int iter)
{
 model_cfg c;
 c.colour = 1 + (int)rn(5);
 c.need = k_need[c.colour];
 c.bat_mode = rn(3) ? 8 : 0;
 c.plane = 1;
 c.bank_bat = (int)rn(2); c.bank_cg = rn(4) ? c.bank_bat : 1 - c.bank_bat;
 if(!c.bat_mode) c.bank_bat = c.bank_cg;

 scene_reset();
 kram_randomise();
 const int cg_base = c.bank_cg ? 0x20000 : 0, bat_base = c.bank_bat ? 0x20000 : 0;
 scene_plane(&c, cg_base, bat_base + (int)(rn(4)) * 1024 * 0);

 uint16 mp[16];
 if(rn(4)) build_valid_program(&c, mp);
 else for(int i = 0; i < 16; i++) mp[i] = (uint16)(rn(8) ? (rnd() & 0x1FF) : 0x100);
 for(int i = 0; i < 16; i++) king->MPROGData[i] = mp[i];

 model_sched s;
 model_analyse(&c, mp, &s);

 const int y = (int)rn(240);
 uint32 a[LINE_PIXELS], b[LINE_PIXELS];

 /* 4. SAFE: deterministic, no stray writes */
 render_line(y, a); render_line(y, b);
 if(memcmp(a, b, sizeof(a))) { safe_fail++; printf("SAFE: non-deterministic iter %d\n", iter); }
 for(int i = 0; i < 8; i++) if(a[i] != GUARD) { safe_fail++; printf("SAFE: wrote left guard iter %d\n", iter); break; }
 for(int i = 8 + 256 + 8; i < LINE_PIXELS; i++) if(a[i] != GUARD) { safe_fail++; printf("SAFE: wrote right guard iter %d\n", iter); break; }

 /* 1. SPEC */
 if(s.valid)
 {
  for(int cell = 0; cell < 32; cell++)
  {
   uint32 exp[8]; int dr[8];
   model_cell(&c, &s, y, cell, cg_base, bat_base, exp, dr);
   int late_here = 0;
   for(int k = 0; k < s.ncg && k < 8; k++) late_here |= s.late[k];
   late_cells += (uint64_t)late_here;
   for(int p = 0; p < 8; p++)
   {
    const uint32 got = a[8 + cell * 8 + p];
    const uint32 want = dr[p] ? exp[p] : 0;
    checked_cells++;
    if(got != want)
    {
     if(spec_fail < 10)
      printf("SPEC mismatch iter %d colour %d bat %d banks(%d,%d) y %d cell %d px %d got %08x want %08x late %d\n",
             iter, c.colour, c.bat_mode, c.bank_bat, c.bank_cg, y, cell, p, got, want, late_here);
     spec_fail++;
     cell = 99; break;
    }
   }
  }
 }
 else undefined_cells += 32;

 /* 2. BANK: scribble over the unused bank's slots (the plane's BAT bank and CG bank are kept as they are) */
 if(c.bank_bat == c.bank_cg)
 {
  const int other = 1 - c.bank_cg;
  for(int i = 0; i < 8; i++) king->MPROGData[other * 8 + i] = (uint16)(rnd() & 0x1FF);
  render_line(y, b);
  if(memcmp(a, b, sizeof(a))) { bank_fail++; if(bank_fail < 10) printf("BANK: other-bank slots changed pixels iter %d\n", iter); }
  for(int i = 0; i < 16; i++) king->MPROGData[i] = mp[i];
 }

 /* 3. PLANE: rewrite every slot that is not this plane's */
 {
  for(int i = 0; i < 16; i++)
   if(!(((mp[i] >> 6) & 3) == 1 && !(mp[i] & 0x100)))
    king->MPROGData[i] = (uint16)((rn(2) ? 0x100 : ((rn(3) == 0 ? 0 : 2 + rn(2)) << 6)) | (rnd() & 0x3F));
  render_line(y, b);
  if(memcmp(a, b, sizeof(a))) { plane_fail++; if(plane_fail < 10) printf("PLANE: other planes' slots changed pixels iter %d\n", iter); }
  for(int i = 0; i < 16; i++) king->MPROGData[i] = mp[i];
 }
}

/* Affine census: BG0 rotated, microprogram has no BG0 fetches at all. */
static void affine_census(int n)
{
 for(int i = 0; i < n; i++)
 {
  scene_reset();
  kram_randomise();
  king->bgmode = (uint16)(4);				/* BG0 64K-colour direct (rotate path handles modes 1-4) */
  king->BGSize[0] = 0x8888;
  king->priority = (uint16)(0x1000 | 1);		/* affine on, BG0 priority 1 */
  king->BGAffinA = 0x100; king->BGAffinD = 0x100;
  for(int s = 0; s < 16; s++) king->MPROGData[s] = (uint16)(0x100 | (rn(2) ? (2 << 6) : 0));	/* NOPs / other plane only */
  uint32 line[LINE_PIXELS];
  for(int p = 0; p < LINE_PIXELS; p++) line[p] = GUARD;
  memset(line + 8, 0, 256 * 4);
  fx_vce.raster_counter = 22 + (int)rn(240);
  DrawBG(line, 0);
  affine_cases++;
  for(int p = 8; p < 264; p++) if(line[p] != 0) { affine_no_fetch_drawn++; break; }
 }

 /* Positive control: the spec-valid schedule (8 rotated CG fetches for BG0, bank A) must draw in every mode. */
 for(int i = 0; i < n; i++)
 {
  scene_reset();
  for(int q = 0; q < 2; q++) for(unsigned k = 0; k < 262144; k++) king->KRAM[q][k] = 0xFFFF;
  king->bgmode = (uint16)(4);
  king->BGSize[0] = 0x8888;
  king->priority = (uint16)(0x1000 | 1);
  king->BGAffinA = 0x100; king->BGAffinD = 0x100;
  for(int s = 0; s < 16; s++) king->MPROGData[s] = (uint16)(s < 8 ? 0x20 : 0x100);	/* BG0, rotated, direct, offset 0 */
  uint32 line[LINE_PIXELS];
  for(int p = 0; p < LINE_PIXELS; p++) line[p] = GUARD;
  memset(line + 8, 0, 256 * 4);
  fx_vce.raster_counter = 22 + (int)rn(240);
  DrawBG(line, 0);
  for(int p = 8; p < 264; p++) if(line[p] != 0) { affine_valid_drawn++; break; }
 }
}

/* The Super Mario Bros PC-FX world-3 HUD, as a unit test of the hardware-confirmed latency. */
static void mario_case(int bat_cycle, int cg0, int cg1, int expect_late)
{
 scene_reset();
 model_cfg c = { 8, 1, 1, 1, 0, 0 };		/* BAT-mode 4-colour, plane 1, bank A */
 for(int i = 0; i < 16; i++) king->MPROGData[i] = 0x100;
 king->MPROGData[bat_cycle] = (1 << 6) | 0x10;
 king->MPROGData[cg0] = (1 << 6) | 8 | 0;
 (void)cg1;
 memset(king->KRAM, 0, sizeof(king->KRAM));
 /* tile 1: all pixels colour 1; tile 2: all pixels colour 2.  BAT row 0: alternate tiles 1,2,1,2 ... */
 scene_plane(&c, 0x10000, 0);
 king->BGCGAddr[1] = 0x10000 / 1024;
 for(int cell = 0; cell < 32; cell++) king->KRAM[0][cell] = (cell & 1) ? 2 : 1;
 for(int r = 0; r < 8; r++) { king->KRAM[0][0x10000 + 1 * 8 + r] = 0x5555; king->KRAM[0][0x10000 + 2 * 8 + r] = 0xAAAA; }
 uint32 line[LINE_PIXELS];
 render_line(0, line);
 const uint32 c1 = pal_colour(1) | ((uint32)(LAYER_BG0 + 1) << 28), c2 = pal_colour(2) | ((uint32)(LAYER_BG0 + 1) << 28);
 int ok = 1;
 for(int cell = 2; cell < 30; cell++)
 {
  const uint32 right = (cell & 1) ? c2 : c1, left = (cell & 1) ? c1 : c2;
  const uint32 want = expect_late ? left : right;
  if(line[8 + cell * 8] != want) ok = 0;
 }
 printf("  Mario BAT@%d CG@%d: %s (expected %s)\n", bat_cycle, cg0, ok ? "ok" : "MISMATCH", expect_late ? "cell to the LEFT" : "own cell");
 assert(ok);
}

int main(int argc, char **argv)
{
 const int iters = argc > 1 ? atoi(argv[1]) : 20000;
 rng_state = argc > 2 ? strtoull(argv[2], 0, 0) : 0x9E3779B97F4A7C15ull;
 if(!rng_state) rng_state = 1;
 king = (king_t *)calloc(1, sizeof(king_t));
 assert(king);

 printf("King microprogram fuzz (TENTATIVE reference model), %d cases, seed %llu\n", iters, (unsigned long long)rng_state);
 mario_case(2, 3, 4, 1);	/* the broken HUD: BAT at 2, first BAT-CG read at 3 */
 mario_case(0, 2, 3, 0);	/* the fixed HUD */
 mario_case(6, 0, 1, 0);	/* BAT at 6: cycles 0-7 all legal (wrap) */
 mario_case(7, 0, 1, 1);	/* BAT at 7: window starts at 1, a read at 0 is early */
 mario_case(5, 7, 7, 0);	/* BAT at 5: only cycle 7 */
 mario_case(5, 6, 6, 1);

 for(int i = 0; i < iters; i++) run_case(i);
 affine_census(200);

 printf("cells compared %llu  undefined(not compared) %llu  latency-affected %llu\n",
        (unsigned long long)checked_cells, (unsigned long long)undefined_cells, (unsigned long long)late_cells);
 printf("cross-bank BAT/CG schedules (BAT bank != CG bank), not compared: %llu\n", (unsigned long long)cross_bank_undefined);
 printf("SPEC %llu  BANK %llu  PLANE %llu  SAFE %llu failures\n", (unsigned long long)spec_fail,
        (unsigned long long)bank_fail, (unsigned long long)plane_fail, (unsigned long long)safe_fail);
 printf("CENSUS affine BG0 with no microprogram fetches: drawn in %llu of %llu cases "
        "(spec: needs 8 rotated CG fetches; real behaviour UNVERIFIED -> DIAG7 lab K7)\n",
        (unsigned long long)affine_no_fetch_drawn, (unsigned long long)affine_cases);
 printf("CONTROL affine BG0 with the 8 rotated fetches the manual asks for: drawn in %llu of 200 cases\n",
        (unsigned long long)affine_valid_drawn);
 if(affine_valid_drawn != 200) { puts("FAIL: spec-valid affine schedule must always draw"); return 1; }
 {
  const char *st = getenv("PCFXEMU_KING_STRICT_AFFINE");
  if(st && st[0] == '1' && affine_no_fetch_drawn != 0) { puts("FAIL: strict affine drew a plane without fetches"); return 1; }
 }
 if(spec_fail || bank_fail || plane_fail || safe_fail) { puts("FAIL king_mprog_fuzz_test"); return 1; }
 puts("PASS king_mprog_fuzz_test");
 return 0;
}
