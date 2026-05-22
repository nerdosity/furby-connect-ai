#include "ble_furby.h"

// ── Utility ───────────────────────────────────────────────────────────────────
static String hexFmt(const uint8_t* data, size_t len) {
    char buf[4]; String o; o.reserve(len * 3);
    for (size_t i = 0; i < len; i++) {
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        if (i) o += ' ';
        o += buf;
    }
    return o;
}

// ── BLE state reset ───────────────────────────────────────────────────────────
void bleResetState() {
    connected               = false;
    bleConnectedMs          = 0;
    pRemoteCharacteristicTX = nullptr;
    pCharGPListen           = nullptr;
    pCharNWrite             = nullptr;
    pCharNListen            = nullptr;
}

// ── Callbacks ─────────────────────────────────────────────────────────────────
class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient*) {}
  void onDisconnect(BLEClient*) {
      bleResetState();
      Serial.println("BLE: Furby disconnesso.");
  }
};

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
      String name = advertisedDevice.haveName() ? advertisedDevice.getName().c_str() : "";
      String addr = advertisedDevice.getAddress().toString().c_str();
      String nameLower = name; nameLower.toLowerCase();
      bool isFurby = nameLower.indexOf("furby") >= 0 ||
                     (advertisedDevice.haveServiceUUID() &&
                      advertisedDevice.isAdvertisingService(serviceUUID));
      if (!isFurby) return;
      for (int i = 0; i < furbyListCount; i++) {
          if (furbyList[i].addr == addr) {
              if (name.length() && furbyList[i].name == addr) {
                  furbyList[i].name = name;
                  if (myDevice) delete myDevice;
                  myDevice = new BLEAdvertisedDevice(advertisedDevice);
              }
              return;
          }
      }
      if (furbyListCount < MAX_FURBY_SCAN) {
          esp_ble_addr_type_t atype = advertisedDevice.getAddressType();
          furbyList[furbyListCount++] = { name.length() ? name : addr, addr, atype };
          if (myDevice) delete myDevice;
          myDevice = new BLEAdvertisedDevice(advertisedDevice);
          Serial.println("BLE: trovato [" + (name.length() ? name : addr) + "] " + addr);
      }
  }
};

// ── Public API ────────────────────────────────────────────────────────────────
void bleDisconnect() {
    bleUserDisconnect = true;
    if (pBleClient && pBleClient->isConnected()) pBleClient->disconnect();
    bleResetState();
}

void bleScanStop() {
    if (!bleScanning) return;
    BLEDevice::getScan()->stop();
    bleScanning = false;
}

void bleScanStart(int durationSec) {
    if (bleScanning || connected) return;
    bleScanning = true; doConnect = false; furbyListCount = 0;
    BLEScan* scan = BLEDevice::getScan();
    scan->clearResults();
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(durationSec, [](BLEScanResults r) {
        bleScanning = false;
        Serial.printf("BLE: scan ok - %d Furby su %d device\n", furbyListCount, r.getCount());
    }, false);
    Serial.println("BLE: scan avviato (" + String(durationSec) + "s)");
}

void furbyWrite(const uint8_t* buf, size_t len) {
    if (!connected || !pRemoteCharacteristicTX) return;
    Serial.println("BLE TX: " + hexFmt(buf, len));
    try {
        pRemoteCharacteristicTX->writeValue((uint8_t*)buf, len, false);
    } catch (...) {
        Serial.println("BLE TX: errore write");
        bleResetState();
    }
}

// ── Internal: sensor packet decoder ──────────────────────────────────────────
static void parseSensorPacket(uint8_t* data, size_t len) {
    if (len < 5 || data[0] != 0x21) return;
    uint8_t b1=data[1], b2=data[2], b3=data[3], b4=data[4];
    furbyState.rawB1=b1; furbyState.rawB2=b2; furbyState.rawB3=b3; furbyState.rawB4=b4;
    furbyState.antennaLeft    = b1 & 0x02;
    furbyState.antennaRight   = b1 & 0x01;
    furbyState.antennaForward = b2 & 0x40;
    furbyState.antennaBack    = b2 & 0x80;
    furbyState.tickleHead     = b2 & 0x01;
    furbyState.tickleTummy    = b2 & 0x02;
    furbyState.tickleRight    = b2 & 0x04;
    furbyState.tickleLeft     = b2 & 0x08;
    furbyState.pullTail       = b2 & 0x10;
    furbyState.pushTongue     = b2 & 0x20;
    furbyState.upright        = b4 & 0x01;
    furbyState.upsideDown     = b4 & 0x02;
    furbyState.onRightSide    = b4 & 0x04;
    furbyState.onLeftSide     = b4 & 0x08;
    furbyState.leanBack       = b4 & 0x20;
    furbyState.tiltRight      = b4 & 0x40;
    furbyState.tiltLeft       = b4 & 0x80;
    lastSensorMs = millis();
    applyBehaviorRules(prevFurbyState, furbyState);
    prevFurbyState = furbyState;
}

static void gpListenNotifyCB(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (len == 0) return;
    Serial.println("BLE RX: " + hexFmt(data, len));
    if (data[0] == 0x21) parseSensorPacket(data, len);
    if (data[0] == 0x22 && data[1] == 0x21 && len >= 16) {
        uint8_t b11=data[11], b15=data[15];
        if (b15 <= 100) ble_battery_pct = b15;
        else if (b11 <= 100) ble_battery_pct = b11;
    }
}

static bool connectToFurbyByAddr(BLEAddress bleAddr, const String& nameHint, esp_ble_addr_type_t addrType = BLE_ADDR_TYPE_RANDOM) {
    String addr = bleAddr.toString().c_str();
    if (pBleClient) {
        if (pBleClient->isConnected()) { pBleClient->disconnect(); vTaskDelay(300/portTICK_PERIOD_MS); }
        delete pBleClient; pBleClient = nullptr;
    }
    bleResetState();
    pBleClient = BLEDevice::createClient();
    pBleClient->setClientCallbacks(new MyClientCallback());
    if (!pBleClient->connect(bleAddr, addrType)) {
        delete pBleClient; pBleClient = nullptr; return false;
    }
    vTaskDelay(200/portTICK_PERIOD_MS);
    BLERemoteService* svc = pBleClient->getService(serviceUUID);
    if (!svc) { pBleClient->disconnect(); delete pBleClient; pBleClient = nullptr; return false; }
    pRemoteCharacteristicTX = svc->getCharacteristic(charUUID_GPWrite);
    pCharGPListen           = svc->getCharacteristic(charUUID_GPListen);
    pCharNWrite             = svc->getCharacteristic(charUUID_NWrite);
    pCharNListen            = svc->getCharacteristic(charUUID_NListen);
    if (!pRemoteCharacteristicTX) { pBleClient->disconnect(); delete pBleClient; pBleClient = nullptr; return false; }
    if (pCharGPListen && pCharGPListen->canNotify())
        pCharGPListen->registerForNotify(gpListenNotifyCB);
    if (pCharNListen && pCharNListen->canNotify())
        pCharNListen->registerForNotify([](BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            Serial.println("BLE RX Nordic: " + hexFmt(data, len));
        });
    connected = true; bleConnectedMs = millis();
    ble_last_addr = addr;
    ble_last_name = nameHint.length() ? nameHint : addr;
    ble_battery_pct = -1;
    if (ble_last_name == addr) {
        for (int i = 0; i < furbyListCount; i++)
            if (furbyList[i].addr == addr && furbyList[i].name != addr) { ble_last_name = furbyList[i].name; break; }
    }
    try {
        BLERemoteService* batSvc = pBleClient->getService(BLEUUID((uint16_t)0x180F));
        if (batSvc) {
            BLERemoteCharacteristic* batChar = batSvc->getCharacteristic(BLEUUID((uint16_t)0x2A19));
            if (batChar && batChar->canRead()) {
                std::string val = batChar->readValue();
                if (val.length() > 0) { ble_battery_pct = (uint8_t)val[0]; Serial.printf("BLE Battery: %d%%\n", ble_battery_pct); }
            }
        }
    } catch(...) {}
    Serial.println("BLE: connesso a [" + ble_last_name + "] " + ble_last_addr);
    return true;
}

bool connectToFurby(BLEAdvertisedDevice* dev) {
    if (!dev) dev = myDevice;
    if (!dev) return false;
    String name = dev->haveName() ? dev->getName().c_str() : "";
    return connectToFurbyByAddr(dev->getAddress(), name, dev->getAddressType());
}

// ── Tasks ─────────────────────────────────────────────────────────────────────
void keepAliveTask(void* pvParameters) {
    const uint8_t ka[] = {0x20, 0x06};
    for (;;) {
        if (connected) {
            if (pBleClient && !pBleClient->isConnected()) { bleResetState(); }
            else {
                furbyWrite(ka, sizeof(ka));
                if (ble_last_name == ble_last_addr) {
                    for (int i = 0; i < furbyListCount; i++)
                        if (furbyList[i].addr == ble_last_addr && furbyList[i].name != ble_last_addr) {
                            ble_last_name = furbyList[i].name; break;
                        }
                }
            }
        }
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
}

void bleConnectTask(void* pvParameters) {
    String addr = pendingConnAddr, name = pendingConnName;
    esp_ble_addr_type_t atype = pendingConnAddrType;
    pendingConnAddr = ""; pendingConnName = "";
    if (bleScanning) { BLEDevice::getScan()->stop(); vTaskDelay(300/portTICK_PERIOD_MS); bleScanning = false; }
    bool ok = addr.length() > 0
        ? connectToFurbyByAddr(BLEAddress(addr.c_str()), name, atype)
        : connectToFurby();
    if (!ok) Serial.println("BLE: connessione fallita");
    bleConnecting = false;
    vTaskDelete(NULL);
}

void lipSyncTask(void* pvParameters) {
    uint8_t open_[]   = {0x10, 0x00, 0x0A, 0x01};
    uint8_t closed_[] = {0x10, 0x00, 0x0A, 0x00};
    for (;;) {
        if (isSpeaking && connected)
            furbyWrite(currentAmplitude > 500 ? open_ : closed_,
                       currentAmplitude > 500 ? sizeof(open_) : sizeof(closed_));
        vTaskDelay(isSpeaking && connected ? 80 : 100 / portTICK_PERIOD_MS);
    }
}

// ── BLE init helper (called from setup) ──────────────────────────────────────
void bleInit() {
    BLEDevice::init("");
    BLEDevice::getScan()->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
}

// ── Behavior configs persistence ──────────────────────────────────────────────
void saveBehaviorConfigs() {
    JsonDocument doc;
    doc["active"] = activeBehaviorConfig;
    JsonArray cfgs = doc["configs"].to<JsonArray>();
    for (int i = 0; i < behaviorConfigCount; i++) {
        JsonObject c = cfgs.add<JsonObject>();
        c["name"] = behaviorConfigs[i].name;
        JsonArray rules = c["rules"].to<JsonArray>();
        for (int r = 0; r < MAX_CFG_RULES; r++) {
            BehaviorRule& rule = behaviorConfigs[i].rules[r];
            if (rule.sensorId == SEN_NONE) continue;
            JsonObject ro = rules.add<JsonObject>();
            ro["s"] = rule.sensorId; ro["l"] = rule.len;
            JsonArray ba = ro["b"].to<JsonArray>();
            for (int b = 0; b < rule.len; b++) ba.add(rule.bytes[b]);
        }
    }
    String out; serializeJson(doc, out);
    Preferences p; p.begin("furby", false); p.putString("behaviors", out); p.end();
}

void loadBehaviorConfigs() {
    // chiamata durante boot con preferences globale già aperto
    String raw = preferences.getString("behaviors", "");
    if (raw.length() == 0) {
        behaviorConfigCount = 1; activeBehaviorConfig = 0;
        strncpy(behaviorConfigs[0].name, "Default", 24);
        memset(behaviorConfigs[0].rules, 0, sizeof(behaviorConfigs[0].rules));
        behaviorConfigs[0].rules[0] = { SEN_TICKLE_HEAD,  {0x13,0x00,39,3,1,1}, 6 };
        behaviorConfigs[0].rules[1] = { SEN_PULL_TAIL,    {0x13,0x00,39,3,8,1}, 6 };
        behaviorConfigs[0].rules[2] = { SEN_UPSIDE_DOWN,  {0x14,255,0,0},       4 };
        behaviorConfigs[0].rules[3] = { SEN_UPRIGHT,      {0x14,0,255,0},       4 };
        saveBehaviorConfigs(); return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
    activeBehaviorConfig = doc["active"] | 0;
    behaviorConfigCount  = 0;
    for (JsonObject c : doc["configs"].as<JsonArray>()) {
        if (behaviorConfigCount >= MAX_CONFIGS) break;
        BehaviorConfig& cfg = behaviorConfigs[behaviorConfigCount++];
        strncpy(cfg.name, c["name"] | "Config", 24);
        memset(cfg.rules, 0, sizeof(cfg.rules));
        int ri = 0;
        for (JsonObject ro : c["rules"].as<JsonArray>()) {
            if (ri >= MAX_CFG_RULES) break;
            cfg.rules[ri].sensorId = ro["s"] | 0;
            cfg.rules[ri].len      = ro["l"] | 0;
            int bi = 0;
            for (int v : ro["b"].as<JsonArray>()) if (bi < 6) cfg.rules[ri].bytes[bi++] = (uint8_t)v;
            ri++;
        }
    }
}

void applyBehaviorRules(const FurbySensors& prev, const FurbySensors& cur) {
    if (activeBehaviorConfig < 0 || activeBehaviorConfig >= behaviorConfigCount) return;
    if (bleConnectedMs && millis() - bleConnectedMs < 3000) return;
    BehaviorConfig& cfg = behaviorConfigs[activeBehaviorConfig];
    bool curVals[SEN_COUNT]={}, prevVals[SEN_COUNT]={};
    curVals[SEN_ANT_L]=cur.antennaLeft;    prevVals[SEN_ANT_L]=prev.antennaLeft;
    curVals[SEN_ANT_R]=cur.antennaRight;   prevVals[SEN_ANT_R]=prev.antennaRight;
    curVals[SEN_ANT_F]=cur.antennaForward; prevVals[SEN_ANT_F]=prev.antennaForward;
    curVals[SEN_ANT_B]=cur.antennaBack;    prevVals[SEN_ANT_B]=prev.antennaBack;
    curVals[SEN_TICKLE_HEAD]=cur.tickleHead;     prevVals[SEN_TICKLE_HEAD]=prev.tickleHead;
    curVals[SEN_TICKLE_TUMMY]=cur.tickleTummy;   prevVals[SEN_TICKLE_TUMMY]=prev.tickleTummy;
    curVals[SEN_TICKLE_R]=cur.tickleRight;       prevVals[SEN_TICKLE_R]=prev.tickleRight;
    curVals[SEN_TICKLE_L]=cur.tickleLeft;        prevVals[SEN_TICKLE_L]=prev.tickleLeft;
    curVals[SEN_PULL_TAIL]=cur.pullTail;         prevVals[SEN_PULL_TAIL]=prev.pullTail;
    curVals[SEN_PUSH_TONGUE]=cur.pushTongue;     prevVals[SEN_PUSH_TONGUE]=prev.pushTongue;
    curVals[SEN_UPRIGHT]=cur.upright;            prevVals[SEN_UPRIGHT]=prev.upright;
    curVals[SEN_UPSIDE_DOWN]=cur.upsideDown;     prevVals[SEN_UPSIDE_DOWN]=prev.upsideDown;
    curVals[SEN_SIDE_R]=cur.onRightSide;         prevVals[SEN_SIDE_R]=prev.onRightSide;
    curVals[SEN_SIDE_L]=cur.onLeftSide;          prevVals[SEN_SIDE_L]=prev.onLeftSide;
    curVals[SEN_LEAN_BACK]=cur.leanBack;         prevVals[SEN_LEAN_BACK]=prev.leanBack;
    curVals[SEN_TILT_R]=cur.tiltRight;           prevVals[SEN_TILT_R]=prev.tiltRight;
    curVals[SEN_TILT_L]=cur.tiltLeft;            prevVals[SEN_TILT_L]=prev.tiltLeft;
    for (int r = 0; r < MAX_CFG_RULES; r++) {
        BehaviorRule& rule = cfg.rules[r];
        if (rule.sensorId == SEN_NONE || rule.len == 0) continue;
        if (curVals[rule.sensorId] && !prevVals[rule.sensorId]) furbyWrite(rule.bytes, rule.len);
    }
    if (!isProcessing && !wakeUpTriggered) {
        for (int s = 1; s < SEN_COUNT; s++) {
            if (curVals[s] && !prevVals[s]) {
                pendingTrigger = TRG_SENSOR; pendingSensorId = (uint8_t)s;
                pendingEvent = EVT_NONE; wakeUpTriggered = true; break;
            }
        }
    }
}
