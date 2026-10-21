// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Attila Agas

/**
 * @file udisplay_nimble_port.h
 * @brief Platform glue the NimBLE backend needs (internal).
 *
 * udisplay_nimble.c uses only the NimBLE host API; these three functions
 * are everything platform-specific. ESP-IDF's implementation is
 * udisplay_nimble_port_esp_idf.c. Another NimBLE platform (e.g. Apache
 * Mynewt) provides its own file implementing the same functions and
 * compiles it instead.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct ble_npl_eventq;

/** Bring up the BLE controller and NimBLE host so that NPL objects and the
 *  host API can be used. Called once, from udisplay_nimble_init().
 *  @return 0 on success, else a platform error code. */
int udisplay_nimble_port_init(void);

/** Start running the NimBLE host (e.g. its task). Called once, from
 *  udisplay_nimble_start(). @return 0 on success, else a platform error code. */
int udisplay_nimble_port_start(void);

/** The event queue the host task services. The backend's callouts are
 *  posted there, so they run in the host task. */
struct ble_npl_eventq* udisplay_nimble_port_eventq(void);

#ifdef __cplusplus
}
#endif
