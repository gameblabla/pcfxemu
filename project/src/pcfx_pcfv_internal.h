#ifndef PCFX_PCFV_INTERNAL_H
#define PCFX_PCFV_INTERNAL_H

/* Private contract between the PCFV player core (pcfx_pcfv_player.c) and the
   streamed-audio backend compiled into the build.  Exactly one of these is
   defined (the Makefile's AUDIO= picks it):

     PCFX_PCFV_AUDIO_MP2    pcfv_mp2_stream.c   MP2 decoded on the V810 -> PSG
     PCFX_PCFV_AUDIO_ADPCM  pcfv_adpcm_stream.c KING ADPCM ring in KRAM
     (neither)              silent build; video paced by the field counter

   Both backends share the player's single non-blocking CD -> KRAM DMA
   channel, so no blocking CD read ever runs during playback. */

#include <stdint.h>

/* Older Makefiles passed -DPCFX_PCFV_USE_MP2=1. */
#if defined(PCFX_PCFV_USE_MP2) && PCFX_PCFV_USE_MP2 && !defined(PCFX_PCFV_AUDIO_MP2)
#define PCFX_PCFV_AUDIO_MP2 1
#endif
#if defined(PCFX_PCFV_AUDIO_MP2) && defined(PCFX_PCFV_AUDIO_ADPCM)
#error "build one audio backend: PCFX_PCFV_AUDIO_MP2 or PCFX_PCFV_AUDIO_ADPCM"
#endif
#if defined(PCFX_PCFV_AUDIO_MP2) || defined(PCFX_PCFV_AUDIO_ADPCM)
#define PCFX_PCFV_HAVE_AUDIO 1
#endif

#if defined(PCFX_PCFV_FORM2)
#define PCFV_SECTOR_SIZE       2324u
#else
#define PCFV_SECTOR_SIZE       2048u
#endif

/* PCFV header flags (bit0 is set by the ADPCM encoders, bit1 by MP2 muxers). */
#define PCFV_FLAG_ADPCM_AUDIO  0x0001u
#define PCFV_FLAG_MP2_AUDIO    0x0002u
#define PCFV_FLAG_FORM2        0x0008u /* raw CD Mode 2 Form 2 payload sectors */
#define PCFV_FLAG_PAIRED_480I  0x0004u /* odd/even compressed records per 15fps picture */

#define PCFV_AUDIO_CODEC_NONE  0u
#define PCFV_AUDIO_CODEC_ADPCM 1u
#define PCFV_AUDIO_CODEC_MP2   2u

/* One interleaved audio run on disc, from a PCFV frame entry. */
typedef struct {
    uint32_t sector;       /* relative to the PCFV file */
    uint32_t sectors;
    uint32_t byte_offset;  /* offset of its first byte in the audio stream */
    uint16_t frame_index;  /* the video frame it follows */
} PcfvAudioChunk;

typedef struct {
    uint32_t lba;                    /* absolute disc LBA of the PCFV file */
    uint32_t data_start_sector;
    uint32_t audio_bytes;
    uint32_t audio_sectors;
    uint32_t audio_rate_hz;
    uint32_t audio_preroll_sectors;  /* ADPCM: audio stored before frame 0 */
    uint32_t audio_refill_sectors;
    uint32_t ring_half_bytes;        /* ADPCM ring half the stream was cut for */
    uint16_t flags;
    uint16_t fps_num;
    uint16_t fps_den;
    uint16_t frame_count;
    uint16_t audio_chunk_count;
    uint8_t  audio_codec;            /* PCFV_AUDIO_CODEC_* from the header */
    const PcfvAudioChunk *audio_chunks;
} PcfvStreamInfo;

/* ---- Player services used by the backend -------------------------------- */

int      pcfv_cd_busy(void);
/* Queue one READ(10) of `sectors` into KRAM through KING SCSI DMA.  Completion
   calls pcfv_audio_cd_done(), failure pcfv_audio_cd_failed(). */
int      pcfv_cd_read_audio(uint32_t lba, uint32_t kram_word_addr, uint32_t sectors);
/* KRAM -> RAM copy with libpcfx's read cursor (king_set_kram_read). */
void     pcfv_kram_read(uint32_t word_addr, uint8_t *dst, uint32_t bytes);
uint16_t pcfv_video_ready_count(void);
uint16_t pcfv_video_prebuffer_target(void);
uint32_t pcfv_video_frames_presented(void);

/* ---- Backend contract (implemented by the selected backend) -------------- */

#if defined(PCFX_PCFV_HAVE_AUDIO)
/* Accept the stream's audio.  0 means this backend cannot play it; the player
   then plays the video silently, paced by the field counter. */
int      pcfv_audio_attach(const PcfvStreamInfo *s);
void     pcfv_audio_reset(void);                 /* per-open state */
/* Stop playback and prime from the audio that belongs to video frame `frame`. */
void     pcfv_audio_seek(uint16_t frame);
int      pcfv_audio_preroll_ready(void);         /* boot/seek waits for this */
int      pcfv_audio_fetch_urgent(void);          /* before video; 1 = CD busy now */
int      pcfv_audio_fetch_background(void);      /* after video had its turn */
void     pcfv_audio_cd_done(void);
void     pcfv_audio_cd_failed(void);
void     pcfv_audio_new_field(void);             /* once per displayed field */
void     pcfv_audio_service(void);               /* cheap; call from poll loops */
int      pcfv_audio_boot_prefill_pending(void);  /* hidden fill after prebuffer */
void     pcfv_audio_boot_prefill_done(uint32_t fields);
void     pcfv_audio_after_boot(void);
void     pcfv_audio_visible_field(void);         /* start/restart when ready */
int      pcfv_audio_playing(void);
int      pcfv_audio_started(void);
int      pcfv_audio_is_clock(void);              /* video follows pcfv_audio_clock */
uint32_t pcfv_audio_clock(void);                 /* samples played from stream start */
uint32_t pcfv_audio_buffered(void);              /* samples ready ahead of the clock */
int      pcfv_audio_complete(void);
void     pcfv_audio_set_paused(int paused);
void     pcfv_audio_stop(void);
#endif

#endif
