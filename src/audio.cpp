#include "audio.h"
#include "hw.h"
#include "llm.h"

// ── Cache audio ───────────────────────────────────────────────────────────────
// Layout: /<persId>/<lang>/<file>.{pcm,mp3}, indice in /<persId>/index.json
// Entry index: chiave = "<lang>/<file>" (path relativo alla cartella personality).
// SD-first con fallback SPIFFS; se un file esiste su SPIFFS ma non su SD, viene
// migrato automaticamente su SD e rimosso da SPIFFS al primo accesso.

static String sanitizeId(const char* s) {
    String o; o.reserve(32);
    for (const char* p = s; *p; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') o += c;
        else o += '_';
    }
    if (o.length() == 0) o = "default";
    return o;
}

static String persLang() {
    if (gpActivePers && gpActivePers->lang[0]) return String(gpActivePers->lang);
    return gPersonalityLang.length() ? gPersonalityLang : String("it");
}

static String persDir() {
    String id = (gpActivePers && gpActivePers->id[0]) ? sanitizeId(gpActivePers->id) : String("default");
    return "/" + id;
}

String currentPersonalityDir() { return persDir(); }

String personalityDirByIndex(int idx) {
    if (!gAllPersonalities || idx < 0 || idx >= gPersonalityCount) return String("/default");
    const char* id = gAllPersonalities[idx].id;
    String s = (id && id[0]) ? sanitizeId(id) : String("default");
    return "/" + s;
}

static String cacheIndexPath() { return persDir() + "/index.json"; }

static void ensureDirsSD(const String& fullPath) {
    int slash = 1;
    while (true) {
        int next = fullPath.indexOf('/', slash);
        if (next < 0) break;
        String sub = fullPath.substring(0, next);
        if (sub.length() > 0 && !SD_MMC.exists(sub.c_str())) SD_MMC.mkdir(sub.c_str());
        slash = next + 1;
    }
}

static void ensureDirsSPIFFS(const String& fullPath) {
    int slash = 1;
    while (true) {
        int next = fullPath.indexOf('/', slash);
        if (next < 0) break;
        String sub = fullPath.substring(0, next);
        if (sub.length() > 0 && !SPIFFS.exists(sub.c_str())) SPIFFS.mkdir(sub.c_str());
        slash = next + 1;
    }
}

// Migra un file da SPIFFS a SD solo se la SD è realmente operativa e il file su SD manca.
// "SD non inserita" != "SD vuota": migriamo solo quando sdAvailable è true.
// Per sicurezza, cancelliamo SPIFFS solo se la copia su SD risulta integra (size byte-per-byte).
static bool migrateToSD(const String& fullPath) {
    if (!sdAvailable) return false;
    if (SD_MMC.exists(fullPath.c_str())) return true;
    if (!SPIFFS.exists(fullPath.c_str())) return false;
    File src = SPIFFS.open(fullPath.c_str(), "r");
    if (!src) return false;
    size_t srcSize = src.size();
    ensureDirsSD(fullPath);
    File dst = SD_MMC.open(fullPath.c_str(), FILE_WRITE);
    if (!dst) { src.close(); Serial.printf("[CACHE] SD open fallito per %s, non migro\n", fullPath.c_str()); return false; }
    uint8_t buf[1024];
    size_t written = 0;
    bool ioErr = false;
    while (src.available()) {
        int n = src.read(buf, sizeof(buf));
        if (n <= 0) { ioErr = true; break; }
        size_t w = dst.write(buf, n);
        if (w != (size_t)n) { ioErr = true; break; }
        written += w;
    }
    src.close(); dst.close();
    if (ioErr || written != srcSize) {
        Serial.printf("[CACHE] migrazione %s ABORT (scritti %u/%u): mantengo SPIFFS\n", fullPath.c_str(), (unsigned)written, (unsigned)srcSize);
        SD_MMC.remove(fullPath.c_str());
        return false;
    }
    File chk = SD_MMC.open(fullPath.c_str(), "r");
    if (!chk || chk.size() != srcSize) {
        if (chk) chk.close();
        Serial.printf("[CACHE] verify %s fallita: mantengo SPIFFS\n", fullPath.c_str());
        SD_MMC.remove(fullPath.c_str());
        return false;
    }
    chk.close();
    SPIFFS.remove(fullPath.c_str());
    Serial.printf("[CACHE] migrato SPIFFS->SD (%u B): %s\n", (unsigned)srcSize, fullPath.c_str());
    return true;
}

bool migrateFileToSD(const String& fullPath) { return migrateToSD(fullPath); }

// Apre per lettura SOLO su SD. Cache audio non esiste senza SD.
static File openRead(const String& fullPath) {
    if (!sdAvailable) return File();
    if (!SD_MMC.exists(fullPath.c_str())) return File();
    return SD_MMC.open(fullPath.c_str(), "r");
}

// Apre per scrittura SOLO su SD. Cache audio non esiste senza SD.
// Se SD non disponibile o open fallisce, ritorna File() vuoto: chi chiama deve gestirlo.
static File openWrite(const String& fullPath) {
    if (!sdAvailable) return File();
    ensureDirsSD(fullPath);
    return SD_MMC.open(fullPath.c_str(), FILE_WRITE);
}

String getCacheJSON() {
    File f = openRead(cacheIndexPath());
    if (!f) return "{}";
    String data = f.readString();
    f.close();
    return data;
}

String getCacheSummaryJSON() {
    auto full = JsonDocPsram();
    deserializeJson(full, getCacheJSON());
    auto summary = JsonDocPsram();
    for (JsonPair kv : full.as<JsonObject>()) {
        JsonVariant v = kv.value();
        if (v.is<JsonObject>()) {
            if (v["canned"] | false) continue; // le canned non vanno proposte all'LLM
            summary[kv.key()] = v["text"].as<String>();
        } else {
            summary[kv.key()] = v.as<String>();
        }
    }
    String out; serializeJson(summary, out); return out;
}

static void saveCacheIndex(JsonDocument& doc) {
    File f = openWrite(cacheIndexPath());
    if (f) { serializeJson(doc, f); f.close(); }
}

void updateCacheJSON(String newFilename, String text) {
    auto doc = JsonDocPsram();
    deserializeJson(doc, getCacheJSON());
    JsonObject entry = doc[newFilename].to<JsonObject>();
    entry["text"]   = text;
    entry["prefix"] = "";
    saveCacheIndex(doc);
}

void updateCacheJSONCanned(String newFilename, String text) {
    auto doc = JsonDocPsram();
    deserializeJson(doc, getCacheJSON());
    JsonObject entry = doc[newFilename].to<JsonObject>();
    entry["text"]   = text;
    entry["prefix"] = "";
    entry["canned"] = true;
    saveCacheIndex(doc);
}

String getCachedPrefix(const String& audioFile) {
    auto doc = JsonDocPsram();
    deserializeJson(doc, getCacheJSON());
    JsonVariant v = doc[audioFile];
    if (v.is<JsonObject>()) {
        String p = v["prefix"].as<String>();
        if (p.length() > 0) return p;
    }
    return "";
}

void setCachedPrefix(const String& audioFile, const String& prefixFile) {
    auto doc = JsonDocPsram();
    deserializeJson(doc, getCacheJSON());
    JsonVariant v = doc[audioFile];
    if (v.is<JsonObject>()) {
        v["prefix"] = prefixFile;
    } else {
        String oldText = v.as<String>();
        JsonObject entry = doc[audioFile].to<JsonObject>();
        entry["text"]   = oldText;
        entry["prefix"] = prefixFile;
    }
    saveCacheIndex(doc);
}

String getNextFilename() {
    Preferences prefs; prefs.begin("furby_sys", false);
    int counter = prefs.getInt("file_id", 0) + 1;
    prefs.putInt("file_id", counter); prefs.end();
    return persLang() + "/" + String(counter) + ".pcm";
}

String elOutputFormat() {
    return el_audio_fmt == "mp3" ? "mp3_22050_32" : "pcm_16000_16_mono";
}

// ── AudioOutput I2S ───────────────────────────────────────────────────────────

class AudioOutputI2SDirect : public AudioOutput {
public:
    bool begin() override { isSpeaking = true; return true; }

    // chiamato dal decoder quando cambia sample rate (es. MP3 a 22050 Hz)
    bool SetRate(int hz) override {
        i2s_set_clk(I2S_NUM, hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
        return true;
    }

    bool ConsumeSample(int16_t sample[2]) override {
        int16_t mono = (int16_t)(((int32_t)sample[0] + sample[1]) / 2);
        currentAmplitude = (int)abs(mono);
        i2s_write_mono(&mono, 1);
        return true;
    }
    bool stop() override {
        i2s_zero_dma_buffer(I2S_NUM);
        isSpeaking = false; currentAmplitude = 0;
        i2s_set_clk(I2S_NUM, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
        return true;
    }
};

void i2s_write_mono(const int16_t* buf, int samples) {
    const int CHUNK = 128;
    int16_t stereo[CHUNK * 2];
    size_t written;
    for (int i = 0; i < samples; i += CHUNK) {
        int n = (samples - i < CHUNK) ? (samples - i) : CHUNK;
        for (int k = 0; k < n; k++) { stereo[k*2] = buf[i+k]; stereo[k*2+1] = buf[i+k]; }
        i2s_write(I2S_NUM, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    }
}

// ── MP3 playback ──────────────────────────────────────────────────────────────

static void playMp3FromStream(WiFiClient* stream, HTTPClient& http) {
    int contentLen = http.getSize();
    size_t bufSize = (contentLen > 0) ? (size_t)contentLen : 256 * 1024;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { Serial.println("[MP3] ERRORE: malloc PSRAM fallito"); return; }
    size_t received = 0;
    uint8_t tmp[512];
    while ((http.connected() || stream->available()) && received < bufSize) {
        if (stream->available()) {
            int n = stream->readBytes(tmp, min((int)sizeof(tmp), (int)(bufSize - received)));
            memcpy(buf + received, tmp, n);
            received += n;
        } else { delay(1); }
    }
    AudioFileSourceBuffer* src = new AudioFileSourceBuffer(new AudioFileSourcePROGMEM(buf, received), 4096);
    AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
    AudioOutputI2SDirect* out = new AudioOutputI2SDirect();
    out->begin(); mp3->begin(src, out);
    while (mp3->isRunning()) { if (!mp3->loop()) { mp3->stop(); break; } }
    out->stop();
    delete mp3; delete src; delete out; free(buf);
}

// ── SD playback ───────────────────────────────────────────────────────────────

void playAudioSD(String filename) {
    String full = persDir() + "/" + filename;
    File file = openRead(full);
    if (!file) { Serial.printf("[AUDIO] ERRORE: file %s non trovato\n", full.c_str()); return; }
    size_t sz = file.size();

    if (filename.endsWith(".mp3")) {
        uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) { Serial.println("[AUDIO-SD] ERRORE malloc MP3"); file.close(); return; }
        file.read(buf, sz); file.close();
        AudioFileSourceBuffer* src = new AudioFileSourceBuffer(
            new AudioFileSourcePROGMEM(buf, sz), 4096);
        AudioGeneratorMP3* mp3 = new AudioGeneratorMP3();
        AudioOutputI2SDirect* out = new AudioOutputI2SDirect();
        out->begin(); mp3->begin(src, out);
        while (mp3->isRunning()) { if (!mp3->loop()) { mp3->stop(); break; } }
        out->stop();
        delete mp3; delete src; delete out; free(buf);
    } else {
        isSpeaking = true;
        size_t bytesRead;
        uint8_t buffer[1024];
        while (file.available()) {
            bytesRead = file.read(buffer, sizeof(buffer));
            int16_t* pcm = (int16_t*)buffer;
            int n = bytesRead / 2; long sum = 0;
            for (int i = 0; i < n; i++) sum += abs(pcm[i]);
            currentAmplitude = n > 0 ? sum / n : 0;
            i2s_write_mono(pcm, n);
        }
        file.close();
        i2s_zero_dma_buffer(I2S_NUM);
        isSpeaking = false; currentAmplitude = 0;
    }
}

void playAudioSDWithPrefix(const String& filename) {
    String prefixFile = getCachedPrefix(filename);
    if (prefixFile.length() == 0) {
        String sysP = "Sei un Furby cinico e scocciato. Genera UNA SOLA frase breve (max 10 parole) "
                      "per introdurre una risposta che hai già dato in precedenza. "
                      "Solo la frase, nessun'altra parola.";
        String prefixText = callLLM("", sysP, "");
        prefixText.trim();
        if (prefixText.length() > 0) {
            prefixFile = generateAndSaveTTS_SD(prefixText);
            if (prefixFile.length() > 0) {
                setCachedPrefix(filename, prefixFile);
                playAudioSD(prefixFile);
            }
        }
    } else {
        playAudioSD(prefixFile);
    }
    playAudioSD(filename);
}

// ── TTS generation ────────────────────────────────────────────────────────────

String generateAndSaveTTS_SD(const String& text) {
    bool mp3mode = (el_audio_fmt == "mp3");
    String ext = mp3mode ? ".mp3" : ".pcm";
    String filename = getNextFilename();
    if (mp3mode) filename = filename.substring(0, filename.lastIndexOf('.')) + ext;
    String full = persDir() + "/" + filename;
    Serial.printf("[TTS-SAVE] fmt=%s \"%s\" -> %s (%s)\n", el_audio_fmt.c_str(), text.c_str(), full.c_str(), sdAvailable ? "SD" : "SPIFFS");
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id
               + "?output_format=" + elOutputFormat());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    JsonDocument doc; doc["text"] = text; doc["model_id"] = "eleven_multilingual_v2";
    String payload; serializeJson(doc, payload);
    int code = http.POST(payload);
    if (code == 200) {
        File f = openWrite(full);
        if (f) { http.writeToStream(&f); f.close(); }
        else { http.end(); return ""; }
    } else {
        Serial.printf("[TTS-SAVE] ERRORE HTTP %d\n", code);
        http.end(); return "";
    }
    http.end();
    return filename;
}

void generateAndPlayTTS_SD(String text) {
    String filename = generateAndSaveTTS_SD(text);
    if (filename.length() == 0) return;
    updateCacheJSON(filename, text);
    playAudioSD(filename);
}

void streamAndPlayTTS_RAM(String text) {
    bool mp3mode = (el_audio_fmt == "mp3");
    Serial.printf("[TTS-RAM] fmt=%s testo: \"%s\"\n", el_audio_fmt.c_str(), text.c_str());
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, "https://api.elevenlabs.io/v1/text-to-speech/" + elevenlabs_voice_id
               + "?output_format=" + elOutputFormat());
    http.addHeader("Content-Type", "application/json");
    http.addHeader("xi-api-key", elevenlabs_api_key);
    JsonDocument doc; doc["text"] = text; doc["model_id"] = "eleven_multilingual_v2";
    String payload; serializeJson(doc, payload);
    int code = http.POST(payload);
    if (code == 200) {
        WiFiClient* stream = http.getStreamPtr();
        if (mp3mode) {
            playMp3FromStream(stream, http);
        } else {
            uint8_t buffer[1024];
            isSpeaking = true;
            int totalBytes = 0;
            while (http.connected() || stream->available()) {
                if (stream->available()) {
                    int n = stream->readBytes(buffer, sizeof(buffer));
                    totalBytes += n;
                    int16_t* pcm = (int16_t*)buffer;
                    int ns = n / 2; long sum = 0;
                    for (int i = 0; i < ns; i++) sum += abs(pcm[i]);
                    currentAmplitude = ns > 0 ? sum / ns : 0;
                    i2s_write_mono(pcm, ns);
                }
                delay(1);
            }
            Serial.printf("[TTS-RAM] fine, %d byte PCM\n", totalBytes);
            i2s_zero_dma_buffer(I2S_NUM);
            isSpeaking = false; currentAmplitude = 0;
        }
    } else {
        Serial.printf("[TTS-RAM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
    }
    http.end();
}

// ── VAD task ──────────────────────────────────────────────────────────────────

void vadTask(void* pvParameters) {
    const int BUF_SAMPLES = 512;
    int16_t buf[BUF_SAMPLES];
    size_t bytesRead;

    uint32_t speechStart  = 0;
    uint32_t silenceStart = 0;
    bool inSpeech = false;

    // IIR high-pass ~250Hz @ 16kHz: filters mechanical noise, keeps voice (300-3400Hz)
    float hpL = 0, hpR = 0, prevL = 0, prevR = 0;
    const float HP_ALPHA = 0.91f;

    for (;;) {
        if (isSpeaking || isConfigMode || !vadEnabled) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            speechStart = 0; silenceStart = 0; inSpeech = false;
            hpL = hpR = prevL = prevR = 0;
            gSttLen = 0;
            continue;
        }

        if (micTestActive) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
            continue;
        }

        i2s_read(I2S_MIC_NUM, buf, sizeof(buf), &bytesRead, portMAX_DELAY);
        int n = bytesRead / 2;

        int64_t sumL = 0, sumR = 0;
        for (int i = 0; i < n; i += 2) {
            float xL = (float)buf[i];
            float xR = (float)buf[i+1];
            hpL = HP_ALPHA * (hpL + xL - prevL);
            hpR = HP_ALPHA * (hpR + xR - prevR);
            prevL = xL; prevR = xR;
            sumL += (int64_t)(hpL * hpL);
            sumR += (int64_t)(hpR * hpR);
        }
        int halfN = n / 2;
        int rms  = (int)sqrt((double)sumL / halfN);
        int rms2 = (int)sqrt((double)sumR / halfN);
        micRmsLive  = rms;
        micRmsLive2 = rms2;

        uint32_t now = millis();

        if (rms > vad_threshold) {
            if (!inSpeech) { inSpeech = true; speechStart = now; gSttLen = 0; }
            silenceStart = now;
            micVadActive = true;
            if (sttEnabled && gSttBuf) {
                for (int i = 0; i < n; i += 2) {
                    if (gSttLen < STT_BUF_MAX_SAMPLES)
                        gSttBuf[gSttLen++] = buf[i];
                }
            }
        } else {
            if (inSpeech) {
                uint32_t speechLen  = now - speechStart;
                uint32_t silenceLen = now - silenceStart;
                if (speechLen >= VAD_SPEECH_MS && silenceLen >= VAD_SILENCE_MS) {
                    inSpeech = false;
                    micVadActive = false;
                    if (!connected && !gDryRun) {
                        // nessun Furby connesso e dry run disattivo: ignora silenziosamente
                    } else {
                        Serial.printf("VAD: parlato rilevato -> trigger (STT buf=%d samples)\n", (int)gSttLen);
                        pendingTrigger = TRG_VAD;
                        pendingSensorId = 0;
                        pendingEvent = EVT_VAD;
                        wakeUpTriggered = true;
                    }
                }
            } else {
                micVadActive = false;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
