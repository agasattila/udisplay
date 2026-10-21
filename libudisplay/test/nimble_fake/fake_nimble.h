// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/* Test-side control/inspection of the fake NimBLE host (C++ only). */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "host/ble_hs.h"

struct FakeNimble {
    /* Recorded calls */
    int port_init_calls = 0;
    int port_start_calls = 0;
    std::string device_name;
    const ble_gatt_svc_def* svcs = nullptr;
    std::vector<std::vector<uint8_t>> indications;    /* payload per indicate call */
    std::vector<uint16_t> indicate_conn_handles;
    std::vector<uint16_t> terminated;                 /* conn handles */
    int adv_starts = 0;
    ble_gap_event_fn* gap_cb = nullptr;
    ble_hs_adv_fields adv{};
    std::string adv_name;
    bool rsp_set = false;
    std::string rsp_name;
    bool rsp_name_complete = false;
    std::vector<ble_npl_mutex*> mutexes;

    /* Injected results */
    int port_init_rc = 0;
    int indicate_rc = 0;
    int add_svcs_rc = 0;
    bool mbuf_alloc_fails = false;

    static constexpr uint16_t kDataValHandle = 42;
};

extern FakeNimble g_fake;

void fake_nimble_reset();

/* Deliver a GAP event to the callback registered by ble_gap_adv_start. */
int fake_gap_event(ble_gap_event& ev);

/* Run an armed callout (as the host task would on expiry). */
void fake_fire(ble_npl_callout* co);

/* Simulate a client write to a characteristic's access callback. */
int fake_write(const ble_gatt_chr_def& chr, const std::vector<uint8_t>& data);

/* Total lock depth across mutexes the backend created (0 = all released). */
int fake_lock_depth();
