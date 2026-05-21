#include "behaviors.h"
#include "audio.h"
#include "llm.h"
#include "ble_furby.h"
#include "hw.h"

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

void speakText(const String& text) {
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
        case CSQ_PROMPT_FIXED: {
            String img = csq.snapshot ? base64Img : "";
            String userMsg = sttText.length() > 0
                ? "L'utente ha detto: \"" + sttText + "\". " + String(csq.text)
                : String(csq.text);
            String answer = callLLM(img, gPersonalityPrompt, userMsg);
            if (answer.length() > 0) speakText(answer);
            else Serial.println("[CSQ] ERRORE: LLM risposta vuota");
            break;
        }
        case CSQ_PROMPT_LLM: {
            String img = csq.snapshot ? base64Img : "";
            String userMsg = sttText.length() > 0 ? sttText : String(csq.text);
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
                    if (act) { furbyWrite(act->cmd, act->len); delay(1500); }
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
