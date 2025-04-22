/*
 Basic rtl_433_ESP example for OOK/ASK Devices

*/

#include <ArduinoJson.h>
#include <ArduinoLog.h>
#include <NimBLEDevice.h>
#include <rtl_433_ESP.h>

#ifndef RF_MODULE_FREQUENCY
#  define RF_MODULE_FREQUENCY 433.92
#endif

#define JSON_MSG_BUFFER 512

char messageBuffer[JSON_MSG_BUFFER];

static NimBLEServer* pBLEServer;

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
    std::string str = "Client ID: ";
    str += connInfo.getConnHandle();
    str += " Address: ";
    str += connInfo.getAddress().toString();
    if (subValue == 0) {
      str += " Unsubscribed to ";
    } else if (subValue == 1) {
      str += " Subscribed to notifications for ";
    } else if (subValue == 2) {
      str += " Subscribed to indications for ";
    } else if (subValue == 3) {
      str += " Subscribed to notifications and indications for ";
    }
    str += std::string(pCharacteristic->getUUID());

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

const char* bleServiceUUID = "E5E84350-FFD5-4B15-BA12-024B7E65ED06";
const char* bleCharacteristicUUID = "E5E84351-FFD5-4B15-BA12-024B7E65ED06";

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

        char buffer[50];
        memset(buffer, 0, sizeof(buffer));
        sprintf(buffer, "%s|%d|%d", id, pressure, temperature);
        pCharacteristic->setValue(buffer);
        pCharacteristic->notify();
      }
    }
  }
}

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
  Log.notice(F("Received message : %s" CR), JSONmessageBuffer);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  char bleNameWithIPAddress[50];
  sprintf(bleNameWithIPAddress, "HLLYTPMS123456789");

  NimBLEDevice::init(bleNameWithIPAddress);

  pBLEServer = NimBLEDevice::createServer();
  pBLEServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pBLEServer->createService(bleServiceUUID);
  NimBLECharacteristic* pCharacteristic = pService->createCharacteristic(bleCharacteristicUUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
  pCharacteristic->setCallbacks(&chrCallbacks);
  pService->start();

  /** Create an advertising instance and add the services to the advertised data */
  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(bleNameWithIPAddress);
  pAdvertising->addServiceUUID(pService->getUUID());
  pAdvertising->enableScanResponse(true);
  pAdvertising->start();

#ifndef LOG_LEVEL
  #define LOG_LEVEL LOG_LEVEL_SILENT
#endif
  Log.begin(LOG_LEVEL, &Serial);
  Log.notice(F(" " CR));
  Log.notice(F("****** setup ******" CR));
  rf.initReceiver(RF_MODULE_RECEIVER_GPIO, RF_MODULE_FREQUENCY);
  rf.setCallback(rtl_433_Callback, messageBuffer, JSON_MSG_BUFFER);
  rf.enableReceiver();
  Log.notice(F("****** setup complete ******" CR));
  rf.getModuleStatus();
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

void loop() {
  rf.loop();

#if defined(setBitrate) || defined(setFreqDev) || defined(setRxBW)
  char stepPrint[8];
  if (uptime() > next) {
    next = uptime() + 120; // 60 seconds
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