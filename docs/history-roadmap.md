## Project History

Development of **uDisplay** began in early 2025 in a private self-hosted Git repository. Between 2025 and the summer of 2026, the project went through several iterations while the overall scope, communication protocol, and initial widget set were refined.

In early 2026, I started using agentic coding extensively. It proved to be a very effective workflow, significantly accelerating development. My role gradually shifted toward software architecture, design decisions, code reviews, and integration, while I continued to handle complex debugging and critical bug fixes personally instead of spending time on straightforward implementation work.

This increase in productivity also allowed the project scope to expand. The most significant architectural improvements were the redesign of the widget model to support more sophisticated layouts and the introduction of authentication capabilities into the protocol specification.

As a result, the project roadmap now includes several ambitious long-term goals that appear achievable within a realistic development timeline.

---

## Versioning

The current development branch will become **v0.9**.

Subsequent releases will follow the **v0.9.x** versioning scheme until **v1.0.0** is reached. Version **1.0** marks the first stable release, where the protocol specification and the initial widget set will be considered frozen. New widgets and backward-compatible features may be introduced in subsequent **v1.x** releases.

The **v2.x** series is planned to introduce an MVVM architecture together with WebView and IPFS integration. Although the v1 client will eventually become obsolete, the v2 client is planned to remain backward compatible with v1 firmware.

---

## Project Status

| Component | Status | Notes |
|-----------|--------|-------|
| **libudisplay** | ✅ Ready | |
| **udisplay-gen** | 🚧 Functional, not yet complete | MicroPython backend is still missing. |
| **udisplay-client** | 🚧 TCP/mDNS stable, BLE functional but still being stabilized | Current development focus. Extensive testing and bug fixing are in progress. |
| **demo01–03** | ✅ Ready | Simple Linux command-line TCP demonstrations. |
| **demo04** | ⏳ Planned | Depends on the MicroPython backend in `udisplay-gen`. |
| **demo05** | ✅ Ready | Simple BLE demonstration for ESP32. |
| **demo06** | ⏳ Planned | Additional BLE demonstration for ESP32. |

---

## Project Roadmap

### Release v0.9

- Initial widget set largely finalized
- Protocol specification largely finalized
- `udisplay-gen`
  - C backend
  - C++ backend
  - Modern C++ backend
- Demo applications
  - `demo01`–`demo03`
  - `demo05`
- `udisplay-client`
  - TCP transport/mDNS discovery
  - BLE transport
  - Linux x86_64 build (`.tar.gz`)
  - Android build (manual ADB installation)

### Release v1.0

- Stable protocol specification
- Widget set v1.0 finalized
- `udisplay-gen`
  - C backend
  - C++ backend
  - Modern C++ backend
  - MicroPython backend
- Demo applications
  - `demo01`–`demo07`
- `udisplay-client`
  - TCP transport/mDNS discovery
  - BLE transport
  - Linux x86_64 packages
    - `.tar.gz`
    - `.deb`
    - AppImage
  - Android release
    - F-Droid
    - Google Play (planned)

### Parallel Development

After the v1.0 release, development of the **v2.x** branch will begin while the **v1.x** branch continues to receive maintenance and feature updates.

Planned improvements for the v1.x series include:

- Frame Buffer widget
- Terminal widget
- Touchpad / vector control widget
- Additional widgets and quality-of-life improvements

An iOS and macOS port of **udisplay-client** is also planned during this period.

### Release v2.0

Everything available in v1, plus:

- MVVM architecture
  - YAML specifications
  - Code generator support
  - Transport support
  - Client-side support
- Embedded WebView support
- IPFS integration
