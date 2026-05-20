# Furby Connect AI

ESP32-S3 firmware that turns a classic Furby into an AI-powered interactive robot.

## What it does

The Furby looks around with its camera, thinks with GPT-4o-mini (or Claude), responds with voice via ElevenLabs TTS, and moves its mouth in sync with the audio over Bluetooth LE. It has a deliberately rude and cynical personality.

```
Button press → capture image → GPT-4o-mini (vision)
  → text → ElevenLabs TTS → MP3/PCM audio
  → I2S (ES8311 DAC) + BLE lipsync → Furby mouth
```

## Hardware

- **Board**: [Waveshare ESP32-S3-CAM](https://amzn.to/4fyMf8R)
- **Speaker**: ES8311 codec via I2S
- **Microphones**: ES7210 codec (4-mic array) via I2S
- **Furby**: any Furby Connect (2016) with BLE

## Project status

> Updated May 2026. Progress bars reflect actual state, not aspirational state.

| Component | Progress | Notes |
|---|---|---|
| Camera + vision AI | `████████░░` 80% | Working. High latency on large frames |
| ElevenLabs TTS | `███████░░░` 70% | MP3 works (free tier); PCM requires pro account |
| ES8311 DAC (speaker) | `█████████░` 90% | Stable |
| ES7210 ADC (microphones) | `████████░░` 80% | VAD works, sensitivity needs tuning |
| VAD with 250Hz filter | `███████░░░` 70% | False negatives with soft speech |
| Multi-WiFi (5 networks) | `█████████░` 90% | Stable, mesh-compatible |
| Multi-LLM provider | `████████░░` 80% | OpenAI solid; Claude needs more testing |
| BLE scan + connect | `████████░░` 80% | Stable but sometimes slow to connect |
| BLE lipsync | `████░░░░░░` 40% | Coarse mouth movement, tuning in progress |
| SD card audio cache | `███████░░░` 70% | Working; JSON index needs optimization |
| Debug web UI | `████████░░` 80% | Functional; mobile/desktop UX still rough |
| MP3 decoding (ESP32) | `██████░░░░` 60% | Integrated but not stress-tested |
| Captive portal config | `█████████░` 90% | Stable on first boot |

## Features

- Context-aware responses with camera vision (GPT-4o-mini / Claude)
- Multilingual speech synthesis (ElevenLabs) — MP3 for free accounts, PCM for pro
- Real-time mouth lipsync via BLE (tuning in progress)
- VAD (voice activity detection) with 250Hz high-pass filter to ignore mechanical noise
- SD card audio cache: previously heard responses are not regenerated
- Debug web UI at `http://furby.local`
- WiFi setup via captive portal (AP `Furby_Config`)
- Multi-WiFi (up to 5 saved networks, priority configurable)
- Multi-LLM provider: OpenAI or Anthropic Claude

## Build & flash

**Requirements**: PlatformIO (recommended) or Arduino IDE 2.x with ESP32-S3 core.

```bash
# Build + flash firmware
pio run -t upload

# Flash filesystem (web UI)
pio run -t uploadfs
```

On first boot (or with no saved WiFi), the device creates the `Furby_Config` AP: connect to it and go to `http://192.168.4.1` to enter WiFi credentials and API keys.

## Configuration

Credentials are stored in flash via `Preferences`.

| Parameter | Where |
|---|---|
| WiFi SSID/password | captive portal or web UI |
| OpenAI API key | web UI → Settings |
| Anthropic API key | web UI → Settings |
| ElevenLabs API key + Voice ID | web UI → Settings |
| TTS format (MP3/PCM) | web UI → Settings |
| LLM provider (openai/claude) | web UI → Settings |
| VAD threshold | web UI → Audio |

## Italian README

[README.it.md](README.it.md)
