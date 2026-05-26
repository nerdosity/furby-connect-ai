#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SD_MMC.h>
#include <esp_camera.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"
#include "mbedtls/platform.h"
#include "esp_heap_caps.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <SPIFFS.h>

#define FW_VERSION "2026.0522.0151"

#define BLE_SVC_DEFAULT  "dab91435-b5a1-e29c-b041-bcd562613bde"
#define BLE_CHAR_DEFAULT "dab91383-b5a1-e29c-b041-bcd562613bde"

// ── Pinout camera OV2640 ─────────────────────────────────────────────────────
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     38
#define SIOD_GPIO_NUM     8
#define SIOC_GPIO_NUM     7
#define Y9_GPIO_NUM       21
#define Y8_GPIO_NUM       39
#define Y7_GPIO_NUM       40
#define Y6_GPIO_NUM       42
#define Y5_GPIO_NUM       46
#define Y4_GPIO_NUM       48
#define Y3_GPIO_NUM       47
#define Y2_GPIO_NUM       45
#define VSYNC_GPIO_NUM    17
#define HREF_GPIO_NUM     18
#define PCLK_GPIO_NUM     41

// ── SD card 1-bit MMC ────────────────────────────────────────────────────────
#define SD_MMC_CLK        16
#define SD_MMC_CMD        43
#define SD_MMC_D0         44

// ── I2S ─────────────────────────────────────────────────────────────────────
#define I2S_MCLK          10
#define I2S_BCLK          11
#define I2S_LRCK          12
#define I2S_DOUT          14
#define I2S_DIN           13
#define I2S_NUM           I2S_NUM_0
#define I2S_MIC_NUM       I2S_NUM_0

// ── VAD ──────────────────────────────────────────────────────────────────────
#define VAD_THRESHOLD_DEFAULT  3000
#define VAD_SILENCE_MS         1200
#define VAD_SPEECH_MS          400

// ── I2C bus ──────────────────────────────────────────────────────────────────
#define I2C_SCL_PIN       7
#define I2C_SDA_PIN       8
#define ES8311_ADDR       0x18
#define ES7210_ADDR       0x40
#define CH32_ADDR         0x24

#define WAKE_BTN_PIN      0
#define MAX_WIFI_NETS     5

// ── ES7210 registri ──────────────────────────────────────────────────────────
#define ES7210_RESET_REG00       0x00
#define ES7210_MAINCLK_REG02     0x02
#define ES7210_LRCK_DIVH_REG04   0x04
#define ES7210_LRCK_DIVL_REG05   0x05
#define ES7210_POWER_DOWN_REG06  0x06
#define ES7210_OSR_REG07         0x07
#define ES7210_SDP_IFACE1_REG11  0x11
#define ES7210_SDP_IFACE2_REG12  0x12
#define ES7210_ADC1_VOL_REG1B    0x1B
#define ES7210_ADC2_VOL_REG1C    0x1C
#define ES7210_ADC3_VOL_REG1D    0x1D
#define ES7210_ADC4_VOL_REG1E    0x1E
#define ES7210_ADC12_HPF2_REG22  0x22
#define ES7210_ADC12_HPF1_REG23  0x23
#define ES7210_ADC34_HPF2_REG20  0x20
#define ES7210_ADC34_HPF1_REG21  0x21
#define ES7210_ANALOG_REG40      0x40
#define ES7210_MIC12_BIAS_REG41  0x41
#define ES7210_MIC34_BIAS_REG42  0x42
#define ES7210_MIC1_GAIN_REG43   0x43
#define ES7210_MIC2_GAIN_REG44   0x44
#define ES7210_MIC1_POWER_REG47  0x47
#define ES7210_MIC2_POWER_REG48  0x48
#define ES7210_MIC12_POWER_REG4B 0x4B
#define ES7210_MIC34_POWER_REG4C 0x4C

// ── ES8311 registri ──────────────────────────────────────────────────────────
#define ES8311_RESET_REG00        0x00
#define ES8311_CLK_MANAGER_REG01  0x01
#define ES8311_CLK_MANAGER_REG02  0x02
#define ES8311_CLK_MANAGER_REG03  0x03
#define ES8311_CLK_MANAGER_REG04  0x04
#define ES8311_CLK_MANAGER_REG05  0x05
#define ES8311_CLK_MANAGER_REG06  0x06
#define ES8311_CLK_MANAGER_REG07  0x07
#define ES8311_CLK_MANAGER_REG08  0x08
#define ES8311_SDPIN_REG09        0x09
#define ES8311_SDPOUT_REG0A       0x0A
#define ES8311_ADC_REG1C          0x1C
#define ES8311_SYSTEM_REG0D       0x0D
#define ES8311_SYSTEM_REG0E       0x0E
#define ES8311_SYSTEM_REG12       0x12
#define ES8311_SYSTEM_REG13       0x13
#define ES8311_DAC_REG11          0x11
#define ES8311_DAC_REG12          0x12
#define ES8311_DAC_REG14          0x14
#define ES8311_DAC_REG31          0x31
#define ES8311_DAC_REG32          0x32
#define ES8311_DAC_REG37          0x37
#define ES8311_GP_REG45           0x45

// ── Struct / enum ─────────────────────────────────────────────────────────────
struct WifiNet { String ssid; String pass; };

#define MAX_CFG_RULES   16
#define MAX_CONFIGS     8

struct BehaviorRule {
    uint8_t sensorId;
    uint8_t bytes[6];
    uint8_t len;
};

enum SensorId : uint8_t {
    SEN_NONE=0, SEN_ANT_L, SEN_ANT_R, SEN_ANT_F, SEN_ANT_B,
    SEN_TICKLE_HEAD, SEN_TICKLE_TUMMY, SEN_TICKLE_R, SEN_TICKLE_L,
    SEN_PULL_TAIL, SEN_PUSH_TONGUE,
    SEN_UPRIGHT, SEN_UPSIDE_DOWN, SEN_SIDE_R, SEN_SIDE_L,
    SEN_LEAN_BACK, SEN_TILT_R, SEN_TILT_L,
    SEN_COUNT
};

struct FurbySensors {
    bool antennaLeft    = false;
    bool antennaRight   = false;
    bool antennaForward = false;
    bool antennaBack    = false;
    bool tickleHead     = false;
    bool tickleTummy    = false;
    bool tickleRight    = false;
    bool tickleLeft     = false;
    bool pullTail       = false;
    bool pushTongue     = false;
    bool upright        = false;
    bool upsideDown     = false;
    bool onRightSide    = false;
    bool onLeftSide     = false;
    bool leanBack       = false;
    bool tiltRight      = false;
    bool tiltLeft       = false;
    uint8_t rawB1 = 0, rawB2 = 0, rawB3 = 0, rawB4 = 0;
};

struct BehaviorConfig {
    char     name[24];
    BehaviorRule rules[MAX_CFG_RULES];
};

struct FurbyDevice {
    String name; String addr; esp_ble_addr_type_t addrType;
    FurbyDevice() : addrType(BLE_ADDR_TYPE_PUBLIC) {}
    FurbyDevice(const String& n, const String& a, esp_ble_addr_type_t t) : name(n), addr(a), addrType(t) {}
};

struct FurbyActionDef { const char* id; const char* label; uint8_t cmd[6]; uint8_t len; };

enum ConsequenceType : uint8_t {
    CSQ_NONE = 0, CSQ_FURBY_ACTION, CSQ_TTS_FIXED, CSQ_PROMPT_FIXED, CSQ_PROMPT_LLM, CSQ_PROMPT_AUTO
};
enum TriggerType : uint8_t { TRG_VAD=0, TRG_BUTTON=1, TRG_SENSOR=2 };
enum EventType   : uint8_t { EVT_NONE=0, EVT_VAD, EVT_BUTTON };

#define MAX_REACTIONS        3
#define MAX_CONSEQUENCES     3
#define MAX_EVENT_BEHAVIORS  64
#define MAX_FURBY_SCAN       8
#define STT_BUF_MAX_SAMPLES  128000

struct Consequence {
    ConsequenceType type         = CSQ_NONE;
    char action_id[32]           = {};
    char text[256]               = {};
    bool snapshot                = false;
    bool ctx_beh_name            = false;  // includi nome behavior nel prompt
    bool ctx_sensor              = false;  // includi sensore stimolato nel prompt
    char reactions[MAX_REACTIONS][32] = {};
    uint8_t reaction_count       = 0;
};

struct EventBehavior {
    char        id[32]           = {};
    TriggerType trigger          = TRG_VAD;
    uint8_t     sensor_id        = 0;
    char        name[48]         = {};
    Consequence consequences[MAX_CONSEQUENCES] = {};
    uint8_t     consequence_count = 0;
};

// Allocata in PSRAM - una sola istanza attiva alla volta
struct Personality {
    char         id[32]                        = {};
    char         name[48]                      = {};
    char         prompt[4096]                  = {};
    char         voice_id[64]                  = {};
    char         lang[8]                       = {};  // "it" o "en"
    EventBehavior behaviors[MAX_EVENT_BEHAVIORS] = {};
    int          behavior_count                = 0;
};

struct BleAutoRecCtx { String addr; String name; esp_ble_addr_type_t atype; };

// ── ArduinoJson PSRAM allocator ───────────────────────────────────────────────
class SpiRamAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t size) override {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
        return p;
    }
    void deallocate(void* ptr) override {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t new_size) override {
        // heap_caps_realloc: semantica POSIX, ptr rimane valido se fallisce.
        void* p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) p = heap_caps_realloc(ptr, new_size, MALLOC_CAP_8BIT);
        return p;
    }
    static ArduinoJson::Allocator* instance() {
        static SpiRamAllocator alloc;
        return &alloc;
    }
};
// Crea un JsonDocument che alloca preferibilmente in PSRAM (ArduinoJson v7)
#define JsonDocPsram() ArduinoJson::JsonDocument(SpiRamAllocator::instance())

// ── Globals extern ────────────────────────────────────────────────────────────
extern Preferences  preferences;
extern WebServer    server;
extern DNSServer    dnsServer;
extern bool         isConfigMode;
extern bool         sdAvailable;
extern bool         camActive;

extern WifiNet      wifiNets[MAX_WIFI_NETS];
extern int          wifiNetCount;

extern String llm_provider;
extern String llm_model;
extern String openai_api_key;
extern String claude_api_key;
extern String elevenlabs_api_key;
extern String elevenlabs_voice_id;
extern String el_audio_fmt;

extern String ble_service_uuid;
extern String ble_char_uuid_tx;
extern BLEUUID serviceUUID;
extern BLEUUID charUUID_GPWrite;
extern BLEUUID charUUID_GPListen;
extern BLEUUID charUUID_NWrite;
extern BLEUUID charUUID_NListen;
extern volatile boolean doConnect;
extern String   pendingConnAddr;
extern String   pendingConnName;
extern esp_ble_addr_type_t pendingConnAddrType;
extern volatile bool bleConnecting;
extern volatile bool bleUserDisconnect;
extern boolean  connected;
extern boolean  bleScanning;
extern SemaphoreHandle_t        bleMutex;
extern BLEClient*               pBleClient;
extern BLERemoteCharacteristic* pRemoteCharacteristicTX;
extern BLERemoteCharacteristic* pCharGPListen;
extern BLERemoteCharacteristic* pCharNWrite;
extern BLERemoteCharacteristic* pCharNListen;
extern BLEAdvertisedDevice*     myDevice;
extern String   ble_last_name;
extern String   ble_last_addr;
extern int      ble_battery_pct;

extern FurbyDevice furbyList[MAX_FURBY_SCAN];
extern int         furbyListCount;

extern FurbySensors furbyState;
extern FurbySensors prevFurbyState;
extern unsigned long lastSensorMs;
extern unsigned long bleConnectedMs;

extern BehaviorConfig behaviorConfigs[MAX_CONFIGS];
extern int   behaviorConfigCount;
extern int   activeBehaviorConfig;

extern const char* SENSOR_NAMES[];
extern const char* SENSOR_NAMES_EN[];
extern const FurbyActionDef FURBY_ACTIONS[];
extern const int   FURBY_ACTIONS_COUNT;

extern String        gPersonalityPrompt;
extern String        gPersonalityVoiceId;
extern String        gPersonalityLang;
extern String        gCamDescPrompt;

extern int           camStreamQuality;   // JPEG quality stream (4-63)
extern framesize_t   camStreamSize;      // risoluzione stream
extern int           camSnapQuality;     // JPEG quality snapshot LLM (4-63)
extern framesize_t   camSnapSize;        // risoluzione snapshot LLM
extern int           camFlicker;         // 0=nessuno, 50=50Hz, 60=60Hz
extern int           camGainCeiling;     // 0-6 (GAINCEILING_2X..GAINCEILING_128X)
extern int           camBrightness;      // -2..+2
extern int           camAgc;            // 1=auto, 0=manual
extern volatile uint32_t camLastUsed;
void camTouch();
// Array in PSRAM - allocato in setup() via gEventBehaviorsInit()
extern EventBehavior* gEventBehaviors;
extern int            gEventBehaviorCount;

// Unica personalità attiva - allocata in PSRAM
extern Personality*  gpActivePers;
extern int           gActivePersonality;  // indice nel JSON
extern int           gDebugPersonality;   // -1 = usa quella attiva

extern volatile TriggerType pendingTrigger;
extern volatile uint8_t     pendingSensorId;
extern volatile EventType   pendingEvent;
extern volatile bool isSpeaking;
extern volatile bool wakeUpTriggered;
extern volatile bool isProcessing;
extern volatile bool gDryRun;
extern volatile int  currentAmplitude;
extern bool gSimSkipTts;
extern bool gSimSkipBle;

extern int  vad_threshold;
extern int  mic_gain;
extern bool vadEnabled;
extern volatile int  micRmsLive;
extern volatile int  micRmsLive2;
extern volatile bool micVadActive;
extern volatile bool micTestActive;

extern int16_t* gSttBuf;
extern volatile int gSttLen;
extern bool sttEnabled;

extern uint8_t ch32PortState;
