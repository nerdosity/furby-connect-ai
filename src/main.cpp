#include <math.h>
#include "globals.h"
#include "hw.h"
#include "audio.h"
#include "llm.h"
#include "behaviors.h"
#include "ble_furby.h"
#include "web.h"
#include "AudioGeneratorMP3.h"
#include "AudioFileSourceBuffer.h"
#include "AudioFileSourcePROGMEM.h"

// Redirect SSL/TLS allocations to PSRAM to keep internal heap free
static void* mbedtls_psram_calloc(size_t n, size_t size) {
    size_t total = n * size;
    void* p = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p) memset(p, 0, total);
    return p;
}
static void mbedtls_psram_free(void* p) { free(p); }

void IRAM_ATTR isrWakeUp() {
    pendingTrigger  = TRG_BUTTON;
    pendingSensorId = 0;
    pendingEvent    = EVT_BUTTON;
    wakeUpTriggered = true;
}

void setup() {
    Serial.begin(115200);
    mbedtls_platform_set_calloc_free(mbedtls_psram_calloc, mbedtls_psram_free);
    setCpuFrequencyMhz(240);
    Serial.println("CPU: " + String(getCpuFrequencyMhz()) + " MHz");
    Serial.println("Heap interno: " + String(ESP.getFreeHeap()) + " B");
    Serial.println("PSRAM: " + String(psramFound() ? ESP.getFreePsram() : 0) + " B liberi / " +
                               String(psramFound() ? ESP.getPsramSize()  : 0) + " B totali");

    if (!SPIFFS.begin(true)) Serial.println("SPIFFS: mount fallito");
    else Serial.printf("SPIFFS: %u KB usati / %u KB totali\n",
        SPIFFS.usedBytes()/1024, SPIFFS.totalBytes()/1024);

    preferences.begin("furby", false);
    loadWifiNets();
    llm_provider        = preferences.getString("llm_prov",  "openai");
    llm_model           = preferences.getString("llm_model", "gpt-4o-mini");
    openai_api_key = preferences.getString("openai", "");
    if (openai_api_key.length() == 0)
        openai_api_key = preferences.getString("openai_key", "");
    claude_api_key = preferences.getString("claude", "");
    if (claude_api_key.length() == 0)
        claude_api_key = preferences.getString("claude_key", "");
    elevenlabs_api_key  = preferences.getString("11labs",    "");
    if (elevenlabs_api_key.length() == 0)
        elevenlabs_api_key = preferences.getString("11labs_key", "");
    elevenlabs_voice_id = preferences.getString("11labs_vid","pNInz6obpgDQGcFmaJcg");
    el_audio_fmt        = preferences.getString("11labs_fmt","pcm");
    ble_service_uuid    = preferences.getString("ble_svc",  BLE_SVC_DEFAULT);
    ble_char_uuid_tx    = preferences.getString("ble_char", BLE_CHAR_DEFAULT);
    serviceUUID      = BLEUUID(ble_service_uuid.c_str());
    charUUID_GPWrite = BLEUUID(ble_char_uuid_tx.c_str());
    vad_threshold   = preferences.getInt("vad_thr",  VAD_THRESHOLD_DEFAULT);
    vadEnabled      = preferences.getBool("vad_en",  true);
    sttEnabled      = preferences.getBool("stt_en",  false);
    gCamDescPrompt  = preferences.getString("cam_desc_prompt",
        "Sei un Furby maleducato e cinico. Descrivi in modo sintetico e sgarbato quello che vedi nell'immagine.");
    camStreamQuality = preferences.getInt("cam_stq", 12);
    camStreamSize    = (framesize_t)preferences.getInt("cam_sts", (int)FRAMESIZE_QVGA);
    camSnapQuality   = preferences.getInt("cam_snq", 8);
    camSnapSize      = (framesize_t)preferences.getInt("cam_sns", (int)FRAMESIZE_VGA);
    camFlicker       = preferences.getInt("cam_flk", 0);
    camGainCeiling   = preferences.getInt("cam_gc",  0);
    camBrightness    = preferences.getInt("cam_br",  0);
    camAgc           = preferences.getInt("cam_agc", 1);
    preferences.end(); // chiude handle globale - da qui in poi solo handle locali
    Serial.printf("[NVS] provider=%s model=%s openai=%s claude=%s el=%s vid=%s fmt=%s\n",
        llm_provider.c_str(), llm_model.c_str(),
        openai_api_key.length()  ? "OK" : "MANCANTE",
        claude_api_key.length()  ? "OK" : "MANCANTE",
        elevenlabs_api_key.length()? "OK" : "MANCANTE",
        elevenlabs_voice_id.c_str(), el_audio_fmt.c_str());
    gEventBehaviors = (EventBehavior*)heap_caps_calloc(
        MAX_EVENT_BEHAVIORS, sizeof(EventBehavior), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gEventBehaviors) {
        Serial.println("[MEM] WARN: PSRAM non disponibile per gEventBehaviors, fallback SRAM");
        gEventBehaviors = new EventBehavior[MAX_EVENT_BEHAVIORS];
    }
    loadBehaviorConfigs();
    loadEventBehaviors();

    camInit();
    camApplySettings(camStreamSize, camStreamQuality);
    camApplyFlicker(camFlicker);

    // I2C - after camInit (camera uses GPIO8/7 as SCCB); bus-stuck recovery: 9 SCL pulses
    pinMode(I2C_SCL_PIN, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
    }
    pinMode(I2C_SCL_PIN, INPUT);
    delay(10);
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    delay(50);

    Serial.println("I2C scan...");
    int i2cFound = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  I2C device trovato: 0x%02X\n", addr);
            i2cFound++;
        }
    }
    if (i2cFound == 0) Serial.println("  I2C: nessun device trovato!");
    else Serial.printf("  I2C scan: %d device(s)\n", i2cFound);

    // sequenza da sample Waveshare 05_audio_out_tf:
    // IO_EXTENSION_Init → IO2=1 → IO6=1 → SD_MMC.begin() → IO4=1
    ch32Init();          // mode=0xFF, IO4=HIGH (SD CS)
    ch32SetBit(2, true); // IO2 HIGH (backlight)
    delay(50);

    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    sdAvailable = SD_MMC.begin("/sdcard", true);

    if (sdAvailable) {
        uint8_t t = SD_MMC.cardType();
        const char* ts = (t==CARD_MMC)?"MMC":(t==CARD_SD)?"SDSC":(t==CARD_SDHC)?"SDHC":"UNKNOWN";
        Serial.printf("SD: OK  tipo=%s  %lluMB totali  %lluMB usati\n",
            ts, SD_MMC.totalBytes()/(1024*1024), SD_MMC.usedBytes()/(1024*1024));
    } else {
        Serial.println("SD: assente o non riconosciuta - modalita RAM");
    }

    initI2S();
    initES8311();
    initES7210();

    {
        Preferences p; p.begin("furby", true);
        String hn = p.getString("hostname", "furby");
        WiFi.setHostname(hn.c_str());
        String sip = p.isKey("static_ip") ? p.getString("static_ip", "") : "";
        if (sip.length()) {
            IPAddress ip, gw, mask, dns;
            String sgw   = p.isKey("static_gw")   ? p.getString("static_gw",   "") : "";
            String smask = p.isKey("static_mask")  ? p.getString("static_mask", "") : "";
            String sdns  = p.isKey("static_dns")   ? p.getString("static_dns",  "") : sgw;
            if (ip.fromString(sip) && gw.fromString(sgw) && mask.fromString(smask)) {
                dns.fromString(sdns.length() ? sdns : sgw);
                WiFi.config(ip, gw, mask, dns);
            }
        }
        p.end();
    }
    if (!tryConnectWifi(false)) startCaptivePortal();
    startWebServer();

    if (!isConfigMode) {
        bleInit();

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

// ── CPU frequency throttling ──────────────────────────────────────────────────
static void cpuThrottle() {
    static uint8_t curMhz = 240;
    static uint32_t idleSince = 0;
    bool busy = isProcessing || isSpeaking || bleConnecting || isConfigMode;
    if (busy) {
        idleSince = millis();
        if (curMhz != 240) { setCpuFrequencyMhz(240); curMhz = 240; }
    } else {
        // scende a 80MHz solo dopo 10s di idle
        if (curMhz == 240 && millis() - idleSince > 10000) {
            setCpuFrequencyMhz(80); curMhz = 80;
        }
    }
}

void loop() {
    if (isConfigMode) dnsServer.processNextRequest();
    server.handleClient();

    if (!isConfigMode) {
        if (doConnect && !bleConnecting) {
            doConnect     = false;
            bleConnecting = true;
            xTaskCreatePinnedToCore(bleConnectTask, "BLEConn", 8192, NULL, 2, NULL, 0);
        }

        if (wakeUpTriggered && !isProcessing) {
            wakeUpTriggered = false;
            isProcessing    = true;
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
    cpuThrottle();
    vTaskDelay(1);
}
