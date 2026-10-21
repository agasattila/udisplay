# Design: optional NimBLE BLE transport backend (issue #50)

Non-interactive session; decisions made autonomously.
Branch: feat/nimble-transport-backend
Repo: agasattila/udisplay
Issue: #50
Status: IMPLEMENTED (PR pending)

## Problem

demo05 carries ~400 lines of NimBLE GATT-server code that every uDisplay BLE
firmware needs verbatim: the service/characteristic table, advertising, the
connect → subscribe → `udisplay_on_connect()` sequencing, MTU tracking, the
one-indication-in-flight TX queue, the HELLO retry callout, the heartbeat
timer, and the "client never subscribed" watchdog. An application author has
to understand all of it to put a UI on BLE.

## Requirements (from the issue)

- libudisplay stays a C library; the backend and its API are C.
- The generic `udisplay_send_fn` transport stays the core's only transport
  dependency; applications can still bring their own transport.
- Explicit compile-time opt-in. Without it: no NimBLE headers, no NimBLE
  link, no NimBLE code.
- NimBLE-generic code separated from ESP-IDF glue, so a Mynewt/other port
  only needs new glue.
- demo05 migrated; existing wire behavior (INDICATE with confirmation,
  WRITE-with-response, HELLO readiness, 2-byte-length TX ring semantics)
  preserved.

## Public API — `include/libudisplay/udisplay_nimble.h`

No NimBLE types in the header, so it can be included from any C/C++ file
without the NimBLE include paths.

```c
typedef struct {
    const char* device_name;        /* GAP name + advertised name (required) */
    uint32_t    heartbeat_interval_ms;  /* 0 → UDISPLAY_NIMBLE_HEARTBEAT_MS_DEFAULT (5000) */
    void (*on_connect)(void* userdata);               /* link up (optional) */
    void (*on_disconnect)(int reason, void* userdata);/* link down (optional) */
    void* userdata;
} udisplay_nimble_config_t;

void udisplay_nimble_send(const uint8_t* data, uint16_t len, void* userdata);
int  udisplay_nimble_init(udisplay_t* ctx, const udisplay_nimble_config_t* cfg);
int  udisplay_nimble_start(void);
void udisplay_nimble_disconnect(void);
int  udisplay_nimble_is_connected(void);
void udisplay_nimble_lock(void);
void udisplay_nimble_unlock(void);
```

Application code (demo05 after migration):

```cpp
ui.init(udisplay_nimble_send, UDISPLAY_TRANSPORT_BLE);
udisplay_nimble_config_t ble = {};
ble.device_name   = "Demo05";
ble.on_disconnect = on_ble_disconnect;
ESP_ERROR_CHECK(udisplay_nimble_init(ui.ctx(), &ble));
ESP_ERROR_CHECK(udisplay_nimble_start());
```

### Decisions

1. **Singleton.** The GATT service table is registered once with the NimBLE
   host and `udisplay_send_fn` carries only the wrapper's userdata, not the
   ctx. One backend instance serving one `udisplay_t` and one connection
   matches today's demo and the NimBLE peripheral role. (A second uDisplay
   session would go over TCP, which multi-instance already supports.)
2. **The app passes `udisplay_nimble_send` to its existing init call**
   (`ui.init(...)` / `udisplay_ui_init(...)` / `udisplay_init(...)`), so no
   codegen change is needed. `udisplay_nimble_init()` checks that the ctx's
   send fn and transport match and fails otherwise.
3. **The backend owns the lifecycle**: stack init, GAP/GATT services,
   advertising (restart after disconnect / failed connect / adv complete),
   subscribe-gated `udisplay_on_connect()`, MTU → `udisplay_ble_set_mtu()`,
   control writes → `udisplay_feed()` (framing error → disconnect), the HELLO
   one-shot timer (`udisplay_set_timer`), the heartbeat, and the
   subscribe-wait watchdog.
4. **Automatic comms-error teardown.** `udisplay_nimble_init()` interposes
   on the ctx's `on_comms_error`: the app's handler (if any) still runs, then
   the backend terminates the link. Every BLE firmware did this by hand.
5. **Heartbeat moves into the NimBLE host task** (an NPL callout instead of a
   FreeRTOS software timer). All library calls the backend makes now come
   from the host task, which fixes a latent race in demo05: its heartbeat
   ran in the FreeRTOS timer task concurrently with `udisplay_feed()` in the
   host task, against the documented per-instance serialization contract.
6. **Serialization for app code on other tasks**: every backend call into the
   library holds a recursive NPL mutex. App code that calls into the ctx from
   outside uDisplay callbacks (e.g. a GPIO task pushing sensor values) wraps
   the call in `udisplay_nimble_lock()/unlock()`. Inside uDisplay callbacks
   the lock is already held; taking it again is allowed (recursive).
7. **Logging via NimBLE's `MODLOG_DFLT`**, which every NimBLE port provides
   (ESP-IDF routes it to `ESP_LOG` under the "NimBLE" tag). No ESP-IDF
   logging in the generic file.
8. **Advertising name**: flags + 128-bit service UUID + complete name fit in
   31 bytes only for names ≤ 8 bytes (demo05's "Demo05" does). Longer names
   go into the scan response; the UUID always stays in the advertising data
   because the client's scanner filters on it.
9. **Per-connection MTU reset.** Every CONNECT resets the instance to the
   default 20-byte payload; the library otherwise keeps the previous
   connection's MTU (a latent demo05 bug). Negotiated MTUs above the 517-byte
   fragment buffer are clamped, not rejected.
10. **The lock is held across `ble_gatts_indicate_custom()`** (review
   finding): releasing it first let a disconnect + reconnect race share the
   fragment buffer and terminate the new link. Safe because NimBLE never
   calls GAP/GATT callbacks with its host lock held.
11. **Legacy advertising only**: the build stops if NimBLE extended
   advertising is enabled (the legacy `ble_gap_adv_*` calls are no-ops then).
12. **The control-write copy buffer is static** (512 B, the ATT attribute
   max) instead of demo05's 1 KB on the host task stack.

## File layout

```
libudisplay/
  include/libudisplay/udisplay_nimble.h   public C API (NimBLE-free)
  src/tx_ring.{c,h}                       length-prefixed fragment ring (pure C, unit-tested)
  src/nimble/udisplay_nimble.c            NimBLE host API only (GAP, GATT, NPL)
  src/nimble/udisplay_nimble_port.h       port interface (3 functions)
  src/nimble/udisplay_nimble_port_esp_idf.c  nimble_port_init / FreeRTOS host task / default eventq
  Kconfig                                 UDISPLAY_ENABLE_NIMBLE (+ TX queue size)
```

Port interface (what a Mynewt port would implement instead of the ESP-IDF
file):

```c
int  udisplay_nimble_port_init(void);               /* bring up controller + host */
int  udisplay_nimble_port_start(void);              /* start running the host */
struct ble_npl_eventq* udisplay_nimble_port_eventq(void); /* host task's event queue */
```

## Build integration

- **ESP-IDF**: `Kconfig` adds `UDISPLAY_ENABLE_NIMBLE` (default n, depends on
  `BT_NIMBLE_ENABLED`) and `UDISPLAY_NIMBLE_TX_QUEUE_BYTES` (default 2048).
  `CMakeLists.txt` adds the backend sources and `REQUIRES bt` only when it is
  set. Having NimBLE enabled doesn't enable the backend.
- **Standalone CMake**: `option(UDISPLAY_ENABLE_NIMBLE OFF)`. When ON, the
  generic backend sources compile and the caller supplies the NimBLE
  include/link target via `UDISPLAY_NIMBLE_TARGET` and their own port glue.
  This path is what the host unit tests use, with a fake NimBLE host.

## Testing

- `test_tx_ring` — ring wraparound, overflow refusal, reset.
- `test_nimble_backend` — the generic backend compiled against a small fake
  NimBLE host (headers + recording stubs in `test/nimble_fake/`): config
  validation, GATT table shape, advertising layout (short/long names),
  connect → subscribe → HELLO, one indication in flight, EDONE pumping,
  indication failure/timeout → terminate, MTU, control write feed and
  framing error, subscribe watchdog, comms-error teardown, disconnect reset
  and adv restart.
- demo05 cross-builds with `idf.py build` for every documented target.
- Hardware smoke test (demo05 README checklist) can't run here; flagged in
  the PR.

## Review (/review, 2026-10-21)

Core pass found the MTU carry-over and the tx_pump reconnect race; an
adversarial pass confirmed the lock argument against the ESP-IDF v6.1 NimBLE
sources and found five informational issues (TX queue Kconfig minimum,
extended advertising, name length checked after stack bring-up, MTU clamp,
spurious link error on NOTIFY_TX from a broken link). All fixed with
regression tests where host-testable (the race isn't: the fake is
single-threaded).

## Not in scope

Mynewt/Zephyr glue, multiple concurrent BLE connections, bonding/security,
codegen changes.
