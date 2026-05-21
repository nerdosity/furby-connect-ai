#pragma once
#include "globals.h"

String base64Encode(uint8_t* data, size_t length);
String callLLM(const String& base64Img, const String& systemPrompt, const String& userText);
String transcribeAudio();
