# uDisplay Binary Protocol Specification — v2.2 (proto 0x04)

This document is the authoritative wire format reference. Both `udisplay-gen` (Python)
and `libudisplay` (C++) implement from this spec independently. `tests/protocol_vectors.json`
provides canonical encoded test vectors for each message type; any deviation from those
vectors is a bug, not a spec ambiguity.

For Merkle hash computation, chunk size, and padding rules, see `docs/merkle.md`.

---

## Conventions

- All multi-byte integers are **little-endian**.
- `u8` = 1 byte unsigned. `u16` = 2 bytes unsigned. `i32` = 4 bytes signed. `f32` = 4 bytes IEEE 754.
- All string values are UTF-8, length-prefixed by a `u8` byte (max 255 chars).
- The device is the **server** (serves blob, sends state updates, sends heartbeats).
- The client (Qt app) is the **initiator** (connects, requests chunks, sends events).

---

## Transport Layer

### mDNS/DNS-SD Device Discovery

Devices advertise their TCP endpoint via mDNS (DNS-SD, RFC 6763). The Qt client
(`MdnsScanner`) browses for this service type:

```
_udisplay._tcp
```

The mDNS **service instance name** is the device's human-readable name (e.g., `Multimeter`) and
serves as the stable unique ID in the discovery list. A TXT record `name=<displayName>` overrides
the display label if present.

**Firmware requirement:** `libudisplay` must register the mDNS service on startup. In ESP-IDF:
```c
mdns_service_add(NULL, "_udisplay", "_tcp", udisplay_cfg.port, NULL, 0);
mdns_instance_name_set(device_name);
```

**Client behavior:** `MdnsScanner` (Qt) browses `_udisplay._tcp`. Discovered devices appear in
the `DiscoveryModel` list alongside BLE-discovered devices. Manual TCP entry is always visible
as a fallback.

**Build requirement (Linux):** mDNS scanning requires `libavahi-client-dev`. Without it, the
CMake build falls back to a stub scanner that emits an informational error; the manual TCP
connection path still works normally.

### TCP (WiFi)

Every message is framed with a 2-byte length prefix:

```
[u16 payload_length][payload_length bytes: msg_type u8 + message body]
```

The length field counts the payload bytes only (does not include the 2-byte header itself).
Maximum message size: 1024 bytes (`UDISPLAY_MAX_MSG_SIZE`). Messages with `payload_length > 1024`
are rejected at the framing layer on both sender and receiver. This fits within a standard
Ethernet frame payload and bounds static RAM usage on the device.

### BLE GATT

**Service UUID:** `29825AAA-D882-46F7-A4D6-EA8431AD3455` (uDisplay Service)

Two characteristics:

| Characteristic | UUID | Properties | Direction | Purpose |
|---|---|---|---|---|
| `control` | `29825AAA-D882-46F7-A4D6-EA8431AD3456` | WRITE_WITH_RESPONSE | client → device | Commands: handshake ack, hash request, chunk requests, events |
| `data` | `29825AAA-D882-46F7-A4D6-EA8431AD3457` | NOTIFY | device → client | Responses: handshake, hash batch, chunk data, state updates, heartbeat, property messages |

**MTU negotiation:** Client requests MTU = 512 bytes on connect. Device accepts up to its stack maximum (ESP-IDF supports up to 512). Effective payload per ATT packet = negotiated MTU − 3.

**`control` characteristic — WRITE_WITH_RESPONSE:** Every write to `control` uses ATT `WRITE_WITH_RESPONSE`, which provides ATT-level delivery confirmation. This eliminates the silent drop risk present in `WRITE_NO_RESPONSE` on Android (OEM-specific buffering behaviour). Client messages up to and including `HANDSHAKE_ACK(auth)` (35 bytes) exceed a single ATT packet at minimum MTU; all client→device messages are subject to the BLE framing rules below.

**BLE fragmentation (both characteristics).** Any uDisplay message that does not fit in a single ATT packet must be fragmented. The framing header differs between the first and continuation fragments.

**First fragment** (offset = 0):
```
[u16 offset=0  ]  byte position in original message (always 0x0000 for first)
[u8  packet_id ]  increments for each new message; wraps 255 → 0; independent per transmitter
[u16 length    ]  total byte count of the original (unframed) message
[u8  flags     ]  reserved; MUST be 0x00; receiver rejects first fragment with flags ≠ 0x00
[N×  u8 payload]  first N bytes of the message (as many as fit in the ATT packet)
```

**Continuation fragment** (offset > 0):
```
[u16 offset    ]  byte position of this fragment's payload within the original message
[u8  packet_id ]  same value as the first fragment for this message
[N×  u8 payload]  next N bytes of the message
```

Header sizes: first fragment = 6 bytes; continuation fragment = 3 bytes. Minimum usable MTU is 7 bytes effective payload (covers 6-byte first-fragment header + 1 payload byte). Implementations MUST support any negotiated MTU ≥ 7 effective bytes; the framing spec is MTU-agnostic.

**Single-fragment messages:** When the entire message (including the 6-byte first-fragment header) fits in one ATT packet, it is sent as a single first fragment with no continuations. The receiver detects completion immediately: `offset (0) + fragment_payload_size == length`.

**Completion rule:** The receiver assembles fragments until `offset + fragment_payload_size == length`. At that point the full message is complete and ready for protocol parsing. Any fragment whose payload would cause `accumulated_bytes > length` is a framing error — discard the in-flight message and reset state.

**packet_id semantics and wrap-around:** `packet_id` is an 8-bit counter maintained independently by each transmitter (device for `data`; client for `control`). It increments by 1 for each new message and wraps from 255 to 0. All fragments of the same message carry the same `packet_id`. Wrap-around is a normal condition: a new message with `packet_id = 0` following `packet_id = 255` MUST be accepted as a fresh message, not confused with the preceding packet. `packet_id` counters on both sides reset to 0 on each new BLE connection.

**Receiver error rules:** The receiver MUST discard an in-flight reassembly and reset to idle when any of the following occur:

| Condition | Action |
|---|---|
| `flags ≠ 0x00` in first fragment | Reject; wait for next fragment with `offset = 0` |
| `length > 1024` (`UDISPLAY_MAX_MSG_SIZE`) in first fragment | Reject; wait for next fragment with `offset = 0` |
| `offset > expected_next_offset` (gap in sequence) | Discard in-flight; process this as a potential new first fragment if `offset = 0` |
| `offset ≠ expected_next_offset` (wrong, non-gap offset) | Discard in-flight; wait for next fragment with `offset = 0` |
| Unexpected `packet_id` on continuation fragment | Discard in-flight; wait for next fragment with `offset = 0` |
| `accumulated_bytes + fragment_payload_size > length` (over-completion) | Discard in-flight; wait for next fragment with `offset = 0` |

A fragment with `offset = 0` always starts a fresh reassembly, discarding any previously accumulated bytes for an incomplete message.

**Connection reset:** Both `packet_id` counters reset to 0 on BLE disconnect and reconnect. The first fragment of the first message after reconnect will have `offset = 0, packet_id = 0`; the receiver accepts it unconditionally.

---

## Message Types

| Value | Name | Direction | Description |
|---|---|---|---|
| `0x00` | `HANDSHAKE` | device → client | Sent immediately after client connects |
| `0x01` | `HANDSHAKE_ACK` | client → device | Client confirms compatible protocol version |
| `0x02` | `CLIENT_READY` | client → device | Client signals bootstrap complete; device enters active state |
| `0x10` | `CHUNK_HEADER_REQUEST` | client → device | Request header (hash + length) for one chunk |
| `0x11` | `CHUNK_HEADER_RESPONSE` | device → client | Hash and length indicator for one chunk |
| `0x20` | `CHUNK_REQUEST` | client → device | Request a specific chunk by index |
| `0x21` | `CHUNK_RESPONSE` | device → client | One chunk of YAML blob data |
| `0x30` | `STATE_UPDATE` | device → client | Widget value changed |
| `0x31` | `EVENT` | client → device | User interaction (button, slider, toggle, text) |
| `0x32` | `SET_PROPERTY` | device → client | Set a runtime property of a widget |
| `0x33` | `RESET_PROPERTY` | device → client | Reset a widget property to its YAML-defined default |
| `0x40` | `HEARTBEAT` | device ↔ client | Keepalive; client echoes back to device watchdog |
| `0xFF` | `ERR_INVALID_CHUNK` | device → client | Requested chunk index out of bounds |

---

## Widget ID Ranges

```
0x00–0x0F   Reserved system widget IDs (16 slots)
              Reserved for future system use.

0x10–0xFF   User-defined widget IDs (240 slots)
              Assigned by udisplay-gen build, starting at 0x10.
```

The `widget_id` field in STATE_UPDATE, SET_PROPERTY, and RESET_PROPERTY always identifies a user-defined widget (0x10–0xFF). A `widget_id` outside this range is a protocol error — client logs and ignores.

---

## Message Formats

### HANDSHAKE (0x00) — device → client

Sent by the device immediately upon client connection. The `flags` byte (proto 0x04+) selects
between the no-auth bootstrap path and the auth-challenge path.

**No authentication (flags = 0x00):**

```
[u8  0x00          ]  msg_type
[u8  proto_version ]  uDisplay protocol version (current = 0x04)
[u8  flags         ]  0x00 = no auth; bits 1-7 reserved (MUST be 0)
[32× u8 merkle_root]  Merkle root of the compressed YAML blob (see docs/merkle.md)
[u16 chunk_count   ]  number of chunks in the blob
[u16 chunk_size    ]  size of each chunk in bytes (v1 = 256; last chunk may be smaller)
```

Total: 39 bytes (proto 0x04+). Prior to proto 0x04 the flags byte was absent: 38 bytes with
merkle_root at offset 2. Clients MUST check `proto_version` before parsing: ≤ 0x03 → old
38-byte layout; ≥ 0x04 → new 39-byte layout.

**Authentication required (flags = 0x01):**

```
[u8  0x00          ]  msg_type
[u8  proto_version ]  uDisplay protocol version (0x04)
[u8  flags         ]  0x01 = auth required
[u8  algorithm     ]  hash algorithm: 0x01 = HMAC-SHA256 (others reserved)
[32× u8 salt       ]  device-generated random salt; new salt issued on each attempt
```

Total: 35 bytes. The merkle_root is NOT included — it is sent in a second HANDSHAKE(flags=0)
after authentication succeeds.

### HANDSHAKE_ACK (0x01) — client → device

Client confirms protocol version and echoes the flags byte.

**No authentication (flags = 0x00):**

```
[u8  0x01              ]  msg_type
[u8  client_proto_max  ]  highest protocol version the client supports (current = 0x04)
[u8  flags             ]  0x00 = no auth (echoes HANDSHAKE flags)
```

Total: 3 bytes (proto 0x04+). Old firmware (proto ≤ 0x03) uses `len ≥ 2` check, so the
3-byte ACK is silently forward-compatible.

**Authentication (flags = 0x01):**

```
[u8  0x01              ]  msg_type
[u8  client_proto_max  ]  highest protocol version the client supports
[u8  flags             ]  0x01 = auth credential follows
[32× u8 credential     ]  HMAC-SHA256(key=password, message=salt)
```

Total: 35 bytes.

**Credential computation:** `credential = HMAC-SHA256(key=password, message=salt)` per RFC 2104.
The password is the HMAC key; the challenge salt is the HMAC message. Every platform ships
a well-tested HMAC implementation and the key/message roles are unambiguous — no concatenation
order or pre-hashing convention to get wrong.

**Version compatibility rule:** If `proto_version` from HANDSHAKE ≤ `client_proto_max`, the
client proceeds. If `proto_version` > `client_proto_max`, the client aborts and shows:
"Incompatible device: requires uDisplay protocol v{proto_version}. Update the app."

### Authentication Flow

When the device has `auth_algo = UDISPLAY_AUTH_HMAC_SHA256` configured:

```
CLIENT                                         DEVICE
  |                                               |
  |  [TCP/BLE connect]                            |
  |                            on_connect():      |
  |                            gen random salt    |
  |←──── HANDSHAKE(flags=1, HMAC-SHA256, salt) ───|
  |                                               |
  |  credential = HMAC-SHA256(key=pwd, msg=salt)  |
  |                                               |
  |── HANDSHAKE_ACK(flags=1, credential) ────────→|
  |                          call auth_check():   |
  |                          FAIL ────────────────┐
  |←──── HANDSHAKE(flags=1, HMAC-SHA256, new_salt)┘
  |  [retry — new salt prevents replay]           |
  |── HANDSHAKE_ACK(flags=1, new_credential) ────→|
  |                          PASS ────────────────┐
  |←──── HANDSHAKE(flags=0, merkle_root, ...) ────┘
  |── HANDSHAKE_ACK(flags=0) ────────────────────→|
  |  [normal bootstrap follows]                   |
```

`auth_check` return values: 1 = accepted, 0 = retry (new salt issued), -1 = disconnect.
No rate limiting is built into the library; firmware MUST limit retries via `auth_check`.

**Security properties:**

| Property | Analysis |
|---|---|
| Password never in clear | Only HMAC-SHA256(key=password, message=salt) is transmitted |
| Replay prevention | New random salt per attempt |
| Brute-force resistance | No rate limiting in library; firmware must limit via `auth_check` |
| Algorithm agility | HMAC-SHA256 only (0x01); other IDs reserved but MUST NOT be implemented |
| Offline dictionary | Weak passwords vulnerable; KDF (PBKDF2, argon2) is firmware's responsibility |

### CLIENT_READY (0x02) — client → device

Client signals that bootstrap is complete and it is ready to receive runtime messages. The device enters **active state** on receipt: STATE_UPDATE pushes begin and EVENT messages are dispatched to firmware callbacks.

```
[u8  0x02]  msg_type
```

Total: 1 byte.

Sent by the client immediately before emitting `succeeded()`, in both the cache-hit path and the full-download path. If the device receives CLIENT_READY before HANDSHAKE_ACK (out-of-order), it is ignored. Receiving CLIENT_READY a second time while already active is idempotent.

**`on_client_ready` callback:** When the device transitions `active` from 0 to 1 on the first CLIENT_READY of a connection, it calls `cfg.on_client_ready(userdata)` (if non-NULL). This fires exactly once per connection; idempotent re-sends do not re-trigger it. Firmware uses this to push initial sensor values and reset internal state after bootstrap completes.

**Device behavior on active=0:** STATE_UPDATE sends are blocked; EVENT messages from the remote are silently dropped. HEARTBEAT is always sent regardless of active state (it only requires `connected=1`).

**Active state resets:** The device resets `active=0` on every new connection (both explicit disconnect+reconnect and reconnect-without-disconnect). A new CLIENT_READY must be sent for each connection.

### CHUNK_HEADER_REQUEST (0x10) — client → device

Client requests the header (hash + length indicator) for one chunk by index. The client sends N such requests after HANDSHAKE_ACK when the YAML blob is not cached.

```
[u8   0x10          ]  msg_type
[u16  chunk_index   ]  zero-based index of the chunk
```

Total: 3 bytes.

The client sends all N requests at once, then accumulates N sequential
CHUNK_HEADER_RESPONSEs. The responses arrive in the same order as the requests
because both TCP and BLE provide ordered delivery.

### CHUNK_HEADER_RESPONSE (0x11) — device → client

Device replies with the chunk's SHA-256 hash and a length indicator byte.
After all N responses are received, the client verifies
`SHA-256(hash_0 ‖ hash_1 ‖ ... ‖ hash_{N-1}) == merkle_root` before downloading chunks.
See `docs/merkle.md` for exact Merkle root computation.

```
[u8   0x11          ]  msg_type
[32×  u8  hash      ]  SHA-256 of the chunk (padded to 256 bytes before hashing)
[u8   len_byte      ]  0 = full 256-byte chunk; 1-255 = partial last chunk byte count
```

Total: 34 bytes (fixed). No chunk index is included — the client fills responses
sequentially into its hash array, relying on ordered delivery.

If `chunk_index` is out of range (≥ chunk_count from HANDSHAKE), the device replies
with `ERR_INVALID_CHUNK` (0xFF) instead.

Over BLE, a 34-byte response is fragmented across two ATT notifications at MTU=23 (see BLE GATT § BLE fragmentation).

### CHUNK_REQUEST (0x20) — client → device

Client requests one chunk by zero-based index. The client sends requests for all missing chunks (those whose hash was not in cache or whose cached hash does not match).

```
[u8   0x20          ]  msg_type
[u16  chunk_index   ]  zero-based index of requested chunk
```

Total: 3 bytes.

### CHUNK_RESPONSE (0x21) — device → client

Device sends the requested chunk's raw bytes. No hash included (hashes were downloaded in batch; client verifies before reassembly).

```
[u8   0x21          ]  msg_type
[u16  chunk_index   ]  echoes the requested index
[N×   u8  data      ]  chunk_size bytes (or fewer for the last chunk)
```

Total: 3 + N bytes. For v1 chunk_size = 256: maximum 259 bytes.

The client verifies `SHA-256(data) == hash[chunk_index]` after receipt. On mismatch: discard and re-request the chunk (max 3 retries, then abort with error).

### ERR_INVALID_CHUNK (0xFF) — device → client

Device sends this when the requested chunk_index ≥ chunk_count.

```
[u8   0xFF          ]  msg_type
[u16  chunk_index   ]  echoes the invalid index
```

Total: 3 bytes.

Client behavior on receipt: abort transfer, show "Device reported invalid chunk error — firmware may be corrupted." Log the bad index.

### STATE_UPDATE (0x30) — device → client

Device pushes a new value for a widget.

```
[u8   0x30          ]  msg_type
[u8   widget_id     ]  target widget (0x10–0xFF)
[u8   value_type    ]  see Value Types table below
[N×   u8  value     ]  encoded value (N depends on value_type)
```

### EVENT (0x31) — client → device

Client sends a user interaction event to the device.

```
[u8   0x31          ]  msg_type
[u8   widget_id     ]  target widget ID (must be 0x10–0xFF)
[u8   event_type    ]  see Event Types table below
[N×   u8  value     ]  encoded value (N depends on event_type / value_type)
```

### HEARTBEAT (0x40) — device ↔ client

Device sends periodically to signal it is alive; client echoes the same 1-byte message back to the device. No additional payload.

```
[u8   0x40]  msg_type
```

Total: 1 byte.

**Heartbeat interval:** Recommended 5 seconds. Client watchdog timeout: 15 seconds (3× interval). On timeout, client enters RECONNECTING state.

**Symmetric echo:** When the client receives a HEARTBEAT from the device, it immediately sends an identical HEARTBEAT (0x40) back. This echo is the device's signal that the client is still processing messages.

**Device watchdog (`on_comms_error` callback):** After `active=1`, each call to `udisplay_heartbeat()` that is not preceded by an inbound HEARTBEAT echo increments an internal `hb_missed_count` counter. When `hb_missed_count` reaches `UDISPLAY_HB_MISS_MAX` (3), the library calls `cfg.on_comms_error(userdata)` exactly once. The counter is capped at `UDISPLAY_HB_MISS_MAX` — no further callbacks fire until the counter resets. The counter resets to 0 on: (a) any inbound HEARTBEAT echo, (b) `on_connect()`, or (c) `on_disconnect()`. Before `active=1`, missed heartbeats do not increment the counter (the client is still bootstrapping).

**`UDISPLAY_HB_MISS_MAX`** is defined in `udisplay.h` and equals 3 (15 s at the recommended 5 s interval).

---

### SET_PROPERTY (0x32) — device → client

Device sets a runtime property of a widget. The override is applied immediately and persists until the connection closes.

```
[u8   0x32          ]  msg_type
[u8   widget_id     ]  target widget (0x10–0xFF)
[u8   property_id   ]  which property to set (see Standard Properties below)
[N×   u8  value     ]  new value (encoding depends on property; see Standard Properties)
```

### RESET_PROPERTY (0x33) — device → client

Device resets a widget property to its YAML-defined default. No value bytes.

```
[u8   0x33          ]  msg_type
[u8   widget_id     ]  target widget (0x10–0xFF)
[u8   property_id   ]  which property to reset
```

Total: 3 bytes.

### Standard Properties

| Code | Name | Value type | Applicable to | Description |
|---|---|---|---|---|
| `0x01` | `ENABLED` | `uint8` (0=disabled, 1=enabled) | all widgets | Disabled widget is non-interactive and visually grayed out |
| `0x02` | `VISIBLE` | `uint8` (0=hidden, 1=shown) | all widgets | Hidden widget takes no space in the layout |
| `0x03` | `MODE` | `uint8` (0=ro, 1=rw) | `text` only | Switch text field between display and input mode at runtime |
| `0x04` | `STYLE` | `uint8` (widget-specific) | TBD | Change visual variant at runtime |
| `0x05–0x0F` | Reserved | TBD | TBD | Event filters, label changes, etc. |
| `0x10–0xFF` | User-defined | TBD | TBD | Future extension space |

**Device firmware API (libudisplay):**
```c
// Disable the rate slider during calibration
udisplay_set_property(WIDGET_ID_RATE, PROP_ENABLED, 0);

// Switch SSID field to read-only after config is saved
udisplay_set_property(WIDGET_ID_SSID, PROP_MODE, 0);

// Re-enable after calibration completes
udisplay_set_property(WIDGET_ID_RATE, PROP_ENABLED, 1);

// Restore SSID to its YAML-defined default mode
udisplay_reset_property(WIDGET_ID_SSID, PROP_MODE);
```

**Client behavior:**
- Property overrides are applied immediately on receipt, before the next render frame.
- Property overrides are **not persisted** — they exist only while the connection is open.
- On reconnect, all widget properties reset to their YAML-defined defaults (full reinitialization).
- An unknown `property_id` is logged and silently ignored (forward-compatible).
- A `widget_id` in the reserved range (< 0x10) is a protocol error — client logs and ignores.

---

## Value Types

Used in STATE_UPDATE (data path) and EVENT messages.

| Code | Name | Size | Encoding |
|---|---|---|---|
| `0x01` | `float32` | 4 bytes | IEEE 754 single-precision, little-endian |
| `0x02` | `int32` | 4 bytes | signed 32-bit integer, little-endian |
| `0x03` | `uint8` | 1 byte | unsigned 8-bit integer |
| `0x04` | `string` | 1 + N bytes | `u8` length prefix + UTF-8 bytes (max 255 chars) |

---

## Event Types

Used in EVENT messages (client → device).

| Code | Name | Value encoding | Sent for widget type |
|---|---|---|---|
| `0x01` | `button_click` | no value (0 bytes) | `button`, `button-group` item |
| `0x02` | `slider_change` | `float32` (new position) | `slider` |
| `0x03` | `toggle_change` | `uint8` (0=off, 1=on) | `toggle` |
| `0x04` | `text_submit` | `string` (confirmed text) | `text` (rw mode) |
| `0x05` | `selection_change` | `uint8` (0-based index) | `dropdown` |
| `0x06` | `button_press` | no value (0 bytes) | `button`, `button-group` item |
| `0x07` | `button_release` | no value (0 bytes) | `button`, `button-group` item |

`button_click` (0x01) fires only on a complete tap (press + release within bounds). `button_press` (0x06) fires on press-down; `button_release` (0x07) fires on any release (including canceled touches). All three are always emitted by the client; firmware wires up only the handlers it needs.

For `button-group`: `widget_id` identifies the selected item within the group (each item has its own widget_id assigned by codegen).

For `dropdown`: `widget_id` identifies the dropdown itself. The value byte is the 0-based index of the selected item in YAML declaration order.

---

## Connection States

The device maintains a two-flag state machine per connection, with an optional `awaiting_auth_ack` sub-state when authentication is enabled, and a shared comms-miss watchdog that covers both a stalled BOOTSTRAP and a silent ACTIVE connection:

```
connected=0, active=0                ← initial / disconnected
       │
       │ on_connect()
       │   auth_algo=NONE   → sends HANDSHAKE(flags=0x00)
       │   auth_algo=HMAC_SHA256 → sends HANDSHAKE(flags=0x01, salt), sets awaiting_auth_ack=1
       ▼
connected=1, active=0                ← WAITING / BOOTSTRAP
       │
       │  [awaiting_auth_ack=1: AWAITING_AUTH_ACK sub-state]
       │  HANDSHAKE_ACK(flags=0x01) received:
       │    auth_check() → pass (1) : clears awaiting_auth_ack, sends HANDSHAKE(flags=0x00)
       │    auth_check() → fail (0) : resends HANDSHAKE(flags=0x01) with new salt (retry)
       │    auth_check() → drop (-1): closes connection
       │
       │  [awaiting_auth_ack=0: normal bootstrap]
       │  HANDSHAKE_ACK(flags=0x00) from client → device sends chunk headers/chunks
       │
       │  ┌── comms_miss_count reaches UDISPLAY_HB_MISS_MAX (no HANDSHAKE_ACK,
       │  │   CLIENT_READY, CHUNK_HEADER_REQUEST or CHUNK_REQUEST for N heartbeats)
       │  │   → on_comms_error() → firmware disconnects
       │  │
       │  CLIENT_READY received
       ▼
connected=1, active=1                ← ACTIVE
       │
       │  comms_miss_count reaches UDISPLAY_HB_MISS_MAX (no HEARTBEAT echo for
       │  N heartbeats) → on_comms_error() → firmware disconnects
       │
       │ on_disconnect() OR on_connect() (reconnect without disconnect)
       ▼
connected=0, active=0                ← disconnected (active always reset)
```

**In AWAITING_AUTH_ACK sub-state:**
- HEARTBEAT is sent (requires only `connected=1`).
- All other outbound messages are held until auth passes or the connection drops.
- A misbehaving client that sends HANDSHAKE_ACK(flags=0x00) while `awaiting_auth_ack=1` causes a silent disconnect.

**In WAITING/BOOTSTRAP state:**
- HEARTBEAT is sent (bypasses active gate — requires only `connected=1`).
- STATE_UPDATE pushes are blocked (`active=0`).
- Incoming EVENT messages are silently dropped.
- **Bootstrap-stall watchdog:** if the client connects but never progresses (no `HANDSHAKE_ACK`, `CLIENT_READY`, `CHUNK_HEADER_REQUEST`, or `CHUNK_REQUEST` arrives) for `UDISPLAY_HB_MISS_MAX` consecutive heartbeats, `on_comms_error` fires and the device gives up on the connection — the same recovery path as the post-active heartbeat-miss watchdog below, just triggered earlier. This shares one counter (`comms_miss_count`) and one threshold with the ACTIVE-state watchdog, since a connection is never in both states at once.
- On BLE transport specifically, this watchdog only starts counting once the counted connection begins (`on_connect()` is called) — see the demo05 firmware notes below for the narrower window between radio-level connect and BLE notification subscribe, which this watchdog does not cover (firmware-level concern, not a library state).

**In ACTIVE state:**
- All message types flow normally.
- **Heartbeat-miss watchdog:** unchanged from before — resets only on a `HEARTBEAT` echo from the client; fires `on_comms_error` after `UDISPLAY_HB_MISS_MAX` consecutive misses.

**Active flag reset:** `active` is set to 0 on both `on_disconnect()` and `on_connect()`. The second reset handles the BLE drop-without-disconnect case: if the device receives a new connection without a prior disconnect event, it resets active and waits for CLIENT_READY.

---

## Bootstrap Sequence

```
CLIENT                                  DEVICE
  |                                        |
  |                                connected=1, active=0 (WAITING)
  |←────── HANDSHAKE ─────────────────────|  (proto_version, merkle_root, chunk_count)
  |                                        |
  |── HANDSHAKE_ACK ──────────────────────→|  (client_proto_max)
  |                                        |
  | [client checks cache]                  |
  |                                        |
  | [cache HIT]────────────────────────────────────────────────────────────┐
  |                                        |                               │
  | [cache MISS]                           |                               │
  |── CHUNK_HEADER_REQUEST(0) ────────────→|                               │
  |── CHUNK_HEADER_REQUEST(1) ────────────→|                               │
  |    ... (all N sent at once)            |                               │
  |←── CHUNK_HEADER_RESPONSE (hash0, lb0)─|                               │
  |←── CHUNK_HEADER_RESPONSE (hash1, lb1)─|                               │
  |    ... (N responses, ordered)          |                               │
  | [verify SHA256(h0‖…‖hN-1)==merkle_root]|                               │
  |── CHUNK_REQUEST (index i) ────────────→|                               │
  |←────── CHUNK_RESPONSE (index i) ──────|  (or ERR_INVALID_CHUNK)       │
  | [verify chunk hash, retry up to 3×]   |                               │
  | ... repeat for all N chunks ...        |                               │
  | [reassemble blob, store in cache]      |                               │
  |                                        |◄──────────────────────────────┘
  |── CLIENT_READY ───────────────────────→|  device sets active=1 (ACTIVE)
  |                                        |
  |←────── STATE_UPDATE (0x10) ───────────|  (widget data, continuous stream)
  |←────── SET_PROPERTY / RESET_PROPERTY ─|  (property override, optional)
  |←────── HEARTBEAT ─────────────────────|  (every ~5s; also sent during bootstrap)
  |── EVENT (widget_id >= 0x10) ──────────→|
```

**On disconnect:** Client enters RECONNECTING state. All widgets disabled. All runtime property overrides cleared. Full bootstrap sequence runs on reconnect — no fast-resume shortcut.

---

## Widget ID Assignment

Widget IDs are assigned by `udisplay-gen build` at code generation time:

1. Sort all widgets in the YAML definition alphabetically by their `id` key. For nested `widgets:` blocks (`button` face children, `row`/`grid`/`section` contents), sort by fully-qualified `parent_id.child_id` path.
2. Assign `widget_id` starting at **0x10**, incrementing by 1 in sorted order.
3. The mapping is embedded in the generated `udisplay_ui.h`:
   ```c
   #define WIDGET_ID_READING   0x10
   #define WIDGET_ID_MODE      0x11
   #define WIDGET_ID_RATE      0x12
   // relay1.relay1_status is a child widget — also gets its own ID
   #define WIDGET_ID_RELAY1    0x13
   #define WIDGET_ID_RELAY1_STATUS 0x14
   ```
4. Maximum 240 user-defined widgets per device (0x10–0xFF).

The Qt client derives the same mapping by parsing the YAML blob (same sort order). Both sides use `widget_id` as the sole identifier in all STATE_UPDATE and EVENT messages.

The system property constants are also defined in `udisplay_ui.h`:
```c
#define PROP_ENABLED  0x01
#define PROP_VISIBLE  0x02
#define PROP_MODE     0x03
#define PROP_STYLE    0x04
```

---

## v1 Layout Model

The v1 client renders all top-level widgets in a **single vertical `QScrollArea`**, in
the order they are declared in the YAML definition.

```
┌─────────────────────────┐
│  QScrollArea            │
│  ┌─────────────────┐    │
│  │ widget[0]       │    │
│  ├─────────────────┤    │
│  │ widget[1]       │    │
│  ├─────────────────┤    │
│  │ widget[2]       │    │
│  │   ...           │    │
│  └─────────────────┘    │
└─────────────────────────┘
```

**Rules:**

- YAML declaration order = render order. The codegen does NOT sort top-level widgets
  for rendering (only for widget ID assignment).
- Each top-level widget is full-width within the scroll area.
- `button-group` is the only interactive widget with an internal layout (`layout: grid|dpad`).
- `button` with `widgets:` face children (`led`, `rgbled`, `display`, `label`, or a
  nested `row`/`grid` of those) renders them inline, at normal size, within the
  button — not as a separate row. `label`/`led`/`rgbled` faces use the button's
  accent/face color instead of the standalone text/dot color, but are not
  shrunk (compact mode no longer resizes them).
- Standalone `led` renders as a full-width row (dot + label, left-aligned).
- The `VISIBLE` property (set via `SET_PROPERTY`) hides a widget and
  **collapses its space** — it does not leave a gap.

**v1 layout containers** — `section`, `row`, and `grid` — are available as of
v1 widget expansion. They carry no widget IDs and send no protocol messages.
Their children are regular widgets with IDs:

- `section` — groups widgets under a collapsible header. Children render vertically inside.
- `row` — renders children side-by-side with optional `flex` weights (proportional width).
  Children with no `flex` use their implicit width. Children with `flex: N` fill
  remaining space proportionally. Both `row` and its children accept `align`
  (`"left"`\|`"right"`\|`"center"`) to control cross-axis-free positioning for
  non-stretching children.
- `grid` — renders children in a multi-column grid. `columns` controls the column count.
  Same `flex`/`align` semantics as `row`, but `flex` ratios only compete within a column.

All three declare `layout-v2` as their capability token — currently reserved for
future use and not yet enforced by the client (see `docs/widgets.md` § capabilities
field). Omit `capabilities:` in the `device:` block for now.

**v2:** The WebView widget and MVVM reactive datamodel are the planned v2 additions.

---

## Error Codes

| Code | Name | Direction | Meaning |
|---|---|---|---|
| `0xFF` | `ERR_INVALID_CHUNK` | device → client | Requested chunk index ≥ chunk_count |

All other error conditions are handled by the connection layer (disconnect + reconnect) or the Qt client UI (error messages). See `docs/error-codes.md` for the full client-side error catalog.

---

## Protocol Test Vectors

`tests/protocol_vectors.json` contains canonical encoded byte sequences for each message type. Both `pytest` (codegen) and `gtest` (libudisplay) validate against these vectors in CI. Any encoding disagreement between the two implementations is a build failure.

Format:
```json
{
  "HANDSHAKE": {
    "input": { "proto_version": 3, "merkle_root": "...", "chunk_count": 16, "chunk_size": 256 },
    "bytes": "00 03 ..."
  },
  "STATE_UPDATE_data": {
    "input": { "widget_id": "0x10", "value_type": "float32", "value": 3.14 },
    "bytes": "30 10 01 c3 f5 48 40"
  },
  "SET_PROPERTY": {
    "input": { "widget_id": "0x10", "property_id": "ENABLED", "value": 0 },
    "bytes": "32 10 01 00"
  },
  "RESET_PROPERTY": {
    "input": { "widget_id": "0x10", "property_id": "ENABLED" },
    "bytes": "33 10 01"
  },
  ...
}
```

---

## Development Tools

### demo01–demo03 — hardware-less TCP device emulators

`demos/demo01/`, `demos/demo02/`, and `demos/demo03/` are Linux applications that each
act as a uDisplay device over TCP, with no ESP32 required. They are the primary
showcase, development, and integration-test tool for the Qt client — use one instead
of real hardware during desktop development. The three demos share the same widget
layout and TCP device architecture; they differ in which `udisplay-gen` output
language they build against: demo01 generates plain C, demo02 generates C++
(`--lang cpp`), and demo03 generates modern C++ with `std::function` handlers
(`--lang cpp --modern`) — see [Widget reference](widgets.md) for what each generated
API looks like.

**What they simulate:** each demo drives a full YAML widget definition
(`demos/demoNN/demoNN.yaml`) with simulated sensor and interaction behavior — a
numeric reading on a timer, an LED that reacts to a button, a slider/toggle that
echoes back, and so on. The exact widget set and behaviors are demo-specific and
expected to change over time as the showcase evolves; read the relevant `.yaml` and
source file for the current details rather than treating any specific widget list as
fixed.

**Architecture (representative — demo01):**

```
main thread ──── TCP accept (single-client; second connection rejected + logged)
recv thread ──── recv() accumulation + udisplay_tcp_unframe() + udisplay_on_message()
timer thread ─── nanosleep(100ms) base tick
                  ├── heartbeat every 5 s
                  └── data update at (base_rate × multiplier) Hz
```

All libudisplay calls serialised behind a single `pthread_mutex_t`. The send callback
wraps raw messages with `udisplay_tcp_frame()` before writing to the socket.

**Build:**

```bash
cmake -S demos/demo01 -B build/demo01 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/demo01
./build/demo01/demo01 [port]   # default port: 5555
```

Substitute `demo02`/`demo03` for `demo01` to build the other two. Each `CMakeLists.txt`
pulls in `libudisplay` as a subdirectory (standalone non-IDF build) and runs
`udisplay-gen build demoNN.yaml` (with `--lang cpp`/`--lang cpp --modern` for demo02/03)
as a CMake custom command at build time.

**Known gaps (deferred):**
- Not wired into the root CMake build — configure each demo separately as shown above
- No test coverage for the multi-thread architecture
- No graceful SIGINT/SIGTERM handler
- TCP send has no partial-write/EAGAIN retry

**Successor:** `demo05` (minimal ESP32 BLE demo, real hardware) is the first firmware
demo built on real hardware instead of a TCP emulator — see
[`demos/demo05/README.md`](../demos/demo05/README.md). demo01–demo03 remain the
desktop-side development and integration-test tools; demo05 (and eventually demo07)
are the real-hardware showpieces.

---

## Changelog

| Version | Date | Changes |
|---|---|---|
| v1.0 | 2025-03-23 | Initial specification. |
| v1.1 | 2025-03-23 | Reserved widget_id 0x00–0x0F. Added property command processor (widget_id=0x00). User widgets start at 0x10. Added text_submit event type (0x04). |
| v1.2 | 2025-03-24 | Added Development Tools section documenting demo01 (hardware-less TCP emulator, all 7 widget types). |
| v1.3 | 2025-03-26 | Added CLIENT_READY (0x02): client→device message that gates active state. PROTO_VERSION bumped to 0x02. Added Connection States section with active-flag state machine. STATE_UPDATE and EVENT only valid in active state; HEARTBEAT bypasses gate. |
| v1.4 | 2025-03-31 | Dropped Property Command Processor (STATE_UPDATE widget_id=0x00 hack). Replaced with dedicated top-level messages SET_PROPERTY (0x32) and RESET_PROPERTY (0x33). Standard Properties table carried forward. PROTO_VERSION bumped to 0x03. Widget ID range 0x00–0x0F fully reserved (no assigned semantics). |
| v1.5 | 2026-02-03 | Added `selection_change` event (0x05) for `dropdown` widget type. Updated v1 Layout Model to document `section`, `row`, and `grid` as v1 layout containers (not deferred to v2). Fixed HANDSHAKE `proto_version` annotation to reflect current value 0x03. |
| v1.6 | 2026-05-04 | HEARTBEAT (0x40) is now device ↔ client (symmetric echo). Added `on_client_ready` callback (fires once on 0→1 active transition). Added device-side heartbeat watchdog (`hb_missed_count`, `UDISPLAY_HB_MISS_MAX=3`, `on_comms_error` callback). No wire-format change; no protocol version bump. |
| v1.7 | 2026-05-08 | Added `button_press` (0x06) and `button_release` (0x07) event types for `button` and `button-group` items. Renamed existing 0x01 constant to `button_click` (wire code unchanged; backward compatible). No protocol version bump — EVENT message format is unchanged; new event_type values are additive. |
| v2.1 | 2026-05-09 | Optional HMAC-SHA256 challenge-response authentication. PROTO_VERSION bumped to 0x04 (breaking). HANDSHAKE(flags=0x00) now 39 bytes — flags byte inserted at offset 2, merkle_root shifts to offset 3. New HANDSHAKE(flags=0x01) auth-challenge (36 bytes: msg_type + proto_version + flags + algo + salt[32]). HANDSHAKE_ACK(flags=0x00) now 3 bytes; HANDSHAKE_ACK(flags=0x01) 35 bytes (adds 32-byte credential). Connection States updated with AWAITING_AUTH_ACK sub-state. Clients with proto_version < 0x04 continue to receive the legacy 38-byte no-auth HANDSHAKE. |
| v2.2 | 2026-05-17 | BLE GATT framing redesign. Replaced 1-byte `frag_flags` scheme with offset+packet_id framing: first fragment carries `[u16 offset=0][u8 packet_id][u16 length][u8 flags]`; continuations carry `[u16 offset][u8 packet_id]`. Completion detected by `offset + frag_payload_size == length`. `control` characteristic upgraded from WRITE_NO_RESPONSE to WRITE_WITH_RESPONSE (ATT-level delivery confirmation). Both characteristics use BLE framing (not data-only as previously stated — `HANDSHAKE_ACK(auth)` is 35 bytes and requires fragmentation at min MTU). `packet_id` counters are independent per transmitter and reset to 0 on reconnect. Explicit error rules added for flags, length cap (1024 bytes), offset gaps, wrong offsets, unexpected packet_id, and over-completion. No PROTO_VERSION bump — BLE framing is implemented in the client's BleTransport, not yet deployed as of this changelog entry. |
