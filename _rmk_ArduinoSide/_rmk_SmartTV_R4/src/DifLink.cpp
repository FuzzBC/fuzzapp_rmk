#include "DifLink.h"
#include "Globals.h"
#include "Led.h"
#include "Eeprom.h"
#include "Net.h"
#include "AppLink.h"
#include "Debug.h"
#include <Frame.h>
#include <string.h>

namespace DIF {

using namespace Proto;

static void idleTimerReset();
static void logPush(uint8_t triggerBy, uint8_t mode, uint8_t effect);

/* -- async, non-blocking outbound send -------------------------------------
   Mirrors the original's difAsyncMsg/TickAsyncSend() design: Send() used to
   busy-wait up to DIF_UDP_TIMEOUT ms TWICE per call (beginPacket + endPacket)
   with no yield - a real, reproduced bug under rapid-fire Dual Color spam
   (150 back-to-back commands wedged UDP responsiveness completely). Fix:
   only stage the frame here; TickAsyncSend() (called every loop()) advances
   the handshake one non-blocking step at a time. A newer stage simply
   overwrites whatever's still in flight - only the freshest diffuser state
   ever matters for these "push current state" messages. */
static uint8_t       g_asyncFrame[Proto::FRAME_MAX_BYTES];
static size_t         g_asyncLen     = 0;
static bool          g_asyncPending = false;
static uint8_t       g_asyncPhase   = 0;   // 0=idle 1=awaiting beginPacket() 2=awaiting endPacket()
static unsigned long g_asyncPhaseAt = 0;

static void stageFrame(Opcode opcode, uint8_t seq, uint8_t flags, const uint8_t *payload, size_t payloadLen) {
    if (!NET::IsConnected()) return;
    size_t n = BuildFrame(opcode, seq, flags, payload, payloadLen, g_asyncFrame, sizeof(g_asyncFrame));
    if (n == 0) return;
    g_asyncLen     = n;
    g_asyncPending = true;
    g_asyncPhase   = 0;
}

/** Advances the staged send by one non-blocking step. Call every loop()
    iteration. No-op when nothing is staged. */
void TickAsyncSend() {
    if (!g_asyncPending) return;

    if (g_asyncPhase == 0) {
        g_asyncPhase   = 1;
        g_asyncPhaseAt = millis();
    }

    if (g_asyncPhase == 1) {
        if (DIF_UDP.beginPacket(DIF_TARGET_IP, DIF_UDP_PORT)) {
            DIF_UDP.write(g_asyncFrame, g_asyncLen);
            g_asyncPhase   = 2;
            g_asyncPhaseAt = millis();
            return;
        }
        if (millis() - g_asyncPhaseAt >= DIF_UDP_TIMEOUT) {
            g_asyncPending = false;
            g_asyncPhase   = 0;
        }
        return;
    }

    if (g_asyncPhase == 2) {
        if (DIF_UDP.endPacket() == 1) {
            g_asyncPending = false;
            g_asyncPhase   = 0;
            return;
        }
        if (millis() - g_asyncPhaseAt >= DIF_UDP_TIMEOUT) {
            g_asyncPending = false;
            g_asyncPhase   = 0;
        }
    }
}

/* -- relay-pending queue ------------------------------------------------
   Same design as the original's DIFx::PendingCmdSeq/PendingAppSeq: queues
   a newly-sent command's {ourSeq, appSeq} pair so its eventual ACK (matched
   in Loop()'s dispatch) can be forwarded to the right app seq, even if
   another relayed command is sent before this one is answered. */
static uint8_t g_pendingRelayAppSeq = 0xFF;   // armed by ArmRelay(), consumed by the next SendCommand()

void ArmRelay(uint8_t appSeq) {
    g_pendingRelayAppSeq = appSeq;
}

static void pushPending(uint8_t cmdSeq, uint8_t appSeq) {
    if (appSeq == 0xFF) return;
    if (DIF::State.PendingCount >= DIF_PENDING_ACK_MAX) {
        for (uint8_t i = 1; i < DIF_PENDING_ACK_MAX; i++) {
            DIF::State.PendingCmdSeq[i - 1] = DIF::State.PendingCmdSeq[i];
            DIF::State.PendingAppSeq[i - 1] = DIF::State.PendingAppSeq[i];
        }
        DIF::State.PendingCount = DIF_PENDING_ACK_MAX - 1;
    }
    DIF::State.PendingCmdSeq[DIF::State.PendingCount] = cmdSeq;
    DIF::State.PendingAppSeq[DIF::State.PendingCount] = appSeq;
    DIF::State.PendingCount++;
}

static bool popPendingByCmdSeq(uint8_t cmdSeq, uint8_t &outAppSeq) {
    for (uint8_t i = 0; i < DIF::State.PendingCount; i++) {
        if (DIF::State.PendingCmdSeq[i] == cmdSeq) {
            outAppSeq = DIF::State.PendingAppSeq[i];
            for (uint8_t j = i + 1; j < DIF::State.PendingCount; j++) {
                DIF::State.PendingCmdSeq[j - 1] = DIF::State.PendingCmdSeq[j];
                DIF::State.PendingAppSeq[j - 1] = DIF::State.PendingAppSeq[j];
            }
            DIF::State.PendingCount--;
            return true;
        }
    }
    return false;
}

bool IsAppSeqPending(uint8_t appSeq) {
    for (uint8_t i = 0; i < DIF::State.PendingCount; i++) {
        if (DIF::State.PendingAppSeq[i] == appSeq) return true;
    }
    return false;
}

/** Flushes every still-pending relayed command with the given result (e.g.
    on a status no-reply timeout) and empties the queue. */
void FlushPending(Proto::AckResult result) {
    for (uint8_t i = 0; i < DIF::State.PendingCount; i++) {
        APP::RelayDiffuserAck(DIF::State.PendingAppSeq[i], result);
    }
    DIF::State.PendingCount = 0;
}

/** Stages a command frame with ACK_REQ set, queuing relayAppSeq (armed via
    ArmRelay(), 0xFF = none/autonomous) so the eventual ACK is forwarded to
    the right app seq. */
static void sendCommand(Opcode opcode, const uint8_t *payload, size_t len) {
    uint8_t seq = (uint8_t)(DIF::State.CmdSeq + 1);
    DIF::State.CmdSeq = seq;
    pushPending(seq, g_pendingRelayAppSeq);
    g_pendingRelayAppSeq = 0xFF;
    stageFrame(opcode, seq, (uint8_t)Flag::ACK_REQ, payload, len);
}

/** Opens the DIF UDP socket on DIF_UDP_PORT. Call after any WiFi reconnect. */
void UdpSet() {
    DIF_UDP.begin(DIF_UDP_PORT);
}

/** Initialises the diffuser UDP socket, then registers the two locked
    repeating background tasks: StatusCheck (polls status every
    DIF_STATUS_CHECK_S) and IdleCheck (idle-pulse state machine, every
    DIF_IDLE_CHECK_S). Call once in setup(). */
void Setup() {
    if (NET::IsConnected()) {
        UdpSet();
    }

    SCHED::AddTask("DIF_Setup", "StatusCheck", StatusCheck, SCHED::Unit::S, DIF_STATUS_CHECK_S, 0, true);
    SCHED::AddTask("DIF_Setup", "IdleCheck", IdleCheck, SCHED::Unit::S, DIF_IDLE_CHECK_S, 0, true);
}

/** Handles an ACK frame from the diffuser: relays the result to the app if
    this cmdSeq had a relay armed, and mirrors the result into
    DIF::State.CmdResult for local diagnostics. */
static void handleAck(const ParsedFrame &f) {
    AckPayload p{};
    if (!Unpack(f.payload, f.payloadLen, p)) return;
    DIF::State.CmdResult = p.result;

    uint8_t appSeq;
    if (popPendingByCmdSeq(f.seq, appSeq)) {
        APP::RelayDiffuserAck(appSeq, (Proto::AckResult)p.result);
    }
}

/** Handles an unsolicited/polled TELEM_DIFFUSER_STATUS frame - updates the
    local mirror and pushes the app's own status packet so the diffuser dot
    updates immediately without the app having to poll separately. */
static void handleStatus(const ParsedFrame &f) {
    TelemDiffuserStatusPayload s{};
    if (!Unpack(f.payload, f.payloadLen, s)) return;

    DIF::State.Mode              = s.mode;
    DIF::State.StripStatus       = s.strip;
    DIF::State.ParfumMin         = s.parfum_min;
    DIF::State.UsageAccumMin     = s.usage_min;
    DIF::State.UsageAvgMin       = s.avg_min;
    DIF::State.UsageRefillCount  = s.refill_count;
    DIF::State.UsageTotalRefills = s.lifetime_refills;
    DIF::State.LastStatusTime    = TimeNow;

    APP::updStatus("DIF::handleStatus");
}

/** Handles a TELEM_DIFFUSER_HISTORY reply - relayed straight through to the
    app unchanged, on the spot. Deliberately NOT cached in DIF::State
    (bulkier and rarely needed, no reason to keep a stale copy between
    requests, same reasoning as the original). */
static void handleHistory(const ParsedFrame &f) {
    APP::RelayDiffuserHistory(f.payload, f.payloadLen);
}

/** Receives and dispatches one incoming UDP frame from the diffuser per
    loop() iteration. Call every loop() iteration. */
void Loop() {
    if (!NET::Connected_Cached()) return;

    static uint8_t difPollPhase = 1;
    if (++difPollPhase % 3 != 1) return;

    int pkSize = DIF_UDP.parsePacket();
    if (pkSize <= 0) return;
    if (pkSize >= DIF_UDP_MAX_BUFFER_SIZE) { DIF_UDP.flush(); return; }

    static uint8_t recvBuff[DIF_UDP_MAX_BUFFER_SIZE];
    int len = DIF_UDP.read(recvBuff, DIF_UDP_MAX_BUFFER_SIZE - 1);
    if (len <= 0) return;

    ParsedFrame f = ParseFrame(recvBuff, (size_t)len);
    if (!f.ok) return;

    if (f.flags & (uint8_t)Flag::IS_ACK) { handleAck(f); return; }

    switch ((Opcode)f.opcode) {
        case Opcode::TELEM_DIFFUSER_STATUS:  handleStatus(f);  break;
        case Opcode::TELEM_DIFFUSER_HISTORY: handleHistory(f); break;
        default: break;   // LOG/others from the diffuser - no local sink yet
    }
}

/** Locked, repeating task: polls DIFFUSER_STATUS_QUERY every
    DIF_STATUS_CHECK_S and detects no-reply timeouts by comparing
    LastStatusTime against LastRequestTime from the previous tick. On
    timeout, marks DIF::State.Mode = DIF_MODE_NO_RESPONSE, drops the stale
    parfum mirror, and flushes every pending relayed command so the app
    isn't left waiting forever. */
void StatusCheck(SCHED::TaskId taskId) {
    (void)taskId;
    if (DIF::State.LastRequestTime != 0 && DIF::State.LastStatusTime < DIF::State.LastRequestTime) {
        DIF::State.Mode = DIF_MODE_NO_RESPONSE;
        DIF::State.ParfumMin = 0;

        APP::updStatus("DIF::StatusCheck");
        FlushPending(AckResult::UNREACHABLE);
    }

    DIF::State.LastRequestTime = TimeNow;
    RequestStatus(false);
}

/** Locked, repeating task driving the entire idle-pulse state machine
    (wait -> on -> off) - single task, no nested one-shot spawned for the
    on-duration; everything is timed off TimeNow snapshots checked on every
    tick of this same task. See the original DIF::IdleCheck()'s header
    comment for the full state description (unchanged logic here). */
void IdleCheck(SCHED::TaskId taskId) {
    (void)taskId;
    if (DIF::State.ParfumMin > 0) {
        idleTimerReset();
        return;
    }
    if (DIF::State.IdlePulseActive) {
        const uint8_t  onMin = EE::Get(EE_DIF_IDLE_ON_MIN);
        const uint32_t onMs  = (uint32_t)onMin * 60000UL;
        if ((TimeNow - DIF::State.IdlePulseStart) < onMs) return;

        Shutdown();
        DIF::State.IdlePulseActive = false;
        idleTimerReset();
        return;
    }

    const uint8_t waitMin = EE::Get(EE_DIF_IDLE_WAIT_MIN);
    if (waitMin == 0) return;
    const uint8_t idleMode = EE::Get(EE_DIF_IDLE_MODE);
    if (idleMode == 0) return;

    if (ActiveModeSetting() != 0xFF) {
        idleTimerReset();
        return;
    }

    const uint32_t idleMs = (uint32_t)waitMin * 60000UL;
    if ((TimeNow - DIF::State.IdleTimerReset) < idleMs) return;

    const uint8_t onMin = EE::Get(EE_DIF_IDLE_ON_MIN);
    if (onMin == 0) return;

    DIF::State.IdlePulseActive = true;
    DIF::State.IdlePulseStart  = TimeNow;
    logPush(difTrigIdle, idleMode, 0);
    DIF_Colorx black = { 0, 0, 0, 0, 0, 0 };
    TurnOn(idleMode, 0, &black);
}

/** Resolves which EE_DIF_MODE_* setting matches the currently active
    trigger source (priority TV > UDPRAW > MOTION > Ambient Mode).
    @return EE_DIF_MODE_* setting index, or 0xFF if nothing is active. */
uint8_t ActiveModeSetting() {
    if (TV::State.Status)     return EE_DIF_MODE_TV;
    if (UDPRAW::State.Status) return EE_DIF_MODE_UDPRAW;
    const bool motionActive = (MOTION::State.Status == motCOM || MOTION::State.Status == motBED);
    if (motionActive)  return EE_DIF_MODE_MOTION;
    if (APP::Am.Status)      return EE_DIF_MODE_AMBIENT;
    return 0xFF;
}

// Free functions, not member access, so these don't collide with the
// DIF::State.IdleTimerReset/LOG[] fields of the same name.
static void idleTimerReset() {
    DIF::State.IdleTimerReset  = TimeNow;
    DIF::State.IdlePulseActive = false;
}

static void logPush(uint8_t triggerBy, uint8_t mode, uint8_t effect) {
    for (int i = DIF_LOG_INDEX_MAX - 1; i > 0; i--) {
        DIF::State.LOG[i] = DIF::State.LOG[i - 1];
    }
    DIF::State.LOG[0].epoch     = RTC_TimeClient.getEpochTime();
    DIF::State.LOG[0].TriggerBy = triggerBy;
    DIF::State.LOG[0].Mode      = mode;
    DIF::State.LOG[0].Effect    = effect;
}

/** Shuts the diffuser off only when ALL activity sources have gone idle -
    no-op if any source is still active. Called by TV::Off(), UDPRAW::End(),
    MOTION's off-fade completion, ambient mode OFF. */
void AutoOff() {
    if (ActiveModeSetting() != 0xFF) return;
    idleTimerReset();
    Shutdown();
}

/** Turns the diffuser on via EE settings when any activity source becomes
    active. No-op if the mode stored in EE is 0 (disabled by the user for
    that source). @param colorOverride Explicit colour, or NULL to derive
    live from whatever's currently lit. */
void AutoOn(uint8_t eeModeSetting, const DIF_Colorx *colorOverride) {
    const uint8_t mode   = EE::Get(eeModeSetting);
    const uint8_t effect = EE::Get(EE_DIF_EFFECT);
    if (mode == 0) return;
    idleTimerReset();
    const uint8_t trig = (eeModeSetting == EE_DIF_MODE_TV)      ? difTrigTV      :
                         (eeModeSetting == EE_DIF_MODE_UDPRAW)   ? difTrigUDPRAW  :
                         (eeModeSetting == EE_DIF_MODE_MOTION)   ? difTrigMotion  : difTrigAmbient;
    logPush(trig, mode, effect);
    TurnOn(mode, effect, colorOverride);
}

/** Averages whatever the LED strip is currently showing into one or two RGB
    colours - same "non-black" lit-pixel test as the original. @param dual
    also split at the midpoint into r2/g2/b2 (left/right pair for the TV's
    own dual-colour mode). */
static void colorFromCurrentLEDs(bool dual, DIF_Colorx &out) {
    uint16_t sumR = 0, sumG = 0, sumB = 0, cnt = 0;
    uint16_t sumR2 = 0, sumG2 = 0, sumB2 = 0, cnt2 = 0;
    const int half = LED_NUM_TOTAL / 2;

    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        const CRGB &c = LED::State.CurrentColor[i];
        if ((c.r + c.g + c.b) == 0) continue;
        if (dual && i >= half) { sumR2 += c.r; sumG2 += c.g; sumB2 += c.b; cnt2++; }
        else                   { sumR  += c.r; sumG  += c.g; sumB  += c.b; cnt++;  }
    }

    out.r1 = cnt  ? (uint8_t)(sumR  / cnt)  : 255;
    out.g1 = cnt  ? (uint8_t)(sumG  / cnt)  : 255;
    out.b1 = cnt  ? (uint8_t)(sumB  / cnt)  : 255;
    if (dual) {
        out.r2 = cnt2 ? (uint8_t)(sumR2 / cnt2) : out.r1;
        out.g2 = cnt2 ? (uint8_t)(sumG2 / cnt2) : out.g1;
        out.b2 = cnt2 ? (uint8_t)(sumB2 / cnt2) : out.b1;
    }
}

/** Re-pushes mode/effect to the diffuser right now, if any source is
    active. No-op if nothing's active. Call after anything that changes
    EE_HB_DUAL_COLOR/EE_DIF_EFFECT/any EE_DIF_MODE_* while a source may
    already be running. */
void PushLiveIfActive(const DIF_Colorx *colorOverride) {
    const uint8_t activeModeSetting = ActiveModeSetting();
    if (activeModeSetting != 0xFF) {
        TurnOn(EE::Get(activeModeSetting), EE::Get(EE_DIF_EFFECT), colorOverride);
    }
}

/** Sends DIFFUSER_TURN_ON. Wire payload mirrors the Diffuser's own
    ProtoLink.cpp::handleTurnOn() parse exactly: mode(1) + dual-flag(1) +
    r1g1b1(3) [+ r2g2b2(3) if dual] + brightness(1) + effect(1) + speedMs(1).
    Dual vs single is decided by EE_HB_DUAL_COLOR - the diffuser only ever
    splits when the TV strip itself is in dual colour mode. Brightness is
    EE_DIF_BRIGHTNESS run through the same lux-compensation the TV strip
    uses, then rescaled from [0,LED_BRIGHTNESS_MAX] to [0,255] so the
    diffuser gets its full range. */
void TurnOn(uint8_t mode, uint8_t effect, const DIF_Colorx *colorOverride) {
    if (mode > DIF_MODE_MAX || effect > DIF_EFFECT_COUNT) return;

    const bool    dual       = EE::Get(EE_HB_DUAL_COLOR);
    const uint8_t rawBr      = (uint8_t)constrain(LED::getLuxBrightness(EE::Get(EE_DIF_BRIGHTNESS)), 0, LED_BRIGHTNESS_MAX);
    const uint8_t brightness = (uint8_t)constrain(map(rawBr, 0, LED_BRIGHTNESS_MAX, 0, 255), 0, 255);
    const uint8_t speedMs    = EE::Get(EE_DIF_SPEED);
    DIF_Colorx c;
    if (colorOverride) c = *colorOverride;
    else                colorFromCurrentLEDs(dual, c);

    uint8_t payload[11];
    payload[0] = mode;
    payload[1] = dual ? 1 : 0;
    payload[2] = c.r1; payload[3] = c.g1; payload[4] = c.b1;
    size_t off = 5;
    if (dual) { payload[5] = c.r2; payload[6] = c.g2; payload[7] = c.b2; off = 8; }
    payload[off]     = brightness;
    payload[off + 1] = effect;
    payload[off + 2] = speedMs;

    sendCommand(Opcode::DIFFUSER_TURN_ON, payload, off + 3);
}

/** Sends DIFFUSER_SHUTDOWN. While a parfum window is active, the diffuser
    queues this instead of applying it immediately. */
void Shutdown() {
    sendCommand(Opcode::DIFFUSER_SHUTDOWN, NULL, 0);
}

/** Sends DIFFUSER_PARFUM_START/CANCEL. @param minutes 1..DIF_PARFUM_MAX_MIN
    starts a timed run; 0 cancels (dispatched as PARFUM_CANCEL instead, per
    the opcode table's split vs. the original's single "Dp0000" overload).
    @param mode Dispense pattern, 1..DIF_MODE_MAX (ignored when cancelling). */
void Parfum(uint16_t minutes, uint8_t mode) {
    if (minutes > DIF_PARFUM_MAX_MIN) return;

    if (minutes == 0) {
        ParfumCancel();
        return;
    }

    DiffuserParfumStartPayload p{};
    p.minutes = minutes;
    p.mode    = mode;
    uint8_t buf[DIFFUSER_PARFUM_START_SIZE];
    size_t n = Pack(p, buf);
    sendCommand(Opcode::DIFFUSER_PARFUM_START, buf, n);
}

void ParfumCancel() {
    sendCommand(Opcode::DIFFUSER_PARFUM_CANCEL, NULL, 0);
}

/** Sends DIFFUSER_MANUAL_REFILL - tells the diffuser a manual refill just
    happened (banks the current usage cycle, resets the accumulator, clears
    any out-of-water alert). Applied instantly on the diffuser's side. */
void ManualRefill() {
    sendCommand(Opcode::DIFFUSER_MANUAL_REFILL, NULL, 0);
}

/** Sends DIFFUSER_STATUS_QUERY. @param verbose false for the routine
    periodic poll (StatusCheck) - true for an explicit app-requested check,
    which also forces every dirty-gated status field to resend regardless
    of whether it actually changed (see APP::updStatus's force path). */
void RequestStatus(bool verbose) {
    DiffuserStatusQueryPayload p{};
    p.verbose = verbose ? 1 : 0;
    uint8_t buf[DIFFUSER_STATUS_QUERY_SIZE];
    size_t n = Pack(p, buf);
    sendCommand(Opcode::DIFFUSER_STATUS_QUERY, buf, n);
}

/** Sends DIFFUSER_HISTORY_QUERY - on-demand only, reply relayed straight
    through to the app by handleHistory(), not cached locally. */
void RequestHistory() {
    sendCommand(Opcode::DIFFUSER_HISTORY_QUERY, NULL, 0);
}

/** Sends DIFFUSER_HISTORY_REMOVE for one one-based history index, relaying
    the diffuser's ack to the given app seq. */
void RemoveHistoryEntry(uint8_t index, uint8_t relayAppSeq) {
    ArmRelay(relayAppSeq);
    DiffuserHistoryRemovePayload p{};
    p.index = index;
    uint8_t buf[DIFFUSER_HISTORY_REMOVE_SIZE];
    size_t n = Pack(p, buf);
    sendCommand(Opcode::DIFFUSER_HISTORY_REMOVE, buf, n);
}

const char* getEffectName(uint8_t effect) {
    if (effect > DIF_EFFECT_COUNT) return "UNKNOWN";
    return (const char*)pgm_read_ptr(&DIF_EFFECT_NAMES[effect]);
}

const char* getModeName(uint8_t mode) {
    if (mode < 1 || mode > DIF_MODE_MAX) return "UNKNOWN";
    return (const char*)pgm_read_ptr(&DIF_MODE_NAMES[mode - 1]);
}

const char* getStripStatusName(uint8_t status) {
    if (status > 10) return "UNKNOWN";
    return (const char*)pgm_read_ptr(&DIF_STRIP_STATUS_NAMES[status]);
}

} // namespace DIF
