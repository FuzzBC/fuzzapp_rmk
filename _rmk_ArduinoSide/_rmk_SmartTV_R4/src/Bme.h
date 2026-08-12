#pragma once
// Bme.h - BME280 temperature/humidity sensor (I2C, probed at 0x76/0x77).
// Ported 1:1 from _FuZzAPP_SmartTV_R4.ino's BME namespace (~142 lines).
#include "Scheduler.h"

namespace BME {

void Setup();
void Check(SCHED::TaskId taskId);

} // namespace BME
