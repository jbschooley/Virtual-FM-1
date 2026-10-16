/* choralroot_core.c -- ChoralRoot 0.23 (engines/choralroot/upstream, GPL-3.0-only: Quixotic7's chord instrument for
 * the M-VAVE FM-1, a fork of Leo Kuroshita's Felucca, Hügelton Instruments), the whole firmware as a library the
 * plugin drives, as the other cores (felucca_core.c) are.
 *
 * Built the way ChoralRoot builds itself for a computer: its own emulator's firmware side
 * (upstream/tools/emu/emu_fw.c, which includes the firmware's sources through emu_firmware.h and tests/hostsim.c,
 * with its HAL in emu_hal_fw.h) is included here unchanged, as the single-core package's firmware (CR_SINGLE_CORE 1,
 * from CMakeLists.txt). The plugin drives it through the boundary that emulator gives its window (emu_hooks.h):
 * emu_hal (keys, buttons, knobs, LEDs, the screen) and the emu_fw_* hooks (power-on, the 1 ms timer, a UI frame, the
 * audio interrupt, MIDI in and out). What the window, the audio device and CoreMIDI do there is done here:
 *   - time is simulated (emu_hal.ready 2 before the power-on): it moves only with the audio rendered, a tick a
 *     millisecond and a UI frame every 15 ms of it, so an instance does the same whatever the host's timing;
 *   - the flash is RAM only (no file): what the firmware saves lives in the instance's state, and in the plugin's
 *     device file through the backup objects below;
 *   - its backup protocol (cr_backup.c, Felucca's editor protocol's backup set: INFO, LIST, GET, PUT) answers SysEx:
 *     frames are assembled here and handed to crb_handle, its replies (CRB_SEND) go out as USB-MIDI packets; the
 *     emulator has no such transport. The stored objects (object_get / object_put) go through the same protocol.
 * Never acted on, as in every core: the update and boot paths (exit, restarts: no-ops here; atexit not taken).
 *
 * One copy of the code for every instance: fel_state.py moves every writable variable, marked by the section pragma
 * below, into the state each instance owns (felucca_core.h). choralroot_names.h (gen_names.py) gives the external
 * names this copy's prefix, so nothing meets another core's at link time. */
/* as its release's single-core package is built (build.sh: CR_SINGLE_CORE=1, --release 0.23) */
#define CR_SINGLE_CORE 1
#define FELUCCA_VERSION "ChoralRoot 0.23"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef FEL_PREFIX
#error "FEL_PREFIX names this copy (felucca_core.h)"
#endif
#include "felucca_core_api.h"
#ifndef CR_NAMES_PROBE
#include "choralroot_names.h"
#endif

#if !defined(__clang__) || !defined(FEL_BSS_SECTION)
#error "the firmware cores are built with Clang through fel_state.py (CMakeLists.txt fm1_core)"
#endif
#define FEL_PRAGMA(x) _Pragma(#x)
#define FEL_SECTIONS(b, d) FEL_PRAGMA(clang section bss = b data = d)
FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)

/* what the emulator's process-level calls would do to the host: nothing. A restart, the ROM boot or the boot stub's
 * request end the emulator's process; here they are let go (the plugin's instance runs on). Its log lines (the boot
 * guard, CR_TRACE's "param:" lines) are not printed. */
static void chrc_noexit(int code) { (void)code; }
static int chrc_noprint(const char *fmt, ...) { (void)fmt; return 0; }
static int chrc_noatexit(void (*f)(void)) { (void)f; return 0; }
#define exit(c) chrc_noexit(c)
#define printf(...) chrc_noprint(__VA_ARGS__)
#define atexit(f) chrc_noatexit(f)

/* the backup protocol's replies (cr_backup.c CRB_SEND: one whole frame, F0 .. F7): out as USB-MIDI packets, taken
 * between audio blocks by FEL(midi_out) */
#define CHRC_OUTQ 8192u
static uint32_t chrc_out[CHRC_OUTQ], chrc_out_r, chrc_out_w;
static uint8_t *chrc_capture;               /* object_get / object_put: a reply copied here instead (chrc_proto) */
static uint32_t chrc_capture_n, chrc_capture_max;
static void chrc_send(const uint8_t *p, uint32_t n)
{
    uint32_t i = 0;
    if (chrc_capture) {
        if (n <= chrc_capture_max) {
            memcpy(chrc_capture, p, n);
            chrc_capture_n = n;
        }
        return;
    }
    while (i < n) {                       /* USB-MIDI SysEx: CIN 4 (continues) with three bytes, 5..7 (ends with 1..3) */
        uint32_t k = n - i > 3u ? 3u : n - i, cin = i + k == n ? 4u + k : 4u, pkt = cin, j;
        if (i + k == n && k == 3u)
            cin = 7u;
        pkt = cin;
        for (j = 0; j < k; j++)
            pkt |= (uint32_t)p[i + j] << (8u * (j + 1u));
        if (chrc_out_w - chrc_out_r >= CHRC_OUTQ)
            chrc_out_r++;                   /* full (nobody takes them): the oldest go */
        chrc_out[chrc_out_w++ % CHRC_OUTQ] = pkt;
        i += k;
    }
}
#define CRB_SEND(p, n) chrc_send((p), (uint32_t)(n))

/* a restart (chr_restart_take): the emulator's flash file (emu_hal_fw.h emu_flash_open, given a path by
 * emu_fw_options) is the image the restart keeps, read from memory; nothing is written anywhere (its writes through
 * to the file are let go: the image is the instance's own emu_flash). Its other file calls (tests/hostsim.c's WAV
 * writers) are never made here. */
static const uint8_t *chrc_boot;            /* the image this power-on starts from (chr_boot_from), else none */
static FILE *chrc_fopen(const char *path, const char *mode)
{
    (void)path;
    return chrc_boot && mode[0] == 'r' ? stdin : NULL;   /* (any FILE: only these calls see it) */
}
static size_t chrc_fread(void *p, size_t size, size_t n, FILE *f)
{
    (void)f;
    if (!chrc_boot)
        return 0;
    memcpy(p, chrc_boot, size * n);
    return n;
}
static size_t chrc_fwrite(const void *p, size_t size, size_t n, FILE *f) { (void)p; (void)size; (void)f; return n; }
static int chrc_fseek(FILE *f, long off, int whence) { (void)f; (void)off; (void)whence; return 0; }
static int chrc_fflush(FILE *f) { (void)f; return 0; }
static int chrc_fclose(FILE *f) { (void)f; return 0; }
#define fopen chrc_fopen
#define fread chrc_fread
#define fwrite chrc_fwrite
#define fseek chrc_fseek
#define fflush chrc_fflush
#define fclose chrc_fclose

#include "../upstream/tools/emu/emu_fw.c"

#undef fopen
#undef fread
#undef fwrite
#undef fseek
#undef fflush
#undef fclose

/* the plugin's own per-instance state, in the copy's sections with the firmware's */
static uint64_t chrc_frames;                /* audio rendered since the power-on */
static uint32_t chrc_ms, chrc_frame_ms;       /* the simulated milliseconds ticked; the last UI frame's */
static uint8_t chrc_sx[4096];               /* a SysEx frame coming in (F0 .. F7), and whether it is complete */
static uint32_t chrc_sx_n;
static uint8_t chrc_sx_on, chrc_sx_done;
static int16_t chrc_half[2u * HALF_FRAMES];   /* the audio interrupt's half buffer, given out a control block at a time */
static uint32_t chrc_half_pos;
static uint8_t chrc_restart;                /* a restart is due: a settings record restored, or 72 RESTART asked */

#undef exit
#undef printf
#undef atexit
#pragma clang section bss = "" data = ""

/* ---- time: the 1 ms timer and the UI frames follow the audio rendered ------------------------------------------ */
static void chrc_advance(void)
{
    const uint32_t ms = (uint32_t)(chrc_frames * 1000u / EMU_FS);
    while (chrc_ms < ms)
        emu_fw_tick(++chrc_ms);
    if (chrc_ms - chrc_frame_ms >= 15u) {     /* emu.c: a frame every 15 ms */
        chrc_frame_ms = chrc_ms;
        emu_fw_frame();
    }
}

/* ---- restarts: what the device restarts for, the plugin does by a power-on that keeps the flash ----------------
 * 72 RESTART restarts the unit once its reply is out (cr_backup.c crb_reboot_ask; on the unit ed_service's
 * crb_leave, which the emulator does not build), and a restored settings record waits for a restart
 * (cr_restore_lock: "settings restored: restart"; its installer page restarts the unit after a restore). Either makes
 * a restart due; FeluccaEngine takes it (chr_restart_take) and powers the instance on again from the flash image
 * (chr_boot_from), the rest of its state as at a power-on. */
static void chrc_handled(void)
{
    if (crb.reboot) {                     /* (72 RESTART: answered; the reboot itself is ours) */
        crb.reboot = 0;
        chrc_restart = 1;
    }
    if (cr_restore_lock)
        chrc_restart = 1;
}
/* 1: a restart is due (taken: not due again until asked again), with the flash image to start from */
uint32_t FEL(restart_take)(const uint8_t **image, uint32_t *n)
{
    if (!chrc_restart)
        return 0;
    chrc_restart = 0;
    *image = emu_flash;
    *n = (uint32_t)sizeof emu_flash;
    return 1;
}
/* before FEL(init), after state_init: the power-on starts from this image (the caller's, until init returns) */
void FEL(boot_from)(const uint8_t *image) { chrc_boot = image; }

/* ---- the backup protocol in-process: a request frame handed to the handler, its reply taken (for object_*) ---- */
static uint32_t chrc_proto(const uint8_t *req, uint32_t n, uint8_t *reply, uint32_t max)
{
    chrc_capture = reply;
    chrc_capture_max = max;
    chrc_capture_n = 0;
    crb_handle(req + 1, n - 2u);          /* (between F0 and F7) */
    chrc_capture = 0;
    chrc_handled();
    return chrc_capture_n;
}
/* u32 in five 7-bit groups, as cr_backup.c's crb_u32 */
static uint32_t chrc_u32(uint8_t *p, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        p[i] = (uint8_t)((v >> (7u * i)) & 0x7Fu);
    return 5u;
}
static uint32_t chrc_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static uint32_t chrc_crc32(const uint8_t *p, uint32_t n)   /* zlib's CRC-32 (LIST's and PUT's) */
{
    uint32_t c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8u; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ---- the API ------------------------------------------------------------------------------------------------ */

void FEL(init)(void)
{
    chrc_frames = 0;
    chrc_ms = chrc_frame_ms = 0;
    chrc_sx_n = 0;
    chrc_sx_on = chrc_sx_done = 0;
    chrc_half_pos = 0;
    emu_hal.ready = 2;                    /* simulated time (emu_fw_init: emu_sim) */
    if (chrc_boot)                        /* a restart: the flash as it was (read through chrc_fopen) */
        emu_fw_options("flash", 0, 0, 1);
    else
        emu_fw_options(NULL, 1, 0, 1);    /* the flash in RAM, no file: erased */
    emu_hal.master = 724;                 /* the MASTER pot where felucca_init's default level is (emu_fw_init) */
    emu_fw_init(0);
    chrc_boot = 0;
}

const char *FEL(version)(void) { return "v0.23"; }   /* ChoralRoot 0.23 (its INFO: "ChoralRoot 0.23") */
/* the plugin renders in blocks of 32 frames (FeluccaEngine's block_, Felucca's CTL); ChoralRoot's interrupt fills
 * 128-frame halves: one half is rendered and given out in four blocks */
#define CHRC_BLOCK 32u
_Static_assert(HALF_FRAMES % CHRC_BLOCK == 0u, "a half is whole blocks");
uint32_t FEL(ctl)(void) { return CHRC_BLOCK; }
uint32_t FEL(rate)(void) { return FS; }
uint32_t FEL(ntracks)(void) { return NTRK; }
uint32_t FEL(nparts)(void) { return NPART; }
uint32_t FEL(nengines)(void) { return NENGINES; }
uint32_t FEL(pcount)(void) { return 0; }   /* (none offered: below) */
uint32_t FEL(pe0)(void) { return 0; }
uint32_t FEL(gcount)(void) { return 0; }
uint32_t FEL(nsteps)(void) { return 0; }   /* (no sequencer: its looper is its own) */

const char *FEL(engine_name)(uint32_t e) { return e < NENGINES && ENGINES[e] ? ENGINES[e]->name : ""; }
const char *FEL(engine_page)(uint32_t e, uint32_t page) { (void)e; (void)page; return ""; }
uint32_t FEL(npresets)(uint32_t e) { (void)e; return 0; }
const char *FEL(preset_name)(uint32_t e, uint32_t i) { (void)e; (void)i; return ""; }
/* ChoralRoot's sounds are its own pages' (cr_pages.c) and pool: no parameter is offered to the host, the Sound page
 * or the editor protocol (which it does not speak beyond the backup) */
int FEL(param_desc)(uint32_t track, uint32_t id, fel_desc_t *out) { (void)track; (void)id; (void)out; return 0; }
int FEL(global_desc)(uint32_t id, fel_desc_t *out) { (void)id; (void)out; return 0; }
int32_t FEL(param_get)(uint32_t track, uint32_t id) { (void)track; (void)id; return 0; }
void FEL(param_set)(uint32_t track, uint32_t id, int32_t v) { (void)track; (void)id; (void)v; }
int32_t FEL(global_get)(uint32_t id) { (void)id; return 0; }
void FEL(global_set)(uint32_t id, int32_t v) { (void)id; (void)v; }
uint32_t FEL(engine_of)(uint32_t track) { return track < NTRK ? trk[track].eng_req : 0u; }
uint32_t FEL(preset_of)(uint32_t track) { (void)track; return 0; }
void FEL(set_engine)(uint32_t track, uint32_t e) { (void)track; (void)e; }
void FEL(apply_preset)(uint32_t track, uint32_t pi) { (void)track; (void)pi; }
uint32_t FEL(engines_shown)(void) { return 0; }
uint32_t FEL(engine_shown)(uint32_t n) { (void)n; return 0; }
uint32_t FEL(fm6_engine)(void) { return ENGI_FM6; }
void FEL(fm6_patch_get)(uint32_t track, uint8_t *v155) { (void)track; memset(v155, 0, 155); }
void FEL(fm6_patch_set)(uint32_t track, const uint8_t *v155) { (void)track; (void)v155; }

/* MIDI in: notes and the rest to the firmware's queue (usb.c), SysEx assembled here for its backup handler */
void FEL(midi)(uint32_t pkt)
{
    const uint32_t cin = pkt & 0xFu, k = cin == 5u ? 1u : cin == 6u ? 2u : 3u;
    if (cin >= 4u && cin <= 7u) {
        uint32_t j;
        for (j = 0; j < k; j++) {
            const uint8_t b = (uint8_t)(pkt >> (8u * (j + 1u)));
            if (b == 0xF0u) {
                chrc_sx_on = 1;
                chrc_sx_done = 0;
                chrc_sx_n = 0;
            }
            if (!chrc_sx_on)
                continue;
            if (chrc_sx_n < sizeof chrc_sx)
                chrc_sx[chrc_sx_n++] = b;
            if (b == 0xF7u) {
                chrc_sx_on = 0;
                chrc_sx_done = chrc_sx_n < sizeof chrc_sx;
            }
        }
        return;
    }
    (void)emu_fw_midi_in(pkt);
}

/* a frame waiting: ChoralRoot's (7D 46 4C) to its handler, its reply out; anything else dropped */
void FEL(service)(void)
{
    if (!chrc_sx_done)
        return;
    chrc_sx_done = 0;
    if (chrc_sx_n >= 6u && chrc_sx[1] == CRB_HDR0 && chrc_sx[2] == CRB_HDR1 && chrc_sx[3] == CRB_HDR2)
        crb_handle(chrc_sx + 1, chrc_sx_n - 2u);
    chrc_handled();
}
int FEL(service_ready)(void)
{
    if (chrc_sx_done)
        FEL(service)();
    return !chrc_sx_done;
}

void FEL(render)(int32_t *out, uint32_t frames)   /* interleaved stereo, 16-bit full scale; frames a multiple of ctl */
{
    uint32_t i, k;
    for (i = 0; i + CHRC_BLOCK <= frames; i += CHRC_BLOCK) {
        if (chrc_half_pos == 0u) {        /* the next half: the timer and the UI up to it, then the interrupt */
            chrc_advance();
            emu_fw_audio(chrc_half, HALF_FRAMES);
            chrc_frames += HALF_FRAMES;
        }
        for (k = 0; k < 2u * CHRC_BLOCK; k++)
            out[2u * i + k] = chrc_half[2u * chrc_half_pos + k];
        chrc_half_pos = (chrc_half_pos + CHRC_BLOCK) % HALF_FRAMES;
    }
}

/* ---- the front panel and the screen ----------------------------------------------------------------------- */
uint32_t FEL(nbuttons)(void) { return NB; }
const char *FEL(button_name)(uint32_t b) { return b < NB ? B_NAME[b] : ""; }
uint32_t FEL(nknobs)(void) { return NE; }
const char *FEL(knob_name)(uint32_t k) { return k < NE ? E_NAME[k] : ""; }
void FEL(button)(uint32_t b, int down)   /* by label (B_*): the matrix button the panel's calibration names */
{
    uint32_t bit;
    if (b >= NB)
        return;
    bit = 1u << emu_hal.btn_id[b];
    if (down) {
        emu_hal.buttons |= bit;
        emu_hal.buttons_tap |= bit;      /* (a press shorter than a tick still counts) */
    } else {
        emu_hal.buttons &= ~bit;
    }
}
void FEL(key)(uint32_t k, int down)      /* the 27 keys, 0 = the lowest */
{
    uint32_t bit;
    if (k >= EMU_NKEY)
        return;
    bit = 1u << k;
    if (down) {
        emu_hal.keys |= bit;
        emu_hal.keys_tap |= bit;
    } else {
        emu_hal.keys &= ~bit;
    }
}
void FEL(knob)(uint32_t role, int32_t steps)   /* by role (EN_*), clockwise + */
{
    if (role < NE)
        emu_hal.enc[emu_hal.enc_id[role] % EMU_NENC] += steps * emu_hal.enc_dir[role];
}
void FEL(draw)(uint16_t *screen)          /* the screen as its last UI frame drew it: 240 x 240 RGB565, big endian */
{
    if (screen)
        memcpy(screen, emu_hal.lcd, sizeof emu_hal.lcd);
}

uint32_t FEL(midi_out)(uint32_t *pkts, uint32_t max)   /* the backup's replies, then what the firmware sends */
{
    uint32_t n = 0, p;
    while (chrc_out_r != chrc_out_w && n < max)
        pkts[n++] = chrc_out[chrc_out_r++ % CHRC_OUTQ];
    while (n < max && emu_fw_midi_out_take(&p))
        pkts[n++] = p;
    return n;
}

/* ---- the selection and the transport: ChoralRoot's are its own (its parts are fixed, its looper plays) -------- */
uint32_t FEL(selected)(void) { return 0; }
void FEL(select)(uint32_t t) { (void)t; }
void FEL(transport)(int play) { (void)play; }
int FEL(playing)(void) { return 0; }
int32_t FEL(step_of)(uint32_t track) { (void)track; return -1; }
uint32_t FEL(armed)(void) { return 0; }
uint32_t FEL(scale_mask)(uint32_t track) { (void)track; return 0xFFFu; }

/* the LEDs: 14 buttons by label, the 27 keys, then PLAY's green LED; 0 dark, 1 glowing (dim, faint), 2 mid, 3 lit */
uint32_t FEL(leds)(uint8_t *out, uint32_t max)
{
    uint32_t i, n = 0;
    for (i = 0; i < NB + 27u + 1u && n < max; i++) {
        uint32_t id = i < NB ? emu_hal.btn_id[i] : 14u + (i - NB), p, r, v = 0;
        if (i == NB + 27u)
            v = (emu_hal.led[emu_hal.led_play_green >> 3] >> (emu_hal.led_play_green & 7u)) & 1u ? 3u : 0u;
        else
            for (p = 0; p < EMU_NCOL; p++)
                for (r = 1; r < 5u; r++)
                    if (emu_keymap[r][p] == (int8_t)id)
                        v = (emu_hal.led[p] >> r) & 1u ? 3u : (emu_hal.led_mid[p] >> r) & 1u ? 2u
                          : ((emu_hal.led_dim[p] | emu_hal.led_faint[p]) >> r) & 1u ? 1u : 0u;
        out[n++] = (uint8_t)v;
    }
    return n;
}
uint32_t FEL(nlanes)(uint32_t track) { (void)track; return 0; }
const char *FEL(lane_name)(uint32_t track, uint32_t lane) { (void)track; (void)lane; return ""; }
int32_t FEL(arr_do)(uint32_t op, uint32_t arg) { (void)op; (void)arg; return -1; }
uint32_t FEL(arr_state)(uint8_t *out, uint32_t max) { (void)out; (void)max; return 0; }
int32_t FEL(arr_chain)(const uint8_t *e, uint32_t n, int loop) { (void)e; (void)n; (void)loop; return -1; }

/* ---- the stored objects, as its backup carries them (cr_backup.c's ids: 1 the settings, 8 the FM6 bank, 14..21
 * the CZ-1 banks, 23 the favourites, 40..49 the loop slots, 50..62 the sound pool's banks), read and written through
 * its own protocol's LIST, GET and PUT, so exactly as an FM-1 running it does them ------------------------------- */
#define CR_OBJ_MAX 65536u
uint32_t FEL(object_max)(void) { return CR_OBJ_MAX; }

int32_t FEL(object_get)(uint32_t id, uint8_t *out, uint32_t max)
{
    uint8_t q[32], r[1024];
    uint32_t n, i, count, size = 0, got = 0;
    int found = 0;
    /* LIST: 1, rc, count, per object id, size u32, crc u32 */
    q[0] = 0xF0; q[1] = CRB_HDR0; q[2] = CRB_HDR1; q[3] = CRB_HDR2; q[4] = CRB_LIST; q[5] = 0xF7;
    n = chrc_proto(q, 6, r, sizeof r);
    if (n < 9u || r[4] != CRB_LIST || r[6] != 0)
        return -1;
    count = r[7];
    for (i = 0; i < count && 8u + 11u * i + 11u <= n; i++)
        if (r[8u + 11u * i] == id) {
            size = chrc_r32(r + 9u + 11u * i);
            found = 1;
        }
    if (!found)
        return -1;
    if (size > max)
        return -1;
    while (got < size) {                  /* GET: id, offset u32, count -> id, rc, offset u32, count, pack7 */
        uint32_t want = size - got > 256u ? 256u : size - got, a, m;
        q[0] = 0xF0; q[1] = CRB_HDR0; q[2] = CRB_HDR1; q[3] = CRB_HDR2; q[4] = CRB_GET; q[5] = (uint8_t)id;
        m = 6u + chrc_u32(q + 6, got);
        q[m++] = (uint8_t)(want & 0x7Fu);
        q[m++] = (uint8_t)(want >> 7);
        q[m++] = 0xF7;
        n = chrc_proto(q, m, r, sizeof r);
        if (n < 14u || r[4] != CRB_GET || r[6] != 0)
            return -1;
        a = crb_unpack(r + 14, n - 15u, out + got, want);
        if (a != want)
            return -1;
        got += a;
    }
    return (int32_t)size;
}

/* 0 ok, 1 refused, 2 failed (cr_backup.c's rc, else 2) */
uint32_t FEL(object_put)(uint32_t id, const uint8_t *data, uint32_t len)
{
    uint8_t q[512], r[64];
    uint32_t n, m, at = 0;
    q[0] = 0xF0; q[1] = CRB_HDR0; q[2] = CRB_HDR1; q[3] = CRB_HDR2; q[4] = CRB_PUT; q[5] = 0; q[6] = (uint8_t)id;
    m = 7u + chrc_u32(q + 7, len);
    m += chrc_u32(q + m, chrc_crc32(data, len));
    q[m++] = 0xF7;
    n = chrc_proto(q, m, r, sizeof r);
    if (n < 8u || r[4] != CRB_PUT)
        return 2;
    if (r[7] != 0)
        return r[7] == 1u ? 1u : 2u;
    while (at < len) {                    /* data: id, offset u32, pack7 (at most 128 bytes a frame) */
        uint32_t k = len - at > 128u ? 128u : len - at, rest, j;
        const uint8_t *p = data + at;
        q[0] = 0xF0; q[1] = CRB_HDR0; q[2] = CRB_HDR1; q[3] = CRB_HDR2; q[4] = CRB_PUT; q[5] = 1; q[6] = (uint8_t)id;
        m = 7u + chrc_u32(q + 7, at);
        for (rest = k; rest;) {
            uint32_t g = rest > 7u ? 7u : rest, bits = 0;
            for (j = 0; j < g; j++)
                bits |= (uint32_t)(p[j] >> 7) << j;
            q[m++] = (uint8_t)bits;
            for (j = 0; j < g; j++)
                q[m++] = (uint8_t)(p[j] & 0x7Fu);
            p += g;
            rest -= g;
        }
        q[m++] = 0xF7;
        n = chrc_proto(q, m, r, sizeof r);
        if (n < 8u || r[4] != CRB_PUT || r[7] != 0)
            return 2;
        at += k;
    }
    q[0] = 0xF0; q[1] = CRB_HDR0; q[2] = CRB_HDR1; q[3] = CRB_HDR2; q[4] = CRB_PUT; q[5] = 2; q[6] = (uint8_t)id; q[7] = 0xF7;
    n = chrc_proto(q, 8, r, sizeof r);
    if (n < 8u || r[4] != CRB_PUT)
        return 2;
    return r[7] == 0 ? 0u : r[7] == 1u ? 1u : 2u;
}
void FEL(objects_done)(void) {}   /* (a batch of objects put: each was committed as the device commits it) */
