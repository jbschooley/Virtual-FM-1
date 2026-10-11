/* SPDX-License-Identifier: GPL-3.0-only */
/* Native user presets: DX7 VMEM bytes and Casio CZ-1 tone bytes, without
 * Felucca parameters or patterns. Storage grouping is invisible to the player.
 * CZ keeps all 128 existing tones. FM6 gets 64 independent slots. */
#define NATIVE_FM_SLOTS 64u
#define NATIVE_CZ_SLOTS 128u
#define NATIVE_FM_MAGIC 0x314D464Eu /* NFM1 */
typedef struct { uint32_t magic; uint16_t ver, slots; uint32_t used; uint8_t tone[16][FM6_PACKED]; } native_fm_t;
_Static_assert(sizeof(native_fm_t)==2060u, "native DX7 preset object");
static uint32_t native_limit(uint32_t e) { return e==ENGI_FM6 ? NATIVE_FM_SLOTS : e==ENGI_CZ ? NATIVE_CZ_SLOTS : e==ENGI_PROPHET ? P5_USER_SLOTS : 0u; }
#if MELODEE_FLASH
static uint32_t native_fm_obj(uint32_t b) { return b<2u ? OBJ_NATIVEFM0+b : b==2u ? OBJ_FM6BANK : OBJ_UPFM6_EXT; }
#endif
static int native_fm_valid(const native_fm_t *b);
static void native_index(uint32_t e, uint32_t b);
/* Names and used masks stay resident. Audio owns independent track patches;
 * only main-loop loading/editing touches these single-bank caches. */
static struct { uint16_t used; uint8_t ready, valid; char name[16][13]; } native_meta[12];
#if MELODEE_FLASH
static native_fm_t native_fm_cache __attribute__((section(".pool")));
#define native_cz_cache cz_bank_cache
static uint8_t native_fm_cached = 255;
static native_fm_t *native_fm_bank(uint32_t b)
{
    if (native_fm_cached != b) {
        int n = flash_ok ? st_load(native_fm_obj(b), &native_fm_cache, sizeof native_fm_cache) : -1;
        if (n != sizeof native_fm_cache || !native_fm_valid(&native_fm_cache)) {
            memset(&native_fm_cache, 0, sizeof native_fm_cache);
        }
        native_fm_cached = (uint8_t)b;
        native_index(ENGI_FM6, b);
    }
    return &native_fm_cache;
}
static cz_bank_t *native_cz_bank(uint32_t b)
{
    if (native_cz_cached != b) {
        cz_bank_cached=255;
        int n = flash_ok ? st_load(OBJ_CZBANK0 + b, &native_cz_cache, sizeof native_cz_cache) : -1;
        if (n != sizeof native_cz_cache || !cz_bank_valid(&native_cz_cache)) memset(&native_cz_cache, 0, sizeof native_cz_cache);
        native_cz_cached = (uint8_t)b;
        native_index(ENGI_CZ, b);
    }
    return &native_cz_cache;
}
#else
static native_fm_t native_fm_host[4] __attribute__((section(".pool")));
static cz_bank_t native_cz_host[8] __attribute__((section(".pool")));
static native_fm_t *native_fm_bank(uint32_t b) { return &native_fm_host[b]; }
static cz_bank_t *native_cz_bank(uint32_t b) { return &native_cz_host[b]; }
#endif
static void native_cache_reset(void)
{
    memset(native_meta, 0, sizeof native_meta);
    p5_user_reset();
#if MELODEE_FLASH
    native_fm_cached = native_cz_cached = 255;
#else
    memset(native_fm_host, 0, sizeof native_fm_host); memset(native_cz_host, 0, sizeof native_cz_host);
#endif
}
static int native_fm_valid(const native_fm_t *b)
{
    if(b->magic!=NATIVE_FM_MAGIC || b->ver!=1u || b->slots!=16u || b->used>>16)return 0;
    for(uint32_t k=0;k<16u;k++)if(b->used>>k&1u)for(uint32_t j=0;j<FM6_PACKED;j++)if(b->tone[k][j]>127u)return 0;
    return 1;
}
static void native_index(uint32_t e, uint32_t b)
{
    uint32_t idx = e == ENGI_FM6 ? b : b + 4u;
    const native_fm_t *f = e == ENGI_FM6 ? native_fm_bank(b) : 0;
    const cz_bank_t *c = e == ENGI_CZ ? native_cz_bank(b) : 0;
    native_meta[idx].ready = 1;
    native_meta[idx].valid = (uint8_t)(f ? native_fm_valid(f) : cz_bank_valid(c));
    native_meta[idx].used = native_meta[idx].valid ? (uint16_t)(f ? f->used : c->used) : 0;
    for (uint32_t j = 0; j < 16u; j++) {
        const uint8_t *raw = f ? f->tone[j] + 118u : c->tone[j].raw + 128u;
        uint32_t len = f ? 10u : 12u, n = 0;
        for (uint32_t i = 0; i < len; i++) {
            char ch = raw[i] >= 32u && raw[i] <= 126u ? (char)raw[i] : ' ';
            native_meta[idx].name[j][i] = ch; if (ch != ' ') n = i + 1u;
        }
        native_meta[idx].name[j][n] = 0;
    }
}
static int native_fm_active(void)
{
    for (uint32_t b = 0; b < 4u; b++) {
        if (!native_meta[b].ready) native_index(ENGI_FM6, b);
        if (!native_meta[b].valid) return 0;
    }
    return 1;
}
static void native_fm_empty(uint32_t b) { native_fm_t *f = native_fm_bank(b); memset(f,0,sizeof *f);f->magic=NATIVE_FM_MAGIC;f->ver=1;f->slots=16; }
static int native_used(uint32_t e,uint32_t k)
{
    if(e==ENGI_PROPHET)return p5_user_used(k);
    if(k>=native_limit(e))return 0;
    uint32_t idx = k/16u + (e == ENGI_FM6 ? 0u : 4u);
    if (!native_meta[idx].ready) native_index(e, k/16u);
    return (native_meta[idx].used >> (k%16u)) & 1u;
}
static const uint8_t *native_raw(uint32_t e,uint32_t k) { return e==ENGI_PROPHET ? (const uint8_t *)&p5_user_bank(k/P5_BANK_SLOTS)->patch[k%P5_BANK_SLOTS] : e==ENGI_FM6 ? native_fm_bank(k/16u)->tone[k%16u] : native_cz_bank(k/16u)->tone[k%16u].raw; }
static uint32_t native_size(uint32_t e) { return e==ENGI_PROPHET ? sizeof(p5_patch_t) : e==ENGI_FM6 ? FM6_PACKED : CZ_BYTES; }
static void native_name(uint32_t e,uint32_t k,char *out)
{
    if(e==ENGI_PROPHET){const char *n=p5_user_name(k);uint32_t i=0;for(;i<12u&&n[i];i++)out[i]=n[i];out[i]=0;return;}
    uint32_t idx = k/16u + (e == ENGI_FM6 ? 0u : 4u);
    if (!native_meta[idx].ready) native_index(e, k/16u);
    memcpy(out, native_meta[idx].name[k%16u], 13);
}
static uint32_t native_count(uint32_t e) { uint32_t n=0;for(uint32_t k=0;k<native_limit(e);k++)n+=native_used(e,k);return n; }
static uint32_t native_nth(uint32_t e,uint32_t n) { for(uint32_t k=0;k<native_limit(e);k++)if(native_used(e,k) && !n--)return k;return native_limit(e); }
static uint32_t native_rank(uint32_t e,uint32_t k) { uint32_t n=0;for(uint32_t i=0;i<k;i++)n+=native_used(e,i);return n; }
static int native_save_bank(uint32_t e,uint32_t b)
{
    int rc = 3;
#if MELODEE_FLASH
    if(!flash_ok)return 2;
    if (flash_ok) rc = st_save(e==ENGI_FM6?native_fm_obj(b):OBJ_CZBANK0+b,
                             e==ENGI_FM6?(const void *)native_fm_bank(b):(const void *)native_cz_bank(b),
                             e==ENGI_FM6?sizeof(native_fm_t):sizeof(cz_bank_t)) ? 2 : 0;
#endif
    if (rc != 2) native_index(e, b);
    return rc;
}
/* Commit the two new objects before reusing either old owned-voice object.
 * At every interruption, voices still reside in an old source or a committed
 * native object. Legacy user records remain available for older archives. */
static void native_release_shared(void)
{
    int marked=0;
    for(uint32_t b=0;b<UP_SLOTS/UP_PER_BANK;b++){
        up_bank_t old=(*up_cache_bank(b));uint32_t changed=0;
        for(uint32_t j=0;j<UP_PER_BANK;j++){
            uint32_t k=b*UP_PER_BANK+j;if(!up_used(k))continue;
            uint32_t e=up_engine(k),slot=USER_NONE;
            if(e==ENGI_FM6 && native_used(e,k))slot=k;
            else if(e==ENGI_CZ){uint8_t raw[CZ_BYTES];if(up_cz_raw(up_rec(k),raw))for(uint32_t i=0;i<NATIVE_CZ_SLOTS;i++)if(native_used(e,i) && !memcmp(native_raw(e,i),raw,CZ_BYTES)){slot=i;break;}}
            if(slot==USER_NONE)continue;
            if(favorite_has(USER_GENERAL,k)){favorite_set(e==ENGI_FM6?USER_NATIVE_FM:USER_NATIVE_CZ,slot,1);favorite_set(USER_GENERAL,k,0);marked=1;}
            memset(up_rec(k),0,sizeof(up_rec_t));changed++;
        }
        if(changed){
#if MELODEE_FLASH
            if(flash_ok && up_save_bank(b)){(*up_cache_bank(b))=old;ui_message("PRESET SAVE ERROR");continue;}
#endif
            up_gen++;
        }
    }
    if(marked)settings_save();
}
static void native_boot(void)
{
    for(uint32_t b=0;b<4u;b++){
#if MELODEE_FLASH
        if(flash_ok && st_load(native_fm_obj(b),native_fm_bank(b),sizeof (*native_fm_bank(b)))==(int)sizeof (*native_fm_bank(b)) && native_fm_valid(native_fm_bank(b))) { native_index(ENGI_FM6,b); continue; }
#endif
        native_fm_empty(b);
        for(uint32_t j=0;j<16u;j++){
            uint32_t k=b*16u+j;uint8_t pk[FM6_PACKED];
            if(!upf_fm6(k))continue;
            if(upf_get(k,pk))continue;
            memcpy(native_fm_bank(b)->tone[j],pk,sizeof pk);native_fm_bank(b)->used|=1u<<j;
        }
        /* Empty objects are completion markers too: never resurrect an erased
         * native preset from its retained legacy record on a later boot. */
        if(native_save_bank(ENGI_FM6,b)==2){ui_message("PRESET SAVE ERROR");return;}
    }
    uint32_t migrated=0;
    for(uint32_t b=0;b<8u;b++){
        cz_bank_t *old=cz_bank_load(b);
        if(cz_bank_saved)(*native_cz_bank(b))=*old;else cz_bank_empty(native_cz_bank(b),b);
        migrated+=native_cz_bank(b)->ver==2u;
        native_index(ENGI_CZ,b);
    }
    if(migrated==8u){native_release_shared();return;}
    /* Imported ordinary CZ presets are copied into free native slots, without
     * overwriting the historical 128-tone collection. Exact duplicates skip. */
    for(uint32_t k=0;k<UP_SLOTS;k++)if(up_used(k)){
        uint8_t raw[CZ_BYTES];if(!up_cz_raw(up_rec(k),raw))continue;
        uint32_t i,free=NATIVE_CZ_SLOTS;
        for(i=0;i<NATIVE_CZ_SLOTS;i++){if(!native_used(ENGI_CZ,i)){if(free==NATIVE_CZ_SLOTS)free=i;}else if(!memcmp(native_raw(ENGI_CZ,i),raw,CZ_BYTES))break;}
        if(i<NATIVE_CZ_SLOTS || free==NATIVE_CZ_SLOTS)continue;
        if(!cz_bank_valid(native_cz_bank(free/16u)))cz_bank_empty(native_cz_bank(free/16u),free/16u);
        cz_bank_t old=(*native_cz_bank(free/16u));memcpy(native_cz_bank(free/16u)->tone[free%16u].raw,raw,CZ_BYTES);native_cz_bank(free/16u)->used|=1u<<(free%16u);
        if(native_save_bank(ENGI_CZ,free/16u)==2){(*native_cz_bank(free/16u))=old;ui_message("PRESET SAVE ERROR");return;}
        cz_bank_cached=255;
    }
    for(uint32_t b=0;b<8u;b++){cz_bank_t *c=native_cz_bank(b);if(!cz_bank_valid(c))cz_bank_empty(c,b);c->ver=2;if(native_save_bank(ENGI_CZ,b)==2){ui_message("PRESET SAVE ERROR");return;}}
    native_release_shared();
}
/* Mutations keep the last committed tone in RAM if flash fails. */
static int native_put(uint32_t e,uint32_t k,const uint8_t *raw)
{
    if(e==ENGI_PROPHET){if(transport_busy())return 2;p5_patch_t p;if(raw)memcpy(&p,raw,sizeof p);int rc=p5_user_put(k,raw?&p:0);if(!rc){if(!raw)for(uint32_t t=0;t<NTRK;t++)if(trk[t].user_native && trk[t].eng_req==e && trk[t].user==k+1u)trk[t].user=0;up_gen++;sync_reload=1;ui.force=1;}return rc;}
    if(k>=native_limit(e))return 1;
    if(raw){if(e==ENGI_CZ){if(!cz_patch_valid(raw))return 1;}else for(uint32_t j=0;j<FM6_PACKED;j++)if(raw[j]>127u)return 1;}
#if MELODEE_FLASH
    if(!flash_ok)return 2;
#endif
    if(transport_busy())return 2;
    if(e==ENGI_FM6 && !native_fm_valid(native_fm_bank(k/16u)))native_fm_empty(k/16u);
    if(e==ENGI_CZ && !cz_bank_valid(native_cz_bank(k/16u))){cz_bank_empty(native_cz_bank(k/16u),k/16u);native_cz_bank(k/16u)->ver=2;}
    uint8_t old[CZ_BYTES];memcpy(old,native_raw(e,k),native_size(e));
    uint32_t *used=e==ENGI_FM6?&native_fm_bank(k/16u)->used:&native_cz_bank(k/16u)->used,mask=*used;
    if(raw){memcpy((void *)native_raw(e,k),raw,native_size(e));*used|=1u<<(k%16u);}else *used&=~(1u<<(k%16u));
    int rc=native_save_bank(e,k/16u);
    if(rc==2){memcpy((void *)native_raw(e,k),old,native_size(e));*used=mask;return rc;}
    cz_bank_cached=255;up_gen++;sync_reload=1;ui.force=1;
    if(!raw){favorite_set(e==ENGI_FM6?USER_NATIVE_FM:USER_NATIVE_CZ,k,0);for(uint32_t t=0;t<NTRK;t++)if(trk[t].user_native && trk[t].eng_req==e && trk[t].user==k+1u)trk[t].user=0;settings_save();}
    return rc;
}
/* A DX7 cartridge replaces one half of the FM6 pool. Validate before writing,
 * then commit once per 16-tone storage object. A failed object keeps its old
 * tones; *saved reports any preceding object already committed. */
static int native_fm_import32(uint32_t first, const uint8_t *raw, uint32_t *saved)
{
    *saved = 0;
    if (first != 0u && first != 32u) return 1;
    for (uint32_t j = 0; j < 32u * FM6_PACKED; j++) if (raw[j] > 127u) return 1;
#if MELODEE_FLASH
    if (!flash_ok) return 2;
#endif
    if (transport_busy()) return 2;
    int result = 0;
    for (uint32_t b = first / 16u; b < first / 16u + 2u; b++) {
        native_fm_t *bank = native_fm_bank(b), old = *bank;
        if (!native_fm_valid(bank)) native_fm_empty(b);
        memcpy(bank->tone, raw + (b - first / 16u) * 16u * FM6_PACKED, sizeof bank->tone);
        bank->used = 0xffffu;
        int rc = native_save_bank(ENGI_FM6, b);
        if (rc == 2) { *bank = old; return rc; }
        if (rc == 3) result = 3;
        *saved += 16u;
        up_gen++; sync_reload = 1; ui.force = 1;
    }
    return result;
}
static int native_load(uint32_t e,uint32_t k,uint32_t tr)
{
    if(tr>=NTRK || !native_used(e,k))return 1;
    uint8_t loaded[CZ_BYTES];memcpy(loaded,native_raw(e,k),native_size(e));
    track_t *t=&trk[tr];load_begin(t,UNDO_SOUND);panic_req|=(uint8_t)(1u<<tr);fm1_irq_off();
    uint32_t previous=t->eng_req;
    t->eng_req=(uint8_t)e;t->preset=0;t->user=(uint8_t)(k+1u);t->user_native=1;
    if(previous!=e || e==ENGI_PROPHET)for(uint32_t j=0;j<8u;j++)t->p[P_E0+j]=ENGINES[e]->edit[j].def;
    if(e==ENGI_FM6){uint8_t v[FP_SIZE+1u];fm6_unpack(loaded,v);fm6_set_patch(tr,v);}
    else if(e==ENGI_PROPHET){memcpy(&p5_patch[tr],loaded,sizeof(p5_patch_t));p5_ready[tr]=1;p5_track_accept(t);}
    else {memcpy(cz_patch[tr].raw,loaded,CZ_BYTES);t->p[P_E7]=CZ_NATIVE;cz_track_accept(t);}
    fm1_irq_on();load_end(t);sync_reload=1;ui.force=1;return 0;
}
static int native_store(uint32_t e,uint32_t k,uint32_t tr,const char *name)
{
    if(tr>=NTRK || trk[tr].eng_req!=e)return 1;
    uint8_t raw[CZ_BYTES];if(e==ENGI_FM6)fm6_pack(fm6_patch[tr],raw);else if(e==ENGI_PROPHET)memcpy(raw,p5_patch_of(&trk[tr]),sizeof(p5_patch_t));else memcpy(raw,cz_patch[tr].raw,CZ_BYTES);
    if(name && name[0]){uint32_t off=e==ENGI_PROPHET?P5_NAME:e==ENGI_FM6?118u:128u,n=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u;memset(raw+off,' ',n);for(uint32_t i=0;i<n && name[i];i++)raw[off+i]=(uint8_t)name[i];}
    return native_put(e,k,raw);
}
/* Older archives carry two 32-slot owned-voice objects. Copy their restored
 * records into the corresponding independent native slots, then commit in the
 * same order used at boot. */
static int native_import_owned(uint32_t group)
{
    for (uint32_t b = group*2u; b < group*2u+2u; b++) {
        if(!native_fm_valid(native_fm_bank(b)))native_fm_empty(b);
        native_fm_t *f = native_fm_bank(b);
        for (uint32_t j = 0; j < 16u; j++) {
            uint32_t k=b*16u+j; uint8_t pk[FM6_PACKED]; f->used &= ~(1u<<j);
            if(upf_fm6(k)){
                int ok=!upf_get(k,pk);
                if(ok){memcpy(f->tone[j],pk,FM6_PACKED);f->used|=1u<<j;}
            }
        }
        if (native_save_bank(ENGI_FM6,b)==2) return 2;
    }
    return 0;
}
static uint32_t user_limit(void) { uint32_t n=native_limit(TSEL->eng_req);return n?n:UP_SLOTS; }
static int user_used(uint32_t k) { return native_limit(TSEL->eng_req)?native_used(TSEL->eng_req,k):up_used(k); }
static void user_name(uint32_t k,char *out) { if(native_limit(TSEL->eng_req))native_name(TSEL->eng_req,k,out);else up_name(k,out); }
static void user_label(char *out,uint32_t k)
{
    if(!native_limit(TSEL->eng_req)){up_slot_label(out,k);return;}
    out[0]=TSEL->eng_req==ENGI_PROPHET?'P':TSEL->eng_req==ENGI_FM6?'F':'Z';out[1]=(char)('0'+(k+1u)/100u);out[2]=(char)('0'+(k+1u)/10u%10u);out[3]=(char)('0'+(k+1u)%10u);out[4]=0;
}
static void user_ui_named(uint32_t op,uint32_t k,const char *name)
{
    uint32_t e=TSEL->eng_req;if(!native_limit(e)){up_ui_named(op,k,name);return;}
    int rc;
    if(op==0u)rc=native_load(e,k,song.sel);
    else if(op==1u)rc=native_put(e,k,0);
    else if(op==2u)rc=native_store(e,k,song.sel,name);
    else if(!native_used(e,k))rc=1;
    else {uint8_t raw[CZ_BYTES];memcpy(raw,native_raw(e,k),native_size(e));uint32_t off=e==ENGI_PROPHET?P5_NAME:e==ENGI_FM6?118u:128u,n=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u;memset(raw+off,' ',n);for(uint32_t i=0;i<n && name && name[i];i++)raw[off+i]=(uint8_t)name[i];rc=native_put(e,k,raw);}
    if(rc==1)ui_message("EMPTY SLOT");else if(rc==2)ui_message(transport_busy()?"STOP TO SAVE":"SAVE ERROR");else {char label[5];user_label(label,k);ui_say(op==0?"LOADED ":op==1?"ERASED ":op==3?"RENAMED ":"SAVED ",label);}
    ui.force=1;
}
