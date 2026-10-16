/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* One before/after swap for a recorded pattern. Capture on its first mutation,
 * seal after duration/quantization processing. Other banks keep their events,
 * including when they reuse a slot formerly belonging to this pattern. The
 * audio owner allocates the bounded snapshot; no per-sample history work. */
typedef struct {
    recorded_note_t notes[RECORD_MAX];
    step_t steps[NSTEP];
    motion_store_t motion;
    int16_t timing[4];
    uint32_t pattern_gen, signature;
    uint8_t valid, active, redo, track, bank, engine;
} recording_undo_t;
static recording_undo_t *recording_undo_ptr;

static recording_undo_t *recording_undo_state(void)
{
    return resource[RES_RECORD_UNDO].size ? recording_undo_ptr : 0;
}
static void recording_undo_clear(void)
{
    resource_release(RES_RECORD_UNDO);
    recording_undo_ptr=0;
    recording_undo_lost=0;
    recording_undo_active=recording_undo_skip=0;
}
static uint32_t recording_undo_signature(const track_t *t)
{
    uint32_t h=fnv(2166136261u,t->step,sizeof t->step),owner=recording_owner(t),k=trk_index(t);
    h=fnv(h,&t->p[P_SLEN],4u*sizeof(int16_t));
    h=fnv(h,&t->eng_req,sizeof t->eng_req);
    for(uint32_t i=0;i<RECORD_MAX;i++)if(recording[i].vel&&(recording[i].owner&31u)==owner)
        h=fnv(h,&recording[i],sizeof recording[i]);
    for(uint32_t i=0;i<motion.count;i++)if((motion.event[i].place>>6)==k&&motion_pattern[i]==t->pattern)
        h=fnv(h,&motion.event[i],sizeof motion.event[i]);
    return h^((motion.on>>k)&1u);
}
static int recording_undo_context(const recording_undo_t *u,const track_t *t)
{
    return u->valid&&u->track==trk_index(t)&&u->bank==t->pattern&&u->pattern_gen==t->pattern_gen&&u->engine==t->eng_req;
}
static void recording_undo_capture(track_t *t)
{
    uint8_t bit=(uint8_t)(1u<<trk_index(t));
    if(recording_undo_skip&bit)return;
    recording_undo_t *u=recording_undo_state();
    if(u&&u->active){
        if(recording_undo_context(u,t))return;
        /* Only one pattern can own the slot during overlapping takes. */
        if(u->track!=trk_index(t)){recording_undo_skip|=bit;return;}
    }
    recording_undo_ptr=resource_get(RES_RECORD_UNDO,sizeof(recording_undo_t));
    u=recording_undo_ptr;
    if(!u){recording_undo_lost=1;recording_undo_skip|=bit;return;}
    uint32_t owner=recording_owner(t);
    for(uint32_t i=0;i<RECORD_MAX;i++){
        if(recording[i].vel&&(recording[i].owner&31u)==owner)u->notes[i]=recording_snapshot(i);
        else memset(&u->notes[i],0,sizeof u->notes[i]);
    }
    memcpy(u->steps,t->step,sizeof u->steps);
    memcpy(u->timing,&t->p[P_SLEN],sizeof u->timing);
    motion_snapshot_track(t,&u->motion);
    u->track=(uint8_t)trk_index(t);u->bank=t->pattern;u->pattern_gen=t->pattern_gen;u->engine=t->eng_req;
    u->signature=recording_undo_signature(t);
    u->valid=u->active=1;u->redo=0;
    recording_undo_active=bit;
    /* Motion-only takes also supersede earlier manual note transactions. */
    recording_generation++;
}
static void recording_undo_finish(track_t *t)
{
    uint8_t bit=(uint8_t)(1u<<trk_index(t));
    recording_undo_skip&=(uint8_t)~bit;recording_undo_active&=(uint8_t)~bit;
    recording_undo_t *u=recording_undo_state();
    if(!u||!u->active||u->track!=trk_index(t))return;
    if(!recording_undo_context(u,t)){recording_undo_clear();return;}
    uint32_t after=recording_undo_signature(t);
    u->active=0;
    if(after==u->signature){recording_undo_clear();return;}
    u->signature=after;
}
/* Caller holds the interrupt guard. 0: unavailable; 1: swapped; 2: busy;
 * 3: insufficient event capacity. Preflight everything before changing data. */
static int recording_undo_apply(int redo)
{
    recording_undo_t *u=recording_undo_state();track_t *t=TSEL;
    if(!u||!recording_undo_context(u,t))return 0;
    if(u->active||rec_on(t))return 2;
    if(u->signature!=recording_undo_signature(t))return 0;
    if(u->redo!=(uint32_t)redo)return 0;
    uint32_t owner=recording_owner(t),blocked=0,free=0;
    for(uint32_t i=0;i<RECORD_MAX;i++){
        int other=recording[i].vel&&(recording[i].owner&31u)!=owner;
        blocked+=u->notes[i].vel&&other;
        free+=!u->notes[i].vel&&!other;
    }
    if(blocked>free||motion.count-motion_count(t)+u->motion.count>MOTION_MAX)return 3;
    motion_store_t current;motion_snapshot_track(t,&current);
    if(motion_replace_track(t,&u->motion))return 3;
    u->motion=current;
    recording_stop(t);seq_release(t);
    /* Relocate only saved events whose old slots now belong to another bank.
     * A forward free cursor keeps this scan bounded by twice RECORD_MAX. */
    uint32_t dest=0;
    for(uint32_t i=0;i<RECORD_MAX;i++)if(u->notes[i].vel&&recording[i].vel&&(recording[i].owner&31u)!=owner){
        while(u->notes[dest].vel||(recording[dest].vel&&(recording[dest].owner&31u)!=owner))dest++;
        u->notes[dest]=u->notes[i];memset(&u->notes[i],0,sizeof u->notes[i]);dest++;
    }
    for(uint32_t i=0;i<RECORD_MAX;i++)if(u->notes[i].vel||(recording[i].vel&&(recording[i].owner&31u)==owner)){
        recorded_note_t r=recording[i];
        recording[i]=u->notes[i];u->notes[i]=r;
        memset(&recording_run[i],0,sizeof recording_run[i]);recording_flags[i]=0;
    }
    for(uint32_t i=0;i<NSTEP;i++){step_t s=t->step[i];t->step[i]=u->steps[i];u->steps[i]=s;}
    for(uint32_t i=0;i<4u;i++){int16_t p=t->p[P_SLEN+i];t->p[P_SLEN+i]=u->timing[i];u->timing[i]=p;}
    t->rh_n=t->rh_ties=t->rskip_n=0;
    recording_fraction[trk_index(t)]=0;
    recording_reindex();u->redo^=1u;u->signature=recording_undo_signature(t);
    return 1;
}
