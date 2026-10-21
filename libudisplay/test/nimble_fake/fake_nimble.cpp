// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

#include "fake_nimble.h"

#include <cstring>

#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "nimble/udisplay_nimble_port.h"

FakeNimble g_fake;
struct ble_hs_cfg ble_hs_cfg;

static ble_npl_eventq s_eventq;
static os_mbuf s_mbuf;   /* one mbuf is enough: the backend hands it straight to indicate */

void fake_nimble_reset()
{
    g_fake = FakeNimble{};
    ble_hs_cfg = {};
}

int fake_gap_event(ble_gap_event& ev)
{
    return g_fake.gap_cb ? g_fake.gap_cb(&ev, nullptr) : -1;
}

void fake_fire(ble_npl_callout* co)
{
    co->armed = 0;
    co->fn(&co->ev);
}

int fake_write(const ble_gatt_chr_def& chr, const std::vector<uint8_t>& data)
{
    os_mbuf om{};
    om.len = static_cast<uint16_t>(data.size());
    std::memcpy(om.data, data.data(), data.size());
    ble_gatt_access_ctxt ctxt{BLE_GATT_ACCESS_OP_WRITE_CHR, &om};
    return chr.access_cb(1, 0, &ctxt, chr.arg);
}

int fake_lock_depth()
{
    int d = 0;
    for (auto* m : g_fake.mutexes) d += m->depth;
    return d;
}

extern "C" {

/* Port glue */
int udisplay_nimble_port_init(void) { g_fake.port_init_calls++; return g_fake.port_init_rc; }
int udisplay_nimble_port_start(void) { g_fake.port_start_calls++; return 0; }
struct ble_npl_eventq* udisplay_nimble_port_eventq(void) { return &s_eventq; }

/* NPL */
ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex* mu)
{
    mu->depth = 0;
    mu->initialized = 1;
    g_fake.mutexes.push_back(mu);
    return 0;
}
ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex* mu, ble_npl_time_t) { mu->depth++; return 0; }
ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex* mu) { mu->depth--; return 0; }
void ble_npl_callout_init(struct ble_npl_callout* co, struct ble_npl_eventq*,
                          ble_npl_event_fn* fn, void* arg)
{
    co->fn = fn;
    co->ev.arg = arg;
    co->armed = 0;
    co->ticks = 0;
}
ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout* co, ble_npl_time_t ticks)
{
    co->armed = 1;
    co->ticks = ticks;
    return 0;
}
void ble_npl_callout_stop(struct ble_npl_callout* co) { co->armed = 0; }
ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }

/* mbufs */
struct os_mbuf* ble_hs_mbuf_from_flat(const void* buf, uint16_t len)
{
    if (g_fake.mbuf_alloc_fails) return nullptr;
    s_mbuf.len = len;
    std::memcpy(s_mbuf.data, buf, len);
    return &s_mbuf;
}
int ble_hs_mbuf_to_flat(const struct os_mbuf* om, void* flat, uint16_t max_len,
                        uint16_t* out_copy_len)
{
    uint16_t n = om->len < max_len ? om->len : max_len;
    std::memcpy(flat, om->data, n);
    if (out_copy_len) *out_copy_len = n;
    return om->len > max_len ? BLE_HS_EMSGSIZE : 0;
}

/* GATT */
int ble_gatts_count_cfg(const struct ble_gatt_svc_def*) { return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def* svcs)
{
    g_fake.svcs = svcs;
    for (auto* chr = svcs[0].characteristics; chr->uuid; ++chr) {
        if (chr->val_handle) *chr->val_handle = FakeNimble::kDataValHandle;
    }
    return 0;
}
int ble_gatts_indicate_custom(uint16_t conn_handle, uint16_t, struct os_mbuf* om)
{
    if (g_fake.indicate_rc != 0) return g_fake.indicate_rc;
    g_fake.indications.emplace_back(om->data, om->data + om->len);
    g_fake.indicate_conn_handles.push_back(conn_handle);
    return 0;
}

/* GAP */
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields* f)
{
    g_fake.adv = *f;
    g_fake.adv_name = f->name ? std::string(reinterpret_cast<const char*>(f->name), f->name_len)
                              : std::string();
    return 0;
}
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields* f)
{
    g_fake.rsp_set = true;
    g_fake.rsp_name = std::string(reinterpret_cast<const char*>(f->name), f->name_len);
    g_fake.rsp_name_complete = f->name_is_complete;
    return 0;
}
int ble_gap_adv_start(uint8_t, const ble_addr_t*, int32_t, const struct ble_gap_adv_params*,
                      ble_gap_event_fn* cb, void*)
{
    g_fake.adv_starts++;
    g_fake.gap_cb = cb;
    return 0;
}
int ble_gap_terminate(uint16_t conn_handle, uint8_t)
{
    g_fake.terminated.push_back(conn_handle);
    return 0;
}
int ble_hs_id_infer_auto(int, uint8_t* out) { *out = BLE_OWN_ADDR_PUBLIC; return 0; }
int ble_hs_util_ensure_addr(int) { return 0; }

/* Services */
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
int ble_svc_gap_device_name_set(const char* name) { g_fake.device_name = name; return 0; }

} /* extern "C" */
