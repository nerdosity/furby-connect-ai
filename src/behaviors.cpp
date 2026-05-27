#include "behaviors.h"
#include "audio.h"
#include "llm.h"
#include "ble_furby.h"
#include "hw.h"

// ── Logging strutturato per simulazione ───────────────────────────────────────
// In modalità simulazione (gSimLog!=nullptr) produce blocchi per-azione leggibili.
// In modalità normale (gSimLog==nullptr) va solo su Serial.
// timestamp relativo all'inizio simulazione (azzerato in processStimulusSimulated)
static uint32_t _simT0 = 0;
static String _simTs() {
    if (!_simT0) return String("[+----ms] ");
    uint32_t d = millis() - _simT0;
    char buf[16];
    snprintf(buf, sizeof(buf), "[+%5lums] ", (unsigned long)d);
    return String(buf);
}
#define SIMLOG(s)   do { String _m = _simTs() + (s); Serial.println(_m);                            if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
#define SIMHEAD(s)  do { String _m = _simTs() + (s); Serial.println(_m);                            if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
#define SIMACT(s)   do { String _m = String("\n") + _simTs() + "=== " + (s) + " ===";               Serial.println(_m); if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
#define SIMSTEP(s)  do { String _m = _simTs() + "  " + (s);                                         Serial.println(_m); if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
#define SIMSUB(s)   do { String _m = _simTs() + "    " + (s);                                       Serial.println(_m); if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
#define SIMSKIP(s)  do { String _m = _simTs() + "  SKIP: " + (s);                                   Serial.println(_m); if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)
static String _csqTypeName(uint8_t t) {
    switch (t) {
        case CSQ_FURBY_ACTION: return "Azione Furby";
        case CSQ_TTS_FIXED:    return "TTS fisso";
        case CSQ_PROMPT_FIXED: return "Prompt fisso";
        case CSQ_PROMPT_LLM:   return "LLM libero";
        case CSQ_PROMPT_AUTO:  return "LLM sceglie azione";
        case CSQ_CANNED:       return "Frase pronta";
        default:               return String("? (") + t + ")";
    }
}
static String _short(const String& s, int max=200) {
    if ((int)s.length() <= max) return s;
    return s.substring(0, max) + "… (+" + String((int)s.length() - max) + " char)";
}

// ── Helpers JSON per serializzare/deserializzare EventBehavior ────────────────
void serializeBehavior(JsonObject& bo, const EventBehavior& b) {
    bo["id"]        = b.id;
    bo["trigger"]   = (int)b.trigger;
    bo["sensor_id"] = (int)b.sensor_id;
    bo["name"]      = b.name;
    JsonArray cs = bo["consequences"].to<JsonArray>();
    for (int c = 0; c < b.consequence_count; c++) {
        const Consequence& q = b.consequences[c];
        JsonObject co = cs.add<JsonObject>();
        co["type"]         = (int)q.type;
        co["action_id"]    = q.action_id;
        co["text"]         = q.text;
        co["snapshot"]     = q.snapshot;
        co["ctx_beh_name"] = q.ctx_beh_name;
        co["ctx_sensor"]   = q.ctx_sensor;
        co["canned_id"]    = q.canned_id;
        co["canned_random"]= q.canned_random;
        JsonArray ra = co["reactions"].to<JsonArray>();
        for (int r = 0; r < q.reaction_count; r++) ra.add(q.reactions[r]);
    }
}

static void deserializeBehavior(EventBehavior& b, JsonObject bo) {
    strlcpy(b.id,   bo["id"]   | "", sizeof(b.id));
    b.trigger   = (TriggerType)(bo["trigger"]   | (int)(bo["event"] | 0));
    b.sensor_id = (uint8_t)(bo["sensor_id"] | 0);
    strlcpy(b.name, bo["name"] | "", sizeof(b.name));
    b.consequence_count = 0;
    for (JsonObject co : bo["consequences"].as<JsonArray>()) {
        if (b.consequence_count >= MAX_CONSEQUENCES) break;
        Consequence& q = b.consequences[b.consequence_count++];
        q.type     = (ConsequenceType)(co["type"] | 0);
        strlcpy(q.action_id, co["action_id"] | "", sizeof(q.action_id));
        strlcpy(q.text,      co["text"]      | "", sizeof(q.text));
        q.snapshot     = co["snapshot"]     | false;
        q.ctx_beh_name = co["ctx_beh_name"] | false;
        q.ctx_sensor   = co["ctx_sensor"]   | false;
        strlcpy(q.canned_id, co["canned_id"] | "", sizeof(q.canned_id));
        q.canned_random = co["canned_random"] | false;
        q.reaction_count = 0;
        for (JsonVariant rv : co["reactions"].as<JsonArray>()) {
            if (q.reaction_count >= MAX_REACTIONS) break;
            const char* s = rv.as<const char*>();
            strlcpy(q.reactions[q.reaction_count++], s ? s : "", 32);
        }
    }
}

static void deserializePersonality(Personality& p, JsonObject po) {
    memset(&p, 0, sizeof(p));
    strlcpy(p.id,       po["id"]       | "", sizeof(p.id));
    strlcpy(p.name,     po["name"]     | "", sizeof(p.name));
    strlcpy(p.prompt,   po["prompt"]   | "", sizeof(p.prompt));
    strlcpy(p.voice_id, po["voice_id"] | "", sizeof(p.voice_id));
    strlcpy(p.lang,     po["lang"]     | "it", sizeof(p.lang));
    p.behavior_count = 0;
    for (JsonObject bo : po["behaviors"].as<JsonArray>()) {
        if (p.behavior_count >= MAX_EVENT_BEHAVIORS) break;
        deserializeBehavior(p.behaviors[p.behavior_count++], bo);
    }
    p.canned_count = 0;
    for (JsonObject ko : po["canned"].as<JsonArray>()) {
        if (p.canned_count >= MAX_CANNED_PHRASES) break;
        CannedPhrase& k = p.canned[p.canned_count++];
        strlcpy(k.id,   ko["id"]   | "", sizeof(k.id));
        strlcpy(k.text, ko["text"] | "", sizeof(k.text));
        // Il campo "file" non è inviato dal frontend (popolato solo dopo la prima esecuzione TTS).
        // Lo recuperiamo dalla personality attiva in memoria se l'id coincide.
        const char* existingFile = ko["file"] | "";
        if (existingFile[0]) {
            strlcpy(k.file, existingFile, sizeof(k.file));
        } else if (gAllPersonalities && k.id[0]) {
            // cerca in TUTTE le personality già caricate (per save bulk che ridefinisce l'array)
            for (int pi = 0; pi < gPersonalityCount && !k.file[0]; pi++) {
                Personality& old = gAllPersonalities[pi];
                for (int i = 0; i < old.canned_count; i++) {
                    if (strcmp(old.canned[i].id, k.id) == 0) {
                        if (strcmp(old.canned[i].text, k.text) == 0)
                            strlcpy(k.file, old.canned[i].file, sizeof(k.file));
                        break;
                    }
                }
            }
        }
    }
}

// ── Alloca array di Personality in PSRAM (chiamato una sola volta in setup) ──
static void ensurePersonalitiesAllocated() {
    if (gAllPersonalities) return;
    size_t sz = sizeof(Personality) * MAX_PERSONALITIES;
    void* mem = heap_caps_calloc(MAX_PERSONALITIES, sizeof(Personality), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        Serial.printf("[PERS] PSRAM esaurita (%u), fallback DRAM per array\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        mem = calloc(MAX_PERSONALITIES, sizeof(Personality));
    }
    gAllPersonalities = static_cast<Personality*>(mem);
    Serial.printf("[PERS] array allocato in PSRAM: %u byte (%d slot)\n", (unsigned)sz, MAX_PERSONALITIES);
}

static void buildDefaultPersonality(Personality& p) {
    memset(&p, 0, sizeof(p));
    strlcpy(p.id,       "default",  sizeof(p.id));
    strlcpy(p.name,     "Default",  sizeof(p.name));
    strlcpy(p.lang,     "it",       sizeof(p.lang));
    strlcpy(p.voice_id, elevenlabs_voice_id.c_str(), sizeof(p.voice_id));
    // 17 comportamenti - uno per sensore SEN_ANT_L..SEN_TILT_L
    p.behavior_count = 0;
    for (int s = SEN_ANT_L; s < SEN_COUNT; s++) {
        EventBehavior& b = p.behaviors[p.behavior_count];
        snprintf(b.id,   sizeof(b.id),   "sen_%d", s);
        snprintf(b.name, sizeof(b.name), "%d", p.behavior_count + 1);
        b.trigger   = TRG_SENSOR;
        b.sensor_id = (uint8_t)s;
        b.consequences[0].type       = CSQ_PROMPT_AUTO;
        b.consequences[0].ctx_sensor = true;
        b.consequence_count = 1;
        p.behavior_count++;
    }
}

// ── applyActivePersonality: copia prompt/voice/behaviors dai globals ──────────
void applyActivePersonality() {
    if (!gpActivePers) return;
    gPersonalityPrompt  = String(gpActivePers->prompt);
    gPersonalityVoiceId = String(gpActivePers->voice_id);
    gPersonalityLang    = gpActivePers->lang[0] ? String(gpActivePers->lang) : "it";
    gEventBehaviorCount = gpActivePers->behavior_count;
    for (int i = 0; i < gpActivePers->behavior_count; i++)
        gEventBehaviors[i] = gpActivePers->behaviors[i];
}

void setActiveByIndex(int idx) {
    if (idx < 0 || idx >= gPersonalityCount) idx = 0;
    if (gPersonalityCount == 0) { gpActivePers = nullptr; gActivePersonality = 0; return; }
    gActivePersonality = idx;
    gpActivePers = &gAllPersonalities[idx];
    applyActivePersonality();
}

void serializeActivePersonalityTo(JsonObject po) {
    if (!gpActivePers) return;
    po["id"]       = gpActivePers->id;
    po["name"]     = gpActivePers->name;
    po["prompt"]   = gpActivePers->prompt;
    po["voice_id"] = gpActivePers->voice_id;
    po["lang"]     = gpActivePers->lang[0] ? gpActivePers->lang : "it";
    JsonArray ba = po["behaviors"].to<JsonArray>();
    for (int b = 0; b < gpActivePers->behavior_count; b++) {
        JsonObject bo = ba.add<JsonObject>();
        serializeBehavior(bo, gpActivePers->behaviors[b]);
    }
    JsonArray ka = po["canned"].to<JsonArray>();
    for (int k = 0; k < gpActivePers->canned_count; k++) {
        JsonObject ko = ka.add<JsonObject>();
        ko["id"]   = gpActivePers->canned[k].id;
        ko["text"] = gpActivePers->canned[k].text;
        ko["file"] = gpActivePers->canned[k].file;
    }
}

// ── Storage personalities ────────────────────────────────────────────────────
// Architettura RAM-first:
//   - gAllPersonalities[] preallocato in PSRAM (MAX_PERSONALITIES slot)
//   - gpActivePers punta a una entry dell'array (mai allocazione separata)
//   - gPersonalitiesOrigin = sorgente da cui è stato letto (SD > SPIFFS > DEFAULT)
//   - lettura da disco SOLO al boot + reconcile su SD-appear
//   - scrittura: atomic via tmp+rename, mirror SD+SPIFFS quando SD disponibile

static const char* PERS_FILE     = "/personalities.json";
static const char* PERS_FILE_TMP = "/personalities.json.tmp";

static String serializeAllPersonalities() {
    auto doc = JsonDocPsram();
    doc["active"] = gActivePersonality;
    JsonArray arr = doc["personalities"].to<JsonArray>();
    for (int i = 0; i < gPersonalityCount; i++) {
        Personality& p = gAllPersonalities[i];
        JsonObject po = arr.add<JsonObject>();
        po["id"]       = p.id;
        po["name"]     = p.name;
        po["prompt"]   = p.prompt;
        po["voice_id"] = p.voice_id;
        po["lang"]     = p.lang[0] ? p.lang : "it";
        JsonArray ba = po["behaviors"].to<JsonArray>();
        for (int b = 0; b < p.behavior_count; b++) {
            JsonObject bo = ba.add<JsonObject>();
            serializeBehavior(bo, p.behaviors[b]);
        }
        JsonArray ka = po["canned"].to<JsonArray>();
        for (int k = 0; k < p.canned_count; k++) {
            JsonObject ko = ka.add<JsonObject>();
            ko["id"]   = p.canned[k].id;
            ko["text"] = p.canned[k].text;
            ko["file"] = p.canned[k].file;
        }
    }
    String out; serializeJson(doc, out); return out;
}

// scrittura atomica: scrivi tmp, fsync via close, remove target, rename tmp→target.
// Se rename non disponibile/fallisce, restiamo con tmp+target e il recover al boot prende il buono.
template<typename FS>
static bool atomicWriteFS(FS& fs, const String& payload, const char* path, const char* tmpPath) {
    fs.remove(tmpPath);
    File tf = fs.open(tmpPath, "w");
    if (!tf) return false;
    size_t written = tf.print(payload);
    tf.close();
    if (written != payload.length()) { fs.remove(tmpPath); return false; }
    fs.remove(path);
    bool ok = fs.rename(tmpPath, path);
    if (!ok) { // rename fallito: tieni almeno il tmp leggibile
        File t = fs.open(tmpPath, "r");
        if (!t) return false;
        File f = fs.open(path, "w");
        if (f) { while (t.available()) f.write(t.read()); f.close(); fs.remove(tmpPath); ok = true; }
        t.close();
    }
    return ok;
}

// ricovero al boot: se esiste tmp insieme al target valido, tmp è una scrittura
// interrotta → cancella tmp. Se il target è corrotto e tmp esiste valido, promuove tmp.
template<typename FS>
static void recoverInterruptedWrite(FS& fs, const char* path, const char* tmpPath) {
    if (!fs.exists(tmpPath)) return;
    // verifica se target è leggibile e parsabile
    bool targetOk = false;
    if (fs.exists(path)) {
        File t = fs.open(path, "r");
        if (t) {
            auto d = JsonDocPsram();
            targetOk = (deserializeJson(d, t) == DeserializationError::Ok);
            t.close();
        }
    }
    if (targetOk) { fs.remove(tmpPath); Serial.printf("[PERS] recover: %s rimosso (target ok)\n", tmpPath); return; }
    // target ko, prova tmp
    File t = fs.open(tmpPath, "r");
    if (!t) return;
    auto d = JsonDocPsram();
    bool tmpOk = (deserializeJson(d, t) == DeserializationError::Ok);
    t.close();
    if (tmpOk) {
        fs.remove(path);
        fs.rename(tmpPath, path);
        Serial.printf("[PERS] recover: promosso %s → %s (target era corrotto)\n", tmpPath, path);
    } else {
        fs.remove(tmpPath);
        Serial.printf("[PERS] recover: %s era corrotto anche lui, rimosso\n", tmpPath);
    }
}

// Scrittura: mirror SD+SPIFFS quando SD disponibile, solo SPIFFS altrimenti.
bool writePersFile(const String& payload) {
    bool okSd = false, okSpiffs = false;
    if (sdAvailable) {
        okSd = atomicWriteFS(SD_MMC, payload, PERS_FILE, PERS_FILE_TMP);
        if (!okSd) Serial.println("[PERS] WARN: scrittura SD fallita");
    }
    okSpiffs = atomicWriteFS(SPIFFS, payload, PERS_FILE, PERS_FILE_TMP);
    if (!okSpiffs) Serial.println("[PERS] WARN: scrittura SPIFFS fallita");
    if (sdAvailable && okSd && okSpiffs) Serial.println("[PERS] salvato su SD+SPIFFS (mirror)");
    else if (okSd)                        Serial.println("[PERS] salvato su SD");
    else if (okSpiffs)                    Serial.println("[PERS] salvato su SPIFFS");
    return okSd || okSpiffs;
}

void savePersonalities() {
    if (!gAllPersonalities || gPersonalityCount == 0) { Serial.println("[PERS] save: niente da salvare"); return; }
    String out = serializeAllPersonalities();
    if (!writePersFile(out)) Serial.println("[PERS] ERRORE: scrittura fallita su tutti i FS");
}

// Carica TUTTE le personality in gAllPersonalities. Ritorna l'origin scelta.
static PersOrigin loadPersonalitiesFromFS() {
    ensurePersonalitiesAllocated();
    if (!gAllPersonalities) { Serial.println("[PERS] FATAL: array non allocato"); return PORG_NONE; }
    gPersonalityCount = 0;

    // recover scritture interrotte
    if (sdAvailable) recoverInterruptedWrite(SD_MMC, PERS_FILE, PERS_FILE_TMP);
    recoverInterruptedWrite(SPIFFS, PERS_FILE, PERS_FILE_TMP);

    File f;
    PersOrigin origin = PORG_NONE;
    if (sdAvailable) {
        f = SD_MMC.open(PERS_FILE, FILE_READ);
        if (f) origin = PORG_SD;
    }
    if (!f) {
        f = SPIFFS.open(PERS_FILE, "r");
        if (f) origin = PORG_SPIFFS;
    }
    if (!f) {
        Personality& p0 = gAllPersonalities[0];
        buildDefaultPersonality(p0);
        gPersonalityCount = 1;
        gActivePersonality = 0;
        setActiveByIndex(0);
        Serial.println("[PERS] nessun file (SD/SPIFFS) - default in RAM");
        return PORG_DEFAULT;
    }
    Serial.printf("[PERS] carico da %s\n", origin == PORG_SD ? "SD" : "SPIFFS");

    auto doc = JsonDocPsram();
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) {
        Serial.printf("[PERS] ERRORE parse %s: %s — uso default\n", origin == PORG_SD ? "SD" : "SPIFFS", err.c_str());
        Personality& p0 = gAllPersonalities[0];
        buildDefaultPersonality(p0);
        gPersonalityCount = 1;
        gActivePersonality = 0;
        setActiveByIndex(0);
        return PORG_DEFAULT;
    }

    int active = doc["active"] | 0;
    JsonArray arr = doc["personalities"].as<JsonArray>();
    for (JsonVariant v : arr) {
        if (gPersonalityCount >= MAX_PERSONALITIES) { Serial.println("[PERS] WARN: array pieno, troncamento"); break; }
        if (!v.is<JsonObject>()) continue;
        deserializePersonality(gAllPersonalities[gPersonalityCount], v.as<JsonObject>());
        gPersonalityCount++;
    }
    if (gPersonalityCount == 0) {
        Personality& p0 = gAllPersonalities[0];
        buildDefaultPersonality(p0);
        gPersonalityCount = 1;
        active = 0;
        origin = PORG_DEFAULT;
        Serial.println("[PERS] array vuoto, default in RAM");
    }
    if (active < 0 || active >= gPersonalityCount) active = 0;
    gActivePersonality = active;
    setActiveByIndex(active);
    Serial.printf("[PERS] %d personalit%s in RAM, attiva=\"%s\" (PSRAM libera=%u)\n",
        gPersonalityCount, gPersonalityCount == 1 ? "à" : "à",
        gpActivePers ? gpActivePers->name : "?",
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return origin;
}

bool loadAllPersonalitiesFromJSON(const String& body, int* outActive) {
    auto doc = JsonDocPsram();
    if (deserializeJson(doc, body) != DeserializationError::Ok) return false;
    JsonArray arr = doc["personalities"].as<JsonArray>();
    if (arr.isNull()) return false;
    ensurePersonalitiesAllocated();
    if (!gAllPersonalities) return false;
    // Deserialize in slot temp PSRAM così deserializePersonality può cercare i canned.file
    // nel vecchio gAllPersonalities mentre costruisce i nuovi.
    Personality* tmp = (Personality*)heap_caps_calloc(MAX_PERSONALITIES, sizeof(Personality), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp) tmp = (Personality*)calloc(MAX_PERSONALITIES, sizeof(Personality));
    if (!tmp) return false;
    int newCount = 0;
    for (JsonVariant v : arr) {
        if (newCount >= MAX_PERSONALITIES) break;
        if (!v.is<JsonObject>()) continue;
        deserializePersonality(tmp[newCount], v.as<JsonObject>()); // legge vecchio gAllPersonalities
        newCount++;
    }
    if (newCount == 0) { heap_caps_free(tmp); return false; }
    // Swap atomico
    for (int i = 0; i < newCount; i++) gAllPersonalities[i] = tmp[i];
    for (int i = newCount; i < MAX_PERSONALITIES; i++) memset(&gAllPersonalities[i], 0, sizeof(Personality));
    heap_caps_free(tmp);
    gPersonalityCount = newCount;
    int active = doc["active"] | 0;
    if (active < 0 || active >= gPersonalityCount) active = 0;
    setActiveByIndex(active);
    if (outActive) *outActive = active;
    return true;
}

void loadPersonalities() {
    gPersonalitiesOrigin = loadPersonalitiesFromFS();
    // Se RAM proviene da SPIFFS e SD è disponibile, fai mirror immediato su SD.
    if (gPersonalitiesOrigin == PORG_SPIFFS && sdAvailable) {
        Serial.println("[PERS] origin=SPIFFS ma SD disponibile: mirror su SD");
        if (writePersFile(serializeAllPersonalities())) gPersonalitiesOrigin = PORG_SD;
    }
    sdSetOnAppearCallback(reconcilePersonalitiesWithSD);
}

// Chiamata quando SD passa da assente a presente. Politica:
//   - se SD ha personalities.json valido → carico quello (SD vince sempre)
//   - se SD vuota e RAM viene da SPIFFS/DEFAULT → mirror su SD
//   - SPIFFS non viene mai cancellato (resta come backup)
void reconcilePersonalitiesWithSD() {
    if (!sdAvailable) return;
    recoverInterruptedWrite(SD_MMC, PERS_FILE, PERS_FILE_TMP);
    bool sdHasFile = SD_MMC.exists(PERS_FILE);
    if (sdHasFile) {
        if (gPersonalitiesOrigin == PORG_SD) return; // già in sync
        Serial.println("[PERS] reconcile: SD comparsa con file presente → ricarico da SD");
        gPersonalitiesOrigin = loadPersonalitiesFromFS();
    } else {
        Serial.println("[PERS] reconcile: SD comparsa vuota → mirror RAM su SD");
        if (writePersFile(serializeAllPersonalities())) gPersonalitiesOrigin = PORG_SD;
    }
}

void activatePersonality(int idx) {
    if (idx < 0 || idx >= gPersonalityCount) {
        Serial.printf("[PERS] activate: idx %d fuori range (count=%d)\n", idx, gPersonalityCount);
        return;
    }
    setActiveByIndex(idx);
    savePersonalities();
    Serial.printf("[PERS] attivata: \"%s\" (idx=%d)\n", gpActivePers->name, idx);
}


// ── Lookup azione ─────────────────────────────────────────────────────────────
const FurbyActionDef* findFurbyAction(const char* id) {
    for (int i = 0; i < FURBY_ACTIONS_COUNT; i++)
        if (strcmp(FURBY_ACTIONS[i].id, id) == 0) return &FURBY_ACTIONS[i];
    return nullptr;
}

void speakText(const String& text) {
    bool inSim = (gSimLog != nullptr);
    // skip_tts: non chiamare TTS, dichiara cosa avrei fatto e basta
    if (gSimSkipTts) {
        SIMSTEP(String("→ TTS: SKIP (skip_tts attivo) — avrei generato \"") + text + "\"");
        return;
    }
    sdCheck();
    // audio_local (simulazione): genera ma manda al browser, niente riproduzione ESP.
    // Senza SD non c'è cache su cui appoggiarsi: genera+streamma comunque e NON salvare nulla.
    if (gSimAudioLocal) {
        if (!sdAvailable) {
            SIMSTEP(String("→ TTS: genero \"") + text + "\" (senza SD: nessuna cache, file volatile non disponibile per audio_local)");
            SIMSTEP("→ audio: SKIP invio browser — serve SD per produrre un path scaricabile");
            return;
        }
        SIMSTEP(String("→ TTS: genero \"") + text + "\" (SD)");
        uint32_t t0 = millis();
        String f = generateAndSaveTTS_SD(text);
        if (f.length() == 0) { SIMSTEP(String("← TTS: ERRORE (") + String(millis() - t0) + "ms)"); return; }
        updateCacheJSON(f, text);
        gSimAudioFile = f;
        SIMSTEP(String("← TTS: file=") + f + " (" + String(millis() - t0) + "ms)");
        SIMSTEP("→ audio: invio al browser (skip riproduzione ESP)");
        return;
    }
    // produzione:
    //  - con SD: TTS+save+play da cache (riusabile)
    //  - senza SD: streaming RAM puro, NESSUNA cache mai
    if (inSim) SIMSTEP(String("→ TTS+play: \"") + text + "\" (" + (sdAvailable ? "SD cache" : "RAM stream — no SD, no cache") + ")");
    else Serial.printf("[SPEAK] \"%s\" (SD=%s)\n", text.c_str(), sdAvailable ? "si" : "no");
    if (sdAvailable) generateAndPlayTTS_SD(text);
    else             streamAndPlayTTS_RAM(text);
}

// helper: esegue (o logga skip di) un'azione BLE Furby
static void runFurbyAction(const FurbyActionDef* act) {
    if (!act) return;
    if (gDryRun || gSimSkipBle) {
        SIMSTEP(String("→ BLE: SKIP (skip_ble attivo) — comando ") + act->id + " (\"" + act->label + "\")");
        return;
    }
    SIMSTEP(String("→ BLE: invio ") + act->id + " (\"" + act->label + "\")");
    furbyWrite(act->cmd, act->len);
    SIMSTEP("  attesa 1500ms (movimento fisico)");
    delay(1500);
}

// ── Esecuzione conseguenza ────────────────────────────────────────────────────
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText, const char* behName, uint8_t sensorId) {
    bool inSim = (gSimLog != nullptr);
    if (!inSim) Serial.printf("[CSQ] tipo=%d snapshot=%d testo=\"%s\"\n", csq.type, csq.snapshot, csq.text);

    switch (csq.type) {

        case CSQ_FURBY_ACTION: {
            const FurbyActionDef* act = findFurbyAction(csq.action_id);
            if (!act) { SIMSTEP(String("ERRORE: azione \"") + csq.action_id + "\" non trovata"); break; }
            runFurbyAction(act);
            break;
        }

        case CSQ_TTS_FIXED: {
            String t = String(csq.text); t.trim();
            if (t.length() == 0) { SIMSTEP("testo vuoto, nulla da pronunciare"); break; }
            SIMSTEP(String("testo: \"") + t + "\"");
            speakText(t);
            break;
        }

        case CSQ_PROMPT_FIXED:
        case CSQ_PROMPT_LLM: {
            String img = csq.snapshot ? base64Img : "";
            String userMsg;
            if (csq.type == CSQ_PROMPT_LLM)
                userMsg = sttText.length() > 0 ? sttText : String(csq.text);
            else
                userMsg = sttText.length() > 0
                    ? "L'utente ha detto: \"" + sttText + "\". " + String(csq.text)
                    : String(csq.text);
            if (csq.ctx_beh_name && behName && behName[0])
                userMsg = "[Regola attiva: \"" + String(behName) + "\"] " + userMsg;
            if (csq.ctx_sensor && sensorId > 0 && sensorId < SEN_COUNT) {
                const char* sname = (gPersonalityLang == "en") ? SENSOR_NAMES_EN[sensorId] : SENSOR_NAMES[sensorId];
                userMsg = "[Sensore: " + String(sname) + "] " + userMsg;
            }
            SIMSTEP(String("snapshot: ") + (csq.snapshot ? (img.length() ? "si (" + String(img.length()) + " char base64)" : "richiesto ma non disponibile") : "no"));
            SIMSTEP(String("→ LLM INVIO:"));
            SIMSUB(String("system: \"") + _short(gPersonalityPrompt) + "\"");
            SIMSUB(String("user:   \"") + _short(userMsg) + "\"");

            if (csq.reaction_count == 0) {
                uint32_t t0 = millis();
                String answer = callLLM(img, gPersonalityPrompt, userMsg);
                SIMSTEP(String("← LLM RICEZIONE (") + String(millis() - t0) + "ms): \"" + _short(answer) + "\"");
                if (answer.length() == 0) { SIMSTEP("ERRORE: risposta vuota"); break; }
                speakText(answer);
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
                SIMSUB(String("reazioni ammesse: ") + String(csq.reaction_count));
                uint32_t t0 = millis();
                String answer = callLLM(img, sys, userMsg);
                SIMSTEP(String("← LLM RICEZIONE (") + String(millis() - t0) + "ms): \"" + _short(answer) + "\"");
                String speechBefore, speechAfter, actionId;
                auto rdoc = JsonDocPsram();
                if (deserializeJson(rdoc, answer) == DeserializationError::Ok) {
                    speechBefore = rdoc["speech_before"] | "";
                    speechAfter  = rdoc["speech_after"]  | "";
                    actionId     = rdoc["action"]         | "";
                    SIMSUB(String("parse JSON ok — before=\"") + speechBefore + "\" action=" + actionId + " after=\"" + speechAfter + "\"");
                } else {
                    speechBefore = answer;
                    SIMSUB("parse JSON fallito, tratto tutto come speech_before");
                }
                if (speechBefore.length() > 0 && speechBefore != "null") speakText(speechBefore);
                if (actionId.length() > 0 && actionId != "null") {
                    const FurbyActionDef* act = findFurbyAction(actionId.c_str());
                    if (act) runFurbyAction(act);
                    else SIMSTEP(String("azione id=\"") + actionId + "\" non trovata");
                }
                if (speechAfter.length() > 0 && speechAfter != "null") speakText(speechAfter);
            }
            break;
        }

        case CSQ_CANNED: {
            if (!gpActivePers || gpActivePers->canned_count == 0) {
                SIMSTEP("nessuna frase pronta definita per questa personality");
                break;
            }
            int idx = -1;
            if (csq.canned_random || csq.canned_id[0] == 0) {
                idx = (int)(esp_random() % (uint32_t)gpActivePers->canned_count);
                SIMSTEP(String("selezione: casuale tra ") + String(gpActivePers->canned_count) + " frasi");
            } else {
                for (int i = 0; i < gpActivePers->canned_count; i++) {
                    if (strcmp(gpActivePers->canned[i].id, csq.canned_id) == 0) { idx = i; break; }
                }
                SIMSTEP(String("selezione: id=") + csq.canned_id);
            }
            if (idx < 0) { SIMSTEP(String("ERRORE: canned_id \"") + csq.canned_id + "\" non trovato"); break; }
            CannedPhrase& cp = gpActivePers->canned[idx];
            String text = String(cp.text);
            String file = String(cp.file);
            SIMSTEP(String("→ scelta: \"") + text + "\" (id=" + cp.id + ")");
            SIMSTEP(String("cache file: ") + (file.length() ? file : "(non ancora generato)"));

            if (gSimSkipTts) {
                if (file.length() == 0) SIMSKIP("TTS (skip_tts) — avrei generato e marcato come canned");
                else                     SIMSKIP("riproduzione (skip_tts) — file gia' pronto: " + file);
                break;
            }
            if (file.length() == 0) {
                sdCheck();
                uint32_t t0 = millis();
                String saved = generateAndSaveTTS_SD(text);
                if (saved.length() == 0) { SIMSTEP(String("← TTS: ERRORE (") + String(millis() - t0) + "ms)"); break; }
                strlcpy(cp.file, saved.c_str(), sizeof(cp.file));
                updateCacheJSONCanned(saved, text);
                savePersonalities();
                file = saved;
                SIMSTEP(String("← TTS: salvato come ") + file + " (" + String(millis() - t0) + "ms, marcato canned)");
            }
            if (gSimAudioLocal) {
                gSimAudioFile = file;
                SIMSTEP("→ audio: invio al browser (skip riproduzione ESP)");
                break;
            }
            SIMSTEP(String("→ play: ") + file);
            playAudioSD(file);
            break;
        }

        case CSQ_PROMPT_AUTO: {
            String img = csq.snapshot ? base64Img : "";
            String userMsg = sttText.length() > 0 ? sttText : String(csq.text);
            if (userMsg.length() == 0) userMsg = "Reagisci allo stimolo ricevuto.";
            if (csq.ctx_beh_name && behName && behName[0])
                userMsg = "[Regola attiva: \"" + String(behName) + "\"] " + userMsg;
            if (csq.ctx_sensor && sensorId > 0 && sensorId < SEN_COUNT) {
                const char* sname = (gPersonalityLang == "en") ? SENSOR_NAMES_EN[sensorId] : SENSOR_NAMES[sensorId];
                userMsg = "[Sensore: " + String(sname) + "] " + userMsg;
            }
            bool isEn = (gPersonalityLang == "en");
            String actionList;
            for (int i = 0; i < FURBY_ACTIONS_COUNT; i++)
                actionList += String(FURBY_ACTIONS[i].id) + ": " + (isEn ? FURBY_ACTIONS[i].label_en : FURBY_ACTIONS[i].label) + "\n";
            String sys = gPersonalityPrompt + (isEn
                ? " Choose ONE action from this list and reply with ONLY its id, nothing else:\n"
                : " Scegli UNA SOLA azione da questo elenco e rispondi con SOLO il suo id, nient'altro:\n") + actionList;
            SIMSTEP(String("azioni disponibili: ") + String(FURBY_ACTIONS_COUNT));
            SIMSTEP("→ LLM INVIO:");
            SIMSUB(String("user: \"") + _short(userMsg) + "\"");
            uint32_t t0 = millis();
            String answer = callLLM(img, sys, userMsg);
            answer.trim();
            SIMSTEP(String("← LLM RICEZIONE (") + String(millis() - t0) + "ms): \"" + answer + "\"");
            const FurbyActionDef* act = findFurbyAction(answer.c_str());
            if (!act) { SIMSTEP(String("ERRORE: id azione non valido \"") + answer + "\""); break; }
            SIMSTEP(String("→ azione scelta: ") + act->id + " (\"" + act->label + "\")");
            runFurbyAction(act);
            break;
        }

        default:
            SIMSTEP(String("tipo conseguenza sconosciuto: ") + String((int)csq.type));
            break;
    }
}

// ── Simulazione (debug, dry-run, personalità non attiva via idx) ──────────────
String processStimulusSimulated(TriggerType trg, uint8_t sensorId, const String& vadText, bool skipLlm, int personalityIdx) {
    String log;
    gSimLog = &log; // attivato per primo così SIMACT/SIMSTEP funzionano da subito
    _simT0 = millis(); // timestamp 0 = inizio simulazione

    bool prevDry = gDryRun;
    gDryRun = true;

    String savedPrompt   = gPersonalityPrompt;
    String savedVoiceId  = gPersonalityVoiceId;
    int    savedBehCount = gEventBehaviorCount;
    EventBehavior* savedBehaviors = (EventBehavior*)heap_caps_malloc(
        MAX_EVENT_BEHAVIORS * sizeof(EventBehavior), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!savedBehaviors) savedBehaviors = new EventBehavior[MAX_EVENT_BEHAVIORS];
    for (int i = 0; i < gEventBehaviorCount; i++) savedBehaviors[i] = gEventBehaviors[i];

    Personality* prevActive = gpActivePers;     // ripristinato a fine simulazione
    int          prevActiveIdx = gActivePersonality;
    Personality* debugPers = nullptr;

    SIMACT("Setup simulazione");
    if (personalityIdx >= 0 && personalityIdx != gActivePersonality && personalityIdx < gPersonalityCount) {
        debugPers = &gAllPersonalities[personalityIdx];
        // swap temporaneo dell'attiva: serve a executeConsequence per leggere le canned giuste
        gpActivePers = debugPers;
        gActivePersonality = personalityIdx;
        gPersonalityPrompt  = String(debugPers->prompt);
        gPersonalityVoiceId = String(debugPers->voice_id);
        gPersonalityLang    = debugPers->lang[0] ? String(debugPers->lang) : "it";
        gEventBehaviorCount = debugPers->behavior_count;
        for (int i = 0; i < debugPers->behavior_count; i++) gEventBehaviors[i] = debugPers->behaviors[i];
        SIMSTEP(String("personalità (debug): \"") + debugPers->name + "\"");
    } else if (personalityIdx >= 0 && personalityIdx >= gPersonalityCount) {
        SIMSTEP(String("WARN: personalità idx=") + String(personalityIdx) + " fuori range, uso attiva");
    }
    if (!debugPers) SIMSTEP(String("personalità (attiva): \"") + (gpActivePers ? gpActivePers->name : "?") + "\"");
    SIMSTEP(String("lingua: ") + gPersonalityLang + " | voice: " + gPersonalityVoiceId);
    SIMSTEP(String("prompt: \"") + _short(gPersonalityPrompt) + "\"");
    SIMSTEP(String("trigger: ") + (trg==TRG_VAD?"VAD":trg==TRG_BUTTON?"BOTTONE":"SENSORE") + (trg==TRG_SENSOR?(String(" id=")+sensorId):""));
    SIMSTEP(String("flag: skip_llm=") + (skipLlm?"ON":"OFF") + " | skip_tts=" + (gSimSkipTts?"ON":"OFF") + " | skip_ble=" + (gSimSkipBle?"ON":"OFF") + " | audio_local=" + (gSimAudioLocal?"ON":"OFF"));
    String sttText = vadText;
    if (sttText.length() > 0) SIMSTEP(String("VAD simulato: \"") + sttText + "\"");

    EventBehavior* beh = nullptr;
    for (int i = 0; i < gEventBehaviorCount; i++) {
        EventBehavior& b = gEventBehaviors[i];
        if (b.trigger != trg) continue;
        if (trg == TRG_SENSOR && b.sensor_id != sensorId) continue;
        beh = &b;
        SIMSTEP(String("comportamento match: \"") + b.name + "\" (" + String(b.consequence_count) + " azioni)");
        break;
    }
    if (!beh) SIMSTEP("nessun comportamento match - userò processStimulusDefault");

    // cattura camera solo se almeno una conseguenza la richiede
    bool needsCam = false;
    if (beh) { for (int c = 0; c < beh->consequence_count; c++) if (beh->consequences[c].snapshot) { needsCam = true; break; } }
    else needsCam = true;

    String base64Img;
    SIMACT("Camera");
    if (needsCam) {
        if (camActive || camInit()) {
            camTouch();
            camApplySettings(camSnapSize, camSnapQuality);
            uint32_t t0 = millis();
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb) {
                base64Img = base64Encode(fb->buf, fb->len);
                esp_camera_fb_return(fb);
                SIMSTEP(String("frame catturato in ") + String(millis()-t0) + "ms → " + String(base64Img.length()) + " char base64");
            } else {
                SIMSTEP("esp_camera_fb_get() NULL, procedo senza immagine");
            }
            camApplySettings(camStreamSize, camStreamQuality);
        } else {
            SIMSTEP("camera: non disponibile");
        }
    } else {
        SIMSTEP("non richiesta da questo behavior");
    }

    if (!beh) {
        SIMACT("Stimolo default (no behavior match)");
        if (skipLlm) SIMSKIP("processStimulusDefault (skip_llm)");
        else         processStimulusDefault(base64Img);
    } else {
        for (int c = 0; c < beh->consequence_count; c++) {
            const Consequence& csq = beh->consequences[c];
            SIMACT(String("Azione ") + String(c+1) + "/" + String(beh->consequence_count) + " — " + _csqTypeName(csq.type));
            bool isLlm = (csq.type == CSQ_PROMPT_LLM || csq.type == CSQ_PROMPT_FIXED || csq.type == CSQ_PROMPT_AUTO);
            if (isLlm && skipLlm) {
                String simMsg = sttText.length() > 0 ? sttText : String(csq.text);
                if (simMsg.length() == 0) simMsg = "Reagisci allo stimolo ricevuto.";
                if (csq.ctx_beh_name && beh->name[0])
                    simMsg = "[Regola attiva: \"" + String(beh->name) + "\"] " + simMsg;
                if (csq.ctx_sensor && sensorId > 0 && sensorId < SEN_COUNT) {
                    const char* sname = (gPersonalityLang == "en") ? SENSOR_NAMES_EN[sensorId] : SENSOR_NAMES[sensorId];
                    simMsg = "[Sensore: " + String(sname) + "] " + simMsg;
                }
                SIMSKIP(String("LLM (skip_llm) — userMsg sarebbe: \"") + simMsg + "\"");
            }
            else executeConsequence(csq, base64Img, sttText, beh->name, sensorId);
        }
    }

    SIMACT("Fine");
    gSimLog = nullptr;

    // debugPers è un puntatore dentro gAllPersonalities, non va liberato.
    // Ripristino i puntatori dell'attiva se erano stati swappati.
    gpActivePers       = prevActive;
    gActivePersonality = prevActiveIdx;

    gPersonalityPrompt  = savedPrompt;
    gPersonalityVoiceId = savedVoiceId;
    gEventBehaviorCount = savedBehCount;
    for (int i = 0; i < savedBehCount; i++) gEventBehaviors[i] = savedBehaviors[i];
    free(savedBehaviors);
    gDryRun         = prevDry;
    gSimSkipTts     = false;
    gSimSkipBle     = false;
    gSimAudioLocal  = false;
    return log;
}

void processStimulusDefault(const String& base64Img) {
    sdCheck();
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
    if (answer.length() == 0) return;
    if (sdAvailable) {
        if (answer.startsWith("NEW:"))    generateAndPlayTTS_SD(answer.substring(4));
        else if (answer.endsWith(".pcm") || answer.endsWith(".mp3")) playAudioSDWithPrefix(answer);
        else                              generateAndPlayTTS_SD(answer);
    } else {
        if (answer.startsWith("NEW:")) answer = answer.substring(4);
        streamAndPlayTTS_RAM(answer);
    }
}

void processStimulus(TriggerType trg, uint8_t sensorId) {
    if (!connected && !gDryRun) {
        Serial.println("[STIMULUS] skip - Furby non connesso e dry run disattivo");
        return;
    }
    Serial.printf("[STIMULUS] trigger=%d sensorId=%d behaviors=%d heap=%u PSRAM=%u\n",
        trg, sensorId, gEventBehaviorCount, esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    String sttText;
    if (trg == TRG_VAD && sttEnabled && gSttLen > 0) {
        sttText = transcribeAudio();
        gSttLen = 0;
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
        Serial.printf("[STIMULUS] nessun behavior per trigger=%d sensor=%d - skip\n", (int)trg, (int)sensorId);
        return;
    }

    // cattura camera solo se almeno una conseguenza la richiede
    String base64Img;
    bool needsCam = false;
    for (int c = 0; c < beh->consequence_count; c++) if (beh->consequences[c].snapshot) { needsCam = true; break; }
    if (needsCam) {
        if (camActive || camInit()) {
            camTouch();
            camApplySettings(camSnapSize, camSnapQuality);
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb) { base64Img = base64Encode(fb->buf, fb->len); esp_camera_fb_return(fb); }
            else Serial.println("[STIMULUS] WARN: esp_camera_fb_get() restituito NULL");
            camApplySettings(camStreamSize, camStreamQuality);
        } else {
            Serial.println("[STIMULUS] WARN: camera non disponibile, procedo senza immagine");
        }
    }

    for (int c = 0; c < beh->consequence_count; c++)
        executeConsequence(beh->consequences[c], base64Img, sttText, beh->name, sensorId);
}
