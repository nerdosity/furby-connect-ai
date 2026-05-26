#pragma once
#include "globals.h"

void savePersonalities();
void loadPersonalities();
void activatePersonality(int idx);
void applyActivePersonality();
void serializeActivePersonalityTo(JsonObject po);


const FurbyActionDef* findFurbyAction(const char* id);
void speakText(const String& text);
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText, const char* behName = "", uint8_t sensorId = 0);
void processStimulusDefault(const String& base64Img);
void processStimulus(TriggerType trg, uint8_t sensorId);
String processStimulusSimulated(TriggerType trg, uint8_t sensorId, const String& vadText, bool skipLlm = false, int personalityIdx = -1);
