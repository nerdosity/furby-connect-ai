# Furby Connect AI

Firmware per ESP32-S3 che trasforma un Furby classico in un robot interattivo alimentato da AI.

## Cosa fa

Il Furby guarda con la camera, pensa con GPT-4o-mini (o Claude), risponde a voce tramite ElevenLabs TTS e muove la bocca sincronizzata all'audio via Bluetooth LE. Ha una personalità volutamente maleducata e cinica.

```
Pressione tasto → cattura immagine → GPT-4o-mini (vision)
  → testo → ElevenLabs TTS → audio MP3/PCM
  → I2S (ES8311 DAC) + lipsync BLE → bocca Furby
```

## Hardware

- **Board**: [Waveshare ESP32-S3-CAM](https://amzn.to/4fyMf8R)
- **Speaker**: codec ES8311 via I2S
- **Microfoni**: codec ES7210 (4 mic array) via I2S
- **Furby**: qualsiasi Furby Connect (2016) con BLE

## Stato del progetto

> Aggiornato a maggio 2026. Le barre indicano il completamento reale, non quello desiderato.

| Componente | Progresso | Note |
|---|---|---|
| Camera + vision AI | `████████░░` 80% | Funziona. Latenza alta su immagini grandi |
| ElevenLabs TTS | `███████░░░` 70% | MP3 funziona (free); PCM solo account pro |
| ES8311 DAC (speaker) | `█████████░` 90% | Stabile |
| ES7210 ADC (microfoni) | `████████░░` 80% | VAD funziona, sensibilità da affinare |
| VAD con filtro 250Hz | `███████░░░` 70% | Falsi negativi con parlato sottovocce |
| Multi-WiFi (5 reti) | `█████████░` 90% | Stabile, mesh-compatible |
| Multi-provider LLM | `████████░░` 80% | OpenAI ok; Claude da testare più a fondo |
| BLE scan + connect | `████████░░` 80% | Connessione stabile ma a volte lenta |
| Lipsync BLE | `████░░░░░░` 40% | Movimenti bocca grossolani, tuning in corso |
| Cache audio SD card + prefix | `████████░░` 80% | Cache hit: prefisso cinico generato 1 volta (1 call LLM + 1 TTS), poi tutto da SD |
| Web UI debug | `████████░░` 80% | Funzionale; UX mobile/desktop ancora ruvida |
| Decodifica MP3 (ESP32) | `██████░░░░` 60% | Integrato ma poco testato su lungo periodo |
| Captive portal config | `█████████░` 90% | Stabile al primo avvio |

## Funzionalità

- Risposta contestuale con visione camera (GPT-4o-mini / Claude)
- Sintesi vocale multilingua (ElevenLabs) — MP3 per account free, PCM per pro
- Lipsync bocca via BLE in tempo reale (in tuning)
- VAD (rilevamento parlato) con filtro passa-alto 250Hz per ignorare rumori meccanici
- Cache audio su SD card: le risposte già sentite non vengono rigenerate; alla seconda richiesta viene generato un prefisso cinico ("come ti avevo già detto…") con 1 sola call LLM+TTS, poi salvato — dalla terza in poi tutto da SD, zero token
- Web UI di debug su `http://furby.local`
- Configurazione WiFi via captive portal (AP `Furby_Config`)
- Multi-WiFi (fino a 5 reti salvate, priorità configurabile)
- Multi-provider LLM: OpenAI o Anthropic Claude

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

Le credenziali vengono salvate in flash via `Preferences`.

| Parametro | Dove |
|---|---|
| WiFi SSID/password | captive portal o web UI |
| OpenAI API key | web UI → Impostazioni |
| Anthropic API key | web UI → Impostazioni |
| ElevenLabs API key + Voice ID | web UI → Impostazioni |
| Formato TTS (MP3/PCM) | web UI → Impostazioni |
| Provider LLM (openai/claude) | web UI → Impostazioni |
| Soglia VAD | web UI → Audio |

## README in inglese

[README.en.md](README.en.md)
