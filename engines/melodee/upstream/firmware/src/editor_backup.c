/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Complete musical archive: no raw addresses are accepted. Objects are staged
 * in main-loop RAM and fully validated before the existing A/B commit path writes.
 * Runtime and saved projects include all 32 pattern banks. Retired user sample
 * slots are absent from the inventory and cannot be restored over project data.
 */
#ifndef ED_BK_FLASH_PTR
#define ED_BK_FLASH_PTR(off) fm1_xip_ptr(off)
#endif
static const uint8_t ED_BK_IDS[28] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27};
#define ED_BK_N ((uint32_t)sizeof ED_BK_IDS)
#define ED_BK_MAX ((uint32_t)sizeof proj_wire_u)
#define ED_BK_RAW ((uint8_t *)&proj_wire_u)  /* reuse the existing serialized main-loop scratch */
static struct { persist_t p; tmpl_t t; } ed_bk_set;   /* id 1: the settings
                                                                                          * record (+ the template) */
static uint32_t ed_bk_set_len;
static uint8_t ed_bk_valid, ed_bk_put, ed_bk_id, ed_bk_gen;
static uint32_t ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms, ed_bk_usb;
#if MELODEE_FLASH
static uint32_t ed_bk_source_obj, ed_bk_source_copy, ed_bk_source_crc;
#endif
static void ed_bk_u32(uint32_t n) { for (uint32_t i = 0; i < 5u; i++) ed_b((n >> (i * 7u)) & 127u); }
static uint32_t ed_bk_r32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, mask = 0;
        for (uint32_t i = 0; i < k; i++) mask |= (uint32_t)(p[i] >> 7) << i;
        ed_b(mask);
        for (uint32_t i = 0; i < k; i++) ed_b(p[i]);
        p += k; n -= k;
    }
}
static const uint8_t *ed_bk_object(uint32_t id, uint32_t *len)
{
    *len = 0;
#if MELODEE_FLASH
    ed_bk_source_obj = OBJ_COUNT;
#endif
    if(id>=23u && id<=27u){p5_bank_t *b=p5_user_bank(id-23u);*len=sizeof *b;return (const uint8_t *)b;}
    if (id == 0u) { *len = BANK_STORE_SIZE; return ED_BK_RAW; }
    if (id == 1u) { *len = ed_bk_set_len; return (const uint8_t *)&ed_bk_set; }
    if (id >= 2u && id <= 5u) {
        uint32_t slot = id - 2u;
#if MELODEE_FLASH
        st_hdr_t h;
        uint32_t obj = OBJ_BANK0 + slot;
        int copy = st_current(obj, &h);
        if (copy < 0) { obj = OBJ_PROJECT0 + slot; copy = st_current(obj, &h); }
        if (copy >= 0) {
            *len = h.len; ed_bk_source_obj = obj; ed_bk_source_copy = (uint32_t)copy; ed_bk_source_crc = h.crc;
            return ED_BK_FLASH_PTR(st_sector(obj, (uint32_t)copy) + ST_PAYLOAD_OFF);
        }
        return ED_BK_RAW;                   /* empty: a valid pointer, zero length */
#else
        if (bank_valid(proj_bank_slot[slot], BANK_STORE_SIZE) &&
            !memcmp(proj_bank_slot[slot] + 8u, &proj_slot[slot], sizeof proj_slot[slot])) {
            *len = BANK_STORE_SIZE; return proj_bank_slot[slot];
        }
        if (project_used(slot)) *len = sizeof proj_slot[slot];
        return (const uint8_t *)&proj_slot[slot];
#endif
    }
    if (id == 6u || id == 7u || id == 17u || id == 18u) {
        uint32_t b = id < 8u ? id - 6u : id - 15u;
        if (up_cache_bank(b)->magic == UP_BANK_MAGIC) *len = sizeof (*up_cache_bank(0));
        return (const uint8_t *)up_cache_bank(b);
    }
    if (id == 8u) return ED_BK_RAW; /* retired bank: older archives may still restore it */
    if (id >= 19u && id <= 22u) {
        uint32_t nb=id<21u?id-17u:id-21u;
        if(native_fm_valid(native_fm_bank(nb))){*len=sizeof(native_fm_t);return (const uint8_t *)native_fm_bank(nb);}
        if(id>=21u)return ED_BK_RAW;
        uint32_t b = id - 19u;
        if (upf_valid(upf_bank((b) * UPF_SLOTS))) *len = sizeof (*upf_bank((b) * UPF_SLOTS));
        return (const uint8_t *)upf_bank((b) * UPF_SLOTS);
    }
    if(id>=9u && id<=16u){if(cz_bank_valid(native_cz_bank(id-9u))){*len=sizeof(cz_bank_t);return (const uint8_t *)native_cz_bank(id-9u);}cz_bank_t *b=cz_bank_load(id-9u);if(cz_bank_saved)*len=sizeof *b;return (const uint8_t *)b;}
    return 0;
}
static uint32_t ed_bk_capture(void)
{
    if (ed_flash_stop()) return 3;
    project_capture(&proj_scratch);
    if (!bank_pack(ED_BK_RAW, &proj_scratch, 1)) return 2;
    ed_bk_gen = ++proj_wire_gen;
#if MELODEE_FLASH
    ed_bk_set.p = persist_saved;                    /* fields absent from this build survive */
#else
    memset(&ed_bk_set.p, 0, sizeof ed_bk_set.p);
#endif
    settings_export(&ed_bk_set.p);
    ed_bk_set.t = tmpl;                             /* the record as it is stored: the template after the settings */
    ed_bk_set_len = sizeof ed_bk_set.p + (template_used() ? sizeof ed_bk_set.t : 0u);
    ed_bk_put = 0; ed_bk_valid = 1; ed_bk_usb = usb.resets;
    return 0;
}
static int ed_bk_panel_valid(const panel_t *p)
{
    uint32_t b = 0, e = 0;
    if (p->magic != PANEL_MAGIC) return 0;
    for (uint32_t i = 0; i < NB; i++) {
        if (p->btn[i] >= NB || (b & (1u << p->btn[i]))) return 0;
        b |= 1u << p->btn[i];
    }
    for (uint32_t i = 0; i < NE; i++) {
        if (p->enc[i] >= NE || (e & (1u << p->enc[i])) || (p->dir[i] != 1 && p->dir[i] != -1)) return 0;
        e |= 1u << p->enc[i];
    }
    return 1;
}
static uint32_t ed_bk_commit_inner(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t obj;
    if (ed_bk_pos != ed_bk_len || st_crc32(raw, ed_bk_len) != ed_bk_crc) return 2;
    if (ed_bk_id == 0u || (ed_bk_id >= 2u && ed_bk_id <= 5u)) {
        int full = bank_full(ed_bk_len);
        if (ed_bk_len && !(full ? bank_valid(raw, ed_bk_len) : proj_import(&proj_scratch, raw, (int)ed_bk_len))) return 2;
        if (full) { bank_upgrade(raw); ed_bk_len = BANK_STORE_SIZE; }
        if (ed_bk_id == 0u) {
            if (!ed_bk_len) return 2;
            if (project_restore_runtime(&proj_scratch)) return 2;
            if (full) bank_restore(raw);
            return 0;
        }
        if (ed_bk_len && !full) {
            proj_bound(&proj_scratch); chain_defaults(&proj_scratch.chain);
            if (!bank_pack(raw, &proj_scratch, 0)) return 2;
            ed_bk_len = BANK_STORE_SIZE;
        }
        if (proj_write_slot(ed_bk_id - 2u, raw, ed_bk_len)) return 4;
        if (proj_cur == ed_bk_id - 2u) proj_cur = PROJ_NO_SLOT;
        sync_reload = 1; ui.force = 1;
        return 0;
    } else if (ed_bk_id == 1u) {
        const persist_t *p = (const persist_t *)raw;         /* (PER4: Felucca 1.0's, without ext) */
        const uint8_t *t = raw + sizeof *p;                  /* a template after the settings (TPL6, TPL5) */
        uint32_t tl = ed_bk_len - sizeof *p, tm = 0, ts = 0;
        if (ed_bk_len >= sizeof *p + 8u) {
            memcpy(&ts, t + tl - 8u, 4);
            memcpy(&tm, t + tl - 4u, 4);
        }
        if (!(ed_bk_len==PERSIST_LEN4 ? p->magic==PERSIST_MAGIC4 :
              ed_bk_len>=sizeof *p && ed_bk_len<=sizeof ed_bk_set && p->magic==PERSIST_MAGIC &&
              p->ext.usb_off<=3u && p->ext.boot<=4u &&
              (ed_bk_len==sizeof *p || tmpl_blob_valid(t,tl))) || !palette_stored_ok(p->palette) ||
            p->lowcut > 2u || p->zoom > 1u || !hold_stored_ok(p->bold) || p->favorites.filter > 1u || !ed_bk_panel_valid(&p->panel)) return 2;
        if(tl && ed_bk_len>sizeof *p && !tmpl_blob_valid(t,tl))return 2;
        obj = OBJ_SETTINGS;
    } else if (ed_bk_id == 6u || ed_bk_id == 7u || ed_bk_id == 17u || ed_bk_id == 18u) {
        const up_bank_t *p = (const up_bank_t *)raw;
        if ((ed_bk_id >= 17u && ed_bk_len && (ed_bk_len != sizeof(up_bank_t) || p->rsize != sizeof(up_rec_t))) ||
            (ed_bk_len && (!up_bank_shape(ed_bk_len,p->rsize) || p->magic != UP_BANK_MAGIC || p->nslot != UP_PER_BANK))) return 2;
        /* Unknown record versions remain inert bytes, preserving future/older bank data. */
        obj = ed_bk_id < 8u ? OBJ_UPRESET0 + ed_bk_id - 6u : OBJ_UPRESET_EXT0 + ed_bk_id - 17u;
    } else if (ed_bk_id == 8u) {
        if (!ed_bk_len) return 0;
        return 2; /* retired selector-based bank archives are unsupported */
    } else if (ed_bk_id >= 19u && ed_bk_id <= 22u && (ed_bk_len==sizeof(native_fm_t) || !ed_bk_len)) {
        uint32_t b=ed_bk_id<21u?ed_bk_id-17u:ed_bk_id-21u;
        if(ed_bk_len && !native_fm_valid((const native_fm_t *)raw))return 2;
        native_fm_t old=(*native_fm_bank(b));if(ed_bk_len)memcpy(native_fm_bank(b),raw,sizeof (*native_fm_bank(b)));else native_fm_empty(b);
        if(native_save_bank(ENGI_FM6,b)==2){(*native_fm_bank(b))=old;return 4;}
        up_gen++;sync_reload=1;ui.force=1;return 0;
    } else if (ed_bk_id == 19u || ed_bk_id == 20u) {
        uint32_t b = ed_bk_id - 19u;
        if (ed_bk_len && (ed_bk_len != sizeof(upf_t) || !upf_valid((const upf_t *)raw))) return 2;
        upf_t *work=upf_bank(b*UPF_SLOTS);if(!work)return 4;
        if (ed_bk_len) memcpy(work, raw, sizeof *work); else upf_empty_bank(b);
        if(native_import_owned(b))return 4;
        sync_reload = 1; ui.force = 1;
        return 0;
    } else if(ed_bk_id>=9u && ed_bk_id<=16u){
        if(ed_bk_len && (ed_bk_len!=sizeof(cz_bank_t)||!cz_bank_valid((const cz_bank_t *)raw)))return 2;
        uint32_t b=ed_bk_id-9u;cz_bank_t old=(*native_cz_bank(b));
        if(ed_bk_len)memcpy(native_cz_bank(b),raw,sizeof(cz_bank_t));else {cz_bank_empty(native_cz_bank(b),b);native_cz_bank(b)->ver=2;}
        if(native_save_bank(ENGI_CZ,b)==2){(*native_cz_bank(b))=old;return 4;}
        cz_bank_import(b,(const uint8_t *)native_cz_bank(b),sizeof(cz_bank_t));
        up_gen++;sync_reload=1;ui.force=1;return 0;
    } else if(ed_bk_id>=23u && ed_bk_id<=27u){
        uint32_t bank=ed_bk_id-23u;
        if(ed_bk_len && (ed_bk_len!=sizeof(p5_bank_t)||!p5_bank_valid((const p5_bank_t *)raw,bank)))return 2;
        p5_bank_t *dst=p5_user_bank(bank),old=*dst;
        if(ed_bk_len)memcpy(dst,raw,sizeof *dst);else {memset(dst,0,sizeof *dst);dst->magic=P5_BANK_MAGIC;}
        if(p5_user_save_bank(bank)){*dst=old;p5_index(bank,dst);return 4;}
        up_gen++;sync_reload=1;ui.force=1;return 0;
    } else return 1;
#if MELODEE_FLASH
    if (!flash_ok || st_save(obj, ed_bk_id == 17u || ed_bk_id == 18u ? raw + 8u : raw,
                            (ed_bk_id == 17u || ed_bk_id == 18u) && ed_bk_len ? ed_bk_len - 8u : ed_bk_len)) return 4;
#else
    (void)obj;
#endif
    if (ed_bk_id == 1u) {
        uint32_t ns = ed_bk_len > sizeof ed_bk_set.p ? sizeof ed_bk_set.p : ed_bk_len;
        memset(&ed_bk_set, 0, sizeof ed_bk_set);
        memcpy(&ed_bk_set.p, raw, ns);
        settings_import(&ed_bk_set.p, (int)ns);         /* (a PER4 one becomes PER5) */
        tmpl_take(raw + ns, ed_bk_len - ns);            /* (none in the backup: none now; TPL5: converted) */
        ed_bk_set.t = tmpl;
        tmpl_dirty = 0;
        panel_init(); settings_init(); palette_set(settings.palette);
#if MELODEE_FLASH
        persist_saved = ed_bk_set.p; persist_pending = 0;
#endif
    } else if(ed_bk_id>=9u && ed_bk_id<=16u){
        cz_bank_import(ed_bk_id-9u,raw,ed_bk_len);
        if(ed_bk_len)memcpy(native_cz_bank(ed_bk_id-9u),raw,sizeof(cz_bank_t));else {cz_bank_empty(native_cz_bank(ed_bk_id-9u),ed_bk_id-9u);native_cz_bank(ed_bk_id-9u)->ver=2;}
        up_gen++;
    } else {
        uint32_t b = ed_bk_id < 8u ? ed_bk_id - 6u : ed_bk_id - 15u;
        memset(up_cache_bank(b), 0, sizeof (*up_cache_bank(b)));
        if (ed_bk_len) memcpy(up_cache_bank(b), raw, ed_bk_len);
        up_bank_check(b, (int)ed_bk_len); up_gen++;
    }
    sync_reload = 1; ui.force = 1;
    return 0;
}
/* Legacy payloads are conversion workspace, never part of the live preset cache. */
static uint32_t ed_bk_commit(void)
{
    uint32_t rc=ed_bk_commit_inner();upf_release();return rc;
}
static uint32_t ed_bk_write(const uint8_t *a, uint32_t n)
{
    if (n < 2u || a[0] > 3u || a[1] > 27u) return 1;
    if (ed_flash_stop()) return 3;
    if (a[0] == 0u) {
        if (n != 12u || a[6] > 15u || a[11] > 15u) return 1;
        uint32_t len = ed_bk_r32(a + 2);
        if (len > ED_BK_MAX || (a[1] == 0u && !bank_full(len) && len != PROJ_STORE_V13 && len != PROJ_STORE_V12 && len != PROJ_STORE_V11 && len != PROJ_STORE_V8 && len!=PROJ_LEGACY_CZ && len!=PROJ_LEGACY_CZ_OLD && len != sizeof(project_store_t) && len != PROJ_STORE_V7) ||
            (a[1] == 1u && len != sizeof(persist_t) && len != PERSIST_LEN4 && len != sizeof ed_bk_set &&
             len != sizeof(persist_t)+TMPL_SIZE_A && len != sizeof(persist_t)+TMPL_CZ_OLD && len != sizeof(persist_t)+TMPL_CZ_NEXT && len != sizeof(persist_t) + TMPL_SIZE5 && len != sizeof(persist_t) + TMPL_SIZE6) ||
            (a[1] >= 2u && a[1] <= 5u && len && !bank_full(len) && len != PROJ_STORE_V13 && len != PROJ_STORE_V12 && len != PROJ_STORE_V11 && len != PROJ_STORE_V8 && len!=PROJ_LEGACY_CZ && len!=PROJ_LEGACY_CZ_OLD && len != sizeof(project_store_t) && len != PROJ_STORE_V7) ||
            ((a[1] == 6u || a[1] == 7u || a[1] == 17u || a[1] == 18u) && len && len != sizeof(up_bank_t) && len!=UP_BANK_LEGACY_SIZE) ||
            (a[1] == 8u && len && len != sizeof(fm6_bank_t)) ||
            (a[1]>=9u && a[1]<=16u && len && len!=sizeof(cz_bank_t)) ||
            (a[1]>=23u && len && len!=sizeof(p5_bank_t)) ||
            (a[1]>=19u && a[1]<=22u && len && len!=sizeof(native_fm_t) && !(a[1]<=20u && len==sizeof(upf_t)))) return 1;
        ed_bk_valid = 0; ed_bk_put = 1; ed_bk_id = a[1]; ed_bk_len = len; ed_bk_gen = ++proj_wire_gen;
        ed_bk_crc = ed_bk_r32(a + 7); ed_bk_pos = 0;
        ed_bk_usb = usb.resets; ed_bk_ms = fm1_ms;
        return 0;
    }
    if (!ed_bk_put || ed_bk_id != a[1] || ed_bk_usb != usb.resets || fm1_ms - ed_bk_ms > 15000u ||
        ed_bk_gen != proj_wire_gen) {                 /* (a project save / load reused the staging RAM) */
        ed_bk_put = 0; return 5;
    }
    ed_bk_ms = fm1_ms;
    if (a[0] == 3u) { ed_bk_put = 0; return n == 2u ? 0u : 1u; }
    if (a[0] == 2u) {
        if (n != 2u) return 1;
        uint32_t rc = ed_bk_commit(); ed_bk_put = 0; return rc;
    }
    if (n < 9u || a[6] > 15u || ed_bk_r32(a + 2) != ed_bk_pos) return 1;
    uint32_t count = ed_unpack7(a + 7, n - 7u, ed_smp_buf, 256u);
    if (!count || count > ed_bk_len - ed_bk_pos) return 1;
    memcpy(ED_BK_RAW + ed_bk_pos, ed_smp_buf, count); ed_bk_pos += count;
    return 0;
}
static int ed_backup_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd == ED_BACKUP_LIST) {
        uint32_t rc = n ? 1u : ed_bk_capture();
        ed_b(1); ed_b(rc); ed_b(rc ? 0u : ED_BK_N);
        if (!rc) for (uint32_t i = 0; i < ED_BK_N; i++) {
            uint32_t len;
            const uint8_t *p = ed_bk_object(ED_BK_IDS[i], &len);
            uint32_t crc;
#if MELODEE_FLASH
            if (ed_bk_source_obj < OBJ_COUNT) crc = ed_bk_source_crc;
            else
#endif
                crc = st_crc32(p, len);
            ed_b(ED_BK_IDS[i]); ed_bk_u32(len); ed_bk_u32(crc);
            fm1_wdt_feed();
        }
        return 1;
    }
    if (cmd == ED_BACKUP_GET) {
        uint32_t len = 0, off = n >= 6u ? ed_bk_r32(a + 1) : 0u;
        uint32_t count = n == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        const uint8_t *p = n ? ed_bk_object(a[0], &len) : 0;
        uint32_t rc = (!ed_bk_valid || ed_bk_usb != usb.resets || (n && !a[0] && ed_bk_gen != proj_wire_gen)) ? 5u : transport_busy() ? 3u :
            n != 8u || a[5] > 15u || !p || !count || count > 256u || off > len || count > len - off ? 1u : 0u;
        uint32_t data_off = off;
#if MELODEE_FLASH
        if (!rc && ed_bk_source_obj < OBJ_COUNT) {
            if (st_read_range(ed_bk_source_obj, ed_bk_source_copy, off, ed_smp_buf, count)) rc = 4;
            p = ed_smp_buf; data_off = 0;
        }
#endif
        ed_b(n ? a[0] : 127u); ed_b(rc); ed_bk_u32(off); ed_b(rc ? 0u : count & 127u); ed_b(rc ? 0u : count >> 7);
        if (!rc) ed_bk_pack(p + data_off, count);
        return 1;
    }
    if (cmd == ED_BACKUP_PUT) {
        uint32_t rc = ed_bk_write(a, n);
        ed_b(n ? a[0] : 127u); ed_b(n >= 2u ? a[1] : 127u); ed_b(rc);
        return 1;
    }
    return 0;
}
