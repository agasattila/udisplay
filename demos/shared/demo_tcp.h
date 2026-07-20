// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Attila Agas

/*
 * demo_tcp — shared single-client TCP server core for uDisplay PC demos.
 *
 * Extracts socket creation, accept loop, recv thread, and timer thread so
 * each demo's main file contains only uDisplay API calls and simulation logic.
 *
 * Usage:
 *   1. Implement the four hooks below.
 *   2. Pass demo_tcp_send to udisplay_ui_init() / UDisplay::init().
 *   3. Call demo_tcp_run(port, &hooks) — blocks forever.
 *
 * Threading: all hooks are called under an internal mutex.
 *            demo_tcp_send is safe to call from any thread.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Called under mutex when TCP bytes arrive from the client. */
typedef void (*demo_tcp_rx_fn)   (const uint8_t* data, uint16_t len);

/* Called under mutex every ~100 ms while a client is connected.
 * dt_sec is the elapsed seconds since the last tick (nominally 0.1). */
typedef void (*demo_tcp_tick_fn) (double dt_sec);

/* Called under mutex on client connect / disconnect. */
typedef void (*demo_tcp_conn_fn) (void);

typedef struct {
    demo_tcp_rx_fn   on_rx;
    demo_tcp_tick_fn on_tick;
    demo_tcp_conn_fn on_connect;
    demo_tcp_conn_fn on_disconnect;
} demo_tcp_hooks_t;

/*
 * Send callback: pass directly to udisplay_ui_init(send, UDISPLAY_TRANSPORT_TCP)
 * or UDisplay::init(send, UDISPLAY_TRANSPORT_TCP). Writes @p msg verbatim to
 * the current client socket -- libudisplay itself already prepends the
 * u16-LE length prefix before invoking this callback when transport is
 * UDISPLAY_TRANSPORT_TCP, so this function must NOT frame again.
 */
void demo_tcp_send(const uint8_t* msg, uint16_t len, void* ud);

/*
 * Start the TCP server and block forever.
 * Spawns a timer thread; each accepted client gets a dedicated recv thread.
 * Only one client is served at a time; further connections are rejected.
 */
void demo_tcp_run(int port, const demo_tcp_hooks_t* hooks);

#ifdef __cplusplus
}
#endif
