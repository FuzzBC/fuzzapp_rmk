#pragma once
// Log.h - debug output sink (Serial + Telnet), formatting, console reports.
// Ported 1:1 from _FuZzAPP_Diffuser.ino's LOG namespace.
#include <Arduino.h>

namespace LOG {

void cmdDebugInfo();
void cmdHelp();
void cmdQuickStatus();
void dbgPrintf(const char *fmt, ...);
void dbgWrite(const char *buf);
void dbgPrintln(const char *s);
char* formatMSG(const char *fmt, ...);

// LITE - always emitted: state changes, commands that did something, errors,
// boot/network events. Use vlogMsg() (Globals.h macro) for VERBOSE-only lines.
void logMsg(const char *tag, const char *fmt, ...);

void telnetWrite(const char *buf);
const char* uptimeStr(unsigned long ms);

} // namespace LOG
