#include "llm.h"

String base64Encode(uint8_t* data, size_t length) {
    size_t outLen;
    mbedtls_base64_encode(nullptr, 0, &outLen, data, length);
    unsigned char* buf = (unsigned char*)heap_caps_malloc(outLen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (unsigned char*)malloc(outLen);
    if (!buf) return "";
    mbedtls_base64_encode(buf, outLen, &outLen, data, length);
    String result = String((char*)buf); free(buf);
    return result;
}

// Builds JSON payload in PSRAM to avoid exhausting internal heap (~80-100KB with base64)
String callLLM(const String& base64Img, const String& systemPrompt, const String& userText) {
    Serial.printf("[LLM] provider=%s model=%s img=%u char heap=%u PSRAM=%u\n",
        llm_provider.c_str(), llm_model.c_str(), base64Img.length(),
        esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    String answer;

    auto makePsramPayload = [&](const String& body) -> uint8_t* {
        size_t len = body.length();
        uint8_t* buf = (uint8_t*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = (uint8_t*)malloc(len + 1);
        if (!buf) Serial.printf("[LLM] ERRORE: malloc payload %u byte fallito\n", len + 1);
        else memcpy(buf, body.c_str(), len + 1);
        return buf;
    };

    if (llm_provider == "openai") {
        http.begin(client, "https://api.openai.com/v1/chat/completions");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", "Bearer " + openai_api_key);

        String userContent = "[{\"type\":\"text\",\"text\":\"" + userText + "\"}";
        if (base64Img.length() > 0)
            userContent += ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64," + base64Img + "\"}}";
        userContent += "]";
        String body = "{\"model\":\"" + llm_model + "\",\"messages\":["
            "{\"role\":\"system\",\"content\":\"" + systemPrompt + "\"},"
            "{\"role\":\"user\",\"content\":" + userContent + "}]}";
        uint8_t* buf = makePsramPayload(body);
        body = "";
        if (buf) {
            int code = http.POST(buf, strlen((char*)buf));
            Serial.printf("[LLM] HTTP %d\n", code);
            if (code == 200) {
                String res = http.getString();
                int s = res.indexOf("\"content\": \"") + 12;
                int e = res.indexOf("\"", s);
                if (s > 11) answer = res.substring(s, e);
                else Serial.printf("[LLM] ERRORE parsing risposta: %s\n", res.substring(0, 200).c_str());
            } else {
                Serial.printf("[LLM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
            }
            free(buf);
        }

    } else if (llm_provider == "claude") {
        http.begin(client, "https://api.anthropic.com/v1/messages");
        http.addHeader("Content-Type", "application/json");
        http.addHeader("x-api-key", claude_api_key);
        http.addHeader("anthropic-version", "2023-06-01");

        String claudeContent = "[{\"type\":\"text\",\"text\":\"" + userText + "\"}";
        if (base64Img.length() > 0)
            claudeContent += ",{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\",\"data\":\"" + base64Img + "\"}}";
        claudeContent += "]";
        String body = "{\"model\":\"" + llm_model + "\",\"max_tokens\":128,"
            "\"system\":\"" + systemPrompt + "\","
            "\"messages\":[{\"role\":\"user\",\"content\":" + claudeContent + "}]}";
        uint8_t* buf = makePsramPayload(body);
        body = "";
        if (buf) {
            int code = http.POST(buf, strlen((char*)buf));
            Serial.printf("[LLM] HTTP %d\n", code);
            if (code == 200) {
                String res = http.getString();
                int s = res.indexOf("\"text\": \"") + 9;
                int e = res.indexOf("\"", s);
                if (s > 8) answer = res.substring(s, e);
                else Serial.printf("[LLM] ERRORE parsing risposta: %s\n", res.substring(0, 200).c_str());
            } else {
                Serial.printf("[LLM] ERRORE HTTP %d: %s\n", code, http.getString().substring(0, 200).c_str());
            }
            free(buf);
        }
    } else {
        Serial.printf("[LLM] ERRORE: provider sconosciuto \"%s\"\n", llm_provider.c_str());
    }

    http.end();
    answer.replace("\\n", ""); answer.trim();
    Serial.printf("[LLM] risposta finale: \"%s\"\n", answer.c_str());
    return answer;
}

String transcribeAudio() {
    if (!gSttBuf || gSttLen <= 0) return "";
    String key = openai_api_key;
    if (key.length() == 0) { Preferences p; p.begin("furby",true); key = p.getString("openai",""); p.end(); }
    if (key.length() == 0) { Serial.println("[STT] nessuna chiave OpenAI"); return ""; }

    size_t pcmBytes = (size_t)gSttLen * 2;
    uint8_t wav[44];
    uint32_t dataSize   = (uint32_t)pcmBytes;
    uint32_t chunkSize  = 36 + dataSize;
    uint32_t sampleRate = 16000;
    uint16_t channels   = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate   = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign = channels * bitsPerSample / 8;
    memcpy(wav,      "RIFF", 4);
    memcpy(wav+4,  &chunkSize,    4);
    memcpy(wav+8,    "WAVE", 4);
    memcpy(wav+12,   "fmt ", 4);
    uint32_t subchunk1 = 16; memcpy(wav+16, &subchunk1, 4);
    uint16_t audioFmt  = 1;  memcpy(wav+20, &audioFmt,  2);
    memcpy(wav+22, &channels,      2);
    memcpy(wav+24, &sampleRate,    4);
    memcpy(wav+28, &byteRate,      4);
    memcpy(wav+32, &blockAlign,    2);
    memcpy(wav+34, &bitsPerSample, 2);
    memcpy(wav+36,   "data", 4);
    memcpy(wav+40, &dataSize,      4);

    String boundary = "----WavBnd7210";
    String head = "--" + boundary + "\r\n"
                  "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                  "Content-Type: audio/wav\r\n\r\n";
    String modelPart = "\r\n--" + boundary + "\r\n"
                       "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                       "whisper-1"
                       "\r\n--" + boundary + "--\r\n";

    size_t totalLen = head.length() + 44 + pcmBytes + modelPart.length();

    WiFiClientSecure cli;
    cli.setInsecure();
    if (!cli.connect("api.openai.com", 443)) {
        Serial.println("[STT] connessione fallita");
        return "";
    }
    cli.printf("POST /v1/audio/transcriptions HTTP/1.1\r\n"
               "Host: api.openai.com\r\n"
               "Authorization: Bearer %s\r\n"
               "Content-Type: multipart/form-data; boundary=%s\r\n"
               "Content-Length: %u\r\n"
               "Connection: close\r\n\r\n",
               key.c_str(), boundary.c_str(), (unsigned)totalLen);
    cli.print(head);
    cli.write(wav, 44);
    const size_t CHUNK = 4096;
    uint8_t* pcmPtr = (uint8_t*)gSttBuf;
    size_t sent = 0;
    while (sent < pcmBytes) {
        size_t toSend = min(CHUNK, pcmBytes - sent);
        cli.write(pcmPtr + sent, toSend);
        sent += toSend;
    }
    cli.print(modelPart);

    unsigned long t0 = millis();
    while (!cli.available() && millis() - t0 < 15000) delay(50);
    String resp;
    while (cli.available()) resp += (char)cli.read();
    cli.stop();

    int bodyStart = resp.indexOf("\r\n\r\n");
    if (bodyStart < 0) { Serial.println("[STT] risposta malformata"); return ""; }
    String body = resp.substring(bodyStart + 4);
    // Skip chunked transfer encoding size line if present
    if (body.length() > 0 && isxdigit(body[0])) {
        int nl = body.indexOf("\r\n");
        if (nl >= 0) body = body.substring(nl + 2);
    }

    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) {
        Serial.println("[STT] JSON parse error");
        return "";
    }
    String text = doc["text"].as<String>();
    text.trim();
    Serial.printf("[STT] trascrizione: \"%s\"\n", text.c_str());
    return text;
}
