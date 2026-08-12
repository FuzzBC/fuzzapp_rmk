#include "ModeButton.h"
#include "Globals.h"
#include "Log.h"
#include "Strip.h"
#include "Wifi.h"
#include "ProtoLink.h"
#include "../_rmk_Diffuser_DEF.h"

namespace MODE {

void applyModeAdvance() {
    g_modePending = false;
    if (!g_powered) {
        g_powered    = true; g_mode = 1;
        g_powerOnMs  = millis();
        g_outOfWater = false;
        // g_shutdownRetried is NOT touched here - this path also fires mid-retry
        // (Buzzer's auto-restart steps mode 0->N through this same function),
        // so clearing it here would re-arm the one-shot retry before the retry
        // proved itself, causing an endless retry loop instead of one retry.
        // Only re-armed by cmdDiffuserOn() on a genuine new start.
    } else if (g_mode < MODE_MAX) {
        g_mode++;
    } else {
        applyShutdown();
        LOG::logMsg("MODE", "-> OFF");
    }
}

void applyModeTargetAdvance() {
    applyModeAdvance();
    continueModeTarget();
}

/** Only actually blanks the strip when OFF is the real destination - an
    out-of-water shutdown, or a direct Df/M0 request. If a mode-target chain
    is still active and aiming for a real mode (1..MODE_MAX), this is just
    the forced pass-through the physical MODE button takes on its way to a
    lower-numbered mode (hardware only ever advances forward, wrapping
    through OFF) - continueModeTarget() steps on immediately after. */
void applyShutdown() {
    g_powered     = false;
    g_mode        = 0;
    g_modePending = false;
    bool passingThrough = g_modeTargetActive && g_modeTargetWraps && !g_outOfWater;
    if (passingThrough) return;
    STRIP::stripOff();
    if (g_oowAlertActive) { g_oowAlertActive = false; LOG::logMsg("WATER", "out-of-water alert stopped (shutdown)"); }
}

void clearPendingStatusReply() {
    g_udpStatusReplyMode = UDP_STATUS_NONE;
}

/** DIFFUSER_TURN_ON - apply strip payload p immediately, drive MODE to
    target m in parallel. m must be 1..MODE_MAX (M0/off is invalid here -
    use the shutdown opcode instead). */
bool cmdDiffuserOn(uint8_t m, const DnPayload &p, bool isWaterRetry) {
    if (WIFI::wifiSequenceActive()) { LOG::logMsg("UDP_R", "turn-on request ignored - WiFi self-test sequence active"); return false; }
    if (!isWaterRetry) {
        g_shutdownRetried = false;
    }
    if (g_outOfWater) {
        LOG::logMsg("WATER", "turn-on request received - clearing out-of-water, retrying normal start");
        if (g_oowAlertActive) { g_oowAlertActive = false; LOG::logMsg("WATER", "out-of-water alert stopped (retrying)"); }
        g_outOfWater = false;
    }
    if (m == 0 || m > MODE_MAX) { LOG::logMsg("UDP_R", "turn-on request rejected - mode %u is invalid (must be 1-%u)", m, MODE_MAX); return false; }

    g_lastDnMode    = m;
    g_lastDnPayload = p;
    g_haveLastDn    = true;

    STRIP::applyDnPayload(p);

    if (g_powered && g_mode == m) {
        PROTOLINK::cmdStatusQuery();
        return true;
    }

    g_udpStatusReplyMode = UDP_STATUS_AFTER_TARGET;
    bool accepted = cmdModeTarget(m);
    if (!accepted) clearPendingStatusReply();
    return accepted;
}

bool cmdModeTarget(uint8_t t) {
    if (WIFI::wifiSequenceActive())  { LOG::logMsg("MODE", "mode change ignored - WiFi self-test sequence active"); return false; }
    if (t > MODE_MAX)         { LOG::logMsg("MODE", "mode %u rejected - out of range (0-%u)", t, MODE_MAX); return false; }
    if (t == 0) {
        if (g_parfumActive) {
            g_parfumActive      = false;
            g_parfumSetMin      = 0;
            g_parfumPendingKind = PARFUM_PEND_NONE;
            g_parfumHavePrevDn  = false;
            LOG::logMsg("PARFUM", "cancelled - switched off");
        }
        if (g_oowAlertActive) {
            g_oowAlertActive = false;
            LOG::logMsg("WATER", "out-of-water alert stopped (switched off)");
        }
        STRIP::stripOff();
        g_modeTargetWraps = false;
        if (!g_powered) {
            vlogMsg("MODE", "already off");
            sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
            return true;
        }
        LOG::logMsg("MODE", "shutdown requested (long-press)");
        g_shutdownCmdPending = true;
        pulseModeRaw(BTN_LONG_MS);
        return true;
    }
    if (g_powered && g_mode == t) {
        vlogMsg("MODE", "already %s", modeName(t));
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        return true;
    }
    g_modeTarget       = t;
    g_modeTargetActive = true;
    // Hardware only ever steps MODE forward, wrapping through OFF - a wrap
    // is only needed when the target is numerically lower than where we are
    // now. An ascending target reaches it directly, never touching OFF, so
    // a shutdown mid-ascending-sequence is a real drop, not a step.
    g_modeTargetWraps  = g_powered && (t < g_mode);
    { int steps = g_powered ? (int)t - (int)g_mode : (int)t;
      LOG::logMsg("MODE", "%s -> %s [%d step(s)]",
                modeName(g_mode), modeName(t), steps); }
    continueModeTarget();
    return true;
}

void continueModeTarget() {
    if (!g_modeTargetActive || g_modePending) return;
    if (g_mode == g_modeTarget) {
        g_modeTargetActive = false;
        LOG::logMsg("MODE", "now %s", modeName(g_mode));
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        return;
    }
    pulseModeShort();
    g_modePending  = true;
    g_modePendMs   = millis();
}

const char* modeName(uint8_t m) {
    if (m == 0) return "OFF";
    if (m <= MODE_MAX) return MODE_NAMES[m - 1];
    return "?";
}

void pulseModeRaw(unsigned long dur) {
    pinMode(MODE_PIN, OUTPUT);
    digitalWrite(MODE_PIN, LOW);
    g_modeReleaseMs  = millis() + dur;
    g_modePinPending = true;
}

void pulseModeShort() {
    pinMode(MODE_PIN, OUTPUT);
    digitalWrite(MODE_PIN, LOW);
    g_modeReleaseMs  = millis() + BTN_SHORT_MS;
    g_modePinPending = true;
}

void sendPendingStatusReply(UdpStatusReplyMode mode) {
    if (g_udpStatusReplyMode != mode) return;
    clearPendingStatusReply();
    PROTOLINK::cmdStatusQuery();
}

void tickModeTimeout() {
    if (g_modePending && (millis() - g_modePendMs) > MODE_CONFIRM_MS) {
        g_modePending      = false;
        g_modeTargetActive = false;
        g_modeTargetWraps  = false;
        LOG::logMsg("MODE", "pending press TIMED OUT - no buzzer confirm");
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
    }
}

void tickPinRelease() {
    unsigned long now = millis();
    if (g_modePinPending && now >= g_modeReleaseMs)  { pinMode(MODE_PIN,  INPUT); g_modePinPending = false; }
}

} // namespace MODE
