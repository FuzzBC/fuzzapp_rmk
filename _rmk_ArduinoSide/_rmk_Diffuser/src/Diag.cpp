#include "Diag.h"
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Usage.h"
#include "Parfum.h"
#include "ProtoLink.h"
#include <stdarg.h>

namespace DIAG {

static const uint8_t LOG_LEVEL_INFO = 2;

/** Format one diagnostic line and send it both ways: Serial/Telnet (tagged,
    via LOG::logMsg) and Link A (as a LOG telemetry frame to the requester -
    see PROTOLINK::sendLog()). */
static void diagLine(const char *fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LOG::logMsg("DIAG", "%s", buf);
    PROTOLINK::sendLog(LOG_LEVEL_INFO, buf);
}

/** DIAG_HEALTH - one-shot health summary across every subsystem. */
void health() {
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    bool heapOk = (ESP.getFreeHeap() >= 8000);
    unsigned long sinceSaveS = (millis() - g_usageLastSaveMs) / 1000UL;

    diagLine("==== HEALTH SUMMARY ====");
    diagLine("WiFi     : %s (%d dBm)", wifiOk ? "OK" : "DOWN", (int)WiFi.RSSI());
    diagLine("Power    : %s%s", g_powered ? "ON" : "OFF", g_outOfWater ? " [OUT OF WATER]" : "");
    diagLine("Mode     : %s", MODE::modeName(g_mode));
    diagLine("Free heap: %u B%s", (unsigned)ESP.getFreeHeap(), heapOk ? "" : " [LOW]");
    diagLine("EEPROM   : last save %lus ago", sinceSaveS);
    diagLine("Parfum   : %s", g_parfumActive ? "ACTIVE" : "inactive");
    diagLine("Buzzer   : env=%.1f active=%s burst=%u totalBeeps=%lu",
              g_buzEnv, g_buzToneActive ? "yes" : "no", g_buzBurst, g_buzTotalCount);
    diagLine("History  : %u/%u cycles, %lu lifetime refills", g_refillHistoryCount, REFILL_HISTORY_SIZE, g_totalRefillCount);
    diagLine("==== %s ====", (wifiOk && heapOk) ? "ALL OK" : "ISSUES FOUND - see above");
}

/** DIAG_PARFUM_TRACE - every Parfum-related field in one shot. */
void parfumTrace() {
    diagLine("==== PARFUM TRACE ====");
    diagLine("active     : %s", g_parfumActive ? "yes" : "no");
    if (g_parfumActive) {
        diagLine("remaining  : %u of %u min set", PARFUM::parfumRemainingMin(), g_parfumSetMin);
    }
    const char *pendStr = (g_parfumPendingKind == PARFUM_PEND_ON)  ? "ON queued"  :
                          (g_parfumPendingKind == PARFUM_PEND_OFF) ? "OFF queued" : "none";
    diagLine("pending    : %s", pendStr);
    if (g_parfumHavePrevDn) {
        diagLine("havePrevDn : yes (reverts to %s on natural expiry, if nothing queued)", MODE::modeName(g_parfumPrevMode));
    } else {
        diagLine("havePrevDn : no (expiry shuts down if nothing queued)");
    }
    diagLine("haveLastDn : %s (last mode %s)", g_haveLastDn ? "yes" : "no", MODE::modeName(g_lastDnMode));
}

} // namespace DIAG
