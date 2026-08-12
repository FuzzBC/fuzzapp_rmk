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

// 12 hex chars (WiFi MAC, no separators) + NUL - stable per-board identity
// for the MQTT topic path (see Globals.h's MQTT_TOPIC_* comment). Cached
// after the first call; safe before WiFi.begin() has fully settled since
// WiFi.macAddress() returns the adapter's burned-in address either way.
const char* DeviceId();

void RTC_Begin();
uint32_t RTC_EpochUTC();
void RTC_Parse(SCHED::TaskId taskId);
void RTC_Resync(SCHED::TaskId taskId);
void RTC_RetryTask(SCHED::TaskId taskId);
void setConnectTime(bool reset);

} // namespace NET
