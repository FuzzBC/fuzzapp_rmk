#pragma once
// Debug.h - local Serial + Telnet-mirrored diagnostic output. A much
// smaller replacement for the original's PRNT namespace (~872 lines):
// PRNT::_print()/formatMSG() carry over as Debug::print()/Debug::log()
// (Serial + TELNET::Mirror()), but PRNT::_Debug()'s giant per-module dump
// switch is NOT ported - it has no opcode in 01_PROTOCOL.md's table (the
// original 'K' debug command doesn't exist in the new wire protocol), and
// its one genuinely load-bearing case (a health summary) is already
// covered by the DIAG_HEALTH opcode in AppLink.cpp. The rest of that
// switch was a Serial-only convenience for whoever had a cable plugged
// in - Telnet (once enabled via SET_TELNET_ENABLE) plus this module's own
// Debug::log() calls from each module now serve that same purpose without
// needing a giant dispatcher to reach it.
#include <Arduino.h>

namespace Debug {

void print(const char *line);                          // raw line, no formatting/newline added - Serial (guarded) + Telnet mirror
void printSerial(const char *line);                    // Serial only (guarded), no Telnet - for callers with their own Telnet gating (e.g. AppLink::termMsgLog())
void log(const char *format, ...);                      // formatMSG-style, newline-terminated

} // namespace Debug
