# TODOS

## Infrastructure

### TODO-050: Release signing + Play Store distribution for udisplay-client Android APK

**What:** Add a GitHub Secrets-backed keystore, tag-derived version codes, and signed
release APK builds for the Android CI job.

**Why:** Unlocks real distribution beyond sideloading — Play Store or any channel that
requires a consistently-signed APK.

**Context:** Deferred during the `/office-hours` design session for the Android CI job
(Approach B, considered and explicitly not chosen — see
`~/.gstack/projects/agasattila-udisplay/prog-main-design-20260724-145313.md`). The v1
Android job (Approach A) ships a debug-signed, sideload-only APK. A signing keystore is
a one-way door — losing it means any future update to an app published under that
signature is impossible — so this should only be picked up once there's actual demand
for wider distribution, not preemptively.

**Effort:** L
**Priority:** P3
**Depends on:** The v1 Android CI job (build-android) landing and working first.

**Known tradeoff (v1):** debug-signed also means `android:debuggable="true"` —
permits JDWP debugger attach and `adb run-as` shell access on any device the APK is
installed on. Accepted for v1: sideload-only hobbyist tool, no credentials/user data
handled client-side (it talks to the user's own uDisplay device over local
network/BLE), uploaded as a public 30-day CI artifact on this already-public repo.
Revisit this specifically (not just signing) if the threat model ever changes.

### TODO-051: Multi-ABI matrix build for udisplay-client Android APK

**What:** Extend the `build-android` CI job to a matrix building `armeabi-v7a` and/or
`x86_64` in addition to `arm64-v8a`.

**Why:** Covers older Android devices and Android emulator testing (emulators typically
run x86_64).

**Context:** Deferred during the `/office-hours` design session — arm64-v8a covers
effectively all real Android hardware from the last several years, so a single-ABI build
was chosen for v1. Revisit if a real user reports an incompatible device, or if emulator
testing becomes a priority.

**Effort:** M
**Priority:** P4
**Depends on:** The v1 Android CI job (build-android) landing and working first.

### TODO-052: Prebuilt Docker image for the Qt-Android CI toolchain

**What:** Bake the Qt 6.11 Android kit + NDK into a Docker image (published to GHCR) so CI
runs pull the image instead of reinstalling the toolchain every time.

**Why:** Faster CI runs, more reproducible builds, insulated from GitHub Actions cache
flakiness — once the base Android job is proven stable.

**Context:** Considered as Approach C during the `/office-hours` design session for the
Android CI job and explicitly not chosen for v1 — building this before the base
`build-android` job has even run successfully once would mean maintaining a new Dockerfile
+ registry + version-sync burden before there's a working job to optimize.

**Effort:** M
**Priority:** P4
**Depends on:** The v1 Android CI job (build-android) landing, running successfully for a
while, and CI time/reliability becoming a real friction point.

## P1 — Blocking / Risk Validation

### TODO-002: iOS BLE validation (Qt6)
**What:** Build a minimal Qt6 iOS test app that scans for and connects to an ESP32 BLE GATT service. Confirm Qt6 Bluetooth works on iOS without native CoreBluetooth bridging.
**Why:** If Qt6 BLE on iOS is broken/insufficient, the iOS strategy must pivot to native Swift.
**Pros:** Closes the biggest unknown risk. Gates the iOS roadmap with real evidence.
**Cons:** Requires Mac + Apple Developer account + physical iOS device.
**Context:** Android ships first regardless (v1). iOS is planned between v1 and v2.
**Effort:** S (human: ~1 day / CC: n/a — requires hardware)
**Depends on:** Qt6 Bluetooth Android work (leverage the same Qt6 setup).

## P2 — Deferred Features

## P2 — Post-Launch Distribution & Quality

### TODO-004: Package manager distribution (v1.1)
**What:** Publish the Qt desktop client to Homebrew (macOS tap), winget (Windows), and AppImage (Linux). F-Droid (Android) is already handled at v1 launch.
**Why:** Firmware devs discover tools via `brew search` and package managers, not Google. GitHub Releases is sufficient for early adopters but limits discoverability for the broader community.
**Pros:** Significantly increases discoverability on each platform post-launch. Standard expectation for developer tools.
**Cons:** Each registry has its own maintenance burden (update PRs needed on each release). Small but real overhead — worth it only once v1 is stable.
**Context:** Revisit after v1 ships and user platform data is available. Prioritize the platform with the most users first.
**Effort:** S per platform (human: ~1 day total / CC: ~30 min)
**Priority:** P2 — after v1 validation
**Depends on:** v1 stable release, user data on platform breakdown.

---

### TODO-005: QML UI test coverage (v1.1)
**Status:** ⚠️ PARTIALLY DONE (organically, not as this task) — `udisplay-client/test/qml/`
exists with 9 tests wired into CTest (`test/CMakeLists.txt`), but every one of them is a
one-off regression test pinned to a specific bug found during development, not the
systematic sweep this TODO scoped:
- Covered incidentally: Row/Grid flex/align/nesting math (4 tests), button-face
  composition (3 tests), global-stylesheet reaching page chrome (1 test), version-label/
  status-dot overlap (1 test).
- **Discovery screen:** no test.
- **Bootstrap flow:** not covered in QML at all (covered in C++ via `test_bootstrap.cpp`/
  `test_device_controller.cpp`, but that's not a QML/UI test).
- **Widget rendering, all types:** only the widgets that happened to be a bug-fix vehicle
  are touched (row/grid/label/button/display/led/rgbled). `DropdownWidget`,
  `SliderWidget`, `ToggleWidget`, `TextWidget`, `SeparatorWidget`, `ButtonGroupWidget`,
  `SectionWidget` have zero QML test coverage.
- **Error states:** no dedicated test.
- The "all 7 widget types" figure below is also stale — the widgets dir now has 14 QML
  widget files.
**What:** Add automated QML/UI tests for the discovery screen, bootstrap flow, widget rendering (all widget types), and error states using Qt's `TestCase` QML type or a screenshot comparison tool.
**Why:** v1 deferred QML testing because the UI will change during early development. After v1 ships and the design stabilizes, test coverage locks in correct behavior and prevents regressions.
**Pros:** Catches UI regressions in widget rendering and state transitions after the design settles.
**Cons:** QML testing is brittle for rapidly changing UI — wrong to add during active design iteration.
**Context:** QML unit tests are the v1.1 quality step. The existing 9 tests are a useful
foundation (same `qml -platform offscreen` + CTest harness this task would use) but were
added as regression guards during unrelated bug fixes, not as deliberate coverage —
remaining work is discovery screen, bootstrap flow, the 7 untested widget types, and
error states.
**Effort:** M (human: ~3 days / CC: ~1 hour)
**Priority:** P2 — after v1 UI stabilizes
**Depends on:** v1 shipped, /plan-design-review completed, Qt UI design settled.

---

### TODO-007: BLE Android OEM compatibility matrix (`docs/ble-compat.md`)
**What:** A tracked compatibility matrix documenting known BLE quirks per Android OEM/version (Samsung NOTIFY drops after MTU negotiation, Xiaomi MIUI background BLE restrictions, etc.) and the corresponding workaround in libudisplay or the Qt client.
**Why:** BLE OEM bugs are invisible until a specific-model user files a bug report. A tracked matrix prevents the same bugs from being re-diagnosed and ensures workarounds get merged rather than re-discovered.
**Pros:** Prevents duplicate debugging. Gives users self-service info for known issues.
**Cons:** Requires ongoing maintenance — update as new devices are tested in the wild.
**Context:** Deferred from eng review. The 2-characteristic GATT layout is the right architecture. Populate this doc after first real-user hardware reports come in post-v1 launch.
**Effort:** S to create (human: ~2 hours / CC: ~10 min); ongoing per-issue as reports come in.
**Priority:** P2 — start populating after first round of real-user testing
**Depends on:** v1 shipped; real user hardware reports.

---

### TODO-015: Document `udisplay_ui_feed` TCP buffer sizing in `docs/protocol.md`
**What:** Add a section to `docs/protocol.md` explaining why the generated `_ui_rx_buf` is `2 * (UDISPLAY_MAX_MSG_SIZE + 2u)` = 2052 bytes (not 1026). Explain the TCP streaming invariant: at any call boundary, up to 1025 bytes of a partial frame may already be buffered, so the worst case is 1025 (leftover) + 1026 (full next frame) = 2051 bytes in flight before the drain loop runs.
**Why:** The original plan claimed 1026 bytes was safe ("unreachable with fixed buffer"), which is incorrect. Without documentation, future contributors are likely to repeat this mistake — e.g., if someone refactors the buffer or ports the feed logic. The 2x sizing rationale needs to be on record.
**Pros:** Prevents the same reasoning error from reoccurring. Explains a non-obvious constant in the generated code.
**Cons:** Documentation-only effort — no code change needed.
**Context:** Discovered during post-implementation eng review. The plan's `Failure Modes & Guards` table listed this buffer as safe; it was not. Fixed by doubling the buffer. The comment `/* 2052 bytes — holds worst-case partial + full frame */` is in the generated code, but the protocol doc should explain the math.
**Effort:** XS (human: ~30 min / CC: ~5 min)
**Priority:** P2 — informational, no functional impact
**Depends on:** Nothing.

---

### TODO-010: DeviceController integration test (`test_device_controller.cpp`)
**What:** A test that exercises the full connection path end-to-end without real hardware: mock TCP server injects a valid Merkle handshake + hash batch + compressed YAML chunk, verifies that `DeviceController` reaches `state == "running"` and `widgetModel.rowCount() > 0` after connecting. Also verifies the heartbeat watchdog fires an error after 15s of silence (using a QSignalSpy + QTest::qWait).
**Why:** Unit tests cover BootstrapManager, YamlParser, and WidgetModel individually, but the wiring between them (signal connections, argument order, callback registration) is currently only tested on real hardware. A bug in that wiring would not be caught by the existing test suite.
**Pros:** Closes the one integration gap remaining in the test pyramid. No hardware required — uses MockTransport or a local TCP echo. Catches regressions in connectTcp() and onBootstrapSucceeded() wiring.
**Cons:** Moderate effort — requires either extending MockTransport or a QTcpServer fixture. The watchdog test needs a real timer (QTest::qWait) which adds ~15s to the test run unless the watchdog interval is made injectable.
**Context:** Proposed during plan-eng-review. All components have unit test coverage. This test plugs the wiring gap.
**Effort:** S (human: ~1 day / CC: ~20 min)
**Priority:** P2 — after v1 ships
**Depends on:** Stable v1 connection flow; consider making watchdog interval injectable (constructor param) to enable fast-path testing.

---

### TODO-022: Remove capability strings before first official client release
**Status:** ⚠️ PARTIALLY DONE (corrected — see completed TODO-045) — `kKnownCapabilities` emptied in DeviceController; `capabilities:` removed from all codegen test fixtures (conftest.py); `test_capabilities_*` tests removed from test_validate.py; inline `capabilities:` removed from all remaining test_validate.py dicts. Schema still accepts `capabilities:` for backward compat (free-form list). **The capability-check loop itself was never removed and is NOT a no-op**: `for (const QString& cap: capabilities) if (!kKnownCapabilities.contains(cap))` with an empty `kKnownCapabilities` rejects every non-empty `capabilities:` list — the opposite of dead weight. The "remove the `capabilities` handshake rejection logic" scope item below was never done.
**What:** Before publishing the first official v1 client release, remove all capability string checks from the codebase: `dropdown`, `label`, `layout-v2`. New widget types added during v1 development (LED color, rgbled) must NOT add new capability strings.
**Why:** Capability gating only protects users if older published clients know to reject unknown capabilities. Before any client has been published, the feature has no users to protect and only adds dead weight. The mechanism adds noise (required in YAML, checked in DeviceController, tested in pytest) with zero benefit at this stage. Once a client has been published and users can have old versions, capability negotiation becomes meaningful — at that point, re-introduce it with a clear versioning policy.
**Scope:**
- `udisplay-gen/udisplay_gen/schema/udisplay.schema.json` — remove `capabilities` from the allowed device block fields (or change to a free-form list for future use)
- `udisplay-gen/udisplay_gen/validate.py` — remove capability string validation
- `udisplay-client/src/DeviceController.cpp` — remove the `capabilities` handshake rejection logic
- `udisplay-gen/tests/test_validate.py` — remove `test_capabilities_*` tests
- All demo YAMLs and test fixtures — remove `capabilities:` lines
**When:** Before tagging the first official v1 release. Must not block any pre-release development.
**Effort:** XS (human: ~1 hour / CC: ~10 min)
**Priority:** P1 — must complete before first official v1 client release
**Depends on:** Nothing technical. Timing: after TODO-016/017/018/009/020/021 are all done and the feature set is stable, immediately before cutting the release tag.

## V2 — MVVM + Webview (Direction Set, Details TBD)

V2 is a architectural rethinking of the client rendering model. Where v1 uses a native
Qt widget tree driven by a flat device-authoritative state machine, v2 introduces:

- **YAML datamodel:** A reactive data layer declared in YAML alongside widgets. Bindings,
  computed values, and state derivations are expressed declaratively — replacing the
  imperative `source` approach.
- **Webview client:** The Qt client hosts a Webview as its rendering surface. The UI is
  defined in HTML/CSS/JS with two-way data binding to the device state via the datamodel.
  This replaces the native QML widget renderers.
- **True MVVM separation:** Model (device state + YAML datamodel), ViewModel (binding
  engine), View (Webview). The device remains authoritative for raw values; the datamodel
  handles all derived/computed/formatted state client-side.

### TODO-019: V2 MVVM architecture — WebView widget + tar.gz BLOB + IPFS
**What:** Extend the YAML format and code generation pipeline to support a `webview`
widget type. The same YAML also declares the reactive datamodel (bindings, computed
values) that drives it — the datamodel is not a separate format. Codegen packs the YAML
and web assets into a tar.gz that becomes the V2 BLOB package. The Qt client hosts a
Webview surface driven by that YAML-declared datamodel.
**Why:** v1's native Qt widget tree cannot support complex layouts, rich data
visualisation, or custom branding without extending the Qt renderer for every new widget
type. A Webview surface decouples UI expression from the platform client entirely —
web developers can build arbitrary UIs against the device state without touching
Qt or firmware.
**Architecture settled:**
- YAML is the single base format for both V1 and V2. A V2 YAML is a superset, and it
  is also where the reactive datamodel is declared.
- `type: webview` top-level widget triggers V2 code generation. `capabilities: [webview-v2]`
  signals to the client that a V2 BLOB is required. **WebView is exclusive** — a V2 YAML
  may not mix V1 widgets alongside `webview` in the same document.
- The V2 BLOB package is a tar.gz (YAML + web assets) produced by codegen. Inside it, the
  YAML and the web assets are chunked and Merkle-hashed as **two independent trees**: the
  existing YAML BLOB merkle root, plus a new web-assets merkle root. Codegen injects the
  web-assets merkle root back into the YAML itself, so the YAML BLOB alone is enough for
  the client to verify/locate the assets.
- The device may store one, both, or neither of the two blobs (YAML BLOB / web-assets
  BLOB) — a firmware storage tradeoff, not a protocol requirement. On connect, the client
  queries the device first for each blob; whatever the device doesn't have, the client
  falls back to looking it up on IPFS by its Merkle root.
- Expected common pattern: firmware embeds the (small) YAML BLOB directly and publishes
  the (larger) web-assets BLOB only to IPFS — keeps firmware size down while the device
  remains the primary source for its own UI definition.
- Web content is immutable after firmware compilation in every mode (security guarantee)
  — once a merkle root is baked into the YAML, the referenced assets can't change without
  changing the hash.
- Client-side chunk cache: every downloaded chunk (from device or IPFS) is cached
  content-addressed by hash, so only the first connection to a given device/CID pays the
  download cost — later connections resolve from cache.
**Open questions:**
1. Datamodel YAML schema (binding syntax, computed values, V1 widget source references)
   — still TBD; only "declared in YAML, webview-exclusive" is settled so far.
**Pros:** Arbitrary UI complexity. Web skills sufficient for custom UIs. Branding trivial.
  Creates a developer services opportunity around the Webview bridge API.
**Cons:** Webview binary size (~50MB QtWebEngine on Android or system WebView).
  Webview/native bridge requires careful API design. IPFS adds network/tooling dependency.
**Context:** Direction set during product vision discussion. Specify full
design after V1 widget system expansion (TODO-016/017/018/009) is proven in production.
**Effort:** L — multi-month; full spec TBD in dedicated design session
**Priority:** V2 — after V1 complete
**Depends on:** V1 fully shipped including widget system expansion (TODO-016/017/018/009).

---

### TODO-027: Firmware-driven style switching
**What:** Implement the protocol mechanism for a device to command the client to switch to a named theme. The exact method is TBD between: (a) a new message type in the uDisplay binary protocol, or (b) `SET_PROPERTY` on a reserved `widgetId` (e.g., 0x00 or a dedicated "meta" widget ID).
**Why:** Multi-style support in the client ships with `setActiveStyle(name)` infrastructure, but no way for the firmware to trigger it. The feature is only usable if the device can switch themes (e.g., switch to "warning" theme on alarm state).
**Pros:** Closes the style switching loop without any client-side UI. Firmware owns the UX state machine.
**Cons:** Requires protocol spec decision + codegen update + client handler + firmware library update.
**Context:** Identified during plan-eng-review. User clarified style switching is firmware-side only. `DeviceController::setActiveStyle()` Q_INVOKABLE is in place as the call target; the protocol trigger mechanism is what's missing.
**Effort:** M (human: ~2 days / CC: ~30 min, after protocol decision is made)
**Depends on:** Global stylesheet feature shipped; protocol spec decision made (potentially relates to TODO-001 Merkle spec — same governance process).
**Priority:** P2

---

### TODO-029: DeviceController auth UI wiring
**What:** Wire `BootstrapManager::authFailed()` signal to a QML password prompt and register a credential provider via `setAuthCredentialProvider()` on DeviceController so that auth-enabled devices can be accessed interactively.
**Why:** The core auth protocol (libudisplay + BootstrapManager) is fully implemented, but DeviceController does not set a credential provider. A user connecting to an auth-protected device currently receives "Device requires authentication but no credential provider is set" — no way to enter a password.
**Pros:** Completes the end-to-end auth UX. Firmware teams can ship password-protected displays.
**Cons:** Requires a new QML dialog, a DeviceController signal (`authRequired()`), and a Q_INVOKABLE `submitCredential(password)` that computes `HMAC-SHA256(key=password, message=salt)` and feeds the result back.
**Context:** Auth protocol shipped in proto 0x04. DeviceController.cpp has zero auth references. The safe fallback until this lands is the error path in onBootstrapFailed().
**Effort:** S (human: ~3 hours / CC: ~10 min)
**Depends on:** Auth protocol feature (proto 0x04 — shipped).
**Priority:** P1 — auth-enabled devices are unusable without this

---

### TODO-030: BleTransport ATT write timeout
**What:** Add a per-ATT-write timeout (5s) to `BleTransport::drainWriteQueue()`. If `characteristicWritten` does not fire within 5 seconds of a `writeCharacteristic()` call, emit `errorOccurred("BLE write timeout")` and disconnect. Implement as a `QTimer` member that starts on each write and is stopped on `characteristicWritten`.
**Why:** If the ESP32 firmware hangs mid-bootstrap, or an Android OEM ATT stack silently drops a WRITE_WITH_RESPONSE confirmation, the BleTransport write queue stalls indefinitely. The connection appears live (no disconnect signal), but no data flows. The 15-second heartbeat watchdog in DeviceController eventually catches this, but only after 15 seconds and only after bootstrap completes — a mid-bootstrap hang would never trigger the watchdog at all.
**Pros:** Catches stuck connections 3× faster than the heartbeat. Protects mid-bootstrap hangs that the heartbeat doesn't cover. Low complexity — a single QTimer member.
**Cons:** False-positive risk if an ATT write genuinely takes > 5s (extremely congested radio environment). The timeout value needs tuning from real-device data.
**Context:** Identified during plan-eng-review. The write queue is designed correctly for sequential delivery, but has no timeout guard. Raised as the one critical gap in the BleTransport failure mode table.
**Effort:** XS (human: ~2 hours / CC: ~5 min)
**Depends on:** TODO-011 (BleTransport base implementation).
**Priority:** P2 — implement after first real-device testing reveals actual timeout behavior

---

### TODO-032: Section inside Row/Grid support
**What:** Support `section` type as a child of Row/Grid containers in WidgetDelegate. Currently `WidgetDelegate.qml` maps `section` → null (zero-height placeholder, safe default).
**Why:** The flat-model collapse mechanism (`SectionWidget` + `sectionOwnerRow`) was designed for top-level use only. A Section's children are separate rows in `m_widgets` with a visibility flag — they are NOT embedded in `props.items`. Supporting Section inside a Row/Grid requires either: (a) serializing Section children into `props.items` (architectural change to `buildPropsMap`), or (b) a new Section variant that works inline.
**Context:** Explicitly deferred in TODO-031 PR 2 review. `WidgetDelegate` maps `section` → null to avoid the rendering confusion of showing a header with no children. Top-level Section behavior (flat model + collapse) is unaffected.
**Effort:** M (human: ~2 days / CC: ~30 min)
**Priority:** P3 — edge case; no known firmware use case for Section-inside-Row
**Depends on:** TODO-031 complete.

---

### TODO-036: Cap recursion depth for nested row/grid/section/button-children
**What:** Add a depth counter (parameter or thread-local) threaded through
`YamlParser.cpp`'s `buildTopLevelWidget()`/`appendRowGridChild()`/
`buildAndAppendWidgets()` recursion, `WidgetModel::indexDescendants()`, and
`WidgetDump::dumpWidget()`, that errors out (not just warns) past a sane
max nesting depth (e.g. 8-10 levels). The QML side (`WidgetDelegate.qml`'s
`rowComp`/`gridComp` dynamic `Loader{source:...}` self-recursion) inherits
whatever depth the parsed model reaches, so capping at parse time is
sufficient — no separate QML-side guard needed.
**Why:** Found during adversarial review of the row/grid alignment PR — `row`/`grid`/`section` are ID-transparent (`isContainer()`
in YamlParser.cpp, consume no widget ID), so the existing `240`-widget
safety cap only bounds *leaf* widget count, not container nesting depth.
A YAML file with thousands of nested empty `row: {widgets: {inner: {type:
row, ...}}}` levels sails past that cap entirely and can stack-overflow
the client during parsing/model-building/rendering. Predates this PR —
the unlimited-depth recursion itself is TODO-031's original design; this TODO is specifically the missing depth cap, not the
recursion feature itself.
**Pros:** Closes a real stack-overflow DoS vector from a single malformed
or (per the firmware-update-tampering scenario) tampered YAML file — no
network/auth layer currently stands between a YAML blob and this parser.
**Cons:** Touches several recursive functions across two files; needs a
sensible depth constant chosen deliberately (too low breaks legitimate
deeply-nested dashboards, too high doesn't meaningfully bound the stack).
**Context:** Raised as a TODO (not fixed inline) during `/ship` of the
RowWidget/GridWidget/LabelWidget alignment work on `fix/widget-model-redesign` — orthogonal to that PR's layout/alignment scope, and the
recursion structure it needs to modify predates this PR by roughly a
month.
**Effort:** S (human: ~3-4 hours / CC: ~25 min)
**Priority:** P2 — real DoS vector, but requires firmware YAML tampering
or a malicious/corrupted config file to trigger; not exploitable via any
currently-network-reachable path.
**Depends on:** Nothing blocking.

---

### TODO-037: Unify container-transparency logic across YamlParser.cpp and widget_ids.py
**What:** A single source of truth (or at minimum a cross-reference test asserting
agreement) for "is this type transparent to ID assignment" — currently three
independent implementations: `YamlParser.cpp`'s `isContainer()` (client-side
`WidgetDef`/`idMap` construction), `udisplay-gen/udisplay_gen/widget_ids.py`'s
`CONTAINER_TYPES` (used separately by `_collect()` for ID-path assignment and
`collect_types()` for C-API type inference), and the documented-but-unenforced
convention referenced in `WidgetModel.cpp`'s comments.
**Concrete failure mode (confirmed via adversarial review):** this isn't
just a style/consistency issue — `collectPathsRecursive`'s button-child loop
(`YamlParser.cpp:154-169`) checks `isDecoration()` but never `isContainer()`. If a
device sends raw, unvalidated YAML with `type: row` or `type: grid` as a button child
(schema-illegal via `buttonFaceChild`'s `oneOf`, but the client parses device YAML
directly with no runtime schema validation), that container is treated as a flat leaf
and its own nested children are never registered in `idMap` under any path. When
`buildTopLevelWidget`'s `Row`/`Grid` case (`YamlParser.cpp:~397`) then looks up those
grandchildren via **bare, unprefixed key** (`idMap.count(ck)` — correct only for
legitimate top-level/container-transparent nesting), a grandchild whose bare key
happens to coincide with any other widget's name elsewhere in the same document
**silently inherits that unrelated widget's real protocol ID** — causing STATE_UPDATE
messages to cross-apply between two semantically unrelated widgets. Verified
pre-existing (not introduced by the button-face `children:`→`widgets:` rename — the
`isContainer` check was never present in this loop before or after that change).
Closing this properly requires the recursive type restriction already planned for
Increment 2 of the button-face redesign (schema + client enforce `led/rgbled/display/
label` only, recursively, at every depth inside a button's face) — this TODO's original
"three independent implementations" framing is a symptom of the same root cause, not a
separate concern.
**Why:** Found broken independently in three places during a single `/plan-eng-review`
session for the button-face composition redesign (`fix/widget-model-redesign`): `YamlParser.cpp:collectPathsRecursive`'s flat, non-recursive handling of
button children; `widget_ids.py:_collect()`'s identical flat-loop bug for the same
concept; and `widget_ids.py:collect_types()`'s hardcoded `"led"` type for every button
child regardless of actual type. All three are the same root-cause class — "a place
that walks the widget tree forgot that a button child can itself be a container" —
discovered independently rather than fixed once and cross-checked. A fourth
occurrence (e.g. a future `WidgetDump.cpp`-style tree walker) is a when, not an if,
without either a shared implementation or a test that catches drift between the C++
and Python copies.
**Pros:** Prevents this exact bug class from recurring a fourth time. A cross-language
shared implementation isn't feasible (C++ vs Python), but a generated/shared constant
list plus a test asserting `CONTAINER_TYPES` (Python) and `isContainer()`'s type set
(C++) stay in sync would catch drift at test time instead of via manual review.
**Cons:** Real scope — touches two languages, three functions, and needs either a
build-time codegen step (single source YAML/JSON listing container types, consumed by
both C++ and Python) or a hand-maintained cross-reference test kept in sync manually
(weaker, but zero build-system changes).
**Context:** Raised during `/plan-eng-review` of the button-face `children:` ->
`widgets:` redesign — the three specific instances found this session are
fixed as part of that redesign's Increment 1/2 work; this TODO is about preventing the
pattern from recurring, not about the instances themselves.
**Increment 2 status:** All three concrete instances are now fixed —
`collectPathsRecursive`'s button-child loop now recurses (`YamlParser.cpp`),
`buildTopLevelWidget`'s Row/Grid case now threads an explicit `idPrefix` so nested
container ID lookups resolve correctly, and `widget_ids.py`'s `_collect()`/
`collect_types()` both recurse the same way. The concrete cross-widget
state-corruption path described above no longer exists. What remains open is this
TODO's actual ask: a single source of truth (or cross-reference test) so a *fourth*
occurrence doesn't require finding it by hand again.
**A fourth occurrence, same session:** `YamlParser.cpp`'s new `kButtonFaceExcludedTypes`
(the client-side warning list for interactive types disallowed on a button face) is a
third independent place encoding "what's allowed on a button face" — alongside the two
schema copies' `buttonFaceChild` `oneOf` (already TODO-035). Flagged by the
maintainability specialist during this session's `/ship`; no shared source exists yet.
**Effort:** M (human: ~1-2 days / CC: ~30 min)
**Priority:** P1 — bumped from P2 after adversarial review confirmed a concrete
cross-widget state-corruption path (see failure mode above), not just a style/DRY
concern. Not blocking Increment 1 (the vulnerable code path predates it and requires
Increment 2's type restriction to close), but should not sit at P2/"whenever" priority.
**Depends on:** Increment 1 and Increment 2 of the button-face composition redesign
landing (fixes the three known instances first; this TODO generalizes the fix).

---

### TODO-038: Pre-existing `test_handshake` merkle_root vector mismatch
**What:** `udisplay-gen/tests/test_vectors.py::TestMessageVectors::test_handshake` fails —
the computed `merkle_root` slice (`raw[2:34].hex()`) doesn't match
`tests/protocol_vectors.json`'s `messages.HANDSHAKE.input.merkle_root`. The byte pattern is
suspicious, not random noise: actual = `0074ac9fa684...d10bac7bdffdb` (32 bytes, leading
`00`), expected = `74ac9fa68428...0bac7bdffdb6e` (32 bytes, trailing `6e`). Actual reads
like expected shifted right by one byte with a `00` prepended and the real trailing byte
dropped off the end of the read window — a strong hint that either the `HANDSHAKE` message
layout gained/lost a byte somewhere before `merkle_root` (e.g. an extra flags/reserved byte)
without the test vector being regenerated, or the vector's own `bytes` field is stale
relative to the current message encoder.
**Why:** Blocks trusting `test_handshake` as a real regression gate — it's currently "always
red," so a genuine handshake-encoding regression would ship unnoticed alongside this
known-failing baseline.
**Pros:** Once fixed, restores `test_handshake` as a meaningful canary for the handshake
message format — currently dead weight in the suite.
**Cons:** Requires tracing the actual `HANDSHAKE` message encoder (likely in `libudisplay` or
`Protocol.cpp`) against the vector's `bytes` field byte-by-byte to find where the one-byte
drift is introduced — not a quick fix, needs proper investigation (`/investigate` recommended).
**Context:** Confirmed pre-existing and unrelated to `feat/widget-model-redesign-button` —
verified by stashing all Increment 1 changes and re-running; failure is byte-for-byte
identical with and without those changes. First noticed during `/review` on
`fix/widget-model-redesign`, re-confirmed during `/ship` of
`feat/widget-model-redesign-button`.
**Effort:** M (human: ~half day / CC: ~30-45 min, mostly investigation)
**Priority:** P0 — a permanently-red protocol-compatibility test undermines trust in the
whole vector suite; should be root-caused before it masks a real regression.
**Depends on:** Nothing blocking. Independent of the button-face composition redesign.

---

### TODO-039: Untested non-scalar guard branches in YamlParser.cpp
**What:** `parseFlex()`, `parseGridColumns()`, and `parseAlign()` (`udisplay-client/src/
YamlParser.cpp`) all guard with `!node[key].IsScalar()` and silently treat a non-scalar
value (a YAML list or map given for `flex:`, `columns:`, or `align:`) as omitted — no
warning, falls back to the default. This fallback path has zero test coverage across
`test_yaml_parser.cpp` and `udisplay-gen/tests/test_validate.py`.
**Why:** Found by the testing specialist during `/ship` of `feat/widget-model-redesign-
button`. Low real-world risk (a firmware author writing `flex: [1, 2]` is an
unlikely typo, not a realistic authoring mistake), but the fallback behavior is currently
unverified — a future refactor changing it (e.g. to warn or error) could ship unnoticed.
**Pros:** Closes a genuine, if low-probability, coverage gap. Cheap to write (3 small test
cases, one per parser).
**Cons:** None significant — small, isolated addition.
**Context:** Pre-existing gap in parsing helpers that predate the button-face composition
redesign; not introduced or worsened by that work. Deferred rather than fixed inline to
keep that ship's diff focused.
**Effort:** XS (human: ~1h / CC: ~10 min)
**Priority:** P3 — low real-world risk, cheap whenever picked up.
**Depends on:** Nothing blocking.

---

### TODO-040: Parse-warning diagnostics have zero QML subscribers — invisible on real devices
**What:** `DeviceController::parseWarningsChanged(QList<YamlParser::ParseDiagnostic>)`
(`DeviceController.h:133`) is emitted in two places (`DeviceController.cpp:333,461`) but
has no QML listener anywhere in the app — confirmed via repo-wide grep for
`parseWarnings`/`onParseWarningsChanged`, zero hits outside the C++ emit sites. Add a
QML `Connections` (or equivalent) that surfaces these diagnostics somewhere a user can
actually see — a toast, a debug panel, a status indicator — at minimum in `-design`
live-preview mode where a developer is actively iterating on YAML.
**Why:** Found during adversarial review of the button-face `children:`→`widgets:`
redesign — that redesign added a new Warning diagnostic for the legacy
`children:` key specifically so a stale/hand-authored device YAML doesn't silently lose
button face content. But on an embedded touchscreen device with no attached console,
that warning (and every other existing `Severity::Warning` diagnostic in the app) is
only ever logged to stderr/journal — nobody will ever see it. The design intent
("must not lose data without a diagnostic") isn't actually satisfied in the field.
**Pros:** Makes every existing and future parse-warning diagnostic actually useful in
production, not just in a terminal during development. Closes the gap for the new
legacy-key warning specifically, but fixes it for the whole diagnostics system.
**Cons:** Needs a UX decision (toast vs. panel vs. status icon) — not just a wiring fix.
**Context:** Systemic, pre-existing gap (applies to every diagnostic the parser has ever
emitted) surfaced while adversarially reviewing one new instance of it.
**Effort:** S (human: ~3-4h / CC: ~20 min, plus a UX decision)
**Priority:** P2 — real gap, but no diagnostic has caused a reported field issue yet.
**Depends on:** Nothing blocking.

---

### TODO-041: Extract `demos/shared/demo_ble.c/h` BLE GATT helper
**What:** Extract the NimBLE GATT server boilerplate from `demo05/main/main.cpp` into a shared `demos/shared/demo_ble.c/h` helper, mirroring `demo_tcp.c/h`. The helper owns: service table registration, GAP event handling (connect/disconnect), advertising setup, send callback wiring. Each demo's main file would contain only uDisplay API calls and application logic.
**Why:** When TODO-012 (multimeter ESP32 demo) is implemented, it will need identical NimBLE GATT boilerplate. Without extraction, two copies of ~200-line setup code will diverge and debugging requires diffing both.
**Pros:** DRY — each BLE demo's main.cpp is ~50 lines of application logic, not ~250 lines of BLE plumbing. Mirrors the `demo_tcp.c/h` abstraction pattern already established.
**Cons:** Extraction adds a header design step (what does `demo_ble_hooks_t` look like?). Rule of three: don't extract until the second BLE demo starts.
**Context:** Identified during /plan-eng-review of demo05. demo05 is the first BLE demo; `demo_tcp.c/h` was extracted after the TCP demos established the pattern. Extract when TODO-012 development begins, not before.
**Effort:** S (human: ~3 hours / CC: ~10 min)
**Depends on:** demo05 (working BLE demo as extraction source); TODO-012 start (trigger condition).
**Priority:** P3 — deferred until second BLE demo starts

---

### TODO-042: Cross-device BLE subscribe-timing verification (Android OEM / iOS)
**What:** Verify `BLE_GAP_EVENT_SUBSCRIBE` fires reliably and promptly across real Android OEM BLE stacks and iOS CoreBluetooth, once more than one physical device is available for testing.
**Why:** demo05's HANDSHAKE-drop fix (this session) moved `udisplay_on_connect()` from `BLE_GAP_EVENT_CONNECT` to `BLE_GAP_EVENT_SUBSCRIBE`, assuming standard NimBLE/CCCD semantics on the central side. The original office-hours design doc's Known Risks section already flags "Qt Bluetooth Android fragmentation: BLE behavior varies across Android versions and OEM stacks" — OEM stacks are the documented risk category most likely to deviate from the assumption this fix relies on.
**Pros:** Catches a stack-specific regression before it reaches users. Gives TODO-014's bootstrap timeout a concrete tuning input, mirroring TODO-030's own precedent of deferring timeout tuning to "first real-device testing."
**Cons:** Needs multiple physical Android devices across OEMs plus an iOS device — not doable without hardware access.
**Context:** Identified during /plan-eng-review of the demo05 HANDSHAKE-drop fix. The subscribe-gated fix and the bootstrap timeout (TODO-014) are the mitigation; this TODO is the verification step that confirms the mitigation holds on hardware this project hasn't tested yet.
**Effort:** S (human: ~4h across devices / CC: N/A — requires physical hardware)
**Depends on:** demo05 subscribe-gating fix (this session) landing first; access to multiple physical Android/iOS test devices.
**Priority:** P3 — informational until real-device drift is observed, same tier as TODO-030's tuning note.

---

### TODO-043: Expose auth_algo/auth_check/fill_random through the generated udisplay_ui_init API
**What:** Extend `udisplay_ui_init`/`UDisplay::init` (or a follow-on `udisplay_ui_init_auth` variant) so firmware using the generated codegen API can enable HMAC-SHA256 auth (`UDISPLAY_AUTH_HMAC_SHA256`) without dropping to the raw `udisplay_init`/`udisplay_config_t` API.
**Why:** proto 0x04 auth is fully implemented in `libudisplay` and TODO-029 tracks the Qt client's UI wiring, but there's no codegen-side story for firmware authors who want auth — they'd have to bypass `udisplay_ui_init` entirely, defeating the point of "the preferred way of using the framework."
**Pros:** Closes the last remaining gap between the raw API's capability and the generated API's capability; keeps auth-enabled firmware inside the `ui_` namespace like everything else.
**Cons:** Needs a design decision on shape (extra params vs. an opts struct — same fork as this session's transport-param decision) and touches both codegen backends again; no current demo needs it, so there's no concrete consumer driving urgency.
**Context:** Surfaced during /plan-eng-review of the demo05 udisplay_ui_init transport-param fix. Zero-initializing `cfg` in that fix made the auth-field omission safe-by-default (defaults to `UDISPLAY_AUTH_NONE`) instead of undefined behavior; the omission itself — no way to opt in via generated code — remains.
**Effort:** M (human: ~3h / CC: ~30min)
**Depends on:** This session's shared cfg-init helper (`_shared.py`) landing first, since auth params would extend that same helper.
**Priority:** P3 — no current demo consumer; revisit when a demo needs auth or TODO-029 lands client-side.

---

### TODO-044: `SUPPORTED_TYPES` hand-sync in udisplay-gen/validate.py
**What:** `udisplay_gen/validate.py:19-24` hand-maintains a `SUPPORTED_TYPES` list used in
validation error messages, with a comment admitting it must be "kept in sync with schema
oneOf." Derive it from the schema's `oneOf` branches (or the `type` const values under
each widget/container `$defs` entry) instead of hand-listing them.
**Why:** Same bug shape as TODO-035 (two hand-synced representations of the same set of
widget types, no test catching drift) — spotted as an adjacent finding while reviewing
TODO-035's schema-file consolidation, deliberately not pulled into that PR's scope.
**Pros:** Kills the same drift-bug class for a second artifact. Once schema-introspection
exists it's cheap to keep current — someone adding a widget type to the schema can't
forget this list, because there's no separate list to forget.
**Cons:** Needs its own small investigation into extracting oneOf branch `const` values
cleanly via the `jsonschema` library (or a plain JSON walk, matching this repo's existing
style — see the ad-hoc walk used to enumerate `debug_state` locations during TODO-035's review).
**Context:** Lower blast radius than TODO-035 — this list only feeds human-readable
validation error message text, not actual accept/reject validation logic, so a stale entry
degrades an error message rather than silently accepting or rejecting the wrong YAML.
**Effort:** S (human: ~1-2h / CC: ~20min)
**Priority:** P3 — real cleanup opportunity, no reported issue yet, lower blast radius than TODO-035.
**Depends on:** Nothing blocking.

---

### TODO-046: onBootstrapSucceeded YAML-parse-failure leaves transport connected, no watchdog
**What:** Decide and implement the right recovery behavior when a real device sends YAML
that fails to parse. `DeviceController::onBootstrapSucceeded` sets state to `"error"` but
never calls `teardown()` on parse failure, and the heartbeat watchdog never starts (it only
starts after a full successful bootstrap) — so the transport stays open indefinitely with
nothing monitoring it.
**Why:** A single malformed YAML push (firmware bug, corrupted blob, bad OTA update) leaves
the app connected-but-frozen with no user-visible way back except manually calling
`disconnectDevice()` — worse than the capability-rejection path, which already calls
`teardown()` and returns to a clean `"error"` state.
**Pros:** Closes a real field-reliability gap; makes recovery behavior consistent across all
bootstrap failure modes instead of leaving one silently different.
**Cons:** Needs a product decision, not just a one-line fix — should parse failure also
`teardown()` like capability rejection, should it retry, or should a shorter watchdog detect
a stuck connection?
**Context:** Found while grounding the capability-rejection-vs-parse-failure
asymmetry that TODO-034's refactor is required to preserve exactly (see TODO-034's Architecture
Decision D1). Pre-existing behavior, not introduced by TODO-034.
**Effort:** S (human: ~2-3h / CC: ~20min, after a decision on target behavior)
**Priority:** P2
**Depends on:** Nothing blocking. Sequence after TODO-034 lands (same code area).

---

### TODO-048: demo05 BUTTON_GPIO dead code + misleading README smoke-test step
**What:** `demos/demo05/main/main.cpp:31` defines `BUTTON_GPIO` (`GPIO_NUM_9`) but never
references it anywhere else in the file — dead code. The LED actually toggles from the
BLE-driven virtual `push_btn` widget (`ui.push_btn.on_click`, client-app-initiated), not a
physical GPIO button press. The README's manual smoke-test step 3 says "press the physical
button," which doesn't match what the firmware does — someone following it literally
presses a button wired to nothing.
**Why:** Dead code plus a smoke-test instruction that doesn't match reality risks wasted
debugging time for anyone following the README, and the unused `#define` implies unfinished
work that was never resolved one way or the other.
**Pros:** Cheap correctness fix (remove dead code) or completes a half-finished feature
(actually wire up a physical button) — either way, closes a real doc/code mismatch.
**Cons:** Needs a product decision first — remove `BUTTON_GPIO` and fix the README wording,
or actually implement physical-button GPIO handling (polling or ISR) to match what the
README already claims. Not a mechanical fix until that's decided.
**Context:** Found during `/office-hours` + `/plan-eng-review` of demo05's
board-specific LED config design. Explicitly scoped out of that design (LED-only) to avoid
turning a config-portability task into also completing a half-finished feature.
**Effort:** XS-S (human: ~30min-2h / CC: ~10-20min, depending on which direction is chosen)
**Priority:** P3
**Depends on:** Nothing blocking.

---

### TODO-LIC-01: CI license header enforcement
**What:** A GitHub Actions workflow that verifies every source file has an SPDX-License-Identifier header. Either `reuse lint` (FSFE REUSE tool) or a simple grep step.
**Why:** Without enforcement, files added after this PR silently ship without headers. The plan adds headers to 72 files; CI is what keeps it that way.
**Pros:** Zero-cost regression gate once CI exists. A 5-line grep catches the failure immediately.
**Cons:** Requires setting up GitHub Actions first (no `.github/` directory exists).
**Context:** Pick up when CI is first introduced to the repo. The `reuse` tool at https://reuse.software is the FSFE standard — a 10-minute setup that runs as a GitHub Action. Alternatively: `grep -rL SPDX-License-Identifier -- '*.c' '*.h' '*.cpp' '*.py' '*.qml' | xargs -r false`.
**Effort:** S (human: ~1h / CC: ~10min)
**Depends on:** GitHub Actions CI setup.

---

### TODO-LIC-02: Contributor License Agreement (CLA) or Developer Certificate of Origin (DCO)
**What:** A contributor policy clarifying the rights granted when someone submits a PR. DCO is the simplest option: add a `CONTRIBUTING.md` with the DCO policy and a `.github/PULL_REQUEST_TEMPLATE.md` prompting `Signed-off-by:` in commits.
**Why:** Without a policy, if a contributor later disputes their contribution, you're in a gray legal area. The DCO (Linux kernel approach) provides clear, low-friction protection.
**Pros:** Legal clarity for accepting external contributions. DCO requires no GitHub bot or paperwork.
**Cons:** Minor overhead. Low priority for a solo project; becomes important at first external contributor.
**Context:** The simplest path: `CONTRIBUTING.md` stating that contributions are made under the project's license (by component), with a DCO sign-off. Add `.github/PULL_REQUEST_TEMPLATE.md` with `Signed-off-by: Your Name <email>` reminder.
**Effort:** XS (human: ~30min / CC: ~5min)
**Depends on:** Nothing. Can be done any time.

## Completed

### TODO-045: "capability check is a no-op" false claim — in TODOS.md AND in the schema
**What:** Correct every copy of the false "capability check is a no-op" claim. Two
independent copies existed: TODOS.md's TODO-022 status line, and
`udisplay.schema.json`'s `capabilities` property description (both claimed the check is
a no-op; actually, with an empty `kKnownCapabilities`, it rejects every non-empty
`capabilities:` list — the opposite).
**Status:** ✅ DONE — TODO-022's status line corrected; `udisplay.schema.json`'s
`capabilities` description rewritten to state the actual (reject-by-default) behavior
and to tell YAML authors to omit the field until the client ships a known-capabilities
list.
**Depends on:** Nothing blocking.

---

### TODO-049: Verify Android APK on a real device
**What:** Sideload the `.apk` produced by the `build-android` CI job onto a real Android
device and confirm it launches, and that mDNS/BLE discovery finds a uDisplay device.
**Why:** The design doc's Success Criteria requires real-device verification. CI is now
green ([run #30111643875](https://github.com/agasattila/udisplay/actions/runs/30111643875)
produced a real 24.5 MB APK) — was the only remaining verification step before the
Android CI job could be considered fully done.
**Status:** ✅ DONE — verified on a real Android device.
**Depends on:** Nothing else — `build-android` is green, artifact is ready to download.

---

### TODO-001: Merkle spec document (`docs/merkle.md`)
**What:** A single shared specification that both the Python codegen AND the Qt C++ client implement from. Exact byte layout, hash computation algorithm, chunk size parameterization, last-chunk padding rules.
**Why:** Two independent Merkle implementations without a shared spec will diverge on edge cases.
**Status:** ✅ DONE — `docs/merkle.md` exists in the repo.
**Depends on:** Nothing — this is the foundation.
