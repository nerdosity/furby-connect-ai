#include <Arduino.h>
#include <Wire.h>
#include <math.h>
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
#include "AudioGeneratorMP3.h"
#include "AudioFileSourceBuffer.h"
#include "AudioFileSourcePROGMEM.h"

#define FW_VERSION "2.0.0"

// ==========================================
// PINOUT E CONFIG HARDWARE
// (da schema ESP32-S3-CAM-OVxxxx Waveshare)
// ==========================================

// Camera OV2640
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

// SD Card (1-bit MMC mode)
#define SD_MMC_CLK        16
#define SD_MMC_CMD        43
#define SD_MMC_D0         44

// I2S verso ES8311 (TX speaker) + da ES7210 (RX mic) — da esempi Waveshare 03/06
// ES7210 è slave: condivide MCLK/BCLK/LRCK con ES8311, solo DIN diverso
#define I2S_MCLK          10   // MCK — condiviso TX+RX
#define I2S_BCLK          11   // BCK — condiviso TX+RX
#define I2S_LRCK          12   // WS  — condiviso TX+RX
#define I2S_DOUT          14   // DOUT → ES8311 DAC
#define I2S_DIN           13   // DIN  ← ES7210 SDOUT
#define I2S_NUM           I2S_NUM_0
// ES7210 è slave sul clock di I2S_NUM_0; si usa full-duplex TX+RX sullo stesso driver
#define I2S_MIC_NUM       I2S_NUM_0

// Soglia VAD: ampiezza RMS sopra cui si considera "parlato"
// Con ES7210 a 30dB di gain i valori tipici sono: silenzio ~200-600, voce ~3000-15000
// Regolabile dalla UI web (tab Sensori → sezione VAD)
#define VAD_THRESHOLD_DEFAULT  3000
// Durata minima del silenzio dopo il parlato prima di triggerare (ms)
#define VAD_SILENCE_MS         1200
// Durata minima del parlato per considerarlo valido e non rumore (ms)
#define VAD_SPEECH_MS          400

// I2C bus (ES8311 + ES7210 + IO expander, da i2c.h Waveshare: SDA=8, SCL=7)
#define I2C_SCL_PIN       7
#define I2C_SDA_PIN       8

#define ES8311_ADDR       0x18
#define ES7210_ADDR       0x40
#define CH32_ADDR         0x24  // IO expander Waveshare (stesso chip del repo ufficiale)

// Wake button (BOOT, schema: GPIO0)
#define WAKE_BTN_PIN      0

// Numero massimo di reti WiFi memorizzabili
#define MAX_WIFI_NETS     5

// ==========================================
// ES7210 — registri ADC microfoni (da es7210_reg.h ufficiale Waveshare)
// ==========================================
#define ES7210_RESET_REG00       0x00
#define ES7210_MAINCLK_REG02     0x02
#define ES7210_LRCK_DIVH_REG04   0x04
#define ES7210_LRCK_DIVL_REG05   0x05
#define ES7210_POWER_DOWN_REG06  0x06
#define ES7210_OSR_REG07         0x07
#define ES7210_SDP_IFACE1_REG11  0x11
#define ES7210_SDP_IFACE2_REG12  0x12
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

// ==========================================
// ES8311 — registri
// ==========================================
#define ES8311_RESET_REG00        0x00
#define ES8311_CLK_MANAGER_REG01  0x01
#define ES8311_CLK_MANAGER_REG02  0x02
#define ES8311_CLK_MANAGER_REG03  0x03
#define ES8311_CLK_MANAGER_REG04  0x04
#define ES8311_CLK_MANAGER_REG05  0x05
#define ES8311_CLK_MANAGER_REG06  0x06
#define ES8311_CLK_MANAGER_REG07  0x07
#define ES8311_CLK_MANAGER_REG08  0x08
#define ES8311_SDPIN_REG09        0x09  // Serial Data Port IN  (ADC → I2S)
#define ES8311_SDPOUT_REG0A       0x0A  // Serial Data Port OUT (I2S → DAC)
#define ES8311_ADC_REG1C          0x1C  // ADC equalizer bypass
#define ES8311_SYSTEM_REG0D       0x0D  // power up analog
#define ES8311_SYSTEM_REG0E       0x0E  // enable PGA/ADC
#define ES8311_SYSTEM_REG12       0x12  // power up DAC
#define ES8311_SYSTEM_REG13       0x13  // enable HP output driver
#define ES8311_DAC_REG11          0x11
#define ES8311_DAC_REG12          0x12
#define ES8311_DAC_REG14          0x14
#define ES8311_DAC_REG31          0x31  // DAC mute control
#define ES8311_DAC_REG32          0x32  // DAC volume (0=mute, 0xFF=max)
#define ES8311_DAC_REG37          0x37  // DAC equalizer
#define ES8311_GP_REG45           0x45

// ==========================================
// GLOBALI
// ==========================================
Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;
bool isConfigMode = false;
bool sdAvailable  = false;
bool camActive    = false; // true = esp_camera_init() completata con successo

// WiFi: lista ordinata per priorità (indice 0 = priorità massima)
struct WifiNet { String ssid; String pass; };
WifiNet wifiNets[MAX_WIFI_NETS];
int wifiNetCount = 0;

// API
String llm_provider  = "openai";   // "openai" | "claude"
String llm_model     = "gpt-4o-mini";
String openai_api_key = "";
String claude_api_key = "";
String elevenlabs_api_key  = "";
String elevenlabs_voice_id = "pNInz6obpgDQGcFmaJcg";
String el_audio_fmt        = "pcm";  // "pcm" (pro) | "mp3" (free)

// BLE — UUID reali Furby Connect (da github.com/pdjstone/furby-web-bluetooth)
String ble_service_uuid = "dab91435-b5a1-e29c-b041-bcd562613bde";
String ble_char_uuid_tx = "dab91383-b5a1-e29c-b041-bcd562613bde"; // GeneralPlusWrite
static BLEUUID serviceUUID("dab91435-b5a1-e29c-b041-bcd562613bde");
static BLEUUID charUUID_GPWrite("dab91383-b5a1-e29c-b041-bcd562613bde");  // GeneralPlusWrite
static BLEUUID charUUID_GPListen("dab91382-b5a1-e29c-b041-bcd562613bde"); // GeneralPlusListen (notify)
static BLEUUID charUUID_NWrite("dab90757-b5a1-e29c-b041-bcd562613bde");   // NordicWrite
static BLEUUID charUUID_NListen("dab90756-b5a1-e29c-b041-bcd562613bde");  // NordicListen (notify)
static volatile boolean doConnect = false;
static String  pendingConnAddr = "";
static String  pendingConnName = "";
static esp_ble_addr_type_t pendingConnAddrType = BLE_ADDR_TYPE_RANDOM;
static volatile bool bleConnecting = false; // task di connessione in corso
static volatile bool bleUserDisconnect = false; // l'utente ha richiesto disconnessione manuale
static boolean connected  = false;
static boolean bleScanning = false;
static BLEClient*             pBleClient               = nullptr;
static BLERemoteCharacteristic* pRemoteCharacteristicTX  = nullptr; // GeneralPlusWrite
static BLERemoteCharacteristic* pCharGPListen            = nullptr;
static BLERemoteCharacteristic* pCharNWrite              = nullptr;
static BLERemoteCharacteristic* pCharNListen             = nullptr;
static BLEAdvertisedDevice*   myDevice                  = nullptr;
String ble_last_name = "";
String ble_last_addr = "";
int    ble_battery_pct = -1;   // -1 = non rilevata; aggiornata da 0x22/0x21 o Battery Service

// Lista Furby trovati nell'ultimo scan (solo device riconosciuti come Furby)
#define MAX_FURBY_SCAN 8
struct FurbyDevice {
    String name; String addr; esp_ble_addr_type_t addrType;
    FurbyDevice() : addrType(BLE_ADDR_TYPE_PUBLIC) {}
    FurbyDevice(const String& n, const String& a, esp_ble_addr_type_t t) : name(n), addr(a), addrType(t) {}
};
static FurbyDevice furbyList[MAX_FURBY_SCAN];
static int furbyListCount = 0;

// ==========================================
// CONFIGURAZIONI COMPORTAMENTO (sensore → azione)
// ==========================================
// Ogni regola mappa un sensore a un comando BLE (fino a 6 byte)
#define MAX_CFG_RULES   16  // regole per configurazione
#define MAX_CONFIGS     8   // configurazioni salvabili

struct BehaviorRule {
    uint8_t sensorId;    // 0=disabilitato, vedi enum SensorId
    uint8_t bytes[6];    // payload BLE da inviare
    uint8_t len;         // lunghezza payload (1-6)
};

// ID sensori (corrispondono ai campi FurbySensors in ordine)
enum SensorId : uint8_t {
    SEN_NONE=0, SEN_ANT_L, SEN_ANT_R, SEN_ANT_F, SEN_ANT_B,
    SEN_TICKLE_HEAD, SEN_TICKLE_TUMMY, SEN_TICKLE_R, SEN_TICKLE_L,
    SEN_PULL_TAIL, SEN_PUSH_TONGUE,
    SEN_UPRIGHT, SEN_UPSIDE_DOWN, SEN_SIDE_R, SEN_SIDE_L,
    SEN_LEAN_BACK, SEN_TILT_R, SEN_TILT_L,
    SEN_COUNT
};

// Stato sensori Furby (aggiornato dalle notifiche GeneralPlusListen)
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
static FurbySensors furbyState;
static FurbySensors prevFurbyState;
static unsigned long lastSensorMs   = 0;
static unsigned long bleConnectedMs = 0; // millis() al momento della connessione BLE

struct BehaviorConfig {
    char     name[24];
    BehaviorRule rules[MAX_CFG_RULES];
};

static BehaviorConfig behaviorConfigs[MAX_CONFIGS];
static int   behaviorConfigCount  = 0;
static int   activeBehaviorConfig = 0;

// Nomi sensori per la UI
static const char* SENSOR_NAMES[] = {
    "—", "Antenna sx", "Antenna dx", "Antenna avanti", "Antenna indietro",
    "Tickle testa", "Tickle pancia", "Tickle dx", "Tickle sx",
    "Tira coda", "Spingi lingua",
    "Dritto", "Capovolto", "Lato dx", "Lato sx",
    "Inclinato back", "Inclinato dx", "Inclinato sx"
};

// ==========================================
// SISTEMA EVENTI / COMPORTAMENTI (event → conseguenze)
// ==========================================

// Azioni Furby invocabili via BLE — lista curata, comuni a tutti i Furby Connect
// Formato: cmd[0]=0x13 (specific), 0x00, input, index, subindex, specific
// oppure cmd[0]=0x14 (antenna), r, g, b
struct FurbyActionDef { const char* id; const char* label; uint8_t cmd[6]; uint8_t len; };
static const FurbyActionDef FURBY_ACTIONS[] = {
    // Reazioni fisiche (coccole)
    {"pet_happy",    "Coccola felice",          {0x13,0x00, 1,0,0,0}, 6},
    {"pet_reluctant","Coccola riluttante",       {0x13,0x00, 1,1,0,0}, 6},
    // Risate / solletico
    {"tickle_laugh", "Risata (solletico)",        {0x13,0x00, 2,0,0,0}, 6},
    {"belly_laugh",  "Risata di pancia",          {0x13,0x00, 2,3,0,0}, 6},
    {"laugh_snort",  "Risata con sbuffo",         {0x13,0x00, 2,3,0,6}, 6},
    // Peti e ruttini
    {"fart_musical", "Peto musicale",             {0x13,0x00, 7,0,0,0}, 6},
    {"fart_wet",     "Peto umido",                {0x13,0x00, 7,0,0,2}, 6},
    {"fart_silent",  "Silent but deadly",         {0x13,0x00, 7,3,0,3}, 6},
    {"burp",         "Rutto",                     {0x13,0x00, 7,3,0,0}, 6},
    {"burp_loud",    "Rutto forte",               {0x13,0x00,16,0,2,2}, 6},
    {"hiccup",       "Singhiozzo",                {0x13,0x00,16,0,0,0}, 6},
    // Musica / danza
    {"sing",         "Cantare",                   {0x13,0x00,17,0,0,0}, 6},
    {"beatbox",      "Beatbox",                   {0x13,0x00,17,0,0,5}, 6},
    {"dance",        "Ballare",                   {0x13,0x00,17,2,0,3}, 6},
    // Movimento
    {"shake",        "Tremare/agitato",            {0x13,0x00, 9,0,0,0}, 6},
    {"vomit",        "Vomitare",                  {0x13,0x00, 9,1,0,1}, 6},
    // Sonno
    {"sleep",        "Addormentarsi",             {0x13,0x00,12,0,0,0}, 6},
    {"snore",        "Russare",                   {0x13,0x00,12,3,0,0}, 6},
    {"lullaby",      "Ninna nanna",               {0x13,0x00,12,2,1,1}, 6},
    {"wakeup",       "Svegliarsi",                {0x13,0x00,13,0,0,0}, 6},
    // Emozioni
    {"hungry",       "Fame",                      {0x13,0x00,23,0,0,0}, 6},
    {"sick",         "Malato",                    {0x13,0x00,22,0,0,0}, 6},
    {"dropped",      "Caduto",                    {0x13,0x00,21,0,0,0}, 6},
    {"loud_noise",   "Rumore forte",              {0x13,0x00,20,0,0,0}, 6},
    // Conversazione
    {"convo_yes",    "Conversazione: si!",        {0x13,0x00, 8,0,0,0}, 6},
    {"convo_no",     "Conversazione: no",         {0x13,0x00, 8,1,0,0}, 6},
    {"convo_bored",  "Conversazione: annoiato",   {0x13,0x00, 8,2,0,0}, 6},
    {"eating",       "Mangiare",                  {0x13,0x00,14,0,0,0}, 6},
    // Antenna LED
    {"ant_red",      "Antenna rossa",             {0x14,255,  0,  0,0,0}, 4},
    {"ant_blue",     "Antenna blu",               {0x14,  0,  0,255,0,0}, 4},
    {"ant_green",    "Antenna verde",             {0x14,  0,255,  0,0,0}, 4},
    {"ant_off",      "Antenna spenta",            {0x14,  0,  0,  0,0,0}, 4},
};
static const int FURBY_ACTIONS_COUNT = (int)(sizeof(FURBY_ACTIONS)/sizeof(FURBY_ACTIONS[0]));

enum ConsequenceType : uint8_t {
    CSQ_NONE = 0,
    CSQ_FURBY_ACTION,  // BLE → Furby
    CSQ_TTS_FIXED,     // testo fisso → ElevenLabs
    CSQ_PROMPT_FIXED,  // prompt fisso → LLM → TTS
    CSQ_PROMPT_LLM     // LLM sceglie: action + speech_before + speech_after
};

// Trigger di un comportamento
// trigger=0 → VAD (microfono)
// trigger=1 → pulsante fisico ESP32
// trigger=2 → sensore Furby (usa sensor_id, stessa numerazione SensorId: 1=ant_sx … 17=tilt_sx)
enum TriggerType : uint8_t { TRG_VAD=0, TRG_BUTTON=1, TRG_SENSOR=2 };

// Alias di compatibilità (usato solo internamente da processStimulus)
enum EventType : uint8_t { EVT_NONE=0, EVT_VAD, EVT_BUTTON };

#define MAX_REACTIONS        3
#define MAX_CONSEQUENCES     3
#define MAX_EVENT_BEHAVIORS  16

struct Consequence {
    ConsequenceType type         = CSQ_NONE;
    char action_id[32]           = {};
    char text[256]               = {};
    bool snapshot                = false;
    char reactions[MAX_REACTIONS][32] = {};
    uint8_t reaction_count       = 0;
};

struct EventBehavior {
    char        id[32]           = {};
    TriggerType trigger          = TRG_VAD;
    uint8_t     sensor_id        = 0;   // valido solo se trigger==TRG_SENSOR
    char        name[48]         = {};
    Consequence consequences[MAX_CONSEQUENCES] = {};
    uint8_t     consequence_count = 0;
};

// Forward declarations
void saveBehaviorConfigs();
void applyBehaviorRules(const FurbySensors& prev, const FurbySensors& cur);
static bool connectToFurbyByAddr(BLEAddress bleAddr, const String& nameHint, esp_ble_addr_type_t addrType);
bool camInit();
void camDeinit();
static void i2s_write_mono(const int16_t* buf, int samples);
void generateAndPlayTTS_SD(String text);
void streamAndPlayTTS_RAM(String text);
String base64Encode(uint8_t* data, size_t length);
String callLLM(const String& base64Img, const String& systemPrompt, const String& userText);
String transcribeAudio();
static String elOutputFormat();
void loadEventBehaviors();
void saveEventBehaviors();
const FurbyActionDef* findFurbyAction(const char* id);
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText = "");
void processStimulusDefault(const String& base64Img);
void processStimulus(TriggerType trg, uint8_t sensorId);

struct BleAutoRecCtx { String addr; String name; esp_ble_addr_type_t atype; };

// Globals — sistema eventi/comportamenti
static String           gPersonalityPrompt;
static String           gPersonalityVoiceId;
static String           gCamDescPrompt;
static EventBehavior    gEventBehaviors[MAX_EVENT_BEHAVIORS];
static int              gEventBehaviorCount = 0;
volatile TriggerType    pendingTrigger  = TRG_VAD;
volatile uint8_t        pendingSensorId = 0;
// alias mantenuto per compatibilità con il codice VAD
volatile EventType      pendingEvent    = EVT_VAD;

volatile bool isSpeaking      = false;
volatile bool wakeUpTriggered = false;
volatile bool isProcessing    = false; // processStimulus in corso su task separato
volatile bool gDryRun         = false; // se true, salta azioni BLE (test senza Furby)
volatile int  currentAmplitude = 0;

// VAD (Voice Activity Detection)
int  vad_threshold = VAD_THRESHOLD_DEFAULT;
bool vadEnabled    = true;

// Mic live: aggiornato dal vadTask ogni ciclo, letto dalla UI via /debug/mic/rms
volatile int  micRmsLive   = 0;  // RMS MIC1 (canale L)
volatile int  micRmsLive2  = 0;  // RMS MIC2 (canale R)
volatile bool micVadActive  = false; // true se VAD ha rilevato parlato in corso
volatile bool micTestActive = false; // blocca vadTask durante test mic

// STT (Speech-to-Text via Whisper)
// Buffer PSRAM: max 8s @ 16kHz mono int16 = 256000 byte
#define STT_BUF_MAX_SAMPLES  128000   // 8s @ 16kHz mono
static int16_t* gSttBuf       = nullptr; // allocato in setup() da PSRAM
static volatile int gSttLen   = 0;       // campioni mono validi nel buffer
bool sttEnabled = false;                 // disabilitato di default — richiede chiave OpenAI

// ==========================================
// PERSISTENZA WiFi (lista reti)
// ==========================================
void saveWifiNets() {
    preferences.putInt("wifi_count", wifiNetCount);
    for (int i = 0; i < wifiNetCount; i++) {
        preferences.putString(("ws" + String(i)).c_str(), wifiNets[i].ssid);
        preferences.putString(("wp" + String(i)).c_str(), wifiNets[i].pass);
    }
}

void loadWifiNets() {
    wifiNetCount = preferences.getInt("wifi_count", 0);
    if (wifiNetCount > MAX_WIFI_NETS) wifiNetCount = MAX_WIFI_NETS;
    for (int i = 0; i < wifiNetCount; i++) {
        wifiNets[i].ssid = preferences.getString(("ws" + String(i)).c_str(), "");
        wifiNets[i].pass = preferences.getString(("wp" + String(i)).c_str(), "");
    }
}

// Aggiunge o aggiorna una rete; se esiste per SSID aggiorna solo la password
void upsertWifiNet(const String& ssid, const String& pass, int priority) {
    for (int i = 0; i < wifiNetCount; i++) {
        if (wifiNets[i].ssid == ssid) {
            wifiNets[i].pass = pass;
            // sposta in posizione priority
            WifiNet tmp = wifiNets[i];
            for (int j = i; j > priority; j--) wifiNets[j] = wifiNets[j-1];
            wifiNets[priority] = tmp;
            saveWifiNets();
            return;
        }
    }
    if (wifiNetCount < MAX_WIFI_NETS) {
        // inserisce in posizione priority
        int pos = min(priority, wifiNetCount);
        for (int j = wifiNetCount; j > pos; j--) wifiNets[j] = wifiNets[j-1];
        wifiNets[pos] = {ssid, pass};
        wifiNetCount++;
        saveWifiNets();
    }
}

void removeWifiNet(int idx) {
    if (idx < 0 || idx >= wifiNetCount) return;
    for (int i = idx; i < wifiNetCount - 1; i++) wifiNets[i] = wifiNets[i+1];
    wifiNetCount--;
    saveWifiNets();
}

// ==========================================
// IO EXPANDER (Waveshare, addr 0x24)
// Registro 0x02 = mode (0xff = tutti output)
// Registro 0x03 = output bitmask
// ==========================================
uint8_t ch32PortState = 0x00;

void ch32WritePort(uint8_t value) {
    uint8_t data[2] = {0x03, value};
    Wire.beginTransmission(CH32_ADDR);
    uint8_t written = Wire.write(data, 2);
    uint8_t err = Wire.endTransmission();
    Serial.printf("IO exp write: port=0x%02X written=%d err=%d\n", value, written, err);
}

void ch32SetBit(uint8_t bit, bool val) {
    if (val) ch32PortState |=  (1 << bit);
    else     ch32PortState &= ~(1 << bit);
    ch32WritePort(ch32PortState);
}

void ch32Init() {
    uint8_t modeData[2] = {0x02, 0xFF};
    Wire.beginTransmission(CH32_ADDR);
    uint8_t written = Wire.write(modeData, 2);
    uint8_t err = Wire.endTransmission();
    Serial.printf("IO exp init mode: written=%d err=%d\n", written, err);
    ch32PortState = 0x00;
    ch32WritePort(ch32PortState);
    ch32SetBit(6, true);  // IO6 = alimentazione codec audio
    ch32SetBit(4, true);  // IO4 = PA_EN amplificatore (sempre ON come da esempio Waveshare)
    Serial.println("IO expander: init OK, IO6+IO4=1");
}

void setAmplifier(bool enable) {
    Serial.printf("setAmplifier(%s) port sarà 0x%02X\n", enable?"ON":"OFF",
        enable ? (ch32PortState | (1<<4)) : (ch32PortState & ~(1<<4)));
    ch32SetBit(4, enable);
}

// Legge tensione batteria dall'ADC dell'IO expander (reg 0x06, 12-bit, Vref=3.3V).
// Il partitore sul PCB Waveshare è 1:2 → moltiplica per 2 per ottenere la tensione reale.
// Restituisce mV, -1 se I2C fallisce.
int readBatteryMv() {
    Wire.beginTransmission(CH32_ADDR);
    Wire.write(0x06);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom((uint8_t)CH32_ADDR, (uint8_t)2) != 2) return -1;
    uint8_t lo = Wire.read(), hi = Wire.read();
    uint16_t raw = (uint16_t)(hi << 8 | lo);
    // raw è 12-bit (0-4095), Vref=3.3V, partitore 1:2
    return (int)((uint32_t)raw * 3300 * 2 / 4095);
}

// ==========================================
// ES8311 codec
// ==========================================
void es8311WriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg); Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Serial.printf("ES8311 I2C err reg=0x%02X val=0x%02X err=%d\n", reg, val, err);
}

void initES8311() {
    Serial.println("ES8311: init...");

    // Reset → normal op → power-on
    es8311WriteReg(ES8311_RESET_REG00, 0x1F);
    delay(20);
    es8311WriteReg(ES8311_RESET_REG00, 0x00);
    es8311WriteReg(ES8311_RESET_REG00, 0x80);

    // REG01: abilita tutti i clock interni, sorgente MCLK dal pin
    es8311WriteReg(ES8311_CLK_MANAGER_REG01, 0x3F);

    // REG02-08: coefficienti per MCLK=4096000Hz, Fs=16000Hz
    // Dalla tabella coeff_div: {4096000,16000, pre_div=1,pre_multi=0, adc_div=1,dac_div=1,
    //   fs_mode=0, lrck_h=0x00,lrck_l=0xFF, bclk_div=4, adc_osr=0x10, dac_osr=0x10}
    es8311WriteReg(ES8311_CLK_MANAGER_REG02, 0x00); // pre_div=1 → (0)<<5, pre_multi=0 → (0)<<3
    es8311WriteReg(ES8311_CLK_MANAGER_REG03, 0x10); // fs_mode=0, adc_osr=0x10
    es8311WriteReg(ES8311_CLK_MANAGER_REG04, 0x10); // dac_osr=0x10
    es8311WriteReg(ES8311_CLK_MANAGER_REG05, 0x00); // (adc_div-1)<<4|(dac_div-1) = 0
    es8311WriteReg(ES8311_CLK_MANAGER_REG06, 0x03); // bclk_div=4 → 4-1=3
    es8311WriteReg(ES8311_CLK_MANAGER_REG07, 0x00); // lrck_h=0x00
    es8311WriteReg(ES8311_CLK_MANAGER_REG08, 0xFF); // lrck_l=0xFF

    // REG09/0A: formato I2S 16-bit slave (SDPIN=ADC in, SDPOUT=DAC out)
    // ES8311_RESOLUTION_16 → *reg |= (3<<2) = 0x0C
    es8311WriteReg(ES8311_SDPIN_REG09,  0x0C); // ADC  → I2S, 16-bit
    es8311WriteReg(ES8311_SDPOUT_REG0A, 0x0C); // I2S → DAC, 16-bit

    // Power-up analog (da es8311_init ufficiale)
    es8311WriteReg(ES8311_SYSTEM_REG0D, 0x01); // power up analog
    es8311WriteReg(ES8311_SYSTEM_REG0E, 0x02); // enable analog PGA + ADC modulator
    es8311WriteReg(ES8311_SYSTEM_REG12, 0x00); // power up DAC
    es8311WriteReg(ES8311_SYSTEM_REG13, 0x10); // enable output HP driver

    // ADC equalizer bypass (come nel sample ufficiale)
    es8311WriteReg(ES8311_ADC_REG1C, 0x6A);

    // DAC: bypass equalizer, unmute
    es8311WriteReg(ES8311_DAC_REG37, 0x08); // bypass DAC equalizer
    es8311WriteReg(ES8311_DAC_REG31, 0x00); // unmute DAC (bit6/5 = 0)

    // Volume iniziale 75% (reg = 0x80 + 75*0x7F/100 = 0x80 + 95 = 0xDF)
    es8311WriteReg(ES8311_DAC_REG32, 186);  // setVolume(75) = -3dB

    es8311WriteReg(ES8311_GP_REG45, 0x00);
    Serial.println("ES8311: init OK, volume=75%");
}

// ==========================================
// ES7210 — init ADC microfoni
// 16kHz, 16bit, 2 canali (MIC1 + MIC2), MCLK=256*Fs=4.096MHz
// ==========================================
void es7210WriteReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES7210_ADDR);
    Wire.write(reg); Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Serial.printf("ES7210 I2C err reg=0x%02X val=0x%02X err=%d\n", reg, val, err);
}

void initES7210() {
    Serial.println("ES7210: init...");

    // Reset
    es7210WriteReg(ES7210_RESET_REG00, 0xFF);
    delay(20);
    es7210WriteReg(ES7210_RESET_REG00, 0x32);  // normal op

    // Timing di init
    es7210WriteReg(0x09, 0x30);  // TIME_CONTROL0
    es7210WriteReg(0x0A, 0x30);  // TIME_CONTROL1

    // HPF per ADC1-4 (rimuove offset DC)
    es7210WriteReg(ES7210_ADC12_HPF1_REG23, 0x2A);
    es7210WriteReg(ES7210_ADC12_HPF2_REG22, 0x0A);
    es7210WriteReg(ES7210_ADC34_HPF1_REG21, 0x2A);
    es7210WriteReg(ES7210_ADC34_HPF2_REG20, 0x0A);

    // Formato I2S: 16-bit standard I2S, no TDM
    // SDP_IFACE1: bit_width=16bit → 0x60; fmt=I2S → 0x00; combined=0x60
    es7210WriteReg(ES7210_SDP_IFACE1_REG11, 0x60);
    es7210WriteReg(ES7210_SDP_IFACE2_REG12, 0x00);

    // Alimentazione analogica ADC
    es7210WriteReg(ES7210_ANALOG_REG40, 0xC3);

    // Bias MIC: 2.87V (necessario per mic condensatore)
    es7210WriteReg(ES7210_MIC12_BIAS_REG41, 0x70);
    es7210WriteReg(ES7210_MIC34_BIAS_REG42, 0x70);

    // Gain MIC: 30dB (valore enum=10, reg=10|0x10=0x1A)
    es7210WriteReg(ES7210_MIC1_GAIN_REG43, 0x1A);
    es7210WriteReg(ES7210_MIC2_GAIN_REG44, 0x1A);
    es7210WriteReg(0x45, 0x1A);  // MIC3
    es7210WriteReg(0x46, 0x1A);  // MIC4

    // Power on singoli MIC
    es7210WriteReg(ES7210_MIC1_POWER_REG47, 0x08);
    es7210WriteReg(ES7210_MIC2_POWER_REG48, 0x08);
    es7210WriteReg(0x49, 0x08);  // MIC3
    es7210WriteReg(0x4A, 0x08);  // MIC4

    // Clock: MCLK=4096000Hz, Fs=16kHz
    // Tabella {4096000,16000}: osr=0x20, adc_div=1, doubler=1, dll=1(bypass)
    // MAINCLK = adc_div|(doubler<<6)|(dll<<7) = 0x01|0x40|0x80 = 0xC1
    es7210WriteReg(ES7210_OSR_REG07,        0x20);
    es7210WriteReg(ES7210_MAINCLK_REG02,    0xC1);
    es7210WriteReg(ES7210_LRCK_DIVH_REG04,  0x01);
    es7210WriteReg(ES7210_LRCK_DIVL_REG05,  0x00);

    // Power down DLL (bypassed da MAINCLK)
    es7210WriteReg(ES7210_POWER_DOWN_REG06, 0x04);

    // Power on MIC1-4 bias + ADC + PGA
    es7210WriteReg(ES7210_MIC12_POWER_REG4B, 0x0F);
    es7210WriteReg(ES7210_MIC34_POWER_REG4C, 0x0F);

    // Enable ADC
    es7210WriteReg(ES7210_RESET_REG00, 0x71);
    es7210WriteReg(ES7210_RESET_REG00, 0x41);

    Serial.println("ES7210: init OK");
}

// ==========================================
// VAD TASK — Voice Activity Detection
// Gira su Core 0 insieme a lipSyncTask.
// Legge dal mic, calcola RMS su 512 samples,
// triggera processStimulus() dopo parlato valido
// seguito da silenzio >= VAD_SILENCE_MS.
// Non si attiva mentre il Furby sta parlando (isSpeaking).
// ==========================================
void vadTask(void* pvParameters) {
    const int BUF_SAMPLES = 512;          // stereo → 256 campioni per canale
    int16_t buf[BUF_SAMPLES];
    size_t bytesRead;

    uint32_t speechStart  = 0;
    uint32_t silenceStart = 0;
    bool inSpeech = false;

    // Filtro IIR passa-alto ~250Hz a 16kHz (alpha=0.91): filtra rumore meccanico
    // e vibrazioni strutturali, mantiene frequenze vocali (300-3400Hz)
    float hpL = 0, hpR = 0, prevL = 0, prevR = 0;
    const float HP_ALPHA = 0.91f;

    for (;;) {
        // Non ascoltare mentre il Furby parla o la WiFi non è pronta
        if (isSpeaking || isConfigMode || !vadEnabled) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            speechStart = 0; silenceStart = 0; inSpeech = false;
            hpL = hpR = prevL = prevR = 0;
            gSttLen = 0;
            continue;
        }

        if (micTestActive) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            continue;
        }

        i2s_read(I2S_MIC_NUM, buf, sizeof(buf), &bytesRead, portMAX_DELAY);
        int n = bytesRead / 2;

        // RMS dopo filtro passa-alto: esclude colpi meccanici e basse frequenze
        int64_t sumL = 0, sumR = 0;
        for (int i = 0; i < n; i += 2) {
            float xL = (float)buf[i];
            float xR = (float)buf[i+1];
            hpL = HP_ALPHA * (hpL + xL - prevL);
            hpR = HP_ALPHA * (hpR + xR - prevR);
            prevL = xL; prevR = xR;
            sumL += (int64_t)(hpL * hpL);
            sumR += (int64_t)(hpR * hpR);
        }
        int halfN = n / 2;
        int rms  = (int)sqrt((double)sumL / halfN);
        int rms2 = (int)sqrt((double)sumR / halfN);
        micRmsLive  = rms;
        micRmsLive2 = rms2;

        uint32_t now = millis();

        if (rms > vad_threshold) {
            if (!inSpeech) { inSpeech = true; speechStart = now; gSttLen = 0; }
            silenceStart = now;
            micVadActive = true;
            // Accumula canale L (mono) nel buffer STT se abilitato
            if (sttEnabled && gSttBuf) {
                for (int i = 0; i < n; i += 2) {
                    if (gSttLen < STT_BUF_MAX_SAMPLES)
                        gSttBuf[gSttLen++] = buf[i];
                }
            }
        } else {
            if (inSpeech) {
                uint32_t speechLen  = now - speechStart;
                uint32_t silenceLen = now - silenceStart;
                if (speechLen >= VAD_SPEECH_MS && silenceLen >= VAD_SILENCE_MS) {
                    inSpeech = false;
                    micVadActive = false;
                    Serial.printf("VAD: parlato rilevato -> trigger (STT buf=%d samples)\n", (int)gSttLen);
                    pendingTrigger = TRG_VAD;
                    pendingSensorId = 0;
                    pendingEvent = EVT_VAD;
                    wakeUpTriggered = true;
                }
            } else {
                micVadActive = false;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ==========================================
// BLE CLIENT & LIPSYNC
// ==========================================

// Resetta tutto lo stato BLE senza disconnettere (usato dopo disconnessione)
static void bleResetState() {
    connected               = false;
    bleConnectedMs          = 0;
    pRemoteCharacteristicTX = nullptr;
    pCharGPListen           = nullptr;
    pCharNWrite             = nullptr;
    pCharNListen            = nullptr;
}

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
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

      // Aggiorna lista — se già presente aggiorna nome e device (scan response arriva dopo l'adv)
      for (int i = 0; i < furbyListCount; i++) {
          if (furbyList[i].addr == addr) {
              if (name.length() && furbyList[i].name == addr) {
                  furbyList[i].name = name;
                  if (myDevice) delete myDevice;
                  myDevice = new BLEAdvertisedDevice(advertisedDevice);
                  Serial.println("BLE: nome aggiornato -> [" + name + "] " + addr);
              }
              return;
          }
      }
      if (furbyListCount < MAX_FURBY_SCAN) {
          esp_ble_addr_type_t atype = advertisedDevice.getAddressType();
          furbyList[furbyListCount++] = { name.length() ? name : addr, addr, atype };
          if (myDevice) delete myDevice;
          myDevice = new BLEAdvertisedDevice(advertisedDevice);
          Serial.println("BLE: trovato [" + (name.length() ? name : addr) + "] " + addr + " t=" + String(atype));
      }
  }
};

void bleDisconnect() {
    bleUserDisconnect = true;
    if (pBleClient && pBleClient->isConnected()) pBleClient->disconnect();
    bleResetState();
}

void bleScanStop() {
    if (!bleScanning) return;
    BLEDevice::getScan()->stop();
    bleScanning = false;
    Serial.println("BLE: scan interrotto manualmente");
}

void bleScanStart(int durationSec = 10) {
    if (bleScanning || connected) return;
    bleScanning    = true;
    doConnect      = false;
    furbyListCount = 0;
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

// Decodifica pacchetto sensori [0x21, b1, b2, b3, b4]
static void parseSensorPacket(uint8_t* data, size_t len) {
    if (len < 5 || data[0] != 0x21) return;
    uint8_t b1 = data[1], b2 = data[2], b3 = data[3], b4 = data[4];
    furbyState.rawB1 = b1; furbyState.rawB2 = b2;
    furbyState.rawB3 = b3; furbyState.rawB4 = b4;
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

    // Log sensori attivi
    String active;
    if (furbyState.antennaLeft)    active += "ANT_L ";
    if (furbyState.antennaRight)   active += "ANT_R ";
    if (furbyState.antennaForward) active += "ANT_F ";
    if (furbyState.antennaBack)    active += "ANT_B ";
    if (furbyState.tickleHead)     active += "TICKLE_HEAD ";
    if (furbyState.tickleTummy)    active += "TICKLE_TUMMY ";
    if (furbyState.tickleRight)    active += "TICKLE_R ";
    if (furbyState.tickleLeft)     active += "TICKLE_L ";
    if (furbyState.pullTail)       active += "PULL_TAIL ";
    if (furbyState.pushTongue)     active += "TONGUE ";
    if (furbyState.upright)        active += "UPRIGHT ";
    if (furbyState.upsideDown)     active += "UPSIDE_DOWN ";
    if (furbyState.onRightSide)    active += "SIDE_R ";
    if (furbyState.onLeftSide)     active += "SIDE_L ";
    if (furbyState.leanBack)       active += "LEAN_BACK ";
    if (furbyState.tiltRight)      active += "TILT_R ";
    if (furbyState.tiltLeft)       active += "TILT_L ";
    Serial.println("SEN: [" + active + "] b1=" + String(b1,HEX) +
                   " b2=" + String(b2,HEX) + " b3=" + String(b3,HEX) + " b4=" + String(b4,HEX));

    applyBehaviorRules(prevFurbyState, furbyState);
    prevFurbyState = furbyState;
}

static void gpListenNotifyCB(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (len == 0) return;
    String hex;
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) hex += "0";
        hex += String(data[i], HEX);
        if (i < len - 1) hex += " ";
    }
    Serial.println("BLE RX: " + hex);

    if (data[0] == 0x21) parseSensorPacket(data, len);

    // Risposta al keep-alive [0x20,0x06]: contiene dati di stato Furby
    // Byte 11 e 15 sono candidati batteria (da reverse engineering bluefluff)
    // Vengono loggati finché non confermiamo quale scende con le pile scariche
    if (data[0] == 0x22 && data[1] == 0x21 && len >= 16) {
        uint8_t b11 = data[11], b15 = data[15];
        Serial.printf("BLE status: b11=%d b15=%d (candidati batteria)\n", b11, b15);
        // Usa il byte più plausibile come percentuale (0-100); preferisce b15 se in range
        if (b15 <= 100) ble_battery_pct = b15;
        else if (b11 <= 100) ble_battery_pct = b11;
    }
}

// Logica di connessione comune
static bool connectToFurbyByAddr(BLEAddress bleAddr, const String& nameHint, esp_ble_addr_type_t addrType = BLE_ADDR_TYPE_RANDOM) {
    String addr = bleAddr.toString().c_str();
    Serial.println("BLE: connessione a " + addr + " (type=" + String(addrType) + ")");

    // Disconnetti e distruggi il client precedente se esiste.
    // NON fare deinit/init dello stack — è distruttivo e non necessario.
    if (pBleClient) {
        if (pBleClient->isConnected()) {
            pBleClient->disconnect();
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }
        delete pBleClient;
        pBleClient = nullptr;
    }
    bleResetState();

    pBleClient = BLEDevice::createClient();
    pBleClient->setClientCallbacks(new MyClientCallback());

    Serial.println("BLE: chiamata connect()...");
    if (!pBleClient->connect(bleAddr, addrType)) {
        Serial.println("BLE: connect() fallito");
        delete pBleClient;
        pBleClient = nullptr;
        return false;
    }
    Serial.println("BLE: GATT connesso, cerco servizi...");

    // Breve attesa per stabilizzare il GATT prima di discovery
    vTaskDelay(200 / portTICK_PERIOD_MS);

    BLERemoteService* svc = pBleClient->getService(serviceUUID);
    if (!svc) {
        Serial.println("BLE: servizio Furby non trovato");
        pBleClient->disconnect();
        delete pBleClient;
        pBleClient = nullptr;
        return false;
    }

    // retrieveDescriptors "Unknown" è benigno: Furby Connect non ha CCCD standard
    pRemoteCharacteristicTX = svc->getCharacteristic(charUUID_GPWrite);
    pCharGPListen           = svc->getCharacteristic(charUUID_GPListen);
    pCharNWrite             = svc->getCharacteristic(charUUID_NWrite);
    pCharNListen            = svc->getCharacteristic(charUUID_NListen);

    if (!pRemoteCharacteristicTX) {
        Serial.println("BLE: caratteristica GPWrite non trovata");
        pBleClient->disconnect();
        delete pBleClient;
        pBleClient = nullptr;
        return false;
    }

    if (pCharGPListen && pCharGPListen->canNotify())
        pCharGPListen->registerForNotify(gpListenNotifyCB);
    if (pCharNListen && pCharNListen->canNotify())
        pCharNListen->registerForNotify([](BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
            String hex;
            for (size_t i = 0; i < len; i++) { if (data[i]<0x10) hex+="0"; hex+=String(data[i],HEX); if(i<len-1)hex+=" "; }
            Serial.println("BLE RX Nordic: " + hex);
        });

    connected      = true;
    bleConnectedMs = millis();
    ble_last_addr  = addr;
    ble_last_name  = nameHint.length() ? nameHint : addr;
    ble_battery_pct = -1;
    if (ble_last_name == addr) {
        for (int i = 0; i < furbyListCount; i++) {
            if (furbyList[i].addr == addr && furbyList[i].name != addr) {
                ble_last_name = furbyList[i].name; break;
            }
        }
    }

    // Tenta lettura Battery Service standard BLE (UUID 0x180F / 0x2A19)
    try {
        BLERemoteService* batSvc = pBleClient->getService(BLEUUID((uint16_t)0x180F));
        if (batSvc) {
            BLERemoteCharacteristic* batChar = batSvc->getCharacteristic(BLEUUID((uint16_t)0x2A19));
            if (batChar && batChar->canRead()) {
                std::string val = batChar->readValue();
                if (val.length() > 0) {
                    ble_battery_pct = (uint8_t)val[0];
                    Serial.printf("BLE Battery Service: %d%%\n", ble_battery_pct);
                }
            }
        } else {
            Serial.println("BLE: Battery Service non trovato - attendo 0x22/0x21");
        }
    } catch(...) {}

    Serial.println("BLE: connesso a [" + ble_last_name + "] " + ble_last_addr);
    return true;
}

bool connectToFurby(BLEAdvertisedDevice* dev = nullptr) {
    if (!dev) dev = myDevice;
    if (!dev) { Serial.println("BLE: nessun device target"); return false; }
    String name = dev->haveName() ? dev->getName().c_str() : "";
    return connectToFurbyByAddr(dev->getAddress(), name, dev->getAddressType());
}

// Invia un comando grezzo su GeneralPlusWrite — logga sempre, rileva errori
static void furbyWrite(const uint8_t* buf, size_t len) {
    if (!connected || !pRemoteCharacteristicTX) return;

    // Log hex del payload
    String hex;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) hex += "0";
        hex += String(buf[i], HEX);
        if (i < len - 1) hex += " ";
    }
    Serial.println("BLE TX: " + hex);

    try {
        pRemoteCharacteristicTX->writeValue((uint8_t*)buf, len, false);
    } catch (...) {
        Serial.println("BLE TX: errore write, connessione persa");
        bleResetState();
    }
}

// Keep-alive: [0x20, 0x06] ogni 1s — verifica anche isConnected() per rilevare
// disconnessioni silenziose (il Furby spegne il BLE dopo ~30s senza keep-alive)
void keepAliveTask(void* pvParameters) {
    const uint8_t ka[] = {0x20, 0x06};
    for (;;) {
        if (connected) {
            if (pBleClient && !pBleClient->isConnected()) {
                Serial.println("BLE: connessione persa (rilevata da keep-alive)");
                bleResetState();
            } else {
                furbyWrite(ka, sizeof(ka));
                // Aggiorna il nome se è ancora l'addr placeholder
                if (ble_last_name == ble_last_addr && furbyListCount > 0) {
                    for (int i = 0; i < furbyListCount; i++) {
                        if (furbyList[i].addr == ble_last_addr && furbyList[i].name != ble_last_addr) {
                            ble_last_name = furbyList[i].name;
                            Serial.println("BLE: nome risolto -> [" + ble_last_name + "]");
                            break;
                        }
                    }
                }
            }
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

// Task BLE connect — gira su Core 0, non blocca il loop/web server
void bleConnectTask(void* pvParameters) {
    String addr = pendingConnAddr;
    String name = pendingConnName;
    esp_ble_addr_type_t atype = pendingConnAddrType;
    pendingConnAddr = "";
    pendingConnName = "";

    // Ferma eventuali scan in corso prima di connettersi
    if (bleScanning) {
        BLEDevice::getScan()->stop();
        vTaskDelay(300 / portTICK_PERIOD_MS);
        bleScanning = false;
    }

    bool ok;
    if (addr.length() > 0)
        ok = connectToFurbyByAddr(BLEAddress(addr.c_str()), name, atype);
    else
        ok = connectToFurby();

    if (!ok) Serial.println("BLE: connessione fallita");

    bleConnecting = false;
    vTaskDelete(NULL);
}

void lipSyncTask(void* pvParameters) {
    uint8_t open_[]   = {0x10, 0x00, 0x0A, 0x01};
    uint8_t closed_[] = {0x10, 0x00, 0x0A, 0x00};
    for (;;) {
        if (isSpeaking && connected) {
            furbyWrite(currentAmplitude > 500 ? open_ : closed_,
                       currentAmplitude > 500 ? sizeof(open_) : sizeof(closed_));
            vTaskDelay(80 / portTICK_PERIOD_MS);
        } else {
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}


// ==========================================
// HANDLERS WEB
// ==========================================
void handleRoot() {
    File f = SPIFFS.open("/index.html", "r");
    if (!f) { server.send(503, "text/plain", "index.html non trovato — eseguire uploadfs"); return; }
    server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    server.streamFile(f, "text/html; charset=utf-8");
    f.close();
}

// Connette immediatamente alle reti salvate in ordine di priorità
void handleWifiConnect() {
    if (wifiNetCount == 0) {
        server.send(200, "text/html",
            "<meta charset='UTF-8'><p>Nessuna rete salvata. <a href='/'>Torna</a></p>");
        return;
    }
    WiFi.disconnect(true); delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    bool ok = false;
    String connectedSSID;
    for (int i = 0; i < wifiNetCount && !ok; i++) {
        WiFi.begin(wifiNets[i].ssid.c_str(), wifiNets[i].pass.c_str());
        for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++) delay(500);
        if (WiFi.status() == WL_CONNECTED) { ok = true; connectedSSID = wifiNets[i].ssid; }
        else { WiFi.disconnect(true); delay(200); }
    }
    if (ok) {
        isConfigMode = false;
        String ip = WiFi.localIP().toString();
        server.send(200, "text/html",
            "<meta charset='UTF-8'>"
            "<meta http-equiv='refresh' content='3;url=http://" + ip + "/'>"
            "<style>body{font-family:sans-serif;background:#f0f4f8;display:flex;align-items:center;"
            "justify-content:center;height:100vh;margin:0}"
            ".box{background:#fff;border-radius:14px;padding:32px 40px;text-align:center;"
            "box-shadow:0 4px 20px rgba(0,0,0,.1)}"
            "h2{color:#16a34a;margin-bottom:8px}p{color:#5a6a8a;font-size:.9rem}</style>"
            "<div class='box'><h2>&#x2705; Connesso!</h2>"
            "<p>Rete: <b>" + connectedSSID + "</b><br>IP: <b>" + ip + "</b><br><br>"
            "Reindirizzo in 3 secondi...</p></div>");
    } else {
        server.send(200, "text/html",
            "<meta charset='UTF-8'>"
            "<meta http-equiv='refresh' content='3;url=/'>"
            "<style>body{font-family:sans-serif;background:#f0f4f8;display:flex;align-items:center;"
            "justify-content:center;height:100vh;margin:0}"
            ".box{background:#fff;border-radius:14px;padding:32px 40px;text-align:center;"
            "box-shadow:0 4px 20px rgba(0,0,0,.1)}"
            "h2{color:#dc2626;margin-bottom:8px}p{color:#5a6a8a;font-size:.9rem}</style>"
            "<div class='box'><h2>&#x274C; Connessione fallita</h2>"
            "<p>Controlla SSID e password.<br>Torno alla configurazione...</p></div>");
    }
}

// GET /api/wifi/scan — scansiona e restituisce JSON
void handleApiWifiScan() {
    int n = WiFi.scanNetworks(false, true);
    String j = "{\"nets\":[";
    for (int i = 0; i < n && i < 20; i++) {
        if (i) j += ",";
        String ssid = WiFi.SSID(i);
        ssid.replace("\"", "\\\"");
        bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
        j += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" + (open?"true":"false") + "}";
    }
    j += "]}";
    WiFi.scanDelete();
    server.send(200, "application/json", j);
}

// Aggiunge/aggiorna rete WiFi
void handleWifiAdd() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    int    prio = server.arg("prio").toInt();
    if (ssid.length() > 0) upsertWifiNet(ssid, pass, prio);
    server.sendHeader("Location", "/"); server.send(303);
}

// Rimuove rete
void handleWifiDel() {
    removeWifiNet(server.arg("idx").toInt());
    server.sendHeader("Location", "/"); server.send(303);
}

// Sposta rete su
void handleWifiUp() {
    int idx = server.arg("idx").toInt();
    if (idx > 0) {
        WifiNet tmp = wifiNets[idx];
        wifiNets[idx] = wifiNets[idx-1];
        wifiNets[idx-1] = tmp;
        saveWifiNets();
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// Sposta rete giù
void handleWifiDown() {
    int idx = server.arg("idx").toInt();
    if (idx < wifiNetCount - 1) {
        WifiNet tmp = wifiNets[idx];
        wifiNets[idx] = wifiNets[idx+1];
        wifiNets[idx+1] = tmp;
        saveWifiNets();
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// Ritorna lista modelli dal provider come JSON array di stringhe
// GET /api/models?provider=openai  oppure  ?provider=claude
void handleApiModels() {
    String provider = server.arg("provider");
    if (provider.length() == 0) provider = llm_provider;

    // Rilegge dalla NVS in caso la RAM sia stata svuotata per qualsiasi motivo
    if (openai_api_key.length() == 0) openai_api_key = preferences.getString("openai", "");
    if (claude_api_key.length() == 0) claude_api_key  = preferences.getString("claude", "");

    Serial.printf("[API-MODELS] provider=%s openai_key_len=%u claude_key_len=%u\n",
        provider.c_str(), openai_api_key.length(), claude_api_key.length());

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String result = "[";

    server.sendHeader("Cache-Control", "no-store");

    if (provider == "openai") {
        if (openai_api_key.length() == 0) {
            Serial.println("[API-MODELS] openai_api_key vuota -> ritorno errore");
            server.send(200, "application/json", "{\"ok\":false,\"error\":\"no_key\"}"); return;
        }
        http.begin(client, "https://api.openai.com/v1/models");
        http.addHeader("Authorization", "Bearer " + openai_api_key);
        int oaCode = http.GET();
        Serial.printf("[API-MODELS] OpenAI /models HTTP %d\n", oaCode);
        if (oaCode == 200) {
            String body = http.getString();
            Serial.printf("[API-MODELS] OpenAI body len=%u\n", body.length());
            // Filtra modelli testuali: gpt-*, o1*, o3*, o4*, chatgpt-*
            // Esclude: embedding, tts, dall-e, whisper, babbage, davinci, ada
            int pos = 0;
            bool first = true;
            while (true) {
                // OpenAI risponde con "id": "..." (spazio dopo i due punti)
                int idx  = body.indexOf("\"id\":\"",  pos);
                int idx2 = body.indexOf("\"id\": \"", pos);
                if (idx < 0 && idx2 < 0) break;
                int useIdx;
                if      (idx < 0)        useIdx = idx2 + 7;
                else if (idx2 < 0)       useIdx = idx  + 6;
                else if (idx2 < idx)     useIdx = idx2 + 7;
                else                     useIdx = idx  + 6;
                int end = body.indexOf("\"", useIdx);
                if (end < 0) break;
                String id = body.substring(useIdx, end);
                bool isText = id.startsWith("gpt-") || id.startsWith("o1") ||
                              id.startsWith("o3")   || id.startsWith("o4") ||
                              id.startsWith("chatgpt-");
                bool isExcl = id.indexOf("embed") >= 0 || id.indexOf("tts") >= 0 ||
                              id.indexOf("dall-e") >= 0 || id.indexOf("whisper") >= 0 ||
                              id.indexOf("babbage") >= 0 || id.indexOf("davinci") >= 0 ||
                              id.indexOf("ada") >= 0;
                if (isText && !isExcl) {
                    if (!first) result += ",";
                    result += "\"" + id + "\"";
                    first = false;
                }
                pos = end + 1;
            }
            Serial.printf("[API-MODELS] OpenAI modelli trovati: %s\n", result.c_str());
        } else {
            Serial.printf("[API-MODELS] OpenAI HTTP %d: %s\n", oaCode, http.getString().substring(0,200).c_str());
        }
        http.end();

    } else if (provider == "claude") {
        if (claude_api_key.length() == 0) { server.send(200, "application/json", "{\"ok\":false,\"error\":\"no_key\"}"); return; }
        http.begin(client, "https://api.anthropic.com/v1/models");
        http.addHeader("x-api-key", claude_api_key);
        http.addHeader("anthropic-version", "2023-06-01");
        if (http.GET() == 200) {
            String body = http.getString();
            int pos = 0;
            bool first = true;
            while (true) {
                int idx = body.indexOf("\"id\":\"", pos);
                if (idx < 0) break;
                idx += 6;
                int end = body.indexOf("\"", idx);
                String id = body.substring(idx, end);
                if (!first) result += ",";
                result += "\"" + id + "\"";
                first = false;
                pos = end + 1;
            }
        }
        http.end();
    }

    result += "]";
    server.send(200, "application/json", "{\"ok\":true,\"models\":" + result + "}");
}

// GET /api/config?openai_key=...&claude_key=...&el_key=...&el_vid=...&provider=...&model=...&ssid=...&pass=...
// Imposta uno o più parametri in un colpo solo. Parametri omessi = non modificati.
// Utile per incollare un URL nel browser o fare curl, o generare un QR code con l'URL.
void handleApiConfig() {
    bool changed = false;
    if (server.hasArg("openai_key") && server.arg("openai_key").length()) {
        openai_api_key = server.arg("openai_key");
        preferences.putString("openai", openai_api_key);
        Serial.println("CFG: openai aggiornato");
        changed = true;
    }
    if (server.hasArg("claude_key") && server.arg("claude_key").length()) {
        claude_api_key = server.arg("claude_key");
        preferences.putString("claude", claude_api_key);
        Serial.println("CFG: claude aggiornato");
        changed = true;
    }
    if (server.hasArg("el_key") && server.arg("el_key").length()) {
        elevenlabs_api_key = server.arg("el_key");
        preferences.putString("11labs", elevenlabs_api_key);
        Serial.println("CFG: 11labs aggiornato");
        changed = true;
    }
    if (server.hasArg("el_vid") && server.arg("el_vid").length()) {
        elevenlabs_voice_id = server.arg("el_vid");
        preferences.putString("11labs_vid", elevenlabs_voice_id);
        Serial.println("CFG: el_vid -> " + elevenlabs_voice_id);
        changed = true;
    }
    if (server.hasArg("provider")) {
        String p = server.arg("provider");
        if (p == "openai" || p == "claude") {
            llm_provider = p;
            preferences.putString("llm_prov", llm_provider);
            Serial.println("CFG: provider -> " + llm_provider);
            changed = true;
        }
    }
    if (server.hasArg("model") && server.arg("model").length()) {
        llm_model = server.arg("model");
        preferences.putString("llm_model", llm_model);
        Serial.println("CFG: model -> " + llm_model);
        changed = true;
    }
    if (server.hasArg("ssid") && server.arg("ssid").length()) {
        upsertWifiNet(server.arg("ssid"), server.arg("pass"), 0);
        Serial.println("CFG: WiFi -> " + server.arg("ssid"));
        changed = true;
    }
    server.send(200, "application/json",
        String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}");
}

// GET /api/test?type=llm|el — verifica rapida dei token salvati
void handleApiTest() {
    String type = server.arg("type");
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    bool ok = false; String info, err;

    if (type == "llm") {
        if (llm_provider == "openai") {
            if (openai_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
            Serial.println("API test OpenAI, key: " + openai_api_key);
            http.begin(client, "https://api.openai.com/v1/models");
            http.addHeader("Authorization", "Bearer " + openai_api_key);
            int code = http.GET();
            ok = (code == 200);
            info = ok ? "OpenAI OK" : "HTTP " + String(code);
            if (!ok) err = http.getString();
            http.end();
            Serial.println("API test OpenAI: " + info + (err.length() ? " | " + err : ""));
        } else {
            if (claude_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
            Serial.println("API test Anthropic, key: " + claude_api_key);
            http.begin(client, "https://api.anthropic.com/v1/models");
            http.addHeader("x-api-key", claude_api_key);
            http.addHeader("anthropic-version", "2023-06-01");
            int code = http.GET();
            ok = (code == 200);
            info = ok ? "Anthropic OK" : "HTTP " + String(code);
            if (!ok) err = http.getString();
            http.end();
            Serial.println("API test Anthropic: " + info + (err.length() ? " | " + err : ""));
        }
    } else if (type == "el") {
        if (elevenlabs_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
        // Usa TTS con testo minimo — funziona su qualsiasi account, anche free senza voices_read
        Serial.println("API test ElevenLabs (via TTS probe)");
        http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id + "?output_format=" + elOutputFormat());
        http.addHeader("Content-Type", "application/json");
        http.addHeader("xi-api-key", elevenlabs_api_key);
        int code = http.POST("{\"text\":\"ok\",\"model_id\":\"eleven_multilingual_v2\"}");
        ok = (code == 200);
        if (ok) {
            info = "ElevenLabs OK (voce: " + elevenlabs_voice_id + ")";
        } else {
            info = "HTTP " + String(code);
            err  = http.getString().substring(0, 300);
        }
        http.end();
        Serial.println("API test ElevenLabs: " + info + (err.length() ? " | " + err : ""));
    } else {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"type sconosciuto\"}"); return;
    }

    // Tronca per JSON solo l'errore (può essere molto lungo), la chiave è già in seriale
    String errJson = err.substring(0, 400);
    info.replace("\"", "\\\""); errJson.replace("\"", "\\\"");
    server.send(200, "application/json",
        "{\"ok\":" + String(ok?"true":"false") +
        ",\"info\":\"" + info + "\"" +
        (errJson.length() ? ",\"error\":\"" + errJson + "\"" : "") + "}");
}

// GET /api/voices — lista voci ElevenLabs [{id,name}]
void handleApiVoices() {
    if (elevenlabs_api_key.length() == 0) { server.send(200, "application/json", "[]"); return; }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/voices");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    String result = "[]";
    if (http.GET() == 200) {
        String body = http.getString();
        result = "[";
        bool first = true;
        int pos = 0;
        while (true) {
            int vi = body.indexOf("\"voice_id\":\"", pos);
            if (vi < 0) break;
            vi += 12;
            int ve = body.indexOf("\"", vi);
            String vid = body.substring(vi, ve);
            int ni = body.indexOf("\"name\":\"", ve);
            if (ni < 0) break;
            ni += 8;
            int ne = body.indexOf("\"", ni);
            String vname = body.substring(ni, ne);
            vname.replace("\"", "\\\"");
            if (!first) result += ",";
            result += "{\"id\":\"" + vid + "\",\"name\":\"" + vname + "\"}";
            first = false;
            pos = ve + 1;
        }
        result += "]";
    }
    http.end();
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", result);
}

// Salva config LLM
void handleLlmSave() {
    String newProv  = server.arg("provider");
    String newModel = server.arg("model");
    String newKey   = server.arg("api_key");

    if (newProv == "openai" || newProv == "claude") {
        llm_provider = newProv;
        preferences.putString("llm_prov", llm_provider);
    }
    if (newModel.length() > 0) {
        llm_model = newModel;
        preferences.putString("llm_model", llm_model);
    }
    // Salva il token nel bucket corretto (provider corrente al momento del save)
    if (newKey.length() > 0 && !newKey.startsWith("****")) {
        if (llm_provider == "claude") {
            claude_api_key = newKey;
            preferences.putString("claude", claude_api_key);
        } else {
            openai_api_key = newKey;
            preferences.putString("openai", openai_api_key);
        }
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// Salva config ElevenLabs
void handleElSave() {
    String newKey = server.arg("el_key");
    String newVid = server.arg("el_vid");
    String newFmt = server.arg("el_fmt");
    if (newKey.length() > 0 && !newKey.startsWith("****")) {
        elevenlabs_api_key = newKey;
        preferences.putString("11labs", elevenlabs_api_key);
    }
    if (newVid.length() > 0) {
        elevenlabs_voice_id = newVid;
        preferences.putString("11labs_vid", elevenlabs_voice_id);
    }
    if (newFmt == "pcm" || newFmt == "mp3") {
        el_audio_fmt = newFmt;
        preferences.putString("11labs_fmt", el_audio_fmt);
        Serial.println("EL fmt -> " + el_audio_fmt);
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// VAD: salva soglia e stato enable
void handleVadSave() {
    String thr = server.arg("threshold");
    if (thr.length() > 0) {
        vad_threshold = constrain(thr.toInt(), 0, 32767);
        preferences.putInt("vad_thr", vad_threshold);
    }
    String en = server.arg("enabled");
    if (en.length() > 0) {
        vadEnabled = (en == "1");
        preferences.putBool("vad_en", vadEnabled);
    }
    String stt = server.arg("stt_enabled");
    if (stt.length() > 0) {
        sttEnabled = (stt == "1");
        preferences.putBool("stt_en", sttEnabled);
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// ==========================================
// HANDLER CONFIGURAZIONI COMPORTAMENTO
// ==========================================

// GET /cfg/list → JSON con tutte le configurazioni e quella attiva
void handleCfgList() {
    JsonDocument doc;
    doc["active"] = activeBehaviorConfig;
    JsonArray cfgs = doc["configs"].to<JsonArray>();
    for (int i = 0; i < behaviorConfigCount; i++) {
        JsonObject c = cfgs.add<JsonObject>();
        c["idx"]  = i;
        c["name"] = behaviorConfigs[i].name;
        JsonArray rules = c["rules"].to<JsonArray>();
        for (int r = 0; r < MAX_CFG_RULES; r++) {
            BehaviorRule& rule = behaviorConfigs[i].rules[r];
            if (rule.sensorId == SEN_NONE) continue;
            JsonObject ro = rules.add<JsonObject>();
            ro["slot"] = r;
            ro["sensorId"]   = rule.sensorId;
            ro["sensorName"] = SENSOR_NAMES[rule.sensorId];
            ro["len"]        = rule.len;
            JsonArray ba = ro["bytes"].to<JsonArray>();
            for (int b = 0; b < rule.len; b++) ba.add(rule.bytes[b]);
        }
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// POST /cfg/activate  idx=N
void handleCfgActivate() {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= behaviorConfigCount) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    activeBehaviorConfig = idx;
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /cfg/rename  idx=N&name=...
void handleCfgRename() {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= behaviorConfigCount) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    String nm = server.arg("name");
    nm.trim(); if (nm.length() == 0) nm = "Config";
    strncpy(behaviorConfigs[idx].name, nm.c_str(), 23);
    behaviorConfigs[idx].name[23] = 0;
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /cfg/new  name=...
void handleCfgNew() {
    if (behaviorConfigCount >= MAX_CONFIGS) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"max configs\"}"); return; }
    String nm = server.arg("name"); nm.trim(); if (nm.length() == 0) nm = "Config";
    BehaviorConfig& cfg = behaviorConfigs[behaviorConfigCount];
    strncpy(cfg.name, nm.c_str(), 23); cfg.name[23] = 0;
    memset(cfg.rules, 0, sizeof(cfg.rules));
    behaviorConfigCount++;
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(behaviorConfigCount-1) + "}");
}

// POST /cfg/del  idx=N
void handleCfgDel() {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= behaviorConfigCount || behaviorConfigCount <= 1) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    for (int i = idx; i < behaviorConfigCount - 1; i++) behaviorConfigs[i] = behaviorConfigs[i+1];
    behaviorConfigCount--;
    if (activeBehaviorConfig >= behaviorConfigCount) activeBehaviorConfig = behaviorConfigCount - 1;
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /cfg/rule/set  idx=N&slot=R&sensorId=S&bytes=[...]
void handleCfgRuleSet() {
    int cfgIdx  = server.arg("idx").toInt();
    int slot    = server.arg("slot").toInt();
    int senId   = server.arg("sensorId").toInt();
    String bytesStr = server.arg("bytes");
    if (cfgIdx < 0 || cfgIdx >= behaviorConfigCount || slot < 0 || slot >= MAX_CFG_RULES ||
        senId < 0 || senId >= SEN_COUNT) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"parametri invalidi\"}"); return;
    }
    BehaviorRule& rule = behaviorConfigs[cfgIdx].rules[slot];
    rule.sensorId = (uint8_t)senId;
    if (senId == SEN_NONE || bytesStr.length() == 0) {
        rule.len = 0; memset(rule.bytes, 0, 6);
    } else {
        JsonDocument doc; deserializeJson(doc, bytesStr);
        rule.len = 0;
        for (int v : doc.as<JsonArray>()) { if (rule.len < 6) rule.bytes[rule.len++] = (uint8_t)v; }
    }
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /cfg/rule/del  idx=N&slot=R
void handleCfgRuleDel() {
    int cfgIdx = server.arg("idx").toInt();
    int slot   = server.arg("slot").toInt();
    if (cfgIdx < 0 || cfgIdx >= behaviorConfigCount || slot < 0 || slot >= MAX_CFG_RULES) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    behaviorConfigs[cfgIdx].rules[slot] = { SEN_NONE, {0,0,0,0,0,0}, 0 };
    saveBehaviorConfigs();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /ble/scan — avvia scan, raccoglie tutti i Furby trovati
void handleBleScan() {
    if (isConfigMode || bleScanning || connected) {
        server.send(200, "application/json", "{\"ok\":false,\"reason\":\"busy\"}"); return;
    }
    bleScanStart(10);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleBleScanStop() {
    bleScanStop();
    server.send(200, "application/json", "{\"ok\":true}");
}

// GET /ble/status — stato connessione + lista Furby trovati + uptime
void handleBleStatus() {
    String name = ble_last_name; name.replace("\"", "\\\"");
    unsigned long upMs  = millis();
    unsigned long bleMs = connected ? (upMs - bleConnectedMs) : 0;
    String json = "{\"connected\":"   + String(connected     ? "true" : "false") +
                  ",\"scanning\":"   + String(bleScanning   ? "true" : "false") +
                  ",\"connecting\":" + String(bleConnecting ? "true" : "false") +
                  ",\"name\":\""     + name + "\"" +
                  ",\"battery\":"    + String(ble_battery_pct) +
                  ",\"uptime\":"     + String(upMs / 1000) +
                  ",\"ble_uptime\":" + String(bleMs / 1000) +
                  ",\"devices\":[";
    for (int i = 0; i < furbyListCount; i++) {
        if (i) json += ",";
        String n = furbyList[i].name; n.replace("\"", "\\\"");
        json += "{\"name\":\"" + n + "\",\"addr\":\"" + furbyList[i].addr + "\"}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// POST /ble/connect?addr=XX:XX:XX:XX:XX:XX — connetti a Furby specifico
void handleBleConnect() {
    if (connected)      { server.send(200, "application/json", "{\"ok\":true,\"already\":true}"); return; }
    if (bleConnecting)  { server.send(200, "application/json", "{\"ok\":false,\"error\":\"connecting\"}"); return; }
    String addr = server.arg("addr");
    if (addr.length() == 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"addr mancante\"}"); return;
    }
    String name;
    esp_ble_addr_type_t atype = BLE_ADDR_TYPE_RANDOM;
    bool found = false;
    for (int i = 0; i < furbyListCount; i++) {
        if (furbyList[i].addr == addr) {
            name  = furbyList[i].name;
            atype = furbyList[i].addrType;
            found = true; break;
        }
    }
    if (!found) {
        server.send(404, "application/json", "{\"ok\":false,\"error\":\"device non in lista\"}"); return;
    }
    pendingConnAddr     = addr;
    pendingConnName     = name;
    pendingConnAddrType = atype;
    bleUserDisconnect   = false;
    doConnect = true;
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /ble/disconnect
void handleBleDisconnect() {
    bleDisconnect();
    server.send(200, "application/json", "{\"ok\":true}");
}

// BLE: salva UUID personalizzati e ricarica gli oggetti BLEUUID
void handleBleSave() {
    String newSvc  = server.arg("svc_uuid");
    String newChar = server.arg("char_uuid");
    if (newSvc.length()  == 36) {
        ble_service_uuid = newSvc;
        preferences.putString("ble_svc",  ble_service_uuid);
        serviceUUID = BLEUUID(ble_service_uuid.c_str());
    }
    if (newChar.length() == 36) {
        ble_char_uuid_tx = newChar;
        preferences.putString("ble_char", ble_char_uuid_tx);
        charUUID_GPWrite = BLEUUID(ble_char_uuid_tx.c_str());
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// Formatta / svuota cache SD
void handleSdFormat() {
    if (!sdAvailable) { server.send(400, "text/plain", "SD non disponibile"); return; }

    // Elimina tutti i file .pcm e l'indice
    File root = SD_MMC.open("/");
    File file = root.openNextFile();
    while (file) {
        String name = String("/") + file.name();
        file.close();
        if (name.endsWith(".pcm") || name == "/index.json") SD_MMC.remove(name);
        file = root.openNextFile();
    }
    root.close();

    // Azzera il contatore file_id in flash
    Preferences prefs; prefs.begin("furby_sys", false);
    prefs.putInt("file_id", 0); prefs.end();

    server.sendHeader("Location", "/"); server.send(303);
}

// Handler captive portal redirect — funzione named per evitare lambda con capture
void handleCaptiveRedirect() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}

// ==========================================
// HANDLER SISTEMA PERSONALITÀ / COMPORTAMENTI
// ==========================================

// GET /personality
void handlePersonalityGet() {
    String j = "{\"prompt\":";
    j += "\""; for (char c : gPersonalityPrompt) { if (c=='"') j+="\\\""; else if (c=='\\') j+="\\\\"; else if (c=='\n') j+="\\n"; else j+=c; } j += "\"";
    j += ",\"voice_id\":\"" + gPersonalityVoiceId + "\"}";
    server.send(200, "application/json", j);
}

// POST /personality/save  (form: prompt, voice_id)
void handlePersonalitySave() {
    if (server.hasArg("prompt"))   gPersonalityPrompt  = server.arg("prompt");
    if (server.hasArg("voice_id")) gPersonalityVoiceId = server.arg("voice_id");
    saveEventBehaviors();
    server.send(200, "application/json", "{\"ok\":true}");
}

// GET /behaviors  →  JSON completo (inclusa personality)
void handleBehaviorsGet() {
    if (!SPIFFS.exists("/behaviors.json")) { saveEventBehaviors(); }
    File f = SPIFFS.open("/behaviors.json", "r");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    server.streamFile(f, "application/json"); f.close();
}

// POST /behaviors/save  (body: JSON completo behaviors.json)
void handleBehaviorsSave() {
    String body = server.arg("plain");
    if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"body vuoto\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON non valido\"}"); return;
    }
    File f = SPIFFS.open("/behaviors.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(body); f.close();
    loadEventBehaviors();
    server.send(200, "application/json", "{\"ok\":true}");
}

// GET /behaviors/export  →  download behaviors.json
void handleBehaviorsExport() {
    if (!SPIFFS.exists("/behaviors.json")) saveEventBehaviors();
    File f = SPIFFS.open("/behaviors.json", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=behaviors.json");
    server.streamFile(f, "application/json"); f.close();
}

// POST /behaviors/import  (body: JSON)
void handleBehaviorsImport() {
    String body = server.arg("plain");
    if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"body vuoto\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON non valido\"}"); return;
    }
    File f = SPIFFS.open("/behaviors.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(body); f.close();
    loadEventBehaviors();
    server.send(200, "application/json", "{\"ok\":true}");
}

// GET /behaviors/actions  →  lista azioni Furby disponibili
void handleBehaviorsActions() {
    String j = "[";
    for (int i = 0; i < FURBY_ACTIONS_COUNT; i++) {
        if (i > 0) j += ",";
        j += "{\"id\":\""; j += FURBY_ACTIONS[i].id;
        j += "\",\"label\":\""; j += FURBY_ACTIONS[i].label; j += "\"}";
    }
    j += "]";
    server.send(200, "application/json", j);
}

// POST /test/llm  (form: text)  →  chiama LLM con personality prompt corrente, senza immagine
void handleTestLlm() {
    if (isProcessing) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return; }
    String text = server.arg("text");
    if (text.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"text mancante\"}"); return; }
    String answer = callLLM("", gPersonalityPrompt, text);
    String safe; for (char c : answer) { if (c=='"') safe+="\\\""; else if (c=='\\') safe+="\\\\"; else if (c=='\n') safe+="\\n"; else safe+=c; }
    server.send(200, "application/json", "{\"ok\":true,\"response\":\"" + safe + "\"}");
}

// POST /test/tts  (form: text)  →  invia ad ElevenLabs e riproduce sull'ESP32
void handleTestTts() {
    if (isProcessing || isSpeaking) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return; }
    String text = server.arg("text");
    if (text.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"text mancante\"}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
    // Riproduce in background per non bloccare il server
    xTaskCreatePinnedToCore([](void* p) {
        String* t = (String*)p;
        isSpeaking = true;
        if (sdAvailable) generateAndPlayTTS_SD(*t);
        else             streamAndPlayTTS_RAM(*t);
        isSpeaking = false;
        delete t;
        vTaskDelete(NULL);
    }, "tts_test", 16384, new String(text), 1, NULL, 1);
}

// Voci pre-caricate ElevenLabs disponibili su tutti gli account (incluso free)
static const char EL_BUILTIN_VOICES[] PROGMEM =
    "{\"ok\":true,\"builtin\":true,\"voices\":["
    "{\"id\":\"21m00Tcm4TlvDq8ikWAM\",\"name\":\"Rachel\",\"category\":\"premade\"},"
    "{\"id\":\"AZnzlk1XvdvUeBnXmlld\",\"name\":\"Domi\",\"category\":\"premade\"},"
    "{\"id\":\"EXAVITQu4vr4xnSDxMaL\",\"name\":\"Bella\",\"category\":\"premade\"},"
    "{\"id\":\"ErXwobaYiN019PkySvjV\",\"name\":\"Antoni\",\"category\":\"premade\"},"
    "{\"id\":\"MF3mGyEYCl7XYWbV9V6O\",\"name\":\"Elli\",\"category\":\"premade\"},"
    "{\"id\":\"TxGEqnHWrfWFTfGW9XjX\",\"name\":\"Josh\",\"category\":\"premade\"},"
    "{\"id\":\"VR6AewLTigWG4xSOukaG\",\"name\":\"Arnold\",\"category\":\"premade\"},"
    "{\"id\":\"pNInz6obpgDQGcFmaJcg\",\"name\":\"Adam\",\"category\":\"premade\"},"
    "{\"id\":\"yoZ06aMxZJJ28mfd3POQ\",\"name\":\"Sam\",\"category\":\"premade\"},"
    "{\"id\":\"JBFqnCBsd6RMkjVDRZzb\",\"name\":\"George\",\"category\":\"premade\"},"
    "{\"id\":\"iP95p4xoKVk53GoZ742B\",\"name\":\"Chris\",\"category\":\"premade\"},"
    "{\"id\":\"onwK4e9ZLuTAKqWW03F9\",\"name\":\"Daniel\",\"category\":\"premade\"},"
    "{\"id\":\"XB0fDUnXU5powFXDhCwa\",\"name\":\"Charlotte\",\"category\":\"premade\"},"
    "{\"id\":\"Xb7hH8MSUJpSbSDYk0k2\",\"name\":\"Alice\",\"category\":\"premade\"},"
    "{\"id\":\"nPczCjzI2devNBz1zQrb\",\"name\":\"Brian\",\"category\":\"premade\"},"
    "{\"id\":\"cgSgspJ2msm6clMCkdW9\",\"name\":\"Jessica\",\"category\":\"premade\"},"
    "{\"id\":\"FGY2WhTYpPnrIDTdsKH5\",\"name\":\"Laura\",\"category\":\"premade\"},"
    "{\"id\":\"TX3LPaxmHKxFdv7VOQHJ\",\"name\":\"Liam\",\"category\":\"premade\"}"
    "]}";

// GET /test/voices  →  lista voci ElevenLabs; fallback a lista built-in se account senza voices_read
void handleTestVoices() {
    if (elevenlabs_api_key.length() == 0) {
        server.send(200, "application/json", FPSTR(EL_BUILTIN_VOICES)); return;
    }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/voices");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    int code = http.GET();
    Serial.printf("[VOICES] GET /v1/voices HTTP %d\n", code);
    if (code == 200) {
        String raw = http.getString();
        http.end();
        JsonDocument doc;
        String out = "{\"ok\":true,\"voices\":[";
        bool first = true;
        if (deserializeJson(doc, raw) == DeserializationError::Ok) {
            for (JsonObject v : doc["voices"].as<JsonArray>()) {
                if (!first) out += ",";
                out += "{\"id\":\"";       out += v["voice_id"].as<const char*>();  out += "\"";
                out += ",\"name\":\"";     out += v["name"].as<const char*>();      out += "\"";
                out += ",\"category\":\""; out += v["category"].as<const char*>();  out += "\"}";
                first = false;
            }
        }
        out += "]}";
        server.send(200, "application/json", out);
    } else {
        http.end();
        // 403 = mancano permessi voices_read (account free) → lista built-in
        Serial.printf("[VOICES] fallback a lista built-in (HTTP %d)\n", code);
        server.send(200, "application/json", FPSTR(EL_BUILTIN_VOICES));
    }
}

// POST /test/behavior  (form: trigger, sensor_id, dry_run)
// Simula un trigger come se fosse arrivato dal VAD/button/sensore.
// dry_run=1 → esegue LLM+TTS ma skippa azioni BLE verso il Furby.
void handleTestBehavior() {
    if (isProcessing || isSpeaking) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return;
    }
    int trg      = server.arg("trigger").toInt();
    int sid      = server.arg("sensor_id").toInt();
    bool dry     = server.arg("dry_run") == "1";
    if (trg < 0 || trg > 2) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"trigger non valido\"}"); return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    Serial.printf("[TEST-BEH] trigger=%d sensor_id=%d dry_run=%s\n", trg, sid, dry ? "si" : "no");

    struct Args { TriggerType trg; uint8_t sid; bool dry; };
    Args* a = new Args{ (TriggerType)trg, (uint8_t)sid, dry };
    xTaskCreatePinnedToCore([](void* p) {
        Args* a = (Args*)p;
        gDryRun     = a->dry;
        isProcessing = true;
        processStimulus(a->trg, a->sid);
        isProcessing = false;
        gDryRun      = false;
        delete a;
        vTaskDelete(NULL);
    }, "beh_test", 16384, a, 1, NULL, 1);
}

// ==========================================
// DEBUG HANDLERS
// ==========================================
void handleDebugPage() {
    File f = SPIFFS.open("/debug.html", "r");
    if (!f) { server.send(503, "text/plain", "debug.html non trovato — eseguire uploadfs"); return; }
    server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    server.streamFile(f, "text/html; charset=utf-8");
    f.close();
}

// ==========================================
// HANDLER FILE MANAGER SPIFFS (/fs/*)
// ==========================================
void handleFsList() {
    String json = "{\"total\":" + String(SPIFFS.totalBytes()) +
                  ",\"used\":"  + String(SPIFFS.usedBytes())  + ",\"files\":[";
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool first = true;
    while (file) {
        if (!first) json += ",";
        String nm = file.name();
        if (nm[0] != '/') nm = "/" + nm;
        json += "{\"name\":\"" + nm + "\",\"size\":" + String(file.size()) + "}";
        first = false;
        file = root.openNextFile();
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleFsGet() {
    String path = server.uri().substring(7); // strip "/fs/get"
    if (path.length() == 0) path = "/";
    if (!SPIFFS.exists(path)) { server.send(404, "text/plain", "not found"); return; }
    File f = SPIFFS.open(path, "r");
    if (!f) { server.send(500, "text/plain", "open failed"); return; }
    String ct = "application/octet-stream";
    if (path.endsWith(".html") || path.endsWith(".htm")) ct = "text/html; charset=utf-8";
    else if (path.endsWith(".json")) ct = "application/json; charset=utf-8";
    else if (path.endsWith(".txt") || path.endsWith(".csv") || path.endsWith(".log")) ct = "text/plain; charset=utf-8";
    else if (path.endsWith(".js"))  ct = "application/javascript";
    else if (path.endsWith(".css")) ct = "text/css";
    server.streamFile(f, ct);
    f.close();
}

// Buffer upload globale — usato da handleFsUpload
static File   _uploadFile;
static String _uploadPath;

void handleFsPut() {
    // L'upload viene gestito in streaming da handleFsUpload.
    // Qui arriviamo solo se l'upload handler non ha già inviato la risposta.
    if (!_uploadFile && _uploadPath.length() == 0)
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"nessun file ricevuto\"}");
}

void handleFsUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        _uploadPath = server.arg("path");
        if (_uploadPath.length() == 0 || _uploadPath[0] != '/') _uploadPath = "/" + _uploadPath;
        if (_uploadFile) _uploadFile.close();
        _uploadFile = SPIFFS.open(_uploadPath, "w");
        Serial.println("SPIFFS upload start: " + _uploadPath);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_uploadFile) _uploadFile.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        if (_uploadFile) { _uploadFile.close(); Serial.println("SPIFFS upload done: " + String(up.totalSize) + " B"); }
        server.send(200, "application/json", "{\"ok\":true}");
    }
}

void handleFsDel() {
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"path mancante\"}"); return; }
    if (path[0] != '/') path = "/" + path;
    if (!SPIFFS.exists(path)) { server.send(404, "application/json", "{\"ok\":false,\"error\":\"non trovato\"}"); return; }
    SPIFFS.remove(path);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleDebugSensors() {
    unsigned long age = lastSensorMs ? (millis() - lastSensorMs) : 99999;
    String j = "{";
    j += "\"connected\":" + String(connected ? "true" : "false") + ",";
    j += "\"device\":\"" + ble_last_name + " [" + ble_last_addr + "]\",";
    j += "\"sensor_age_ms\":" + String(age) + ",";
    j += "\"antennaLeft\":"    + String(furbyState.antennaLeft    ? "true":"false") + ",";
    j += "\"antennaRight\":"   + String(furbyState.antennaRight   ? "true":"false") + ",";
    j += "\"antennaForward\":" + String(furbyState.antennaForward ? "true":"false") + ",";
    j += "\"antennaBack\":"    + String(furbyState.antennaBack    ? "true":"false") + ",";
    j += "\"tickleHead\":"     + String(furbyState.tickleHead     ? "true":"false") + ",";
    j += "\"tickleTummy\":"    + String(furbyState.tickleTummy    ? "true":"false") + ",";
    j += "\"tickleRight\":"    + String(furbyState.tickleRight    ? "true":"false") + ",";
    j += "\"tickleLeft\":"     + String(furbyState.tickleLeft     ? "true":"false") + ",";
    j += "\"pullTail\":"       + String(furbyState.pullTail       ? "true":"false") + ",";
    j += "\"pushTongue\":"     + String(furbyState.pushTongue     ? "true":"false") + ",";
    j += "\"upright\":"        + String(furbyState.upright        ? "true":"false") + ",";
    j += "\"upsideDown\":"     + String(furbyState.upsideDown     ? "true":"false") + ",";
    j += "\"onRightSide\":"    + String(furbyState.onRightSide    ? "true":"false") + ",";
    j += "\"onLeftSide\":"     + String(furbyState.onLeftSide     ? "true":"false") + ",";
    j += "\"leanBack\":"       + String(furbyState.leanBack       ? "true":"false") + ",";
    j += "\"tiltRight\":"      + String(furbyState.tiltRight      ? "true":"false") + ",";
    j += "\"tiltLeft\":"       + String(furbyState.tiltLeft       ? "true":"false") + ",";
    j += "\"rawB1\":" + String(furbyState.rawB1) + ",";
    j += "\"rawB2\":" + String(furbyState.rawB2) + ",";
    j += "\"rawB3\":" + String(furbyState.rawB3) + ",";
    j += "\"rawB4\":" + String(furbyState.rawB4);
    j += "}";
    server.send(200, "application/json", j);
}

void handleDebugCmd() {
    String bytesStr = server.arg("bytes");
    String channel  = server.arg("channel"); // "nordic" oppure vuoto = GeneralPlus
    if (bytesStr.length() == 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"bytes missing\"}");
        return;
    }
    if (!connected) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"non connesso\"}");
        return;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, bytesStr);
    if (err || !doc.is<JsonArray>()) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"json invalido\"}");
        return;
    }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0 || arr.size() > 20) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"lunghezza non valida\"}");
        return;
    }
    uint8_t buf[20];
    int i = 0;
    for (JsonVariant v : arr) buf[i++] = (uint8_t)v.as<int>();
    if (channel == "nordic") {
        if (!pCharNWrite) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"NordicWrite non disponibile\"}"); return; }
        pCharNWrite->writeValue(buf, i, false);
    } else {
        furbyWrite(buf, i);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

// ==========================================
// DEBUG AUDIO TEST
// ==========================================

// GET /debug/mic/rms — RMS live da entrambi i microfoni + VAD flag
void handleDebugMicRms() {
    server.send(200, "application/json",
        "{\"rms1\":" + String(micRmsLive) +
        ",\"rms2\":" + String(micRmsLive2) +
        ",\"vad\":" + String(micVadActive ? "true" : "false") +
        ",\"threshold\":" + String(vad_threshold) + "}");
}

// GET /debug/amp — stato amplificatore
// POST /debug/amp?on=1|0 — accende/spegne
void handleDebugAmp() {
    if (server.method() == HTTP_POST) {
        bool on = server.arg("on") == "1";
        setAmplifier(on);
    }
    bool on = (ch32PortState >> 4) & 1;
    server.send(200, "application/json", "{\"ok\":true,\"on\":" + String(on ? "true" : "false") + "}");
}

// ES8311 REG32: 0x00=mute, 0x01=-96dB, step 0.5dB/LSB, 0xFF=+32dB
// Mappa 1-100% su -30dB..+6dB (range percettivo usabile; 50%=-12dB, 75%=-3dB)
void setVolume(int pct) {
    pct = constrain(pct, 0, 100);
    if (pct == 0) { es8311WriteReg(ES8311_DAC_REG32, 0x00); return; }
    float db  = -30.0f + (pct * 36.0f / 100.0f);
    int   reg = constrain((int)((db + 96.0f) * 2.0f), 1, 255);
    es8311WriteReg(ES8311_DAC_REG32, (uint8_t)reg);
    Serial.printf("Volume: %d%% (%.1fdB reg=0x%02X)\n", pct, db, reg);
}

// POST /debug/vol?vol=0-100 — volume DAC ES8311
void handleDebugVol() {
    int vol = server.arg("vol").toInt();
    setVolume(vol);
    server.send(200, "application/json", "{\"ok\":true,\"vol\":" + String(constrain(vol, 0, 100)) + "}");
}

// POST /debug/tone?freq=440&ch=both|left|right&ms=1000
// Genera una sine 16-bit a frequenza data e la invia all'I2S speaker
void handleDebugTone() {
    if (isSpeaking) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"speaking\"}"); return; }
    int freq  = server.arg("freq").toInt(); if (freq <= 0) freq = 440;
    int ms    = server.arg("ms").toInt();   if (ms  <= 0) ms  = 800;
    if (ms > 5000) ms = 5000;
    const int RATE = 16000;
    const int BUF_SAMP = 256;
    int16_t buf[BUF_SAMP];
    size_t written;
    int totalSamples = (RATE * ms) / 1000;
    int phase = 0;

    Serial.printf("Tone: %dHz %dms, totalSamples=%d\n", freq, ms, totalSamples);
    setAmplifier(true);
    delay(50); // stabilizzazione amplificatore
    isSpeaking = true;
    for (int s = 0; s < totalSamples; s += BUF_SAMP) {
        int chunk = min(BUF_SAMP, totalSamples - s);
        for (int i = 0; i < chunk; i++) {
            buf[i] = (int16_t)(16000 * sin(2.0f * M_PI * freq * phase / RATE));
            phase++;
        }
        i2s_write_mono(buf, chunk);
    }
    // drain: scrivi silenzio per svuotare il DMA buffer prima di spegnere l'amp
    memset(buf, 0, sizeof(buf));
    i2s_write_mono(buf, BUF_SAMP);
    delay(50);
    i2s_zero_dma_buffer(I2S_NUM);
    setAmplifier(false);
    isSpeaking = false;
    Serial.println("Tone: done");

    server.send(200, "application/json", "{\"ok\":true,\"freq\":" + String(freq) + ",\"ms\":" + String(ms) + "}");
}

// POST /debug/mic/record — registra 3s da entrambi i mic, riproduce su casse
// Risponde subito con ok, la riproduzione avviene in background (task FreeRTOS)
static void micRecordTask(void* pv) {
    const int RATE      = 16000;
    const int SECS      = 3;
    // Stereo: 2 canali × 2 byte × 16000 Hz × 3s = 192 KB
    const int TOTAL_SAMP_STEREO = RATE * SECS * 2; // campioni stereo (pari=L, disp=R)
    const int TOTAL_BYTES       = TOTAL_SAMP_STEREO * sizeof(int16_t);

    int16_t* recBuf = (int16_t*)heap_caps_malloc(TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recBuf) {
        Serial.printf("MicTest: PSRAM non disponibile (%d bytes liberi), provo heap interno\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        recBuf = (int16_t*)malloc(TOTAL_BYTES);
    }
    if (!recBuf) {
        Serial.printf("MicTest: heap insufficiente (PSRAM=%d, interno=%d liberi)\n",
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM), esp_get_free_heap_size());
        micTestActive = false; vTaskDelete(NULL); return;
    }

    size_t bytesRead;
    int totalRead = 0;
    while (totalRead < TOTAL_BYTES) {
        int toRead = min(512 * 2, TOTAL_BYTES - totalRead); // 512 stereo samples
        i2s_read(I2S_MIC_NUM, (uint8_t*)recBuf + totalRead, toRead, &bytesRead, portMAX_DELAY);
        totalRead += bytesRead;
    }
    micTestActive = false;
    Serial.printf("MicTest: registrati %d bytes, riproduco...\n", totalRead);

    // Riproduce MIC1+MIC2 mixati → mono su casse
    Serial.printf("MicTest: riproduco %d stereo samples\n", totalRead / 2);
    setAmplifier(true);
    isSpeaking = true;
    const int OUT_CHUNK = 256;
    int16_t outBuf[OUT_CHUNK];
    size_t written;
    int stereoSamples = totalRead / (int)sizeof(int16_t);
    for (int i = 0; i < stereoSamples; i += 2 * OUT_CHUNK) {
        int chunk = min(OUT_CHUNK, (stereoSamples - i) / 2);
        for (int k = 0; k < chunk; k++) {
            int32_t mix = (int32_t)recBuf[i + 2*k] + recBuf[i + 2*k + 1];
            outBuf[k] = (int16_t)(mix / 2);
        }
        i2s_write_mono(outBuf, chunk);
    }
    i2s_zero_dma_buffer(I2S_NUM);
    setAmplifier(false);
    isSpeaking = false;
    Serial.println("MicTest: riproduzione completata");

    free(recBuf);
    vTaskDelete(NULL);
}

void handleDebugMicRecord() {
    if (isSpeaking)    { server.send(503, "application/json", "{\"ok\":false,\"error\":\"speaking\"}"); return; }
    if (micTestActive) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"test gia in corso\"}"); return; }
    micTestActive = true;
    xTaskCreatePinnedToCore(micRecordTask, "MicTest", 4096, NULL, 1, NULL, 1);
    server.send(200, "application/json", "{\"ok\":true,\"duration\":3}");
}

// ==========================================
// CAMERA DESCRIBE
// ==========================================

// GET /camera/describe/prompt  →  {"prompt":"..."}
void handleCamDescPromptGet() {
    String safe;
    for (char c : gCamDescPrompt) {
        if (c=='"') safe+="\\\""; else if (c=='\\') safe+="\\\\"; else if (c=='\n') safe+="\\n"; else safe+=c;
    }
    server.send(200, "application/json", "{\"prompt\":\"" + safe + "\"}");
}

// POST /camera/describe/prompt  (form: prompt)  →  salva
void handleCamDescPromptSave() {
    gCamDescPrompt = server.arg("prompt");
    preferences.putString("cam_desc_prompt", gCamDescPrompt);
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /camera/describe  →  cattura frame + LLM + {"ok":true,"text":"..."}
void handleCamDescribe() {
    Serial.printf("[CAM-DESCRIBE] richiesta (heap interno libero: %u B, PSRAM libera: %u B)\n",
        esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (!camActive && !camInit()) {
        Serial.println("[CAM-DESCRIBE] ERRORE: camera non disponibile");
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"camera non disponibile\"}"); return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("[CAM-DESCRIBE] ERRORE: frame non disponibile");
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"frame non disponibile\"}"); return;
    }
    Serial.printf("[CAM-DESCRIBE] frame catturato: %u byte JPEG\n", fb->len);
    String b64 = base64Encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    Serial.printf("[CAM-DESCRIBE] base64: %u char, prompt: \"%s\"\n", b64.length(), gCamDescPrompt.c_str());
    String sysP = gPersonalityPrompt;
    if (gCamDescPrompt.length() > 0) sysP += " " + gCamDescPrompt;
    String answer = callLLM(b64, sysP, "Descrivi quello che vedi, in prima persona, come se fossi tu a guardare.");
    Serial.printf("[CAM-DESCRIBE] risposta LLM (%u char): \"%s\"\n", answer.length(), answer.c_str());
    String safe;
    for (char c : answer) {
        if (c=='"') safe+="\\\""; else if (c=='\\') safe+="\\\\"; else if (c=='\n') safe+="\\n"; else safe+=c;
    }
    server.send(200, "application/json", "{\"ok\":true,\"text\":\"" + safe + "\"}");
}

// ==========================================
// CAMERA PAGE
// ==========================================
void handleCameraPage() {
    if (!camActive) camInit();
    File f = SPIFFS.open("/camera.html", "r");
    if (f) { server.sendHeader("Cache-Control", "public, max-age=3600"); server.streamFile(f, "text/html; charset=utf-8"); f.close(); return; }
    // fallback minimale se SPIFFS non flashato
    server.send(200, "text/html", F("<!DOCTYPE html><html><body>"
        "<p>camera.html non trovato in SPIFFS. Esegui: pio run -t uploadfs</p>"
        "<p><a href='/'>Home</a></p></body></html>"));
}


// Cattura un frame JPEG e lo invia — usato solo per snapshot singolo
void handleCameraFrame() {
    if (!camActive && !camInit()) {
        server.send(503, "text/plain", "camera non disponibile"); return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        server.send(503, "text/plain", "frame non disponibile"); return;
    }
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

// Task FreeRTOS che spinge i frame MJPEG su una connessione TCP già aperta.
// Gira su Core 1 (stesso del WebServer) ma in un task separato così non blocca handleClient().
static void mjpegTask(void* arg) {
    WiFiClient client = *((WiFiClient*)arg);
    delete (WiFiClient*)arg;

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: multipart/x-mixed-replace; boundary=fb");
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) { vTaskDelay(30 / portTICK_PERIOD_MS); continue; }
        client.printf("--fb\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
        client.write(fb->buf, fb->len);
        client.print("\r\n");
        esp_camera_fb_return(fb);
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
    client.stop();
    vTaskDelete(NULL);
}

// MJPEG streaming: una sola connessione TCP, frame spinti in multipart
void handleCameraStream() {
    if (!camActive && !camInit()) {
        server.send(503, "text/plain", "camera non disponibile"); return;
    }
    // Copia il client prima che il WebServer lo chiuda, poi lancia il task
    WiFiClient* client = new WiFiClient(server.client());
    xTaskCreate(mjpegTask, "mjpeg", 4096, client, 1, NULL);
    // Non chiamare server.send() — il task gestisce la connessione direttamente
}

void handleCameraOff() {
    camDeinit();
    server.send(200, "text/plain", "ok");
}

// POST /ble/abort — interrompe connessione o tentativo in corso, resetta stato
void handleBleAbort() {
    bleUserDisconnect = true;
    bleConnecting     = false;
    if (pBleClient && pBleClient->isConnected()) pBleClient->disconnect();
    bleResetState();
    server.send(200, "application/json", "{\"ok\":true}");
}

// POST /ble/reset — azzera lo stack BLE (nuclear option, deinit+reinit)
void handleBleReset() {
    server.send(200, "application/json", "{\"ok\":true}");
    xTaskCreatePinnedToCore([](void*) {
        bleUserDisconnect = true;
        bleConnecting     = false;
        bleScanning       = false;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        if (pBleClient) {
            try { if (pBleClient->isConnected()) pBleClient->disconnect(); } catch(...) {}
            vTaskDelay(300 / portTICK_PERIOD_MS);
            delete pBleClient; pBleClient = nullptr;
        }
        bleResetState();
        BLEDevice::deinit(true);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        BLEDevice::init("");
        BLEDevice::getScan()->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
        furbyListCount = 0;
        Serial.println("BLE: stack resettato");
        vTaskDelete(NULL);
    }, "BLEReset", 4096, NULL, 2, NULL, 0);
}

// GET /sys/info — info sistema (CPU, heap, PSRAM, SPIFFS, SD, flash)
void handleSysInfo() {
    auto kb = [](size_t b) { return b / 1024; };
    String j = "{";
    j += "\"cpu_mhz\":"      + String(getCpuFrequencyMhz())        + ",";
    j += "\"heap_free\":"    + String(kb(ESP.getFreeHeap()))        + ",";
    j += "\"heap_total\":"   + String(kb(ESP.getHeapSize()))        + ",";
    j += "\"psram_free\":"   + String(kb(psramFound() ? ESP.getFreePsram() : 0)) + ",";
    j += "\"psram_total\":"  + String(kb(psramFound() ? ESP.getPsramSize()  : 0)) + ",";
    j += "\"spiffs_used\":"   + String(kb(SPIFFS.usedBytes()))                  + ",";
    j += "\"spiffs_total\":"  + String(kb(SPIFFS.totalBytes()))                 + ",";
    j += "\"sketch_used\":"   + String(kb(ESP.getSketchSize()))  + ",";
    j += "\"sketch_total\":"  + String(0x400000 / 1024)          + ",";
    j += "\"flash_mb\":"      + String(ESP.getFlashChipSize() / (1024*1024))    + ",";
    j += "\"flash_used_kb\":" + String(kb(ESP.getSketchSize()) + kb(SPIFFS.usedBytes())) + ",";
    j += "\"flash_free_kb\":" + String(ESP.getFlashChipSize() / 1024 - kb(ESP.getSketchSize()) - kb(SPIFFS.usedBytes())) + ",";
    j += "\"uptime_s\":"      + String(millis() / 1000) + ",";
    j += "\"bat_mv\":"        + String(readBatteryMv()) + ",";
    j += "\"fw_version\":\""  + String(FW_VERSION) + "\",";
    j += "\"el_key\":\""      + elevenlabs_api_key + "\",";
    j += "\"el_voice_id\":\"" + elevenlabs_voice_id + "\",";
    j += "\"el_fmt\":\""      + el_audio_fmt + "\"";
    if (sdAvailable) {
        j += ",\"sd_used\":"  + String(kb(SD_MMC.usedBytes()));
        j += ",\"sd_total\":" + String(kb(SD_MMC.totalBytes()));
    }
    j += "}";
    server.send(200, "application/json", j);
}

// GET /api/home — tutti i dati dinamici della home page
void handleApiHome() {
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    String ip = isConfigMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String j = "{";
    j += "\"wifi_connected\":" + String(wifiOk ? "true" : "false") + ",";
    j += "\"wifi_ssid\":\""    + (wifiOk ? WiFi.SSID() : String("")) + "\",";
    j += "\"ip\":\""           + ip + "\",";
    j += "\"wifi_nets\":[";
    for (int i = 0; i < wifiNetCount; i++) {
        if (i) j += ",";
        j += "{\"ssid\":\"" + wifiNets[i].ssid + "\"}";
    }
    j += "],";
    j += "\"llm_provider\":\"" + llm_provider + "\",";
    j += "\"llm_model\":\""    + llm_model + "\",";
    j += "\"cur_key\":\""      + (llm_provider == "claude" ? claude_api_key : openai_api_key) + "\",";
    j += "\"el_key\":\""       + elevenlabs_api_key + "\",";
    j += "\"el_vid\":\""       + elevenlabs_voice_id + "\",";
    j += "\"el_fmt\":\""       + el_audio_fmt + "\",";
    j += "\"sd_present\":"     + String(sdAvailable ? "true" : "false") + ",";
    if (sdAvailable) {
        j += "\"sd_used_mb\":"  + String(SD_MMC.usedBytes()  / (1024*1024)) + ",";
        j += "\"sd_total_mb\":" + String(SD_MMC.totalBytes() / (1024*1024)) + ",";
    } else {
        j += "\"sd_used_mb\":0,\"sd_total_mb\":0,";
    }
    j += "\"vad_enabled\":"    + String(vadEnabled  ? "true" : "false") + ",";
    j += "\"vad_threshold\":"  + String(vad_threshold) + ",";
    j += "\"stt_enabled\":"    + String(sttEnabled  ? "true" : "false") + ",";
    j += "\"ble_svc_uuid\":\""  + ble_service_uuid  + "\",";
    j += "\"ble_char_uuid\":\"" + ble_char_uuid_tx  + "\"";
    j += "}";
    server.send(200, "application/json", j);
}

void startWebServer() {
    server.on("/",                           HTTP_GET,  handleRoot);
    server.on("/hotspot-detect.html",        HTTP_GET,  handleCaptiveRedirect);
    server.on("/library/test/success.html",  HTTP_GET,  handleCaptiveRedirect);
    server.on("/generate_204",               HTTP_GET,  handleCaptiveRedirect);
    server.on("/gen_204",                    HTTP_GET,  handleCaptiveRedirect);
    server.on("/connecttest.txt",            HTTP_GET,  handleCaptiveRedirect);
    server.on("/redirect",                   HTTP_GET,  handleCaptiveRedirect);
    server.on("/ncsi.txt",                   HTTP_GET,  handleCaptiveRedirect);
    server.on("/canonical.html",             HTTP_GET,  handleCaptiveRedirect);
    server.on("/wifi/connect",   HTTP_POST, handleWifiConnect);
    server.on("/api/wifi/scan",  HTTP_GET,  handleApiWifiScan);
    server.on("/wifi/add",       HTTP_POST, handleWifiAdd);
    server.on("/wifi/del",       HTTP_POST, handleWifiDel);
    server.on("/wifi/up",        HTTP_POST, handleWifiUp);
    server.on("/wifi/down",      HTTP_POST, handleWifiDown);
    server.on("/api/models",     HTTP_GET,  handleApiModels);
    server.on("/api/config",     HTTP_GET,  handleApiConfig);
    server.on("/api/test",       HTTP_GET,  handleApiTest);
    server.on("/api/voices",     HTTP_GET,  handleApiVoices);
    server.on("/llm/save",       HTTP_POST, handleLlmSave);
    server.on("/el/save",        HTTP_POST, handleElSave);
    server.on("/sd/format",      HTTP_POST, handleSdFormat);
    server.on("/vad/save",       HTTP_POST, handleVadSave);
    server.on("/cfg/list",       HTTP_GET,  handleCfgList);
    server.on("/cfg/activate",   HTTP_POST, handleCfgActivate);
    server.on("/cfg/rename",     HTTP_POST, handleCfgRename);
    server.on("/cfg/new",        HTTP_POST, handleCfgNew);
    server.on("/cfg/del",        HTTP_POST, handleCfgDel);
    server.on("/cfg/rule/set",   HTTP_POST, handleCfgRuleSet);
    server.on("/cfg/rule/del",   HTTP_POST, handleCfgRuleDel);
    server.on("/ble/scan",       HTTP_POST, handleBleScan);
    server.on("/ble/scan/stop",  HTTP_POST, handleBleScanStop);
    server.on("/ble/status",    HTTP_GET,  handleBleStatus);
    server.on("/ble/connect",   HTTP_POST, handleBleConnect);
    server.on("/ble/disconnect", HTTP_POST, handleBleDisconnect);
    server.on("/ble/abort",      HTTP_POST, handleBleAbort);
    server.on("/ble/reset",      HTTP_POST, handleBleReset);
    server.on("/ble/save",       HTTP_POST, handleBleSave);
    server.on("/sys/info",       HTTP_GET,  handleSysInfo);
    server.on("/api/home",       HTTP_GET,  handleApiHome);
    server.on("/reset", HTTP_POST, []() {
        server.send(200, "text/plain", "ok");
        if (bleScanning) { BLEDevice::getScan()->stop(); delay(200); }
        if (pBleClient && pBleClient->isConnected()) pBleClient->disconnect();
        delay(300);
        ESP.restart();
    });
    server.on("/camera",                  HTTP_GET,  handleCameraPage);
    server.on("/camera/stream",           HTTP_GET,  handleCameraStream);
    server.on("/camera/frame",            HTTP_GET,  handleCameraFrame);
    server.on("/camera/off",              HTTP_POST, handleCameraOff);
    server.on("/camera/describe",         HTTP_POST, handleCamDescribe);
    server.on("/camera/describe/prompt",  HTTP_GET,  handleCamDescPromptGet);
    server.on("/camera/describe/prompt",  HTTP_POST, handleCamDescPromptSave);
    server.on("/camera/quality", HTTP_POST, []() {
        int q = server.arg("q").toInt();
        if (camActive && q >= 4 && q <= 63) {
            sensor_t* s = esp_camera_sensor_get();
            if (s) s->set_quality(s, q);
        }
        server.send(200, "text/plain", "ok");
    });
    server.on("/debug",          HTTP_GET,  handleDebugPage);
    server.on("/debug/sensors",    HTTP_GET,  handleDebugSensors);
    server.on("/debug/cmd",        HTTP_POST, handleDebugCmd);
    server.on("/debug/mic/rms",    HTTP_GET,  handleDebugMicRms);
    server.on("/debug/amp",        HTTP_GET,  handleDebugAmp);
    server.on("/debug/amp",        HTTP_POST, handleDebugAmp);
    server.on("/debug/vol",        HTTP_POST, handleDebugVol);
    server.on("/personality",          HTTP_GET,  handlePersonalityGet);
    server.on("/personality/save",     HTTP_POST, handlePersonalitySave);
    server.on("/behaviors",            HTTP_GET,  handleBehaviorsGet);
    server.on("/behaviors/save",       HTTP_POST, handleBehaviorsSave);
    server.on("/behaviors/export",     HTTP_GET,  handleBehaviorsExport);
    server.on("/behaviors/import",     HTTP_POST, handleBehaviorsImport);
    server.on("/behaviors/actions",    HTTP_GET,  handleBehaviorsActions);
    server.on("/test/llm",             HTTP_POST, handleTestLlm);
    server.on("/test/tts",             HTTP_POST, handleTestTts);
    server.on("/test/voices",          HTTP_GET,  handleTestVoices);
    server.on("/test/behavior",        HTTP_POST, handleTestBehavior);
    server.on("/debug/tone",       HTTP_POST, handleDebugTone);
    server.on("/debug/mic/record", HTTP_POST, handleDebugMicRecord);
    server.on("/fs/list",    HTTP_GET,  handleFsList);
    server.on("/fs/put",     HTTP_POST, handleFsPut, handleFsUpload);
    server.on("/fs/del",     HTTP_POST, handleFsDel);
    server.on("/favicon.ico",    HTTP_GET,  []() { server.sendHeader("Cache-Control","public, max-age=86400"); server.send(204); });
    server.on("/apple-touch-icon.png",        HTTP_GET, []() { server.sendHeader("Cache-Control","public, max-age=86400"); server.send(204); });
    server.on("/apple-touch-icon-precomposed.png", HTTP_GET, []() { server.sendHeader("Cache-Control","public, max-age=86400"); server.send(204); });
    server.on("/manifest.json",  HTTP_GET,  []() { server.sendHeader("Cache-Control","public, max-age=86400"); server.send(204); });
    // Serve immagini statiche da SPIFFS: GET /img/<nome>.png|jpg
    server.on("/img/logo.png",    HTTP_GET, []() {
        File f = SPIFFS.open("/logo.png","r");
        if (!f) { server.send(404); return; }
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(f, "image/png"); f.close();
    });
    server.on("/img/title.png",   HTTP_GET, []() {
        File f = SPIFFS.open("/title.png","r");
        if (!f) { server.send(404); return; }
        server.sendHeader("Cache-Control", "public, max-age=86400");
        server.streamFile(f, "image/png"); f.close();
    });
    server.on("/shared.css", HTTP_GET, []() {
        File f = SPIFFS.open("/shared.css","r");
        if (!f) { server.send(404); return; }
        server.sendHeader("Cache-Control", "public, max-age=3600");
        server.streamFile(f, "text/css; charset=utf-8"); f.close();
    });
    server.on("/shared.js", HTTP_GET, []() {
        File f = SPIFFS.open("/shared.js","r");
        if (!f) { server.send(404); return; }
        server.sendHeader("Cache-Control", "public, max-age=3600");
        server.streamFile(f, "application/javascript; charset=utf-8"); f.close();
    });

    server.onNotFound([]() {
        String uri = server.uri();
        // /fs/get/* → serve file da SPIFFS
        if (uri.startsWith("/fs/get/") || uri == "/fs/get") {
            handleFsGet();
            return;
        }
        Serial.println("HTTP 404: " + uri + " [" + String(server.method()) + "]");
        if (isConfigMode) handleCaptiveRedirect();
        else server.send(404, "text/plain", "Not found");
    });
    server.begin();
}

// ==========================================
// CONFIGURAZIONI COMPORTAMENTO — PERSISTENZA
// ==========================================

void saveBehaviorConfigs() {
    // Serializza in JSON su preferences (max ~3800 byte con 8 config)
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
            ro["s"] = rule.sensorId;
            ro["l"] = rule.len;
            JsonArray ba = ro["b"].to<JsonArray>();
            for (int b = 0; b < rule.len; b++) ba.add(rule.bytes[b]);
        }
    }
    String out; serializeJson(doc, out);
    preferences.putString("behaviors", out);
}

void loadBehaviorConfigs() {
    String raw = preferences.getString("behaviors", "");
    if (raw.length() == 0) {
        // Config di default: sensori → azioni di base
        behaviorConfigCount = 1;
        activeBehaviorConfig = 0;
        strncpy(behaviorConfigs[0].name, "Default", 24);
        memset(behaviorConfigs[0].rules, 0, sizeof(behaviorConfigs[0].rules));
        // Tickle testa → Hey hey [0x13,0x00,39,3,1,1]
        behaviorConfigs[0].rules[0] = { SEN_TICKLE_HEAD, {0x13,0x00,39,3,1,1}, 6 };
        // Tira coda → Legendary! [0x13,0x00,39,3,8,1]
        behaviorConfigs[0].rules[1] = { SEN_PULL_TAIL, {0x13,0x00,39,3,8,1}, 6 };
        // Capovolto → antenna rossa [0x14,255,0,0]
        behaviorConfigs[0].rules[2] = { SEN_UPSIDE_DOWN, {0x14,255,0,0}, 4 };
        // Dritto → antenna verde [0x14,0,255,0]
        behaviorConfigs[0].rules[3] = { SEN_UPRIGHT, {0x14,0,255,0}, 4 };
        saveBehaviorConfigs();
        return;
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
            for (int v : ro["b"].as<JsonArray>()) {
                if (bi < 6) cfg.rules[ri].bytes[bi++] = (uint8_t)v;
            }
            ri++;
        }
    }
}

// Esegue le regole attive se il sensore è appena diventato true (fronte)
void applyBehaviorRules(const FurbySensors& prev, const FurbySensors& cur) {
    if (activeBehaviorConfig < 0 || activeBehaviorConfig >= behaviorConfigCount) return;
    // Ignora i primi 3s dopo la connessione — il Furby invia pacchetti di stato iniziali
    // che triggerebbero regole prima che il canale sia stabile
    if (bleConnectedMs && millis() - bleConnectedMs < 3000) return;
    BehaviorConfig& cfg = behaviorConfigs[activeBehaviorConfig];

    // Mappa sensorId → valore corrente/precedente
    bool curVals[SEN_COUNT]  = {};
    bool prevVals[SEN_COUNT] = {};
    curVals[SEN_ANT_L]       = cur.antennaLeft;    prevVals[SEN_ANT_L]       = prev.antennaLeft;
    curVals[SEN_ANT_R]       = cur.antennaRight;   prevVals[SEN_ANT_R]       = prev.antennaRight;
    curVals[SEN_ANT_F]       = cur.antennaForward; prevVals[SEN_ANT_F]       = prev.antennaForward;
    curVals[SEN_ANT_B]       = cur.antennaBack;    prevVals[SEN_ANT_B]       = prev.antennaBack;
    curVals[SEN_TICKLE_HEAD] = cur.tickleHead;     prevVals[SEN_TICKLE_HEAD] = prev.tickleHead;
    curVals[SEN_TICKLE_TUMMY]= cur.tickleTummy;    prevVals[SEN_TICKLE_TUMMY]= prev.tickleTummy;
    curVals[SEN_TICKLE_R]    = cur.tickleRight;    prevVals[SEN_TICKLE_R]    = prev.tickleRight;
    curVals[SEN_TICKLE_L]    = cur.tickleLeft;     prevVals[SEN_TICKLE_L]    = prev.tickleLeft;
    curVals[SEN_PULL_TAIL]   = cur.pullTail;       prevVals[SEN_PULL_TAIL]   = prev.pullTail;
    curVals[SEN_PUSH_TONGUE] = cur.pushTongue;     prevVals[SEN_PUSH_TONGUE] = prev.pushTongue;
    curVals[SEN_UPRIGHT]     = cur.upright;        prevVals[SEN_UPRIGHT]     = prev.upright;
    curVals[SEN_UPSIDE_DOWN] = cur.upsideDown;     prevVals[SEN_UPSIDE_DOWN] = prev.upsideDown;
    curVals[SEN_SIDE_R]      = cur.onRightSide;    prevVals[SEN_SIDE_R]      = prev.onRightSide;
    curVals[SEN_SIDE_L]      = cur.onLeftSide;     prevVals[SEN_SIDE_L]      = prev.onLeftSide;
    curVals[SEN_LEAN_BACK]   = cur.leanBack;       prevVals[SEN_LEAN_BACK]   = prev.leanBack;
    curVals[SEN_TILT_R]      = cur.tiltRight;      prevVals[SEN_TILT_R]      = prev.tiltRight;
    curVals[SEN_TILT_L]      = cur.tiltLeft;       prevVals[SEN_TILT_L]      = prev.tiltLeft;

    for (int r = 0; r < MAX_CFG_RULES; r++) {
        BehaviorRule& rule = cfg.rules[r];
        if (rule.sensorId == SEN_NONE || rule.len == 0) continue;
        if (curVals[rule.sensorId] && !prevVals[rule.sensorId])
            furbyWrite(rule.bytes, rule.len);
    }

    // Fronte di salita su qualsiasi sensore → trigger per il sistema comportamenti
    if (!isProcessing && !wakeUpTriggered) {
        for (int s = 1; s < SEN_COUNT; s++) {
            if (curVals[s] && !prevVals[s]) {
                pendingTrigger  = TRG_SENSOR;
                pendingSensorId = (uint8_t)s;
                pendingEvent    = EVT_NONE;
                wakeUpTriggered = true;
                break;
            }
        }
    }
}

void startCaptivePortal() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Furby_Config");
    delay(500);
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    isConfigMode = true;
    Serial.println("Captive portal attivo: " + WiFi.softAPIP().toString());
}

// ==========================================
// AUDIO & CACHE LOGIC (MODALITA' SD)
// ==========================================
String getCacheJSON() {
    if (!sdAvailable) return "{}";
    File f = SD_MMC.open("/index.json");
    if (!f) return "{}";
    String data = f.readString();
    f.close();
    return data;
}

// Restituisce JSON semplificato {filename: "testo"} da passare all'LLM (esclude campo prefix)
String getCacheSummaryJSON() {
    JsonDocument full;
    deserializeJson(full, getCacheJSON());
    JsonDocument summary;
    for (JsonPair kv : full.as<JsonObject>()) {
        JsonVariant v = kv.value();
        if (v.is<JsonObject>()) summary[kv.key()] = v["text"].as<String>();
        else                    summary[kv.key()] = v.as<String>(); // retrocompat
    }
    String out; serializeJson(summary, out); return out;
}

void saveCacheIndex(JsonDocument& doc) {
    File f = SD_MMC.open("/index.json", FILE_WRITE);
    if (f) { serializeJson(doc, f); f.close(); }
}

void updateCacheJSON(String newFilename, String text) {
    if (!sdAvailable) return;
    JsonDocument doc;
    deserializeJson(doc, getCacheJSON());
    // nuovo formato: oggetto {text, prefix}
    JsonObject entry = doc[newFilename].to<JsonObject>();
    entry["text"]   = text;
    entry["prefix"] = "";
    saveCacheIndex(doc);
}

// Restituisce il filename del prefix per audioFile, o "" se non ancora generato
String getCachedPrefix(const String& audioFile) {
    JsonDocument doc;
    deserializeJson(doc, getCacheJSON());
    JsonVariant v = doc[audioFile];
    if (v.is<JsonObject>()) {
        String p = v["prefix"].as<String>();
        if (p.length() > 0) return p;
    }
    return "";
}

// Salva il filename del prefix per audioFile nell'index
void setCachedPrefix(const String& audioFile, const String& prefixFile) {
    if (!sdAvailable) return;
    JsonDocument doc;
    deserializeJson(doc, getCacheJSON());
    JsonVariant v = doc[audioFile];
    if (v.is<JsonObject>()) {
        v["prefix"] = prefixFile;
    } else {
        // vecchio formato stringa: migra
        String oldText = v.as<String>();
        JsonObject entry = doc[audioFile].to<JsonObject>();
        entry["text"]   = oldText;
        entry["prefix"] = prefixFile;
    }
    saveCacheIndex(doc);
}

String getNextFilename() {
    Preferences prefs; prefs.begin("furby_sys", false);
    int counter = prefs.getInt("file_id", 0) + 1;
    prefs.putInt("file_id", counter); prefs.end();
    return String(counter) + ".pcm";
}

static String elOutputFormat() {
    return el_audio_fmt == "mp3" ? "mp3_22050_32" : "pcm_16000_16_mono";
}

class AudioOutputI2SDirect : public AudioOutput {
public:
    bool begin() override { setAmplifier(true); isSpeaking = true; return true; }
    bool ConsumeSample(int16_t sample[2]) override {
        long amp = (abs((int)sample[0]) + abs((int)sample[1])) / 2;
        currentAmplitude = (int)amp;
        i2s_write_mono(sample, 1);
        return true;
    }
    bool stop() override {
        i2s_zero_dma_buffer(I2S_NUM);
        isSpeaking = false; currentAmplitude = 0;
        setAmplifier(false); return true;
    }
};

static void playMp3FromStream(WiFiClient* stream, HTTPClient& http) {
    int contentLen = http.getSize();
    Serial.printf("[MP3] content-length: %d\n", contentLen);
    size_t bufSize = (contentLen > 0) ? (size_t)contentLen : 256 * 1024;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { Serial.println("[MP3] ERRORE: malloc PSRAM fallito"); return; }
    size_t received = 0;
    uint8_t tmp[512];
    while ((http.connected() || stream->available()) && received < bufSize) {
        if (stream->available()) {
            int n = stream->readBytes(tmp, min((int)sizeof(tmp), (int)(bufSize - received)));
            memcpy(buf + received, tmp, n);
            received += n;
        } else { delay(1); }
    }
    Serial.printf("[MP3] ricevuti %u byte\n", received);
    AudioFileSourceBuffer* src = new AudioFileSourceBuffer(new AudioFileSourcePROGMEM(buf, received), 4096);
    AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
    AudioOutputI2SDirect* out = new AudioOutputI2SDirect();
    out->begin(); mp3->begin(src, out);
    while (mp3->isRunning()) { if (!mp3->loop()) { mp3->stop(); break; } }
    out->stop();
    delete mp3; delete src; delete out; free(buf);
}

// Genera TTS, salva su SD, restituisce filename (o "" in caso di errore). Non riproduce.
String generateAndSaveTTS_SD(const String& text) {
    bool mp3mode = (el_audio_fmt == "mp3");
    String ext = mp3mode ? ".mp3" : ".pcm";
    String filename = getNextFilename();
    if (mp3mode) filename = filename.substring(0, filename.lastIndexOf('.')) + ext;
    Serial.printf("[TTS-SAVE] fmt=%s \"%s\" -> %s\n", el_audio_fmt.c_str(), text.c_str(), filename.c_str());
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id
               + "?output_format=" + elOutputFormat());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    JsonDocument doc; doc["text"] = text; doc["model_id"] = "eleven_multilingual_v2";
    String payload; serializeJson(doc, payload);
    int code = http.POST(payload);
    Serial.printf("[TTS-SAVE] HTTP %d\n", code);
    if (code == 200) {
        File f = SD_MMC.open("/" + filename, FILE_WRITE);
        if (f) { http.writeToStream(&f); f.close(); }
        else { Serial.printf("[TTS-SAVE] ERRORE apertura /%s\n", filename.c_str()); http.end(); return ""; }
    } else {
        Serial.printf("[TTS-SAVE] ERRORE HTTP %d\n", code);
        http.end(); return "";
    }
    http.end();
    return filename;
}

void playAudioSD(String filename) {
    if (!sdAvailable) { Serial.println("[AUDIO-SD] ERRORE: SD non disponibile"); return; }
    Serial.printf("[AUDIO-SD] riproduco: /%s\n", filename.c_str());
    File file = SD_MMC.open("/" + filename);
    if (!file) { Serial.printf("[AUDIO-SD] ERRORE: file /%s non trovato\n", filename.c_str()); return; }
    size_t sz = file.size();
    Serial.printf("[AUDIO-SD] file aperto: %u byte\n", sz);

    if (filename.endsWith(".mp3")) {
        uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) { Serial.println("[AUDIO-SD] ERRORE malloc MP3"); file.close(); return; }
        file.read(buf, sz); file.close();
        AudioFileSourceBuffer* src = new AudioFileSourceBuffer(
            new AudioFileSourcePROGMEM(buf, sz), 4096);
        AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
        AudioOutputI2SDirect* out = new AudioOutputI2SDirect();
        out->begin(); mp3->begin(src, out);
        while (mp3->isRunning()) { if (!mp3->loop()) { mp3->stop(); break; } }
        out->stop();
        delete mp3; delete src; delete out; free(buf);
    } else {
        setAmplifier(true); isSpeaking = true;
        size_t bytesRead;
        uint8_t buffer[1024];
        while (file.available()) {
            bytesRead = file.read(buffer, sizeof(buffer));
            int16_t* pcm = (int16_t*)buffer;
            int n = bytesRead / 2; long sum = 0;
            for (int i = 0; i < n; i++) sum += abs(pcm[i]);
            currentAmplitude = n > 0 ? sum / n : 0;
            i2s_write_mono(pcm, n);
        }
        file.close();
        i2s_zero_dma_buffer(I2S_NUM);
        isSpeaking = false; currentAmplitude = 0;
        setAmplifier(false);
    }
}

// Riproduce un file dalla cache SD con prefix lazy:
// - se prefix già esiste in index → riproduce prefix + main
// - se prefix non esiste → genera prefix via LLM+TTS, salva, aggiorna index, poi riproduce
void playAudioSDWithPrefix(const String& filename) {
    String prefixFile = getCachedPrefix(filename);
    if (prefixFile.length() == 0) {
        // prima volta che questo file viene riprodotto dalla cache: genera il prefisso
        Serial.printf("[CACHE-PREFIX] genero prefisso per %s\n", filename.c_str());
        // Chiamata LLM leggera: solo testo, nessuna immagine
        String sysP = "Sei un Furby cinico e scocciato. Genera UNA SOLA frase breve (max 10 parole) "
                      "per introdurre una risposta che hai già dato in precedenza. "
                      "Solo la frase, nessun'altra parola.";
        String prefixText = callLLM("", sysP, "");
        prefixText.trim();
        if (prefixText.length() > 0) {
            Serial.printf("[CACHE-PREFIX] testo: \"%s\"\n", prefixText.c_str());
            prefixFile = generateAndSaveTTS_SD(prefixText);
            if (prefixFile.length() > 0) {
                setCachedPrefix(filename, prefixFile);
                Serial.printf("[CACHE-PREFIX] salvato come %s\n", prefixFile.c_str());
                playAudioSD(prefixFile);
            }
        } else {
            Serial.println("[CACHE-PREFIX] LLM risposta vuota, salto prefisso");
        }
    } else {
        Serial.printf("[CACHE-PREFIX] prefix esistente: %s\n", prefixFile.c_str());
        playAudioSD(prefixFile);
    }
    playAudioSD(filename);
}

void generateAndPlayTTS_SD(String text) {
    String filename = generateAndSaveTTS_SD(text);
    if (filename.length() == 0) return;
    updateCacheJSON(filename, text);
    playAudioSD(filename);
}

// ==========================================
// AUDIO LOGIC (MODALITA' RAM FALLBACK)
// ==========================================
void streamAndPlayTTS_RAM(String text) {
    bool mp3mode = (el_audio_fmt == "mp3");
    Serial.printf("[TTS-RAM] fmt=%s testo: \"%s\"\n", el_audio_fmt.c_str(), text.c_str());
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id
               + "?output_format=" + elOutputFormat());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    JsonDocument doc; doc["text"] = text; doc["model_id"] = "eleven_multilingual_v2";
    String payload; serializeJson(doc, payload);
    int code = http.POST(payload);
    Serial.printf("[TTS-RAM] HTTP %d\n", code);
    if (code == 200) {
        WiFiClient* stream = http.getStreamPtr();
        if (mp3mode) {
            playMp3FromStream(stream, http);
        } else {
            uint8_t buffer[1024];
            setAmplifier(true); isSpeaking = true;
            Serial.println("[TTS-RAM] streaming PCM -> I2S");
            int totalBytes = 0;
            while (http.connected() || stream->available()) {
                if (stream->available()) {
                    int n = stream->readBytes(buffer, sizeof(buffer));
                    totalBytes += n;
                    int16_t* pcm = (int16_t*)buffer;
                    int ns = n / 2; long sum = 0;
                    for (int i = 0; i < ns; i++) sum += abs(pcm[i]);
                    currentAmplitude = ns > 0 ? sum / ns : 0;
                    i2s_write_mono(pcm, ns);
                }
                delay(1);
            }
            Serial.printf("[TTS-RAM] fine, %d byte PCM\n", totalBytes);
            i2s_zero_dma_buffer(I2S_NUM);
            isSpeaking = false; currentAmplitude = 0;
            setAmplifier(false);
        }
    } else {
        Serial.printf("[TTS-RAM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
    }
    http.end();
}

// ==========================================
// I2S INIT
// ==========================================
void initI2S() {
    // Full-duplex: TX → ES8311 (speaker), RX ← ES7210 (mic)
    // ES7210 è I2S slave: usa MCLK/BCLK/LRCK generati da questo master
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate          = 16000,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = true,
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 4096000
    };
    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_MCLK,
        .bck_io_num   = I2S_BCLK,
        .ws_io_num    = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num  = I2S_DIN
    };
    i2s_driver_install(I2S_NUM, &cfg, 0, NULL);
    i2s_set_pin(I2S_NUM, &pins);
    i2s_set_clk(I2S_NUM, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

static void i2s_write_mono(const int16_t* buf, int samples) {
    size_t written;
    i2s_write(I2S_NUM, buf, samples * sizeof(int16_t), &written, portMAX_DELAY);
}

// ==========================================
// CORE AI — chiamata LLM con supporto multi-provider
// ==========================================
String base64Encode(uint8_t* data, size_t length) {
    size_t outLen;
    mbedtls_base64_encode(nullptr, 0, &outLen, data, length);
    // Usa PSRAM: la base64 di un frame QVGA può essere 30-50KB
    unsigned char* buf = (unsigned char*)heap_caps_malloc(outLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (unsigned char*)malloc(outLen); // fallback heap interno
    if (!buf) return "";
    mbedtls_base64_encode(buf, outLen, &outLen, data, length);
    String result = String((char*)buf); free(buf);
    return result;
}

// Costruisce il payload JSON in PSRAM e lo invia; restituisce solo il testo risposta
String callLLM(const String& base64Img, const String& systemPrompt, const String& userText) {
    Serial.printf("[LLM] provider=%s model=%s img=%u char heap=%u PSRAM=%u\n",
        llm_provider.c_str(), llm_model.c_str(), base64Img.length(),
        esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String answer;

    // Costruisce il payload in PSRAM per non esaurire l'heap interno (~80-100KB con base64)
    auto makePsramPayload = [&](const String& body) -> uint8_t* {
        size_t len = body.length();
        uint8_t* buf = (uint8_t*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint8_t*)malloc(len + 1);
        if (!buf) Serial.printf("[LLM] ERRORE: malloc payload %u byte fallito\n", len + 1);
        else memcpy(buf, body.c_str(), len + 1);
        return buf;
    };

    if (llm_provider == "openai") {
        Serial.println("[LLM] -> OpenAI /v1/chat/completions");
        http.begin(client, "https://api.openai.com/v1/chat/completions");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + openai_api_key);

        String userContent = "[{\"type\":\"text\",\"text\":\"" + userText + "\"}";
        if (base64Img.length() > 0)
            userContent += ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," + base64Img + "\"}}";
        userContent += "]";
        String body = "{\"model\":\"" + llm_model + "\",\"messages\":["
            "{\"role\":\"system\",\"content\":\"" + systemPrompt + "\"},"
            "{\"role\":\"user\",\"content\":" + userContent + "}]}";
        Serial.printf("[LLM] payload %u byte\n", body.length());
        uint8_t* buf = makePsramPayload(body);
        body = "";
        if (buf) {
            int code = http.POST(buf, strlen((char*)buf));
            Serial.printf("[LLM] HTTP %d\n", code);
            if (code == 200) {
                String res = http.getString();
                int s = res.indexOf("\"content\": \"") + 12;
                int e = res.indexOf("\"", s);
                if (s > 11) answer = res.substring(s, e);
                else Serial.printf("[LLM] ERRORE parsing risposta: %s\n", res.substring(0, 200).c_str());
            } else {
                Serial.printf("[LLM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
            }
            free(buf);
        }

    } else if (llm_provider == "claude") {
        Serial.println("[LLM] -> Anthropic /v1/messages");
        http.begin(client, "https://api.anthropic.com/v1/messages");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-api-key", claude_api_key);
        http.addHeader("anthropic-version", "2023-06-01");

        String claudeContent = "[{\"type\":\"text\",\"text\":\"" + userText + "\"}";
        if (base64Img.length() > 0)
            claudeContent += ",{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\",\"data\":\"" + base64Img + "\"}}";
        claudeContent += "]";
        String body = "{\"model\":\"" + llm_model + "\",\"max_tokens\":128,"
            "\"system\":\"" + systemPrompt + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":" + claudeContent + "}]}";
        Serial.printf("[LLM] payload %u byte\n", body.length());
        uint8_t* buf = makePsramPayload(body);
        body = "";
        if (buf) {
            int code = http.POST(buf, strlen((char*)buf));
            Serial.printf("[LLM] HTTP %d\n", code);
            if (code == 200) {
                String res = http.getString();
                int s = res.indexOf("\"text\": \"") + 9;
                int e = res.indexOf("\"", s);
                if (s > 8) answer = res.substring(s, e);
                else Serial.printf("[LLM] ERRORE parsing risposta: %s\n", res.substring(0, 200).c_str());
            } else {
                Serial.printf("[LLM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
            }
            free(buf);
        }
    } else {
        Serial.printf("[LLM] ERRORE: provider sconosciuto \"%s\"\n", llm_provider.c_str());
    }

    http.end();
    answer.replace("\\n", ""); answer.trim();
    Serial.printf("[LLM] risposta finale: \"%s\"\n", answer.c_str());
    return answer;
}

// Invia gSttBuf (gSttLen campioni mono int16 @ 16kHz) a Whisper e ritorna la trascrizione.
// Costruisce il WAV header on-the-fly, usa multipart/form-data.
// Ritorna "" in caso di errore.
String transcribeAudio() {
    if (!gSttBuf || gSttLen <= 0) return "";
    String key = openai_api_key;
    if (key.length() == 0) key = preferences.getString("openai", "");
    if (key.length() == 0) { Serial.println("[STT] nessuna chiave OpenAI"); return ""; }

    size_t pcmBytes = (size_t)gSttLen * 2;
    // WAV header: 44 byte fissi, PCM mono 16bit 16kHz
    uint8_t wav[44];
    uint32_t dataSize   = (uint32_t)pcmBytes;
    uint32_t chunkSize  = 36 + dataSize;
    uint32_t sampleRate = 16000;
    uint16_t channels   = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate   = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;
    memcpy(wav,      "RIFF", 4);
    memcpy(wav+4,  &chunkSize,    4);
    memcpy(wav+8,    "WAVE", 4);
    memcpy(wav+12,   "fmt ", 4);
    uint32_t subchunk1 = 16; memcpy(wav+16, &subchunk1, 4);
    uint16_t audioFmt  = 1;  memcpy(wav+20, &audioFmt,  2);
    memcpy(wav+22, &channels,      2);
    memcpy(wav+24, &sampleRate,    4);
    memcpy(wav+28, &byteRate,      4);
    memcpy(wav+32, &blockAlign,    2);
    memcpy(wav+34, &bitsPerSample, 2);
    memcpy(wav+36,   "data", 4);
    memcpy(wav+40, &dataSize,      4);

    // Boundary multipart
    String boundary = "----WavBnd7210";
    String head = "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                  "Content-Type: audio/wav\r\n\r\n";
    String modelPart = "\r\n--" + boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                       "whisper-1"
                       "\r\n--" + boundary + "--\r\n";

    size_t totalLen = head.length() + 44 + pcmBytes + modelPart.length();

    WiFiClientSecure cli;
    cli.setInsecure();
    if (!cli.connect("api.openai.com", 443)) {
        Serial.println("[STT] connessione fallita");
        return "";
    }
    // Request headers
    cli.printf("POST /v1/audio/transcriptions HTTP/1.1\r\n"
               "Host: api.openai.com\r\n"
               "Authorization: Bearer %s\r\n"
               "Content-Type: multipart/form-data; boundary=%s\r\n"
               "Content-Length: %u\r\n"
               "Connection: close\r\n\r\n",
               key.c_str(), boundary.c_str(), (unsigned)totalLen);
    cli.print(head);
    cli.write(wav, 44);
    // Invia PCM a chunk di 4096 byte per non saturare il buffer TCP
    const size_t CHUNK = 4096;
    uint8_t* pcmPtr = (uint8_t*)gSttBuf;
    size_t sent = 0;
    while (sent < pcmBytes) {
        size_t toSend = min(CHUNK, pcmBytes - sent);
        cli.write(pcmPtr + sent, toSend);
        sent += toSend;
    }
    cli.print(modelPart);

    // Leggi risposta HTTP
    unsigned long t0 = millis();
    while (!cli.available() && millis() - t0 < 15000) delay(50);
    String resp;
    while (cli.available()) resp += (char)cli.read();
    cli.stop();

    // Estrai body JSON (dopo doppio \r\n)
    int bodyStart = resp.indexOf("\r\n\r\n");
    if (bodyStart < 0) { Serial.println("[STT] risposta malformata"); return ""; }
    String body = resp.substring(bodyStart + 4);
    // HTTP chunked: il body potrebbe iniziare con la dimensione hex del chunk
    // Se il primo carattere è esadecimale, saltiamo la prima riga
    if (body.length() > 0 && isxdigit(body[0])) {
        int nl = body.indexOf("\r\n");
        if (nl >= 0) body = body.substring(nl + 2);
    }
    Serial.printf("[STT] body: %s\n", body.c_str());

    // Estrai campo "text" dal JSON
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[STT] JSON parse error");
        return "";
    }
    String text = doc["text"].as<String>();
    text.trim();
    Serial.printf("[STT] trascrizione: \"%s\"\n", text.c_str());
    return text;
}

// ==========================================
// SISTEMA EVENTI / COMPORTAMENTI
// ==========================================

void saveEventBehaviors() {
    JsonDocument doc;
    JsonObject pers = doc["personality"].to<JsonObject>();
    pers["prompt"]   = gPersonalityPrompt;
    pers["voice_id"] = gPersonalityVoiceId;
    JsonArray arr = doc["behaviors"].to<JsonArray>();
    for (int i = 0; i < gEventBehaviorCount; i++) {
        EventBehavior& b = gEventBehaviors[i];
        JsonObject bo = arr.add<JsonObject>();
        bo["id"]        = b.id;
        bo["trigger"]   = (int)b.trigger;
        bo["sensor_id"] = (int)b.sensor_id;
        bo["name"]      = b.name;
        JsonArray cs = bo["consequences"].to<JsonArray>();
        for (int c = 0; c < b.consequence_count; c++) {
            Consequence& q = b.consequences[c];
            JsonObject co = cs.add<JsonObject>();
            co["type"]      = (int)q.type;
            co["action_id"] = q.action_id;
            co["text"]      = q.text;
            co["snapshot"]  = q.snapshot;
            JsonArray ra = co["reactions"].to<JsonArray>();
            for (int r = 0; r < q.reaction_count; r++) ra.add(q.reactions[r]);
        }
    }
    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/behaviors.json", "w");
    if (f) { f.print(out); f.close(); }
}

void loadEventBehaviors() {
    gPersonalityPrompt  = "Sei un giocattolo cinico, volgare e scontroso. Genera frasi BREVISSIME (max 8 parole). Sii diretto e sgarbato.";
    gPersonalityVoiceId = elevenlabs_voice_id;
    gEventBehaviorCount = 0;
    if (!SPIFFS.exists("/behaviors.json")) {
        // Comportamento di default: VAD -> prompt_llm senza azioni fisiche
        EventBehavior& b = gEventBehaviors[0];
        strlcpy(b.id,   "vad_default", sizeof(b.id));
        b.trigger   = TRG_VAD;
        b.sensor_id = 0;
        strlcpy(b.name, "Parlato rilevato (VAD)", sizeof(b.name));
        b.consequences[0].type     = CSQ_PROMPT_LLM;
        b.consequences[0].snapshot = true;
        strlcpy(b.consequences[0].text, "Qualcuno ti sta parlando. Reagisci.", sizeof(b.consequences[0].text));
        b.consequence_count = 1;
        gEventBehaviorCount = 1;
        saveEventBehaviors();
        return;
    }
    File f = SPIFFS.open("/behaviors.json", "r");
    if (!f) return;
    String raw = f.readString(); f.close();
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) return;
    if (doc["personality"].is<JsonObject>()) {
        gPersonalityPrompt  = doc["personality"]["prompt"]   | gPersonalityPrompt;
        gPersonalityVoiceId = doc["personality"]["voice_id"] | gPersonalityVoiceId;
    }
    for (JsonObject bo : doc["behaviors"].as<JsonArray>()) {
        if (gEventBehaviorCount >= MAX_EVENT_BEHAVIORS) break;
        EventBehavior& b = gEventBehaviors[gEventBehaviorCount++];
        strlcpy(b.id,   bo["id"]   | "", sizeof(b.id));
        b.trigger   = (TriggerType)(bo["trigger"]   | (int)(bo["event"] | 0));  // compatibilità JSON vecchio
        b.sensor_id = (uint8_t)(bo["sensor_id"] | 0);
        strlcpy(b.name, bo["name"] | "", sizeof(b.name));
        b.consequence_count = 0;
        for (JsonObject co : bo["consequences"].as<JsonArray>()) {
            if (b.consequence_count >= MAX_CONSEQUENCES) break;
            Consequence& q = b.consequences[b.consequence_count++];
            q.type     = (ConsequenceType)(co["type"] | 0);
            strlcpy(q.action_id, co["action_id"] | "", sizeof(q.action_id));
            strlcpy(q.text,      co["text"]      | "", sizeof(q.text));
            q.snapshot = co["snapshot"] | false;
            q.reaction_count = 0;
            for (JsonVariant rv : co["reactions"].as<JsonArray>()) {
                if (q.reaction_count >= MAX_REACTIONS) break;
                strlcpy(q.reactions[q.reaction_count++], rv.as<const char*>() ? rv.as<const char*>() : "", 32);
            }
        }
    }
}

const FurbyActionDef* findFurbyAction(const char* id) {
    for (int i = 0; i < FURBY_ACTIONS_COUNT; i++)
        if (strcmp(FURBY_ACTIONS[i].id, id) == 0) return &FURBY_ACTIONS[i];
    return nullptr;
}

static void speakText(const String& text) {
    Serial.printf("[SPEAK] \"%s\" (SD=%s)\n", text.c_str(), sdAvailable ? "si" : "no");
    if (sdAvailable) generateAndPlayTTS_SD(text);
    else             streamAndPlayTTS_RAM(text);
}

void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText) {
    Serial.printf("[CSQ] tipo=%d snapshot=%d testo=\"%s\"\n", csq.type, csq.snapshot, csq.text);
    switch (csq.type) {
        case CSQ_FURBY_ACTION: {
            const FurbyActionDef* act = findFurbyAction(csq.action_id);
            if (act) {
                if (gDryRun) {
                    Serial.printf("[CSQ] DRY RUN — azione Furby SKIPPATA: %s\n", act->label);
                } else {
                    Serial.printf("[CSQ] azione Furby: %s\n", act->label);
                    furbyWrite(act->cmd, act->len); delay(1500);
                }
            } else {
                Serial.printf("[CSQ] ERRORE: azione \"%s\" non trovata\n", csq.action_id);
            }
            break;
        }
        case CSQ_TTS_FIXED: {
            String t = String(csq.text); t.trim();
            Serial.printf("[CSQ] TTS fisso: \"%s\"\n", t.c_str());
            if (t.length() > 0) speakText(t);
            break;
        }
        case CSQ_PROMPT_FIXED: {
            String img = csq.snapshot ? base64Img : "";
            // Se c'è trascrizione STT, la anteponiamo al prompt fisso per dare contesto
            String userMsg = sttText.length() > 0
                ? "L'utente ha detto: \"" + sttText + "\". " + String(csq.text)
                : String(csq.text);
            Serial.printf("[CSQ] prompt fisso (img=%s): \"%s\"\n", csq.snapshot ? "si" : "no", userMsg.c_str());
            String answer = callLLM(img, gPersonalityPrompt, userMsg);
            if (answer.length() > 0) speakText(answer);
            else Serial.println("[CSQ] ERRORE: LLM risposta vuota");
            break;
        }
        case CSQ_PROMPT_LLM: {
            String img = csq.snapshot ? base64Img : "";
            // STT disponibile: sostituisce il testo del behavior con la trascrizione reale
            String userMsg = sttText.length() > 0 ? sttText : String(csq.text);
            Serial.printf("[CSQ] prompt LLM (img=%s, reazioni=%d, stt=%s): \"%s\"\n",
                csq.snapshot ? "si" : "no", csq.reaction_count,
                sttText.length() > 0 ? "si" : "no", userMsg.c_str());
            if (csq.reaction_count == 0) {
                String answer = callLLM(img, gPersonalityPrompt, userMsg);
                if (answer.length() > 0) speakText(answer);
                else Serial.println("[CSQ] ERRORE: LLM risposta vuota");
            } else {
                String reactList;
                for (int r = 0; r < csq.reaction_count; r++) {
                    const FurbyActionDef* act = findFurbyAction(csq.reactions[r]);
                    if (act) reactList += String(act->id) + ": " + act->label + "\n";
                }
                String sys = gPersonalityPrompt +
                    " Rispondi SOLO con JSON valido (niente altro testo): "
                    "{\"action\":\"id_o_null\",\"speech_before\":\"...\",\"speech_after\":\"...\"}."
                    " Frasi max 6 parole o null. Azioni disponibili:\n" + reactList;
                String answer = callLLM(img, sys, userMsg);
                Serial.printf("[CSQ] risposta JSON: \"%s\"\n", answer.c_str());
                String speechBefore, speechAfter, actionId;
                JsonDocument rdoc;
                if (deserializeJson(rdoc, answer) == DeserializationError::Ok) {
                    speechBefore = rdoc["speech_before"] | "";
                    speechAfter  = rdoc["speech_after"]  | "";
                    actionId     = rdoc["action"]         | "";
                    Serial.printf("[CSQ] JSON parsato: before=\"%s\" action=\"%s\" after=\"%s\"\n",
                        speechBefore.c_str(), actionId.c_str(), speechAfter.c_str());
                } else {
                    Serial.println("[CSQ] parsing JSON fallito, uso risposta come testo");
                    speechBefore = answer;
                }
                if (speechBefore.length() > 0 && speechBefore != "null") speakText(speechBefore);
                if (actionId.length() > 0 && actionId != "null") {
                    const FurbyActionDef* act = findFurbyAction(actionId.c_str());
                    if (act) { Serial.printf("[CSQ] eseguo azione: %s\n", act->label); furbyWrite(act->cmd, act->len); delay(1500); }
                    else Serial.printf("[CSQ] ERRORE: azione \"%s\" non trovata\n", actionId.c_str());
                }
                if (speechAfter.length() > 0 && speechAfter != "null") speakText(speechAfter);
            }
            break;
        }
        default:
            Serial.printf("[CSQ] tipo sconosciuto: %d\n", csq.type);
            break;
    }
}

void processStimulusDefault(const String& base64Img) {
    Serial.printf("[STIMULUS-DEFAULT] SD=%s img=%u char\n", sdAvailable ? "si" : "no", base64Img.length());
    String sysPrompt, userText;
    if (sdAvailable) {
        String cacheJSON = getCacheSummaryJSON();
        sysPrompt = gPersonalityPrompt + " Ti passo un JSON con la cache audio. Se una frase in cache va bene, rispondi SOLO con la sua chiave (es. '1.pcm'). Se NESSUNA va bene, genera una nuova frase BREVISSIMA (max 8 parole) anteponendo 'NEW:'.";
        userText  = "Cache JSON: " + cacheJSON;
    } else {
        sysPrompt = gPersonalityPrompt;
        userText  = "Commenta quello che vedi.";
    }
    String answer = callLLM(base64Img, sysPrompt, userText);
    if (answer.length() == 0) { Serial.println("[STIMULUS-DEFAULT] ERRORE: LLM risposta vuota, stop"); return; }
    Serial.printf("[STIMULUS-DEFAULT] LLM: \"%s\"\n", answer.c_str());
    if (sdAvailable) {
        if (answer.startsWith("NEW:"))    { Serial.println("[STIMULUS-DEFAULT] -> TTS-SD nuovo"); generateAndPlayTTS_SD(answer.substring(4)); }
        else if (answer.endsWith(".pcm") || answer.endsWith(".mp3")) { Serial.printf("[STIMULUS-DEFAULT] -> cache SD: %s (con prefix)\n", answer.c_str()); playAudioSDWithPrefix(answer); }
        else                              { Serial.println("[STIMULUS-DEFAULT] -> TTS-SD (no prefix)"); generateAndPlayTTS_SD(answer); }
    } else {
        if (answer.startsWith("NEW:")) answer = answer.substring(4);
        Serial.println("[STIMULUS-DEFAULT] -> TTS-RAM");
        streamAndPlayTTS_RAM(answer);
    }
}

void processStimulus(TriggerType trg, uint8_t sensorId) {
    Serial.printf("[STIMULUS] trigger=%d sensorId=%d behaviors=%d heap=%u PSRAM=%u\n",
        trg, sensorId, gEventBehaviorCount, esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // STT: trascrivi il buffer audio accumulato dal VAD prima di fare qualsiasi altra cosa
    String sttText;
    if (trg == TRG_VAD && sttEnabled && gSttLen > 0) {
        Serial.printf("[STIMULUS] STT: trascrivo %d samples\n", (int)gSttLen);
        sttText = transcribeAudio();
        gSttLen = 0;
    }

    String base64Img;
    if (camActive || camInit()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            Serial.printf("[STIMULUS] frame catturato: %u byte\n", fb->len);
            base64Img = base64Encode(fb->buf, fb->len);
            esp_camera_fb_return(fb);
        } else {
            Serial.println("[STIMULUS] WARN: esp_camera_fb_get() restituito NULL");
        }
    } else {
        Serial.println("[STIMULUS] WARN: camera non disponibile, procedo senza immagine");
    }

    EventBehavior* beh = nullptr;
    for (int i = 0; i < gEventBehaviorCount; i++) {
        EventBehavior& b = gEventBehaviors[i];
        if (b.trigger != trg) continue;
        if (trg == TRG_SENSOR && b.sensor_id != sensorId) continue;
        beh = &b;
        break;
    }

    if (!beh) {
        Serial.println("[STIMULUS] nessun behavior configurato -> default");
        processStimulusDefault(base64Img);
        return;
    }
    Serial.printf("[STIMULUS] behavior trovato: \"%s\" (%d conseguenze)\n", beh->name, beh->consequence_count);
    for (int c = 0; c < beh->consequence_count; c++)
        executeConsequence(beh->consequences[c], base64Img, sttText);
}

void IRAM_ATTR isrWakeUp() { pendingTrigger = TRG_BUTTON; pendingSensorId = 0; pendingEvent = EVT_BUTTON; wakeUpTriggered = true; }

// ==========================================
// CAMERA INIT/DEINIT
// ==========================================
bool camInit() {
    if (camActive) return true;
    camera_config_t cam;
    cam.ledc_channel  = LEDC_CHANNEL_0; cam.ledc_timer = LEDC_TIMER_0;
    cam.pin_d0        = Y2_GPIO_NUM;  cam.pin_d1  = Y3_GPIO_NUM;
    cam.pin_d2        = Y4_GPIO_NUM;  cam.pin_d3  = Y5_GPIO_NUM;
    cam.pin_d4        = Y6_GPIO_NUM;  cam.pin_d5  = Y7_GPIO_NUM;
    cam.pin_d6        = Y8_GPIO_NUM;  cam.pin_d7  = Y9_GPIO_NUM;
    cam.pin_xclk      = XCLK_GPIO_NUM;
    cam.pin_pclk      = PCLK_GPIO_NUM; cam.pin_vsync = VSYNC_GPIO_NUM; cam.pin_href = HREF_GPIO_NUM;
    cam.pin_sccb_sda  = SIOD_GPIO_NUM; cam.pin_sccb_scl = SIOC_GPIO_NUM;
    cam.pin_pwdn      = PWDN_GPIO_NUM; cam.pin_reset = RESET_GPIO_NUM;
    cam.xclk_freq_hz  = 20000000;
    cam.pixel_format  = PIXFORMAT_JPEG;
    if (psramFound()) { cam.frame_size = FRAMESIZE_QVGA;  cam.jpeg_quality = 12; cam.fb_count = 2; cam.fb_location = CAMERA_FB_IN_PSRAM; }
    else              { cam.frame_size = FRAMESIZE_QQVGA; cam.jpeg_quality = 12; cam.fb_count = 1; cam.fb_location = CAMERA_FB_IN_DRAM; }
    camActive = (esp_camera_init(&cam) == ESP_OK);
    Serial.println(camActive ? "CAM: init OK" : "CAM: init FALLITA");
    return camActive;
}

void camDeinit() {
    if (!camActive) return;
    esp_camera_deinit();
    camActive = false;
    Serial.println("CAM: deinit");
}

// ==========================================
// SETUP & LOOP
// ==========================================
// mbedTLS alloca da PSRAM invece che dall'heap interno
static void* mbedtls_psram_calloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memset(p, 0, total);
    return p;
}
static void mbedtls_psram_free(void* p) { free(p); }

void setup() {
    Serial.begin(115200);
    // Redirect SSL/TLS allocations to PSRAM — chiamare prima di qualsiasi WiFiClientSecure
    mbedtls_platform_set_calloc_free(mbedtls_psram_calloc, mbedtls_psram_free);
    setCpuFrequencyMhz(240);
    Serial.println("CPU: " + String(getCpuFrequencyMhz()) + " MHz");
    Serial.println("Heap interno: " + String(ESP.getFreeHeap()) + " B");
    Serial.println("PSRAM: " + String(psramFound() ? ESP.getFreePsram() : 0) + " B liberi / " +
                               String(psramFound() ? ESP.getPsramSize()  : 0) + " B totali");

    // SPIFFS
    if (!SPIFFS.begin(true)) Serial.println("SPIFFS: mount fallito");
    else Serial.printf("SPIFFS: %u KB usati / %u KB totali\n",
        SPIFFS.usedBytes()/1024, SPIFFS.totalBytes()/1024);

    // Carica preferenze
    preferences.begin("furby", false);
    loadWifiNets();
    llm_provider        = preferences.getString("llm_prov",  "openai");
    llm_model           = preferences.getString("llm_model", "gpt-4o-mini");
    openai_api_key      = preferences.getString("openai",    "");
    if (openai_api_key.length() == 0)
        openai_api_key  = preferences.getString("openai_key", ""); // fallback vecchia chiave
    claude_api_key      = preferences.getString("claude",    "");
    if (claude_api_key.length() == 0)
        claude_api_key  = preferences.getString("claude_key", "");
    elevenlabs_api_key  = preferences.getString("11labs",    "");
    if (elevenlabs_api_key.length() == 0)
        elevenlabs_api_key = preferences.getString("11labs_key", "");
    elevenlabs_voice_id = preferences.getString("11labs_vid","pNInz6obpgDQGcFmaJcg");
    el_audio_fmt        = preferences.getString("11labs_fmt","pcm");
    ble_service_uuid    = preferences.getString("ble_svc",  "dab91435-b5a1-e29c-b041-bcd562613bde");
    ble_char_uuid_tx    = preferences.getString("ble_char", "dab91383-b5a1-e29c-b041-bcd562613bde");
    serviceUUID      = BLEUUID(ble_service_uuid.c_str());
    charUUID_GPWrite = BLEUUID(ble_char_uuid_tx.c_str());
    vad_threshold   = preferences.getInt("vad_thr",  VAD_THRESHOLD_DEFAULT);
    vadEnabled      = preferences.getBool("vad_en",  true);
    sttEnabled      = preferences.getBool("stt_en",  false);
    gCamDescPrompt  = preferences.getString("cam_desc_prompt",
        "Sei un Furby maleducato e cinico. Descrivi in modo sintetico e sgarbato quello che vedi nell'immagine.");
    loadBehaviorConfigs();
    loadEventBehaviors();

    // SD
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    sdAvailable = SD_MMC.begin("/sdcard", true);
    Serial.println(sdAvailable ? "SD: OK" : "SD: assente, modalita RAM");

    // Camera — init al boot; può essere deinit/reinit dalla pagina /camera
    camInit();

    // I2C — dopo camInit perché camera usa GPIO8/7 come SCCB; Wire usa I2C0 hardware
    // Bus stuck recovery: 9 clock pulses su SCL per sbloccare eventuali slave bloccati
    pinMode(I2C_SCL_PIN, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
    }
    pinMode(I2C_SCL_PIN, INPUT);
    delay(10);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    delay(50);

    // Scan I2C — stampa tutti i dispositivi trovati sul bus
    Serial.println("I2C scan...");
    int i2cFound = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  I2C device trovato: 0x%02X\n", addr);
            i2cFound++;
        }
    }
    if (i2cFound == 0) Serial.println("  I2C: nessun device trovato! Verifica cablaggio SDA/SCL");
    else Serial.printf("  I2C scan: %d device(s)\n", i2cFound);

    // GPIO expander — IO6=power audio, IO4=PA_EN amplificatore
    ch32Init();

    // Audio TX (speaker) + RX (microfoni)
    initI2S();
    initES8311();
    initES7210();

    // WiFi: tenta le reti in ordine di priorità (solo SSID, ignora BSSID/MAC)
    bool wifiOk = false;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    for (int i = 0; i < wifiNetCount && !wifiOk; i++) {
        Serial.println("WiFi: provo " + wifiNets[i].ssid);
        WiFi.begin(wifiNets[i].ssid.c_str(), wifiNets[i].pass.c_str());
        for (int t = 0; t < 20 && WiFi.status() != WL_CONNECTED; t++) delay(500);
        if (WiFi.status() == WL_CONNECTED) {
            wifiOk = true;
            Serial.println("WiFi connesso: " + WiFi.localIP().toString());
        } else {
            WiFi.disconnect(true);
            delay(200);
        }
    }

    if (!wifiOk) startCaptivePortal();

    // Web server sempre attivo — una sola chiamata in entrambi i casi
    startWebServer();

    if (!isConfigMode) {
        BLEDevice::init("");
        BLEDevice::getScan()->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());

        // Buffer STT in PSRAM: 8s @ 16kHz mono int16 = 256KB
        gSttBuf = (int16_t*)heap_caps_malloc(STT_BUF_MAX_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (gSttBuf) Serial.println("STT: buffer PSRAM allocato");
        else          Serial.println("STT: WARN buffer PSRAM non allocato, STT disabilitato");

        xTaskCreatePinnedToCore(lipSyncTask,   "LipSync",   2048, NULL, 1, NULL, 0);
        xTaskCreatePinnedToCore(keepAliveTask, "KeepAlive", 2048, NULL, 1, NULL, 0);
        xTaskCreatePinnedToCore(vadTask,       "VAD",       4096, NULL, 1, NULL, 0);

        pinMode(WAKE_BTN_PIN, INPUT_PULLUP);
        attachInterrupt(WAKE_BTN_PIN, isrWakeUp, FALLING);
    }
}

void loop() {
    // DNS solo in captive portal
    if (isConfigMode) dnsServer.processNextRequest();

    // Web server sempre attivo
    server.handleClient();

    if (!isConfigMode) {
        if (doConnect && !bleConnecting) {
            doConnect     = false;
            bleConnecting = true;
            // Connessione su task separato: connect() è bloccante ~2-5s,
            // non possiamo tenerla nel loop altrimenti il web server si blocca
            xTaskCreatePinnedToCore(bleConnectTask, "BLEConn", 8192, NULL, 2, NULL, 0);
        }

        if (wakeUpTriggered && !isProcessing) {
            wakeUpTriggered = false;
            isProcessing    = true;
            // Impacchetta trigger+sensorId in un uintptr_t: byte alto = trigger, byte basso = sensorId
            uintptr_t arg = ((uintptr_t)pendingTrigger << 8) | pendingSensorId;
            xTaskCreatePinnedToCore([](void* p) {
                uintptr_t v = (uintptr_t)p;
                TriggerType trg = (TriggerType)((v >> 8) & 0xFF);
                uint8_t     sid = (uint8_t)(v & 0xFF);
                processStimulus(trg, sid);
                isProcessing = false;
                vTaskDelete(NULL);
            }, "Stimulus", 16384, (void*)arg, 1, NULL, 1);
        }
    }
}
