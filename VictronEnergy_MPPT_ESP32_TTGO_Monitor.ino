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
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "logo.h"

// ESP32 BLE may return String or std::string; set to match your board package
#define USE_String

#define FW_VERSION          "1.0.0"
#define DEBUG_VICTRON      0   // 1 = enable Serial debug output

#define BLE_SCAN_TIME_SEC   1
#define NO_SIGNAL_TIMEOUT_MS 10000
#define TFT_BACKLIGHT_PIN   4
#define BTN1_PIN            0   // Left button (GPIO 0) - backlight
#define BTN2_PIN            35  // Right button (GPIO 35) - unused
#define RELAY_PIN           25  // Relay (GPIO 25 + GND) - use 12, 13, 21, 22, 27 if 25 unavailable
#define RELAY_ACTIVE_LOW    0   // 1 = relay ON when pin LOW; 0 = relay ON when pin HIGH
#define RELAY_ON_THRESHOLD_V   24.0f   // relay ON when battery >= this
#define RELAY_OFF_THRESHOLD_V  23.8f   // relay OFF when battery < this
#define RELAY_DEBOUNCE_MS   15000   // voltage must hold for 15 s before relay state changes
#define BACKLIGHT_CHANNEL   0
#define NUM_PAGES           3
#define PAGE_INTERVAL_MS    5000
#define HEADER_HEIGHT       28   // Solar W only
#define CONTENT_OFFSET_Y    5    // pixels below header for page content
#define FOOTER_HEIGHT       22   // Battery V, A, V*A on all pages
#define COLOR_LABEL         TFT_YELLOW
#define COLOR_MINMAX        TFT_CYAN
#define LINE_H              26   // font 4 line height (font 3 not in TFT_eSPI)
#define BACKLIGHT_FREQ      5000
#define BACKLIGHT_RES      8
// Sanity bounds for decoded values (avoid layout/buffer issues from bad packets)
#define CLAMP_V_MIN         0.0f
#define CLAMP_V_MAX         100.0f
#define CLAMP_I_MAX         100.0f
#define CLAMP_POWER_MAX     10000
#define CLAMP_YIELD_MAX     999999.0f
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
uint8_t  currentPage;       // 0 = Status, 1 = Yield, 2 = Min/Max
uint32_t lastPageSwitchTime;

// Max/min values today (reset when todayYield drops = new day, or at boot)
uint16_t maxSolarW;
float    maxLoadW;
float    maxBatteryV;
float    maxBatteryI;
uint16_t minSolarW;
float    minLoadW;
float    minBatteryV;
float    minBatteryI;
float    lastYieldWh;       // for new-day detection

// --- Check key is exactly 32 hex characters (for startup validation) ---
static bool isKeyValid(const char* hex) {
  if (!hex) return false;
  if (strlen(hex) != 32) return false;
  for (int i = 0; i < 32; i++)
    if (!isxdigit((unsigned char)hex[i])) return false;
  return true;
}

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

    float newBatteryV  = (float)pd->batteryVoltage * 0.01f;
    float newBatteryI  = (float)pd->batteryCurrent * 0.1f;
    uint16_t newInputPowerW = pd->inputPower;
    float newTodayYieldWh  = (float)pd->todayYield * 0.01f * 1000.0f;
    int outCurr9       = ((pd->outputCurrentHi & 0x01) << 8) | pd->outputCurrentLo;
    float newOutputCurrentA = (float)outCurr9 * 0.1f;
    float newLoadW     = newBatteryV * newOutputCurrentA;

    // Clamp to sane ranges (corrupted packets / overflow)
    if (newBatteryV < CLAMP_V_MIN) newBatteryV = CLAMP_V_MIN;
    if (newBatteryV > CLAMP_V_MAX) newBatteryV = CLAMP_V_MAX;
    if (newBatteryI < -CLAMP_I_MAX) newBatteryI = -CLAMP_I_MAX;
    if (newBatteryI > CLAMP_I_MAX) newBatteryI = CLAMP_I_MAX;
    if (newInputPowerW > (uint16_t)CLAMP_POWER_MAX) newInputPowerW = CLAMP_POWER_MAX;
    if (newTodayYieldWh < 0.0f) newTodayYieldWh = 0.0f;
    if (newTodayYieldWh > CLAMP_YIELD_MAX) newTodayYieldWh = CLAMP_YIELD_MAX;
    if (newOutputCurrentA < 0.0f) newOutputCurrentA = 0.0f;
    if (newOutputCurrentA > CLAMP_I_MAX) newOutputCurrentA = CLAMP_I_MAX;
    newLoadW = newBatteryV * newOutputCurrentA;
    if (newLoadW < 0.0f) newLoadW = 0.0f;
    if (newLoadW > (float)CLAMP_POWER_MAX) newLoadW = (float)CLAMP_POWER_MAX;

    // New day: yield dropped (midnight rollover) -> reset max/min values
    if (lastPacketTime != 0 && newTodayYieldWh < lastYieldWh) {
      maxSolarW = maxLoadW = maxBatteryV = maxBatteryI = 0;
      minSolarW = 65535;
      minLoadW = minBatteryV = 99999.0f;
      minBatteryI = 99999.0f;
    }

    // Only trigger redraw when values actually changed (reduces flicker)
    bool firstPacket = (lastPacketTime == 0);
    bool changed = firstPacket ||
      (newBatteryV != dispBatteryV || newBatteryI != dispBatteryI ||
       newInputPowerW != dispInputPowerW || newTodayYieldWh != dispTodayYieldWh ||
       newOutputCurrentA != dispOutputCurrentA || pd->deviceState != dispDeviceState ||
       pd->errorCode != dispErrorCode);

    dispBatteryV       = newBatteryV;
    dispBatteryI       = newBatteryI;
    dispInputPowerW    = newInputPowerW;
    dispTodayYieldWh   = newTodayYieldWh;
    dispOutputCurrentA = newOutputCurrentA;
    dispDeviceState    = pd->deviceState;
    dispErrorCode      = pd->errorCode;
    lastYieldWh        = newTodayYieldWh;

    // Update max values
    if (newInputPowerW > maxSolarW) maxSolarW = newInputPowerW;
    if (newLoadW > maxLoadW) maxLoadW = newLoadW;
    if (newBatteryV > maxBatteryV) maxBatteryV = newBatteryV;
    if (newBatteryI > maxBatteryI) maxBatteryI = newBatteryI;
    // Update min values
    if (newInputPowerW < minSolarW) minSolarW = newInputPowerW;
    if (newLoadW < minLoadW) minLoadW = newLoadW;
    if (newBatteryV < minBatteryV) minBatteryV = newBatteryV;
    if (newBatteryI < minBatteryI) minBatteryI = newBatteryI;

    dataUpdated        = changed;
    lastPacketTime     = millis();
  }
};

// --- Header: Solar W centered, Victron logo top right ---
static void drawHeader() {
  char buf[24];
  snprintf(buf, sizeof(buf), "%u W", (unsigned)dispInputPowerW);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.drawString(buf, 120, 2, 4);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  // Victron logo top right, scaled to header height (28px)
  tft.drawBitmap(240 - LOGO_W - 2, 2, logoBitmap, LOGO_W, LOGO_H, VICTRON_BLUE);
}

// --- Footer: Bat V, A, V*A centered ---
static void drawFooter() {
  char buf[48];
  float batPowerW = dispBatteryV * dispBatteryI;
  snprintf(buf, sizeof(buf), "%.2f V  %.2f A  %.0f W", (double)dispBatteryV, (double)dispBatteryI, (double)batPowerW);
  const char* lbl = "Bat ";
  int wLbl = tft.textWidth(lbl, 2);
  int wVal = tft.textWidth(buf, 2);
  int totalW = wLbl + wVal;
  int startX = (240 - totalW) / 2;
  if (startX < 4) startX = 4;
  int fy = 135 - 2;
  tft.setTextDatum(BL_DATUM);
  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString(lbl, startX, fy, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, startX + wLbl, fy, 2);
  tft.setTextDatum(TL_DATUM);
}

// --- Page 0: State, error, load (battery V/A in footer) ---
static void drawPageStatus() {
  tft.setTextDatum(TL_DATUM);
  char buf[64];
  int y = HEADER_HEIGHT + CONTENT_OFFSET_Y;

  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("State: ", 4, y, 4);
  snprintf(buf, sizeof(buf), "%s", getStateString(dispDeviceState));
  if (dispErrorCode != 0)
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "  Err:%u", (unsigned)dispErrorCode);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 4 + tft.textWidth("State: ", 4), y, 4);
  y += LINE_H;

  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("Load ", 4, y, 4);
  const char* loadStateStr = (dispOutputCurrentA > 0.01f) ? "ON" : "OFF";
  float loadPowerW = dispBatteryV * dispOutputCurrentA;
  snprintf(buf, sizeof(buf), "%s  %.0f W", loadStateStr, (double)loadPowerW);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 4 + tft.textWidth("Load ", 4), y, 4);
}

// --- Page 1: Yield + firmware version ---
static void drawPageYieldInfo() {
  tft.setTextDatum(TL_DATUM);
  char buf[48];
  int y = HEADER_HEIGHT + CONTENT_OFFSET_Y;

  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("Yield ", 4, y, 4);
  snprintf(buf, sizeof(buf), "%.0f Wh", (double)dispTodayYieldWh);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, 4 + tft.textWidth("Yield ", 4), y, 4);
  y += LINE_H;

  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("FW ", 4, y, 2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(FW_VERSION, 4 + tft.textWidth("FW ", 2), y, 2);
}

// --- Page 2: Min/Max combined, 3 lines, format: Label Min: val Max: val ---
static void drawPageMinMax() {
  tft.setTextDatum(TL_DATUM);
  char buf[32];
  int y = HEADER_HEIGHT + CONTENT_OFFSET_Y;
  int x = 4;

  // Line 1: Solar  Min: X W  Max: Y W (font 3 not in TFT_eSPI; use 4 and 2)
  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("SOL ", x, y, 4);
  x += tft.textWidth("SOL ", 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Min: ", x, y + 4, 2);
  x += tft.textWidth("Min: ", 2);
  snprintf(buf, sizeof(buf), "%u W  ", (unsigned)(minSolarW < 65535 ? minSolarW : 0));
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
  x += tft.textWidth(buf, 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Max: ", x, y + 4, 2);
  x += tft.textWidth("Max: ", 2);
  snprintf(buf, sizeof(buf), "%u W", (unsigned)maxSolarW);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
  y += LINE_H;

  // Line 2: Load  Min: X W  Max: Y W
  x = 4;
  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("LD ", x, y, 4);
  x += tft.textWidth("LD ", 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Min: ", x, y + 4, 2);
  x += tft.textWidth("Min: ", 2);
  snprintf(buf, sizeof(buf), "%.0f W  ", (double)(minLoadW < 99999.0f ? minLoadW : 0.0f));
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
  x += tft.textWidth(buf, 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Max: ", x, y + 4, 2);
  x += tft.textWidth("Max: ", 2);
  snprintf(buf, sizeof(buf), "%.0f W", (double)maxLoadW);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
  y += LINE_H;

  // Line 3: Bat  Min: X V  Max: Y V
  x = 4;
  tft.setTextColor(COLOR_LABEL, TFT_BLACK);
  tft.drawString("BT ", x, y, 4);
  x += tft.textWidth("BT ", 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Min: ", x, y + 4, 2);
  x += tft.textWidth("Min: ", 2);
  snprintf(buf, sizeof(buf), "%.2f V  ", (double)(minBatteryV < 99999.0f ? minBatteryV : 0.0f));
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
  x += tft.textWidth(buf, 4);
  tft.setTextColor(COLOR_MINMAX, TFT_BLACK);
  tft.drawString("Max: ", x, y + 4, 2);
  x += tft.textWidth("Max: ", 2);
  snprintf(buf, sizeof(buf), "%.2f V", (double)maxBatteryV);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(buf, x, y, 4);
}

// --- Draw header + current page + footer ---
void drawDisplay() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  if (currentPage == 0)
    drawPageStatus();
  else if (currentPage == 1)
    drawPageYieldInfo();
  else
    drawPageMinMax();
  drawFooter();
}

void drawNoSignal() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("SmartSolar MPPT", 4, 4, 4);
  tft.drawString("No BLE signal", 4, 32, 4);
  tft.drawString("Check key & range", 4, 60, 4);
}

static void drawBadKeyScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("Bad key", 4, 4, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Check config.h: 32 hex chars", 4, 32, 2);
  tft.drawString("Copy config.example.h -> config.h", 4, 50, 2);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("Victron Monitor " FW_VERSION);

  // Init display first so we can show "Bad key" if config is wrong
  ledcSetup(BACKLIGHT_CHANNEL, BACKLIGHT_FREQ, BACKLIGHT_RES);
  ledcAttachPin(TFT_BACKLIGHT_PIN, BACKLIGHT_CHANNEL);
  ledcWrite(BACKLIGHT_CHANNEL, backlightLevels[backlightIndex]);
  pinMode(BTN1_PIN, INPUT);
  pinMode(BTN2_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);   // relay OFF at boot
  tft.init();
  tft.setRotation(1);   // landscape: 240 wide x 135 tall
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Starting...", 4, 4, 4);

  if (!isKeyValid(VICTRON_AES_KEY_HEX)) {
    drawBadKeyScreen();
    for (;;) delay(1000);
  }
  parseHexKey(VICTRON_AES_KEY_HEX, key);

  strcpy(savedDeviceName, "(unknown)");
  dataUpdated = false;
  lastPacketTime = 0;
  currentPage = 0;
  lastPageSwitchTime = millis();
  maxSolarW = 0;
  maxLoadW = maxBatteryV = maxBatteryI = lastYieldWh = 0.0f;
  minSolarW = 65535;
  minLoadW = minBatteryV = minBatteryI = 99999.0f;

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Scanning BLE...", 4, 4, 4);
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

  // Relay with hysteresis + time debounce: state changes only after voltage holds for RELAY_DEBOUNCE_MS
  static bool relayState = false;
  static bool relayDesiredState = false;
  static uint32_t relayStateChangeTime = 0;
  bool hasValidData = (lastPacketTime != 0 && (millis() - lastPacketTime <= NO_SIGNAL_TIMEOUT_MS));

  if (!hasValidData) {
    relayState = false;           // fail-safe: OFF immediately when no BLE data
    relayStateChangeTime = 0;
  } else {
    bool desired;
    if (dispBatteryV >= RELAY_ON_THRESHOLD_V)
      desired = true;
    else if (dispBatteryV < RELAY_OFF_THRESHOLD_V)
      desired = false;
    else
      desired = relayDesiredState;   // hysteresis band: keep previous desired
    relayDesiredState = desired;

    if (desired != relayState) {
      if (relayStateChangeTime == 0)
        relayStateChangeTime = millis();
      if ((millis() - relayStateChangeTime) >= RELAY_DEBOUNCE_MS) {
        relayState = desired;
        relayStateChangeTime = 0;
      }
    } else {
      relayStateChangeTime = 0;   // reset so next change requires full debounce
    }
  }

  #if RELAY_ACTIVE_LOW
  digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);  // active-LOW: ON when pin LOW
  #else
  digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);   // active-HIGH: ON when pin HIGH
  #endif

  static bool noSignalShown = false;
  bool needDraw = false;

  // BLE update: redraw current page with new data (independent of page timer)
  if (dataUpdated) {
    dataUpdated = false;
    noSignalShown = false;
    needDraw = true;
  }

  // Page switch: every 5 s when we have signal (independent of BLE polling)
  if (lastPacketTime != 0 && (millis() - lastPacketTime <= NO_SIGNAL_TIMEOUT_MS) &&
      (millis() - lastPageSwitchTime >= PAGE_INTERVAL_MS)) {
    lastPageSwitchTime = millis();
    currentPage = (currentPage + 1) % NUM_PAGES;
    needDraw = true;
  }

  if (needDraw) {
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
