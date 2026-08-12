#pragma once
// Lisens.h - ambient light sensor sampling, lux-level hysteresis state
// machine, and the single choke point (setLux) that arms lux-driven
// transitions per active source. Ported 1:1 from _FuZzAPP_SmartTV_R4.ino's
// LISENS namespace (~260 lines).
#include "Scheduler.h"

namespace LISENS {

void Setup();
void Check(SCHED::TaskId taskId);
void setLux(int newLux);
void ResetTime();

} // namespace LISENS
