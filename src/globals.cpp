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
SemaphoreHandle_t        bleMutex                 = nullptr;
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
    // ── Coccole (input 1) ────────────────────────────────────────────────────
    {"pet_happy",    "Coccola felice",            {0x13,0x00, 1,0,0,0}, 6},  // "ah-may moh-moh"
    {"pet_reluctant","Coccola riluttante",         {0x13,0x00, 1,1,0,0}, 6},  // "kah woo-bye kah-tay"
    // ── Risate / solletico (input 2) ─────────────────────────────────────────
    {"tickle_laugh", "Risata (solletico)",          {0x13,0x00, 2,0,0,0}, 6},  // risata frenetica
    {"belly_laugh",  "Risata di pancia",            {0x13,0x00, 2,3,0,0}, 6},  // gefaw
    {"laugh_snort",  "Risata con sbuffo",           {0x13,0x00, 2,3,0,6}, 6},  // laugh snort "ow"
    // ── Peti / ruttini (input 7) ─────────────────────────────────────────────
    {"fart_musical", "Peto musicale",               {0x13,0x00, 7,0,0,0}, 6},  // fanfare musicale
    {"fart_wet",     "Peto umido",                  {0x13,0x00, 7,0,0,2}, 6},  // echoey wet "MEATBALLS!"
    {"fart_silent",  "Silent but deadly",           {0x13,0x00, 7,3,0,3}, 6},  // "wait for it... silent but deadly"
    {"burp",         "Rutto",                       {0x13,0x00, 7,3,0,0}, 6},  // "hehe wait wait" burp "mmm tasty"
    // ── Singhiozzo / rutto sonoro (input 16) ────────────────────────────────
    {"hiccup",       "Singhiozzo",                  {0x13,0x00,16,0,0,0}, 6},  // hiccup
    {"burp_loud",    "Rutto + risata",              {0x13,0x00,16,0,2,0}, 6},  // burp giggle (16|0|2|0)
    // ── Canto / ballo (input 17) ─────────────────────────────────────────────
    {"sing",         "Cantare",                     {0x13,0x00,17,0,0,0}, 6},  // "boo boo believe in"
    {"beatbox",      "Beatbox",                     {0x13,0x00,17,0,0,5}, 6},  // beatboxing
    {"dance",        "Ballare",                     {0x13,0x00,17,2,0,3}, 6},  // "oo-nye dance with kah?"
    // ── Movimento / vomito (input 9) ─────────────────────────────────────────
    {"shake",        "Tremare/agitato",              {0x13,0x00, 9,0,0,0}, 6},  // "wugh"
    {"vomit",        "Vomitare",                    {0x13,0x00, 9,1,0,1}, 6},  // "kah regret nothing" + puke
    // ── Sonno (input 12 = sonnolento, 28 = addormentarsi) ───────────────────
    {"sleepy",       "Assonnato",                   {0x13,0x00,12,0,0,0}, 6},  // purr "kah likey" (sonnolento)
    {"sleep",        "Addormentarsi",               {0x13,0x00,28,0,0,0}, 6},  // "kah be back" (vero sleep)
    {"lullaby",      "Ninna nanna",                 {0x13,0x00,12,2,1,1}, 6},  // "rock a by furby"
    // ── Svegliarsi (input 27) ─────────────────────────────────────────────────
    {"wakeup",       "Svegliarsi",                  {0x13,0x00,27,0,0,0}, 6},  // "oh, kah dream about oo-nye"
    {"wakeup_reluctant","Sveglia a fatica",          {0x13,0x00,27,3,0,0}, 6},  // "kah don't wanna go to sleep"
    // ── Reazioni fisiologiche ─────────────────────────────────────────────────
    {"hungry",       "Fame",                        {0x13,0x00,23,0,0,0}, 6},  // "ugh, so hungry"
    {"sick",         "Malato",                      {0x13,0x00,22,0,0,0}, 6},  // coughing vomit
    {"dropped",      "Caduto",                      {0x13,0x00,21,0,0,0}, 6},  // waugh
    {"loud_noise",   "Rumore forte",                {0x13,0x00,20,0,0,0}, 6},  // "huh! What was that?"
    {"eating",       "Mangiare",                    {0x13,0x00,14,0,0,0}, 6},  // eating noises
    // ── Conversazione (input 8) ──────────────────────────────────────────────
    {"convo_yes",    "Conversazione: sì!",          {0x13,0x00, 8,0,0,0}, 6},  // "yaaaas"
    {"convo_no",     "Conversazione: no",           {0x13,0x00, 8,1,0,0}, 6},  // "pft, doo know what?"
    {"convo_bored",  "Conversazione: annoiato",     {0x13,0x00, 8,2,0,0}, 6},  // "if oo-nye say so"
    {"convo_gossip", "Conversazione: pettegolezzo", {0x13,0x00, 8,3,0,15}, 6}, // whisper "did kah tell you"
    // ── Antenna LED (0x14) ───────────────────────────────────────────────────
    {"ant_red",      "Antenna rossa",               {0x14,255,  0,  0,0,0}, 4},
    {"ant_blue",     "Antenna blu",                 {0x14,  0,  0,255,0,0}, 4},
    {"ant_green",    "Antenna verde",               {0x14,  0,255,  0,0,0}, 4},
    {"ant_off",      "Antenna spenta",              {0x14,  0,  0,  0,0,0}, 4},
};
const int FURBY_ACTIONS_COUNT = (int)(sizeof(FURBY_ACTIONS)/sizeof(FURBY_ACTIONS[0]));

String        gPersonalityPrompt;
String        gPersonalityVoiceId;
String        gCamDescPrompt;

int          camStreamQuality = 12;
framesize_t  camStreamSize    = FRAMESIZE_QVGA;
int          camSnapQuality   = 8;
framesize_t  camSnapSize      = FRAMESIZE_VGA;
int          camFlicker       = 0;
int          camGainCeiling   = 0; // 0=2X default conservativo
int          camBrightness    = 0; // -2..+2
int          camAgc           = 1; // 1=auto AGC con ceiling limitato
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
