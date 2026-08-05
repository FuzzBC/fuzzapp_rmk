// _FuZzAPP_Diffuser.ino
// WeMos D1 Mini — WiFi + OTA + UDP + Telnet + diffuser virtual buttons
// ============================================================
//  16-PIN IC PINOUT  (top view, pin 1 = top-left)
//
//   1 ──┬──────────────────────────┬── 9  ◄─ BUZZER_PIN A0 in, 4.7k series
//   2 ──┤                          ├── 10
//   3 ──┤       ╔════════╗         ├── 11
//   4 ──┤       ║        ║         ├── 12 ◄─ MODE_PIN D6 out, Hi-Z default, active-LOW
//   5 ──┤       ╚════════╝         ├── 13
//   6 ──┤                          ├── 14
//   7 ──┤                          ├── 15
//   8 ──┴──────────────────────────┴── 16
//
// ============================================================
//  PROTOCOL MAP  —  quick reference (prose details below)
// ============================================================
//  UDP :8439   IN  (from SmartTV)                → reply
//    Ds                          status query     → DsMMSSTTTT
//    Dc                          status poll       → DsMMSSTTTT  (silent)
//    Dh                          full refill history → Dh + count + 10x4hex minutes, oldest-first
//    Dr                          manual refill     → DsMMSSTTTT  (banks + resets usage now, no ack-only wait)
//    Df                          shutdown          → DsMMSSTTTT
//    DpMMMME                     parfum start      → (ack only)  MMMM=hex min 0001-0168, E=1|2
//    Dp0000                      parfum cancel     → (ack only)
//    DnXXrrggbbBREESP           on, solid         → (ack only)  XX=mode EE=00-08 SP=ms
//    DnXXrrggbbRRGGBBBREESP     on, dual          → (ack only)
//    #SS<cmd>                    ack-wrapped any   → #SSR        R: 0 ok 1 clamp 2 rej 4 lock 5 nowater 6 unsup
//
//  UDP :8439   OUT (to SmartTV)
//    DsMMSSTTTT                  MM=mode(05=OOW)  SS=strip-code  TTTT=parfum-min(0000=off)
//    #SSR                        SS=echoed seq    R=result code
//
//  CONSOLE  Serial + Telnet:23  (human, decimal)
//    M0-M4     drive mode (M0=off)        P<min>  parfum (P30, P0=cancel)
//    E         next effect               Crrggbb solid colour test
//    S         status                    D       debug dump
//    ? / H     help                      Q       close telnet
//
//  GPIO  bridge <-> diffuser
//    OUT MODE_PIN D6   short 100ms = advance mode    long 3000ms = shutdown
//    IN  BUZZER  A0    1 beep = mode confirmed        >1 beep = shutdown
// ============================================================
// LED:  WiFi OK → solid OFF (active-LOW), WiFi lost → blink (500 ms)
// STRIP: WS2812 x6 on STRIP_PIN, mirrors SmartTV colour protocol —
//        Dn<mode>RRGGBB<BR><EE><SP> (solid + brightness + effect + speed) /
//        Dn<mode>RRGGBBrrggbb<BR><EE><SP> (dual + brightness + effect + speed)
//        BR (hex 00-FF): colour scale factor — baked into the colour, never a
//        NeoPixel hardware brightness register, so it always fades in smoothly
//        EE (hex): 00=static 01=fade 02=pulse 03=random 04=rainbow 05=sparkle
//        06=fire 07=bounce 08=confetti
//        (Chase is reserved internally for the out-of-water alert — not user-selectable;
//        a random-colour breathe cue is likewise reserved internally for Parfum mode)
//        SP (hex 00-FF): animated-effect frame period, ms (floor-clamped)
//        all changes fade in smoothly; BR/EE/SP are mandatory, no legacy short form
// UDP:  port 8439, mirrors SmartTV protocol (M0-M4, strip EE/RGB/dual)
// ACK:  any command may arrive wrapped as "#SS<cmd>" (SS = 2 hex seq); the
//        inner command is dispatched and answered with "#SSR" (R = 1 hex
//        result: 0 ok / 1 coerced / 2 rejected / 4 parfum-locked / 5 no water /
//        6 unsupported). Bare commands still work and get no "#SSR". See udpExec().
// PARFUM: DpMMMME (hex minutes 0001-0168 + mode digit E=1|2) = timed run that
//        refuses Df/Dn until expiry — but queues the LAST one received and
//        applies it instead of the plain shutdown once the window naturally
//        expires. With nothing queued, natural expiry restores the
//        mode/colour/effect that was active right before the window started
//        (e.g. M1 -> parfum -> back to M1) instead of shutting down; only
//        shuts down if no such state existed. E is still validated as M1/M2
//        (M0 "off" and M3/M4 "after sleep" don't apply to a manual Parfum
//        run), but the window always physically runs in M2 10 SEC — CONT
//        (E=1) is coerced too, so a long unattended insist never sprays
//        continuously; cancel stays Dp0000, no E.
//        A manual Dp0000/M0 cancel always shuts down and drops both the
//        queue and the restore snapshot; strip runs a random-colour breathe
//        cue while active (falls back to a fixed violet PULSE only if an
//        unsolicited-shutdown auto-restart fires mid-window); Ds reply
//        extended to DsMMSSTTTT (TTTT = remaining minutes)
// Buzzer: A0 peak-hold envelope; 1 beep = mode advance confirm, >1 = shutdown
// Logging: every line is tagged "[TAG   ] message" (UDP_R, UDP_S, MODE, WATER,
//          PARFUM, BUZZ, STRIP, SEQ, WIFI, OTA, TELNET, CMD, BOOT, DIF).
//          Only important lines are emitted by default; define _DEBUG in the .h
//          to add the repeating per-poll/per-frame detail. Console replies
//          (S / D / ? / banner) always print.
// Telnet: port 23, same command set as Serial — M0-M4/E/Crrggbb/S/D/? (see cmdHelp()),
//         plus telnet-only "Q" which closes the session (sent by the phone app
//         when its TELNET console toggle is switched off)
// ============================================================

#include "_FuZzAPP_Diffuser.h"
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ════════════════════════════════════════════════════════════════════════════
   GLOBALS
   ════════════════════════════════════════════════════════════════════════════ */

// ── Telnet ──────────────────────────────────────────────────
static WiFiServer  g_telnetSrv(TELNET_PORT);
static WiFiClient  g_telnetCli;
static String      g_telnetBuf;

// ── UDP ─────────────────────────────────────────────────────
static WiFiUDP    g_udp;
static IPAddress  g_udpSenderIP(0, 0, 0, 0);
static char       g_udpRxBuf[UDP_BUF_SIZE];

// ── Loop task timestamps ─────────────────────────────────────
static unsigned long g_tWifi   = 0;
static unsigned long g_tLed    = 0;
static unsigned long g_tBuzzer = 0;
static unsigned long g_tUsage  = 0;

// ── Virtual MODE button one-shot state ───────────────────────
static bool          g_modePinPending  = false;
static unsigned long g_modeReleaseMs   = 0;

// ── Buzzer detection ─────────────────────────────────────────
static int           g_buzBaseline    = 512;
static float         g_buzEnv         = 0.0f;
static bool          g_buzToneActive  = false;
static unsigned long g_buzLastEndMs   = 0;
static unsigned long g_buzTotalCount  = 0;
static uint8_t       g_buzBurst       = 0;
static uint8_t       g_buzQuiet       = 0;

// ── Diffuser virtual mirror ───────────────────────────────────
static bool    g_powered      = true;             // Boot: assume diffuser already running
static uint8_t g_mode         = 1;               // M0=off, M1-M4=running

// ── Out-of-water detection ────────────────────────────────────
static unsigned long g_powerOnMs   = 0;          // millis() when diffuser was last powered ON
static bool          g_outOfWater  = false;       // true if last shutdown was a fast unsolicited one

// ── Usage tracking / refill estimation (see _FuZzAPP_Diffuser.h) ──────
// g_usageAccumSec accrues duty-cycle-weighted "effective" running seconds
// while g_powered, reset to 0 each time a refill cycle finalizes. On that
// finalize, if it cleared USAGE_MIN_CYCLE_SEC it's pushed into the
// g_refillHistory ring buffer (last REFILL_HISTORY_SIZE completed cycles).
// All fields mirror EEPROM (see loadUsageFromEeprom()/saveUsageToEeprom()).
static uint32_t      g_usageAccumSec     = 0;
static float         g_usageFracSec      = 0.0f;  // sub-second remainder carried between tickUsageAccum() calls
static unsigned long g_usageLastTickMs   = 0;
static unsigned long g_usageLastSaveMs   = 0;      // millis() of the last ACTUAL EEPROM write (debug display / flash-wear history)
static unsigned long g_usageLastCheckMs  = 0;      // millis() of the last periodic checkpoint EVALUATION (advances every USAGE_EEPROM_CHECKPOINT_MS regardless of whether it wrote)
static uint32_t      g_usageLastSavedAccumSec = 0; // g_usageAccumSec as of the last actual write - lets the periodic checkpoint skip a no-op write when nothing changed (e.g. diffuser was off the whole interval)
static uint32_t       g_refillHistory[REFILL_HISTORY_SIZE] = {0};
static uint8_t        g_refillHistoryCount = 0;    // valid entries, 0-REFILL_HISTORY_SIZE
static uint8_t        g_refillHistoryNext  = 0;    // ring-buffer next-write index
static uint32_t       g_totalRefillCount   = 0;    // lifetime count (not windowed)

// ── Mode sequenced targeting ──────────────────────────────────
static bool    g_modePending      = false;
static unsigned long g_modePendMs = 0;
static bool    g_modeTargetActive = false;
static uint8_t g_modeTarget       = 1;
// True only when reaching g_modeTarget genuinely requires wrapping through OFF
// (hardware only steps forward, so this means the target is numerically lower
// than the mode we started from) - set in cmdModeTarget(), read by
// applyShutdown()/buzzerFinalizeBurst() to tell a real drop apart from the
// expected forced pass-through.
static bool    g_modeTargetWraps  = false;

// ── UDP "Dn" strip payload (decoded from RRGGBB<BR><EE><SP> / RRGGBBrrggbb<BR><EE><SP>) ──
struct DnPayload {
    bool    dual;             // false = single colour, true = dual colour
    uint8_t effect;           // 0=static 1-8=animated (hex EE) — see EFFECT_NAMES
    uint8_t r1, g1, b1;       // primary colour   (always set)
    uint8_t r2, g2, b2;       // secondary colour (dual only)
    uint8_t brightness;       // 00-FF — colour scale factor, see applyBrightness()
    uint8_t speedMs;          // 00-FF — animated-effect frame period, ms (see g_effectTickMs)
};
// (Dn colour/effect payload is applied immediately in cmdDiffuserOn() — see below —
// no longer deferred/chained behind the physical MODE-button stepping sequence.)

// ── Unsolicited-shutdown auto-recovery ────────────────────────
// Distinguishes a shutdown WE asked for (Df/M0 long-press) from the diffuser
// dropping out on its own. An unsolicited drop gets one auto-restart with the
// last Dn settings; only a second unsolicited drop right after that restart
// is diagnosed as out of water (see buzzerFinalizeBurst()) — no further
// retries follow; it stays in the OOW alert until a genuine new Dn arrives.
static bool          g_shutdownCmdPending = false;   // true while our own long-press awaits buzzer confirm
static bool          g_shutdownRetried    = false;   // one retry used; re-armed only by a genuine
                                                       // cmdDiffuserOn() start, never by the retry itself
static bool          g_haveLastDn         = false;   // true once at least one Dn payload has been received
static uint8_t       g_lastDnMode         = 1;       // mode last requested via Dn — auto-restart target
static DnPayload     g_lastDnPayload      = {};      // colour/effect/brightness/speed last requested via Dn

// ── Deferred UDP status replies for Dn/Df command completion ──
enum UdpStatusReplyMode : uint8_t {
    UDP_STATUS_NONE,
    UDP_STATUS_AFTER_TARGET,
    UDP_STATUS_AFTER_SHUTDOWN
};
static UdpStatusReplyMode g_udpStatusReplyMode = UDP_STATUS_NONE;

// ── WiFi connect/reconnect self-test sequence ─────────────────
enum WifiSeqPhase : uint8_t { SEQ_NONE, SEQ_MODE_STEP, SEQ_DONE };
static WifiSeqPhase  g_seqPhase        = SEQ_NONE;
static unsigned long g_seqNextMs       = 0;
static uint8_t        g_seqModeAttempts = 0;

// ── Non-blocking WiFi connect/reconnect strip cue ─────────────
static bool          g_wifiConnecting   = false;
static unsigned long g_wifiConnNextMs   = 0;

// ── Out-of-water "Dn while dry" strip alert ───────────────────
static bool          g_oowAlertActive   = false;

// ── Parfum mode (timed "insist" run — see _FuZzAPP_Diffuser.h) ─
static bool          g_parfumActive     = false;   // true while a DpMMMME window is running
static unsigned long g_parfumEndMs      = 0;       // millis() deadline of the current window
static uint16_t      g_parfumSetMin     = 0;       // minutes originally requested (dbg/status)

// ── Queued post-parfum command (last Df/Dn received while parfum insisted) ──
// Df/Dn are refused while g_parfumActive (see udpExec()), but the most recent
// one is latched here and applied by parfumStop() ONLY on natural timer expiry
// (naturalExpiry=true) — see tickParfum(). Any manual stop (Dp0000, console
// P0, local M0, OOW cancel) drops the queue instead of applying it.
enum ParfumPendingKind : uint8_t { PARFUM_PEND_NONE, PARFUM_PEND_ON, PARFUM_PEND_OFF };
static ParfumPendingKind g_parfumPendingKind    = PARFUM_PEND_NONE;
static uint8_t           g_parfumPendingMode    = 1;
static DnPayload         g_parfumPendingPayload = {};

// ── Pre-parfum snapshot (restore target when the window expires naturally
//    with nothing queued) — captured once, at the start of a FRESH window
//    only, so a Dp re-trigger while already active does not overwrite it
//    with the parfum's own violet-cue mode/payload. See parfumStart()/parfumStop().
static bool              g_parfumHavePrevDn     = false;   // false = nothing to restore, fall back to shutdown
static uint8_t           g_parfumPrevMode       = 1;       // mode active right before this parfum window
static DnPayload         g_parfumPrevPayload    = {};      // colour/effect/brightness/speed active right before it

// ── RGB strip (WS2812 x6) mirror ──────────────────────────────
struct StripColor { uint8_t r, g, b; };
enum StripMode : uint8_t { STRIP_OFF, STRIP_STATIC, STRIP_DUAL, STRIP_ANIM, STRIP_WIFI_CUE, STRIP_OOW_CHASE, STRIP_PARFUM_BREATHE };
static Adafruit_NeoPixel g_strip(STRIP_LED_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);
static StripColor  g_stripCur[STRIP_LED_COUNT] = {};
static StripColor  g_stripTgt[STRIP_LED_COUNT] = {};
static StripMode   g_stripMode  = STRIP_OFF;
static uint8_t     g_animEffect = 0;     // 0..STRIP_EFFECT_COUNT-1 (animated only; Dn "EE" effect# minus 1)
static uint8_t     g_animPhase  = 0;     // rolling animation phase/hue — also reused by WiFi cue

// ── Base colour(s) for effects that animate the sent Dn colour (fade/pulse/chase) ──
static StripColor  g_effectBase1 = { 255, 255, 255 };   // LED0-4 (right-half) colour — see applyDnPayload() swap
static StripColor  g_effectBase2 = { 255, 255, 255 };   // LED5-9 (left-half) colour   (dual only)
static bool        g_effectDual  = false;

// ── Live brightness / effect-speed (from Dn "BR"/"SP") ────────────────
// Brightness is never applied as a NeoPixel hardware brightness register —
// it's baked into the target colour itself (see applyBrightness()), so a
// brightness-only change rides the existing tickStripFade() smoothing and
// never pops instantly.
static uint8_t     g_stripBrightness = 255;             // 00-FF colour scale factor, see applyBrightness()
static uint8_t     g_effectTickMs    = STRIP_EFFECT_TICK_MS;  // live-adjustable animated-effect frame period

// ── Parfum "breathe" strip cue (random-colour fill/drain loop) ────────────
// Reserved internally, like STRIP_OOW_CHASE — bypasses the numbered EE
// effect set; driven only via startParfumBreathe()/stripParfumBreatheFrame().
static uint8_t     g_parfumBreatheLevel = 0;             // LEDs currently lit, 0..STRIP_LED_COUNT
static int8_t      g_parfumBreatheDir   = 1;             // +1 = filling, -1 = draining
static StripColor  g_parfumBreatheColor = {};            // current hue, re-rolled every time level hits 0

// Fill order — front-centre pair (LED4/LED5) outward to the back seam
// (LED0/LED9), one physical LED added per level step (see ring layout in
// _FuZzAPP_Diffuser.h). PARFUM_BREATHE_ORDER[fill position] = physical LED.
static const uint8_t PARFUM_BREATHE_ORDER[STRIP_LED_COUNT] = { 4, 5, 3, 6, 2, 7, 1, 8, 0, 9 };
#define dbgPrint(s)    LOG::dbgWrite(s)
#define dbgPrintln(s)  do { LOG::dbgWrite(s); LOG::dbgWrite("\n"); } while(0)

#ifdef _DEBUG
    #define vlogMsg(...)  LOG::logMsg(__VA_ARGS__)
#else
    #define vlogMsg(...)  do { } while (0)     /* compiled out, strings too */
#endif

// -- UDP command ACK state (set by UDP::udpExec()/UDP::udpDispatch()) --
static uint8_t g_ackSeq    = 0;        // seq id of the command being handled
static bool    g_ackValid  = false;    // true while an enveloped command is in flight
static uint8_t g_ackResult = ACK_OK;   // outcome selected by UDP::udpDispatch()

/* ============================================================================
   FORWARD DECLARATIONS  (namespace-qualified - every function is declared here
   before use, so definition order inside each namespace below doesn't matter)
   ============================================================================ */

namespace LOG {
    static void cmdDebugInfo();
    static void cmdHelp();
    static void dbgPrintf(const char *fmt, ...);
    static void dbgWrite(const char *buf);
    static char* formatMSG(const char *fmt, ...);
    static void logMsg(const char *tag, const char *fmt, ...);
    static void telnetWrite(const char *buf);
    static const char* uptimeStr(unsigned long ms);
}

namespace STRIP {
    static inline StripColor applyBrightness(const StripColor &base);
    static void applyDnPayload(const DnPayload &p);
    static inline uint8_t centreDistance(uint8_t i);
    static inline const char* effectName(uint8_t ee);
    static void fireArmStep(uint8_t *heat, uint8_t armLen);
    static inline StripColor heatColor(uint8_t heat);
    static inline StripColor scaleColor(const StripColor &base, uint8_t scale);
    static void startOowAlert();
    static void startParfumBreathe();
    static void stripApplyDual(uint8_t r1, uint8_t g1, uint8_t b1,
                            uint8_t r2, uint8_t g2, uint8_t b2);
    static void stripApplyEffectNext();
    static void stripApplyEffectSet(uint8_t n);
    static void stripApplyStatic(uint8_t r, uint8_t g, uint8_t b);
    static inline void stripDecayAll(uint8_t factor);
    static void stripEffectFrame();
    static void stripOff();
    static void stripOowChaseFrame();
    static void stripParfumBreatheFrame();
    static void stripSetTargetAll(uint8_t r, uint8_t g, uint8_t b);
    static void stripSetTargetDual(uint8_t r1, uint8_t g1, uint8_t b1,
                                uint8_t r2, uint8_t g2, uint8_t b2);
    static inline uint8_t stripStatusCode();
    static inline const char* stripStatusName(uint8_t code);
    static bool stripStepChan(uint8_t &cur, uint8_t tgt, uint8_t inc);
    static void stripWheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b);
    static void tickStripEffect();
    static void tickStripFade();
    static void tickStripOowChase();
    static void tickStripParfumBreathe();
    static void tickStripWifiCue();
    static inline uint8_t triWave(uint8_t phase);
}

namespace MODE {
    static void applyModeAdvance();
    static void applyModeTargetAdvance();
    static void applyShutdown();
    static void clearPendingStatusReply();
    static bool cmdDiffuserOn(uint8_t m, const DnPayload &p, bool isWaterRetry = false);
    static bool cmdModeTarget(uint8_t t);
    static void continueModeTarget();
    static inline const char* modeName(uint8_t m);
    static void pulseModeRaw(unsigned long dur);
    static void pulseModeShort();
    static void sendPendingStatusReply(UdpStatusReplyMode mode);
    static void tickModeTimeout();
    static void tickPinRelease();
}

namespace BUZZ {
    static void buzzerFinalizeBurst();
    static void tickBuzzer();
}

namespace USAGE {
    static uint32_t averageRefillCycleSec();
    static float dutyCycleForMode(uint8_t mode);
    static void finalizeRefillCycle();
    static void loadUsageFromEeprom();
    static void saveUsageToEeprom(bool includeHistory);
    static void tickUsageAccum();
}

namespace PARFUM {
    static uint16_t parfumRemainingMin();
    static bool parfumStart(uint16_t minutes, uint8_t mode = PARFUM_DIF_MODE);
    static bool parfumStop(const char *reason, bool naturalExpiry = false);
    static void tickParfum();
}

namespace WIFI {
    static void beginWifiConnect();
    static void connectWiFi();
    static void finishWifiConnect();
    static void setupOTA();
    static void startWifiSequence();
    static void tickLed();
    static void tickWifi();
    static void tickWifiConnecting();
    static void tickWifiSequence();
    static inline bool wifiSequenceActive();
}

namespace PROTO {
    static uint8_t ackHexByte(const char *h);
    static void cmdHistoryQuery();
    static void cmdStatusQuery(bool silent = false);
    static void tickUDP();
    static void udpAck(uint8_t seq, uint8_t result);
    static void udpDispatch(char *buf);
    static void udpExec(char *buf);
    static void udpOpen();
    static void udpSend(const char *msg);
}

namespace TELNET {
    static void handleCmd(String s);
    static bool parseCmd(const String &s, char prefix, uint8_t maxVal, uint8_t &out);
    static void tickTelnet();
}


/* ============================================================================
   FUNCTIONS - grouped by category, alphabetical within each (see FUNCS map in
   refactor notes). Cross-category calls are explicitly namespace-qualified;
   same-category calls stay bare, matching the SmartTV file's convention.
   ============================================================================ */

/* ============================================================================
   LOG
   Debug/telnet output sink, formatting, and console reporting.
   ============================================================================ */
namespace LOG {

/** @brief  Serial/Telnet "D" — full diffuser/strip/buzzer/network debug dump.
 *  One field per line, "%-14s: value" throughout - a few original lines
 *  crammed 2-3 unrelated fields together with ad-hoc spacing (e.g. WiFi
 *  status + RSSI + IP + MAC all on one line); split out for readability,
 *  same information, nothing dropped. */
static void cmdDebugInfo() {
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
    dbgPrintf ("%-14s: M%u (%s)%s\n", "Mode", g_mode, MODE::modeName(g_mode),
               g_outOfWater ? "  [OUT OF WATER]" : "");
    dbgPrintf ("%-14s: lastDn=%s (M%u)   retried=%s\n", "Auto-retry",
               g_haveLastDn ? "yes" : "no", g_lastDnMode, g_shutdownRetried ? "yes" : "no");
    if (g_parfumActive) {
        dbgPrintf("%-14s: ACTIVE - %u/%u min remaining\n", "Parfum", PARFUM::parfumRemainingMin(), g_parfumSetMin);
        if (g_parfumPendingKind == PARFUM_PEND_ON)
            dbgPrintf("%-14s: Dn -> M%u (%s)\n", "Parfum queue", g_parfumPendingMode, MODE::modeName(g_parfumPendingMode));
        else if (g_parfumPendingKind == PARFUM_PEND_OFF)
            dbgPrintf("%-14s: Df (off)\n", "Parfum queue");
        else if (g_parfumHavePrevDn)
            dbgPrintf("%-14s: none - expiry reverts to M%u (%s)\n", "Parfum queue", g_parfumPrevMode, MODE::modeName(g_parfumPrevMode));
        else
            dbgPrintf("%-14s: none - expiry shuts down (no prior mode)\n", "Parfum queue");
    } else
        dbgPrintf("%-14s: inactive\n", "Parfum");
    if (g_modeTargetActive)
        dbgPrintf("%-14s: -> M%u (%s), %s\n", "Targeting", g_modeTarget, MODE::modeName(g_modeTarget),
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
    dbgPrintf ("%-14s: %s\n", "OOW alert", g_oowAlertActive ? "active" : "no");
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
        // uptimeStr() returns a pointer to a single shared static buffer (not
        // re-entrant) - calling it twice as args to the SAME dbgPrintf() would
        // have the second call silently overwrite the first's result before
        // vsnprintf ever reads it, so both fields would print identically.
        // Two separate calls sidestep that instead of adding a second buffer.
        dbgPrintf("%-14s: last save %s ago (%s)\n", "EEPROM", uptimeStr(sinceSaveMs),
                  dirty ? "unsaved change pending" : "up to date, nothing to save");
        dbgPrintf("%-14s  next checkpoint in %s (skipped if still unchanged; sooner if a refill finalizes)\n", "",
                  uptimeStr(nextCheckMs));
    }
    dbgPrintln("─────────────────────────────");
}

/** @brief  Serial/Telnet "?"/"H" — list available console commands. */
static void cmdHelp() {
    dbgPrintln("──────── COMMANDS ────────");
    dbgPrintln("M0-M4      Drive diffuser to mode (M0 = off, long-press shutdown)");
    dbgPrintln("P<min>     Parfum mode: P30 = insist 30 min (1-360), P0 = cancel + off");
    dbgPrintln("E          Cycle to next animated strip effect");
    dbgPrintln("Crrggbb    Set solid strip colour (manual test — full brightness)");
    dbgPrintln("S          Quick status (mode / strip)");
    dbgPrintln("D          Debug info (full state dump — mode, strip, buzzer, WiFi)");
    dbgPrintln("? or H     This help");
    dbgPrintln("Q          Close this telnet session (telnet only)");
    dbgPrintln("──────────────────────────");
}

/** @brief  printf to 256-byte stack buffer → dbgWrite. */
static void dbgPrintf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbgWrite(buf);
}

/** @brief  Write buf to Serial and connected telnet client. */
static void dbgWrite(const char *buf) {
    if (!buf) return;
    Serial.print(buf);
    telnetWrite(buf);
}

/* ════════════════════════════════════════════════════════════════════════════
   formatMSG  — lightweight printf-style formatter (SmartTV spec subset)
   Specifiers: %s  %~Ns  %d  %X  %T  %F  %l
   @return Pointer to internal static 512-byte buffer (not re-entrant).
   ════════════════════════════════════════════════════════════════════════════ */
static char* formatMSG(const char *fmt, ...) {
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

/**
 * @brief  True if `msg` reads like a problem report, not routine state - picks
 *         the '!' symbol for logMsg() instead of '#', same convention verified
 *         across the SmartTV side's own formatMSG() call sites: every "!"
 *         line there is a rejected/invalid/out-of-range report with the tag
 *         LEFT-aligned ("%32s ! ..."), while routine action/event lines use
 *         "#" with the tag RIGHT-aligned ("%~32s # ..."). Keyword heuristic
 *         rather than a severity parameter on every one of the 30+ existing
 *         logMsg() call sites - most error-ish messages in this file already
 *         say so ("bad payload", "invalid subcommand", "rejected", ...).
 */
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

/**
 * @brief  Tagged log line, Serial + telnet - same two-column convention
 *         verified on the SmartTV side, not an approximation of it:
 *           routine (action/event happened): "%~32s # message"  (tag RIGHT-
 *             aligned - leading-pad spaces, tag ends right at column 32)
 *           problem (rejected/invalid/etc):  "%32s ! message"   (tag LEFT-
 *             aligned - trailing-pad spaces, tag starts at column 0)
 *         via this file's own formatMSG() (already ported, same width/'~'
 *         specifiers as the SmartTV one).
 *
 * Use the short forms below rather than calling this directly:
 *
 *   logMsg(tag, fmt, ...)   ALWAYS emitted — state changes, commands that
 *                           did something, errors, boot/network events.
 *   vlogMsg(tag, fmt, ...)  Only when _DEBUG is defined in the .h — raw
 *                           packet dumps, per-frame/per-poll detail, and
 *                           anything that repeats on a timer.
 *
 * Console REPLIES (S / D / ? / connect banner) never go through here: they
 * answer a request and must show up regardless of _DEBUG, so they keep
 * using raw dbgPrintf/dbgPrintln.
 *
 * @param  tag  Short subsystem tag, e.g. "UDP_R", "MODE", "WATER".
 * @param  fmt  printf format WITHOUT trailing newline — appended here.
 */
static void logMsg(const char *tag, const char *fmt, ...) {
    char buf[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    dbgWrite(formatMSG(looksLikeProblem(buf) ? "%32s ! %s\n" : "%~32s # %s\n", tag, buf));
}

/**
 * @brief  Send buf to the telnet client, translating LF to CRLF.
 *
 * Telnet terminals expect CRLF line endings; a bare LF moves the cursor
 * down without returning it to column 0, so every line renders stacked
 * at the same column instead of starting a new one. Serial is unaffected.
 */
static void telnetWrite(const char *buf) {
    if (!buf) return;
    if (!g_telnetCli || !g_telnetCli.connected()) return;

    for (const char *p = buf; *p; ++p) {
        if (*p == '\n') g_telnetCli.write('\r');
        g_telnetCli.write(*p);
    }
}

/** @brief  Format ms as "Dd HHh MMm SSs" into a static buffer (not re-entrant). */
static const char* uptimeStr(unsigned long ms) {
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

/* ============================================================================
   STRIP
   WS2812 strip rendering, effects, and colour helpers.
   ============================================================================ */
namespace STRIP {

/** @return base colour scaled by the live g_stripBrightness (00-FF).
 *  Used instead of a NeoPixel hardware brightness register so brightness
 *  changes ride the normal colour-fade smoothing in tickStripFade(). */
static inline StripColor applyBrightness(const StripColor &base) {
    return scaleColor(base, g_stripBrightness);
}

/** @brief  Apply a decoded Dn payload immediately — colour first, then effect override.
 *  Brightness (p.brightness) is baked into the colour here — once — so every
 *  downstream consumer (static/dual target, and Fade/Pulse/Sparkle/Bounce which
 *  read g_effectBase1/2 every frame) automatically reflects it, and the change
 *  fades in smoothly via tickStripFade() like any other colour change. */
static void applyDnPayload(const DnPayload &p) {
    g_stripBrightness = p.brightness;
    g_effectTickMs    = (p.speedMs >= STRIP_EFFECT_SPEED_MIN_MS) ? p.speedMs : STRIP_EFFECT_SPEED_MIN_MS;

    // NOTE: r1/r2 swapped vs received order — corrects a physical L/R inversion
    // (front/back was already correct). base1 always ends up on LED0-4, base2 on LED5-9.
    StripColor c1 = applyBrightness({ p.r1, p.g1, p.b1 });
    StripColor c2 = applyBrightness({ p.r2, p.g2, p.b2 });
    g_effectBase1 = c2;
    g_effectBase2 = c1;
    g_effectDual  = p.dual;

    if (p.dual) stripApplyDual(c2.r, c2.g, c2.b, c1.r, c1.g, c1.b);
    else        stripApplyStatic(c1.r, c1.g, c1.b);
    if (p.effect != 0) stripApplyEffectSet(p.effect);   // 0 = stay static/dual as just applied above
}

/** @return LED i's distance from strip centre, 0 = centre pair .. max = the two ends. */
static inline uint8_t centreDistance(uint8_t i) {
    uint8_t mid = STRIP_LED_COUNT / 2;
    return (i < mid) ? (uint8_t)(mid - 1 - i) : (uint8_t)(i - mid);
}

/** @return Human-readable effect name for EE 0-STRIP_EFFECT_COUNT. */
static inline const char* effectName(uint8_t ee) {
    if (ee <= STRIP_EFFECT_COUNT) return EFFECT_NAMES[ee];
    return "?";
}

/** @brief One cool -> diffuse -> spark update for a single fire "arm".
 *  heat[] is ordered base (index 0, at the seam) -> tip (index armLen-1, at
 *  the front-centre pair). Every tick each cell cools a little, then heat
 *  drifts from base toward tip (averaged with the two cells behind it) so
 *  embers visibly climb instead of flickering in place, and the base is
 *  occasionally re-ignited by a fresh spark. Classic cool/diffuse/spark
 *  fire model (Kriegsman "Fire2012"), split across the ring's two
 *  symmetric seam->centre arms. */
static void fireArmStep(uint8_t *heat, uint8_t armLen) {
    const uint8_t COOLING  = 55;    // higher = flame dies down faster
    const uint8_t SPARKING = 120;   // higher = more frequent new sparks (0-255 chance/tick)
    for (uint8_t i = 0; i < armLen; i++) {                        // 1) cool every cell a little
        uint8_t cool = (uint8_t)random(0, ((uint16_t)COOLING * 10) / armLen + 2);
        heat[i] = (uint8_t)max((int)heat[i] - (int)cool, 0);
    }
    for (uint8_t i = armLen - 1; i >= 2; i--) {                   // 2) heat drifts base -> tip
        heat[i] = (uint8_t)(((uint16_t)heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3);
    }
    if (random(255) < SPARKING) {                                 // 3) occasional new spark at the base
        uint8_t y = (uint8_t)random(2);
        heat[y] = (uint8_t)min((int)heat[y] + (int)random(160, 255), 255);
    }
}

/** @return heat 0-255 mapped to a black→red→orange→yellow→white flame colour. */
static inline StripColor heatColor(uint8_t heat) {
    uint8_t t192     = (uint8_t)(((uint16_t)heat * 191) / 255);   // scale to 0-191
    uint8_t heatramp = (uint8_t)((t192 & 0x3F) << 2);              // ramp within third, 0-252
    if (t192 < 64)       return { heatramp, 0, 0 };                // black -> red
    else if (t192 < 128) return { 255, heatramp, 0 };              // red -> yellow
    else                 return { 255, 255, heatramp };            // yellow -> white
}

/** @return base colour scaled by scale/255 (brightness envelope). */
static inline StripColor scaleColor(const StripColor &base, uint8_t scale) {
    return { (uint8_t)(((uint16_t)base.r * scale) / 255),
             (uint8_t)(((uint16_t)base.g * scale) / 255),
             (uint8_t)(((uint16_t)base.b * scale) / 255) };
}

/** @brief  Begin the OOW alert (blue Chase); called on out-of-water diagnosis. */
static void startOowAlert() {
    g_oowAlertActive = true;
    g_effectBase1     = { 0, 90, 255 };      // alert blue
    g_effectDual      = false;
    LOG::logMsg("WATER", "out of water - blue Chase alert until mode-off command");
    g_stripMode = STRIP_OOW_CHASE;           // Chase is reserved internally, bypasses the numbered effect set
}

/** @brief  Begin the Parfum breathe cue — resets to an empty ring with a
 *  freshly rolled hue and starts filling; called from PARFUM::parfumStart() right
 *  after the normal Dn payload apply (see applyDnPayload()), whose seed
 *  colour/effect this immediately overrides. */
static void startParfumBreathe() {
    uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
    g_parfumBreatheColor = { r, g, b };
    g_parfumBreatheLevel = 0;
    g_parfumBreatheDir   = 1;
    g_stripMode = STRIP_PARFUM_BREATHE;      // reserved internally, bypasses the numbered effect set
}

/** @brief  Dn "RRGGBBrrggbb" payload — split dual colour, smooth fade-in. */
static void stripApplyDual(uint8_t r1, uint8_t g1, uint8_t b1,
                            uint8_t r2, uint8_t g2, uint8_t b2) {
    g_stripMode = STRIP_DUAL;
    stripSetTargetDual(r1, g1, b1, r2, g2, b2);
    vlogMsg("STRIP", "dual -> #%02X%02X%02X / #%02X%02X%02X", r1, g1, b1, r2, g2, b2);
}

/** @brief  Manual test helper (Serial/Telnet "E") — cycle to the next animated effect. */
static void stripApplyEffectNext() {
    g_stripMode  = STRIP_ANIM;
    g_animEffect = (g_animEffect + 1) % STRIP_EFFECT_COUNT;
    vlogMsg("STRIP", "effect -> #%u (%s)", g_animEffect + 1, effectName(g_animEffect + 1));
}

/**
 * @brief  Dn "EE" payload — select effect directly by number.
 * @param  n  0=static (freeze current colour) 1-12=animated, see EFFECT_NAMES.
 */
static void stripApplyEffectSet(uint8_t n) {
    if (n == 0) {
        g_stripMode = STRIP_STATIC;              // freeze whatever colour is currently showing
        vlogMsg("STRIP", "effect -> #0 (STATIC)");
        return;
    }
    g_stripMode  = STRIP_ANIM;
    g_animEffect = n - 1;                        // 1..4 -> animated index 0..3
    vlogMsg("STRIP", "effect -> #%u (%s)", n, effectName(n));
}

/** @brief  Dn "RRGGBB" payload — solid colour, all 6 LEDs, smooth fade-in. */
static void stripApplyStatic(uint8_t r, uint8_t g, uint8_t b) {
    g_stripMode = STRIP_STATIC;
    stripSetTargetAll(r, g, b);
    vlogMsg("STRIP", "static -> #%02X%02X%02X", r, g, b);
}

/** @brief  Scale every LED's target colour down by factor/255 in place (trail decay). */
static inline void stripDecayAll(uint8_t factor) {
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[i] = scaleColor(g_stripTgt[i], factor);
}

/** @brief  Compute one animation frame into g_stripTgt[] for the selected effect.
 *  g_animEffect (EE-1): 0=Fade 1=Pulse 2=Random 3=Rainbow 4=Sparkle 5=Fire 6=Bounce
 *  7=Confetti — see EFFECT_NAMES. (Chase exists separately — see stripOowChaseFrame() —
 *  reserved for the out-of-water alert and not part of this user-selectable set.)
 *  Fade/Pulse/Sparkle/Bounce animate g_effectBase1/2 (the colour(s) last sent via Dn);
 *  Random/Rainbow/Fire/Confetti generate their own colour. */
static void stripEffectFrame() {
    g_animPhase++;
    uint8_t half = STRIP_LED_COUNT / 2;
    switch (g_animEffect) {
        case 0: {                                   // Fade — halves desync 180° when dual
            uint8_t triL = triWave(g_animPhase);
            uint8_t triR = g_effectDual ? triWave((uint8_t)(g_animPhase + 128)) : triL;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                bool right = g_effectDual && i >= half;
                g_stripTgt[i] = scaleColor(right ? g_effectBase2 : g_effectBase1, right ? triR : triL);
            }
            break;
        }
        case 1: {                                   // Pulse — width grows centre->ends, then retracts
            uint8_t maxDist = (STRIP_LED_COUNT + 1) / 2;
            uint8_t width   = (uint8_t)(((uint16_t)triWave(g_animPhase) * maxDist) / 255);
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                const StripColor &base = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;
                g_stripTgt[i] = (centreDistance(i) <= width) ? base : StripColor{ 0, 0, 0 };
            }
            break;
        }
        case 2: {                                   // Random — one random LED lit, rest off
            static uint8_t hopTicks = 0;
            if (++hopTicks < 8) break;               // hop to a new LED+colour every 8 frames
            hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
            uint8_t led = (uint8_t)random(STRIP_LED_COUNT);
            g_stripTgt[led] = applyBrightness({ (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256) });
            break;
        }
        case 3: {                                   // Rainbow — continuous hue sweep, ignores base colour
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                uint8_t h = g_animPhase + (uint8_t)(i * (256 / STRIP_LED_COUNT));
                uint8_t r, g, b; stripWheel(h, r, g, b);
                g_stripTgt[i] = applyBrightness({ r, g, b });
            }
            break;
        }
        case 4: {                                   // Sparkle — random LEDs flash over a dim base glow, then decay
            static uint8_t hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                const StripColor &base = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;
                StripColor restLevel = scaleColor(base, 40);          // resting glow level, never decays below this
                StripColor decayed   = scaleColor(g_stripTgt[i], 220);
                g_stripTgt[i].r = max(restLevel.r, decayed.r);
                g_stripTgt[i].g = max(restLevel.g, decayed.g);
                g_stripTgt[i].b = max(restLevel.b, decayed.b);
            }
            if (++hopTicks < 3) break;               // spark a random LED every 3 frames
            hopTicks = 0;
            uint8_t i = (uint8_t)random(STRIP_LED_COUNT);
            g_stripTgt[i] = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;   // full-brightness spark
            break;
        }
        case 5: {                                   // Fire — heat climbs from the seam (base) to the front-centre pair (tip)
            const uint8_t armLen = STRIP_LED_COUNT / 2;        // LEDs per arm, base(0) .. tip(armLen-1)
            static uint8_t heatL[STRIP_LED_COUNT / 2] = {};    // pos 0 = LED0  (seam) .. pos armLen-1 = LED(armLen-1) (tip)
            static uint8_t heatR[STRIP_LED_COUNT / 2] = {};    // pos 0 = LED(COUNT-1) (seam) .. pos armLen-1 = tip, mirrored
            fireArmStep(heatL, armLen);
            fireArmStep(heatR, armLen);
            for (uint8_t pos = 0; pos < armLen; pos++) {
                g_stripTgt[pos]                       = applyBrightness(heatColor(heatL[pos]));
                g_stripTgt[STRIP_LED_COUNT - 1 - pos] = applyBrightness(heatColor(heatR[pos]));
            }
            break;
        }
        case 6: {                                   // Bounce — one LED scans back and forth, reversing at each end
            static int8_t  pos      = 0;
            static int8_t  dir      = 1;
            static uint8_t hopTicks = 0;
            if (++hopTicks < 4) break;               // advance one LED every 4 frames
            hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
            g_stripTgt[pos] = (g_effectDual && pos >= half) ? g_effectBase2 : g_effectBase1;
            if (pos + dir < 0 || pos + dir >= STRIP_LED_COUNT) dir = (int8_t)-dir;   // reverse at either end
            pos = (int8_t)(pos + dir);
            break;
        }
        case 7: {                                   // Confetti — random-hue sparks over a slowly fading trail
            static uint8_t hopTicks = 0;
            stripDecayAll(235);                      // slow trail decay, every frame
            if (++hopTicks < 3) break;               // spawn a new spark every 3 frames
            hopTicks = 0;
            uint8_t led = (uint8_t)random(STRIP_LED_COUNT);
            uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
            g_stripTgt[led] = applyBrightness({ r, g, b });
            break;
        }
    }
}

/** @brief  Force strip to black (shutdown / OOW-stop). */
static void stripOff() {
    g_stripMode = STRIP_OFF;
    stripSetTargetAll(0, 0, 0);
}

/** @brief  Chase animation frame — one LED scans the ring, wraps at LED9->LED0.
 *  Reserved internally for the out-of-water alert only; not part of the
 *  user-selectable EFFECT_NAMES set (see stripEffectFrame()). */
static void stripOowChaseFrame() {
    static uint8_t pos      = 0;
    static uint8_t hopTicks = 0;
    if (++hopTicks < 4) return;                  // advance one LED every 4 frames
    hopTicks = 0;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
    g_stripTgt[pos] = g_effectBase1;
    pos = (pos + 1) % STRIP_LED_COUNT;
}

/** @brief  Parfum "breathe" cue frame — LEDs fill front (LED4/LED5) to back
 *  (LED0/LED9) per PARFUM_BREATHE_ORDER with a shared random hue, brightness
 *  rising in lock-step with the lit count (full count == full brightness),
 *  then both drain back to dark together in the same front-to-back order.
 *  The instant the level bottoms out at 0 a fresh random hue is rolled and
 *  filling starts again — continuously. Reserved internally for Parfum
 *  mode; not part of the user-selectable EFFECT_NAMES set (see
 *  stripEffectFrame()). */
static void stripParfumBreatheFrame() {
    static uint8_t hopTicks = 0;
    if (++hopTicks < PARFUM_BREATHE_STEP_TICKS) return;   // one LED step every N frames
    hopTicks = 0;

    if (g_parfumBreatheDir > 0) {
        if (g_parfumBreatheLevel < STRIP_LED_COUNT) g_parfumBreatheLevel++;
        else                                         g_parfumBreatheDir = -1;   // full — start draining
    } else {
        if (g_parfumBreatheLevel > 0) {
            g_parfumBreatheLevel--;
        } else {                                                                // empty — reroll + refill
            uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
            g_parfumBreatheColor = { r, g, b };
            g_parfumBreatheDir   = 1;
            g_parfumBreatheLevel = 1;
        }
    }

    uint8_t    levelScale = (uint8_t)(((uint16_t)g_parfumBreatheLevel * 255) / STRIP_LED_COUNT);
    StripColor lit        = applyBrightness(scaleColor(g_parfumBreatheColor, levelScale));
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[PARFUM_BREATHE_ORDER[i]] = (i < g_parfumBreatheLevel) ? lit : StripColor{ 0, 0, 0 };
}

/** @brief  Set every LED's target colour to the same RGB. */
static void stripSetTargetAll(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { r, g, b };
}

/** @brief  Split target colour across the strip — first half c1, second half c2. */
static void stripSetTargetDual(uint8_t r1, uint8_t g1, uint8_t b1,
                                uint8_t r2, uint8_t g2, uint8_t b2) {
    uint8_t half = STRIP_LED_COUNT / 2;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[i] = (i < half) ? StripColor{ r1, g1, b1 } : StripColor{ r2, g2, b2 };
}

/** @return Compact status code for the strip: 0=off/transient, 1=static, 2=dual, 3+=animated effect. */
static inline uint8_t stripStatusCode() {
    switch (g_stripMode) {
        case STRIP_STATIC: return 1;
        case STRIP_DUAL:   return 2;
        case STRIP_ANIM:   return 3 + g_animEffect;
        default:           return 0;   // OFF / WIFI_CUE / OOW_ALERT / PARFUM_BREATHE — transient states
    }
}

/** @return Human-readable name for a stripStatusCode() value (for dbg logging). */
static inline const char* stripStatusName(uint8_t code) {
    if (code == 0) return "OFF";
    if (code == 1) return "STATIC";
    if (code == 2) return "DUAL";
    return effectName(code - 2);   // code 3.. -> EE 1.. -> EFFECT_NAMES[1..]
}

/** @brief  Step a single channel one increment toward target. @return true if changed. */
static bool stripStepChan(uint8_t &cur, uint8_t tgt, uint8_t inc) {
    if (cur == tgt) return false;
    int diff = (int)tgt - (int)cur;
    cur += (abs(diff) <= (int)inc) ? diff : (diff > 0 ? (int)inc : -(int)inc);
    return true;
}

/** @brief  Fast 3-phase HSV wheel → RGB (integer only, no float) — SmartTV LED_HsvToRgb style. */
static void stripWheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (h < 85)       { r = 255 - h * 3; g = h * 3;        b = 0; }
    else if (h < 170) { h -= 85;  r = 0; g = 255 - h * 3;  b = h * 3; }
    else              { h -= 170; r = h * 3; g = 0;        b = 255 - h * 3; }
}

/** @brief  Non-blocking: advance the animated-effect frame; call every loop() iteration.
 *  Frame period is the live g_effectTickMs (from Dn "SP"), not the fixed
 *  STRIP_EFFECT_TICK_MS — WiFi cue / OOW alert keep the fixed constant. */
static void tickStripEffect() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_ANIM) return;
    unsigned long now = millis();
    if (now - last < g_effectTickMs) return;
    last = now;
    stripEffectFrame();
}

/** @brief  Step current colours toward target and push to hardware; call every loop() iteration. */
static void tickStripFade() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last < STRIP_TICK_MS) return;
    last = now;

    bool changed = false;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
        changed |= stripStepChan(g_stripCur[i].r, g_stripTgt[i].r, STRIP_FADE_STEP);
        changed |= stripStepChan(g_stripCur[i].g, g_stripTgt[i].g, STRIP_FADE_STEP);
        changed |= stripStepChan(g_stripCur[i].b, g_stripTgt[i].b, STRIP_FADE_STEP);
    }
    if (!changed) return;

    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_strip.setPixelColor(i, g_strip.Color(g_stripCur[i].r, g_stripCur[i].g, g_stripCur[i].b));
    g_strip.show();
}

/** @brief  Non-blocking: advance the reserved OOW Chase alert frame; call every loop() iteration. */
static void tickStripOowChase() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_OOW_CHASE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    stripOowChaseFrame();
}

/** @brief  Non-blocking: advance the reserved Parfum breathe cue frame; call every loop() iteration. */
static void tickStripParfumBreathe() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_PARFUM_BREATHE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    stripParfumBreatheFrame();
}

/** @brief  Non-blocking: spinning-rainbow "still connecting" cue; call every loop() iteration. */
static void tickStripWifiCue() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_WIFI_CUE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    g_animPhase++;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
        uint8_t h = g_animPhase + (uint8_t)(i * (256 / STRIP_LED_COUNT));
        uint8_t r, gc, b; stripWheel(h, r, gc, b);
        g_stripTgt[i] = { r, gc, b };
    }
}

/** @return Triangle wave 0-255 for phase 0-255 (one full up/down cycle). */
static inline uint8_t triWave(uint8_t phase) {
    return (phase < 128) ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
}

} // namespace STRIP

/* ============================================================================
   MODE
   Diffuser mode/state machine - physical button stepping, shutdown, targeting.
   ============================================================================ */
namespace MODE {

/** @brief  Update mirror after 1-beep confirmation of a single MODE press. */
static void applyModeAdvance() {
    g_modePending = false;
    if (!g_powered) {
        g_powered    = true; g_mode = 1;
        g_powerOnMs  = millis();
        g_outOfWater = false;                 // Fresh power-on — clear any stale OOW flag
        // NOTE: g_shutdownRetried is NOT touched here. This path also fires mid-retry
        // (BUZZ::buzzerFinalizeBurst()'s auto-restart steps mode 0->N through this same
        // function), so clearing it here re-armed the one-shot retry before the retry
        // had even proven itself — causing an endless retry loop instead of one retry.
        // It's now only re-armed by cmdDiffuserOn() on a genuine new start.
    } else if (g_mode < MODE_MAX) {
        g_mode++;
    } else {
        applyShutdown();
        LOG::logMsg("MODE", "-> M0 (OFF)");
    }
}

/** @brief  Update mirror after mode-target step; continue sequence. */
static void applyModeTargetAdvance() {
    applyModeAdvance();
    continueModeTarget();
}

/** @brief  Reset mirror to off state. Only actually blanks the strip when
 *  OFF is the real destination — an out-of-water shutdown, or a direct
 *  Df/M0 request. If a mode-target chain is still active and aiming for a
 *  real mode (1..MODE_MAX), this call is just the forced pass-through the
 *  physical MODE button takes on its way to a lower-numbered mode (hardware
 *  only ever advances forward, wrapping through OFF) — continueModeTarget()
 *  steps on immediately after, so keep showing whatever the strip is
 *  already on instead of blanking it. */
static void applyShutdown() {
    g_powered     = false;
    g_mode        = 0;
    g_modePending = false;
    bool passingThrough = g_modeTargetActive && g_modeTargetWraps && !g_outOfWater;
    if (passingThrough) return;
    STRIP::stripOff();
    if (g_oowAlertActive) { g_oowAlertActive = false; LOG::logMsg("WATER", "OOW alert stopped (OFF)"); }
}

static void clearPendingStatusReply() {
    g_udpStatusReplyMode = UDP_STATUS_NONE;
}

/**
 * @brief  UDP "Dn" — apply strip payload p immediately, and drive MODE to target m in parallel.
 * @param  m  Target mode, 1..MODE_MAX (M0/off is invalid here — use Df instead).
 * @param  p  Decoded strip payload — effect-next / solid colour / dual colour.
 */
static bool cmdDiffuserOn(uint8_t m, const DnPayload &p, bool isWaterRetry) {
    if (WIFI::wifiSequenceActive()) { LOG::logMsg("UDP_R", "Dn ignored - WiFi self-test sequence active"); return false; }
    if (!isWaterRetry) {
        g_shutdownRetried = false;            // Genuine new start (real Dn, or non-fast auto-restore) —
    }                                          // re-arm the one-shot; the retry itself must NOT do this.
    if (g_outOfWater) {                       // New start attempt — reset OOW, retry & re-check normally
        LOG::logMsg("WATER", "Dn received - resetting out-of-water, retrying normal start");
        if (g_oowAlertActive) { g_oowAlertActive = false; LOG::logMsg("WATER", "OOW alert stopped (retry)"); }
        g_outOfWater = false;                 // Falls through — buzzerFinalizeBurst will re-diagnose
    }
    if (m == 0 || m > MODE_MAX) { LOG::logMsg("UDP_R", "Dn invalid mode %02X (M1-M%u)", m, MODE_MAX); return false; }

    g_lastDnMode    = m;                       // Remember for unsolicited-shutdown auto-restart
    g_lastDnPayload = p;
    g_haveLastDn    = true;

    STRIP::applyDnPayload(p);                         // Colour/effect applied now — doesn't wait on MODE stepping

    if (g_powered && g_mode == m) {            // Already at target mode — nothing left to chain
        PROTO::cmdStatusQuery();                      // Reply immediately
        return true;
    }

    g_udpStatusReplyMode = UDP_STATUS_AFTER_TARGET;
    bool accepted = cmdModeTarget(m);          // Mode steps toward target independently, in parallel
    if (!accepted) clearPendingStatusReply();
    return accepted;
}

static bool cmdModeTarget(uint8_t t) {
    if (WIFI::wifiSequenceActive())  { LOG::logMsg("MODE", "ignored - WiFi self-test sequence active"); return false; }
    if (t > MODE_MAX)         { LOG::logMsg("MODE", "invalid M%u (M0-M%u)", t, MODE_MAX); return false; }
    if (t == 0) {
        if (g_parfumActive) {                      // Local override (console M0) — PARFUM::parfumStop() clears the
            g_parfumActive      = false;           // flag before calling here, so this only fires for
            g_parfumSetMin      = 0;               // Serial/Telnet commands. UDP Df never reaches this.
            g_parfumPendingKind = PARFUM_PEND_NONE; // Drop any queued Dn/Df — explicit local stop wins.
            LOG::logMsg("PARFUM", "cancelled - local M0 override");
        }
        if (g_oowAlertActive) {
            g_oowAlertActive = false;
            LOG::logMsg("WATER", "OOW alert stopped (mode-off)");
        }
        STRIP::stripOff();                            // Stop command received — strip off now
        g_modeTargetWraps = false;
        if (!g_powered) {
            vlogMsg("MODE", "already M0 (OFF)");
            sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
            return true;
        }
        LOG::logMsg("MODE", "M0 - shutdown (long-press)");
        g_shutdownCmdPending = true;           // Marks the upcoming buzzer burst as commanded, not a fault
        pulseModeRaw(BTN_LONG_MS);
        return true;
    }
    if (g_powered && g_mode == t) {
        vlogMsg("MODE", "already M%u (%s)", t, modeName(t));
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        return true;
    }
    g_modeTarget       = t;
    g_modeTargetActive = true;
    // Hardware only ever steps MODE forward, wrapping through OFF - a wrap is
    // only actually needed when the target is numerically lower than where we
    // are now. An ascending target (t > g_mode) reaches it directly, never
    // touching OFF, so a shutdown mid-sequence there is a real drop, not a step.
    g_modeTargetWraps  = g_powered && (t < g_mode);
    { int steps = g_powered ? (int)t - (int)g_mode : (int)t;
      LOG::logMsg("MODE", "M%u (%s) -> M%u (%s) [%d step(s)]",
                g_mode, modeName(g_mode), t, modeName(t), steps); }
    continueModeTarget();
    return true;
}

/** @brief  Issue next step toward g_modeTarget if not yet reached. */
static void continueModeTarget() {
    if (!g_modeTargetActive || g_modePending) return;
    if (g_mode == g_modeTarget) {
        g_modeTargetActive = false;
        LOG::logMsg("MODE", "M%u (%s)", g_mode, modeName(g_mode));
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        return;
    }
    pulseModeShort();
    g_modePending  = true;
    g_modePendMs   = millis();
}

/** @return Human-readable mode name for M0-M4. */
static inline const char* modeName(uint8_t m) {
    if (m == 0) return "OFF";
    if (m <= MODE_MAX) return MODE_NAMES[m - 1];
    return "?";
}

/** @brief  Assert MODE_PIN LOW for BTN_LONG_MS (shutdown). */
static void pulseModeRaw(unsigned long dur) {
    pinMode(MODE_PIN, OUTPUT);
    digitalWrite(MODE_PIN, LOW);
    g_modeReleaseMs  = millis() + dur;
    g_modePinPending = true;
}

/** @brief  Assert MODE_PIN LOW for BTN_SHORT_MS. */
static void pulseModeShort() {
    pinMode(MODE_PIN, OUTPUT);
    digitalWrite(MODE_PIN, LOW);
    g_modeReleaseMs  = millis() + BTN_SHORT_MS;
    g_modePinPending = true;
}

static void sendPendingStatusReply(UdpStatusReplyMode mode) {
    if (g_udpStatusReplyMode != mode) return;
    clearPendingStatusReply();
    PROTO::cmdStatusQuery();
}

static void tickModeTimeout() {
    if (g_modePending && (millis() - g_modePendMs) > MODE_CONFIRM_MS) {
        g_modePending      = false;
        g_modeTargetActive = false;
        g_modeTargetWraps  = false;
        LOG::logMsg("MODE", "pending press TIMED OUT - no buzzer confirm");
        // Whatever Dn/Df was waiting on this press would otherwise never get its
        // deferred Ds - flush it now with whatever state we actually ended up in,
        // rather than leaving the SmartTV side waiting indefinitely.
        sendPendingStatusReply(UDP_STATUS_AFTER_TARGET);
        sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
    }
}

static void tickPinRelease() {
    unsigned long now = millis();
    if (g_modePinPending && now >= g_modeReleaseMs)  { pinMode(MODE_PIN,  INPUT); g_modePinPending = false; }
}

} // namespace MODE

/* ============================================================================
   BUZZ
   Buzzer confirmation detection.
   ============================================================================ */
namespace BUZZ {

/** @brief  Dispatch a completed burst: 1 beep = mode confirm, >1 = shutdown. */
static void buzzerFinalizeBurst() {
    uint8_t n = g_buzBurst;
    g_buzBurst = 0;

    if (WIFI::wifiSequenceActive()) {                          // Step-by-step MODE detection
        if (n == 1 && g_modePending) {
            g_modePending = false;
            if (g_mode < MODE_MAX) g_mode++;              // Mirror one confirmed step forward
            g_seqNextMs = millis() + WIFI_SEQ_MODE_GAP_MS; // Pace the next press a little slower
            vlogMsg("SEQ", "burst %u beep(s) - step confirmed, M%u (%s)", n, g_mode, MODE::modeName(g_mode));
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
        bool commanded = g_shutdownCmdPending;             // WE asked for this (Df/M0) vs it dropping on its own
        g_shutdownCmdPending = false;
        bool wasFast = !commanded && g_powered && (millis() - g_powerOnMs) < WATER_OUT_TIMEOUT_MS;

        // If a mode-target sequence is active AND actually needs to wrap through OFF to
        // reach its (lower-numbered) target, this shutdown burst is the expected forced
        // pass-through (e.g. M2->M1 passes through M0) - not a real drop. An ASCENDING
        // target never needs to pass through OFF (hardware only steps forward), so a
        // shutdown mid-ascending-sequence is a genuine drop (dry tank, fault, etc.) and
        // must fall through to the diagnosis below instead of being silently absorbed.
        bool wasModeTarget    = g_modeTargetActive;
        bool targetWasM0      = (g_modeTarget == 0);
        bool expectedPassThru = wasModeTarget && !targetWasM0 && g_modeTargetWraps;
        if (wasModeTarget && !expectedPassThru) g_modeTargetActive = false;  // target reached (M0) or genuinely aborted

        MODE::applyShutdown();

        // If genuinely passing through to a non-zero target, clear g_modePending to allow MODE::continueModeTarget()
        // to issue the next pulse. Otherwise the transition stalls at M0.
        if (expectedPassThru) {
            g_modePending = false;
            MODE::continueModeTarget();
        }

        // Only send shutdown status reply if we're not mid an expected pass-through
        // (that case sends its own status once the real target is reached).
        if (!expectedPassThru) {
            MODE::sendPendingStatusReply(UDP_STATUS_AFTER_SHUTDOWN);
        }

        if (commanded) {
            LOG::logMsg("MODE", "M0 (OFF) - commanded");
        } else if (expectedPassThru) {
            // Expected shutdown during mode transition to a non-zero target - continue the transition
            vlogMsg("MODE", "M0 (OFF) - passing through to M%u", g_modeTarget);
        } else if (wasModeTarget && targetWasM0) {
            // Target was M0 - transition complete
            LOG::logMsg("MODE", "M0 (OFF) - target reached");
        } else if (wasFast && !g_shutdownRetried && g_haveLastDn) {
            // First fast unsolicited drop this cycle — one auto-restart before concluding
            // it's dry. g_shutdownRetried stays true through the retry itself (see
            // cmdDiffuserOn's isWaterRetry) so a second fast drop falls through to OOW
            // below instead of looping; it's only re-armed by a genuine new start.
            g_shutdownRetried = true;
            LOG::logMsg("WATER", "unsolicited fast shutdown - retrying M%u with last settings", g_lastDnMode);
            MODE::cmdDiffuserOn(g_lastDnMode, g_lastDnPayload, true);   // isWaterRetry — don't re-arm the one-shot
        } else if (wasFast) {
            LOG::logMsg("WATER", "out of water - shut down again right after restart");
            g_outOfWater = true;
            USAGE::finalizeRefillCycle();             // Bank this cycle's usage, reset the accumulator
            if (g_parfumActive) {              // Dry diffuser can't be insisted on — end the window
                g_parfumActive      = false;
                g_parfumSetMin      = 0;
                g_parfumPendingKind = PARFUM_PEND_NONE;   // Drop any queued Dn/Df too — tank is dry
                LOG::logMsg("PARFUM", "cancelled - out of water");
            }
            STRIP::startOowAlert();                   // Overrides MODE::applyShutdown()'s STRIP::stripOff() with blue-alert
            PROTO::cmdStatusQuery();                  // Push Ds immediately — don't wait to be asked
        } else if (g_parfumActive && g_haveLastDn && !g_modeTargetActive) {
            // Parfum insists: an unsolicited (non-fast) shutdown mid-window is restarted
            // with the latched parfum payload. If the tank is actually dry, the restart
            // fast-drops → one retry → OOW diagnosis above cancels the window cleanly.
            // Skip auto-restart while a genuine (wrapping) mode-target transition is still
            // active (e.g., M2→M1 during parfum) - g_modeTargetActive is already false here
            // for an aborted ascending target, so that case still gets the restart.
            LOG::logMsg("PARFUM", "unsolicited shutdown - restarting, %u min remaining", PARFUM::parfumRemainingMin());
            MODE::cmdDiffuserOn(g_lastDnMode, g_lastDnPayload);
        } else {
            LOG::logMsg("MODE", "M0 (OFF) - unsolicited");   // No auto-restore — stays off until a new Dn
        }
    } else {
        vlogMsg("BUZZ", "unmapped burst");
    }
}

/** @brief  Sample A0, update peak-hold envelope, detect rising/falling edges. */
static void tickBuzzer() {
    int   raw = analogRead(BUZZER_PIN);
    int   dev = abs(raw - g_buzBaseline);
    g_buzEnv  = max((float)dev, g_buzEnv * BUZZER_ENV_DECAY);

#if BUZZER_DEBUG
    LOG::logMsg("BUZZ", "raw=%d dev=%d env=%.1f act=%d str=%u",
              raw, dev, g_buzEnv, g_buzToneActive, g_buzQuiet);
#endif

    if (!g_buzToneActive && g_buzEnv > BUZZER_ON_THR) {          // Rising edge
        g_buzToneActive = true;
        g_buzQuiet      = 0;
    }
    if (g_buzToneActive) {
        g_buzQuiet = (dev < BUZZER_OFF_THR) ? g_buzQuiet + 1 : 0;
        if (g_buzQuiet >= BUZZER_QUIET_CONF) {                    // Falling edge confirmed
            g_buzToneActive = false;
            g_buzQuiet      = 0;
            g_buzEnv        = dev;                                 // Snap down — block phantom re-trigger
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

/* ============================================================================
   USAGE
   Refill tracking and EEPROM persistence.
   ============================================================================ */
namespace USAGE {

/** @brief  Average of the valid entries in the refill-history ring buffer, in seconds (0 if none yet). */
static uint32_t averageRefillCycleSec() {
    if (g_refillHistoryCount == 0) return 0;
    uint64_t sum = 0;
    for (uint8_t i = 0; i < g_refillHistoryCount; i++) sum += g_refillHistory[i];
    return (uint32_t)(sum / g_refillHistoryCount);
}

/** @brief  Duty-cycle multiplier for a given mode (1-MODE_MAX). See MODE_DUTY_CYCLE. */
static float dutyCycleForMode(uint8_t mode) {
    if (mode < 1 || mode > MODE_MAX) return 0.0f;
    return MODE_DUTY_CYCLE[mode - 1];
}

/**
 * @brief  Bank the current cycle's accumulated runtime into the refill
 *         history and reset the accumulator. Called only from
 *         BUZZ::buzzerFinalizeBurst()'s confirmed out-of-water branch.
 *
 *         A cycle shorter than USAGE_MIN_CYCLE_SEC is NOT pushed into the
 *         history — it's almost certainly a still-dry retry attempt (the
 *         diffuser was refilled insufficiently, or not at all, and fast-
 *         dropped again right away) rather than a genuine full-tank cycle,
 *         and would otherwise pollute the rolling average with a near-zero
 *         outlier. The accumulator is reset either way — there's nothing
 *         meaningful left to carry forward into the next attempt.
 */
static void finalizeRefillCycle() {
    if (g_usageAccumSec >= USAGE_MIN_CYCLE_SEC) {
        g_refillHistory[g_refillHistoryNext] = g_usageAccumSec;
        g_refillHistoryNext = (g_refillHistoryNext + 1) % REFILL_HISTORY_SIZE;
        if (g_refillHistoryCount < REFILL_HISTORY_SIZE) g_refillHistoryCount++;
        g_totalRefillCount++;
        LOG::logMsg("USAGE", "refill cycle finalized: %lus (history %u/%u, avg=%lus, total=%lu)",
                  g_usageAccumSec, g_refillHistoryCount, REFILL_HISTORY_SIZE,
                  averageRefillCycleSec(), g_totalRefillCount);
    } else {
        vlogMsg("USAGE", "cycle too short (%lus < %lus) - not counted as a refill",
                  g_usageAccumSec, (unsigned long)USAGE_MIN_CYCLE_SEC);
    }
    g_usageAccumSec = 0;
    g_usageFracSec  = 0.0f;
    saveUsageToEeprom(true);
}

/** @brief  Load all usage-tracking fields from EEPROM (called once from setup()). */
static void loadUsageFromEeprom() {
    EEPROM.get(EE_ADDR_USAGE_ACCUM,    g_usageAccumSec);
    EEPROM.get(EE_ADDR_REFILL_COUNT,   g_refillHistoryCount);
    EEPROM.get(EE_ADDR_REFILL_NEXT,    g_refillHistoryNext);
    EEPROM.get(EE_ADDR_TOTAL_REFILLS,  g_totalRefillCount);
    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
        EEPROM.get(EE_ADDR_REFILL_HISTORY + i * 4, g_refillHistory[i]);
    }

    // Sanity-check virgin/corrupt flash (all-0xFF or nonsensical values) rather
    // than trusting it blindly — a blank ESP8266 reads back 0xFF per byte.
    if (g_refillHistoryCount > REFILL_HISTORY_SIZE) g_refillHistoryCount = 0;
    if (g_refillHistoryNext  >= REFILL_HISTORY_SIZE) g_refillHistoryNext = 0;
    if (g_refillHistoryCount == 0) {
        g_refillHistoryNext = 0;
        for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) g_refillHistory[i] = 0;
    }
    if (g_usageAccumSec == 0xFFFFFFFFUL) g_usageAccumSec = 0;
    if (g_totalRefillCount == 0xFFFFFFFFUL) g_totalRefillCount = 0;

    // What we just loaded IS what's currently on flash - seed the "last
    // persisted value" tracker with it so the first periodic checkpoint
    // after boot doesn't immediately re-write an unchanged value right back.
    g_usageLastSavedAccumSec = g_usageAccumSec;

    LOG::logMsg("USAGE", "loaded: accum=%lus history=%u/%u total=%lu avg=%lus",
              g_usageAccumSec, g_refillHistoryCount, REFILL_HISTORY_SIZE,
              g_totalRefillCount, averageRefillCycleSec());
}

/**
 * @brief  Persist usage-tracking fields to EEPROM.
 * @param  includeHistory  false = only the live accumulator (periodic
 *         checkpoint, see USAGE_EEPROM_CHECKPOINT_MS); true = everything,
 *         used right after a refill cycle finalizes.
 */
static void saveUsageToEeprom(bool includeHistory) {
    EEPROM.put(EE_ADDR_USAGE_ACCUM, g_usageAccumSec);
    if (includeHistory) {
        EEPROM.put(EE_ADDR_REFILL_COUNT,  g_refillHistoryCount);
        EEPROM.put(EE_ADDR_REFILL_NEXT,   g_refillHistoryNext);
        EEPROM.put(EE_ADDR_TOTAL_REFILLS, g_totalRefillCount);
        for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
            EEPROM.put(EE_ADDR_REFILL_HISTORY + i * 4, g_refillHistory[i]);
        }
    }
    EEPROM.commit();
    g_usageLastSaveMs        = millis();
    g_usageLastSavedAccumSec = g_usageAccumSec;
}

/**
 * @brief  Accumulate duty-cycle-weighted "effective" running seconds while
 *         powered. Called every USAGE_TICK_MS from loop(); also throttles a
 *         periodic EEPROM checkpoint of the live accumulator so a power loss
 *         mid-cycle doesn't lose more than ~USAGE_EEPROM_CHECKPOINT_MS worth.
 *
 *         The sub-second remainder (e.g. M2's 0.5x duty means every tick
 *         only contributes half a "real" second) is carried in
 *         g_usageFracSec across calls rather than rounded off each time -
 *         rounding per-tick would silently inflate M2's weighted total back
 *         up to the same as full duty (0.5 + 0.5 rounds to 1 EVERY tick,
 *         instead of the correct 1 every TWO ticks).
 *
 *         The checkpoint itself only actually writes when g_usageAccumSec has
 *         moved since the last write - g_usageLastCheckMs advances every
 *         USAGE_EEPROM_CHECKPOINT_MS regardless (so this only re-evaluates on
 *         schedule, not every tick), but if the diffuser was off/idle the
 *         whole interval there's nothing new to persist, and re-writing the
 *         same value is a pointless flash-wear cost. See saveUsageToEeprom().
 */
static void tickUsageAccum() {
    unsigned long now     = millis();
    unsigned long elapsed = now - g_usageLastTickMs;
    g_usageLastTickMs     = now;

    if (g_powered && !g_outOfWater) {
        g_usageFracSec += (elapsed / 1000.0f) * dutyCycleForMode(g_mode);
        uint32_t wholeSec = (uint32_t)g_usageFracSec;
        if (wholeSec > 0) {
            g_usageAccumSec += wholeSec;
            g_usageFracSec  -= wholeSec;
        }
    }

    if (now - g_usageLastCheckMs >= USAGE_EEPROM_CHECKPOINT_MS) {
        g_usageLastCheckMs = now;
        if (g_usageAccumSec != g_usageLastSavedAccumSec) {
            saveUsageToEeprom(false);
        } else {
            vlogMsg("USAGE", "checkpoint skipped - accum unchanged (%lus) since last save", g_usageAccumSec);
        }
    }
}

} // namespace USAGE

/* ============================================================================
   PARFUM
   Timed insist dispense mode.
   ============================================================================ */
namespace PARFUM {

/** @return Remaining parfum minutes (ceil, min 1 while active), 0 when inactive. */
static uint16_t parfumRemainingMin() {
    if (!g_parfumActive) return 0;
    long remMs = (long)(g_parfumEndMs - millis());
    if (remMs <= 0) return 1;                                   // Expiring this loop — still "active"
    return (uint16_t)((remMs + 59999L) / 60000L);               // Ceil to whole minutes
}

/**
 * @brief  Start (or restart) a parfum window.
 * @param  minutes  Duration, clamped to 1..PARFUM_MAX_MIN.
 * @param  mode     Requested diffuser mode (M1 CONT or M2 10 SEC — UDP DpMMMME's
 *                   E digit, validated in PROTO::udpExec(); console "P<min>" has no E
 *                   digit and relies on the PARFUM_DIF_MODE default). Logged for
 *                   reference only — the window always physically runs in
 *                   PARFUM_RUN_MODE (10 SEC) regardless, per the coercion below.
 *
 * Drives MODE to the coerced run mode via the normal MODE::cmdDiffuserOn() path with a
 * violet PULSE seed payload — latched as lastDn, so an unsolicited-shutdown
 * auto-restart (which re-sends g_lastDnPayload directly, bypassing this function)
 * still has a valid visual cue to fall back on. The live cue is then switched to
 * the random-colour breathe effect via STRIP::startParfumBreathe(), which overrides it.
 */
static bool parfumStart(uint16_t minutes, uint8_t mode) {
    if (minutes < 1)              minutes = 1;
    if (minutes > PARFUM_MAX_MIN) minutes = PARFUM_MAX_MIN;

    uint8_t reqMode = mode;             // keep the requested E purely for the log line below

    if (!g_parfumActive) {                     // Fresh window only — a Dp re-trigger while already
        g_parfumHavePrevDn  = g_haveLastDn;    // active just extends/restarts the timer, so keep the
        g_parfumPrevMode    = g_lastDnMode;    // original pre-parfum snapshot instead of re-capturing
        g_parfumPrevPayload = g_lastDnPayload; // the parfum's own violet-cue state.
    }

    DnPayload p{};
    p.dual       = false;
    p.effect     = PARFUM_EFFECT;                               // PULSE — visible "parfum" cue
    p.brightness = PARFUM_BRIGHTNESS;
    p.speedMs    = STRIP_EFFECT_TICK_MS;
    p.r1 = PARFUM_COLOR_R; p.g1 = PARFUM_COLOR_G; p.b1 = PARFUM_COLOR_B;
    // NOTE: colour duplicated into r2/g2/b2 on purpose — STRIP::applyDnPayload() swaps
    // the bases (g_effectBase1 = c2), and non-dual animated effects read
    // g_effectBase1 for every LED. A single-colour payload with r2/g2/b2 left
    // at zero therefore pulses BLACK (no colour). Filling both slots makes the
    // violet cue render identically whichever base the effect frame picks.
    p.r2 = PARFUM_COLOR_R; p.g2 = PARFUM_COLOR_G; p.b2 = PARFUM_COLOR_B;

    g_parfumActive      = true;
    g_parfumSetMin      = minutes;
    g_parfumEndMs       = millis() + (unsigned long)minutes * 60000UL;
    g_parfumPendingKind = PARFUM_PEND_NONE;    // Fresh window — drop any queue left from a prior one

    LOG::logMsg("PARFUM", "ON - %u min, requested M%u -> running M%u (%s), breathe cue (fallback #%02X%02X%02X %s)",
              minutes, reqMode, mode, MODE::modeName(mode),
              PARFUM_COLOR_R, PARFUM_COLOR_G, PARFUM_COLOR_B, STRIP::effectName(PARFUM_EFFECT));

    if (!MODE::cmdDiffuserOn(mode, p)) {                              // Rejected (WiFi seq active) — abort window
        g_parfumActive = false;
        g_parfumSetMin = 0;
        LOG::logMsg("PARFUM", "start rejected - window cancelled");
        return false;
    }
    STRIP::startParfumBreathe();                                       // override the PULSE seed with the live cue
    return true;
}

/**
 * @brief  End the parfum window — restore, hand off to a queued command, or shut down.
 * @param  reason         Log tag: "cancel" (Dp0000), "timer expired", "console", …
 * @param  naturalExpiry  true only from tickParfum(). Resolution order on natural
 *                         expiry: (1) a Dn queued while parfum insisted (see PROTO::udpExec())
 *                         wins, if still valid (MODE::cmdDiffuserOn() still accepts it);
 *                         (2) else a queued Df always shuts down — an explicit off
 *                         request must never be overridden by the pre-parfum revert;
 *                         (3) else, with nothing queued, the mode/colour/effect active
 *                         right before this window started (see parfumStart() snapshot),
 *                         if still valid; (4) else shut down. Manual stops (Dp0000/M0/OOW
 *                         cancel) always shut down and drop both the queue and the snapshot.
 */
static bool parfumStop(const char *reason, bool naturalExpiry) {
    if (!g_parfumActive) return true;                           // Already inactive - nothing to do
    g_parfumActive = false;
    g_parfumSetMin = 0;
    LOG::logMsg("PARFUM", "OFF - %s", reason);

    ParfumPendingKind pending = g_parfumPendingKind;            // Snapshot before the one-shot reset below —
    g_parfumPendingKind = PARFUM_PEND_NONE;                     // otherwise a queued Df is indistinguishable
                                                                 // from "nothing queued" by the time we get here.
    if (naturalExpiry && pending == PARFUM_PEND_ON) {
        uint8_t   m = g_parfumPendingMode;
        DnPayload p = g_parfumPendingPayload;
        LOG::logMsg("PARFUM", "applying queued Dn mode=%02X(%s)", m, MODE::modeName(m));
        if (MODE::cmdDiffuserOn(m, p)) return true;                   // Still valid — run it instead of shutting down
        LOG::logMsg("PARFUM", "queued Dn no longer valid - falling back to shutdown");
    } else if (naturalExpiry && pending == PARFUM_PEND_NONE && g_parfumHavePrevDn) {
        uint8_t   m = g_parfumPrevMode;                         // Nothing queued — fall back to whatever mode
        DnPayload p = g_parfumPrevPayload;                      // was running right before this parfum window
        g_parfumHavePrevDn = false;                             // started, instead of turning off.
        LOG::logMsg("PARFUM", "no command received - reverting to pre-parfum M%02X(%s)", m, MODE::modeName(m));
        if (MODE::cmdDiffuserOn(m, p)) return true;                   // Still valid — restore it instead of shutting down
        LOG::logMsg("PARFUM", "pre-parfum mode no longer valid - falling back to shutdown");
    }
    // pending == PARFUM_PEND_OFF (explicit Df queued) falls straight through to shutdown below,
    // same as a manual stop or either fallback above failing validity.
    g_parfumHavePrevDn = false;                                 // Drop a stale snapshot either way (manual stop too)

    g_udpStatusReplyMode = UDP_STATUS_AFTER_SHUTDOWN;           // Reply Ds once truly off, like Df
    bool ok = MODE::cmdModeTarget(0);
    if (!ok) MODE::clearPendingStatusReply();
    return ok;
}

/** @brief  Expire the parfum window when its deadline passes. Call every loop(). */
static void tickParfum() {
    if (!g_parfumActive) return;
    if ((long)(millis() - g_parfumEndMs) < 0) return;           // Still running - Logic
    parfumStop("timer expired", true);                          // true — apply a queued Dn if one is valid
}

} // namespace PARFUM

/* ============================================================================
   WIFI
   WiFi connection sequencing and status LED.
   ============================================================================ */
namespace WIFI {

/** @brief  Kick off a (re)connect attempt; non-blocking — see tickWifiConnecting(). */
static void beginWifiConnect() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    g_wifiConnecting = true;
    g_wifiConnNextMs = millis();
    g_stripMode      = STRIP_WIFI_CUE;          // rainbow spin until connected
    g_animPhase      = 0;
}

static void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    LOG::logMsg("WIFI", "connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    beginWifiConnect();
    // Boot-time only: nothing else needs servicing yet, so pump the same
    // non-blocking pieces (strip cue + pin release) without delay().
    while (g_wifiConnecting) {
        MODE::tickPinRelease();
        tickWifiConnecting();
        STRIP::tickStripWifiCue();
        STRIP::tickStripFade();
        yield();
    }
}

/** @brief  Finish wiring up services once WiFi reports connected. */
static void finishWifiConnect() {
    g_wifiConnecting = false;
    STRIP::stripOff();                                 // clear connecting cue — next Dn sets real state
    LOG::logMsg("WIFI", "OK: %s", WiFi.localIP().toString().c_str());
    g_telnetSrv.begin();                        // Must (re)bind after WiFi comes up
    g_telnetSrv.setNoDelay(true);
    PROTO::udpOpen();
    startWifiSequence();                        // connected -> step MODE until shutdown detected
}

static void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        LOG::logMsg("OTA", "start: %s",
            (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem");
    });
    ArduinoOTA.onEnd([]()                     { LOG::logMsg("OTA", "done - rebooting"); ESP.restart(); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
#ifdef _DEBUG
        LOG::dbgPrintf("[OTA   ] %u%%\r", p / (t / 100));     // \r overwrite - raw, not logMsg
#else
        (void)p; (void)t;
#endif
    });
    ArduinoOTA.onError([](ota_error_t e) {
        const char* reason =
            e == OTA_AUTH_ERROR    ? "Auth Failed"    :
            e == OTA_BEGIN_ERROR   ? "Begin Failed"   :
            e == OTA_CONNECT_ERROR ? "Connect Failed" :
            e == OTA_RECEIVE_ERROR ? "Receive Failed" :
            e == OTA_END_ERROR     ? "End Failed"     : "Unknown";
        LOG::logMsg("OTA", "error[%u]: %s", e, reason);
    });
    ArduinoOTA.begin();
    LOG::logMsg("OTA", "ready - hostname: %s", OTA_HOSTNAME);
}

/** @brief  Kick off step-by-step MODE detection; call once per successful WiFi (re)connect. */
static void startWifiSequence() {
    if (g_parfumActive) {                  // Parfum window running — don't seize the MODE button and
        vlogMsg("SEQ", "parfum active - calibration deferred");   // disturb it just to calibrate ground truth
        g_seqPhase = SEQ_DONE;
        return;
    }

    // Cancel anything else driving MODE so the sequence has sole control.
    g_modeTargetActive = false;
    g_modeTargetWraps  = false;
    g_modePending      = false;

    if (!g_powered) {                      // Already OFF — nothing to detect/calibrate
        vlogMsg("SEQ", "already OFF - calibration not needed");
        g_seqPhase = SEQ_DONE;
        return;
    }
    g_seqPhase        = SEQ_MODE_STEP;
    g_seqModeAttempts = 0;
    g_seqNextMs       = millis();          // first press fires immediately
    vlogMsg("SEQ", "WiFi connected - stepping MODE until shutdown beep detected");
}

static void tickLed() {
    if (WiFi.status() == WL_CONNECTED) digitalWrite(LED_PIN, HIGH);          // Active-LOW: HIGH = off
    else                               digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Blink = no WiFi
}

static void tickWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (g_wifiConnecting) return;               // already (re)connecting — tickWifiConnecting() drives it
    LOG::logMsg("WIFI", "lost - reconnecting...");
    WiFi.disconnect();
    beginWifiConnect();                         // non-blocking; strip cue + finalize via tickWifiConnecting()
}

/** @brief  Non-blocking: paces the "." progress dot while connecting;
 *  the rainbow cue itself is advanced by STRIP::tickStripWifiCue().
 *  Call every loop() iteration. */
static void tickWifiConnecting() {
    if (!g_wifiConnecting) return;
    if (WiFi.status() == WL_CONNECTED) { finishWifiConnect(); return; }
    unsigned long now = millis();
    if (now < g_wifiConnNextMs) return;
    LOG::dbgWrite(".");
    g_wifiConnNextMs = now + WIFI_SEQ_STEP_MS;
}

/** @brief  Advance the post-connect MODE-step detection; called every loop(). */
static void tickWifiSequence() {
    if (!wifiSequenceActive()) return;
    if (g_modePending) return;                        // waiting on buzzer confirm/timeout
    if (g_modePinPending) return;                      // wait for current pulse to release

    unsigned long now = millis();
    if (now < g_seqNextMs) return;

    if (g_seqModeAttempts >= WIFI_SEQ_MODE_MAX_ATTEMPTS) {
        // Safety net: never detected a shutdown beep after enough short
        // presses to have cycled through every mode — fall back to a forced
        // long-press so the mirror still ends up calibrated.
        LOG::logMsg("SEQ", "MODE step cap reached - forcing long-press shutdown");
        MODE::pulseModeRaw(BTN_LONG_MS);
        MODE::applyShutdown();
        g_seqPhase = SEQ_DONE;
        return;
    }

    MODE::pulseModeShort();
    g_modePending = true;
    g_modePendMs  = now;
    g_seqModeAttempts++;
    vlogMsg("SEQ", "MODE step press #%u", g_seqModeAttempts);
}

/** @return true while the forced sequence owns the MODE button. */
static inline bool wifiSequenceActive() {
    return g_seqPhase != SEQ_NONE && g_seqPhase != SEQ_DONE;
}

} // namespace WIFI

/* ============================================================================
   PROTO
   Wire protocol / command dispatch (port 8439).
   ============================================================================ */
namespace PROTO {

/**
 * @brief  Parse two hex chars into a byte (case-insensitive, 0 on non-hex).
 * @param  h  Pointer to at least two characters.
 * @return Parsed byte value.
 */
static uint8_t ackHexByte(const char *h) {
    auto nib = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    return (uint8_t)((nib(h[0]) << 4) | nib(h[1]));
}

/**
 * @brief  UDP "Dh" — report the full refill-cycle history, on demand only.
 *
 * Unlike cmdStatusQuery()'s accum/avg/count summary (which rides every
 * periodic Ds/Dc poll), this sends all up-to-REFILL_HISTORY_SIZE individual
 * past cycle durations — bulkier and only needed when something actually
 * wants to show the breakdown, not every poll. SmartTV relays this straight
 * through to the app without caching it (see SmartTV's DIF::AckParse-style
 * "Dh" handling) — this firmware doesn't know or care what happens to it
 * after it's sent.
 *
 * Reply is "Dh" + RR(2-hex valid count, 0-REFILL_HISTORY_SIZE) + one
 * VVVV(4-hex minutes) per slot, oldest-first, unused trailing slots as 0000.
 */
static void cmdHistoryQuery() {
    char resp[48];
    int pos = snprintf(resp, sizeof(resp), "Dh%02X", g_refillHistoryCount);

    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
        uint32_t minutes = 0;
        if (i < g_refillHistoryCount) {
            // Oldest-first: while the ring hasn't wrapped yet (count < size),
            // slot i is already in insertion order. Once full, the true
            // oldest entry sits at g_refillHistoryNext (the slot about to be
            // overwritten next) — wrap forward from there instead.
            uint8_t idx = (g_refillHistoryCount < REFILL_HISTORY_SIZE)
                        ? i
                        : (uint8_t)((g_refillHistoryNext + i) % REFILL_HISTORY_SIZE);
            minutes = g_refillHistory[idx] / 60UL;
        }
        pos += snprintf(resp + pos, sizeof(resp) - pos, "%04X",
                         (unsigned)min(minutes, (uint32_t)0xFFFF));
    }

    udpSend(resp);
    vlogMsg("UDP_S", "Dh -> %s (history %u/%u)", resp, g_refillHistoryCount, REFILL_HISTORY_SIZE);
}

/**
 * @brief  UDP "Ds"/"Dc" — report current diffuser status.
 * @param  silent  true = skip dbg log (used by "Dc" poll, called every X seconds
 *                  so it doesn't spam the debug sink); false = log as usual ("Ds").
 *
 * Reply is "Ds" + MM(mode) + SS(strip) + TTTT(parfum min) + UUUU(usage accum min) +
 * VVVV(avg refill-cycle min) + RR(refill history count, 0-10) + LLLL(lifetime refill
 * count, clamped to 0xFFFF) — 24 chars total. See ParseStatus() on the SmartTV side.
 */
static void cmdStatusQuery(bool silent) {
    uint8_t  reportMode   = g_outOfWater ? STATUS_MODE_OUT_OF_WATER : g_mode;
    uint8_t  stripCode    = STRIP::stripStatusCode();
    uint16_t parfumMin    = PARFUM::parfumRemainingMin();                  // 0000 = parfum inactive
    uint16_t accumMin     = (uint16_t)min((uint32_t)(g_usageAccumSec / 60UL), (uint32_t)0xFFFF);
    uint16_t avgMin       = (uint16_t)min((uint32_t)(USAGE::averageRefillCycleSec() / 60UL), (uint32_t)0xFFFF);
    uint16_t totalRefills = (uint16_t)min(g_totalRefillCount, (uint32_t)0xFFFF);
    char resp[32];
    snprintf(resp, sizeof(resp), "Ds%02X%02X%04X%04X%04X%02X%04X", reportMode, stripCode, parfumMin,
             accumMin, avgMin, g_refillHistoryCount, totalRefills);
    udpSend(resp);
    if (silent) return;
    vlogMsg("UDP_S", "Ds -> %s (mode=M%u(%s)%s%s strip=%u(%s) dual=%s br=%u/255 speed=%ums base1=#%02X%02X%02X base2=#%02X%02X%02X usage=%umin avg=%umin refills=%u/%u total=%u)",
              resp,
              g_mode,       MODE::modeName(g_mode),
              g_outOfWater ? " [OUT OF WATER]" : "",
              g_parfumActive ? " [PARFUM]" : "",
              stripCode,    STRIP::stripStatusName(stripCode),
              g_effectDual ? "yes" : "no", g_stripBrightness, g_effectTickMs,
              g_effectBase1.r, g_effectBase1.g, g_effectBase1.b,
              g_effectBase2.r, g_effectBase2.g, g_effectBase2.b,
              accumMin, avgMin, g_refillHistoryCount, REFILL_HISTORY_SIZE, totalRefills);
}

/** @brief  Poll UDP socket; dispatch one packet per call. */
static void tickUDP() {
    static unsigned long chk = 0;
    static bool          ok  = false;
    unsigned long now = millis();
    if (now - chk >= 500) { ok = (WiFi.status() == WL_CONNECTED); chk = now; }
    if (!ok) return;

    int sz = g_udp.parsePacket();
    if (sz <= 0) return;
    if (sz >= UDP_BUF_SIZE) { g_udp.flush(); return; }

    g_udpSenderIP = g_udp.remoteIP();
    int len = g_udp.read(g_udpRxBuf, UDP_BUF_SIZE - 1);
    if (len <= 0) return;
    while (len > 0 && (g_udpRxBuf[len-1] == '\n' || g_udpRxBuf[len-1] == '\r')) len--;
    g_udpRxBuf[len] = '\0';

    vlogMsg("UDP_R", "recv [%s] (%d)", g_udpRxBuf, len);
    udpExec(g_udpRxBuf);
}

/**
 * @brief  Reply "#SSR" to the sender of the current packet (same path as Ds).
 * @param  seq     Sequence id echoed from the command.
 * @param  result  ACK_* result code (0..6).
 */
static void udpAck(uint8_t seq, uint8_t result) {
    char msg[5];                       // '#' + 2 hex seq + 1 hex result + NUL
    snprintf(msg, sizeof(msg), "%c%02x%1x", ACK_CHAR, seq, result & 0x0F);
    udpSend(msg);
}

/**
 * @brief  Dispatch one inbound command (envelope already stripped by udpExec()).
 *         Any command may arrive wrapped as "#SS<cmd>" (SS = 2 hex seq); the
 *         udpExec() wrapper then replies "#SSR" with the result code this
 *         function selects via g_ackResult (default ACK_OK).
 * @param  buf  Null-terminated command (already stripped of CR/LF and envelope).
 *
 * Recognized:
 *   Ds                          — query status  (replies "DsMMSSTTTT" hex: mode / strip-status-code /
 *                                  parfum remaining minutes, 0000 = parfum inactive)
 *   Dc                          — poll status   (same reply as Ds; silent — no dbg log, for periodic polling)
 *   Df                          — shutdown      (drives MODE down to off, same as M0; strip forced off;
 *                                  QUEUED while parfum is active — applied at natural expiry, see
 *                                  PARFUM note above; Ds still replied immediately)
 *   DpMMMME                     — parfum mode   (MMMM hex minutes, well-formed hex > 0168 is CLAMPED to
 *                                  0168 (360 min max) rather than rejected, ACK_CLAMPED if so + mode
 *                                  digit E: timed run in mode E that ignores Df/Dn until expiry, violet
 *                                  PULSE strip cue; E = 1 or 2 only — M0 "off"/M3-M4 "after sleep" don't
 *                                  apply to a manual Parfum run; E is mandatory whenever MMMM != 0000)
 *   Dp0000                      — parfum cancel + shutdown (no mode digit — "off" needs no mode)
 *   DnXXrrggbbBREESP           — turn on (XX = target MODE hex 01-04) + solid colour rrggbb
 *                                  + brightness BR + effect EE + speed SP
 *   DnXXrrggbbRRGGBBBREESP     — turn on + dual colour rrggbb/RRGGBB (split across the 6 LEDs)
 *                                  + brightness BR + effect EE + speed SP
 *                                 (BR hex 00-FF: colour scale factor, never a NeoPixel hardware
 *                                  brightness register — baked into the colour so it always fades
 *                                  in smoothly; EE hex 00-08: 00=static 01=fade 02=pulse 03=random
 *                                  04=rainbow 05=sparkle 06=fire 07=bounce 08=confetti; Chase is
 *                                  reserved internally for the out-of-water alert and is not
 *                                  reachable via this command; SP hex 00-FF: animated-effect frame
 *                                  period in ms, clamped to STRIP_EFFECT_SPEED_MIN_MS; colour is
 *                                  always required — animated effects use it as fallback/base.
 *                                  All fields are mandatory — no legacy short form accepted)
 */
/* ════════════════════════════════════════════════════════════════════════════
   UDP  ·  INBOUND   —  commands received FROM the SmartTV
   udpDispatch() parses; udpExec() unwraps the #SS envelope; tickUDP() polls.
   ════════════════════════════════════════════════════════════════════════════ */

static void udpDispatch(char *buf) {
    vlogMsg("UDP_R", "exec [%s]", buf);
    size_t len = strlen(buf);
    if (len >= 2 && (buf[0] == 'D' || buf[0] == 'd')) {
        char c1 = buf[1];
        if ((c1 == 's' || c1 == 'S') && len == 2) {
            cmdStatusQuery(false);
            return;
        }
        if ((c1 == 'c' || c1 == 'C') && len == 2) {
            cmdStatusQuery(true);   // silent — periodic poll, don't spam dbg sink
            return;
        }
        if ((c1 == 'h' || c1 == 'H') && len == 2) {
            cmdHistoryQuery();      // on-demand full refill history — see cmdHistoryQuery()
            return;
        }
        if ((c1 == 'r' || c1 == 'R') && len == 2) {
            LOG::logMsg("USAGE", "manual refill - banking %lus, resetting accumulator", g_usageAccumSec);
            if (g_oowAlertActive) {
                g_oowAlertActive = false;
                LOG::logMsg("WATER", "OOW alert cleared (manual refill)");
            }
            g_outOfWater = false;                  // Tank is no longer dry - Logic
            USAGE::finalizeRefillCycle();           // Bank current usage, reset accumulator, persist EEPROM
            cmdStatusQuery();                       // Reply immediately - Ds reflects the fresh usage state
            return;
        }
        if ((c1 == 'f' || c1 == 'F') && len == 2) {
            if (g_parfumActive) {                                   // Parfum insists — stop refused now, but
                g_parfumPendingKind = PARFUM_PEND_OFF;               // queued as the last command (clears any
                LOG::logMsg("PARFUM", "Df queued (off) - %u min remaining", PARFUM::parfumRemainingMin());
                cmdStatusQuery();                                    // queued Dn) — applied at natural expiry
                g_ackResult = ACK_LOCKED;                            // deferred, not applied now
                return;
            }
            LOG::logMsg("UDP_R", "Df - shutdown");
            g_udpStatusReplyMode = UDP_STATUS_AFTER_SHUTDOWN;
            if (!MODE::cmdModeTarget(0)) { MODE::clearPendingStatusReply(); g_ackResult = ACK_LOCKED; }
            return;
        }
        if ((c1 == 'p' || c1 == 'P') && (len == 6 || len == 7)) {   // Dp0000 cancel, or DpMMMME start
            unsigned int mins = 0;
            if (sscanf(buf + 2, "%4x", &mins) != 1) {
                LOG::logMsg("UDP_R", "Dp bad payload [%s] (0000-%04X)", buf, PARFUM_MAX_MIN);
                g_ackResult = ACK_REJECTED;
                return;
            }
            bool minsClamped = false;
            if (mins > PARFUM_MAX_MIN) {                            // Out of range but well-formed — coerce
                LOG::logMsg("UDP_R", "Dp minutes %04X clamped to %04X (max)", mins, PARFUM_MAX_MIN);
                mins = PARFUM_MAX_MIN;
                minsClamped = true;
            }
            if (mins == 0) {
                if (len != 6) { LOG::logMsg("UDP_R", "dp0000 cancel takes no mode digit [%s]", buf); g_ackResult = ACK_REJECTED; return; }
                if (g_parfumActive) { if (!PARFUM::parfumStop("cancel command")) g_ackResult = ACK_LOCKED; }
                else { vlogMsg("PARFUM", "cancel - already inactive"); cmdStatusQuery(); }
                return;
            }
            if (len != 7) {                                         // Starting a run — E is mandatory
                LOG::logMsg("UDP_R", "Dp start needs mode digit (DpMMMME) [%s]", buf);
                g_ackResult = ACK_REJECTED;
                return;
            }
            unsigned int e = 0;
            if (sscanf(buf + 6, "%1x", &e) != 1 || (e != 1 && e != 2)) {
                LOG::logMsg("UDP_R", "Dp bad mode E=%X [%s] (E=1 or 2 only)", e, buf);
                g_ackResult = ACK_REJECTED;
                return;
            }
            if (!PARFUM::parfumStart((uint16_t)mins, (uint8_t)e)) g_ackResult = ACK_LOCKED;
            else if (minsClamped) g_ackResult = ACK_CLAMPED;
            return;
        }
        if ((c1 == 'n' || c1 == 'N') && len >= 16) {
            unsigned int mh = 0;
            const char  *pay    = buf + 4;
            size_t       payLen = len - 4;
            DnPayload    p{};
            bool         ok = (sscanf(buf + 2, "%2x", &mh) == 1);

            if (ok && payLen == 12) {                      // rrggbb + BR + EE + SP
                unsigned int rgb = 0, br = 0, eff = 0, sp = 0;
                ok = (sscanf(pay,      "%6x", &rgb) == 1)
                  && (sscanf(pay + 6,  "%2x", &br)  == 1)
                  && (sscanf(pay + 8,  "%2x", &eff) == 1) && eff <= STRIP_EFFECT_COUNT
                  && (sscanf(pay + 10, "%2x", &sp)  == 1);
                p.dual       = false;
                p.effect     = (uint8_t)eff;
                p.brightness = (uint8_t)br;
                p.speedMs    = (uint8_t)sp;
                p.r1 = (rgb >> 16) & 0xFF; p.g1 = (rgb >> 8) & 0xFF; p.b1 = rgb & 0xFF;
            } else if (ok && payLen == 18) {               // rrggbb + rr2gg2bb2 + BR + EE + SP
                unsigned int rgb1 = 0, rgb2 = 0, br = 0, eff = 0, sp = 0;
                ok = (sscanf(pay,      "%6x", &rgb1) == 1)
                  && (sscanf(pay + 6,  "%6x", &rgb2) == 1)
                  && (sscanf(pay + 12, "%2x", &br)   == 1)
                  && (sscanf(pay + 14, "%2x", &eff)  == 1) && eff <= STRIP_EFFECT_COUNT
                  && (sscanf(pay + 16, "%2x", &sp)   == 1);
                p.dual       = true;
                p.effect     = (uint8_t)eff;
                p.brightness = (uint8_t)br;
                p.speedMs    = (uint8_t)sp;
                p.r1 = (rgb1 >> 16) & 0xFF; p.g1 = (rgb1 >> 8) & 0xFF; p.b1 = rgb1 & 0xFF;
                p.r2 = (rgb2 >> 16) & 0xFF; p.g2 = (rgb2 >> 8) & 0xFF; p.b2 = rgb2 & 0xFF;
            } else {
                ok = false;
            }

            if (!ok) { LOG::logMsg("UDP_R", "Dn bad payload [%s]", buf); g_ackResult = ACK_REJECTED; return; }
            if (mh == 0 || mh > MODE_MAX) {
                LOG::logMsg("UDP_R", "Dn bad mode %02X (M1-M%u) [%s]", mh, MODE_MAX, buf);
                g_ackResult = ACK_REJECTED;
                return;
            }

            if (g_parfumActive) {                                   // Parfum owns mode + strip look now, but
                g_parfumPendingKind    = PARFUM_PEND_ON;             // queue this as the last command — applied
                g_parfumPendingMode    = (uint8_t)mh;                // instead of the plain shutdown once the
                g_parfumPendingPayload = p;                          // window naturally expires (see PARFUM::parfumStop())
                LOG::logMsg("PARFUM", "Dn queued mode=%02X(%s) - %u min remaining",
                          mh, MODE::modeName((uint8_t)mh), PARFUM::parfumRemainingMin());
                g_ackResult = ACK_LOCKED;                            // deferred, not applied now
                return;
            }

            LOG::logMsg ("UDP_R", "Dn mode=%02X(%s) effect=%u(%s)", mh, MODE::modeName((uint8_t)mh), p.effect, STRIP::effectName(p.effect));
            vlogMsg("UDP_R", "Dn dual=%s rgb1=#%02X%02X%02X rgb2=#%02X%02X%02X br=%u/255 speed=%ums payload=[%s]",
                    p.dual ? "yes" : "no", p.r1, p.g1, p.b1, p.r2, p.g2, p.b2,
                    p.brightness, p.speedMs, pay);
            if (!MODE::cmdDiffuserOn((uint8_t)mh, p)) g_ackResult = ACK_LOCKED;
            return;
        }
    }
    LOG::logMsg("UDP_R", "unrecognized: [%s]", buf);
    g_ackResult = ACK_UNSUPPORTED;
}

/**
 * @brief  UDP entry point. Unwraps an optional "#SS" ACK envelope, dispatches
 *         the inner command, then replies "#SSR" with the outcome. Bare
 *         (un-enveloped) commands dispatch exactly as before and get no ack.
 * @param  buf  Null-terminated inbound packet (CR/LF already stripped).
 */
static void udpExec(char *buf) {
    g_ackValid  = false;
    g_ackResult = ACK_OK;

    if (buf[0] == ACK_CHAR && strlen(buf) >= ACK_HDR) {   // "#SS<cmd>"
        g_ackSeq   = ackHexByte(buf + 1);
        g_ackValid = true;
        buf += ACK_HDR;                                   // strip envelope header
    }

    udpDispatch(buf);

    if (g_ackValid) udpAck(g_ackSeq, g_ackResult);
}

static void udpOpen() {
    g_udp.begin(UDP_PORT);
    vlogMsg("UDP", "socket opened on port %d", UDP_PORT);
}

/**
 * @brief  Send msg UDP packet to cached sender IP:UDP_PORT.
 * @param  msg  Null-terminated string.
 */
/* ════════════════════════════════════════════════════════════════════════════
   UDP  ·  OUTBOUND  —  replies sent TO the SmartTV
   udpSend() = raw send; udpAck() = "#SSR"; cmdStatusQuery() = "DsMMSSTTTT".
   ════════════════════════════════════════════════════════════════════════════ */

static void udpSend(const char *msg) {
    if (!msg || WiFi.status() != WL_CONNECTED) return;
    if (g_udpSenderIP == IPAddress(0,0,0,0)) return;

    unsigned long t = millis();
    bool ok = false;
    while (millis()-t < UDP_TIMEOUT_MS) { if (g_udp.beginPacket(g_udpSenderIP, UDP_PORT)) { ok=true; break; } }
    if (!ok) return;
    g_udp.write(msg);
    t = millis();
    while (millis()-t < UDP_TIMEOUT_MS) { if (g_udp.endPacket()) break; }
}

} // namespace PROTO

/* ============================================================================
   TELNET
   Console (port 23).
   ============================================================================ */
namespace TELNET {

static void handleCmd(String s) {
    s.trim(); s.replace("\r", ""); s.toUpperCase();
    if (!s.length()) return;
    uint8_t v = 0;
    if      (parseCmd(s, 'M', MODE_MAX, v)) MODE::cmdModeTarget(v);
    else if (s.charAt(0) == 'P' && s.length() >= 2) {          // "P<min>" — parfum on/off (decimal)
        long mins = s.substring(1).toInt();
        bool numeric = true;
        for (uint8_t i = 1; i < s.length(); i++)
            if (s.charAt(i) < '0' || s.charAt(i) > '9') { numeric = false; break; }
        if (!numeric || mins > PARFUM_MAX_MIN) LOG::logMsg("CMD", "bad parfum minutes: [%s] (0-%d)", s.c_str(), PARFUM_MAX_MIN);
        else if (mins == 0) { if (g_parfumActive) PARFUM::parfumStop("console"); else vlogMsg("PARFUM", "already inactive"); }
        else                PARFUM::parfumStart((uint16_t)mins);
    }
    else if (s == "E") { STRIP::stripApplyEffectNext(); LOG::logMsg("STRIP", "effect (manual)"); }
    else if (s.charAt(0) == 'C' && s.length() == 7) {          // "Crrggbb" — manual solid colour test
        unsigned int rgb = 0;
        if (sscanf(s.c_str() + 1, "%6x", &rgb) == 1) {
            DnPayload p{}; p.dual = false; p.effect = 0;
            p.brightness = 255; p.speedMs = STRIP_EFFECT_TICK_MS;   // manual test — full BR, default speed
            p.r1 = (rgb >> 16) & 0xFF; p.g1 = (rgb >> 8) & 0xFF; p.b1 = rgb & 0xFF;
            STRIP::applyDnPayload(p);
        } else LOG::logMsg("CMD", "bad colour: [%s]", s.c_str());
    }
    else if (s == "S")            PROTO::cmdStatusQuery();
    else if (s == "D")            LOG::cmdDebugInfo();
    else if (s == "?" || s == "H") LOG::cmdHelp();
    else LOG::logMsg("CMD", "unknown: [%s] - send ? or H for help", s.c_str());
}

/** @brief  Parse "X<N>" where X==prefix; validate ≤ maxVal; set out. @return true on success. */
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

static void tickTelnet() {
    // A fresh incoming connection always takes over immediately, even if the
    // current client still *thinks* it's connected - WiFiClient::connected()
    // can be slow to notice a peer that went away without a clean close (e.g.
    // a tool that crashed or exited without closing the socket), and until it
    // does, the old logic here kept the single client slot occupied,
    // refusing every new connection attempt in the meantime. Only one telnet
    // client is ever kept; a new one always bumps the previous one off.
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
            LOG::dbgPrintf ("%-10s: M%u (%s)%s\n", "Mode", g_mode, MODE::modeName(g_mode),
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
            if (q == "Q") {                     // Session quit — telnet-only, never reaches handleCmd()
                LOG::telnetWrite("──────── session closed ────────\n");
                g_telnetCli.stop();             // Free the single client slot now
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

/* ============================================================================
   SETUP / LOOP
   ============================================================================ */
void setup() {
    Serial.begin(115200);
    delay(100);
    LOG::logMsg("BOOT", "fuZz D1 Mini starting...");

    pinMode(LED_PIN,   OUTPUT); digitalWrite(LED_PIN, HIGH);     // LED off; WIFI::tickLed() blinks while disconnected
    pinMode(MODE_PIN,  INPUT);                                    // Hi-Z default

    g_strip.begin();
    g_strip.show();                                               // all pixels off at boot

    WIFI::connectWiFi();
    WIFI::setupOTA();

    PROTO::udpOpen();

    // ── Buzzer baseline calibration (boot-time silence assumed) ──
    long sum = 0;
    for (int i = 0; i < BUZZER_CAL_SAMPLES; i++) { sum += analogRead(BUZZER_PIN); delay(2); }
    g_buzBaseline = sum / BUZZER_CAL_SAMPLES;
    LOG::logMsg("BUZZ", "baseline: %d", g_buzBaseline);

    EEPROM.begin(EE_USAGE_SIZE);
    USAGE::loadUsageFromEeprom();

    unsigned long now = millis();
    g_tWifi = g_tLed = g_tBuzzer = g_tUsage = now;
    g_usageLastTickMs = g_usageLastSaveMs = g_usageLastCheckMs = now;
    if (g_powered) g_powerOnMs = now;          // Boot assumes already running — start the OOW window now

    LOG::logMsg("BOOT", "done - M0-M%u / strip x%u", MODE_MAX, STRIP_LED_COUNT);
    LOG::logMsg("DIF", "powered=%s mode=M%u (%s) strip=%s",
              g_powered ? "true" : "false",
              g_mode, MODE::modeName(g_mode),
              g_stripMode == STRIP_OFF ? "off" : "on");
}

void loop() {
    unsigned long now = millis();

    ArduinoOTA.handle();
    PROTO::tickUDP();

    if (now - g_tWifi   >= WIFI_CHECK_MS)  { g_tWifi   = now; WIFI::tickWifi();   }
    if (now - g_tLed    >= LED_BLINK_MS)   { g_tLed    = now; WIFI::tickLed();    }
    if (now - g_tBuzzer >= BUZZER_SAMPLE_MS){ g_tBuzzer = now; BUZZ::tickBuzzer(); }
    if (now - g_tUsage  >= USAGE_TICK_MS)  { g_tUsage  = now; USAGE::tickUsageAccum(); }

    MODE::tickPinRelease();
    WIFI::tickWifiConnecting();
    WIFI::tickWifiSequence();
    MODE::tickModeTimeout();
    PARFUM::tickParfum();
    TELNET::tickTelnet();

    STRIP::tickStripWifiCue();
    STRIP::tickStripEffect();
    STRIP::tickStripOowChase();
    STRIP::tickStripParfumBreathe();
    STRIP::tickStripFade();

    if (Serial.available()) TELNET::handleCmd(Serial.readStringUntil('\n'));
}
