# Furby Connect AI

Firmware per ESP32-S3 che trasforma un Furby classico in un robot interattivo alimentato da AI.

## Cosa fa

Il Furby guarda con la camera, pensa con GPT-4o-mini (o Claude), risponde a voce tramite ElevenLabs TTS e muove la bocca sincronizzata all'audio via Bluetooth LE. Ha una personalità volutamente maleducata e cinica.

```
Pressione tasto → cattura immagine → GPT-4o-mini (vision)
  → testo → ElevenLabs TTS → audio PCM 16kHz
  → I2S (ES8311 DAC) + lipsync BLE → bocca Furby
```

## Hardware

- **Board**: Waveshare ESP32-S3-CAM
- **Speaker**: codec ES8311 via I2S
- **Microfoni**: codec ES7210 (4 mic array) via I2S
- **Furby**: qualsiasi Furby Connect (2016) con BLE

## Funzionalità

- Risposta contestuale con visione camera (GPT-4o-mini / Claude)
- Sintesi vocale multilingua (ElevenLabs)
- Lipsync bocca via BLE in tempo reale
- VAD (rilevamento parlato) con filtro passa-alto 250Hz per ignorare rumori meccanici
- Cache audio su SD card: le risposte già sentite non vengono rigenerate
- Web UI di debug su `http://furby.local`
- Configurazione WiFi via captive portal (AP `Furby_Config`)
- Multi-WiFi (fino a 5 reti salvate in flash, priorità configurabile)
- Multi-provider LLM: OpenAI o Anthropic Claude

## Stato del progetto

| Componente | Stato |
|---|---|
| Camera + GPT-4o-mini vision | funzionante |
| ElevenLabs TTS + audio I2S | funzionante |
| ES8311 DAC (speaker) | funzionante |
| ES7210 ADC (microfoni) | funzionante |
| VAD con filtro frequenziale | funzionante |
| BLE scan + connect Furby | funzionante |
| Lipsync BLE | parziale (in tuning) |
| Cache SD card | funzionante |
| Web UI debug | funzionante |
| Multi-WiFi | funzionante |

## Build e flash

**Requisiti**: PlatformIO (consigliato) o Arduino IDE 2.x con core ESP32-S3.

```bash
# Build + flash firmware
pio run -t upload

# Flash filesystem (web UI)
pio run -t uploadfs
```

Al primo avvio (o senza WiFi salvato) il dispositivo crea l'AP `Furby_Config`: collegati e vai su `http://192.168.4.1` per inserire le credenziali WiFi e le API key.

## Configurazione

Le credenziali vengono salvate in flash via `Preferences` e non escono dal dispositivo.

| Parametro | Dove |
|---|---|
| WiFi SSID/password | captive portal o web UI |
| OpenAI API key | web UI → Impostazioni |
| Anthropic API key | web UI → Impostazioni |
| ElevenLabs API key + Voice ID | web UI → Impostazioni |
| Provider LLM (openai/claude) | web UI → Impostazioni |
| Soglia VAD | web UI → Audio |
