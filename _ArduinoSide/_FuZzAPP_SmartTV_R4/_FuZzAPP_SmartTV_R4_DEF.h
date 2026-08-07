/* =========================================================================== */
/* _FuZzAPP_SmartTV_R4_DEF.h  -- Master Header / Single-Point Include          */
/* FuZzAPP SmartTV R4 -- Arduino UNO R4 WiFi                                   */
/* =========================================================================== */
/*
/*  Contains: library includes, compile switches, defines, enums,
/*  structs, extern declarations, forward declarations for the entire project.
/*  No separate .h files -- everything is here.
 */

/*
           LED POSITION
  10 11 12 13 14  15 16 17 18 19   //
 09                            20  // 
 08                            21  // 
 07                            22  // TV Leds
 06                            23  //
 05                            24  //
  04 03 02 01 00  29 28 27 26 25   //
                                   //             /\      /\      /\
        HB LEDS                    // [60] __/\__/  \  __/  \  __/  \  __/\__ [237]
                                   //                \/      \/      \/
  30 31 32 33 34  35 36 37 38 39   // Com Leds
   41                        40    // uCom Leds
   
   42 43 44 45      46 47 48 49    // Bed Leds
  50 51 52 53 54  55 56 57 58 59   // Lamp Leds
*/

#ifndef _FuZzAPP_SmartTV_R4_DEF_h
#define _FuZzAPP_SmartTV_R4_DEF_h

/* --- LIBRARY INCLUDES -------------------------------------------------------- */
#include <Arduino.h>
#include <stdarg.h>
#include <Adafruit_NeoPixel.h>
// TaskJockeyMod's diagnostics feature (per-task handler execution time +
// table high-water marks) is toggled via TASKJOCKEY_ENABLE_DIAGNOSTICS
// directly in TaskJockeyMod.h, NOT here - it has its own .cpp compiled as a
// separate translation unit, which never sees a #define made by an includer,
// only its own header's. Currently on (see that file) for the Dual-Color-
// spam blocking investigation.
#include <TaskJockeyMod.h>
#include <EEPROM.h>
#include <Wire.h>
#include <BME280I2C.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <TimeLib.h>

/* --- CRGB COLOR STRUCT (NeoPixel Compatible) -------------------------------- */
struct CRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t _r, uint8_t _g, uint8_t _b) : r(_r), g(_g), b(_b) {}
};

/* --- CHSV COLOR STRUCT (HSV Color Space) ------------------------------------ */
struct CHSV {
    uint8_t h;  // hue (0-255)
    uint8_t s;  // saturation (0-255)
    uint8_t v;  // value/brightness (0-255)
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t _h, uint8_t _s, uint8_t _v) : h(_h), s(_s), v(_v) {}
};

/* --- COMPILE-TIME SWITCHES (uncomment to enable) ----------------------------- */
    // All debug logging OFF for now - uncomment individually to bring any of
    // it back. Two-tier scheme used throughout below: the base macro is the
    // normal/lite tier (one line per discrete command/event/state-change -
    // cheap, safe to leave on). Its "_VERBOSE" sibling is the deep tier
    // (per-tick/per-packet/internal-churn detail - noisy, can measurably slow
    // animations down, e.g. LED_VERBOSE - confirmed live during this
    // session's LD/Ld investigation). Enabling a _VERBOSE macro always pulls
    // in its own base tier too, so you never have to define both by hand -
    // see the cascade block below all the switches.

//  #define ENABLE_EXECUTIONTIME
    // Loop/task timing probe (us per section). Standalone - already a single
    // deep diagnostic, no lighter counterpart to split it against.

//  #define ENABLE_LOG_APP
//  #define ENABLE_LOG_APP_VERBOSE
    // APP_VERBOSE = raw per-packet "sent [...]" confirmations from
    // termMsgSend()/termMsgSendBin() - fires on every LK color-sync flush
    // during an active animation, too frequent for lite. APP alone still
    // covers recv/exec/settings/status/link/fault one-line events.

//  #define ENABLE_LOG_LUX
//  #define ENABLE_LOG_LUX_VERBOSE

//  #define ENABLE_LOG_UDPRAW
//  #define ENABLE_LOG_UDPRAW_VERBOSE

//  #define ENABLE_LOG_BME280
//  #define ENABLE_LOG_BME280_VERBOSE

//  #define ENABLE_LOG_TASK
//  #define ENABLE_LOG_TASK_VERBOSE
    // TASK_VERBOSE is the scheduler-internal tier (formerly its own macro,
    // ENABLE_LOG_TASKINFO) - TSK::AddTask/KillID/KillTasksAvoidLocked/
    // ResetTime/setTaskInterval churn. TASK alone is the narrative
    // "effect task started/ended" tier.

//  #define ENABLE_LOG_LED
//  #define ENABLE_LOG_LED_VERBOSE
    // LED_VERBOSE is the per-tick animation-task tier (formerly its own
    // macro, ENABLE_LOG_LED_TASK) - T_SMOOTH_CHANGE/T_DUAL_COLOR/
    // T_SHAKE_DUAL_COLOR/etc. Stay off during normal use - fires every
    // single animation tick and measurably slows transitions down
    // (confirmed live during this session's LD/Ld investigation).

//  #define ENABLE_LOG_EEPROM
//  #define ENABLE_LOG_EEPROM_VERBOSE

//  #define ENABLE_LOG_MOTION
//  #define ENABLE_LOG_MOTION_VERBOSE
    // MOTION_VERBOSE is the per-effect motion-task tier (formerly its own
    // macro, ENABLE_LOG_MOTION_TASK). MOTION alone is TV::Status()'s
    // TV-pin + motion-sensor polling.

//  #define ENABLE_LOG_NET
//  #define ENABLE_LOG_NET_VERBOSE

//  #define ENABLE_LOG_DIF
//  #define ENABLE_LOG_DIF_VERBOSE

//  #define ENABLE_LOG_MQTT
//  #define ENABLE_LOG_MQTT_VERBOSE
    // MQTT_VERBOSE = per-packet "sent [...]" confirmations from Publish() -
    // same split as APP/APP_VERBOSE on the UDP side. MQTT alone still covers
    // recv/dispatch and link-down/dropped events.

    // --- Effect/animation tracing (TV-ON/TV-OFF/motion/idle-HB sub-effects) ---
    // Same two-tier scheme, named INFO/VERBOSE instead of bare/VERBOSE since
    // this tag predates the others: INFO = start / phase-transition /
    // completion events (cheap, one line per state change). VERBOSE =
    // anything deeper/more frequent a future effect might want to add
    // (per-tick detail) - reserved for that, nothing uses it yet. Tag every
    // line from this tier "[ANIME]" so it's greppable on its own, separate
    // from the general TASK/MOTION task-lifecycle logs above.
//  #define ENABLE_LOG_ANIME_INFO
//  #define ENABLE_LOG_ANIME_VERBOSE

    /* --- verbose cascades: enabling any _VERBOSE macro pulls in its base tier too --- */
    #if defined(ENABLE_LOG_APP_VERBOSE)     && !defined(ENABLE_LOG_APP)
        #define ENABLE_LOG_APP
    #endif
    #if defined(ENABLE_LOG_LUX_VERBOSE)     && !defined(ENABLE_LOG_LUX)
        #define ENABLE_LOG_LUX
    #endif
    #if defined(ENABLE_LOG_UDPRAW_VERBOSE)  && !defined(ENABLE_LOG_UDPRAW)
        #define ENABLE_LOG_UDPRAW
    #endif
    #if defined(ENABLE_LOG_BME280_VERBOSE)  && !defined(ENABLE_LOG_BME280)
        #define ENABLE_LOG_BME280
    #endif
    #if defined(ENABLE_LOG_TASK_VERBOSE)    && !defined(ENABLE_LOG_TASK)
        #define ENABLE_LOG_TASK
    #endif
    #if defined(ENABLE_LOG_LED_VERBOSE)     && !defined(ENABLE_LOG_LED)
        #define ENABLE_LOG_LED
    #endif
    #if defined(ENABLE_LOG_EEPROM_VERBOSE)  && !defined(ENABLE_LOG_EEPROM)
        #define ENABLE_LOG_EEPROM
    #endif
    #if defined(ENABLE_LOG_MOTION_VERBOSE)  && !defined(ENABLE_LOG_MOTION)
        #define ENABLE_LOG_MOTION
    #endif
    #if defined(ENABLE_LOG_NET_VERBOSE)     && !defined(ENABLE_LOG_NET)
        #define ENABLE_LOG_NET
    #endif
    #if defined(ENABLE_LOG_DIF_VERBOSE)     && !defined(ENABLE_LOG_DIF)
        #define ENABLE_LOG_DIF
    #endif
    #if defined(ENABLE_LOG_MQTT_VERBOSE)    && !defined(ENABLE_LOG_MQTT)
        #define ENABLE_LOG_MQTT
    #endif
    #if defined(ENABLE_LOG_ANIME_VERBOSE)   && !defined(ENABLE_LOG_ANIME_INFO)
        #define ENABLE_LOG_ANIME_INFO
    #endif

//  #define ENABLE_LOG_RAM_MONITOR
    // Periodic free-RAM print in loop() (see RAM_MONITOR_INTERVAL_MS below) -
    // independent of the on-demand K0D debug dump. Added to watch for a leak
    // while stress-testing rapid-fire Dual Color (LD/Ld) commands, the same
    // way the earlier Collision-effect RAM leak was caught. Turn off once
    // that investigation is done - it's a Serial.print per interval, cheap
    // but pointless noise otherwise.
    #define RAM_MONITOR_INTERVAL_MS 1000

/* --- GLOBAL MACROS ----------------------------------------------------------- */
#define NL              "\n"
#define FW_NAME          "FuZzAPP SmartTV R4"  /* reported in the debug report header */
#define SERIAL_BAUD     115200
#define TIMEFIELD(t, f) (t[f] < 10 ? "0" : ""), t[f]   /* HH:MM:SS zero-pad */
#define TIMEVAL(v)      ((v) < 10 ? "0" : ""), (v)     /* zero-pad a raw HH/MM/SS/DD/MM value, e.g. TIMEVAL(hour(epoch)) */
#define S_TO_MS(x)      ((uint32_t)(x) * 1000UL)
#define TASK_ID_NONE    255                              /* no task assigned */

/* --- Bit manipulation macros for bitfield arrays -------------------------------- */
#define BIT_SET(arr, bit)   ((arr)[(bit) / 8] |= (1 << ((bit) % 8)))
#define BIT_CLEAR(arr, bit) ((arr)[(bit) / 8] &= ~(1 << ((bit) % 8)))
#define BIT_TEST(arr, bit)  (((arr)[(bit) / 8] & (1 << ((bit) % 8))) != 0)
#define BIT_CLEAR_ALL(arr, size) memset((arr), 0, (size))

/* --- LED MODULE -------------------------------------------------------------- */
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
#define HB_BRIGHTNESS_MAX   100   /* hard cap on HB (heartbeat) strip brightness, enforced in LED::setPixel() regardless of source */
#define LED_REFRESH_TIME   1000   /* ms */
/* LED refresh-rate cap (Settings > OTHER > LED FPS) -- index into LED_FPS_TABLE, EE_OTHER_LED_FPS */
#define LED_FPS_OPTIONS_TOTAL   9   /* entries in LED_FPS_TABLE, mirrors Android's FPS option list */
#define LED_FPS_DEFAULT_INDEX   5   /* index of 120 fps below -- EEPROM-reset value */
extern const uint8_t LED_FPS_TABLE[LED_FPS_OPTIONS_TOTAL] PROGMEM; /* refresh period ms, by fps: 15/25/30/60/90/120/150/200/240 */

/* --- LIGHT SENSOR (LISENS) --------------------------------------------------- */
#define LIGHT_SENSOR_PIN          A1
#define LIGHT_SENSOR_CHECK_TIME   2000   /* ms */
#define LIGHT_SENSOR_SAMPLES      200
#define LIGHT_SENSOR_LUX_FIX        5   /* hysteresis band */

/* --- TV MODULE --------------------------------------------------------------- */
#define TV_PIN              17        /* A3 */
#define TV_ADC_DIVIDER      15        /* scale ADC 0-1023 to 0-68 */
#define TV_TEST_PIN_ON       0        /* test-mode simulated ON value */
#define TV_TEST_PIN_OFF     30        /* test-mode simulated OFF value */
#define TV_DEBOUNCE_TIME    70        /* ms */
#define TV_READ_STATUS_TIME 1000      /* ms */
#define TV_LOG_INDEX_MAX    30        /* on/off event ring-buffer depth */

/* --- MOTION MODULE ----------------------------------------------------------- */
#define MOTION_PIN_COM          4    /* INPUT, active HIGH */
#define MOTION_PIN_BED          8    /* INPUT_PULLUP, active LOW */
#define MOTION_CHECK_TIME    2000    /* ms -- min interval between triggers */
#define MOTION_TIME_FIX       300    /* ms -- debounce hold */
#define MOTION_LOG_INDEX_MAX    5    /* event ring-buffer depth */
#define MOTION_COLLISION_BLOOM_MAX_TICKS 150  /* safety cap -- see T_EFFECT_MOTION_ON_5_TheCollision phase 3 */

/* --- NET MODULE -------------------------------------------------------------- */
#define NET_CHECK_TIME      30UL     /* seconds -- WiFi health-check period */
#define NET_CONNECT_TIMEOUT 10UL     /* seconds -- blocking connect ceiling */
#define NET_RETRY_DELAY     30UL     /* seconds -- reconnect retry delay */
#define NET_RTC_RETRY       10UL     /* seconds -- NTP retry delay */
#define NET_RTC_RESYNC   21600UL     /* seconds -- 6-hour NTP re-sync */
#define NET_IP_TIMEOUT      10UL     /* seconds -- DHCP polling ceiling */
#define NET_TIMEZONE             3   /* UTC+N hours */
#define NET_CHECK_INTERVAL     500   /* ms -- NET::Connected_Cached() refresh period */
/* Time-field index aliases */
#define _HH  0
#define _MI  1
#define _SS  2
#define _DD  3
#define _MM  4
#define _YY  5
/* Back-compat aliases */
#define Check_TIME  NET_CHECK_TIME
#define RTC_RETRY_TIME   NET_RTC_RETRY
#define RTC_TIMEZONE     NET_TIMEZONE

/* --- UDPRAW MODULE ----------------------------------------------------------- */
#define UDPRAW_CHECK_TIME       5000  /* ms -- stream timeout */
#define UDPRAW_LED_MOVIE_DELAY  6000  /* ms -- startup fade delay */

/* --- APP MODULE -------------------------------------------------------------- */
#define APP_BRIGHT              LED_BRIGHTNESS_MAX
#define APP_UDP_PORT            8472
#define APP_UDP_MAX_BUFFER_SIZE 512
#define APP_UDP_TIMEOUT         100   /* ms - was 5ms, same fix as DIF_UDP_TIMEOUT below: too
                                          tight for a peer without steady traffic already
                                          keeping ARP warm (e.g. a test tool vs. the phone app) */
#define APP_ACK_CHAR            '#'   /* command ACK envelope: "#SS<cmd>" in / "#SSR" out */
#define APP_ACK_HDR               3   /* envelope header bytes stripped: '#' + 2 hex seq */
#define APP_ACK_DEDUP_MS       3000   /* re-ack (don't re-run) a repeated seq within this window */
#define APP_LOG_CHAR            '*'   /* term log envelope: "*L SS QQ <text>" - no spaces on the wire   */
#define APP_LOG_HDR               5   /* header chars after '*': level(1) + source(2) + seq(2), all hex */
#define APP_PROTO_VER             4   /* bumped: v8.1 split/dirty protocol, keep-alive, 'V' identity removed */
#define RTC_EPOCH_SANE   1600000000L  /* Sep 2020 - anything below this means the clock never synced    */
#define APP_LINK_PUSH_MS      10000   /* min gap between unsolicited link/time pushes                   */

/* Keep-alive: the board pings 'k' to the app on a fixed cadence; any inbound
   app packet (its 'k' reply, a command, anything) refreshes liveness. If the
   app stays silent past APP_ALIVE_TIMEOUT_MS the board is "suspended" - it stops
   all outbound (UDP and the MQTT mirror) except the 'k' ping, so a dead/absent
   app is never spammed. First inbound after that resumes + forces a full sync. */
#define KeepAlive_MS      10000   /* ms - 'k' ping cadence, board -> app                            */
#define APP_ALIVE_TIMEOUT_MS  25000   /* ms - no inbound for this long => app dead, suspend TX (~2 pings)*/

/* Fault flags - bitmask in the 'f' packet. The app can react to these as
   state; before, they only ever existed as log text. */
#define APP_FAULT_WIFI        0x0001  /* WiFi lost or retrying                     */
#define APP_FAULT_NTP         0x0002  /* clock never synced / NTP retrying         */
#define APP_FAULT_EEPROM      0x0004  /* a scheduled EEPROM write did not complete */
#define APP_FAULT_BME         0x0008  /* climate sensor reading implausible        */
#define APP_FAULT_DIF_NORESP  0x0010  /* diffuser not answering status requests    */
#define APP_FAULT_DIF_NOWATER 0x0020  /* diffuser reports out of water             */
#define APP_WATCHDOG_MS       1500     /* ms -- app-inactivity poll interval */
#define ARD_TIMEOUT        240000     /* ms -- app inactivity timeout */
#define ARD_RAM_TOTAL       32768     /* R4 WiFi total SRAM */

/* --- DIF MODULE (Diffuser) ---------------------------------------------------- */
#define DIF_UDP_PORT             8439  /* target + listen port for diffuser UDP link */
#define DIF_UDP_MAX_BUFFER_SIZE    64  /* was 16 - too small once "Ds"/"Dc" grew to 24 chars (usage/refill
                                           fields) and "Dh" (full history reply) to 44 chars; every reply
                                           past the old 16-byte ceiling was silently dropped as "too large"
                                           in DIF::Loop(), which is why usage/history never reached the app */
#define DIF_UDP_TIMEOUT             100  /* ms -- beginPacket()/endPacket() ceiling per non-blocking attempt (see TickAsyncSend()) - was 5ms, too tight for two marginal-signal WiFi hops */
#define DIF_MSG_MAX_LEN              48  /* staging buffer for the async sender - matches the "#SS"+longest Dn body sizing already used at the SendCmd() call site */
#define DIF_MODE_MAX                4  /* CONT, 10 SEC, 2H AFTER SLEEP, 4H AFTER SLEEP -- only range ever sent in a "Dn" command */
#define DIF_EFFECT_COUNT            8  /* animated effects, EE 1-8 (EE=0 is always static) -- mirrors diffuser firmware */
#define DIF_PARFUM_MAX_MIN        360  /* max parfum-mode duration, minutes -- mirrors diffuser firmware PARFUM_MAX_MIN */
#define DIF_MODE_OUT_OF_WATER       5  /* synthetic mode the diffuser reports in "Ds" when dry -- status-only, never sent in "Dn" */
#define DIF_MODE_NO_RESPONSE        6  /* synthetic mode set when "Ds" goes unanswered -- status-only, never sent in "Dn" */
#define DIF_STATUS_CHECK_S          5  /* s -- locked task interval: send "Dc" and judge whether the previous one was answered */
#define DIF_IDLE_CHECK_S           10  /* s -- locked task interval: check whether idle-pulse X timeout has elapsed */
#define DIF_LOG_INDEX_MAX          15  /* idle-pulse event ring-buffer depth */
#define DIF_PENDING_ACK_MAX         4  /* in-flight relayed app commands awaiting the diffuser's ack, at once */

/* --- MQTT MODULE (HiveMQ Cloud) -- optional, last module; mirrors APP over TLS -- */
#include <PubSubClient.h>                        /* Nick O'Leary                    */

#define MQTT_HOST       "cb6c04d1ec6d4bf7b31ec5533ff91102.s1.eu.hivemq.cloud" /* cluster URL, no scheme         */
#define MQTT_PORT       8883                     /* TLS                             */
/* MQTT_USER/MQTT_PASS used to be hardcoded here (shared by every install of
   this firmware+app pair). Removed - credentials are now supplied by the
   app over the local UDP link (see MQTTCRED::cmdSetCredentials(), the '$'
   command in Exec()) and cached in EEPROM at the far end of the region
   (EEPROM.length() - MQTTCRED_BLOCK_SIZE), deliberately not contiguous with
   the settings block at EE_START_READ_INDEX so neither can grow into the
   other. See MQTTCRED namespace in the .ino for load/save/verify. */
#define MQTT_BASE       "LEDs"           /* topic root -- one per device     */
#define MQTT_TOPIC      MQTT_BASE "/cmd"         /* single duplex topic -- both ends pub+sub */
#define MQTT_TAG_DEV    'D'                      /* prefix WE publish with -- echo filter    */
#define MQTT_TAG_APP    'A'                      /* prefix the app publishes with            */
#define MQTT_BUF_SIZE   APP_UDP_MAX_BUFFER_SIZE  /* 512 -- match app buffer          */
#define MQTT_KEEPALIVE  30                       /* s                               */
#define MQTT_RETRY_MS   5000                     /* ms -- reconnect throttle         */

/* Single active transport -- only one of UDP/MQTT carries commands + replies at a
   time; the idle one is only watched for a 'Z' welcome (see APPx.ActiveProtocol
   below and Exec()/termMsgSend() in the .ino). */
#define APP_TRANSPORT_UDP  0
#define APP_TRANSPORT_MQTT 1

/* Link state -- value-init in the .ino GLOBALS block. */
typedef struct MQTTx {
    bool     Up;         /* true while broker session is live               */
    uint32_t LastTry;    /* TimeNow of last (re)connect attempt (throttle)  */
    bool     RxPending;  /* a command payload is staged in APP::State.RecvBuff     */
    int      RxLen;      /* length of that payload                          */
} MQTTx;                 /* rx reuses APP::State.RecvBuff -- saves 512B static RAM   */

namespace MQTT {
void Setup();                                    /* @brief init TLS client + first connect  */
void Loop();                                     /* @brief service broker + dispatch rx     */
void Reconnect();                                /* @brief throttled (re)connect + subscribe*/
void Callback(char* t, byte* p, unsigned int l); /* @param t topic @param p payload @param l len */
void Publish(const char* msg);                   /* @param msg line to tag + publish on MQTT_TOPIC */
void Publish(const uint8_t* buf, size_t len);    /* binary overload: tag byte + raw payload (for the 'LK' colour packet) */
static void Dispatch();                           /* fwd decl - defined in .ino, file-local (MQTT::Dispatch, no args) */
} // namespace MQTT

/* --- EEPROM MODULE ----------------------------------------------------------- */
#define EE_START_READ_INDEX         5
#define EE_MEM_X                   50    /* settings array size -- padded to next 10 boundary */
//#define EE_SAVE_TIME             30000    /* ms -- 30 sec before write */
#define EE_SAVE_TIME             3000    /* ms -- 30 sec before write */
#define EE_SAVE_DELAY_BETWEEN_CHUNKS 1   /* ms between byte writes */

/* --- MQTT CREDENTIAL STORAGE --------------------------------------------------
   Anchored at the far END of the EEPROM region (EEPROM.length() - MQTTCRED_
   BLOCK_SIZE, computed at runtime - EEPROM.length() isn't a compile-time
   constant) rather than appended after the settings block above, so the
   settings array growing (EE_MEM_X) and this block growing never collide -
   they grow from opposite ends toward each other. 8192 bytes total on the
   UNO R4 (FLASH_TOTAL_SIZE in pins_arduino.h) vs ~55 used by settings today,
   so there's ample room either way; this is just defensive layout. */
#define MQTTCRED_USER_MAX      32   /* chars, +1 for null terminator when stored */
#define MQTTCRED_PASS_MAX      32   /* chars, +1 for null terminator when stored */
#define MQTTCRED_MAGIC       0xA5   /* marks the block as "configured"; anything else = never set */
#define MQTTCRED_BLOCK_SIZE  (1 + (MQTTCRED_USER_MAX + 1) + (MQTTCRED_PASS_MAX + 1))  /* magic + user + pass */

/* RAM cache of the credential block above -- loaded once at boot by
   MQTTCRED::Load(), and updated only after a candidate pair is verified
   live against the broker (see MQTTCRED::cmdSetCredentials()). valid==false
   means "never configured" -- Reconnect() then skips MQTT entirely instead
   of trying MQTT_USER/MQTT_PASS defines, which no longer exist. */
typedef struct MQTTCREDx {
    char user[MQTTCRED_USER_MAX + 1];   /* '\0' if never configured */
    char pass[MQTTCRED_PASS_MAX + 1];
    bool valid;
} MQTTCREDx;

namespace MQTTCRED {
void Load();                                  /* @brief read cached creds from EEPROM at boot; leaves valid=false if magic byte absent */
void cmdSetCredentials(char *buff, int len);  /* @brief '$' command handler -- decode b64 user/pass, verify live against broker, persist on success */
} // namespace MQTTCRED

/* --- BME280 MODULE ----------------------------------------------------------- */
#define CheckTIME  10  /* seconds between sensor reads */

/* --- TEST MODE --------------------------------------------------------------- */
#define TESTMODE_DURATION  120000  /* ms */

/* =========================================================================== */
/* ENUMS                                                                       */
/* =========================================================================== */

/* Debug / inspector target -- used by _Debug(item) */
enum __debug {
    _debug_led_info,
    _debug_led_selected,
    _debug_led_order,
    _debug_led_color,
    _debug_led_tempcolor,
    _debug_motion,
    _debug_tv,
    _debug_ee,
    _debug_app,
    _debug_udpraw,
    _debug_bme280,
    _debug_ambientmode,
    _debug_wifi,
    _debug_arduino,
    _debug_lisens,
    _debug_heartbeat,
    _debug_dif,
    _debug_task,      /* scheduler: shared step state + every known task handle */
    _debug_rtc,       /* clock / NTP sync                                       */
    _debug_testmode,  /* forced test state                                      */
    _debug_all,       /* one-line summary from every module                     */
    _debug_mqtt,      /* broker link + queued rx cmd -- appended last to keep   */
                       /* every existing item's wire index stable                */
};

/* Test-mode override selection */
enum __testmode {
    _testmode_none,
    _testmode_tvOn,
    _testmode_tvOff,
    _testmode_udpraw,
    _testmode_motionCom,
    _testmode_motionBed,
    _testmode_dif,          /* diffuser test active -- exit cleanup runs DIF::AutoOff() */
    _testmode_lux,          /* app-forced lux level -- Check holds sensor updates */
};

/* Task completion sentinel values */
enum TASK_Status { taskDone = 99, taskHBEffectOn = 100 };

/* App command ACK result codes -- returned to the app as "#SSR" (R = 1 hex nibble) */
enum APP_AckResult {
    APP_ACK_OK          = 0,  /* applied as requested                          */
    APP_ACK_CLAMPED     = 1,  /* applied, but a value was clamped/coerced       */
    APP_ACK_REJECTED    = 2,  /* malformed / unparseable command                */
    APP_ACK_BLOCKED     = 3,  /* refused by a source guard (no active source)   */
    APP_ACK_LOCKED      = 4,  /* refused/queued: diffuser busy in a parfum window*/
    APP_ACK_NOWATER     = 5,  /* diffuser out of water / no response            */
    APP_ACK_UNSUPPORTED = 6,  /* unknown / unsupported command                  */
    APP_ACK_UNAUTHORIZED = 7  /* MQTTCRED '$': broker rejected candidate user/pass */
};

/* Term log severity - first hex digit of the '*' envelope.
   ERR/WRN/INF/DBG are filterable in the app; SEC/GAP are layout-only records. */
enum APP_LogLevel {
    APP_LOG_ERR = 0,  /* failure the user must see                       */
    APP_LOG_WRN = 1,  /* recovered / degraded / guard refused            */
    APP_LOG_INF = 2,  /* live state: transitions, current values         */
    APP_LOG_DBG = 3,  /* constants, table dumps, wiring detail           */
    APP_LOG_SEC = 4,  /* section header - app draws a divider with title */
    APP_LOG_GAP = 5   /* blank spacer - app draws vertical whitespace    */
};

/* Term log source - 2 hex digits of the '*' envelope. Replaces the old
   "MODULE ::" text prefix: the app renders that tag from this id instead. */
enum APP_LogSource {
    APP_SRC_SYS     = 0x00,
    APP_SRC_NET     = 0x01,
    APP_SRC_RTC     = 0x02,
    APP_SRC_EE      = 0x03,
    APP_SRC_APP     = 0x04,
    APP_SRC_LED     = 0x05,
    APP_SRC_TV      = 0x06,
    APP_SRC_MOTION  = 0x07,
    APP_SRC_LUX     = 0x08,
    APP_SRC_DIF     = 0x09,
    APP_SRC_UDPRAW  = 0x0A,
    APP_SRC_AMBIENT = 0x0B,
    APP_SRC_BME     = 0x0C,
    APP_SRC_TASK    = 0x0D,
    APP_SRC_HB      = 0x0E,
    APP_SRC_TEST    = 0x0F
};

/* Motion sensor states */
enum Status { motAUTOOFF, motOFF, motON, motCOM, motBED };

/* Diffuser auto-on trigger sources -- stored in DIF_LOGx.TriggerBy */
enum DIF_TriggerBy { difTrigTV, difTrigUDPRAW, difTrigMotion, difTrigAmbient, difTrigIdle };

/* WiFi + NTP state machines */
enum NET_WiFiStatus { netWifiOK, netWifiLost, netWifiRetrying };
enum NET_RtcStatus  { rtcOK, rtcRETRY, rtcRETRYING };

/* EEPROM setting value type */
typedef enum { EE_TYPE_ONOFF, EE_TYPE_NUMERIC } EE_SettingType;

/* EEPROM setting indices */
enum EE_Settings {
    /* --- TV -------------------------------------------------------------- */
    EE_TV_ON_EFF                             = 0,
    EE_TV_OFF_EFF                            = 1,
    EE_TV_OFF_TIME                           = 2,
    EE_TV_BR_TV                              = 17,
    EE_TV_BR_COM                             = 3,
    EE_TV_BR_UCOM                            = 4,
    EE_TV_BR_BED                             = 5,
    EE_TV_BR_LAMP                            = 6,
    EE_TV_RANDOM_COLOR_START                 = 15,
    EE_TV_ON_BR_CL_DEL                       = 23,
    EE_TV_ON_BR_CL_INC                       = 24,
    EE_TV_OFF_BR_CL_DEL                      = 25,
    EE_TV_OFF_BR_CL_INC                      = 26,

    /* --- MOTION ------------------------------------------------------------ */
    EE_MOTION_ON_EFF                         = 20,
    EE_MOTION_BRIGHTNESS                     = 7,
    EE_MOTION_ON_TIME                        = 8,
    EE_MOTION_RANDOM_COLOR                   = 10,
    EE_MOTION_DIVIDE_BRIGHTNESS              = 16,
    EE_MOTION_RENEW_COLOR_TIME               = 18,
    EE_MOTION_AUTO_OFF_TIME                  = 19,
    EE_MOTION_BR_CL_DEL                      = 27,
    EE_MOTION_BR_CL_INC                      = 28,

    /* --- HEARTBEAT (HB) ----------------------------------------------------- */
    EE_HB_DUAL_COLOR                         = 31,
    EE_HB_EFFECT                             = 32,
    EE_HB_EFFECT_SPEED                       = 33,
    EE_TV_ON_HB_EFF                          = 34,

    /* --- AMBILIGHT / UDPRAW --------------------------------------------------- */
    EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX       = 9,
    EE_UDPRAW_BR_CL_DEL                      = 29,
    EE_UDPRAW_BR_CL_INC                      = 30,

    /* --- DIFFUSER ------------------------------------------------------------ */
    EE_DIF_EFFECT                              = 35,  /* diffuser strip effect: 0=static 1=fade 2=pulse 3=random 4=rainbow 5=sparkle 6=fire 7=bounce 8=confetti */
    EE_DIF_BRIGHTNESS                         = 43,  /* diffuser strip brightness base value, 0-APP_BRIGHT -- lux-compensated via LED::getLuxBrightness() before sending */
    EE_DIF_SPEED                              = 44,  /* diffuser animated-effect frame period, ms (Dn "SP") */
    EE_DIF_MODE_TV                            = 36,
    EE_DIF_MODE_MOTION                        = 37,
    EE_DIF_MODE_UDPRAW                        = 38,
    EE_DIF_MODE_AMBIENT                       = 39,
    /* 40-49 reserved -- 10-slot expansion block */
    EE_DIF_IDLE_WAIT_MIN                      = 40,  /* minutes idle (all sources off) before auto-pulse; 0 = disabled */
    EE_DIF_IDLE_ON_MIN                        = 41,  /* minutes to run diffuser during idle pulse */
    EE_DIF_IDLE_MODE                          = 42,  /* diffuser mode for idle pulse (1..DIF_MODE_MAX); 0 = feature disabled */

    /* --- OTHER --------------------------------------------------------------- */
    EE_OTHER_BR_CL_DEL                       = 11,
    EE_OTHER_BR_CL_INC                       = 12,
    EE_OTHER_BRIGHTNESS_AUTO                 = 13,
    EE_OTHER_TO_OFF_TIME                     = 14,
    EE_OTHER_AMBIENT_MODE_TIME               = 21,
    EE_OTHER_LED_FPS                          = 22,  /* LED refresh-rate cap, index into LED_FPS_TABLE (options-driven) -- reuses unused EE_TASK_DISABLED2 slot */
};

/* =========================================================================== */
/* STRUCTS                                                                     */
/* =========================================================================== */

/* --- Task engine shared state ------------------------------------------------ */
typedef struct TASKx { int Phase; int ParamA; int ParamB; } TASKx;

/* --- Heartbeat animation state ----------------------------------------------- */
typedef struct HBx { uint8_t Phase; int ParamA; taskId_t TaskID = TASK_ID_NONE; } HBx;

/* --- Main LED state (color buffers, order arrays, flags) --------------------- */
typedef struct LEDx {
    bool     Enabled  = true;
    bool     NeedsUpdate = false;
    uint8_t  Selected[(LED_NUM + 7) / 8];
    uint8_t  PixelOrder[LED_NUM - LED_HB_NUM_FAKE];
    uint8_t  HeartbeatOrder[LED_HB_NUM];
    CRGB     CurrentColor[LED_NUM_TOTAL];
    uint8_t  CurrentBrightness[LED_NUM_TOTAL];
    CRGB     TargetColor[LED_NUM_TOTAL];         /* smooth-transition target */
    CRGB     StoredColor[LED_NUM];                /* EEPROM-persisted colors */
    uint8_t  StoredBrightness[LED_NUM];           /* EEPROM-persisted brightness */
    CRGB     AmbientBackgroundColor[LED_NUM];           /* ambient mode colors */
    uint8_t  AmbientBackgroundBrightness[LED_NUM];
    CRGB     StreamColor;                        /* UDPRAW last-received color */
    uint8_t  StreamBrightness;
    /* Pixel[] removed -- colors are written straight into the NeoPixel strip
       buffers via LED::H_writeStripPixel(); saves LED_NUM_TOTAL*3 bytes RAM. */
    uint32_t LastUpdateTime;
    uint32_t LastRefreshTime;
    uint32_t PackedRGBValue;
    uint8_t  Changed[(LED_NUM + 7) / 8];      /* delta change tracking */
    bool     DeltaMode = true;
} LEDx;

/* --- Ambient light sensor state ---------------------------------------------- */
typedef struct LISENSx {
    int      Average     = 0;
    int      Lux         = 1;
    uint32_t SampleCount = 0;
    uint32_t SampleSum   = 0;
    taskId_t TaskID      = TASK_ID_NONE;
} LISENSx;

/* --- Ambilight UDP stream state ---------------------------------------------- */
typedef struct UDPRAWx {
    bool     Status   = false;
    uint32_t LastCheck;
    uint8_t  InitTime[6];           /* [HH MI SS DD MM YY] */
    uint8_t  CachedBrightness = 0;
    float    Fps      = 0.0f;       /* Live stream rate, recomputed every ~1s in Loop(); 0 when idle */
} UDPRAWx;

/* --- TV on/off event log entry ----------------------------------------------- */
typedef struct TV_LOGx {
    uint32_t epoch;            /* Unix epoch (RTC_TimeClient.getEpochTime()) when the transition was confirmed; 0 = empty slot */
    bool     Event;            /* true = turned ON, false = turned OFF */
    int      PinValueBefore;   /* TV::State.PinValue from the previous TV::Status() read cycle, before this transition */
    int      PinValueAtTrigger;/* TV::State.PinValue at the moment the transition was confirmed and TV::On()/TV::Off() fired */
} TV_LOGx;

typedef struct TVx {
    bool     ReadPin; bool Status; bool LastStatus;
    int      PinValue;
    int      PrevPinValue;     /* TV::State.PinValue snapshot from the previous read cycle -- feeds TV_LOGx.PinValueBefore */
    uint32_t LastDebounceTime;
    uint32_t LastReadStatus;
    uint32_t CountdownTimer;
    /* True from TV::On()/TV::Off() until T_EFFECT_TV_ON/T_EFFECT_TV_OFF's own
       finalization barrier clears it -- an explicit, narrowly-scoped signal for
       "the on/off transition task is still running" (see LISENS::setLux() and
       MQTT::Reconnect()). NOT the same as "any LED still differs from its
       TargetColor" -- HB's default fade-on only steps CurrentBrightness, never
       CurrentColor, so that comparison stays true indefinitely once the TV
       is on and was a real bug when tried here first. */
    bool     Transitioning = false;
    TV_LOGx  LOG[TV_LOG_INDEX_MAX]; /* ring-buffer of on/off events -- newest at [0] */
} TVx;

/* --- Motion event log entry -------------------------------------------------- */
typedef struct MOTION_LOGx {
    uint32_t epoch;       /* Unix epoch (RTC_TimeClient.getEpochTime()); 0 = empty slot */
    uint8_t  TriggerBy;   /* motCOM or motBED */
} MOTION_LOGx;

/* --- Motion detection state -------------------------------------------------- */
typedef struct MOTIONx {
    uint8_t     Status        = motON;
    bool Trigger;
    uint8_t TriggerBy;
    uint32_t TriggerTime;
    uint32_t LastCheck;
    uint32_t LastChangeColor;
    uint32_t AutoOffTime;
    CRGB        Color;
    MOTION_LOGx LOG[MOTION_LOG_INDEX_MAX];
} MOTIONx;

/* --- BME280 sensor readings -------------------------------------------------- */
typedef struct BME280x { float Temperature; float Humidity; } BME280x;

/* --- WiFi / NTP state -------------------------------------------------------- */
typedef struct NETx  { uint8_t ConnectTime[6]; } NETx;   /* [HH MI SS DD MM YY] */
typedef struct DATEx { uint8_t time[6]; } DATEx;         /* [HH MI SS DD MM YY] */

/* --- App UDP communication state --------------------------------------------- */
typedef struct APPx {
    char     RecvBuff[APP_UDP_MAX_BUFFER_SIZE];
    uint8_t  SelectedLedCache[LED_NUM];   /* cached selected LED indices */
    int      SelectedCount;
    bool     SelectedCacheDirty;
    uint8_t  CurSeq;        /* seq id of the command currently being handled    */
    bool     CurSeqValid;   /* true while an ACK-enveloped command is in flight  */
    uint8_t  LastResult;    /* APP_AckResult the active handler wants to report  */
    uint8_t  LastSeq;       /* seq of the most recently handled command          */
    bool     LastSeqValid;  /* true once at least one seq has been handled        */
    uint8_t  LastSeqResult; /* result of that command (to re-ack duplicates)      */
    uint32_t LastSeqTime;   /* TimeNow when it was handled (dedup window)         */
    uint8_t  LogSeq;        /* rolling seq stamped on every '*' term log packet   */
    uint16_t SessionId;     /* random per boot - still used as the MQTT client id */
    bool     Suspended;     /* true while the app is presumed dead (TX gated off)  */
    taskId_t KeepAliveID;   /* live 'k' keep-alive task id, or TASK_ID_NONE (set in Setup) */
    bool     SaveFailed;    /* last scheduled EEPROM write never completed        */
    uint8_t  ActiveProtocol; /* APP_TRANSPORT_UDP/MQTT -- the ONLY transport replies + pushes go out on */
    uint8_t  RxProtocol;     /* APP_TRANSPORT_UDP/MQTT -- transport the packet in Exec() right now arrived on */
    bool     ColorSyncPending; /* a call site marked LEDs dirty; drainColorSync() owes the app a flush   */
    uint32_t ColorSyncLast;    /* TimeNow of the last colour-sync flush -- FPS gate for drainColorSync() */
} APPx;

/* --- Diffuser auto-on event log entry ---------------------------------------- */
typedef struct DIF_LOGx {
    uint32_t epoch;      /* Unix epoch (RTC_TimeClient.getEpochTime()) when diffuser turned on; 0 = empty slot */
    uint8_t  Mode;       /* diffuser mode sent (1..DIF_MODE_MAX, or EE_DIF_IDLE_MODE for idle) */
    uint8_t  Effect;     /* diffuser strip effect sent (0..DIF_EFFECT_COUNT) */
    uint8_t  TriggerBy;  /* DIF_TriggerBy: difTrigTV/UDPRAW/Motion/Ambient/Idle */
} DIF_LOGx;

/* --- Diffuser strip colour pair -- primary (or single) + secondary (dual only) -- */
typedef struct DIF_Colorx { uint8_t r1, g1, b1, r2, g2, b2; } DIF_Colorx;

/* --- Diffuser (DIF) UDP communication state ---------------------------------- */
typedef struct DIFx {
    uint8_t  Mode;            /* last known mode -- 0=off, 1-4=DIF_MODE_NAMES index, 5=NO_WATER, 6=NO_RESPONSE (status-only; DIF::TurnOn() never sends 5/6) */
    uint16_t ParfumMin;       /* parfum-mode remaining minutes from the last "DsMMSSTTTT" reply -- 0 = parfum inactive */
    uint8_t  StripStatus;     /* last known strip status reported by diffuser via Ds/Dc -- 0=off, 1=static, 2=dual, 3-6=animated (mirrors diffuser's stripStatusCode()) */
    uint32_t LastStatusTime;  /* TimeNow snapshot of the last received "Ds" status reply */
    uint32_t LastRequestTime; /* TimeNow snapshot of the last "Ds" request sent -- used by StatusCheck to detect a timeout */
    uint32_t IdleTimerReset;  /* TimeNow snapshot of last source-active event; idle pulse triggers when (TimeNow-IdleTimerReset) >= EE_DIF_IDLE_WAIT_MIN*60000 */
    bool     IdlePulseActive; /* true = currently in the on-duration of an idle pulse (IdleCheck state flag) */
    uint32_t IdlePulseStart;  /* TimeNow snapshot of when the idle pulse turned on; pulse ends when (TimeNow-IdlePulseStart) >= EE_DIF_IDLE_ON_MIN*60000 */
    uint8_t  CmdSeq;          /* rolling seq id for our outgoing diffuser commands */
    /* FIFO of in-flight relayed app commands: each app-originated Dn/Dp/Df/@D we
       relay to the diffuser gets its own {CmdSeq, AppSeq} slot here, so a second
       relayed command sent before the first is acked doesn't orphan it (a single
       scalar pair here previously meant the second send silently clobbered the
       first's pending state). See DIF::PushPending/PopPendingByCmdSeq/FlushPending. */
    uint8_t  PendingCmdSeq[DIF_PENDING_ACK_MAX];
    uint8_t  PendingAppSeq[DIF_PENDING_ACK_MAX];
    uint8_t  PendingCount;
    uint8_t  CmdResult;       /* last result code parsed from a diffuser "#SSR" ack */
    DIF_LOGx LOG[DIF_LOG_INDEX_MAX]; /* ring-buffer of auto-on events (TV/UDPRAW/Motion/Ambient/Idle) -- newest at [0] */
    /* Usage/refill stats, from the extended "Ds"/"Dc" reply (see ParseStatus()) --
       all 0 if the diffuser hasn't sent the extended-length reply yet (older
       firmware, or right after boot before the first reply arrives). */
    uint16_t UsageAccumMin;     /* effective (mode-weighted) minutes run since the last refill */
    uint16_t UsageAvgMin;       /* rolling average of the last UsageRefillCount completed cycles, minutes */
    uint8_t  UsageRefillCount;  /* valid entries in the diffuser's rolling history, 0-10 */
    uint16_t UsageTotalRefills; /* lifetime refill count (clamped to 0xFFFF), not windowed */
} DIFx;

/* --- Arduino watchdog/timing state and ambient mode flag --------------------- */
/* LastReceive: any inbound (UDP or MQTT) -- drives suspend/keep-alive.
   LastUdpReceive: UDP packets only -- drives APP_RECV_IP expiry, so an
   MQTT-only session (app off the local subnet) doesn't keep a stale,
   unreachable UDP peer address alive forever. */
typedef struct ARDx { uint32_t LastReceive; uint32_t LastUdpReceive; } ARDx;
typedef struct AMx  { bool Status = false; } AMx;

/* --- EEPROM write-task state ------------------------------------------------- */
typedef struct EEx { int Index = EE_START_READ_INDEX; taskId_t tID = TASK_ID_NONE; } EEx;

/* --- EEPROM setting metadata ------------------------------------------------- */
typedef struct { uint8_t id; const char* name; EE_SettingType type; uint8_t index; } EE_SettingDef;

/* =========================================================================== */
/* GLOBAL STATE -- extern declarations (defined in the .ino GLOBALS block)     */
/* =========================================================================== */

extern uint32_t TimeNow;  /* millis() snapshot, updated every loop */
extern bool     NET_Connected;  /* cached NET::IsConnected() result */
extern uint32_t NET_LastCheck;  /* TimeNow of last cache refresh */
extern TASKx TASK;  /* shared task step/state */
extern TaskJockeyMod _TASK;  /* scheduler engine */
extern __testmode TestMode;
extern taskId_t TestMode_tID;
namespace LED { extern LEDx State; }
extern Adafruit_NeoPixel stripFront;
extern Adafruit_NeoPixel stripBack;
extern Adafruit_NeoPixel stripHB;
namespace LISENS { extern LISENSx State; }
extern const unsigned int LIGHT_SENS_LUX[3];  /* ADC thresholds */
namespace UDPRAW { extern UDPRAWx State; }
extern WiFiUDP UDPRAW_UDP;
extern const int UDPRAW_BuffSize;
extern uint8_t UDPRAW_Buffer[];
namespace TV { extern TVx State; }
namespace MOTION { extern MOTIONx State; }
namespace HB { extern HBx State; }
namespace APP { extern AMx Am; }
namespace APP { extern APPx State; }
extern WiFiUDP APP_UDP;
extern IPAddress APP_RECV_IP;
namespace DIF { extern DIFx State; }
extern WiFiUDP DIF_UDP;
extern IPAddress DIF_TARGET_IP;
namespace APP { extern ARDx Ard; }

/* WIFI_SSID/WIFI_PASS PROGMEM values (defined in the .ino) are sourced from
   the shared, gitignored _ArduinoSide/_Shared/WiFiCredentials.h (never
   committed) so both this sketch and _FuZzAPP_Diffuser read the WiFi
   password from one place. Copy WiFiCredentials.h.example there and fill
   in real values - see AGENTS.md. Angle-bracket include on purpose: a
   quoted relative path ("../_Shared/...") does not reliably resolve under
   arduino-cli, so FlashConsole.pyw's compiler.py instead adds _Shared as a
   --library search path at compile time. */
#include <WiFiCredentials.h>
extern const char WIFI_SSID[];
extern const char WIFI_PASS[];
namespace NET { extern NETx Wifi;  /* connectTime[6] */ }
namespace NET { extern DATEx Date;  /* time[6] */ }
extern WiFiUDP RTC_UDP;
extern NTPClient RTC_TimeClient;
extern uint8_t NET_WifiSt;  /* NET_WiFiStatus */
extern uint8_t RTC_Status;  /* NET_RtcStatus */
namespace MQTT { extern MQTTx State; }
extern WiFiSSLClient MQTT_Net;
extern PubSubClient  MQTT_Cli;
namespace BME { extern BME280x State; }
extern BME280I2C::Settings settings;
extern BME280I2C bme280sensor;
namespace EE { extern EEx State; }
extern uint8_t EE_SET[EE_MEM_X];
extern uint8_t EE_Changed[(EE_MEM_X + 7) / 8];
extern uint8_t EE_ColorChanged[(LED_NUM + 7) / 8];  /* bitfield: 1 bit per LED */
extern uint8_t EE_AmbientChanged[(LED_NUM + 7) / 8]; /* bitfield: 1 bit per LED */
extern bool EE_UdpChanged;
extern bool EE_MotionChanged;
extern const EE_SettingDef EE_SETTINGS_TABLE[EE_MEM_X];
extern "C" char* sbrk(int incr);

/* --- Internal module state (file-local, moved from .ino) --------------------- */
static uint8_t g_difRelaySeq = 0xFF;  /* app seq to relay diffuser's ack to (0xFF = none armed) */

/* Last value sent per group; a group re-sends only when its own value(s)
   change. Sentinels (-1 / !init) force a resend. APP::TxCacheReset() arms a
   full resend for the welcome/resync path. Faults keep their own transition
   state and are deliberately NOT cleared by the reset (term-only, on occurrence). */
static int      _txS_tv=-1,_txS_mo=-1,_txS_ur=-1,_txS_am=-1,_txS_ds=-1; /* s core    */
static int      _txH_tp=-1,_txH_hu=-1;                                  /* H climate */
static int      _txE_en=-1;                                             /* E enable  */
static int      _txM_lux=-1;                                            /* M lux     */
static int      _txW_bar=-1,_txW_wf=-1;                                 /* w link    */
static int      _txP_min=-1;                                            /* p parfum  */
static bool     _txF_init=false; static uint16_t _txF_mask=0;          /* f faults  */
static int      _txU_accum=-1,_txU_avg=-1,_txU_cnt=-1,_txU_tot=-1;      /* u dif usage */

/* --- Diffuser name-lookup tables (moved from .ino) ---------------------------- */
// Indexed 1-4 (DIF_MODE_MAX entries) - stored in PROGMEM to save RAM
static const char* const DIF_MODE_NAMES[DIF_MODE_MAX] PROGMEM = {
    "CONT", "10 SEC", "2H AFTER SLEEP", "4H AFTER SLEEP"
};
// Indexed 0-DIF_EFFECT_COUNT directly by EE value (0=static) -- mirrors diffuser's EFFECT_NAMES - stored in PROGMEM
static const char* const DIF_EFFECT_NAMES[DIF_EFFECT_COUNT + 1] PROGMEM = {
    "STATIC", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};
// Indexed 0-10 by the diffuser's stripStatusCode() (Ds/Dc reply, 2nd byte) -- debug display only - stored in PROGMEM
static const char* const DIF_STRIP_STATUS_NAMES[11] PROGMEM = {
    "OFF", "STATIC", "DUAL", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};

/* Name prefixes used to group the EEPROM dump by owning module, and the
   heading shown for each. Both arrays are index-parallel. Stored in PROGMEM to save RAM. */
static const char* const EE_GROUPS[] PROGMEM      = { "TV_", "MOTION_", "UDPRAW_", "HB_", "OTHER_", "TASK_", "DIF_" };
static const char* const EE_GROUP_NAMES[] PROGMEM = { "TV",  "MOTION",  "UDPRAW",  "HB",  "OTHER",  "TASK",  "DIFFUSER" };
static const uint8_t EE_GROUP_COUNT = sizeof(EE_GROUPS) / sizeof(EE_GROUPS[0]);

/* =========================================================================== */
/* FORWARD DECLARATIONS -- all functions, grouped by module                    */
/* =========================================================================== */

/* --- Debug ------------------------------------------------------------------- */
namespace PRNT {
void  _print(const char *msg);
char* formatMSG(const char *format, ...);   char* vformatMSG(const char *format, va_list args);
void  _Debug(uint8_t d);
} // namespace PRNT

/* --- Task scheduler ---------------------------------------------------------- */
/* New API: unit before Interval. Legacy prototypes kept for compatibility. */
namespace TSK {
taskId_t AddTask(const char* source, const char* call_func_name, void (*CALL_Func)(taskId_t), taskUnit_t unit, uint32_t Interval, uint32_t StartTime, bool Locked);
taskId_t AddTask(const char* source, const char* call_func_name, void (*CALL_Func)(taskId_t), uint32_t Interval, uint32_t StartTime, bool Locked);
void     setTaskInterval(const char* source, taskId_t tID, taskUnit_t unit, uint32_t Interval);
void     ResetTime(taskId_t tID);
} // namespace TSK
namespace LED { void T_AMBIENT_MODE_ON(taskId_t taskId); }
namespace APP { const char* _TestModeName(uint8_t m); void T_END_TEST_MODE(taskId_t taskId); }

namespace LED {
/* --- LED core ---------------------------------------------------------------- */
uint16_t TV(uint8_t i);   uint16_t COM(uint8_t i);   uint16_t UCOM(uint8_t i);
uint16_t BED(uint8_t i);  uint16_t LAMP(uint8_t i);  uint16_t HB(uint16_t i);
void     Setup();         void     Refresh(taskId_t taskId);
void     Refresh(uint32_t now);   /* hardware-push overload -- MUST be declared before loop() so LED::Refresh(TimeNow) binds here, not to Refresh(taskId_t) */
void     H_writeStripPixel(int gIdx, uint8_t r, uint8_t g, uint8_t b);   /* global idx -> owning strip buffer (replaces Pixel[]) */
CRGB     H_readStripPixel(int gIdx);                                     /* read one pixel back from a strip buffer (debug only)   */
bool     ShouldRefresh(uint32_t now);
uint16_t getFpsLimitMs();  /* current refresh period ms, from EE_OTHER_LED_FPS via LED_FPS_TABLE */
void     Show();          void     setAll(int r, int g, int b, int br);
void     setPixel(int p, int r, int g, int b, int brVal, bool s);
void     Select(int l, bool state);   bool IsSelected(int l);   bool IsHB(int l);
bool     LED_TG_Step(uint8_t &current, uint8_t target, uint8_t inc);
bool     TG_Step(uint8_t &current, uint8_t target, uint8_t inc);      /* fwd decl - defined in .ino, file-local (LED::TG_Step) */
bool     TG_BRIGHTNESS(int led, uint8_t brVal, uint8_t inc, bool lisensReset = true);
bool     TG_COLOR(int led, int r, int g, int b, int inc, bool lisensReset = true);
bool     TG_TEMPCOLOR(int led, int r, int g, int b, int inc, bool lisensReset = true);
void     setRandomColor(bool z);
void     UDPRAW_SetColor(int r, int g, int b, int br);
void     MOTION_SetColor(int r, int g, int b, int br);
void     setDualColorMapping(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
void     setColorToSelected(uint8_t r, uint8_t g, uint8_t b);   void setBrightnessToSelected(uint8_t br);
void     getHarmoniousPair(CHSV &outA, CHSV &outB);
uint8_t  getLuxBrightness(int b);   uint32_t getRandomColor();
uint16_t getLuxAdaptFactor();
uint16_t getLuxAdaptDelay(uint16_t del);   uint16_t getLuxAdaptInc(uint16_t inc);
void     shuffleArray(uint8_t *array, int size);
void     LED_MarkChanged(int ledIdx);   void MarkChangedRange(int start, int end);
uint8_t  getChangedCount();
bool     IsChanged(int ledIdx);
int      HB_GetBaseBr();
void     LED_HB_SetAll(uint8_t r, uint8_t g, uint8_t b, uint8_t br, bool show = false);
void     HB_SetFromPreColor(uint8_t br, bool show = false);
void     HB_SetPixelFromPreColor(int i, uint8_t br);
void     HsvToRgb(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b);
bool     FadeAllToZero(uint8_t brInc);
void     T_LUX_BR_CHANGE(taskId_t taskId);   void T_SMOOTH_CHANGE(taskId_t taskId);
void     T_DUAL_COLOR(taskId_t taskId);       void T_SHAKE_DUAL_COLOR(taskId_t taskId);
void     T_LEDS_TO_OFF(taskId_t taskId);
} // namespace LED

namespace LISENS {
/* --- Light sensor ------------------------------------------------------------ */
void Setup();   void Check(taskId_t taskId);   void ResetTime();
void setLux(int newLux);
} // namespace LISENS

namespace HB {
/* --- Heartbeat effects ------------------------------------------------------- */
void StartEffect(bool silent, bool reset, bool fast);
void EndTask();
void T_EFFECT_HB(taskId_t taskId);
void T_EFFECT_HB_1_WhiteMove(taskId_t taskId);
void T_EFFECT_HB_2_Heartbeat(taskId_t taskId);
void T_EFFECT_HB_3_RandomFade(taskId_t taskId);
void T_EFFECT_HB_4_TravelingShadow(taskId_t taskId);
void T_EFFECT_HB_5_ExpandingRaindrops(taskId_t taskId);
void T_EFFECT_HB_6_Colors(taskId_t taskId);
void T_EFFECT_HB_7_ShootingStarRandom(taskId_t taskId);
void T_EFFECT_HB_8_RandomSparklingPop(taskId_t taskId);
void T_EFFECT_HB_9_GlidingAurora(taskId_t taskId);
void T_EFFECT_HB_10_TheGlitchMatrix(taskId_t taskId);
void T_EFFECT_HB_11_StochasticPlasma(taskId_t taskId);
void T_EFFECT_HB_12_DigitalRain(taskId_t taskId);
void T_EFFECT_HB_13_DualPulse(taskId_t taskId);
void T_EFFECT_HB_14_RainbowWavePulse(taskId_t taskId);
} // namespace HB

namespace TV {
/* --- TV detection and effects ------------------------------------------------ */
void Status();   void On();   void Off();
void LogPush(bool event, int pinBefore, int pinAtTrigger); /* push on/off event into TV::State.LOG ring-buffer */
void T_EFFECT_TV_ON(taskId_t taskId);
void T_EFFECT_TV_ON_Default(taskId_t tID);          void T_EFFECT_TV_ON_1_RandomStatic(taskId_t tID);
void T_EFFECT_TV_ON_2_MidToOutSep(taskId_t tID);   void T_EFFECT_TV_ON_3_MidToOutAll(taskId_t tID);
void T_EFFECT_TV_ON_4_5_HalfRun(taskId_t tID);     void T_EFFECT_TV_ON_6_7_MidToExt(taskId_t tID);
void T_EFFECT_TV_ON_8_ComEffect(taskId_t tID);      void T_EFFECT_TV_ON_9_QuadPointHB(taskId_t tID);
void T_EFFECT_TV_ON_10_LiquidFill(taskId_t tID);    void T_EFFECT_TV_ON_11_PixelBoot(taskId_t tID);
void T_EFFECT_H_FadeOn(taskId_t tID);     void T_EFFECT_H_CenterBloom(taskId_t tID);
void T_EFFECT_H_LinearSweep(taskId_t tID);void T_EFFECT_H_QuadPoint(taskId_t tID);
void T_EFFECT_TV_OFF(taskId_t taskId);
void T_EFFECT_TV_OFF_Default(taskId_t tID);          void T_EFFECT_TV_OFF_1_DelayWTvOff(taskId_t tID);
void T_EFFECT_TV_OFF_2_DelayAll(taskId_t tID);       void T_EFFECT_TV_OFF_3_SlowTvSequential(taskId_t tID);
void T_EFFECT_TV_OFF_4_5_Countdown(taskId_t tID);   void T_EFFECT_TV_OFF_6_RandomHalf(taskId_t tID);
void T_EFFECT_TV_OFF_7_QuadPointHB(taskId_t tID);
} // namespace TV

namespace MOTION {
/* --- Motion detection and effects -------------------------------------------- */
void    Status();   uint8_t PinStatus(int p);
void    T_EFFECT_MOTION_ON(taskId_t taskId);   bool T_EFFECT_MOTION_ON_Default();
void    T_EFFECT_MOTION_ON_1_FromMiddle(taskId_t tID);   void T_EFFECT_MOTION_ON_2_LineMoving(taskId_t tID);
void    T_EFFECT_MOTION_ON_3_Random(taskId_t tID);       void T_EFFECT_MOTION_ON_4_Cascade(taskId_t tID);
void    T_EFFECT_MOTION_ON_5_TheCollision(taskId_t tID);
void    T_EFFECT_MOTION_ON_6_RightToLeft(taskId_t tID);
void    T_EFFECT_MOTION_OFF(taskId_t taskId);   void T_MOTION_CHANGE_COLOR(taskId_t taskId);
} // namespace MOTION

namespace APP {
/* --- App UDP communication --------------------------------------------------- */
void Setup();   void Loop();   void UdpSet();
/* Display name for an APP_TRANSPORT_* value (UDP/MQTT), for log lines - defined
   later in the .ino (still inside namespace APP), but used from Exec()/
   cmdConnected() which come textually earlier - Arduino's auto-prototype
   generator doesn't reliably pick this up on its own, same class of issue as
   the other explicit forward decls in this file. */
const char* TransportName(uint8_t proto);
void Exec(char *buff, int len);   void termMsgSend(const char *msg);
void cmdConnected();
void cmdEnableDisable();
void cmdSettings(char *buff, int len);
void cmdAmbientMode(char *buff, int len);
void cmdDebug(char *buff, int len);
void cmdTestMode(char *buff, int len);
bool cmdTestMode_Diffuser(uint8_t val);
bool cmdTestMode_Lux(uint8_t level);
void cmdDiffuserTurnOn(char *buff, int len);
void cmdDiffuserParfum(char *buff, int len);
void cmdChangeBrightness(char *buff, int len);
void cmdChangeColor(char *buff, int len);
void cmdChangeDualColor(char *buff, int len, bool shakeMode);
void cmdSetLed(char *buff, int len);
void T_WatchdogCheck(taskId_t tID);
void T_KeepAlive(taskId_t tID);
uint8_t ClampByte(float v);
uint8_t HexByte(const char *hex);   uint8_t HexNibble(char c);
uint16_t HexWord(const char *hex);
void updTestMode();
bool updLink();   bool updFaults();   void Notify_Saved(uint8_t result);
void termMsgLog(uint8_t level, uint8_t source, const char *ns, const char *func, const char *format, ...);
void termMsgLogSection(uint8_t source, const char *ns, const char *func, const char *format, ...);
void termMsgLogGap(const char *ns, const char *func);
void Dispatch(char *buff, int len);
uint8_t H_ClampByte(float v);                                        /* fwd decl - defined in .ino, file-local (APP::H_ClampByte) */
uint8_t H_RssiBars(uint8_t mag);                                     /* fwd decl - defined in .ino, file-local (APP::H_RssiBars) */
static uint32_t H_ParseHexColor(const char *hex);                    /* fwd decl - defined in .ino, file-local (APP::H_ParseHexColor) */
extern char _sharedTxBuffer[APP_UDP_MAX_BUFFER_SIZE];                /* defined in .ino, file-local (APP::_sharedTxBuffer, static removed to match extern) */
extern uint8_t _binTxBuffer[APP_UDP_MAX_BUFFER_SIZE];               /* binary staging buffer for the 'LK' compressed colour packet */

/* --- 'LK' compressed colour packet ------------------------------------------ *
 * Wire: 'L' 'K' then a run of records until packet end. Body is RAW BYTES, so
 * the app receives it on a byte-level branch (never String-decoded).           *
 *   LK_OP_FILL start count r g b   -> LEDs [start, start+count) all = (r,g,b)   *
 *   LK_OP_SETN count {idx r g b}.. -> 'count' scattered pixels                  *
 * A solid strip is one FILL (~6 B) vs ~482 B of ASCII 'LC'; worst-case         *
 * scattered is 4 B/LED vs 8. Lossless -- every changed LED still ships.         */
#define LK_HDR0        'L'
#define LK_HDR1        'K'
#define LK_OP_FILL     0x01   /* start(1) count(1) r(1) g(1) b(1)                */
#define LK_OP_SETN     0x02   /* count(1) then count x { idx(1) r(1) g(1) b(1) } */
#define LK_MIN_RUN     3      /* runs >= this many equal-colour LEDs -> FILL, else SETN */

void termMsgAck(uint8_t seq, uint8_t result);
void termMsgSendBin(const uint8_t *buf, size_t len);                /* length-aware binary send (UDP write / MQTT publish) -- no strlen */
void updBrMax();
void updSettings();
void updColors_Force();
void updDeltaColors();                                             /* now just flags ColorSyncPending -- real work is flushColorSync() */
void flushColorSync(bool force);                                    /* build + send one 'LK' delta; clears only the LEDs it actually shipped */
void drainColorSync(uint32_t now);                                  /* loop hook: flush at most once per FPS period when a sync is pending */
void updStatus(const char *by);
bool updLux();
void RefreshSelectedCache();
void DiffuserParfum(char *buff, int len);   /* app 'Dp'+4-hex-minutes+1-hex-mode relay handler */
void TxCacheReset();
int  getFreeRam();
} // namespace APP

namespace DIF {
/* --- Diffuser UDP communication ------------------------------------------------ */
void        Setup();   void Loop();   void UdpSet();
void        Exec(char *buff, int len);   void Send(const char *msg);
void        TickAsyncSend();   /* non-blocking beginPacket()/write()/endPacket() step, call every loop() - see Send() */
void        SendCmd(const char *body, uint8_t relayAppSeq);   void AckParse(char *buff, int len);
void        PushPending(uint8_t cmdSeq, uint8_t appSeq);   bool PopPendingByCmdSeq(uint8_t cmdSeq, uint8_t &outAppSeq);
bool        IsAppSeqPending(uint8_t appSeq);   void FlushPending(uint8_t result);   /* small in-flight relayed-ack FIFO, see DIFx comment */
void        SendMaybeAck(const char *body);   /* envelope+relay if armed by an app command, else bare Send */
void        ParseStatus(char *buff, int len);
void        RequestStatus();   void Shutdown();
void        RequestHistory();   void RelayHistoryToApp(char *buff, int len);   /* on-demand full refill history - see cmdHistoryQuery() on the diffuser */
void        ManualRefill();     /* sends "Dr" -- tells the diffuser a manual refill just happened (banks + resets usage now) */
void        Parfum(uint16_t minutes, uint8_t mode);       /* forward "DpMMMME" to the diffuser -- 0 minutes = cancel + shutdown */
void        PollStatus();      /* sends "Dc" -- periodic poll variant of RequestStatus(), used by StatusCheck */
void        StatusCheck(taskId_t taskId);   /* locked task -- polls "Dc" every DIF_STATUS_CHECK_S */
void        IdleCheck(taskId_t taskId);     /* locked task -- single recurring task drives the whole idle-pulse state machine (wait -> on -> off), no nested task */
void        IdleTimerReset();               /* call on any source-active event to restart the idle countdown */
void        LogPush(uint8_t triggerBy, uint8_t mode, uint8_t effect); /* push auto-on event into DIF::State.LOG ring-buffer */
void        TurnOn(uint8_t mode, uint8_t effect, const DIF_Colorx *colorOverride = NULL);
void        AutoOn(uint8_t eeModeSetting, const DIF_Colorx *colorOverride = NULL);   void AutoOff();   /* activity-driven auto control */
void        PushLiveIfActive(const DIF_Colorx *colorOverride = NULL);   /* re-push mode/effect/colour now if a source is active -- call after dual-color/effect/mode changes */
bool        AnySourceActive();                /* true if TV/UDPRAW/motion/AM still active */
uint8_t     ActiveModeSetting();              /* EE_DIF_MODE_* matching the active source, or 0xFF */
const char* getModeName(uint8_t mode);   const char* getEffectName(uint8_t effect);
const char* getStripStatusName(uint8_t status);
void        ColorFromCurrentLEDs(bool dual, DIF_Colorx &out);  /* averages LED::State.CurrentColor[] -- split into halves when dual */
} // namespace DIF

namespace EE {
/* --- EEPROM persistence ------------------------------------------------------ */
void                 Read();   void Write(taskId_t taskId);   void WriteTime();
bool                 w(int index, int value);
const EE_SettingDef* getDef(uint8_t settingId);
const EE_SettingDef* getDefByIndex(uint8_t index);
const char*          getName(uint8_t settingId);
bool                 InAnyGroup(const char* name);
void                 LogSetting(const EE_SettingDef* def);
uint8_t              Get(uint8_t settingId);   bool Set(uint8_t settingId, uint8_t value);
void                 MarkChanged(uint8_t index);   bool IsChanged(uint8_t index);
int                  getChangedCount();   void ClearAllChanges();
void                 MarkColorChanged(uint8_t ledIndex);   void MarkAmbientChanged(uint8_t ledIndex);
void                 MarkUdpChanged();   void MarkMotionChanged();
} // namespace EE

namespace UDPRAW {
/* --- UDPRAW ambilight stream ------------------------------------------------- */
void Setup();   void UdpSet();   void Loop();
void Init();    void End(bool handover = true);      void T_UDPRAW_SET_COLOR(taskId_t taskId);
} // namespace UDPRAW

namespace BME {
/* --- BME280 ------------------------------------------------------------------ */
void Setup();   void Check(taskId_t taskId);
} // namespace BME

namespace NET {
/* --- Network (WiFi + NTP) ---------------------------------------------------- */
void      Setup();   IPAddress getIP();
void      Connect();   void Check(taskId_t taskId);
void      Reconnect(taskId_t taskId);   void setConnectTime(bool reset);
void      RTC_Begin();   void RTC_RetryTask(taskId_t taskId);
void      RTC_Parse(taskId_t taskId);   void RTC_Resync(taskId_t taskId);
uint32_t  RTC_EpochUTC();
bool      IsConnected();   bool      Connected_Cached();
static void WaitWithYield(uint32_t ms);                              /* fwd decl - defined in .ino, file-local (NET::WaitWithYield) */
} // namespace NET

/* =========================================================================== */
/* EFFECT DISPATCH TABLES -- function pointer typedefs + handler arrays        */
/* (moved from .ino; all referenced handlers are forward-declared above)      */
/* =========================================================================== */

/* --- Heartbeat effect dispatch -------------------------------------------- */
typedef void (*HBEffectHandler)(taskId_t);

static const HBEffectHandler HB_EFFECT_HANDLERS[] = {
    HB::T_EFFECT_HB_1_WhiteMove,              // 1: Moving white highlight
    HB::T_EFFECT_HB_2_Heartbeat,              // 2: Heartbeat pulse
    HB::T_EFFECT_HB_3_RandomFade,             // 3: Random fade
    HB::T_EFFECT_HB_4_TravelingShadow,        // 4: Traveling shadow
    HB::T_EFFECT_HB_5_ExpandingRaindrops,     // 5: Expanding raindrops
    HB::T_EFFECT_HB_6_Colors,                 // 6: Color cycling
    HB::T_EFFECT_HB_7_ShootingStarRandom,     // 7: Shooting star
    HB::T_EFFECT_HB_8_RandomSparklingPop,     // 8: Random sparkling
    HB::T_EFFECT_HB_9_GlidingAurora,          // 9: Gliding aurora
    HB::T_EFFECT_HB_10_TheGlitchMatrix,       // 10: Glitch matrix
    HB::T_EFFECT_HB_11_StochasticPlasma,      // 11: Stochastic plasma
    HB::T_EFFECT_HB_12_DigitalRain,           // 12: Digital rain
    HB::T_EFFECT_HB_13_DualPulse,             // 13: Dual pulse
    HB::T_EFFECT_HB_14_RainbowWavePulse       // 14: Rainbow wave pulse (last)
};
static const int HB_EFFECT_HANDLERS_COUNT = sizeof(HB_EFFECT_HANDLERS) / sizeof(HB_EFFECT_HANDLERS[0]);

/* --- TV on/off + Heartbeat-on effect dispatch ----------------------------- */
typedef void (*TVEffectHandler)(taskId_t);

static const TVEffectHandler TV_ON_HANDLERS[] = {
    TV::T_EFFECT_TV_ON_Default,       // 0: Default fade-in
    TV::T_EFFECT_TV_ON_1_RandomStatic,
    TV::T_EFFECT_TV_ON_2_MidToOutSep,
    TV::T_EFFECT_TV_ON_3_MidToOutAll,
    TV::T_EFFECT_TV_ON_4_5_HalfRun,    // 4: Cascade half
    TV::T_EFFECT_TV_ON_4_5_HalfRun,    // 5: Cascade half (alias)
    TV::T_EFFECT_TV_ON_6_7_MidToExt,   // 6: Mid to Ext Vertical
    TV::T_EFFECT_TV_ON_6_7_MidToExt,   // 7: Mid to Ext Horizontal
    TV::T_EFFECT_TV_ON_8_ComEffect,
    TV::T_EFFECT_TV_ON_9_QuadPointHB,
    TV::T_EFFECT_TV_ON_10_LiquidFill,  // 10: Liquid fill bottom-to-top
    TV::T_EFFECT_TV_ON_11_PixelBoot    // 11: Pixel boot sequence
};
static const int TV_ON_HANDLERS_COUNT = sizeof(TV_ON_HANDLERS) / sizeof(TV_ON_HANDLERS[0]);

static const HBEffectHandler HB_ON_HANDLERS[] = {
    TV::T_EFFECT_H_FadeOn,       // 0: Default fade-in
    TV::T_EFFECT_H_CenterBloom,  // 1: Center-outward bloom
    TV::T_EFFECT_H_LinearSweep,  // 2: Sequential linear sweep
    TV::T_EFFECT_H_QuadPoint     // 3: Quad-anchor expansion
};
static const int HB_ON_HANDLERS_COUNT = sizeof(HB_ON_HANDLERS) / sizeof(HB_ON_HANDLERS[0]);

static const TVEffectHandler TV_OFF_HANDLERS[] = {
    TV::T_EFFECT_TV_OFF_Default,         // 0: Default fade-off
    TV::T_EFFECT_TV_OFF_1_DelayWTvOff,   // 1: Delay (w tv off)
    TV::T_EFFECT_TV_OFF_2_DelayAll,      // 2: Delay (all)
    TV::T_EFFECT_TV_OFF_3_SlowTvSequential, // 3: Slow TV Sequential
    TV::T_EFFECT_TV_OFF_4_5_Countdown,   // 4: Countdown
    TV::T_EFFECT_TV_OFF_4_5_Countdown,   // 5: Bomb Countdown (alias)
    TV::T_EFFECT_TV_OFF_6_RandomHalf,    // 6: Random Half
    TV::T_EFFECT_TV_OFF_7_QuadPointHB    // 7: Quad Point HB
};
static const int TV_OFF_HANDLERS_COUNT = sizeof(TV_OFF_HANDLERS) / sizeof(TV_OFF_HANDLERS[0]);

/* --- Motion effect dispatch ----------------------------------------------- */
typedef void (*MotionEffectHandler)(taskId_t);

namespace MOTION {
static void T_EFFECT_MOTION_ON_Default_Wrapper(taskId_t taskId);  /* fwd decl - defined in .ino, file-local */
} // namespace MOTION

static const MotionEffectHandler MOTION_ON_HANDLERS[] = {
    MOTION::T_EFFECT_MOTION_ON_Default_Wrapper,   // 0: Default center-out fade
    MOTION::T_EFFECT_MOTION_ON_1_FromMiddle,      // 1: Middle-out expansion
    MOTION::T_EFFECT_MOTION_ON_2_LineMoving,      // 2: Line sweep animation
    MOTION::T_EFFECT_MOTION_ON_3_Random,          // 3: Random sparkle effect
    MOTION::T_EFFECT_MOTION_ON_4_Cascade,         // 4: Waterfall/cascade effect
    MOTION::T_EFFECT_MOTION_ON_5_TheCollision     // 5: Collision/bounce effect
};
static const int MOTION_ON_HANDLERS_COUNT = sizeof(MOTION_ON_HANDLERS) / sizeof(MOTION_ON_HANDLERS[0]);

#endif /* _FuZzAPP_SmartTV_R4_DEF_h */

