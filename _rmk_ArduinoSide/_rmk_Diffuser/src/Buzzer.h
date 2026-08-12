#pragma once
// Buzzer.h - buzzer confirmation detection. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's BUZZ namespace. See _rmk_docs/00_PLAN.md SS9 R4:
// this is a 2-strike behavioural heuristic, not real tone discrimination -
// kept exactly as production pending verification against real hardware.
#include <Arduino.h>

namespace BUZZ {

void buzzerFinalizeBurst();
void tickBuzzer();

} // namespace BUZZ
