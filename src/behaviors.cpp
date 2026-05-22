#include "behaviors.h"
#include "audio.h"
#include "llm.h"
#include "ble_furby.h"
#include "hw.h"

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
        co["type"]      = (int)q.type;
        co["action_id"] = q.action_id;
        co["text"]      = q.text;
        co["snapshot"]  = q.snapshot;
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
        q.snapshot = co["snapshot"] | false;
        q.reaction_count = 0;
        for (JsonVariant rv : co["reactions"].as<JsonArray>()) {
            if (q.reaction_count >= MAX_REACTIONS) break;
            const char* s = rv.as<const char*>();
            strlcpy(q.reactions[q.reaction_count++], s ? s : "", 32);
        }
    }
}

// ── Persistenza multi-personality ─────────────────────────────────────────────
void savePersonalities() {
    JsonDocument doc;
    doc["active"] = gActivePersonality;
    JsonArray arr = doc["personalities"].to<JsonArray>();
    for (int i = 0; i < gPersonalityCount; i++) {
        Personality& p = gPersonalities[i];
        JsonObject po = arr.add<JsonObject>();
        po["id"]       = p.id;
        po["name"]     = p.name;
        po["prompt"]   = p.prompt;
        po["voice_id"] = p.voice_id;
        JsonArray ba = po["behaviors"].to<JsonArray>();
        for (int b = 0; b < p.behavior_count; b++) {
            JsonObject bo = ba.add<JsonObject>();
            serializeBehavior(bo, p.behaviors[b]);
        }
    }
    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/personalities.json", "w");
    if (f) { f.print(out); f.close(); }
}

static void buildDefaultPersonality(Personality& p) {
    strlcpy(p.id,       "default", sizeof(p.id));
    strlcpy(p.name,     "Default", sizeof(p.name));
    strlcpy(p.prompt,   "Sei un giocattolo cinico, volgare e scontroso. Genera frasi BREVISSIME (max 8 parole). Sii diretto e sgarbato.", sizeof(p.prompt));
    strlcpy(p.voice_id, elevenlabs_voice_id.c_str(), sizeof(p.voice_id));
    EventBehavior& b = p.behaviors[0];
    strlcpy(b.id,   "vad_default", sizeof(b.id));
    b.trigger   = TRG_VAD;
    b.sensor_id = 0;
    strlcpy(b.name, "Parlato rilevato (VAD)", sizeof(b.name));
    b.consequences[0].type     = CSQ_PROMPT_LLM;
    b.consequences[0].snapshot = true;
    strlcpy(b.consequences[0].text, "Qualcuno ti sta parlando. Reagisci.", sizeof(b.consequences[0].text));
    b.consequence_count = 1;
    p.behavior_count = 1;
}

void loadPersonalities() {
    gPersonalityCount  = 0;
    gActivePersonality = 0;

    // migrazione da /behaviors.json (formato vecchio)
    bool migrateFromOld = false;
    if (!SPIFFS.exists("/personalities.json") && SPIFFS.exists("/behaviors.json")) {
        migrateFromOld = true;
    }

    if (!SPIFFS.exists("/personalities.json") && !migrateFromOld) {
        buildDefaultPersonality(gPersonalities[0]);
        gPersonalityCount = 1;
        savePersonalities();
        applyActivePersonality();
        return;
    }

    const char* src = migrateFromOld ? "/behaviors.json" : "/personalities.json";
    File f = SPIFFS.open(src, "r");
    if (!f) {
        buildDefaultPersonality(gPersonalities[0]);
        gPersonalityCount = 1;
        applyActivePersonality();
        return;
    }
    String raw = f.readString(); f.close();
    JsonDocument doc;
    if (deserializeJson(doc, raw) != DeserializationError::Ok) {
        buildDefaultPersonality(gPersonalities[0]);
        gPersonalityCount = 1;
        applyActivePersonality();
        return;
    }

    if (migrateFromOld) {
        // vecchio formato: {"personality":{...}, "behaviors":[...]}
        Personality& p = gPersonalities[0];
        buildDefaultPersonality(p);
        if (doc["personality"].is<JsonObject>()) {
            strlcpy(p.prompt,   doc["personality"]["prompt"]   | p.prompt,   sizeof(p.prompt));
            strlcpy(p.voice_id, doc["personality"]["voice_id"] | p.voice_id, sizeof(p.voice_id));
        }
        p.behavior_count = 0;
        for (JsonObject bo : doc["behaviors"].as<JsonArray>()) {
            if (p.behavior_count >= MAX_EVENT_BEHAVIORS) break;
            deserializeBehavior(p.behaviors[p.behavior_count++], bo);
        }
        gPersonalityCount  = 1;
        gActivePersonality = 0;
        savePersonalities();
    } else {
        gActivePersonality = doc["active"] | 0;
        for (JsonObject po : doc["personalities"].as<JsonArray>()) {
            if (gPersonalityCount >= MAX_PERSONALITIES) break;
            Personality& p = gPersonalities[gPersonalityCount++];
            memset(&p, 0, sizeof(p));
            strlcpy(p.id,       po["id"]       | "", sizeof(p.id));
            strlcpy(p.name,     po["name"]      | "", sizeof(p.name));
            strlcpy(p.prompt,   po["prompt"]    | "", sizeof(p.prompt));
            strlcpy(p.voice_id, po["voice_id"]  | "", sizeof(p.voice_id));
            p.behavior_count = 0;
            for (JsonObject bo : po["behaviors"].as<JsonArray>()) {
                if (p.behavior_count >= MAX_EVENT_BEHAVIORS) break;
                deserializeBehavior(p.behaviors[p.behavior_count++], bo);
            }
        }
    }

    if (gPersonalityCount == 0) {
        buildDefaultPersonality(gPersonalities[0]);
        gPersonalityCount = 1;
    }
    if (gActivePersonality >= gPersonalityCount) gActivePersonality = 0;
    applyActivePersonality();
}

void applyActivePersonality() {
    if (gPersonalityCount == 0) return;
    Personality& p = gPersonalities[gActivePersonality];
    gPersonalityPrompt  = String(p.prompt);
    gPersonalityVoiceId = String(p.voice_id);
    gEventBehaviorCount = p.behavior_count;
    for (int i = 0; i < p.behavior_count; i++)
        gEventBehaviors[i] = p.behaviors[i];
}

// ── Legacy compat ─────────────────────────────────────────────────────────────
void saveEventBehaviors() {
    if (gPersonalityCount == 0) return;
    Personality& p = gPersonalities[gActivePersonality];
    strlcpy(p.prompt,   gPersonalityPrompt.c_str(),  sizeof(p.prompt));
    strlcpy(p.voice_id, gPersonalityVoiceId.c_str(), sizeof(p.voice_id));
    p.behavior_count = gEventBehaviorCount;
    for (int i = 0; i < gEventBehaviorCount; i++)
        p.behaviors[i] = gEventBehaviors[i];
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
    Serial.printf("[SPEAK] \"%s\" (SD=%s)\n", text.c_str(), sdAvailable ? "si" : "no");
    if (sdAvailable) generateAndPlayTTS_SD(text);
    else             streamAndPlayTTS_RAM(text);
}

// ── Esecuzione conseguenza ────────────────────────────────────────────────────
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText) {
    Serial.printf("[CSQ] tipo=%d snapshot=%d testo=\"%s\"\n", csq.type, csq.snapshot, csq.text);
    switch (csq.type) {
        case CSQ_FURBY_ACTION: {
            const FurbyActionDef* act = findFurbyAction(csq.action_id);
            if (act) {
                if (gDryRun) {
                    Serial.printf("[CSQ] DRY RUN — azione Furby SKIPPATA: %s\n", act->label);
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
                String speechBefore, speechAfter, actionId;
                JsonDocument rdoc;
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
                        if (gDryRun) Serial.printf("[CSQ] DRY RUN — azione Furby: %s\n", act->label);
                        else { furbyWrite(act->cmd, act->len); delay(1500); }
                    }
                }
                if (speechAfter.length() > 0 && speechAfter != "null") speakText(speechAfter);
            }
            break;
        }
        case CSQ_PROMPT_AUTO: {
            // LLM sceglie UNA azione dalla lista completa FURBY_ACTIONS
            String img = csq.snapshot ? base64Img : "";
            String userMsg = sttText.length() > 0 ? sttText : String(csq.text);
            if (userMsg.length() == 0) userMsg = "Reagisci allo stimolo ricevuto.";
            String actionList;
            for (int i = 0; i < FURBY_ACTIONS_COUNT; i++)
                actionList += String(FURBY_ACTIONS[i].id) + ": " + FURBY_ACTIONS[i].label + "\n";
            String sys = gPersonalityPrompt +
                " Scegli UNA SOLA azione da questo elenco e rispondi con SOLO il suo id, nient'altro:\n" + actionList;
            String answer = callLLM(img, sys, userMsg);
            answer.trim();
            const FurbyActionDef* act = findFurbyAction(answer.c_str());
            if (act) {
                Serial.printf("[CSQ] PROMPT_AUTO → azione scelta: %s (%s)\n", act->id, act->label);
                if (gDryRun) Serial.printf("[CSQ] DRY RUN — azione Furby SKIPPATA: %s\n", act->label);
                else { furbyWrite(act->cmd, act->len); delay(1500); }
            } else {
                Serial.printf("[CSQ] PROMPT_AUTO — azione LLM non valida: \"%s\"\n", answer.c_str());
            }
            break;
        }
        default:
            Serial.printf("[CSQ] tipo sconosciuto: %d\n", csq.type);
            break;
    }
}

// ── Simulazione (debug, dry-run opzionale, skip LLM opzionale) ───────────────
String processStimulusSimulated(TriggerType trg, uint8_t sensorId, const String& vadText, bool skipLlm, int personalityIdx) {
    String log;
    auto L = [&](const String& s){ log += s + "\n"; Serial.println(s); };

    bool prevDry = gDryRun;
    gDryRun = true;

    // personalità da debuggare (può differire da quella attiva)
    String savedPrompt  = gPersonalityPrompt;
    String savedVoiceId = gPersonalityVoiceId;
    int    savedBehCount = gEventBehaviorCount;
    EventBehavior savedBehaviors[MAX_EVENT_BEHAVIORS];
    for (int i = 0; i < gEventBehaviorCount; i++) savedBehaviors[i] = gEventBehaviors[i];

    if (personalityIdx >= 0 && personalityIdx < gPersonalityCount && personalityIdx != gActivePersonality) {
        Personality& dp = gPersonalities[personalityIdx];
        gPersonalityPrompt  = String(dp.prompt);
        gPersonalityVoiceId = String(dp.voice_id);
        gEventBehaviorCount = dp.behavior_count;
        for (int i = 0; i < dp.behavior_count; i++) gEventBehaviors[i] = dp.behaviors[i];
        L("[SIM] personalità debug: \"" + String(dp.name) + "\"");
    } else {
        L("[SIM] personalità: \"" + String(gPersonalities[gActivePersonality].name) + "\" (attiva)");
    }

    if (skipLlm) L("[SIM] modalità skip-LLM attiva — chiamate LLM simulate");

    L("[SIM] trigger=" + String(trg) + " sensorId=" + String(sensorId)
      + " behaviors=" + String(gEventBehaviorCount));

    String sttText = vadText;
    if (sttText.length() > 0)
        L("[SIM] testo VAD simulato: \"" + sttText + "\"");

    String base64Img;
    if (camActive || camInit()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
            base64Img = base64Encode(fb->buf, fb->len);
            esp_camera_fb_return(fb);
            L("[SIM] camera: frame catturato (" + String(base64Img.length()) + " char base64)");
        } else {
            L("[SIM] camera: esp_camera_fb_get() NULL, procedo senza immagine");
        }
    } else {
        L("[SIM] camera: non disponibile");
    }

    EventBehavior* beh = nullptr;
    for (int i = 0; i < gEventBehaviorCount; i++) {
        EventBehavior& b = gEventBehaviors[i];
        if (b.trigger != trg) continue;
        if (trg == TRG_SENSOR && b.sensor_id != sensorId) continue;
        beh = &b;
        L("[SIM] comportamento trovato: \"" + String(b.name) + "\" (" + String(b.consequence_count) + " conseguenze)");
        break;
    }

    if (!beh) {
        L("[SIM] nessun comportamento specifico — uso processStimulusDefault");
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
              + (csq.snapshot     ? " [📷snapshot]" : ""));
            if (isLlm && skipLlm) {
                L("[SIM] → LLM SKIPPATO — prompt sarebbe: \"" + String(csq.text) + "\"");
            } else {
                executeConsequence(csq, base64Img, sttText);
            }
        }
    }

    // ripristina stato
    gPersonalityPrompt  = savedPrompt;
    gPersonalityVoiceId = savedVoiceId;
    gEventBehaviorCount = savedBehCount;
    for (int i = 0; i < savedBehCount; i++) gEventBehaviors[i] = savedBehaviors[i];
    gDryRun = prevDry;
    return log;
}

void processStimulusDefault(const String& base64Img) {
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
        Serial.println("[STIMULUS] skip — Furby non connesso e dry run disattivo");
        return;
    }
    Serial.printf("[STIMULUS] trigger=%d sensorId=%d behaviors=%d heap=%u PSRAM=%u\n",
        trg, sensorId, gEventBehaviorCount, esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    String sttText;
    if (trg == TRG_VAD && sttEnabled && gSttLen > 0) {
        sttText = transcribeAudio();
        gSttLen = 0;
    }

    String base64Img;
    if (camActive || camInit()) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb) {
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
        processStimulusDefault(base64Img);
        return;
    }
    for (int c = 0; c < beh->consequence_count; c++)
        executeConsequence(beh->consequences[c], base64Img, sttText);
}
