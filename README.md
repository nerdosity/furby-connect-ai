<p align="center">
  <img src="graphic/writing_512_154_dark.png#gh-light-mode-only" alt="Furby LLM" width="380">
  <img src="graphic/writing_512_154_light.png#gh-dark-mode-only" alt="Furby LLM" width="380">
</p>

<p align="center">
  Firmware ESP32-S3 che trasforma un Furby Connect (2016) in un robot AI autonomo<br>
  con visione camera, sintesi vocale e lipsync via Bluetooth LE.
</p>

<p align="center">
  <a href="README.en.md">🇬🇧 English</a>
</p>

<br>

[![GitHub last commit](https://img.shields.io/github/last-commit/nerdosity/furby-connect-ai)](https://github.com/nerdosity/furby-connect-ai/commits/main)
[![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-orange)](https://platformio.org/)

<details>
<summary>Contenuti</summary>

- [Come funziona](#come-funziona)
- [Hardware](#hardware)
- [Funzionalità](#funzionalità)
- [Architettura sorgente](#architettura-sorgente)
- [Stato del progetto](#stato-del-progetto)
- [Build & flash](#build--flash)
- [Configurazione](#configurazione)

</details>

## Come funziona

```
Pressione tasto / VAD / sensore BLE
  → captureImage()   → Base64 JPEG
  → LLM (OpenAI GPT-4o-mini o Anthropic Claude)   ← cache SD card
  → testo risposta
  → ElevenLabs TTS   → MP3 / PCM 16-bit 16 kHz mono
  → I2S → ES8311 DAC → speaker
  → ampiezza PCM → lipsync BLE → bocca Furby
```

Il Furby ha una personalità deliberatamente maleducata e cinica, configurabile dalla web UI.

## Hardware

| Componente | Dettaglio |
|---|---|
| **MCU** | [Waveshare ESP32-S3-CAM](https://amzn.to/4fyMf8R) - 240 MHz, 8 MB PSRAM, 16 MB flash |
| **Camera** | OV2640 su connettore DVP |
| **DAC / speaker** | ES8311 via I2S (MCLK 10, BCLK 11, LRCK 12, DOUT 14) |
| **ADC / microfoni** | ES7210 - array 4 mic via I2S |
| **SD card** | microSD in modalità 1-bit MMC (CLK 16, CMD 43, D0 44) |
| **Furby** | Furby Connect (2016) - connessione BLE |
| **Tasto** | GPIO 0 (ISR su fronte di discesa) |

## Funzionalità

- **Visione contestuale** - cattura JPEG, inviato al LLM con prompt di personalità
- **Multi-provider LLM** - OpenAI (GPT-4o-mini default) o Anthropic Claude; modello configurabile dalla UI
- **TTS multilingua** - ElevenLabs `multilingual_v2`; MP3 su account free, PCM 16 kHz su pro
- **Lipsync BLE** - ampiezza PCM → byte open/close inviati in real-time al Furby via BLE
- **VAD** - rilevamento parlato con filtro passa-alto 250 Hz per ignorare i rumori meccanici del Furby
- **STT** (opzionale) - buffer PSRAM, disattivato di default
- **Cache audio SD card** - le risposte già sentite vengono riutilizzate; alla seconda richiesta un prefisso cinico viene generato con 1 sola call LLM+TTS e salvato su SD - dalla terza in poi zero token
- **Multi-WiFi** - fino a 5 reti in flash con priorità configurabile; mesh-compatible
- **Web UI** - dashboard su `http://furby.local` con Bootstrap 5
- **Captive portal** - AP `Furby_Config` al primo avvio per configurare WiFi e API key
- **Behaviors** - sistema di trigger (VAD, tasto, sensori BLE) con conseguenze configurabili (azione Furby, TTS fisso, prompt LLM)
- **Flicker filter** - filtro anti-flicker 50/60 Hz per la camera OV2640

## Architettura sorgente

```
src/
├── main.cpp          - setup(), loop(), ISR
├── globals.h/cpp     - variabili condivise, struct, costanti pin
├── hw.h/cpp          - camera, ES8311 DAC, ES7210 ADC, SD card, I/O expander
├── audio.h/cpp       - pipeline audio: I2S, MP3, PCM, lipsync
├── llm.h/cpp         - chiamate OpenAI / Claude, ElevenLabs TTS
├── ble_furby.h/cpp   - BLE scan, connect, lipsync task (Core 0)
├── behaviors.h/cpp   - sistema trigger/conseguenze, personalità
└── web.h/cpp         - WebServer, MJPEG stream, API REST, web UI HTML
```

Protocollo BLE:

| | UUID |
|---|---|
| Service | `dab91435-b5a1-e29c-b041-bcd562613bde` |
| TX characteristic | `dab91383-b5a1-e29c-b041-bcd562613bde` |

## Stato del progetto

> Aggiornato maggio 2026.

| Componente | Progresso | Note |
|---|---|---|
| Camera + vision AI | `████████░░` 80% | Funziona; latenza alta su frame grandi |
| ElevenLabs TTS | `███████░░░` 70% | MP3 funziona (free); PCM solo account pro |
| ES8311 DAC | `█████████░` 90% | Stabile |
| ES7210 ADC / VAD | `████████░░` 80% | VAD ok; falsi negativi con parlato sottovocce |
| Multi-WiFi | `█████████░` 90% | Stabile, mesh-compatible |
| Multi-provider LLM | `████████░░` 80% | OpenAI ok; Claude in test |
| BLE scan + connect | `████████░░` 80% | Stabile, connessione iniziale a volte lenta |
| Lipsync BLE | `████░░░░░░` 40% | Movimenti grossolani, tuning in corso |
| Cache audio SD card | `████████░░` 80% | Cache hit con prefisso cinico - 1 call poi tutto da SD |
| Behaviors / triggers | `███████░░░` 70% | Funziona; UI di configurazione da rifinire |
| Web UI | `████████░░` 80% | Funzionale; UX mobile/desktop ancora ruvida |
| MP3 decoding | `██████░░░░` 60% | Integrato, poco testato su lungo periodo |
| Captive portal | `█████████░` 90% | Stabile al primo avvio |

## Build & flash

**Requisiti:** PlatformIO (consigliato) o Arduino IDE 2.x con core ESP32-S3.

```bash
# Build + flash firmware
pio run -t upload

# Flash filesystem SPIFFS (web UI)
pio run -t uploadfs
```

Al primo avvio (o senza reti WiFi salvate) il dispositivo crea l'AP **`Furby_Config`**.
Collegati e vai su `http://192.168.4.1` per inserire WiFi, OpenAI key, ElevenLabs key e Voice ID.

## Configurazione

Tutti i parametri vengono salvati in flash via `Preferences` (namespace `furby`).

| Parametro | Dove |
|---|---|
| WiFi SSID / password (fino a 5) | captive portal o web UI - Reti |
| OpenAI API key | web UI - Impostazioni |
| Anthropic API key | web UI - Impostazioni |
| ElevenLabs API key + Voice ID | web UI - Impostazioni |
| Formato TTS (`mp3` / `pcm`) | web UI - Impostazioni |
| Provider LLM (`openai` / `claude`) | web UI - Impostazioni |
| Modello LLM | web UI - Impostazioni |
| Soglia VAD | web UI - Audio |
| Qualità / risoluzione camera | web UI - Camera |
| Filtro flicker camera | web UI - Camera |
| Prompt personalità | web UI - Personalità |
| Behaviors e trigger | web UI - Behaviors |
