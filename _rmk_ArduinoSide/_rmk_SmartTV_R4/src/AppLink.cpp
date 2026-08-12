#include "AppLink.h"
#include "Globals.h"
#include "Led.h"
#include "Lisens.h"
#include "Eeprom.h"
#include "Net.h"
#include "Mqtt.h"
#include "MqttCred.h"
#include "DifLink.h"
#include "Udpraw.h"
#include "Tv.h"
#include "Motion.h"
#include "Hb.h"
#include "Telnet.h"
#include "Debug.h"
#include <Frame.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

namespace APP {

using namespace Proto;

// -- dirty-gated telemetry caches (mirrors the original's _tx*_ group) ------
static int      _txS_tv = -1, _txS_mo = -1, _txS_ur = -1, _txS_am = -1, _txS_ds = -1;
static int      _txH_tp = -1, _txH_hu = -1;
static int      _txE_en = -1;
static int      _txM_lux = -1;
static int      _txW_bar = -1, _txW_wf = -1;
static int      _txP_min = -1;
static int      _txU_accum = -1, _txU_avg = -1, _txU_cnt = -1, _txU_tot = -1;
static bool     _txF_init = false; static uint16_t _txF_mask = 0;

static uint8_t _binTxBuffer[APP_UDP_MAX_BUFFER_SIZE];

// -- outbound plumbing -------------------------------------------------

static void rawSend(const uint8_t *frame, size_t n) {
    if (State.Suspended) return;
    if (!NET::IsConnected()) return;

    if (State.ActiveProtocol == APP_TRANSPORT_MQTT) {
        MQTT::Publish(frame, n);
        return;
    }

    if (APP_RECV_IP == INADDR_NONE || APP_RECV_IP == IPAddress(0, 0, 0, 0)) return;

    unsigned long startTime = millis();
    bool started = false;
    while (millis() - startTime < APP_UDP_TIMEOUT) {
        if (APP_UDP.beginPacket(APP_RECV_IP, APP_UDP_PORT)) { started = true; break; }
    }
    if (!started) return;

    APP_UDP.write(frame, n);

    unsigned long endStart = millis();
    while (millis() - endStart < APP_UDP_TIMEOUT) {
        if (APP_UDP.endPacket() == 1) break;
    }
}

static void sendFrame(Opcode opcode, uint8_t seq, uint8_t flags, const uint8_t *payload, size_t payloadLen) {
    uint8_t frame[FRAME_MAX_BYTES];
    size_t n = BuildFrame(opcode, seq, flags, payload, payloadLen, frame, sizeof(frame));
    if (n == 0) return;
    rawSend(frame, n);
}

static void sendTelemetry(Opcode opcode, const uint8_t *payload, size_t payloadLen) {
    sendFrame(opcode, 0, (uint8_t)Flag::IS_TELEMETRY, payload, payloadLen);
}

static void sendAck(uint8_t seq, uint8_t result) {
    uint8_t payload[1] = { result };
    sendFrame(Opcode::ACK, seq, (uint8_t)Flag::IS_ACK, payload, 1);
}

/** Sends one Term-console log line as a LOG (0x03) telemetry frame -
    level/source/text fields, same information the original's '*' envelope
    carried, no more ASCII header packing. */
void termMsgLog(uint8_t level, uint8_t source, const char *ns, const char *func, const char *format, ...) {
    char text[160];
    int off = snprintf(text, sizeof(text), "[%s::%s] ", ns, func);
    if (off < 0) off = 0;
    if ((size_t)off < sizeof(text)) {
        va_list args;
        va_start(args, format);
        vsnprintf(text + off, sizeof(text) - off, format, args);
        va_end(args);
    }

    size_t textLen = strlen(text);
    uint8_t payload[2 + sizeof(text)];
    payload[0] = level;
    payload[1] = source;
    memcpy(payload + 2, text, textLen);
    sendTelemetry(Opcode::LOG, payload, 2 + textLen);
}

/** Rebuilds the selected-LED cache from the current selection bitmask -
    avoids repeated full scans in Led.cpp's batch brightness/colour ops. */
void RefreshSelectedCache() {
    State.SelectedCount = 0;
    for (int i = 0; i < LED_NUM; i++) {
        if (LED::IsSelected(i)) {
            State.SelectedLedCache[State.SelectedCount++] = i;
        }
    }
    State.SelectedCacheDirty = false;
}

/** Confirms a scheduled EEPROM write actually landed - TELEM_SAVE_RESULT.
    @param result 0 = written, non-zero = write did not complete. */
void Notify_Saved(uint8_t result) {
    State.SaveFailed = (result != 0);
    TelemSaveResultPayload p{ result };
    uint8_t buf[TELEM_SAVE_RESULT_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_SAVE_RESULT, buf, n);
}

/** Approximate free RAM between the heap break and the current stack -
    sbrk(0) (the original's approach) doesn't exist on this board's newlib
    (arduino:renesas_uno, unlike the AVR/ESP cores where that trick usually
    lives), so this allocates a throwaway few bytes at the current heap
    break and measures the gap to a local stack variable instead - same
    idea, portable via plain malloc/free. */
int getFreeRam() {
    char stackVar;
    char *heapTop = (char*)malloc(4);
    if (!heapTop) return 0;
    int freeRam = (int)(&stackVar - heapTop);
    free(heapTop);
    return freeRam;
}

// -- colour sync (TELEM_COLOR_SYNC) --------------------------------------
// Same FILL/SETN compression scheme as the original's 'LK' packet, now
// carried as this opcode's "records" raw payload instead of an 'L''K'
// prefixed ad-hoc sub-protocol.
static const uint8_t LK_OP_FILL = 0xF0;
static const uint8_t LK_OP_SETN = 0xF1;
static const int     LK_MIN_RUN = 3;

static void flushColorSync(bool force) {
    if (!LED::State.DeltaMode) { State.ColorSyncPending = false; return; }
    if (LED::getChangedCount() == 0) { State.ColorSyncPending = false; return; }

    uint8_t *buf = _binTxBuffer;
    int      n   = 0;

    const int LIMIT = APP_UDP_MAX_BUFFER_SIZE - FRAME_OVERHEAD_BYTES - 6;

    int  i        = 0;
    bool overflow = false;

    while (i < LED_NUM && !overflow) {
        if (!LED::IsChanged(i)) { i++; continue; }

        const CRGB c = LED::State.CurrentColor[i];

        int run = 1;
        while (i + run < LED_NUM && run < 255
               && LED::IsChanged(i + run)
               && LED::State.CurrentColor[i + run].r == c.r
               && LED::State.CurrentColor[i + run].g == c.g
               && LED::State.CurrentColor[i + run].b == c.b) {
            run++;
        }

        if (run >= LK_MIN_RUN) {
            if (n + 6 > LIMIT) { overflow = true; break; }
            buf[n++] = LK_OP_FILL;
            buf[n++] = (uint8_t)i;
            buf[n++] = (uint8_t)run;
            buf[n++] = c.r; buf[n++] = c.g; buf[n++] = c.b;
            for (int k = 0; k < run; k++) BIT_CLEAR(LED::State.Changed, i + k);
            i += run;
        } else {
            if (n + 5 > LIMIT) { overflow = true; break; }
            int cntPos = n;
            buf[n++] = LK_OP_SETN;
            buf[n++] = 0;
            uint8_t cnt = 0;
            while (i < LED_NUM && cnt < 255 && n + 4 <= LIMIT) {
                if (!LED::IsChanged(i)) { i++; continue; }
                const CRGB p = LED::State.CurrentColor[i];
                int look = 1;
                while (i + look < LED_NUM && look < LK_MIN_RUN
                       && LED::IsChanged(i + look)
                       && LED::State.CurrentColor[i + look].r == p.r
                       && LED::State.CurrentColor[i + look].g == p.g
                       && LED::State.CurrentColor[i + look].b == p.b) look++;
                if (look >= LK_MIN_RUN) break;

                buf[n++] = (uint8_t)i;
                buf[n++] = p.r; buf[n++] = p.g; buf[n++] = p.b;
                BIT_CLEAR(LED::State.Changed, i);
                cnt++; i++;
            }
            buf[cntPos + 1] = cnt;
            if (cnt == 0) n = cntPos;
        }
    }

    if (n == 0) { if (!force) State.ColorSyncPending = force; return; }

    sendTelemetry(Opcode::TELEM_COLOR_SYNC, buf, (size_t)n);
    State.ColorSyncLast = TimeNow;

    State.ColorSyncPending = overflow;
    if (force && overflow) flushColorSync(true);
}

/** Marks every LED changed and flushes immediately - full sync the app is
    blocking on (welcome / colour-info request). */
void updColors_Force() {
    LED::MarkChangedRange(0, LED_NUM - 1);
    flushColorSync(true);
}

/** Flags a pending colour sync - the ~80 animation call sites throughout
    Led/Tv/Motion/Hb/Udpraw just flag this; TickColorSync() coalesces every
    mark made this frame into one TELEM_COLOR_SYNC, rate-matched to the LED
    FPS cap so it never sends faster than the strip actually updates. */
void updDeltaColors() {
    State.ColorSyncPending = true;
}

/** Loop hook - flush one coalesced colour sync, at most once per FPS
    period. Call every loop() iteration. */
void TickColorSync(uint32_t now) {
    if (!State.ColorSyncPending) return;
    if ((uint32_t)(now - State.ColorSyncLast) < LED::getFpsLimitMs()) return;
    flushColorSync(false);
}

// -- dirty-gated status/telemetry pushes ---------------------------------

static bool pushLink() {
    const bool ok   = (WiFi.status() == WL_CONNECTED);
    const int  rssi = ok ? (int)WiFi.RSSI() : 0;
    uint8_t    mag  = (uint8_t)((rssi < 0) ? -rssi : rssi);
    if (!ok) mag = 0;

    uint8_t bar;
    if      (mag == 0)  bar = 0;
    else if (mag <= 55) bar = 4;
    else if (mag <= 65) bar = 3;
    else if (mag <= 75) bar = 2;
    else                bar = 1;

    if (bar == _txW_bar && (int)NET_WifiSt == _txW_wf) return false;
    _txW_bar = bar; _txW_wf = NET_WifiSt;

    TelemLinkPayload p{ mag, NET_WifiSt };
    uint8_t buf[TELEM_LINK_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_LINK, buf, n);
    return true;
}

bool updLux() {
    if ((int)LISENS::State.Lux == _txM_lux) return false;
    _txM_lux = LISENS::State.Lux;

    TelemLuxPayload p{ (uint8_t)LISENS::State.Lux };
    uint8_t buf[TELEM_LUX_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_LUX, buf, n);
    return true;
}

static void pushTestMode() {
    TelemTestModePayload p{ (uint8_t)TestMode };
    uint8_t buf[TELEM_TEST_MODE_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_TEST_MODE, buf, n);
}

static bool pushFaults() {
    uint16_t f = 0;

    if (WiFi.status() != WL_CONNECTED || NET_WifiSt != netWifiOK) f |= APP_FAULT_WIFI;
    if (RTC_Status != rtcOK || NET::RTC_EpochUTC() == 0)          f |= APP_FAULT_NTP;
    if (State.SaveFailed)                                          f |= APP_FAULT_EEPROM;
    if (isnan(BME::State.Temperature) || isnan(BME::State.Humidity)) f |= APP_FAULT_BME;
    if (DIF::State.Mode == DIF_MODE_NO_RESPONSE)                   f |= APP_FAULT_DIF_NORESP;
    if (DIF::State.Mode == DIF_MODE_OUT_OF_WATER)                  f |= APP_FAULT_DIF_NOWATER;

    if (_txF_init && f == _txF_mask) return false;
    _txF_init = true; _txF_mask = f;

    TelemFaultsPayload p{ f };
    uint8_t buf[TELEM_FAULTS_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_FAULTS, buf, n);
    return true;
}

/** Pushes every dirty-gated telemetry group. Called from ~all state
    changes; the welcome/resume path resets every cache first for a full
    sync. Each group sends only when its value(s) changed. */
void updStatus(const char *by) {
    (void)by;

    const uint8_t tv = (TV::State.Status) ? 1 : 0;
    const uint8_t mo = MOTION::State.Status;
    const uint8_t ur = (UDPRAW::State.Status) ? 1 : 0;
    const uint8_t am = (Am.Status) ? 1 : ((!UDPRAW::State.Status && (!MOTION::PinStatus(MOTION_PIN_BED)) && !TV::State.Status && LED::State.Enabled) ? 2 : 0);
    const uint8_t ds = DIF::State.Mode == DIF_MODE_NO_RESPONSE ? 3 : (DIF::State.Mode == DIF_MODE_OUT_OF_WATER ? 2 : (DIF::State.ParfumMin > 0 ? 4 : (DIF::State.Mode == 0 ? 0 : 1)));

    if (tv != _txS_tv || mo != _txS_mo || ur != _txS_ur || am != _txS_am || ds != _txS_ds) {
        _txS_tv = tv; _txS_mo = mo; _txS_ur = ur; _txS_am = am; _txS_ds = ds;
        TelemStatusPayload p{ tv, mo, ur, am, ds };
        uint8_t buf[TELEM_STATUS_SIZE];
        size_t n = Pack(p, buf);
        sendTelemetry(Opcode::TELEM_STATUS, buf, n);
    }

    const int8_t  tp = (isnan(BME::State.Temperature)) ? (int8_t)-128 : (int8_t)constrain((int)BME::State.Temperature, -127, 127);
    const uint8_t hu = (isnan(BME::State.Humidity)) ? 0 : (uint8_t)constrain((int)BME::State.Humidity, 0, 255);
    if ((int)tp != _txH_tp || (int)hu != _txH_hu) {
        _txH_tp = tp; _txH_hu = hu;
        TelemClimatePayload p{ tp, hu };
        uint8_t buf[TELEM_CLIMATE_SIZE];
        size_t n = Pack(p, buf);
        sendTelemetry(Opcode::TELEM_CLIMATE, buf, n);
    }

    const uint8_t en = (LED::State.Enabled) ? 1 : 0;
    if ((int)en != _txE_en) {
        _txE_en = en;
        TelemEnablePayload p{ en };
        uint8_t buf[TELEM_ENABLE_SIZE];
        size_t n = Pack(p, buf);
        sendTelemetry(Opcode::TELEM_ENABLE, buf, n);
    }

    if ((int)DIF::State.ParfumMin != _txP_min && (DIF::State.ParfumMin > 0 || _txP_min > 0)) {
        TelemParfumRemainingPayload p{ DIF::State.ParfumMin };
        uint8_t buf[TELEM_PARFUM_REMAINING_SIZE];
        size_t n = Pack(p, buf);
        sendTelemetry(Opcode::TELEM_PARFUM_REMAINING, buf, n);
    }
    _txP_min = DIF::State.ParfumMin;

    if (DIF::State.Mode != DIF_MODE_NO_RESPONSE &&
        ((int)DIF::State.UsageAccumMin != _txU_accum || (int)DIF::State.UsageAvgMin != _txU_avg ||
         (int)DIF::State.UsageRefillCount != _txU_cnt || (int)DIF::State.UsageTotalRefills != _txU_tot)) {
        TelemDiffuserUsagePayload p{ DIF::State.UsageAccumMin, DIF::State.UsageAvgMin, DIF::State.UsageRefillCount, DIF::State.UsageTotalRefills };
        uint8_t buf[TELEM_DIFFUSER_USAGE_SIZE];
        size_t n = Pack(p, buf);
        sendTelemetry(Opcode::TELEM_DIFFUSER_USAGE, buf, n);
    }
    _txU_accum = DIF::State.UsageAccumMin; _txU_avg = DIF::State.UsageAvgMin;
    _txU_cnt   = DIF::State.UsageRefillCount; _txU_tot = DIF::State.UsageTotalRefills;

    updLux();
    pushLink();
    pushFaults();
}

/** Sends all EE_MEM_X settings as one TELEM_SETTINGS_FULL frame. */
void updSettings() {
    TelemSettingsFullPayload p{};
    for (int i = 0; i < EE_MEM_X; i++) p.values[i] = EE::Get(i);
    uint8_t buf[TELEM_SETTINGS_FULL_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_SETTINGS_FULL, buf, n);
}

/** Sends this board's MQTT device ID (12 ASCII hex chars, same string
    NET::DeviceId() builds the fuzz/<id>/... topics from) so the app can
    learn it locally and cache it for the cloud fallback path - the MQTT
    topic is per-device and the app has no other way to discover it. */
static void updDeviceId() {
    TelemDeviceIdPayload p{};
    memcpy(p.device_id, NET::DeviceId(), sizeof(p.device_id));
    uint8_t buf[TELEM_DEVICE_ID_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_DEVICE_ID, buf, n);
}

/** Arms every dirty-gated group (except faults) to resend on next push -
    called before updStatus() on welcome/resume for a full sync. */
static void TxCacheReset() {
    _txS_tv=_txS_mo=_txS_ur=_txS_am=_txS_ds=-1;
    _txH_tp=_txH_hu=-1;
    _txE_en=-1;
    _txM_lux=-1;
    _txW_bar=_txW_wf=-1;
    _txP_min=-1;
    _txU_accum=_txU_avg=_txU_cnt=_txU_tot=-1;
}

// -- diffuser relay bridge (called from DifLink.cpp) ---------------------

void RelayDiffuserAck(uint8_t appSeq, Proto::AckResult result) {
    sendAck(appSeq, (uint8_t)result);
}

void RelayDiffuserHistory(const uint8_t *payload, size_t len) {
    sendTelemetry(Opcode::TELEM_DIFFUSER_HISTORY, payload, len);
}

// -- background tasks ------------------------------------------------------

static void T_KeepAlive(SCHED::TaskId tID) {
    if ((uint32_t)(TimeNow - Ard.LastReceive) > APP_ALIVE_TIMEOUT_MS) {
        State.Suspended   = true;
        State.KeepAliveID = SCHED::TASK_ID_NONE;
        SCHED::KillId(tID);
        return;
    }
    sendFrame(Opcode::KEEPALIVE, 0, 0, NULL, 0);
}

static void T_WatchdogCheck(SCHED::TaskId tID) {
    (void)tID;
    if (APP_RECV_IP != INADDR_NONE && APP_RECV_IP != IPAddress(0, 0, 0, 0)) {
        if ((Ard.LastUdpReceive + ARD_TIMEOUT) <= TimeNow) {
            APP_RECV_IP = INADDR_NONE;
        }
    }
}

static void T_END_TEST_MODE(SCHED::TaskId taskId) {
    switch (TestMode) {
        case _testmode_udpraw: UDPRAW::End(false); break;
        case _testmode_dif:    DIF::AutoOff();     break;
        case _testmode_lux:    LISENS::ResetTime(); break;
        default: break;
    }

    TestMode     = _testmode_none;
    pushTestMode();
    TestMode_tID = SCHED::TASK_ID_NONE;

    SCHED::KillId(taskId);
}

// -- HELLO / welcome ---------------------------------------------------

static void cmdConnected(uint8_t transport) {
    State.ActiveProtocol = transport;

    updDeviceId();

    TelemMaxBrightnessPayload maxBr{ (uint8_t)APP_BRIGHT };
    uint8_t maxBrBuf[TELEM_MAX_BRIGHTNESS_SIZE];
    sendTelemetry(Opcode::TELEM_MAX_BRIGHTNESS, maxBrBuf, Pack(maxBr, maxBrBuf));

    updSettings();

    TxCacheReset();
    updStatus("APP::cmdConnected");

    pushTestMode();

    updColors_Force();

    State.Suspended = false;
    if (State.KeepAliveID == SCHED::TASK_ID_NONE)
        State.KeepAliveID = SCHED::AddTask("APP_Connected", "T_KeepAlive", T_KeepAlive, SCHED::Unit::MS, KeepAlive_MS, KeepAlive_MS, true);
    else
        SCHED::ResetTime(State.KeepAliveID);
}

// -- test mode -----------------------------------------------------------

static const char* TestModeName(uint8_t m) {
    switch (m) {
        case _testmode_none:       return "none";
        case _testmode_tvOn:       return "TV ON";
        case _testmode_tvOff:      return "TV OFF";
        case _testmode_udpraw:     return "UDPRAW";
        case _testmode_motionCom:  return "MOTION COM";
        case _testmode_motionBed:  return "MOTION BED";
        case _testmode_dif:        return "DIFFUSER";
        case _testmode_lux:        return "LUX";
        default:                   return "UNKNOWN";
    }
}

/** Action-only helper for SET_TEST_DIFFUSER - exercises the diffuser from
    the Test Mode UI, bypassing DIF::ActiveModeSetting()'s source guard.
    @param val 0x00 = shutdown, 0x01..DIF_MODE_MAX = turn on in that mode,
    0xFF = re-push current/last valid mode with a fresh random colour pair.
    @return AckResult::OK on a valid value, REJECTED otherwise. */
static AckResult cmdTestDiffuser(uint8_t val, uint8_t relaySeq) {
    DIF::ArmRelay(relaySeq);

    if (val == 0x00) {
        DIF::Shutdown();
    } else if (val == 0xFF) {
        const uint32_t c1 = LED::getRandomColor();
        const uint32_t c2 = LED::getRandomColor();
        DIF_Colorx c = { (uint8_t)(c1 >> 16), (uint8_t)(c1 >> 8), (uint8_t)c1,
                         (uint8_t)(c2 >> 16), (uint8_t)(c2 >> 8), (uint8_t)c2 };
        const uint8_t mode = (DIF::State.Mode >= 1 && DIF::State.Mode <= DIF_MODE_MAX) ? DIF::State.Mode : 1;
        DIF::TurnOn(mode, EE::Get(EE_DIF_EFFECT), &c);
    } else if (val <= DIF_MODE_MAX) {
        DIF::TurnOn(val, EE::Get(EE_DIF_EFFECT));
    } else {
        return AckResult::REJECTED;
    }
    return AckResult::OK;
}

/** Action-only helper for SET_TEST_LUX - forces LISENS::State.Lux via the
    same adaptive-speed path a real ambient-light change takes. */
static AckResult cmdTestLux(uint8_t level) {
    static const int maxLuxLevel = (sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0])) + 1;
    if (level < 1 || level > maxLuxLevel) return AckResult::REJECTED;

    LISENS::setLux(level);
    updLux();
    return AckResult::OK;
}

/** SET_TEST_MODE handler - latches TestMode and (re)arms the
    TESTMODE_DURATION auto-cancel, mirroring the original's classic tail:
    exit cleanup for the mode being left, then the new value, then the
    task. */
static void applyTestMode(uint8_t modeValue) {
    if (TestMode != modeValue) {
        if      (TestMode == _testmode_udpraw) UDPRAW::End(false);
        else if (TestMode == _testmode_dif)    DIF::AutoOff();
        else if (TestMode == _testmode_lux)    LISENS::ResetTime();
    }

    TestMode = (__testmode)modeValue;

    if (TestMode_tID != SCHED::TASK_ID_NONE) {
        SCHED::KillId(TestMode_tID);
        TestMode_tID = SCHED::TASK_ID_NONE;
    }

    if (TestMode != _testmode_none) {
        TestMode_tID = SCHED::AddTask("cmdTestMode", "T_END_TEST_MODE", T_END_TEST_MODE, SCHED::Unit::MS, 1, TESTMODE_DURATION, true);
        termMsgLog(APP_LOG_WRN, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode [%s] armed for [%d] s", TestModeName(TestMode), TESTMODE_DURATION / 1000);
    } else {
        termMsgLog(APP_LOG_INF, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode [CANCELLED] sensors released");
    }

    pushTestMode();
    updStatus("APP::cmdTestMode");
}

// -- settings write --------------------------------------------------------

static AckResult applySettingsWrite(const uint8_t *pairs, size_t len) {
    if (len == 0 || (len % 2) != 0) return AckResult::REJECTED;

    bool difSettingChanged = false;
    bool anyRejected       = false;

    for (size_t i = 0; i + 1 < len; i += 2) {
        uint8_t item  = pairs[i];
        uint8_t value = pairs[i + 1];

        if (item >= EE_MEM_X) { anyRejected = true; continue; }

        bool rejected = false;
        switch (item) {
            case EE_TV_ON_EFF:     if (value >= TV::TV_ON_HANDLERS_COUNT)     rejected = true; break;
            case EE_TV_OFF_EFF:    if (value >= TV::TV_OFF_HANDLERS_COUNT)    rejected = true; break;
            case EE_TV_ON_HB_EFF:  if (value >= TV::HB_ON_HANDLERS_COUNT)     rejected = true; break;
            case EE_MOTION_ON_EFF: if (value >= MOTION::MOTION_ON_HANDLERS_COUNT) rejected = true; break;
            case EE_HB_EFFECT:     if (value > HB::EFFECT_HANDLERS_COUNT)     rejected = true; break;
            case EE_DIF_EFFECT:    if (value > DIF_EFFECT_COUNT)              rejected = true; break;
            case EE_DIF_BRIGHTNESS: if (value > APP_BRIGHT)                   rejected = true; break;
            default: break;
        }
        if (rejected) { anyRejected = true; continue; }

        EE::Set(item, value);

        if (item == EE_HB_EFFECT) {
            HB::EndTask();
            HB::StartEffect(true, true, false);
        }

        if (item == EE_TV_RANDOM_COLOR_START && value == 2) {
            EE::Set(EE_HB_DUAL_COLOR, 1);
            difSettingChanged = true;
        }

        if (item == EE_DIF_EFFECT || item == EE_DIF_MODE_TV || item == EE_DIF_MODE_MOTION
                || item == EE_DIF_MODE_UDPRAW || item == EE_DIF_MODE_AMBIENT
                || item == EE_DIF_IDLE_MODE || item == EE_HB_DUAL_COLOR
                || item == EE_DIF_BRIGHTNESS || item == EE_DIF_SPEED) {
            difSettingChanged = true;
        }
    }

    if (difSettingChanged) DIF::PushLiveIfActive();
    EE::WriteTime();

    return anyRejected ? AckResult::REJECTED : AckResult::OK;
}

// -- ambient mode ------------------------------------------------------

static AckResult applyAmbientMode(bool on) {
    if (UDPRAW::State.Status) return AckResult::BLOCKED;

    if (on) {
        if (MOTION::PinStatus(MOTION_PIN_BED)) return AckResult::BLOCKED;
        if (TV::State.Status) return AckResult::BLOCKED;

        Am.Status = true;
        MOTION::State.Status = motOFF;
        LED::setRandomColor(false);

        SCHED::Scratch &S = SCHED::ScratchFor(SCHED::AddTask("APP_AmbientMode", "T_AMBIENT_MODE_ON", LED::T_AMBIENT_MODE_ON, SCHED::Unit::MS, EE::Get(EE_OTHER_BR_CL_DEL), 0, false));
        S.phase = 1;

        uint16_t sumR = 0, sumG = 0, sumB = 0, litCount = 0;
        for (int i = 0; i < LED_NUM; i++) {
            const CRGB &c = LED::State.AmbientBackgroundColor[i];
            if ((c.r + c.g + c.b) == 0) continue;
            sumR += c.r; sumG += c.g; sumB += c.b; litCount++;
        }
        DIF_Colorx amColor  = { 0, 0, 0, 0, 0, 0 };
        const bool haveColor = (litCount > 0);
        if (haveColor) {
            const uint8_t avgR = sumR / litCount, avgG = sumG / litCount, avgB = sumB / litCount;
            amColor = DIF_Colorx{ avgR, avgG, avgB, avgR, avgG, avgB };
        }

        const uint8_t amMode = EE::Get(EE_DIF_MODE_AMBIENT);
        const bool    difOn  = (DIF::State.Mode >= 1 && DIF::State.Mode <= DIF_MODE_MAX);

        if (amMode != 0) {
            DIF::AutoOn(EE_DIF_MODE_AMBIENT, haveColor ? &amColor : NULL);
        } else if (difOn) {
            DIF::PushLiveIfActive(haveColor ? &amColor : NULL);
        }

        return AckResult::OK;
    }

    if (!Am.Status) return AckResult::BLOCKED;

    Am.Status = false;
    SCHED::KillAllUnlocked();

    MOTION::State.Status = motON;
    TV::State.Status = false;

    LED::setAll(0, 0, 0, 0);

    updStatus("APP::cmdAmbientMode");
    updDeltaColors();

    DIF::AutoOff();

    return AckResult::OK;
}

// -- inbound dispatch ---------------------------------------------------

static AckResult g_ackResult = AckResult::OK;

static void handleLedSetColor(const uint8_t *payload, size_t len) {
    LedSetColorPayload p{};
    if (!Unpack(payload, len, p)) { g_ackResult = AckResult::REJECTED; return; }

    bool isUdpraw = UDPRAW::State.Status;
    bool isMotion = MOTION::State.Status > motON;

    if (isUdpraw) {
        LED::UDPRAW_SetColor(p.r, p.g, p.b, LED::State.StreamBrightness);
        updDeltaColors();
        DIF_Colorx colorOverride = { p.r, p.g, p.b, p.r, p.g, p.b };
        DIF::PushLiveIfActive(&colorOverride);
        return;
    }

    if (isMotion) {
        EE::Set(EE_MOTION_RANDOM_COLOR, 0);
        LED::MOTION_SetColor(p.r, p.g, p.b, EE::Get(EE_MOTION_BRIGHTNESS));
        updDeltaColors();
        return;
    }

    LED::setRandomColor(false);
    LED::setColorToSelected(p.r, p.g, p.b);

    SCHED::Scratch &S = SCHED::ScratchFor(SCHED::AddTask("ChangeColor", "T_SMOOTH_CHANGE", LED::T_SMOOTH_CHANGE, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL), 20, false));
    S.paramA = 0;   // colour mode

    if (EE::Get(EE_HB_DUAL_COLOR) != 0) {
        EE::Set(EE_HB_DUAL_COLOR, 0);
        updSettings();
    }

    DIF_Colorx colorOverride = { p.r, p.g, p.b, p.r, p.g, p.b };
    DIF::PushLiveIfActive(&colorOverride);

    MOTION::State.Status = motOFF;
}

static void handleLedGetColor() {
    updColors_Force();
}

static void handleLedSetDualColor(const uint8_t *payload, size_t len) {
    LedSetDualColorPayload p{};
    if (!Unpack(payload, len, p)) { g_ackResult = AckResult::REJECTED; return; }

    if (UDPRAW::State.Status) { g_ackResult = AckResult::BLOCKED; return; }
    if (Am.Status)            { g_ackResult = AckResult::BLOCKED; return; }
    if (MOTION::State.Status > motON) { g_ackResult = AckResult::BLOCKED; return; }

    LED::setRandomColor(false);
    MOTION::State.Status = motOFF;

    LED::State.TargetColor[0] = CRGB(p.r1, p.g1, p.b1);
    LED::State.TargetColor[1] = CRGB(p.r2, p.g2, p.b2);

    SCHED::KillAllUnlocked();

    if (EE::Get(EE_HB_DUAL_COLOR) != 1) {
        EE::Set(EE_HB_DUAL_COLOR, 1);
        updSettings();
    }

    DIF_Colorx dualOverride = { p.r1, p.g1, p.b1, p.r2, p.g2, p.b2 };
    DIF::PushLiveIfActive(&dualOverride);

    if (p.shake) {
        SCHED::Scratch &S = SCHED::ScratchFor(SCHED::AddTask("ChangeDualColor", "T_SHAKE_DUAL_COLOR", LED::T_SHAKE_DUAL_COLOR, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false));
        S.phase = 1; S.paramA = 0; S.paramB = 0;
    } else {
        SCHED::Scratch &S = SCHED::ScratchFor(SCHED::AddTask("ChangeDualColor", "T_DUAL_COLOR", LED::T_DUAL_COLOR, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false));
        S.phase = 1;
    }
}

static void handleLedGetDualColor() {
    TelemDualColorPayload p{
        LED::State.CurrentColor[LED::UCOM(0)].r, LED::State.CurrentColor[LED::UCOM(0)].g, LED::State.CurrentColor[LED::UCOM(0)].b,
        LED::State.CurrentColor[LED::UCOM(1)].r, LED::State.CurrentColor[LED::UCOM(1)].g, LED::State.CurrentColor[LED::UCOM(1)].b
    };
    uint8_t buf[TELEM_DUAL_COLOR_SIZE];
    size_t n = Pack(p, buf);
    sendTelemetry(Opcode::TELEM_DUAL_COLOR, buf, n);
}

static void handleLedSetSelection(const uint8_t *payload, size_t len) {
    LedSetSelectionPayload p{};
    if (!Unpack(payload, len, p)) { g_ackResult = AckResult::REJECTED; return; }

    for (int i = 0; i < LED_NUM; i++) {
        LED::Select(i, (p.mask[i / 8] & (1 << (i % 8))) != 0);
    }
    State.SelectedCacheDirty = true;
}

static void handleLedSetBrightness(const uint8_t *payload, size_t len) {
    LedSetBrightnessPayload p{};
    if (!Unpack(payload, len, p)) { g_ackResult = AckResult::REJECTED; return; }

    uint8_t br = constrain(p.value, 0, APP_BRIGHT);

    bool isUdpraw = UDPRAW::State.Status;
    bool isMotion = MOTION::State.Status > motON;

    if (isUdpraw) {
        LED::UDPRAW_SetColor(LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b, br);
        updDeltaColors();
        DIF_Colorx colorOverride = { LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b,
                                     LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b };
        DIF::PushLiveIfActive(&colorOverride);
        return;
    }

    if (isMotion) { g_ackResult = AckResult::BLOCKED; return; }

    LED::setBrightnessToSelected(br);

    SCHED::Scratch &S = SCHED::ScratchFor(SCHED::AddTask("ChangeBrightness", "T_SMOOTH_CHANGE", LED::T_SMOOTH_CHANGE, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL), 50, false));
    S.paramA = 1;   // brightness mode

    MOTION::State.Status = motOFF;
}

static void handleLedSetEnable() {
    LED::State.Enabled = !LED::State.Enabled;

    if (!LED::State.Enabled) {
        TV::State.Status     = false;
        MOTION::State.Status = motOFF;
        UDPRAW::State.Status = false;

        LED::ForceOffAndKillTasks("Enable_Disable");
        DIF::Shutdown();

        updDeltaColors();
    } else {
        MOTION::State.Status = motON;
    }

    updStatus("APP::cmdEnableDisable");
}

static void handleDiffuserTurnOn(const uint8_t *payload, size_t len, uint8_t relaySeq) {
    if (DIF::ActiveModeSetting() == 0xFF) { g_ackResult = AckResult::BLOCKED; return; }
    if (len < 2) { g_ackResult = AckResult::REJECTED; return; }

    uint8_t mode   = payload[0];
    uint8_t effect = payload[1];
    if (mode > DIF_MODE_MAX || effect > DIF_EFFECT_COUNT) { g_ackResult = AckResult::REJECTED; return; }

    DIF::ArmRelay(relaySeq);
    DIF::TurnOn(mode, effect);
}

static void handleDiffuserParfumStart(const uint8_t *payload, size_t len, uint8_t relaySeq) {
    DiffuserParfumStartPayload p{};
    if (!Unpack(payload, len, p)) { g_ackResult = AckResult::REJECTED; return; }
    if (p.minutes > DIF_PARFUM_MAX_MIN) { g_ackResult = AckResult::REJECTED; return; }
    if (p.mode < 1 || p.mode > DIF_MODE_MAX) { g_ackResult = AckResult::REJECTED; return; }

    DIF::ArmRelay(relaySeq);
    DIF::Parfum(p.minutes, p.mode);
}

/** Dispatches one fully-parsed frame. Sets g_ackResult (default OK) as its
    outcome; RELAY-tier opcodes arm DifLink's relay instead of resolving
    the ack immediately - see the tail of dispatchFrame(). */
static void dispatch(Opcode opcode, const uint8_t *payload, size_t len, uint8_t relaySeq) {
    switch (opcode) {
        case Opcode::HELLO:
            cmdConnected(State.RxProtocol);
            break;

        case Opcode::KEEPALIVE:
            break;   // liveness only

        case Opcode::DIAG_HEALTH: {
            bool wifiOk = (WiFi.status() == WL_CONNECTED);
            bool ramOk  = (getFreeRam() >= 4000);
            termMsgLog(APP_LOG_INF, APP_SRC_SYS, "APP", "DiagHealth", "==== HEALTH SUMMARY ====");
            termMsgLog(APP_LOG_INF, APP_SRC_NET, "APP", "DiagHealth", "WiFi [%s] (%d dBm)", wifiOk ? "OK" : "DOWN", (int)WiFi.RSSI());
            termMsgLog(APP_LOG_INF, APP_SRC_SYS, "APP", "DiagHealth", "RAM free [%d] of [%d] B%s", getFreeRam(), ARD_RAM_TOTAL, ramOk ? "" : " [LOW]");
            termMsgLog(APP_LOG_INF, APP_SRC_LED, "APP", "DiagHealth", "LEDs [%s]", LED::State.Enabled ? "enabled" : "disabled");
            termMsgLog(APP_LOG_INF, APP_SRC_TV,  "APP", "DiagHealth", "TV [%s]", TV::State.Status ? "on" : "off");
            termMsgLog(APP_LOG_INF, APP_SRC_DIF, "APP", "DiagHealth", "Diffuser mode [%d] parfum [%d] min", DIF::State.Mode, DIF::State.ParfumMin);
            termMsgLog(APP_LOG_INF, APP_SRC_EE,  "APP", "DiagHealth", "EEPROM [%s]", (EE::State.tID != SCHED::TASK_ID_NONE) ? "save pending" : "up to date");
            termMsgLog(APP_LOG_INF, APP_SRC_SYS, "APP", "DiagHealth", "==== %s ====", (wifiOk && ramOk) ? "ALL OK" : "ISSUES FOUND");
            break;
        }

        case Opcode::LED_SET_COLOR:       handleLedSetColor(payload, len);      break;
        case Opcode::LED_GET_COLOR:       handleLedGetColor();                  break;
        case Opcode::LED_SET_DUAL_COLOR:  handleLedSetDualColor(payload, len);  break;
        case Opcode::LED_GET_DUAL_COLOR:  handleLedGetDualColor();              break;
        case Opcode::LED_SET_SELECTION:   handleLedSetSelection(payload, len);  break;
        case Opcode::LED_SET_BRIGHTNESS:  handleLedSetBrightness(payload, len); break;
        case Opcode::LED_SET_ENABLE:      handleLedSetEnable();                 break;

        case Opcode::SETTINGS_READ_ALL:
            updSettings();
            updStatus("APP::cmdSettings");
            break;

        case Opcode::SETTINGS_READ_ONE: {
            SettingsReadOnePayload q{};
            if (!Unpack(payload, len, q) || q.id >= EE_MEM_X) { g_ackResult = AckResult::REJECTED; break; }
            TelemSettingsOnePayload p{ q.id, EE::Get(q.id) };
            uint8_t buf[TELEM_SETTINGS_ONE_SIZE];
            size_t n = Pack(p, buf);
            sendTelemetry(Opcode::TELEM_SETTINGS_ONE, buf, n);
            break;
        }

        case Opcode::SETTINGS_WRITE:
            g_ackResult = applySettingsWrite(payload, len);
            break;

        case Opcode::SET_AMBIENT_MODE: {
            SetAmbientModePayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            g_ackResult = applyAmbientMode(q.on != 0);
            break;
        }

        case Opcode::SET_TEST_MODE: {
            SetTestModePayload q{};
            if (!Unpack(payload, len, q) || q.mode > _testmode_motionBed) { g_ackResult = AckResult::REJECTED; break; }
            applyTestMode(q.mode);
            break;
        }

        case Opcode::SET_TEST_DIFFUSER: {
            SetTestDiffuserPayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            g_ackResult = cmdTestDiffuser(q.value, relaySeq);
            if (g_ackResult == AckResult::OK) applyTestMode((q.value == 0x00) ? ((TestMode == _testmode_dif) ? _testmode_none : TestMode) : _testmode_dif);
            break;
        }

        case Opcode::SET_TEST_LUX: {
            SetTestLuxPayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            g_ackResult = cmdTestLux(q.level);
            if (g_ackResult == AckResult::OK) applyTestMode(_testmode_lux);
            break;
        }

        case Opcode::SET_TELNET_ENABLE: {
            SetTelnetEnablePayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            TELNET::SetEnabled(q.on != 0);
            termMsgLog(APP_LOG_WRN, APP_SRC_SYS, "APP", "SetTelnetEnable", "Telnet console [%s]", q.on ? "ENABLED" : "disabled");
            break;
        }

        case Opcode::SET_MQTT_CREDENTIALS: {
            // payload: userLen(1B)+user+passLen(1B)+pass, raw bytes (01_PROTOCOL.md SS3)
            if (len < 2) { g_ackResult = AckResult::REJECTED; break; }
            uint8_t userLen = payload[0];
            if (len < (size_t)(1 + userLen + 1)) { g_ackResult = AckResult::REJECTED; break; }
            uint8_t passLen = payload[1 + userLen];
            if (len < (size_t)(1 + userLen + 1 + passLen)) { g_ackResult = AckResult::REJECTED; break; }
            char user[MQTTCRED_USER_MAX + 1];
            char pass[MQTTCRED_PASS_MAX + 1];
            uint8_t uCopy = min(userLen, (uint8_t)MQTTCRED_USER_MAX);
            uint8_t pCopy = min(passLen, (uint8_t)MQTTCRED_PASS_MAX);
            memcpy(user, payload + 1, uCopy); user[uCopy] = '\0';
            memcpy(pass, payload + 2 + userLen, pCopy); pass[pCopy] = '\0';
            g_ackResult = MQTTCRED::SetCredentials(user, pass);
            break;
        }

        case Opcode::DIFFUSER_STATUS_QUERY: {
            DiffuserStatusQueryPayload q{};
            bool verbose = Unpack(payload, len, q) ? (q.verbose != 0) : true;
            DIF::ArmRelay(relaySeq);
            DIF::RequestStatus(verbose);
            if (verbose && DIF::State.ParfumMin > 0) {
                TelemParfumRemainingPayload pr{ DIF::State.ParfumMin };
                uint8_t buf[TELEM_PARFUM_REMAINING_SIZE];
                sendTelemetry(Opcode::TELEM_PARFUM_REMAINING, buf, Pack(pr, buf));
            }
            break;
        }

        case Opcode::DIFFUSER_HISTORY_QUERY:
            DIF::ArmRelay(relaySeq);
            DIF::RequestHistory();
            break;

        case Opcode::DIFFUSER_HISTORY_REMOVE: {
            DiffuserHistoryRemovePayload q{};
            if (!Unpack(payload, len, q)) { g_ackResult = AckResult::REJECTED; break; }
            DIF::RemoveHistoryEntry(q.index, relaySeq);
            break;
        }

        case Opcode::DIFFUSER_MANUAL_REFILL:
            DIF::ArmRelay(relaySeq);
            DIF::ManualRefill();
            break;

        case Opcode::DIFFUSER_SHUTDOWN:
            DIF::ArmRelay(relaySeq);
            DIF::Shutdown();
            break;

        case Opcode::DIFFUSER_TURN_ON:
            handleDiffuserTurnOn(payload, len, relaySeq);
            break;

        case Opcode::DIFFUSER_PARFUM_START:
            handleDiffuserParfumStart(payload, len, relaySeq);
            break;

        case Opcode::DIFFUSER_PARFUM_CANCEL:
            DIF::ArmRelay(relaySeq);
            DIF::ParfumCancel();
            break;

        default:
            g_ackResult = AckResult::UNSUPPORTED;
            break;
    }
}

/** Entry point for a fully-received binary frame's payload, shared by UDP's
    own Loop() and MQTT::Dispatch(). Wakes from suspend, defers the next
    keep-alive, de-dups a retransmitted seq (re-acks without re-running,
    unless a diffuser relay is still pending for it), enforces the single-
    active-transport gate (everything except HELLO is dropped on the
    inactive transport), dispatches, then sends the ACK - deferred if a
    DIFFUSER_* relay is still awaiting the diffuser's own reply. */
void dispatchFrame(const uint8_t *data, int len, uint8_t transport) {
    ParsedFrame f = ParseFrame(data, (size_t)len);
    if (!f.ok) return;

    if (State.Suspended) State.Suspended = false;
    SCHED::ResetTime(State.KeepAliveID);

    if (f.opcode == (uint8_t)Opcode::KEEPALIVE) return;

    State.RxProtocol = transport;

    if (f.opcode != (uint8_t)Opcode::HELLO && State.RxProtocol != State.ActiveProtocol) return;

    bool wantAck = (f.flags & (uint8_t)Flag::ACK_REQ) != 0;

    if (wantAck && State.LastSeqValid && f.seq == State.LastSeq
            && (uint32_t)(TimeNow - State.LastSeqTime) < APP_ACK_DEDUP_MS) {
        if (!DIF::IsAppSeqPending(f.seq)) sendAck(f.seq, State.LastSeqResult);
        return;
    }

    g_ackResult = AckResult::OK;

    dispatch((Opcode)f.opcode, f.payload, f.payloadLen, wantAck ? f.seq : 0xFF);

    updStatus("APP::dispatchFrame");

    if (wantAck) {
        State.LastSeq       = f.seq;
        State.LastSeqValid  = true;
        State.LastSeqResult = (uint8_t)g_ackResult;
        State.LastSeqTime   = TimeNow;

        if (!DIF::IsAppSeqPending(f.seq)) sendAck(f.seq, (uint8_t)g_ackResult);
    }
}

Proto::AckResult SaveMqttCredentials(const char *user, const char *pass) {
    return MQTTCRED::SetCredentials(user, pass);
}

// -- entry points ---------------------------------------------------------

void UdpSet() {
    APP_UDP.begin(APP_UDP_PORT);
}

void Setup() {
    State.SessionId    = (uint16_t)random(0x0001, 0xFFFF);
    State.Suspended    = false;
    State.KeepAliveID  = SCHED::TASK_ID_NONE;

    if (NET::IsConnected()) {
        UdpSet();
    }

    SCHED::AddTask("APP_Setup", "T_WatchdogCheck", T_WatchdogCheck, SCHED::Unit::MS, APP_WATCHDOG_MS, 0, true);
}

/** Receives and dispatches one incoming UDP frame per loop() iteration.
    Round-robin polled (every 3rd call) same as the original, sharing the
    loop() cycle fairly with DifLink's own poll phase. */
void Loop() {
    if (!NET::Connected_Cached()) return;

    static uint8_t udpPollPhase = 0;
    if (++udpPollPhase % 3 != 0) return;

    int pkSize = APP_UDP.parsePacket();
    if (pkSize <= 0) return;

    if (pkSize >= APP_UDP_MAX_BUFFER_SIZE) { APP_UDP.flush(); return; }

    APP_RECV_IP = APP_UDP.remoteIP();

    static uint8_t recvBuf[APP_UDP_MAX_BUFFER_SIZE];
    int len = APP_UDP.read(recvBuf, APP_UDP_MAX_BUFFER_SIZE);
    if (len <= 0) return;

    Ard.LastReceive    = TimeNow;
    Ard.LastUdpReceive = TimeNow;
    dispatchFrame(recvBuf, len, APP_TRANSPORT_UDP);
}

} // namespace APP
