/* sloop_core.c -- SLOOP 2.3 (engines/sloop/upstream, GPL-3.0-only; isod89, after Felucca by Leo
 * Kuroshita / Hügelton Instruments), the whole firmware, as a library the plugin can drive: a
 * virtual FM-1 running Sloop, with its sound, sequencer, arranger, screen, front panel, projects,
 * user presets and editor protocol.
 *
 * Sloop is a fork of Felucca and is built the same way: one compilation unit whose state is all
 * file-level statics, compiled several times (CMakeLists.txt, SLOOP_COPIES), each copy with its own
 * FEL_PREFIX and sections, behind the same API as felucca_core.c (felucca_core.h). See that file
 * for how a copy is restored and its state saved and put back; this one only differs in what it
 * includes and what it replaces.
 *
 * The firmware's own sources are included unchanged, in the order its felucca.c includes them.
 * What touches hardware is replaced below: the panel's buttons, keys and knobs, the LEDs (none) and
 * the screen are the plugin's, and the flash is RAM. Never built: lcd.c, audio.c, the update and
 * boot loader paths (ota.c, recovery.c), the serial console, the TRS MIDI input and the USB audio
 * input: nothing here acts on them. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifndef FEL_PREFIX
#error "FEL_PREFIX names this copy (felucca_core.h)"
#endif
#include "felucca_core_api.h"

/* the firmware's globals that are not static: one per copy */
#define bootguard FEL(bootguard)
#define panel FEL(panel)
#define settings FEL(settings)
#define proj_slot FEL(proj_slot)
#define felucca_dbg FEL(felucca_dbg)
#define fm1_crash FEL(fm1_crash)

#if !defined(__clang__) || !defined(FEL_BSS_SECTION)
#error "Sloop's copies need Clang and their sections (CMakeLists.txt)"
#endif
#define FEL_PRAGMA(x) _Pragma(#x)
#define FEL_SECTIONS(b, d) FEL_PRAGMA(clang section bss = b data = d)
#ifdef FEL_BSS_FIRST                     /* Windows: where the sections start */
FEL_SECTIONS(FEL_BSS_FIRST, FEL_DATA_FIRST)
__attribute__((used)) static uint8_t fel_bss_start;
__attribute__((used)) static uint8_t fel_data_start = 1;
#endif
FEL_SECTIONS(FEL_BSS_SECTION, FEL_DATA_SECTION)

#define __attribute__(x)
#define FELUCCA_OTA 1                    /* the editor's SysEx plumbing in usb.c (not ota.c: never built) */
#define FELUCCA_FLASH 1                  /* projects, user presets and settings in "flash" (RAM below) */
#define FELUCCA_UAC 0                    /* no USB audio input */
#define FELUCCA_CDC 0                    /* no serial console */
#define FELUCCA_UART 0                   /* no TRS MIDI input */
#define FELUCCA_ARRANGER 1
#define FELUCCA_ICONS 1
/* the user sample slots: empty, and never written (the plugin takes no samples yet), so one array
 * of zeros for every copy (sloop_shared.c, written by CMakeLists.txt) */
extern const uint8_t slp_no_samples[3][0x14000];
#define SMP_USER_XIP(k) ((const uint8_t *)slp_no_samples[k])
#define memset FEL(memset)
#define memcpy FEL(memcpy)
#define memcmp FEL(memcmp)
#include "felucca_tables.h"
#include "../upstream/firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp

/* ---- the hardware, as Sloop's host tests replace it (tests/ui_pages_test.c) ------------------- */
static struct { volatile uint32_t notes, buttons; } fm1_in;
#define FM1_NCOL 16u
static const int8_t FM1_KEYMAP[5][16];
static uint8_t fm1_led[16], fm1_led_dim[16], fm1_led_bg[16];   /* (no LEDs here) */
static volatile uint16_t fm1_led_bg_ns;
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static int32_t fm1_adc_read(int c) { (void)c; return -1; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];  /* RGB565, as the LCD takes it (big endian) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
#include "../upstream/firmware/src/gfx.c"
#include "../upstream/firmware/src/core.h"
#include "../upstream/firmware/src/engines.c"
#include "../upstream/firmware/src/drums.c"
#include "../upstream/firmware/src/params.c"
#include "../upstream/firmware/src/voice.c"
#include "../upstream/firmware/src/slicer.c"
#include "../upstream/firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../upstream/firmware/src/usb.c"
/* what goes out (the editor's replies and pushes): usb.c's queue, moved on to this one, which the
 * plugin takes between audio blocks (FEL(midi_out)); see felucca_core.c */
#define FEL_OUTQ 8192u
static uint32_t fel_out[FEL_OUTQ], fel_out_r, fel_out_w;
static void fel_out_drain(void)
{
    while (so_r != so_w) {
        if (fel_out_w - fel_out_r >= FEL_OUTQ)
            fel_out_r++;
        fel_out[fel_out_w++ % FEL_OUTQ] = sx_out_q[so_r++ % SXQ];
    }
}
static uint32_t fel_wait_ms;
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fel_out_drain();
    if (so_w - so_r >= SXQ) {
        fel_wait_ms++;
        fm1_ms++;
    }
}
#include "../upstream/firmware/src/arranger.c"
#include "../upstream/firmware/src/seq.c"
#define SCOPE_N 512u                     /* audio.c's (not built): the HOME oscilloscope, fed in FEL(render) */
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = host_enc[e % 7u]; host_enc[e % 7u] = 0; return s; }
static void seq_stop(void);
static void fm1_wdt_feed(void)           /* while the main loop waits: see felucca_core.c */
{
    if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    fel_wait_ms++;
    fm1_ms++;
}
static struct { uint32_t magic, stage, page, home, ui_frames; } felucca_dbg;
#include "../upstream/firmware/src/panel.c"
#include "../upstream/firmware/src/ui.c"
#include "../upstream/firmware/src/ui_song.c"
#include "../upstream/firmware/src/ui_studio.c"
#include "../upstream/firmware/src/icons.c"
#include "../upstream/firmware/src/ui_draw.c"
#include "../upstream/firmware/src/ui_layers.c"
static void fel_panel_setup(void) { ui_message("PANEL: ON THE FM-1"); }
#define panel_setup() fel_panel_setup()
#include "../upstream/firmware/src/ui_menu.c"
#undef panel_setup
#include "../upstream/firmware/src/ui_input.c"

/* the flash: the storage objects' sectors in RAM, as the part the firmware expects */
#define FEL_NSECT 32u
static uint32_t sect_addr[FEL_NSECT];    /* address + 1; 0: free */
static uint8_t sect_data[FEL_NSECT][4096];
static uint8_t *sect(uint32_t off)
{
    uint32_t base = off & ~4095u, i;
    for (i = 0; i < FEL_NSECT; i++)
        if (sect_addr[i] == base + 1u)
            return sect_data[i] + (off - base);
    for (i = 0; i < FEL_NSECT; i++)
        if (!sect_addr[i]) {
            sect_addr[i] = base + 1u;
            memset(sect_data[i], 0xFF, 4096);
            return sect_data[i] + (off - base);
        }
    return 0;
}
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {                          /* (a read may cross sectors here) */
        uint32_t k = 4096u - (off & 4095u);
        const uint8_t *p = sect(off);
        if (k > n)
            k = n;
        if (!p)
            return -1;
        memcpy(d, p, k);
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off) { uint8_t *p = sect(off); if (!p) return -8; memset(p, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    while (n) {
        uint32_t k = 4096u - (off & 4095u);
        uint8_t *p = sect(off);
        if (k > n)
            k = n;
        if (!p)
            return -8;
        memcpy(p, s, k);
        off += k;
        s += k;
        n -= k;
    }
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the expected part: flash_ok */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { (void)off; *took = 0; return -1; }   /* no user samples yet */
static int fl_write(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
#include "../upstream/firmware/src/storage.c"
#include "../upstream/firmware/src/upreset.c"
#include "../upstream/firmware/src/project.c"
#include "../upstream/firmware/src/editor.c"

/* ---- from main.c (not built: the device's boot) --------------------------------------------- */
/* power-on: three parts with their default sounds (TRK_DEF), the drum track, empty patterns */
static void felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    TDRUM->p[P_E0] = DRUM_DEFAULT_KIT;
    song.sel = 0;
    song.master_q12 = 2048;                   /* (the MASTER knob: the plugin's own volume follows) */
    autosave_resume();
    song.g[G_SYNC] = (int16_t)lights_sync;
    layers_init();
    go_home();
    ui.force = 1;
}

/* the plugin's own per-instance state, in the copy's sections with Sloop's: the audio rendered (the
 * clock), when the main loop last ran, and when the editor was last served */
static uint64_t fel_frames;
static uint32_t fel_main_ms;
static uint64_t fel_served_at;

#undef __attribute__                     /* Sloop's code is done: attributes mean something again */
static uint8_t fel_bss_marker;
static uint8_t fel_data_marker = 1;
#ifdef FEL_BSS_LAST
FEL_SECTIONS(FEL_BSS_LAST, FEL_DATA_LAST)
__attribute__((used)) static uint8_t fel_bss_stop;
__attribute__((used)) static uint8_t fel_data_stop = 1;
#endif
#pragma clang section bss = "" data = ""

/* ---- the copy's state, as the program started (as felucca_core.c) ----------------------------- */
#ifndef FEL_BSS_LAST
extern uint8_t fel_bss_start __asm(FEL_BSS_START);
extern uint8_t fel_bss_stop __asm(FEL_BSS_STOP);
extern uint8_t fel_data_start __asm(FEL_DATA_START);
extern uint8_t fel_data_stop __asm(FEL_DATA_STOP);
#endif
static uint8_t *fel_pristine;

static uint8_t *fel_at(uint8_t *p)
{
    __asm__ volatile("" : "+r"(p));
    return p;
}
#define FEL_NB ((size_t)(fel_at(&fel_bss_stop) - fel_at(&fel_bss_start)))
#define FEL_ND ((size_t)(fel_at(&fel_data_stop) - fel_at(&fel_data_start)))

void *malloc(size_t);

__attribute__((noinline)) void FEL(restore)(void)
{
    const size_t nb = FEL_NB, nd = FEL_ND;
    (void)fel_bss_marker; (void)fel_data_marker;
    if (!fel_pristine) {
        fel_pristine = (uint8_t *)malloc(nd ? nd : 1);
        if (fel_pristine)
            FEL(memcpy)(fel_pristine, fel_at(&fel_data_start), nd);
        FEL(memset)(fel_at(&fel_bss_start), 0, nb);
        return;
    }
    FEL(memset)(fel_at(&fel_bss_start), 0, nb);
    FEL(memcpy)(fel_at(&fel_data_start), fel_pristine, nd);
    __asm__ volatile("" ::: "memory");
}

uint32_t FEL(state_bytes)(void) { return (uint32_t)(FEL_NB + FEL_ND); }

__attribute__((noinline)) void FEL(state_get)(uint8_t *out)
{
    const size_t nb = FEL_NB, nd = FEL_ND;
    __asm__ volatile("" ::: "memory");
    FEL(memcpy)(out, fel_at(&fel_bss_start), nb);
    FEL(memcpy)(out + nb, fel_at(&fel_data_start), nd);
}
__attribute__((noinline)) void FEL(state_put)(const uint8_t *in)
{
    const size_t nb = FEL_NB, nd = FEL_ND;
    FEL(memcpy)(fel_at(&fel_bss_start), in, nb);
    FEL(memcpy)(fel_at(&fel_data_start), in + nb, nd);
    __asm__ volatile("" ::: "memory");
}

/* ---- the API ------------------------------------------------------------------------------ */

static const param_desc_t *track_param_desc(uint32_t track, uint32_t id)
{
    if (track >= NTRK || id >= P_COUNT)
        return 0;
    return track_desc(&trk[track], id);   /* params.c: the drum kit, an engine's mode-dependent names */
}

static void fill(fel_desc_t *out, const param_desc_t *d)
{
    uint32_t n = 0;
    out->label = d->label;
    out->unit = d->unit;
    out->fmt = d->fmt;
    out->min = d->min;
    out->max = d->max;
    out->def = d->def;
    out->names = d->names;
    if (d->fmt == F_ENUM && d->names)
        n = (uint32_t)(d->max - d->min + 1);
    out->nnames = n;
}

static void fel_clock(void)
{
    const uint64_t us = fel_frames * 1000000u / FS + (uint64_t)fel_wait_ms * 1000u;
    fm1_ms = (uint32_t)(us / 1000u);
    host_ticks = (uint32_t)us;
}

/* the editor protocol's waiting frame, if any; a frame that is not the editor's is dropped (see
 * felucca_core.c) */
static void fel_editor(void)
{
    ed_service();
    if (sx_ready)
        ota_frame_done();
}

/* one pass of main.c's loop, without what is the device's own (the watchdog, USB, the battery and
 * MASTER knob, the update and boot loader requests, the LEDs and the screen, drawn when asked) */
static void fel_main_pass(void)
{
    usb.uboot_req = 0;
    usb.ota_req = 0;
    fel_editor();
    ui_input();
    autosave_tick();                     /* the working project into "flash", when quiet */
    sections_flush();                    /* the live sections and the recorded song, when quiet */
}

void FEL(init)(void)   /* main.c fm1_main, up to its loop: the stored settings and objects, then the parts */
{
    fel_frames = 0;
    fel_wait_ms = 0;
    fel_main_ms = 0;
    fel_clock();
    persist_boot();
    settings_init();
    panel_init();
    felucca_init();
    usb.config = 1;
}

const char *FEL(version)(void) { return FELUCCA_VERSION; }   /* ui.c's: "SLOOP 2.3" */
uint32_t FEL(ctl)(void) { return CTL; }
uint32_t FEL(rate)(void) { return FS; }
uint32_t FEL(ntracks)(void) { return NTRK; }
uint32_t FEL(nparts)(void) { return NPART; }
uint32_t FEL(nengines)(void) { return NENGINES; }
uint32_t FEL(pcount)(void) { return P_COUNT; }
uint32_t FEL(pe0)(void) { return P_E0; }
uint32_t FEL(gcount)(void) { return G_COUNT; }
uint32_t FEL(nsteps)(void) { return NSTEP; }

const char *FEL(engine_name)(uint32_t e) { return e < NENGINES ? ENGINES[e]->name : ""; }
const char *FEL(engine_page)(uint32_t e, uint32_t page) { return e < NENGINES && page < 2u ? ENGINES[e]->page_title[page] : ""; }
uint32_t FEL(npresets)(uint32_t e) { return e < NENGINES ? ENGINES[e]->npresets : 0u; }
const char *FEL(preset_name)(uint32_t e, uint32_t i) { return e < NENGINES && i < ENGINES[e]->npresets ? ENGINES[e]->presets[i].name : ""; }

int FEL(param_desc)(uint32_t track, uint32_t id, fel_desc_t *out)
{
    const param_desc_t *d = track_param_desc(track, id);
    if (!d)
        return 0;
    fill(out, d);
    return 1;
}

int FEL(global_desc)(uint32_t id, fel_desc_t *out)
{
    if (id >= G_COUNT)
        return 0;
    fill(out, &GP[id]);
    return 1;
}

int32_t FEL(param_get)(uint32_t track, uint32_t id) { return track < NTRK && id < P_COUNT ? trk[track].p[id] : 0; }
void FEL(param_set)(uint32_t track, uint32_t id, int32_t v)
{
    const param_desc_t *d = track_param_desc(track, id);
    if (d && d->max > d->min)
        trk[track].p[id] = (int16_t)clamp(v, d->min, d->max);
}
int32_t FEL(global_get)(uint32_t id) { return id < G_COUNT ? song.g[id] : 0; }
void FEL(global_set)(uint32_t id, int32_t v)
{
    if (id < G_COUNT && GP[id].max > GP[id].min)
        song.g[id] = (int16_t)clamp(v, GP[id].min, GP[id].max);
}

uint32_t FEL(engine_of)(uint32_t track) { return track < NTRK ? trk[track].eng_req : 0u; }
uint32_t FEL(preset_of)(uint32_t track) { return track < NTRK ? trk[track].preset : 0u; }
void FEL(set_engine)(uint32_t track, uint32_t e) { if (track < NPART) set_engine_of(&trk[track], e); }   /* (not the drum track) */
void FEL(apply_preset)(uint32_t track, uint32_t pi) { if (track < NPART) apply_preset_to(&trk[track], pi); }

/* the engines one can pick: all of Sloop's, in its order */
uint32_t FEL(engines_shown)(void) { return NENGINES; }
uint32_t FEL(engine_shown)(uint32_t n) { return n < NENGINES ? n : 0u; }

/* Sloop has no FM6 engine: none of its engines is one, and no track has an FM6 patch */
uint32_t FEL(fm6_engine)(void) { return NENGINES; }
void FEL(fm6_patch_get)(uint32_t track, uint8_t *v155) { (void)track; FEL(memset)(v155, 0, 155); }
void FEL(fm6_patch_set)(uint32_t track, const uint8_t *v155) { (void)track; (void)v155; }

void FEL(midi)(uint32_t pkt) { midi_in_event(pkt); }

void FEL(service)(void)   /* the editor now, between audio blocks (see felucca_core.c) */
{
    usb.uboot_req = 0;
    usb.ota_req = 0;
    if (fel_frames == fel_served_at) {
        fel_wait_ms += 25u;
        fel_clock();
    }
    fel_served_at = fel_frames;
    fel_editor();
}
int FEL(service_ready)(void)
{
    if (sx_ready)
        FEL(service)();
    return !sx_ready;
}

void FEL(render)(int32_t *out, uint32_t frames)   /* interleaved stereo, frames a multiple of CTL */
{
    uint32_t i;
    for (i = 0; i + CTL <= frames; i += CTL) {
        if (fm1_ms - fel_main_ms >= 16u) {
            fel_main_ms = fm1_ms;
            fel_main_pass();
        }
        mix_block(out + 2u * i, CTL);
        {   /* the HOME screen's oscilloscope, as audio.c's audio_block feeds it */
            uint32_t k;
            for (k = 1u; k < CTL; k += 2u)
                scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * (i + k)];
        }
        fel_frames += CTL;
        fel_clock();
    }
}

/* ---- the front panel and the screen -------------------------------------------------------------- */
uint32_t FEL(nbuttons)(void) { return NB; }
const char *FEL(button_name)(uint32_t b) { return b < NB ? B_NAME[b] : ""; }
uint32_t FEL(nknobs)(void) { return NE; }
const char *FEL(knob_name)(uint32_t k) { return k < NE ? E_NAME[k] : ""; }

void FEL(button)(uint32_t b, int down)
{
    uint32_t bit;
    if (b >= NB)
        return;
    bit = 1u << panel.btn[b];
    if (down) {
        if (!(fm1_in.buttons & bit))
            host_pressed |= bit;
        fm1_in.buttons |= bit;
    } else {
        fm1_in.buttons &= ~bit;
    }
}

void FEL(key)(uint32_t k, int down)   /* the 27 keys, 0 = the lowest */
{
    uint32_t bit;
    if (k >= 27u)
        return;
    bit = 1u << k;
    if (down) {
        if (!(fm1_in.notes & bit))
            host_notes |= bit;
        fm1_in.notes |= bit;
    } else {
        fm1_in.notes &= ~bit;
    }
}

void FEL(knob)(uint32_t role, int32_t steps)
{
    if (role < NE)
        host_enc[panel.enc[role] % 7u] += steps * panel.dir[role];
}

void FEL(draw)(uint16_t *screen)
{
    ui_leds();                           /* (the keys' lights: none here, but it keeps the UI's state as on the device) */
    ui_draw();
    if (screen)
        memcpy(screen, host_screen, sizeof host_screen);
}

uint32_t FEL(midi_out)(uint32_t *pkts, uint32_t max)
{
    uint32_t n = 0;
    fel_out_drain();
    while (fel_out_r != fel_out_w && n < max)
        pkts[n++] = fel_out[fel_out_r++ % FEL_OUTQ];
    return n;
}

uint32_t FEL(selected)(void) { return song.sel; }
void FEL(select)(uint32_t t)
{
    if (t < NTRK && t != song.sel)
        track_select(t);
}

void FEL(transport)(int play) { transport_req = play ? 1u : 2u; }
int FEL(playing)(void) { return song.playing != 0u; }
/* the step a track plays now (0-based, within its length), -1 while stopped: the playhead (seq.c:
 * seq_abs, the transport grid's step last played) */
int32_t FEL(step_of)(uint32_t track)
{
    if (track >= NTRK || !song.playing || trk[track].seq_abs == SEQ_NONE)
        return -1;
    return (int32_t)(trk[track].seq_abs % trk_len(&trk[track]));
}
uint32_t FEL(armed)(void) { return song.rec; }   /* live recording armed: a bit per track */
/* the track's scale as 12 bits from its ROOT (seq.c); the drum track's 16 sounds (drums.c), none
 * on a synth part */
uint32_t FEL(scale_mask)(uint32_t track) { return track < NTRK ? scale_mask(&trk[track]) : 0xFFFu; }
uint32_t FEL(nlanes)(uint32_t track) { return track == TRK_DRUM ? DRUM_LANES : 0u; }
const char *FEL(lane_name)(uint32_t track, uint32_t lane) { return track == TRK_DRUM && lane < DRUM_LANES ? LANE_NAME[lane] : ""; }

/* ---- the device's stored objects, as the editor's full backup carries them (editor.c v6): 0 the
 * working project, 1 the settings, 2..5 the projects A..D, 6 and 7 the user preset banks. The user
 * sample slots (32..34) are not kept here. ---------------------------------------------------------- */
uint32_t FEL(object_max)(void) { return (uint32_t)sizeof proj_tmp; }

int32_t FEL(object_get)(uint32_t id, uint8_t *out, uint32_t max)
{
    const uint8_t *p;
    uint32_t len;
    if (id == 0u)
        proj_capture((project_t *)ED_BK_RAW);
    else if (id == 1u)
        persist_fill(&ed_bk_set);
    else if (id > 7u)
        return -1;
    p = ed_bk_obj(id, &len);
    if (!p || len > max)
        return -1;
    memcpy(out, p, len);
    ed_bk_valid = 0;                     /* (the staging RAM is not a LIST's snapshot any more) */
    return (int32_t)len;
}

uint32_t FEL(object_put)(uint32_t id, const uint8_t *data, uint32_t len)
{
    uint32_t rc;
    if (id > 7u || len > sizeof proj_tmp)
        return 1;
    if (transport_req == 2u) {           /* a stop asked for (a project loaded just before): as the next */
        seq_stop();                      /* audio block would, before Sloop refuses this as busy (rc 3) */
        transport_req = 0;
        song.rec = 0;
        rec_wait = 0;
    }
    memcpy(ED_BK_RAW, data, len);
    ed_bk_id = (uint8_t)id;
    ed_bk_len = len;
    ed_bk_pos = len;
    ed_bk_crc = st_crc32(ED_BK_RAW, len);
    rc = ed_bk_commit();
    ed_bk_put = 0;
    ed_bk_valid = 0;
    return rc;
}
