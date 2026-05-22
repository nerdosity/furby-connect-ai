#include "globals.h"

Preferences  preferences;
WebServer    server(80);
DNSServer    dnsServer;
bool         isConfigMode = false;
bool         sdAvailable  = false;
bool         camActive    = false;

WifiNet      wifiNets[MAX_WIFI_NETS];
int          wifiNetCount = 0;

String llm_provider        = "openai";
String llm_model           = "gpt-4o-mini";
String openai_api_key      = "";
String claude_api_key      = "";
String elevenlabs_api_key  = "";
String elevenlabs_voice_id = "pNInz6obpgDQGcFmaJcg";
String el_audio_fmt        = "pcm";

String ble_service_uuid = BLE_SVC_DEFAULT;
String ble_char_uuid_tx = BLE_CHAR_DEFAULT;
BLEUUID serviceUUID(BLE_SVC_DEFAULT);
BLEUUID charUUID_GPWrite(BLE_CHAR_DEFAULT);
BLEUUID charUUID_GPListen("dab91382-b5a1-e29c-b041-bcd562613bde");
BLEUUID charUUID_NWrite("dab90757-b5a1-e29c-b041-bcd562613bde");
BLEUUID charUUID_NListen("dab90756-b5a1-e29c-b041-bcd562613bde");
volatile boolean doConnect     = false;
String   pendingConnAddr       = "";
String   pendingConnName       = "";
esp_ble_addr_type_t pendingConnAddrType = BLE_ADDR_TYPE_RANDOM;
volatile bool bleConnecting    = false;
volatile bool bleUserDisconnect = false;
boolean  connected             = false;
boolean  bleScanning           = false;
BLEClient*               pBleClient               = nullptr;
BLERemoteCharacteristic* pRemoteCharacteristicTX  = nullptr;
BLERemoteCharacteristic* pCharGPListen            = nullptr;
BLERemoteCharacteristic* pCharNWrite              = nullptr;
BLERemoteCharacteristic* pCharNListen             = nullptr;
BLEAdvertisedDevice*     myDevice                 = nullptr;
String   ble_last_name  = "";
String   ble_last_addr  = "";
int      ble_battery_pct = -1;

FurbyDevice furbyList[MAX_FURBY_SCAN];
int         furbyListCount = 0;

FurbySensors furbyState;
FurbySensors prevFurbyState;
unsigned long lastSensorMs   = 0;
unsigned long bleConnectedMs = 0;

BehaviorConfig behaviorConfigs[MAX_CONFIGS];
int   behaviorConfigCount  = 0;
int   activeBehaviorConfig = 0;

const char* SENSOR_NAMES[] = {
    "-", "Antenna sx", "Antenna dx", "Antenna avanti", "Antenna indietro",
    "Tickle testa", "Tickle pancia", "Tickle dx", "Tickle sx",
    "Tira coda", "Spingi lingua",
    "Dritto", "Capovolto", "Lato dx", "Lato sx",
    "Inclinato back", "Inclinato dx", "Inclinato sx"
};

const FurbyActionDef FURBY_ACTIONS[] = {
    {"pet_happy",    "Coccola felice",          {0x13,0x00, 1,0,0,0}, 6},
    {"pet_reluctant","Coccola riluttante",       {0x13,0x00, 1,1,0,0}, 6},
    {"tickle_laugh", "Risata (solletico)",        {0x13,0x00, 2,0,0,0}, 6},
    {"belly_laugh",  "Risata di pancia",          {0x13,0x00, 2,3,0,0}, 6},
    {"laugh_snort",  "Risata con sbuffo",         {0x13,0x00, 2,3,0,6}, 6},
    {"fart_musical", "Peto musicale",             {0x13,0x00, 7,0,0,0}, 6},
    {"fart_wet",     "Peto umido",                {0x13,0x00, 7,0,0,2}, 6},
    {"fart_silent",  "Silent but deadly",         {0x13,0x00, 7,3,0,3}, 6},
    {"burp",         "Rutto",                     {0x13,0x00, 7,3,0,0}, 6},
    {"burp_loud",    "Rutto forte",               {0x13,0x00,16,0,2,2}, 6},
    {"hiccup",       "Singhiozzo",                {0x13,0x00,16,0,0,0}, 6},
    {"sing",         "Cantare",                   {0x13,0x00,17,0,0,0}, 6},
    {"beatbox",      "Beatbox",                   {0x13,0x00,17,0,0,5}, 6},
    {"dance",        "Ballare",                   {0x13,0x00,17,2,0,3}, 6},
    {"shake",        "Tremare/agitato",            {0x13,0x00, 9,0,0,0}, 6},
    {"vomit",        "Vomitare",                  {0x13,0x00, 9,1,0,1}, 6},
    {"sleep",        "Addormentarsi",             {0x13,0x00,12,0,0,0}, 6},
    {"snore",        "Russare",                   {0x13,0x00,12,3,0,0}, 6},
    {"lullaby",      "Ninna nanna",               {0x13,0x00,12,2,1,1}, 6},
    {"wakeup",       "Svegliarsi",                {0x13,0x00,13,0,0,0}, 6},
    {"hungry",       "Fame",                      {0x13,0x00,23,0,0,0}, 6},
    {"sick",         "Malato",                    {0x13,0x00,22,0,0,0}, 6},
    {"dropped",      "Caduto",                    {0x13,0x00,21,0,0,0}, 6},
    {"loud_noise",   "Rumore forte",              {0x13,0x00,20,0,0,0}, 6},
    {"convo_yes",    "Conversazione: si!",        {0x13,0x00, 8,0,0,0}, 6},
    {"convo_no",     "Conversazione: no",         {0x13,0x00, 8,1,0,0}, 6},
    {"convo_bored",  "Conversazione: annoiato",   {0x13,0x00, 8,2,0,0}, 6},
    {"eating",       "Mangiare",                  {0x13,0x00,14,0,0,0}, 6},
    {"ant_red",      "Antenna rossa",             {0x14,255,  0,  0,0,0}, 4},
    {"ant_blue",     "Antenna blu",               {0x14,  0,  0,255,0,0}, 4},
    {"ant_green",    "Antenna verde",             {0x14,  0,255,  0,0,0}, 4},
    {"ant_off",      "Antenna spenta",            {0x14,  0,  0,  0,0,0}, 4},
};
const int FURBY_ACTIONS_COUNT = (int)(sizeof(FURBY_ACTIONS)/sizeof(FURBY_ACTIONS[0]));

String        gPersonalityPrompt;
String        gPersonalityVoiceId;
String        gCamDescPrompt;

int          camStreamQuality = 12;
framesize_t  camStreamSize    = FRAMESIZE_QVGA;
int          camSnapQuality   = 8;
framesize_t  camSnapSize      = FRAMESIZE_VGA;
EventBehavior* gEventBehaviors     = nullptr;  // allocato in PSRAM da setup()
int            gEventBehaviorCount = 0;

Personality*  gpActivePers       = nullptr;
int           gActivePersonality = 0;
int           gDebugPersonality  = -1;

volatile TriggerType pendingTrigger   = TRG_VAD;
volatile uint8_t     pendingSensorId  = 0;
volatile EventType   pendingEvent     = EVT_VAD;
volatile bool isSpeaking      = false;
volatile bool wakeUpTriggered = false;
volatile bool isProcessing    = false;
volatile bool gDryRun         = false;
volatile int  currentAmplitude = 0;

int  vad_threshold = VAD_THRESHOLD_DEFAULT;
bool vadEnabled    = true;
volatile int  micRmsLive   = 0;
volatile int  micRmsLive2  = 0;
volatile bool micVadActive  = false;
volatile bool micTestActive = false;

int16_t* gSttBuf     = nullptr;
volatile int gSttLen = 0;
bool sttEnabled      = false;

uint8_t ch32PortState = 0x00;
