# Quickstart

You'll go from a blank YAML file to a working (simulated) uDisplay device in four
steps: write a device UI as YAML, watch it render live in the desktop client, generate
firmware code from it, and wire that generated code into a minimal C++ program.

No hardware is required for any of this — `udisplay-client --design` mode renders your
YAML without a device connection, and the C++ sketch in the last step uses placeholder
transport functions instead of real sockets.

## What you'll need

- `udisplay-client` built — see [Building from source](building.md#udisplay-client-qt-desktop-app)
- `udisplay-gen` installed — see [Building from source](building.md#udisplay-gen-python-codegen)
- A C++17 compiler, if you want to compile the sketch in step 4 (optional — the code
  is meant to be read, not run as-is; see the note at the start of that step)

## Step 1: Write the YAML — a button with an LED

Create `device.yaml`:

```yaml
device:
  name: "Quickstart Demo"

widgets:
  power_btn:
    type: button
    label: "Power"
    shape: circle
    widgets:
      power_led:
        type: led
        label: "PWR"
```

This is the smallest interesting example: one `button` widget with an `led` child
rendered on its face. `device.name` is the only required field. Every other field
here is optional — see [Widget reference](widgets.md) for the full attribute list per
widget type.

## Step 2: Preview it live with `--design` mode

`udisplay-client` can render a YAML file directly, without any device connection.
Point it at the file you just wrote:

```bash
./udisplay-client/build/udisplay-client --design device.yaml
```

You should see the Power button with its LED, rendered exactly as it would on a real
device. This is **design mode**: no TCP, no BLE, no bootstrap sequence — the client
reads the YAML file straight off disk and renders it.

Now edit the file while the client is still open. Change the button's label:

```yaml
    label: "Main Power"
```

Save the file. The client picks up the change and re-renders **within about 150ms** —
no restart needed. Under the hood, a `QFileSystemWatcher` watches the file path; every
save triggers a short debounce window (multiple rapid saves from your editor's autosave
only trigger one reload) and then a full re-parse.

If you introduce a YAML error (try misspelling `type: buttn`), the client doesn't
crash or navigate away — it shows the parse error in place and keeps waiting for your
next save to fix it.

This edit-save-look loop is the fastest way to iterate on a device UI: no firmware
rebuild, no flashing, no reconnect.

## Step 3: Generate firmware code with `udisplay-gen`

`udisplay-gen` turns the YAML into firmware-ready code. First, sanity-check the file
against the schema (catches mistakes like the misspelled `type` above with a clear
error instead of a silent misrender):

```bash
udisplay-gen validate device.yaml
```

Then build. `udisplay-gen build` supports three output modes, selected by `--lang` and
`--modern`:

| Command | Output files | Handler style |
|---|---|---|
| `udisplay-gen build device.yaml` | `udisplay_ui.h`, `udisplay_ui.c`, `udisplay_ui.bin` | C function pointers in a `udisplay_ui_handlers_t` struct |
| `udisplay-gen build device.yaml --lang cpp` | `udisplay_ui.hpp`, `udisplay_ui.bin` | C++ raw function pointers (`void (*on_change)(...)`) |
| `udisplay-gen build device.yaml --lang cpp --modern` | `udisplay_ui.hpp`, `udisplay_ui.bin` | C++ `std::function` — enables lambda handlers |

All three modes embed the same compiled UI blob (widget IDs, Merkle root, compressed
YAML) — `--lang` only changes the *shape of the API you call from firmware*, not the
wire format the client sees. `udisplay_ui.bin` is a byte-for-byte copy of the blob for
workflows that flash it separately; the C++ header embeds the same bytes inline as
static arrays, so `udisplay_ui.bin` isn't actually needed to build against `.hpp`.

For this quickstart, generate the modern C++ variant — it's what step 4 uses:

```bash
udisplay-gen build device.yaml --lang cpp --modern -o generated
```

This writes `generated/udisplay_ui.hpp` and `generated/udisplay_ui.bin`. Open the
`.hpp` and you'll find a `udisplay_ui::UDisplay` class with one member per widget —
`power_btn` (a `Widget` with press/release/click handlers and a nested `power_led`
member) — see [Widget reference § Generated C++ API](widgets.md#button) for the exact
shape.

## Step 4: Wire it up in a minimal C++ program

This sketch shows **only the wiring** between the generated code and a transport —
`platform_tcp_send` and `platform_tcp_receive` below are declared but not defined,
standing in for whatever your platform's real socket calls are (desktop BSD sockets,
ESP-IDF lwIP, or a demo helper like [`demos/shared/demo_tcp`](../demos/shared/) that
demo01–03 use). Swap them for real I/O and this becomes a working device.

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;
static int g_power_on = 0;

// -- Abstract transport — replace with real TCP send/receive for your platform --
extern void platform_tcp_send(const uint8_t* data, uint16_t len);
extern int  platform_tcp_receive(uint8_t* buf, uint16_t cap); // returns bytes read, 0 if none

// udisplay_send_fn — the library calls this once per already-framed TCP message.
static void send_cb(const uint8_t* data, uint16_t len, void* /*userdata*/)
{
    platform_tcp_send(data, len);
}

int main(/* ... */) {

    // 1. Initialise the library once. UDISPLAY_TRANSPORT_TCP makes the library
    //    add/parse the u16 length prefix for you — send_cb only sees framed bytes.
    ui.init(send_cb, UDISPLAY_TRANSPORT_TCP);

    // 2. Wire up handlers. Lambdas require --lang cpp --modern (see step 3).
    ui.power_btn.on_press = []() {
        g_power_on = !g_power_on;
        ui.power_btn.power_led.set((bool)g_power_on);
    };

    // 3. Tell the library a client has connected — this sends the HANDSHAKE
    //    that kicks off bootstrap. Call this from your accept()/on-connect path.
    udisplay_on_connect();

    for (;;) {
        // 4. Feed inbound bytes as they arrive. ui.feed() reassembles TCP frames
        //    internally and dispatches complete messages (including EVENTs,
        //    which land on the lambdas set in step 2).
        uint8_t buf[256];
        int n = platform_tcp_receive(buf, sizeof(buf));
        if (n > 0) {
            ui.feed(buf, (uint16_t)n);
        }

        // 5. Call periodically — recommended every 5s, not every loop iteration.
        //    Keeps the client's connection-alive watchdog satisfied.
        udisplay_heartbeat();

        // 6. On disconnect, reset bootstrap state so a reconnect starts clean:
        //    udisplay_on_disconnect();
    }
}
```

Every widget interaction you'd add later follows the same pattern as `power_btn`
above: assign a lambda to the generated member (`ui.<widget>.on_change`,
`ui.<widget>.on_submit`, ...), and push state back out with `ui.<widget>.set(...)`.
See [Widget reference](widgets.md) for the setter/handler shape of every widget type.

## What you built

You now have: a device UI defined in YAML, a live preview loop for iterating on it
without touching firmware, generated C++ code from that YAML, and a wiring sketch
showing exactly which library calls glue the generated code to a transport.

From here:

- [Widget reference](widgets.md) — every widget type's attributes, YAML examples, and
  generated C/C++ API, with usage examples in the same style as step 4 above
- [Protocol](protocol.md) — the binary wire format behind `ui.feed()`/`send_cb`, and
  (§ Development Tools) demo01–03, the reference TCP implementations — including one
  in this same `--lang cpp --modern` mode — that this quickstart's sketch was
  abstracted from
- [demos/demo05](../demos/demo05/) — the same `init`/handler-lambda/`feed` wiring
  pattern running for real, over BLE, on physical ESP32 hardware
- [Building from source](building.md) — full dependency and build instructions for
  `udisplay-client` and `udisplay-gen`
