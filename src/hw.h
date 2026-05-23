#pragma once
#include "globals.h"

void ch32WritePort(uint8_t value);
void ch32SetBit(uint8_t bit, bool val);
void ch32Init();
int  readBatteryMv();

void es8311WriteReg(uint8_t reg, uint8_t val);
void initES8311();
void es7210WriteReg(uint8_t reg, uint8_t val);
void initES7210();
void initI2S();

bool camInit();
void camDeinit();
void camApplySettings(framesize_t size, int quality);
void camApplyFlicker(int hz);
void camApplyExposure(int gainCeiling, int brightness, int agc);

bool sdMount();
void sdUnmount();
bool sdCheck();

void saveWifiNets();
void loadWifiNets();
void upsertWifiNet(const String& ssid, const String& pass, int priority);
void removeWifiNet(int idx);
bool tryConnectWifi(bool useDelay);
void startCaptivePortal();
