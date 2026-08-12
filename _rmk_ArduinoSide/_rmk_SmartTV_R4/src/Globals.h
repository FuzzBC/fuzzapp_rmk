#pragma once
// Globals.h - state structs + extern declarations for every module-level
// variable, mirroring _FuZzAPP_SmartTV_R4_DEF.h's struct/extern section and
// the .ino's GLOBALS block. All defined once in Globals.cpp.
//
// Removed vs the original: TASKx/HBx (the shared step-state singleton) -
// replaced by SCHED::Scratch, one private slot per task (see Scheduler.h).
// Removed: taskId_t (TaskJockeyMod's type) - replaced by SCHED::TaskId.
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <BME280I2C.h>
#include <TimeLib.h>
#include "Scheduler.h"
#include "Led.h"   // CRGB/CHSV

/* --- GLOBAL MACROS --- */
#define NL              "\n"
#define FW_NAME          "FuZzAPP SmartTV R4 (rmk)"
#define SERIAL_BAUD     115200
#define TIMEFIELD(t, f) (t[f] < 10 ? "0" : ""), t[f]
#define TIMEVAL(v)      ((v) < 10 ? "0" : ""), (v)
#define S_TO_MS(x)      ((uint32_t)(x) * 1000UL)

#define BIT_SET(arr, bit)   ((arr)[(bit) / 8] |= (1 << ((bit) % 8)))
#define BIT_CLEAR(arr, bit) ((arr)[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BIT_TEST(arr, bit)  (((arr)[(bit) / 8] & (1 << ((bit) % 8))) != 0)
#define BIT_CLEAR_ALL(arr, size) memset((arr), 0, (size))
// memset(arr, true, size) is NOT equivalent to BIT_SET_ALL - true/1 as a
// fill byte only sets bit 0 of every byte (0x01), not all 8 bits. Confirmed
// live in production as the root cause of colour changes silently not
// persisting for ~7 of every 8 LEDs - see 02_REGRESSION.md.
#define BIT_SET_ALL(arr, size)   memset((arr), 0xFF, (size))

/* --- LED zone layout (see _rmk_docs/02_REGRESSION.md A.5 for the ASCII diagram) --- */
#define LED_PIN_FRONT   10
#define LED_PIN_BACK    11
#define LED_PIN_HB       7
#define LED_TV_NUM      30
#define LED_COM_NUM     10
#define LED_UCOM_NUM     2
#define LED_BED_NUM      8
#define LED_LAMP_NUM    10
#define LED_HB_NUM     178
#define LED_START_I_TV    0
#define LED_START_I_COM   (LED_START_I_TV   + LED_TV_NUM)
#define LED_START_I_UCOM  (LED_START_I_COM  + LED_COM_NUM)
#define LED_START_I_BED   (LED_START_I_UCOM + LED_UCOM_NUM)
#define LED_START_I_LAMP  (LED_START_I_BED  + LED_BED_NUM)
#define LED_START_I_HB    (LED_START_I_LAMP + LED_LAMP_NUM)
#define LED_HB_NUM_FAKE   1
#define LED_NUM           (LED_START_I_HB + LED_HB_NUM_FAKE)
#define LED_NUM_TOTAL     (LED_START_I_HB + LED_HB_NUM)
#define LED_BRIGHTNESS_MAX  120
#define HB_BRIGHTNESS_MAX   100
#define LED_REFRESH_TIME   1000
#define LED_FPS_OPTIONS_TOTAL   9
#define LED_FPS_DEFAULT_INDEX   5
extern const uint8_t LED_FPS_TABLE[LED_FPS_OPTIONS_TOTAL] PROGMEM;

/* --- LIGHT SENSOR --- */
#define LIGHT_SENSOR_PIN          A1
#define LIGHT_SENSOR_CHECK_TIME   2000
#define LIGHT_SENSOR_SAMPLES      200
#define LIGHT_SENSOR_LUX_FIX        5

/* --- TV --- */
#define TV_PIN              17
#define TV_ADC_DIVIDER      15
#define TV_TEST_PIN_ON       0
#define TV_TEST_PIN_OFF     30
#define TV_DEBOUNCE_TIME    70
#define TV_READ_STATUS_TIME 1000
#define TV_LOG_INDEX_MAX    30

/* --- MOTION --- */
#define MOTION_PIN_COM          4
#define MOTION_PIN_BED          8
#define MOTION_CHECK_TIME    2000
#define MOTION_TIME_FIX       300
#define MOTION_LOG_INDEX_MAX    5
#define MOTION_COLLISION_BLOOM_MAX_TICKS 150

/* --- NET --- */
#define NET_CHECK_TIME      30UL
#define NET_CONNECT_TIMEOUT 10UL
#define NET_RETRY_DELAY     30UL
#define NET_RTC_RETRY       10UL
#define NET_RTC_RESYNC   21600UL
#define NET_IP_TIMEOUT      10UL
#define NET_TIMEZONE             3
#define NET_CHECK_INTERVAL     500
#define _HH  0
#define _MI  1
#define _SS  2
#define _DD  3
#define _MM  4
#define _YY  5

/* --- UDPRAW --- */
#define UDPRAW_CHECK_TIME       5000
#define UDPRAW_LED_MOVIE_DELAY  6000

/* --- APP (Link B) --- */
#define APP_BRIGHT              LED_BRIGHTNESS_MAX
#define APP_UDP_PORT            8472
#define APP_UDP_MAX_BUFFER_SIZE 256
#define APP_UDP_TIMEOUT         100
#define APP_TRANSPORT_UDP        0
#define APP_TRANSPORT_MQTT       1
#define APP_ACK_DEDUP_MS      3000   /* re-ack (don't re-run) a repeated seq within this window */
#define KeepAlive_MS      10000
#define APP_ALIVE_TIMEOUT_MS  25000
#define APP_FAULT_WIFI        0x0001
#define APP_FAULT_NTP         0x0002
#define APP_FAULT_EEPROM      0x0004
#define APP_FAULT_BME         0x0008
#define APP_FAULT_DIF_NORESP  0x0010
#define APP_FAULT_DIF_NOWATER 0x0020
#define APP_WATCHDOG_MS       1500
#define ARD_TIMEOUT        240000
#define ARD_RAM_TOTAL       32768
#define RTC_EPOCH_SANE   1600000000L
#define APP_LINK_PUSH_MS      10000

/* --- DIF (Link A) --- */
#define DIF_UDP_PORT             8439
#define DIF_UDP_MAX_BUFFER_SIZE    64
#define DIF_UDP_TIMEOUT             100
#define DIF_MSG_MAX_LEN              48
#define DIF_MODE_MAX                4
#define DIF_EFFECT_COUNT            8
#define DIF_PARFUM_MAX_MIN        360
#define DIF_MODE_OUT_OF_WATER       5
#define DIF_MODE_NO_RESPONSE        6
#define DIF_STATUS_CHECK_S          5
#define DIF_IDLE_CHECK_S           10
#define DIF_LOG_INDEX_MAX          15
#define DIF_PENDING_ACK_MAX         4

/* Diffuser name-lookup tables (debug display only), PROGMEM. */
extern const char* const DIF_MODE_NAMES[DIF_MODE_MAX] PROGMEM;         // indexed 1-4 (DIF_MODE_MAX entries)
extern const char* const DIF_EFFECT_NAMES[DIF_EFFECT_COUNT + 1] PROGMEM; // indexed 0-DIF_EFFECT_COUNT directly by EE value
extern const char* const DIF_STRIP_STATUS_NAMES[11] PROGMEM;           // indexed 0-10 by stripStatusCode()

/* --- MQTT (HiveMQ Cloud) ---
   Two one-way topics (01_PROTOCOL.md SS5), not the original's single
   self-tagged duplex topic - no sender-tag byte needed, no "is this my own
   echo" check, raw v1 frame bytes on the wire (no base64). <deviceId> isn't
   pinned down in 01_PROTOCOL.md itself (flagged there as a real design
   choice, not exhaustively speced) - this rewrite uses the board's WiFi MAC
   (already unique per physical unit, needs no new EEPROM identity state);
   see NET::DeviceId(). */
#define MQTT_HOST       "cb6c04d1ec6d4bf7b31ec5533ff91102.s1.eu.hivemq.cloud"
#define MQTT_PORT       8883
#define MQTT_KEEPALIVE  30
#define MQTT_RETRY_MS   5000
#define MQTT_BACKOFF_STREAK    3       /* consecutive Reconnect() failures before backing off */
#define MQTT_BACKOFF_RETRY_MS  60000   /* retry interval once backed off - see Mqtt.cpp Reconnect() */
#define MQTT_TOPIC_BASE     "fuzz/"
#define MQTT_TOPIC_C2D_SUFFIX  "/b/c2d"
#define MQTT_TOPIC_D2C_SUFFIX  "/b/d2c"
#define MQTT_DEVICE_ID_MAX    17   /* "AABBCCDDEEFF" (12 hex chars) + NUL, rounded up */
#define MQTT_BUF_SIZE   APP_UDP_MAX_BUFFER_SIZE

/* --- EEPROM --- */
#define EE_START_READ_INDEX         5
#define EE_MEM_X                   50
#define EE_SAVE_TIME             60000UL
#define EE_SAVE_DELAY_BETWEEN_CHUNKS 1
#define EE_ADDR_SET      EE_START_READ_INDEX
#define EE_ADDR_COLOR    (EE_ADDR_SET     + EE_MEM_X)
#define EE_ADDR_AMBIENT  (EE_ADDR_COLOR   + (LED_NUM << 2))
#define EE_ADDR_UDP      (EE_ADDR_AMBIENT + (LED_NUM << 2))
#define EE_ADDR_MOTION   (EE_ADDR_UDP     + 4)
#define EE_ADDR_MAX      (EE_ADDR_MOTION  + 3)
// Schema version + per-region CRC16 added vs the original (00_PLAN.md SS4.2) -
// anchored right after the existing low region, before the two blocks that
// grow from the high end (self-test, MQTT creds), so nothing else shifts.
#define EE_SCHEMA_VERSION 1
#define EE_ADDR_SCHEMA_VER  EE_ADDR_MAX
#define EE_ADDR_CRC16       (EE_ADDR_SCHEMA_VER + 1)
#define EE_ADDR_LOW_END     (EE_ADDR_CRC16 + 2)

#define MQTTCRED_USER_MAX      32
#define MQTTCRED_PASS_MAX      32
#define MQTTCRED_MAGIC       0xA5
#define MQTTCRED_BLOCK_SIZE  (1 + (MQTTCRED_USER_MAX + 1) + (MQTTCRED_PASS_MAX + 1))
#define EE_TEST_MAGIC        0xE7
#define EE_TEST_BLOCK_SIZE   6

#define CheckTIME  10
#define TESTMODE_DURATION  120000

/* =========================================================================== */
/* ENUMS                                                                        */
/* =========================================================================== */
enum Status { motAUTOOFF, motOFF, motON, motCOM, motBED };
enum DIF_TriggerBy { difTrigTV, difTrigUDPRAW, difTrigMotion, difTrigAmbient, difTrigIdle };
enum NET_WiFiStatus { netWifiOK, netWifiLost, netWifiRetrying };
enum NET_RtcStatus  { rtcOK, rtcRETRY, rtcRETRYING };
typedef enum { EE_TYPE_ONOFF, EE_TYPE_NUMERIC } EE_SettingType;

// Term log severity - first hex digit of the app protocol's log envelope.
// ERR/WRN/INF/DBG are filterable in the app; SEC/GAP are layout-only records.
enum APP_LogLevel {
    APP_LOG_ERR = 0, APP_LOG_WRN = 1, APP_LOG_INF = 2,
    APP_LOG_DBG = 3, APP_LOG_SEC = 4, APP_LOG_GAP = 5
};
// Term log source - 2 hex digits of the envelope; the app renders the
// module-name tag from this id rather than a text prefix.
enum APP_LogSource {
    APP_SRC_SYS = 0x00, APP_SRC_NET = 0x01, APP_SRC_RTC = 0x02, APP_SRC_EE = 0x03,
    APP_SRC_APP = 0x04, APP_SRC_LED = 0x05, APP_SRC_TV = 0x06, APP_SRC_MOTION = 0x07,
    APP_SRC_LUX = 0x08, APP_SRC_DIF = 0x09, APP_SRC_UDPRAW = 0x0A, APP_SRC_AMBIENT = 0x0B,
    APP_SRC_BME = 0x0C, APP_SRC_TASK = 0x0D, APP_SRC_HB = 0x0E, APP_SRC_TEST = 0x0F
};
enum __testmode {
    _testmode_none, _testmode_tvOn, _testmode_tvOff, _testmode_udpraw,
    _testmode_motionCom, _testmode_motionBed, _testmode_dif, _testmode_lux,
};

enum EE_Settings {
    EE_TV_ON_EFF = 0, EE_TV_OFF_EFF = 1, EE_TV_OFF_TIME = 2, EE_TV_BR_TV = 17,
    EE_TV_BR_COM = 3, EE_TV_BR_UCOM = 4, EE_TV_BR_BED = 5, EE_TV_BR_LAMP = 6,
    EE_TV_RANDOM_COLOR_START = 15, EE_TV_ON_BR_CL_DEL = 23, EE_TV_ON_BR_CL_INC = 24,
    EE_TV_OFF_BR_CL_DEL = 25, EE_TV_OFF_BR_CL_INC = 26,
    EE_MOTION_ON_EFF = 20, EE_MOTION_BRIGHTNESS = 7, EE_MOTION_ON_TIME = 8,
    EE_MOTION_RANDOM_COLOR = 10, EE_MOTION_DIVIDE_BRIGHTNESS = 16, EE_MOTION_RENEW_COLOR_TIME = 18,
    EE_MOTION_AUTO_OFF_TIME = 19, EE_MOTION_BR_CL_DEL = 27, EE_MOTION_BR_CL_INC = 28,
    EE_HB_DUAL_COLOR = 31, EE_HB_EFFECT = 32, EE_HB_EFFECT_SPEED = 33, EE_TV_ON_HB_EFF = 34,
    EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX = 9, EE_UDPRAW_BR_CL_DEL = 29, EE_UDPRAW_BR_CL_INC = 30,
    EE_DIF_EFFECT = 35, EE_DIF_BRIGHTNESS = 43, EE_DIF_SPEED = 44,
    EE_DIF_MODE_TV = 36, EE_DIF_MODE_MOTION = 37, EE_DIF_MODE_UDPRAW = 38, EE_DIF_MODE_AMBIENT = 39,
    EE_DIF_IDLE_WAIT_MIN = 40, EE_DIF_IDLE_ON_MIN = 41, EE_DIF_IDLE_MODE = 42,
    EE_OTHER_BR_CL_DEL = 11, EE_OTHER_BR_CL_INC = 12, EE_OTHER_BRIGHTNESS_AUTO = 13,
    EE_OTHER_TO_OFF_TIME = 14, EE_OTHER_AMBIENT_MODE_TIME = 21, EE_OTHER_LED_FPS = 22,
};

/* =========================================================================== */
/* STRUCTS                                                                      */
/* =========================================================================== */
typedef struct LEDx {
    bool     Enabled  = true;
    bool     NeedsUpdate = false;
    uint8_t  Selected[(LED_NUM + 7) / 8];
    uint8_t  PixelOrder[LED_NUM - LED_HB_NUM_FAKE];
    uint8_t  HeartbeatOrder[LED_HB_NUM];
    CRGB     CurrentColor[LED_NUM_TOTAL];
    uint8_t  CurrentBrightness[LED_NUM_TOTAL];
    CRGB     TargetColor[LED_NUM_TOTAL];
    CRGB     StoredColor[LED_NUM];
    uint8_t  StoredBrightness[LED_NUM];
    CRGB     AmbientBackgroundColor[LED_NUM];
    uint8_t  AmbientBackgroundBrightness[LED_NUM];
    CRGB     StreamColor;
    uint8_t  StreamBrightness;
    uint32_t LastUpdateTime;
    uint32_t LastRefreshTime;
    uint32_t PackedRGBValue;
    uint8_t  Changed[(LED_NUM + 7) / 8];
    bool     DeltaMode = true;
} LEDx;

typedef struct LISENSx {
    int      Average     = 0;
    int      Lux         = 1;
    uint32_t SampleCount = 0;
    uint32_t SampleSum   = 0;
    SCHED::TaskId TaskID = SCHED::TASK_ID_NONE;
} LISENSx;

typedef struct UDPRAWx {
    bool     Status   = false;
    uint32_t LastCheck;
    uint8_t  InitTime[6];
    uint8_t  CachedBrightness = 0;
    float    Fps      = 0.0f;
} UDPRAWx;

typedef struct TV_LOGx {
    uint32_t epoch;
    bool     Event;
    int      PinValueBefore;
    int      PinValueAtTrigger;
} TV_LOGx;

typedef struct TVx {
    bool     ReadPin; bool Status; bool LastStatus;
    int      PinValue;
    int      PrevPinValue;
    uint32_t LastDebounceTime;
    uint32_t LastReadStatus;
    uint32_t CountdownTimer;
    bool     Transitioning = false;
    TV_LOGx  LOG[TV_LOG_INDEX_MAX];
} TVx;

typedef struct MOTION_LOGx {
    uint32_t epoch;
    uint8_t  TriggerBy;
} MOTION_LOGx;

typedef struct MOTIONx {
    uint8_t     Status        = motON;
    bool Trigger;
    uint8_t TriggerBy;
    uint32_t TriggerTime;
    uint32_t LastCheck;
    uint32_t LastChangeColor;
    uint32_t AutoOffTime;
    CRGB        Color;
    bool        Transitioning = false;
    MOTION_LOGx LOG[MOTION_LOG_INDEX_MAX];
} MOTIONx;

typedef struct BME280x { float Temperature; float Humidity; } BME280x;
typedef struct NETx  { uint8_t ConnectTime[6]; } NETx;
typedef struct DATEx { uint8_t time[6]; } DATEx;

typedef struct APPx {
    char     RecvBuff[APP_UDP_MAX_BUFFER_SIZE];
    uint8_t  SelectedLedCache[LED_NUM];
    int      SelectedCount;
    bool     SelectedCacheDirty;
    uint8_t  CurSeq;
    bool     CurSeqValid;
    uint8_t  LastResult;
    uint8_t  LastSeq;
    bool     LastSeqValid;
    uint8_t  LastSeqResult;
    uint32_t LastSeqTime;
    uint8_t  LogSeq;
    uint16_t SessionId;
    bool     Suspended;
    SCHED::TaskId KeepAliveID;
    bool     SaveFailed;
    uint8_t  ActiveProtocol;
    uint8_t  RxProtocol;
    bool     ColorSyncPending;
    uint32_t ColorSyncLast;
} APPx;

typedef struct DIF_LOGx {
    uint32_t epoch;
    uint8_t  Mode;
    uint8_t  Effect;
    uint8_t  TriggerBy;
} DIF_LOGx;

typedef struct DIF_Colorx { uint8_t r1, g1, b1, r2, g2, b2; } DIF_Colorx;

typedef struct DIFx {
    uint8_t  Mode;
    uint16_t ParfumMin;
    uint8_t  StripStatus;
    uint32_t LastStatusTime;
    uint32_t LastRequestTime;
    uint32_t IdleTimerReset;
    bool     IdlePulseActive;
    uint32_t IdlePulseStart;
    uint8_t  CmdSeq;
    uint8_t  PendingCmdSeq[DIF_PENDING_ACK_MAX];
    uint8_t  PendingAppSeq[DIF_PENDING_ACK_MAX];
    uint8_t  PendingCount;
    uint8_t  CmdResult;
    DIF_LOGx LOG[DIF_LOG_INDEX_MAX];
    uint16_t UsageAccumMin;
    uint16_t UsageAvgMin;
    uint8_t  UsageRefillCount;
    uint16_t UsageTotalRefills;
} DIFx;

typedef struct ARDx { uint32_t LastReceive; uint32_t LastUdpReceive; } ARDx;
typedef struct AMx  { bool Status = false; } AMx;
typedef struct EEx { int Index = EE_START_READ_INDEX; SCHED::TaskId tID = SCHED::TASK_ID_NONE; } EEx;
typedef struct { uint8_t id; const char* name; EE_SettingType type; uint8_t index; } EE_SettingDef;

typedef struct MQTTx {
    bool     Up;
    uint32_t LastTry;
    bool     RxPending;
    int      RxLen;
    // Consecutive Reconnect() failures - backs the retry interval off once
    // this crosses MQTT_BACKOFF_STREAK, so a run of rejected/unreachable
    // attempts (each a blocking TLS connect, up to several seconds) can't
    // keep re-firing every MQTT_RETRY_MS and starve loop() of time UDP/LED/
    // TV handling also needs. Reset to 0 on the next successful connect.
    uint8_t  FailStreak;
} MQTTx;

typedef struct MQTTCREDx {
    char user[MQTTCRED_USER_MAX + 1];
    char pass[MQTTCRED_PASS_MAX + 1];
    bool valid;
} MQTTCREDx;

// HBx - a second, HB-specific scratch beyond the generic per-task
// SCHED::Scratch. Both the 14 idle HB effects (Hb.cpp) AND the TV-on HB
// helper functions (T_EFFECT_H_FadeOn/CenterBloom/LinearSweep/QuadPoint in
// Tv.cpp) read/write HB::State.Phase/ParamA alongside their own task's
// Scratch (e.g. effect 2 "Heartbeat" uses HB::State.Phase as "how many
// beats this cycle" while its own Scratch.phase tracks "which stage of the
// current beat"; T_EFFECT_H_CenterBloom uses HB::State.ParamA as the
// expansion-pair index). Kept as a dedicated global, not folded into
// Scratch, because - unlike the original TASKx problem - only ONE HB
// effect/helper is EVER active at a time (dispatched directly by function
// pointer, not via separate scheduled tasks), so there's no cross-task
// collision risk here to fix.
typedef struct HBx { int Phase = 0; int ParamA = 0; SCHED::TaskId TaskID = SCHED::TASK_ID_NONE; } HBx;

/* --- TELNET (new capability - see 00_PLAN.md SS9 R1) --- */
#define TELNET_PORT   23
// Verbosity gates termMsgLog()'s Telnet mirror only (see AppLink.cpp) -
// 0 normal (ERR/WRN/INF), 1 debug (+DBG), 2 verbose (+SEC/GAP). Neither
// EEPROM-persisted, same as Enabled - always starts at normal (0) on boot.
typedef struct TELNETx { bool Enabled = false; uint8_t Verbosity = 0; } TELNETx;

/* =========================================================================== */
/* GLOBAL STATE - extern declarations (defined in Globals.cpp)                 */
/* =========================================================================== */
extern uint32_t TimeNow;
extern bool     NET_Connected;
extern uint32_t NET_LastCheck;
extern __testmode TestMode;
extern SCHED::TaskId TestMode_tID;
namespace LED { extern LEDx State; }
extern Adafruit_NeoPixel stripFront;
extern Adafruit_NeoPixel stripBack;
extern Adafruit_NeoPixel stripHB;
namespace LISENS { extern LISENSx State; }
namespace HB { extern HBx State; }
extern const unsigned int LIGHT_SENS_LUX[3];
namespace UDPRAW { extern UDPRAWx State; }
extern WiFiUDP UDPRAW_UDP;
extern const int UDPRAW_BuffSize;
extern uint8_t UDPRAW_Buffer[];
namespace TV { extern TVx State; }
namespace MOTION { extern MOTIONx State; }
namespace APP { extern AMx Am; }
namespace APP { extern APPx State; }
extern WiFiUDP APP_UDP;
extern IPAddress APP_RECV_IP;
namespace DIF { extern DIFx State; }
extern WiFiUDP DIF_UDP;
extern IPAddress DIF_TARGET_IP;
namespace APP { extern ARDx Ard; }
namespace TELNET { extern TELNETx State; }
extern WiFiServer TELNET_Srv;
extern WiFiClient TELNET_Cli;
namespace NET { extern NETx Wifi; }
namespace NET { extern DATEx Date; }
extern WiFiUDP RTC_UDP;
extern NTPClient RTC_TimeClient;
extern BME280I2C bme280sensor;
extern uint8_t NET_WifiSt;
extern uint8_t RTC_Status;
namespace MQTT { extern MQTTx State; }
extern WiFiSSLClient MQTT_Net;
extern PubSubClient  MQTT_Cli;
namespace BME { extern BME280x State; }
namespace EE { extern EEx State; }
extern uint8_t EE_SET[EE_MEM_X];
extern uint8_t EE_Changed[(EE_MEM_X + 7) / 8];
extern uint8_t EE_ColorChanged[(LED_NUM + 7) / 8];
extern uint8_t EE_AmbientChanged[(LED_NUM + 7) / 8];
extern bool EE_UdpChanged;
extern bool EE_MotionChanged;
extern const EE_SettingDef EE_SETTINGS_TABLE[EE_MEM_X];
namespace MQTTCRED { extern MQTTCREDx State; }

extern const char WIFI_SSID[];
extern const char WIFI_PASS[];
extern const char MQTT_DEVICE_ID[];
