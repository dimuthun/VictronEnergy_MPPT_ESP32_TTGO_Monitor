# Architecture

## Overview

The monitor reads live data from a Victron SmartSolar MPPT via **BLE advertising** (no GATT connection). The controller broadcasts encrypted manufacturer data; the ESP32 scans, decrypts with the AES key from VictronConnect, parses the payload, and shows values on the TTGO T-Display.

## Data flow

```
Victron SmartSolar (BLE advertisements)
         │
         ▼
   BLE scan (BLEScan)
         │
         ▼
   Filter: Vendor 0x02e1, Record type 0x01 (Solar Charger)
         │
         ▼
   Check encryptKeyMatch == key[0]
         │
         ▼
   AES-CTR decrypt (mbedtls, 16-byte nonce from packet)
         │
         ▼
   Parse victronPanelData_t (device state, V, I, power, yield, …)
         │
         ▼
   Validate (e.g. outputCurrentHi & 0xfe == 0xfe)
         │
         ▼
   Update display globals → drawDisplay() on next loop
```

## Components

- **BLE:** ESP32 `BLEDevice`, `BLEScan`, `BLEAdvertisedDevice`; callback copies manufacturer data with `memcpy`/`c_str()` to avoid truncation on `0x00`.
- **Crypto:** `mbedtls/aes.h` – AES-128-CTR; nonce = 16 bytes (2-byte counter from packet, little-endian + zero padding).
- **Display:** TFT_eSPI (ST7789, 135×240); one screen with battery V/I, solar W, yield Wh, load A, state, error.
- **Config:** AES key from `config.h` (`VICTRON_AES_KEY_HEX`), parsed at startup.

## Display behaviour

- Refresh when a new valid BLE packet is decoded.
- After 10 s with no valid packet: show “No signal” once (no redraw every loop).
- Left button (GPIO 0): cycle backlight (off / low / mid / high).
