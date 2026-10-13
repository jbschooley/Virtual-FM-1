/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound designer's input (ui_designer.c), ahead of the rest of ui_input: all of it but NAME and the question.
 * START: KNOB 1..3 ENGINE START PAGES, OCT+ designs, OCT- / HOME closes. The stages: OCT+ / OCT- the next /
 * previous stage (past either end, and HOME: the designer's sheet), PRESETS a stage, SELECT the pages, the knobs the
 * page's (FEATURES: KNOB 2 the row, KNOB 1 its value; a dim control: what it needs switched on), SAVE the original
 * and back */
static void page_knob(uint32_t k, int32_t s);          /* ui_input.c */
static void designer_leave_feats(void)                  /* FEATURES left: the pages of what it switched on */
{
    if(ds.n && ds.route[ds.at].page==DS_FEAT){designer_route();ds.at=0;}
}
static void designer_stage(int32_t dir,int sheet)
{
    int32_t i,st;
    designer_draft();designer_leave_feats();            /* (the draft's switches: not the original's) */
    i=ds.at;st=ds.stage;
    if(dir>0){while(++i<ds.n && ds.route[i].stage==st){}if(i>=ds.n){if(sheet)designer_finish();return;}}
    else {while(--i>=0 && ds.route[i].stage==st){}if(i<0){if(sheet)designer_finish();return;}
        st=ds.route[i].stage;while(i>0 && ds.route[i-1].stage==st)i--;}
    ds.at=(uint8_t)i;designer_page();
}
static int designer_input(uint32_t pressed,uint32_t notes)
{
    uint32_t oct=oct_taps(pressed,1),k;
    int32_t s;
    (void)notes;
    if(song.sel!=ds.track || (ui.designer_mode==DS_SHAPE && !ds.compare && TSEL->eng_req!=ds.eng)){
        /* another track or engine (the editor, MIDI): theirs now, nothing restored over them */
        motion_design_pause&=(uint8_t)~(1u<<ds.track);designer_quit=0;ui.designer_mode=DS_OFF;ui.force=1;
        if(song.sel==ds.track)ui_message("SOUND CHANGED");
        return 0;
    }
    song.grid=0;kb_mask=kb_lock=0;            /* (the keys play) */
    seq_midi_events(0);
    if(pressed&(1u<<panel.btn[B_PLAY]))transport_req=transport_busy()?2:1;
    panel_enc(EN_ALGO);                                 /* (the track stays the designer's) */
    if(pop.on==POP_SHEET || pop.on==POP_LIST){          /* its sheet (ui_popup.c) */
        int32_t k1=panel_enc(EN_K1),k2=panel_enc(EN_K2);
        if(pop.on==POP_LIST)vlist_input(k2,oct);
        else sheet_input(k1,k2,oct);
        enc_drop();
        return 1;
    }
    if(ui.designer_mode==DS_SETUP){
        for(k=0;k<4u;k++)if((s=panel_enc(EN_K1+k))!=0){
            ui.hot_col=(uint8_t)k;ui.hot_t=40;
            if(k==0u){ds.eng=(uint8_t)eng_step(ds.eng,s);designer_start_set(designer_start());}
            else if(k==1u)designer_start_set((uint32_t)clamp((int32_t)designer_start()+(s>0?1:-1),0,DG_COUNT));
            else if(k==2u)ds.expert=s>0;
        }
        enc_drop();
        if((oct&1u) || (pressed&(1u<<panel.btn[B_HOME])))designer_close(0);
        else if(oct&2u)designer_begin();
        autosave_poll(1);
        return 1;
    }
    if(pressed&(1u<<panel.btn[B_SAVE]))designer_compare();
    if(pressed&(1u<<panel.btn[B_HOME]))designer_finish();
    else if(oct&2u)designer_stage(1,1);
    else if(oct&1u)designer_stage(-1,1);
    if((s=panel_enc(EN_PRESET))!=0)designer_stage(s,0);
    if((s=panel_enc(EN_SELECT))!=0 && ds.n){
        designer_draft();designer_leave_feats();ds.at=(uint8_t)clamp(ds.at+(s>0?1:-1),0,ds.n-1);designer_page();
    }
    for(k=0;k<4u;k++)if((s=panel_enc(EN_K1+k))!=0){
        if(ds.compare){ui_message("COMPARING");continue;}
        if(designer_feat_on()){ui.hot_col=(uint8_t)k;ui.hot_t=40;designer_feat_knob(k,s);continue;}
        ds.focus=(uint8_t)k;
        designer_enable(s);                             /* (a dim control: what it needs first) */
        page_knob(k,s);                                 /* the page's own: its value, a list's row, a picker */
        sync_reload=1;
    }
    autosave_poll(1);                                   /* a draft is never saved by project autosave */
    return 1;
}
