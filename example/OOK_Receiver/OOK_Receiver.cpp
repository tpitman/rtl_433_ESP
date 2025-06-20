/*
 Basic rtl_433_ESP example for OOK/ASK Devices

*/

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <FastLED.h>
#include <NimBLEDevice.h>
#include <esp_sleep.h>
#include <rtl_433_ESP.h>

#include "lis3dh.h"

#ifndef RF_MODULE_FREQUENCY
#  define RF_MODULE_FREQUENCY 433.92
#endif

#define JSON_MSG_BUFFER        512
#define LIS3DH_INT_PIN         GPIO_NUM_26 // ESP32 GPIO pin connected to LIS3DH INT1
#define SLEEP_DURATION_MINUTES 5
#define SLEEP_DURATION_US      (SLEEP_DURATION_MINUTES * 60 * 1000000ULL)
#define GO_TO_SLEEP_MS         30000

#define LED_PIN     D8
#define NUM_LEDS    1
#define BRIGHTNESS  64 // Max 255
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

const char* bleServiceUUID = "e5e84350-ffd5-4b15-ba12-024b7e65ed06";
const char* bleCharacteristicUUID = "e5e84351-ffd5-4b15-ba12-024b7e65ed06";
const char* bleBatteryCharacteristicUUID = "e5e84352-ffd5-4b15-ba12-024b7e65ed06";
const char* bleAccelerometerCharacteristicUUID = "e5e84353-ffd5-4b15-ba12-024b7e65ed06";

CRGB leds[NUM_LEDS];

uint32_t _milliVolts = 0;

char messageBuffer[JSON_MSG_BUFFER];

static NimBLEServer* pBLEServer;
static LIS3DH lis3dh;

static bool subscribedToAccelerometer = false;

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    Serial.printf("Client address: %s\n", connInfo.getAddress().toString().c_str());

    /**
         *  We can use the connection handle here to ask for different connection parameters.
         *  Args: connection handle, min connection interval, max connection interval
         *  latency, supervision timeout.
         *  Units; Min/Max Intervals: 1.25 millisecond increments.
         *  Latency: number of intervals allowed to skip.
         *  Timeout: 10 millisecond increments.
         */
    pServer->updateConnParams(connInfo.getConnHandle(), 24, 48, 0, 180);
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    Serial.printf("Client disconnected - start advertising\n");
    subscribedToAccelerometer = false;
    NimBLEDevice::startAdvertising();
  }

  void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
    Serial.printf("MTU updated: %u for connection ID: %u\n", MTU, connInfo.getConnHandle());
  }

  /********************* Security handled here *********************/
  uint32_t onPassKeyDisplay() override {
    Serial.printf("Server Passkey Display\n");
    /**
         * This should return a random 6 digit number for security
         *  or make your own static passkey as done here.
         */
    return 123456;
  }

  void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pass_key) override {
    Serial.printf("The passkey YES/NO number: %" PRIu32 "\n", pass_key);
    /** Inject false if passkeys don't match. */
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    /** Check that encryption was successful, if not we disconnect the client */
    if (!connInfo.isEncrypted()) {
      NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
      Serial.printf("Encrypt connection failed - disconnecting client\n");
      return;
    }

    Serial.printf("Secured connection to: %s\n", connInfo.getAddress().toString().c_str());
  }
} serverCallbacks;

/** Handler class for characteristic actions */
class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.printf("%s : onRead(), value: %s\n",
                  pCharacteristic->getUUID().toString().c_str(),
                  pCharacteristic->getValue().c_str());
  }

  void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
    Serial.printf("%s : onWrite(), value: %s\n",
                  pCharacteristic->getUUID().toString().c_str(),
                  pCharacteristic->getValue().c_str());
  }

  /**
     *  The value returned in code is the NimBLE host return code.
     */
  void onStatus(NimBLECharacteristic* pCharacteristic, int code) override {
    Serial.printf("Notification/Indication return code: %d, %s\n", code, NimBLEUtils::returnCodeToString(code));
  }

  /** Peer subscribed to notifications/indications */
  void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
    std::string uuid = std::string(pCharacteristic->getUUID());

    std::string str = "Address: ";
    str += connInfo.getAddress().toString();
    if (subValue == 0) {
      if (uuid == bleAccelerometerCharacteristicUUID)
        subscribedToAccelerometer = false;

      str += " Unsubscribed to ";
    } else if (subValue == 1) {
      if (uuid == bleAccelerometerCharacteristicUUID)
        subscribedToAccelerometer = true;

      str += " Subscribed to notifications for ";
    } else if (subValue == 2) {
      if (uuid == bleAccelerometerCharacteristicUUID)
        subscribedToAccelerometer = true;

      str += " Subscribed to indications for ";
    } else if (subValue == 3) {
      if (uuid == bleAccelerometerCharacteristicUUID)
        subscribedToAccelerometer = true;

      str += " Subscribed to notifications and indications for ";
    }
    str += uuid;

    Serial.printf("%s\n", str.c_str());
  }
} chrCallbacks;

/** Handler class for descriptor actions */
class DescriptorCallbacks : public NimBLEDescriptorCallbacks {
  void onWrite(NimBLEDescriptor* pDescriptor, NimBLEConnInfo& connInfo) override {
    std::string dscVal = pDescriptor->getValue();
    Serial.printf("Descriptor written value: %s\n", dscVal.c_str());
  }

  void onRead(NimBLEDescriptor* pDescriptor, NimBLEConnInfo& connInfo) override {
    Serial.printf("%s Descriptor read\n", pDescriptor->getUUID().toString().c_str());
  }
} dscCallbacks;

rtl_433_ESP rf; // use -1 to disable transmitter

int count = 0;

void logJson(JsonDocument jsondata) {
#if defined(ESP8266) || defined(ESP32) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega1280__)
  char JSONmessageBuffer[measureJson(jsondata) + 1];
  serializeJson(jsondata, JSONmessageBuffer, measureJson(jsondata) + 1);
#else
  char JSONmessageBuffer[JSON_MSG_BUFFER];
  serializeJson(jsondata, JSONmessageBuffer, JSON_MSG_BUFFER);
#endif
#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)
  Log.setShowLevel(false);
  Log.notice(F("."));
  Log.setShowLevel(true);
#else
  uint32_t seconds = ((float)millis()) / 1000.0f;
  uint32_t hours = ((float)seconds) / 3600.0f;
  seconds = seconds % 3600;
  uint32_t minutes = ((float)seconds) / 60.0f;
  seconds = seconds % 60;

  printf("Received message : %02d:%02d:%02d - %s\n", hours, minutes, seconds, JSONmessageBuffer);
#endif
}

// Function to enter deep sleep
void goToDeepSleep() {
  Serial.println("Preparing to enter deep sleep.");

  Serial.println("Turning off the led");
  FastLED.setBrightness(0);
  FastLED.show();

  // Configure LIS3DH interrupt pin (GPIO26) as EXT0 wakeup source.
  // LIS3DH click interrupt is active HIGH by default. Wake up when GPIO26 goes HIGH.
  Serial.println("Enabling EXT0 wakeup on GPIO " + String(LIS3DH_INT_PIN) + " (Active HIGH)");
  esp_sleep_enable_ext0_wakeup(LIS3DH_INT_PIN, 1);

  // Configure timer wakeup
  Serial.println("Enabling timer wakeup in " + String(SLEEP_DURATION_MINUTES) + " minutes.");
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US);

  Serial.flush(); // Ensure all serial messages are sent
  esp_deep_sleep_start(); // Enter deep sleep
}

// Function to print the cause of wakeup
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal on RTC_IO (LIS3DH Interrupt).");
      lis3dh.ClearInterruptSource(); // IMPORTANT: Clear the interrupt source on LIS3DH
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer.");
      break;
    // Add other cases as needed (EXT1, TOUCHPAD, ULP, etc.)
    default:
      Serial.printf("Wakeup was not by EXT0 or Timer (reason: %d)\n", wakeup_reason);
      break;
  }
}

// {"model":"Schrader-EG53MA4","type":"TPMS","flags":"4c900080","id":"06C463","pressure_PSI":0,"temperature_F":81.0,"mic":"CHECKSUM","protocol":"Schrader TPMS EG53MA4, PA66GF35","rssi":-63,"duration":2511996}

void rtl_433_Callback(char* message) {
  JsonDocument jsonDocument;
  deserializeJson(jsonDocument, message);
  logJson(jsonDocument);
  count++;

  if (pBLEServer->getConnectedCount()) {
    NimBLEService* pService = pBLEServer->getServiceByUUID(bleServiceUUID);
    if (pService) {
      NimBLECharacteristic* pCharacteristic = pService->getCharacteristic(bleCharacteristicUUID);
      if (pCharacteristic) {
        const char* id = jsonDocument["id"];
        int pressure = jsonDocument["pressure_PSI"];
        int temperature = jsonDocument["temperature_F"];
        int rssi = jsonDocument["rssi"];

        char buffer[50];
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "%s|%d|%d|%d", id, pressure, temperature, rssi);
        pCharacteristic->setValue(buffer);
        pCharacteristic->notify();
      }
    }
  }
}

void setup() {
  Serial.begin(921600);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  leds[0] = CRGB::Green;
  FastLED.show();

  delay(1000);

  print_wakeup_reason();

  if (!lis3dh.Init(LIS3DH_INT_PIN)) {
  }

  lis3dh.ClearInterruptSource();

  char bleNameWithIPAddress[50];
  sprintf(bleNameWithIPAddress, "HLLYTPMS123456789");

  NimBLEDevice::init(bleNameWithIPAddress);

  pBLEServer = NimBLEDevice::createServer();
  pBLEServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pBLEServer->createService(bleServiceUUID);
  NimBLECharacteristic* pCharacteristic = pService->createCharacteristic(bleCharacteristicUUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  pCharacteristic->setCallbacks(&chrCallbacks);
  NimBLECharacteristic* pBatteryCharacteristic = pService->createCharacteristic(bleBatteryCharacteristicUUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pBatteryCharacteristic->setCallbacks(&chrCallbacks);
  NimBLECharacteristic* pAccelerometerCharacteristic = pService->createCharacteristic(bleAccelerometerCharacteristicUUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pAccelerometerCharacteristic->setCallbacks(&chrCallbacks);
  pService->start();

  /** Create an advertising instance and add the services to the advertised data */
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(bleNameWithIPAddress);
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

#ifndef LOG_LEVEL
#  define LOG_LEVEL LOG_LEVEL_SILENT
#endif
  Log.begin(LOG_LEVEL, &Serial);
  Log.notice(F(" " CR));
  Log.notice(F("****** setup ******" CR));
  rf.initReceiver(RF_MODULE_RECEIVER_GPIO, RF_MODULE_FREQUENCY);
  rf.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
  rf.enableReceiver();
  Log.notice(F("****** setup complete ******" CR));
  rf.getModuleStatus();

  pinMode(A2, INPUT);
}

unsigned long uptime() {
  static unsigned long lastUptime = 0;
  static unsigned long uptimeAdd = 0;
  unsigned long uptime = millis() / 1000 + uptimeAdd;
  if (uptime < lastUptime) {
    uptime += 4294967;
    uptimeAdd += 4294967;
  }
  lastUptime = uptime;
  return uptime;
}

int next = uptime() + 30;

#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)

#  ifdef setBitrate
#    define TEST    "setBitrate" // 17.24 was suggested
#    define STEP    2
#    define stepMin 1
#    define stepMax 300
// #    define STEP    1
// #    define stepMin 133
// #    define stepMax 138
#  elif defined(setFreqDev) // 40 kHz was suggested
#    define TEST    "setFrequencyDeviation"
#    define STEP    1
#    define stepMin 5
#    define stepMax 200
#  elif defined(setRxBW)
#    define TEST "setRxBandwidth"

#    ifdef defined(RF_SX1276) || defined(RF_SX1278)
#      define STEP    5
#      define stepMin 5
#      define stepMax 250
#    else
#      define STEP    5
#      define stepMin 58
#      define stepMax 812
// #      define STEP    0.01
// #      define stepMin 202.00
// #      define stepMax 205.00
#    endif
#  endif
float step = stepMin;
#endif

uint32_t battery_timeout = 0;

uint16_t readMilliVolts(uint8_t pin) {
  return analogReadMilliVolts(pin) * 2;
}

void loop() {
  static unsigned long lastActivityTimestamp = millis();
  unsigned long idleDurationBeforeSleep = GO_TO_SLEEP_MS; // idle + no BLE

  rf.loop();

  if (battery_timeout++ % 10000 == 0) {
    _milliVolts = readMilliVolts(A2);

    if (_milliVolts > 4000)
      lastActivityTimestamp = millis();

    Serial.print(_milliVolts);
    Serial.println(" mV");

    if (pBLEServer->getConnectedCount()) {
      NimBLEService* pService = pBLEServer->getServiceByUUID(bleServiceUUID);
      if (pService) {
        NimBLECharacteristic* pCharacteristic = pService->getCharacteristic(bleBatteryCharacteristicUUID);
        if (pCharacteristic) {
          pCharacteristic->setValue(_milliVolts);
          pCharacteristic->notify();
        }
      }
    }
  }

  if (pBLEServer && pBLEServer->getConnectedCount() > 0) {
    lastActivityTimestamp = millis(); // Reset idle timer if BLE is connected

    if (subscribedToAccelerometer) {
      Log.notice(F("getting accelerometer data.\n"));

      AccelData accelData;
      if (lis3dh.ReadAcceleration(accelData)) {
        // Create simple string format: x|y|z
        char accelBuffer[64];
        memset(accelBuffer, 0, sizeof(accelBuffer));
        sprintf(accelBuffer, "%.3f|%.3f|%.3f", accelData.x, accelData.y, accelData.z);
        //Log.notice(F(accelBuffer));

        NimBLEService* pService = pBLEServer->getServiceByUUID(bleServiceUUID);
        if (pService) {
          NimBLECharacteristic* pCharacteristic = pService->getCharacteristic(bleAccelerometerCharacteristicUUID);
          if (pCharacteristic) {
            pCharacteristic->setValue(accelBuffer);
            pCharacteristic->notify();
          }
        }
      }
    }
  } else {
    if (millis() - lastActivityTimestamp > idleDurationBeforeSleep) {
      Log.notice(F("Idle time reached with no BLE connection. Entering deep sleep." CR));
      goToDeepSleep();
    }
  }

#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)
  char stepPrint[8];
  if (uptime() > next) {
    next = uptime() + 30; // 15 seconds
    dtostrf(step, 7, 2, stepPrint);
    Log.notice(F(CR "Finished %s: %s, count: %d" CR), TEST, stepPrint, count);
    step += STEP;
    if (step > stepMax) {
      step = stepMin;
    }
    dtostrf(step, 7, 2, stepPrint);
    Log.notice(F("Starting %s with %s" CR), TEST, stepPrint);
    count = 0;

    int16_t state = 0;
#  ifdef setBitrate
    state = rf.setBitRate(step);
    RADIOLIB_STATE(state, TEST);
#  elif defined(setFreqDev)
    state = rf.setFrequencyDeviation(step);
    RADIOLIB_STATE(state, TEST);
#  elif defined(setRxBW)
    state = rf.setRxBandwidth(step);
    if ((state) != RADIOLIB_ERR_NONE) {
      Log.notice(F(CR "Setting  %s: to %s, failed" CR), TEST, stepPrint);
      next = uptime() - 1;
    }
#  endif

    rf.receiveDirect();
    // rf.getModuleStatus();
  }
#endif
}