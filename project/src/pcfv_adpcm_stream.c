#include <stdint.h>
#include <pcfx/king.h>
#include <pcfx/sound.h>
#include "pcfv_adpcm_stream.h"

/* KING ADPCM registers (vendor/pcfxemu/mednafen/pcfx/king.c, PCFX_Skills
   pcfx-audio): 0x50 channel enable + rate, 0x51 ch0 buffer mode (bit0 =
   repeat from START at END), 0x53 status (read clears; ch0 bit0 = END
   reached, bit1 = intermediate reached), 0x58 START in 256-word units,
   0x59 END word address, 0x5A intermediate address in 64-word units. */
#define KING_ADPCM_CTRL     0x50u
#define KING_ADPCM_MODE0    0x51u
#define KING_ADPCM_MODE1    0x52u
#define KING_ADPCM_STATUS   0x53u
#define KING_ADPCM_START0   0x58u
#define KING_ADPCM_END0     0x59u
#define KING_ADPCM_HALF0    0x5Au
#define ADPCM_CH0_ENABLE    0x0001u
#define ADPCM_MODE_REPEAT   0x0001u
#define ADPCM_STATUS_END    0x0001u /* ring half 1 finished */
#define ADPCM_STATUS_HALF   0x0002u /* ring half 0 finished */
#define ADPCM_NO_BLOCK      0xffffffffu

static PcfvStreamInfo s_stream;
static uint8_t g_adpcm_enabled;
static uint8_t g_adpcm_rate_code;
static uint32_t g_adpcm_samples_per_field;
static uint32_t g_adpcm_blocks;
static uint32_t g_adpcm_preroll_blocks;
static uint32_t g_adpcm_total_samples;

static uint32_t g_adpcm_loaded[2];     /* block held by each ring half */
static uint32_t g_adpcm_want[2];       /* block each half must hold next */
static uint32_t g_adpcm_inflight[2];   /* block being DMA'd into each half */
static uint32_t g_adpcm_play_block;    /* block in the half being played */
static uint32_t g_adpcm_start_byte;    /* where the next (re)start begins */
static uint32_t g_adpcm_block_offset;  /* samples into play_block at the anchor */
static uint32_t g_adpcm_fields;        /* fields since the anchor */
static uint8_t g_adpcm_started;
static uint8_t g_adpcm_playing;
static uint8_t g_adpcm_finished;
static uint8_t g_adpcm_reset_decoder;

/* Validation counters (read by tools/extract_pcfv_stats.py). */
static uint32_t g_adpcm_underruns;       /* played a half whose block was not loaded */
static uint32_t g_adpcm_refills;
static uint32_t g_adpcm_requests;
static uint32_t g_adpcm_boundaries;
static uint32_t g_adpcm_parity_errors;   /* status bit for the other half */
static uint32_t g_adpcm_clock_error_max; /* |field estimate - hardware|, samples */

/* Keep this freestanding: GCC lowers the whole-struct assignment to memcpy
   at -Os, but the PC-FX runtime deliberately links without libc. */
static void stream_info_copy(PcfvStreamInfo *dst, const PcfvStreamInfo *src)
{
    dst->lba = src->lba;
    dst->data_start_sector = src->data_start_sector;
    dst->audio_bytes = src->audio_bytes;
    dst->audio_sectors = src->audio_sectors;
    dst->audio_rate_hz = src->audio_rate_hz;
    dst->audio_preroll_sectors = src->audio_preroll_sectors;
    dst->audio_refill_sectors = src->audio_refill_sectors;
    dst->ring_half_bytes = src->ring_half_bytes;
    dst->flags = src->flags;
    dst->fps_num = src->fps_num;
    dst->fps_den = src->fps_den;
    dst->frame_count = src->frame_count;
    dst->audio_chunk_count = src->audio_chunk_count;
    dst->audio_codec = src->audio_codec;
    dst->audio_chunks = src->audio_chunks;
}

static inline void king_select(uint16_t reg)
{
    __asm__ volatile ("out.h %0,0[%1]" :: "r"(reg), "r"(0x600u) : "memory");
}

static void king_w16(uint16_t reg, uint16_t value)
{
    king_select(reg);
    __asm__ volatile ("out.h %0,0[%1]" :: "r"(value), "r"(0x604u) : "memory");
}

static void king_w32(uint16_t reg, uint32_t value)
{
    king_select(reg);
    __asm__ volatile ("out.w %0,0[%1]" :: "r"(value), "r"(0x604u) : "memory");
}

static uint16_t king_r16(uint16_t reg)
{
    uint16_t value;
    king_select(reg);
    __asm__ volatile ("in.h 0[%1],%0" : "=r"(value) : "r"(0x604u) : "memory");
    return value;
}

static uint32_t block_samples(uint32_t block)
{
    uint32_t start = block * PCFV_ADPCM_HALF_BYTES;
    uint32_t bytes = s_stream.audio_bytes - start;
    if (bytes > PCFV_ADPCM_HALF_BYTES) bytes = PCFV_ADPCM_HALF_BYTES;
    return bytes * 2u;
}

/* Disc extent of a block: inside the preroll, or its own refill chunk. */
static void block_extent(uint32_t block, uint32_t *sector, uint32_t *sectors)
{
    uint32_t n = (block_samples(block) / 2u + PCFV_SECTOR_SIZE - 1u) / PCFV_SECTOR_SIZE;
    if (block < g_adpcm_preroll_blocks) {
        *sector = s_stream.data_start_sector + block * PCFV_ADPCM_HALF_SECTORS;
        *sectors = n;
    } else {
        const PcfvAudioChunk *c = &s_stream.audio_chunks[block - g_adpcm_preroll_blocks];
        *sector = c->sector;
        *sectors = c->sectors;
    }
}

int pcfv_audio_attach(const PcfvStreamInfo *s)
{
    uint32_t preroll_bytes, i;
    stream_info_copy(&s_stream, s);
    g_adpcm_enabled = 0u;
    if (s->audio_codec != PCFV_AUDIO_CODEC_ADPCM || !s->audio_bytes) return 0;
    if (s->ring_half_bytes != PCFV_ADPCM_HALF_BYTES || !s->fps_num || !s->fps_den) return 0;

    if (s->audio_rate_hz >= 24000u) g_adpcm_rate_code = 0u;
    else if (s->audio_rate_hz >= 12000u) g_adpcm_rate_code = 1u;
    else if (s->audio_rate_hz >= 6000u) g_adpcm_rate_code = 2u;
    else g_adpcm_rate_code = 3u;
    g_adpcm_samples_per_field = PCFV_ADPCM_RATE0_SAMPLES_PER_FIELD >> g_adpcm_rate_code;

    /* The preroll holds whole blocks and every refill chunk is the next
       block, cut at a ring-half boundary; anything else would put audio in
       the wrong half. */
    g_adpcm_blocks = (s->audio_bytes + PCFV_ADPCM_HALF_BYTES - 1u) / PCFV_ADPCM_HALF_BYTES;
    preroll_bytes = s->audio_preroll_sectors * PCFV_SECTOR_SIZE;
    if (!preroll_bytes) return 0;
#if defined(PCFX_PCFV_FORM2)
    /* The last of 29 Form 2 sectors in each 64-KiB ring half contains
       padding; physical preroll length is not a multiple of 65536. */
    if (s->audio_preroll_sectors % PCFV_ADPCM_HALF_SECTORS) return 0;
    g_adpcm_preroll_blocks = s->audio_preroll_sectors / PCFV_ADPCM_HALF_SECTORS;
    if (g_adpcm_preroll_blocks > g_adpcm_blocks) return 0;
#else
    if (preroll_bytes >= s->audio_bytes) g_adpcm_preroll_blocks = g_adpcm_blocks;
    else if (preroll_bytes % PCFV_ADPCM_HALF_BYTES) return 0;
    else g_adpcm_preroll_blocks = preroll_bytes / PCFV_ADPCM_HALF_BYTES;
#endif
    if (g_adpcm_preroll_blocks + s->audio_chunk_count != g_adpcm_blocks) return 0;
    for (i = 0; i < s->audio_chunk_count; ++i) {
        const PcfvAudioChunk *c = &s->audio_chunks[i];
        uint32_t block = g_adpcm_preroll_blocks + i;
        if (c->byte_offset != block * PCFV_ADPCM_HALF_BYTES) return 0;
        if (c->sectors * PCFV_SECTOR_SIZE < block_samples(block) / 2u) return 0;
        if (c->sectors > PCFV_ADPCM_HALF_SECTORS) return 0;
    }
    g_adpcm_total_samples = s->audio_bytes * 2u;
    g_adpcm_enabled = 1u;
    return 1;
}

static void hw_stop(void)
{
    king_w16(KING_ADPCM_CTRL, 0x0000u);
    g_adpcm_playing = 0u;
}

void pcfv_audio_reset(void)
{
    g_adpcm_enabled = 0u;
    g_adpcm_started = 0u;
    g_adpcm_playing = 0u;
    g_adpcm_finished = 0u;
    g_adpcm_underruns = 0u;
    g_adpcm_refills = 0u;
    g_adpcm_requests = 0u;
    g_adpcm_boundaries = 0u;
    g_adpcm_parity_errors = 0u;
    g_adpcm_clock_error_max = 0u;
    g_adpcm_loaded[0] = g_adpcm_loaded[1] = ADPCM_NO_BLOCK;
    g_adpcm_want[0] = g_adpcm_want[1] = ADPCM_NO_BLOCK;
    g_adpcm_inflight[0] = g_adpcm_inflight[1] = ADPCM_NO_BLOCK;
}

void pcfv_audio_seek(uint16_t frame)
{
    uint32_t byte, block;
    if (!g_adpcm_enabled) return;
    if (g_adpcm_playing) hw_stop();
    g_adpcm_started = 0u;
    g_adpcm_finished = 0u;

    /* RE1 PCFV inputs are capped at 2304 frames and 31468 Hz, so the
       numerator is below 73 million and fits the V810's 32-bit registers. */
    byte = (frame * s_stream.audio_rate_hz * (uint32_t)s_stream.fps_den /
            (uint32_t)s_stream.fps_num) / 2u;
    if (byte >= s_stream.audio_bytes) byte = s_stream.audio_bytes - 1u;
    byte &= ~(PCFV_ADPCM_START_ALIGN_BYTES - 1u);
    block = byte / PCFV_ADPCM_HALF_BYTES;

    /* The CD channel was idle or cancelled by the caller: nothing in flight. */
    g_adpcm_inflight[0] = g_adpcm_inflight[1] = ADPCM_NO_BLOCK;
    g_adpcm_want[block & 1u] = block;
    g_adpcm_want[(block + 1u) & 1u] = (block + 1u < g_adpcm_blocks) ? block + 1u : ADPCM_NO_BLOCK;
    g_adpcm_start_byte = byte;
    g_adpcm_play_block = block;
    g_adpcm_block_offset = (byte % PCFV_ADPCM_HALF_BYTES) * 2u;
    g_adpcm_fields = 0u;
    /* Reset the decoder: predictor 0 is exact at frame 0 (the encoder starts
       there) and a short settling transient anywhere else. */
    g_adpcm_reset_decoder = 1u;
}

static int half_ready(uint32_t h)
{
    return g_adpcm_want[h] == ADPCM_NO_BLOCK || g_adpcm_loaded[h] == g_adpcm_want[h];
}

int pcfv_audio_preroll_ready(void)
{
    return !g_adpcm_enabled || (half_ready(0u) && half_ready(1u));
}

/* Every ADPCM read is urgent: a refill has one half of the ring (~4.2 s at
   31.47 kHz) before the hardware plays stale data, while video has its
   read-ahead ring.  The playing half is loaded first after a seek. */
int pcfv_audio_fetch_urgent(void)
{
    uint32_t order[2], k;
    if (!g_adpcm_enabled || pcfv_cd_busy()) return 0;
    order[0] = g_adpcm_play_block & 1u;
    order[1] = order[0] ^ 1u;
    for (k = 0; k < 2u; ++k) {
        uint32_t h = order[k], sector, sectors;
#if !defined(DW3_EXPERIMENTAL_PAIRED_480I)
        uint32_t other = h ^ 1u;
#endif
        uint32_t block = g_adpcm_want[h];
        if (half_ready(h) || g_adpcm_inflight[h] != ADPCM_NO_BLOCK) continue;
        block_extent(block, &sector, &sectors);
        g_adpcm_inflight[h] = block;
        /* Non-paired builds may coalesce adjacent KRAM halves. Paired mode
           instead limits each request to 16 sectors (32 KiB), avoiding a
           64 KiB KING DMA boundary failure observed in PCFXEMU v35. */
#if !defined(DW3_EXPERIMENTAL_PAIRED_480I)
        if (h == 0u && g_adpcm_want[other] == block + 1u && !half_ready(other) &&
            g_adpcm_inflight[other] == ADPCM_NO_BLOCK && sectors == PCFV_ADPCM_HALF_SECTORS) {
            uint32_t sector1, sectors1;
            block_extent(block + 1u, &sector1, &sectors1);
            if (sector1 == sector + sectors) {
                sectors += sectors1;
                g_adpcm_inflight[other] = block + 1u;
            }
        }
#endif
        if (pcfv_cd_read_audio(s_stream.lba + sector,
                               PCFV_ADPCM_KRAM_WORD_ADDR + h * PCFV_ADPCM_HALF_WORDS, sectors)) {
            g_adpcm_requests++;
            return 1;
        }
        g_adpcm_inflight[0] = g_adpcm_inflight[1] = ADPCM_NO_BLOCK;
        return 0;
    }
    return 0;
}

int pcfv_audio_fetch_background(void) { return 0; }

void pcfv_audio_cd_done(void)
{
    uint32_t h;
    for (h = 0; h < 2u; ++h) {
        if (g_adpcm_inflight[h] == ADPCM_NO_BLOCK) continue;
        g_adpcm_loaded[h] = g_adpcm_inflight[h];
        g_adpcm_inflight[h] = ADPCM_NO_BLOCK;
        g_adpcm_refills++;
    }
}

void pcfv_audio_cd_failed(void)
{
    g_adpcm_inflight[0] = g_adpcm_inflight[1] = ADPCM_NO_BLOCK; /* retried */
}

static uint32_t in_block_samples(void)
{
    uint32_t n = g_adpcm_block_offset + g_adpcm_fields * g_adpcm_samples_per_field;
    return n < PCFV_ADPCM_HALF_SAMPLES ? n : PCFV_ADPCM_HALF_SAMPLES - 1u;
}

/* The hardware finished the half holding play_block: re-anchor the clock and
   queue block + 2 for that half. */
static void block_boundary(void)
{
    uint32_t est = g_adpcm_block_offset + g_adpcm_fields * g_adpcm_samples_per_field;
    uint32_t err = est > PCFV_ADPCM_HALF_SAMPLES ? est - PCFV_ADPCM_HALF_SAMPLES : PCFV_ADPCM_HALF_SAMPLES - est;
    uint32_t done = g_adpcm_play_block, next = done + 2u;
    if (err > g_adpcm_clock_error_max) g_adpcm_clock_error_max = err;
    g_adpcm_boundaries++;
    g_adpcm_want[done & 1u] = next < g_adpcm_blocks ? next : ADPCM_NO_BLOCK;
    g_adpcm_play_block = done + 1u;
    g_adpcm_block_offset = 0u;
    g_adpcm_fields = 0u;
    if (g_adpcm_play_block >= g_adpcm_blocks) {
        hw_stop();
        g_adpcm_finished = 1u;
    } else if (g_adpcm_loaded[g_adpcm_play_block & 1u] != g_adpcm_play_block) {
        g_adpcm_underruns++;
    }
}

static void poll_status(void)
{
    uint16_t st;
    int n;
    if (!g_adpcm_playing) return;
    st = king_r16(KING_ADPCM_STATUS) & (ADPCM_STATUS_END | ADPCM_STATUS_HALF);
    for (n = 0; st && n < 2; ++n) {
        uint16_t expect = (g_adpcm_play_block & 1u) ? ADPCM_STATUS_END : ADPCM_STATUS_HALF;
        if (!(st & expect)) {
            g_adpcm_parity_errors++;
            expect = st & ADPCM_STATUS_END ? ADPCM_STATUS_END : ADPCM_STATUS_HALF;
        }
        st &= (uint16_t)~expect;
        block_boundary();
        if (!g_adpcm_playing) return;
    }
    /* The encoder pads >= 4 fields of silence after the audio; stop two fields
       early so the field-granular clock never runs into stale ring data. */
    if (g_adpcm_play_block * PCFV_ADPCM_HALF_SAMPLES + in_block_samples() +
        2u * g_adpcm_samples_per_field >= g_adpcm_total_samples) {
        hw_stop();
        g_adpcm_finished = 1u;
    }
}

void pcfv_audio_new_field(void)
{
    if (g_adpcm_playing) g_adpcm_fields++;
    poll_status();
}

void pcfv_audio_service(void) {
    poll_status();
}

int pcfv_audio_boot_prefill_pending(void) { return 0; }
void pcfv_audio_boot_prefill_done(uint32_t fields) { (void)fields; }
void pcfv_audio_after_boot(void) { }

/* Program channel 0 over the whole ring and enable it at start_byte.  START
   (REG.58) is both the enable address and the END wrap target, so a start
   inside the ring points it there for the enable edge, then back at the ring
   base for the wrap (the emulator reads START only on those two events). */
static void hw_start(void)
{
    uint32_t base = PCFV_ADPCM_KRAM_WORD_ADDR;
    uint32_t start = base + (g_adpcm_play_block & 1u) * PCFV_ADPCM_HALF_WORDS +
                     (g_adpcm_start_byte % PCFV_ADPCM_HALF_BYTES) / 2u;
    adpcm_rate rate = (adpcm_rate)g_adpcm_rate_code;

    king_set_kram_pages(0, 0, 0, 0);
    king_w16(KING_ADPCM_CTRL, 0x0000u);
    if (g_adpcm_reset_decoder) {
        adpcm_set_control(rate, 1, 1, 1, 1);
        adpcm_set_volume(0, 63, 63);
        adpcm_set_volume(1, 0, 0);
        cdda_set_volume(0, 0);
    }
    king_w16(KING_ADPCM_MODE0, ADPCM_MODE_REPEAT);
    king_w16(KING_ADPCM_MODE1, 0x0000u);
    king_w16(KING_ADPCM_START0, (uint16_t)(start >> 8));
    king_w32(KING_ADPCM_END0, base + PCFV_ADPCM_RING_WORDS - 1u);
    king_w16(KING_ADPCM_HALF0, (uint16_t)((base + PCFV_ADPCM_HALF_WORDS) >> 6));
    adpcm_set_control(rate, 1, 1, 0, 0);
    (void)king_r16(KING_ADPCM_STATUS);
    king_w16(KING_ADPCM_CTRL, (uint16_t)(ADPCM_CH0_ENABLE | (g_adpcm_rate_code << 2)));
    if ((start >> 8) != (base >> 8)) king_w16(KING_ADPCM_START0, (uint16_t)(base >> 8));

    g_adpcm_reset_decoder = 0u;
    g_adpcm_block_offset = (g_adpcm_start_byte % PCFV_ADPCM_HALF_BYTES) * 2u;
    g_adpcm_fields = 0u;
    g_adpcm_playing = 1u;
}

void pcfv_audio_visible_field(void)
{
    if (!g_adpcm_enabled || g_adpcm_started || g_adpcm_finished) return;
    if (!pcfv_audio_preroll_ready()) return;
    hw_start();
    g_adpcm_started = 1u;
}

int pcfv_audio_playing(void) { return g_adpcm_playing; }
int pcfv_audio_started(void) { return g_adpcm_started; }
int pcfv_audio_is_clock(void) { return g_adpcm_enabled; }

uint32_t pcfv_audio_clock(void)
{
    if (g_adpcm_finished) return g_adpcm_total_samples;
    if (!g_adpcm_started) return g_adpcm_start_byte * 2u;
    return g_adpcm_play_block * PCFV_ADPCM_HALF_SAMPLES + in_block_samples();
}

uint32_t pcfv_audio_buffered(void)
{
    uint32_t n, next = g_adpcm_play_block + 1u;
    if (!g_adpcm_enabled || g_adpcm_finished) return 0u;
    n = block_samples(g_adpcm_play_block) - in_block_samples();
    if (next < g_adpcm_blocks && g_adpcm_loaded[next & 1u] == next) n += block_samples(next);
    return n;
}

int pcfv_audio_complete(void)
{
    return !g_adpcm_enabled || !g_adpcm_started || g_adpcm_finished;
}

/* Disabling channel 0 freezes the ring, but re-enabling restarts at START
   (king.c REG.50 write), so pause remembers the clock position and resume
   re-enables there, rounded down to START's 512-byte grain (<= 1024 samples
   replayed).  The decoder is not reset: its predictor is from the pause point,
   within a few fields of where playback resumes. */
void pcfv_audio_set_paused(int paused)
{
    if (!g_adpcm_enabled || !g_adpcm_started || g_adpcm_finished) return;
    if (paused) {
        uint32_t pos;
        if (!g_adpcm_playing) return;
        poll_status();
        if (!g_adpcm_playing) return;
        pos = g_adpcm_play_block * PCFV_ADPCM_HALF_BYTES + in_block_samples() / 2u;
        hw_stop();
        g_adpcm_start_byte = pos & ~(PCFV_ADPCM_START_ALIGN_BYTES - 1u);
        g_adpcm_block_offset = (g_adpcm_start_byte % PCFV_ADPCM_HALF_BYTES) * 2u;
        g_adpcm_fields = 0u;
    } else if (!g_adpcm_playing) {
        hw_start();
    }
}

void pcfv_audio_stop(void)
{
    if (g_adpcm_enabled || g_adpcm_playing) hw_stop();
}
