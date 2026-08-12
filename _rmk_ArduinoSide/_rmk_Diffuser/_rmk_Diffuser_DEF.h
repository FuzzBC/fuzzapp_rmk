// _rmk_Diffuser_DEF.h
// Compile-time configuration for the _rmk_ (clean-room rewrite) Diffuser
// bridge. Ported from _ArduinoSide/_FuZzAPP_Diffuser/_FuZzAPP_Diffuser.h -
// every timing/threshold constant is carried over UNCHANGED (they encode
// real tuned/tested behaviour - see _rmk_docs/02_REGRESSION.md B.3/B.4) even
// though the wire protocol and file layout are new. Two comment fixes vs the
// original (see _rmk_docs/00_PLAN.md SS9 R3/R9): the strip is 10 LEDs, not 6
// as the original header claimed, and the Parfum "always coerces to 10 SEC"
// claim is removed - the real, current, unchanged code passes the requested
// mode straight through. Both were documentation bugs, not behaviour changes.
// ============================================================
#pragma once

// -- Credentials --------------------------------------------
// Shared, gitignored _rmk_Shared/WiFiCredentials.h - a copy of the frozen
// project's own credentials file, made when _rmk_~FuZz_APPv8.0 became a
// standalone project directory (see 00_PLAN.md) so this project doesn't
// need to reach back into the frozen one. OTA hostname suffixed "-rmk" so
// it's distinguishable from the production Diffuser if both are ever on
// the same LAN.
#include <WiFiCredentials.h>
#define WIFI_SSID           WIFI_SSID_VALUE
#define WIFI_PASS           WIFI_PASS_VALUE
#define OTA_HOSTNAME        "FuZz-Diffuser-rmk"
#define OTA_PASSWORD        OTA_PASSWORD_VALUE

// -- Pins ------------------------------------------------------
#define LED_PIN             LED_BUILTIN   // Active-LOW built-in LED
#define BUZZER_PIN          A0            // Buzzer sense via 4.7 kOhm series
#define MODE_PIN            D6            // Virtual MODE/POWER button (active-LOW, Hi-Z default)
#define STRIP_PIN           D5            // WS2812 data in

// -- Network -----------------------------------------------------
#define TELNET_PORT         23
#define UDP_PORT            8439          // Link A - see _rmk_docs/01_PROTOCOL.md
#define UDP_BUF_SIZE        256           // Max inbound packet (bytes) - matches Proto::FRAME_MAX_BYTES headroom

// -- Diffuser button timing ------------------------------------
#define BTN_SHORT_MS        100UL         // Normal contact duration
#define BTN_LONG_MS         2000UL        // Shutdown long-press duration

// -- Diffuser state limits -------------------------------------
#define MODE_MAX            4             // M0=off, M1-M4=running

// -- RGB strip (WS2812 x10) -------------------------------------
#define STRIP_LED_COUNT      10             // Physical pixel count (see ring layout below)
#define STRIP_TICK_MS        15UL          // Fade-toward-target tick period (smoothing)
#define STRIP_FADE_STEP      8             // Per-channel step size per tick
#define STRIP_EFFECT_TICK_MS 17UL          // Animated-effect internal frame period (hue/phase step);
                                            // also the boot-time default for the live speed field
#define STRIP_EFFECT_SPEED_MIN_MS 5        // Floor clamp for the live effect-speed field
#define STRIP_EFFECT_COUNT   8             // Animated effects 1-8 (0=static). Chase is reserved
                                            // internally for the out-of-water alert only.

// -- Physical LED layout - ring, not a closed loop ----------------
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

// -- Mode-change confirmation timeout -----------------------
#define MODE_CONFIRM_MS     3000UL        // Max wait for buzzer after MODE press

// -- Out-of-water detection ------------------------------------
// A 2-strike behavioural heuristic (not a distinct buzzer tone - the buzzer
// front-end has no frequency/tone discrimination, only amplitude-envelope
// burst counting). See _rmk_docs/00_PLAN.md SS9 R4: unverified against real
// hardware whether the physical diffuser actually emits something more
// specific for this condition - kept exactly as the production firmware's
// (tuned from 4 live trials) pending that verification.
#define WATER_OUT_TIMEOUT_MS      10000UL // ms after power-on to confirm not out of water
#define STATUS_MODE_OUT_OF_WATER  5        // Synthetic mode value for status replies

// -- Parfum mode -------------------------------------------------
// Timed "insist" run. MMMM = minutes 0001-0168 hex, E = requested mode
// (M1 CONT or M2 10 SEC). Unlike the original header's claim, E is NOT
// coerced - the diffuser runs in the mode actually requested. IGNORES any
// stop/reconfigure command until the timer expires or an explicit cancel
// arrives (queued instead, applied on natural expiry - see Parfum.cpp).
#define PARFUM_MAX_MIN        360         // Max duration, minutes (6h)
#define PARFUM_DIF_MODE       1           // Requested-mode default for console "P<min>" (no E digit)
#define PARFUM_BREATHE_STEP_TICKS 6       // Frames (STRIP_EFFECT_TICK_MS each) per fill/drain LED step
#define PARFUM_EFFECT         2           // Fallback strip effect (PULSE) - seed colour only, see Parfum.cpp
#define PARFUM_BRIGHTNESS     255
#define PARFUM_COLOR_R        0xB4        // Fallback cue colour - violet
#define PARFUM_COLOR_G        0x00
#define PARFUM_COLOR_B        0xFF

// -- WiFi connect/reconnect self-test sequence ------------------
#define WIFI_SEQ_STEP_MS         300UL
#define WIFI_SEQ_MODE_GAP_MS     300UL
#define WIFI_SEQ_MODE_MAX_ATTEMPTS (MODE_MAX + 2)
#define WIFI_BOOT_CONNECT_TIMEOUT_MS 30000UL

// -- Usage tracking / refill estimation --------------------------
#define REFILL_HISTORY_SIZE        10
#define USAGE_TICK_MS              1000UL
#define USAGE_EEPROM_CHECKPOINT_MS 300000UL
#define USAGE_MIN_CYCLE_SEC        300UL

// EEPROM layout (flash-emulated) - schema version added vs the original
// (00_PLAN.md SS4.3: closes the "no version/magic field" gap). A future
// layout change bumps EE_SCHEMA_VERSION and adds a migration in Usage.cpp.
#define EE_SCHEMA_VERSION       1
#define EE_ADDR_SCHEMA_VER      0               // uint8_t
#define EE_ADDR_USAGE_ACCUM     1               // uint32_t
#define EE_ADDR_REFILL_COUNT    5               // uint8_t
#define EE_ADDR_REFILL_NEXT     6               // uint8_t
#define EE_ADDR_REFILL_HISTORY  7                // uint32_t[REFILL_HISTORY_SIZE]
#define EE_ADDR_TOTAL_REFILLS   (EE_ADDR_REFILL_HISTORY + REFILL_HISTORY_SIZE * 4)
#define EE_ADDR_CRC16           (EE_ADDR_TOTAL_REFILLS + 4)  // uint16_t over bytes [0, EE_ADDR_CRC16)
#define EE_USAGE_SIZE           (EE_ADDR_CRC16 + 2)

// -- Buzzer A0 detection ------------------------------------------
#define BUZZER_SAMPLE_MS    5UL
#define BUZZER_CAL_SAMPLES  50
#define BUZZER_ENV_DECAY    0.90f
#define BUZZER_ON_THR       40
#define BUZZER_OFF_THR      20
#define BUZZER_BURST_GAP_MS 400UL
#define BUZZER_QUIET_CONF   4

// -- Loop tick intervals -------------------------------------------
#define WIFI_CHECK_MS       10000UL
#define LED_BLINK_MS        500UL

// -- Feature switches -----------------------------------------------
// #define DIF_DEBUG_VERBOSE
// #define DIF_DEBUG_BUZZER_RAW
// #define DIF_TEST_MODE

#if defined(DIF_TEST_MODE) && !defined(DIF_DEBUG_VERBOSE)
    #define DIF_DEBUG_VERBOSE
#endif

#ifdef DIF_TEST_MODE
    #define PARFUM_UNIT_MS          1000UL
    #undef  WATER_OUT_TIMEOUT_MS
    #define WATER_OUT_TIMEOUT_MS    2000UL
    #undef  USAGE_MIN_CYCLE_SEC
    #define USAGE_MIN_CYCLE_SEC     10UL
#else
    #define PARFUM_UNIT_MS          60000UL
#endif

// -- String tables (ROM) -------------------------------------------
static const char* const MODE_NAMES[MODE_MAX] = {
    "CONT", "10 SEC", "2H AFTER SLEEP", "4H AFTER SLEEP"
};
static const float MODE_DUTY_CYCLE[MODE_MAX] = {
    1.0f,   // M1 CONT
    0.5f,   // M2 10 SEC (10s on / 10s off)
    1.0f,   // M3 2H AFTER SLEEP
    1.0f    // M4 4H AFTER SLEEP
};
static const char* const EFFECT_NAMES[STRIP_EFFECT_COUNT + 1] = {
    "STATIC", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};
