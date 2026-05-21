#pragma once
#include "globals.h"

void bleInit();
void bleResetState();
void bleDisconnect();
void bleScanStop();
void bleScanStart(int durationSec = 10);
bool connectToFurby(BLEAdvertisedDevice* dev = nullptr);
void furbyWrite(const uint8_t* buf, size_t len);
void keepAliveTask(void* pvParameters);
void bleConnectTask(void* pvParameters);
void lipSyncTask(void* pvParameters);
void saveBehaviorConfigs();
void loadBehaviorConfigs();
void applyBehaviorRules(const FurbySensors& prev, const FurbySensors& cur);
