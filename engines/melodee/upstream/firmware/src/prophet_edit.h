/* SPDX-License-Identifier: GPL-3.0-only */
/* Native panel descriptors. Editing one field leaves all opaque bytes intact. */
static const char *const P5_FILTER_NAMES[]={"SSI","CURTIS"};
static const char *const P5_TRACK_NAMES[]={"OFF","HALF","FULL"};
static const char *const P5_ASSIGN_NAMES[]={"LOW","LOW RET","LAST","LAST RET"};
static int16_t p5_cell[4],p5_store_slot=1;
#define P5_K(n,l) [n]=PD(l,F_INT,0,127,0)   /* stored knobs span 0..127 */
#define P5_B(n,l) [n]=PE(l,N_ONOFF,0)
static const param_desc_t P5_PANEL[88]={
    P5_K(P5_FREQ_A,"FREQ A"),P5_K(P5_FREQ_B,"FREQ B"),P5_K(P5_FINE_B,"FINE B"),
    P5_B(P5_SAW_A,"SAW A"),P5_B(P5_PULSE_A,"PULSE A"),P5_B(P5_SAW_B,"SAW B"),
    P5_B(P5_TRI_B,"TRI B"),P5_B(P5_PULSE_B,"PULSE B"),P5_K(P5_PW_A,"WIDTH A"),P5_K(P5_PW_B,"WIDTH B"),
    P5_B(P5_SYNC,"SYNC"),P5_B(P5_LOW_B,"LOW B"),P5_B(P5_KEY_B,"KEY B"),P5_K(P5_GLIDE,"GLIDE"),
    P5_K(P5_LEVEL_A,"OSC A"),P5_K(P5_LEVEL_B,"OSC B"),P5_K(P5_NOISE,"NOISE"),
    P5_K(P5_CUTOFF,"CUTOFF"),P5_K(P5_RESONANCE,"RES"),[P5_KEY_FILTER]=PE("KEY",P5_TRACK_NAMES,0),
    [P5_FILTER_REV]=PE("MODEL",P5_FILTER_NAMES,1),P5_K(P5_LFO_RATE,"RATE"),P5_K(P5_LFO_INITIAL,"INITIAL"),
    P5_B(P5_LFO_SAW,"SAW"),P5_B(P5_LFO_TRI,"TRI"),P5_B(P5_LFO_PULSE,"SQUARE"),P5_K(P5_WHEEL_MIX,"N MIX"),
    P5_B(P5_WHEEL_FREQ_A,"FREQ A"),P5_B(P5_WHEEL_FREQ_B,"FREQ B"),P5_B(P5_WHEEL_PW_A,"PW A"),P5_B(P5_WHEEL_PW_B,"PW B"),P5_B(P5_WHEEL_FILTER,"FILTER"),
    P5_K(P5_POLY_ENV,"ENV AMT"),P5_K(P5_POLY_B,"OSC B"),P5_B(P5_POLY_FREQ,"FREQ A"),P5_B(P5_POLY_PW,"PW A"),P5_B(P5_POLY_FILTER,"FILTER"),
    P5_K(P5_VINTAGE,"VINTAGE"),P5_B(P5_PRESS_FILTER,"AT FLT"),P5_B(P5_PRESS_LFO,"AT LFO"),
    P5_K(P5_ENV_FILTER,"ENV AMT"),P5_B(P5_VEL_FILTER,"VEL FLT"),P5_B(P5_VEL_AMP,"VEL AMP"),
    P5_K(P5_ATTACK_FILTER,"ATK"),P5_K(P5_ATTACK_AMP,"ATK"),P5_K(P5_DECAY_FILTER,"DEC"),P5_K(P5_DECAY_AMP,"DEC"),
    P5_K(P5_SUSTAIN_FILTER,"SUS"),P5_K(P5_SUSTAIN_AMP,"SUS"),P5_K(P5_RELEASE_FILTER,"REL"),P5_K(P5_RELEASE_AMP,"REL"),
    P5_B(P5_RELEASE_ON,"RELEASE"),P5_B(P5_UNISON,"UNISON"),[P5_UNISON_COUNT]=PD("VOICES",F_INT,1,5,5),
    [P5_UNISON_DETUNE]=PD("DETUNE",F_INT,0,7,0),[P5_BEND]=PD("BEND",F_INT,0,11,0),
    [P5_RETRIGGER]=PE("PRIORITY",P5_ASSIGN_NAMES,0)
};
#undef P5_K
#undef P5_B
static const param_desc_t P5_BEND_DISPLAY=PD("BEND",F_INT,1,12,1);
static const param_desc_t P5_STORE[4]={PD("SLOT",F_INT,1,128,1),PD("STORE",F_INT,0,1,0),PD("SEND",F_INT,0,1,0),PD("INIT",F_INT,0,1,0)};
static void p5_edit_value(track_t *t,uint32_t id,int32_t value)
{
    if(id>=NELEM(P5_PANEL)||!P5_PANEL[id].label)return;
    p5_patch_t *p=p5_patch_of(t);p->raw[id]=(uint8_t)clamp(value,P5_PANEL[id].min,P5_PANEL[id].max);
    if(id==P5_UNISON || id==P5_UNISON_COUNT || id==P5_UNISON_DETUNE || id==P5_RETRIGGER)p5_track_accept(t);
}
