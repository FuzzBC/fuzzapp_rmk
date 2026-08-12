#pragma once
// ProtoLink.h - Link A binary protocol (port 8439), built on
// _rmk_Shared's generated codec (protocol_opcodes.h) + Frame.h. Replaces
// _FuZzAPP_Diffuser.ino's PROTO namespace (ASCII "Ds"/"Dn"/"#SS..." scheme)
// - see _rmk_docs/01_PROTOCOL.md for the wire format this now speaks.
#include <Arduino.h>
#include <protocol_opcodes.h>
// Must come before the #ifdef DIF_TEST_MODE below - see Test.h's comment on
// this exact bug class (a header can't check a macro it hasn't included the
// definition of yet).
#include "../_rmk_Diffuser_DEF.h"

namespace PROTOLINK {

void udpOpen();
void tick();                        // poll socket, dispatch one frame per call
void cmdStatusQuery(bool silent = false);  // push TELEM_DIFFUSER_STATUS to the cached sender
void sendLog(uint8_t level, const char *text);  // push one LOG (0x03) frame - used by DIAG

#ifdef DIF_TEST_MODE
// Test-only entry point (see Test.cpp) - runs a payload through the exact
// same dispatch() the real UDP path uses, without needing an actual socket
// round-trip. Returns the AckResult the dispatch selected.
Proto::AckResult dispatchForTest(uint8_t opcode, const uint8_t *payload, size_t len);
#endif

} // namespace PROTOLINK
