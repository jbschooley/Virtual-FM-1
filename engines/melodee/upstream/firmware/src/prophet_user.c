/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Previous current-format Prophet banks remain valid conversion/archive inputs.
 * Factory stars keep their existing settings row. User stars live in records. */
#define P5_USER_SLOTS UP_SLOTS
#define P5_BANK_SLOTS 26u
#define P5_BANK_MAGIC 0x31553550u
#include "melodee_prophet_factory.h"
typedef struct {uint32_t magic,used,favorites;p5_patch_t patch[P5_BANK_SLOTS];} p5_bank_t;
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

static int p5_user_get(uint32_t slot,p5_patch_t *patch)
{
    if(!native_used(ENGI_PROPHET,slot))return 2;memcpy(patch,native_raw(ENGI_PROPHET,slot),sizeof *patch);return 0;
}
static int p5_favorite_has(uint32_t slot,int factory)
{
    if(!factory)return up_favorite(slot);
    const uint8_t *row=favorites.factory[0];
    return slot<=P5_FACTORY_N && row[28]=='P' && row[29]=='5' && row[30]=='F' && row[31]==1u && (row[2u+slot/8u]>>(slot%8u)&1u);
}
static int p5_favorite_set(uint32_t slot,int on,int factory)
{
    if(!factory)return up_favorite_set(slot,on);
    uint8_t *row=favorites.factory[0];if(slot>P5_FACTORY_N || p5_favorite_has(slot,1)==!!on)return 0;
    if(row[28]!='P'||row[29]!='5'||row[30]!='F'||row[31]!=1u){memset(row+2,0,26u);row[28]='P';row[29]='5';row[30]='F';row[31]=1u;}
    if(on)row[2u+slot/8u]|=(uint8_t)(1u<<(slot%8u));else row[2u+slot/8u]&=(uint8_t)~(1u<<(slot%8u));return 1;
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
