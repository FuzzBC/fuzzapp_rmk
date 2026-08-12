#include "Globals.h"
#include "../_rmk_Diffuser_DEF.h"

WiFiServer  g_telnetSrv(TELNET_PORT);
WiFiClient  g_telnetCli;
String      g_telnetBuf;

WiFiUDP    g_udp;
IPAddress  g_udpSenderIP(0, 0, 0, 0);
uint8_t    g_udpRxBuf[UDP_BUF_SIZE];

unsigned long g_tWifi   = 0;
unsigned long g_tLed    = 0;
unsigned long g_tBuzzer = 0;
unsigned long g_tUsage  = 0;

bool          g_modePinPending  = false;
unsigned long g_modeReleaseMs   = 0;

int           g_buzBaseline    = 512;
float         g_buzEnv         = 0.0f;
bool          g_buzToneActive  = false;
unsigned long g_buzLastEndMs   = 0;
unsigned long g_buzTotalCount  = 0;
uint8_t       g_buzBurst       = 0;
uint8_t       g_buzQuiet       = 0;

bool    g_powered      = true;
uint8_t g_mode         = 1;

unsigned long g_powerOnMs   = 0;
bool          g_outOfWater  = false;

uint32_t      g_usageAccumSec     = 0;
float         g_usageFracSec      = 0.0f;
unsigned long g_usageLastTickMs   = 0;
unsigned long g_usageLastSaveMs   = 0;
unsigned long g_usageLastCheckMs  = 0;
uint32_t      g_usageLastSavedAccumSec = 0;
uint32_t      g_refillHistory[REFILL_HISTORY_SIZE] = {0};
uint8_t       g_refillHistoryCount = 0;
uint8_t       g_refillHistoryNext  = 0;
uint32_t      g_totalRefillCount   = 0;

bool    g_modePending      = false;
unsigned long g_modePendMs = 0;
bool    g_modeTargetActive = false;
uint8_t g_modeTarget       = 1;
bool    g_modeTargetWraps  = false;

bool          g_shutdownCmdPending = false;
bool          g_shutdownRetried    = false;
bool          g_haveLastDn         = false;
uint8_t       g_lastDnMode         = 1;
DnPayload     g_lastDnPayload      = {};

UdpStatusReplyMode g_udpStatusReplyMode = UDP_STATUS_NONE;

WifiSeqPhase  g_seqPhase        = SEQ_NONE;
unsigned long g_seqNextMs       = 0;
uint8_t       g_seqModeAttempts = 0;

bool          g_wifiConnecting   = false;
unsigned long g_wifiConnNextMs   = 0;
bool          g_coldBoot         = true;

bool          g_oowAlertActive   = false;

bool          g_parfumActive     = false;
unsigned long g_parfumEndMs      = 0;
uint16_t      g_parfumSetMin     = 0;
ParfumPendingKind g_parfumPendingKind    = PARFUM_PEND_NONE;
uint8_t           g_parfumPendingMode    = 1;
DnPayload         g_parfumPendingPayload = {};
bool              g_parfumHavePrevDn     = false;
uint8_t           g_parfumPrevMode       = 1;
DnPayload         g_parfumPrevPayload    = {};

Adafruit_NeoPixel g_strip(STRIP_LED_COUNT, STRIP_PIN, NEO_GRB + NEO_KHZ800);
StripColor  g_stripCur[STRIP_LED_COUNT] = {};
StripColor  g_stripTgt[STRIP_LED_COUNT] = {};
StripMode   g_stripMode  = STRIP_OFF;
uint8_t     g_animEffect = 0;
uint8_t     g_animPhase  = 0;
StripColor  g_effectBase1 = { 255, 255, 255 };
StripColor  g_effectBase2 = { 255, 255, 255 };
bool        g_effectDual  = false;
uint8_t     g_stripBrightness = 255;
uint8_t     g_effectTickMs    = STRIP_EFFECT_TICK_MS;
uint8_t     g_parfumBreatheLevel = 0;
int8_t      g_parfumBreatheDir   = 1;
StripColor  g_parfumBreatheColor = {};
const uint8_t PARFUM_BREATHE_ORDER[STRIP_LED_COUNT] = { 4, 5, 3, 6, 2, 7, 1, 8, 0, 9 };

uint8_t g_ackSeq    = 0;
bool    g_ackValid  = false;
Proto::AckResult g_ackResult = Proto::AckResult::OK;
