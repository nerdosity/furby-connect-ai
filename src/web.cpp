#include "web.h"
#include "hw.h"
#include "audio.h"
#include "llm.h"
#include "behaviors.h"
#include "ble_furby.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static void nvsPut(const char* key, const String& val) {
    Preferences p;
    if (!p.begin("furby", false)) { Serial.printf("[NVS] begin fallito per key=%s\n", key); return; }
    size_t written = p.putString(key, val);
    if (written == 0) Serial.printf("[NVS] putString fallito key=%s len=%u\n", key, val.length());
    p.end();
}

static void httpLog() {
    String uri = server.uri();
    // skippa polling frequente e risorse statiche
    if (uri == "/sys/info" || uri == "/debug/mic/rms" || uri == "/debug/sensors" ||
        uri == "/ble/status" || uri.endsWith(".map")) return;
    String args;
    for (int i = 0; i < server.args(); i++) {
        const String& n = server.argName(i);
        if (n == "api_key" || n == "el_key" || n == "pass" || n == "openai_key" || n == "claude_key")
            args += " " + n + "=[***]";
        else
            args += " " + n + "=" + server.arg(i).substring(0, 60);
    }
    Serial.printf("[HTTP] %s %s%s\n",
        server.method() == HTTP_POST ? "POST" : "GET",
        uri.c_str(), args.c_str());
}
#define HTTP_LOG() httpLog()

static String jsonEscape(const String& s) {
    String o; o.reserve(s.length() + 16);
    for (char c : s) {
        if      (c == '"')  o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else                o += c;
    }
    return o;
}

static String spiffsVersion() {
    File f = SPIFFS.open("/version.txt", "r");
    if (!f) return FW_VERSION;
    String v = f.readStringUntil('\n');
    f.close();
    v.trim();
    return v.length() ? v : FW_VERSION;
}

static void serveSpiffs(const char* path, const char* mime, const char* cache) {
    File f = SPIFFS.open(path, "r");
    if (!f) { server.send(404); return; }
    server.sendHeader("Cache-Control", cache);
    server.streamFile(f, mime);
    f.close();
}

static void serveSpiffsETag(const char* path, const char* mime, const char* cache) {
    String etag = "\"" + spiffsVersion() + "\"";
    if (server.header("If-None-Match") == etag) { server.send(304); return; }
    File f = SPIFFS.open(path, "r");
    if (!f) { server.send(404); return; }
    server.sendHeader("ETag", etag);
    server.sendHeader("Cache-Control", cache);
    server.streamFile(f, mime);
    f.close();
}

// ES8311 REG32: 0x00=mute, step 0.5dB/LSB. Maps 1-100% → -30..+6dB.
static void setVolume(int pct) {
    pct = constrain(pct, 0, 100);
    if (pct == 0) { es8311WriteReg(ES8311_DAC_REG32, 0x00); return; }
    float db  = -30.0f + (pct * 36.0f / 100.0f);
    int   reg = constrain((int)((db + 96.0f) * 2.0f), 1, 255);
    es8311WriteReg(ES8311_DAC_REG32, (uint8_t)reg);
    Serial.printf("Volume: %d%% (%.1fdB reg=0x%02X)\n", pct, db, reg);
}

// ── WiFi connect async state ──────────────────────────────────────────────────
// 0=idle, 1=in corso, 2=ok, 3=fallita
static volatile int wifiConnectState = 0;
static String wifiConnectSSID;
static String wifiConnectIP;

static void wifiConnectTask(void*) {
    if (tryConnectWifi(true)) {
        wifiConnectSSID  = WiFi.SSID();
        wifiConnectIP    = WiFi.localIP().toString();
        wifiConnectState = 2;
        vTaskDelay(2000 / portTICK_PERIOD_MS); // dai tempo al browser di ricevere la risposta
        esp_restart();
    } else {
        wifiConnectState = 3;
    }
    vTaskDelete(NULL);
}

// ── Captive portal helpers ────────────────────────────────────────────────────

static void handleCaptiveRedirect() {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
}

static void handleCaptiveIos() {
    if (!isConfigMode) { server.send(200, "text/plain", ""); return; }
    handleCaptiveRedirect();
}

static void handleCaptiveWindows() {
    if (!isConfigMode) { server.send(200, "text/plain", "Microsoft Connect Test"); return; }
    handleCaptiveRedirect();
}

static void handleCaptiveAndroid() {
    if (!isConfigMode) { server.send(204, "text/plain", ""); return; }
    // 302 verso l'IP del portal — Samsung apre il browser se riceve un non-204
    server.sendHeader("Cache-Control", "no-store");
    handleCaptiveRedirect();
}

// ── Route handlers ────────────────────────────────────────────────────────────

static void handleRoot() {
    File f = SPIFFS.open("/index.html", "r");
    if (!f) { server.send(503, "text/plain", "index.html non trovato — eseguire uploadfs"); return; }
    server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    server.streamFile(f, "text/html; charset=utf-8");
    f.close();
}

static void handleWifiConnect() {
    HTTP_LOG();
    if (wifiNetCount == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"no_nets\"}"); return; }
    wifiConnectState = 1;
    xTaskCreatePinnedToCore(wifiConnectTask, "wifiConn", 4096, NULL, 1, NULL, 1);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiConnectStatus() {
    int s = wifiConnectState;
    JsonDocument doc;
    doc["state"] = s;
    if (s == 2) { doc["ip"] = wifiConnectIP; doc["ssid"] = wifiConnectSSID; }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleApiWifiScan() {
    HTTP_LOG();
    int n = WiFi.scanNetworks(false, true);
    JsonDocument doc;
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
        JsonObject net = nets.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    WiFi.scanDelete();
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleWifiAdd() {
    HTTP_LOG();
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    int    prio = server.arg("prio").toInt();
    if (ssid.length() > 0) upsertWifiNet(ssid, pass, prio);
    server.sendHeader("Location", "/"); server.send(303);
}

static void handleWifiDel() {
    HTTP_LOG();
    removeWifiNet(server.arg("idx").toInt());
    server.sendHeader("Location", "/"); server.send(303);
}

static void handleWifiUp() {
    HTTP_LOG();
    int idx = server.arg("idx").toInt();
    if (idx > 0) {
        WifiNet tmp = wifiNets[idx];
        wifiNets[idx] = wifiNets[idx-1];
        wifiNets[idx-1] = tmp;
        saveWifiNets();
    }
    server.sendHeader("Location", "/"); server.send(303);
}

static void handleWifiDown() {
    HTTP_LOG();
    int idx = server.arg("idx").toInt();
    if (idx < wifiNetCount - 1) {
        WifiNet tmp = wifiNets[idx];
        wifiNets[idx] = wifiNets[idx+1];
        wifiNets[idx+1] = tmp;
        saveWifiNets();
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// Extracts all "id" values from a JSON body into arr (lightweight, no heap)
static void extractJsonIds(const String& body, JsonArray arr) {
    int pos = 0;
    while (true) {
        int idx  = body.indexOf("\"id\":\"",  pos);
        int idx2 = body.indexOf("\"id\": \"", pos);
        if (idx < 0 && idx2 < 0) break;
        int start;
        if      (idx < 0)    start = idx2 + 7;
        else if (idx2 < 0)   start = idx  + 6;
        else if (idx2 < idx) start = idx2 + 7;
        else                 start = idx  + 6;
        int end = body.indexOf("\"", start);
        if (end < 0) break;
        arr.add(body.substring(start, end));
        pos = end + 1;
    }
}

static void handleApiModels() {
    String provider = server.arg("provider");
    if (provider.length() == 0) provider = llm_provider;

    if (openai_api_key.length() == 0) { Preferences p; p.begin("furby",true); openai_api_key = p.getString("openai",""); p.end(); }
    if (claude_api_key.length() == 0) { Preferences p; p.begin("furby",true); claude_api_key  = p.getString("claude",""); p.end(); }

    const String& key = (provider == "claude") ? claude_api_key : openai_api_key;
    if (key.length() == 0) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"no_key\"}"); return;
    }

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    server.sendHeader("Cache-Control", "no-store");

    JsonDocument doc;
    JsonArray models = doc["models"].to<JsonArray>();

    if (provider == "openai") {
        http.begin(client, "https://api.openai.com/v1/models");
        http.addHeader("Authorization", "Bearer " + openai_api_key);
        int code = http.GET();
        if (code == 200) {
            String body = http.getString();
            JsonDocument tmp;
            JsonArray all = tmp["a"].to<JsonArray>();
            extractJsonIds(body, all);
            for (JsonVariant v : all) {
                String id = v.as<String>();
                bool isText = id.startsWith("gpt-") || id.startsWith("o1") ||
                              id.startsWith("o3")   || id.startsWith("o4") ||
                              id.startsWith("chatgpt-");
                bool isExcl = id.indexOf("embed") >= 0 || id.indexOf("tts") >= 0 ||
                              id.indexOf("dall-e") >= 0 || id.indexOf("whisper") >= 0 ||
                              id.indexOf("babbage") >= 0 || id.indexOf("davinci") >= 0 ||
                              id.indexOf("ada") >= 0;
                if (isText && !isExcl) models.add(id);
            }
        }
    } else if (provider == "claude") {
        http.begin(client, "https://api.anthropic.com/v1/models");
        http.addHeader("x-api-key", claude_api_key);
        http.addHeader("anthropic-version", "2023-06-01");
        if (http.GET() == 200) extractJsonIds(http.getString(), models);
    }
    http.end();

    doc["ok"] = true;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleApiConfig() {
    HTTP_LOG();
    bool changed = false;
    if (server.hasArg("openai_key") && server.arg("openai_key").length()) {
        openai_api_key = server.arg("openai_key");
        nvsPut("openai", openai_api_key);
        changed = true;
    }
    if (server.hasArg("claude_key") && server.arg("claude_key").length()) {
        claude_api_key = server.arg("claude_key");
        nvsPut("claude", claude_api_key);
        changed = true;
    }
    if (server.hasArg("el_key") && server.arg("el_key").length()) {
        elevenlabs_api_key = server.arg("el_key");
        nvsPut("11labs", elevenlabs_api_key);
        changed = true;
    }
    if (server.hasArg("el_vid") && server.arg("el_vid").length()) {
        elevenlabs_voice_id = server.arg("el_vid");
        nvsPut("11labs_vid", elevenlabs_voice_id);
        changed = true;
    }
    if (server.hasArg("provider")) {
        String p = server.arg("provider");
        if (p == "openai" || p == "claude") {
            llm_provider = p;
            nvsPut("llm_prov", llm_provider);
            changed = true;
        }
    }
    if (server.hasArg("model") && server.arg("model").length()) {
        llm_model = server.arg("model");
        nvsPut("llm_model", llm_model);
        changed = true;
    }
    if (server.hasArg("ssid") && server.arg("ssid").length()) {
        upsertWifiNet(server.arg("ssid"), server.arg("pass"), 0);
        changed = true;
    }
    server.send(200, "application/json",
        String("{\"ok\":true,\"changed\":") + (changed ? "true" : "false") + "}");
}

static void handleApiTest() {
    HTTP_LOG();
    String type = server.arg("type");
    WiFiClientSecure client; client.setInsecure(); client.setTimeout(15);
    HTTPClient http; http.setReuse(false);
    bool ok = false; String info, err;

    if (type == "llm") {
        if (llm_provider == "openai") {
            if (openai_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
            http.begin(client, "https://api.openai.com/v1/models");
            http.addHeader("Authorization", "Bearer " + openai_api_key);
            int code = http.GET();
            ok = (code == 200);
            info = ok ? "OpenAI OK" : "HTTP " + String(code);
            if (!ok) err = http.getString();
            http.end();
        } else {
            if (claude_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
            http.begin(client, "https://api.anthropic.com/v1/models");
            http.addHeader("x-api-key", claude_api_key);
            http.addHeader("anthropic-version", "2023-06-01");
            int code = http.GET();
            ok = (code == 200);
            info = ok ? "Anthropic OK" : "HTTP " + String(code);
            if (!ok) err = http.getString();
            http.end();
        }
    } else if (type == "el") {
        if (elevenlabs_api_key.length() == 0) { server.send(200,"application/json","{\"ok\":false,\"error\":\"token assente\"}"); return; }
        http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id + "?output_format=" + elOutputFormat());
        http.addHeader("Content-Type", "application/json");
        http.addHeader("xi-api-key", elevenlabs_api_key);
        int code = http.POST("{\"text\":\"ok\",\"model_id\":\"eleven_multilingual_v2\"}");
        ok = (code == 200);
        info = ok ? "ElevenLabs OK (voce: " + elevenlabs_voice_id + ")" : "HTTP " + String(code);
        if (!ok) err = http.getString().substring(0, 300);
        http.end();
    } else {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"type sconosciuto\"}"); return;
    }

    String errJson = err.substring(0, 400);
    info.replace("\"", "\\\""); errJson.replace("\"", "\\\"");
    server.send(200, "application/json",
        "{\"ok\":" + String(ok?"true":"false") +
        ",\"info\":\"" + info + "\"" +
        (errJson.length() ? ",\"error\":\"" + errJson + "\"" : "") + "}");
}

static void handleApiVoices() {
    if (elevenlabs_api_key.length() == 0) { server.send(200, "application/json", "[]"); return; }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/voices");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (http.GET() == 200) {
        String body = http.getString();
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
            JsonObject vo = arr.add<JsonObject>();
            vo["id"]   = vid;
            vo["name"] = body.substring(ni, ne);
            pos = ve + 1;
        }
    }
    http.end();
    String out; serializeJson(doc, out);
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", out);
}

static void handleLlmSave() {
    HTTP_LOG();
    String newProv  = server.arg("provider");
    String newModel = server.arg("model");
    String newKey   = server.arg("api_key");
    if (newProv == "openai" || newProv == "claude") {
        llm_provider = newProv;
        nvsPut("llm_prov", llm_provider);
    }
    if (newKey.length() > 0 && !newKey.startsWith("****")) {
        if (llm_provider == "claude") {
            claude_api_key = newKey;
            nvsPut("claude", claude_api_key);
        } else {
            openai_api_key = newKey;
            nvsPut("openai", openai_api_key);
        }
    }
    // salva il modello solo se c'è una chiave valida in RAM per il provider attivo
    const String& activeKey = (llm_provider == "claude") ? claude_api_key : openai_api_key;
    if (newModel.length() > 0 && activeKey.length() > 0) {
        llm_model = newModel;
        nvsPut("llm_model", llm_model);
    }
    server.sendHeader("Location", "/"); server.send(303);
}

static void handleElSave() {
    HTTP_LOG();
    String newKey = server.arg("el_key");
    String newVid = server.arg("el_vid");
    String newFmt = server.arg("el_fmt");
    if (newKey.length() > 0 && !newKey.startsWith("****")) {
        elevenlabs_api_key = newKey;
        nvsPut("11labs", elevenlabs_api_key);
    }
    // salva la voce solo se c'è una chiave valida in RAM
    if (newVid.length() > 0 && elevenlabs_api_key.length() > 0) {
        elevenlabs_voice_id = newVid;
        nvsPut("11labs_vid", elevenlabs_voice_id);
    }
    if (newFmt == "pcm" || newFmt == "mp3") {
        el_audio_fmt = newFmt;
        nvsPut("11labs_fmt", el_audio_fmt);
    }
    server.sendHeader("Location", "/"); server.send(303);
}

static void handleVadSave() {
    HTTP_LOG();
    Preferences p; p.begin("furby", false);
    String thr = server.arg("threshold");
    if (thr.length() > 0) {
        vad_threshold = constrain(thr.toInt(), 0, 32767);
        p.putInt("vad_thr", vad_threshold);
    }
    String en = server.arg("enabled");
    if (en.length() > 0) {
        vadEnabled = (en == "1");
        p.putBool("vad_en", vadEnabled);
    }
    String stt = server.arg("stt_enabled");
    if (stt.length() > 0) {
        sttEnabled = (stt == "1");
        p.putBool("stt_en", sttEnabled);
    }
    p.end();
    server.send(200, "application/json",
        "{\"ok\":true,\"vad_enabled\":" + String(vadEnabled ? "true" : "false") +
        ",\"stt_enabled\":"             + String(sttEnabled ? "true" : "false") +
        ",\"vad_threshold\":"           + String(vad_threshold) + "}");
}

// ── /personalities/* ─────────────────────────────────────────────────────────

static void handlePersonalitiesList() {
    // legge direttamente dal JSON su SPIFFS — non carica tutto in RAM
    if (!SPIFFS.exists("/personalities.json")) {
        savePersonalities();
    }
    File f = SPIFFS.open("/personalities.json", "r");
    if (!f) { server.send(500, "application/json", "{\"error\":\"file non trovato\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, f) != DeserializationError::Ok) {
        f.close(); server.send(500, "application/json", "{\"error\":\"JSON corrotto\"}"); return;
    }
    f.close();
    doc["active"] = gActivePersonality;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handlePersonalitiesActivate() {
    HTTP_LOG();
    int idx = server.arg("idx").toInt();
    activatePersonality(idx);
    if (gActivePersonality != idx) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePersonalitiesNew() {
    HTTP_LOG();
    String nm = server.arg("name"); nm.trim(); if (nm.length() == 0) nm = "Nuova";

    // legge JSON, aggiunge entry, riscrive
    JsonDocument doc;
    if (SPIFFS.exists("/personalities.json")) {
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }
    JsonArray arr = doc["personalities"].is<JsonArray>()
        ? doc["personalities"].as<JsonArray>()
        : doc["personalities"].to<JsonArray>();

    JsonObject po = arr.add<JsonObject>();
    String id = "p" + String(millis());
    po["id"]       = id;
    po["name"]     = nm;
    po["prompt"]   = gpActivePers ? String(gpActivePers->prompt) : gPersonalityPrompt;
    po["voice_id"] = gpActivePers ? String(gpActivePers->voice_id) : gPersonalityVoiceId;
    po["behaviors"] = JsonArray{};
    int newIdx = (int)arr.size() - 1;

    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/personalities.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(out); f.close();

    server.send(200, "application/json", "{\"ok\":true,\"idx\":" + String(newIdx) + "}");
}

static void handlePersonalitiesDel() {
    HTTP_LOG();
    int idx = server.arg("idx").toInt();

    JsonDocument doc;
    if (SPIFFS.exists("/personalities.json")) {
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }
    JsonArray arr = doc["personalities"].as<JsonArray>();
    if (idx < 0 || idx >= (int)arr.size() || (int)arr.size() <= 1) {
        server.send(400, "application/json", "{\"ok\":false}"); return;
    }
    arr.remove(idx);
    int newActive = gActivePersonality;
    if (newActive >= (int)arr.size()) newActive = (int)arr.size() - 1;
    doc["active"] = newActive;

    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/personalities.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(out); f.close();

    activatePersonality(newActive);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePersonalitiesSave() {
    HTTP_LOG();
    String body = server.arg("plain");
    if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"body vuoto\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON non valido\"}"); return;
    }
    File f = SPIFFS.open("/personalities.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(body); f.close();
    loadPersonalities();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePersonalitiesExport() {
    HTTP_LOG();
    if (!SPIFFS.exists("/personalities.json")) savePersonalities();
    File f = SPIFFS.open("/personalities.json", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=personalities.json");
    server.streamFile(f, "application/json"); f.close();
}

static void handlePersonalitiesImport() {
    HTTP_LOG();
    String body = server.arg("plain");
    if (body.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"body vuoto\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"JSON non valido\"}"); return;
    }
    File f = SPIFFS.open("/personalities.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(body); f.close();
    loadPersonalities();
    server.send(200, "application/json", "{\"ok\":true}");
}

// sensori disponibili
static void handleSensorsList() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 1; i < SEN_COUNT; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]   = i;
        o["name"] = SENSOR_NAMES[i];
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ── /cfg/* — mantenuti per compatibilità, delegano alle nuove funzioni ────────
static void handleCfgList() {
    // rimanda a personalities list per non rompere client esistenti
    handlePersonalitiesList();
}

static void handleCfgActivate() {
    handlePersonalitiesActivate();
}

static void handleCfgRename() {
    HTTP_LOG();
    int idx = server.arg("idx").toInt();
    String nm = server.arg("name"); nm.trim(); if (nm.length() == 0) nm = "Config";

    JsonDocument doc;
    if (SPIFFS.exists("/personalities.json")) {
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) { deserializeJson(doc, f); f.close(); }
    }
    JsonArray arr = doc["personalities"].as<JsonArray>();
    if (idx < 0 || idx >= (int)arr.size()) { server.send(400, "application/json", "{\"ok\":false}"); return; }
    arr[idx]["name"] = nm;
    if (idx == gActivePersonality && gpActivePers)
        strlcpy(gpActivePers->name, nm.c_str(), sizeof(gpActivePers->name));

    String out; serializeJson(doc, out);
    File f = SPIFFS.open("/personalities.json", "w");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    f.print(out); f.close();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleCfgNew() {
    handlePersonalitiesNew();
}

static void handleCfgDel() {
    handlePersonalitiesDel();
}

static void handleCfgRuleSet() {
    server.send(410, "application/json", "{\"ok\":false,\"error\":\"usa /personalities/save\"}");
}

static void handleCfgRuleDel() {
    server.send(410, "application/json", "{\"ok\":false,\"error\":\"usa /personalities/save\"}");
}

static void handleBleScan() {
    HTTP_LOG();
    if (isConfigMode || bleScanning || connected) {
        server.send(200, "application/json", "{\"ok\":false,\"reason\":\"busy\"}"); return;
    }
    bleScanStart(10);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleBleScanStop() {
    HTTP_LOG();
    bleScanStop();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleBleStatus() {
    JsonDocument d;
    d["connected"]  = connected;
    d["scanning"]   = bleScanning;
    d["connecting"] = bleConnecting;
    d["name"]       = ble_last_name;
    d["battery"]    = ble_battery_pct;
    d["uptime"]     = millis() / 1000;
    d["ble_uptime"] = connected ? (millis() - bleConnectedMs) / 1000 : 0;
    JsonArray devs = d["devices"].to<JsonArray>();
    for (int i = 0; i < furbyListCount; i++) {
        JsonObject o = devs.add<JsonObject>();
        o["name"] = furbyList[i].name;
        o["addr"] = furbyList[i].addr;
    }
    String j; serializeJson(d, j);
    server.send(200, "application/json", j);
}

static void handleBleConnect() {
    HTTP_LOG();
    if (connected)     { server.send(200, "application/json", "{\"ok\":true,\"already\":true}"); return; }
    if (bleConnecting) { server.send(200, "application/json", "{\"ok\":false,\"error\":\"connecting\"}"); return; }
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

static void handleBleDisconnect() {
    HTTP_LOG();
    bleDisconnect();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleBleSave() {
    HTTP_LOG();
    String newSvc  = server.arg("svc_uuid");
    String newChar = server.arg("char_uuid");
    if (newSvc.length()  == 36) {
        ble_service_uuid = newSvc;
        nvsPut("ble_svc", ble_service_uuid);
        serviceUUID = BLEUUID(ble_service_uuid.c_str());
    }
    if (newChar.length() == 36) {
        ble_char_uuid_tx = newChar;
        nvsPut("ble_char", ble_char_uuid_tx);
        charUUID_GPWrite = BLEUUID(ble_char_uuid_tx.c_str());
    }
    server.sendHeader("Location", "/"); server.send(303);
}

// svuota solo file .pcm e index.json (cache audio)
static void handleSdFormat() {
    HTTP_LOG();
    if (!sdCheck()) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"SD non disponibile\"}"); return; }
    File root = SD_MMC.open("/");
    File file = root.openNextFile();
    while (file) {
        String name = String("/") + file.name();
        file.close();
        if (name.endsWith(".pcm") || name == "/index.json") SD_MMC.remove(name);
        file = root.openNextFile();
    }
    root.close();
    Preferences prefs; prefs.begin("furby_sys", false);
    prefs.putInt("file_id", 0); prefs.end();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleSdReinit() {
    HTTP_LOG();
    sdUnmount();
    delay(200);
    bool ok = sdMount();
    if (ok) {
        uint8_t t = SD_MMC.cardType();
        const char* ts = (t==CARD_MMC)?"MMC":(t==CARD_SD)?"SDSC":(t==CARD_SDHC)?"SDHC":"UNKNOWN";
        String msg = String("{\"ok\":true,\"type\":\"") + ts
            + "\",\"total_mb\":" + String((int)(SD_MMC.totalBytes() /(1024*1024)))
            + ",\"used_mb\":"    + String((int)(SD_MMC.usedBytes()  /(1024*1024))) + "}";
        server.send(200, "application/json", msg);
    } else {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"card non riconosciuta\"}");
    }
}

static void handleSdUnmount() {
    HTTP_LOG();
    sdUnmount();
    server.send(200, "application/json", "{\"ok\":true}");
}

// formatta la SD in FAT32 (distrugge tutti i dati) — bloccante per design
static void handleSdFormatFAT() {
    HTTP_LOG();
    sdUnmount();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    sdAvailable = SD_MMC.begin("/sdcard", true, true, 4000);
    if (sdAvailable) {
        Preferences prefs; prefs.begin("furby_sys", false);
        prefs.putInt("file_id", 0); prefs.end();
        server.send(200, "application/json", "{\"ok\":true}");
    } else {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"formattazione fallita\"}");
    }
}

static void handleBleAbort() {
    HTTP_LOG();
    bleUserDisconnect = true;
    bleConnecting     = false;
    if (pBleClient && pBleClient->isConnected()) pBleClient->disconnect();
    bleResetState();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleBleReset() {
    HTTP_LOG();
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
        bleInit();
        furbyListCount = 0;
        Serial.println("BLE: stack resettato");
        vTaskDelete(NULL);
    }, "BLEReset", 4096, NULL, 2, NULL, 0);
}

static void handleSysInfo() {
    auto kb = [](size_t b) -> size_t { return b / 1024; };
    size_t sramTotal  = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t sramFree   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psramTotal = psramFound() ? heap_caps_get_total_size(MALLOC_CAP_SPIRAM) : 0;
    size_t psramFree  = psramFound() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM)  : 0;
    size_t sketchKb   = kb(ESP.getSketchSize());
    size_t spiffsKb   = kb(SPIFFS.usedBytes());
    JsonDocument d;
    d["cpu_mhz"]      = getCpuFrequencyMhz();
    d["heap_free"]    = kb(sramFree);
    d["heap_total"]   = kb(sramTotal);
    d["psram_free"]   = kb(psramFree);
    d["psram_total"]  = kb(psramTotal);
    d["spiffs_used"]  = kb(SPIFFS.usedBytes());
    d["spiffs_total"] = kb(SPIFFS.totalBytes());
    d["sketch_used"]  = sketchKb;
    d["sketch_total"] = 0x400000 / 1024;
    d["flash_mb"]     = ESP.getFlashChipSize() / (1024*1024);
    d["flash_used_kb"]= sketchKb + spiffsKb;
    d["flash_free_kb"]= ESP.getFlashChipSize() / 1024 - sketchKb - spiffsKb;
    d["uptime_s"]     = millis() / 1000;
    d["bat_mv"]       = readBatteryMv();
    d["chip_temp_c"]  = (int)temperatureRead();
    d["fw_version"]   = spiffsVersion();
    d["el_key"]       = elevenlabs_api_key;
    d["el_voice_id"]  = elevenlabs_voice_id;
    d["el_fmt"]       = el_audio_fmt;
    if (sdAvailable) {
        d["sd_used_mb"]  = (int)(SD_MMC.usedBytes()  / (1024*1024));
        d["sd_total_mb"] = (int)(SD_MMC.totalBytes() / (1024*1024));
    }
    String j; serializeJson(d, j);
    server.send(200, "application/json", j);
}

static void handleApiHome() {
    sdCheck();
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    JsonDocument doc;
    doc["wifi_connected"] = wifiOk;
    doc["wifi_ssid"]      = wifiOk ? WiFi.SSID() : "";
    doc["ip"]             = isConfigMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    JsonArray nets = doc["wifi_nets"].to<JsonArray>();
    for (int i = 0; i < wifiNetCount; i++) {
        JsonObject net = nets.add<JsonObject>();
        net["ssid"] = wifiNets[i].ssid;
    }
    doc["llm_provider"]   = llm_provider;
    doc["llm_model"]      = llm_model;
    doc["cur_key"]        = (llm_provider == "claude") ? claude_api_key : openai_api_key;
    doc["el_key"]         = elevenlabs_api_key;
    doc["el_vid"]         = elevenlabs_voice_id;
    doc["el_fmt"]         = el_audio_fmt;
    doc["sd_present"]     = sdAvailable;
    doc["sd_used_mb"]     = sdAvailable ? (int)(SD_MMC.usedBytes() / (1024*1024)) : 0;
    doc["sd_total_mb"]    = sdAvailable ? (int)(SD_MMC.totalBytes() / (1024*1024)) : 0;
    doc["vad_enabled"]    = vadEnabled;
    doc["vad_threshold"]  = vad_threshold;
    doc["stt_enabled"]    = sttEnabled;
    doc["ble_svc_uuid"]        = ble_service_uuid;
    doc["ble_char_uuid"]       = ble_char_uuid_tx;
    doc["ble_svc_uuid_default"]  = BLE_SVC_DEFAULT;
    doc["ble_char_uuid_default"] = BLE_CHAR_DEFAULT;
    doc["hostname"]    = WiFi.getHostname();
    doc["static_ip"]   = WiFi.localIP().toString();
    doc["static_gw"]   = WiFi.gatewayIP().toString();
    doc["static_mask"] = WiFi.subnetMask().toString();
    doc["static_dns"]  = WiFi.dnsIP().toString();
    {   Preferences p; p.begin("furby", true);
        doc["static_enabled"] = p.getString("static_ip", "").length() > 0;
        p.end();
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleSysReboot() {
    HTTP_LOG();
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    esp_restart();
}

static void handleWifiHostname() {
    HTTP_LOG();
    String name = server.arg("hostname");
    name.trim();
    if (name.length() == 0 || name.length() > 32) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"hostname non valido\"}"); return;
    }
    nvsPut("hostname", name);
    WiFi.setHostname(name.c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleWifiStatic() {
    HTTP_LOG();
    String ip      = server.arg("ip");
    String gw      = server.arg("gw");
    String mask    = server.arg("mask");
    String dns1    = server.arg("dns1");
    bool   disable = server.arg("disable") == "1";
    if (disable) {
        nvsPut("static_ip", "");
        server.send(200, "application/json", "{\"ok\":true}");
        return;
    }
    IPAddress a, g, m, d;
    if (!a.fromString(ip) || !g.fromString(gw) || !m.fromString(mask)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"IP non valido\"}"); return;
    }
    if (dns1.length() && !d.fromString(dns1)) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"DNS non valido\"}"); return;
    }
    nvsPut("static_ip",   ip);
    nvsPut("static_gw",   gw);
    nvsPut("static_mask", mask);
    nvsPut("static_dns",  dns1.length() ? dns1 : gw);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handlePersonalityGet() {
    JsonDocument doc;
    doc["prompt"]   = gPersonalityPrompt;
    doc["voice_id"] = gPersonalityVoiceId;
    doc["active"]   = gActivePersonality;
    // conta le personality dal JSON senza caricarle in RAM
    int count = 0;
    if (SPIFFS.exists("/personalities.json")) {
        File f = SPIFFS.open("/personalities.json", "r");
        if (f) {
            JsonDocument pd;
            if (deserializeJson(pd, f) == DeserializationError::Ok && pd["personalities"].is<JsonArray>())
                count = (int)pd["personalities"].as<JsonArray>().size();
            f.close();
        }
    }
    doc["count"] = count;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handlePersonalitySave() {
    HTTP_LOG();
    if (server.hasArg("prompt"))   gPersonalityPrompt  = server.arg("prompt");
    if (server.hasArg("voice_id")) gPersonalityVoiceId = server.arg("voice_id");
    saveEventBehaviors();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleBehaviorsGet() {
    if (!SPIFFS.exists("/behaviors.json")) saveEventBehaviors();
    File f = SPIFFS.open("/behaviors.json", "r");
    if (!f) { server.send(500, "application/json", "{\"ok\":false}"); return; }
    server.streamFile(f, "application/json"); f.close();
}

static void handleBehaviorsSave() {
    HTTP_LOG();
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

static void handleBehaviorsExport() {
    HTTP_LOG();
    if (!SPIFFS.exists("/behaviors.json")) saveEventBehaviors();
    File f = SPIFFS.open("/behaviors.json", "r");
    if (!f) { server.send(404, "text/plain", "not found"); return; }
    server.sendHeader("Content-Disposition", "attachment; filename=behaviors.json");
    server.streamFile(f, "application/json"); f.close();
}

static void handleBehaviorsImport() {
    HTTP_LOG();
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

static void handleBehaviorsActions() {
    HTTP_LOG();
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < FURBY_ACTIONS_COUNT; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["id"]    = FURBY_ACTIONS[i].id;
        o["label"] = FURBY_ACTIONS[i].label;
        o["len"]   = FURBY_ACTIONS[i].len;
        JsonArray ba = o["bytes"].to<JsonArray>();
        for (int b = 0; b < FURBY_ACTIONS[i].len; b++) ba.add(FURBY_ACTIONS[i].cmd[b]);
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleTestLlm() {
    HTTP_LOG();
    if (isProcessing) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return; }
    String text = server.arg("text");
    if (text.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"text mancante\"}"); return; }
    String answer = callLLM("", gPersonalityPrompt, text);
    JsonDocument doc; doc["ok"] = true; doc["response"] = answer;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleTestTts() {
    HTTP_LOG();
    if (isProcessing || isSpeaking) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return; }
    String text = server.arg("text");
    if (text.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"text mancante\"}"); return; }
    server.send(200, "application/json", "{\"ok\":true}");
    xTaskCreatePinnedToCore([](void* p) {
        String* t = (String*)p;
        isSpeaking = true;
        sdCheck();
        if (sdAvailable) generateAndPlayTTS_SD(*t);
        else             streamAndPlayTTS_RAM(*t);
        isSpeaking = false;
        delete t;
        vTaskDelete(NULL);
    }, "tts_test", 16384, new String(text), 1, NULL, 1);
}

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

static void handleTestVoices() {
    HTTP_LOG();
    if (elevenlabs_api_key.length() == 0) {
        server.send(200, "application/json", FPSTR(EL_BUILTIN_VOICES)); return;
    }
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/voices");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    int code = http.GET();
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
        server.send(200, "application/json", FPSTR(EL_BUILTIN_VOICES));
    }
}

static void handleTestBehavior() {
    HTTP_LOG();
    if (isProcessing || isSpeaking) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return;
    }
    int trg  = server.arg("trigger").toInt();
    int sid  = server.arg("sensor_id").toInt();
    bool dry = server.arg("dry_run") == "1";
    if (trg < 0 || trg > 2) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"trigger non valido\"}"); return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    struct Args { TriggerType trg; uint8_t sid; bool dry; };
    Args* a = new Args{ (TriggerType)trg, (uint8_t)sid, dry };
    xTaskCreatePinnedToCore([](void* p) {
        Args* a = (Args*)p;
        gDryRun      = a->dry;
        isProcessing = true;
        processStimulus(a->trg, a->sid);
        isProcessing = false;
        gDryRun      = false;
        delete a;
        vTaskDelete(NULL);
    }, "beh_test", 16384, a, 1, NULL, 1);
}

static void handleTestSimulate() {
    HTTP_LOG();
    if (isProcessing || isSpeaking) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"occupato\"}"); return;
    }
    int trg      = server.arg("trigger").toInt();
    int sid      = server.arg("sensor_id").toInt();
    int persIdx  = server.hasArg("personality") ? server.arg("personality").toInt() : -1;
    bool skipLlm = server.arg("skip_llm") == "1";
    String vadText = server.arg("vad_text");
    if (trg < 0 || trg > 2) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"trigger non valido\"}"); return;
    }
    isProcessing = true;
    String simLog = processStimulusSimulated((TriggerType)trg, (uint8_t)sid, vadText, skipLlm, persIdx);
    isProcessing = false;
    JsonDocument doc;
    doc["ok"]  = true;
    doc["log"] = simLog;
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleDebugPage() {
    File f = SPIFFS.open("/debug.html", "r");
    if (!f) { server.send(503, "text/plain", "debug.html non trovato — eseguire uploadfs"); return; }
    server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    server.streamFile(f, "text/html; charset=utf-8");
    f.close();
}

static void handleFsList() {
    JsonDocument doc;
    doc["total"] = SPIFFS.totalBytes();
    doc["used"]  = SPIFFS.usedBytes();
    JsonArray files = doc["files"].to<JsonArray>();
    File root = SPIFFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        JsonObject o = files.add<JsonObject>();
        String nm = f.name();
        o["name"] = (nm[0] == '/') ? nm : "/" + nm;
        o["size"] = f.size();
    }
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
}

static void handleFsGet() {
    String path = server.uri().substring(7);
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

static File   _uploadFile;
static String _uploadPath;

static void handleFsPut() {
    HTTP_LOG();
    if (!_uploadFile && _uploadPath.length() == 0)
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"nessun file ricevuto\"}");
}

static bool fsReadOnly(const String& path) {
    return path.endsWith(".html") || path.endsWith(".htm")
        || path.endsWith(".css")  || path.endsWith(".js")
        || path.endsWith(".woff2")|| path.endsWith(".png");
}

static void handleFsUpload() {
    HTTP_LOG();
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        _uploadPath = server.arg("path");
        if (_uploadPath.length() == 0 || _uploadPath[0] != '/') _uploadPath = "/" + _uploadPath;
        if (fsReadOnly(_uploadPath)) { server.send(403, "application/json", "{\"ok\":false,\"error\":\"file protetto\"}"); return; }
        if (_uploadFile) _uploadFile.close();
        _uploadFile = SPIFFS.open(_uploadPath, "w");
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_uploadFile) _uploadFile.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        if (_uploadFile) { _uploadFile.close(); }
        server.send(200, "application/json", "{\"ok\":true}");
    }
}

static void handleFsDel() {
    HTTP_LOG();
    String path = server.arg("path");
    if (path.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"path mancante\"}"); return; }
    if (path[0] != '/') path = "/" + path;
    if (fsReadOnly(path)) { server.send(403, "application/json", "{\"ok\":false,\"error\":\"file protetto\"}"); return; }
    if (!SPIFFS.exists(path)) { server.send(404, "application/json", "{\"ok\":false,\"error\":\"non trovato\"}"); return; }
    SPIFFS.remove(path);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleDebugSensors() {
    JsonDocument d;
    d["connected"]      = connected;
    d["device"]         = ble_last_name + " [" + ble_last_addr + "]";
    d["sensor_age_ms"]  = lastSensorMs ? (millis() - lastSensorMs) : 99999;
    d["antennaLeft"]    = furbyState.antennaLeft;
    d["antennaRight"]   = furbyState.antennaRight;
    d["antennaForward"] = furbyState.antennaForward;
    d["antennaBack"]    = furbyState.antennaBack;
    d["tickleHead"]     = furbyState.tickleHead;
    d["tickleTummy"]    = furbyState.tickleTummy;
    d["tickleRight"]    = furbyState.tickleRight;
    d["tickleLeft"]     = furbyState.tickleLeft;
    d["pullTail"]       = furbyState.pullTail;
    d["pushTongue"]     = furbyState.pushTongue;
    d["upright"]        = furbyState.upright;
    d["upsideDown"]     = furbyState.upsideDown;
    d["onRightSide"]    = furbyState.onRightSide;
    d["onLeftSide"]     = furbyState.onLeftSide;
    d["leanBack"]       = furbyState.leanBack;
    d["tiltRight"]      = furbyState.tiltRight;
    d["tiltLeft"]       = furbyState.tiltLeft;
    d["rawB1"]          = furbyState.rawB1;
    d["rawB2"]          = furbyState.rawB2;
    d["rawB3"]          = furbyState.rawB3;
    d["rawB4"]          = furbyState.rawB4;
    String j; serializeJson(d, j);
    server.send(200, "application/json", j);
}

static void handleDebugCmd() {
    HTTP_LOG();
    String bytesStr = server.arg("bytes");
    String channel  = server.arg("channel");
    if (bytesStr.length() == 0) { server.send(400, "application/json", "{\"ok\":false,\"error\":\"bytes missing\"}"); return; }
    if (!connected)              { server.send(503, "application/json", "{\"ok\":false,\"error\":\"non connesso\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, bytesStr) || !doc.is<JsonArray>()) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"json invalido\"}"); return;
    }
    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0 || arr.size() > 20) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"lunghezza non valida\"}"); return;
    }
    uint8_t buf[20]; int i = 0;
    for (JsonVariant v : arr) buf[i++] = (uint8_t)v.as<int>();
    if (channel == "nordic") {
        if (!pCharNWrite) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"NordicWrite non disponibile\"}"); return; }
        pCharNWrite->writeValue(buf, i, false);
    } else {
        furbyWrite(buf, i);
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleDebugMicRms() {
    server.send(200, "application/json",
        "{\"rms1\":" + String(micRmsLive) +
        ",\"rms2\":" + String(micRmsLive2) +
        ",\"vad\":" + String(micVadActive ? "true" : "false") +
        ",\"threshold\":" + String(vad_threshold) + "}");
}

// long-poll: risponde solo quando i valori cambiano o dopo 2s di timeout
static void handleDebugMicPoll() {
    int prev1 = micRmsLive, prev2 = micRmsLive2;
    bool prevVad = micVadActive;
    unsigned long t0 = millis();
    while (millis() - t0 < 2000) {
        if (micRmsLive != prev1 || micRmsLive2 != prev2 || micVadActive != prevVad) break;
        delay(20);
    }
    server.send(200, "application/json",
        "{\"rms1\":" + String(micRmsLive) +
        ",\"rms2\":" + String(micRmsLive2) +
        ",\"vad\":" + String(micVadActive ? "true" : "false") +
        ",\"threshold\":" + String(vad_threshold) + "}");
}

static void handleDebugAmp() {
    HTTP_LOG();
    if (server.method() == HTTP_POST) setAmplifier(server.arg("on") == "1");
    bool on = (ch32PortState >> 6) & 1;
    server.send(200, "application/json", "{\"ok\":true,\"on\":" + String(on ? "true" : "false") + "}");
}

static void handleDebugVol() {
    HTTP_LOG();
    setVolume(server.arg("vol").toInt());
    server.send(200, "application/json", "{\"ok\":true,\"vol\":" + String(constrain(server.arg("vol").toInt(), 0, 100)) + "}");
}

static void handleDebugTone() {
    HTTP_LOG();
    if (isSpeaking) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"speaking\"}"); return; }
    int freq = server.arg("freq").toInt(); if (freq <= 0) freq = 440;
    int ms   = server.arg("ms").toInt();   if (ms  <= 0) ms   = 800;
    if (ms > 5000) ms = 5000;
    const int RATE = 16000;
    const int BUF_SAMP = 256;
    int16_t buf[BUF_SAMP];
    int totalSamples = (RATE * ms) / 1000;
    int phase = 0;
    setAmplifier(true);
    delay(50);
    isSpeaking = true;
    for (int s = 0; s < totalSamples; s += BUF_SAMP) {
        int chunk = min(BUF_SAMP, totalSamples - s);
        for (int i = 0; i < chunk; i++) {
            buf[i] = (int16_t)(16000 * sin(2.0f * M_PI * freq * phase / RATE));
            phase++;
        }
        i2s_write_mono(buf, chunk);
    }
    memset(buf, 0, sizeof(buf));
    i2s_write_mono(buf, BUF_SAMP);
    delay(50);
    i2s_zero_dma_buffer(I2S_NUM);
    setAmplifier(false);
    isSpeaking = false;
    server.send(200, "application/json", "{\"ok\":true,\"freq\":" + String(freq) + ",\"ms\":" + String(ms) + "}");
}

static void micRecordTask(void*) {
    const int RATE      = 16000;
    const int SECS      = 3;
    const int TOTAL_SAMP_STEREO = RATE * SECS * 2;
    const int TOTAL_BYTES       = TOTAL_SAMP_STEREO * sizeof(int16_t);
    int16_t* recBuf = (int16_t*)heap_caps_malloc(TOTAL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!recBuf) recBuf = (int16_t*)malloc(TOTAL_BYTES);
    if (!recBuf) { micTestActive = false; vTaskDelete(NULL); return; }
    size_t bytesRead;
    int totalRead = 0;
    while (totalRead < TOTAL_BYTES) {
        int toRead = min(512 * 2, TOTAL_BYTES - totalRead);
        i2s_read(I2S_MIC_NUM, (uint8_t*)recBuf + totalRead, toRead, &bytesRead, portMAX_DELAY);
        totalRead += bytesRead;
    }
    micTestActive = false;
    setAmplifier(true);
    isSpeaking = true;
    const int OUT_CHUNK = 256;
    int16_t outBuf[OUT_CHUNK];
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
    free(recBuf);
    vTaskDelete(NULL);
}

static void handleDebugMicRecord() {
    HTTP_LOG();
    if (isSpeaking)    { server.send(503, "application/json", "{\"ok\":false,\"error\":\"speaking\"}"); return; }
    if (micTestActive) { server.send(503, "application/json", "{\"ok\":false,\"error\":\"test gia in corso\"}"); return; }
    micTestActive = true;
    xTaskCreatePinnedToCore(micRecordTask, "MicTest", 4096, NULL, 1, NULL, 1);
    server.send(200, "application/json", "{\"ok\":true,\"duration\":3}");
}

static void handleCamDescPromptGet() {
    server.send(200, "application/json", "{\"prompt\":\"" + jsonEscape(gCamDescPrompt) + "\"}");
}

static void handleCamDescPromptSave() {
    HTTP_LOG();
    gCamDescPrompt = server.arg("prompt");
    nvsPut("cam_desc_prompt", gCamDescPrompt);
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handleCamDescribe() {
    HTTP_LOG();
    if (!camActive && !camInit()) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"camera non disponibile\"}"); return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        server.send(503, "application/json", "{\"ok\":false,\"error\":\"frame non disponibile\"}"); return;
    }
    String b64 = base64Encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    String sysP = gPersonalityPrompt;
    if (gCamDescPrompt.length() > 0) sysP += " " + gCamDescPrompt;
    String answer = callLLM(b64, sysP, "Descrivi quello che vedi, in prima persona, come se fossi tu a guardare.");
    server.send(200, "application/json", "{\"ok\":true,\"text\":\"" + jsonEscape(answer) + "\"}");
}

static void handleCameraPage() {
    HTTP_LOG();
    if (!camActive) camInit();
    File f = SPIFFS.open("/camera.html", "r");
    if (f) { server.sendHeader("Cache-Control", "no-cache, must-revalidate"); server.streamFile(f, "text/html; charset=utf-8"); f.close(); return; }
    server.send(200, "text/html", F("<!DOCTYPE html><html><body>"
        "<p>camera.html non trovato in SPIFFS. Esegui: pio run -t uploadfs</p>"
        "<p><a href='/'>Home</a></p></body></html>"));
}

static void handleCameraFrame() {
    HTTP_LOG();
    if (!camActive && !camInit()) { server.send(503, "text/plain", "camera non disponibile"); return; }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { server.send(503, "text/plain", "frame non disponibile"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
}

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

static void handleCameraStream() {
    HTTP_LOG();
    if (!camActive && !camInit()) { server.send(503, "text/plain", "camera non disponibile"); return; }
    WiFiClient* client = new WiFiClient(server.client());
    xTaskCreate(mjpegTask, "mjpeg", 4096, client, 1, NULL);
}

static void handleCameraOff() {
    HTTP_LOG();
    camDeinit();
    server.send(200, "text/plain", "ok");
}

// ── startWebServer ────────────────────────────────────────────────────────────

void startWebServer() {
    server.on("/",                              HTTP_GET,  handleRoot);
    server.on("/hotspot-detect.html",           HTTP_GET,  handleCaptiveIos);
    server.on("/library/test/success.html",     HTTP_GET,  handleCaptiveIos);
    server.on("/canonical.html",                HTTP_GET,  handleCaptiveIos);
    server.on("/generate_204",                  HTTP_GET,  handleCaptiveAndroid);
    server.on("/gen_204",                       HTTP_GET,  handleCaptiveAndroid);
    server.on("/mobile/status.php",             HTTP_GET,  handleCaptiveAndroid); // Samsung fallback
    server.on("/connecttest.txt",               HTTP_GET,  handleCaptiveWindows);
    server.on("/ncsi.txt",                      HTTP_GET,  handleCaptiveWindows);
    server.on("/redirect",                      HTTP_GET,  handleCaptiveRedirect);
    server.on("/wifi/connect",        HTTP_POST, handleWifiConnect);
    server.on("/wifi/connect/status", HTTP_GET,  handleWifiConnectStatus);
    server.on("/wifi/connecting",     HTTP_GET,  []() { serveSpiffs("/wifi_connect.html", "text/html; charset=utf-8", "no-cache"); });
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
    server.on("/sd/reinit",      HTTP_POST, handleSdReinit);
    server.on("/sd/unmount",     HTTP_POST, handleSdUnmount);
    server.on("/sd/formatfat",   HTTP_POST, handleSdFormatFAT);
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
    server.on("/ble/status",     HTTP_GET,  handleBleStatus);
    server.on("/ble/connect",    HTTP_POST, handleBleConnect);
    server.on("/ble/disconnect", HTTP_POST, handleBleDisconnect);
    server.on("/ble/abort",      HTTP_POST, handleBleAbort);
    server.on("/ble/reset",      HTTP_POST, handleBleReset);
    server.on("/ble/save",       HTTP_POST, handleBleSave);
    server.on("/sys/info",        HTTP_GET,  handleSysInfo);
    server.on("/api/home",        HTTP_GET,  handleApiHome);
    server.on("/sys/reboot",      HTTP_POST, handleSysReboot);
    server.on("/wifi/hostname",   HTTP_POST, handleWifiHostname);
    server.on("/wifi/static",     HTTP_POST, handleWifiStatic);
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
    server.on("/debug",            HTTP_GET,  handleDebugPage);
    server.on("/debug/sensors",    HTTP_GET,  handleDebugSensors);
    server.on("/debug/cmd",        HTTP_POST, handleDebugCmd);
    server.on("/debug/mic/rms",    HTTP_GET,  handleDebugMicRms);
    server.on("/debug/mic/poll",   HTTP_GET,  handleDebugMicPoll);
    server.on("/debug/amp",        HTTP_GET,  handleDebugAmp);
    server.on("/debug/amp",        HTTP_POST, handleDebugAmp);
    server.on("/debug/vol",        HTTP_POST, handleDebugVol);
    server.on("/debug/tone",       HTTP_POST, handleDebugTone);
    server.on("/debug/mic/record", HTTP_POST, handleDebugMicRecord);
    server.on("/personality",         HTTP_GET,  handlePersonalityGet);
    server.on("/personality/save",    HTTP_POST, handlePersonalitySave);
    server.on("/behaviors",           HTTP_GET,  handleBehaviorsGet);
    server.on("/behaviors/save",      HTTP_POST, handleBehaviorsSave);
    server.on("/behaviors/export",    HTTP_GET,  handleBehaviorsExport);
    server.on("/behaviors/import",    HTTP_POST, handleBehaviorsImport);
    server.on("/behaviors/actions",   HTTP_GET,  handleBehaviorsActions);
    server.on("/personalities",          HTTP_GET,  handlePersonalitiesList);
    server.on("/personalities/activate", HTTP_POST, handlePersonalitiesActivate);
    server.on("/personalities/new",      HTTP_POST, handlePersonalitiesNew);
    server.on("/personalities/del",      HTTP_POST, handlePersonalitiesDel);
    server.on("/personalities/save",     HTTP_POST, handlePersonalitiesSave);
    server.on("/personalities/export",   HTTP_GET,  handlePersonalitiesExport);
    server.on("/personalities/import",   HTTP_POST, handlePersonalitiesImport);
    server.on("/sensors",                HTTP_GET,  handleSensorsList);
    server.on("/test/llm",            HTTP_POST, handleTestLlm);
    server.on("/test/tts",            HTTP_POST, handleTestTts);
    server.on("/test/voices",         HTTP_GET,  handleTestVoices);
    server.on("/test/behavior",       HTTP_POST, handleTestBehavior);
    server.on("/test/simulate",       HTTP_POST, handleTestSimulate);
    server.on("/fs/list",    HTTP_GET,  handleFsList);
    server.on("/fs/put",     HTTP_POST, handleFsPut, handleFsUpload);
    server.on("/fs/del",     HTTP_POST, handleFsDel);
    server.on("/favicon.ico",                     HTTP_GET, []() { serveSpiffs("/apple-touch-icon.png", "image/png", "public, max-age=86400"); });
    server.on("/apple-touch-icon.png",            HTTP_GET, []() { serveSpiffs("/apple-touch-icon.png", "image/png", "public, max-age=86400"); });
    server.on("/apple-touch-icon-precomposed.png",HTTP_GET, []() { serveSpiffs("/apple-touch-icon.png", "image/png", "public, max-age=86400"); });
    server.on("/manifest.json",                   HTTP_GET, []() { server.send(204); });
    server.on("/robots.txt",                      HTTP_GET, []() { server.send(204); });
    server.on("/sitemap.xml",                     HTTP_GET, []() { server.send(204); });
    server.on("/.well-known/appspecific/com.chrome.devtools.json", HTTP_GET, []() { server.send(204); });
    server.on("/img/logo.png",  HTTP_GET, []() { serveSpiffs("/logo.png",  "image/png",                            "public, max-age=86400"); });
    server.on("/img/title.png", HTTP_GET, []() { serveSpiffs("/title.png", "image/png",                            "public, max-age=86400"); });
    server.on("/shared.css", HTTP_GET, []() {
        serveSpiffsETag("/shared.css", "text/css; charset=utf-8", "no-cache");
    });
    server.on("/shared.js",  HTTP_GET, []() {
        serveSpiffsETag("/shared.js", "application/javascript; charset=utf-8", "no-cache");
    });

    static const char* hdrs[] = {"If-None-Match"};
    server.collectHeaders(hdrs, 1);

    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
        String uri = server.uri();
        if (uri.startsWith("/fs/get/") || uri == "/fs/get") { handleFsGet(); return; }
        // file statici da SPIFFS (.css .js .png .ico ecc.)
        if (server.method() == HTTP_GET) {
            String ct;
            if      (uri.endsWith(".css"))  ct = "text/css; charset=utf-8";
            else if (uri.endsWith(".js"))   ct = "application/javascript; charset=utf-8";
            else if (uri.endsWith(".png"))  ct = "image/png";
            else if (uri.endsWith(".ico"))  ct = "image/x-icon";
            else if (uri.endsWith(".svg"))  ct = "image/svg+xml";
            else if (uri.endsWith(".woff2"))ct = "font/woff2";
            if (ct.length() && SPIFFS.exists(uri)) {
                String cache = (uri.indexOf("bootstrap") >= 0) ? "public, max-age=604800" : "public, max-age=3600";
                serveSpiffs(uri.c_str(), ct.c_str(), cache.c_str());
                return;
            }
        }
        if (uri.endsWith(".map")) { server.send(204); return; }
        Serial.printf("[HTTP] 404 %s %s\n",
            server.method()==HTTP_POST?"POST":server.method()==HTTP_GET?"GET":"OTHER",
            uri.c_str());
        if (isConfigMode) handleCaptiveRedirect();
        else server.send(404, "text/plain", "Not found");
    });
    server.begin();
}
