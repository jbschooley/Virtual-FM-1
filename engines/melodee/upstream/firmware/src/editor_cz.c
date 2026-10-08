/* SPDX-License-Identifier: GPL-3.0-only */
/* Full native Casio tone over the editor transport, nibble-packed, low first.
 * 75 GET / 76 PUT. target 0: track; target 1: ordinary user preset slot.
 * Native slots use UP_VER_CZ, so their existing A/B store is atomic. */
enum { ED_CZ_GET = 75, ED_CZ_PUT = 76, ED_CZ_BANK = 77 };
static int ed_cz_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if(cmd==ED_CZ_BANK){
        uint32_t k=n?a[0]:127u;cz_bank_t *b=n==1?cz_bank_load(k):0;ed_b(k);ed_b(b?0:1);
        if(b){ed_str(b->name,16);for(uint32_t i=0;i<16;i++){ed_b((b->used>>i)&1u);char name[17]={0};if((b->used>>i)&1u)for(uint32_t j=0;j<16;j++)name[j]=b->tone[i].raw[128+j]>=32 && b->tone[i].raw[128+j]<=126?(char)b->tone[i].raw[128+j]:' ';ed_str(name,16);}}
        return 1;
    }
    if (cmd != ED_CZ_GET && cmd != ED_CZ_PUT) return 0;
    uint32_t target = n ? a[0] : 127u, index = n > 1u ? a[1] : 127u, rc = 0;
    uint8_t raw[CZ_BYTES];
    if (target > 2u || index >= (target == 2u ? 128u : target ? UP_SLOTS : NTRK) || (target==2u && cmd==ED_CZ_PUT)) rc = 1;
    if (!rc && cmd == ED_CZ_GET) {
        if(n!=2u)rc=1;else if(target==2u)rc=cz_bank_get(index/16u,index%16u,raw)?2u:0u;
        else
        if (n != 2u) rc = 1;
        else if (!target) memcpy(raw, cz_patch[index].raw, CZ_BYTES);
        else if (up_used(index) && up_cz_raw(up_rec(index),raw)) { /* complete native tone */ }
        else rc = 2;
    }
    if (!rc && cmd == ED_CZ_PUT) {
        if (n != 2u + 2u*CZ_BYTES && n != 2u + 2u*CZ_BYTES + 32u) rc = 1;
        for (uint32_t i=0; !rc && i<CZ_BYTES; i++) {
            if (a[2u+2u*i] > 15u || a[3u+2u*i] > 15u) rc = 1;
            else raw[i] = a[2u+2u*i] | (uint8_t)(a[3u+2u*i]<<4);
        }
        if (!rc && !cz_patch_valid(raw)) rc = 1;
        if (!rc && target) {
            up_rec_t r; memset(&r,0,sizeof r);
            r.used=UP_USED; r.ver=UP_VER_CZ; r.engine=ENGI_CZ; r.np=P_COUNT;
            memcpy(r.packed,raw,CZ_BYTES);
            char name[13];
            for (uint32_t i=0;i<12u;i++) name[i]=raw[128u+i]>=32u && raw[128u+i]<=126u ? (char)raw[128u+i] : ' ';
            name[12]=0; up_set_name(&r,index,name);
            if (n > 2u+2u*CZ_BYTES) for (uint32_t i=0;i<16u;i++) {
                r.note[i]=a[2u+2u*CZ_BYTES+2u*i]; r.flags[i]=a[3u+2u*CZ_BYTES+2u*i];
                up_pat_norm(&r.note[i],&r.flags[i]);
            }
            int result=ed_flash_stop() ? 2 : up_put(index,&r);
            rc=result==1 ? 1u : result==2 ? 2u : 0u;
        } else if (!rc) {
            track_t *t=&trk[index];
            load_begin(t,UNDO_SOUND);
            panic_req |= (uint8_t)(1u<<index);
            fm1_irq_off();
            memcpy(cz_patch[index].raw,raw,CZ_BYTES); t->eng_req=ENGI_CZ;
            for (uint32_t i=P_E0;i<P_COUNT;i++) t->p[i]=param_desc_of(ENGI_CZ,i)->def;
            t->p[P_E7]=CZ_NATIVE; t->preset=0; t->user=0; t->user_native=0; cz_track_accept(t);
            fm1_irq_on();
            load_end(t); sync_reload=1; ui.force=1;
        }
    }
    ed_b(target); ed_b(index); ed_b(rc);
    if (cmd == ED_CZ_GET && !rc) for (uint32_t i=0;i<CZ_BYTES;i++) { ed_b(raw[i]&15u); ed_b(raw[i]>>4); }
    return 1;
}
