// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/*
 * Minimal fake of the NimBLE host API — only what
 * src/nimble/udisplay_nimble.c uses, with the same names and signatures as
 * ESP-IDF's NimBLE. Implemented by test/nimble_fake/fake_nimble.cpp, which
 * records calls and lets tests inject GAP events.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── NPL ─────────────────────────────────────────────────────────────────── */

typedef uint32_t ble_npl_time_t;
typedef int      ble_npl_error_t;
#define BLE_NPL_TIME_FOREVER UINT32_MAX

struct ble_npl_event;
typedef void ble_npl_event_fn(struct ble_npl_event* ev);
struct ble_npl_event  { void* arg; };
struct ble_npl_eventq { int unused; };
struct ble_npl_mutex  { int depth; int initialized; };
struct ble_npl_callout {
    struct ble_npl_event ev;
    ble_npl_event_fn*    fn;
    int                  armed;
    ble_npl_time_t       ticks;
};

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex* mu);
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex* mu, ble_npl_time_t timeout);
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex* mu);
void            ble_npl_callout_init(struct ble_npl_callout* co, struct ble_npl_eventq* evq,
                                     ble_npl_event_fn* fn, void* arg);
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout* co, ble_npl_time_t ticks);
void            ble_npl_callout_stop(struct ble_npl_callout* co);
ble_npl_time_t  ble_npl_time_ms_to_ticks32(uint32_t ms);

/* ── mbufs ───────────────────────────────────────────────────────────────── */

struct os_mbuf { uint16_t len; uint8_t data[600]; };

struct os_mbuf* ble_hs_mbuf_from_flat(const void* buf, uint16_t len);
int ble_hs_mbuf_to_flat(const struct os_mbuf* om, void* flat, uint16_t max_len,
                        uint16_t* out_copy_len);

/* ── Constants ───────────────────────────────────────────────────────────── */

#define BLE_HS_FOREVER                 INT32_MAX
#define BLE_HS_CONN_HANDLE_NONE        0xffff
#define BLE_HS_EDONE                   14
#define BLE_HS_ETIMEOUT                13
#define BLE_HS_EMSGSIZE                4
#define BLE_HS_ENOMEM                  6
#define BLE_ERR_REM_USER_CONN_TERM     0x13
#define BLE_ATT_ERR_INSUFFICIENT_RES   0x11
#define BLE_OWN_ADDR_PUBLIC            0x00
#define BLE_HS_ADV_F_DISC_GEN          0x02
#define BLE_HS_ADV_F_BREDR_UNSUP       0x04
#define BLE_HS_ADV_MAX_FIELD_SZ        29
#define BLE_GAP_CONN_MODE_UND          2
#define BLE_GAP_DISC_MODE_GEN          2

/* ── UUIDs ───────────────────────────────────────────────────────────────── */

#define BLE_UUID_TYPE_128 128
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) { { BLE_UUID_TYPE_128 }, { __VA_ARGS__ } }

/* ── GATT server ─────────────────────────────────────────────────────────── */

#define BLE_GATT_ACCESS_OP_READ_CHR  0
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GATT_SVC_TYPE_PRIMARY    1
#define BLE_GATT_CHR_F_WRITE         0x0008
#define BLE_GATT_CHR_F_INDICATE      0x0020

struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf* om; };
typedef int ble_gatt_access_fn(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt* ctxt, void* arg);

struct ble_gatt_dsc_def;
struct ble_gatt_chr_def {
    const ble_uuid_t*        uuid;
    ble_gatt_access_fn*      access_cb;
    void*                    arg;
    struct ble_gatt_dsc_def* descriptors;
    uint16_t                 flags;
    uint8_t                  min_key_size;
    uint16_t*                val_handle;
};
struct ble_gatt_svc_def {
    uint8_t                         type;
    const ble_uuid_t*               uuid;
    const struct ble_gatt_svc_def** includes;
    const struct ble_gatt_chr_def*  characteristics;
};

int ble_gatts_count_cfg(const struct ble_gatt_svc_def* defs);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def* svcs);
int ble_gatts_indicate_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                              struct os_mbuf* om);

/* ── GAP ─────────────────────────────────────────────────────────────────── */

#define BLE_GAP_EVENT_CONNECT        0
#define BLE_GAP_EVENT_DISCONNECT     1
#define BLE_GAP_EVENT_ADV_COMPLETE   9
#define BLE_GAP_EVENT_NOTIFY_TX     13
#define BLE_GAP_EVENT_SUBSCRIBE     14
#define BLE_GAP_EVENT_MTU           15

struct ble_gap_conn_desc { uint16_t conn_handle; };
struct ble_gap_event {
    uint8_t type;
    union {
        struct { int status; uint16_t conn_handle; } connect;
        struct { int reason; struct ble_gap_conn_desc conn; } disconnect;
        struct { int reason; } adv_complete;
        struct {
            int status; uint16_t conn_handle; uint16_t attr_handle;
            unsigned indication : 1;
        } notify_tx;
        struct {
            uint16_t conn_handle; uint16_t attr_handle; uint8_t reason;
            unsigned prev_notify : 1; unsigned cur_notify : 1;
            unsigned prev_indicate : 1; unsigned cur_indicate : 1;
        } subscribe;
        struct { uint16_t conn_handle; uint16_t channel_id; uint16_t value; } mtu;
    };
};
typedef int ble_gap_event_fn(struct ble_gap_event* event, void* arg);

struct ble_hs_adv_fields {
    uint8_t              flags;
    const ble_uuid128_t* uuids128;
    uint8_t              num_uuids128;
    unsigned             uuids128_is_complete : 1;
    const uint8_t*       name;
    uint8_t              name_len;
    unsigned             name_is_complete : 1;
};
struct ble_gap_adv_params { uint8_t conn_mode; uint8_t disc_mode; };
typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;

int ble_gap_adv_set_fields(const struct ble_hs_adv_fields* adv_fields);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields* rsp_fields);
int ble_gap_adv_start(uint8_t own_addr_type, const ble_addr_t* direct_addr,
                      int32_t duration_ms, const struct ble_gap_adv_params* adv_params,
                      ble_gap_event_fn* cb, void* cb_arg);
int ble_gap_terminate(uint16_t conn_handle, uint8_t hci_reason);
int ble_hs_id_infer_auto(int privacy, uint8_t* out_addr_type);

/* ── Host config ─────────────────────────────────────────────────────────── */

struct ble_hs_cfg {
    void (*reset_cb)(int reason);
    void (*sync_cb)(void);
};
extern struct ble_hs_cfg ble_hs_cfg;

#ifdef __cplusplus
}
#endif
