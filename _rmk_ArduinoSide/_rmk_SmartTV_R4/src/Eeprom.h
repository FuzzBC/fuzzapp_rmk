#pragma once
// Eeprom.h - settings table read/write, delta-tracked chunked persistence,
// LED/ambient/UDPRAW/motion colour blocks, cross-boot self-test, and the
// raw backup dump. Ported 1:1 from _FuZzAPP_SmartTV_R4.ino's EE namespace
// (~900 lines), plus the schema-version + CRC16 integrity check called for
// by 00_PLAN.md SS4.2/SS4.3 (see Eeprom.cpp's Read() for the ruling this
// took given R15 is still open - no factory-default table exists to fall
// back to).
#include "Scheduler.h"
#include "Globals.h"   // EE_SettingDef

namespace EE {

void WriteTime();
void Write(SCHED::TaskId taskId);

void Read();
void SelfTest();
void DumpBackup();

uint8_t Get(uint8_t settingId);
bool Set(uint8_t settingId, uint8_t value);
int getChangedCount();
const EE_SettingDef* getDef(uint8_t settingId);
const EE_SettingDef* getDefByIndex(uint8_t index);
const char* getName(uint8_t settingId);
bool InAnyGroup(const char* name);
bool IsChanged(uint8_t index);
void LogSetting(const EE_SettingDef* def);
void MarkAmbientChanged(uint8_t ledIndex);
void MarkChanged(uint8_t index);
void MarkColorChanged(uint8_t ledIndex);
void MarkMotionChanged();
void MarkUdpChanged();
void ClearAllChanges();

bool w(int index, int value);

} // namespace EE
