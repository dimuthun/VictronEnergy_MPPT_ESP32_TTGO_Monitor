# Implementation notes

## File layout

| File | Purpose |
|------|--------|
| **VictronEnergy_MPPT_ESP32_TTGO_Monitor.ino** | Main sketch: includes, globals, Victron structs, BLE callback, `isKeyValid`, `parseHexKey`, `getStateString`, `drawPageStatus`, `drawPageYieldInfo`, `drawPageMinMax`, `drawDisplay`, `drawNoSignal`, `drawBadKeyScreen`, `setup`, `loop`. |
| **config.h** | `VICTRON_AES_KEY_HEX` (32-char hex). Copy from **config.example.h** and set your key; do not commit real key (config.h is in .gitignore). |
| **config.example.h** | Template with placeholder key; copy to config.h and replace with your Victron key. |
| **README.md** | User-facing setup and usage (root of project). |
| **docs/** | Architecture, setup, protocol, and this implementation context. |

## Key constants (.ino)

- `FW_VERSION` = "1.0.0" (shown on Yield page and at Serial boot)
- `DEBUG_VICTRON` = 0 (set to 1 to enable extra Serial debug output)
- `BLE_SCAN_TIME_SEC` = 1
- `NO_SIGNAL_TIMEOUT_MS` = 10000
- `TFT_BACKLIGHT_PIN` = 4, `BTN1_PIN` = 0, `BTN2_PIN` = 35
- `RELAY_PIN` = 25, `RELAY_ON_THRESHOLD_V` = 24.0, `RELAY_OFF_THRESHOLD_V` = 23.8, `RELAY_DEBOUNCE_MS` = 15000
- `NUM_PAGES` = 3 (Status, Yield, Min/Max)
- `PAGE_INTERVAL_MS` = 5000 (pages auto-rotate every 5 s when signal present)
- Backlight PWM: channel 0, 5 kHz, 8-bit; levels `{ 0, 80, 160, 255 }`

## Config and startup

- At startup the key in config.h is validated: must be exactly 32 hex characters. If invalid, `drawBadKeyScreen()` shows "Bad key" on the TFT and the sketch halts (no BLE init).

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

- **Multi-page:** Three pages. Page 0 = Status (State, Error, Load W). Page 1 = Yield (today Wh) + FW version. Page 2 = Min/Max (Solar, Load, Bat). Pages auto-rotate every 5 s when BLE data is valid.
- **BTN1 (GPIO 0):** Cycle backlight. **BTN2 (GPIO 35):** Not used for page switch (rotation is timer-based).
- Refresh when `dataUpdated` is true (set in BLE callback) or when the page timer triggers a switch.
- "No signal" drawn once when timeout expires; `noSignalShown` prevents redraw every loop.

## Relay

- **Pin:** GPIO 25 (configurable; use 12, 13, 21, 22, 27 if 25 unavailable).
- **Logic:** ON when battery voltage ≥ `RELAY_ON_THRESHOLD_V` (24 V), OFF when &lt; `RELAY_OFF_THRESHOLD_V` (23.8 V); hysteresis prevents chatter.
- **Debounce:** Voltage must hold for `RELAY_DEBOUNCE_MS` (15 s) before relay state changes.
- **Fail-safe:** Relay is forced OFF immediately when there is no valid BLE data (no packet or stale &gt; 10 s).

## Limitations

- **History** (daily yield, Pmax, Vmax, consumption, lifetime, since reset) is not in the BLE advertising payload; use the Victron app for full history.
- **Solar voltage/current** are not in the parsed BLE payload; only solar power (W) is shown.
