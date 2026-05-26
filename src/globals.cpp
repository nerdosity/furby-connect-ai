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

const char* SENSOR_NAMES_EN[] = {
    "-", "Left antenna", "Right antenna", "Forward antenna", "Back antenna",
    "Head tickle", "Tummy tickle", "Right tickle", "Left tickle",
    "Pull tail", "Push tongue",
    "Upright", "Upside down", "Right side", "Left side",
    "Lean back", "Tilt right", "Tilt left"
};

// Fonte: bluefluff/doc/actionlist.md (Jeija) + pdjstone/furby-web-bluetooth
const FurbyActionDef FURBY_ACTIONS[] = {
    // ── Coccole (input 1) ────────────────────────────────────────────────────
    {"pet_happy",        "Coccola: felice",            "Pet: happy",              {0x13,0x00, 1,0,0,0}, 6},
    {"pet_happy2",       "Coccola: sì sì",             "Pet: yes yes",            {0x13,0x00, 1,0,0,1}, 6},
    {"pet_happy3",       "Coccola: wow feel good",     "Pet: wow feel good",      {0x13,0x00, 1,0,0,3}, 6},
    {"pet_reluctant",    "Coccola: riluttante",        "Pet: reluctant",          {0x13,0x00, 1,1,0,0}, 6},
    {"pet_reluctant2",   "Coccola: meh really?",      "Pet: meh really?",        {0x13,0x00, 1,1,0,1}, 6},
    {"pet_sleepy",       "Coccola: yawn besties",      "Pet: yawn besties",       {0x13,0x00, 1,2,0,0}, 6},
    {"pet_purr",         "Coccola: purr amore",        "Pet: purr love",          {0x13,0x00, 1,3,0,3}, 6},
    // ── Risate / solletico (input 2) ─────────────────────────────────────────
    {"tickle_laugh",     "Risata: frenetica",          "Laugh: frantic",          {0x13,0x00, 2,0,0,0}, 6},
    {"tickle_fart",      "Risata: + peto",             "Laugh: + fart",           {0x13,0x00, 2,0,0,3}, 6},
    {"tickle_tinkle",    "Risata: kah tinkled",        "Laugh: kah tinkled",      {0x13,0x00, 2,0,1,3}, 6},
    {"belly_laugh",      "Risata: di pancia (gefaw)",  "Belly laugh (gefaw)",     {0x13,0x00, 2,3,0,0}, 6},
    {"laugh_snort",      "Risata: con sbuffo ow",      "Laugh: snort ow",         {0x13,0x00, 2,3,0,6}, 6},
    {"laugh_burp",       "Risata: + rutto",            "Laugh: + burp",           {0x13,0x00, 2,3,0,2}, 6},
    // ── Peti gruppo 1 (input 7) ──────────────────────────────────────────────
    {"fart_musical",     "Peto: musicale fanfare",     "Fart: musical fanfare",   {0x13,0x00, 7,0,0,0}, 6},
    {"fart_wet_meatball","Peto: umido MEATBALLS",      "Fart: wet MEATBALLS",     {0x13,0x00, 7,0,0,2}, 6},
    {"fart_wet",         "Peto: umido",                "Fart: wet",               {0x13,0x00, 7,0,0,3}, 6},
    {"fart_echoey",      "Peto: echoey good pants",    "Fart: echoey good pants", {0x13,0x00, 7,1,0,0}, 6},
    {"fart_silent",      "Peto: silent but deadly",    "Fart: silent but deadly", {0x13,0x00, 7,3,0,3}, 6},
    {"burp",             "Rutto: mmm tasty",           "Burp: mmm tasty",         {0x13,0x00, 7,3,0,0}, 6},
    {"burp_directors",   "Rutto: director's cut",      "Burp: director's cut",    {0x13,0x00, 7,3,0,2}, 6},
    // ── Peti gruppo 2 (input 19) ─────────────────────────────────────────────
    {"fart_trumpet1",    "Peto: tromba 1",             "Fart: trumpet 1",         {0x13,0x00,19,0,0,0}, 6},
    {"fart_trumpet2",    "Peto: tromba 2",             "Fart: trumpet 2",         {0x13,0x00,19,0,0,1}, 6},
    {"fart_motorboat1",  "Peto: barca 1",              "Fart: motorboat 1",       {0x13,0x00,19,0,0,3}, 6},
    {"fart_dry1",        "Peto: secco 1",              "Fart: dry 1",             {0x13,0x00,19,0,0,9}, 6},
    // ── Conversazione (input 8) ──────────────────────────────────────────────
    {"convo_yes",        "Conv: yaaaas",               "Convo: yaaaas",           {0x13,0x00, 8,0,0,0}, 6},
    {"convo_no_way",     "Conv: no way!",              "Convo: no way!",          {0x13,0x00, 8,0,0,1}, 6},
    {"convo_best_news",  "Conv: best news ever",       "Convo: best news ever",   {0x13,0x00, 8,0,0,3}, 6},
    {"convo_no",         "Conv: da no likey",          "Convo: da no likey",      {0x13,0x00, 8,1,0,3}, 6},
    {"convo_awkward",    "Conv: awkward",              "Convo: awkward",          {0x13,0x00, 8,1,0,4}, 6},
    {"convo_bored",      "Conv: annoiato (if u say)",  "Convo: bored (if u say)", {0x13,0x00, 8,2,0,0}, 6},
    {"convo_wonder",     "Conv: kah wonder",           "Convo: kah wonder",       {0x13,0x00, 8,2,1,0}, 6},
    {"convo_go_on",      "Conv: go on",                "Convo: go on",            {0x13,0x00, 8,3,0,0}, 6},
    {"convo_hmmm",       "Conv: hmmm",                 "Convo: hmmm",             {0x13,0x00, 8,3,0,6}, 6},
    {"convo_gossip",     "Conv: pettegolezzo",         "Convo: gossip",           {0x13,0x00, 8,3,0,15}, 6},
    // ── Movimento / agitato (input 9) ────────────────────────────────────────
    {"shake",            "Agitato: wugh",              "Shaken: wugh",            {0x13,0x00, 9,0,0,0}, 6},
    {"shake_safe",       "Agitato: this safe?",        "Shaken: this safe?",      {0x13,0x00, 9,0,0,1}, 6},
    {"shake_belly",      "Agitato: belly is jelly",    "Shaken: belly is jelly",  {0x13,0x00, 9,0,0,3}, 6},
    {"vomit",            "Vomitare: regret nothing",   "Vomit: regret nothing",   {0x13,0x00, 9,1,0,1}, 6},
    {"vomit2",           "Vomitare: clean up aisle",   "Vomit: clean up aisle",   {0x13,0x00, 9,1,0,2}, 6},
    {"shake_what_furb",  "Agitato: what in the furb",  "Shaken: what in the furb",{0x13,0x00, 9,3,0,1}, 6},
    {"shake_jiggly",     "Agitato: jiggly wiggly",     "Shaken: jiggly wiggly",   {0x13,0x00, 9,3,0,7}, 6},
    // ── Capovolto (input 10) ─────────────────────────────────────────────────
    {"upsidedown",       "Capovolto: up is down",      "Upside down: up is down", {0x13,0x00,10,0,0,0}, 6},
    {"upsidedown2",      "Capovolto: yodeling",        "Upside down: yodeling",   {0x13,0x00,10,0,1,0}, 6},
    {"upsidedown_ok",    "Capovolto: geronimo",        "Upside down: geronimo",   {0x13,0x00,10,3,0,0}, 6},
    // ── Rimesso su (input 11) ────────────────────────────────────────────────
    {"rightsideup",      "Rimesso su: phew awesome",   "Right side up: phew awesome",{0x13,0x00,11,0,0,0}, 6},
    {"rightsideup2",     "Rimesso su: love ground",    "Right side up: love ground", {0x13,0x00,11,0,0,2}, 6},
    // ── Singhiozzo / rutto (input 16) ────────────────────────────────────────
    {"hiccup",           "Singhiozzo",                 "Hiccup",                  {0x13,0x00,16,0,0,0}, 6},
    {"throat_noise",     "Gola rumorosa",              "Throat noise",            {0x13,0x00,16,0,1,0}, 6},
    {"burp_giggle",      "Rutto + risata",             "Burp + giggle",           {0x13,0x00,16,0,2,0}, 6},
    {"burp_loud",        "Rutto forte nasty",          "Loud burp nasty",         {0x13,0x00,16,0,2,2}, 6},
    // ── Canto / ballo (input 17) ─────────────────────────────────────────────
    {"sing",             "Canta: boo boo believe",     "Sing: boo boo believe",   {0x13,0x00,17,0,0,0}, 6},
    {"sing_fever",       "Canta: no lah fever",        "Sing: no lah fever",      {0x13,0x00,17,0,0,1}, 6},
    {"beatbox",          "Beatbox",                    "Beatbox",                 {0x13,0x00,17,0,0,5}, 6},
    {"dance_reluctant",  "Ballo: stubby legs",         "Dance: stubby legs",      {0x13,0x00,17,1,0,2}, 6},
    {"dance",            "Ballo: dance with kah?",     "Dance: dance with kah?",  {0x13,0x00,17,2,0,3}, 6},
    {"dance_hustle",     "Ballo: do the furb",         "Dance: do the furb",      {0x13,0x00,17,2,0,4}, 6},
    {"dance_wanna",      "Ballo: wanna dance?",        "Dance: wanna dance?",     {0x13,0x00,17,3,0,1}, 6},
    // ── Musica ascoltata (input 18) ──────────────────────────────────────────
    {"hear_music",       "Sente musica: workout",      "Hears music: workout",    {0x13,0x00,18,0,1,0}, 6},
    {"hear_music2",      "Sente musica: beatbox",      "Hears music: beatbox",    {0x13,0x00,18,0,1,5}, 6},
    // ── Sonno assonnato (input 12) ───────────────────────────────────────────
    {"sleepy",           "Assonnato: purr",            "Sleepy: purr",            {0x13,0x00,12,0,0,0}, 6},
    {"sleepy_hold",      "Assonnato: hold me",         "Sleepy: hold me",         {0x13,0x00,12,1,0,5}, 6},
    {"sleepy_stretch",   "Assonnato: stretching",      "Sleepy: stretching",      {0x13,0x00,12,2,0,0}, 6},
    {"sleepy_love",      "Assonnato: love sleep",      "Sleepy: love sleep",      {0x13,0x00,12,2,0,2}, 6},
    {"lullaby",          "Ninna nanna (sung)",         "Lullaby (sung)",          {0x13,0x00,12,2,1,1}, 6},
    {"lullaby2",         "Ninna nanna: snug as bug",   "Lullaby: snug as bug",    {0x13,0x00,12,2,1,3}, 6},
    {"sleepy_purr",      "Sonno: purr noo loo",        "Sleep: purr noo loo",     {0x13,0x00,12,3,0,0}, 6},
    // ── Riassonnato (input 13) ───────────────────────────────────────────────
    {"drowsy",           "Riassonnato: yawn ok hi",    "Drowsy: yawn ok hi",      {0x13,0x00,13,0,0,0}, 6},
    // ── Addormentarsi (input 28) ─────────────────────────────────────────────
    {"sleep",            "Addormentarsi: kah be back", "Falling asleep: kah be back",{0x13,0x00,28,0,0,0}, 6},
    {"sleep_goodnight",  "Addormentarsi: good night",  "Falling asleep: good night", {0x13,0x00,28,0,0,1}, 6},
    {"sleep_powernap",   "Addormentarsi: power nap",   "Falling asleep: power nap",  {0x13,0x00,28,0,0,2}, 6},
    // ── Svegliarsi (input 27) ─────────────────────────────────────────────────
    {"wakeup",           "Svegliarsi: sogno oo-nye",   "Wake up: dream oo-nye",   {0x13,0x00,27,0,0,0}, 6},
    {"wakeup_hi",        "Svegliarsi: hi miss kah?",   "Wake up: hi miss kah?",   {0x13,0x00,27,0,0,1}, 6},
    {"wakeup_morning",   "Svegliarsi: buenos días",    "Wake up: buenos días",    {0x13,0x00,27,2,0,0}, 6},
    {"wakeup_early",     "Svegliarsi: too early",      "Wake up: too early",      {0x13,0x00,27,3,1,0}, 6},
    {"wakeup_reluctant", "Svegliarsi: non voglio",     "Wake up: don't wanna",    {0x13,0x00,27,3,0,0}, 6},
    // ── Mangiare (input 14 / 15 / 26) ────────────────────────────────────────
    {"eating",           "Mangiare: crunch",           "Eating: crunch",          {0x13,0x00,14,0,0,0}, 6},
    {"eating_big",       "Mangiare: crunch enorme",    "Eating: big crunch",      {0x13,0x00,14,0,0,1}, 6},
    {"eating_vomit",     "Mangiare: vomita dopo",      "Eating: vomits after",    {0x13,0x00,14,0,1,0}, 6},
    {"feed_burp",        "Pasto: rutto + risata",      "Meal: burp + laugh",      {0x13,0x00,15,0,0,0}, 6},
    {"feed_yummy",       "Pasto: mmm yummy",           "Meal: mmm yummy",         {0x13,0x00,15,0,1,0}, 6},
    {"feed_more",        "Pasto: voglio di più",       "Meal: want more",         {0x13,0x00,15,0,2,0}, 6},
    {"eat_react",        "Pasto: spicey",              "Meal: spicey",            {0x13,0x00,26,0,0,0}, 6},
    // ── Reazioni fisiche ─────────────────────────────────────────────────────
    {"hungry",           "Fame: ugh so hungry",        "Hungry: ugh so hungry",   {0x13,0x00,23,0,0,0}, 6},
    {"hungry2",          "Fame: starving",             "Hungry: starving",        {0x13,0x00,23,0,0,1}, 6},
    {"hungry_lunch",     "Fame: lunch time",           "Hungry: lunch time",      {0x13,0x00,23,2,0,0}, 6},
    {"hungry_dinner",    "Fame: dinner time",          "Hungry: dinner time",     {0x13,0x00,23,3,0,0}, 6},
    {"bored",            "Noia: kah wanna play",       "Bored: kah wanna play",   {0x13,0x00,24,0,0,0}, 6},
    {"bored_song",       "Noia: canta da solo",        "Bored: sings alone",      {0x13,0x00,24,2,0,0}, 6},
    {"bored_fart",       "Noia: deal with it",         "Bored: deal with it",     {0x13,0x00,24,3,0,2}, 6},
    {"sick",             "Malato: coughing vomit",     "Sick: coughing vomit",    {0x13,0x00,22,0,0,0}, 6},
    {"sick2",            "Malato: sneeze nurse",       "Sick: sneeze nurse",      {0x13,0x00,22,0,0,2}, 6},
    {"sick3",            "Malato: congested",          "Sick: congested",         {0x13,0x00,22,0,1,2}, 6},
    {"dropped",          "Caduto: waugh",              "Dropped: waugh",          {0x13,0x00,21,0,0,0}, 6},
    {"dropped2",         "Caduto: crying",             "Dropped: crying",         {0x13,0x00,21,0,0,1}, 6},
    {"dropped3",         "Caduto: can't get up",       "Dropped: can't get up",   {0x13,0x00,21,0,0,8}, 6},
    {"loud_noise",       "Rumore: what was that?",     "Loud noise: what was that?",{0x13,0x00,20,0,0,0}, 6},
    {"loud_noise2",      "Rumore: kah hearing things", "Loud noise: hearing things",{0x13,0x00,20,0,0,2}, 6},
    // ── World App (input 29-36) ───────────────────────────────────────────────
    {"back_from",        "World: ritorno wooh hi!",    "World: back wooh hi!",    {0x13,0x00,29,0,0,0}, 6},
    {"back_from2",       "World: ritorno furbtastic",  "World: back furbtastic",  {0x13,0x00,29,0,0,2}, 6},
    {"friend_meet",      "World: dragonslayer name",   "World: dragonslayer name",{0x13,0x00,30,0,0,0}, 6},
    {"friend_again",     "World: hey old pal",         "World: hey old pal",      {0x13,0x00,30,0,1,0}, 6},
    {"friend_chat",      "World: so oo-nye into...",   "World: so oo-nye into...",{0x13,0x00,30,1,0,0}, 6},
    {"react_love",       "React: love it!",            "React: love it!",         {0x13,0x00,32,0,0,0}, 6},
    {"react_notsure",    "React: not so sure",         "React: not so sure",      {0x13,0x00,32,0,1,0}, 6},
    {"react_cozy",       "React: feel cozy",           "React: feel cozy",        {0x13,0x00,32,0,2,0}, 6},
    {"react_sick",       "React: need doctor",         "React: need doctor",      {0x13,0x00,32,0,4,0}, 6},
    {"connected",        "World: connected!",          "World: connected!",       {0x13,0x00,34,0,0,0}, 6},
    {"connected2",       "World: success!",            "World: success!",         {0x13,0x00,34,0,0,2}, 6},
    // ── Note musicali (input 71) ─────────────────────────────────────────────
    {"note_do",          "Nota: Do",                   "Note: C",                 {0x13,0x00,71,0,0,0}, 6},
    {"note_re",          "Nota: Re",                   "Note: D",                 {0x13,0x00,71,0,0,1}, 6},
    {"note_mi",          "Nota: Mi",                   "Note: E",                 {0x13,0x00,71,0,0,2}, 6},
    {"note_fa",          "Nota: Fa",                   "Note: F",                 {0x13,0x00,71,0,0,3}, 6},
    {"note_sol",         "Nota: Sol",                  "Note: G",                 {0x13,0x00,71,0,0,4}, 6},
    {"note_la",          "Nota: La",                   "Note: A",                 {0x13,0x00,71,0,0,5}, 6},
    {"note_ti",          "Nota: Ti",                   "Note: B",                 {0x13,0x00,71,0,0,6}, 6},
    {"note_do2",         "Nota: Do (alta)",            "Note: C (high)",          {0x13,0x00,71,0,0,7}, 6},
    // ── Sogni personalità (input 74) ─────────────────────────────────────────
    {"dream_dj",         "Sogno: sarò un DJ",          "Dream: I'll be a DJ",     {0x13,0x00,74,0,0,0}, 6},
    // ── Antenna LED (0x14) ───────────────────────────────────────────────────
    {"ant_red",          "Antenna: rossa",             "Antenna: red",            {0x14,255,  0,  0,0,0}, 4},
    {"ant_blue",         "Antenna: blu",               "Antenna: blue",           {0x14,  0,  0,255,0,0}, 4},
    {"ant_green",        "Antenna: verde",             "Antenna: green",          {0x14,  0,255,  0,0,0}, 4},
    {"ant_yellow",       "Antenna: gialla",            "Antenna: yellow",         {0x14,255,255,  0,0,0}, 4},
    {"ant_purple",       "Antenna: viola",             "Antenna: purple",         {0x14,128,  0,128,0,0}, 4},
    {"ant_white",        "Antenna: bianca",            "Antenna: white",          {0x14,255,255,255,0,0}, 4},
    {"ant_off",          "Antenna: spenta",            "Antenna: off",            {0x14,  0,  0,  0,0,0}, 4},
};
const int FURBY_ACTIONS_COUNT = (int)(sizeof(FURBY_ACTIONS)/sizeof(FURBY_ACTIONS[0]));

String        gPersonalityPrompt;
String        gPersonalityVoiceId;
String        gPersonalityLang = "it";
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
bool gSimSkipTts = false;
bool gSimSkipBle = false;
volatile int  currentAmplitude = 0;

int  vad_threshold = VAD_THRESHOLD_DEFAULT;
int  mic_gain      = 10; // 30dB default (ES7210: valore 0-14, step 3dB)
bool vadEnabled    = true;
volatile int  micRmsLive   = 0;
volatile int  micRmsLive2  = 0;
volatile bool micVadActive  = false;
volatile bool micTestActive = false;

int16_t* gSttBuf     = nullptr;
volatile int gSttLen = 0;
bool sttEnabled      = false;

uint8_t ch32PortState = 0x00;
