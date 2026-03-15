# Changelog

## [1.0.0] – Production release

- **Config:** Added `config.example.h` and key validation at startup; invalid key (not 32 hex chars) shows "Bad key" on TFT. `config.h` is in .gitignore.
- **Versioning:** Firmware version `FW_VERSION` (1.0.0) shown on Serial at boot and on the Yield page. `DEBUG_VICTRON` flag for optional debug output.
- **Relay:** Voltage-based control with hysteresis and 15 s debounce; fail-safe OFF when no BLE data.
- **Display:** Three pages (Status, Yield, Min/Max), auto-rotate every 5 s; header (Solar W + logo), footer (Battery V/A/W).
- **Docs:** README relay section; implementation.md aligned with code; this changelog.
