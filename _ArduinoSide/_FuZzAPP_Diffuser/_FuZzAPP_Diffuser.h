// _FuZzAPP_Diffuser.h
// Compile-time configuration — edit here only, never in .ino
// ============================================================
#pragma once

// ── Credentials ─────────────────────────────────────────────
// WIFI_SSID / WIFI_PASS / OTA_PASSWORD are pulled from the shared,
// gitignored _ArduinoSide/_Shared/WiFiCredentials.h (never committed) so
// both this sketch and _FuZzAPP_SmartTV_R4 read the WiFi password from one
// place. Copy WiFiCredentials.h.example there and fill in real values -
// see AGENTS.md. Angle-bracket include on purpose: a quoted relative path
// ("../_Shared/...") does not reliably resolve under arduino-cli, so
// FlashConsole.pyw's compiler.py instead adds _Shared as a --library
// search path at compile time.
#include <WiFiCredentials.h>
#define WIFI_SSID           WIFI_SSID_VALUE
#define WIFI_PASS           WIFI_PASS_VALUE
#define OTA_HOSTNAME        "FuZz-Diffuser"
#define OTA_PASSWORD        OTA_PASSWORD_VALUE

// ── Pins ────────────────────────────────────────────────────
#define LED_PIN             LED_BUILTIN   // Active-LOW built-in LED
#define BUZZER_PIN          A0            // Buzzer sense via 4.7 kΩ series
#define MODE_PIN            D6            // Virtual MODE/POWER button (active-LOW, Hi-Z default)
#define STRIP_PIN           D5            // WS2812 data in — reuses freed LIGHT_PIN line

// ── Network ─────────────────────────────────────────────────
#define TELNET_PORT         23
#define UDP_PORT            8439
#define UDP_BUF_SIZE        256           // Max inbound packet (bytes)

// ── UDP command ACK envelope ────────────────────────────────
// SmartTV may wrap any command as "#SS<cmd>" (SS = 2 hex seq); we dispatch the
// inner command and reply "#SSR" (R = 1 hex result). Bare commands still work
// (no "#SSR" is sent). Codes mirror the SmartTV APP_ACK_* set so a result can
// be relayed straight to the phone. See udpExec()/udpAck() in the .ino.
#define ACK_CHAR            '#'
#define ACK_HDR             3             // header stripped: '#' + 2 hex seq
#define ACK_OK              0             // applied as requested
#define ACK_CLAMPED         1             // applied, but a value was coerced
#define ACK_REJECTED        2             // malformed / unparseable command
#define ACK_LOCKED          4             // ignored/queued: a parfum window is active
// 5 (SmartTV APP_ACK_NOWATER) intentionally has no local equivalent here — the
// diffuser never emits it itself: out-of-water is asynchronous (detected mid-
// operation from buzzer timing, not while handling a command) and is reported
// continuously via the Ds/Dc status reply's mode field instead (see
// STATUS_MODE_OUT_OF_WATER below). SmartTV synthesizes APP_ACK_NOWATER=5 on
// its own relay layer when the diffuser doesn't respond at all. Left as a gap
// (not renumbered) so this table stays numerically mirrored to SmartTV's set.
#define ACK_UNSUPPORTED     6             // unknown command

// ── Diffuser button timing ────────────────────────────────
#define BTN_SHORT_MS        100UL         // Normal contact duration
#define BTN_LONG_MS         2000UL        // Shutdown long-press duration -- reduced from 3000ms; validated
                                          // across repeated live trials with no misdetection. Note: this
                                          // does NOT measurably speed up shutdown (the diffuser's own beep
                                          // confirmation always completes well before either hold time
                                          // elapses, so it isn't on the critical path) -- kept lower purely
                                          // as tested-safe headroom, not for a timing win.

// ── Diffuser state limits ────────────────────────────────
#define MODE_MAX            4             // M0=off, M1-M4=running

// ── RGB strip (WS2812 x6) ─────────────────────────────────
// Replaces the old built-in-light button emulation entirely — the strip
// is driven directly in firmware instead of pressing a physical LIGHT button.
#define STRIP_LED_COUNT      10             // Physical pixel count
#define STRIP_TICK_MS        15UL          // Fade-toward-target tick period (smoothing)
#define STRIP_FADE_STEP      8             // Per-channel step size per tick — ↓ = slower/smoother
#define STRIP_EFFECT_TICK_MS 17UL          // Animated-effect internal frame period (hue/phase step);
                                            // also the boot-time default for the live Dn "SP" speed
#define STRIP_EFFECT_SPEED_MIN_MS 5        // Floor clamp for the live Dn "SP" frame-period field
#define STRIP_EFFECT_COUNT   8             // Animated effects, EE 1-8 (EE=0 is always static).
                                            // Chase is reserved internally for the out-of-water
                                            // alert only and is not part of this selectable set.

// ── Physical LED layout — ring, not a closed loop ────────────
// The 10 LEDs sit in a circle but the strip data line has two open ends,
// both at the BACK, one step either side of the rear seam. Index order
// sweeps from back-right, around the FRONT, to back-left:
//
//                     FRONT
//                4           5
//            3                   6
//       R   2                     7   L
//            1                   8
//                0           9
//                     BACK (seam)
//
//   LED0 = mid-right-behind  LED9 = mid-left-behind
//   LED4/LED5 = front-centre pair (furthest from the seam)

// ── Mode-change confirmation timeout ─────────────────────
// Failure-mode ceiling only -- never gates a successful confirmation, so it
// cannot be "sped up". Deliberately left at 3000ms: live shutdown trials
// measured confirm times up to 2410ms, so a lower value risks misreading a
// legitimate-but-slow confirmation as a timeout (a real regression, not a
// safe trim).
#define MODE_CONFIRM_MS     3000UL        // Max wait for buzzer after MODE press

// ── Out-of-water detection ────────────────────────────────
// An unsolicited shutdown (multi-beep with no Df/M0 sent by us) is given one
// auto-restart with the last Dn settings. Only if it also drops within this
// many ms of that restart is it diagnosed as out of water. Reported in Ds
// status as mode=5. See buzzerFinalizeBurst() / applyModeAdvance().
#define WATER_OUT_TIMEOUT_MS      10000UL // ms after power-on to confirm not out of water
#define STATUS_MODE_OUT_OF_WATER  5        // Synthetic mode value for Ds reply

// ── Parfum mode ───────────────────────────────────────────
// Timed "insist" run started via UDP "DpMMMME" (MMMM = hex minutes 0001-0168,
// E = requested M1/M2): the diffuser always physically runs PARFUM_RUN_MODE
// (10 SEC) for that many minutes — CONT (E=1) is coerced too, see
// parfumStart() — and IGNORES any UDP stop (Df) or re-configure (Dn) until
// the timer expires or a "Dp0000" cancel arrives. The strip runs a
// random-colour breathe cue (see
// stripParfumBreatheFrame()) so the mode is visible at a glance. An
// unsolicited shutdown mid-parfum is auto-restarted; a confirmed
// out-of-water diagnosis cancels parfum.
#define PARFUM_MAX_MIN        360         // Max duration, minutes (6h)
#define PARFUM_DIF_MODE       1           // Requested-mode default for console "P<min>" (no E digit) —
                                           // logging/interface value only; parfumStart() always coerces
                                           // the actual physical run mode to PARFUM_RUN_MODE below.
#define PARFUM_BREATHE_STEP_TICKS 6       // Frames (STRIP_EFFECT_TICK_MS each) per fill/drain LED
                                           // step — ↓ = faster breathe cycle
// PARFUM_EFFECT/COLOR_* below seed the initial Dn payload (latched as lastDn)
// and are what an unsolicited-shutdown auto-restart mid-window falls back to
// — see cmdDiffuserOn()'s isWaterRetry path — since that retry re-sends
// g_lastDnPayload directly and never calls startParfumBreathe(). The live
// cue seen during a normal parfum run is the breathe effect, not this PULSE.
#define PARFUM_EFFECT         2           // Fallback strip effect (PULSE)
#define PARFUM_BRIGHTNESS     255         // Strip colour-scale factor (Dn "BR" semantics)
#define PARFUM_COLOR_R        0xB4        // Fallback cue colour — violet
#define PARFUM_COLOR_G        0x00
#define PARFUM_COLOR_B        0xFF

// ── WiFi connect/reconnect self-test sequence ────────────
// While (re)connecting: the strip breathes blue every WIFI_SEQ_STEP_MS
// as a "still trying" indicator. Once connected: MODE is short-pressed one
// step at a time, waiting for the diffuser's real buzzer confirmation
// between presses, until a shutdown (>1 beep) burst is detected — which
// re-calibrates the firmware's mirrored state to known ground truth.
#define WIFI_SEQ_STEP_MS         300UL    // Strip breathing-cue interval while connecting
#define WIFI_SEQ_MODE_GAP_MS     300UL    // Extra pacing after each confirmed MODE step
#define WIFI_SEQ_MODE_MAX_ATTEMPTS (MODE_MAX + 2)  // Safety cap if no shutdown beep is heard

// WIFI::connectWiFi() (setup()-time only) blocks the boot sequence while it
// waits for the first connect. A wrong/out-of-range WIFI_SSID would other-
// wise hang setup() forever with no OTA/UDP/telnet ever coming up, and no
// way back short of a physical re-flash. After this long it gives up
// blocking and lets setup() continue; WIFI::tickWifi() (see WIFI_CHECK_MS)
// then keeps retrying non-blocking from loop() forever afterward, so the
// device still self-heals once the AP is reachable again.
#define WIFI_BOOT_CONNECT_TIMEOUT_MS 30000UL

// ── Out-of-water strip alert ──────────────────────────────
// As soon as out-of-water is diagnosed, the strip runs the blue Chase
// effect (see EFFECT_NAMES) as a continuous "no water" cue until a
// mode-off (Df/M0) stop command is received via UDP, at which point
// the strip is forced off.

// ── Usage tracking / refill estimation ────────────────────
// Tracks how long the diffuser actually runs between refills, weighted by
// each mode's physical duty cycle (see MODE_DUTY_CYCLE below), so a rolling
// average of the last REFILL_HISTORY_SIZE completed cycles can estimate
// "time until empty". Persisted in EEPROM (flash-emulated on ESP8266) so it
// survives reboot/power loss. Finalized in finalizeRefillCycle(), called
// from the single existing out-of-water diagnosis point in
// buzzerFinalizeBurst(); accumulated in tickUsageAccum(), called from loop().
#define REFILL_HISTORY_SIZE        10          // Rolling window - last N completed cycles
#define USAGE_TICK_MS              1000UL      // Accumulator update period
#define USAGE_EEPROM_CHECKPOINT_MS 300000UL    // Periodic EEPROM save of the live accumulator (5 min) -
                                                // bounds data loss on power-cut without wearing flash every tick
#define USAGE_MIN_CYCLE_SEC        300UL       // Minimum accumulated runtime (5 min) for a completed cycle to
                                                // count as a real refill - guards against a failed/aborted
                                                // restart attempt (still-dry retry) polluting the rolling
                                                // average with a near-zero entry (see finalizeRefillCycle()).

// EEPROM layout (flash-emulated - kept small & write-throttled, see above)
#define EE_ADDR_USAGE_ACCUM     0               // uint32_t - accumulated effective seconds since last refill
#define EE_ADDR_REFILL_COUNT    4               // uint8_t  - valid entries in history, 0-REFILL_HISTORY_SIZE
#define EE_ADDR_REFILL_NEXT     5               // uint8_t  - ring-buffer next-write index
#define EE_ADDR_REFILL_HISTORY  6               // uint32_t[REFILL_HISTORY_SIZE] - past cycles, effective seconds
#define EE_ADDR_TOTAL_REFILLS   (EE_ADDR_REFILL_HISTORY + REFILL_HISTORY_SIZE * 4)  // uint32_t - lifetime count
#define EE_USAGE_SIZE           (EE_ADDR_TOTAL_REFILLS + 4)     // EEPROM.begin() size for this block

// ── Buzzer A0 detection ──────────────────────────────────
#define BUZZER_SAMPLE_MS    5UL           // A0 sample interval
#define BUZZER_CAL_SAMPLES  50            // Boot-time calibration sample count
#define BUZZER_ENV_DECAY    0.90f         // Peak-hold decay per sample (↑ = slower)
#define BUZZER_ON_THR       40            // Envelope level → tone ACTIVE
#define BUZZER_OFF_THR      20            // Envelope level → tone ENDED  (must be < ON)
#define BUZZER_BURST_GAP_MS 400UL         // Post-burst silence that closes a burst — measured real
                                          // inter-beep gap on a 2-beep shutdown burst is ~90-170ms
                                          // (4 live trials); 400ms keeps ~2.3x safety margin above the
                                          // worst observed gap while cutting ~200ms off every confirmed
                                          // press/shutdown vs the old 600ms
#define BUZZER_QUIET_CONF   4             // Consecutive quiet RAW samples to confirm beep end

// ── Loop tick intervals ──────────────────────────────────
#define WIFI_CHECK_MS       10000UL       // WiFi reconnect poll period
#define LED_BLINK_MS        500UL         // LED toggle period (when connected)

// ── Feature switches ─────────────────────────────────────
// Log verbosity — two-tier scheme, same naming as the SmartTV file: LITE is
// every logMsg() call and is always on (boot/network events, commands that
// changed something, errors and warnings). VERBOSE is every vlogMsg() call,
// gated by this switch — the repeating, per-poll and per-frame detail (raw
// UDP packets, Ds status replies, strip effect changes, buzzer bursts, WiFi
// self-test steps, OTA progress %) — see the log table in the changelog.
// Console REPLIES (S / D / ? / connect banner) are never affected either way.
// #define DIF_DEBUG_VERBOSE              // Uncomment for verbose logging

// Buzzer raw envelope trace (threshold tuning) — kept on its OWN switch, not
// folded into DIF_DEBUG_VERBOSE: this alone prints one line per A0 sample
// (BUZZER_SAMPLE_MS = 5ms), 100+ lines/sec while a tone is active, which
// would flood everything else VERBOSE prints (confirmed live: it buried the
// self-test's step-by-step output under ~130KB of buzzer noise the first
// time this ran folded in). Turn on only when actively tuning
// BUZZER_ON_THR/BUZZER_OFF_THR - never together with a "T" self-test run.
// #define DIF_DEBUG_BUZZER_RAW

// Test mode — adds a "T" console/telnet command (self-test: sweeps every
// command except WiFi/OTA against the real diffuser, printing each step) and
// keeps usage/refill-history EEPROM RAM-only for as long as this is defined,
// so repeated test runs never wear the flash or overwrite real accumulated
// data. Automatically pulls in DIF_DEBUG_VERBOSE too (cascade below), so a
// test run always prints the full per-step trace, not just LITE lines.
// The mode sweep (M1-M4, M0, and the UDP "Dn"/"Df" equivalents) physically
// pulses MODE_PIN and waits on a real buzzer confirmation from the diffuser —
// expect "TIMED OUT" lines if nothing is wired up yet, that's normal.
// #define DIF_TEST_MODE                  // Uncomment for the "T" self-test + EEPROM-safe testing

#if defined(DIF_TEST_MODE) && !defined(DIF_DEBUG_VERBOSE)
    #define DIF_DEBUG_VERBOSE
#endif

// ── String tables (ROM) ─────────────────────────────────
// Indexed 1-4 (MODE_MAX entries)
static const char* const MODE_NAMES[MODE_MAX] = {
    "CONT", "10 SEC", "2H AFTER SLEEP", "4H AFTER SLEEP"
};
// Effective duty-cycle multiplier per mode, indexed 1-MODE_MAX (see MODE_NAMES,
// dutyCycleForMode()). M1 CONT and M3/M4 AFTER-SLEEP run at full output the
// entire time they're powered on. M2 "10 SEC" physically pulses the diffuser
// 10s on/10s off inside the standalone hardware itself (opaque to this
// firmware - no PWM/timing logic exists here to measure it), so its
// wall-clock powered time is weighted down to the known ~50% actual duty.
static const float MODE_DUTY_CYCLE[MODE_MAX] = {
    1.0f,   // M1 CONT
    0.5f,   // M2 10 SEC (10s on / 10s off)
    1.0f,   // M3 2H AFTER SLEEP
    1.0f    // M4 4H AFTER SLEEP
};
// Indexed 0-STRIP_EFFECT_COUNT directly by EE value (0=static)
static const char* const EFFECT_NAMES[STRIP_EFFECT_COUNT + 1] = {
    "STATIC", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};
