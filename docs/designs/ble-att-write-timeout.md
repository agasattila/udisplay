# Design: BLE ATT write timeout (TODO-030)

Non-interactive session; decisions made autonomously.
Branch: feat/ble-att-write-timeout
Repo: agasattila/udisplay
Issue: #23
Status: IMPLEMENTED (PR pending)

## Problem Statement
`BleTransport` sends client-to-device data one ATT write at a time
(WRITE_WITH_RESPONSE on the Control characteristic). The next fragment goes out
only when `characteristicWritten` confirms the previous one. If that
confirmation never arrives (firmware hung, or an OS/OEM stack silently drops
the write response), the queue stalls for good:

- No `disconnected()` arrives, so the link looks alive.
- **The client's heartbeat watchdog doesn't catch it.** `DeviceController`
  restarts `m_watchdog` on every *inbound* HEARTBEAT. Inbound traffic travels as
  indications on the Data characteristic, which a stuck write queue doesn't
  block. The device keeps heartbeating, the watchdog keeps getting fed, and
  every echo, button press and slider change the user makes is queued forever.
  (The issue's "15 s heartbeat catches it eventually" assumption doesn't hold
  on the client side.)
- During bootstrap, the 30 s `BootstrapManager` timeout eventually fires. That's
  6× slower than needed, and the message ("device did not respond") points at
  the wrong side.

## Premises
1. On a WRITE_WITH_RESPONSE link, a write that isn't confirmed within a bounded
   time means the link is dead. Disconnecting is the right response (same
   policy as `CharacteristicWriteError` and framing errors: "delivered or
   disconnected"). No retry: the fragment's fate is unknown, and resending
   could duplicate it.
2. 5 s is the starting value, per the issue. Phone BLE connection intervals
   are 7.5-50 ms, so a healthy write confirms in well under 100 ms. 5 s
   still leaves headroom for a congested radio, and it's well under the
   Bluetooth spec's 30 s ATT transaction timeout.
3. The value has to be easy to tune once real-device data exists. It's one
   named constant.

## Approaches Considered
### A: Extract the write queue into a small Bluetooth-free class (chosen)
Move the existing queue + in-flight flag into `BleWriteQueue` (a QObject in
`src/BleWriteQueue.{h,cpp}`, no Qt Bluetooth dependency) and add a single-shot
`QTimer` that starts when a fragment is handed out and stops when it's
confirmed. `BleTransport` forwards `writeRequested` to `writeCharacteristic()`
and `timedOut` to `failLink()`.
Pros: the queue logic (ordering, one in flight, timeout, late confirmation,
clear) becomes unit-testable without a BLE adapter. Today none of
`BleTransport` is tested, and it can't be, since it needs a live
`QLowEnergyController`. Cons: one new small file pair. Effort XS/S.
### B: A QTimer member directly in BleTransport (the issue's literal sketch)
Smallest diff, but the timeout can't be tested at all: `BleTransport` needs
real hardware, and the CI/build host here has none. Rejected because it ships
untested timing logic into the one path that exists to catch hangs.
### C: A transport-agnostic outbound liveness check in DeviceController
Rejected: TCP has no per-write confirmation to time, and it moves a BLE-specific
failure away from the code that knows about it.

## Recommended Approach
A, with these decisions:
- `BleWriteQueue(int timeoutMs = kDefaultTimeoutMs, QObject* parent)`,
  `static constexpr int kDefaultTimeoutMs = 5'000`. The ctor parameter exists
  for tests (short timeouts). `BleTransport` uses the default.
- API: `enqueue(frag)` (sends right away if idle), `confirm()` (the in-flight
  write was confirmed: stop the timer and send the next one), `clear()` (drop
  everything, stop the timer), `isIdle()`. Signals:
  `writeRequested(QByteArray)` and `timedOut()`.
- The state (in-flight flag, timer start) is set **before** `writeRequested` is
  emitted, so a backend that confirms synchronously inside
  `writeCharacteristic()` re-enters a consistent queue.
- `confirm()` with nothing in flight is a no-op. That covers a late
  confirmation arriving after a timeout or `clear()`.
- On timeout: `BleTransport::failLink("BLE write timeout")`, which
  clears the queue, emits `errorOccurred`, and disconnects the controller. That
  error reaches `BootstrapManager::failed` → `DeviceController` in any state,
  the same path `CharacteristicWriteError` already uses.
- Firmware contract (found in /review): NimBLE sends the write response only
  after the GATT write callback returns, and `udisplay_feed()` runs the
  firmware's event handlers inside that callback (`demos/demo05` `ctrl_access`).
  A handler that blocks for more than 5 s now loses the BLE link. This is
  documented in `docs/protocol.md` (v2.7).
- One timeout per fragment, not per message: each ATT write is its own
  transaction, and a long message just means more 5 s windows, each of which
  normally takes milliseconds.

## Not in scope
- The CCCD descriptor write during setup. A hang there stops `connected()` from
  ever being emitted, and the 30 s bootstrap timeout already covers it. It
  uses `writeDescriptor`, not the data queue. It can be added later with the
  same timer if hardware testing shows it hangs in practice.
- Tuning the value from real devices (needs ESP32 + Android/iOS hardware),
  left to the hardware-verification TODO.
- Any device-side (libudisplay) change.

## Open Questions
- How Qt's Android/BlueZ/CoreBluetooth backends report a stuck write (an error
  vs. silence) hasn't been verified on hardware. A backend that already reports
  `CharacteristicWriteError` earlier just takes the existing path first. The
  timer is the backstop.

## Success Criteria
- An unconfirmed write fails the link with "BLE write timeout" after about 5 s.
- Confirmed writes keep strict one-in-flight ordering (behavior unchanged).
- Unit tests cover: ordering, a timeout firing, the timer resetting per
  fragment, a late confirm being ignored, clear() stopping the timer.
- TODO-030 marked done in TODOS.md, with the tuning note kept.

---

# Eng review (/plan-eng-review, 2026-10-19)

Target: `docs/designs/ble-att-write-timeout.md` (this file). The user asked for
a non-interactive run, so every choice below is **auto-decided** with its
recommended option and recorded here instead of being asked.
Outside voice: Codex was unavailable (its sandbox can't start on this host:
`bwrap: loopback: Failed RTM_NEWADDR`). The same-model fallback needs a
background-task tool this session doesn't have. No outside coverage.

## Scope Challenge

**What already exists**
- One-in-flight queue: `BleTransport::drainWriteQueue()` / `onCharacteristicWritten()`
  (`udisplay-client/src/BleTransport.cpp`). Moved, not rebuilt.
- Link-failure path: `BleTransport::failLink()` already handles
  `CharacteristicWriteError` (clear, `errorOccurred`, controller disconnect).
  Reused as-is for the timeout.
- Injectable-timeout pattern for tests: `BootstrapManager(…, int timeoutMs = kBootstrapTimeoutMs)`
  (`BootstrapManager.h:77,163`). Reused for `BleWriteQueue`.
- Qt built-ins: `QTimer` single-shot, `QSignalSpy::wait` for tests [Layer 1].
  Search was unavailable (no Aside/WebSearch in this run), so this relies on in-distribution knowledge.

**Complexity:** 8 files (BleWriteQueue.h/.cpp new, BleTransport.h/.cpp,
client CMakeLists.txt, test/CMakeLists.txt, test/test_ble_write_queue.cpp,
TODOS.md) and 1 new class. That reaches the 8-file gate. No feature cuts proposed.
Structure: *Original arrangement* (auto-decided, recommended). A header-only
`BleWriteQueue.h` would save one file, but it breaks the repo's
`.h`/`.cpp` convention for production classes (`MockTransport.h` is header-only
because it's a test double).
Scope record: feature answers: none needed; structure: A (auto); accepted scope:
the plan as written plus the approved remedies below; pending remedies: none.

**Findings**
1. [P2] (confidence: 9/10) `DeviceController.cpp:580` `m_watchdog.start();` in
   `onHeartbeat()`: the client watchdog is fed by inbound traffic only, so it
   never notices an outbound stall. This corrects the issue's premise, which
   claimed the 15 s heartbeat catches it. It changes no behavior: it's recorded
   in the Problem Statement and strengthens the case for the change.

**REALITY rows**
- REALITY: premise-table pass 4 premises, 0 unverified (watchdog fed inbound `DeviceController.cpp:580`; bootstrap 30 s `BootstrapManager.h:163`; errorOccurred→failed in all states `BootstrapManager.cpp:19-21`; teardown uses deleteLater `DeviceController.cpp:274`)
- REALITY: already-done pass no timeout on origin/main; last BleTransport change b7d8f04 (#45) `udisplay-client/src/BleTransport.cpp:110`
- REALITY: surface-check pass QTimer/QSignalSpy/QLowEnergyService::characteristicWritten already used in tree `udisplay-client/src/BleTransport.cpp:156`
- REALITY: binding-decisions pass no branch decisions logged; ble-indicate design explicitly deferred TODO-030 to its own change `docs/designs/ble-indicate-reliable-delivery.md:47`
- REALITY: numbers pass 5 s [issue #23], 30 s bootstrap [measured: BootstrapManager.h:163], 15 s watchdog [measured: DeviceController.h:280], 7.5–50 ms phone conn interval and 30 s ATT transaction timeout [estimated: BT Core spec, not re-measured] `docs/designs/ble-att-write-timeout.md:30`
- REALITY: deferred-asks pass issue #23 asks timeout, report+disconnect, QTimer per write cancelled on completion, ~5 s tunable: all in plan, none deferred `docs/designs/ble-att-write-timeout.md:58`
- REALITY: pr-count pass one PR (no .gstack/ship-policy.json, default 1) `docs/designs/ble-att-write-timeout.md:1`

Scope Challenge result: scope accepted as-is.

## 1. Architecture review

- [P1] (confidence: 9/10) Re-entrancy through `failLink`. `BleTransport.cpp`
  `failLink()`: `emit errorOccurred(reason);` → `BootstrapManager.cpp:20`
  `emit failed(err);` → `DeviceController::onBootstrapFailed` → `teardown()` →
  `m_transport->disconnectFromDevice();` runs **synchronously inside the
  `timedOut` emission**. `disconnectFromDevice()` then calls `m_writeQueue.clear()`
  on the queue that's still emitting. Remedy (R1, auto-approved): `BleWriteQueue`
  sets every bit of its state (in-flight cleared, timer stopped) before emitting `timedOut` or
  `writeRequested`, and touches no member after an emit. Tests cover `clear()`
  and `enqueue()` called from inside both signal handlers.
- [P3] (confidence: 6/10) Medium confidence, verify this is actually an issue:
  a backend that confirms synchronously inside `writeCharacteristic()` would
  nest `confirm()` → `writeRequested` once per queued fragment (recursion depth
  = queue length). Client-to-device messages are small (events, heartbeats,
  chunk requests), so depth stays tiny. Disposition: accepted as-is (R2, auto),
  documented in the class comment.
- Production failure per new path: the timeout fires on a link that's slow but
  alive (a congested radio, > 5 s for one ATT write). The user sees "BLE write
  timeout" and reconnects. That's visible, not silent. Tuning stays a hardware TODO.

```
 send(msg) ─► bleFrame ─► BleWriteQueue.enqueue(frag…)
                              │ idle? ──► inFlight=true, timer.start(5 s)
                              │           emit writeRequested(frag) ─► writeCharacteristic()
 characteristicWritten(ctrl) ─► confirm() ─► timer.stop(); next? ─► (same as above)
 timer timeout ─► inFlight=false, queue cleared ─► emit timedOut
                              └─► BleTransport::failLink("BLE write timeout")
                                    ├─ errorOccurred ─► BootstrapManager::failed ─► DeviceController: setError + teardown
                                    └─ controller->disconnectFromDevice()
```

Dispositions: R1 accepted (auto-decided, recommended); R2 accepted as-is (auto).

## 2. Code quality review

- [P2] (confidence: 9/10) Three reset sites to migrate. `BleTransport.cpp`
  `connectToDevice()`, `disconnectFromDevice()` and `failLink()` each do
  `m_writeInFlight = false; m_writeQueue.clear();`. All three must become
  `m_writeQueue.clear()` (which also stops the timer). If one is missed, a timer
  armed on the old connection fires on the new one. Remedy R3 (auto-approved).
- [P3] (confidence: 8/10) `TODOS.md:584` TODO-030 needs to be marked done. The
  tuning note goes into TODO-042 (the hardware verification TODO), which
  already cites TODO-030. Remedy R4 (auto-approved).

Dispositions: R3 accepted (auto); R4 accepted (auto).

## 3. Test review

Framework: QtTest + CTest (`udisplay-client/test/CMakeLists.txt`), per CLAUDE.md.

```
CODE PATHS                                         USER FLOWS
[+] BleWriteQueue (new, Bluetooth-free)            [+] Running link, device stops confirming
  ├── enqueue() idle → send, timer armed             └── [GAP→manual] user sees "BLE write timeout"
  │   └── [PLANNED ★★★] order + one-in-flight             error and returns to discovery (needs hardware)
  ├── enqueue() busy → queued                      [+] Bootstrap, write stalls
  │   └── [PLANNED ★★★] (same test)                  └── [GAP→manual] fails in ~5 s, not 30 s
  ├── confirm() → stop, next / idle
  │   └── [PLANNED ★★★] per-fragment timer reset
  ├── confirm() idle (late / after clear)
  │   └── [PLANNED ★★★] no-op, no writeRequested
  ├── timeout → state reset, timedOut once
  │   └── [PLANNED ★★★] fires once, isIdle after
  ├── clear() → drop queue, stop timer
  │   └── [PLANNED ★★★] no timedOut afterwards
  └── re-entrant clear()/enqueue() inside signals
      └── [PLANNED ★★★] (R1)
[+] BleTransport wiring (HAVE_BLE)
  └── [GAP] no unit seam (needs QLowEnergyController); compile-checked only
COVERAGE (planned): 7/9 paths  |  code 7/7 queue paths  |  user flows 0/2 (hardware)
```

Tests (new `udisplay-client/test/test_ble_write_queue.cpp`, unit):
- Value: protects=strict one-in-flight FIFO; fails_when=drain sends without waiting or reorders; why_new=BleTransport queue has no test; seam=timeoutMs ctor param (BootstrapManager precedent); no_claim=Qt BLE backend behavior
- Value: protects=unconfirmed write → one timedOut; fails_when=timer never armed or fires twice; why_new=new behavior; seam=timeoutMs; no_claim=real ATT timing
- Value: protects=timer restarts per fragment; fails_when=timer armed once per burst; why_new=new; seam=timeoutMs; no_claim=none beyond protects
- Value: protects=late confirm after timeout/clear is a no-op; fails_when=confirm sends next fragment on a dead link; why_new=new; seam=none; no_claim=none beyond protects
- Value: protects=clear stops timer and drops queue; fails_when=stale timer fires on next connection (R3); why_new=new; seam=timeoutMs; no_claim=BleTransport calls clear at all 3 sites
- Value: protects=re-entrant clear/enqueue from handlers (R1); fails_when=queue mutates after emit; why_new=new; seam=none; no_claim=DeviceController teardown order
Timing tests use generous margins (timeout ≥ 200 ms, waits well clear of it) to stay stable on the loaded 4-core host.
Regression: existing one-in-flight behavior is covered by the first test (CRITICAL; behavior unchanged).
Tests made obsolete: none.

## 4. Performance review

No issues found. Each fragment does one timer start/stop (constant cost). Queue
memory is the same as today.

## NOT in scope
- CCCD descriptor write timeout: the 30 s bootstrap timeout covers it, and it isn't on the data queue.
- Real-device tuning of 5 s: needs hardware; tracked in TODO-042.
- Outbound liveness for TCP: kernel send buffering makes the failure mode different; not asked.

## Failure modes
| Path | Failure | Test | Handling | User sees |
|---|---|---|---|---|
| timeout | slow but alive link >5 s | unit (timing) | failLink | "BLE write timeout" |
| timeout during teardown | re-entrant clear | unit (R1) | state set before emit | nothing extra |
| reconnect | stale timer from old link | unit (clear) + R3 | clear() at 3 sites | nothing |
| late confirm | confirm after timeout | unit | no-op | nothing |
Critical gaps: 0.

## Worktree parallelization strategy
Sequential implementation, no parallelization opportunity.

## Implementation Tasks
- [ ] **T1 (P1, human: ~1.5h / CC: ~10min)** — client — Add `BleWriteQueue` (state before emit, no member access after emit)
  - Surfaced by: Architecture — R1 re-entrancy via failLink → teardown
  - Files: udisplay-client/src/BleWriteQueue.{h,cpp}, udisplay-client/CMakeLists.txt
  - Verify: test_ble_write_queue
- [ ] **T2 (P1, human: ~30min / CC: ~5min)** — client — Route BleTransport through the queue at all three reset sites
  - Surfaced by: Code quality — R3
  - Files: udisplay-client/src/BleTransport.{h,cpp}
  - Verify: client build with HAVE_BLE=ON
- [ ] **T3 (P1, human: ~1h / CC: ~10min)** — client tests — test_ble_write_queue.cpp
  - Surfaced by: Test review
  - Files: udisplay-client/test/test_ble_write_queue.cpp, udisplay-client/test/CMakeLists.txt
  - Verify: ctest -R test_ble_write_queue
- [ ] **T4 (P3, human: ~10min / CC: ~2min)** — docs — Mark TODO-030 done, move the tuning note to TODO-042
  - Surfaced by: Code quality — R4
  - Files: TODOS.md

## Decision ledger
- R1 re-entrancy contract: approved (auto-decided, recommended; user asked for non-interactive run).
- R2 synchronous-confirm recursion: accepted as-is (auto).
- R3 migrate all three reset sites: approved (auto).
- R4 TODO-030 done + tuning note into TODO-042: approved (auto).
- Complexity structure: original arrangement (auto).
- TODO proposal "CCCD write timeout": B) skip (auto) — the bootstrap timeout covers it.
Approval readiness: PASS (R1–R4, structure, TODO; all auto-decided per the user's non-interactive instruction).

## Unresolved decisions
None.

## Completion summary
- Step 0: Scope Challenge — scope accepted as-is
- Reality rows: 7/7 applicable rows emitted, 0 unverified premises
- Architecture Review: 2 issues found
- Code Quality Review: 2 issues found
- Test Review: diagram produced, 2 gaps identified (hardware-only user flows)
- Performance Review: 0 issues found
- NOT in scope: written
- What already exists: written
- TODOS.md updates: 1 item proposed (skipped, auto)
- Failure modes: 0 critical gaps flagged
- Unresolved decisions: 0 in this review
- Outside voice: codex, unavailable (sandbox can't start; no native fallback tooling)
- Parallelization: 1 lane, 0 parallel / 1 sequential
- Lake Score: N/A (no completeness-scored answers)

## Suppressed findings
- (confidence: 4/10) Qt's Android backend may run its own GATT job timeout/retry
  that overlaps the 5 s window. It couldn't be verified (no Qt Bluetooth sources
  on this host), so it's listed under Open Questions.

## GSTACK REVIEW REPORT

| Review | Trigger | Why | Runs | Status | Findings |
|--------|---------|-----|------|--------|----------|
| CEO Review | `/plan-ceo-review` | Scope & strategy | 0 | — | — |
| Outside Review | codex (auto, plan-review) | Independent 2nd opinion | 1 | unavailable | sandbox can't start on host |
| Eng Review | `/plan-eng-review` | Architecture & tests (required) | 1 | ISSUES OPEN | 6 issues, 0 critical gaps (all mapped to tasks T1–T4) |
| Design Review | `/plan-design-review` | UI/UX gaps | 0 | — | — |
| DX Review | `/plan-devex-review` | Developer experience gaps | 0 | — | — |

- **OUTSIDE COVERAGE:** codex, plan-review, unavailable (bwrap sandbox failure); no native fallback ran.
- **VERDICT:** No review CLEAR; findings are mapped implementation tasks, not blockers. eng review required

NO UNRESOLVED DECISIONS
