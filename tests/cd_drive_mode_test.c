/* PC-FX CD drive model regression test.
 *
 * Drives mednafen/cdrom/scsicd.c over the SCSI bus like the BIOS does and
 * checks the behaviour documented in docs/cd-drive-model.md:
 *   - CUE layout: INDEX 00/01, PREGAP, single-BIN and split-BIN LBAs
 *   - DISC ID (TOC point A0 PSEC) and the MODE SELECT defaults it selects
 *   - MODE SELECT PF=0 (10-byte vendor-unique list) and PF=1 (SCSI-2)
 *   - refusal of lists/lengths the real drive does not accept
 *   - error judgement by sector header (Mode 1 EDC, Mode 2 none)
 *   - audio / pregap / no-sync sectors cannot be read as data
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../mednafen/cdrom/scsicd.h"
#include "../mednafen/cdrom/cdromif.h"
#include "../mednafen/cdrom/CDUtility.h"

static void irq_cb(int v) { (void)v; }
static void ssc_cb(uint8_t a, int b) { (void)a; (void)b; }

static uint32_t ts;

static void run(unsigned n) { ts += n; SCSICD_Run(ts); }

#define PH_MASK (SCSICD_MSG_mask | SCSICD_CD_mask | SCSICD_IO_mask)

/* One complete SCSI command.  Returns the SCSI status byte. */
static int scsi(const uint8_t *cdb, unsigned cdblen, const uint8_t *out, unsigned outlen,
                uint8_t *in, unsigned inmax, unsigned *inlen)
{
	unsigned ci = 0, oi = 0, ii = 0;
	int status = -1;

	if(inlen) *inlen = 0;
	SCSICD_SetSEL(true);
	run(8);
	SCSICD_SetSEL(false);

	for(unsigned guard = 0; guard < 4000000; guard++)
	{
		run(400);
		if(!SCSICD_GetBSY() && status >= 0)
			break;
		if(!SCSICD_GetREQ())
			continue;

		const uint32_t ph = cd_bus.signals & PH_MASK;
		if(ph == SCSICD_CD_mask)		/* COMMAND */
		{
			SCSICD_SetDB(ci < cdblen ? cdb[ci] : 0);
			ci++;
		}
		else if(ph == 0)			/* DATA OUT */
		{
			SCSICD_SetDB(oi < outlen ? out[oi] : 0);
			oi++;
		}
		else if(ph == SCSICD_IO_mask)		/* DATA IN */
		{
			uint8_t b = SCSICD_GetDB();
			if(in && ii < inmax) in[ii] = b;
			ii++;
		}
		else if(ph == (SCSICD_IO_mask | SCSICD_CD_mask))	/* STATUS */
			status = SCSICD_GetDB();
		/* MESSAGE IN: just acknowledge. */

		SCSICD_SetACK(true);
		run(8);
		SCSICD_SetACK(false);
		run(8);
	}
	if(inlen) *inlen = ii < inmax ? ii : inmax;
	return status;
}

static uint8_t sense_key(void)
{
	uint8_t cdb[6] = { 0x03, 0, 0, 0, 18, 0 }, buf[32];
	unsigned n;
	memset(buf, 0, sizeof(buf));
	scsi(cdb, 6, NULL, 0, buf, sizeof(buf), &n);
	return buf[2] & 0x0F;
}

static int do_read(unsigned lba, unsigned count, uint8_t *buf, unsigned max, unsigned *got)
{
	uint8_t cdb[10] = { 0x28, 0, lba >> 24, lba >> 16, lba >> 8, lba, 0, count >> 8, count, 0 };
	return scsi(cdb, 10, NULL, 0, buf, max, got);
}

static unsigned read_capacity_bl(void)
{
	uint8_t cdb[10] = { 0x25 }, buf[8];
	unsigned n;
	assert(scsi(cdb, 10, NULL, 0, buf, 8, &n) == 0 && n == 8);
	return (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];
}

static int mode_select(int pf, const uint8_t *list, unsigned len)
{
	uint8_t cdb[6] = { 0x15, pf ? 0x10 : 0x00, 0, 0, (uint8_t)len, 0 };
	return scsi(cdb, 6, list, len, NULL, 0, NULL);
}

static int vendor_select(uint8_t b4, uint8_t retry)
{
	uint8_t l[10] = { 0, 0, 0, 0, b4, 0, 0, 0, 0, retry };
	return mode_select(0, l, 10);
}

static int scsi2_select(uint8_t density, unsigned blen)
{
	uint8_t l[12] = { 0, 0, 0, 8, density, 0, 0, 0, 0, blen >> 16, blen >> 8, blen };
	return mode_select(1, l, 12);
}

static unsigned disc_id(void)
{
	uint8_t cdb[10] = { 0xDE, 0x03, 0xA0 }, buf[64];
	unsigned n;
	cdb[8] = 64;
	assert(scsi(cdb, 10, NULL, 0, buf, sizeof(buf), &n) == 0);
	return buf[2 + 8];	/* first descriptor: PSEC */
}

static unsigned track_lba(int track)
{
	uint8_t cdb[10] = { 0xDE, 0x02, 0 }, buf[4];
	unsigned n;
	cdb[2] = (track / 10) << 4 | (track % 10);
	assert(scsi(cdb, 10, NULL, 0, buf, 4, &n) == 0);
	return (((buf[0] >> 4) * 10 + (buf[0] & 15)) * 60 + ((buf[1] >> 4) * 10 + (buf[1] & 15))) * 75 +
	       ((buf[2] >> 4) * 10 + (buf[2] & 15)) - 150;
}

/* ---- synthetic discs ---- */
static char dir[256];

static void fname(char *out, const char *leaf) { snprintf(out, 512, "%s/%s", dir, leaf); }

static void put_sector(FILE *f, int mode, unsigned lba, unsigned fill)
{
	uint8_t s[2352];
	memset(s, 0, sizeof(s));
	if(mode == 0) { memset(s, 0x11, sizeof(s)); }		/* audio */
	else if(mode == 9) { }				/* zero pregap silence, no sync */
	else
	{
		for(int i = 0; i < 2048; i++) s[16 + i] = (uint8_t)(fill + i);
		if(mode == 1) encode_mode1_sector(lba + 150, s);
		else if(mode == 2)				/* XA Form 1 */
		{
			memset(s + 16, 0, 8);			/* subheader: file/channel/submode 08 (data)/coding */
			s[18] = s[22] = 0x08;
			for(int i = 0; i < 2048; i++) s[24 + i] = (uint8_t)(fill + i);
			encode_mode2_form1_sector(lba + 150, s);
		}
		else if(mode == 3)				/* formless Mode 2: raw 2336 user bytes */
		{
			for(int i = 0; i < 2336; i++) s[16 + i] = (uint8_t)(0xA0 + i);
			memcpy(s + 16, "PCFV0001", 8);
			encode_mode2_sector(lba + 150, s);
		}
		else if(mode == 4)				/* Mode 1 with a bad EDC */
		{
			encode_mode1_sector(lba + 150, s);
			s[2064] ^= 0xFF;
		}
	}
	fwrite(s, 1, 2352, f);
}

/* Layout that mimics the RAINBOW disc:
 *   LBA    0..374  audio                          (Track 1)
 *   LBA  375..449  75 sectors of silence, no sync (Track 2 INDEX 00)
 *   LBA  450..599  150 Mode 1 pregap sectors
 *   LBA  600..639  Mode 1 data                    (Track 2 INDEX 01)
 *   LBA  640..679  Mode 2 Form 1 (XA) data        (Track 3, own file)
 *   LBA  680..719  formless Mode 2 data           (Track 4, own file)
 *   LBA  720       Mode 1 with a bad EDC          (Track 4 tail)
 */
static void make_split_disc(const char *cue_name, const char *rem)
{
	char p[512];
	FILE *f;
	fname(p, "t1.bin"); f = fopen(p, "wb"); for(int i = 0; i < 375; i++) put_sector(f, 0, i, 0); fclose(f);
	fname(p, "t2.bin"); f = fopen(p, "wb");
	for(int i = 0; i < 75; i++) put_sector(f, 9, 375 + i, 0);
	for(int i = 0; i < 150; i++) put_sector(f, 1, 450 + i, 0x00);
	for(int i = 0; i < 40; i++) put_sector(f, 1, 600 + i, 0x40 + i);
	fclose(f);
	fname(p, "t3.bin"); f = fopen(p, "wb"); for(int i = 0; i < 40; i++) put_sector(f, 2, 640 + i, 0x80); fclose(f);
	fname(p, "t4.bin"); f = fopen(p, "wb");
	for(int i = 0; i < 40; i++) put_sector(f, 3, 680 + i, 0);
	put_sector(f, 4, 720, 0x30);
	fclose(f);
	fname(p, cue_name); f = fopen(p, "w");
	fprintf(f, "%s", rem);
	fprintf(f, "FILE \"t1.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
	fprintf(f, "FILE \"t2.bin\" BINARY\n  TRACK 02 MODE1/2352\n    INDEX 00 00:00:00\n    INDEX 01 00:03:00\n");
	fprintf(f, "FILE \"t3.bin\" BINARY\n  TRACK 03 MODE2/2352\n    INDEX 01 00:00:00\n");
	fprintf(f, "FILE \"t4.bin\" BINARY\n  TRACK 04 MODE2/2352\n    INDEX 01 00:00:00\n");
	fclose(f);
}

/* The same disc as ONE bin, INDEX times absolute within it. */
static void make_single_disc(const char *cue_name)
{
	char p[512];
	FILE *f;
	fname(p, "one.bin"); f = fopen(p, "wb");
	for(int i = 0; i < 375; i++) put_sector(f, 0, i, 0);
	for(int i = 0; i < 75; i++) put_sector(f, 9, 375 + i, 0);
	for(int i = 0; i < 150; i++) put_sector(f, 1, 450 + i, 0x00);
	for(int i = 0; i < 40; i++) put_sector(f, 1, 600 + i, 0x40 + i);
	for(int i = 0; i < 40; i++) put_sector(f, 2, 640 + i, 0x80);
	fclose(f);
	fname(p, cue_name); f = fopen(p, "w");
	fprintf(f, "FILE \"one.bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n"
	           "  TRACK 02 MODE1/2352\n    INDEX 00 00:05:00\n    INDEX 01 00:08:00\n"
	           "  TRACK 03 MODE2/2352\n    INDEX 01 00:08:40\n");
	fclose(f);
}

static void load(const char *cue)
{
	char p[512];
	fname(p, cue);
	CDIF *c = CDIF_Open_C(p, false);
	assert(c);
	SCSICD_SetDisc(true, NULL, true);
	SCSICD_SetDisc(false, c, false);
	SCSICD_Power(ts);
	/* clear UNIT ATTENTION from the disc change */
	uint8_t tur[6] = { 0 };
	scsi(tur, 6, NULL, 0, NULL, 0, NULL);
	assert(scsi(tur, 6, NULL, 0, NULL, 0, NULL) == 0);
}

static void reset_bus(void)
{
	SCSICD_SetRST(true); run(64); SCSICD_SetRST(false); run(64);
}

int main(void)
{
	snprintf(dir, sizeof(dir), "/tmp/cd_drive_mode_test.%d", (int)getpid());
	mkdir(dir, 0700);
	unsetenv("PCFXEMU_CD_LEGACY");
	unsetenv("PCFXEMU_CD_DISC_ID");

	SCSICD_Init(SCSICD_PCFX, 3, NULL, 153600, 21477273, irq_cb, ssc_cb);

	uint8_t buf[4096];
	unsigned n;

	/* ---------- 1. layout: split BINs, INDEX 00/01 ---------- */
	make_split_disc("split.cue", "");
	load("split.cue");
	assert(track_lba(1) == 0);
	assert(track_lba(2) == 600);	/* INDEX 01, NOT the start of the pregap file (375) */
	assert(track_lba(3) == 640);
	assert(track_lba(4) == 680);
	puts("ok  split CUE: track 2 starts at INDEX 01 (LBA 600), not at its pregap (375)");

	/* ---------- 2. layout: single BIN with absolute INDEX times ---------- */
	make_single_disc("single.cue");
	load("single.cue");
	assert(track_lba(2) == 600 && track_lba(3) == 640);
	assert(do_read(600, 1, buf, sizeof(buf), &n) == 0 && n == 2048 && buf[0] == 0x40);
	assert(do_read(601, 1, buf, sizeof(buf), &n) == 0 && n == 2048 && buf[0] == 0x41);
	puts("ok  single-BIN CUE: tracks share one file and land on 0/600/640");

	/* ---------- 3. defaults and READ in default mode ---------- */
	load("split.cue");
	assert(disc_id() == 0x00);
	assert(read_capacity_bl() == 2048);
	assert(do_read(600, 1, buf, sizeof(buf), &n) == 0 && n == 2048);
	assert(buf[0] == 0x40 && buf[1] == 0x41);	/* Mode 1 user data starts at byte 16 */
	puts("ok  default mode (disc id 00): 2048-byte Mode 1 blocks");

	/* ---------- 4. things that cannot be read as data ---------- */
	assert(do_read(100, 1, buf, sizeof(buf), &n) != 0);		/* audio track */
	assert(sense_key() == 3);
	assert(do_read(400, 1, buf, sizeof(buf), &n) != 0);		/* pregap silence is in track 1's range */
	puts("ok  audio / pregap-silence sectors refuse to read as data");

	/* ---------- 5. MODE SELECT validation ---------- */
	{
		/* The RAINBOW player's original list: PF=0, 12 bytes, BDL=8, "2336". */
		uint8_t old_player[12] = { 0, 0, 0, 8, 0, 0, 0, 0, 0, 0x00, 0x09, 0x20 };
		assert(mode_select(0, old_player, 12) != 0);
		assert(sense_key() == 5);
		assert(read_capacity_bl() == 2048);	/* drive stayed in 2048-byte mode */
		puts("ok  PF=0 + 12-byte list (old RAINBOW player) -> CHECK CONDITION, still 2048-byte blocks");
	}
	assert(vendor_select(0x06, 3) == 0);			/* XA=0 SH=0 EC=1 EJ=2 (0x06) */
	assert(read_capacity_bl() == 2336);
	puts("ok  PF=0 vendor-unique list, EJ=10 -> 2336-byte blocks");
	assert(vendor_select(0x00, 0) == 0 && read_capacity_bl() == 2048);
	assert(vendor_select(0x03, 0) == 0 && read_capacity_bl() == 2340);
	assert(vendor_select(0x00, 0) == 0);
	{
		uint8_t bad[10] = { 0, 0, 0, 0, 0x08, 0, 0, 0, 0, 0 };	/* reserved bit 3 set */
		assert(mode_select(0, bad, 10) != 0);
		uint8_t bad_retry[10] = { 0, 0, 0, 0, 0x00, 0, 0, 0, 0, 0x1F };
		assert(mode_select(0, bad_retry, 10) != 0);
	}
	assert(scsi2_select(0x00, 2352) != 0);		/* 2352 is not offered by the drive */
	assert(scsi2_select(0x00, 2324) != 0);		/* 2324 needs the XA density */
	assert(scsi2_select(0x42, 2048) != 0);		/* unknown density */
	assert(read_capacity_bl() == 2048);
	assert(scsi2_select(0x81, 2324) == 0 && read_capacity_bl() == 2324);
	assert(scsi2_select(0x00, 2336) == 0 && read_capacity_bl() == 2336);
	puts("ok  PF=1 density/block-length table enforced (2352, 2324@00, bad density refused)");

	/* ---------- 6. MODE SENSE reports the real mode; BIOS save/restore ---------- */
	{
		uint8_t sense[32], cdb[6] = { 0x1A, 0, 0x29, 0, 32, 0 };
		unsigned m;
		assert(vendor_select(0x06, 3) == 0);		/* 2336 */
		memset(sense, 0, sizeof(sense));
		assert(scsi(cdb, 6, NULL, 0, sense, 32, &m) == 0);
		assert(sense[3] == 8 && sense[4] == 0x00 && sense[9] == 0 && sense[10] == 0x09 && sense[11] == 0x20);
		/* The BIOS clears byte 0 and sends the first 12 bytes back with PF=1. */
		assert(vendor_select(0x00, 15) == 0 && read_capacity_bl() == 2048);
		sense[0] = 0;
		assert(mode_select(1, sense, 12) == 0);
		assert(read_capacity_bl() == 2336);
		puts("ok  MODE SENSE -> change -> MODE SELECT(PF=1) restore round-trips the mode");
		assert(vendor_select(0x00, 15) == 0);
	}

	/* ---------- 7. Mode 1 / Mode 2 windows ---------- */
	assert(vendor_select(0x00, 0) == 0);
	assert(vendor_select(0x06, 3) == 0);		/* 2336 */
	assert(do_read(640, 1, buf, sizeof(buf), &n) == 0 && n == 2336);
	assert(buf[2] == 0x08 && buf[6] == 0x08);	/* subheader first: bytes from offset 16 */
	assert(do_read(680, 1, buf, sizeof(buf), &n) == 0 && n == 2336);
	assert(memcmp(buf, "PCFV0001", 8) == 0);	/* formless Mode 2 is delivered as-is, no error check */
	puts("ok  2336 mode returns the raw Mode 2 user bytes (formless Mode 2: no error judgement)");

	assert(scsi2_select(0x81, 2048) == 0);		/* the BIOS Photo CD probe's select */
	assert(do_read(640, 1, buf, sizeof(buf), &n) == 0 && n == 2048 && buf[0] == 0x80);	/* Form 1 data at byte 24 */
	puts("ok  density 81h / 2048 reads XA Form 1 data from byte 24");

	/* ---------- 8. EDC judgement follows the header ---------- */
	assert(vendor_select(0x00, 0) == 0);
	assert(do_read(720, 1, buf, sizeof(buf), &n) != 0);
	assert(sense_key() == 3);
	puts("ok  Mode 1 sector with bad EDC -> MEDIUM ERROR");
	assert(vendor_select(0x06, 3) == 0);
	assert(do_read(720, 1, buf, sizeof(buf), &n) != 0);	/* Mode 1 is judged regardless of EJ */
	puts("ok  Mode 1 judged by ECC/EDC even in 2336 mode");

	/* ---------- 9. bus reset restores defaults ---------- */
	assert(vendor_select(0x06, 3) == 0 && read_capacity_bl() == 2336);
	reset_bus();
	assert(read_capacity_bl() == 2048);
	puts("ok  SCSI bus reset restores the default mode");

	/* ---------- 10. DISC ID 20h (CD-ROM XA) changes the default ---------- */
	make_split_disc("xa.cue", "REM PCFX_DISC_ID 20\n");
	load("xa.cue");
	assert(disc_id() == 0x20);
	assert(read_capacity_bl() == 2048);
	assert(do_read(600, 1, buf, sizeof(buf), &n) == 0 && n == 2048);
	/* XA=1 SH=1 EJ=01: window starts at byte 24, so the PC-FX system sector
	 * is shifted by 8 bytes and the BIOS' 2048-byte compare fails. */
	assert(buf[0] != 0x40 && buf[0] == 0x48);
	puts("ok  DISC ID 20h: default read is XA (byte 24); Mode 1 boot sector comes back shifted by 8");
	assert(vendor_select(0x00, 15) == 0);
	assert(do_read(600, 1, buf, sizeof(buf), &n) == 0 && buf[0] == 0x40);
	puts("ok  an explicit XA=0 list recovers it");

	printf("PASS cd_drive_mode_test (%s)\n", dir);
	return 0;
}
