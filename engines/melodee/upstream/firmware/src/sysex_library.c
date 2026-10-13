/* SPDX-License-Identifier: GPL-3.0-only */
/* Incoming dumps retain their native names and bytes until the player chooses
 * a destination. The receive ISR never writes flash or changes a track. */
static struct {
    uint8_t engine, count, general;
    uint16_t size, first;
    const uint8_t *data;
    uint8_t raw[238];
} sx_import;
static struct { uint16_t anchor, end; uint8_t on, engine, track, pattern; uint32_t generation; uint8_t notes[RECORD_MAX/8u]; } bulk;
static uint8_t sx_checksum(const uint8_t *p,uint32_t n)
{
    uint32_t sum=0;while(n--)sum+=*p++;return (uint8_t)(-sum&127u);
}
static uint32_t library_engine(void)
{
    return sx_import.count && !ui.home && cur_page()->graph == GR_USER ? sx_import.engine : TSEL->eng_req;
}
static uint32_t library_native(void) { return sx_import.count && sx_import.general && !ui.home && cur_page()->graph==GR_USER ? 0u : native_limit(library_engine()); }
static void bulk_clear(void) { bulk.on = 0; ui.note_copy = ui.note_move = 0; ui.force = 1; }
static int bulk_has(uint32_t k)
{
    uint32_t a = bulk.anchor < bulk.end ? bulk.anchor : bulk.end;
    uint32_t b = bulk.anchor > bulk.end ? bulk.anchor : bulk.end;
    return bulk.on && k >= a && k <= b;
}
static void sysex_import_name(char *nm)
{
    uint32_t e=sx_import.engine,offset=sx_import.general?4u:e==ENGI_FM6?118u:e==ENGI_CZ?128u:P5_NAME;
    uint32_t n=!sx_import.general&&e==ENGI_FM6?10u:12u;
    for(uint32_t i=0;i<n;i++){uint8_t c=sx_import.data[offset+i];nm[i]=c>=32u&&c<=126u?(char)c:' ';}nm[n]=0;
}
static int sysex_stage(uint32_t e, const uint8_t *raw, uint32_t count, uint32_t size)
{
    if(!count || (count*size>sizeof sx_import.raw && !(e==ENGI_FM6&&count==32u&&size==FM6_PACKED)))return 0;
    if (sx_import.count || ui.confirm || ui.name_active || ui.new_active || ui.layer || ui.ly || designer_on()) { ui_message("FINISH CURRENT IMPORT"); return 0; }
    if(count*size>sizeof sx_import.raw){sx_import.data=raw;fm6_rx_ready=2;}
    else {memcpy(sx_import.raw,raw,count*size);sx_import.data=sx_import.raw;}
    sx_import.general=size==238u;
    sx_import.engine = (uint8_t)e; sx_import.count = (uint8_t)count; sx_import.size = (uint16_t)size;
    sx_import.first = 0;
    bulk_clear();
    ui.menu = ui.layer = ui.ly = 0;
    for(uint32_t p=0;p<NPAGES;p++)if(PAGES[p].graph==GR_USER){ui.page=(uint8_t)p;break;}
    ui.import_refresh=1;ui.home=0;ui.fam_last[FAM_SAVE]=ui.page;ui.uslot=0;song.seq_mode=0;ui.entry_open=0;ui.note_pick=0;ui.act=0;seq_midi_reset();
    char nm[16];sysex_import_name(nm);
    ui_say("IMPORT ", nm); str_cpy(ui.msg2, "CHOOSE USER SLOTS", sizeof ui.msg2); ui.force = 1;
    return 1;
}
static void sysex_cancel(void) { if(fm6_rx_ready==2u)fm6_rx_ready=0;sx_import.count = 0; bulk_clear(); ui_message("IMPORT CANCELLED"); }
static void sysex_commit(void)
{
    if (!sx_import.count) return;
    if (transport_busy()) { ui_message("STOP TO IMPORT"); return; }
    uint32_t saved = 0;
    while (saved < sx_import.count) {
        int rc = sx_import.general ? general_sysex_save(sx_import.first+saved,sx_import.data+saved*sx_import.size) : native_put(sx_import.engine,sx_import.first+saved,sx_import.data+saved*sx_import.size);
        if (rc == 1 || rc == 2) { /* Keep only the unsaved suffix for an honest retry. */
            if(saved){sx_import.data+=saved*sx_import.size;sx_import.count-=saved;sx_import.first+=saved;ui.uslot=(uint8_t)sx_import.first;}
            ui_message("IMPORT SAVE ERROR"); return;
        }
        saved++;
    }
    sx_import.count=0;if(fm6_rx_ready==2u)fm6_rx_ready=0;bulk_clear();ui_message("PATCHES IMPORTED");
}
static void sysex_choose(void)
{
    if (transport_busy()) { ui_message("STOP TO IMPORT"); return; }
    uint32_t first = ui.uslot;
    if (first + sx_import.count > (sx_import.general?UP_SLOTS:native_limit(sx_import.engine))) { ui_message("NOT ENOUGH SLOTS"); return; }
    sx_import.first = (uint16_t)first;
    for(uint32_t k=first;k<first+sx_import.count;k++)if(sx_import.general?(up_used(k)||up_old(k)):(native_used(sx_import.engine,k)||native_old(sx_import.engine,k))){confirm_open(CF_IMPORT,0);return;}
    sysex_commit();
}
/* Native single-voice frames preserve each selected slot's name on USB. */
static int library_send(uint32_t e, uint32_t k)
{
#if MELODEE_OTA
    if(!native_limit(e))return general_sysex_send(k);
    if (!usb.config || !native_used(e,k)) return 1;
    uint8_t raw[CZ_BYTES], frame[320]; uint32_t n;
    memcpy(raw,native_raw(e,k),native_size(e));
    if(e==ENGI_FM6){memcpy(frame,(uint8_t[]){0xf0,0x43,0,0,1,0x1b},6);uint8_t voice[FP_SIZE+1u];fm6_unpack(raw,voice);memcpy(frame+6,voice,FP_SIZE);frame[161]=sx_checksum(frame+6,FP_SIZE);frame[162]=0xf7;n=163;}
    else if(e==ENGI_CZ){memcpy(frame,(uint8_t[]){0xf0,0x44,0,0,0x70,0x30},6);for(uint32_t i=0;i<CZ_BYTES;i++){frame[6+2*i]=raw[i]&15;frame[7+2*i]=raw[i]>>4;}frame[6+2*CZ_BYTES]=0xf7;n=7+2*CZ_BYTES;}
    else if(e==ENGI_PROPHET){p5_patch_t p;memcpy(&p,raw,sizeof p);n=p5_patch_encode(&p,frame,sizeof frame);}
    else return 1;
    return !n || ota_wire_send(frame,n);
#else
    (void)e; (void)k; return 1;
#endif
}
static void bulk_erase(void)
{
    if(transport_busy()){ui_message("STOP TO ERASE");return;}
    uint32_t e=bulk.engine;
    for (uint32_t k = 0; k < (native_limit(e) ? native_limit(e) : UP_SLOTS); k++)
        if (bulk_has(k) && (native_limit(e) ? native_old(e,k) : up_old(k))) { ui_message("ERASE OLD SLOT FIRST"); return; }
    for(uint32_t k=0;k<(native_limit(e)?native_limit(e):UP_SLOTS);k++)if(bulk_has(k)&& (native_limit(e)?native_used(e,k):up_used(k))){
        if(native_limit(e)){int rc=native_put(e,k,0);if(rc==1||rc==2){ui_message("ERASE ERROR");return;}}
        else {up_ui_named(1,k,0);if(up_used(k))return;}
    }
    bulk_clear();ui_message("PRESETS ERASED");
}
static void bulk_erase_ask(void) { if(transport_busy())ui_message("STOP TO ERASE");else confirm_open(CF_BULK_ERASE,0); }
static void bulk_send(void)
{
    for(uint32_t k=0;k<(native_limit(bulk.engine)?native_limit(bulk.engine):UP_SLOTS);k++)if(bulk_has(k)&&(native_limit(bulk.engine)?native_used(bulk.engine,k):up_used(k)))if(library_send(bulk.engine,k)){ui_message("SYSEX SEND ERROR");return;}
    ui_message("PRESETS SENT");
}
static void library_send_current(void) { ui_message(library_send(library_engine(),ui.uslot)?"SYSEX SEND ERROR":"PRESET SENT"); }
