/* melodee_core.c -- Melodee 0.13 (engines/melodee/upstream, GPL-3.0-only, Kerem Kilic, from
 * Felucca by Leo Kuroshita / Hügelton Instruments), the whole firmware, as a library the plugin
 * can drive: a virtual FM-1 running Melodee, with its sound, sequencer, screen, front panel,
 * projects, user presets, CZ and FM6 tone banks and editor protocol. Built as
 * engines/felucca/felucca_core.c builds Felucca, behind the same API (felucca_core.h); what
 * differs is said where it does.
 *
 * Melodee is built as one compilation unit whose state is all file-level statics, so
 * one compiled copy is one synth. This file is compiled several times (CMakeLists.txt,
 * MELODEE_COPIES), each copy with its own FEL_PREFIX, so each plugin instance gets a
 * copy of its own: no state is shared or swapped.
 *
 * The firmware's own sources are included unchanged, in the order melodee.c includes them,
 * the way its host tests (upstream/tests/hostsim.c, editor_test.c) build them. What touches hardware is replaced below: the panel's buttons, keys and knobs and
 * the screen are the plugin's, and the flash is RAM. Never built: the update and boot
 * loader paths (ota.c, the boot loader's request): nothing here acts on them.
 *
 * A copy is given to one instance after another. To start each from the state the
 * program started with (no voices, tails or sequencer positions left from the last
 * one), every writable variable of the copy (some of them static inside
 * functions) is placed in sections of its own (FEL_BSS_SECTION, FEL_DATA_SECTION),
 * whose image is kept from the start and put back by FEL(restore). That needs Clang's
 * section pragma; CMakeLists.txt builds these engines only with Clang. Where the sections start
 * and stop comes from the linker (macOS, Linux), or on Windows, whose linker gives no such
 * symbols, from a variable in a section sorted before them and one sorted after: its linker
 * puts "felb3$a", "felb3$m" and "felb3$z" together in that order, as one section. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>   /* Melodee's code past libc.c uses the C library's memcpy and memset */

#ifndef FEL_PREFIX
#error "FEL_PREFIX names this copy (felucca_core.h)"
#endif
#include "felucca_core_api.h"

/* the firmware's globals that are not static: one per copy (in one unit, so renaming
 * persist_t's member "panel" along with them changes nothing) */
#define bootguard FEL(bootguard)
#define panel FEL(panel)
#define settings FEL(settings)
#define proj_slot FEL(proj_slot)

#if !defined(__clang__) || !defined(FEL_BSS_SECTION)
#error "Melodee's copies need Clang and their sections (CMakeLists.txt)"
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
#define MELODEE_OTA 1                    /* the editor's SysEx plumbing in usb.c (not ota.c: never built) */
#define MELODEE_FLASH 1                  /* projects, user presets, the CZ and FM6 banks in "flash" (RAM below) */
#define MELODEE_USB_AUDIO 0              /* the FM-1's USB audio device: the plugin is the audio */
#define MELODEE_CDC 0
#define MELODEE_UART 1                   /* (its MIDI parser: the plugin's MIDI comes in through it too) */
#define MELODEE_VERSION "v0.13"
#define memset FEL(memset)
#define memcpy FEL(memcpy)
#define memcmp FEL(memcmp)
#include "melodee_tables.h"
#include "../upstream/firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
/* the screen: gfx.c draws into it (melodee.c includes gfx.c before core.h; lcd.c is the device's) */
static uint16_t host_screen[240 * 240];   /* RGB565, as the LCD takes it (big endian) */
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
#include "../upstream/firmware/src/params.c"
#include "../upstream/firmware/src/mod.c"
#include "../upstream/firmware/src/voice.c"
#include "../upstream/firmware/src/slicer.c"
#include "../upstream/firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../upstream/firmware/src/usb.c"
/* what goes out (the editor's replies and pushes): usb.c's queue holds 64 packets, less than one
 * reply; while it waits for room (ota_idle), its packets move on to this one, which the plugin
 * takes between audio blocks (FEL(midi_out)) */
#define FEL_OUTQ 8192u
static uint32_t fel_out[FEL_OUTQ], fel_out_r, fel_out_w;
static void fel_out_drain(void)   /* full (nobody takes them): the oldest go, the sender never waits */
{
    while (so_r != so_w) {
        if (fel_out_w - fel_out_r >= FEL_OUTQ)
            fel_out_r++;
        fel_out[fel_out_w++ % FEL_OUTQ] = sx_out_q[so_r++ % SXQ];
    }
}
/* time that passes while the main loop waits for something the device's interrupts would do
 * (the clock is the audio's, which cannot run meanwhile): counted on, so fm1_ms never goes back */
static uint32_t fel_wait_ms;
static uint32_t ota_now_ms(void) { return fm1_ms; }   /* usb.c's SysEx sender waits on these */
static void ota_idle(void)
{
    fel_out_drain();
    if (so_w - so_r >= SXQ) {            /* still full: its 200 ms run out */
        fel_wait_ms++;
        fm1_ms++;
    }
}
#include "../upstream/firmware/src/midi_uart.c"
#include "../upstream/firmware/src/song_chain.c"
#include "../upstream/firmware/src/seq.c"


/* ---- the hardware, as Melodee's host tests replace it (tests/hostsim.c, editor_test.c) ----------------------- */
#define FM1_NCOL 11u
/* key id at (physical column, packed row bit), -1 = none: hal/fm1_input.h's, for the LEDs
 * (ui_input.c led_pos_init finds each key's LED by it) */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
/* lit, and two dimmer planes (hal/fm1_input.h): 0 the keys shown dim (a layout, a guide), 1 the
 * buttons' idle glow; read by FEL(leds) */
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[2][FM1_NCOL], fm1_led_dim_mask[2];
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = host_enc[e % 7u]; host_enc[e % 7u] = 0; return s; }
/* fed while the main loop waits for the audio side: ed_flash_stop waits for the transport to
 * stop, which the next audio block would do. Do it here, as Melodee's editor test does, and let
 * the time pass */
static void seq_stop(void);
static void fm1_wdt_feed(void)
{
    if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    fel_wait_ms++;
    fm1_ms++;
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#define SCOPE_N 512u                     /* audio.c's (not built): the HOME oscilloscope, fed in FEL(render) */
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static struct { uint32_t stage, page, home, ui_frames; } melodee_dbg;
#include "../upstream/firmware/src/panel.c"
#include "../upstream/firmware/src/ui.c"
#include "../upstream/firmware/src/icons.c"
#include "../upstream/firmware/src/ui_graph.c"
#include "../upstream/firmware/src/ui_draw.c"
/* HOME > PANEL calibrates the device's button matrix: the plugin's panel has none (its buttons
 * go by label), and the setup waits for real buttons, holding up the audio. It says so instead. */
static void fel_panel_setup(void) { ui_message("PANEL: ON THE FM-1"); }
#define panel_setup() fel_panel_setup()
#include "../upstream/firmware/src/ui_menu.c"
#undef panel_setup
#include "../upstream/firmware/src/ui_input.c"
#include "../upstream/firmware/src/ui_layer.c"

/* the flash: the storage objects' sectors in RAM, as the 1 MiB part the firmware expects. Melodee
 * keeps more than Felucca (storage.c: settings, the legacy project slots, four bank projects of five
 * sectors and a two-sector extension per copy, eight CZ banks, user preset banks, the FM6 bank and
 * extension, two native tone pools, 0.13's five PROPHET banks; two copies of each: 108 sectors in
 * its map): a sector takes RAM when first written; one never written reads erased */
#define FEL_NSECT 108u
static uint32_t sect_addr[FEL_NSECT];    /* address + 1; 0: free */
static uint8_t sect_data[FEL_NSECT][4096];
static uint8_t *sect_find(uint32_t off)
{
    uint32_t base = off & ~4095u, i;
    for (i = 0; i < FEL_NSECT; i++)
        if (sect_addr[i] == base + 1u)
            return sect_data[i] + (off - base);
    return 0;
}
static uint8_t *sect(uint32_t off)
{
    uint32_t base = off & ~4095u, i;
    uint8_t *p = sect_find(off);
    if (p)
        return p;
    for (i = 0; i < FEL_NSECT; i++)
        if (!sect_addr[i]) {
            sect_addr[i] = base + 1u;
            memset(sect_data[i], 0xFF, 4096);
            return sect_data[i] + (off - base);
        }
    return 0;
}
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)   /* (a bank project is read five sectors at once) */
{
    uint8_t *d = (uint8_t *)dst;
    while (n) {
        const uint32_t k = n < 4096u - (off & 4095u) ? n : 4096u - (off & 4095u);
        const uint8_t *p = sect_find(off);
        if (p)
            memcpy(d, p, k);
        else
            memset(d, 0xFF, k);          /* never written: erased */
        d += k; off += k; n -= k;
    }
    return 0;
}
static int st_erase(uint32_t off) { uint8_t *p = sect(off); if (!p) return -8; memset(p, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n) {
        const uint32_t k = n < 4096u - (off & 4095u) ? n : 4096u - (off & 4095u);
        uint8_t *p = sect(off);
        if (!p)
            return -8;
        memcpy(p, s, k);
        s += k; off += k; n -= k;
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
static int fl_erase4k(uint32_t off, uint32_t *took) { (void)off; *took = 0; return -1; }   /* no user samples yet */
static int fl_write(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
/* editor_backup.c's pointer to a stored project is only where its reads start: they go through
 * st_read_range, here from the RAM sectors (FEL(object_get) reads the same way) */
#define ED_BK_FLASH_PTR(off) ((const uint8_t *)ED_BK_RAW)
#include "../upstream/firmware/src/storage.c"
#include "../upstream/firmware/src/upreset.c"
#include "../upstream/firmware/src/project.c"
#include "../upstream/firmware/src/cz_store.c"
#include "../upstream/firmware/src/fm6_store.c"
#include "../upstream/firmware/src/editor.c"

/* ---- from main.c (not built: the device's boot) --------------------------------------------- */
/* power-on: the parts with their default sounds (TRK_DEF); the sequencers empty */
static void melodee_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    pattern_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    cz_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);            /* a default pattern, not the user's */
        pat_last[i] = TRK_DEF[i][2];
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

/* the plugin's own per-instance state, in the copy's sections with Melodee's (so it goes with
 * it when an instance's state is saved and put back): the audio rendered (the clock), when the
 * main loop last ran, and when the editor was last served */
static uint64_t fel_frames;
static uint32_t fel_main_ms;
static uint64_t fel_served_at;

#undef __attribute__                     /* Melodee's code is done: attributes mean something again */
static uint8_t fel_bss_marker;          /* make sure both sections exist */
static uint8_t fel_data_marker = 1;
#ifdef FEL_BSS_LAST                      /* Windows: where they stop */
FEL_SECTIONS(FEL_BSS_LAST, FEL_DATA_LAST)
__attribute__((used)) static uint8_t fel_bss_stop;
__attribute__((used)) static uint8_t fel_data_stop = 1;
#endif
#pragma clang section bss = "" data = ""

/* ---- the copy's state, as the program started ----------------------------------------------- */
#ifndef FEL_BSS_LAST
extern uint8_t fel_bss_start __asm(FEL_BSS_START);
extern uint8_t fel_bss_stop __asm(FEL_BSS_STOP);
extern uint8_t fel_data_start __asm(FEL_DATA_START);
extern uint8_t fel_data_stop __asm(FEL_DATA_STOP);
#endif
static uint8_t *fel_pristine;            /* outside the sections: kept across restores */

/* a bound's address, hidden from the optimizer: to it, each bound is a 1-byte variable that
 * nothing may be read or written beyond */
static uint8_t *fel_at(uint8_t *p)
{
    __asm__ volatile("" : "+r"(p));
    return p;
}
#define FEL_NB ((size_t)(fel_at(&fel_bss_stop) - fel_at(&fel_bss_start)))
#define FEL_ND ((size_t)(fel_at(&fel_data_stop) - fel_at(&fel_data_start)))

void *malloc(size_t);

/* never inlined, and a compiler barrier after: the section bounds are 1-byte symbols to
 * the optimizer, which must not move reads of the state across the copy */
__attribute__((noinline)) void FEL(restore)(void)
{
    const size_t nb = FEL_NB, nd = FEL_ND;
    (void)fel_bss_marker; (void)fel_data_marker;
    if (!fel_pristine) {                 /* the first time: nothing has run yet, keep the image */
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

/* an instance's whole state out of the copy, and back (state_bytes of it): an instance that
 * shares this copy with others is put back before it plays. Only into this same copy: the state
 * holds pointers into the copy's own variables and tables */
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
    if (id >= P_E0) {   /* as params.c's track_desc: an engine's mode-dependent label and names first */
        const engine_t *e = ENGINES[trk[track].eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(&trk[track], id - P_E0) : 0;
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
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

/* the clock: fm1_ms and the tick counter follow the audio rendered, as on the device they follow
 * its timer; the main loop runs about every 16 ms of it (main.c: ~60 UI frames a second) */

static void fel_clock(void)
{
    const uint64_t us = fel_frames * 1000000u / FS + (uint64_t)fel_wait_ms * 1000u;
    fm1_ms = (uint32_t)(us / 1000u);
    host_ticks = (uint32_t)us;
}

/* the editor protocol's waiting frame, if any. A frame that is not the editor's (M-VAVE's identity
 * query or reply, another firmware's SysEx) is left waiting by ed_service, for ota_service (the
 * updater's handshake) to take on the device; nothing here does, so it is dropped: else it would
 * hold the one frame slot and every editor request after it would be dropped (seen in the app,
 * whose MIDI input can be the FM-1's own port) */
static void fel_editor(void)
{
    ed_service();
    if (sx_ready)
        ota_frame_done();
}

/* one pass of main.c's loop, without what is the device's own (the watchdog, USB, the update and
 * boot loader requests, the LEDs and the screen, drawn when the plugin asks) */
static void fel_main_pass(void)
{
    usb.uboot_req = 0;                   /* the boot loader key or an update command: never acted on */
    usb.ota_req = 0;
    cz_service();                        /* (main.c's loop, in its order) */
    fm6_service();
    fel_editor();                        /* the editor protocol */
    ui_input();
    glo_poll();
    settings_poll();
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
    melodee_init();
    usb.config = 1;                      /* as a computer that has set the device up: the editor may reply */
}

const char *FEL(version)(void) { return MELODEE_VERSION; }   /* "v0.13": the release built here */
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
const char *FEL(preset_name)(uint32_t e, uint32_t i)
{
    /* "" for an alias (browsing skips it on the device) */
    return e < NENGINES && i < ENGINES[e]->npresets && preset_orig(ENGINES[e], i) == i ? ENGINES[e]->presets[i].name : "";
}

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
/* three globals are the device's settings, not the song's (params.c's global page): A4 (tuning_a4),
 * BOOT and the DRUM channel; set, they are saved as its GLO page saves them (ui_input.c) */
int32_t FEL(global_get)(uint32_t id)
{
    if (id == G_A4)
        return tuning_a4;
    if (id == G_BOOT)
        return settings_boot;
    if (id == G_DRUMCH)
        return settings_drumch;
    return id < G_COUNT ? song.g[id] : 0;
}
void FEL(global_set)(uint32_t id, int32_t v)
{
    if (id >= G_COUNT || GP[id].max <= GP[id].min)
        return;
    v = clamp(v, GP[id].min, GP[id].max);
    if (id == G_A4 || id == G_BOOT || id == G_DRUMCH) {
        if (id == G_A4)
            tuning_a4 = (int16_t)v;
        else if (id == G_BOOT)
            settings_boot = (uint8_t)v;
        else
            settings_drumch = (uint8_t)v;
        settings_save();
        return;
    }
    song.g[id] = (int16_t)v;
}

uint32_t FEL(engine_of)(uint32_t track) { return track < NTRK ? trk[track].eng_req : 0u; }
uint32_t FEL(preset_of)(uint32_t track) { return track < NTRK ? trk[track].preset : 0u; }
void FEL(set_engine)(uint32_t track, uint32_t e) { if (track < NPART) set_engine_of(&trk[track], e); }
void FEL(apply_preset)(uint32_t track, uint32_t pi) { if (track < NPART) apply_preset_to(&trk[track], pi); }

/* the engines one can pick, in the order Melodee shows them (never DIGITAL's reserved 1) */
uint32_t FEL(engines_shown)(void) { return NENG_SHOWN; }
uint32_t FEL(engine_shown)(uint32_t n) { return eng_vis(n); }

/* FM6's patch of a track: the 155-byte DX7 single-voice layout (VCED) */
uint32_t FEL(fm6_engine)(void) { return ENGI_FM6; }
void FEL(fm6_patch_get)(uint32_t track, uint8_t *v155)
{
    if (track < NTRK)
        FEL(memcpy)(v155, fm6_patch[track], FP_SIZE);
}
void FEL(fm6_patch_set)(uint32_t track, const uint8_t *v155)   /* as editor_fm6.c's FM6_PUT to a track */
{
    if (track < NTRK) {
        fm6_set_patch(track, v155);
        ui.force = 1;
    }
}

void FEL(midi)(uint32_t pkt) { midi_in_event(pkt); }

/* the editor protocol now, between audio blocks: a request just given gets its reply. Melodee
 * holds one frame at a time (usb.c drops another while one waits): FEL(service_ready) says
 * whether a new one would be taken, after serving what waited. Without audio rendered
 * meanwhile, its clock moves on by what its editor needs to push changes (5 ms between
 * passes, 20 ms between pushes of one value), as time would on the device. */
void FEL(service)(void)
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
}   /* as a USB-MIDI packet in: notes, clock, SysEx (the editor) */

void FEL(render)(int32_t *out, uint32_t frames)   /* interleaved stereo, frames a multiple of CTL */
{
    uint32_t i;
    for (i = 0; i + CTL <= frames; i += CTL) {
        if (fm1_ms - fel_main_ms >= 16u) {
            fel_main_ms = fm1_ms;
            fel_main_pass();
        }
        mix_block(out + 2u * i, CTL);
        {   /* the HOME screen's oscilloscope, as audio.c's audio_block feeds it: every other left sample */
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

void FEL(button)(uint32_t b, int down)   /* by label (B_*): through the panel's calibration, as the matrix */
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

void FEL(knob)(uint32_t role, int32_t steps)   /* by role (EN_*), clockwise +, as the panel turns it */
{
    if (role < NE)
        host_enc[panel.enc[role] % 7u] += steps * panel.dir[role];
}

/* the screen, after drawing what changed: 240 x 240 RGB565, big endian as the LCD takes it */
void FEL(draw)(uint16_t *screen)
{
    ui_leds();                           /* (as main.c's frame: the LEDs, then the screen) */
    ui_draw();
    if (screen)
        memcpy(screen, host_screen, sizeof host_screen);
}

/* ---- MIDI and SysEx out (the editor's replies and pushes): USB-MIDI packets ----------------------- */
uint32_t FEL(midi_out)(uint32_t *pkts, uint32_t max)
{
    uint32_t n = 0;
    fel_out_drain();
    while (fel_out_r != fel_out_w && n < max)
        pkts[n++] = fel_out[fel_out_r++ % FEL_OUTQ];
    return n;
}

/* ---- the selected part: what the keys, the pages and the editor's track commands act on ---- */
uint32_t FEL(selected)(void) { return song.sel; }
void FEL(select)(uint32_t t)   /* as ALGORITHM on the device (and the editor's TRACK) */
{
    if (t < NTRK && t != song.sel)
        track_select(t);
}

/* ---- the transport ------------------------------------------------------------------------------ */
void FEL(transport)(int play) { transport_req = play ? 1u : 2u; }   /* as PLAY; the next block acts on it */
int FEL(playing)(void) { return song.playing != 0u; }
/* the step a track plays now (0-based, within its length), -1 while stopped: the playhead */
int32_t FEL(step_of)(uint32_t track)
{
    uint32_t len;
    if (track >= NTRK || !song.playing)
        return -1;
    len = trk[track].p[P_SLEN] > 0 ? (uint32_t)trk[track].p[P_SLEN] : 1u;
    return (int32_t)(trk[track].seq_idx % len);
}
uint32_t FEL(armed)(void) { return song.rec; }   /* live recording armed: a bit per track */
/* the track's scale as 12 bits from its ROOT (seq.c), its drum lanes (8 on any track; their names
 * as its KIT plays them when its engine is DRUM, else the DRUM KIT's) */
uint32_t FEL(scale_mask)(uint32_t track) { return track < NTRK ? scale_mask(&trk[track]) : 0xFFFu; }

/* the LEDs as ui_leds last set them: 14 buttons by label, the 27 keys, then PLAY's green LED;
 * each 0 dark, 1 glowing (the idle glow), 2 dim (a key the layout shows), 3 lit */
uint32_t FEL(leds)(uint8_t *out, uint32_t max)
{
    uint32_t i, n = 0;
    for (i = 0; i < NB + 27u + 1u && n < max; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB), p, r, v = 0;
        if (i == NB + 27u)
            v = (fm1_led[8] >> 1) & 1u ? 3u : 0u;   /* (Felucca's green LED, column 8 bit 1: Melodee lights PLAY's own instead, so dark) */
        else
            for (p = 0; p < FM1_NCOL; p++)
                for (r = 1; r < 5u; r++)
                    if (FM1_KEYMAP[r][p] == (int8_t)id)
                        v = (fm1_led[p] >> r) & 1u ? 3u : (fm1_led_dim[0][p] >> r) & 1u ? 2u : (fm1_led_dim[1][p] >> r) & 1u ? 1u : 0u;
        out[n++] = (uint8_t)v;
    }
    return n;
}
uint32_t FEL(nlanes)(uint32_t track) { (void)track; return NLANE; }
/* SLOOP's live sections and song (sloop_core.c): Melodee has none (its song chain is the editor's
 * SONG command) */
int32_t FEL(arr_do)(uint32_t op, uint32_t arg) { (void)op; (void)arg; return -1; }
uint32_t FEL(arr_state)(uint8_t *out, uint32_t max) { (void)out; (void)max; return 0; }
int32_t FEL(arr_chain)(const uint8_t *e, uint32_t n, int loop) { (void)e; (void)n; (void)loop; return -1; }
const char *FEL(lane_name)(uint32_t track, uint32_t lane)
{
    static const track_t plain;          /* (KIT 0: the DRUM KIT's names) */
    if (track >= NTRK || lane >= NLANE)
        return "";
    /* its KIT only on a DRUM track: on another engine E1 is that engine's parameter */
    return drum_lane_name(trk[track].eng_req == ENGI_DRUM ? &trk[track] : &plain, lane);
}

/* ---- the device's stored objects, as the editor's full backup carries them (editor_backup.c):
 * 0 the music now (all 32 pattern banks), 1 the settings (and the template), 2..5 the project
 * slots, 6, 7, 17 and 18 the user preset banks, 8 retired (always empty), 9..16 the CZ banks,
 * 19..22 the FM6 and native tone pools. Without stopping the transport: the plugin calls these
 * between audio blocks. ------------------------------------------------------------------------- */
uint32_t FEL(object_max)(void) { return ED_BK_MAX; }

/* the object's bytes into out (at most max); its length (0: empty), or -1 */
int32_t FEL(object_get)(uint32_t id, uint8_t *out, uint32_t max)
{
    const uint8_t *p;
    uint32_t len;
    if (id == 0u) {                      /* as ed_bk_capture, without its stop */
        project_capture(&proj_scratch);
        if (!bank_pack(ED_BK_RAW, &proj_scratch, 1))
            return -1;
        ++proj_wire_gen;
    } else if (id == 1u) {
        ed_bk_set.p = persist_saved;
        settings_export(&ed_bk_set.p);
        ed_bk_set.t = tmpl;
        ed_bk_set_len = sizeof ed_bk_set.p + (template_used() ? sizeof ed_bk_set.t : 0u);
    } else if (id >= ED_BK_N) {
        return -1;
    }
    p = ed_bk_object(id, &len);
    if (!p || len > max)
        return -1;
    if (ed_bk_source_obj < OBJ_COUNT) {  /* a stored project: from its sectors */
        if (st_read_range(ed_bk_source_obj, ed_bk_source_copy, 0, out, len))
            return -1;
    } else {
        memcpy(out, p, len);
    }
    return (int32_t)len;
}

/* an object back (len 0: an empty slot), validated and converted as a backup restore does it;
 * editor_backup.c's rc: 0 ok, 1 invalid, 2 validation failed, 4 storage */
uint32_t FEL(object_put)(uint32_t id, const uint8_t *data, uint32_t len)
{
    uint32_t rc;
    if (id >= ED_BK_N || len > ED_BK_MAX)
        return 1;
    memcpy(ED_BK_RAW, data, len);
    ed_bk_id = (uint8_t)id;
    ed_bk_len = len;
    ed_bk_pos = len;
    ed_bk_crc = st_crc32(ED_BK_RAW, len);
    rc = ed_bk_commit();
    ed_bk_put = 0;
    ed_bk_valid = 0;
    ++proj_wire_gen;
    if (id == 1u && rc == 0u)
        glo_restore();                   /* the kept CLK TUNE MIDI ROUT, as the device applies them at power-on
                                          * (else glo_poll would save the instance's own back over them) */
    return rc;
}
