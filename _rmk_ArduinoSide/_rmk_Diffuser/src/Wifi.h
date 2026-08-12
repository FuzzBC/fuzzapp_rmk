#pragma once
// Wifi.h - WiFi connection sequencing and status LED. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's WIFI namespace.
#include <Arduino.h>

namespace WIFI {

void connectWiFi();
void finishWifiConnect();
void setupOTA();
void tickLed();
void tickWifi();
void tickWifiConnecting();
void tickWifiSequence();
bool wifiSequenceActive();

} // namespace WIFI
