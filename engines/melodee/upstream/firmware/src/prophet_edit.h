/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
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
static const param_desc_t P5_TUNE_DISPLAY[2]={PD("FREQ A",F_SEMI,-12,36,0),PD("FREQ B",F_SEMI,-12,36,0)};
static const param_desc_t P5_STORE[4]={PD("SLOT",F_INT,1,UP_SLOTS,1),PD("STORE",F_INT,0,1,0),PD("SEND",F_INT,0,1,0),PD("INIT",F_INT,0,1,0)};
static int p5_keyed_tune(const track_t *t,uint32_t id)
{
    return id==P5_FREQ_A || (id==P5_FREQ_B && p5_patch_of(t)->raw[P5_KEY_B]);
}
/* Panel tuning is in audible semitone detents. Native SysEx/field edits keep
 * their raw range; merely viewing an imported odd/upper value never changes it. */
static int32_t p5_panel_raw(const track_t *t,uint32_t id,int32_t value)
{
    return p5_keyed_tune(t,id)?(value+12)*2:value-(id==P5_BEND?1:0);
}
/* Conservative signal-path hints, as on CZ-1: dim controls whose required
 * waveform, destination or release switch is off. They remain editable.
 * This describes dependencies, not an audibility threshold or MIDI presence. */
static int p5_ed_active(const track_t *t,uint32_t id)
{
    const uint8_t *p=p5_patch_of(t)->raw;
    /* A track matrix can open a mixer level or supply extra modulation.
     * Leave hints conservative while any such dynamic route is enabled. */
    for(uint32_t k=0;k<4u;k++)if(t->p[P_M1SRC+3u*k]&&t->p[P_M1DST+3u*k]&&t->p[P_M1AMT+3u*k])return 1;
    if((id==P5_UNISON_COUNT||id==P5_UNISON_DETUNE||id==P5_RETRIGGER) &&
       t->p[P_VOICE]!=(p[P5_UNISON]?V_UNISON:V_POLY))return 1;
    int a=clamp(p[P5_LEVEL_A]+t->p[P_E6],0,127)>0;
    int ap=a&&p[P5_PULSE_A];
    int b=clamp(p[P5_LEVEL_B]+t->p[P_E7],0,127)>0;
    int poly=p[P5_POLY_FREQ]||p[P5_POLY_PW]||p[P5_POLY_FILTER];
    int source=p[P5_POLY_ENV]||p[P5_POLY_B];
    int wheel=p[P5_WHEEL_FREQ_A]||p[P5_WHEEL_FREQ_B]||p[P5_WHEEL_PW_A]||p[P5_WHEEL_PW_B]||p[P5_WHEEL_FILTER];
    int depth=p[P5_LFO_INITIAL]||t->mw||p[P5_PRESS_LFO];
    int wave=p[P5_LFO_SAW]||p[P5_LFO_TRI]||p[P5_LFO_PULSE];
    int filter_env=p[P5_ENV_FILTER]||(p[P5_POLY_ENV]&&poly);
    switch(id){
    case P5_PW_A:return ap;
    case P5_PW_B:return p[P5_PULSE_B]&&(b||(p[P5_POLY_B]&&poly));
    case P5_FREQ_B:case P5_FINE_B:case P5_LOW_B:case P5_KEY_B:
        return b || (p[P5_POLY_B]&&poly) || (p[P5_SYNC]&&a);
    case P5_LFO_RATE:return wheel&&depth&&wave&&p[P5_WHEEL_MIX]<127u;
    case P5_LFO_SAW:case P5_LFO_TRI:case P5_LFO_PULSE:
        return wheel&&depth&&p[P5_WHEEL_MIX]<127u;
    case P5_LFO_INITIAL:return wheel&&(wave||p[P5_WHEEL_MIX]);
    case P5_WHEEL_MIX:return wheel&&depth;
    case P5_WHEEL_PW_A:return ap;
    case P5_WHEEL_PW_B:return p[P5_PULSE_B]&&(b||(p[P5_POLY_B]&&poly));
    case P5_POLY_ENV:case P5_POLY_B:return poly;
    case P5_POLY_FREQ:case P5_POLY_FILTER:return source;
    case P5_POLY_PW:return source&&ap;
    case P5_VEL_FILTER:return p[P5_ENV_FILTER]!=0;
    case P5_ATTACK_FILTER:case P5_DECAY_FILTER:case P5_SUSTAIN_FILTER:return filter_env;
    case P5_RELEASE_FILTER:return filter_env&&p[P5_RELEASE_ON];
    case P5_RELEASE_AMP:return p[P5_RELEASE_ON]!=0;
    case P5_UNISON_COUNT:case P5_RETRIGGER:return p[P5_UNISON]!=0;
    case P5_UNISON_DETUNE:return p[P5_UNISON]&&p5_cap(t)>1u;
    default:return 1;
    }
}
static void p5_edit_value(track_t *t,uint32_t id,int32_t value)
{
    if(id>=NELEM(P5_PANEL)||!P5_PANEL[id].label)return;
    p5_patch_t *p=p5_patch_of(t);p->raw[id]=(uint8_t)clamp(value,P5_PANEL[id].min,P5_PANEL[id].max);
    if(id==P5_UNISON || id==P5_UNISON_COUNT || id==P5_UNISON_DETUNE || id==P5_RETRIGGER)p5_track_accept(t);
}
