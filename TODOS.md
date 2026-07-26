# TODOS

## Infrastructure

### 1. Verify Android APK on a real device

**What:** Sideload the `.apk` produced by the `build-android` CI job onto a real Android
device and confirm it launches, and that mDNS/BLE discovery finds a uDisplay device.

**Why:** The design doc's Success Criteria requires real-device verification. CI is now
green ([run #30111643875](https://github.com/agasattila/udisplay/actions/runs/30111643875)
produced a real 24.5 MB APK) — this is now the only remaining verification step before
the Android CI job can be considered fully done. It genuinely couldn't happen earlier:
no APK existed until CI ran for the first time, and getting to green took 4 iteration
fixes (see PR #3 for the full list — a wrong Qt path, a missing CMake install rule, an
unimported Qt6::Bluetooth target, and an unsigned release build type).

**Effort:** S
**Priority:** P1
**Depends on:** Nothing else — `build-android` is green, artifact is ready to download.

### 2. Release signing + Play Store distribution for udisplay-client Android APK

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

### 3. Multi-ABI matrix build for udisplay-client Android APK

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

### 4. Prebuilt Docker image for the Qt-Android CI toolchain

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

## Widgets

### 5. Real device-authoritative setter for `button-group`

**What:** Give `button-group` a real setter for its exclusive-select state, mirroring
`dropdown`'s existing `OutputWidget<uint8_t>` + `.set()` pattern
(`udisplay-gen/udisplay_gen/backends/cpp_backend.py:214-235`), and a round-trip contract
test (firmware `.set()` → STATE_UPDATE → QML `value` → visibly-selected item).

**Why:** Investigated during the `/office-hours` + `/plan-eng-review` session that split
`dpad` out of `button-group` (see
`~/.gstack/projects/agasattila-udisplay/prog-main-design-20260726-204340.md`).
`button-group`'s only reason to exist as a distinct widget type — versus just placing
several `button`s in a grid — is exclusive-select semantics. That's currently
unimplemented: `cpp_backend.py:196-212` generates a bare `Widget` with no `.set()` (unlike
`dropdown`), `docs/widgets.md:484-573` documents "no setter for the group," and none of
demo01/02/03 (the only real usages) ever report a selected value back from firmware. In
every real deployment today, every `button-group` item is permanently stuck
"unselected" — the selection highlight the QML styling is built around never fires. This
is exactly how it shipped unnoticed across three demos: no round-trip test exists for
any stateful widget today, so nothing would have caught it.

**Context:** The dpad-split design deliberately deferred this — same reasoning as the
Play Store signing precedent (#2 above): don't build speculative protocol surface
without a concrete firmware consumer asking for it. **Note the tension explicitly**
(flagged by `/plan-eng-review`'s outside voice, Codex): the design argues
exclusive-select is `button-group`'s only reason to exist, then ships the taxonomy split
while leaving exactly that nonfunctional. Deliberately accepted, not silently dropped —
this TODO is how it stays tracked. Smallest correct implementation (per Codex's
cold-read in the design session): start with a failing round-trip test, then copy the
`dropdown` pattern almost literally — `OutputWidget<uint8_t>` instead of bare `Widget`,
generate `.set(itemId)` pushing STATE_UPDATE, generate symbolic item-value constants
(e.g. `mode.fast`) so firmware code doesn't hardcode numeric IDs, stay
device-authoritative (no optimistic flip — selection only changes after the
STATE_UPDATE round-trips back).

**Effort:** M
**Priority:** P3
**Depends on:** The `dpad` split (PR2, see the design doc above) landing first, so
`button-group` is grid-only before its setter semantics are locked in. No concrete
firmware consumer requesting this yet — revisit when one exists.

### 6. Dedupe `CONTAINER_TYPES`/`DECORATION_TYPES` between `widget_ids.py` and `validate.py`

**What:** `udisplay-gen/udisplay_gen/widget_ids.py` and
`udisplay-gen/udisplay_gen/validate.py` each independently define their own copies of
`CONTAINER_TYPES` (`{"section", "row", "grid"}`) and a "no ID"/"decoration" types set.
Share one definition instead of two.

**Why:** Noticed during the dpad-split `/plan-eng-review` (Code Quality section, see
`~/.gstack/projects/agasattila-udisplay/prog-main-design-20260726-204340.md`) — two
independently-maintained copies of the same constant can silently drift, which is
exactly the class of bug (a hardcoded gate quietly going stale) that motivated the whole
dpad-split session in the first place (see TODO #5 above and the design doc's
Problem Statement).

**Context:** Not an active bug today — both copies are currently in sync. Preventive
cleanup: move both sets into one shared module both `widget_ids.py` and `validate.py`
import from.

**Effort:** S
**Priority:** P4
**Depends on:** None.

## Completed
