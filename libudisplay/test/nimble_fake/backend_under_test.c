// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/*
 * The backend keeps its state in a file-scope singleton with no deinit
 * (firmware never tears it down). Tests need a fresh one per test, so they
 * compile the backend through this file, which adds a reset hook without
 * putting test code in the library.
 */
#include "../../src/nimble/udisplay_nimble.c"

void udisplay_nimble_test_reset(void)
{
    memset(&s, 0, sizeof(s));
}

struct ble_npl_callout* udisplay_nimble_test_heartbeat(void) { return &s.heartbeat; }
struct ble_npl_callout* udisplay_nimble_test_ud_timer(void)  { return &s.ud_timer; }
