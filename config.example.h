/*
 * Victron SmartSolar MPPT - AES key for BLE Instant Readout
 *
 * Setup:
 *   1. Copy this file to config.h:  cp config.example.h config.h
 *   2. Get your key from VictronConnect:
 *      Pair the SmartSolar -> Settings -> Product info -> Instant readout via Bluetooth -> Show
 *   3. Copy the 32-character hex string and replace the placeholder below in config.h.
 *
 * Do not commit config.h if it contains your real key (config.h is in .gitignore).
 */
#ifndef CONFIG_H
#define CONFIG_H

// Replace with your Victron encryption key (exactly 32 hex characters)
#define VICTRON_AES_KEY_HEX "00000000000000000000000000000000"

#endif
