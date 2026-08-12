#pragma once
// Test.h - self-test command ("T", DIF_TEST_MODE builds only). Ported 1:1
// from _FuZzAPP_Diffuser.ino's TEST namespace.
#include <Arduino.h>
// Must come before the #ifdef below - DIF_TEST_MODE is defined in here, and
// a translation unit can't "discover" a macro by including its definition
// AFTER already having checked #ifdef for it (confirmed live: Test.cpp's
// entire body, including its own later include of this same header,
// silently compiled away to nothing without this line - a link error on
// the missing runSelfTest symbol, not a compile error, made it non-obvious).
#include "../_rmk_Diffuser_DEF.h"

#ifdef DIF_TEST_MODE
namespace TEST {
void runSelfTest();
} // namespace TEST
#endif
