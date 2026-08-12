#pragma once
// Diag.h - Link A read-only diagnostics (DIAG_HEALTH / DIAG_PARFUM_TRACE
// opcodes). Ported from _FuZzAPP_Diffuser.ino's DIAG namespace. The old '!'-
// marker text-collision workaround is gone - the new protocol dispatches by
// opcode, not by sniffing the first character of ASCII text, so the
// collision it guarded against structurally can't happen anymore. Replies
// go out as one or more LOG (0x03) telemetry frames instead of raw text.
#include <Arduino.h>

namespace DIAG {

void health();
void parfumTrace();

} // namespace DIAG
