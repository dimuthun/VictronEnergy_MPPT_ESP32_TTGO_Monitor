# VictronEnergy_MPPT_ESP32_TTGO_Monitor

Displays live data from a Victron SmartSolar MPPT 100/20 (or compatible) on an ESP32 TTGO T-Display 1.14" by reading and decrypting BLE advertising packets.

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

1. In **VictronConnect**, pair your SmartSolar and open **Settings → Product info**.
2. Enable **Instant readout via Bluetooth** and tap **Show** in the Encryption data section.
3. Copy the 32-character hex key.
4. In this sketch, open **config.h** and replace `VICTRON_AES_KEY_HEX` with your key (or replace the `key[]` array in the main .ino if you use the byte array there).

## Usage

Upload the sketch, power the TTGO from USB or 5 V. Ensure the SmartSolar is powered and within BLE range. The display shows battery voltage, current, solar power, today’s yield, charge state, and optional error code. If no BLE packet is received for several seconds, "No signal" is shown.

**Display pages:** Use the **right button (GPIO 35)** to switch pages. Page 1 = Status (solar W, battery V/I, state, error, load state/current/power). Page 2 = Yield & info (today Wh, device name, "History: use Victron app", last update).

**Buttons:** Left button (GPIO 0) cycles backlight brightness (off / low / mid / high). Right button (GPIO 35) switches to the next display page.

**Limitations:** History (daily yield, Pmax, lifetime, etc.) and solar voltage/current are not available from BLE advertising; use the Victron app for those.

## References

- [Victron BLE advertising protocol](https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html)
- [hoberman/victron_ble_advertising_example](https://github.com/hoberman/victron_ble_advertising_example)
