#pragma once
// Telnet.h - console (Serial + Telnet, port 23). Ported 1:1 from
// _FuZzAPP_Diffuser.ino's TELNET namespace - this one is real and already
// works, unlike the SmartTV's (see _rmk_docs/00_PLAN.md SS9 R1).
#include <Arduino.h>

namespace TELNET {

void handleCmd(String s);
void tickTelnet();

} // namespace TELNET
