#include "Telnet.h"
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Strip.h"
#include "Parfum.h"
#include "ProtoLink.h"
#include "Test.h"
#include "../_rmk_Diffuser_DEF.h"

namespace TELNET {

static bool parseCmd(const String &s, char prefix, uint8_t maxVal, uint8_t &out) {
    if (s.length() < 2 || s.charAt(0) != prefix) return false;
    uint16_t v = 0;
    for (uint8_t i = 1; i < s.length(); i++) {
        char c = s.charAt(i);
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
        if (v > maxVal) return false;
    }
    out = (uint8_t)v;
    return true;
}

void handleCmd(String s) {
    s.trim(); s.replace("\r", ""); s.toUpperCase();
    if (!s.length()) return;
    uint8_t v = 0;
    if      (parseCmd(s, 'M', MODE_MAX, v)) MODE::cmdModeTarget(v);
    else if (s.charAt(0) == 'P' && s.length() >= 2) {
        long mins = s.substring(1).toInt();
        bool numeric = true;
        for (uint8_t i = 1; i < s.length(); i++)
            if (s.charAt(i) < '0' || s.charAt(i) > '9') { numeric = false; break; }
        if (!numeric || mins > PARFUM_MAX_MIN) LOG::logMsg("CMD", "bad parfum minutes: [%s] (0-%d)", s.c_str(), PARFUM_MAX_MIN);
        else if (mins == 0) { if (g_parfumActive) PARFUM::parfumStop("console"); else vlogMsg("PARFUM", "already inactive"); }
        else                PARFUM::parfumStart((uint16_t)mins);
    }
    else if (s == "E") { STRIP::stripApplyEffectNext(); LOG::logMsg("STRIP", "effect (manual)"); }
    else if (s.charAt(0) == 'C' && s.length() == 7) {
        unsigned int rgb = 0;
        if (sscanf(s.c_str() + 1, "%6x", &rgb) == 1) {
            DnPayload p{}; p.dual = false; p.effect = 0;
            p.brightness = 255; p.speedMs = STRIP_EFFECT_TICK_MS;
            p.r1 = (rgb >> 16) & 0xFF; p.g1 = (rgb >> 8) & 0xFF; p.b1 = rgb & 0xFF;
            STRIP::applyDnPayload(p);
        } else LOG::logMsg("CMD", "bad colour: [%s]", s.c_str());
    }
    else if (s == "S")            { PROTOLINK::cmdStatusQuery(); LOG::cmdQuickStatus(); }
    else if (s == "D")            LOG::cmdDebugInfo();
    else if (s == "?" || s == "H") LOG::cmdHelp();
#ifdef DIF_TEST_MODE
    else if (s == "T")            TEST::runSelfTest();
#endif
    else LOG::logMsg("CMD", "unknown: [%s] - send ? or H for help", s.c_str());
}

void tickTelnet() {
    if (g_telnetSrv.hasClient()) {
        WiFiClient nc = g_telnetSrv.available();
        if (nc) {
            if (g_telnetCli && g_telnetCli.connected()) {
                LOG::logMsg("TELNET", "new client connecting - dropping previous one");
                g_telnetCli.stop();
            }
            g_telnetCli = nc;
            g_telnetCli.setNoDelay(true);
            g_telnetBuf = "";
            LOG::dbgPrintf ("\n──────── %s TELNET ────────\n", OTA_HOSTNAME);
            LOG::dbgPrintf ("%-10s: %s\n", "IP", WiFi.localIP().toString().c_str());
            LOG::dbgPrintf ("%-10s: %s\n", "MAC", WiFi.macAddress().c_str());
            LOG::dbgPrintf ("%-10s: %d dBm\n", "Signal", (int)WiFi.RSSI());
            LOG::dbgPrintf ("%-10s: %s\n", "Uptime", LOG::uptimeStr(millis()));
            LOG::dbgPrintf ("%-10s: %s%s\n", "Mode", MODE::modeName(g_mode),
                       g_outOfWater ? "  [OUT OF WATER]" : "");
            LOG::dbgPrintf ("%-10s: %s\n", "Strip", STRIP::stripStatusName(STRIP::stripStatusCode()));
            LOG::cmdHelp();
        }
    }

    if (!g_telnetCli || !g_telnetCli.connected()) {
        if (g_telnetCli) g_telnetCli.stop();
        return;
    }
    while (g_telnetCli.available()) {
        char c = g_telnetCli.read();
        if (c == '\r') continue;
        if (c == '\n') {
            String s = g_telnetBuf; g_telnetBuf = "";
            String q = s; q.trim(); q.toUpperCase();
            if (q == "Q") {
                LOG::telnetWrite("──────── session closed ────────\n");
                g_telnetCli.stop();
                g_telnetBuf = "";
                LOG::logMsg("TELNET", "client quit (Q)");
                return;
            }
            handleCmd(s);
        } else {
            g_telnetBuf += c;
            if (g_telnetBuf.length() > 128)
                g_telnetBuf = g_telnetBuf.substring(g_telnetBuf.length() - 128);
        }
    }
    if (!g_telnetCli.connected()) { g_telnetCli.stop(); LOG::logMsg("TELNET", "disconnected"); }
}

} // namespace TELNET
