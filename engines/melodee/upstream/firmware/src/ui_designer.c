/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound designer (docs/SOUND_DESIGNER.md): a sound-only transaction over the native editors, which stay the
 * authority for every control. START is NEW SONG's key screen (rings over a large panel, the keys under it), FEATURES
 * a list page (KNOB 2 the row, KNOB 1 its value), the shaping stages the sound pages themselves: the stages as the
 * tabs under the header, a stage's pages as its dots. OCT+ / OCT- the next / previous stage, PRESETS a stage, SELECT
 * the pages, SAVE the original and back, HOME (or OCT past either end) its sheet: Keep, Save as user.., Discard.
 * No serialized IDs, project formats or DSP defaults change. Included by ui_draw.c */
enum { DS_OFF, DS_SETUP, DS_SHAPE };
enum { DG_FREE, DG_BASS, DG_PAD, DG_LEAD, DG_KEYS, DG_PLUCK, DG_FX, DG_COUNT };
enum { DT_BUILD, DT_SOURCE, DT_TONE, DT_ENV, DT_MOVE, DT_PLAY, DT_COUNT };
static const char *const DS_GOAL[] = {"FREE", "BASS", "PAD", "LEAD", "KEYS", "PLUCK", "FX"};
static const char *const DS_TAB[DT_COUNT] = {"Features", "Source", "Tone", "Env", "Motion", "Play"};
static void designer_page(void);
/* One compact snapshot swaps A (original) and B (draft). Editing is allowed
 * only on B; switching engine during setup does not touch the track. */
typedef struct {
    uint8_t eng, preset, user, native;
    int16_t p[P_COUNT];
    union {
        struct { uint8_t patch[FP_SIZE+1u], fn[FM6_NFN], on; } fm;
        cz_patch_t cz;
        p5_patch_t p5;
        drum_patch_t drum;
    } raw;
} designer_sound_t;
#define DS_ROUTES 160u
static struct {
    uint8_t track, eng, goal, current, expert, stage, at, n, compare, focus;
    uint8_t old_home, old_page, row, first;             /* row, first: FEATURES' row, its first shown */
    struct { uint8_t page, op, stage; } route[DS_ROUTES];
    designer_sound_t other;
    uint32_t sig;
} ds __attribute__((section(".pool")));

static void designer_snapshot(designer_sound_t *s)
{
    track_t *t=TSEL;uint32_t k=song.sel;
    memset(s,0,sizeof *s);
    s->eng=t->eng_req;s->preset=t->preset;s->user=t->user;s->native=t->user_native;
    memcpy(s->p,t->p,sizeof s->p);
    if(s->eng==ENGI_FM6){memcpy(s->raw.fm.patch,fm6_patch[k],sizeof s->raw.fm.patch);memcpy(s->raw.fm.fn,fm6_fn[k],FM6_NFN);s->raw.fm.on=fm6_on[k];}
    else if(s->eng==ENGI_CZ)s->raw.cz=cz_patch[k];
    else if(s->eng==ENGI_PROPHET)s->raw.p5=*p5_patch_of(t);
    else if(s->eng==ENGI_DRUM)s->raw.drum=drum_patch[k];
}
static void designer_restore(const designer_sound_t *s)
{
    track_t *t=TSEL;uint32_t k=song.sel;
    panic_req|=(uint8_t)(1u<<k);
    t->eng_req=s->eng;t->preset=s->preset;t->user=s->user;t->user_native=s->native;
    for(uint32_t i=0;i<P_COUNT;i++)if(!param_kept(i))t->p[i]=s->p[i];
    if(s->eng==ENGI_FM6){fm6_put_patch(k,s->raw.fm.patch,0);memcpy(fm6_fn[k],s->raw.fm.fn,FM6_NFN);fm6_on[k]=s->raw.fm.on;}
    else if(s->eng==ENGI_CZ){cz_patch[k]=s->raw.cz;cz_track_accept(t);}
    else if(s->eng==ENGI_PROPHET){p5_patch[k]=s->raw.p5;p5_ready[k]=1;}
    else if(s->eng==ENGI_DRUM)drum_patch[k]=s->raw.drum;
    sync_reload=1;
}
static void designer_compare(void)
{
    designer_sound_t now;
    fm1_irq_off();designer_snapshot(&now);designer_restore(&ds.other);ds.other=now;fm1_irq_on();
    ds.compare^=1u;ui.force=1;
    if(!ds.compare && ui.designer_mode==DS_SHAPE)designer_page();   /* (another engine's original showed its own page) */
}
static void designer_draft(void) { if(ds.compare)designer_compare(); }
static void designer_open(void)
{
    if(designer_on()){ui.menu=0;ui.force=1;return;}   /* (the menu over it: back to it) */
    if(chain_busy()){ui_message("STOP SONG TO DESIGN");return;}
    browse_commit();momentary_restore();pop_close();smap.on=0;
    memset(&ds,0,sizeof ds);ds.track=song.sel;ds.eng=TSEL->eng_req;
    if(!eng_ok(ds.eng))ds.eng=ENGI_FM6;
    ds.current=1;ds.old_home=ui.home;ds.old_page=ui.page;
    designer_quit=designer_close;ui.designer_mode=DS_SETUP;ui.home=0;ui.menu=0;ui.layer=ui.lock=ui.ly=0;ui.ly_t0=0;
    ui.pg_down=0;ui.force=1;ui.msg_t=0;ui.act=0;
    kb_mask=kb_lock=0;
}
static void designer_close(int discard)
{
    if(ui.designer_mode!=DS_SETUP){
        if(discard){if(!ds.compare){fm1_irq_off();designer_restore(&ds.other);fm1_irq_on();}}
        else designer_draft();
        fm1_irq_off();motion_design_pause&=(uint8_t)~(1u<<ds.track);motion_rebase(&trk[ds.track]);fm1_irq_on();
    }
    designer_quit=0;ui.designer_mode=DS_OFF;ui.home=ds.old_home;ui.page=ds.old_page;
    ui.home_t0=ui.save_t0=ui.rec_t0=ui.seq_t0=ui.oct_t0=0;
    ui.force=1;sec.ok=0;ui.pg_down=0;song.grid=0;page_entered();
}
static void designer_cz_put(uint32_t id,uint32_t v)
{
    uint8_t raw[CZ_BYTES];
    if(cz_ed_put(song.sel,id,v,raw))memcpy(cz_patch[song.sel].raw,raw,CZ_BYTES);
}
/* FEATURES, the first stage: the paths a sound is built from, as the
 * instrument's own switches (Prophet pulse, Poly-Mod, unison; FM operators;
 * CZ lines, ring and noise). ON wires an audible path and keeps an amount
 * already set; OFF opens the path again, keeping routing amounts. */
#define DS_FEAT 0xFFu                                   /* the FEATURES route (its list, not a page) */
enum { DF_OSCB, DF_PULSEA, DF_PULSEB, DF_NOISE, DF_SYNC, DF_POLY, DF_LFO, DF_FENV, DF_UNISON, DF_RELEASE, DF_VEL,
       DF_P5GLIDE, DF_OP1, DF_OP6 = DF_OP1 + 5, DF_FB, DF_FMLFO, DF_PEG, DF_LINES, DF_RING, DF_CZNOISE, DF_DETUNE,
       DF_CZVIB, DF_VOICE, DF_GLIDE, DF_SPREAD, DF_PENV, DF_VIB, DF_WOBBLE, DF_TREM, DF_ENVF, DF_COUNT, DF_NONE = 0xFF };
static const char *const DF_WORD[DF_COUNT] = {"Osc B", "Pulse A", "Pulse B", "Noise", "Sync", "Poly-Mod", "LFO",
    "Filter env", "Unison", "Release", "Velocity", "Glide", "OP1", "OP2", "OP3", "OP4", "OP5", "OP6", "Feedback", "LFO",
    "Pitch EG", "Lines", "Ring", "Noise", "Detune", "Vibrato", "Voice", "Glide", "Spread", "Pitch env", "Vibrato",
    "Wobble", "Tremolo", "Filter env"};
static const uint8_t DS_F_P5[] = {DF_OSCB, DF_PULSEA, DF_PULSEB, DF_NOISE, DF_SYNC, DF_POLY, DF_LFO, DF_FENV, DF_UNISON,
                                  DF_RELEASE, DF_VEL, DF_P5GLIDE};
static const uint8_t DS_F_FM[] = {DF_OP1, DF_OP1 + 1, DF_OP1 + 2, DF_OP1 + 3, DF_OP1 + 4, DF_OP6, DF_FB, DF_PEG, DF_FMLFO,
                                  DF_VOICE};
static const uint8_t DS_F_CZ[] = {DF_LINES, DF_RING, DF_CZNOISE, DF_DETUNE, DF_CZVIB, DF_VOICE};
static const uint8_t DS_F_ANY[] = {DF_VOICE, DF_GLIDE, DF_SPREAD, DF_PENV, DF_VIB, DF_WOBBLE, DF_TREM, DF_ENVF};
static const uint8_t *designer_feats(uint32_t *n)       /* the selected track's engine's (a drum kit: none) */
{
    uint32_t e=TSEL->eng_req;
    if(drum_track(TSEL)){*n=0;return 0;}
    *n=e==ENGI_PROPHET?sizeof DS_F_P5:e==ENGI_FM6?sizeof DS_F_FM:e==ENGI_CZ?sizeof DS_F_CZ:sizeof DS_F_ANY;
    return e==ENGI_PROPHET?DS_F_P5:e==ENGI_FM6?DS_F_FM:e==ENGI_CZ?DS_F_CZ:DS_F_ANY;
}
static int designer_feat_on(void) { return ui.designer_mode==DS_SHAPE && ds.n && ds.route[ds.at].page==DS_FEAT; }
static uint32_t designer_feat_states(uint32_t f) { return f==DF_VOICE || f==DF_LINES ? 4u : 2u; }
static int designer_p5_on(const uint8_t *q,uint32_t level,uint32_t offset,int wave)
{
    return wave && clamp(q[level]+TSEL->p[offset],0,127)>0;
}
static uint32_t designer_feat_get(uint32_t f)
{
    track_t *t=TSEL;const uint8_t *q=p5_patch_of(t)->raw,*v=fm6_patch[song.sel];
    uint8_t c[LCZ_PACKED];
    if(f>=DF_LINES && f<=DF_CZVIB)cz_ed_decode(c,cz_patch[song.sel].raw);
    if(f>=DF_OP1 && f<=DF_OP6){uint32_t k=5u-(f-DF_OP1);return fm6_op_on(t,k) && v[k*FP_OP+FP_OL];}
    switch(f){
    case DF_OSCB:return designer_p5_on(q,P5_LEVEL_B,P_E7,q[P5_SAW_B]||q[P5_TRI_B]||q[P5_PULSE_B]);
    case DF_PULSEA:return q[P5_PULSE_A]!=0;
    case DF_PULSEB:return q[P5_PULSE_B]!=0;
    case DF_NOISE:return q[P5_NOISE]!=0;
    case DF_SYNC:return q[P5_SYNC]!=0;
    case DF_POLY:return (q[P5_POLY_FREQ]||q[P5_POLY_PW]||q[P5_POLY_FILTER]) && (q[P5_POLY_ENV]||q[P5_POLY_B]);
    case DF_LFO:return (q[P5_WHEEL_FREQ_A]||q[P5_WHEEL_FREQ_B]||q[P5_WHEEL_PW_A]||q[P5_WHEEL_PW_B]||q[P5_WHEEL_FILTER]) &&
                       q[P5_LFO_INITIAL] && (q[P5_LFO_SAW]||q[P5_LFO_TRI]||q[P5_LFO_PULSE]||q[P5_WHEEL_MIX]);
    case DF_FENV:return q[P5_ENV_FILTER]!=0;
    case DF_UNISON:return q[P5_UNISON]!=0;
    case DF_RELEASE:return q[P5_RELEASE_ON]!=0;
    case DF_VEL:return q[P5_VEL_AMP]||q[P5_VEL_FILTER];
    case DF_P5GLIDE:return q[P5_GLIDE]!=0;
    case DF_FB:return v[FP_FB]!=0;
    case DF_FMLFO:return (v[FP_LPMD] && v[FP_LPMS]) || v[FP_LAMD];
    case DF_PEG:return v[FP_PL1]!=50 || v[FP_PL1+1]!=50 || v[FP_PL1+2]!=50 || v[FP_PL1+3]!=50;
    case DF_LINES:return c[LCZ_LINE];
    case DF_RING:return c[LCZ_LINE] && c[LCZ_MOD]==1;
    case DF_CZNOISE:return c[LCZ_LINE] && c[LCZ_MOD]==2;
    case DF_DETUNE:return c[LCZ_LINE] && (c[LCZ_FINE]||c[LCZ_NOTE]||c[LCZ_DOCT]);
    case DF_CZVIB:return c[LCZ_VDEP]!=0;
    case DF_VOICE:return (uint32_t)clamp(t->p[P_VOICE],0,3);
    case DF_GLIDE:return t->p[P_GLIDE]!=0;
    case DF_SPREAD:return t->p[P_SPRD]!=0;
    case DF_PENV:return t->p[P_ED_PIT]!=0;
    case DF_VIB:return t->p[P_LD_PIT]!=0;
    case DF_WOBBLE:return t->p[P_LD_FLT]!=0;
    case DF_TREM:return t->p[P_LD_AMP]!=0;
    case DF_ENVF:return t->p[P_ED_FLT]!=0;
    }
    return 0;
}
static uint8_t designer_amount(uint8_t now,uint32_t on,uint8_t def) { return on?(now?now:def):0; }
/* A sweep from a general engine's wide-open filter is lost above it: switching
 * one on brings a cutoff (its EDIT knob CUT..) above three quarters to the middle */
static void designer_sweep_room(void)
{
    const engine_t *e=ENGINES[TSEL->eng_req%NENGINES];
    for(uint32_t i=0;i<8u;i++)if(e->edit[i].label && str_prefix(e->edit[i].label,"CUT")){
        if(TSEL->p[P_E0+i]>e->edit[i].min+(e->edit[i].max-e->edit[i].min)*3/4)TSEL->p[P_E0+i]=(int16_t)((e->edit[i].min+e->edit[i].max)/2);
        return;
    }
}
static int16_t designer_track(int16_t now,uint32_t on,int16_t def) { return on?(now?now:def):0; }
/* A path's state s into the patch (the audio code reads it: interrupts off) */
static void designer_feat_set(uint32_t f,uint32_t s)
{
    track_t *t=TSEL;uint8_t *q=p5_patch_of(t)->raw;
    if(f<=DF_P5GLIDE){
        switch(f){
        case DF_PULSEB:q[P5_PULSE_B]=(uint8_t)s;if(!s)break;/* fall through: B into the mix */
        case DF_OSCB:
            if(!s){q[P5_LEVEL_B]=0;if(t->p[P_E7]>0)t->p[P_E7]=0;break;}
            if(!q[P5_LEVEL_B])q[P5_LEVEL_B]=80;
            if(q[P5_LEVEL_B]+t->p[P_E7]<=0)t->p[P_E7]=0;
            if(!q[P5_SAW_B] && !q[P5_TRI_B] && !q[P5_PULSE_B])q[P5_SAW_B]=1;
            break;
        case DF_PULSEA:
            q[P5_PULSE_A]=(uint8_t)s;
            if(s && !q[P5_LEVEL_A])q[P5_LEVEL_A]=100;
            if(s && q[P5_LEVEL_A]+t->p[P_E6]<=0)t->p[P_E6]=0;
            break;
        case DF_NOISE:q[P5_NOISE]=designer_amount(q[P5_NOISE],s,60);break;
        case DF_SYNC:
            q[P5_SYNC]=(uint8_t)s;
            if(s && !designer_p5_on(q,P5_LEVEL_A,P_E6,q[P5_SAW_A]||q[P5_PULSE_A])){
                if(!q[P5_SAW_A] && !q[P5_PULSE_A])q[P5_SAW_A]=1;
                if(!q[P5_LEVEL_A])q[P5_LEVEL_A]=100;
                if(q[P5_LEVEL_A]+t->p[P_E6]<=0)t->p[P_E6]=0;
            }
            break;
        case DF_POLY:
            if(!s){q[P5_POLY_FREQ]=q[P5_POLY_PW]=q[P5_POLY_FILTER]=0;break;}
            if(!q[P5_POLY_FREQ] && !q[P5_POLY_PW] && !q[P5_POLY_FILTER])q[P5_POLY_FILTER]=1;
            if(!q[P5_POLY_ENV] && !q[P5_POLY_B])q[P5_POLY_ENV]=35;
            if(!q[P5_SAW_B] && !q[P5_PULSE_B] && !q[P5_TRI_B])q[P5_SAW_B]=1;
            break;
        case DF_LFO:
            if(!s){q[P5_WHEEL_FREQ_A]=q[P5_WHEEL_FREQ_B]=q[P5_WHEEL_PW_A]=q[P5_WHEEL_PW_B]=q[P5_WHEEL_FILTER]=0;break;}
            if(!q[P5_LFO_SAW] && !q[P5_LFO_TRI] && !q[P5_LFO_PULSE])q[P5_LFO_TRI]=1;
            if(!q[P5_WHEEL_FREQ_A] && !q[P5_WHEEL_FREQ_B] && !q[P5_WHEEL_PW_A] && !q[P5_WHEEL_PW_B] && !q[P5_WHEEL_FILTER])q[P5_WHEEL_FILTER]=1;
            if(!q[P5_LFO_INITIAL])q[P5_LFO_INITIAL]=25;
            if(!q[P5_LFO_RATE])q[P5_LFO_RATE]=50;
            if(q[P5_WHEEL_MIX]==127)q[P5_WHEEL_MIX]=0;
            break;
        case DF_FENV:q[P5_ENV_FILTER]=designer_amount(q[P5_ENV_FILTER],s,40);break;
        case DF_UNISON:
            q[P5_UNISON]=(uint8_t)s;
            if(s && q[P5_UNISON_COUNT]==1)q[P5_UNISON_COUNT]=3;
            p5_track_accept(t);
            break;
        case DF_RELEASE:q[P5_RELEASE_ON]=(uint8_t)s;break;
        case DF_VEL:q[P5_VEL_AMP]=q[P5_VEL_FILTER]=(uint8_t)s;break;
        case DF_P5GLIDE:q[P5_GLIDE]=designer_amount(q[P5_GLIDE],s,30);break;
        }
        return;
    }
    if(f>=DF_OP1 && f<=DF_PEG){
        uint8_t v[FP_SIZE+1u];memcpy(v,fm6_patch[song.sel],sizeof v);
        if(f>=DF_OP1 && f<=DF_OP6){
            uint32_t k=5u-(f-DF_OP1);
            if(s && !v[k*FP_OP+FP_OL])v[k*FP_OP+FP_OL]=(uint8_t)((fm6_carriers(v[FP_ALG])>>k)&1u?90:60);
            fm6_on[song.sel]=(uint8_t)(s?fm6_on[song.sel]|1u<<k:fm6_on[song.sel]&~(1u<<k));
        }else if(f==DF_FB)v[FP_FB]=designer_amount(v[FP_FB],s,5);
        else if(f==DF_FMLFO){
            if(s){if(!v[FP_LPMD])v[FP_LPMD]=10;if(!v[FP_LPMS])v[FP_LPMS]=3;if(!v[FP_LFS])v[FP_LFS]=35;}
            else v[FP_LPMD]=v[FP_LAMD]=0;
        }else if(f==DF_PEG){
            /* from L4 up to L1 at once, back by R2 to the centre: a blip as the note starts */
            v[FP_PL1]=(uint8_t)(s?58:50);v[FP_PL1+1]=v[FP_PL1+2]=v[FP_PL1+3]=50;
            if(s){v[FP_PR1]=99;v[FP_PR1+1]=60;}
        }
        fm6_put_patch(song.sel,v,0);
        return;
    }
    if(f>=DF_LINES && f<=DF_CZVIB){
        uint8_t c[LCZ_PACKED];cz_ed_decode(c,cz_patch[song.sel].raw);
        if(f==DF_LINES){designer_cz_put(LCZ_LINE,s);return;}
        if(f==DF_CZVIB){designer_cz_put(LCZ_VDEP,designer_amount(c[LCZ_VDEP],s,15));return;}
        if(s && !c[LCZ_LINE])designer_cz_put(LCZ_LINE,2);   /* line 2 needs a second line: 1+2' */
        if(f==DF_RING || f==DF_CZNOISE){
            uint32_t m=f==DF_RING?1u:2u;
            if(s)designer_cz_put(LCZ_MOD,m);else if(c[LCZ_MOD]==m)designer_cz_put(LCZ_MOD,0);
        }else if(!s){designer_cz_put(LCZ_FINE,0);designer_cz_put(LCZ_NOTE,0);designer_cz_put(LCZ_DOCT,0);}
        else if(!c[LCZ_FINE] && !c[LCZ_NOTE] && !c[LCZ_DOCT])designer_cz_put(LCZ_FINE,8);
        return;
    }
    switch(f){
    case DF_VOICE:t->p[P_VOICE]=(int16_t)s;break;
    case DF_GLIDE:t->p[P_GLIDE]=designer_track(t->p[P_GLIDE],s,40);break;
    case DF_SPREAD:t->p[P_SPRD]=designer_track(t->p[P_SPRD],s,70);break;
    case DF_PENV:t->p[P_ED_PIT]=designer_track(t->p[P_ED_PIT],s,8);break;
    case DF_VIB:t->p[P_LD_PIT]=designer_track(t->p[P_LD_PIT],s,4);break;
    case DF_WOBBLE:t->p[P_LD_FLT]=designer_track(t->p[P_LD_FLT],s,24);if(s)designer_sweep_room();break;
    case DF_TREM:t->p[P_LD_AMP]=designer_track(t->p[P_LD_AMP],s,40);break;
    case DF_ENVF:t->p[P_ED_FLT]=designer_track(t->p[P_ED_FLT],s,32);if(s)designer_sweep_room();break;
    }
}
/* A build that cannot sound: said when it is made (a header notice) */
static const char *designer_silent(void);
/* FEATURES: KNOB 2 the row, KNOB 1 its value */
static void designer_feat_knob(uint32_t k,int32_t s)
{
    uint32_t n,f,st;const uint8_t *fs=designer_feats(&n);
    if(!n)return;
    if(k==1u){ds.row=(uint8_t)clamp((int32_t)ds.row+s,0,(int32_t)n-1);return;}
    if(k)return;
    f=fs[ds.row%n];st=designer_feat_get(f);
    st=designer_feat_states(f)>2u?(uint32_t)clamp((int32_t)st+(s>0?1:-1),0,(int32_t)designer_feat_states(f)-1):s>0;
    fm1_irq_off();designer_feat_set(f,st);fm1_irq_on();
    sync_reload=1;
    if(designer_silent())ui_message(designer_silent());
}
static const char *designer_feat_text(uint32_t f,uint32_t s)
{
    static const char *const VOICE[]={"Poly","Mono","Legato","Unison"},*const LINES[]={"Line 1","Line 2","1+2'","1+1'"};
    return f==DF_VOICE?VOICE[s&3u]:f==DF_LINES?LINES[s&3u]:s?"On":"Off";
}
static const char *designer_silent(void)
{
    track_t *t=TSEL;
    if(t->eng_req==ENGI_PROPHET){
        const uint8_t *q=p5_patch_of(t)->raw;
        if(!designer_p5_on(q,P5_LEVEL_A,P_E6,q[P5_SAW_A]||q[P5_PULSE_A]) && !designer_feat_get(DF_OSCB) && !q[P5_NOISE])
            return "MIXER SILENT";
    }else if(t->eng_req==ENGI_FM6){
        uint32_t c=fm6_carriers(fm6_patch[song.sel][FP_ALG]);
        for(uint32_t k=0;k<6u;k++)if(((c>>k)&1u) && fm6_op_on(t,k) && fm6_patch[song.sel][k*FP_OP+FP_OL])return 0;
        return "NO CARRIER ON";
    }
    return 0;
}
/* Recipes are starting gestures, applied only when INIT was chosen. Current
 * sound means byte-for-byte current sound, regardless of the chosen goal. */
static void designer_recipe(void)
{
    track_t *t=TSEL;uint32_t g=ds.goal;
    if(g==DG_FREE || drum_track(t))return;
    int pad=g==DG_PAD, shorty=g==DG_PLUCK||g==DG_KEYS;
    t->p[P_VOICE]=(g==DG_BASS||g==DG_LEAD)?V_MONO:V_POLY;
    if(t->eng_req==ENGI_PROPHET){
        uint8_t *p=p5_patch_of(t)->raw;
        p[P5_ATTACK_AMP]=pad?70:0;p[P5_DECAY_AMP]=shorty?60:45;
        p[P5_SUSTAIN_AMP]=shorty?0:110;p[P5_RELEASE_AMP]=pad?85:shorty?45:20;p[P5_RELEASE_ON]=1;
        p[P5_ATTACK_FILTER]=pad?55:0;p[P5_DECAY_FILTER]=65;p[P5_SUSTAIN_FILTER]=pad?90:0;
        p[P5_RELEASE_FILTER]=pad?70:30;p[P5_ENV_FILTER]=shorty||g==DG_BASS?55:20;
        p[P5_CUTOFF]=g==DG_BASS?55:pad?80:100;
        if(pad){p[P5_LEVEL_B]=80;p[P5_SAW_B]=1;p[P5_FINE_B]=68;}
        if(g==DG_LEAD){p[P5_UNISON]=1;p[P5_UNISON_COUNT]=3;p[P5_UNISON_DETUNE]=2;}
        if(g==DG_FX){p[P5_POLY_B]=25;p[P5_POLY_FREQ]=1;p[P5_SAW_B]=1;p[P5_LOW_B]=1;p[P5_KEY_B]=0;}
        p5_track_accept(t);
    }else if(t->eng_req==ENGI_FM6){
        uint8_t v[FP_SIZE+1u];memcpy(v,fm6_patch[song.sel],sizeof v);
        v[FP_ALG]=4; /* three independent two-operator pairs */
        for(uint32_t op=0;op<6;op++){
            uint8_t *p=v+op*FP_OP;
            p[FP_OL]=(op&1u)?(op==5?99:pad?78:0):(op==4?shorty?72:45:pad?35:0);
            p[FP_FC]=(op&1u)?1:shorty?3:1;
            p[FP_R1]=pad?45:99;p[FP_R1+1]=shorty?60:75;p[FP_R1+2]=65;p[FP_R1+3]=pad?42:shorty?60:80;
            p[FP_L1]=99;p[FP_L1+1]=shorty?65:99;p[FP_L1+2]=shorty?0:99;p[FP_L1+3]=0;
        }
        if(g==DG_FX){v[FP_FB]=5;v[4*FP_OP+FP_FC]=7;v[4*FP_OP+FP_OL]=85;}
        fm6_put_patch(song.sel,v,0);fm6_on[song.sel]=FM6_ON_ALL;
    }else if(t->eng_req==ENGI_CZ){
        for(uint32_t l=0;l<2;l++){
            uint32_t b=LCZ_EBASE(l,2);
            designer_cz_put(b+16,8); /* no sustain while resetting the end point */
            designer_cz_put(b+17,3);
            designer_cz_put(b,pad?45:99);designer_cz_put(b+1,shorty?65:80);
            designer_cz_put(b+2,70);designer_cz_put(b+3,pad?40:75);
            designer_cz_put(b+8,99);designer_cz_put(b+9,shorty?70:99);designer_cz_put(b+10,shorty?0:99);
            designer_cz_put(b+16,shorty?8:2);
        }
        if(g==DG_FX){designer_cz_put(LCZ_LINE,2);designer_cz_put(LCZ_MOD,1);designer_cz_put(LCZ_FINE,30);}
    }else{
        t->p[P_ATK]=pad?75:0;t->p[P_DEC]=shorty?70:45;t->p[P_SUS]=shorty?0:110;t->p[P_REL]=pad?85:shorty?40:20;
        if(g==DG_FX){t->p[P_LRATE]=65;t->p[P_LD_PIT]=25;t->p[P_LD_SHP]=40;}
    }
}
/* a page's stage: ENV MOD (the filter envelope's amount, velocity, release) with the envelopes */
static int designer_category(uint32_t i)
{
    const page_t *p=&PAGES[i];const char *s=p->title;
    if(p->fam==FAM_ENV || str_prefix(s,"P5 FLT ENV") || str_prefix(s,"P5 AMP ENV") || str_prefix(s,"P5 ENV MOD") ||
       str_prefix(s,"EG ") || str_prefix(s,"PITCH E") || str_prefix(s,"PITCH L") || str_prefix(s,"C1 ") || str_prefix(s,"C2 "))return DT_ENV;
    if(p->fam==FAM_LFO || str_prefix(s,"P5 LFO") || str_prefix(s,"P5 WHEEL") || str_prefix(s,"P5 POLY") ||
       str_prefix(s,"P5 MOD") || str_prefix(s,"FM LFO") || str_prefix(s,"CZ VIB"))return DT_MOVE;
    if(str_prefix(s,"VOICE") || str_prefix(s,"P5 UNI") || str_prefix(s,"P5 BEND") || str_prefix(s,"FM BEND") ||
       str_prefix(s,"FM PORT") || str_prefix(s,"FM WH") || str_prefix(s,"FM BR") || str_prefix(s,"CZ DET"))return DT_PLAY;
    if(str_prefix(s,"P5 FILTER") || str_prefix(s,"CZ WINDOW") || str_eq(s,"EDIT 2") || str_eq(s,"SCALE") || str_eq(s,"CURVE") || str_prefix(s,"DRUM MIX"))return DT_TONE;
    return DT_SOURCE;
}
static int designer_page_ok(uint32_t i)
{
    const page_t *p=&PAGES[i];
    if(!page_visible(i) || (p->fam!=FAM_EDIT && p->fam!=FAM_ENV && p->fam!=FAM_LFO))return 0;
    if(p->graph==GR_FMSTORE || p->graph==GR_CZTOOLS || p->scope==SC_P5STORE || p->graph==GR_MOTION)return 0;
    /* Track mixer/performance parameters and the drum lanes' notes are outside a tone. */
    if(p->scope==SC_GLOBAL || str_prefix(p->title,"LANES"))return 0;
    if(TSEL->eng_req==ENGI_PROPHET && (str_eq(p->title,"VOICE") || str_eq(p->title,"VOICE 2")))return 0;
    if(TSEL->eng_req==ENGI_PROPHET && p->scope==SC_ENGINE)return 0;
    if(drum_track(TSEL) && (str_prefix(p->title,"VOICE") || p->fam==FAM_ENV))return 0;
    return 1;
}
/* MAIN pages: the engine's own first. The Prophet, FM6 and CZ-1 have LFOs of their own (the track LFO and the
 * matrix: ALL), a drum kit its sounds; FM6's macros (EDIT 1 / 2) repeat its own pages */
static int designer_guided(uint32_t i)
{
    static const char *const skip[]={"P5 BEND","FM BEND","FM PORTA","FM WH/FT","FM BR/AT","SCALE","CURVE","VOICE 3","CZ WINDOW",
                                     "PITCH EG","PITCH LV","LFO 2","MOD"};
    const page_t *p=&PAGES[i];const char *s=p->title;uint32_t e=TSEL->eng_req;
    for(uint32_t j=0;j<NELEM(skip);j++)if(str_eq(s,skip[j]))return 0;
    if(p->scope==SC_TRACK && p->fam==FAM_LFO && (e==ENGI_PROPHET || e==ENGI_FM6 || e==ENGI_CZ || drum_track(TSEL)))return 0;
    if(e==ENGI_FM6 && p->scope==SC_ENGINE)return 0;
    if(str_prefix(s,"C1 PIT") || str_prefix(s,"C2 PIT"))return 0;
    if((str_prefix(s,"C1 ") || str_prefix(s,"C2 ")) && (str_prefix(s+7,"R5") || str_prefix(s+7,"L5")))return 0;
    return 1;
}
static int designer_in(uint32_t i,uint32_t st)
{
    return designer_page_ok(i) && designer_category(i)==(int)st && (ds.expert || designer_guided(i));
}
/* what FEATURES leaves out: an FM operator switched off, a CZ line its line mode does not play */
static int designer_live(uint32_t i)
{
    const char *s=PAGES[i].title;
    if(PAGES[i].scope!=SC_CZ1)return 1;
    if(str_prefix(s,"CZ1 ") || str_prefix(s,"C1 "))return cz_ed_active(cz_patch[song.sel].raw,LCZ_LBASE(0)+LCZ_W1);
    if(str_prefix(s,"CZ2 ") || str_prefix(s,"C2 "))return cz_ed_active(cz_patch[song.sel].raw,LCZ_LBASE(1)+LCZ_W1);
    return 1;
}
static void designer_add(uint32_t i,uint32_t op,uint32_t st)
{
    if(ds.n>=DS_ROUTES)return;
    ds.route[ds.n].page=(uint8_t)i;ds.route[ds.n].op=(uint8_t)op;ds.route[ds.n++].stage=(uint8_t)st;
}
/* FEATURES, then each stage's pages: the engine's own before the track's, an FM operator's pages together, in
 * operator order (built again whenever FEATURES is left: what it switched off is not shaped) */
static void designer_route(void)
{
    uint32_t feats,st,pass,i,j,op;
    ds.n=0;designer_feats(&feats);
    if(feats)designer_add(DS_FEAT,0,DT_BUILD);
    for(st=DT_SOURCE;st<DT_COUNT;st++)for(pass=0;pass<2u;pass++){
        int ops_done=0;
        for(i=0;i<NPAGES;i++){
            if(!designer_in(i,st) || (PAGES[i].scope==SC_TRACK)!=(int)pass)continue;
            if(PAGES[i].scope!=SC_FMOP){if(designer_live(i))designer_add(i,0,st);continue;}
            if(ops_done)continue;
            ops_done=1;
            for(op=0;op<6u;op++)if(designer_feat_get(DF_OP1+op))
                for(j=i;j<NPAGES;j++)if(PAGES[j].scope==SC_FMOP && designer_in(j,st))designer_add(j,op,st);
        }
    }
}
static void designer_page(void)
{
    if(!ds.n)return;
    ds.stage=ds.route[ds.at].stage;
    if(ds.route[ds.at].page!=DS_FEAT){ui.page=ds.route[ds.at].page;fm6_opsel=ds.route[ds.at].op;}
    else for(uint32_t i=0;i<ds.n;i++)if(ds.route[i].page!=DS_FEAT){ui.page=ds.route[i].page;break;}   /* (a page of this engine) */
    ds.focus=0;
    if(pop.on==POP_PICK)pop_close();
    page_entered();
}
static void designer_begin(void)
{
    undo_seal();fm1_irq_off();motion_restore(TSEL);motion_design_pause|=(uint8_t)(1u<<ds.track);designer_snapshot(&ds.other);fm1_irq_on();
    load_begin(TSEL,UNDO_SOUND);
    if(!ds.current || ds.eng!=TSEL->eng_req){sound_init_of(TSEL,ds.eng);fm1_irq_off();designer_recipe();fm1_irq_on();}
    load_end(TSEL);undo_seal();
    ds.eng=TSEL->eng_req;ds.compare=0;ds.at=0;designer_route();ui.designer_mode=DS_SHAPE;designer_page();
}
/* What the control the knob turned (ds.focus; a list page: its row) needs before it sounds: the FEATURES path to
 * switch on, DF_NONE nothing, DF_COUNT a CZ envelope step after its END (that is the POINT page's to change) */
static uint32_t designer_need(void)
{
    const page_t *p=cur_page();uint32_t id=p->id[ds.focus&3u];
    const uint8_t *q=p5_patch_of(TSEL)->raw;
    const list_t *l=list_on();
    if(l){
        const lrow_t *rw=list_at(l,lst.row);uint32_t pg=page_titled(rw->page);
        if(ds.focus || pg>=NPAGES)return DF_NONE;     /* (KNOB 2: the row) */
        p=&PAGES[pg];id=p->id[rw->slot&3u];
    }
    if(p->scope==SC_P5 && !p5_ed_active(TSEL,id))switch(id){
        case P5_PW_A:case P5_WHEEL_PW_A:return DF_PULSEA;
        case P5_POLY_PW:return designer_p5_on(q,P5_LEVEL_A,P_E6,q[P5_PULSE_A])?DF_POLY:DF_PULSEA;
        case P5_PW_B:case P5_WHEEL_PW_B:return q[P5_PULSE_B]?DF_OSCB:DF_PULSEB;
        case P5_FREQ_B:case P5_FINE_B:case P5_LOW_B:case P5_KEY_B:return DF_OSCB;
        case P5_UNISON_COUNT:case P5_UNISON_DETUNE:case P5_RETRIGGER:return DF_UNISON;
        case P5_POLY_ENV:case P5_POLY_B:case P5_POLY_FREQ:case P5_POLY_FILTER:return DF_POLY;
        case P5_RELEASE_AMP:return DF_RELEASE;
        case P5_RELEASE_FILTER:return q[P5_RELEASE_ON]?DF_FENV:DF_RELEASE;
        case P5_ATTACK_FILTER:case P5_DECAY_FILTER:case P5_SUSTAIN_FILTER:case P5_VEL_FILTER:return DF_FENV;
        default:return DF_LFO;                          /* rate, waves, initial, mix */
    }
    if(p->scope==SC_FMOP && id!=FP_OP && !designer_feat_get(DF_OP1+fm6_opsel%6u))return DF_OP1+fm6_opsel%6u;
    if(p->scope==SC_CZ1 && !cz_ed_active(cz_patch[song.sel].raw,id)){
        uint32_t line;
        if(id==LCZ_VWAVE || id==LCZ_VRATE || id==LCZ_VDELAY)return DF_CZVIB;
        if(id==LCZ_SIGN)return DF_DETUNE;
        if(id<LCZ_GLOBALS)return DF_LINES;              /* (DETUNE, MOD: line 2's) */
        line=id>=LCZ_WIN(0)?id-LCZ_WIN(0):id<LCZ_EBASE(0,0)?(id-LCZ_GLOBALS)/8u:(id-LCZ_EBASE(0,0))/54u;   /* (cz_ed_active) */
        if(id>=LCZ_EBASE(0,0) && id<LCZ_OLD_NP && cz_ed_active(cz_patch[song.sel].raw,LCZ_LBASE(line&1u)+LCZ_W1))return DF_COUNT;
        return DF_LINES;                                /* (line 2's values; line 1's in LINE2) */
    }
    if(p->scope==SC_TRACK && id==P_DETUNE && TSEL->p[P_VOICE]!=V_UNISON)return DF_VOICE;
    if(p->scope==SC_TRACK && p->fam==FAM_LFO && !str_eq(p->title,"LFO DEST") && p->graph!=GR_MOD &&
       !TSEL->p[P_LD_PIT] && !TSEL->p[P_LD_FLT] && !TSEL->p[P_LD_SHP] && !TSEL->p[P_LD_AMP] && !TSEL->p[P_M1SRC])return DF_VIB;
    return DF_NONE;
}
/* A dim control turned (s): the paths it needs switched on first, the last said in the header ("PULSE A ON").
 * A control that sounds already changes nothing else; a silent FM operator's level turned down stays silent */
static void designer_enable(int32_t s)
{
    char b[24];uint32_t f=DF_NONE,n=0,i;
    if(cur_page()->scope==SC_FMOP && cur_page()->id[ds.focus&3u]==FP_OL && s<0)return;
    while(n<3u && (f=designer_need())<DF_COUNT){
        fm1_irq_off();designer_feat_set(f,f==DF_VOICE?V_UNISON:f==DF_LINES?2u:1u);fm1_irq_on();
        sync_reload=1;n++;
        str_cpy(b,f==DF_VOICE?"Unison":f==DF_LINES?"Line 2":DF_WORD[f],16);
        for(i=0;b[i];i++)if(b[i]>='a' && b[i]<='z')b[i]=(char)(b[i]-32);
        str_cpy(b+str_len(b)," ON",4);
    }
    if(f==DF_COUNT)ui_message("AFTER THE END POINT");
    else if(n)ui_message(b);
}
/* the header: its chip (START, FEATURES, the original compared: their own; a page: its own), the stage's pages as
 * its dots (ui_draw.c head_chip_text, head_pages) */
static int designer_head_chip(char *b)
{
    const char *c=ds.compare?"ORIGINAL":ui.designer_mode==DS_SETUP?"DESIGN":designer_feat_on()?"FEATURES":0;
    if(c){str_cpy(b,c,16);return 1;}
    if(cur_page()->scope==SC_FMOP){                     /* (the tabs are the stages: the operator, the line here) */
        str_cpy(b,"OP1 ",16);b[2]=(char)('1'+fm6_opsel%6u);sec_chip(ui.page,b+4,12);return 1;
    }
    if(cur_page()->scope==SC_CZ1){str_cpy(b,cur_page()->title,16);return 1;}
    return 0;
}
static void designer_pages(uint32_t *n,uint32_t *k)
{
    *n=*k=0;
    for(uint32_t i=0;ui.designer_mode==DS_SHAPE && i<ds.n;i++)if(ds.route[i].stage==ds.stage){if(i==ds.at)*k=*n;(*n)++;}
}
/* the stages there are as the tabs under the header (a sound page's, ui_pages.c) */
static int designer_tabs_on(void) { return ui.designer_mode==DS_SHAPE && !designer_feat_on(); }
static void designer_tabs(void)
{
    const char *nm[DT_COUNT];uint32_t n=0,cur=0,i,st;
    for(st=0;st<DT_COUNT;st++){
        for(i=0;i<ds.n && ds.route[i].stage!=st;i++);
        if(i==ds.n)continue;
        if(st==ds.stage)cur=n;
        nm[n++]=DS_TAB[st];
    }
    tabs_draw(nm,n,cur,cur*7u+n*131u+ds.eng*1031u+ux.gen*977u+ux.pal*31u+song.sel+0x5E5u);
}
/* START: ENGINE, START (the sound now, INIT, or INIT with a goal's recipe), PAGES (MAIN, ALL) as rings, the engine
 * large under them, the keys under that (NEW SONG's key) */
static void new_keys_row(const char *go,uint32_t icon);  /* ui_new.c */
static int designer_init(void) { return !ds.current || ds.eng!=TSEL->eng_req; }
static uint32_t designer_start(void) { return !designer_init()?0u:ds.goal==DG_FREE?1u:1u+ds.goal; }   /* 0 .. DG_COUNT */
static void designer_start_set(uint32_t k)
{
    k=k<(ds.eng==TSEL->eng_req?0u:1u)?1u:k>DG_COUNT?DG_COUNT:k;   /* (another engine: from INIT) */
    ds.current=k==0u;ds.goal=(uint8_t)(k<2u?DG_FREE:k-1u);
}
static void designer_start_draw(void)
{
    char b[48];uint8_t cs=col_style;
    const aafont_t *f=&AF_L;
    uint32_t s=ds.eng*7u+ds.goal*131u+ds.current*1031u+ds.expert*40503u+(ui.hot_t?ui.hot_col+1u:0u)*7919u+ux.gen*977u+
               ux.pal*31u+song.sel;
    if(ui.force)lcd_fill(0,H_HEAD,240,240-H_HEAD,T_BG);
    draw_head();
    if(!ui.force && s==ds.sig)return;
    ds.sig=s;
    col_style=CS_RING;
    uint32_t st=designer_start();
    draw_column(0,"ENGINE",ENGINES[ds.eng]->name,"",VAL(0u),(int32_t)(eng_rank(ds.eng)*1000u/(NENG_SHOWN-1u)),ICON_NONE);
    draw_column(1,"START",st==0u?"CURRENT":st==1u?"INIT":DS_GOAL[ds.goal],"",VAL(1u),(int32_t)(st*1000u/DG_COUNT),ICON_NONE);
    draw_column(2,"PAGES",ds.expert?"ALL":"MAIN","",VAL(2u),ds.expert?1000:0,ICON_NONE);
    draw_column(3,"","","",T_THEME,-1,ICON_NONE);
    col_style=cs;
    cv_begin(240,100,T_BG);
    cv_rrect(PV_PANEL_X,0,PV_PANEL_W,100,6,T_PANEL,T_BG);
    if(text_w(f,ENGINES[ds.eng]->name)>216)f=&AF_M;
    cv_text_c(120,f==&AF_L?22:32,f,ENGINES[ds.eng]->name,T_TEXT,T_PANEL);
    b[0]=0;                                             /* "From this sound \xB7 main pages", "Pad from INIT \xB7 all pages" */
    if(st>1u){menu_words(b,DS_GOAL[ds.goal],16);str_cpy(b+str_len(b)," from INIT",12);}
    else str_cpy(b,st?"From INIT":"From this sound",16);
    str_cpy(b+str_len(b),ds.expert?" \xB7 all pages":" \xB7 main pages",14);
    cv_text_c(120,66,&AF_S,b,T_MID,T_PANEL);
    cv_blit(0,108);
    new_keys_row("Design",ICON_X_RIGHT);
}
/* FEATURES: a row a path, its state at the right (off: dim); an FM operator says what it is in the algorithm */
static int designer_feat_cap(uint32_t r,char *lb,char *v,uint16_t *vc)
{
    uint32_t n,f,st;const uint8_t *fs=designer_feats(&n);
    f=fs[r%n];st=designer_feat_get(f);
    str_cpy(lb,DF_WORD[f],16);
    if(f>=DF_OP1 && f<=DF_OP6)
        str_cpy(lb+3,(fm6_carriers(fm6_patch[song.sel][FP_ALG])>>(5u-(f-DF_OP1)))&1u?" carrier":" modulator",13);
    str_cpy(v,designer_feat_text(f,st),16);
    *vc=st || designer_feat_states(f)>2u?T_TEXT:T_DIM;
    return 0;
}
static void designer_feat_draw(void)
{
    uint32_t n,r,sig,vis=(uint32_t)((PV_SOUND_Y-LIST_Y0-2)/LIST_ROW);
    char lb[16],v[16];uint16_t vc;
    designer_feats(&n);
    if(ui.force){lcd_fill(0,H_HEAD,240,PV_SOUND_Y-H_HEAD,T_BG);ui.graph_sig=0;}
    draw_head();
    pv_sound();
    if(ds.row>=n)ds.row=0;
    if(ds.row<ds.first)ds.first=ds.row;
    if(ds.row>=ds.first+vis)ds.first=(uint8_t)(ds.row+1u-vis);
    sig=ds.row*7u+ds.first*131u+ux.gen*977u+ux.pal*31u+song.sel;
    for(r=ds.first;r<n && r<ds.first+vis;r++){designer_feat_cap(r,lb,v,&vc);sig=str_hash(str_hash(sig,lb),v)+vc;}
    if(ui.force || sig!=ds.sig){ds.sig=sig;list_rows(n,ds.row,ds.first,vis,designer_feat_cap);}
}
/* under a stage's page, as under NEW SONG: OCT- back, the sound, OCT+ the next stage (the last: Finish), in place of
 * the sound's line (ui_pages.c pv_sound) */
static int designer_next(void)                          /* the stage after this one, DT_COUNT none */
{
    for(uint32_t i=ds.at+1u;i<ds.n;i++)if(ds.route[i].stage!=ds.stage)return ds.route[i].stage;
    return DT_COUNT;
}
static int designer_foot(void)
{
    char nm[20];
    uint32_t nx,sig;
    const char *go;
    int32_t gw;
    if(ui.designer_mode!=DS_SHAPE)return 0;
    nx=(uint32_t)designer_next();go=nx<DT_COUNT?DS_TAB[nx]:"Finish";
    sound_name(TSEL,nm);
    sig=str_hash(str_hash(nx*7u+song.sel*131u+ux.gen*977u+ux.pal*31u+0xF00Du,go),nm);
    if(!ui.force && sig==pv.sound)return 1;
    pv.sound=sig;
    gw=text_w(&AF_S,go)+30;
    cv_begin(240,240-PV_SOUND_Y,T_BG);
    cv_rrect(6,1,34,18,5,T_SURF,T_BG);
    cv_icon_mid(17,10,12,ICON_X_UNDO,T_MID,T_SURF);
    cv_free_text(48,2,&AF_S,nm,T_SEC,T_BG,234-gw-8-48);
    cv_rrect(234-gw,1,gw,18,5,T_THEME,T_BG);
    cv_icon_mid(234-gw+8,10,12,nx<DT_COUNT?ICON_X_RIGHT:ICON_X_CHECK,T_INK,T_THEME);
    cv_text_on(234-gw+22,3,&AF_S,go,T_INK,T_THEME);
    cv_blit(0,PV_SOUND_Y);
    return 1;
}
/* START and FEATURES (draw_page): the shaping stages are pages, drawn as ever */
static int designer_screen(void) { return designer_on() && (ui.designer_mode==DS_SETUP || designer_feat_on()); }
static void designer_draw(void)
{
    if(ui.designer_mode==DS_SETUP)designer_start_draw();
    else designer_feat_draw();
}
/* its sheet (HOME; OCT past the first or the last stage): the sound kept, saved as a user sound, or the original's */
static void ds_keep(void) { designer_close(0); }
static void ds_save_user(void) { designer_close(0);ps_save(); }
static const sheet_row_t SHEET_DESIGN[] = {{"Keep the sound", 0, 0, ds_keep, 0}, {"Save as user\x85", 0, 0, ds_save_user, 0},
                                           {"Discard", 0, 0, 0, CF_DISCARD_DESIGN}};
static void designer_finish(void)
{
    char b[24];
    sound_name(TSEL,b);
    sheet_open(b[0]?b:"Sound design","design",SHEET_DESIGN,NELEM(SHEET_DESIGN));
}
