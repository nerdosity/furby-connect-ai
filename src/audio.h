#pragma once
#include "globals.h"
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceBuffer.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioOutput.h>

void   i2s_write_mono(const int16_t* buf, int samples);
String elOutputFormat();
void   vadTask(void* pvParameters);

String getCacheJSON();
String getCacheSummaryJSON();
void   updateCacheJSON(String newFilename, String text);
String getCachedPrefix(const String& audioFile);
void   setCachedPrefix(const String& audioFile, const String& prefixFile);
String getNextFilename();

void   playAudioSD(String filename);
void   playAudioSDWithPrefix(const String& filename);
String generateAndSaveTTS_SD(const String& text);
void   generateAndPlayTTS_SD(String text);
void   streamAndPlayTTS_RAM(String text);
