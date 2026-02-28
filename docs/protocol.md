# Victron BLE protocol (context)

## Source

- Victron **Instant readout via Bluetooth**: BLE advertising with manufacturer-specific data.
- Encrypted with **AES-128-CTR**; key from VictronConnect (Product info → Encryption data → Show).
- Official doc: [Victron BLE advertising protocol](https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html).  
- Extra manufacturer data layout: [extra-manufacturer-data PDF](https://community.victronenergy.com/storage/attachments/48745-extra-manufacturer-data-2022-12-14.pdf).

## Filtering

- **Vendor ID:** `0x02e1` (Victron).
- **Record type:** `0x01` = Solar Charger (this project only parses this type).
- **encryptKeyMatch:** must equal first byte of your 16-byte AES key.

## Manufacturer data structure (packed)

```c
typedef struct {
  uint16_t vendorID;           // 0x02e1
  uint8_t  beaconType;
  uint8_t  unknownData1[3];
  uint8_t  victronRecordType;  // 0x01 = Solar Charger
  uint16_t nonceDataCounter;   // nonce for AES-CTR
  uint8_t  encryptKeyMatch;   // must == key[0]
  uint8_t  victronEncryptedData[21];
} victronManufacturerData_t;
```

Header length = 10 bytes; encrypted payload = manufacturer data length − 10 (decrypt first 16 bytes for the panel struct).

## Decrypted panel data (packed)

```c
typedef struct {
  uint8_t  deviceState;
  uint8_t  errorCode;
  int16_t  batteryVoltage;    // × 0.01 → V
  int16_t  batteryCurrent;   // × 0.1  → A
  uint16_t todayYield;       // × 0.01 × 1000 → Wh
  uint16_t inputPower;       // W
  uint8_t  outputCurrentLo;  // low 8 bits of 9-bit value
  uint8_t  outputCurrentHi;  // bit 0 = MSB; bits 1–7 unused (0xfe check)
  uint8_t  unused[4];
} victronPanelData_t;
```

## Scaling

| Field           | Raw type  | Scale        | Unit |
|----------------|-----------|-------------|------|
| batteryVoltage | int16_t   | × 0.01      | V    |
| batteryCurrent | int16_t   | × 0.1       | A    |
| todayYield     | uint16_t  | × 0.01 × 1000 | Wh   |
| inputPower     | uint16_t  | 1           | W    |
| output current | 9-bit (Hi<<8\|Lo) | × 0.1 | A    |

## Device state (display)

- 0 = Off, 3 = Bulk, 4 = Absorption, 5 = Float, 7 = Equalize; others shown as "---".

## Validation

- Require `(outputCurrentHi & 0xfe) == 0xfe` to drop corrupted decodes (as in hoberman’s example).

## References

- [hoberman/victron_ble_advertising_example](https://github.com/hoberman/victron_ble_advertising_example) (Arduino BLE + decrypt + structs)
- [Victron BLE advertising protocol](https://community.victronenergy.com/questions/187303/victron-bluetooth-advertising-protocol.html)
