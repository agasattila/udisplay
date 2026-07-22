# demo05 — Minimal ESP32 BLE demo

Button press toggles an LED. State is pushed to a connected uDisplay client
over BLE using NimBLE GATT notifications.

## Supported targets

ESP-IDF builds one target at a time. The target is stored in `sdkconfig` and
affects the toolchain, linker script, and peripheral drivers — switch targets
with `idf.py set-target` (see below).

`idf.py set-target` only fixes the chip family — it does not know which
specific devkit you're using, and different boards in the same family wire
their onboard LED to different pins, with different types (plain GPIO vs.
addressable WS2812) and polarities. Board selection is a separate step, done
via `idf.py menuconfig` → **Demo05 Configuration** → **Target board** (see
`main/Kconfig.projbuild`), and only the ESP32-C3 Super Mini row has been
checked against real hardware — every other row is desk research against
public pinout docs.

| Target | Board | LED GPIO | LED type | Polarity |
|--------|-------|----------|----------|----------|
| `esp32c3` | ESP32-C3 Super Mini **(hardware-verified)** | 8 (some clones: 10) | plain GPIO | active-low |
| `esp32` | ESP32 DevKitC (UNVERIFIED) | 2 | plain GPIO | active-high |
| `esp32s3` | ESP32-S3-DevKitC-1 (UNVERIFIED) | 38 (v1.0 boards: 48) | WS2812 | n/a |
| `esp32c6` | ESP32-C6-DevKitC-1 (UNVERIFIED) | 8 | WS2812 | n/a |
| `esp32h2` | ESP32-H2-DevKitM-1 (UNVERIFIED) | 8 | WS2812 | n/a |

The firmware code uses only NimBLE and FreeRTOS APIs, so it is portable across
all targets. If your board isn't hardware-verified above, or you know your LED
is wired differently than these Kconfig defaults, override
`DEMO05_LED_GPIO`/`DEMO05_LED_IS_WS2812`/`DEMO05_LED_ACTIVE_LOW`/
`DEMO05_LED_WS2812_COLOR` directly in menuconfig.

**Switching boards without switching targets** (e.g. two different boards
that both build for `esp32c3`): Kconfig only recomputes `DEMO05_LED_*`
defaults for values you haven't explicitly touched. If you're starting from
someone else's committed `sdkconfig`, or one carried over from a different
board, run `idf.py menuconfig` and re-check every `DEMO05_LED_*` value
yourself after switching `DEMO05_BOARD` — don't assume the new board's
GPIO/polarity took effect just because the board name changed. When in
doubt, delete `sdkconfig` and start clean.

## Build

```bash
cd demos/demo05

# First time or when switching targets:
idf.py set-target esp32        # or esp32c3, esp32s3, etc.

# Select your specific board (LED GPIO/type/polarity) — see table above:
idf.py menuconfig               # Demo05 Configuration -> Target board

idf.py build
```

## Flash

Find your serial port first:

```bash
ls /dev/ttyUSB* /dev/ttyACM*
```

Flash and open the serial monitor in one command:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Exit the monitor with **Ctrl+]**.

> If the board does not enter download mode automatically, hold the **BOOT**
> button while the flash command starts.
>
> Permission error on the port? Run `sudo usermod -aG dialout $USER` then
> log out and back in.

## Switching targets

`set-target` wipes the build directory and regenerates `sdkconfig` for the new
chip — a full rebuild is required:

```bash
idf.py set-target esp32c3
idf.py -p /dev/ttyUSB0 flash monitor
```

To build binaries for multiple targets and keep them:

```bash
for target in esp32 esp32c3; do
    idf.py set-target $target
    idf.py build
    cp build/demo05.bin artifacts/demo05-${target}.bin
done
```

## Project structure

```
demo05/
  CMakeLists.txt        — top-level IDF project
  main/
    CMakeLists.txt      — idf_component_register + udisplay-gen invocation
    Kconfig.projbuild   — board choice + LED GPIO/type/polarity config
    idf_component.yml   — managed dependency on espressif/led_strip (WS2812 boards)
    demo05.yaml         — widget layout (push_btn + status_led)
    main.cpp            — NimBLE GATT server + uDisplay callbacks
  sdkconfig             — current target + board config (esp32c3 / Super Mini)
```

## Manual smoke test

demo05 is ESP32 firmware — GPIO and BLE can't be exercised in an automated
test suite, so this checklist is the regression check to run by hand after
any change to `main.cpp` or the generated uDisplay UI (e.g. switching
`udisplay-gen` output language). Run against a real board with a connected
uDisplay client (Qt app):

1. **Boot state — physical LED off** — flash and power on the board *before*
   connecting any client. Confirm the physical LED is **off** at boot. (This
   is a regression check: a prior bug called the raw active-low GPIO write
   directly and left the LED lit from power-on until first button press —
   `led_init()`/`led_set(false)` must leave it off on every board/polarity.)
2. **Advertise + connect** — flash and monitor; confirm `advertising as
   "Demo05"` in the log, then connect from the client. Confirm `connected`
   and `client subscribed — sending HANDSHAKE` appear.
3. **Initial state push** — once the client reports ready, confirm the LED
   widget renders **OFF** (matches the firmware's `status_led` initial
   value pushed from `on_client_ready`).
4. **Button click → LED toggle** — press the physical button. Confirm the
   physical LED and the client's LED widget both flip state, and stay in
   sync across repeated presses.
5. **Heartbeat / comms-error disconnect** — background the client (or block
   its network) for >15s. Confirm the firmware logs
   `comms_error: heartbeat timeout` and terminates the BLE connection, AND
   confirm the physical LED turns **off** at the moment of disconnect (not
   just the client widget) — this is a regression check for a prior bug
   where disconnect reset `g_led` in software but never drove the physical
   pin/strip, leaving the LED lit if it was on when the link dropped.
6. **Reconnect resets state** — reconnect the client. Confirm the LED
   widget shows **OFF** again regardless of the toggle state before
   disconnect (firmware resets `g_led = 0` on `BLE_GAP_EVENT_DISCONNECT`).

Build must also succeed for every supported target before merging any
`main.cpp`/`Kconfig.projbuild`/codegen change (CI runs this matrix on every
PR — see `.github/workflows/demo05-build.yml` — but a local check catches
issues before pushing):

```bash
source ~/esp/esp-idf/export.sh   # or wherever ESP-IDF is installed
for target in esp32 esp32c3 esp32s3 esp32c6 esp32h2; do
    idf.py set-target $target && idf.py build
done
```
