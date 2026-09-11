# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Attila Agas

"""
demo04 -- uDisplay hardware-less TCP device emulator, MicroPython backend.

Matches demo01's simulated behaviour exactly (same YAML, same widget names,
same rules) so the two are directly comparable -- this is the proof that
the MicroPython backend produces an equivalent device, not just "a working
Python script."

Usage:
  udisplay-gen build demo04.yaml --lang micropython -o .
  micropython main.py [port]      # or: python3 main.py [port]
  (default port: 5555)

Simulated behaviour (mirrors demos/demo01/main.c):
  temp_display  -- 20 + 5*sin(t) degC, updated at (base_rate * multiplier) Hz
  status_led    -- toggles every 3 update ticks
  power_btn     -- BUTTON_PRESS toggles power_led state
  mode_sel      -- fast=2x, slow=1x, turbo=5x rate multiplier
  rate_slider   -- live update interval (0.1-10.0 Hz base rate); echoed back
  enable_toggle -- pause/resume temp_display and status_led updates
  text_input    -- submitted string is logged to stdout

Single-client TCP server, single-threaded: a `settimeout()`'d socket polled
in a loop, not demo01's separate recv/timer pthreads -- MicroPython's thread
support is inconsistent across ports (ESP8266 has none), so this loop shape
is the one that runs unmodified everywhere `ui.py` does. See Next Step 8 in
docs/designs/micropython-backend.md.
"""
import errno
import math
import socket
import sys
import time

import ui as ui_module

TICK_S = 0.1
HEARTBEAT_S = 5.0

# MicroPython 1.24.1's errno module doesn't define EWOULDBLOCK (only
# ETIMEDOUT and EAGAIN) -- confirmed against a real build. getattr with a
# fallback avoids an AttributeError that would otherwise crash the whole
# process the first time a recv() timeout is hit under MicroPython.
_TIMEOUT_ERRNOS = (
    errno.ETIMEDOUT,
    errno.EAGAIN,
    getattr(errno, "EWOULDBLOCK", errno.EAGAIN),
)

# ── Simulation state (module-level: mirrors demo01/main.c's static globals;
#    MicroPython doesn't need a mutex here since this is single-threaded) ──
_base_rate_hz = 1.0
_multiplier = 1.0
_enabled = True
_power_on = False
_sim_time = 0.0
_tick = 0
_initial_sent = False


def _temp_display_update(u):
    if _enabled:
        temp = 20.0 + 5.0 * math.sin(_sim_time)
        u.temp_display.set(temp)


def _make_ui(conn):
    """Build a UI bound to this connection, with demo01-equivalent handlers."""
    u = ui_module.UI(send=conn.send)

    def on_client_ready():
        print("[EVENT] client_ready")
        u.enable_toggle.set(1 if _enabled else 0)
        _temp_display_update(u)
        u.power_btn.power_led.set(1 if _power_on else 0)
        u.rate_slider.set(_base_rate_hz)
        u.text_input.set("")

    def on_power_press():
        global _power_on
        _power_on = not _power_on
        u.power_btn.power_led.set(1 if _power_on else 0)
        print("[EVENT] power_btn  -> power_led", "ON" if _power_on else "OFF")

    def on_mode_fast():
        global _multiplier
        _multiplier = 2.0
        print("[EVENT] mode_sel   -> fast (2x)")

    def on_mode_slow():
        global _multiplier
        _multiplier = 1.0
        print("[EVENT] mode_sel   -> slow (1x)")

    def on_mode_turbo():
        global _multiplier
        _multiplier = 5.0
        print("[EVENT] mode_sel   -> turbo (5.0x)")

    def on_rate_change(v):
        global _base_rate_hz
        if v < 0.1:
            v = 0.1
        if v > 10.0:
            v = 10.0
        _base_rate_hz = v
        u.rate_slider.set(_base_rate_hz)
        print("[EVENT] rate_slider-> %.2f Hz" % _base_rate_hz)

    def on_enable_change(state):
        global _enabled
        _enabled = bool(state)
        u.enable_toggle.set(1 if _enabled else 0)
        print("[EVENT] enable     ->", "ON" if _enabled else "OFF")

    def on_text_submit(text):
        print('[EVENT] text_input -> "%s"' % text)

    u.on_client_ready = on_client_ready
    u.power_btn.on_press = on_power_press
    u.mode_sel.fast.on_press = on_mode_fast
    u.mode_sel.slow.on_press = on_mode_slow
    u.mode_sel.turbo.on_press = on_mode_turbo
    u.rate_slider.on_change = on_rate_change
    u.enable_toggle.on_change = on_enable_change
    u.text_input.on_submit = on_text_submit
    return u


def _recv_or_none(conn):
    """One poll of the socket: returns received bytes, None on timeout (no
    data this tick -- normal), or raises on a real error. Both CPython's
    socket.timeout (a TimeoutError/OSError subclass since 3.10) and
    MicroPython's OSError(ETIMEDOUT/EAGAIN) land in the `except` branch;
    errno is checked so a genuine connection error still propagates instead
    of being treated as a harmless timeout forever."""
    try:
        data = conn.recv(2048)
    except OSError as e:
        args0 = e.args[0] if e.args else None
        # CPython: a socket.settimeout() recv() timeout raises TimeoutError
        # (an OSError subclass since 3.10) with args=("timed out",) and
        # errno=None -- not an errno at all. MicroPython instead raises a
        # plain OSError with an integer errno (ETIMEDOUT/EAGAIN/EWOULDBLOCK).
        # Recognize both shapes as "no data this tick", not a real error.
        # (Confirmed empirically against CPython 3.12 -- an earlier version
        # of this check compared args[0] only against errno ints and so
        # treated every single CPython timeout as a fatal disconnect.)
        if args0 == "timed out" or args0 in _TIMEOUT_ERRNOS:
            return None
        raise
    if not data:
        raise OSError("peer closed the connection")
    return data


def _serve_one_connection(conn):
    global _sim_time, _tick, _initial_sent

    conn.settimeout(TICK_S)
    u = _make_ui(conn)
    u.on_connect()
    _initial_sent = False
    hb_acc = 0.0
    update_acc = 0.0
    last = time.time()

    try:
        while True:
            data = _recv_or_none(conn)
            if data:
                u.feed(data)

            now = time.time()
            dt = now - last
            last = now

            hb_acc += dt
            if hb_acc >= HEARTBEAT_S:
                hb_acc = 0.0
                u.heartbeat()

            rate = _base_rate_hz * _multiplier
            if rate < 0.001:
                rate = 0.001
            period = 1.0 / rate

            update_acc += dt
            if update_acc < period:
                continue
            _sim_time += update_acc
            update_acc = 0.0
            _tick += 1

            if not _initial_sent:
                _initial_sent = True
                u.rate_slider.set(_base_rate_hz)
                u.enable_toggle.set(1 if _enabled else 0)
                u.power_btn.power_led.set(1 if _power_on else 0)

            _temp_display_update(u)

            if _tick % 3 == 0:
                u.status_led.set((_tick // 3) % 2)
    finally:
        u.on_disconnect()


def run(port):
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # MicroPython's socket.bind() requires a resolved sockaddr, not a plain
    # (host, port) tuple with a string host the way CPython accepts directly
    # -- confirmed against a real MicroPython 1.24.1 build (raises TypeError:
    # "object with buffer protocol required" otherwise). getaddrinfo() works
    # identically on both runtimes.
    server.bind(socket.getaddrinfo("0.0.0.0", port)[0][-1])
    server.listen(1)
    print("[demo04] listening on port %d" % port)
    print("[demo04] connect with:  udisplay-client tcp://127.0.0.1:%d" % port)
    print("[demo04] press Ctrl+C to exit")

    while True:
        conn, addr = server.accept()
        # MicroPython's accept() returns the peer address as a raw sockaddr
        # bytearray, not a (host, port) tuple like CPython -- print it as-is
        # rather than assuming tuple structure (confirmed against a real
        # MicroPython 1.24.1 build).
        print("[CONN] client connected:", addr)
        try:
            _serve_one_connection(conn)
        except OSError:
            pass  # peer closed / real error -- fall through to close + re-accept
        finally:
            conn.close()
            print("[CONN] Client disconnected.")


if __name__ == "__main__":
    _port = 5555
    if len(sys.argv) > 1:
        _port = int(sys.argv[1])
    run(_port)
