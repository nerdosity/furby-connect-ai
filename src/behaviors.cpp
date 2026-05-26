#include "behaviors.h"
#include "audio.h"
#include "llm.h"
#include "ble_furby.h"
#include "hw.h"

// append a gSimLog (se attivo) e a Serial
#define SIMLOG(s) do { String _m = (s); Serial.println(_m); if (gSimLog) { *gSimLog += _m + "\n"; } } while(0)

// ── Helpers JSON per serializzare/deserializzare EventBehavior ────────────────
static void serializeBehavior(JsonObject& bo, const EventBehavior& b) {
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
}

// ── Alloca/libera gpActivePers in PSRAM ───────────────────────────────────────
static Personality* allocPersonalityPSRAM() {
    void* mem = heap_caps_malloc(sizeof(Personality), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        Serial.printf("[PERS] PSRAM esaurita (%u), fallback DRAM\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        mem = malloc(sizeof(Personality));
    }
    return static_cast<Personality*>(mem);
}

static void freeActivePers() {
    if (gpActivePers) { heap_caps_free(gpActivePers); gpActivePers = nullptr; }
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
}

// ── savePersonalities: legge tutto il JSON, aggiorna entry attiva, riscrive ───
void savePersonalities() {
    if (!gpActivePers) return;

    auto doc = JsonDocPsram();
    if (SPIFFS.exists("/personalities.json")) {
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }

    doc["active"] = gActivePersonality;
    JsonArray arr = doc["personalities"].is<JsonArray>()
        ? doc["personalities"].as<JsonArray>()
        : doc["personalities"].to<JsonArray>();

    // aggiorna o aggiunge l'entry all'indice gActivePersonality
    // non creare buchi: se l'indice è oltre la fine, append in coda
    if (gActivePersonality > (int)arr.size()) gActivePersonality = (int)arr.size();
    if (gActivePersonality == (int)arr.size()) arr.add(JsonObject{});
    JsonObject po = arr[gActivePersonality].as<JsonObject>();
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

    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/personalities.json", "w");
    if (f) { f.print(out); f.close(); }
}

// ── loadPersonalities: alloca in PSRAM solo quella attiva ─────────────────────
void loadPersonalities() {
    gActivePersonality = 0;

    if (!SPIFFS.exists("/personalities.json")) {
        freeActivePers();
        gpActivePers = allocPersonalityPSRAM();
        if (!gpActivePers) return;
        buildDefaultPersonality(*gpActivePers);
        applyActivePersonality();
        Serial.println("[PERS] nessun file - personalità default in RAM");
        return;
    }

    File f = SPIFFS.open("/personalities.json", "r");
    if (!f) {
        Serial.println("[PERS] ERRORE: impossibile aprire file, uso default in RAM");
        freeActivePers();
        gpActivePers = allocPersonalityPSRAM();
        if (!gpActivePers) return;
        buildDefaultPersonality(*gpActivePers);
        applyActivePersonality();
        return;
    }

    auto doc = JsonDocPsram();
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err != DeserializationError::Ok) {
        Serial.println("[PERS] ERRORE: JSON corrotto, uso default in RAM senza sovrascrivere");
        freeActivePers();
        gpActivePers = allocPersonalityPSRAM();
        if (!gpActivePers) return;
        buildDefaultPersonality(*gpActivePers);
        applyActivePersonality();
        return;
    }

    freeActivePers();
    gpActivePers = allocPersonalityPSRAM();
    if (!gpActivePers) return;

    gActivePersonality = doc["active"] | 0;
    JsonArray arr = doc["personalities"].as<JsonArray>();
    if ((int)arr.size() == 0) {
        buildDefaultPersonality(*gpActivePers);
        applyActivePersonality();
        Serial.println("[PERS] array vuoto, uso default in RAM");
        return;
    }

    if (gActivePersonality >= (int)arr.size()) gActivePersonality = 0;
    while (gActivePersonality < (int)arr.size() && !arr[gActivePersonality].is<JsonObject>()) gActivePersonality++;
    if (gActivePersonality >= (int)arr.size()) gActivePersonality = 0;
    if (arr[gActivePersonality].is<JsonObject>())
        deserializePersonality(*gpActivePers, arr[gActivePersonality].as<JsonObject>());
    else {
        buildDefaultPersonality(*gpActivePers);
        applyActivePersonality();
        Serial.println("[PERS] nessuna personalità valida, uso default in RAM");
        return;
    }

    applyActivePersonality();
    Serial.printf("[PERS] caricata: \"%s\" (idx=%d, PSRAM=%u liberi)\n",
        gpActivePers->name, gActivePersonality, heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ── activatePersonality: switcha personalità senza ricaricare tutto ───────────
void activatePersonality(int idx) {
    File f = SPIFFS.open("/personalities.json", "r");
    if (!f) return;
    auto doc = JsonDocPsram();
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err != DeserializationError::Ok) return;

    JsonArray arr = doc["personalities"].as<JsonArray>();
    if (idx < 0 || idx >= (int)arr.size() || !arr[idx].is<JsonObject>()) return;

    freeActivePers();
    gpActivePers = allocPersonalityPSRAM();
    if (!gpActivePers) return;

    deserializePersonality(*gpActivePers, arr[idx].as<JsonObject>());
    gActivePersonality = idx;

    // persiste l'indice attivo
    doc["active"] = idx;
    String out; serializeJson(doc, out);
    File fw = SPIFFS.open("/personalities.json", "w");
    if (fw) { fw.print(out); fw.close(); }

    applyActivePersonality();
    Serial.printf("[PERS] attivata: \"%s\" (idx=%d)\n", gpActivePers->name, idx);
}

// ── Legacy compat ─────────────────────────────────────────────────────────────
void saveEventBehaviors() {
    if (!gpActivePers) return;
    strlcpy(gpActivePers->prompt,   gPersonalityPrompt.c_str(),  sizeof(gpActivePers->prompt));
    strlcpy(gpActivePers->voice_id, gPersonalityVoiceId.c_str(), sizeof(gpActivePers->voice_id));
    gpActivePers->behavior_count = gEventBehaviorCount;
    for (int i = 0; i < gEventBehaviorCount; i++)
        gpActivePers->behaviors[i] = gEventBehaviors[i];
    savePersonalities();
}

void loadEventBehaviors() {
    loadPersonalities();
}

// ── Lookup azione ─────────────────────────────────────────────────────────────
const FurbyActionDef* findFurbyAction(const char* id) {
    for (int i = 0; i < FURBY_ACTIONS_COUNT; i++)
        if (strcmp(FURBY_ACTIONS[i].id, id) == 0) return &FURBY_ACTIONS[i];
    return nullptr;
}

void speakText(const String& text) {
    if (gSimSkipTts) { Serial.printf("[SPEAK] skip-TTS: \"%s\"\n", text.c_str()); return; }
    sdCheck();
    Serial.printf("[SPEAK] \"%s\" (SD=%s)\n", text.c_str(), sdAvailable ? "si" : "no");
    if (sdAvailable) generateAndPlayTTS_SD(text);
    else             streamAndPlayTTS_RAM(text);
}

// ── Esecuzione conseguenza ────────────────────────────────────────────────────
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText, const char* behName, uint8_t sensorId) {
    Serial.printf("[CSQ] tipo=%d snapshot=%d testo=\"%s\"\n", csq.type, csq.snapshot, csq.text);
    switch (csq.type) {
        case CSQ_FURBY_ACTION: {
            const FurbyActionDef* act = findFurbyAction(csq.action_id);
            if (act) {
                if (gDryRun || gSimSkipBle) {
                    Serial.printf("[CSQ] BLE SKIPPATA: %s\n", act->label);
                } else {
                    furbyWrite(act->cmd, act->len); delay(1500);
                }
            } else {
                Serial.printf("[CSQ] ERRORE: azione \"%s\" non trovata\n", csq.action_id);
            }
            break;
        }
        case CSQ_TTS_FIXED: {
            String t = String(csq.text); t.trim();
            if (t.length() > 0) speakText(t);
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
            // contesto aggiuntivo richiesto dalla regola
            if (csq.ctx_beh_name && behName && behName[0])
                userMsg = "[Regola attiva: \"" + String(behName) + "\"] " + userMsg;
            if (csq.ctx_sensor && sensorId > 0 && sensorId < SEN_COUNT) {
                const char* sname = (gPersonalityLang == "en") ? SENSOR_NAMES_EN[sensorId] : SENSOR_NAMES[sensorId];
                userMsg = "[Sensore: " + String(sname) + "] " + userMsg;
            }
            if (csq.reaction_count == 0) {
                String answer = callLLM(img, gPersonalityPrompt, userMsg);
                if (answer.length() > 0) { SIMLOG("[CSQ] LLM risposta: \"" + answer + "\""); speakText(answer); }
                else SIMLOG("[CSQ] ERRORE: LLM risposta vuota");
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
                String speechBefore, speechAfter, actionId;
                auto rdoc = JsonDocPsram();
                if (deserializeJson(rdoc, answer) == DeserializationError::Ok) {
                    speechBefore = rdoc["speech_before"] | "";
                    speechAfter  = rdoc["speech_after"]  | "";
                    actionId     = rdoc["action"]         | "";
                } else {
                    speechBefore = answer;
                }
                if (speechBefore.length() > 0 && speechBefore != "null") speakText(speechBefore);
                if (actionId.length() > 0 && actionId != "null") {
                    const FurbyActionDef* act = findFurbyAction(actionId.c_str());
                    if (act) {
                        if (gDryRun || gSimSkipBle) SIMLOG(String("[CSQ] BLE SKIPPATA: ") + act->label);
                        else { furbyWrite(act->cmd, act->len); delay(1500); }
                    }
                }
                if (speechAfter.length() > 0 && speechAfter != "null") speakText(speechAfter);
            }
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
            String answer = callLLM(img, sys, userMsg);
            answer.trim();
            const FurbyActionDef* act = findFurbyAction(answer.c_str());
            if (act) {
                SIMLOG("[CSQ] PROMPT_AUTO → azione scelta: " + String(act->id) + " (" + act->label + ")");
                if (gDryRun || gSimSkipBle) SIMLOG(String("[CSQ] BLE SKIPPATA: ") + act->label);
                else { furbyWrite(act->cmd, act->len); delay(1500); }
            } else {
                SIMLOG("[CSQ] PROMPT_AUTO - azione LLM non valida: \"" + answer + "\"");
            }
            break;
        }
        default:
            Serial.printf("[CSQ] tipo sconosciuto: %d\n", csq.type);
            break;
    }
}

// ── Simulazione (debug, dry-run, personalità non attiva via idx) ──────────────
String processStimulusSimulated(TriggerType trg, uint8_t sensorId, const String& vadText, bool skipLlm, int personalityIdx) {
    String log;
    auto L = [&](const String& s){ log += s + "\n"; Serial.println(s); };

    bool prevDry = gDryRun;
    gDryRun = true;

    String savedPrompt   = gPersonalityPrompt;
    String savedVoiceId  = gPersonalityVoiceId;
    int    savedBehCount = gEventBehaviorCount;
    EventBehavior* savedBehaviors = (EventBehavior*)heap_caps_malloc(
        MAX_EVENT_BEHAVIORS * sizeof(EventBehavior), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!savedBehaviors) savedBehaviors = new EventBehavior[MAX_EVENT_BEHAVIORS];
    for (int i = 0; i < gEventBehaviorCount; i++) savedBehaviors[i] = gEventBehaviors[i];

    Personality* debugPers = nullptr;
    bool ownDebugPers = false;

    if (personalityIdx >= 0 && personalityIdx != gActivePersonality) {
        // carica temporaneamente la personalità debug in PSRAM
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) {
            auto doc = JsonDocPsram();
            if (deserializeJson(doc, f) == DeserializationError::Ok) {
                JsonArray arr = doc["personalities"].as<JsonArray>();
                if (personalityIdx < (int)arr.size()) {
                    debugPers = allocPersonalityPSRAM();
                    if (debugPers) {
                        deserializePersonality(*debugPers, arr[personalityIdx].as<JsonObject>());
                        ownDebugPers = true;
                        gPersonalityPrompt  = String(debugPers->prompt);
                        gPersonalityVoiceId = String(debugPers->voice_id);
                        gEventBehaviorCount = debugPers->behavior_count;
                        for (int i = 0; i < debugPers->behavior_count; i++) gEventBehaviors[i] = debugPers->behaviors[i];
                        L("[SIM] personalità debug: \"" + String(debugPers->name) + "\"");
                    }
                }
            }
            f.close();
        }
        if (!debugPers) L("[SIM] WARN: personalità idx=" + String(personalityIdx) + " non trovata, uso attiva");
    }

    if (!debugPers) {
        L("[SIM] personalità: \"" + String(gpActivePers ? gpActivePers->name : "?") + "\" (attiva)");
    }

    if (skipLlm)       L("[SIM] skip-LLM attivo - chiamate LLM simulate");
    if (gSimSkipTts)   L("[SIM] skip-TTS attivo - audio non riprodotto");
    if (gSimSkipBle)   L("[SIM] skip-BLE attivo - azioni Furby non inviate (sovrascrive gDryRun)");
    L("[SIM] trigger=" + String(trg) + " sensorId=" + String(sensorId)
      + " behaviors=" + String(gEventBehaviorCount));

    String sttText = vadText;
    if (sttText.length() > 0)
        L("[SIM] testo VAD simulato: \"" + sttText + "\"");

    EventBehavior* beh = nullptr;
    for (int i = 0; i < gEventBehaviorCount; i++) {
        EventBehavior& b = gEventBehaviors[i];
        if (b.trigger != trg) continue;
        if (trg == TRG_SENSOR && b.sensor_id != sensorId) continue;
        beh = &b;
        L("[SIM] comportamento trovato: \"" + String(b.name) + "\" (" + String(b.consequence_count) + " conseguenze)");
        break;
    }

    // cattura camera solo se almeno una conseguenza la richiede
    bool needsCam = false;
    if (beh) { for (int c = 0; c < beh->consequence_count; c++) if (beh->consequences[c].snapshot) { needsCam = true; break; } }
    else needsCam = true; // processStimulusDefault la usa sempre

    String base64Img;
    if (needsCam) {
        if (camActive || camInit()) {
            camTouch();
            camApplySettings(camSnapSize, camSnapQuality);
            camera_fb_t* fb = esp_camera_fb_get();
            if (fb) {
                base64Img = base64Encode(fb->buf, fb->len);
                esp_camera_fb_return(fb);
                L("[SIM] camera: frame catturato (" + String(base64Img.length()) + " char base64)");
            } else {
                L("[SIM] camera: esp_camera_fb_get() NULL, procedo senza immagine");
            }
            camApplySettings(camStreamSize, camStreamQuality);
        } else {
            L("[SIM] camera: non disponibile");
        }
    } else {
        L("[SIM] camera: non richiesta da questo behavior");
    }

    gSimLog = &log;

    if (!beh) {
        L("[SIM] nessun comportamento specifico - uso processStimulusDefault");
        if (!skipLlm) processStimulusDefault(base64Img);
        else L("[SIM] processStimulusDefault SKIPPATO (skip-LLM)");
    } else {
        for (int c = 0; c < beh->consequence_count; c++) {
            const Consequence& csq = beh->consequences[c];
            bool isLlm = (csq.type == CSQ_PROMPT_LLM || csq.type == CSQ_PROMPT_FIXED || csq.type == CSQ_PROMPT_AUTO);
            L("[SIM] conseguenza " + String(c+1) + "/" + String(beh->consequence_count)
              + " tipo=" + String(csq.type)
              + (csq.action_id[0] ? String(" action=") + csq.action_id : "")
              + (csq.text[0]      ? String(" testo=\"") + csq.text + "\"" : "")
              + (csq.snapshot     ? " [snapshot]" : ""));
            if (isLlm && skipLlm) {
                String simMsg = sttText.length() > 0 ? sttText : String(csq.text);
                if (simMsg.length() == 0) simMsg = "Reagisci allo stimolo ricevuto.";
                if (csq.ctx_beh_name && beh->name[0])
                    simMsg = "[Regola attiva: \"" + String(beh->name) + "\"] " + simMsg;
                if (csq.ctx_sensor && sensorId > 0 && sensorId < SEN_COUNT) {
                    const char* sname = (gPersonalityLang == "en") ? SENSOR_NAMES_EN[sensorId] : SENSOR_NAMES[sensorId];
                    simMsg = "[Sensore: " + String(sname) + "] " + simMsg;
                }
                L("[SIM] → LLM SKIPPATO - userMsg sarebbe: \"" + simMsg + "\"");
            }
            else
                executeConsequence(csq, base64Img, sttText, beh->name, sensorId);
        }
    }

    gSimLog = nullptr;

    if (ownDebugPers) { free(debugPers); debugPers = nullptr; }

    gPersonalityPrompt  = savedPrompt;
    gPersonalityVoiceId = savedVoiceId;
    gEventBehaviorCount = savedBehCount;
    for (int i = 0; i < savedBehCount; i++) gEventBehaviors[i] = savedBehaviors[i];
    free(savedBehaviors);
    gDryRun      = prevDry;
    gSimSkipTts  = false;
    gSimSkipBle  = false;
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
