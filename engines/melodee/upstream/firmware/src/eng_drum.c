/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: synthesized TR-808 circuits, eight mono lanes per part, with GM note mapping.
 * Retriggers reuse a lane's voice; closed hats choke open hats. Key release does not end a hit.
 * KIT retains stored value 4 for existing patches. All older custom kits now use the 808.
 * Parameters and output processing remain live while the hit rings. No recorded material. */
/* Lane and GM roles shared by the grid and the synthesized 808. */
enum { DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_RIM, DV_BELL, DV_NLANE };
enum { DVT_PUNCH, DVT_ROUND, DVT_SNARE, DVT_CLAP, DVT_HATC, DVT_HATO, DVT_TOM, DVT_CONGA,
       DVT_RIM, DVT_CLAVE, DVT_BELL, DVT_CYM, DVT_COUNT };
#include "x0x/fastmath.h"
#include "drum_808.c"
#include "x0x/drum909.c"

enum { DK_808 = 4, DK_909 = 5 }; /* Retain the stored value of existing 808 patches. */
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};

typedef struct {
    dr8_t r;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    uint8_t sound;
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
} drum_lane_t;

static drum_lane_t *drum_kit_part(uint32_t part);  /* engines.c eng_state: the part's DV_NLANE lanes */

/* Allocate a kit only for its selected model; changing KIT invalidates old owners. */
typedef struct {
    drum909_t d;
    uint8_t owner[D9_NUM], ready;
    float *nz;
} drum909_part_t;
static drum909_part_t *drum909_part(uint32_t part);
static uint32_t drum_lane(uint32_t note);
static uint32_t drum_gm(const int16_t *p,uint32_t note,int32_t *st);
static int32_t drum_event_pitch;
static uint32_t drum_event_length;
static int drum_is909(const track_t *t) { return t->p[P_E0] == DK_909; }
static uint32_t drum_bytes(const track_t *t) { return drum_is909(t) ? sizeof(drum909_part_t) : sizeof(drum_lane_t)*DV_NLANE; }
static drum909_part_t *drum909_of(const track_t *t)
{
    drum909_part_t *k = drum909_part((uint32_t)(t-trk));
    if (k && !k->ready) { drum909_init(&k->d); k->ready=1; }
    return k;
}
static uint32_t drum909_ins(uint32_t note)
{
    if (note==49 || note==52 || note==55 || note==57) return D9_CR;
    if (note==51 || note==53 || note==59 || note==56) return D9_RD;
    switch (drum_lane(note)) {
    case DV_KICK:return D9_BD; case DV_SNARE:return D9_SD; case DV_CLAP:return D9_CP;
    case DV_HATC:return D9_CH; case DV_HATO:return D9_OH; case DV_RIM:return D9_RS;
    default:return note<45?D9_LT:note<48?D9_MT:D9_HT;
    }
}
static const uint8_t DRUM_SOUND_NOTE[16]={36,38,41,45,50,37,39,42,46,49,51,64,61,60,75,70};
static const char *const DRUM_SOUND_NAME[16]={"KICK","SNARE","TOM L","TOM M","TOM H","RIM","CLAP","HAT C","HAT O","CRASH","RIDE","CONGL","CONGM","CONGH","CLAVE","MARAC"};
static uint32_t drum_sound_count(const track_t *t){return drum_is909(t)?D9_NUM:16u;}
static uint32_t drum_sound_note(const track_t *t,uint32_t sound){return !drum_is909(t) && sound==D9_RD?56u:DRUM_SOUND_NOTE[sound];}
static const char *drum_sound_name(const track_t *t,uint32_t sound){return !drum_is909(t) && sound==D9_RD?"BELL":!drum_is909(t) && sound==D9_CR?"CYMB":DRUM_SOUND_NAME[sound];}
static uint32_t drum_sound_of(const track_t *t,uint32_t note)
{
    if(drum_is909(t))return drum909_ins(note);
    static const uint8_t map[DR_N]={D9_BD,D9_SD,D9_LT,D9_MT,D9_HT,11,12,13,D9_RS,14,D9_CP,15,D9_RD,D9_CR,D9_OH,D9_CH};
    int32_t st;uint32_t role=drum_gm(t->p,note,&st);return map[dr8_ins(note,role)];
}
static const char *drum_character(uint32_t sound)
{return sound==D9_SD?"SNAPPY":sound<=D9_HT?"ATTACK":"TONE";}
static void drum808_values(const track_t *t,uint32_t sound,int16_t *p)
{
    memcpy(p,t->p,sizeof t->p);const int8_t *c=drum_patch[t-trk].c[sound];
    p[P_E3]=(int16_t)clamp(p[P_E3]+c[1],0,127);
    if(sound==D9_SD)p[P_E4]=(int16_t)clamp(p[P_E4]+c[2],0,127);
}
static void drum909_values(track_t *t,drum909_part_t *k,uint32_t ins,int32_t pitch,uint32_t vel)
{
    drum909_t *d=&k->d;const int8_t *c=drum_patch[t-trk].c[ins];
    for(int j=0;j<d9_nspec[ins];j++){
        const d9_pspec_t *s=&d9_specs[ins][j];int32_t value=s->ui.def;
        if(s->field==D9_F_ATTACK || s->field==D9_F_SNAPPY || (ins>=D9_RS && s->field==D9_F_TUNE))value+=c[2]+(s->field==D9_F_SNAPPY?0:t->p[P_E2]-64);
        if(s->field==D9_F_SNAPPY)value+=t->p[P_E4]-64;
        if(s->field==D9_F_TONE)value+=t->p[P_E2]-64;
        drum909_set(d,(int)ins,j,value);
    }
    float ratio=fm_exp2f((float)(c[0]+pitch)/12.0f+(float)(t->p[P_E1]-64)*3.0f/192.0f);
    float decay=fm_exp2f((float)(c[1]+t->p[P_E3]-64)/24.0f);
    if(ins<=D9_HT){
        static const float dec[5]={1380.0f,320.0f,1700.0f,1050.0f,1100.0f};
        d->bt[ins].decay=dec[ins]*decay;
        if(ins==D9_SD){d->bt[ins].tune*=ratio;d->bt[ins].tune2=325.0f*ratio;d->bt[ins].noise_decay=1200.0f*fm_exp2f((float)(t->p[P_E2]-64)/24.0f)*decay;}
        else if(ins!=D9_BD)d->bt[ins].tune*=ratio;
    }else if(ins==D9_RS){d->rim.tune*=ratio;d->rim.tune2=220.0f*ratio;d->rim.decay=40.0f*decay;d9_rim_retune(&d->rim);}
    else if(ins==D9_CP){d->clap.tune*=ratio;d->clap.tail_decay*=decay;d9_clap_retune(&d->clap);}
    else {d->smp[ins-D9_CH].pitch*=ratio;d->smp[ins-D9_CH].decay*=decay;d9_smp_rate(&d->smp[ins-D9_CH]);}
    d->accent=2.0f*(float)dr8_level(vel,t->p[P_E5])/(float)dr8_level(vel,100);
    if(ins==D9_BD && t->p[P_E6])d->bt[0].attack*=0.5f;
    drum909_trigger(d,(int)ins,(float)vel/127.0f);
    if(ins==D9_SD){d->bt[1].decay=340.0f*decay;d9_env_exp(&d->bt[1].amp,0.00001f,d->bt[1].decay*D9_MS);d->bt[1].mute=d9_ceil((d->bt[1].noise_decay+d->bt[1].decay+60.0f)*D9_MS);}
    if(ins==D9_RS){d->rim.decay=200.0f*decay;d9_env_exp(&d->rim.amp,0.00001f,d->rim.decay*D9_MS);d->rim.mute=d9_ceil((d->rim.decay+20.0f)*D9_MS);}
    if(ins==D9_BD){d->bt[0].bd_base*=ratio;d->bt[0].bd_df*=ratio*(t->p[P_E6]?0.5f:1.0f);d->bt[0].pitch.v*=ratio;}
}

static int drum909_live(const drum909_part_t *k, uint32_t ins)
{
    if(ins<=D9_SD)return k->d.bt[ins].mute>0;
    if(ins<=D9_HT)return k->d.tom[ins-D9_LT].mute>0;
    if(ins==D9_RS)return k->d.rim.mute>0;
    if(ins==D9_CP)return k->d.clap.mute>0;
    return k->d.smp[ins-D9_CH].mute>0;
}
static void drum_block(track_t *t)
{
    if(drum_is909(t)) { drum909_part_t *k=drum909_of(t); if(k)k->nz=d9_noise_block(&k->d,CTL); }
}

static const char *const N_DRUM_KIT[] = {"808", "808", "808", "808", "808", "909"};
static const char *const N_DRUM_KICK[] = {"PUNCH", "ROUND"};

/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*) and its semitones; KICK picks the kick, the 808 selects its instrument from the GM note */
static uint32_t drum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)DRUM_GM[n - 35u][0];
    *st = DRUM_GM[n - 35u][1];
    if (t == DVT_PUNCH && p[P_E6] > 0)
        t = DVT_ROUND;
    return t;
}

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > STEP on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a GM note strikes */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "HATCL", "HATOP", "TOM", "RIM", "BELL"};
    if(drum_is909(t) && (l&(NLANE-1u))==DV_BELL)return "RIDE";
    return N[l & (NLANE - 1u)];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    if(drum_is909(t) && (l&(NLANE-1u))==DV_BELL)return "RD";
    return N[l & (NLANE - 1u)];
}

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(s->note[i]);
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return s->flags & SF_ACCENT ? step_lanes(s) : s->hit ? s->acc & step_lanes(s) : 0u; }

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    if (!s->hit) s->acc = 0;                       /* recorded synth gate is not a drum accent */
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = drum_lane(s->note[i]);
        if (s->note[i] == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? drum_kit_part((uint32_t)(t - trk)) : 0;
}

static int drum_live(const drum_lane_t *L) { return L->r.on; }

static void drum_choke(drum_lane_t *K)                   /* a closed hat chokes the open one */
{
    drum_lane_t *o = &K[DV_HATO];
    if (o->r.on && o->r.ins == DR_OH)
        o->r.choke = 1;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    if (drum_is909(t) || v->s[2] == DK_909) return 0;
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    if(drum_is909(t)) {
        drum909_part_t *k=drum909_of(t); uint32_t ins=drum909_ins(note), o=k?k->owner[ins]:0;
        return o && o<=NVOICE && t->v[o-1].active && t->v[o-1].s[2]==DK_909 ? &t->v[o-1]:0;
    }
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

/* lanes hit since the UI last looked, a bit per lane (drum_lane) and track: Stage flashes their names (ui_stage.c) */
static volatile uint8_t drum_flash[NTRK];
static void drum_note_on(track_t *t, voice_t *v)
{
    drum_flash[(uint32_t)(t - trk) % NTRK] |= (uint8_t)(1u << drum_lane(v->note));
    v->s[3]=(int32_t)drum_event_length; v->s[4]=drum_event_pitch; v->s[5]=drum_event_length!=0;
    if(drum_is909(t)) {
        drum909_part_t *k=drum909_of(t); if(!k)return;
        uint32_t ins=drum909_ins(v->note), o=(uint32_t)(v-t->v)+1;
        for(uint32_t j=0;j<D9_NUM;j++)if(k->owner[j]==o)k->owner[j]=0;
        k->owner[ins]=(uint8_t)o; v->s[0]=drum_lane(v->note); v->s[1]=(int32_t)ins; v->s[2]=DK_909;
        drum909_values(t,k,ins,drum_event_pitch,v->vel); v->env_out=32767; return;
    }
    v->s[2]=DK_808;
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = drum_gm(t->p, v->note, &st);
    lane = DV_TYPE_LANE[role];
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    L->role = (uint8_t)role;
    uint32_t ins = dr8_ins(v->note, role);
    L->sound=(uint8_t)drum_sound_of(t,v->note);
    int16_t local[P_COUNT];drum808_values(t,L->sound,local);
    L->r.decay=drum_patch[t-trk].c[L->sound][1];
    L->r.pitch=(drum_patch[t-trk].c[L->sound][0]+drum_event_pitch)*16;
    dr8_hit(&L->r, ins, v->vel, local);
    v->env_out = mulq15(127 * 258, dr8_level(v->vel, t->p[P_E5]));
    if (lane == DV_HATC && ins == DR_CH)
        drum_choke(K);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here. The 808's hits have their
 * velocity in their trigger level: the voice's own (voice.c: x velocity) taken out again, ACC's put in */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    if(v->s[5] && v->s[3]<=0){v->active=v->gate=v->stage=0;v->env=0;return 0;}
    if(drum_is909(t) && v->s[2]==DK_909) {
        drum909_part_t *k=drum909_of(t); uint32_t ins=(uint32_t)v->s[1];
        if(k && ins<D9_NUM && k->owner[ins]==(uint32_t)(v-t->v)+1 && drum909_live(k,ins)) { v->env=1<<24; return 32767*127/(v->vel?v->vel:1); }
        v->active=v->gate=v->stage=0; v->env=0; return 0;
    }
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || !drum_live(L)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return (int32_t)(((int64_t)(32767 * 127 / (v->vel ? v->vel : 1)) * dr8_level(v->vel, t->p[P_E5])) >> 15);
}

/* Optional processing has bounded helpers; the neutral output loop stays small. */
static __attribute__((noinline)) int drum_source(track_t *t,voice_t *v,drum_lane_t *L,int32_t *y,uint32_t n)
{
    uint32_t i;
    if(drum_is909(t)) {
        drum909_part_t *k=drum909_of(t); if(!k || !k->nz)return 0;
        float f[CTL]={0}; drum909_render_voice(&k->d,v->s[1],k->nz,f,(int)n);
        for(i=0;i<n;i++)y[i]=(int32_t)(fm_clampf(f[i],-2.0f,2.0f)*12000.0f);
    } else {int16_t local[P_COUNT];drum808_values(t,L->sound,local);dr8_run(&L->r,local,y,n);}
    return 1;
}
static __attribute__((noinline)) void drum_limit(voice_t *v,int32_t *y,uint32_t n)
{
    uint32_t i;
    if(v->s[5]){
        uint32_t left=(uint32_t)v->s[3];
        for(i=0;i<n;i++)y[i]=i>=left?0:left-i<64u?y[i]*(int32_t)(left-i)/64:y[i];
        v->s[3]=left>n?(int32_t)(left-n):0;
    }
}
static __attribute__((noinline)) void drum_color(drum_lane_t *L,int32_t value,int32_t *y,uint32_t n)
{
    int32_t k=dr_lp1(dr_exp(4000u,value,24));
    for(uint32_t i=0;i<n;i++){L->r.color+=(int32_t)(((int64_t)(y[i]-L->r.color)*k)>>14);y[i]=value<0?L->r.color:y[i]+((y[i]-L->r.color)*value>>6);}
}
static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drum_lane_t *L = drum_lane_of(t, v);
    int32_t y[CTL], drv = p[P_E7], g = 0, mk = 0;
    uint32_t i;
    if (!L && !drum_is909(t))
        return;
    if (n > CTL)
        n = CTL;
    if(!drum_source(t,v,L,y,n))return;
    if(v->s[5])drum_limit(v,y,n);
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    uint32_t sound=drum_is909(t)?(uint32_t)v->s[1]:L->sound;
    const int8_t *c=drum_patch[t-trk].c[sound];
    if(!drum_is909(t) && sound!=D9_SD && c[2])drum_color(L,c[2],y,n);
    int32_t lv = clamp(p[P_LN0 + ((uint32_t)v->s[0] & (DV_NLANE - 1u))], 0, 127)*(127-c[3])/127;
    /* Fold lane gain into the block's amplitude ramp: no extra per-sample multiply. */
    vmod_t lane_mod;
    if (lv != 127) {
        int32_t lane_gain = lv * lv * 32767 / (127 * 127);
        lane_mod = *m;
        lane_mod.amp0 = mulq15(m->amp0, lane_gain);
        lane_mod.amp1 = mulq15(m->amp1, lane_gain);
        m = &lane_mod;
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        s = voice_amp(soft_knee(s + (s >> 2), 24000), m, i);
        out[i] += s << 1;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV}; every kit suggests the BEAT pattern (GM notes) */
static const preset_t DRUM_PRESETS[] = {
    {"808 KIT", {DK_808, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20)},
    {"909 KIT", {DK_909, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20)},
};

static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", F_ENUM, DK_808, DK_909, DK_808, N_DRUM_KIT, 0},
        {"TUNE", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 64, 0, 0},
        {"DECY", F_PCT, 0, 127, 64, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 100, 0, 0},
        {"KICK", F_ENUM, 0, 1, 0, N_DRUM_KICK, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .block = drum_block,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
};
