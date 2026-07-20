# demo05 — Minimal ESP32 BLE demo

Button press toggles an LED. State is pushed to a connected uDisplay client
over BLE using NimBLE GATT notifications.

## Supported targets

ESP-IDF builds one target at a time. The target is stored in `sdkconfig` and
affects the toolchain, linker script, and peripheral drivers — switch targets
with `idf.py set-target` (see below).

| Target | CPU | Notes |
|--------|-----|-------|
| `esp32` | Xtensa LX6 (dual-core) | default |
| `esp32s3` | Xtensa LX7 (dual-core) | more RAM |
| `esp32c3` | RISC-V | common cheap boards |
| `esp32c6` | RISC-V | WiFi 6 + BLE 5.3 |
| `esp32h2` | RISC-V | BLE/802.15.4 only |

The firmware code uses only NimBLE and FreeRTOS APIs, so it is portable across
all targets. GPIO pin numbers may differ between boards — adjust `demo05.yaml`
if your LED or button is wired to a different pin.

## Build

```bash
cd demos/demo05

# First time or when switching targets:
idf.py set-target esp32        # or esp32c3, esp32s3, etc.

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
    demo05.yaml         — widget layout (push_btn + status_led)
    main.cpp            — NimBLE GATT server + uDisplay callbacks
  sdkconfig             — current target config (esp32c3)
```

## Manual smoke test

demo05 is ESP32 firmware — GPIO and BLE can't be exercised in an automated
test suite, so this checklist is the regression check to run by hand after
any change to `main.cpp` or the generated uDisplay UI (e.g. switching
`udisplay-gen` output language). Run against a real board with a connected
uDisplay client (Qt app):

1. **Advertise + connect** — flash and monitor; confirm `advertising as
   "Demo05"` in the log, then connect from the client. Confirm `connected`
   and `client subscribed — sending HANDSHAKE` appear.
2. **Initial state push** — once the client reports ready, confirm the LED
   widget renders **OFF** (matches the firmware's `status_led` initial
   value pushed from `on_client_ready`).
3. **Button click → LED toggle** — press the physical button. Confirm the
   physical LED and the client's LED widget both flip state, and stay in
   sync across repeated presses.
4. **Heartbeat / comms-error disconnect** — background the client (or block
   its network) for >15s. Confirm the firmware logs
   `comms_error: heartbeat timeout` and terminates the BLE connection.
5. **Reconnect resets state** — reconnect the client. Confirm the LED
   widget shows **OFF** again regardless of the toggle state before
   disconnect (firmware resets `g_led = 0` on `BLE_GAP_EVENT_DISCONNECT`).

Build must also succeed for both toolchain families this firmware targets
before merging any `main.cpp`/codegen change:

```bash
source ~/esp/esp-idf/export.sh   # or wherever ESP-IDF is installed
idf.py set-target esp32c3 && idf.py build   # RISC-V
idf.py set-target esp32   && idf.py build   # Xtensa
```
