// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Attila Agas

/*
 * demo02 — uDisplay hardware-less TCP device emulator (C++ safe API).
 *
 * Same simulation as demo01, rewritten using the generated C++ header
 * with function-pointer event handlers (--lang cpp, no --modern).
 * TCP transport is handled by demos/shared/demo_tcp.
 *
 * Usage:  ./demo02 [port]   (default port: 5555)
 */

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "udisplay.h"
#include "udisplay_ui.hpp"
#include "demo_tcp.h"

using namespace udisplay_ui;

static UDisplay ui;

/* ── Simulation state — accessed under demo_tcp mutex ────────────────────── */

static float  g_base_rate_hz = 1.0f;
static float  g_multiplier   = 1.0f;
static int    g_enabled      = 1;
static int    g_power_on     = 0;
static double g_sim_time     = 0.0;
static int    g_tick         = 0;
static int    g_initial_sent = 0;

static double g_hb_acc     = 0.0;
static double g_update_acc = 0.0;

/* ── Event handlers (function pointers — safe variant) ───────────────────── */

static void handle_power_btn()
{
    g_power_on = !g_power_on;
    ui.power_btn.power_led.set((bool)g_power_on);
    printf("[EVENT] power_btn  → power_led %s\n", g_power_on ? "ON" : "OFF");
}

static void handle_mode_sel_fast()  { g_multiplier = 2.0f; printf("[EVENT] mode_sel   → fast (2×)\n"); }
static void handle_mode_sel_slow()  { g_multiplier = 1.0f; printf("[EVENT] mode_sel   → slow (1×)\n"); }
static void handle_mode_sel_turbo() { g_multiplier = 0.5f; printf("[EVENT] mode_sel   → turbo (0.5×)\n"); }

static void handle_rate_slider(float v)
{
    if (v < 0.1f)  v = 0.1f;
    if (v > 10.0f) v = 10.0f;
    g_base_rate_hz = v;
    ui.rate_slider.set(g_base_rate_hz);
    printf("[EVENT] rate_slider→ %.2f Hz\n", g_base_rate_hz);
}

static void handle_enable_toggle(bool state)
{
    g_enabled = state ? 1 : 0;
    ui.enable_toggle.set((bool)g_enabled);
    printf("[EVENT] enable     → %s\n", g_enabled ? "ON" : "OFF");
}

static void handle_text_input(const char* str, uint8_t len)
{
    char buf[256];
    uint8_t n = (len < 255u) ? len : 255u;
    memcpy(buf, str, n);
    buf[n] = '\0';
    printf("[EVENT] text_input → \"%s\"\n", buf);
}

/* ── demo_tcp hooks ──────────────────────────────────────────────────────── */

static void on_rx(const uint8_t* data, uint16_t len)
{
    ui.feed(data, len);
}

static void on_tick(double dt_sec)
{
    /* Heartbeat every 5 s */
    g_hb_acc += dt_sec;
    if (g_hb_acc >= 5.0) {
        g_hb_acc = 0.0;
        udisplay_heartbeat();
    }

    /* Data update at (base_rate × multiplier) Hz */
    float rate = g_base_rate_hz * g_multiplier;
    if (rate < 0.001f) rate = 0.001f;
    double period = 1.0 / static_cast<double>(rate);

    g_update_acc += dt_sec;
    if (g_update_acc < period) return;
    g_sim_time   += g_update_acc;
    g_update_acc  = 0.0;
    g_tick++;

    /* First tick after bootstrap: push initial state for interactive widgets */
    if (!g_initial_sent) {
        g_initial_sent = 1;
        ui.rate_slider.set(g_base_rate_hz);
        ui.enable_toggle.set((bool)g_enabled);
        ui.power_btn.power_led.set((bool)g_power_on);
    }

    /* temp_display: 20 + 5·sin(t) °C */
    if (g_enabled) {
        float temp = 20.0f + 5.0f * static_cast<float>(std::sin(g_sim_time));
        ui.temp_display.set(temp);
    }

    /* status_led: toggle every 3 ticks */
    if (g_tick % 3 == 0)
        ui.status_led.set((bool)((g_tick / 3) % 2));
}

static void on_connect()
{
    g_initial_sent = 0;
    udisplay_on_connect();
}

static void on_disconnect()
{
    udisplay_on_disconnect();
    g_initial_sent = 0;
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char* argv[])
{
    int port = 5555;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "usage: demo02 [port]\n");
            return 1;
        }
    }

    ui.init(demo_tcp_send, UDISPLAY_TRANSPORT_TCP);

    ui.power_btn.on_press       = handle_power_btn;
    ui.mode_sel.fast.on_press   = handle_mode_sel_fast;
    ui.mode_sel.slow.on_press   = handle_mode_sel_slow;
    ui.mode_sel.turbo.on_press  = handle_mode_sel_turbo;
    ui.rate_slider.on_change    = handle_rate_slider;
    ui.enable_toggle.on_change  = handle_enable_toggle;
    ui.text_input.on_submit     = handle_text_input;

    demo_tcp_hooks_t hooks = { on_rx, on_tick, on_connect, on_disconnect };

    printf("[demo02] listening on port %d\n", port);
    printf("[demo02] connect with:  udisplay-client tcp://127.0.0.1:%d\n", port);
    printf("[demo02] press Ctrl+C to exit\n");

    demo_tcp_run(port, &hooks);
    return 0;
}
