#include "ProtoLink.h"
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Strip.h"
#include "Usage.h"
#include "Parfum.h"
#include "Diag.h"
#include "Wifi.h"
#include "../_rmk_Diffuser_DEF.h"
#include <Frame.h>   // angle-bracket - see Globals.h's comment on why

namespace PROTOLINK {

using namespace Proto;

// -- outbound plumbing -------------------------------------------------

/** Send one frame to the cached sender IP:UDP_PORT. Single attempt, no
    spin-retry - mirrors the original PROTO::udpSend()'s reasoning: a retry
    loop could busy-wait loop() long enough to miss or miscount a real
    buzzer confirmation beep. A dropped reply is picked up by the next poll. */
static void sendFrame(Opcode opcode, uint8_t seq, uint8_t flags,
                       const uint8_t *payload, size_t payloadLen) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (g_udpSenderIP == IPAddress(0, 0, 0, 0)) return;

    uint8_t frame[FRAME_MAX_BYTES];
    size_t n = BuildFrame(opcode, seq, flags, payload, payloadLen, frame, sizeof(frame));
    if (n == 0) return;
    if (!g_udp.beginPacket(g_udpSenderIP, UDP_PORT)) return;
    g_udp.write(frame, n);
    g_udp.endPacket();
}

static void sendTelemetry(Opcode opcode, const uint8_t *payload, size_t payloadLen) {
    sendFrame(opcode, 0, (uint8_t)Flag::IS_TELEMETRY, payload, payloadLen);
}

static void sendAck(uint8_t seq, uint8_t result) {
    uint8_t payload[1] = { result };
    sendFrame(Opcode::ACK, seq, (uint8_t)Flag::IS_ACK, payload, 1);
}

void sendLog(uint8_t level, const char *text) {
    size_t textLen = strlen(text);
    if (textLen > FRAME_MAX_BYTES - FRAME_OVERHEAD_BYTES - 2) textLen = FRAME_MAX_BYTES - FRAME_OVERHEAD_BYTES - 2;
    uint8_t payload[FRAME_MAX_BYTES];
    payload[0] = level;
    payload[1] = 0;  // source - no per-module taxonomy on this board yet, see Diag.cpp
    memcpy(payload + 2, text, textLen);
    sendTelemetry(Opcode::LOG, payload, 2 + textLen);
}

/** DIFFUSER_STATUS_QUERY reply - mirrors the original "Ds"/"Dc" reply
    exactly field for field, now as a binary TELEM_DIFFUSER_STATUS frame. */
void cmdStatusQuery(bool silent) {
    TelemDiffuserStatusPayload s{};
    s.mode             = g_outOfWater ? STATUS_MODE_OUT_OF_WATER : g_mode;
    s.strip             = STRIP::stripStatusCode();
    s.parfum_min        = PARFUM::parfumRemainingMin();
    s.usage_min         = (uint16_t)min((uint32_t)(g_usageAccumSec / 60UL), (uint32_t)0xFFFF);
    s.avg_min           = (uint16_t)min((uint32_t)(USAGE::averageRefillCycleSec() / 60UL), (uint32_t)0xFFFF);
    s.refill_count      = g_refillHistoryCount;
    s.lifetime_refills  = (uint16_t)min(g_totalRefillCount, (uint32_t)0xFFFF);

    uint8_t buf[TELEM_DIFFUSER_STATUS_SIZE];
    size_t n = Pack(s, buf);
    sendTelemetry(Opcode::TELEM_DIFFUSER_STATUS, buf, n);

    if (silent) return;
    vlogMsg("UDP_S", "status reply sent (mode=%s%s%s strip=%u(%s) dual=%s br=%u/255 speed=%ums usage=%umin avg=%umin refills=%u/%u total=%u)",
              MODE::modeName(g_mode),
              g_outOfWater ? " [OUT OF WATER]" : "",
              g_parfumActive ? " [PARFUM]" : "",
              s.strip, STRIP::stripStatusName(s.strip),
              g_effectDual ? "yes" : "no", g_stripBrightness, g_effectTickMs,
              s.usage_min, s.avg_min, g_refillHistoryCount, REFILL_HISTORY_SIZE, s.lifetime_refills);
}

/** DIFFUSER_HISTORY_QUERY reply - count + up to 10x minutes, oldest-first,
    unused trailing slots zero. Same "raw" hand-codec as the original ASCII
    "Dh" reply (see protocol_table.json's note on this opcode). */
static void cmdHistoryQuery() {
    uint8_t payload[1 + REFILL_HISTORY_SIZE * 2];
    payload[0] = g_refillHistoryCount;
    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
        uint32_t minutes = 0;
        if (i < g_refillHistoryCount) {
            uint8_t idx = (g_refillHistoryCount < REFILL_HISTORY_SIZE)
                        ? i
                        : (uint8_t)((g_refillHistoryNext + i) % REFILL_HISTORY_SIZE);
            minutes = g_refillHistory[idx] / 60UL;
        }
        uint16_t m = (uint16_t)min(minutes, (uint32_t)0xFFFF);
        payload[1 + i * 2]     = (uint8_t)(m >> 8);
        payload[1 + i * 2 + 1] = (uint8_t)(m & 0xFF);
    }
    sendTelemetry(Opcode::TELEM_DIFFUSER_HISTORY, payload, sizeof(payload));
    vlogMsg("UDP_S", "history reply sent (%u/%u entries)", g_refillHistoryCount, REFILL_HISTORY_SIZE);
}

// -- inbound dispatch ---------------------------------------------------

static void handleTurnOn(const uint8_t *payload, size_t len) {
    if (len < 8) { LOG::logMsg("UDP_R", "turn-on request rejected - payload too short"); g_ackResult = AckResult::REJECTED; return; }
    uint8_t mode = payload[0];
    bool    dual = payload[1] != 0;
    size_t  need = dual ? 11 : 8;
    if (len < need) { LOG::logMsg("UDP_R", "turn-on request rejected - payload too short for dual=%s", dual ? "yes" : "no"); g_ackResult = AckResult::REJECTED; return; }

    DnPayload p{};
    p.dual = dual;
    p.r1 = payload[2]; p.g1 = payload[3]; p.b1 = payload[4];
    size_t off = 5;
    if (dual) { p.r2 = payload[5]; p.g2 = payload[6]; p.b2 = payload[7]; off = 8; }
    p.brightness = payload[off];
    p.effect     = payload[off + 1];
    p.speedMs    = payload[off + 2];

    if (p.effect > STRIP_EFFECT_COUNT) { LOG::logMsg("UDP_R", "turn-on request rejected - effect %u out of range", p.effect); g_ackResult = AckResult::REJECTED; return; }
    if (mode == 0 || mode > MODE_MAX) { LOG::logMsg("UDP_R", "turn-on request rejected - mode %u is invalid (must be 1-%u)", mode, MODE_MAX); g_ackResult = AckResult::REJECTED; return; }

    if (g_parfumActive) {
        g_parfumPendingKind    = PARFUM_PEND_ON;
        g_parfumPendingMode    = mode;
        g_parfumPendingPayload = p;
        LOG::logMsg("PARFUM", "queued mode change: %s - %u min remaining", MODE::modeName(mode), PARFUM::parfumRemainingMin());
        g_ackResult = AckResult::LOCKED;
        return;
    }

    LOG::logMsg("UDP_R", "turn-on request: mode %s, effect %s, brightness %u/255", MODE::modeName(mode), STRIP::effectName(p.effect), p.brightness);
    if (!MODE::cmdDiffuserOn(mode, p)) g_ackResult = AckResult::LOCKED;
}

static void dispatch(uint8_t opcode, const uint8_t *payload, size_t len) {
    switch ((Opcode)opcode) {
        case Opcode::KEEPALIVE:
            break;  // liveness only, no state change

        case Opcode::HELLO:
            break;  // v1 both ends, nothing to negotiate yet - ACK_OK below

        case Opcode::DIAG_HEALTH:
            DIAG::health();
            break;

        case Opcode::DIAG_PARFUM_TRACE:
            DIAG::parfumTrace();
            break;

        case Opcode::DIFFUSER_STATUS_QUERY: {
            DiffuserStatusQueryPayload q{};
            bool verbose = Unpack(payload, len, q) ? (q.verbose != 0) : true;
            cmdStatusQuery(!verbose);
            break;
        }

        case Opcode::DIFFUSER_HISTORY_QUERY:
            cmdHistoryQuery();
            break;

        case Opcode::DIFFUSER_HISTORY_REMOVE: {
            DiffuserHistoryRemovePayload q{};
            if (!Unpack(payload, len, q) || !USAGE::removeHistoryEntryAt(q.index)) {
                LOG::logMsg("UDP_R", "history entry-remove rejected - index out of range");
                g_ackResult = AckResult::REJECTED;
                break;
            }
            cmdStatusQuery();
            break;
        }

        case Opcode::DIFFUSER_MANUAL_REFILL:
            LOG::logMsg("USAGE", "manual refill - banking %lus, resetting accumulator", g_usageAccumSec);
            if (g_oowAlertActive) { g_oowAlertActive = false; LOG::logMsg("WATER", "out-of-water alert cleared (manual refill)"); }
            g_outOfWater = false;
            USAGE::finalizeRefillCycle();
            cmdStatusQuery();
            break;

        case Opcode::DIFFUSER_SHUTDOWN:
            if (g_parfumActive) {
                g_parfumPendingKind = PARFUM_PEND_OFF;
                LOG::logMsg("PARFUM", "turn-off queued - %u min remaining", PARFUM::parfumRemainingMin());
                cmdStatusQuery();
                g_ackResult = AckResult::LOCKED;
                break;
            }
            LOG::logMsg("UDP_R", "shutdown request - turning off");
            g_udpStatusReplyMode = UDP_STATUS_AFTER_SHUTDOWN;
            if (!MODE::cmdModeTarget(0)) { MODE::clearPendingStatusReply(); g_ackResult = AckResult::LOCKED; }
            break;

        case Opcode::DIFFUSER_TURN_ON:
            handleTurnOn(payload, len);
            break;

        case Opcode::DIFFUSER_PARFUM_START: {
            DiffuserParfumStartPayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            if (q.mode != 1 && q.mode != 2) {
                LOG::logMsg("UDP_R", "parfum start rejected - mode %u is invalid (must be 1=CONT or 2=10 SEC)", q.mode);
                g_ackResult = AckResult::REJECTED;
                break;
            }
            bool clamped = false;
            if (q.minutes > PARFUM_MAX_MIN) {
                LOG::logMsg("UDP_R", "parfum minutes clamped: %u min requested, max is %u min", q.minutes, PARFUM_MAX_MIN);
                q.minutes = PARFUM_MAX_MIN;
                clamped = true;
            }
            if (q.minutes == 0) { g_ackResult = AckResult::REJECTED; break; }  // start opcode always needs minutes>0; use CANCEL for 0
            if (!PARFUM::parfumStart(q.minutes, q.mode)) g_ackResult = AckResult::LOCKED;
            else if (clamped) g_ackResult = AckResult::CLAMPED;
            break;
        }

        case Opcode::DIFFUSER_PARFUM_CANCEL:
            if (g_parfumActive) { if (!PARFUM::parfumStop("cancel command")) g_ackResult = AckResult::LOCKED; }
            else {
                LOG::logMsg("UDP_R", "parfum cancel rejected - already inactive");
                g_ackResult = AckResult::REJECTED;
                cmdStatusQuery();
            }
            break;

        default:
            LOG::logMsg("UDP_R", "unrecognized opcode: 0x%02X", opcode);
            g_ackResult = AckResult::UNSUPPORTED;
            break;
    }
}

// -- entry points ---------------------------------------------------------

// Idempotent - called from both WIFI::finishWifiConnect() (the moment WiFi
// connects during setup()'s own blocking wait) and unconditionally again
// right after in _rmk_Diffuser.ino's setup(), which is the only opener at
// all if that first connect attempt timed out instead. Guarding here (one
// state flag, checked in the one place both paths funnel through) means
// neither caller has to know about the other, instead of re-binding the
// same socket twice on every normal boot.
void udpOpen() {
    static bool opened = false;
    if (opened) return;
    g_udp.begin(UDP_PORT);
    opened = true;
    vlogMsg("UDP", "socket opened on port %d", UDP_PORT);
}

void tick() {
    static unsigned long chk = 0;
    static bool          ok  = false;
    unsigned long now = millis();
    if (now - chk >= 500) { ok = (WiFi.status() == WL_CONNECTED); chk = now; }
    if (!ok) return;

    int sz = g_udp.parsePacket();
    if (sz <= 0) return;
    if (sz >= UDP_BUF_SIZE) { g_udp.flush(); return; }

    g_udpSenderIP = g_udp.remoteIP();
    int len = g_udp.read((char*)g_udpRxBuf, UDP_BUF_SIZE - 1);
    if (len <= 0) return;

    ParsedFrame f = ParseFrame(g_udpRxBuf, (size_t)len);
    if (!f.ok) {
        vlogMsg("UDP_R", "malformed frame (%d bytes) dropped", len);
        return;
    }

    vlogMsg("UDP_R", "recv opcode=0x%02X seq=%u len=%u", f.opcode, f.seq, (unsigned)f.payloadLen);
    g_ackResult = AckResult::OK;
    dispatch(f.opcode, f.payload, f.payloadLen);

    if (f.flags & (uint8_t)Flag::ACK_REQ) sendAck(f.seq, (uint8_t)g_ackResult);
}

#ifdef DIF_TEST_MODE
Proto::AckResult dispatchForTest(uint8_t opcode, const uint8_t *payload, size_t len) {
    g_ackResult = AckResult::OK;
    dispatch(opcode, payload, len);
    return g_ackResult;
}
#endif

} // namespace PROTOLINK
