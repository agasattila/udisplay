# TODOS

## Infrastructure

### Verify Android APK on a real device after first CI run

**What:** Sideload the `.apk` produced by the new `build-android` CI job onto a real
Android device and confirm it launches, and that mDNS/BLE discovery finds a uDisplay
device.

**Why:** The design doc's Success Criteria requires real-device verification, but that's
inherently impossible before this ships — no APK exists until CI actually runs once
against real GitHub Actions infrastructure. Shipping now (with this follow-up tracked)
was the explicit call over blocking the ship on a verification that can't happen yet.

**Effort:** S
**Priority:** P1
**Depends on:** `build-android` job landing and completing at least one successful run.

### Release signing + Play Store distribution for udisplay-client Android APK

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

### Multi-ABI matrix build for udisplay-client Android APK

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

### Prebuilt Docker image for the Qt-Android CI toolchain

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

## Completed
