# demo04 — MicroPython PC emulator

**Status: PLACEHOLDER** — blocked on `udisplay-gen` MicroPython backend (not yet implemented).

## Goal

Demonstrate uDisplay on a PC using MicroPython, matching the same simulation as
demo01–03 but with a Python binding. Primary target is MicroPython on ESP8266/ESP32,
but the PC variant lets developers iterate without hardware.

## Blockers

- `udisplay-gen --lang micropython` backend not yet implemented (v2 scope item)
- MicroPython C extension module for libudisplay not yet written

## Planned usage

```bash
udisplay-gen build demo04.yaml --lang micropython -o .
micropython main.py [port]
```
