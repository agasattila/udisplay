# demo06 — ESP8266 MicroPython

**Status: PLACEHOLDER** — blocked on hardware availability. The MicroPython
codegen backend itself is done (see [demo04](../demo04/), its PC-emulator
counterpart) — this demo is purely gated on the remaining items below.

## Goal

Run uDisplay on an ESP8266 with MicroPython over WiFi TCP. Lowest-cost hardware
target; validates that the protocol and UI can run within 80 KB of usable RAM.

## Blockers

- No ESP8266 hardware available for validation
- The 80KB RAM fixture and target MicroPython firmware/version baseline are
  still open (see `docs/designs/micropython-backend.md`'s Open Questions)
- `udisplay_runtime.py`'s RAM footprint on real ESP8266 hardware is
  unvalidated — the design doc's pre-sized-buffer approach targets this
  budget but hasn't been measured on the constrained-heap Unix MicroPython
  port yet, let alone real hardware

## Planned usage

```bash
udisplay-gen build demo06.yaml --lang micropython -o .
ampy put main.py
ampy run main.py
```
