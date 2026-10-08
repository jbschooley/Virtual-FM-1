/* SPDX-License-Identifier: GPL-3.0-only */
/* Rev-4 wire patches. Sequential's 2025 v1.03 files use ID 0x32 and
 * 19 full packed groups (133 raw bytes). The 1.4 MIDI guide instead says
 * ID 0x31 and 128 raw bytes. Keep both explicit layouts, including every
 * opaque byte; interpreting a parameter never modifies its stored value. */
#define P5_RAW_MAX 133u
#define P5_FRAME_MAX 159u
enum {
    P5_FREQ_A, P5_FREQ_B, P5_FINE_B, P5_SAW_A, P5_PULSE_A,
    P5_SAW_B, P5_TRI_B, P5_PULSE_B, P5_PW_A, P5_PW_B,
    P5_SYNC, P5_LOW_B, P5_KEY_B, P5_GLIDE, P5_LEVEL_A, P5_LEVEL_B,
    P5_NOISE, P5_CUTOFF, P5_RESONANCE, P5_KEY_FILTER, P5_FILTER_REV,
    P5_LFO_RATE, P5_LFO_INITIAL, P5_LFO_SAW, P5_LFO_TRI, P5_LFO_PULSE,
    P5_WHEEL_MIX, P5_WHEEL_FREQ_A, P5_WHEEL_FREQ_B, P5_WHEEL_PW_A,
    P5_WHEEL_PW_B, P5_WHEEL_FILTER, P5_POLY_ENV, P5_POLY_B,
    P5_POLY_FREQ, P5_POLY_PW, P5_POLY_FILTER, P5_VINTAGE,
    P5_PRESS_FILTER, P5_PRESS_LFO, P5_ENV_FILTER, P5_VEL_FILTER,
    P5_VEL_AMP, P5_ATTACK_FILTER, P5_ATTACK_AMP, P5_DECAY_FILTER,
    P5_DECAY_AMP, P5_SUSTAIN_FILTER, P5_SUSTAIN_AMP, P5_RELEASE_FILTER,
    P5_RELEASE_AMP, P5_RELEASE_ON, P5_UNISON, P5_UNISON_COUNT,
    P5_UNISON_DETUNE,
    P5_NAME = 65, P5_NAME_LEN = 20, P5_BEND = 86, P5_RETRIGGER = 87
};
typedef struct {
    uint8_t raw[P5_RAW_MAX];
    uint8_t size, model, command, group, program;
} p5_patch_t;

static void p5_patch_init(p5_patch_t *p)
{
    memset(p, 0, sizeof *p);
    p->size = P5_RAW_MAX; p->model = 0x32; p->command = 3;
    p->raw[P5_FREQ_A] = p->raw[P5_FREQ_B] = 24;
    p->raw[P5_FINE_B] = 0;
    p->raw[P5_VINTAGE] = 0;    /* 0: stable Rev 4 .. 127: Rev 1 */
    p->raw[P5_SAW_A] = p->raw[P5_KEY_B] = 1;
    p->raw[P5_PW_A] = p->raw[P5_PW_B] = 64;
    p->raw[P5_LEVEL_A] = p->raw[P5_CUTOFF] = 127;
    p->raw[P5_FILTER_REV] = 1; /* 0: Rev1/2 (SSI); 1: Rev3 (Curtis) */
    p->raw[P5_DECAY_FILTER] = p->raw[P5_DECAY_AMP] = 60;
    p->raw[P5_SUSTAIN_FILTER] = p->raw[P5_SUSTAIN_AMP] = 127;
    p->raw[P5_RELEASE_FILTER] = p->raw[P5_RELEASE_AMP] = 35;
    p->raw[P5_RELEASE_ON] = 1;
    p->raw[P5_BEND] = 0;
    p->raw[P5_UNISON_COUNT] = 5;
    memset(p->raw + P5_NAME, ' ', P5_NAME_LEN);
    memcpy(p->raw + P5_NAME, "INIT PROPHET", 12);
}

/* Atomic on error, including malformed masks and extra/truncated bytes.
 * This is a single exact frame API; bank callers split at F7 themselves. */
static int p5_patch_decode(p5_patch_t *out, const uint8_t *f, uint32_t n)
{
    p5_patch_t p;
    uint32_t start, packed, size, pos = 0;
    if (!f || n < 5u || f[0] != 0xF0 || f[n-1u] != 0xF7 || f[1] != 1u ||
        (f[2] != 0x32u && f[2] != 0x31u) || (f[3] != 2u && f[3] != 3u)) return 0;
    start = f[3] == 2u ? 6u : 4u;
    if (n <= start || (f[3] == 2u && (f[4] > 9u || f[5] > 39u))) return 0;
    packed = n - start - 1u;
    size = packed == 152u ? 133u : packed == 147u ? 128u : 0u;
    if (!size) return 0;
    memset(&p, 0, sizeof p);
    p.size = (uint8_t)size; p.model = f[2]; p.command = f[3];
    if (p.command == 2u) { p.group = f[4]; p.program = f[5]; }
    for (uint32_t i = start; i < n - 1u;) {
        uint32_t k = size - pos < 7u ? size - pos : 7u, mask = f[i++];
        if (mask >= 128u || (mask >> k)) return 0;
        for (uint32_t j = 0; j < k; j++) {
            uint32_t b = f[i++];
            if (b >= 128u) return 0;
            p.raw[pos++] = (uint8_t)(b | (((mask >> j) & 1u) << 7));
        }
    }
    *out = p;
    return 1;
}

static uint32_t p5_patch_encode(const p5_patch_t *p, uint8_t *f, uint32_t cap)
{
    uint32_t size = p->size, n = p->command == 2u ? 6u : 4u;
    if ((size != 128u && size != 133u) || (p->model != 0x31u && p->model != 0x32u) ||
        (p->command != 2u && p->command != 3u) ||
        (p->command == 2u && (p->group > 9u || p->program > 39u)) ||
        cap < n + size + (size + 6u) / 7u + 1u) return 0;
    f[0] = 0xF0; f[1] = 1; f[2] = p->model; f[3] = p->command;
    if (p->command == 2u) { f[4] = p->group; f[5] = p->program; }
    for (uint32_t pos = 0; pos < size;) {
        uint32_t k = size - pos < 7u ? size - pos : 7u, mask = n++;
        f[mask] = 0;
        for (uint32_t j = 0; j < k; j++, pos++) {
            f[mask] |= (uint8_t)((p->raw[pos] >> 7) << j);
            f[n++] = p->raw[pos] & 127u;
        }
    }
    f[n++] = 0xF7;
    return n;
}

static int p5_patch_valid(const p5_patch_t *p)
{
    uint8_t frame[P5_FRAME_MAX];
    return p5_patch_encode(p, frame, sizeof frame) != 0;
}
static void p5_patch_name(char *name, const p5_patch_t *p)
{
    uint32_t end=0;
    for(uint32_t i=0;i<P5_NAME_LEN;i++){
        uint8_t c=p->raw[P5_NAME+i];name[i]=(char)(c>=32u&&c<=126u?c:' ');
        if(name[i]!=' ')end=i+1u;
    }
    name[end]=0;
}
