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

## Not yet implemented

- BLE transport, HMAC auth — deferred (Approach A scope; see the design doc).
- Real ESP8266/ESP32 hardware — that's demo06's job, not this PC emulator.
