#pragma once
#include "globals.h"

void saveEventBehaviors();
void loadEventBehaviors();
const FurbyActionDef* findFurbyAction(const char* id);
void speakText(const String& text);
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText);
void processStimulusDefault(const String& base64Img);
void processStimulus(TriggerType trg, uint8_t sensorId);
