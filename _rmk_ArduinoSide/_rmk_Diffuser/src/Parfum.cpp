#include "Parfum.h"
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Strip.h"

namespace PARFUM {

uint16_t parfumRemainingMin() {
    if (!g_parfumActive) return 0;
    long remMs = (long)(g_parfumEndMs - millis());
    if (remMs <= 0) return 1;
    return (uint16_t)((remMs + 59999L) / 60000L);
}

/**
 * @param minutes Duration, clamped to 1..PARFUM_MAX_MIN.
 * @param mode Requested diffuser mode (M1 CONT or M2 10 SEC). Honoured as
 *        requested - see this file's header comment, this is NOT coerced to
 *        a fixed run mode despite what the original firmware's comments claimed.
 *
 * Drives MODE via the normal cmdDiffuserOn() path with a violet PULSE seed
 * payload - latched as lastDn, so an unsolicited-shutdown auto-restart
 * (which re-sends g_lastDnPayload directly, bypassing this function) still
 * has a valid visual cue to fall back on. The live cue then switches to the
 * random-colour breathe effect via STRIP::startParfumBreathe().
 */
bool parfumStart(uint16_t minutes, uint8_t mode) {
    if (minutes < 1)              minutes = 1;
    if (minutes > PARFUM_MAX_MIN) minutes = PARFUM_MAX_MIN;

    if (!g_parfumActive) {
        g_parfumHavePrevDn  = g_haveLastDn;
        g_parfumPrevMode    = g_lastDnMode;
        g_parfumPrevPayload = g_lastDnPayload;
    }

    DnPayload p{};
    p.dual       = false;
    p.effect     = PARFUM_EFFECT;
    p.brightness = PARFUM_BRIGHTNESS;
    p.speedMs    = STRIP_EFFECT_TICK_MS;
    p.r1 = PARFUM_COLOR_R; p.g1 = PARFUM_COLOR_G; p.b1 = PARFUM_COLOR_B;
    // Colour duplicated into r2/g2/b2 on purpose - STRIP::applyDnPayload()
    // swaps the bases and non-dual animated effects read g_effectBase1 for
    // every LED. A single-colour payload with r2/g2/b2 left at zero would
    // pulse black. Filling both slots renders correctly whichever base the
    // effect frame picks.
    p.r2 = PARFUM_COLOR_R; p.g2 = PARFUM_COLOR_G; p.b2 = PARFUM_COLOR_B;

    g_parfumActive      = true;
    g_parfumSetMin      = minutes;
    g_parfumEndMs       = millis() + (unsigned long)minutes * PARFUM_UNIT_MS;
    g_parfumPendingKind = PARFUM_PEND_NONE;

    LOG::logMsg("PARFUM", "started - %u min, mode %s",
              minutes, MODE::modeName(mode));

    if (!MODE::cmdDiffuserOn(mode, p)) {
        g_parfumActive = false;
        g_parfumSetMin = 0;
        LOG::logMsg("PARFUM", "start rejected - window cancelled");
        return false;
    }
    STRIP::startParfumBreathe();
    return true;
}

/**
 * @param naturalExpiry true only from tickParfum(). Resolution order on
 *        natural expiry: (1) a turn-on queued while parfum insisted wins,
 *        if still valid; (2) else a queued shutdown always wins - an
 *        explicit off request must never be overridden by the pre-parfum
 *        revert; (3) else, with nothing queued, the mode/colour/effect
 *        active right before this window started, if still valid;
 *        (4) else shut down. Manual stops always shut down and drop both
 *        the queue and the snapshot.
 */
bool parfumStop(const char *reason, bool naturalExpiry) {
    if (!g_parfumActive) return true;
    g_parfumActive = false;
    g_parfumSetMin = 0;
    LOG::logMsg("PARFUM", "stopped (%s)", reason);

    ParfumPendingKind pending = g_parfumPendingKind;
    g_parfumPendingKind = PARFUM_PEND_NONE;

    if (naturalExpiry && pending == PARFUM_PEND_ON) {
        uint8_t   m = g_parfumPendingMode;
        DnPayload p = g_parfumPendingPayload;
        LOG::logMsg("PARFUM", "applying queued mode change: %s", MODE::modeName(m));
        if (MODE::cmdDiffuserOn(m, p)) return true;
        LOG::logMsg("PARFUM", "queued mode change no longer valid - shutting down instead");
    } else if (naturalExpiry && pending == PARFUM_PEND_NONE && g_parfumHavePrevDn) {
        uint8_t   m = g_parfumPrevMode;
        DnPayload p = g_parfumPrevPayload;
        g_parfumHavePrevDn = false;
        LOG::logMsg("PARFUM", "nothing queued - reverting to previous mode: %s", MODE::modeName(m));
        if (MODE::cmdDiffuserOn(m, p)) return true;
        LOG::logMsg("PARFUM", "previous mode no longer valid - shutting down instead");
    }
    g_parfumHavePrevDn = false;

    g_udpStatusReplyMode = UDP_STATUS_AFTER_SHUTDOWN;
    bool ok = MODE::cmdModeTarget(0);
    if (!ok) MODE::clearPendingStatusReply();
    return ok;
}

void tickParfum() {
    if (!g_parfumActive) return;
    if ((long)(millis() - g_parfumEndMs) < 0) return;
    parfumStop("timer expired", true);
}

} // namespace PARFUM
