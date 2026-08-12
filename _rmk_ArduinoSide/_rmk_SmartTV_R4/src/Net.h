#pragma once
// Net.h - WiFi + NTP lifecycle: connect/reconnect, cached connectivity
// check, RTC sync/parse/resync. Ported 1:1 from _FuZzAPP_SmartTV_R4.ino's
// NET namespace (~420 lines).
#include <Arduino.h>
#include <IPAddress.h>
#include "Scheduler.h"

namespace NET {

void Setup();
void Check(SCHED::TaskId taskId);

void Connect();
bool Connected_Cached();
IPAddress getIP();
bool IsConnected();
void Reconnect(SCHED::TaskId taskId);

// Fixed per-deployment identity (MQTT_DEVICE_ID_VALUE, WiFiCredentials.h)
// for the MQTT topic path (see Globals.h's MQTT_TOPIC_* comment) - NOT
// derived from the WiFi MAC. That was tried first (auto-discovered by the
// app via TELEM_DEVICE_ID, sent on HELLO) but requires one local-WiFi
// connection to bootstrap before cloud mode works, which isn't always
// possible (e.g. away from the home network); a fixed, both-sides-know-it
// value has no bootstrap step at all. TELEM_DEVICE_ID is still sent (see
// AppLink.cpp) as a harmless no-op safety net in case the two ever drift.
const char* DeviceId();

void RTC_Begin();
uint32_t RTC_EpochUTC();
void RTC_Parse(SCHED::TaskId taskId);
void RTC_Resync(SCHED::TaskId taskId);
void RTC_RetryTask(SCHED::TaskId taskId);
void setConnectTime(bool reset);

} // namespace NET
