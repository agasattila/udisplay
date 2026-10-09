# Building uDisplay

This guide covers building every component of uDisplay from source on a vanilla Ubuntu system.
Tested on Ubuntu 24.04 LTS (Noble). Ubuntu 22.04 LTS works with the same packages unless noted.

## Contents

- [Quick start](#quick-start)
- [Top-level build and framework package](#top-level-build-and-framework-package)
  - [CI](#ci)
- [udisplay-client (Qt desktop app)](#udisplay-client-qt-desktop-app)
- [udisplay-gen (Python codegen)](#udisplay-gen-python-codegen)
- [libudisplay (firmware C library — host build)](#libudisplay-firmware-c-library--host-build)
- [TCP demos (demo01–demo03)](#tcp-demos-demo01demo03)
- [Building udisplay-client for Android](#building-udisplay-client-for-android)
- [ESP32 firmware demos (demo05, demo07)](#esp32-firmware-demos-demo05-demo07)

---

## Quick start

Install all dependencies for the desktop client and codegen in one shot:

```bash
sudo apt update
sudo apt install -y \
    build-essential cmake git pkg-config \
    qt6-base-dev qt6-declarative-dev \
    libqt6quickcontrols2-6 qml6-module-qtquick-controls \
    zlib1g-dev libavahi-client-dev \
    python3 python3-pip python3-venv
```

Then build:

```bash
git clone https://github.com/agasattila/udisplay.git
cd udisplay

# codegen (needed by demos; run first)
pip install ./udisplay-gen

# Qt desktop client
cmake -B udisplay-client/build udisplay-client
cmake --build udisplay-client/build -j$(nproc)
```

The binary lands at `udisplay-client/build/udisplay-client`.

---

## Top-level build and framework package

The `CMakeLists.txt` in the repository root builds every desktop component in one tree:
libudisplay, the TCP demos (demo01–demo03), udisplay-client, and all of their tests. It
also stages the **uDisplay framework** package: libudisplay, udisplay-gen and the demos as
sources, with the version patched in. CI runs the same configure, build, `ctest` and
`cmake --install --component framework` steps, so you can reproduce a release package locally: [`ci.yml`](../.github/workflows/ci.yml) configures and builds this
tree once, then runs the tests, packages the framework and bundles the client AppImage from
that same build tree without recompiling (see [CI](#ci)).

```bash
# udisplay-gen's Python dependencies (the build runs the in-tree udisplay-gen,
# not whatever is on PATH)
python3 -m venv .venv
.venv/bin/pip install -e "./udisplay-gen[dev]"

cmake -B build -DPython3_EXECUTABLE="$PWD/.venv/bin/python" -DUDISPLAY_VERSION=1.2.3
cmake --build build -j$(nproc)
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

# Framework install tree (CI tars this up as udisplay-framework-1.2.3.tar.gz) ...
cmake --install build --component framework --prefix stage/udisplay-framework-1.2.3
# ... or straight to build/package/udisplay-framework-1.2.3.tar.gz (cpack builds
# out-of-date targets first, like `make install`)
cpack --config build/CPackConfig.cmake
```

| Option | Default | Effect |
|---|---|---|
| `UDISPLAY_VERSION` | `0.0.0` | `MAJOR.MINOR.PATCH`, compiled into libudisplay's version macros (`libudisplay/udisplay.h`) and stamped into the packaged libudisplay header and udisplay-gen |
| `UDISPLAY_VERSION_FULL` | `UDISPLAY_VERSION` | Display version with an optional suffix (e.g. `1.2.3-rc1`); used in the package name and `VERSION` file |
| `UDISPLAY_BUILD_CLIENT` | `ON` | Build udisplay-client (needs Qt 6). Turn off for a Qt-free framework build |
| `UDISPLAY_BUILD_DEMOS` | `ON` | Build demo01–demo03 |
| `UDISPLAY_BUILD_TESTS` | `ON` | Build and register all unit tests (gtest, Qt tests, pytest) with CTest |

The package keeps the repository's relative layout, so the libudisplay and demo CMake
projects inside it build as-is and default to the packaged version:

```text
udisplay-framework-<version>/
├── VERSION, README.md, udisplay.schema.json, LICENSES/
├── cmake/          version helpers (+ the pinned package version)
├── libudisplay/    sources, without the test suite; include/libudisplay/udisplay.h carries the package version
├── udisplay-gen/   pip-installable: pip install ./udisplay-gen
└── demos/
```

udisplay-client is an end-user application (AppImage / APK, see [CI](#ci)), so it is
not part of the framework package. To install it from the top-level build anyway, use
`cmake --install build --component client`.

The ESP-IDF demos and the Android client are cross builds and stay out of the top-level
build; the per-component builds below keep working standalone.

### CI

[`ci.yml`](../.github/workflows/ci.yml) runs on every push to `main`, every `v*` tag,
every pull request, and on demand (Run workflow in the Actions tab). It compiles the desktop components once and calls the other workflows
in `.github/workflows/` as reusable workflows:

```text
ci.yml: Desktop build-all (top-level CMake build, client included)
   │
   ▼
test.yml            ctest on the build tree
   │
   ├─► build-appimage.yml   cmake --install --component client -> tar.gz + AppImage
   ├─► build-framework.yml  cmake --install --component framework -> udisplay-framework-<version>.tar.gz
   └─► build-android.yml    own Android cross build -> arm64-v8a debug APK
```

The build tree is passed to the downstream jobs as an artifact, so tests and packaging
reuse the same binaries instead of recompiling. The jobs that need the desktop toolchain
(Qt, apt packages, udisplay-gen venv) share it through the
[`setup-desktop`](../.github/actions/setup-desktop/action.yml) composite action. Tags `vX.Y.Z[-suffix]` set
`UDISPLAY_VERSION`/`UDISPLAY_VERSION_FULL`; other `v*` tags fail CI, and any other ref
builds as `0.0.0-<short sha>`. Once all packaging jobs pass, a tag push creates a GitHub
Release with the client archive and the framework package.

---

## udisplay-client (Qt desktop app)

### System requirements

| Requirement | Minimum | Notes |
|---|---|---|
| Ubuntu | 22.04 LTS | 24.04 LTS recommended |
| CMake | 3.21 | Ubuntu 22.04 ships 3.22; 24.04 ships 3.28 |
| GCC / Clang | GCC 11 / Clang 14 | C++17 required |
| Qt | 6.4 | Qt 6.4.2 available in Ubuntu 24.04 apt |

### Packages

```bash
sudo apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    qt6-base-dev \
    qt6-declarative-dev \
    libqt6quickcontrols2-6 \
    qml6-module-qtquick-controls \
    zlib1g-dev \
    libavahi-client-dev \
    librsvg2-bin
```

Package breakdown:

| Package | Provides |
|---|---|
| `build-essential` | GCC, g++, make |
| `cmake` | Build system (>= 3.21 required) |
| `git` | FetchContent pulls QZeroConf and yaml-cpp at configure time |
| `pkg-config` | Avahi detection in CMake |
| `qt6-base-dev` | Qt6 Core, Network, Sql, Gui |
| `qt6-declarative-dev` | Qt6 Qml, Quick |
| `libqt6quickcontrols2-6` | Qt6 QuickControls2 runtime |
| `qml6-module-qtquick-controls` | QML imports for QuickControls2 |
| `zlib1g-dev` | Compression (Merkle bootstrap) |
| `libavahi-client-dev` | mDNS discovery (optional but recommended) |
| `librsvg2-bin` | `rsvg-convert`, rasterizes the app icon SVG at build time |

`libavahi-client-dev` is optional. Without it, mDNS auto-discovery is disabled at build
time. The app still works — devices must be added by IP address manually. Manual TCP
always works regardless.

`librsvg2-bin` is required — `scripts/gen_app_icons.py` fails the CMake configure step
with a clear error if `rsvg-convert` is missing.

### Build

```bash
cd udisplay-client
cmake -B build .
cmake --build build -j$(nproc)
```

To skip the unit tests (faster):

```bash
cmake -B build . -DUDISPLAY_CLIENT_BUILD_TESTS=OFF
cmake --build build -j$(nproc)
```

### Run

```bash
./build/udisplay-client
```

Design mode (preview a YAML file without a device):

```bash
./build/udisplay-client --design path/to/device.yaml
```

Check what you built:

```bash
./build/udisplay-client --version
```

The version is computed from git at configure time and also shown in a small label in
the app's top-right corner: an exact tag if `HEAD` is tagged (e.g. `v0.9.2`), otherwise
`v0.0.0-<short-hash>`. Building from a source tarball with no `.git` directory shows
`v0.0.0-unknown`, since there's no repository to query.

### Unit tests

```bash
cd build && ctest --output-on-failure
```

All 7 test suites run in under 1 second with no hardware required.

### Dual Qt notes

If your system has both Qt5 and Qt6 installed (e.g. `qmake` → Qt5, `qmake6` → Qt6),
the build already handles this correctly via `QT_DEFAULT_MAJOR_VERSION=6` in
`CMakeLists.txt`. You do not need to set any extra flags.

---

## udisplay-gen (Python codegen)

### Packages

```bash
sudo apt install -y python3 python3-pip python3-venv
```

Python 3.10 or later is required. Ubuntu 22.04 ships Python 3.10; 24.04 ships Python 3.12.

### Install

For development (editable install with test dependencies):

```bash
cd udisplay-gen
python3 -m venv .venv
source .venv/bin/activate
pip install -e ".[dev]"
```

For system-wide use (affects `cmake`-driven demo builds which call `udisplay-gen` from PATH):

```bash
pip install ./udisplay-gen
```

### Usage

```bash
# Validate a YAML file
udisplay-gen validate device.yaml

# Generate C code
udisplay-gen build device.yaml -o ./generated

# Generate C++ code (C++11 compatible)
udisplay-gen build device.yaml --lang cpp -o ./generated

# Generate C++ code with std::function handlers (C++14+)
udisplay-gen build device.yaml --lang cpp --modern -o ./generated

# Multi-instance firmware: two separately-generated outputs coexisting in one
# binary need distinct namespaces so their widget IDs, blob data, and
# bind/init helper don't collide at link time. Omit --namespace for the
# default unprefixed single-instance output.
udisplay-gen build ble_ui.yaml --namespace ble -o ./generated
udisplay-gen build wifi_ui.yaml --namespace wifi -o ./generated
```

`--namespace` must be a valid C identifier (letters, digits, underscore; cannot start
with a digit). It prefixes generated widget-ID macros and blob data arrays
(`WIDGET_ID_*` → `BLE_WIDGET_ID_*`, `UDISPLAY_MERKLE_ROOT` → `BLE_UDISPLAY_MERKLE_ROOT`)
and the bind/init surface (`udisplay_ui_init` → `udisplay_ble_ui_init`, and for
`--lang cpp`, `namespace udisplay_ui` → `namespace udisplay_ble_ui`) — everything else
(the core `udisplay_*` functions in `libudisplay/udisplay.h`) takes the same `udisplay_t* ctx` for
every instance regardless of namespace.

### Run tests

```bash
cd udisplay-gen
source .venv/bin/activate
pytest
```

---

## libudisplay (firmware C library — host build)

`libudisplay` is the C library that runs on the firmware side. The host build compiles it
as a static library for running unit tests on the development machine — you do not need an
ESP32 for this.

### Packages

```bash
sudo apt install -y build-essential cmake git
```

No additional packages. The test suite uses GoogleTest fetched by CMake at configure time.

### Build and test

```bash
cd libudisplay
cmake -B build .
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### Using libudisplay

The public header lives under a `libudisplay/` prefix: the include directory is
`libudisplay/include`, and firmware includes

```c
#include "libudisplay/udisplay.h"
```

That header also defines the version macros (`UDISPLAY_VERSION_MAJOR`/`_MINOR`/`_PATCH`,
`UDISPLAY_VERSION_STRING`, `UDISPLAY_VERSION` and `UDISPLAY_VERSION_ENCODE()`); there is no
separate version header. In a framework package the header carries the release version; in
a git checkout it defaults to `0.0.0`. A CMake build (host or ESP-IDF) also passes the
configured `UDISPLAY_VERSION`/`UDISPLAY_VERSION_FULL` to libudisplay and its consumers as
compile definitions, which take precedence over the header defaults.

---

## TCP demos (demo01–demo03)

The TCP demos run on the host PC and emulate a device over a local TCP connection. They
let you develop and test `udisplay-client` without any hardware.

### Prerequisites

`udisplay-gen` must be installed and on PATH (see above).

### Packages

```bash
sudo apt install -y build-essential cmake git python3 python3-pip
pip install ./udisplay-gen
```

### Build

Each demo can be built standalone:

```bash
cd demos/demo01
cmake -B build .
cmake --build build -j$(nproc)
```

Or build all demos from the parent:

```bash
cd demos
cmake -B build .
cmake --build build -j$(nproc)
```

### Run

```bash
./demos/demo01/build/demo01   # C output demo
./demos/demo02/build/demo02   # C++ output demo
./demos/demo03/build/demo03   # C++ modern (std::function) demo
```

Each demo listens on `localhost:5555`. Connect `udisplay-client` to `localhost` port `5555`
using the manual TCP entry on the discovery screen.

---

## Building udisplay-client for Android

`udisplay-client` builds and runs on Android, including BLE — this has been built and
tested on a physical device. TCP-only was the original milestone; BLE has since shipped
via `Qt6::Bluetooth` and is the primary field connection path on Android (WiFi/TCP still
works for desktop-style development).

CMakeLists.txt already handles the Android-specific wiring: it detects `ANDROID`,
enables `Qt6::Bluetooth` and mDNS via Android NSD/JNI automatically, and copies
`udisplay-client/android/AndroidManifest.xml` (which declares the BLE and location
permissions Android requires for scanning) into the package source dir at configure
time. You do not need to edit `CMakeLists.txt` or the manifest to build for Android.

The manifest only *declares* those permissions; Android still requires a runtime
grant. `udisplay-client` requests Bluetooth and location permissions at point of
use — when the discovery screen starts a BLE scan (`BleScanner::startScan()`), not
at app launch — so the OS prompt appears the first time you actually try to
discover a device. If you deny either permission, the discovery screen shows an
actionable error instead of silently returning an empty device list.

Location must be granted as **precise**, on every API level. On Android 12+ this is
a uDisplay requirement rather than a general Android one: the manifest does not declare
`neverForLocation` (the Bluetooth address is the device's stable id), and without that
flag Android only delivers BLE scan results to apps holding `ACCESS_FINE_LOCATION`.
If the user grants only approximate location, the scan reports a permission error.

Android BLE builds require **Qt 6.6+** (`QBluetoothPermission::setCommunicationModes`);
CMake fails at configure time on older Android kits. Desktop builds keep the Qt 6.4
minimum. CI builds Android with Qt 6.11.

If you just want an APK to sideload instead of building locally, CI already builds one:
[`build-android.yml`](../.github/workflows/build-android.yml) produces an
arm64-v8a debug APK as a workflow artifact on every CI run whose tests pass (`main`, `v*`
tags, pull requests; Actions tab, artifact `udisplay-client-android-arm64-v8a-<sha>`
containing `uDisplay-<sha>-arm64-v8a-debug.apk`, retained 30 days). It's debug-signed for sideloading only — see `TODOS.md` for the
signing/distribution tradeoff. The manual steps below are for local development.

1. Install the Qt for Android toolchain from the Qt online installer (the apt packages do
   not include Android cross-compilation).
2. Install Android SDK and NDK via Android Studio or `sdkmanager`.
3. Configure the CMake Android toolchain:

```bash
cmake -B build-android . \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-26 \
    -DQt6_DIR=$QT_ANDROID/lib/cmake/Qt6
```

4. Build and deploy:

```bash
cmake --build build-android -j$(nproc)
# Deploy via Qt Creator or adb
```

Host-side prerequisites for the Android SDK/NDK toolchain:

```bash
sudo apt install -y \
    openjdk-17-jdk \
    android-sdk-build-tools \
    adb
```

---

## ESP32 firmware demos (demo05, demo07)

### demo05 — minimal ESP32 BLE demo

**Status: exists and works.** A button press toggles an LED; state is pushed to a
connected uDisplay client over BLE via NimBLE GATT indications. See
[`demos/demo05/README.md`](../demos/demo05/README.md) for supported ESP32 targets,
flashing, and the manual smoke-test checklist.

### demo07 — full v1 widget showcase on real ESP32 hardware

**Status: placeholder, blocked on hardware.** The BLE client and firmware plumbing
demo07 needs already exist (demo05 proves the same path end to end); what's missing is
real ESP32 hardware to design and validate demo07 against — no board selection or
wiring plan exists yet. See [`demos/demo07/README.md`](../demos/demo07/README.md).

### Prerequisites

Install ESP-IDF:

```bash
sudo apt install -y \
    git wget flex bison gperf \
    python3 python3-pip python3-venv \
    cmake ninja-build ccache \
    libffi-dev libssl-dev dfu-util \
    libusb-1.0-0-dev

# Install ESP-IDF v5.x
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
./install.sh all
source export.sh
```

### Build (demo05)

```bash
cd demos/demo05
idf.py set-target esp32        # or esp32c3, esp32s3, etc. — see demo05/README.md
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

`libudisplay` integrates as an ESP-IDF component via `idf_component_register` in
`libudisplay/CMakeLists.txt`. No separate library build step is needed — ESP-IDF picks it
up automatically.

### Required hardware

- An ESP32-family devkit — 5 targets supported (`esp32`, `esp32c3`, `esp32s3`,
  `esp32c6`, `esp32h2`); see [`demos/demo05/README.md`](../demos/demo05/README.md)
  for the per-board LED GPIO/type/polarity table (only ESP32-C3 Super Mini is
  hardware-verified today)
- USB cable for flashing
- A uDisplay client (desktop or Android) for BLE testing
