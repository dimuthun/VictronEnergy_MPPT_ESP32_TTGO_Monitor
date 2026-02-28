# Setup

## Hardware

- **Board:** ESP32 + **TTGO T-Display 1.14"** (135×240, ST7789)
- **Pins:** SCLK 18, MOSI 19, DC 16, RST 23, CS 5, BL 4; buttons GPIO 0, 35
- **Victron:** SmartSolar MPPT with built-in Bluetooth (Instant readout enabled)

## Arduino IDE

1. **Board package:** Install **ESP32 by Espressif** (Tools → Board → Boards Manager).
2. **Board selection:** **Tools → Board → "LilyGo TTGO T-Display"** or "ESP32 Dev Module".
3. **Library:** **Sketch → Include Library → Manage Libraries** → install **TFT_eSPI**.

## TFT_eSPI (TTGO T-Display 1.14")

Edit the library’s **User_Setup.h** (e.g. `Arduino/libraries/TFT_eSPI/User_Setup.h`):

- Find the section for **LilyGo TTGO T-Display** or **T-Display 1.14** (search for "TTGO" or "135").
- Uncomment that block; comment out any other TFT driver block.
- Typical: driver **ST7789**, 135×240, SCLK 18, MOSI 19, DC 16, RST 23, CS 5, BL 4.

## Victron encryption key

1. In **VictronConnect**, pair the SmartSolar and open **Settings → Product info**.
2. Enable **Instant readout via Bluetooth** and tap **Show** in the Encryption data section.
3. Copy the 32-character hex key.
4. In this project, open **config.h** and set `VICTRON_AES_KEY_HEX` to that string (e.g. `"dc73cb155351cf950f9f3a958b5cd96f"`).

## Usage

- Upload the sketch; power the TTGO from USB or 5 V.
- Keep the SmartSolar powered and in BLE range.
- Display shows battery V, battery I, solar W, today Wh, load A, charge state, and error (if any). After ~10 s with no valid packet, “No signal” is shown.
- **Left button (GPIO 0):** cycle backlight brightness.
