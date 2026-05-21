#include "audio.h"
#include "hw.h"
#include "llm.h"

// ── Cache SD ──────────────────────────────────────────────────────────────────

String getCacheJSON() {
    if (!sdAvailable) return "{}";
    File f = SD_MMC.open("/index.json");
    if (!f) return "{}";
    String data = f.readString();
    f.close();
    return data;
}

String getCacheSummaryJSON() {
    JsonDocument full;
    deserializeJson(full, getCacheJSON());
    JsonDocument summary;
    for (JsonPair kv : full.as<JsonObject>()) {
        JsonVariant v = kv.value();
        if (v.is<JsonObject>()) summary[kv.key()] = v["text"].as<String>();
        else                    summary[kv.key()] = v.as<String>();
    }
    String out; serializeJson(summary, out); return out;
}

static void saveCacheIndex(JsonDocument& doc) {
    File f = SD_MMC.open("/index.json", FILE_WRITE);
    if (f) { serializeJson(doc, f); f.close(); }
}

void updateCacheJSON(String newFilename, String text) {
    if (!sdAvailable) return;
    JsonDocument doc;
    deserializeJson(doc, getCacheJSON());
    JsonObject entry = doc[newFilename].to<JsonObject>();
    entry["text"]   = text;
    entry["prefix"] = "";
    saveCacheIndex(doc);
}

String getCachedPrefix(const String& audioFile) {
    JsonDocument doc;
    deserializeJson(doc, getCacheJSON());
    JsonVariant v = doc[audioFile];
    if (v.is<JsonObject>()) {
        String p = v["prefix"].as<String>();
        if (p.length() > 0) return p;
    }
    return "";
}

void setCachedPrefix(const String& audioFile, const String& prefixFile) {
    if (!sdAvailable) return;
    JsonDocument doc;
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
    return String(counter) + ".pcm";
}

String elOutputFormat() {
    return el_audio_fmt == "mp3" ? "mp3_22050_32" : "pcm_16000_16_mono";
}

// ── AudioOutput I2S ───────────────────────────────────────────────────────────

class AudioOutputI2SDirect : public AudioOutput {
public:
    bool begin() override { setAmplifier(true); isSpeaking = true; return true; }

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
        setAmplifier(false); return true;
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
    if (!sdAvailable) { Serial.println("[AUDIO-SD] ERRORE: SD non disponibile"); return; }
    File file = SD_MMC.open("/" + filename);
    if (!file) { Serial.printf("[AUDIO-SD] ERRORE: file /%s non trovato\n", filename.c_str()); return; }
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
        setAmplifier(true); isSpeaking = true;
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
        setAmplifier(false);
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
    Serial.printf("[TTS-SAVE] fmt=%s \"%s\" -> %s\n", el_audio_fmt.c_str(), text.c_str(), filename.c_str());
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
        File f = SD_MMC.open("/" + filename, FILE_WRITE);
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
            setAmplifier(true); isSpeaking = true;
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
            setAmplifier(false);
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
                    Serial.printf("VAD: parlato rilevato -> trigger (STT buf=%d samples)\n", (int)gSttLen);
                    pendingTrigger = TRG_VAD;
                    pendingSensorId = 0;
                    pendingEvent = EVT_VAD;
                    wakeUpTriggered = true;
                }
            } else {
                micVadActive = false;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}
