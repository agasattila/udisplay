# Building uDisplay

This guide covers building every component of uDisplay from source on a vanilla Ubuntu system.
Tested on Ubuntu 24.04 LTS (Noble). Ubuntu 22.04 LTS works with the same packages unless noted.

## Contents

- [Quick start](#quick-start)
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
git clone <repo-url> uDisplay
cd uDisplay

# codegen (needed by demos; run first)
pip install ./udisplay-gen

# Qt desktop client
cmake -B udisplay-client/build udisplay-client
cmake --build udisplay-client/build -j$(nproc)
```

The binary lands at `udisplay-client/build/udisplay-client`.

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
```

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
connected uDisplay client over BLE via NimBLE GATT notifications. See
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

- ESP32 development board (ESP32-DevKitC or compatible) — required for demo05 today
- USB cable for flashing
- A uDisplay client (desktop or Android) for BLE testing
