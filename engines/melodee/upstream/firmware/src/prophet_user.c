/* SPDX-License-Identifier: GPL-3.0-only */
/* 128 native programs in five independent A/B pairs. All wire metadata and
 * opaque bytes survive storage. The final pair contains 24 slots. */
#define P5_USER_SLOTS 128u
#define P5_BANK_SLOTS 26u
#define P5_BANK_MAGIC 0x31553550u
#include "melodee_prophet_factory.h"
typedef struct { uint32_t magic, used, favorites; p5_patch_t patch[P5_BANK_SLOTS]; } p5_bank_t;
_Static_assert(sizeof(p5_bank_t)==3600u, "native Prophet bank extent");
static struct { uint32_t used, favorites; char name[P5_BANK_SLOTS][21]; } p5_meta[5] __attribute__((section(".pool")));
static uint8_t p5_meta_ready[5];
#if MELODEE_FLASH
static p5_bank_t p5_cache __attribute__((section(".pool")));
static uint8_t p5_cached=255;
#else
static p5_bank_t p5_host[5] __attribute__((section(".pool")));
#endif
static uint32_t p5_bank_mask(uint32_t bank) { return (1u<<(bank==4u?24u:26u))-1u; }
/* Missing storage starts empty. Sequential's programs already live in the
 * separate 201-entry factory browser; a valid saved bank always wins. */
static void p5_bank_defaults(p5_bank_t *b,uint32_t bank)
{
    (void)bank;
    memset(b,0,sizeof *b);b->magic=P5_BANK_MAGIC;
}
static int p5_bank_valid(const p5_bank_t *b,uint32_t bank)
{
    if(bank>=5u || b->magic!=P5_BANK_MAGIC || (b->used&~p5_bank_mask(bank)) ||
       (b->favorites&~(p5_bank_mask(bank)|(bank?0u:0x80000000u))))return 0;
    for(uint32_t k=0;k<P5_BANK_SLOTS;k++)if((b->used>>k&1u)&&!p5_patch_valid(&b->patch[k]))return 0;
    return 1;
}
static void p5_index(uint32_t bank,const p5_bank_t *b)
{
    p5_meta_ready[bank]=1;p5_meta[bank].used=b->used;p5_meta[bank].favorites=b->favorites;
    for(uint32_t k=0;k<P5_BANK_SLOTS;k++){
        p5_meta[bank].name[k][0]=0;
        if(b->used>>k&1u)p5_patch_name(p5_meta[bank].name[k],&b->patch[k]);
    }
}
static p5_bank_t *p5_user_bank(uint32_t bank)
{
    if(bank>=5u)return 0;
#if MELODEE_FLASH
    if(p5_cached!=bank){
        int n=flash_ok?st_load(OBJ_P5BANK0+bank,&p5_cache,sizeof p5_cache):-1;
        if(n!=sizeof p5_cache || !p5_bank_valid(&p5_cache,bank))p5_bank_defaults(&p5_cache,bank);
        p5_cached=(uint8_t)bank;p5_index(bank,&p5_cache);
    }
    return &p5_cache;
#else
    if(!p5_bank_valid(&p5_host[bank],bank))p5_bank_defaults(&p5_host[bank],bank);
    p5_index(bank,&p5_host[bank]);return &p5_host[bank];
#endif
}
static void p5_user_reset(void)
{
    memset(p5_meta_ready,0,sizeof p5_meta_ready);
#if MELODEE_FLASH
    p5_cached=255;
#endif
}
static int p5_user_used(uint32_t slot)
{
    if(slot>=P5_USER_SLOTS)return 0;uint32_t bank=slot/P5_BANK_SLOTS;
    if(!p5_meta_ready[bank])p5_user_bank(bank);
    return (p5_meta[bank].used>>(slot%P5_BANK_SLOTS))&1u;
}
static const char *p5_user_name(uint32_t slot)
{
    return p5_user_used(slot)?p5_meta[slot/P5_BANK_SLOTS].name[slot%P5_BANK_SLOTS]:"";
}
static int p5_user_get(uint32_t slot,p5_patch_t *patch)
{
    if(!p5_user_used(slot))return 2;
    *patch=p5_user_bank(slot/P5_BANK_SLOTS)->patch[slot%P5_BANK_SLOTS];return 0;
}
static int p5_user_save_bank(uint32_t bank)
{
#if MELODEE_FLASH
    if(!flash_ok || st_save(OBJ_P5BANK0+bank,p5_user_bank(bank),sizeof(p5_bank_t)))return 2;
#endif
    p5_index(bank,p5_user_bank(bank));return 0;
}
static int p5_user_put(uint32_t slot,const p5_patch_t *patch)
{
    if(slot>=P5_USER_SLOTS || (patch&&!p5_patch_valid(patch)))return 1;
    uint32_t bank=slot/P5_BANK_SLOTS,k=slot%P5_BANK_SLOTS;p5_bank_t *b=p5_user_bank(bank),old=*b;
    if(patch){b->patch[k]=*patch;b->used|=1u<<k;}
    else {b->used&=~(1u<<k);b->favorites&=~(1u<<k);memset(&b->patch[k],0,sizeof b->patch[k]);}
    if(p5_user_save_bank(bank)){*b=old;p5_index(bank,b);return 2;}
    return 0;
}
static int p5_favorite_has(uint32_t slot,int factory)
{
    if(factory){
        const uint8_t *row=favorites.factory[0];
        if(slot>P5_FACTORY_N)return 0;
        if(row[28]=='P'&&row[29]=='5'&&row[30]=='F'&&row[31]==1u)
            return (row[2u+slot/8u]>>(slot%8u))&1u;
        return slot==0u && (p5_user_bank(0)->favorites>>31);
    }
    if(slot>=P5_USER_SLOTS)return 0;
    uint32_t bank=slot/P5_BANK_SLOTS,k=slot%P5_BANK_SLOTS;
    if(!p5_meta_ready[bank])p5_user_bank(bank);
    return (p5_meta[bank].favorites>>k)&1u;
}
static int p5_favorite_set(uint32_t slot,int on,int factory)
{
    if(factory){
        uint8_t *row=favorites.factory[0];
        if(slot>P5_FACTORY_N || p5_favorite_has(slot,1)==!!on)return 0;
        if(row[28]!='P'||row[29]!='5'||row[30]!='F'||row[31]!=1u){
            int old_init=(p5_user_bank(0)->favorites>>31)&1u;
            memset(row+2,0,26u);row[2]=(uint8_t)old_init;
            row[28]='P';row[29]='5';row[30]='F';row[31]=1u;
        }
        if(on)row[2u+slot/8u]|=(uint8_t)(1u<<(slot%8u));
        else row[2u+slot/8u]&=(uint8_t)~(1u<<(slot%8u));
        return 1;
    }
    if(slot>=P5_USER_SLOTS || transport_busy() || !p5_user_used(slot))return 0;
    uint32_t bank=slot/P5_BANK_SLOTS,k=slot%P5_BANK_SLOTS;
    p5_bank_t *b=p5_user_bank(bank);uint32_t old=b->favorites;
    if(on)b->favorites|=1u<<k;else b->favorites&=~(1u<<k);
    if(old==b->favorites)return 0;
    if(p5_user_save_bank(bank)){b->favorites=old;p5_index(bank,b);return 0;}
    return 1;
}

static void p5_send(uint32_t tr)
{
#if MELODEE_OTA
    p5_patch_t p=*p5_patch_of(&trk[tr%NTRK]);uint8_t frame[P5_FRAME_MAX];p.command=3;
    uint32_t n=p5_patch_encode(&p,frame,sizeof frame);if(n)ota_wire_send(frame,n);
#else
    (void)tr;
#endif
}
