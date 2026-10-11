/* SPDX-License-Identifier: GPL-3.0-only */
/* CZ-1 is a separate engine: native Casio tone data, never PHASE knob conversion.
 * Envelope state and note reset utilities are shared with the phase family. */
#include "cz_native.c"
#include "melodee_cz1.h"            /* Casio's 64 CZ-1 preset tones (tools/gen_cz1_factory.py) */
/* A factory preset: Casio's tone at its BANK A..D / PTCH 1..16 (the CZ-1's A-1 .. H-8, as the default
 * banks hold them, cz_bank.c), INIT TONE the init voice. Restored and imported tones bypass this hook */
static void cz_factory_loaded(track_t *t)
{
    uint32_t k = (uint32_t)t->p[P_E0] * 16u + (uint32_t)t->p[P_E1] - 1u;
    if (t->eng_req != ENGI_CZ || t->p[P_E7] != CZ_NATIVE)
        return;
    if (t->p[P_E1] > 0 && k < CZ_FACTORY_N)
        memcpy(cz_patch[(uint32_t)(t - trk) % NTRK].raw, CZ_FACTORY[k], CZ_BYTES);
    else
        cz_patch_init(cz_patch[(uint32_t)(t - trk) % NTRK].raw);
}
static int (*cz_user_bank_read)(uint32_t,uint32_t,uint8_t *);
static uint16_t cz_user_pick[NTRK];
static const char *const N_CZ_BANK[]={"A","B","C","D","E","F","G","H"};
/* COMP (the CZ TOOLS page): one track's tone as it was before its first edit, swapped with the
 * edited one. One copy (pool space): the first edit takes it, a tone loaded on that track drops it */
static cz_patch_t cz_compare __attribute__((section(".pool")));
static uint8_t cz_compare_tr;                    /* the track + 1 whose tone cz_compare holds; 0 none */
static void cz_compare_drop(uint32_t tr){if(cz_compare_tr==tr%NTRK+1u)cz_compare_tr=0;}
static void cz_compare_take(uint32_t tr)
{
    if(cz_compare_tr==tr%NTRK+1u)return;
    cz_compare=cz_patch[tr%NTRK];cz_compare_tr=(uint8_t)(tr%NTRK+1u);
}
static void cz_track_accept(track_t *t){uint32_t tr=(uint32_t)(t-trk)%NTRK;cz_user_pick[tr]=(uint16_t)(t->p[P_E0]*17+t->p[P_E1]);cz_compare_drop(tr);}
static void cz_bank_poll(void);
static int cz_native_done(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    const uint8_t *b = cz_patch[(uint32_t)(t - trk) % NTRK].raw;
    uint32_t ls = b[0] & 3u;
    uint32_t a = ls == 1u ? 1u : 0u, z = ls >= 2u ? 1u : a;
    return c->eg[a][2].stage > (b[CZ_ENV_END[a][2]] & 7u) &&
        c->eg[z][2].stage > (b[CZ_ENV_END[ls == 2u ? 0u : z][2]] & 7u);
}
#define CZ_FACTORY_PRESET(n, bank, ptch, pat) \
    {n, {bank, ptch, 0, 0, 0, 0, 0, CZ_NATIVE}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0), PAT(pat)},
static const preset_t CZ_PRESETS[] = {
    {"INIT TONE", {0, 0, 0, 0, 0, 0, 0, CZ_NATIVE}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0), PAT(1)},
    CZ_FACTORY_PRESETS(CZ_FACTORY_PRESET)   /* Casio's, dry as the CZ-1 (no effects) */
};
static const engine_t ENG_CZ = {
    .name = "CZ-1", .page_title = {"CZ-1", "TONE"},
    .edit = {
        {"-", F_INT, 0, 7, 0, 0, 0}, {"-", F_INT, 0, 16, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"TONE", F_INT, CZ_NATIVE, CZ_NATIVE, CZ_NATIVE, 0, 0},
    },
    .presets = CZ_PRESETS, .npresets = NELEM(CZ_PRESETS),
    .ownenv = 1, .done = cz_native_done, .keep = 0x0fu,
    .note_on = phase_note_on, .render = cz_native_render,
    .knob = {P_E0, P_E1, P_E7, P_E7},
};

/* USB interrupt only collects bytes; the main loop validates and publishes. */
#define CZ_RX 296u
static uint8_t cz_rx[CZ_RX],cz_rx_on,cz_rx_req,cz_rx_ready,cz_rx_go,cz_rx_abort;
static uint16_t cz_rx_n;
static void cz_sx_byte(uint8_t b)
{
    if(b>=0xf8)return;
    if(b==0xf0){if(cz_rx_ready)return;cz_rx_on=1;cz_rx_n=0;cz_rx_req=cz_rx_go=0;}
    if(!cz_rx_on||cz_rx_ready)return;
    if((b&128)&&b!=0xf0&&b!=0xf7){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    if(cz_rx_n>=CZ_RX){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    cz_rx[cz_rx_n++]=b;
    if(cz_rx_n==2&&b!=0x44){cz_rx_on=0;return;}
    if(cz_rx_n==7 && cz_rx[2]==0 && cz_rx[3]==0 && (cz_rx[4]&0xf0)==0x70){RING_PUBLISH();cz_rx_req=1;}
    if(cz_rx_n==9&&(cz_rx[5]==0x10||cz_rx[5]==0x11)&&cz_rx[7]==cz_rx[4]&&b==0x31){RING_PUBLISH();cz_rx_go=1;}
    if(b==0xf7){cz_rx_on=0;RING_PUBLISH();cz_rx_ready=1;}
}
