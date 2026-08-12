#pragma once
// Parfum.h - timed insist dispense mode. Ported from _FuZzAPP_Diffuser.ino's
// PARFUM namespace. See _rmk_docs/00_PLAN.md SS9 R3: the original header
// claimed the requested mode (E) is always coerced to 10 SEC - that coercion
// never existed in the actual code. This port preserves the REAL (uncoerced)
// behaviour and drops the false claim from the comments; PARFUM_DIF_MODE/E
// are honoured as requested.
#include <Arduino.h>
#include "../_rmk_Diffuser_DEF.h"

namespace PARFUM {

uint16_t parfumRemainingMin();
bool parfumStart(uint16_t minutes, uint8_t mode = PARFUM_DIF_MODE);
bool parfumStop(const char *reason, bool naturalExpiry = false);
void tickParfum();

} // namespace PARFUM
