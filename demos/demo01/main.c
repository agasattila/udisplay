// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Attila Agas

/*
 * demo01 — uDisplay hardware-less TCP device emulator (pure C API).
 *
 * Exercises all v1 widget types over TCP using the generated C API.
 * TCP transport is handled by demos/shared/demo_tcp.
 *
 * Usage:  ./demo01 [port]   (default port: 5555)
 *
 * Simulated behaviour:
 *   temp_display  — 20 + 5·sin(t) °C, updated at (base_rate × multiplier) Hz
 *   status_led    — toggles every 3 update ticks
 *   power_btn     — BUTTON_PRESS toggles power_led state
 *   mode_sel      — fast=2×, slow=1×, turbo=0.5× rate multiplier
 *   rate_slider   — live update interval (0.1–10.0 Hz base rate); echoed back
 *   enable_toggle — pause/resume temp_display and status_led updates
 *   ssid_input    — submitted string is logged to stdout
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "udisplay.h"
#include "udisplay_ui.h"
#include "demo_tcp.h"

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


/*
 * helper function
 */
static void temp_display()
{
    /* temp_display: 20 + 5·sin(t) °C */
    if (g_enabled) {
        float temp = 20.0f + 5.0f * (float)sin(g_sim_time);
        set_temp_display(temp);
    }   
}


/* ── Event handlers ──────────────────────────────────────────────────────── */

static void handle_client_ready(void)
{
    printf("[EVENT] client_ready\n");
    set_enable_toggle((uint8_t)g_enabled);
    temp_display();
    set_power_btn_power_led((uint8_t)g_power_on);
    set_rate_slider(g_base_rate_hz);
    set_text_input("",0);   
}

static void handle_power_btn(void)
{
    g_power_on = !g_power_on;
    set_power_btn_power_led((uint8_t)g_power_on);
    printf("[EVENT] power_btn  → power_led %s\n", g_power_on ? "ON" : "OFF");
}

static void handle_mode_sel_fast(void)  { g_multiplier = 2.0f; printf("[EVENT] mode_sel   → fast (2×)\n"); }
static void handle_mode_sel_slow(void)  { g_multiplier = 1.0f; printf("[EVENT] mode_sel   → slow (1×)\n"); }
static void handle_mode_sel_turbo(void) { g_multiplier = 5.0f; printf("[EVENT] mode_sel   → turbo (5.0×)\n"); }

static void handle_rate_slider(float v)
{
    if (v < 0.1f)  v = 0.1f;
    if (v > 10.0f) v = 10.0f;
    g_base_rate_hz = v;
    set_rate_slider(g_base_rate_hz);
    printf("[EVENT] rate_slider→ %.2f Hz\n", g_base_rate_hz);
}

static void handle_enable_toggle(uint8_t state)
{
    g_enabled = state ? 1 : 0;
    set_enable_toggle((uint8_t)g_enabled);
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

static const udisplay_ui_handlers_t g_handlers = {
    .on_client_ready          = handle_client_ready,
    .on_enable_toggle_change  = handle_enable_toggle,
    .on_mode_sel_fast_press   = handle_mode_sel_fast,
    .on_mode_sel_slow_press   = handle_mode_sel_slow,
    .on_mode_sel_turbo_press  = handle_mode_sel_turbo,
    .on_power_btn_press       = handle_power_btn,
    .on_rate_slider_change    = handle_rate_slider,
    .on_text_input_submit     = handle_text_input,
};

/* ── demo_tcp hooks ──────────────────────────────────────────────────────── */

static void on_rx(const uint8_t* data, uint16_t len)
{
    udisplay_ui_feed(data, len);
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
    double period = 1.0 / (double)rate;

    g_update_acc += dt_sec;
    if (g_update_acc < period) return;
    g_sim_time   += g_update_acc;
    g_update_acc  = 0.0;
    g_tick++;

    /* First tick after bootstrap: push initial state for interactive widgets */
    if (!g_initial_sent) {
        g_initial_sent = 1;
        set_rate_slider(g_base_rate_hz);
        set_enable_toggle((uint8_t)g_enabled);
        set_power_btn_power_led((uint8_t)g_power_on);
    }
    
    /* temp_display */
    temp_display();

    /* status_led: toggle every 3 ticks */
    if (g_tick % 3 == 0) {
        uint8_t led = (uint8_t)((g_tick / 3) % 2);
        set_status_led(led);
    }
}

static void on_connect(void)
{
    g_initial_sent = 0;
    udisplay_on_connect();
}

static void on_disconnect(void)
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
            fprintf(stderr, "usage: demo01 [port]\n");
            return 1;
        }
    }

    udisplay_ui_init(demo_tcp_send, UDISPLAY_TRANSPORT_TCP);
    udisplay_ui_set_handlers(&g_handlers);

    demo_tcp_hooks_t hooks = {
        .on_rx         = on_rx,
        .on_tick       = on_tick,
        .on_connect    = on_connect,
        .on_disconnect = on_disconnect,
    };

    printf("[demo01] listening on port %d\n", port);
    printf("[demo01] connect with:  udisplay-client tcp://127.0.0.1:%d\n", port);
    printf("[demo01] press Ctrl+C to exit\n");

    demo_tcp_run(port, &hooks);
    return 0;
}
