#pragma once
#include "globals.h"

void savePersonalities();
void loadPersonalities();
void reconcilePersonalitiesWithSD();
bool writePersFile(const String& out);
void activatePersonality(int idx);
void setActiveByIndex(int idx);
void applyActivePersonality();
void serializeActivePersonalityTo(JsonObject po);
void serializeBehavior(JsonObject& bo, const EventBehavior& b);
// Sostituisce l'array RAM con il payload JSON ricevuto. Conserva il campo
// canned.file esistente cercando per id (idem del comportamento attuale di
// deserializePersonality). Non scrive su disco — chiama savePersonalities() dopo.
// Ritorna false se JSON non valido o array vuoto.
bool loadAllPersonalitiesFromJSON(const String& body, int* outActive);


const FurbyActionDef* findFurbyAction(const char* id);
void speakText(const String& text);
void executeConsequence(const Consequence& csq, const String& base64Img, const String& sttText, const char* behName = "", uint8_t sensorId = 0);
void processStimulusDefault(const String& base64Img);
void processStimulus(TriggerType trg, uint8_t sensorId);
String processStimulusSimulated(TriggerType trg, uint8_t sensorId, const String& vadText, bool skipLlm = false, int personalityIdx = -1);
