#include "config_backup.h"
#include "globals.h"
#include "hw.h"
#include <sys/time.h>

#define BACKUP_PATH_SPIFFS "/config_bak.json"
#define BACKUP_PATH_SD     "/config_bak.json"

static void _writeBackupTo(File& f) {
    JsonDocument doc;
    doc["llm_prov"]        = llm_provider;
    doc["llm_model"]       = llm_model;
    doc["openai"]          = openai_api_key;
    doc["claude"]          = claude_api_key;
    doc["11labs"]          = elevenlabs_api_key;
    doc["11labs_vid"]      = elevenlabs_voice_id;
    doc["11labs_fmt"]      = el_audio_fmt;
    doc["ble_svc"]         = ble_service_uuid;
    doc["ble_char"]        = ble_char_uuid_tx;
    doc["vad_thr"]         = vad_threshold;
    doc["mic_gain"]        = mic_gain;
    doc["vad_en"]          = vadEnabled;
    doc["stt_en"]          = sttEnabled;
    doc["cam_desc_prompt"] = gCamDescPrompt;
    doc["cam_stq"]         = camStreamQuality;
    doc["cam_sts"]         = (int)camStreamSize;
    doc["cam_snq"]         = camSnapQuality;
    doc["cam_sns"]         = (int)camSnapSize;
    doc["cam_flk"]         = camFlicker;
    doc["cam_gc"]          = camGainCeiling;
    doc["cam_br"]          = camBrightness;
    doc["cam_agc"]         = camAgc;
    // WiFi nets
    JsonArray nets = doc["wifi_nets"].to<JsonArray>();
    for (int i = 0; i < wifiNetCount; i++) {
        JsonObject n = nets.add<JsonObject>();
        n["ssid"] = wifiNets[i].ssid;
        n["pass"] = wifiNets[i].pass;
    }
    doc["wifi_count"] = wifiNetCount;
    time_t now = time(nullptr);
    if (now > 1000000000) doc["last_ts"] = (long)now;
    serializeJson(doc, f);
}

static bool _restoreFromFile(File& f) {
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) return false;
    bool any = false;
    Preferences p; p.begin("furby", false);
    auto _s = [&](const char* k, String& v, const char* def="") {
        if (doc[k].is<const char*>()) { v = doc[k].as<String>(); p.putString(k, v); any = true; }
        else v = def;
    };
    auto _i = [&](const char* k, int& v, int def=0) {
        if (!doc[k].isNull()) { v = doc[k].as<int>(); p.putInt(k, v); any = true; }
        else v = def;
    };
    auto _b = [&](const char* k, bool& v, bool def=false) {
        if (!doc[k].isNull()) { v = doc[k].as<bool>(); p.putBool(k, v); any = true; }
        else v = def;
    };
    _s("llm_prov",        llm_provider,        "openai");
    _s("llm_model",       llm_model,            "gpt-4o-mini");
    _s("openai",          openai_api_key);
    _s("claude",          claude_api_key);
    _s("11labs",          elevenlabs_api_key);
    _s("11labs_vid",      elevenlabs_voice_id, "pNInz6obpgDQGcFmaJcg");
    _s("11labs_fmt",      el_audio_fmt,        "pcm");
    _s("ble_svc",         ble_service_uuid,    BLE_SVC_DEFAULT);
    _s("ble_char",        ble_char_uuid_tx,    BLE_CHAR_DEFAULT);
    _s("cam_desc_prompt", gCamDescPrompt,
        "Sei un Furby maleducato e cinico. Descrivi in modo sintetico e sgarbato quello che vedi nell'immagine.");
    _i("vad_thr",  vad_threshold, VAD_THRESHOLD_DEFAULT);
    _i("mic_gain", mic_gain,      10);
    _b("vad_en",   vadEnabled,    true);
    _b("stt_en",   sttEnabled,    false);
    _i("cam_stq",  camStreamQuality, 12);
    _i("cam_sts",  (int&)camStreamSize, (int)FRAMESIZE_QVGA);
    _i("cam_snq",  camSnapQuality, 8);
    _i("cam_sns",  (int&)camSnapSize, (int)FRAMESIZE_VGA);
    _i("cam_flk",  camFlicker,    0);
    _i("cam_gc",   camGainCeiling, 0);
    _i("cam_br",   camBrightness,  0);
    _i("cam_agc",  camAgc,         1);
    // Timestamp: ripristina orologio se più recente di quello attuale
    if (!doc["last_ts"].isNull()) {
        long saved_ts = doc["last_ts"].as<long>();
        if (saved_ts > 1000000000) {
            time_t cur = time(nullptr);
            if (cur < 1000000000) {
                struct timeval tv = { .tv_sec = (time_t)saved_ts, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                Serial.printf("[CFG] ora ripristinata da backup: %ld\n", saved_ts);
            }
        }
    }
    // WiFi
    JsonArray nets = doc["wifi_nets"].as<JsonArray>();
    if (nets) {
        wifiNetCount = 0;
        for (JsonObject n : nets) {
            if (wifiNetCount >= MAX_WIFI_NETS) break;
            wifiNets[wifiNetCount].ssid = n["ssid"].as<String>();
            wifiNets[wifiNetCount].pass = n["pass"].as<String>();
            wifiNetCount++;
        }
        p.putInt("wifi_count", wifiNetCount);
        for (int i = 0; i < wifiNetCount; i++) {
            p.putString(("ws" + String(i)).c_str(), wifiNets[i].ssid);
            p.putString(("wp" + String(i)).c_str(), wifiNets[i].pass);
        }
        if (wifiNetCount > 0) any = true;
    }
    p.end();
    return any;
}

void saveConfigBackup() {
    // SPIFFS
    File f = SPIFFS.open(BACKUP_PATH_SPIFFS, "w");
    if (f) { _writeBackupTo(f); f.close(); Serial.println("[CFG] backup su SPIFFS OK"); }
    else    Serial.println("[CFG] backup SPIFFS: open fallito");
    // SD (solo se presente)
    if (sdAvailable) {
        File fs = SD_MMC.open(BACKUP_PATH_SD, "w");
        if (fs) { _writeBackupTo(fs); fs.close(); Serial.println("[CFG] backup su SD OK"); }
        else     Serial.println("[CFG] backup SD: open fallito");
    }
}

bool loadConfigBackupIfNeeded(bool nvsWasEmpty) {
    if (!nvsWasEmpty) return false;
    Serial.println("[CFG] NVS vuoto - tentativo ripristino backup...");
    // Prova SPIFFS
    if (SPIFFS.exists(BACKUP_PATH_SPIFFS)) {
        File f = SPIFFS.open(BACKUP_PATH_SPIFFS, "r");
        if (f) {
            bool ok = _restoreFromFile(f);
            f.close();
            if (ok) { Serial.println("[CFG] ripristino da SPIFFS OK"); return true; }
            Serial.println("[CFG] backup SPIFFS corrotto");
        }
    }
    // Fallback SD
    if (sdAvailable && SD_MMC.exists(BACKUP_PATH_SD)) {
        File f = SD_MMC.open(BACKUP_PATH_SD, "r");
        if (f) {
            bool ok = _restoreFromFile(f);
            f.close();
            if (ok) { Serial.println("[CFG] ripristino da SD OK"); return true; }
            Serial.println("[CFG] backup SD corrotto");
        }
    }
    Serial.println("[CFG] nessun backup valido trovato");
    return false;
}
