#pragma once
// AppLink.h - Link B binary protocol (port 8472 UDP, MQTT fallback), talks
// to the phone app / _rmk_TestMode. Adapted from _FuZzAPP_SmartTV_R4.ino's
// APP namespace (~2220 lines) onto _rmk_Shared's generated codec
// (protocol_opcodes.h) + Frame.h - see 01_PROTOCOL.md. Same shape as the
// Diffuser's ProtoLink.cpp: dispatch(opcode,payload,len) + dirty-gated
// telemetry senders, now on the app-facing side. The ASCII-specific
// plumbing (HexByte/HexNibble, the 'LK' delta-colour sub-protocol,
// termMsgLog's '*' envelope) is gone - TELEM_COLOR_SYNC/LOG opcodes carry
// the same information as native binary fields instead.
//
// Stays namespace APP (not a new "AppLink" namespace) - every other
// already-ported module (Led/Tv/Motion/Lisens/Udpraw/Eeprom/Net/...) calls
// APP::updStatus()/updDeltaColors()/updColors_Force()/updLux()/
// Notify_Saved()/termMsgLog()/Am/Ard/State directly, matching the
// original's own namespacing; splitting the wire-protocol code into this
// file doesn't change which namespace it lives in, same as TV's state
// (Globals.h) and behaviour (Tv.cpp) sharing `namespace TV`.
#include <Arduino.h>
#include <protocol_opcodes.h>
#include "Scheduler.h"

namespace APP {

void Setup();
void Loop();
void TickColorSync(uint32_t now);   // rate-matched delta colour flush, call every loop()

// Entry point for a fully-received binary frame's payload, called by both
// UDP's own Loop() and MQTT::Dispatch() - both transports carry identical
// v1 frame bytes (01_PROTOCOL.md SS5).
void dispatchFrame(const uint8_t *data, int len, uint8_t transport);

void UdpSet();
void RefreshSelectedCache();

// -- telemetry push (dirty-gated, same shape as the original's upd*() family) --
void updStatus(const char *by);
void updSettings();
void updColors_Force();
void updDeltaColors();
bool updLux();
void Notify_Saved(uint8_t result);

void termMsgLog(uint8_t level, uint8_t source, const char *ns, const char *func, const char *format, ...);

Proto::AckResult SaveMqttCredentials(const char *user, const char *pass);

// -- diffuser relay bridge (called from DifLink.cpp) --
void RelayDiffuserAck(uint8_t appSeq, Proto::AckResult result);
void RelayDiffuserHistory(const uint8_t *payload, size_t len);

int getFreeRam();

} // namespace APP
