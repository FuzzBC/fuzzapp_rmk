#pragma once
// DifLink.h - Link A binary protocol client (port 8439, talks to the
// rewritten Diffuser). Adapted from _FuZzAPP_SmartTV_R4.ino's DIF namespace
// (~920 lines) onto _rmk_Shared's generated codec (protocol_opcodes.h) +
// Frame.h - see 01_PROTOCOL.md. Same non-blocking async-send state machine
// as the original (avoids freezing loop() during a Dual-Color-spam burst),
// now framing binary DIFFUSER_* opcodes instead of ASCII "Dn"/"Ds"/"Dp"
// strings; same relay-pending queue so AppLink.cpp can defer an app-side
// ACK until the diffuser actually answers a forwarded command.
#include <Arduino.h>
#include <protocol_opcodes.h>
#include "Scheduler.h"
#include "Globals.h"   // DIF_Colorx

namespace DIF {

void Setup();
void Loop();
void TickAsyncSend();   // call every loop() iteration - advances the staged send
void UdpSet();

void StatusCheck(SCHED::TaskId taskId);
void IdleCheck(SCHED::TaskId taskId);

uint8_t ActiveModeSetting();   // EE_DIF_MODE_* for the active source, or 0xFF
void AutoOn(uint8_t eeModeSetting, const DIF_Colorx *colorOverride = NULL);
void AutoOff();
void PushLiveIfActive(const DIF_Colorx *colorOverride = NULL);

void TurnOn(uint8_t mode, uint8_t effect, const DIF_Colorx *colorOverride = NULL);
void Shutdown();
void Parfum(uint16_t minutes, uint8_t mode);
void ParfumCancel();
void ManualRefill();
void RequestStatus(bool verbose = true);
void RequestHistory();
void RemoveHistoryEntry(uint8_t index, uint8_t relayAppSeq);

// Relay bookkeeping - AppLink.cpp arms relayAppSeq (0xFF = none) before
// calling one of the command functions above, so this command's eventual
// ACK is forwarded to that app seq instead of being dropped on the floor.
void ArmRelay(uint8_t appSeq);
bool IsAppSeqPending(uint8_t appSeq);
void FlushPending(Proto::AckResult result);

const char* getEffectName(uint8_t effect);
const char* getModeName(uint8_t mode);
const char* getStripStatusName(uint8_t status);

} // namespace DIF
