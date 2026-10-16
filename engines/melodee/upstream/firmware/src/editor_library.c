/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Command 80: operation, slot low/high, arguments. Replies echo operation,
 * slot low/high, rc. All slots are absolute positions in the shared library. */
#define ED_LIBRARY 80u
static int ed_library_handle(uint32_t cmd,const uint8_t *a,uint32_t n)
{
    if(cmd!=ED_LIBRARY)return 0;
    uint32_t op=n?a[0]:127u,k=n>=3u?a[1]|(uint32_t)a[2]<<7:UP_SLOTS;
    uint32_t rc=n<3u || op>6u || k>=UP_SLOTS;
    if(!rc && op==0u){if(n!=4u || !a[3] || a[3]>16u)rc=1;}
    else if(!rc && op==1u){if(n!=3u)rc=1;}
    else if(!rc && op==2u){
        uint32_t len=0;while(4u+len<n && a[4u+len])len++;
        uint32_t pos=5u+len,end=pos+2u*P_COUNT+32u;
        up_rec_t r;memset(&r,0,sizeof r);r.used=UP_USED;r.ver=UP_VER;r.engine=n>3u?a[3]:255u;r.np=P_COUNT;
        if(n<end+1u || len<1u || len>20u || r.engine>=NENGINES || !eng_ok(r.engine))rc=1;
        for(uint32_t j=0;!rc && j<len;j++)if(a[4u+j]<32u || a[4u+j]>126u)rc=1;
        int16_t values[P_COUNT];
        for(uint32_t j=0;!rc && j<P_COUNT;j++){values[j]=(int16_t)ed_rv(a+pos+2u*j);if(values[j]<-64 || values[j]>127)rc=1;}
        if(!rc){
            uint32_t defaults=a[end]&64u,preserve_name=a[end]&32u,kind=a[end]&31u;
            if(kind){
                uint32_t e=kind==1u?ENGI_FM6:kind==2u?ENGI_CZ:kind==3u?ENGI_PROPHET:kind==4u?ENGI_DRUM:255u;
                uint8_t raw[CZ_BYTES];uint32_t size=n>=end+3u?a[end+1u]|(uint32_t)a[end+2u]<<7:0;
                if(e!=r.engine || size!=(kind==4u?DRUM_WIRE_BYTES:native_size(e)) || n<end+3u || ed_unpack7(a+end+3u,n-end-3u,raw,sizeof raw)!=size)rc=1;
                else if(kind==4u){
                    drum_patch_t p;if(!drum_patch_unpack(&p,raw))rc=1;
                    else {r.ver=UP_VER_DRUM;r.np=P_COUNT;for(uint32_t j=0;j<P_COUNT;j++)up_set_value(&r,j,values[j]);memcpy(r.cz_extra,raw,46u);memcpy(r.packed+r.np,raw+46u,8u);}
                }else {shared_native_record(&r,e,raw);r.np=defaults?0:sizeof UP_NATIVE_PARAMS;for(uint32_t j=0;j<r.np;j++)r.cz_extra[j]=(uint8_t)(values[UP_NATIVE_PARAMS[j]]+64);}
            }else if(n!=end+1u || len>12u || r.engine==ENGI_PROPHET || r.engine==ENGI_FM6)rc=1;
            else {for(uint32_t j=0;j<P_COUNT;j++)up_set_value(&r,j,values[j]);}
            if(!rc){char name[21];memcpy(name,a+4u,len);name[len]=0;up_set_name(&r,k,name);if(up_native(&r) && !preserve_name)up_native_name(&r,name);if(!up_valid(&r))rc=1;}
            if(!rc)rc=ed_flash_stop()?2u:(uint32_t)up_put(k,&r);
        }
    }else if(!rc && op==3u){
        char name[21];uint32_t len=n-4u;
        if(n<4u || a[3]>=NTRK || len>20u)rc=1;
        for(uint32_t j=0;!rc && j<len;j++)if(a[4u+j]<32u || a[4u+j]>126u)rc=1;
        if(!rc){memcpy(name,a+4u,len);name[len]=0;rc=ed_flash_stop()?2u:(uint32_t)up_store_to(k,a[3],name);}
    }else if(!rc && op==4u){if(n!=4u || a[3]>=NTRK)rc=1;else rc=up_load_to(k,a[3]);}
    else if(!rc && op==5u){if(n!=3u)rc=1;else rc=ed_flash_stop()?2u:(uint32_t)up_put(k,0);}
    else if(!rc && op==6u){
        uint32_t len=n-3u;char name[21];if(!len || len>20u || !up_used(k))rc=1;
        for(uint32_t j=0;!rc && j<len;j++)if(a[3u+j]<32u || a[3u+j]>126u)rc=1;
        if(!rc){memcpy(name,a+3u,len);name[len]=0;rc=ed_flash_stop()?2u:(uint32_t)up_rename(k,name);}
    }
    if(rc==3u)rc=0;
    ed_b(op);ed_b(k&127u);ed_b(k>>7);ed_b(rc);
    if(rc)return 1;
    if(op==0u){
        uint32_t count=a[3];if(count>UP_SLOTS-k)count=UP_SLOTS-k;
        ed_b(count);ed_b(UP_SLOTS&127u);ed_b(UP_SLOTS>>7);
        for(uint32_t j=k;j<k+count;j++){char name[13]={0};int used=up_used(j);if(used || up_old(j))up_name(j,name);ed_b(used);ed_b(used?up_engine(j):0u);ed_b(up_old(j));ed_str(name,12);}
    }else if(op==1u){
        int used=up_used(k);up_rec_t r;memset(&r,0,sizeof r);if(used)r=*up_rec(k);
        int16_t values[P_COUNT];if(used)up_values(&r,values);
        ed_b(used);ed_b(used?up_engine(k):0u);ed_str(r.name,12u);
        for(uint32_t j=0;j<P_COUNT;j++)ed_v(used?values[j]:0);
        /* Phrases no longer load; expose an empty legacy pattern. */
        for(uint32_t j=0;j<32u;j++)ed_b(0);
        uint32_t kind=used && (up_native(&r)||up_native_cz(&r))?(r.engine==ENGI_FM6?1u:r.engine==ENGI_CZ?2u:3u):0u;
        if(used && r.engine==ENGI_DRUM && (r.ver==UP_VER_DRUM || r.ver==UP_VER_DRUM_GRID))kind=4u;
        ed_b(kind);
        if(kind==4u){drum_patch_t p;uint8_t raw[DRUM_WIRE_BYTES];up_drum_raw(&r,&p);drum_patch_pack(raw,&p);ed_b(DRUM_WIRE_BYTES);ed_b(0);ed_bk_pack(raw,sizeof raw);}
        else if(kind){uint32_t size=native_size(r.engine);ed_b(size&127u);ed_b(size>>7);ed_bk_pack(r.packed,size);}
    }
    return 1;
}
