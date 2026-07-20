# demo06 — ESP8266 MicroPython

**Status: PLACEHOLDER** — blocked on MicroPython backend and hardware availability.

## Goal

Run uDisplay on an ESP8266 with MicroPython over WiFi TCP. Lowest-cost hardware
target; validates that the protocol and UI can run within 80 KB of usable RAM.

## Blockers

- `udisplay-gen --lang micropython` backend not yet implemented (v2 scope item)
- MicroPython C extension for libudisplay not yet written
- No ESP8266 hardware available for validation

## Planned usage

```bash
udisplay-gen build demo06.yaml --lang micropython -o .
ampy put main.py
ampy run main.py
```
