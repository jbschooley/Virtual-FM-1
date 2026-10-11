/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */

static uint8_t cz_tx[295],cz_handshake;
static uint32_t cz_wait_at;
static int cz_sx_track(uint32_t ch)
{
    if(song.sel<NTRK&&trk[song.sel].eng_req==ENGI_CZ)return song.sel;
    if(ch<NTRK&&trk[ch].eng_req==ENGI_CZ)return (int)ch;
    for(uint32_t tr=0;tr<NTRK;tr++)if(trk[tr].eng_req==ENGI_CZ)return (int)tr;
    return -1;
}
static void cz_sx_header(uint8_t ch){cz_tx[0]=0xf0;cz_tx[1]=0x44;cz_tx[2]=cz_tx[3]=0;cz_tx[4]=ch;cz_tx[5]=0x30;}
static void cz_sx_send(uint32_t tr,uint8_t ch,int native,int continuation)
{
#if MELODEE_OTA
    uint8_t data[144];uint32_t n=native?144:128;memcpy(data,cz_patch[tr].raw,CZ_BYTES);if(!native){static const uint8_t kw[]={0,25,51,78,106,134,163,193,223,255};for(uint32_t l=0;l<2;l++){uint32_t o=l*57u;data[16+o]&=15;data[19+o]=kw[data[18+o]];for(uint32_t j=20;j<=54;j+=17)data[j+o]&=15;}}cz_sx_header(ch);
    for(uint32_t j=0;j<n;j++){cz_tx[6+2*j]=data[j]&15;cz_tx[7+2*j]=data[j]>>4;}
    cz_tx[6+2*n]=0xf7;ota_wire_send(cz_tx+(continuation?6:0),2*n+1+(continuation?0:6));
#endif
}
static int cz_sx_import(uint32_t tr,const uint8_t *b,uint32_t n)
{
    uint8_t data[CZ_BYTES];if(n!=256&&n!=288)return 0;
    for(uint32_t j=0;j<n;j++)if(b[j]>15)return 0;
    memcpy(data,cz_patch[tr].raw,CZ_BYTES);
    for(uint32_t j=0;j<n/2;j++)data[j]=(uint8_t)(b[j*2]|b[j*2+1]<<4);
    if(n==256){static const uint8_t kw[]={0,31,44,57,70,83,96,111,146,255};for(uint32_t l=0;l<2;l++){uint32_t o=l*57u;if((data[18+o]&15)>9)return 0;data[16+o]&=15;data[19+o]=kw[data[18+o]&15];for(uint32_t j=20;j<=54;j+=17)data[j+o]|=240;}}
    if(!cz_patch_valid(data))return 0;
    track_t *t=&trk[tr];load_begin(t,UNDO_SOUND);panic_req|=1u<<tr;
    fm1_irq_off();memcpy(cz_patch[tr].raw,data,CZ_BYTES);t->p[P_E0]=t->p[P_E1]=0;t->p[P_E7]=CZ_NATIVE;cz_track_accept(t);fm1_irq_on();load_end(t);sync_reload=1;

    ui_message("CZ TONE LOADED");ui.force=1;return 1;
}
static void cz_service(void)
{
    if(cz_rx_abort || (cz_handshake && (fm1_ms-cz_wait_at>3000u || !usb.config))){
#if MELODEE_OTA
        if(cz_handshake&&usb.config){cz_tx[0]=0xf7;ota_wire_send(cz_tx,1);}
#endif
        fm1_irq_off();cz_rx_on=cz_rx_req=cz_rx_go=cz_rx_ready=cz_rx_abort=0;fm1_irq_on();cz_handshake=0;return;
    }
    if(!cz_rx_req&&!cz_rx_ready&&!cz_rx_go)return;
    RING_PUBLISH();uint32_t n=cz_rx_n;uint8_t cmd=cz_rx[5],ch=cz_rx[4];int tr=cz_sx_track(ch&15);
    if(n<7||cz_rx[2]||cz_rx[3]||(ch&0xf0)!=0x70||tr<0)goto done;
    if(cz_rx_ready){
        if(cmd==0x20||cmd==0x21){
            int ok=((cmd==0x20&&n==264)||(cmd==0x21&&n==296))&&cz_sx_import((uint32_t)tr,cz_rx+7,n-8);
            if(ok||cz_handshake){
#if MELODEE_OTA
                if(cz_handshake){cz_tx[0]=0xf7;ota_wire_send(cz_tx,1);}else{cz_sx_header(ch);cz_tx[6]=0xf7;ota_wire_send(cz_tx,7);}
#endif
            }
            if(!ok)ui_message("CZ INVALID TONE");
        }else if(cmd==0x30&&(n==263||n==295))cz_sx_import((uint32_t)tr,cz_rx+6,n-7);
        else if((cmd==0x10||cmd==0x11)&&n==8&&cz_rx[6]==0x60)cz_sx_send((uint32_t)tr,ch,cmd==0x11,0);

        cz_handshake=0;goto done;
    }
    if(cz_rx_go){cz_sx_send((uint32_t)tr,ch,cmd==0x11,1);cz_rx_go=0;cz_rx_req=0;return;}
    if(cz_rx_req){
        if((cmd==0x10||cmd==0x11||cmd==0x20||cmd==0x21)&&cz_rx[6]==0x60){
#if MELODEE_OTA
            cz_sx_header(ch);ota_wire_send(cz_tx,6);
#endif
            cz_handshake=1;cz_wait_at=fm1_ms;
        }
        cz_rx_req=0;return;
    }
    return;
done:cz_rx_req=cz_rx_go=0;RING_PUBLISH();cz_rx_ready=0;
}
