/* SPDX-License-Identifier: GPL-3.0-only */
/* Engine-specific user preset slots. Command 78: engine, operation, slot/start,
 * then operation arguments. Replies begin engine, operation, slot/start, rc.
 * Native tones are VMEM (128 x 7-bit) or CZ-1 (144 low-first nibble pairs). */
#define ED_NATIVE 78u
static int ed_native_handle(uint32_t cmd,const uint8_t *a,uint32_t n)
{
    if(cmd!=ED_NATIVE)return 0;
    uint32_t e=n?a[0]:127u,op=n>1u?a[1]:127u,k=n>2u?a[2]:127u,rc=n<3u || !native_limit(e) || op>7u || k>=(op==7u?NTRK:native_limit(e));
    uint8_t raw[CZ_BYTES];
    if(!rc){
        if(op==0u){if(n!=4u || !a[3] || a[3]>16u)rc=1;}
        else if(op==1u){if(n!=3u)rc=1;else if(!native_used(e,k))rc=2;}
        else if(op==2u){
            if(n!=3u+(e==ENGI_FM6?FM6_PACKED:2u*native_size(e)))rc=1;
            if(!rc){if(e==ENGI_FM6)memcpy(raw,a+3,FM6_PACKED);else for(uint32_t j=0;j<native_size(e);j++){if(a[3u+2u*j]>15u || a[4u+2u*j]>15u){rc=1;break;}raw[j]=a[3u+2u*j]|a[4u+2u*j]<<4;}}
            if(!rc)rc=ed_flash_stop()?3u:(uint32_t)native_put(e,k,raw);
        }else if(op==3u || op==4u){
            if(n!=4u || a[3]>=NTRK)rc=1;
            else if(op==4u)rc=(uint32_t)native_load(e,k,a[3]);
            else rc=ed_flash_stop()?3u:(uint32_t)native_store(e,k,a[3],0);
        }else if(op==5u){if(n!=3u)rc=1;else rc=ed_flash_stop()?3u:(uint32_t)native_put(e,k,0);}
        else if(op==7u){if(n!=3u)rc=1;}
        else {
            uint32_t len=e==ENGI_PROPHET?20u:e==ENGI_FM6?10u:16u;
            if(n<4u || n>3u+len || !native_used(e,k))rc=1;
            for(uint32_t j=3u;!rc && j<n;j++)if(a[j]<32u || a[j]>126u)rc=1;
            if(!rc){memcpy(raw,native_raw(e,k),native_size(e));uint32_t off=e==ENGI_PROPHET?P5_NAME:e==ENGI_FM6?118u:128u;memset(raw+off,' ',len);memcpy(raw+off,a+3,n-3u);rc=ed_flash_stop()?3u:(uint32_t)native_put(e,k,raw);}
        }
    }
    if(rc==3u && !transport_busy())rc=0; /* RAM-only writes use the normal editor success convention. */
    ed_b(e);ed_b(op);ed_b(k);ed_b(rc);
    if(!rc && op==7u){uint32_t u=trk[k].eng_req==e && trk[k].user_native && user_of(&trk[k])<USER_NONE?trk[k].user:0u;ed_b(u&127u);ed_b(u>>7);}
    if(!rc && op==0u){uint32_t count=a[3];if(k+count>native_limit(e))count=native_limit(e)-k;ed_b(count);ed_b(native_limit(e)&127u);ed_b(native_limit(e)>>7);for(uint32_t j=k;j<k+count;j++){char name[13];ed_b(native_used(e,j));if(native_used(e,j))native_name(e,j,name);else name[0]=0;ed_str(name,12);}}
    if(!rc && op==1u){const uint8_t *r=native_raw(e,k);for(uint32_t j=0;j<native_size(e);j++){if(e!=ENGI_FM6){ed_b(r[j]&15u);ed_b(r[j]>>4);}else ed_b(r[j]);}}
    return 1;
}
