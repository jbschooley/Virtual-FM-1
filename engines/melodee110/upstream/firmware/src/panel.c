/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The matrix ids are known, the printed labels are not, so
 * the mapping is a table with a best guess, overridable by HARDWARE CALIBRATION
 * (hold OCT- and OCT+ while powering on): MELODEE asks for each label in turn.
 * The learned table lives in .noinit and, with MELODEE_FLASH, in flash with
 * the settings (project.c); read it back with `fm1t memr` to bake it in. */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 (measured on hardware) */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 (printed labels) */
    {1, 1, 1, 1, 1, 1, 1},
};

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    uint32_t i, buttons = 0, encoders = 0, ok = panel.magic == PANEL_MAGIC;
    for (i = 0; ok && i < NB; i++) {
        ok = panel.btn[i] < NB && !(buttons & (1u << panel.btn[i]));
        if (ok) buttons |= 1u << panel.btn[i];
    }
    for (i = 0; ok && i < NE; i++) {
        ok = panel.enc[i] < NE && !(encoders & (1u << panel.enc[i])) &&
             (panel.dir[i] == 1 || panel.dir[i] == -1);
        if (ok) encoders |= 1u << panel.enc[i];
    }
    if (!ok)
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

/* steps of a role, + = clockwise */
static uint8_t panel_moved;
static int32_t panel_enc(uint32_t role)
{
    int32_t value=fm1_enc_take(panel.enc[role]) * panel.dir[role];
    panel_moved |= value != 0;
    return value;
}

/* Keep the retained settings layout fixed. palette: the index into UI_PALETTES (SET3 held an index
 * into the 20 palettes of earlier firmware) */
#define SETTINGS_MAGIC 0x53455434u              /* "SET4" */
#define SETTINGS_MAGIC_OLD 0x53455433u          /* "SET3" */
struct { uint32_t magic, palette, lowcut, zoom; } settings __attribute__((section(".noinit")));

static void settings_save(void);              /* project.c: flash copy (MELODEE_FLASH) */

/* HOLD (menu): how long a button is held before its layer opens (ui_input.c). Saved in the settings record's
 * retired bold field as HOLD_TAG | index; any other value there (0 or 1 from older firmware) is the default */
#define HOLD_TAG 0x484C4400u
#define HOLD_DEF 1u
static const uint16_t HOLD_MS[4] = {300, 400, 500, 600};
static uint8_t settings_hold = HOLD_DEF;

/* LIGHTS (menu): OFF the keys light only while they sound or are pressed, the buttons while engaged (as Felucca 1.0);
 * LOW .. FULL the keys that play something glow at that level, in the scale with QNT OFF, and the idle buttons at 1/4
 * (ui_input.c ui_leds). Saved in the settings' ext block */
enum { LIGHTS_OFF, LIGHTS_LOW, LIGHTS_MID, LIGHTS_HIGH, LIGHTS_FULL, LIGHTS_N };
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH", "FULL"};
static uint8_t settings_lights = LIGHTS_MID;
static uint32_t hold_from_stored(uint32_t v) { return (v & ~3u) == HOLD_TAG ? v & 3u : HOLD_DEF; }
static uint32_t hold_to_stored(uint32_t old, uint32_t i)
{
    return i % 4u != HOLD_DEF ? HOLD_TAG | (i & 3u) : old > 1u ? 0u : old;
}
static int hold_stored_ok(uint32_t v) { return v <= 1u || (v & ~3u) == HOLD_TAG; }

static void settings_init(void)
{
    if (settings.magic == SETTINGS_MAGIC_OLD && settings.palette < 20u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = palette_from_stored(settings.palette);
    }
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = UI_DEFAULT_INDEX;   /* NIGHT (default) */
        settings.lowcut = 0;
        settings.zoom = 0;                     /* large readout of the touched value: off */
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut % 3u);
}
