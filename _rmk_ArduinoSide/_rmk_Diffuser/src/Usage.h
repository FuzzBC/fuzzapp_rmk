#pragma once
// Usage.h - refill tracking and EEPROM persistence. Ported from
// _FuZzAPP_Diffuser.ino's USAGE namespace, with a schema version byte + a
// CRC16 added over the persisted block (00_PLAN.md SS4.3 - the original had
// neither, only a coarse "all-0xFF" virgin-flash sanity check).
#include <Arduino.h>

namespace USAGE {

uint32_t averageRefillCycleSec();
float dutyCycleForMode(uint8_t mode);
void finalizeRefillCycle();
void loadUsageFromEeprom();
void saveUsageToEeprom(bool includeHistory);
void tickUsageAccum();
bool removeHistoryEntryAt(uint8_t oneBasedIndex);

} // namespace USAGE
