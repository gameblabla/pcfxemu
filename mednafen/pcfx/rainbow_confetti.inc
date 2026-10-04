/*
 * Replace clearly corrupt RAINBOW YUV strips with the previous clean strip
 * for the same frame row. Included by both decoder backends, each of which
 * keeps its own history.
 */
#define RB_STRIPS_PER_FRAME 15
#define RB_STARVE_THRESHOLD 8
#define RB_NOISE_FLOOR 60
#define RB_NOISE_RATIO 3
#define RB_CHROMA_FLOOR 60
#define RB_MAX_GAP 2

static uint32 rb_row_cache[16][256 * 16];
static bool rb_row_cache_valid[16];
static int32 rb_row;

static void RB_ConfettiReset(void)
{
 rb_row = 0;
 memset(rb_row_cache_valid, 0, sizeof(rb_row_cache_valid));
}

static void RB_ConfettiBeginFrame(void)
{
 rb_row = 0;
}

static void RB_ConfettiAdvanceRow(void)
{
 rb_row++;
 if(rb_row >= RB_STRIPS_PER_FRAME)
  rb_row = 0;
}

static void RB_ConfettiRepair(uint32 *dest_base, uint32 happy_color, int32 starve_bytes)
{
 uint32 colnoise[16];
 uint32 colchroma[16];
 uint32 sorted[16];
 uint32 sorted_c[16];
 bool bad[16];
 int first_bad = -1;
 const int32 row = rb_row % RB_STRIPS_PER_FRAME;

 for(int c = 0; c < 16; c++)
 {
  const int bx = c * 16;
  uint32 acc = 0;
  uint32 acc_c = 0;

  for(int y = 0; y < 16; y++)
  {
   const uint32 *line = &dest_base[y * 256 + bx];

   for(int x = 0; x < 15; x++)
   {
    const uint32 p0 = line[x];
    const uint32 p1 = line[x + 1];
    const int32 y0 = (int32)((p0 >> 16) & 0xFF);
    const int32 y1 = (int32)((p1 >> 16) & 0xFF);
    const int32 u0 = (int32)((p0 >> 8) & 0xFF);
    const int32 u1 = (int32)((p1 >> 8) & 0xFF);
    const int32 v0 = (int32)(p0 & 0xFF);
    const int32 v1 = (int32)(p1 & 0xFF);

    acc += (uint32)(y0 > y1 ? y0 - y1 : y1 - y0);
    acc_c += (uint32)(u0 > u1 ? u0 - u1 : u1 - u0);
    acc_c += (uint32)(v0 > v1 ? v0 - v1 : v1 - v0);
   }
  }

  colnoise[c] = acc / (16 * 15);
  colchroma[c] = acc_c / (16 * 15 * 2);
  sorted[c] = colnoise[c];
  sorted_c[c] = colchroma[c];
 }

 for(int i = 1; i < 16; i++)
 {
  const uint32 key = sorted[i];
  int j = i - 1;
  while(j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j--; }
  sorted[j + 1] = key;
 }
 for(int i = 1; i < 16; i++)
 {
  const uint32 key = sorted_c[i];
  int j = i - 1;
  while(j >= 0 && sorted_c[j] > key) { sorted_c[j + 1] = sorted_c[j]; j--; }
  sorted_c[j + 1] = key;
 }

 const uint32 median = (sorted[7] + sorted[8]) / 2;
 const uint32 median_c = (sorted_c[7] + sorted_c[8]) / 2;
 for(int c = 0; c < 16; c++)
 {
  const uint32 y_noise = colnoise[c];
  const uint32 c_noise = colchroma[c];

  bad[c] = (y_noise >= RB_NOISE_FLOOR && y_noise >= median * RB_NOISE_RATIO) ||
           (c_noise >= RB_CHROMA_FLOOR && c_noise >= median_c * RB_NOISE_RATIO);
  if(bad[c] && first_bad < 0)
   first_bad = c;
 }

 /* Absorb short clean-looking gaps between suspect columns. */
 {
  bool grown[16];
  for(int c = 0; c < 16; c++) grown[c] = bad[c];
  for(int c = 0; c < 16; c++)
  {
   if(!bad[c]) continue;
   for(int gap = 2; gap <= RB_MAX_GAP + 1; gap++)
   {
    if(c + gap > 15 || !bad[c + gap]) continue;
    for(int k = c + 1; k < c + gap; k++) grown[k] = true;
    break;
   }
  }
  for(int c = 0; c < 16; c++) bad[c] = grown[c];
 }

 const bool starved = starve_bytes > RB_STARVE_THRESHOLD;
 if((first_bad >= 0 || starved) && rb_row_cache_valid[row])
 {
  memcpy(dest_base, rb_row_cache[row], 256 * 16 * sizeof(uint32));
 }
 else if(first_bad >= 0)
 {
  int c = 0;
  while(c < 16)
  {
   if(!bad[c]) { c++; continue; }
   const int run_start = c;
   while(c < 16 && bad[c]) c++;
   const int run_end = c - 1;
   const int src_col = run_start - 1;

   for(int rc = run_start; rc <= run_end; rc++)
   {
    uint32 *dst = &dest_base[rc * 16];
    for(int y = 0; y < 16; y++)
     for(int x = 0; x < 16; x++)
      dst[y * 256 + x] = (src_col >= 0)
                       ? dest_base[src_col * 16 + y * 256 + x]
                       : happy_color;
   }
  }
 }

 if(first_bad < 0 && !starved)
 {
  memcpy(rb_row_cache[row], dest_base, 256 * 16 * sizeof(uint32));
  rb_row_cache_valid[row] = true;
 }
}

#undef RB_STRIPS_PER_FRAME
#undef RB_STARVE_THRESHOLD
#undef RB_NOISE_FLOOR
#undef RB_NOISE_RATIO
#undef RB_CHROMA_FLOOR
#undef RB_MAX_GAP
