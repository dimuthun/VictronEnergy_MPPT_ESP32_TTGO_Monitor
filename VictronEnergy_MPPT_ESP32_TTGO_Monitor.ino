/*
 * VictronEnergy_MPPT_ESP32_TTGO_Monitor
 * Victron SmartSolar MPPT - ESP32 TTGO T-Display 1.14" Monitor
 *
 * Reads BLE advertising packets from Victron SmartSolar, decrypts with AES-CTR,
 * and displays battery V/I, solar power, yield, charge state on the TFT.
 *
 * BLE/decrypt logic based on: github.com/hoberman/victron_ble_advertising_example
 * Victron protocol: community.victronenergy.com (Instant readout via Bluetooth)
 */

#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "mbedtls/aes.h"
#include <TFT_eSPI.h>
#include "config.h"

// ESP32 BLE may return String or std::string; set to match your board package
#define USE_String

#define BLE_SCAN_TIME_SEC   1
#define NO_SIGNAL_TIMEOUT_MS 10000
#define TFT_BACKLIGHT_PIN   4
#define BTN1_PIN            0   // Left button (GPIO 0)
#define BTN2_PIN            35  // Right button (GPIO 35)
#define BACKLIGHT_CHANNEL   0
#define BACKLIGHT_FREQ      5000
#define BACKLIGHT_RES      8
static const uint8_t backlightLevels[] = { 0, 80, 160, 255 };
#define NUM_BACKLIGHT_LEVELS (sizeof(backlightLevels)/sizeof(backlightLevels[0]))
uint8_t backlightIndex = 2;  // default mid brightness

// --- Victron BLE manufacturer data structures (packed, no padding) ---
typedef struct {
  uint16_t vendorID;
  uint8_t  beaconType;
  uint8_t  unknownData1[3];
  uint8_t  victronRecordType;
  uint16_t nonceDataCounter;
  uint8_t  encryptKeyMatch;
  uint8_t  victronEncryptedData[21];
} __attribute__((packed)) victronManufacturerData_t;

typedef struct {
  uint8_t  deviceState;
  uint8_t  errorCode;
  int16_t  batteryVoltage;
  int16_t  batteryCurrent;
  uint16_t todayYield;
  uint16_t inputPower;
  uint8_t  outputCurrentLo;
  uint8_t  outputCurrentHi;
  uint8_t  unused[4];
} __attribute__((packed)) victronPanelData_t;

// AES key (filled from config in setup)
uint8_t key[16];
static const int keyBits = 128;

BLEScan* pBLEScan;
TFT_eSPI tft = TFT_eSPI();

// Last decoded values for display
float    dispBatteryV;
float    dispBatteryI;
uint16_t dispInputPowerW;
float    dispTodayYieldWh;
float    dispOutputCurrentA;
uint8_t  dispDeviceState;
uint8_t  dispErrorCode;
char     savedDeviceName[32];
bool     dataUpdated;
uint32_t lastPacketTime;

// --- Parse 32-char hex string into 16-byte key ---
static void parseHexKey(const char* hex, uint8_t* out) {
  auto hexChar = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  for (int i = 0; i < 16 && hex[i*2] && hex[i*2+1]; i++)
    out[i] = (hexChar(hex[i*2]) << 4) | hexChar(hex[i*2+1]);
}

// --- Charge state string for display ---
static const char* getStateString(uint8_t state) {
  switch (state) {
    case 0:  return "Off";
    case 3:  return "Bulk";
    case 4:  return "Absorption";
    case 5:  return "Float";
    case 7:  return "Equalize";
    default: return "---";
  }
}

// --- BLE advertised device callback: filter Victron, decrypt, parse ---
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveManufacturerData()) return;

#define MAN_DATA_MAX 31
    uint8_t manBuf[MAN_DATA_MAX + 1];

    // getManufacturerData() returns std::string (BLE library)
    std::string manData = advertisedDevice.getManufacturerData();
    int manLen = manData.length();
    if (manLen < 11 || manLen > MAN_DATA_MAX) return;

    manData.copy((char*)manBuf, manLen);

    victronManufacturerData_t* vic = (victronManufacturerData_t*)manBuf;
    if (vic->vendorID != 0x02e1) return;
    if (vic->victronRecordType != 0x01) return;  // Solar Charger

    if (advertisedDevice.haveName())
      strncpy(savedDeviceName, advertisedDevice.getName().c_str(), sizeof(savedDeviceName) - 1);
    savedDeviceName[sizeof(savedDeviceName)-1] = '\0';

    if (vic->encryptKeyMatch != key[0]) return;

    int encrLen = manLen - 10;
    if (encrLen > 16) encrLen = 16;
    uint8_t inputData[16];
    uint8_t outputData[16] = {0};
    for (int i = 0; i < encrLen; i++) inputData[i] = vic->victronEncryptedData[i];

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key, keyBits) != 0) {
      mbedtls_aes_free(&ctx);
      return;
    }

    uint8_t nonceCounter[16] = {
      (uint8_t)(vic->nonceDataCounter & 0xFF),
      (uint8_t)((vic->nonceDataCounter >> 8) & 0xFF),
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    uint8_t streamBlock[16] = {0};
    size_t nonceOffset = 0;
    if (mbedtls_aes_crypt_ctr(&ctx, encrLen, &nonceOffset, nonceCounter, streamBlock, inputData, outputData) != 0) {
      mbedtls_aes_free(&ctx);
      return;
    }
    mbedtls_aes_free(&ctx);

    victronPanelData_t* pd = (victronPanelData_t*)outputData;
    if ((pd->outputCurrentHi & 0xfe) != 0xfe) return;  // filter corrupted frames

    dispBatteryV       = (float)pd->batteryVoltage * 0.01f;
    dispBatteryI       = (float)pd->batteryCurrent * 0.1f;
    dispInputPowerW    = pd->inputPower;
    dispTodayYieldWh   = (float)pd->todayYield * 0.01f * 1000.0f;
    int outCurr9       = ((pd->outputCurrentHi & 0x01) << 8) | pd->outputCurrentLo;
    dispOutputCurrentA = (float)outCurr9 * 0.1f;
    dispDeviceState    = pd->deviceState;
    dispErrorCode      = pd->errorCode;
    dataUpdated        = true;
    lastPacketTime     = millis();
  }
};

// --- Draw one screen from current decoded values ---
void drawDisplay() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);

  tft.drawString("SmartSolar MPPT", 4, 4, 2);
  tft.drawString("----------------", 4, 22, 2);

  char buf[48];
  snprintf(buf, sizeof(buf), "Battery:  %6.2f V", (double)dispBatteryV);
  tft.drawString(buf, 4, 44, 2);
  snprintf(buf, sizeof(buf), "Current:  %6.2f A", (double)dispBatteryI);
  tft.drawString(buf, 4, 62, 2);
  snprintf(buf, sizeof(buf), "Solar:    %5u W", (unsigned)dispInputPowerW);
  tft.drawString(buf, 4, 80, 2);
  snprintf(buf, sizeof(buf), "Yield:    %6.0f Wh", (double)dispTodayYieldWh);
  tft.drawString(buf, 4, 98, 2);
  snprintf(buf, sizeof(buf), "Load:     %6.2f A", (double)dispOutputCurrentA);
  tft.drawString(buf, 4, 116, 2);
  snprintf(buf, sizeof(buf), "State:    %s", getStateString(dispDeviceState));
  tft.drawString(buf, 4, 134, 2);
  if (dispErrorCode != 0) {
    snprintf(buf, sizeof(buf), "Error:    %u", (unsigned)dispErrorCode);
    tft.drawString(buf, 4, 152, 2);
  }
  tft.drawString(savedDeviceName[0] ? savedDeviceName : "(no name)", 4, 180, 1);
}

void drawNoSignal() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("SmartSolar MPPT", 4, 4, 2);
  tft.drawString("----------------", 4, 22, 2);
  tft.drawString("No BLE signal", 4, 60, 2);
  tft.drawString("Check key & range", 4, 80, 2);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  parseHexKey(VICTRON_AES_KEY_HEX, key);

  strcpy(savedDeviceName, "(unknown)");
  dataUpdated = false;
  lastPacketTime = 0;

  // Backlight: PWM for brightness (Button 1 cycles levels)
  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RES);
  ledcAttachPin(TFT_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, backlightLevels[backlightIndex]);
  pinMode(BTN1_PIN, INPUT);
  pinMode(BTN2_PIN, INPUT);

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Starting...", 4, 4, 2);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Scanning BLE...", 4, 4, 2);
}

void loop() {
  // Button 1 (GPIO 0): cycle backlight brightness
  static bool btn1Prev = true;
  bool btn1 = digitalRead(BTN1_PIN);
  if (!btn1 && btn1Prev) {
    backlightIndex = (backlightIndex + 1) % NUM_BACKLIGHT_LEVELS;
    ledcWrite(BACKLIGHT_CHANNEL, backlightLevels[backlightIndex]);
  }
  btn1Prev = btn1;

  BLEScanResults results = pBLEScan->start(BLE_SCAN_TIME_SEC, false);
  pBLEScan->clearResults();

  static bool noSignalShown = false;

  if (dataUpdated) {
    dataUpdated = false;
    noSignalShown = false;
    drawDisplay();
  } else if (lastPacketTime != 0 && (millis() - lastPacketTime > NO_SIGNAL_TIMEOUT_MS)) {
    if (!noSignalShown) {
      drawNoSignal();
      noSignalShown = true;
    }
    lastPacketTime = 0;
  } else if (lastPacketTime == 0 && millis() > NO_SIGNAL_TIMEOUT_MS) {
    if (!noSignalShown) {
      drawNoSignal();
      noSignalShown = true;
    }
  }
}
