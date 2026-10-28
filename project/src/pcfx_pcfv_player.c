#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pcfx/king.h>
#include <pcfx/tetsu.h>
#include <pcfx/vdc.h>
#include <pcfx/v810.h>
#include <pcfx/sound.h>
#include <pcfx/contrlr.h>
/* From libpcfx <eris/scsi.h>, declared here because that header's SCSI_PHASE_*
   enumerators collide with this file's own non-blocking SCSI state machine. */
void scsi_reset(void);
void eris_scsi_abort(void);
#include "pcfx_pcfv_player.h"
#include "tetsu_field_scheduler.h"
#include "pcfx_pcfv_internal.h"
#if defined(PCFX_PCFV_AUDIO_MP2)
#include "pcfv_mp2_stream.h"
#elif defined(PCFX_PCFV_AUDIO_ADPCM)
#include "pcfv_adpcm_stream.h"
#endif

#if defined(HAVE_GENERATED_LBAS)
#include "lbas.h"
#endif

#if defined(HAVE_GENERATED_AUDIO_INFO)
#include "generated_audio_info.h"
#endif

#ifndef BINARY_LBA_ASSETS_STREAM_PCFV
#define BINARY_LBA_ASSETS_STREAM_PCFV 0u
#endif

#define PCFV_MAX_FRAMES        4096u
#define PCFV_HEADER_READ_BYTES (((64u + PCFV_MAX_FRAMES * 32u + PCFV_SECTOR_SIZE - 1u) / PCFV_SECTOR_SIZE) * PCFV_SECTOR_SIZE)
#define PCFV_MAX_AUDIO_CHUNKS  1024u
/* The opening uses genuine TETSU interlaced scan timing by default.
   Existing 256x240 compressed records remain 240-line *source* images: this
   is NOT 480-line source detail. Native paired odd/even records remain
   optional pending a strict sustained-CD-bandwidth test. */
#if defined(DW3_FORCE_240P_FMV)
#define DW3_FMV_TETSU_LINES TETSU_LINES_262
#else
#define DW3_FMV_TETSU_LINES TETSU_LINES_INTERLACED
#endif
volatile uint32_t dw3_tetsu_odd_fields;
volatile uint32_t dw3_tetsu_even_fields;
volatile uint32_t dw3_tetsu_field_switches;
static uint32_t dw3_prev_tetsu_field = 2u;
#define RAINBOW_WIDTH           256u
#define RAINBOW_HEIGHT          240u
#define RAINBOW_START_SCANLINE  6u
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
#define RAINBOW_RESTART_RASTER  220u /* halt CD polling before late-field pre-arm */
#define RAINBOW_PREARM_BEGIN    248u /* last 16-line decode has completed (230+16) */
#define RAINBOW_PREARM_END      254u /* maintain >=8 raster lines before the next field */
#else
#define RAINBOW_RESTART_RASTER  252u /* halt CD polling before vertical wrap */
#endif
#define RAINBOW_SAFE_LATCH_END  5u /* reserve plenty of margin before visible raster 22 */

#define VIDEO_BUFFER_LOW_COUNT  31u /* capacity for four-sector fields below ADPCM KRAM */
#define VIDEO_BUFFER_COUNT      47u /* 31 lower + 16 upper; actual count depends on stride */
#define VIDEO_SKIP_SEARCH        0u
#define VIDEO_SYNC_SKIP_MAX      27u
#define VIDEO_BUFFER_FIRST_WORD 0x00001000u
#define VIDEO_BUFFER_STRIDE_MAX 0x00002500u /* 16 KiB spacing; up to 8 CD sectors */
#define VIDEO_BUFFER_STRIDE_DEFAULT 0x00001000u
/* Four-sector, 30-record/s paired streams need longer contiguous CD reads.
   The verified six-sector progressive player retains the v33 batch cap. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
#define VIDEO_BATCH_MAX          24u
#else
#define VIDEO_BATCH_MAX          12u
#endif
/* KRAM page 0 word-address layout, ADPCM build:
   0x01000..0x1FFFF: lower video slots, dynamic stride 1..8 sectors
   0x20000..0x2FFFF: dedicated 128KiB ADPCM double ring (never touch)
   0x30000..0x3FFFF: upper video slots, dynamic stride 1..8 sectors
   4 sectors: 31 lower + 16 upper; 6 sectors: 20+10;
   7 sectors: 17+9; 8 sectors: 15+8.
   The two video banks are NOT contiguous for SCSI DMA batching.
   Header/index reuses the low area only before the video ring starts.
   MP2 mode is not supported by this KRAM allocation without replanning. */
#define KRAM_HEADER_WORD_ADDR   0x00001000u
#define SCSI_TIMEOUT_TICKS      0x00300000u
#define SCHEDULER_POLL_BUDGET   96u
#define AUDIO_START_AFTER_VISIBLE_FIELDS 1u

static uint8_t g_pcfv_head[PCFV_HEADER_READ_BYTES] __attribute__((aligned(4)));

typedef struct {
    uint32_t video_sector;
    uint32_t video_size;
    uint32_t video_sectors;
    uint32_t video_crc32;
    uint32_t audio_sector;
    uint32_t audio_sectors;
    uint32_t audio_byte_offset;
    uint32_t flags;
} FrameEntry;

static int cd_dma_is_busy(void);
static uint16_t count_ready_video_buffers(void);
static FrameEntry g_entries[PCFV_MAX_FRAMES];
static PcfvAudioChunk g_audio_chunks[PCFV_MAX_AUDIO_CHUNKS];
static uint16_t g_frame_count;
static uint16_t g_fps_num;
static uint16_t g_fps_den;
static uint16_t g_audio_chunk_count;
static uint32_t g_data_start_sector;
static uint32_t g_audio_preroll_sectors;
static uint32_t g_audio_refill_sectors;
static uint32_t g_ring_half_bytes;
static uint32_t g_audio_bytes;
static uint32_t g_audio_rate_hz;
static uint32_t g_audio_sectors;
static uint16_t g_pcfv_flags;
static uint8_t g_audio_codec;
static uint8_t g_audio_active;        /* the compiled backend plays this stream */
static uint32_t g_audio_clock_samples; /* last audio clock, for validation */

static inline void port_out_h(uint32_t port, uint16_t value) {
    __asm__ volatile ("out.h %0,0[%1]" :: "r"(value), "r"(port) : "memory");
}

static inline void port_out_w(uint32_t port, uint32_t value) {
    __asm__ volatile ("out.w %0,0[%1]" :: "r"(value), "r"(port) : "memory");
}

static inline uint16_t port_in_h(uint32_t port) {
    uint16_t value;
    __asm__ volatile ("in.h 0[%1],%0" : "=r"(value) : "r"(port) : "memory");
    return value;
}

static inline uint32_t port_in_w(uint32_t port) {
    uint32_t value;
    __asm__ volatile ("in.w 0[%1],%0" : "=r"(value) : "r"(port) : "memory");
    return value;
}

static inline void king_select(uint16_t reg) { port_out_h(0x600, reg); }
static inline void king_data_h(uint16_t value) { port_out_h(0x604, value); }

static void king_write_reg16(uint16_t reg, uint16_t value) {
    king_select(reg);
    king_data_h(value);
}

static void king_write_reg32(uint16_t reg, uint32_t value) {
    king_select(reg);
    port_out_w(0x604, value);
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Frame timing comes from the Tetsu raster counter, never from the VDC status
   VD bit: VD is only raised while VDC CR bit 3 (vblank IRQ enable) is set, and
   setup_video() writes CR = BB only, so polling VD at 0x80000400 spins forever.
   The counter must be read twice until two reads agree (HuC6261 latch bug);
   see PCFX_Skills/pcfx-frame-timing.  262-line mode: EVB = 22, SVB = 262. */
static inline uint16_t tetsu_raster_stable(void) {
    uint16_t a, b;
    do {
        a = (uint16_t)tetsu_get_raster();
        b = (uint16_t)tetsu_get_raster();
    } while (a != b);
    return a;
}

static inline int vblank_active(void) {
    uint16_t r = tetsu_raster_stable();
    return r >= 262u || r < 22u;
}

/* Returns on the leading edge of the next vertical blank. */
static void wait_vblank(void) {
    while (vblank_active()) { }
    while (!vblank_active()) { }
}

/* -------------------------------------------------------------------------
   Small non-blocking SCSI READ(10) + KING DMA state machine.

   This replaces liberis' eris_low_scsi_command(), eris_cd_read(),
   eris_cd_read_kram(), and eris_low_scsi_finish_dma() on the playback path.
   The only long phase is data transfer, and it is KING DMA-polled.  Command,
   status, and message phases are advanced one handshake at a time by
   cd_dma_poll_budget().
   ------------------------------------------------------------------------- */

typedef enum {
    SCSI_PHASE_BUS_FREE    = 0,
    SCSI_PHASE_SELECT      = 1,
    SCSI_PHASE_DATA_OUT    = 2,
    SCSI_PHASE_DATA_IN     = 3,
    SCSI_PHASE_COMMAND     = 4,
    SCSI_PHASE_STATUS      = 5,
    SCSI_PHASE_MESSAGE_OUT = 6,
    SCSI_PHASE_MESSAGE_IN  = 7,
    SCSI_PHASE_ILLEGAL     = 8
} ScsiPhase;

typedef enum {
    CD_DMA_IDLE = 0,
    CD_DMA_SELECT_INIT,
    CD_DMA_SELECT_DELAY0,
    CD_DMA_SELECT_DELAY1,
    CD_DMA_SELECT_DELAY2,
    CD_DMA_WAIT_SELECTED,
    CD_DMA_RELEASE_DELAY,
    CD_DMA_WAIT_COMMAND,
    CD_DMA_COMMAND_WAIT_REQ,
    CD_DMA_COMMAND_WAIT_DROP,
    CD_DMA_COMMAND_WAIT_RAISE,
    CD_DMA_WAIT_DATA_REQ,
    CD_DMA_DATAOUT_WAIT_REQ,
    CD_DMA_DATAOUT_WAIT_DROP,
    CD_DMA_DATAOUT_WAIT_RAISE,
    CD_DMA_ACTIVE,
    CD_DMA_FINISH_WAIT_REQ,
    CD_DMA_FINISH_WAIT_DROP,
    CD_DMA_FINISH_WAIT_NEXT,
    CD_DMA_DONE,
    CD_DMA_ERROR
} CdDmaState;

typedef enum {
    CD_REQ_NONE = 0,
    CD_REQ_HEADER,
    CD_REQ_VIDEO,
    CD_REQ_AUDIO,
    CD_REQ_MODE_SELECT
} CdReqKind;

typedef struct {
    CdDmaState state;
    CdReqKind kind;
    uint32_t lba;
    uint32_t kram_word_addr;
    uint32_t bytes;
    uint32_t timeout;
    uint16_t delay;
    uint16_t video_buf;
    uint16_t video_count;
    uint16_t video_early_index;
    uint32_t video_early_bytes;
    uint8_t cdb[10];
    uint8_t cdb_len;
    uint8_t cdb_pos;
    uint8_t status_byte;
    uint8_t message_byte;
    uint8_t data_out[12];
    uint8_t data_out_pos;
} CdDmaRequest;

static CdDmaRequest g_cd_dma;
static uint8_t g_header_ready;
#if defined(PCFX_PCFV_FORM2)
static uint8_t g_sector_mode_ready;
/* Form 2 audio sectors: one uninterrupted 29-sector READ(10) per
   65536-byte ADPCM block. Each disc chunk starts with 1860 padding bytes,
   then 65536 actual samples. DMA starts 930 words before the ring half.
   For half 1 the guard overlaps the END of half 0; save that tail before
   DMA and restore it before reporting the audio block ready. This avoids
   an extra physical CD seek per half without letting zero-padding persist. */
#define FORM2_GUARD_BYTES (PCFV_ADPCM_HALF_SECTORS * PCFV_SECTOR_SIZE - PCFV_ADPCM_HALF_BYTES)
#define FORM2_GUARD_WORDS (FORM2_GUARD_BYTES / 2u)
static uint8_t g_audio_guard_saved;
static uint8_t g_audio_guard_buf[FORM2_GUARD_BYTES] __attribute__((aligned(2)));
#endif
static uint8_t g_rainbow_visible;
static uint32_t g_scsi_dma_started;
static uint32_t g_scsi_dma_completed;
static uint32_t g_scsi_dma_errors;
/* V46: Phase-driven CD->KRAM DMA, independent of a full-sector transfer
   count reaching zero. Exact-count transfers can wedge the real KING DMA
   trailing-handshake erratum. All READ(10) requests are sector aligned. */
volatile uint32_t g_v46_phase_dma_started;
volatile uint32_t g_v46_phase_dma_ended;
volatile uint32_t g_v46_phase_dma_short;
volatile uint32_t g_v46_phase_dma_overrun;
volatile uint32_t g_v47_first_bad_expected;
volatile uint32_t g_v47_first_bad_received;
volatile uint32_t g_v47_first_bad_address;
volatile uint32_t g_v47_first_bad_position;
volatile uint32_t g_v47_first_bad_kind;
volatile uint32_t g_v47_first_bad_lba;

volatile uint32_t g_v46_phase_dma_max_bytes;
/* V38: diagnostic reasons and retry counts for the actual PS1 field stream. */
volatile uint32_t g_v38_dma_timeout_failures;
volatile uint32_t g_v38_dma_phase_failures;
volatile uint32_t g_v38_dma_status_failures;
volatile uint32_t g_v38_video_retry_frames;
volatile uint32_t g_v38_dma_video_failures;
volatile uint32_t g_v38_dma_audio_failures;
volatile uint32_t g_v38_dma_failed_state;
volatile uint32_t g_v38_dma_failed_lba;
volatile uint32_t g_v38_poll_detected_wrap;
volatile uint32_t g_v38_dma_watchdog_recoveries;
static uint16_t g_v38_dma_field_age;
static uint32_t g_scsi_dma_late_frames;
static uint32_t g_scsi_dma_video_underflows;
static uint32_t g_video_frames_presented;
static uint32_t g_video_frames_skipped;
static uint32_t g_video_frames_latched_distinct;
static uint32_t g_video_frames_audio_late;
static uint32_t g_video_frames_held;
static uint32_t g_video_frames_dropped_stale;
/* True 15fps-on-60Hz cadence telemetry: histogram of fields between
   *distinct* frames.  A 60Hz scanout alone cannot reveal jitter. */
static uint32_t g_video_cadence_intervals;
static uint32_t g_video_cadence_exact4;
static uint32_t g_video_cadence_short;
static uint32_t g_video_cadence_long;
static uint32_t g_video_cadence_max_gap;
static uint32_t g_video_cadence_last_field;
static uint32_t g_video_dma_early_ready;
static uint32_t g_video_ready_highwater;
static uint32_t g_video_refill_bursts;
static uint8_t g_video_refill_active;
static uint32_t g_vblank_latched_frames;
static uint32_t g_midfield_latched_frames;
volatile uint32_t g_latch_raster_max;
volatile uint32_t g_latch_after_safe_window;
volatile uint32_t g_latch_raster_sum;
static uint32_t g_video_buffer_stride_words;
static uint16_t g_video_low_count;
static uint16_t g_video_buffer_count;
static uint16_t g_video_prebuffer_target;
static uint16_t g_video_refill_low_water;
static uint32_t g_audio_start_latch_frame;
static uint32_t g_audio_start_visible_fields;
static uint32_t g_audio_start_ring_used;
static uint32_t g_audio_start_samples_emitted;
static uint32_t g_stream_lba;
static uint16_t g_stop_buttons;
static uint16_t g_pause_buttons;
static uint8_t g_loop_playback;
static uint8_t g_done;
static uint8_t g_abort;
static uint8_t g_paused;
static uint8_t g_seek_mode;
static uint8_t g_seek_in_progress;
static uint16_t g_start_frame;
static uint32_t g_seek_sector;
static uint32_t g_prev_pad;
static uint16_t g_fields_per_frame;
static uint16_t g_field_counter;
static uint32_t g_rainbow_visible_fields;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
/* V45: pre-arm the NEXT field's address after current decode is finished.
   These counters distinguish genuine player misses from display-only artifacts. */
volatile uint32_t g_v45_prearm_missed;
volatile uint32_t g_v45_prearm_odd;
volatile uint32_t g_v45_prearm_even;
volatile uint32_t g_v45_prearm_last_raster;
volatile uint32_t g_v45_prearm_late;
volatile uint32_t g_v45_underflow_frame;
volatile uint32_t g_v45_underflow_field;
volatile uint32_t g_v45_long_gap_frame;
volatile uint32_t g_v45_long_gap_fields;
static uint8_t g_v45_rainbow_enabled;
#endif


/* Audio backend hooks.  With no backend compiled (or a stream whose audio the
   backend cannot play) every hook is a no-op and video is paced by fields. */
#if defined(PCFX_PCFV_HAVE_AUDIO)
#define AUDIO_ON (g_audio_active)
static inline void audio_new_field(void) { if (AUDIO_ON) pcfv_audio_new_field(); }
static inline void audio_service(void) { if (AUDIO_ON) pcfv_audio_service(); }
static inline int audio_fetch_urgent(void) { return AUDIO_ON ? pcfv_audio_fetch_urgent() : 0; }
static inline int audio_fetch_background(void) { return AUDIO_ON ? pcfv_audio_fetch_background() : 0; }
static inline void audio_cd_done(void) { if (AUDIO_ON) pcfv_audio_cd_done(); }
static inline void audio_cd_failed(void) { if (AUDIO_ON) pcfv_audio_cd_failed(); }
static inline int audio_preroll_ready(void) { return AUDIO_ON ? pcfv_audio_preroll_ready() : 1; }
static inline void audio_seek(uint16_t frame) { if (AUDIO_ON) pcfv_audio_seek(frame); }
static inline void audio_after_boot(void) { if (AUDIO_ON) pcfv_audio_after_boot(); }
static inline void audio_visible_field(void) { if (AUDIO_ON) pcfv_audio_visible_field(); }
static inline int audio_playing(void) { return AUDIO_ON ? pcfv_audio_playing() : 0; }
static inline int audio_is_clock(void) { return AUDIO_ON && pcfv_audio_is_clock(); }
static inline int audio_clock_running(void) { return audio_is_clock() && pcfv_audio_started(); }
static inline uint32_t audio_clock(void) { return AUDIO_ON ? pcfv_audio_clock() : 0u; }
static inline uint32_t audio_buffered(void) { return AUDIO_ON ? pcfv_audio_buffered() : 0u; }
static inline int audio_complete(void) { return AUDIO_ON ? pcfv_audio_complete() : 1; }
static inline void audio_set_paused(int paused) { if (AUDIO_ON) pcfv_audio_set_paused(paused); }
static inline void audio_stop(void) { if (AUDIO_ON) pcfv_audio_stop(); }
#else
static inline void audio_new_field(void) { }
static inline void audio_service(void) { }
static inline int audio_fetch_urgent(void) { return 0; }
static inline int audio_fetch_background(void) { return 0; }
static inline void audio_cd_done(void) { }
static inline void audio_cd_failed(void) { }
static inline int audio_preroll_ready(void) { return 1; }
static inline void audio_seek(uint16_t frame) { (void)frame; }
static inline void audio_after_boot(void) { }
static inline void audio_visible_field(void) { }
static inline int audio_playing(void) { return 0; }
static inline int audio_is_clock(void) { return 0; }
static inline int audio_clock_running(void) { return 0; }
static inline uint32_t audio_clock(void) { return 0u; }
static inline uint32_t audio_buffered(void) { return 0u; }
static inline int audio_complete(void) { return 1; }
static inline void audio_set_paused(int paused) { (void)paused; }
static inline void audio_stop(void) { }
#endif

static uint8_t g_video_state[VIDEO_BUFFER_COUNT]; /* 0 free, 1 loading, 2 ready, 3 displaying */
static uint8_t g_video_ready_delay[VIDEO_BUFFER_COUNT];
static uint16_t g_video_frame[VIDEO_BUFFER_COUNT];
static uint32_t g_video_kram[VIDEO_BUFFER_COUNT];
static uint16_t g_next_load_frame;
static uint16_t g_next_display_frame;
static uint16_t g_current_display_buf;
static uint8_t g_have_display_buf;
/* Source-derived 480i records are even/odd INDEX PAIRS in the PCFV file.
   A pair stays pinned for four 60Hz fields; both KRAM spans are retained until
   the next complete pair is available. Compiled only for the test-only branch. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
static uint16_t g_current_even_buf;
volatile uint32_t g_paired_complete_latches;
volatile uint32_t g_paired_wait_both;
volatile uint32_t g_paired_odd_restarts;
volatile uint32_t g_paired_even_restarts;
volatile uint32_t g_paired_latches_on_even;
volatile uint32_t g_paired_even_start_deferrals;
volatile uint32_t g_paired_pair_start_odd;
#endif

static inline void scsi_reg_w(uint16_t reg, uint16_t value) {
    port_out_h(0x600, reg);
    port_out_h(0x604, value);
}

static inline uint16_t scsi_reg_r(uint16_t reg) {
    port_out_h(0x600, reg);
    return port_in_h(0x604);
}

static ScsiPhase scsi_phase_now(void) {
    uint16_t sr = port_in_h(0x602);
    uint16_t bits;

    if ((sr & 0x0040u) == 0) return SCSI_PHASE_BUS_FREE;
    if (sr & 0x0002u) return SCSI_PHASE_SELECT;

    bits = sr & 0x001Cu;
    if (bits == 0x0000u) return SCSI_PHASE_DATA_OUT;
    if (bits == 0x0004u) return SCSI_PHASE_DATA_IN;
    if (bits == 0x0008u) return SCSI_PHASE_COMMAND;
    if (bits == 0x000Cu) return SCSI_PHASE_STATUS;
    if (bits == 0x0018u) return SCSI_PHASE_MESSAGE_OUT;
    if (bits == 0x001Cu) return SCSI_PHASE_MESSAGE_IN;
    return SCSI_PHASE_ILLEGAL;
}

static inline int scsi_req_now(void) {
    return (port_in_h(0x602) & 0x0020u) != 0;
}

static void scsi_make_read10(uint8_t cdb[10], uint32_t lba, uint32_t bytes) {
    uint32_t sectors = (bytes + (PCFV_SECTOR_SIZE - 1u)) / PCFV_SECTOR_SIZE;
    cdb[0] = 0x28;
    cdb[1] = 0x00;
    cdb[2] = (uint8_t)(lba >> 24);
    cdb[3] = (uint8_t)(lba >> 16);
    cdb[4] = (uint8_t)(lba >> 8);
    cdb[5] = (uint8_t)(lba);
    cdb[6] = 0x00;
    cdb[7] = (uint8_t)(sectors >> 8);
    cdb[8] = (uint8_t)(sectors);
    cdb[9] = 0x00;
}

static void scsi_begin_king_dma(uint32_t kram_word_addr, uint32_t bytes) {
    scsi_reg_w(0x03, 0x0001);
    king_write_reg32(0x09, kram_word_addr);
    /* Transfer terminates on DATA IN -> STATUS, not an exact byte count.
       A nonzero count can require a trailing SCSI data handshake that a
       whole-sector READ(10) cannot deliver on some real KING revisions. */
    (void)bytes;
    king_write_reg32(0x0A, 0u);
    scsi_reg_w(0x02, 0x0002);
    king_write_reg16(0x0B, 0x0001);
    scsi_reg_w(0x07, 0x0001);
}

static void scsi_stop_king_dma_no_eat(void) {
    king_write_reg16(0x0B, 0x0000); /* disarm DMA on every teardown */
    scsi_reg_w(0x02, 0x0000);
    scsi_reg_w(0x03, 0x0000);
}

static void scsi_ack_assert(uint16_t ack_value) {
    scsi_reg_w(0x01, ack_value);
}

static void scsi_bus_abort_reset(void) {
    eris_scsi_abort();
    scsi_reset();
}

static int cd_dma_is_busy(void) {
    return g_cd_dma.state != CD_DMA_IDLE &&
           g_cd_dma.state != CD_DMA_DONE &&
           g_cd_dma.state != CD_DMA_ERROR;
}

static int cd_dma_start(CdReqKind kind, uint32_t lba, uint32_t kram_word_addr,
                        uint32_t bytes, uint16_t video_buf);
static void cd_dma_fail_current(void);

#if defined(PCFX_PCFV_FORM2)
static void form2_restore_audio_guard(void) {
    uint32_t i;
    if (!g_audio_guard_saved) return;
    king_set_kram_write(PCFV_ADPCM_KRAM_WORD_ADDR + PCFV_ADPCM_HALF_WORDS -
                        FORM2_GUARD_WORDS, 1);
    for (i = 0; i < FORM2_GUARD_WORDS; ++i)
        king_kram_write((uint16_t)g_audio_guard_buf[i*2u] |
                       ((uint16_t)g_audio_guard_buf[i*2u+1u] << 8));
    g_audio_guard_saved = 0u;
}
#endif

static void cd_dma_complete_current(void) {
    CdReqKind kind = g_cd_dma.kind;
    if (kind == CD_REQ_HEADER) {
        g_header_ready = 1;
#if defined(PCFX_PCFV_FORM2)
    } else if (kind == CD_REQ_MODE_SELECT) {
        g_sector_mode_ready = 1u;
    } else if (kind == CD_REQ_AUDIO) {
        form2_restore_audio_guard();
#endif
    } else if (kind == CD_REQ_VIDEO && g_cd_dma.video_buf < g_video_buffer_count) {
        uint16_t n = g_cd_dma.video_count ? g_cd_dma.video_count : 1u;
        uint16_t k;
        for (k = 0; k < n && (uint16_t)(g_cd_dma.video_buf + k) < g_video_buffer_count; ++k) {
            /* A frame safely pipelined earlier may already be displaying.
               Never demote it or restart its latency timer. */
            if (g_video_state[g_cd_dma.video_buf + k] == 1u) {
                g_video_state[g_cd_dma.video_buf + k] = 2;
                g_video_ready_delay[g_cd_dma.video_buf + k] = 2;
            }
        }
    }
    g_cd_dma.kind = CD_REQ_NONE;
    g_cd_dma.state = CD_DMA_DONE;
    g_scsi_dma_completed++;
    if (kind == CD_REQ_AUDIO) audio_cd_done();
}

static void cd_dma_fail_current(void) {
    CdReqKind kind = g_cd_dma.kind;
    g_v38_dma_failed_state = (uint32_t)g_cd_dma.state;
    g_v38_dma_failed_lba = g_cd_dma.lba;
    if (kind == CD_REQ_VIDEO) g_v38_dma_video_failures++;
    else if (kind == CD_REQ_AUDIO) g_v38_dma_audio_failures++;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    if (kind == CD_REQ_VIDEO && g_cd_dma.video_buf < g_video_buffer_count) {
        uint16_t failed_frame = g_video_frame[g_cd_dma.video_buf];
        /* A failed DMA request may have already advanced the prefetch cursor.
           Retrying the missing record is mandatory: otherwise a permanently
           missing half-pair deadlocks the player after the KRAM ring fills. */
        if (failed_frame < g_frame_count) {
            g_next_load_frame = failed_frame;
            g_video_refill_active = 1u;
            g_v38_video_retry_frames++;
        }
    }
#endif
    if (kind == CD_REQ_VIDEO && g_cd_dma.video_buf < g_video_buffer_count) {
        uint16_t n = g_cd_dma.video_count ? g_cd_dma.video_count : 1u;
        uint16_t k;
        for (k = 0; k < n && (uint16_t)(g_cd_dma.video_buf + k) < g_video_buffer_count; ++k) {
            g_video_state[g_cd_dma.video_buf + k] = 0;
            g_video_ready_delay[g_cd_dma.video_buf + k] = 0;
            g_video_frame[g_cd_dma.video_buf + k] = 0xffffu;
        }
    }
#if defined(PCFX_PCFV_FORM2)
    form2_restore_audio_guard();
#endif
    g_cd_dma.kind = CD_REQ_NONE;
    g_cd_dma.state = CD_DMA_ERROR;
    g_scsi_dma_errors++;
    scsi_bus_abort_reset();
    if (kind == CD_REQ_AUDIO) audio_cd_failed();
}

static int cd_dma_start(CdReqKind kind, uint32_t lba, uint32_t kram_word_addr, uint32_t bytes, uint16_t video_buf) {
    if (cd_dma_is_busy()) return 0;
    if (!bytes) return 0;

    bytes = ((bytes + PCFV_SECTOR_SIZE - 1u) / PCFV_SECTOR_SIZE) * PCFV_SECTOR_SIZE;
    memset(&g_cd_dma, 0, sizeof(g_cd_dma));
    g_cd_dma.kind = kind;
    g_cd_dma.lba = lba;
    g_cd_dma.kram_word_addr = kram_word_addr;
    g_cd_dma.bytes = bytes;
    g_cd_dma.video_buf = video_buf;
    g_cd_dma.video_count = 1u;
    g_cd_dma.cdb_len = 10;
    scsi_make_read10(g_cd_dma.cdb, lba, bytes);

    g_cd_dma.state = CD_DMA_SELECT_INIT;
    g_v38_dma_field_age = 0u;
    g_scsi_dma_started++;
    return 1;
}

#if defined(PCFX_PCFV_FORM2)
/* Non-blocking SCSI MODE SELECT(6), 4-byte header and 8-byte descriptor.
   Set 2324 before the very first PCFV READ(10) (including its index). */
static int cd_mode_select_form2_start(void) {
    if (cd_dma_is_busy()) return 0;
    memset(&g_cd_dma, 0, sizeof(g_cd_dma));
    g_cd_dma.kind = CD_REQ_MODE_SELECT;
    g_cd_dma.video_buf = 0xffffu;
    g_cd_dma.cdb[0] = 0x15u;
    g_cd_dma.cdb[4] = 12u;
    g_cd_dma.cdb_len = 6u;
    g_cd_dma.data_out[3] = 8u;
    g_cd_dma.data_out[9] = (uint8_t)(PCFV_SECTOR_SIZE >> 16);
    g_cd_dma.data_out[10] = (uint8_t)(PCFV_SECTOR_SIZE >> 8);
    g_cd_dma.data_out[11] = (uint8_t)PCFV_SECTOR_SIZE;
    g_cd_dma.state = CD_DMA_SELECT_INIT;
    g_scsi_dma_started++;
    return 1;
}
#endif

/* ---- Services for the audio backend (pcfx_pcfv_internal.h) ---- */

int pcfv_cd_busy(void) { return cd_dma_is_busy(); }

int pcfv_cd_read_audio(uint32_t lba, uint32_t kram_word_addr, uint32_t sectors) {
#if defined(PCFX_PCFV_FORM2)
    if (sectors == PCFV_ADPCM_HALF_SECTORS) {
        uint32_t guard_addr = kram_word_addr - FORM2_GUARD_WORDS;
        if (cd_dma_is_busy()) return 0;
        /* Reserve a 930-word gap between low video and the audio ring.
           Half 1's prefix temporarily occupies the tail of half 0;
           restore precisely that saved data before the CD transfer ends. */
        g_audio_guard_saved = (uint8_t)(kram_word_addr ==
            PCFV_ADPCM_KRAM_WORD_ADDR + PCFV_ADPCM_HALF_WORDS);
        if (g_audio_guard_saved)
            pcfv_kram_read(guard_addr, g_audio_guard_buf, FORM2_GUARD_BYTES);
        if (cd_dma_start(CD_REQ_AUDIO, lba, guard_addr,
                         sectors * PCFV_SECTOR_SIZE, 0xffffu))
            return 1;
        g_audio_guard_saved = 0;
        return 0;
    }
#endif
    return cd_dma_start(CD_REQ_AUDIO, lba, kram_word_addr, sectors * PCFV_SECTOR_SIZE, 0xffffu);
}

uint16_t pcfv_video_ready_count(void) { return count_ready_video_buffers(); }

uint16_t pcfv_video_prebuffer_target(void) {
    uint16_t target = g_video_prebuffer_target;
    if (target > g_video_buffer_count - 2u) target = g_video_buffer_count - 2u;
    return target;
}

uint32_t pcfv_video_frames_presented(void) { return g_video_frames_presented; }

/* In a packed VIDEO READ(10), a frame at the beginning of the transfer can
   already have reached KRAM while later frames are still downloading.  The
   transfer byte-count register is updated by KING DMA and is read-only here.
   Expose individually complete frames after a 2-field settling delay, never
   the current incomplete frame.  The final SCSI status is still validated. */
static void video_dma_release_complete_frames(void) {
    uint32_t left, sent;
    if (g_cd_dma.kind != CD_REQ_VIDEO || g_cd_dma.video_buf >= g_video_buffer_count ||
        g_cd_dma.video_early_index >= g_cd_dma.video_count) return;
    /* KING DMA cursor wraps at the 0x20000-word KRAM page boundary.
       Compare modulo 17 bits, not 18: a batch ending exactly at 0x20000
       reads back as zero, while reaching 0x40000 does likewise. This is
       safe because a batch stays within one sub-256KiB video bank.
       The batches stay within one KRAM bank and never exceed 256 KiB. */
    king_select(0x09u);
    left = port_in_w(0x604) & 0x1ffffu;
    sent = ((left - (g_cd_dma.kram_word_addr & 0x1ffffu)) & 0x1ffffu) * 2u;
    if (sent > g_cd_dma.bytes) return;
    while (g_cd_dma.video_early_index < g_cd_dma.video_count) {
        uint16_t buf = g_cd_dma.video_buf + g_cd_dma.video_early_index;
        uint16_t frame;
        uint32_t end_bytes;
        if (buf >= g_video_buffer_count) break;
        frame = g_video_frame[buf];
        if (frame >= g_frame_count) break;
        end_bytes = g_cd_dma.video_early_bytes +
                    g_entries[frame].video_sectors * PCFV_SECTOR_SIZE;
        if (sent < end_bytes) break;
        g_cd_dma.video_early_bytes = end_bytes;
        g_cd_dma.video_early_index++;
        if (g_video_state[buf] == 1u) {
            g_video_state[buf] = 2u;
            g_video_ready_delay[buf] = 1u;
            g_video_dma_early_ready++;
        }
    }
}

static void cd_dma_poll_one(void) {
    ScsiPhase ph;

    if (!cd_dma_is_busy()) return;
    if (++g_cd_dma.timeout > SCSI_TIMEOUT_TICKS) {
        g_v38_dma_timeout_failures++;
        cd_dma_fail_current();
        return;
    }

    switch (g_cd_dma.state) {
    case CD_DMA_SELECT_INIT:
        scsi_reg_w(0x03, 0x0000);
        scsi_reg_w(0x00, 0x0084);
        g_cd_dma.delay = 2;
        g_cd_dma.state = CD_DMA_SELECT_DELAY0;
        break;

    case CD_DMA_SELECT_DELAY0:
        if (g_cd_dma.delay) { g_cd_dma.delay--; break; }
        scsi_reg_w(0x01, 0x0001);        /* assert BSY */
        g_cd_dma.delay = 2;
        g_cd_dma.state = CD_DMA_SELECT_DELAY1;
        break;

    case CD_DMA_SELECT_DELAY1:
        if (g_cd_dma.delay) { g_cd_dma.delay--; break; }
        scsi_reg_w(0x01, 0x0005);        /* assert BSY + SEL */
        g_cd_dma.delay = 2;
        g_cd_dma.state = CD_DMA_SELECT_DELAY2;
        break;

    case CD_DMA_SELECT_DELAY2:
        if (g_cd_dma.delay) { g_cd_dma.delay--; break; }
        g_cd_dma.state = CD_DMA_WAIT_SELECTED;
        break;

    case CD_DMA_WAIT_SELECTED:
        if (scsi_phase_now() != SCSI_PHASE_BUS_FREE) {
            g_cd_dma.delay = 1;
            g_cd_dma.state = CD_DMA_RELEASE_DELAY;
        }
        break;

    case CD_DMA_RELEASE_DELAY:
        if (g_cd_dma.delay) { g_cd_dma.delay--; break; }
        scsi_reg_w(0x01, 0x0000);        /* release select */
        g_cd_dma.delay = 1;
        g_cd_dma.state = CD_DMA_WAIT_COMMAND;
        break;

    case CD_DMA_WAIT_COMMAND:
        if (g_cd_dma.delay) { g_cd_dma.delay--; break; }
        if (scsi_phase_now() == SCSI_PHASE_COMMAND) {
            scsi_reg_w(0x03, 0x0002);
            g_cd_dma.state = CD_DMA_COMMAND_WAIT_REQ;
        }
        break;

    case CD_DMA_COMMAND_WAIT_REQ:
        ph = scsi_phase_now();
        if (ph != SCSI_PHASE_COMMAND) {
            scsi_reg_w(0x01, 0x0000);
            scsi_reg_w(0x03, 0x0000);
            g_cd_dma.state = CD_DMA_WAIT_DATA_REQ;
            break;
        }
        if (scsi_req_now()) {
            uint8_t b = 0;
            if (g_cd_dma.cdb_pos < g_cd_dma.cdb_len) b = g_cd_dma.cdb[g_cd_dma.cdb_pos++];
            scsi_reg_w(0x00, b);
            scsi_reg_w(0x01, 0x0001);
            scsi_reg_w(0x01, 0x0011);
            g_cd_dma.state = CD_DMA_COMMAND_WAIT_DROP;
        }
        break;

    case CD_DMA_COMMAND_WAIT_DROP:
        ph = scsi_phase_now();
        if (ph != SCSI_PHASE_COMMAND) {
            scsi_reg_w(0x01, 0x0000);
            scsi_reg_w(0x03, 0x0000);
            g_cd_dma.state = CD_DMA_WAIT_DATA_REQ;
            break;
        }
        if (!scsi_req_now()) {
            scsi_reg_w(0x01, 0x0001);
            g_cd_dma.state = CD_DMA_COMMAND_WAIT_RAISE;
        }
        break;

    case CD_DMA_COMMAND_WAIT_RAISE:
        ph = scsi_phase_now();
        if (ph != SCSI_PHASE_COMMAND) {
            scsi_reg_w(0x01, 0x0000);
            scsi_reg_w(0x03, 0x0000);
            g_cd_dma.state = CD_DMA_WAIT_DATA_REQ;
            break;
        }
        if (scsi_req_now()) g_cd_dma.state = CD_DMA_COMMAND_WAIT_REQ;
        break;

    case CD_DMA_WAIT_DATA_REQ:
        ph = scsi_phase_now();
#if defined(PCFX_PCFV_FORM2)
        if (g_cd_dma.kind == CD_REQ_MODE_SELECT && ph == SCSI_PHASE_DATA_OUT) {
            scsi_reg_w(0x03, 0x0002);
            g_cd_dma.state = CD_DMA_DATAOUT_WAIT_REQ;
            break;
        }
#endif
        if (ph == SCSI_PHASE_DATA_IN && scsi_req_now()) {
            scsi_begin_king_dma(g_cd_dma.kram_word_addr, g_cd_dma.bytes);
            g_v46_phase_dma_started++;
            g_cd_dma.timeout = 0;
            g_cd_dma.state = CD_DMA_ACTIVE;
        } else if (ph == SCSI_PHASE_STATUS || ph == SCSI_PHASE_MESSAGE_IN || ph == SCSI_PHASE_BUS_FREE) {
            g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
        }
        break;

#if defined(PCFX_PCFV_FORM2)
    case CD_DMA_DATAOUT_WAIT_REQ:
        ph = scsi_phase_now();
        if (ph != SCSI_PHASE_DATA_OUT) {
            scsi_reg_w(0x01, 0x0000);
            scsi_reg_w(0x03, 0x0000);
            if (g_cd_dma.data_out_pos != 12u) cd_dma_fail_current();
            else g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
            break;
        }
        if (scsi_req_now()) {
            if (g_cd_dma.data_out_pos >= 12u) { cd_dma_fail_current(); break; }
            scsi_reg_w(0x00, g_cd_dma.data_out[g_cd_dma.data_out_pos++]);
            scsi_ack_assert(0x0011);
            g_cd_dma.state = CD_DMA_DATAOUT_WAIT_DROP;
        }
        break;
    case CD_DMA_DATAOUT_WAIT_DROP:
        if (scsi_phase_now() != SCSI_PHASE_DATA_OUT) {
            scsi_ack_assert(0);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
        } else if (!scsi_req_now()) {
            scsi_ack_assert(0x0001);
            g_cd_dma.state = CD_DMA_DATAOUT_WAIT_RAISE;
        }
        break;
    case CD_DMA_DATAOUT_WAIT_RAISE:
        if (scsi_phase_now() != SCSI_PHASE_DATA_OUT) {
            scsi_ack_assert(0);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
        } else if (scsi_req_now()) {
            g_cd_dma.state = CD_DMA_DATAOUT_WAIT_REQ;
        }
        break;
#endif
    case CD_DMA_ACTIVE:
        video_dma_release_complete_frames();
        /* Wait for the target to leave DATA IN. Never poll the programmed
           byte count for phase-driven DMA completion: it starts at zero. */
        ph = scsi_phase_now();
        if (ph != SCSI_PHASE_DATA_IN) {
            uint32_t pos, got;
            king_select(0x09u);
            pos = port_in_w(0x604) & 0x1ffffu;
            got = ((pos - (g_cd_dma.kram_word_addr & 0x1ffffu)) & 0x1ffffu) * 2u;
            if (got > g_v46_phase_dma_max_bytes) g_v46_phase_dma_max_bytes = got;
            g_v46_phase_dma_ended++;
            scsi_stop_king_dma_no_eat();
            if (got != g_cd_dma.bytes) {
                if (!g_v47_first_bad_expected) {
                    g_v47_first_bad_expected = g_cd_dma.bytes;
                    g_v47_first_bad_received = got;
                    g_v47_first_bad_address = g_cd_dma.kram_word_addr;
                    g_v47_first_bad_position = pos;
                    g_v47_first_bad_kind = (uint32_t)g_cd_dma.kind;
                    g_v47_first_bad_lba = g_cd_dma.lba;
                }
                if (got < g_cd_dma.bytes) g_v46_phase_dma_short++;
                else g_v46_phase_dma_overrun++;
                g_v38_dma_phase_failures++;
                cd_dma_fail_current();
                break;
            }
            g_cd_dma.timeout = 0;
            g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
        }
        break;

    case CD_DMA_FINISH_WAIT_REQ:
        ph = scsi_phase_now();
        if (ph == SCSI_PHASE_BUS_FREE) {
            cd_dma_complete_current();
            break;
        }
        if (!scsi_req_now()) break;

        if (ph == SCSI_PHASE_STATUS) {
            g_cd_dma.status_byte = (uint8_t)scsi_reg_r(0x00);
            scsi_ack_assert(0x0010);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_DROP;
        } else if (ph == SCSI_PHASE_MESSAGE_IN) {
            g_cd_dma.message_byte = (uint8_t)scsi_reg_r(0x00);
            scsi_ack_assert(0x0010);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_DROP;
        } else if (ph == SCSI_PHASE_DATA_IN) {
            (void)scsi_reg_r(0x00);      /* eat stray input byte if the target supplies one */
            scsi_ack_assert(0x0010);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_DROP;
        } else {
            g_v38_dma_phase_failures++;
            cd_dma_fail_current();
        }
        break;

    case CD_DMA_FINISH_WAIT_DROP:
        if (!scsi_req_now()) {
            scsi_ack_assert(0x0000);
            g_cd_dma.state = CD_DMA_FINISH_WAIT_NEXT;
        }
        break;

    case CD_DMA_FINISH_WAIT_NEXT:
        ph = scsi_phase_now();
        if (ph == SCSI_PHASE_BUS_FREE) {
            if (g_cd_dma.status_byte == 0x00 || g_cd_dma.status_byte == 0x10) cd_dma_complete_current();
            else { g_v38_dma_status_failures++; cd_dma_fail_current(); }
        } else if (scsi_req_now()) {
            g_cd_dma.state = CD_DMA_FINISH_WAIT_REQ;
        }
        break;

    default:
        break;
    }
}

static void cd_dma_poll_budget(uint16_t budget) {
    while (budget--) {
        CdDmaState old_state = g_cd_dma.state;
        uint8_t old_pos = g_cd_dma.cdb_pos;
        cd_dma_poll_one();
        if (!cd_dma_is_busy()) break;
        if (g_cd_dma.state == old_state && g_cd_dma.cdb_pos == old_pos) {
            /* Avoid monopolizing a whole field while waiting for REQ/phase. */
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */

static void setup_rainbow_regs(void) {
    port_out_h(0x200, 0x0000);
    port_out_h(0x202, 0x0000);
    port_out_h(0x208, 0xff80);
    port_out_h(0x20c, 0x0000);
    port_out_h(0x210, 0x0000);
    port_out_h(0x214, 0x0000);
    port_out_h(0x204, 0x0001);
}

static void setup_video(void) {
    king_init();
    king_set_kram_mode(1); /* 4-Mbit KRAM mode before access */
    tetsu_init();

    king_set_kram_pages(0, 0, 0, 0);
    king_set_bg_prio(KING_BGPRIO_HIDE, KING_BGPRIO_HIDE, KING_BGPRIO_HIDE, KING_BGPRIO_HIDE, 0);
    king_set_bg_mode(KING_BGMODE_NONE, KING_BGMODE_NONE, KING_BGMODE_NONE, KING_BGMODE_NONE);

    tetsu_set_vdc_palette(0, 0);
    tetsu_set_king_palette(0, 0, 0, 0);
    tetsu_set_rainbow_palette(0);
    tetsu_set_priorities(0, 0, 0, 0, 0, 0, 0);
    g_rainbow_visible = 0;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    g_v45_rainbow_enabled = 0u;
#endif
    /* Keep RAINBOW fetch enabled from boot, but with output priority zero.
       That lets the HuC6271 pipeline warm without showing the layer. */
    tetsu_set_video_mode(DW3_FMV_TETSU_LINES, 0, TETSU_DOTCLOCK_5MHz,
                              TETSU_COLORS_256, TETSU_COLORS_16,
                              0, 0, 0, 0, 0, 0, 1);

    vdc_setreg(0, VDC_REG_CR, VDC_CR_BB);
    vdc_setreg(1, VDC_REG_CR, VDC_CR_BB);
}

static void set_rainbow_visible(uint8_t visible) {
    tetsu_set_priorities(0, 0, 0, 0, 0, 0, visible ? 7 : 0);
    /* Do not rewrite TETSU video-mode at the logo->FMV handoff. Mode
       is set once during setup_video; switching only the layer priority
       avoids resetting field phase and causes no half-field mode change. */
    g_rainbow_visible = visible ? 1 : 0;
}

/* Interrupt-atomic KING register runs.  Kept local so the player is correct
   with any libpcfx: liberis, and libpcfx before its 012_irq_save_restore fix,
   return the raw PSW.ID bit (0x1000) from irq_disable() and write
   PSW = 0x1000, while irq_restore() keeps only bit 0 of its argument, so
   irq_restore(irq_disable()) always left interrupts ENABLED
   (vendor/libpcfx/src/v810.S).  Here that switched IRQs on at the first
   RAINBOW latch of builds that install no handler (ADPCM, silent), and a
   stray interrupt overwrote the stream index in RAM; MP2 builds only worked
   because it also serviced a pending VDC-A IRQ early (see psg_sample.c). */
static inline uint32_t psw_irq_save(void) {
    uint32_t psw, off;
    __asm__ volatile ("stsr PSW, %0" : "=r"(psw) :: "memory");
    off = psw | 0x1000u;
    __asm__ volatile ("ldsr %0, PSW" :: "r"(off) : "memory");
    return psw & 0x1000u;
}

static inline void psw_irq_restore(uint32_t id) {
    uint32_t psw;
    __asm__ volatile ("stsr PSW, %0" : "=r"(psw) :: "memory");
    psw = (psw & ~0x1000u) | id;
    __asm__ volatile ("ldsr %0, PSW" :: "r"(psw) : "memory");
}

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
/* KING/HuC6272 RAINBOW transfer registers are latched at TSR (raster 6).
   Resetting KR_CR at each new field risks aborting the previous block, and
   writing the target AFTER raster 6 silently loses an entire field.  Once
   enabled, change only the NEXT transfer's KRAM address and length during the
   SAFE LATE window (current field last strip finished at raster 246).
   The current field's buffer is released only after that point. */
static void v45_prearm_next_field(uint32_t addr) {
    uint32_t irq_state = psw_irq_save();
    king_write_reg32(0x41, addr);
    king_write_reg16(0x42, RAINBOW_START_SCANLINE);
    king_write_reg16(0x43, 15u);
    if (!g_v45_rainbow_enabled) {
        king_write_reg16(0x40, 0x0001);
        g_v45_rainbow_enabled = 1u;
    }
    psw_irq_restore(irq_state);
}
#endif

static void start_rainbow_frame(uint32_t kram_word_addr, uint16_t block_count) {
    uint32_t irq_state = psw_irq_save();
    king_write_reg16(0x40, 0x0000);
    king_write_reg32(0x41, kram_word_addr);
    king_write_reg16(0x42, RAINBOW_START_SCANLINE);
    king_write_reg16(0x43, block_count);
    king_write_reg16(0x44, 0x0000);
    king_write_reg16(0x40, 0x0001);
    psw_irq_restore(irq_state);
}

/* KRAM -> RAM with libpcfx's auto-incrementing read cursor, the same
   copy-out eris_cd_read_dma() uses.  Little-endian: byte 0 is the low half. */
void pcfv_kram_read(uint32_t word_addr, uint8_t *dst, uint32_t bytes) {
    uint32_t i, words = bytes >> 1;
    king_set_kram_read(word_addr, 1);
    for (i = 0; i < words; ++i) {
        uint16_t v = king_kram_read();
        dst[i * 2u + 0u] = (uint8_t)v;
        dst[i * 2u + 1u] = (uint8_t)(v >> 8);
    }
    if (bytes & 1u) dst[bytes - 1u] = (uint8_t)king_kram_read();
}

static int parse_pcfv_header(void) {
    uint32_t index_bytes, stride_sectors;
    uint16_t i;

    /* The CD read covers the largest index; copy out only what this one has. */
    pcfv_kram_read(KRAM_HEADER_WORD_ADDR, g_pcfv_head, 64u);
    if (memcmp(g_pcfv_head, "PCFV0001", 8) != 0) return 0;
    if (rd16(g_pcfv_head + 8) != RAINBOW_WIDTH || rd16(g_pcfv_head + 10) != RAINBOW_HEIGHT) return 0;

    g_fps_num = rd16(g_pcfv_head + 12);
    g_fps_den = rd16(g_pcfv_head + 14);
    if (!g_fps_num || !g_fps_den || (uint32_t)g_fps_num > 60u * g_fps_den) return 0;
    g_frame_count = rd16(g_pcfv_head + 16);
    g_pcfv_flags = rd16(g_pcfv_head + 18);
    if (g_frame_count == 0 || g_frame_count > PCFV_MAX_FRAMES) return 0;
#if defined(PCFX_PCFV_FORM2)
    if (!(g_pcfv_flags & PCFV_FLAG_FORM2)) return 0;
#else
    if (g_pcfv_flags & PCFV_FLAG_FORM2) return 0;
#endif
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* 0x0004: each 15fps source frame has two independently encoded
       256x240 field records, odd first. Reject legacy 240p silently
       falling into the 480i playback branch. */
    if ((g_pcfv_flags & 0x0004u) == 0 || (g_frame_count & 1u) ||
        g_fps_num != 15u || g_fps_den != 1u) return 0;
#else
    /* Ordinary builds must not misinterpret a paired interlace stream. */
    if (g_pcfv_flags & 0x0004u) return 0;
#endif

    index_bytes = rd32(g_pcfv_head + 20);
    g_data_start_sector = rd32(g_pcfv_head + 24);
    stride_sectors = rd32(g_pcfv_head + 28);
    g_audio_bytes = rd32(g_pcfv_head + 32);
    g_audio_sectors = rd32(g_pcfv_head + 36);
    g_audio_preroll_sectors = rd32(g_pcfv_head + 40);
    g_audio_refill_sectors = rd32(g_pcfv_head + 44);
    g_ring_half_bytes = rd32(g_pcfv_head + 48);
    g_audio_rate_hz = rd32(g_pcfv_head + 52);
    if (!g_audio_rate_hz) g_audio_rate_hz = 31468u;
    if ((g_pcfv_flags & PCFV_FLAG_MP2_AUDIO) && g_audio_bytes) {
        g_audio_codec = PCFV_AUDIO_CODEC_MP2;
    } else if (g_audio_bytes) {
        g_audio_codec = PCFV_AUDIO_CODEC_ADPCM;
    } else {
        g_audio_codec = PCFV_AUDIO_CODEC_NONE;
    }
    g_video_buffer_stride_words = (stride_sectors * PCFV_SECTOR_SIZE + 1u) >> 1;
    if (!g_video_buffer_stride_words || g_video_buffer_stride_words > VIDEO_BUFFER_STRIDE_MAX) return 0;
    /* Allocate the maximum complete stride-sized slots around (never over)
       the ADPCM ring; retain the original 30/20 split for <=6 sectors. */
    {
        #if defined(PCFX_PCFV_FORM2)
        uint32_t lo = (0x00020000u - FORM2_GUARD_WORDS - VIDEO_BUFFER_FIRST_WORD) / g_video_buffer_stride_words;
#else
        uint32_t lo = (0x00020000u - VIDEO_BUFFER_FIRST_WORD) / g_video_buffer_stride_words;
#endif
        uint32_t hi = (0x00040000u - 0x00030000u) / g_video_buffer_stride_words;
        if (lo > VIDEO_BUFFER_LOW_COUNT) lo = VIDEO_BUFFER_LOW_COUNT;
        if (hi > (VIDEO_BUFFER_COUNT - VIDEO_BUFFER_LOW_COUNT))
            hi = VIDEO_BUFFER_COUNT - VIDEO_BUFFER_LOW_COUNT;
        g_video_low_count = (uint16_t)lo;
        g_video_buffer_count = (uint16_t)(lo + hi);
        if (lo < 2u || hi < 2u || g_video_buffer_count > VIDEO_BUFFER_COUNT) return 0;
        g_video_prebuffer_target = (uint16_t)(g_video_buffer_count - 2u);
        /* V31: the six-sector intro needs a wider refill margin after
         * the title overlay layout change. Leave the existing full-buffer
         * target and protected ADPCM ring untouched. 7/8-sector authoring
         * still requires separate strict full-length validation. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        /* Preserve the previously verified short refill hysteresis; larger
           video bursts can delay the field-pair deadline behind a single
           long SCSI command at real CD timings. */
        g_video_refill_low_water = (uint16_t)(g_video_buffer_count - 6u);
#else
        g_video_refill_low_water = (uint16_t)(g_video_buffer_count - 3u);
#endif
        #if defined(PCFX_PCFV_FORM2)
        if (VIDEO_BUFFER_FIRST_WORD + lo * g_video_buffer_stride_words > 0x00020000u - FORM2_GUARD_WORDS) return 0;
#else
        if (VIDEO_BUFFER_FIRST_WORD + lo * g_video_buffer_stride_words > 0x00020000u) return 0;
#endif
        if (0x00030000u + hi * g_video_buffer_stride_words > 0x00040000u) return 0;
    }
    if (index_bytes != (uint32_t)g_frame_count * 32u) return 0;
    if ((64u + index_bytes) > PCFV_HEADER_READ_BYTES) return 0;
    pcfv_kram_read(KRAM_HEADER_WORD_ADDR, g_pcfv_head, 64u + index_bytes);

    g_audio_chunk_count = 0;
    for (i = 0; i < g_frame_count; ++i) {
        const uint8_t *e = g_pcfv_head + 64u + ((uint32_t)i * 32u);
        g_entries[i].video_sector = rd32(e + 0);
        g_entries[i].video_size = rd32(e + 4);
        g_entries[i].video_sectors = rd32(e + 8);
        g_entries[i].video_crc32 = rd32(e + 12);
        g_entries[i].audio_sector = rd32(e + 16);
        g_entries[i].audio_sectors = rd32(e + 20);
        g_entries[i].audio_byte_offset = rd32(e + 24);
        g_entries[i].flags = rd32(e + 28);
        /* Every compressed frame fits its reserved KRAM slot; contiguous
           variable-size runs are DMA-batched without disc padding. */
        if (!g_entries[i].video_sectors || g_entries[i].video_sectors > stride_sectors ||
            !g_entries[i].video_size ||
            g_entries[i].video_size > g_entries[i].video_sectors * PCFV_SECTOR_SIZE ||
            g_entries[i].video_sector < g_data_start_sector) return 0;
        if (g_entries[i].audio_sectors && g_audio_chunk_count < PCFV_MAX_AUDIO_CHUNKS) {
            g_audio_chunks[g_audio_chunk_count].sector = g_entries[i].audio_sector;
            g_audio_chunks[g_audio_chunk_count].sectors = g_entries[i].audio_sectors;
            g_audio_chunks[g_audio_chunk_count].byte_offset = g_entries[i].audio_byte_offset;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
            g_audio_chunks[g_audio_chunk_count].frame_index = (uint16_t)(i >> 1);
#else
            g_audio_chunks[g_audio_chunk_count].frame_index = i;
#endif
            g_audio_chunk_count++;
        }
    }
    return 1;
}

/* Hand the parsed stream to the compiled audio backend.  A stream it cannot
   play (another codec, a malformed layout) plays silently. */
static void pcfv_attach_audio(void) {
#if defined(PCFX_PCFV_HAVE_AUDIO)
    PcfvStreamInfo info;
    info.lba = g_stream_lba;
    info.data_start_sector = g_data_start_sector;
    info.audio_bytes = g_audio_bytes;
    info.audio_sectors = g_audio_sectors;
    info.audio_rate_hz = g_audio_rate_hz;
    info.audio_preroll_sectors = g_audio_preroll_sectors;
    info.audio_refill_sectors = g_audio_refill_sectors;
    info.ring_half_bytes = g_ring_half_bytes;
    info.flags = g_pcfv_flags;
    info.fps_num = g_fps_num;
    info.fps_den = g_fps_den;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    info.frame_count = (uint16_t)(g_frame_count >> 1);
#else
    info.frame_count = g_frame_count;
#endif
    info.audio_chunk_count = g_audio_chunk_count;
    info.audio_codec = g_audio_codec;
    info.audio_chunks = g_audio_chunks;
    g_audio_active = (uint8_t)(pcfv_audio_attach(&info) ? 1u : 0u);
#endif
}

static uint16_t pcfv_find_frame_for_asset_sector(uint32_t asset_sector) {
    uint16_t i;
    if (!g_frame_count) return 0;

    /* A sector before the first video frame maps to frame 0. */
    for (i = 0; i < g_frame_count; ++i) {
        if (g_entries[i].video_sector >= asset_sector) return i;
        if (asset_sector >= g_entries[i].video_sector &&
            asset_sector < (g_entries[i].video_sector + g_entries[i].video_sectors)) return i;
    }
    return (uint16_t)(g_frame_count - 1u);
}

static uint16_t pcfv_resolve_start_frame(void) {
    uint32_t asset_sector = 0;

    switch (g_seek_mode) {
    case PCFX_PCFV_SEEK_FRAME:
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        return (g_seek_sector < (g_frame_count >> 1)) ?
            (uint16_t)(g_seek_sector * 2u) : (uint16_t)(g_frame_count - 2u);
#else
        return (g_seek_sector < g_frame_count) ? (uint16_t)g_seek_sector : (uint16_t)(g_frame_count - 1u);
#endif
    case PCFX_PCFV_SEEK_STREAM_SECTOR:
        asset_sector = g_seek_sector;
        break;
    case PCFX_PCFV_SEEK_DISC_LBA:
        asset_sector = (g_seek_sector > g_stream_lba) ? (g_seek_sector - g_stream_lba) : 0u;
        break;
    case PCFX_PCFV_SEEK_DATA_SECTOR:
        asset_sector = g_data_start_sector + g_seek_sector;
        break;
    case PCFX_PCFV_SEEK_NONE:
    default:
        asset_sector = 0;
        break;
    }

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* A seek to an even field must begin at its paired odd field. */
    return (uint16_t)(pcfv_find_frame_for_asset_sector(asset_sector) & ~1u);
#else
    return pcfv_find_frame_for_asset_sector(asset_sector);
#endif
}

static void setup_video_buffers(void) {
    uint16_t i;
    for (i = 0; i < g_video_buffer_count; ++i) {
        g_video_state[i] = 0;
        g_video_ready_delay[i] = 0;
        g_video_frame[i] = 0xffffu;
        g_video_kram[i] = i < g_video_low_count
            ? VIDEO_BUFFER_FIRST_WORD + ((uint32_t)i * g_video_buffer_stride_words)
            : 0x00030000u + ((uint32_t)(i - g_video_low_count) * g_video_buffer_stride_words);
    }
    g_next_load_frame = g_start_frame;
    g_video_refill_active = 0;
    g_next_display_frame = g_start_frame;
    g_have_display_buf = 0;
    g_current_display_buf = 0xffffu;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    g_current_even_buf = 0xffffu;
#endif
}


/* Reset the read-ahead ring for a new target while optionally keeping the
   current visible buffer locked.  This is the key distinction between startup
   and in-game seeks: startup shows black while it prebuffers, while runtime
   seeks keep the last good frame on screen until the target frame is ready. */
static void setup_video_buffers_for_seek(uint16_t frame, uint8_t keep_current_display) {
    uint16_t i;
    uint16_t keep = (keep_current_display && g_have_display_buf && g_current_display_buf < g_video_buffer_count)
                    ? g_current_display_buf : 0xffffu;

    for (i = 0; i < g_video_buffer_count; ++i) {
        g_video_kram[i] = i < g_video_low_count
            ? VIDEO_BUFFER_FIRST_WORD + ((uint32_t)i * g_video_buffer_stride_words)
            : 0x00030000u + ((uint32_t)(i - g_video_low_count) * g_video_buffer_stride_words);
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        if (i == keep || (keep_current_display && g_have_display_buf && i == g_current_even_buf)) {
#else
        if (i == keep) {
#endif
            g_video_state[i] = 3;
            /* Preserve g_video_frame[i] as an informational current-frame value. */
            g_video_ready_delay[i] = 0;
        } else {
            g_video_state[i] = 0;
            g_video_ready_delay[i] = 0;
            g_video_frame[i] = 0xffffu;
        }
    }

    g_next_load_frame = frame;
    g_video_refill_active = 0;
    g_next_display_frame = frame;
    g_field_counter = 0;
    g_done = 0;
}

static void cd_dma_cancel_for_seek(void) {
    if (cd_dma_is_busy()) {
        if (g_cd_dma.kind == CD_REQ_VIDEO && g_cd_dma.video_buf < g_video_buffer_count) {
            g_video_state[g_cd_dma.video_buf] = 0;
            g_video_ready_delay[g_cd_dma.video_buf] = 0;
            g_video_frame[g_cd_dma.video_buf] = 0xffffu;
        }
        scsi_bus_abort_reset();
        if (g_cd_dma.kind == CD_REQ_AUDIO) audio_cd_failed();
    }
    memset(&g_cd_dma, 0, sizeof(g_cd_dma));
}


static int find_video_buf_for_frame(uint16_t frame) {
    uint16_t i;
    for (i = 0; i < g_video_buffer_count; ++i) {
        if (g_video_state[i] == 2 && g_video_ready_delay[i] == 0 && g_video_frame[i] == frame) return (int)i;
    }
    return -1;
}

static uint16_t frame_distance(uint16_t from, uint16_t to) {
    if (!g_frame_count) return 0xffffu;
    return (to >= from) ? (uint16_t)(to - from) : (uint16_t)(g_frame_count - from + to);
}

static uint16_t count_ready_video_buffers(void) {
    uint16_t i, n = 0;
    for (i = 0; i < g_video_buffer_count; ++i) if (g_video_state[i] == 2 && g_video_ready_delay[i] == 0) n++;
    if (n > g_video_ready_highwater) g_video_ready_highwater = n;
    return n;
}

static void age_ready_video_buffers(void) {
    uint16_t i;
    for (i = 0; i < g_video_buffer_count; ++i) {
        if (g_video_state[i] == 2 && g_video_ready_delay[i]) g_video_ready_delay[i]--;
    }
}

static void drop_stale_video_buffers(uint16_t target_frame) {
    uint16_t i;
    if (!g_frame_count) return;
    for (i = 0; i < g_video_buffer_count; ++i) {
        if (g_video_state[i] == 2 && g_video_ready_delay[i] == 0) {
            uint16_t d = frame_distance(target_frame, g_video_frame[i]);
            if (d > (g_frame_count / 2u)) {
                g_video_state[i] = 0;
                g_video_ready_delay[i] = 0;
                g_video_frame[i] = 0xffffu;
                g_video_frames_dropped_stale++;
            }
        }
    }
}

static int find_best_ready_video_buf(uint16_t target_frame, uint16_t max_skip, uint16_t *skip_out) {
    uint16_t i;
    uint16_t best_d = 0xffffu;
    int best_i = -1;
    for (i = 0; i < g_video_buffer_count; ++i) {
        if (g_video_state[i] == 2 && g_video_ready_delay[i] == 0) {
            uint16_t d = frame_distance(target_frame, g_video_frame[i]);
            if (d <= max_skip && d < best_d) {
                best_d = d;
                best_i = (int)i;
            }
        }
    }
    if (best_i >= 0 && skip_out) *skip_out = best_d;
    return best_i;
}

static int frame_already_loading_or_ready(uint16_t frame) {
    uint16_t i;
    for (i = 0; i < g_video_buffer_count; ++i) {
        if (g_video_state[i] != 0 && g_video_frame[i] == frame) return 1;
    }
    return 0;
}

/* Prefer the longest free run; grabbing the first isolated slot creates a
   single-frame READ(10) although a second KRAM bank might fit a full batch.
   That raises CD command overhead and starves playback at high bitrates. */
static int alloc_video_buf_run(uint16_t wanted, uint16_t *count_out) {
    uint16_t i, best_n = 0;
    int best_i = -1;
    if (wanted < 1u) wanted = 1u;
    if (wanted > VIDEO_BATCH_MAX) wanted = VIDEO_BATCH_MAX;
    for (i = 0; i < g_video_buffer_count;) {
        uint16_t n = 0;
        uint16_t bank_end = i < g_video_low_count ? g_video_low_count : g_video_buffer_count;
        if (g_video_state[i] != 0) { i++; continue; }
        while ((uint16_t)(i+n) < bank_end && g_video_state[i+n] == 0) n++;
        if (n > best_n) { best_i = (int)i; best_n = n; }
        i = (uint16_t)(i + n);
        if (best_n >= wanted) break;
    }
    if (best_n > wanted) best_n = wanted;
    *count_out = best_n;
    return best_i;
}

/* Batch variable-size, sector-packed video directly into consecutive *reserved*
   KRAM slots.  The physical slot reservation remains stride-sized (so other
   queued frames cannot overlap) but RAINBOW decode addresses are the offsets
   of the packed bitstreams within the DMA block, not the slot starts.

   Audio chunks intentionally interrupt batching; never DMA across their
   intervening disc sectors into the video ring. */
static uint32_t video_slot_base(uint16_t slot) {
    return slot < g_video_low_count
        ? VIDEO_BUFFER_FIRST_WORD + ((uint32_t)slot * g_video_buffer_stride_words)
        : 0x00030000u + ((uint32_t)(slot - g_video_low_count) * g_video_buffer_stride_words);
}

static uint16_t video_contiguous_batch_count(uint16_t frame) {
    uint16_t n = 1u;
    while (n < VIDEO_BATCH_MAX && (uint16_t)(frame + n) < g_frame_count) {
        uint16_t prev = (uint16_t)(frame + n - 1u);
        uint16_t curf = (uint16_t)(frame + n);
        if (frame_already_loading_or_ready(curf)) break;
        if (g_entries[prev].video_sector + g_entries[prev].video_sectors !=
            g_entries[curf].video_sector) break;
        n++;
    }
    return n;
}

static void queue_more_video_if_idle(void) {
    uint16_t probes;
    const uint32_t base = g_stream_lba;

    if (cd_dma_is_busy()) return;
    if (!g_frame_count) return;
    if (!g_loop_playback && g_next_load_frame >= g_frame_count) return;

    /* Hysteresis: avoid single-frame SCSI commands on every displayed frame.
       Start a refill burst when a substantial contiguous run is free, and
       keep filling in multi-frame commands until the high watermark is hit.
       The resident audio ring is independent and urgent audio still wins. */
    if (g_video_frames_presented) {
        uint16_t ready = count_ready_video_buffers();
        if (ready >= g_video_prebuffer_target &&
            frame_already_loading_or_ready(g_next_display_frame)) {
            g_video_refill_active = 0;
            return;
        }
        if (!g_video_refill_active) {
            if (ready > g_video_refill_low_water &&
                frame_already_loading_or_ready(g_next_display_frame)) return;
            g_video_refill_active = 1;
            g_video_refill_bursts++;
        }
    }

    for (probes = 0; probes < g_frame_count; ++probes) {
        int bi;
        uint16_t frame = g_next_load_frame;
        if (!frame_already_loading_or_ready(frame)) {
            uint16_t want = video_contiguous_batch_count(frame);
            uint16_t got = 0, k;
            bi = alloc_video_buf_run(want, &got);
            if (bi < 0 || got == 0u) return;
            if (got > want) got = want;
            /* A single CD read transfers the packed sector extents into the
               entire reserved run.  Each RAINBOW address points to its own
               compressed frame, so no CPU/KRAM repacking is required. */
            uint32_t packed_words = 0;
            uint32_t reserved_words = (uint32_t)got * g_video_buffer_stride_words;
            uint32_t run_base = video_slot_base((uint16_t)bi);
            for (k = 0; k < got; ++k) {
                uint32_t used = g_entries[frame + k].video_sectors * (PCFV_SECTOR_SIZE / 2u);
                g_video_kram[bi + k] = run_base + packed_words;
                packed_words += used;
                g_video_state[bi + k] = 1;
                g_video_ready_delay[bi + k] = 0;
                g_video_frame[bi + k] = (uint16_t)(frame + k);
            }
            if (packed_words > reserved_words) {
                /* Never allow a malformed index to overwrite the next run. */
                for (k = 0; k < got; ++k) g_video_state[bi + k] = 0;
                return;
            }
            if (cd_dma_start(CD_REQ_VIDEO, base + g_entries[frame].video_sector, run_base,
                             packed_words * 2u, (uint16_t)bi)) {
                g_cd_dma.video_count = got;
                g_next_load_frame = (uint16_t)(g_next_load_frame + got);
                if (g_next_load_frame >= g_frame_count) {
                    if (g_loop_playback) g_next_load_frame = 0;
                    else g_next_load_frame = g_frame_count;
                }
                return;
            }
            for (k = 0; k < got; ++k) {
                g_video_state[bi + k] = 0;
                g_video_ready_delay[bi + k] = 0;
                g_video_frame[bi + k] = 0xffffu;
            }
            return;
        }
        g_next_load_frame++;
        if (g_next_load_frame >= g_frame_count) {
            if (g_loop_playback) g_next_load_frame = 0;
            else g_next_load_frame = g_frame_count;
        }
    }
}

static void scheduler_start_if_idle(void) {
    if (cd_dma_is_busy()) return;

#if defined(PCFX_PCFV_FORM2)
    if (!g_sector_mode_ready) {
        (void)cd_mode_select_form2_start();
        return;
    }
#endif
    if (!g_header_ready) {
        (void)cd_dma_start(CD_REQ_HEADER, g_stream_lba, KRAM_HEADER_WORD_ADDR, PCFV_HEADER_READ_BYTES, 0xffffu);
        return;
    }

    /* Audio with a hard deadline (ADPCM refills, a starving MP2 ring) goes
       before speculative video: the video ring has several frames of slack. */
    if (audio_fetch_urgent()) return;

    queue_more_video_if_idle();

    if (!cd_dma_is_busy()) (void)audio_fetch_background();
}

static void scheduler_poll(void) {
    cd_dma_poll_budget(SCHEDULER_POLL_BUDGET);
    if (!cd_dma_is_busy()) scheduler_start_if_idle();
}

static void fail_black_loop(void) {
    setup_rainbow_regs();
    for (;;) wait_vblank();
}

/* One hidden boot field: service CD and audio, then wait for vblank. */
static void boot_field(void) {
    audio_new_field();
    scheduler_poll();
    audio_service();
    wait_vblank();
    if (g_cd_dma.state == CD_DMA_ERROR) fail_black_loop();
}

static void pcfv_boot_stream_async(void) {
    uint16_t ready_count;

    scheduler_start_if_idle();
    while (!g_header_ready) boot_field();
    if (!parse_pcfv_header()) fail_black_loop();
    g_start_frame = pcfv_resolve_start_frame();
    pcfv_attach_audio();
    /* Prime the audio for the start frame.  ADPCM loads its ring now (the
       scheduler puts audio first); MP2 fetching begins once the RAINBOW
       read-ahead is full, so startup cannot deadlock on audio. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    audio_seek((uint16_t)(g_start_frame >> 1));
#else
    audio_seek(g_start_frame);
#endif
    setup_video_buffers();

    scheduler_start_if_idle();
    while (!audio_preroll_ready()) boot_field();
    /* Audio is not started during the hidden video prebuffer: it starts at
       the late-raster latch that makes RAINBOW visible, so it cannot lead
       the black warm-up screen. */

    /* Prebuffer several video frames before first display.  This removes the
       cadence dependency between command latency and visible frame pacing. */
    ready_count = 0;
    {
        uint16_t target = pcfv_video_prebuffer_target();
        if (target > g_frame_count) target = g_frame_count;
        while (ready_count < target) {
            audio_new_field();
            ready_count = count_ready_video_buffers();
            scheduler_poll();
            audio_service();
            wait_vblank();
            age_ready_video_buffers();
            if (g_cd_dma.state == CD_DMA_ERROR) fail_black_loop();
        }
    }

#if defined(PCFX_PCFV_HAVE_AUDIO)
    if (g_audio_active) {
        /* Hidden audio prefill after the RAINBOW ring is already full (MP2:
           decode until the start threshold).  It keeps the first visible
           frames from racing ahead while the first MP2 chunk is read and
           decoded, but it cannot starve startup video. */
        uint32_t guard = 0;
        while (pcfv_audio_boot_prefill_pending() && guard++ < 240u) {
            audio_new_field();
            scheduler_poll();
            audio_service();
            scheduler_start_if_idle();
            wait_vblank();
            age_ready_video_buffers();
            if (g_cd_dma.state == CD_DMA_ERROR) fail_black_loop();
        }
        pcfv_audio_boot_prefill_done(guard);
    }
#endif

    /* Align the playback scheduler to a clean field boundary.  Without this,
       boot/prebuffer completion can fall in the middle of vblank after the
       RAINBOW transfer-start scanline has already passed, producing a few
       top-only startup fields. */
    wait_vblank();
}

static void pcfx_pcfv_reset_state(void) {
    memset(&g_cd_dma, 0, sizeof(g_cd_dma));
    g_header_ready = 0;
#if defined(PCFX_PCFV_FORM2)
    g_sector_mode_ready = 0;
    g_audio_guard_saved = 0;
#endif
    g_rainbow_visible = 0;
    g_paused = 0;
    g_audio_chunk_count = 0;
    g_audio_active = 0;
    g_audio_clock_samples = 0;
    g_frame_count = 0;
    g_start_frame = 0;
    g_seek_in_progress = 0;
    g_next_load_frame = g_start_frame;
    g_video_refill_active = 0;
    g_next_display_frame = g_start_frame;
    g_have_display_buf = 0;
    g_current_display_buf = 0xffffu;
    g_done = 0;
    g_abort = 0;
    g_fields_per_frame = 0;
    g_field_counter = 0;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    g_paired_complete_latches = 0;
    g_paired_wait_both = 0;
    g_paired_odd_restarts = 0;
    g_paired_even_restarts = 0;
    g_paired_latches_on_even = 0;
    g_paired_even_start_deferrals = 0;
    g_paired_pair_start_odd = 0;
    g_v45_prearm_missed = 0u;
    g_v45_prearm_odd = 0u;
    g_v45_prearm_even = 0u;
    g_v45_prearm_last_raster = 0u;
    g_v45_prearm_late = 0u;
    g_v45_underflow_frame=0xffffffffu;
    g_v45_underflow_field=0xffffffffu;
    g_v45_long_gap_frame=0xffffffffu;
    g_v45_long_gap_fields=0u;
    g_v45_rainbow_enabled = 0u;

#endif
    g_vblank_latched_frames = 0;
    g_latch_raster_max = 0;
    g_latch_after_safe_window = 0;
    g_latch_raster_sum = 0;
    g_rainbow_visible_fields = 0;
    g_video_buffer_stride_words = VIDEO_BUFFER_STRIDE_DEFAULT;
    g_video_low_count = VIDEO_BUFFER_LOW_COUNT;
    g_video_buffer_count = VIDEO_BUFFER_COUNT;
    g_video_prebuffer_target = 27u;
    g_video_refill_low_water = 20u;
    /* Per-pass state: LOOP playback reopens the stream, and "nothing presented
       yet" gates prebuffer, urgent audio fetch and the A/V window.  Without
       these the second pass kept the finished MP2 decoder (never restarted),
       sat out the whole hidden-preroll guard on black, then ran video
       unclocked at CD speed with no sound. */
    g_video_frames_presented = 0;
    g_video_cadence_intervals = 0;
    g_video_cadence_exact4 = 0;
    g_video_cadence_short = 0;
    g_video_cadence_long = 0;
    g_video_cadence_max_gap = 0;
    g_video_cadence_last_field = 0xffffffffu;
    g_video_dma_early_ready = 0;
#if defined(PCFX_PCFV_HAVE_AUDIO)
    pcfv_audio_reset();
#endif
}

int pcfx_pcfv_open(uint32_t stream_lba, const PcfxPcfvOptions *opt) {
    pcfx_pcfv_reset_state();
    setup_video();
    setup_rainbow_regs();
    contrlr_pad_init(0);
    scsi_reset();

    g_stream_lba = stream_lba;
    g_stop_buttons = (opt && opt->stop_buttons) ? opt->stop_buttons : PCFX_PCFV_BTN_START;
    g_pause_buttons = opt ? opt->pause_buttons : 0u;
    g_seek_mode = opt ? opt->seek_mode : PCFX_PCFV_SEEK_NONE;
    g_seek_sector = opt ? opt->start_sector : 0u;
    /* The flexible open/update API is intentionally single-shot.  The simple
       blocking pcfx_pcfv_play() handles looping by reopening the stream so
       audio, video, and preread state restart together. */
    g_loop_playback = 0;
    g_done = 0;
    g_abort = 0;
    g_prev_pad = contrlr_pad_read(0);

    if (!g_stream_lba) return 0;
    pcfv_boot_stream_async();
    /* Audio that needs timers/IRQs (a preloaded MP2 asset) starts only after
       the RAINBOW boot: arming the PSG timer earlier left headless PC-FX
       validation on a black screen. */
    audio_after_boot();

    return 1;
}

static uint16_t pcfx_pcfv_fields_per_frame(void) {
    uint32_t rounded;
    if (!g_fps_num) return 1;
    rounded = ((60u * (uint32_t)g_fps_den) + ((uint32_t)g_fps_num / 2u)) / (uint32_t)g_fps_num;
    /* A/V sync uses a fixed field presentation schedule.  The ADPCM clock
       is sampled to *measure* drift, not to release multiple frames at once.
       Releasing frames immediately when audio crosses a sample threshold
       causes catch-up bursts after DMA and ADPCM half-buffer boundaries. */
    if (rounded < 1u) rounded = 1u;
    if (rounded > 10u) rounded = 10u;
    return (uint16_t)rounded;
}

static uint32_t pcfx_pcfv_pad_pressed(void) {
    uint32_t raw = contrlr_pad_read(0);
    uint32_t pressed = raw & ~g_prev_pad;
    g_prev_pad = raw;
    return pressed;
}

void pcfx_pcfv_set_paused(int paused) {
    uint8_t want = paused ? 1u : 0u;
    if (g_paused == want) return;
    g_paused = want;

    /* Keep the current RAINBOW buffer latched; the backend freezes its clock. */
    audio_set_paused(g_paused);
}

void pcfx_pcfv_toggle_paused(void) { pcfx_pcfv_set_paused(!g_paused); }

int pcfx_pcfv_paused(void) { return g_paused ? 1 : 0; }

static int pcfx_pcfv_handle_buttons(void) {
    uint32_t pressed = pcfx_pcfv_pad_pressed();
    if (g_pause_buttons && (pressed & g_pause_buttons)) pcfx_pcfv_set_paused(!g_paused);
    if (pressed & g_stop_buttons) return 1;
    return 0;
}

static int pcfv_audio_playback_complete(void) {
    return audio_complete();
}

static uint16_t pcfv_audio_target_frame_for_sync(void) {
    uint64_t n;
    if (!audio_clock_running() || !g_audio_rate_hz || !g_fps_den) return g_next_display_frame;
    n = (uint64_t)audio_clock() * (uint64_t)g_fps_num;
    n /= ((uint64_t)g_audio_rate_hz * (uint64_t)g_fps_den);
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    n *= 2u;
#endif
    if (n >= g_frame_count) return (g_frame_count ? (uint16_t)(g_frame_count - 1u) : 0u);
    return (uint16_t)n;
}


static int pcfv_latch_ready_display_frame(void) {
    uint16_t skipped = 0;
    int bi;

    if (g_next_display_frame >= g_frame_count) {
        if (pcfv_audio_playback_complete()) g_done = 1;
        return 0;
    }

    drop_stale_video_buffers(g_next_display_frame);
    bi = (g_next_display_frame < g_frame_count) ? find_video_buf_for_frame(g_next_display_frame) : -1;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* Never install one half of an incomplete source frame. Both compressed
       fields must have finished DMA and the readiness delay before release. */
    if (bi >= 0 && find_video_buf_for_frame((uint16_t)(g_next_display_frame + 1u)) < 0) {
        bi = -1;
        g_paired_wait_both++;
    }
#endif
    if (bi < 0 && g_next_display_frame < g_frame_count) {
        uint16_t max_skip = VIDEO_SKIP_SEARCH;
        if (audio_is_clock()) {
            uint16_t audio_target = pcfv_audio_target_frame_for_sync();
            if (!g_loop_playback) {
                if (audio_target > g_next_display_frame) {
                    uint16_t ahead = (uint16_t)(audio_target - g_next_display_frame);
                    if (ahead > max_skip) max_skip = (ahead > VIDEO_SYNC_SKIP_MAX) ? VIDEO_SYNC_SKIP_MAX : ahead;
                }
            } else if (frame_distance(g_next_display_frame, audio_target) < (g_frame_count / 2u)) {
                uint16_t ahead = frame_distance(g_next_display_frame, audio_target);
                if (ahead > max_skip) max_skip = (ahead > VIDEO_SYNC_SKIP_MAX) ? VIDEO_SYNC_SKIP_MAX : ahead;
            }
        }
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        /* Never skip one half or substitute a later record on audio drift. */
        (void)max_skip;
#else
        bi = find_best_ready_video_buf(g_next_display_frame, max_skip, &skipped);
#endif
    }

    if (bi >= 0) {
        uint16_t old_buf = g_current_display_buf;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        uint16_t old_even = g_current_even_buf;
        /* Stale-frame fallback is disabled for paired records: an unpaired
           source record must NEVER be displayed as the other field. */
        if (g_video_frame[bi] != g_next_display_frame) bi = -1;
        if (bi < 0) { g_paired_wait_both++; return 0; }
        int even_bi = find_video_buf_for_frame((uint16_t)(g_next_display_frame + 1u));
        if (even_bi < 0) { g_paired_wait_both++; return 0; }
#endif
        if (g_rainbow_visible) {
            uint32_t now = g_rainbow_visible_fields;
            if (g_video_cadence_last_field != 0xffffffffu) {
                uint32_t gap = now - g_video_cadence_last_field;
                g_video_cadence_intervals++;
                if (gap == 4u) g_video_cadence_exact4++;
                else if (gap < 4u) g_video_cadence_short++;
                else { g_video_cadence_long++;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
                    g_v45_long_gap_frame=g_next_display_frame;
                    g_v45_long_gap_fields=gap;
#endif
                }
                if (gap > g_video_cadence_max_gap) g_video_cadence_max_gap = gap;
            }
            g_video_cadence_last_field = now;
        }
        g_video_state[bi] = 3;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        g_video_state[even_bi] = 3;
        g_current_even_buf = (uint16_t)even_bi;
        g_paired_complete_latches++;
#endif
        g_current_display_buf = (uint16_t)bi;
        g_have_display_buf = 1;
        if (old_buf < g_video_buffer_count && old_buf != (uint16_t)bi) {
            g_video_state[old_buf] = 0;
            g_video_ready_delay[old_buf] = 0;
            g_video_frame[old_buf] = 0xffffu;
        }
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        if (old_even < g_video_buffer_count && old_even != g_current_even_buf &&
            old_even != g_current_display_buf) {
            g_video_state[old_even] = 0;
            g_video_ready_delay[old_even] = 0;
            g_video_frame[old_even] = 0xffffu;
        }
#endif
        g_video_frames_presented++;
        g_video_frames_latched_distinct++;
        if (audio_clock_running() && g_video_frame[bi] < pcfv_audio_target_frame_for_sync()) g_video_frames_audio_late++;
        if (skipped) g_video_frames_skipped += skipped;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        g_next_display_frame = (uint16_t)(g_video_frame[bi] + 2u);
#else
        g_next_display_frame = (uint16_t)(g_video_frame[bi] + 1u);
#endif
        if (g_next_display_frame >= g_frame_count) {
            if (g_loop_playback) g_next_display_frame = 0;
            else { g_next_display_frame = g_frame_count; if (pcfv_audio_playback_complete()) g_done = 1; }
        }
        g_seek_in_progress = 0;
        return 1;
    }

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    if (g_v45_underflow_frame == 0xffffffffu) {
        g_v45_underflow_frame=g_next_display_frame;
        g_v45_underflow_field=g_rainbow_visible_fields;
    }
#endif
    g_scsi_dma_video_underflows++;
    g_video_frames_held++;
    return 0;
}



/* V35: latch the RAINBOW address only in the following field's vertical
   blank, raster 0..21. Resetting HuC6271 near 248 touched the last visible
   scanlines (through 261), so a change of compressed KRAM address could tear
   the bottom edge. Field parity is sampled AFTER TETSU wraps at the boundary. */
static uint32_t pcfv_interlace_display_kram(void) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    if (g_current_even_buf < g_video_buffer_count &&
        dw3_pair_record_for_field((uint8_t)tetsu_get_field()) == 0u) {
        g_paired_odd_restarts++;
        return g_video_kram[g_current_display_buf];
    }
    if (g_current_even_buf < g_video_buffer_count) {
        g_paired_even_restarts++;
        return g_video_kram[g_current_even_buf];
    }
#endif
    return g_video_kram[g_current_display_buf];
}

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
/* V45 native field pipeline.  Main loop enters once per physical field, near
   raster 0.  Select the NEXT field after the outgoing decoder has consumed
   all 15 blocks, then write KR_TSA before the new field reaches raster 6.
   Hold the current pair for the full old-field decode; the old latch path
   recycled KRAM before all 240 lines had finished being fetched. */
static int v45_pcfv_update_paired(void) {
    uint16_t last_raster, r;
    uint8_t next_odd;
    uint8_t begin_picture;
    int crossed_wrap = 0;

    if (cd_dma_is_busy()) {
        if (++g_v38_dma_field_age >= ((g_cd_dma.kind == CD_REQ_AUDIO) ? 32u : 64u)) {
            g_v38_dma_watchdog_recoveries++;
            cd_dma_fail_current();
        }
    } else g_v38_dma_field_age = 0u;
    audio_new_field();
    if (!g_fields_per_frame) g_fields_per_frame = pcfx_pcfv_fields_per_frame();
    if (g_abort || g_done) return 0;
    if (pcfx_pcfv_handle_buttons()) { g_abort=1; return 0; }

    /* A boot handoff may occur at vertical raster 262, which is not yet
       raster zero. Do not treat it as a completed new-field boundary. */
    while (tetsu_raster_stable() >= RAINBOW_PREARM_END) { }
    /* No CD DMA can monopolize the last 42 scanlines before pre-arm. */
    last_raster = tetsu_raster_stable();
    while (last_raster < RAINBOW_RESTART_RASTER) {
        scheduler_poll();
        audio_service();
        scheduler_start_if_idle();
        if (g_cd_dma.state == CD_DMA_ERROR) {
            g_scsi_dma_late_frames++;
            scheduler_start_if_idle();
        }
        if (pcfx_pcfv_handle_buttons()) { g_abort=1; return 0; }
        r = tetsu_raster_stable();
        if (r < last_raster) { crossed_wrap=1; g_v38_poll_detected_wrap++; break; }
        last_raster=r;
    }
    if (!crossed_wrap) {
        r = tetsu_raster_stable();
        while (r < RAINBOW_PREARM_BEGIN) {
            uint16_t next=tetsu_raster_stable();
            if (next < r) { crossed_wrap=1; break; }
            r=next;
        }
    }
    r=tetsu_raster_stable();
    if (!crossed_wrap && r >= RAINBOW_PREARM_BEGIN && r <= RAINBOW_PREARM_END) {
        /* O/E flips only at vertical wrap. The next field is opposite to
           the currently active TETSU field. */
        next_odd=(uint8_t)(tetsu_get_field() == 0u);
        age_ready_video_buffers();
        begin_picture=(uint8_t)(!g_have_display_buf ||
            dw3_pair_due(g_field_counter, next_odd));
        if (g_field_counter == 0u && !next_odd) g_paired_even_start_deferrals++;
        if (begin_picture && !g_paused) {
            if (next_odd) g_paired_pair_start_odd++;
            if (g_rainbow_visible || !g_have_display_buf)
                (void)pcfv_latch_ready_display_frame();
        }
        if (g_have_display_buf && g_current_display_buf < g_video_buffer_count) {
            uint32_t addr;
            if (next_odd) {
                addr=g_video_kram[g_current_display_buf];
                g_paired_odd_restarts++;
                g_v45_prearm_odd++;
            } else if (g_current_even_buf < g_video_buffer_count) {
                addr=g_video_kram[g_current_even_buf];
                g_paired_even_restarts++;
                g_v45_prearm_even++;
            } else addr=g_video_kram[g_current_display_buf];
            v45_prearm_next_field(addr);
            g_v45_prearm_last_raster=r;
            if (r > RAINBOW_PREARM_BEGIN+3u) g_v45_prearm_late++;
            g_vblank_latched_frames++;
        }
        if (!g_paused) g_field_counter=dw3_pair_advance(g_field_counter,next_odd);
    } else {
        /* Never rewrite KR_TSA after its start-line deadline: a late write
           would display the wrong parity or a top-only field. */
        g_v45_prearm_missed++;
    }

    /* TETSU raster 0 is the true field boundary. Reveal the video layer only
       after this boundary, avoiding a priority change across visible lines. */
    r=tetsu_raster_stable();
    while (r >= RAINBOW_PREARM_BEGIN) r=tetsu_raster_stable();
    if (r >= 22u && r < RAINBOW_PREARM_BEGIN) {
        /* Missed the boundary and fell into another active field; preserve
           state and flag it, never fabricate a complete field presentation. */
        g_v45_prearm_missed++;
    }
    if (g_have_display_buf && g_current_display_buf < g_video_buffer_count) {
        uint8_t odd=(uint8_t)(tetsu_get_field()!=0);
        if (odd) ++dw3_tetsu_odd_fields; else ++dw3_tetsu_even_fields;
        if (dw3_prev_tetsu_field <= 1u && dw3_prev_tetsu_field != odd)
            ++dw3_tetsu_field_switches;
        dw3_prev_tetsu_field=odd;
        if (!g_rainbow_visible && g_vblank_latched_frames >= 8u && odd &&
            g_v45_rainbow_enabled) {
            set_rainbow_visible(1);
            g_rainbow_visible_fields=0;
            g_field_counter=0;
        }
        if (g_rainbow_visible && !g_seek_in_progress && !g_paused) {
            g_rainbow_visible_fields++;
            if (g_rainbow_visible_fields >= AUDIO_START_AFTER_VISIBLE_FIELDS) {
                audio_visible_field();
                if (!g_audio_start_latch_frame && audio_playing()) {
                    g_audio_start_latch_frame=g_vblank_latched_frames;
                    g_audio_start_visible_fields=g_rainbow_visible_fields;
                    g_audio_start_ring_used=audio_buffered();
                    g_audio_start_samples_emitted=audio_clock();
                }
            }
        }
    }
    scheduler_poll();
    audio_service();
    scheduler_start_if_idle();
    g_audio_clock_samples=audio_clock();
    if (g_next_display_frame >= g_frame_count && pcfv_audio_playback_complete()) g_done=1;
    return !g_done && !g_abort;
}
#endif

int pcfx_pcfv_update(void) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return v45_pcfv_update_paired();
#else
    uint16_t i;

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* Bound a stalled SCSI request by elapsed fields, not polling iterations.
       Normal audio refills are split into <=16-sector reads (~7 fields at 2x),
       so 32 fields is a generous audio recovery deadline. The video path
       can issue a 24x4-sector READ(10) (~38 fields at 2x), so its timeout
       is 64 fields. Failed audio and video cursors are retried, not skipped. */
    if (cd_dma_is_busy()) {
        if (++g_v38_dma_field_age >=
            ((g_cd_dma.kind == CD_REQ_AUDIO) ? 32u : 64u)) {
            g_v38_dma_watchdog_recoveries++;
            cd_dma_fail_current();
        }
    } else {
        g_v38_dma_field_age = 0u;
    }
#endif
    audio_new_field();
#if !defined(DW3_FORCE_240P_FMV)
    /* Sample the actual HuC6261 status bit, not a synthetic frame counter. */
    {
        const uint32_t field = (uint32_t)tetsu_get_field();
        if(field) ++dw3_tetsu_odd_fields;
        else ++dw3_tetsu_even_fields;
        if(dw3_prev_tetsu_field <= 1u && dw3_prev_tetsu_field != field)
            ++dw3_tetsu_field_switches;
        dw3_prev_tetsu_field = field;
    }
#endif

    if (!g_fields_per_frame) g_fields_per_frame = pcfx_pcfv_fields_per_frame();
    if (g_abort || g_done) return 0;
    if (pcfx_pcfv_handle_buttons()) { g_abort = 1; return 0; }

    if (g_paused) {
        while (tetsu_raster_stable() < RAINBOW_RESTART_RASTER) {
            if (g_seek_in_progress) {
                scheduler_poll();
                audio_service();
                scheduler_start_if_idle();
            } else {
                cd_dma_poll_budget(SCHEDULER_POLL_BUDGET);
            }
            if (pcfx_pcfv_handle_buttons()) { g_abort = 1; return 0; }
        }
        if (g_seek_in_progress) {
            age_ready_video_buffers();
            (void)pcfv_latch_ready_display_frame();
        }
        while (tetsu_raster_stable() >= RAINBOW_RESTART_RASTER) { }
        /* Only rearm after the previous visible field has ended. */
        if (g_have_display_buf && g_current_display_buf < g_video_buffer_count &&
            tetsu_raster_stable() < 22u) {
            start_rainbow_frame(pcfv_interlace_display_kram(), 15);
        }
        g_audio_clock_samples = audio_clock();
        return 1;
    }

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* Polling may run across the raster wrap during a long SCSI phase.
       Detect a genuine TETSU raster decrease and DO NOT wait a second whole
       field: that mistake desynchronizes the four-field picture cadence.
       Actual field parity is still sampled only AFTER the new field starts. */
    {
        uint16_t last_raster = tetsu_raster_stable();
        uint8_t poll_crossed_wrap = 0u;
        for (;;) {
            uint16_t current_raster = tetsu_raster_stable();
            if (current_raster < last_raster) {
                poll_crossed_wrap = 1u;
                g_v38_poll_detected_wrap++;
                break;
            }
            if (current_raster >= RAINBOW_RESTART_RASTER) break;
            last_raster = current_raster;
            scheduler_poll();
            audio_service();
            scheduler_start_if_idle();
            if (g_cd_dma.state == CD_DMA_ERROR) {
                g_scsi_dma_late_frames++;
                scheduler_start_if_idle();
            }
            if (pcfx_pcfv_handle_buttons()) { g_abort = 1; return 0; }
        }
        if (!poll_crossed_wrap) {
            while (tetsu_raster_stable() >= RAINBOW_RESTART_RASTER) { }
        }
    }
#else
    while (tetsu_raster_stable() < RAINBOW_RESTART_RASTER) {
        scheduler_poll();
        audio_service();
        scheduler_start_if_idle();
        if (g_cd_dma.state == CD_DMA_ERROR) {
            g_scsi_dma_late_frames++;
            scheduler_start_if_idle();
        }
        if (pcfx_pcfv_handle_buttons()) { g_abort = 1; return 0; }
    }
    while (tetsu_raster_stable() >= RAINBOW_RESTART_RASTER) { }
#endif

    age_ready_video_buffers();

    /* II can pause inside the poll loop above: then neither advance video nor
       (re)start audio for the rest of this field. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    /* Only start a new source picture at the real C6261 odd-field boundary.
       Do not change pictures halfway through an odd/even woven frame. */
    const uint8_t current_field_odd = (uint8_t)(tetsu_get_field() != 0);
    /* The first pair may be loaded while still invisible on either parity.
       Only visible picture changes are odd-boundary operations. */
    const uint8_t begin_source_picture = (uint8_t)(!g_have_display_buf ||
        dw3_pair_due(g_field_counter, current_field_odd));
    if (g_field_counter == 0u && !current_field_odd) ++g_paired_even_start_deferrals;
#else
    const uint8_t begin_source_picture = (uint8_t)(g_field_counter == 0u);
#endif
    if (begin_source_picture && !g_paused) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
        if (g_have_display_buf && !current_field_odd) ++g_paired_latches_on_even;
        if (current_field_odd) ++g_paired_pair_start_odd;
#endif
        /* During hidden HuC6271 warm-up, keep restarting the first decoded
           RAINBOW frame instead of consuming the timeline.  Distinct visible
           frames advance only on the four-field source cadence.  Do not let
           an irregular ADPCM sample-position estimate burst frames faster. */
        if (g_rainbow_visible || !g_have_display_buf) {
            (void)pcfv_latch_ready_display_frame();
        }
    }

    if (g_have_display_buf && g_current_display_buf < g_video_buffer_count) {
        {
            const uint16_t raster = tetsu_raster_stable();
            if (raster > g_latch_raster_max) g_latch_raster_max = raster;
            g_latch_raster_sum += raster;
            if (raster > RAINBOW_SAFE_LATCH_END) g_latch_after_safe_window++;
        }
        start_rainbow_frame(pcfv_interlace_display_kram(), 15);
        g_vblank_latched_frames++;
        if (!vblank_active()) g_midfield_latched_frames++;
        if (!g_rainbow_visible && g_vblank_latched_frames >= 8u
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
            && current_field_odd
#endif
        ) {
            /* Reveal the first field on an odd scan; never splice the Bandai
               logo and the RAINBOW source at the even half of a field pair. */
            set_rainbow_visible(1);
            g_rainbow_visible_fields = 0;
            g_field_counter = 0;
        }
        /* While a seek is prebuffering, the old frame is still on screen:
           audio waits for the target frame's first visible field. */
        if (g_rainbow_visible && !g_seek_in_progress && !g_paused) {
            g_rainbow_visible_fields++;
            if (g_rainbow_visible_fields >= AUDIO_START_AFTER_VISIBLE_FIELDS) {
                audio_visible_field();
                if (!g_audio_start_latch_frame && audio_playing()) {
                    g_audio_start_latch_frame = g_vblank_latched_frames;
                    g_audio_start_visible_fields = g_rainbow_visible_fields;
                    g_audio_start_ring_used = audio_buffered();
                    g_audio_start_samples_emitted = audio_clock();
                }
            }
        }
    }

    scheduler_poll();
    audio_service();
    scheduler_start_if_idle();

    /* The RAINBOW restart above ran in raster 0..21; do not spin through
       the entire active picture here, which would delay the next field. */

#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    g_field_counter = dw3_pair_advance(g_field_counter, current_field_odd);
#else
    g_field_counter++;
    if (g_field_counter >= g_fields_per_frame) g_field_counter = 0;
#endif
    g_audio_clock_samples = audio_clock();
    if (g_next_display_frame >= g_frame_count && pcfv_audio_playback_complete()) g_done = 1;
    return !g_done && !g_abort;
#endif
}


uint16_t pcfx_pcfv_frame_count(void) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return (uint16_t)(g_frame_count >> 1);
#else
    return g_frame_count;
#endif
}

uint16_t pcfx_pcfv_current_frame(void) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    if (g_have_display_buf && g_current_display_buf < g_video_buffer_count) return (uint16_t)(g_video_frame[g_current_display_buf] >> 1);
    return (uint16_t)(g_start_frame >> 1);
#else
    if (g_have_display_buf && g_current_display_buf < g_video_buffer_count) return g_video_frame[g_current_display_buf];
    return g_start_frame;
#endif
}

uint32_t pcfx_pcfv_stream_sector_for_frame(uint16_t frame_index) {
    if (!g_frame_count) return 0;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    frame_index = (uint16_t)(frame_index * 2u);
#endif
    if (frame_index >= g_frame_count) frame_index = (uint16_t)(g_frame_count - 1u);
    return g_entries[frame_index].video_sector;
}

uint32_t pcfx_pcfv_disc_lba_for_frame(uint16_t frame_index) {
    return g_stream_lba + pcfx_pcfv_stream_sector_for_frame(frame_index);
}

uint32_t pcfx_pcfv_data_sector_for_frame(uint16_t frame_index) {
    uint32_t s = pcfx_pcfv_stream_sector_for_frame(frame_index);
    return (s > g_data_start_sector) ? (s - g_data_start_sector) : 0u;
}

uint16_t pcfx_pcfv_frame_for_stream_sector(uint32_t stream_relative_sector) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return (uint16_t)(pcfv_find_frame_for_asset_sector(stream_relative_sector) >> 1);
#else
    return pcfv_find_frame_for_asset_sector(stream_relative_sector);
#endif
}

uint16_t pcfx_pcfv_frame_for_disc_lba(uint32_t absolute_disc_lba) {
    uint32_t rel = (absolute_disc_lba > g_stream_lba) ? (absolute_disc_lba - g_stream_lba) : 0u;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return (uint16_t)(pcfv_find_frame_for_asset_sector(rel) >> 1);
#else
    return pcfv_find_frame_for_asset_sector(rel);
#endif
}

uint16_t pcfx_pcfv_frame_for_data_sector(uint32_t data_relative_sector) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return (uint16_t)(pcfv_find_frame_for_asset_sector(g_data_start_sector + data_relative_sector) >> 1);
#else
    return pcfv_find_frame_for_asset_sector(g_data_start_sector + data_relative_sector);
#endif
}

int pcfx_pcfv_seek_frame(uint16_t frame_index) {
    uint8_t was_paused;

    if (!g_header_ready || !g_frame_count) return 0;
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    if (frame_index >= (g_frame_count >> 1)) return 0;
    frame_index = (uint16_t)(frame_index * 2u);
#else
    if (frame_index >= g_frame_count) return 0;
#endif

    was_paused = g_paused;
    cd_dma_cancel_for_seek();

    /* Stop audio immediately.  The backend re-primes from the audio for the
       target frame and restarts only on the target frame's first visible
       field, so a seek never plays new audio over the old picture. */
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    audio_seek((uint16_t)(frame_index >> 1));
#else
    audio_seek(frame_index);
#endif

    g_start_frame = frame_index;

    setup_video_buffers_for_seek(frame_index, 1);
    g_seek_in_progress = 1;
    g_done = 0;
    g_abort = 0;
    g_rainbow_visible_fields = 0;

    g_paused = was_paused;
    scheduler_start_if_idle();
    return 1;
}

int pcfx_pcfv_seek_stream_sector(uint32_t stream_relative_sector) {
#if defined(DW3_EXPERIMENTAL_PAIRED_480I)
    return pcfx_pcfv_seek_frame((uint16_t)(pcfv_find_frame_for_asset_sector(stream_relative_sector) >> 1));
#else
    return pcfx_pcfv_seek_frame(pcfv_find_frame_for_asset_sector(stream_relative_sector));
#endif
}

int pcfx_pcfv_seek_disc_lba(uint32_t absolute_disc_lba) {
    uint32_t rel = (absolute_disc_lba > g_stream_lba) ? (absolute_disc_lba - g_stream_lba) : 0u;
    return pcfx_pcfv_seek_stream_sector(rel);
}

int pcfx_pcfv_seek_data_sector(uint32_t data_relative_sector) {
    return pcfx_pcfv_seek_stream_sector(g_data_start_sector + data_relative_sector);
}

void pcfx_pcfv_stop(void) {
    king_write_reg16(0x40, 0x0000);
    audio_stop();
    g_paused = 0;
    set_rainbow_visible(0);
    /* The nonblocking video prefetch may still own an in-flight READ(10).
       Terminate its SCSI DMA BEFORE handing CD ownership to libpcfx's
       synchronous title-asset reader. Otherwise eris_cd_read_dma() can wait
       indefinitely for the command phase on a bus held by the FMV player. */
    scsi_stop_king_dma_no_eat();
    scsi_bus_abort_reset();
    g_cd_dma.state = CD_DMA_IDLE;
}

int pcfx_pcfv_aborted(void) { return g_abort ? 1 : 0; }

int pcfx_pcfv_play(uint32_t stream_lba, const PcfxPcfvOptions *opt) {
    PcfxPcfvOptions one;
    uint8_t loop;

    one.stop_buttons = (opt && opt->stop_buttons) ? opt->stop_buttons : PCFX_PCFV_BTN_START;
    one.pause_buttons = opt ? opt->pause_buttons : 0u;
    one.seek_mode = opt ? opt->seek_mode : PCFX_PCFV_SEEK_NONE;
    one.start_sector = opt ? opt->start_sector : 0u;
    loop = (opt && opt->loop) ? 1u : 0u;

    for (;;) {
        /* Always open the runtime in single-shot mode.  For looping playback we
           restart from the top after a natural end; this also restarts ADPCM
           cleanly instead of letting the video wrap over stale audio state. */
        one.loop = PCFX_PCFV_PLAY_ONCE;
        if (!pcfx_pcfv_open(stream_lba, &one)) return 0;
        while (pcfx_pcfv_update()) { }
        pcfx_pcfv_stop();
        if (pcfx_pcfv_aborted()) return 0;
        if (!loop) return 1;
    }
}

static void pcfv_default_options(PcfxPcfvOptions *opt, uint8_t loop, uint16_t stop_buttons, uint16_t pause_buttons) {
    opt->stop_buttons = stop_buttons ? stop_buttons : PCFX_PCFV_BTN_START;
    opt->pause_buttons = pause_buttons;
    opt->loop = loop;
    opt->seek_mode = PCFX_PCFV_SEEK_NONE;
    opt->start_sector = 0;
}

int pcfx_pcfv_play_once(uint32_t stream_lba, uint16_t stop_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, 0);
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping(uint32_t stream_lba, uint16_t stop_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, 0);
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_once_paused(uint32_t stream_lba, uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, pause_buttons);
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping_paused(uint32_t stream_lba, uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, pause_buttons);
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_once_from_sector(uint32_t stream_lba, uint32_t stream_relative_sector,
                                    uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_STREAM_SECTOR;
    opt.start_sector = stream_relative_sector;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping_from_sector(uint32_t stream_lba, uint32_t stream_relative_sector,
                                       uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_STREAM_SECTOR;
    opt.start_sector = stream_relative_sector;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_once_from_disc_lba(uint32_t stream_lba, uint32_t absolute_disc_lba,
                                      uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_DISC_LBA;
    opt.start_sector = absolute_disc_lba;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping_from_disc_lba(uint32_t stream_lba, uint32_t absolute_disc_lba,
                                         uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_DISC_LBA;
    opt.start_sector = absolute_disc_lba;
    return pcfx_pcfv_play(stream_lba, &opt);
}



int pcfx_pcfv_play_once_from_data_sector(uint32_t stream_lba, uint32_t data_relative_sector,
                                         uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_DATA_SECTOR;
    opt.start_sector = data_relative_sector;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping_from_data_sector(uint32_t stream_lba, uint32_t data_relative_sector,
                                            uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_DATA_SECTOR;
    opt.start_sector = data_relative_sector;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_once_from_frame(uint32_t stream_lba, uint16_t frame_index,
                                   uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_ONCE, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_FRAME;
    opt.start_sector = frame_index;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_looping_from_frame(uint32_t stream_lba, uint16_t frame_index,
                                      uint16_t stop_buttons, uint16_t pause_buttons) {
    PcfxPcfvOptions opt;
    pcfv_default_options(&opt, PCFX_PCFV_PLAY_LOOP, stop_buttons, pause_buttons);
    opt.seek_mode = PCFX_PCFV_SEEK_FRAME;
    opt.start_sector = frame_index;
    return pcfx_pcfv_play(stream_lba, &opt);
}

int pcfx_pcfv_play_loop(uint32_t stream_lba, uint16_t stop_buttons) {
    return pcfx_pcfv_play_looping(stream_lba, stop_buttons);
}


/* MP2 builds implement these in pcfv_mp2_stream.c. */
#if !defined(PCFX_PCFV_AUDIO_MP2)
int pcfx_pcfv_mp2_load_from_cd(uint32_t mp2_lba, uint32_t mp2_size_bytes) { (void)mp2_lba; (void)mp2_size_bytes; return 0; }
uint32_t pcfx_pcfv_mp2_frames_decoded(void) { return 0; }
uint32_t pcfx_pcfv_mp2_underflows(void) { return 0; }
uint32_t pcfx_pcfv_mp2_ring_used(void) { return 0; }
uint32_t pcfx_pcfv_mp2_error_code(void) { return 0; }
uint32_t pcfx_pcfv_mp2_error_offset(void) { return 0; }
#endif
