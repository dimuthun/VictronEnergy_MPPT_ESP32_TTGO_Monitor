/*
 * Victron SmartSolar MPPT - AES key for BLE Instant Readout
 *
 * Get your key from VictronConnect:
 *   Pair the SmartSolar -> Settings -> Product info -> Instant readout via Bluetooth -> Show
 *   Copy the 32-character hex string and convert to the byte array below.
 *
 * Example: if the key is "dc73cb155351cf950f9f3a958b5cd96f"
 * then: dc 73 cb 15 53 51 cf 95 0f 9f 3a 95 8b 5c d9 6f
 */
#ifndef CONFIG_H
#define CONFIG_H

// Replace with your Victron encryption key (32 hex chars -> 16 bytes)
// Example: "dc73cb155351cf950f9f3a958b5cd96f"
#define VICTRON_AES_KEY_HEX "7cc614fcf10a62170520e9fc5ad53ba7"

#endif
