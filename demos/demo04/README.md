# demo04 — MicroPython PC emulator

**Status: Works** — matches demo01's simulated behaviour exactly, running on
the generated MicroPython backend's output.

## Goal

Demonstrate uDisplay on a PC using MicroPython, matching the same simulation as
demo01–03 but with a Python binding. Primary target is MicroPython on ESP8266/ESP32,
but the PC variant lets developers iterate without hardware.

## Usage

```bash
udisplay-gen build demo04.yaml --lang micropython -o .
python3 main.py [port]       # or: micropython main.py [port]
```

Default port: 5555. Connect with the uDisplay desktop client
(`udisplay-client tcp://127.0.0.1:5555`) or any TCP client that speaks the
protocol.

`ui.py` and `udisplay_runtime.py` are generated into this directory by the
build command above and are gitignored — regenerate them any time, they are
never hand-edited (see `ui.py`'s own header comment).

## What's implemented (v0, per docs/designs/micropython-backend.md)

- TCP transport, no auth — matches the current scope of `--lang micropython`.
- Full widget vocabulary from `demo04.yaml`, including composite widgets
  (`power_btn` + its `power_led` child, `mode_sel` button-group + its
  `fast`/`slow`/`turbo` items).
- Runs unmodified under both the MicroPython Unix port and desktop CPython
  — verified live (2026-09-11) against a real MicroPython 1.24.1 Unix
  build: full handshake, chunk transfer, `CLIENT_READY`, button-press event
  dispatch, and periodic state pushes all round-tripped correctly against
  a CPython client. Two real MicroPython-only bugs were found and fixed in
  the process (`socket.bind()` needs a `getaddrinfo()`-resolved address,
  not a bare `(host, port)` tuple; `errno.EWOULDBLOCK` doesn't exist on
  MicroPython) — see `main.py`'s inline comments.
- RAM footprint measured on that same Unix build: fits comfortably under
  an 80KB constrained heap (~68KB minimum for raw source, ~33KB
  precompiled to `.mpy`) — see the design doc's Success Criteria for the
  full numbers and caveats (Unix port is a proxy, not real ESP8266
  hardware).
- Hardened since that first pass, via adversarial review (Claude + Codex)
  against the live server: idle unauthenticated connections are now reaped
  after 3 missed heartbeats (~15s) instead of permanently occupying the
  server's one `listen()` slot — a trivial DoS in the original v0, since
  there's no auth to gate a connection attempt; `main.py` now sends framed
  messages through `tcp_send_all()` instead of a raw `conn.send()`, so a
  partial write on a slow link can no longer desync the stream;
  `udisplay_runtime.py`'s `TcpRx.feed()` compacts its buffer before
  dispatching a message rather than after, so a callback exception can't
  wedge the connection on a poisoned frame forever; and outgoing strings
  that exceed the wire format's 255-byte length field are now truncated on
  a UTF-8 character boundary instead of raising or emitting invalid UTF-8.

## Not yet implemented

- BLE transport, HMAC auth — deferred (Approach A scope; see the design doc).
- Real ESP8266/ESP32 hardware — that's demo06's job, not this PC emulator.
