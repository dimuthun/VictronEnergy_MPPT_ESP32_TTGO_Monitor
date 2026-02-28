# Implementation notes

## File layout

| File | Purpose |
|------|--------|
| **VictronEnergy_MPPT_ESP32_TTGO_Monitor.ino** | Main sketch: includes, globals, Victron structs, BLE callback class, `parseHexKey`, `getStateString`, `drawDisplay`, `drawNoSignal`, `setup`, `loop`. |
| **config.h** | `VICTRON_AES_KEY_HEX` (32-char hex); do not commit real key if sharing. |
| **README.md** | User-facing setup and usage (root of project). |
| **docs/** | Architecture, setup, protocol, and this implementation context. |

## Key constants (.ino)

- `BLE_SCAN_TIME_SEC` = 1  
- `NO_SIGNAL_TIMEOUT_MS` = 10000  
- `TFT_BACKLIGHT_PIN` = 4, `BTN1_PIN` = 0, `BTN2_PIN` = 35  
- Backlight PWM: channel 0, 5 kHz, 8-bit; levels `{ 0, 80, 160, 255 }`

## Dependencies (Arduino ESP32)

- **BLE:** `BLEDevice.h`, `BLEScan.h`, `BLEAdvertisedDevice.h` (from board package).
- **AES:** `mbedtls/aes.h` (from board package; not `esp_aes.h`).
- **Display:** `TFT_eSPI.h` (install via Library Manager; configure User_Setup.h for TTGO T-Display 1.14").

## BLE callback

- Runs during `pBLEScan->start()`; only updates global display variables and `dataUpdated` / `lastPacketTime`.
- No TFT or Serial calls inside the callback to avoid re-entrancy and blocking.

## AES-CTR

- Nonce: 16 bytes = `[nonceDataCounter & 0xFF, (nonceDataCounter >> 8) & 0xFF, 0, …, 0]`.
- Decrypt up to 16 bytes of `victronEncryptedData`; result interpreted as `victronPanelData_t`.

## Display

- One screen; refresh only when `dataUpdated` is true (set in BLE callback).
- “No signal” drawn once when timeout expires; `noSignalShown` prevents redraw every loop.
