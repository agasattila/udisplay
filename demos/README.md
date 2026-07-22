## uDisplay

### Demos

| Demo | What it is | Status |
|---|---|---|
| [demo01](demo01/) | Hardware-less TCP device emulator, plain C (`udisplay-gen` default output) | Works |
| [demo02](demo02/) | Same, C++ output (`--lang cpp`) | Works |
| [demo03](demo03/) | Same, modern C++ output with `std::function` handlers (`--lang cpp --modern`) | Works |
| [demo04](demo04/) | MicroPython PC emulator, matching demo01–03's simulation | Placeholder — blocked on the MicroPython codegen backend |
| [demo05](demo05/) | Minimal ESP32 BLE demo — button press toggles an LED over BLE GATT, Kconfig board selection across 5 ESP32 targets (plain GPIO + WS2812) | Works, on real hardware (ESP32-C3 Super Mini hardware-verified; others desk-researched) |
| [demo06](demo06/) | ESP8266 MicroPython over WiFi TCP | Placeholder — blocked on the MicroPython codegen backend and hardware |
| [demo07](demo07/) | Full v1 widget showcase on real ESP32 hardware over BLE | Placeholder — blocked on hardware |

demo01–03 are the primary desktop-side showcase, development, and integration-test
tools — see [`docs/protocol.md`](../docs/protocol.md) § Development Tools.

### mDNS testing

demo01–03 listen on plain TCP and don't advertise themselves over mDNS. To test the
Qt client's mDNS discovery against one of them, advertise it manually:

```bash
avahi-publish-service "uDisplay Test" _udisplay._tcp 5555
```
