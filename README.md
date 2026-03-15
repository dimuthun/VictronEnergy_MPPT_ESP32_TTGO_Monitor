# VictronEnergy_MPPT_ESP32_TTGO_Monitor

Displays live data from a Victron SmartSolar MPPT 100/20 (or compatible) on an ESP32 TTGO T-Display 1.14" by reading and decrypting BLE advertising packets. Firmware version 1.0.0 (see [CHANGELOG.md](CHANGELOG.md)).

## Hardware

- **ESP32** + **TTGO T-Display 1.14"** (135×240, ST7789)
- **Victron SmartSolar MPPT** with built-in Bluetooth (Instant readout enabled)

## Arduino IDE setup

1. **Board:** Install ESP32 by Espressif (Boards Manager). Select **Tools → Board → "LilyGo TTGO T-Display"** or "ESP32 Dev Module".
2. **Libraries:** **Sketch → Include Library → Manage Libraries** → install **TFT_eSPI**.
3. **TFT_eSPI for TTGO T-Display 1.14":**  
   Edit the TFT_eSPI library’s **User_Setup.h** (in your `Arduino/libraries/TFT_eSPI/` folder):
   - Open `User_Setup.h`.
   - Find the section for **LilyGo TTGO T-Display** or **T-Display 1.14** (e.g. search for "TTGO" or "135").
   - Uncomment the block for this board (comment out any other TFT driver block).
   - Typical settings: driver **ST7789**, 135×240, SCLK 18, MOSI 19, DC 16, RST 23, CS 5, BL 4.

## Victron encryption key

1. Copy **config.example.h** to **config.h** (e.g. `cp config.example.h config.h`). Do not commit **config.h** if it contains your real key (it is listed in .gitignore).
2. In **VictronConnect**, pair your SmartSolar and open **Settings → Product info**.
3. Enable **Instant readout via Bluetooth** and tap **Show** in the Encryption data section.
4. Copy the 32-character hex key and set it in **config.h** as `VICTRON_AES_KEY_HEX`. The key must be exactly 32 hex characters; if not, the display will show "Bad key" at startup.

## Usage

Upload the sketch, power the TTGO from USB or 5 V. Ensure the SmartSolar is powered and within BLE range. The display shows battery voltage, current, solar power, today’s yield, charge state, and optional error code. If no BLE packet is received for several seconds, "No signal" is shown.

**Display pages:** Three pages auto-rotate every 5 seconds when BLE signal is present: Page 1 = Status (state, error, load W). Page 2 = Yield (today Wh) and firmware version. Page 3 = Min/Max (solar, load, battery).

**Buttons:** Left button (GPIO 0) cycles backlight brightness (off / low / mid / high). Right button (GPIO 35) is reserved.

**Relay:** Optional relay on GPIO 25: turns ON when battery voltage ≥ 24 V and OFF when &lt; 23.8 V, with 15 s debounce to avoid chatter. Relay is forced OFF when there is no valid BLE data (fail-safe). Configure `RELAY_PIN`, `RELAY_ON_THRESHOLD_V`, `RELAY_OFF_THRESHOLD_V`, and `RELAY_DEBOUNCE_MS` in the .ino if needed.

**Limitations:** History (daily yield, Pmax, lifetime, etc.) and solar voltage/current are not available from BLE advertising; use the Victron app for those.

## Header logo

The header icon is defined in **logo.h** from `assets/logo_icon.png`. To use a different image: place your PNG in `assets/logo_icon.png`, then run `py scripts/png_to_1bit_logo.py` (requires Python 3 and Pillow) and paste the generated array into **logo.h**. Alternatively use [image2cpp](https://javl.github.io/image2cpp/) with "Horizontal 1 bit per pixel" and height 28.

## References

- [Victron BLE advertising protocol](https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html)
- [hoberman/victron_ble_advertising_example](https://github.com/hoberman/victron_ble_advertising_example)
