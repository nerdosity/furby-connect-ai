# Furby Connect AI

ESP32-S3 firmware that turns a classic Furby into an AI-powered interactive robot.

## What it does

The Furby looks around with its camera, thinks with GPT-4o-mini (or Claude), responds with voice via ElevenLabs TTS, and moves its mouth in sync with the audio over Bluetooth LE. It has a deliberately rude and cynical personality.

```
Button press → capture image → GPT-4o-mini (vision)
  → text → ElevenLabs TTS → PCM audio 16kHz
  → I2S (ES8311 DAC) + BLE lipsync → Furby mouth
```

## Hardware

- **Board**: [Waveshare ESP32-S3-CAM](https://amzn.to/4fyMf8R)
- **Speaker**: ES8311 codec via I2S
- **Microphones**: ES7210 codec (4-mic array) via I2S
- **Furby**: any Furby Connect (2016) with BLE

## Features

- Context-aware responses with camera vision (GPT-4o-mini / Claude)
- Multilingual speech synthesis (ElevenLabs)
- Real-time mouth lipsync via BLE
- VAD (voice activity detection) with 250Hz high-pass filter to ignore mechanical noise
- SD card audio cache: previously heard responses are not regenerated
- Debug web UI at `http://furby.local`
- WiFi setup via captive portal (AP `Furby_Config`)
- Multi-WiFi (up to 5 saved networks, priority configurable)
- Multi-LLM provider: OpenAI or Anthropic Claude

## Project status

| Component | Status |
|---|---|
| Camera + GPT-4o-mini vision | working |
| ElevenLabs TTS + I2S audio | working |
| ES8311 DAC (speaker) | working |
| ES7210 ADC (microphones) | working |
| VAD with frequency filter | working |
| BLE scan + Furby connect | working |
| BLE lipsync | partial (tuning) |
| SD card cache | working |
| Debug web UI | working |
| Multi-WiFi | working |

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

Credentials are stored in flash via `Preferences` and never leave the device.

| Parameter | Where |
|---|---|
| WiFi SSID/password | captive portal or web UI |
| OpenAI API key | web UI → Settings |
| Anthropic API key | web UI → Settings |
| ElevenLabs API key + Voice ID | web UI → Settings |
| LLM provider (openai/claude) | web UI → Settings |
| VAD threshold | web UI → Audio |

## Italian README

[README.it.md](README.it.md)
