# demo07 — ESP32 BLE full-feature (real hardware showcase)

**Status: PLACEHOLDER** — blocked only on hardware. No board or wiring has been
planned or designed yet.

## Goal

demo05 already proves the BLE path end to end, but only for a single LED. demo07's
goal is to go beyond that: a full v1 widget showcase driven by genuine hardware
usage — real sensors and actuators reflected in the UI, not just toggling a single
LED from a button press. Firmware flashed to a physical device, controlled from the
mobile/desktop uDisplay client via BLE GATT notifications.

## Blockers

- No ESP32 hardware selected, and no board/wiring plan designed yet — this is the
  only blocker; the BLE client and firmware plumbing demo05 needed already work

## Planned structure

```
demo07/
  CMakeLists.txt       — idf_component_register
  main/
    demo07.yaml        — full v1 widget vocabulary
    main.cpp            — BLE GATT + simulation + all widget types
    CMakeLists.txt
  sdkconfig.defaults
```

## Planned usage

```bash
idf.py set-target esp32
idf.py build flash monitor
# connect from uDisplay client via BLE
```
