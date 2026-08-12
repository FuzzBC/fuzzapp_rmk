#include "Buzzer.h"
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Strip.h"
#include "Usage.h"
#include "Wifi.h"
#include "ProtoLink.h"
#include "../_rmk_Diffuser_DEF.h"

namespace BUZZ {

/** Dispatch a completed burst: 1 beep = mode confirm, >1 = shutdown. */
void buzzerFinalizeBurst() {
    uint8_t n = g_buzBurst;
    g_buzBurst = 0;

    if (WIFI::wifiSequenceActive()) {
        if (n == 1 && g_modePending) {
            g_modePending = false;
            if (g_mode < MODE_MAX) g_mode++;
            g_seqNextMs = millis() + WIFI_SEQ_MODE_GAP_MS;
            vlogMsg("SEQ", "burst %u beep(s) - step confirmed, now %s", n, MODE::modeName(g_mode));
        } else if (n > 1) {
            LOG::logMsg("SEQ", "burst %u beep(s) - shutdown detected, mirror calibrated", n);
            g_modePending = false;
            MODE::applyShutdown();
            g_seqPhase = SEQ_DONE;
        } else {
            vlogMsg("SEQ", "burst %u beep(s) - unsolicited/unmapped, ignored", n);
        }
        return;
    }

    vlogMsg("BUZZ", "burst %u beep(s)", n);
    if (n == 1) {
        if (g_modePending) MODE::applyModeTargetAdvance();
        else               vlogMsg("BUZZ", "unsolicited beep");
    } else if (n > 1) {
        bool commanded = g_shutdownCmdPending;
        g_shutdownCmdPending = false;
        bool wasFast = !commanded && g_powered && (millis() - g_powerOnMs) < WATER_OUT_TIMEOUT_MS;

        bool wasModeTarget    = g_modeTargetActive;
        bool targetWasM0      = (g_modeTarget == 0);
        bool expectedPassThru = wasModeTarget && !targetWasM0 && g_modeTargetWraps;
        if (wasModeTarget && !expectedPassThru) g_modeTargetActive = false;

        MODE::applyShutdown();

        if (expectedPassThru) {
            g_modePending = false;
            MODE::continueModeTarget();
        }

        if (!expectedPassThru) {
            MODE::sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
        }

        if (commanded) {
            LOG::logMsg("MODE", "shutdown confirmed");
            g_haveLastDn = false;
        } else if (expectedPassThru) {
            vlogMsg("MODE", "passing through OFF, continuing to %s", MODE::modeName(g_modeTarget));
        } else if (wasModeTarget && targetWasM0) {
            LOG::logMsg("MODE", "shutdown complete");
            g_haveLastDn = false;
        } else if (wasFast && !g_shutdownRetried && g_haveLastDn) {
            g_shutdownRetried = true;
            LOG::logMsg("WATER", "unexpected shutdown - retrying %s with last settings", MODE::modeName(g_lastDnMode));
            MODE::cmdDiffuserOn(g_lastDnMode, g_lastDnPayload, true);
        } else if (wasFast) {
            LOG::logMsg("WATER", "out of water - shut down again right after restart");
            g_outOfWater = true;
            USAGE::finalizeRefillCycle();
            if (g_parfumActive) {
                g_parfumActive      = false;
                g_parfumSetMin      = 0;
                g_parfumPendingKind = PARFUM_PEND_NONE;
                g_parfumHavePrevDn  = false;
                LOG::logMsg("PARFUM", "cancelled - out of water");
            }
            STRIP::startOowAlert();
            PROTOLINK::cmdStatusQuery();
        } else if (g_parfumActive && g_haveLastDn && !g_modeTargetActive) {
            LOG::logMsg("PARFUM", "unexpected shutdown - restarting, %u min remaining", (unsigned)g_parfumSetMin);
            MODE::cmdDiffuserOn(g_lastDnMode, g_lastDnPayload);
        } else {
            LOG::logMsg("MODE", "unexpected shutdown");
        }
    } else {
        vlogMsg("BUZZ", "unmapped burst");
    }
}

void tickBuzzer() {
    int   raw = analogRead(BUZZER_PIN);
    int   dev = abs(raw - g_buzBaseline);
    g_buzEnv  = max((float)dev, g_buzEnv * BUZZER_ENV_DECAY);

#ifdef DIF_DEBUG_BUZZER_RAW
    LOG::logMsg("BUZZ", "raw=%d dev=%d env=%.1f act=%d str=%u",
              raw, dev, g_buzEnv, g_buzToneActive, g_buzQuiet);
#endif

    if (!g_buzToneActive && g_buzEnv > BUZZER_ON_THR) {
        g_buzToneActive = true;
        g_buzQuiet      = 0;
    }
    if (g_buzToneActive) {
        g_buzQuiet = (dev < BUZZER_OFF_THR) ? g_buzQuiet + 1 : 0;
        if (g_buzQuiet >= BUZZER_QUIET_CONF) {
            g_buzToneActive = false;
            g_buzQuiet      = 0;
            g_buzEnv        = dev;
            g_buzTotalCount++;
            g_buzBurst++;
            g_buzLastEndMs  = millis();
        }
    }
    if (g_buzBurst > 0 && !g_buzToneActive &&
        (millis() - g_buzLastEndMs) > BUZZER_BURST_GAP_MS) {
        buzzerFinalizeBurst();
    }
}

} // namespace BUZZ
