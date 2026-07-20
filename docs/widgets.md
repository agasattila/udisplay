# uDisplay YAML Widget Reference

**Version:** 1.2
**Status:** Normative for the YAML authoring format.
**Schema:** `udisplay.schema.json` (repo root) — the JSON schema is the machine-authoritative
source. (`udisplay-gen/udisplay_gen/schema/udisplay.schema.json` is a symlink to this file,
not a separate copy.) Any discrepancy between this document and the schema is a bug in this
document.

---

## Design Principles

The v1 widget set was designed around three constraints:

**1. Device-authoritative state.** The client never assumes a state change succeeded.
Every interactive widget waits for a STATE_UPDATE from the device before updating its
visual state. This prevents the client and device from diverging on embedded targets
where operations can fail silently (clamped values, hardware faults, rejected inputs).

**2. One event kind per interaction type** (with a scoped exception for buttons). Each widget
type produces one event kind: SLIDER_CHANGE, TOGGLE_CHANGE, or TEXT_SUBMIT. `button` and
`button-group` items are the sole exception — they produce three events per interaction
(BUTTON_PRESS on finger-down, BUTTON_RELEASE on finger-up, BUTTON_CLICK on complete tap
within bounds). This supports continuous-control use cases (D-PAD → motor start/stop)
while keeping all other widget types single-event. There are no hover states or
partial-input events.

**3. Flat widget ID space.** All widgets — including children and group items — share
a single 8-bit ID namespace (`0x10`–`0xFF`, max 240). There is no hierarchy in the
protocol. The YAML nesting (button → face children, button-group → items) is an authoring
convenience; on the wire every widget is identified by its flat ID.

### What was excluded from v1 and why

| Excluded from base v1 | Reason |
|---|---|
| Client-side computed values | Out of scope for v1. The v2 MVVM reactive datamodel is the right place — a QuickJS shim in v1 would be superseded immediately. |
| Continuous input events | Text fields submit once on confirm, not on every keystroke. Sliders submit on release. Reduces event volume to what firmware can handle. |

---

## YAML Format

A uDisplay YAML file has two required top-level sections, plus an optional `style:`
block:

```yaml
device:
  name: "My Device"          # required
  version: "1.0"             # optional — informational, not parsed by client
  description: "..."         # optional — shown in Qt client detail view

style:                       # optional — see "Global Stylesheet" below
  default:
    accent: "#00d4aa"

widgets:
  widget_name:               # snake_case key → C macro suffix
    type: toggle             # discriminator field — required on every widget
    label: "Enable"          # most widgets accept a label
    # ...type-specific attributes...
```

### `device` block

| Field | Type | Required | Max length | Notes |
|---|---|---|---|---|
| `name` | string | yes | 64 chars | Shown in Qt client discovery list |
| `version` | string | no | 32 chars | Informational — not parsed by client |
| `description` | string | no | 255 chars | Shown in client detail view |
| `capabilities` | array of strings | no | — | List of capability tokens required by this definition. The client rejects connection with "Device requires app update" if any token is unrecognised. Omit for base v1 widgets only. Known values: `dropdown`, `label`, `layout-v2`. |

### `widgets` block

- Keys must be **lowercase snake_case** identifiers matching `^[a-z][a-z0-9_]*$`
- At least one widget is required
- Maximum **240 widgets** per device (IDs `0x10`–`0xFF`)
- Widgets are rendered top-to-bottom in declaration order (v1 layout)
- Widget IDs are assigned alphabetically by key path — not by YAML order

**ID assignment example:**

```yaml
widgets:
  temp_display:   # → WIDGET_ID_TEMP_DISPLAY = 0x1Au  (alphabetical: t)
  rate_slider:    # → WIDGET_ID_RATE_SLIDER  = 0x17u  (alphabetical: r)
  enable_toggle:  # → WIDGET_ID_ENABLE_TOGGLE = 0x10u  (alphabetical: e)
```

Child widgets (button LEDs, button-group items) are addressed as `parent.child` and
sorted into the same alphabetical namespace.

### Validation

```bash
udisplay-gen validate my_device.yaml   # exits 0 on success, non-zero with errors
```

---

## Widget Type Summary

| Type | Interactive | Device→client | Client→device | Setter | Handler |
|---|---|---|---|---|---|
| `display` | no | float (STATE_UPDATE) | — | `set_X(float v)` | — |
| `led` | no | bool (STATE_UPDATE) | — | `set_X(uint8_t v)` | — |
| `rgbled` | no | int32 0x00RRGGBB (STATE_UPDATE) | — | `set_X(int32_t rgb)` | — |
| `button` | yes | — | BUTTON_PRESS / BUTTON_RELEASE / BUTTON_CLICK events | — | `on_X_press()`, `on_X_release()`, `on_X_click()` |
| `button-group` | yes | — | BUTTON_PRESS / BUTTON_RELEASE / BUTTON_CLICK (per item) | — | — |
| `button-group-item` | yes (child) | — | BUTTON_PRESS / BUTTON_RELEASE / BUTTON_CLICK events | — | `on_X_press()`, `on_X_release()`, `on_X_click()` |
| `slider` | yes | float echo (STATE_UPDATE) | SLIDER_CHANGE event | `set_X(float v)` | `on_X_change(float value)` |
| `toggle` | yes | bool echo (STATE_UPDATE) | TOGGLE_CHANGE event | `set_X(uint8_t v)` | `on_X_change(uint8_t state)` |
| `text` (ro) | no | string (STATE_UPDATE) | — | `set_X(const char* s, uint8_t n)` | — |
| `text` (rw) | yes | string echo (STATE_UPDATE) | TEXT_SUBMIT event | `set_X(const char* s, uint8_t n)` | `on_X_submit(const char* str, uint8_t len)` |
| `dropdown` | yes | uint8 index (STATE_UPDATE) | SELECTION_CHANGE event | `set_X(uint8_t index)` | `on_X_change(uint8_t index)` |
| `label` | no | — | — | — | — |
| `separator` | no | — | — | — | — |
| `section` | no (container) | — | — | — | — |
| `row` | no (container) | — | — | — | — |
| `grid` | no (container) | — | — | — | — |

---

## Widget Types

---

### `display`

Numeric read-only display. Renders the latest float value pushed by the device.
No user interaction — the client never sends events for this widget type.

```
Device ──STATE_UPDATE(float)──► Client renders value
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"display"` | yes | — | |
| `label` | string | no | — | Shown above or beside the value. Max 32 chars. |
| `unit` | string | no | — | Unit suffix (e.g. `"V"`, `"°C"`, `"Hz"`). Max 16 chars. |
| `format` | string | no | `"%.2f"` | printf-style format applied to the raw float before display. Max 32 chars. |
| `style` | `"default"` \| `"large"` | no | `"default"` | `"large"` renders as a full-width hero block with 32px bold value — for primary instrument readings. |

**Example:**

```yaml
temp_display:
  type: display
  label: "Temperature"
  unit: "°C"
  format: "%.1f"
  style: large
```

**Generated C API:**

```c
#include "udisplay.h"
#include "udisplay_ui.h"

void someUpdateFunction(float temp) {
    set_temp_display(temp);
}

// No handler — output only
```

**Generated C++ API:**

`udisplay-gen build --lang cpp` emits `udisplay_ui.hpp` instead of `.h`/`.c`. Every
widget becomes a member of a generated `udisplay_ui::UDisplay` class, named after its
YAML key, instead of a set of free functions — construct one `UDisplay` instance and
call through it. Handlers differ by build flag: the default build (`--lang cpp`)
stores them as raw function pointers; adding `--modern` switches every handler to
`std::function<...>` instead, which is what makes lambda handlers possible (see
`button` below). Setters are unaffected by that flag — identical in both variants.

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

void someUpdateFunction(float temp) {
    ui.temp_display.set(temp);
}

// No handler — output only
```

---

### `led`

Boolean indicator. Renders as a colored dot with an optional label. Can be used
standalone or as a child of a `button` (rendered inline on the button face).

State is always **device-authoritative**: the client never flips the LED optimistically.
It stays in its last known state until a STATE_UPDATE arrives.

```
Device ──STATE_UPDATE(bool)──► Client renders dot ON/OFF
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"led"` | yes | — | |
| `label` | string | no | — | Shown beside the dot (standalone) or on the button face (child). Max 32 chars. |
| `color` | string (hex) | no | `#00d4aa` | Active/on color of the dot. Must be `#rrggbb`. Off-state is always `#2a2a4a`. |

**Example (standalone):**

```yaml
status_led:
  type: led
  label: "Active"
```

**Example (as button child — see `button` section):**

```yaml
power_btn:
  type: button
  label: "Power"
  widgets:
    power_led:
      type: led
      label: "PWR"
```

**Generated C API:**

```c
#include "udisplay.h"
#include "udisplay_ui.h"

void someUpdateFunction(uint8_t active) {
    set_status_led(active);                  // standalone
    set_power_btn_power_led(active);          // child LED — see `button` below
}

// No handler — output only
```

**Generated C++ API:**

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

void someUpdateFunction(bool active) {
    ui.status_led.set(active);                // standalone
    ui.power_btn.power_led.set(active);        // child LED — see `button` below
}

// No handler — output only
```

---

### `rgbled`

Full-colour RGB indicator. Renders as a filled dot whose colour is set by the device at
runtime. Use this for hardware RGB LEDs (WS2812, SK6812, etc.) where the device pushes
an arbitrary colour rather than a simple on/off state.

State is **device-authoritative**: the client never changes the dot colour without a
STATE_UPDATE.

```
Device ──STATE_UPDATE(int32 0x00RRGGBB)──► Client renders dot in that colour
```

Value `0` (black) renders the dot as off (`#2a2a4a`). Alpha is ignored — only the
low 24 bits (R, G, B) are used.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"rgbled"` | yes | — | |
| `label` | string | no | — | Shown beside the dot. Max 32 chars. |

**Example:**

```yaml
status_rgb:
  type: rgbled
  label: "Status"
```

**Generated C API:**

```c
#include "udisplay.h"
#include "udisplay_ui.h"

void someUpdateFunction(int32_t rgb) {
    set_status_rgb(rgb);   // 0x00RRGGBB packed, 0 = off
}

// No handler — output only
```

**Generated C++ API:**

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

void someUpdateFunction(uint32_t rgb) {
    ui.status_rgb.set(rgb);   // 0x00RRGGBB packed, 0 = off
}

// No handler — output only
```

---

### `button`

Momentary push button. Sends three events to the device per interaction.
No optimistic state change — the device drives any resulting state via STATE_UPDATE.

Supports child widgets rendered inline, at compact scale, on the button face,
declared under `widgets:` — the same key every other container widget (`row`/`grid`)
uses. Accepts any number of `led`, `rgbled`, `display`, or `label` children, plus a
nested `row` or `grid` of those same types (for multi-widget or vertical face
layouts) — the client renders the face via one embedded `RowWidget`, reusing the
same recursive `WidgetDelegate`/`RowWidget`/`GridWidget` machinery top-level `row`/
`grid` already use. The restriction applies recursively: a nested `row`/`grid`'s own
`widgets:` are just as restricted, at every depth. Interactive controls
(`toggle`, `slider`, `text`, `dropdown`, `button`, `button-group`) are never allowed
anywhere in a button's face — schema-enforced (`udisplay-gen validate` rejects them)
— because they'd create a second, overlapping touch target on top of the button's
own `MouseArea`. `section` face nesting is out of scope, tracked separately.

```
User presses     ──BUTTON_PRESS(0x06)──► Device        on_X_press()   ← start motion
User releases    ──BUTTON_RELEASE(0x07)─► Device        on_X_release() ← stop motion
  (within bounds)─BUTTON_CLICK(0x01)──► Device        on_X_click()   ← optional confirm
User cancels     ──BUTTON_RELEASE(0x07)─► Device        on_X_release() ← emergency stop
```

Firmware wires up only the handlers it needs. Unused handlers stay NULL and are silently skipped.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"button"` | yes | — | |
| `label` | string | no | — | Text on the button face. Max 32 chars. |
| `shape` | `"rect"` \| `"circle"` \| `"square"` | no | `"rect"` | `"circle"` for icon/action buttons; `"square"` for gamepad-style layouts. |
| `widgets` | object | no | — | Named child widgets — `led`, `rgbled`, `display`, `label`, or a nested `row`/`grid` of those (see note above). `led`/`rgbled`/`display` children (at any depth) get a `parent.child` ID path and setter; `label` children get no ID (same as a standalone `label`); a nested `row`/`grid` container itself gets no ID (transparent, same as a top-level container). |

Button color is **not** a per-widget attribute. It comes from the global `style:`
block's `button` / `button_text` tokens (see [Global Stylesheet](#global-stylesheet)
below) — every button on the device shares the same color from the active theme.
An earlier revision of this schema had a per-button `color:` hex attribute; it was
removed when the global stylesheet feature shipped, so a button no longer picks its
own accent color independent of the rest of the UI.

`udisplay-gen validate` is the hard gate for excluded interactive types — the client
itself parses device-supplied YAML directly, with no runtime schema validation, so it
stays permissive: an excluded type found anywhere in a button's face (at any depth)
emits a Warning diagnostic but still parses and renders, as defense in depth for
firmware YAML that bypasses `validate` entirely.

**Example (flat face — most common case):**

```yaml
power_btn:
  type: button
  label: "Power"
  shape: circle
  widgets:
    power_led:
      type: led
      label: "PWR"
```

In the generated header this produces:
- `WIDGET_ID_POWER_BTN` — the button
- `WIDGET_ID_POWER_BTN_POWER_LED` — the LED child
- `set_power_btn_power_led(uint8_t v)` — setter for the LED
- `on_power_btn_press`, `on_power_btn_release`, `on_power_btn_click` — handler fields in `udisplay_ui_handlers_t`

**Example (nested face — vertical layout via a single `grid` child):**

```yaml
power_btn:
  type: button
  label: "Power"
  widgets:
    face:
      type: grid
      columns: 1
      widgets:
        power_label:
          type: label
          text: "PWR"
          style: caption
        power_led:
          type: led
```

`face` (the grid container) is transparent to ID assignment, same as a top-level
`row`/`grid` — the generated header still produces `WIDGET_ID_POWER_BTN_POWER_LED`
and `set_power_btn_power_led(uint8_t v)`, with `face` contributing no path segment
of its own. `power_label` gets no ID (decoration, same as any standalone `label`).

**Generated C API:**

No setter — the device does not push state to buttons. All three handlers
(`on_X_press`, `on_X_release`, `on_X_click`) are fields on `udisplay_ui_handlers_t`;
wire up only the ones you need:

```c
#include "udisplay.h"
#include "udisplay_ui.h"

static int g_power_on = 0;

static void handle_power_btn(void)
{
    g_power_on = !g_power_on;
    set_power_btn_power_led((uint8_t)g_power_on);
}

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_power_btn_press = handle_power_btn,
    // ...
};
```

**Generated C++ API:**

A button with no `widgets:` face is a plain `ButtonWidget` member; a button that
declares a face (like `power_btn` above) gets a derived class with one member per
`led`/`rgbled`/`display` descendant instead (flattened, same ID rule as C). Lambda
handlers require `--lang cpp --modern` — the plain `--lang cpp` build stores handlers
as raw function pointers, which a capturing lambda can't convert to.

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

static int g_power_on = 0;

int main(/* ... */) {

    /* Lambda event handlers — the defining feature of the --modern variant */
    ui.power_btn.on_press = []() {
        g_power_on = !g_power_on;
        ui.power_btn.power_led.set((bool)g_power_on);
    };

}
```

---

### `button-group`

Exclusive-select button group. Exactly one item is active at a time. Sends a
BUTTON_PRESS event with the **item's** widget ID when the selection changes.

The group itself has no setter and no handler — events come from the items.

```
Client selects item ──BUTTON_PRESS(item_id)──► Device
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"button-group"` | yes | — | |
| `label` | string | no | — | Optional group label shown above the group. Max 32 chars. |
| `layout` | `"grid"` \| `"dpad"` | no | `"grid"` | `"grid"` = wrapping grid; `"dpad"` = 5-position directional pad (items must specify `position`). |
| `items` | object | yes | — | Named items; minimum 2. Each item gets its own widget ID. |

**`button-group-item` sub-attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `label` | string | yes | — | Text on the item button. Max 32 chars. |
| `position` | `"top"` \| `"right"` \| `"bottom"` \| `"left"` \| `"center"` | no | — | Required for `dpad` layout; ignored for `grid`. |

**Example (grid layout):**

```yaml
mode_sel:
  type: button-group
  label: "Mode"
  layout: grid
  items:
    fast:
      label: "Fast"
    slow:
      label: "Slow"
    turbo:
      label: "Turbo"
```

**Example (dpad layout):**

```yaml
direction:
  type: button-group
  layout: dpad
  items:
    up:
      label: "▲"
      position: top
    down:
      label: "▼"
      position: bottom
    left:
      label: "◄"
      position: left
    right:
      label: "►"
      position: right
    ok:
      label: "OK"
      position: center
```

**Generated C API:**

No setter for the group itself. Three handlers per item — wire up only the ones
you need:

```c
#include "udisplay.h"
#include "udisplay_ui.h"

static int g_rate_multiplier = 1;

static void handle_fast_click(void) { g_rate_multiplier = 2; }
static void handle_slow_click(void) { g_rate_multiplier = 1; }
static void handle_turbo_click(void) { g_rate_multiplier = 4; }

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_mode_sel_fast_click  = handle_fast_click,
    .on_mode_sel_slow_click  = handle_slow_click,
    .on_mode_sel_turbo_click = handle_turbo_click,
    // ...
};
```

**Generated C++ API:**

The group gets its own derived class holding one `ButtonItem` member per item —
no separate group-level handler. Lambda handlers require `--lang cpp --modern`
(see `button` above for why).

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

static int g_rate_multiplier = 1;

int main(/* ... */) {
    ui.mode_sel.fast.on_click  = []() { g_rate_multiplier = 2; };
    ui.mode_sel.slow.on_click  = []() { g_rate_multiplier = 1; };
    ui.mode_sel.turbo.on_click = []() { g_rate_multiplier = 4; };
}
```

---

### `slider`

Numeric read-write slider. The client sends a SLIDER_CHANGE event when the user
releases the slider handle. The device responds with STATE_UPDATE to confirm the
accepted value — which may differ if the device clamps or quantizes the input.

```
Client drag+release ──SLIDER_CHANGE(float)──► Device
Device ──────────────STATE_UPDATE(float)────► Client (echo/clamped value)
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"slider"` | yes | — | |
| `label` | string | no | — | Shown above the slider. Max 32 chars. |
| `min` | number | yes | — | Minimum value (inclusive). |
| `max` | number | yes | — | Maximum value (inclusive). Must be > `min`. |
| `step` | number | no | `1` | Step increment. Must be positive. |
| `unit` | string | no | — | Unit suffix beside the current value. Max 16 chars. |

**Example:**

```yaml
rate_slider:
  type: slider
  label: "Sample Rate"
  min: 0.1
  max: 10.0
  step: 0.1
  unit: "Hz"
```

**Generated C API:**

Clamp, update state, and echo the accepted value back:

```c
#include "udisplay.h"
#include "udisplay_ui.h"

static float g_rate_hz = 1.0f;

static void handle_rate_slider(float v)
{
    if (v < 0.1f) v = 0.1f;
    if (v > 10.0f) v = 10.0f;
    g_rate_hz = v;
    set_rate_slider(g_rate_hz);   // echo accepted value
}

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_rate_slider_change = handle_rate_slider,
    // ...
};
```

**Generated C++ API:**

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;
static float g_rate_hz = 1.0f;

int main(/* ... */) {
    ui.rate_slider.on_change = [](float v) {
        if (v < 0.1f) v = 0.1f;
        if (v > 10.0f) v = 10.0f;
        g_rate_hz = v;
        ui.rate_slider.set(g_rate_hz);   // echo accepted value
    };
}
```

Lambda handlers require `--lang cpp --modern` (see `button` above for why).

---

### `toggle`

Boolean read-write toggle switch. The client sends a TOGGLE_CHANGE event on user
interaction. The device confirms the new state via STATE_UPDATE. No optimistic flip —
the widget stays in its current visual state until the device confirms.

```
Client tap ──TOGGLE_CHANGE(bool)──► Device
Device ──────STATE_UPDATE(bool)───► Client (confirmed state)
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"toggle"` | yes | — | |
| `label` | string | no | — | Shown beside the toggle. Max 32 chars. |

**Example:**

```yaml
enable_toggle:
  type: toggle
  label: "Enabled"
```

**Generated C API:**

Update state, then echo the confirmed state back:

```c
#include "udisplay.h"
#include "udisplay_ui.h"

static int g_enabled = 0;

static void handle_enable_toggle(uint8_t state)
{
    g_enabled = state;
    set_enable_toggle((uint8_t)g_enabled);   // echo confirmed state
}

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_enable_toggle_change = handle_enable_toggle,
    // ...
};
```

**Generated C++ API:**

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;
static int g_enabled = 0;

int main(/* ... */) {
    ui.enable_toggle.on_change = [](bool state) {
        g_enabled = state;
        ui.enable_toggle.set((bool)g_enabled);   // echo confirmed state
    };
}
```

Lambda handlers require `--lang cpp --modern` (see `button` above for why).

---

### `text`

String widget. Has two modes controlled by the `mode` attribute:

- **`ro`** (default) — display-only. Renders a device-pushed string. The client never
  sends events. The device pushes updates via STATE_UPDATE.
- **`rw`** — editable text field. The client sends a TEXT_SUBMIT event when the user
  confirms input (keyboard Done key or submit button) — **not** on every keystroke.
  The device may echo the accepted string back via STATE_UPDATE.

Both modes have a setter (the device can push text to display). Only `rw` mode has a handler.

```
ro: Device ──STATE_UPDATE(string)──► Client renders string

rw: Client submit ──TEXT_SUBMIT(string)──► Device
    Device ─────────STATE_UPDATE(string)──► Client (optional echo)
```

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"text"` | yes | — | |
| `label` | string | no | — | Shown above the field. Max 32 chars. |
| `mode` | `"ro"` \| `"rw"` | no | `"ro"` | `"ro"` = display-only; `"rw"` = editable. |
| `placeholder` | string | no | — | Placeholder in empty field (`rw` only). Max 64 chars. |
| `maxlength` | integer | no | `255` | Max chars user can enter (`rw` only). Range 1–255. |

**Example (read-write):**

```yaml
ssid_input:
  type: text
  label: "Target SSID"
  mode: rw
  placeholder: "Enter SSID"
  maxlength: 64
```

**Example (read-only):**

```yaml
status_display:
  type: text
  label: "Status"
  # mode: ro is the default — can be omitted
```

**Generated C API:**

```c
#include "udisplay.h"
#include "udisplay_ui.h"

// Setter — push a string to display (both modes)
void someUpdateFunction(const char* status) {
    set_status_display(status, (uint8_t)strlen(status));
}

// Handler — called when the user submits text (rw only)
static void handle_ssid_input(const char* str, uint8_t len)
{
    // ... store the submitted SSID ...
    set_ssid_input(str, len);   // optional echo
}

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_ssid_input_submit = handle_ssid_input,
    // ...
};
// (no handler for ro mode)
```

**Generated C++ API:**

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;

// Setter — push a string to display (both modes)
void someUpdateFunction(const std::string& status) {
    ui.status_display.set(status.c_str(), (uint8_t)status.size());
}

int main(/* ... */) {
    // Handler — called when the user submits text (rw only)
    ui.ssid_input.on_submit = [](const char* str, uint8_t len) {
        // ... store the submitted SSID ...
        ui.ssid_input.set(str, len);   // optional echo
    };
    // (no handler for ro mode)
}
```

Lambda handlers require `--lang cpp --modern` (see `button` above for why).

---

### `dropdown`

Compact exclusive-select widget. Renders as a collapsed label+chevron row, expands to
a scrollable list when tapped. Covers the "too many items for a button-group" case.

The device pushes the current selection via STATE_UPDATE (VAL_UINT8, 0-based index).
The client sends a SELECTION_CHANGE event when the user picks a new item.

```
Client picks item ──SELECTION_CHANGE(index)──► Device
Device ──────────────STATE_UPDATE(index)─────► Client (confirmed selection)
```

**Capability token:** `dropdown` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"dropdown"` | yes | — | |
| `label` | string | no | — | Shown above the collapsed row. Max 32 chars. |
| `items` | object | yes | — | Ordered map of `key: "Display Label"` pairs. Minimum 2 items. Keys must be lowercase snake_case. |

Items are listed as key/value pairs where the value is the display label:

```yaml
items:
  sta:   "Station"
  ap:    "Access Point"
  apsta: "AP + Station"
  off:   "Disabled"
```

The selection index is 0-based in YAML declaration order (`sta`=0, `ap`=1, `apsta`=2, `off`=3).
Items do **not** get individual widget IDs — only the dropdown itself gets one.

**Example:**

```yaml
wifi_mode:
  type: dropdown
  label: "Wi-Fi Mode"
  items:
    sta:   "Station"
    ap:    "Access Point"
    apsta: "AP + Station"
    off:   "Disabled"
```

**Generated C API:**

Per-item index constants (`WIFI_MODE_STA`, `WIFI_MODE_AP`, ...) let you avoid
hardcoding indices on either side:

```c
#include "udisplay.h"
#include "udisplay_ui.h"

static uint8_t g_wifi_mode = WIFI_MODE_STA;

static void handle_wifi_mode(uint8_t index)
{
    g_wifi_mode = index;
    set_wifi_mode(g_wifi_mode);   // echo confirmed selection
}

static const udisplay_ui_handlers_t g_handlers = {
    // ...
    .on_wifi_mode_change = handle_wifi_mode,
    // ...
};
```

**Generated C++ API:**

Dropdowns get their own derived class with a scoped `Option` enum instead of the C
API's `#define` index constants:

```cpp
#include "udisplay.h"
#include "udisplay_ui.hpp"

using namespace udisplay_ui;

static UDisplay ui;
static WifiModeWidget::Option g_wifi_mode = WifiModeWidget::Option::sta;

int main(/* ... */) {
    ui.wifi_mode.on_change = [](WifiModeWidget::Option selection) {
        g_wifi_mode = selection;
        ui.wifi_mode.set(g_wifi_mode);   // echo confirmed selection
    };
}
```

Lambda handlers require `--lang cpp --modern` (see `button` above for why).

---

### `label`

Static text decoration. Renders a heading, body, or caption text block. Content is
set in YAML at compile time and never updated via STATE_UPDATE. Gets no widget ID.
Codegen skips it entirely (no setter, no handler, no ID constant).

**Capability token:** `label` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"label"` | yes | — | |
| `text` | string | yes | — | The static text to render. Max 255 chars. |
| `style` | `"heading"` \| `"body"` \| `"caption"` | no | `"body"` | `heading` = bold, larger. `caption` = small, muted. |
| `flex` | integer | no | — | Layout weight when used as a row/grid child. Same semantics as any other widget's `flex` — see [`row`](#row). |
| `align` | `"left"` \| `"right"` \| `"center"` | no | — | Position within a row/grid cell when used as a child. Same semantics as any other widget's `align` — see [`row`](#row). Not text alignment; use `textAlign` for that. |
| `textAlign` | `"left"` \| `"right"` \| `"center"` \| `"justify"` | no | `"left"` | Text alignment within the label itself. |

**Example:**

```yaml
network_section_label:
  type: label
  text: "Network Settings"
  style: heading
```

**Generated C API:** None — label has no ID and no protocol exchange.

**Generated C++ API:** None — same reason.

---

### `separator`

Horizontal visual divider. No attributes, no widget ID, no protocol exchange.
Codegen skips it entirely.

**Capability token:** `label` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Notes |
|---|---|---|---|
| `type` | `"separator"` | yes | |

**Example:**

```yaml
divider_1:
  type: separator
```

**Generated C API:** None.

**Generated C++ API:** None.

---

### `section`

Named collapsible group container. Renders a header bar with an optional label, with
children displayed vertically below it (collapsible by the user if `collapsible: true`).

The section name is **excluded** from child widget ID paths. A child named `rate` inside
a section named `advanced` gets ID path `rate` → `WIDGET_ID_RATE`, not `WIDGET_ID_ADVANCED_RATE`.
Schema enforces globally unique leaf names across all scopes.

**Capability token:** `layout-v2` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"section"` | yes | — | |
| `label` | string | no | — | Section header label. Max 64 chars. |
| `collapsible` | boolean | no | `false` | Whether the user can collapse the section. |
| `widgets` | object | yes | — | Named child widgets. Same format as the top-level `widgets:` block. |

**Example:**

```yaml
advanced:
  type: section
  label: "Advanced"
  collapsible: true
  widgets:
    rate_slider:
      type: slider
      label: "Rate"
      min: 0.1
      max: 10.0
```

**Generated C API:** None for the section itself. Children get their own API entries.

In the generated header this produces:
- `WIDGET_ID_RATE_SLIDER` (not `WIDGET_ID_ADVANCED_RATE_SLIDER`)
- `set_rate_slider(float v)` and `on_rate_slider_change(float value)` as normal

**Generated C++ API:** None for the section itself — same ID-flattening rule applies.
Children still become normal members of the generated `udisplay_ui::UDisplay` class
(e.g. `SliderWidget rate_slider;`), not nested under a `section` member.

---

### `row`

Horizontal layout container. Children are rendered side-by-side left-to-right.
Gets no widget ID. The row name is excluded from child ID paths.

Each child can declare a `flex: N` integer weight (N ≥ 1). Children with `flex` fill
remaining horizontal space proportionally. Children without `flex` use their implicit
width — each widget type computes this from its own content (label text width, control's
natural size, etc.), not a fixed fallback.

**Capability token:** `layout-v2` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"row"` | yes | — | |
| `widgets` | object | yes | — | Named child widgets. |
| `align` | string | no | `"left"` | Default content alignment (`"left"`\|`"right"`\|`"center"`) for children that don't stretch to fill their space. A child's own `align` (below) overrides this for that one child. |

**Child flex/align:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `flex` | integer | no | — | Layout weight ≥ 1. Omit for auto-width. `flex: 2` = twice the width of a `flex: 1` sibling. |
| `align` | string | no | — | `"left"`\|`"right"`\|`"center"`. Overrides the row's own `align` for this one child. Omit to inherit the row's default. |

This applies uniformly to every child type, including `label` — a label's `align` positions it within the row/grid cell like any other widget. A label's own text alignment is a separate attribute, `textAlign` (see [`label`](#label)), independent of its row/grid `align`.

**Example:**

```yaml
controls_row:
  type: row
  widgets:
    enable_toggle:
      type: toggle
      label: "Enable"
      flex: 1
    rate_slider:
      type: slider
      label: "Rate"
      min: 0.0
      max: 10.0
      flex: 2
```

`rate_slider` takes twice the horizontal space of `enable_toggle`.

**Generated C API:** None for the row itself. Children get their own API entries.

**Generated C++ API:** None for the row itself. Children still become normal members
of the generated `udisplay_ui::UDisplay` class, not nested under a `row` member.

---

### `grid`

Grid layout container. Children are placed left-to-right, wrapping to a new row every
`columns` items. Gets no widget ID. The grid name is excluded from child ID paths.

**Capability token:** `layout-v2` — reserved for future use, see
[capabilities field](#capabilities-field). Omit `capabilities:` for now; the widget
works without it.

**Attributes:**

| Attribute | Type | Required | Default | Notes |
|---|---|---|---|---|
| `type` | `"grid"` | yes | — | |
| `columns` | integer | yes | — | Number of columns. Minimum 1 — `columns: 1` renders as a single-column (ColumnWidget-style) layout. |
| `widgets` | object | yes | — | Named child widgets. Auto-flow left-to-right, top-to-bottom. |
| `align` | string | no | `"left"` | Default content alignment (`"left"`\|`"right"`\|`"center"`) for children that don't stretch to fill their cell. A child's own `align` overrides this for that one child. |

Children accept the same `flex`/`align` attributes as in `row` (optional). A `flex`
ratio is only shared among children in the same virtual column (`index % columns`) —
a grid's columns are sized independently, so a high-flex item in one column can't
claim space from another column.

**Example:**

```yaml
button_grid:
  type: grid
  columns: 2
  widgets:
    btn_a:
      type: button
      label: "A"
    btn_b:
      type: button
      label: "B"
    btn_c:
      type: button
      label: "C"
    btn_d:
      type: button
      label: "D"
```

**Generated C API:** None for the grid itself. Children get their own API entries.

**Generated C++ API:** None for the grid itself. Children still become normal members
of the generated `udisplay_ui::UDisplay` class, not nested under a `grid` member.

---

## capabilities field

`device.capabilities` is a reserved field with no current usage. It exists in the schema
for a planned future feature: gating widget types behind a minimum client version, so an
older client can refuse a definition it can't render instead of misbehaving silently.

That gate is not wired up yet — the client's list of recognised tokens is currently empty,
so declaring **any** capability string today causes the client to reject the connection
with "Device requires app update", regardless of which token you use. Omit `capabilities:`
entirely for every widget set today, including `dropdown`, `label`, `separator`, `section`,
`row`, and `grid` — none of them require it to function.

```yaml
device:
  name: "My Device"
  # capabilities: [...]   # do not use yet — see note above
```

Note: the `style:` block below is unrelated to `capabilities` and is safe to use with
any widget set, including base v1 definitions.

---

## Global Stylesheet

An optional top-level `style:` block defines one or more named color themes for the
whole UI. This replaced per-widget color attributes (e.g. the button's old `color:`
field) with a single place to theme a device.

```yaml
style:
  default:
    accent: "#00d4aa"
    button: "#00d4aa"
  warning:
    accent: "#f5a623"
    button: "#f5a623"

widgets:
  # ...
```

### Structure

- `style:` maps **theme names** (lowercase snake_case) to a set of color tokens.
- The `default` theme is the base every other theme cascades from: a named theme
  only needs to override the tokens it wants to change — anything it omits inherits
  from `default`, and anything `default` itself omits falls back to the client's
  built-in dark-UI defaults (see table below).
- The client renders whichever theme is currently **active**. On connect the active
  theme is always `"default"`. Omit `style:` entirely to use the built-in defaults
  unmodified.

### Color tokens

Each token accepts a CSS hex color (`#rgb`, `#rrggbb`, `#rrggbbaa`) or the keyword
`transparent`. An invalid value is silently ignored (falls back to the inherited /
default color) rather than failing validation — the schema is the authoritative
source for the current token list.

| Token | Used for | Built-in default |
|---|---|---|
| `background` | App / page background | `#0d0d1a` |
| `surface` | Card / panel surface (sliders, text fields, toggles, dropdown, section, button-group) | `#1a1a2e` |
| `text` | Body text | `#c0c0c0` |
| `text_muted` | Secondary / placeholder text | `#888888` |
| `text_heading` | Heading / prominent text (labels, dropdown selection) | `#e0e0e0` |
| `border` | Widget / component border (text field, toggle) | `#1e1e3a` |
| `line` | Horizontal separator line / bottom rule (used by most widgets) | `#1e1e3a` |
| `accent` | Primary accent color (values, sliders, LED on-state fallback) | `#00d4aa` |
| `button` | Button background | `#00d4aa` |
| `button_text` | Button label text | `#0d0d1a` |
| `led_on` | Button-face LED dot fill when on *(reserved — not yet consumed by any widget)* | `#ffffff` |
| `led_off` | Button-face LED dot fill when off *(reserved)* | `transparent` |
| `led_border` | Button-face LED dot border *(reserved)* | `#ffffff` |
| `success` | Success / positive indicator *(reserved)* | `#00d4aa` |
| `warning` | Warning / caution indicator *(reserved)* | `#f5a623` |
| `error` | Error / danger indicator *(reserved)* | `#e05555` |

A standalone `led` widget's own `color` attribute (see the `led` section above)
still overrides the `accent` token for that one widget — the stylesheet only
supplies the fallback when `color` is omitted.

### Switching themes

The Qt client exposes `controller.activeStyle` (the resolved color map for the
current theme), `controller.availableStyles` (theme names declared in the YAML),
and `controller.setActiveStyle(name)` to switch. As of this writing theme
switching is **client-side only** — there is no protocol message for the device
to trigger a theme change. A device YAML can declare
multiple themes today, but only client-initiated switching (e.g. a future
settings UI) can select anything other than `default`.
