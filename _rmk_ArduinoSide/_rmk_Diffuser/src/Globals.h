#pragma once
// Globals.h - extern declarations for every module-level state variable,
// mirroring the SmartTV DEF.h convention (00_PLAN.md SS2.2/SS2.3: real
// module boundaries via file/extern discipline, not just namespace
// cosmetics). All defined once in Globals.cpp. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's GLOBALS section - same names, same meaning.
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include "Types.h"
// Angle-bracket include on purpose - resolved via a --library search path
// added at compile time, same pattern this repo already uses for the shared
// WiFiCredentials.h (see AGENTS.md). This is what makes arduino-cli compile
// _rmk_Shared's .cpp files (protocol_opcodes.cpp, Frame.cpp) as part of the
// build too, not just find the headers - a quoted relative include would
// only satisfy the preprocessor and leave every Proto:: symbol undefined at
// link time.
#include <protocol_opcodes.h>
// Must come before the #ifdef DIF_DEBUG_VERBOSE below - see Test.h's
// comment on this exact bug class (a header can't check a macro it hasn't
// included the definition of yet - every .cpp that includes only Globals.h,
// not the DEF.h directly, depends on this for vlogMsg() to be consistent).
#include "../_rmk_Diffuser_DEF.h"

// -- Telnet ------------------------------------------------------
extern WiFiServer  g_telnetSrv;
extern WiFiClient  g_telnetCli;
extern String      g_telnetBuf;

// -- UDP (Link A) --------------------------------------------------
extern WiFiUDP    g_udp;
extern IPAddress  g_udpSenderIP;
extern uint8_t    g_udpRxBuf[];

// -- Loop task timestamps -------------------------------------------
extern unsigned long g_tWifi;
extern unsigned long g_tLed;
extern unsigned long g_tBuzzer;
extern unsigned long g_tUsage;

// -- Virtual MODE button one-shot state --------------------------------
extern bool          g_modePinPending;
extern unsigned long g_modeReleaseMs;

// -- Buzzer detection --------------------------------------------------
extern int           g_buzBaseline;
extern float         g_buzEnv;
extern bool          g_buzToneActive;
extern unsigned long g_buzLastEndMs;
extern unsigned long g_buzTotalCount;
extern uint8_t       g_buzBurst;
extern uint8_t       g_buzQuiet;

// -- Diffuser virtual mirror -----------------------------------------
extern bool    g_powered;
extern uint8_t g_mode;

// -- Out-of-water detection ---------------------------------------------
extern unsigned long g_powerOnMs;
extern bool          g_outOfWater;

// -- Usage tracking / refill estimation ------------------------------
extern uint32_t      g_usageAccumSec;
extern float         g_usageFracSec;
extern unsigned long g_usageLastTickMs;
extern unsigned long g_usageLastSaveMs;
extern unsigned long g_usageLastCheckMs;
extern uint32_t      g_usageLastSavedAccumSec;
extern uint32_t      g_refillHistory[];
extern uint8_t       g_refillHistoryCount;
extern uint8_t       g_refillHistoryNext;
extern uint32_t      g_totalRefillCount;

// -- Mode sequenced targeting ------------------------------------------
extern bool    g_modePending;
extern unsigned long g_modePendMs;
extern bool    g_modeTargetActive;
extern uint8_t g_modeTarget;
extern bool    g_modeTargetWraps;

// -- Unsolicited-shutdown auto-recovery --------------------------------
extern bool          g_shutdownCmdPending;
extern bool          g_shutdownRetried;
extern bool          g_haveLastDn;
extern uint8_t       g_lastDnMode;
extern DnPayload     g_lastDnPayload;

// -- Deferred UDP status replies ----------------------------------------
extern UdpStatusReplyMode g_udpStatusReplyMode;

// -- WiFi connect/reconnect self-test sequence ---------------------------
extern WifiSeqPhase  g_seqPhase;
extern unsigned long g_seqNextMs;
extern uint8_t       g_seqModeAttempts;

// -- Non-blocking WiFi connect/reconnect strip cue -----------------------
extern bool          g_wifiConnecting;
extern unsigned long g_wifiConnNextMs;
extern bool          g_coldBoot;

// -- Out-of-water "Dn while dry" strip alert ------------------------------
extern bool          g_oowAlertActive;

// -- Parfum mode ----------------------------------------------------------
extern bool          g_parfumActive;
extern unsigned long g_parfumEndMs;
extern uint16_t      g_parfumSetMin;
extern ParfumPendingKind g_parfumPendingKind;
extern uint8_t           g_parfumPendingMode;
extern DnPayload         g_parfumPendingPayload;
extern bool              g_parfumHavePrevDn;
extern uint8_t           g_parfumPrevMode;
extern DnPayload         g_parfumPrevPayload;

// -- RGB strip (WS2812 x10) mirror ----------------------------------------
extern Adafruit_NeoPixel g_strip;
extern StripColor  g_stripCur[];
extern StripColor  g_stripTgt[];
extern StripMode   g_stripMode;
extern uint8_t     g_animEffect;
extern uint8_t     g_animPhase;
extern StripColor  g_effectBase1;
extern StripColor  g_effectBase2;
extern bool        g_effectDual;
extern uint8_t     g_stripBrightness;
extern uint8_t     g_effectTickMs;
extern uint8_t     g_parfumBreatheLevel;
extern int8_t      g_parfumBreatheDir;
extern StripColor  g_parfumBreatheColor;
extern const uint8_t PARFUM_BREATHE_ORDER[];

// -- Protocol (Link A) ack state - set by ProtoLink::dispatch() -------------
// g_ackResult is Proto::AckResult (see protocol_opcodes.h) - replaces the
// old local ACK_OK/ACK_CLAMPED/... defines so this stays in lockstep with
// the single opcode-table source of truth. enum class, not a plain uint8_t,
// so a stray assignment of an unrelated int can't compile silently.
extern uint8_t g_ackSeq;
extern bool    g_ackValid;
extern Proto::AckResult g_ackResult;

#ifdef DIF_DEBUG_VERBOSE
    #define vlogMsg(...)  LOG::logMsg(__VA_ARGS__)
#else
    #define vlogMsg(...)  do { } while (0)
#endif
