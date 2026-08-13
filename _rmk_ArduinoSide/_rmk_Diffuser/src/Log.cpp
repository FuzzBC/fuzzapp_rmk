#include "Log.h"
#include "Globals.h"
#include "ModeButton.h"
#include "Strip.h"
#include "Usage.h"
#include "Parfum.h"
#include "../_rmk_Diffuser_DEF.h"
#include <stdarg.h>
#include <ctype.h>
#include <string.h>

namespace LOG {

void dbgPrintln(const char *s) { dbgWrite(s); dbgWrite("\n"); }

/** Serial/Telnet "D" - full diffuser/strip/buzzer/network debug dump. */
void cmdDebugInfo() {
    dbgPrintln("──────── DEBUG INFO ────────");
    dbgPrintf ("%-14s: %s\n", "Firmware", OTA_HOSTNAME);
    dbgPrintf ("%-14s: %s\n", "Uptime", uptimeStr(millis()));
    dbgPrintln("── Network ──");
    dbgPrintf ("%-14s: %s\n", "WiFi", WiFi.status() == WL_CONNECTED ? "connected" : "DOWN");
    dbgPrintf ("%-14s: %d dBm\n", "Signal", (int)WiFi.RSSI());
    dbgPrintf ("%-14s: %s\n", "IP", WiFi.localIP().toString().c_str());
    dbgPrintf ("%-14s: %s\n", "MAC", WiFi.macAddress().c_str());
    dbgPrintf ("%-14s: %u B\n", "Free heap", (unsigned)ESP.getFreeHeap());
    dbgPrintln("── Diffuser ──");
    dbgPrintf ("%-14s: %s\n", "Power", g_powered ? "ON" : "OFF");
    dbgPrintf ("%-14s: %s%s\n", "Mode", MODE::modeName(g_mode),
               g_outOfWater ? "  [OUT OF WATER]" : "");
    dbgPrintf ("%-14s: last settings=%s (have=%s)   retried=%s\n", "Auto-retry",
               MODE::modeName(g_lastDnMode), g_haveLastDn ? "yes" : "no", g_shutdownRetried ? "yes" : "no");
    if (g_parfumActive) {
        dbgPrintf("%-14s: ACTIVE - %u/%u min remaining\n", "Parfum", PARFUM::parfumRemainingMin(), g_parfumSetMin);
        if (g_parfumPendingKind == PARFUM_PEND_ON)
            dbgPrintf("%-14s: turn on -> %s\n", "Parfum queue", MODE::modeName(g_parfumPendingMode));
        else if (g_parfumPendingKind == PARFUM_PEND_OFF)
            dbgPrintf("%-14s: turn off\n", "Parfum queue");
        else if (g_parfumHavePrevDn)
            dbgPrintf("%-14s: none - expiry reverts to %s\n", "Parfum queue", MODE::modeName(g_parfumPrevMode));
        else
            dbgPrintf("%-14s: none - expiry shuts down (no prior mode)\n", "Parfum queue");
    } else
        dbgPrintf("%-14s: inactive\n", "Parfum");
    if (g_modeTargetActive)
        dbgPrintf("%-14s: -> %s, %s\n", "Targeting", MODE::modeName(g_modeTarget),
                  g_modePending ? "press sent, awaiting buzzer" : "advancing");
    dbgPrintf ("%-14s: env=%.1f   active=%s   burst=%u   quiet=%u   totalBeeps=%lu\n", "Buzzer",
               g_buzEnv, g_buzToneActive ? "yes" : "no", g_buzBurst, g_buzQuiet, g_buzTotalCount);
    dbgPrintln("── Strip ──");
    uint8_t code = STRIP::stripStatusCode();
    dbgPrintf ("%-14s: %u (%s)   dual=%s\n", "Status", code, STRIP::stripStatusName(code), g_effectDual ? "yes" : "no");
    dbgPrintf ("%-14s: %u/255\n", "Brightness", g_stripBrightness);
    dbgPrintf ("%-14s: %ums/frame\n", "Speed", g_effectTickMs);
    dbgPrintf ("%-14s: #%02X%02X%02X\n", "Base1", g_effectBase1.r, g_effectBase1.g, g_effectBase1.b);
    dbgPrintf ("%-14s: #%02X%02X%02X\n", "Base2", g_effectBase2.r, g_effectBase2.g, g_effectBase2.b);
    dbgPrintf ("%-14s: %s\n", "No-water alert", g_oowAlertActive ? "active" : "no");
    dbgPrintf ("%-14s: %s\n", "WiFi seq", g_wifiConnecting ? "connecting" : "idle");
    dbgPrintln("── Usage ──");
    dbgPrintf ("%-14s: %s (effective, mode-weighted)\n", "Since refill", uptimeStr((unsigned long)g_usageAccumSec * 1000UL));
    dbgPrintf ("%-14s: %u/%u cycles   avg=%s   lifetime refills=%lu\n", "History",
               g_refillHistoryCount, REFILL_HISTORY_SIZE,
               uptimeStr((unsigned long)USAGE::averageRefillCycleSec() * 1000UL), g_totalRefillCount);
    if (g_refillHistoryCount > 0) {
        uint32_t avg = USAGE::averageRefillCycleSec();
        dbgPrintf("%-14s: %s remaining until next refill (at current mode's rate)\n", "Estimate",
                  avg > g_usageAccumSec ? uptimeStr((unsigned long)(avg - g_usageAccumSec) * 1000UL) : "due now");
    }
    {
        unsigned long sinceSaveMs  = millis() - g_usageLastSaveMs;
        unsigned long sinceCheckMs = millis() - g_usageLastCheckMs;
        unsigned long nextCheckMs  = (sinceCheckMs < USAGE_EEPROM_CHECKPOINT_MS)
                                    ? (USAGE_EEPROM_CHECKPOINT_MS - sinceCheckMs) : 0UL;
        bool dirty = (g_usageAccumSec != g_usageLastSavedAccumSec);
        dbgPrintf("%-14s: last save %s ago (%s)\n", "EEPROM", uptimeStr(sinceSaveMs),
                  dirty ? "unsaved change pending" : "up to date, nothing to save");
        dbgPrintf("%-14s  next checkpoint in %s (skipped if still unchanged; sooner if a refill finalizes)\n", "",
                  uptimeStr(nextCheckMs));
    }
    dbgPrintln("─────────────────────────────");
}

void cmdHelp() {
    dbgPrintln("──────── COMMANDS ────────");
    dbgPrintln("M0-M4      Drive diffuser to mode (M0 = off, long-press shutdown)");
    dbgPrintln("P<min>     Parfum mode: P30 = insist 30 min (1-360), P0 = cancel + off");
    dbgPrintln("E          Cycle to next animated strip effect");
    dbgPrintln("Crrggbb    Set solid strip colour (manual test - full brightness)");
    dbgPrintln("S          Quick status (mode / strip)");
    dbgPrintln("D          Debug info (full state dump - mode, strip, buzzer, WiFi)");
#ifdef DIF_TEST_MODE
    dbgPrintln("T          Run self-test - sweeps every command (no WiFi/OTA), verbose");
#endif
    dbgPrintln("? or H     This help");
    dbgPrintln("Q          Close this telnet session (telnet only)");
    dbgPrintln("──────────────────────────");
}

void cmdQuickStatus() {
    dbgPrintf ("%-8s: %s\n", "Power", g_powered ? "ON" : "OFF");
    dbgPrintf ("%-8s: %s%s\n", "Mode", MODE::modeName(g_mode), g_outOfWater ? "  [OUT OF WATER]" : "");
    if (g_parfumActive)
        dbgPrintf("%-8s: ACTIVE - %u min remaining\n", "Parfum", PARFUM::parfumRemainingMin());
    uint8_t code = STRIP::stripStatusCode();
    dbgPrintf ("%-8s: %s\n", "Strip", STRIP::stripStatusName(code));
}

void dbgPrintf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbgWrite(buf);
}

void dbgWrite(const char *buf) {
    if (!buf) return;
    Serial.print(buf);
    telnetWrite(buf);
}

/* formatMSG - lightweight printf-style formatter (SmartTV spec subset).
   Specifiers: %s %~Ns %d %X %T %F %l. Returns a pointer to an internal
   static 512-byte buffer (not re-entrant) - see uptimeStr()'s comment for
   the same caveat and the call-site workaround it requires. */
char* formatMSG(const char *fmt, ...) {
    static char _b[512];
    int i = 0;
    va_list ap;
    va_start(ap, fmt);

    while (*fmt && i < 511) {
        if (*fmt != '%') { _b[i++] = *fmt++; continue; }
        if (*++fmt == '%') { _b[i++] = '%'; fmt++; continue; }

        bool lead = (*fmt == '~') && (++fmt, true);
        int  w    = 0;
        bool hasW = (*fmt >= '0' && *fmt <= '9');
        if (hasW) { w = atoi(fmt); while (*fmt >= '0' && *fmt <= '9') fmt++; }

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char*);
                int sl = strlen(s), sp = (hasW && w > sl) ? w - sl : 0;
                if (hasW &&  lead) for (int k = 0; k < sp && i < 511; k++) _b[i++] = ' ';
                i += snprintf(&_b[i], sizeof(_b)-i, "%s", s);
                if (hasW && !lead) for (int k = 0; k < sp && i < 511; k++) _b[i++] = ' ';
                break;
            }
            case 'd': { int v = va_arg(ap, int);
                i += hasW ? snprintf(&_b[i], sizeof(_b)-i, "%0*d", w, v)
                          : snprintf(&_b[i], sizeof(_b)-i, "%d",   v); break; }
            case 'X': { int v = va_arg(ap, int);
                i += hasW ? snprintf(&_b[i], sizeof(_b)-i, "%0*X", w, v)
                          : snprintf(&_b[i], sizeof(_b)-i, "%02X", v); break; }
            case 'T': { bool b = (bool)va_arg(ap, int);
                i += snprintf(&_b[i], sizeof(_b)-i, "%s", b ? "True" : "False"); break; }
            case 'F': { double f = va_arg(ap, double);
                i += hasW ? snprintf(&_b[i], sizeof(_b)-i, "%.*f", w, f)
                          : snprintf(&_b[i], sizeof(_b)-i, "%f",   f); break; }
            case 'l': { unsigned long ul = va_arg(ap, unsigned long);
                i += hasW ? snprintf(&_b[i], sizeof(_b)-i, "%0*lu", w, ul)
                          : snprintf(&_b[i], sizeof(_b)-i, "%lu",   ul); break; }
            default:
                if (i < 511) _b[i++] = '%';
                if (i < 511) _b[i++] = *fmt;
        }
        fmt++;
    }
    _b[i] = '\0';
    va_end(ap);
    return _b;
}

/** True if msg reads like a problem report, not routine state - used to
    pick ERR vs INF below (see logMsg()). */
static bool looksLikeProblem(const char *msg) {
    static const char *const markers[] = {
        "error", "fail", "invalid", "bad ", "reject", "unsup", "lock", "no water", "timeout"
    };
    char lower[224];
    size_t n = 0;
    for (; msg[n] && n < sizeof(lower) - 1; n++) lower[n] = (char)tolower((unsigned char)msg[n]);
    lower[n] = '\0';
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++)
        if (strstr(lower, markers[i])) return true;
    return false;
}

/** Line format: "[LEVEL] [TAG] message" - matches the SmartTV RMK
    firmware's termMsgLog() output ("[%s] [%s::%s] %s", level, ns, func,
    text - see AppLink.cpp) instead of this board's own older "tag # msg" /
    "tag ! msg" convention (itself a port of the ORIGINAL SmartTV
    firmware's PRNT::formatMSG style, now superseded on that side). tag
    plays the same role SmartTV's ns (module name) does; this board never
    tracked a separate per-function name the way SmartTV's __func__-driven
    DLOG_*() macros do, so that part of the bracket is simply omitted
    rather than faked. Level is still inferred from looksLikeProblem()
    (ERR vs INF) - callers weren't updated to pass one explicitly, so
    behaviour is otherwise unchanged, just re-skinned to read the same as
    the SmartTV's Serial/Telnet output now that both boards' Telnet
    sessions can reasonably be watched side by side. */
void logMsg(const char *tag, const char *fmt, ...) {
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbgWrite(formatMSG("[%s] [%s] %s\n", looksLikeProblem(buf) ? "ERR" : "INF", tag, buf));
}

/** Telnet terminals expect CRLF; translate LF -> CRLF so lines don't stack
    at the same column. Serial is unaffected. */
void telnetWrite(const char *buf) {
    if (!buf) return;
    if (!g_telnetCli || !g_telnetCli.connected()) return;
    for (const char *p = buf; *p; ++p) {
        if (*p == '\n') g_telnetCli.write('\r');
        g_telnetCli.write(*p);
    }
}

/** Format ms as "Dd HHh MMm SSs" into a static buffer (not re-entrant - see
    cmdDebugInfo()'s two-separate-calls workaround). */
const char* uptimeStr(unsigned long ms) {
    static char buf[24];
    unsigned long s = ms / 1000UL;
    unsigned long d = s / 86400UL;
    unsigned long h = (s % 86400UL) / 3600UL;
    unsigned long m = (s % 3600UL)  / 60UL;
    unsigned long r = s % 60UL;
    snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus", d, h, m, r);
    return buf;
}

} // namespace LOG
