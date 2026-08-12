#pragma once
// ModeButton.h - virtual MODE button state machine. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's MODE namespace.
#include "Types.h"

namespace MODE {

void applyModeAdvance();
void applyModeTargetAdvance();
void applyShutdown();
void clearPendingStatusReply();
bool cmdDiffuserOn(uint8_t m, const DnPayload &p, bool isWaterRetry = false);
bool cmdModeTarget(uint8_t t);
void continueModeTarget();
const char* modeName(uint8_t m);
void pulseModeRaw(unsigned long dur);
void pulseModeShort();
void sendPendingStatusReply(UdpStatusReplyMode mode);
void tickModeTimeout();
void tickPinRelease();

} // namespace MODE
