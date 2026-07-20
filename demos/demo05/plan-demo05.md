# Plan: demo05 — ESP32 Minimal BLE Demo

**Status:** DRAFT — under /plan-eng-review
**Goal:** Minimal ESP32 firmware for testing BLE connection with the Qt client.
One button, one standalone LED. Button press toggles LED.

---

## Problem

The BleTransport + BleScanner are implemented (TODO-011, PR #33). We need a real
ESP32 device to validate the BLE connection end-to-end. demo01–03 are TCP-only
PC emulators; no ESP32 firmware exists yet.

## Scope

**In scope:**
- `demo05.yaml` — one `button` + one `led` (top-level, not button child)
- `demo05/main/main.cpp` — ESP-IDF app: BLE GATT server + uDisplay init + toggle logic
- `demo05/CMakeLists.txt` — IDF project root (`cmake_minimum_required` + `include(...)`)
- `demo05/main/CMakeLists.txt` — IDF component (`idf_component_register`)
- `demo05/sdkconfig.defaults` — BLE config (stack, MTU=512, disable WiFi)
- `demo05/main/CMakeLists.txt` — runs `udisplay-gen build demo05.yaml --lang c` pre-build
- Generated files from udisplay-gen: `udisplay_ui.h`, `udisplay_ui.c`, blob data

**NOT in scope:**
- see "NOT in scope" section below

## Widget Layout

```yaml
device:
  name: "Demo05"
  version: "1.0"
  description: "Minimal BLE demo — button toggles LED"

widgets:
  push_btn:
    type: button
    label: "Toggle"

  status_led:
    type: led
    label: "LED"
```

Widget ID assignment (alphabetical): `push_btn` → 0x10, `status_led` → 0x11.

## Data Flow

```
ESP32 (demo05)                    Qt Client (Android)
─────────────────────────────     ──────────────────────────────
BLE advertise service UUID
  29825AAA-D882-46F7-A4D6-         BleScanner discovers "Demo05"
  EA8431AD3455                     User taps "Connect"

udisplay_on_connect()         ←── BLE connection established

HANDSHAKE → (data char)       ───► BootstrapManager receives
CHUNK_HEADER_RESPONSE         ───► blob downloaded
CHUNK_RESPONSE ×N             ───►
                              ◄─── HANDSHAKE_ACK / CLIENT_READY

on_client_ready():
  set_status_led(0)           ───► LED widget renders OFF

[User presses "Toggle"]
                              ◄─── EVENT(BUTTON_CLICK, push_btn)
on_button_click():
  g_led = !g_led
  set_status_led(g_led)       ───► LED widget renders ON/OFF

udisplay_heartbeat() (5s)     ───► heartbeat echo expected
```

## ESP-IDF Project Structure

```
demos/demo05/
  CMakeLists.txt          — IDF project root (cmake_minimum_required + include IDF)
  sdkconfig.defaults      — BLE stack, MTU, disable WiFi
  main/
    CMakeLists.txt        — idf_component_register + pre-build udisplay-gen step
    demo05.yaml           — widget definition
    main.cpp              — BLE GATT callbacks → udisplay + toggle logic
    [generated]
      udisplay_ui.h       — generated C API
      udisplay_ui.c       — generated event dispatch
      udisplay_data.h     — BLOB constants
```

## BLE Setup Responsibilities

main.cpp handles:
1. **NimBLE** init (`nimble_port_init`, `ble_gatts_count_cfg`, `ble_gatts_add_svcs`) — chosen over Bluedroid: ~30KB vs ~80KB RAM, Espressif-recommended for IDF 5.x
2. GATT service table with `control` (WRITE_WITH_RESPONSE) + `data` (NOTIFY)
3. GAP advertising with 128-bit service UUID `29825AAA-D882-46F7-A4D6-EA8431AD3455`
4. `on_connect` / `on_disconnect` → `udisplay_on_connect()` / `udisplay_on_disconnect()`
5. `on_write` (control char) → `udisplay_ble_feed(data, len)`
6. Timer (FreeRTOS) → `udisplay_heartbeat()` every 5s
7. `send_cb` → `ble_gatts_notify_custom(conn_handle, data_attr_handle, om)`

## NOT in scope

- Shared BLE helper (`demos/shared/demo_ble.c/h`) — only one BLE demo so far; extract when demo06/TODO-012 arrives (rule of three)
- WiFi / TCP stack — disabled in sdkconfig.defaults
- Authentication (no HMAC) — no-auth path only
- Real sensor data — LED state is the only device→client value
- CI build validation — HIL CI (TODO-006) not yet set up
- iOS BLE validation — covered by TODO-002 separately

## What already exists (reuse)

| Existing | Reused in demo05 |
|---|---|
| `libudisplay` protocol library | Yes — `udisplay_init`, `udisplay_ble_feed`, `udisplay_heartbeat` |
| `udisplay-gen` C codegen | Yes — generates `udisplay_ui.h/c` + BLOB from demo05.yaml |
| `docs/protocol.md` BLE UUIDs | Yes — service UUID `29825AAA-D882-46F7-A4D6-EA8431AD3455` in advertising |
| `demos/shared/demo_tcp.c/h` | **No** — TCP-only; demo05 uses BLE GATT directly |
| `demos/demo01/main.c` event pattern | Reference only — not linked |

## Decisions (from review)

| # | Decision | Choice |
|---|---|---|
| D1 | BLE stack | **NimBLE** (not Bluedroid) — ~30KB RAM, IDF 5.x recommended |
| D2 | Initial LED state push | **`on_client_ready` callback** (not `g_initial_sent` tick flag) |
| D3 | Button event | **`BUTTON_CLICK`** (not `BUTTON_PRESS`) — standard toggle semantics |
| D4 | Reconnect state | **Reset `g_led = 0` on disconnect** — deterministic TC-4 behavior |

## Widget IDs (alphabetical assignment)

- `push_btn` → `WIDGET_ID_PUSH_BTN = 0x10`
- `status_led` → `WIDGET_ID_STATUS_LED = 0x11`

## Failure Modes

| Codepath | Scenario | Test? | Handling | Visible? |
|---|---|---|---|---|
| NimBLE GATT init | `nimble_port_init()` fails (OOM) | No | **None** | Silent — device never advertises |
| `on_client_ready` → `set_status_led` | Sent before active | No | Library guard (`do_send` checks) | Safe |
| Button click dispatch | NimBLE task blocked | No | None | Silent — LED doesn't toggle |
| `xTimerCreate` (heartbeat) | FreeRTOS heap exhausted | No | **None → CRITICAL GAP** | After ~15s: comms_error, disconnect |

**Critical gap:** `xTimerCreate` return value must be checked. If NULL, log `ESP_LOGE` and abort/halt. Without this, a heap exhaustion silently kills the heartbeat with no user indication.

## NOT in scope

| Item | Rationale |
|---|---|
| `demos/shared/demo_ble.c/h` | Rule of three — extract after second BLE demo (TODO-031) |
| WiFi / TCP | Disabled in sdkconfig.defaults — BLE-only test |
| HMAC authentication | No-auth path only; auth demos deferred |
| Real sensor/GPIO | LED state is virtual (no real GPIO wiring) |
| CI build validation | HIL CI (TODO-006) not set up |
| iOS BLE validation | TODO-002 separate scope |

## What already exists (reuse)

| Existing | Reused in demo05 |
|---|---|
| `libudisplay` protocol library | Yes — `udisplay_init`, `udisplay_ble_feed`, `udisplay_heartbeat` |
| `udisplay-gen` C codegen | Yes — generates `udisplay_ui.h/c` + BLOB from demo05.yaml |
| `docs/protocol.md` BLE UUIDs | Yes — service UUID in advertising |
| `demos/shared/demo_tcp.c/h` | **No** — TCP-only |
| `demos/demo01/main.c` | Reference pattern only |

## Implementation Tasks

Synthesized from review findings. Run with Claude Code; checkbox as you ship.

- [ ] **T1 (P1, human: ~10min / CC: ~2min)** — demo05.yaml — create widget definition
  - Surfaced by: Step 0 — minimum viable YAML
  - Files: `demos/demo05/demo05.yaml`
  - Verify: `udisplay-gen validate demos/demo05/demo05.yaml` exits 0

- [ ] **T2 (P1, human: ~2h / CC: ~15min)** — main.cpp — NimBLE GATT server + uDisplay + toggle logic
  - Surfaced by: Architecture review D1–D4
  - Files: `demos/demo05/main/main.cpp`
  - Key: NimBLE stack, `on_client_ready` → `set_status_led(0)`, `on_push_btn_click` toggles, `g_led=0` on disconnect, `xTimerCreate` NULL check
  - Verify: `idf.py build` succeeds; HIL TC-1 through TC-4

- [ ] **T3 (P1, human: ~15min / CC: ~5min)** — CMakeLists — ESP-IDF project + component files
  - Surfaced by: Step 0 — ESP-IDF project structure
  - Files: `demos/demo05/CMakeLists.txt`, `demos/demo05/main/CMakeLists.txt`
  - Verify: `idf.py build` succeeds without warnings

- [ ] **T4 (P1, human: ~5min / CC: ~2min)** — sdkconfig.defaults — NimBLE + MTU=512 + disable WiFi
  - Surfaced by: Performance review — `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512`; D1 — NimBLE stack
  - Files: `demos/demo05/sdkconfig.defaults`
  - Key entries: `CONFIG_BT_ENABLED=y`, `CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512`, `CONFIG_ESP_WIFI_ENABLED=n`
  - Verify: `idf.py menuconfig` shows NimBLE enabled, WiFi disabled

- [ ] **T5 (P2, done)** — TODOS.md — added TODO-031 (demo_ble.c/h extraction)
  - Surfaced by: Code quality — DRY opportunity for TODO-012

## Parallelization

Sequential — all files belong to a single demo05 module. No parallelization opportunity.

## GSTACK REVIEW REPORT

| Review | Trigger | Why | Runs | Status | Findings |
|--------|---------|-----|------|--------|----------|
| CEO Review | `/plan-ceo-review` | Scope & strategy | 0 | — | — |
| Codex Review | `/codex review` | Independent 2nd opinion | 0 | — | — |
| Eng Review | `/plan-eng-review` | Architecture & tests (required) | 30 | CLEAN (PLAN) | 4 issues, 1 critical gap |
| Design Review | `/plan-design-review` | UI/UX gaps | 0 | — | — |
| DX Review | `/plan-devex-review` | Developer experience gaps | 0 | — | — |

- **UNRESOLVED:** 0
- **VERDICT:** ENG CLEARED — ready to implement.
