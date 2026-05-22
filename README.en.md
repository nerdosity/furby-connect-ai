# Furby Connect AI

<p align="center">
  <img src="graphic/writing_512_154_dark.png#gh-light-mode-only" alt="Furby LLM" width="420">
  <img src="graphic/writing_512_154_light.png#gh-dark-mode-only" alt="Furby LLM" width="420">
</p>

<p align="center">
  ESP32-S3 firmware that turns a Furby Connect (2016) into an autonomous AI robot<br>
  with camera vision, speech synthesis, and real-time lipsync over Bluetooth LE.
</p>

<p align="center">
  <a href="README.md">🇮🇹 Italiano</a> &nbsp;·&nbsp;
  <a href="#build--flash">Installation</a> &nbsp;·&nbsp;
  <a href="#project-status">Status</a>
</p>

---

## How it works

```
Button press / VAD / BLE sensor
  → captureImage()   → Base64 JPEG
  → LLM (OpenAI GPT-4o-mini or Anthropic Claude)   ← SD card cache
  → response text
  → ElevenLabs TTS   → MP3 / PCM 16-bit 16 kHz mono
  → I2S → ES8311 DAC → speaker
  → PCM amplitude → BLE lipsync → Furby mouth
```

The Furby has a deliberately rude and cynical personality, fully configurable via the web UI.

---

## Hardware

| Component | Details |
|---|---|
| **MCU** | [Waveshare ESP32-S3-CAM](https://amzn.to/4fyMf8R) — 240 MHz, 8 MB PSRAM, 16 MB flash |
| **Camera** | OV2640 on DVP connector |
| **DAC / speaker** | ES8311 via I2S (MCLK 10, BCLK 11, LRCK 12, DOUT 14) |
| **ADC / microphones** | ES7210 — 4-mic array via I2S |
| **SD card** | microSD in 1-bit MMC mode (CLK 16, CMD 43, D0 44) |
| **Furby** | Furby Connect (2016) — BLE connection |
| **Button** | GPIO 0 (falling-edge ISR) |

---

## Features

- **Contextual vision** — captures JPEG, sends it to the LLM with a personality prompt
- **Multi-provider LLM** — OpenAI (GPT-4o-mini by default) or Anthropic Claude; model configurable from the UI
- **Multilingual TTS** — ElevenLabs `multilingual_v2`; MP3 on free accounts, PCM 16 kHz on pro
- **BLE lipsync** — PCM amplitude → open/close bytes sent in real-time to the Furby via BLE
- **VAD** — voice activity detection with 250 Hz high-pass filter to ignore the Furby's own mechanical noise
- **STT** (optional) — PSRAM buffer, disabled by default
- **SD card audio cache** — previously heard responses are reused; on second request a cynical prefix is generated with 1 LLM+TTS call and saved — from the third request onward, zero tokens, fully served from SD
- **Multi-WiFi** — up to 5 saved networks with configurable priority; mesh-compatible
- **Web UI** — dashboard at `http://furby.local` (or IP) with Bootstrap 5 + Bootstrap Icons
- **Captive portal** — AP `Furby_Config` on first boot to configure WiFi and API keys
- **Behaviors / personalities** — trigger system (VAD, button, BLE sensors) with configurable consequences (Furby action, fixed TTS, LLM prompt)

---

## Source architecture

```
src/
├── main.cpp          — setup(), loop(), ISR
├── globals.h/cpp     — shared variables, structs, pin constants
├── hw.h/cpp          — camera, ES8311 DAC, ES7210 ADC, SD card, I/O expander
├── audio.h/cpp       — audio pipeline: I2S, MP3, PCM, lipsync
├── llm.h/cpp         — OpenAI / Claude calls, ElevenLabs TTS
├── ble_furby.h/cpp   — BLE scan, connect, lipsync task (Core 0)
├── behaviors.h/cpp   — trigger/consequence system, personalities
└── web.h/cpp         — WebServer, MJPEG stream, REST API, web UI HTML
```

### BLE protocol

| | UUID |
|---|---|
| Service | `dab91435-b5a1-e29c-b041-bcd562613bde` |
| TX characteristic | `dab91383-b5a1-e29c-b041-bcd562613bde` |

The `lipSyncTask` FreeRTOS task runs on Core 0 and writes bytes derived from real-time PCM amplitude.

---

## Project status

> Updated May 2026. Progress bars reflect actual state, not aspirational state.

| Component | Progress | Notes |
|---|---|---|
| Camera + vision AI | `████████░░` 80% | Working; high latency on large frames |
| ElevenLabs TTS | `███████░░░` 70% | MP3 works (free tier); PCM requires pro account |
| ES8311 DAC | `█████████░` 90% | Stable |
| ES7210 ADC / VAD | `████████░░` 80% | VAD works; false negatives with soft speech |
| Multi-WiFi | `█████████░` 90% | Stable, mesh-compatible |
| Multi-LLM provider | `████████░░` 80% | OpenAI solid; Claude in testing |
| BLE scan + connect | `████████░░` 80% | Stable but initial connection can be slow |
| BLE lipsync | `████░░░░░░` 40% | Coarse mouth movement, tuning in progress |
| SD card audio cache | `████████░░` 80% | Cache hit with cynical prefix — 1 LLM call then fully from SD |
| Behaviors / triggers | `███████░░░` 70% | Working; config UI needs polish |
| Web UI | `████████░░` 80% | Functional; mobile/desktop UX still rough |
| MP3 decoding | `██████░░░░` 60% | Integrated, not stress-tested long-term |
| Captive portal | `█████████░` 90% | Stable on first boot |

---

## Build & flash

**Requirements:** PlatformIO (recommended) or Arduino IDE 2.x with ESP32-S3 core.

```bash
# Build + flash firmware
pio run -t upload

# Flash SPIFFS filesystem (web UI)
pio run -t uploadfs
```

On first boot (or with no saved WiFi networks), the device creates the **`Furby_Config`** AP.  
Connect to it and go to `http://192.168.4.1` to enter WiFi credentials, OpenAI key, ElevenLabs key and Voice ID.

---

## Configuration

All parameters are stored in flash via `Preferences` (namespace `furby`).

| Parameter | Where |
|---|---|
| WiFi SSID / password (up to 5) | captive portal or web UI → Networks |
| OpenAI API key | web UI → Settings |
| Anthropic API key | web UI → Settings |
| ElevenLabs API key + Voice ID | web UI → Settings |
| TTS format (`mp3` / `pcm`) | web UI → Settings |
| LLM provider (`openai` / `claude`) | web UI → Settings |
| LLM model | web UI → Settings |
| VAD threshold | web UI → Audio |
| Camera quality / resolution | web UI → Camera |
| Personality prompt | web UI → Personality |
| Behaviors and triggers | web UI → Behaviors |

---

## License

No formal license — hobby project. Use the code as you wish, but no warranties are provided.
