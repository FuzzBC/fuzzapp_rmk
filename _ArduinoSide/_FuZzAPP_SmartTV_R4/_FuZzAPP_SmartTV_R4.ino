/* ------------------------------------------------------------------------ */
/* _FuZzAPP_SmartTV_R4.ino  -- Consolidated Single-File Build              */
/* FuZzAPP SmartTV R4 -- Arduino UNO R4 WiFi                                  */
/* ------------------------------------------------------------------------ */
/*
/*  All module implementations merged here in dependency order.
/*
/*  MODULE ORDER
/*    1. GLOBALS       -- all module-level variable definitions
/*    2. SETUP / LOOP  -- sketch entry points
/*    3. DEBUG         -- _print, formatMSG, _Debug
/*    4. TASK          -- scheduler wrappers + T_AMBIENT_MODE_ON / T_END_TEST_MODE
/*    5. LED           -- zone helpers, pixel ops, LED transition tasks
/*    6. LISENS        -- ambient light sensor + lux callbacks
/*    7. HB            -- heartbeat strip effects 1-14
/*    8. TV            -- detection, On/Off, 9 on-effects, 7 off-effects, HB helpers
/*    9. MOTION        -- PIR detection, 5 on-effects, off task, color change task
/*   10. APP           -- UDP command socket + all handlers
/*   11. DIF           -- diffuser UDP link (port 8439) -- send Ds/Df/Dn, parse status
/*   12. EE            -- EEPROM persistence (read/write/settings table)
/*   13. UDPRAW        -- ambilight stream receiver
/*   14. BME280        -- temperature/humidity sensor
/*   15. NET           -- WiFi + NTP lifecycle
/*   16. MQTT            -- HiveMQ Cloud alternate to the APP UDP protocol; always compiled, kept last.
/*                         Only ONE of UDP/MQTT is ever the active transport (APP::State.ActiveProtocol) --
/*                         the idle one is watched only for a 'Z' welcome, which is how the app requests
/*                         a switch. See Exec() and termMsgSend() in the APP module.
 */

#include "_FuZzAPP_SmartTV_R4_DEF.h"

/* MQTT (HiveMQ Cloud) config, MQTTx typedef, and forward decls moved to DEF.h
   (defined in DEF.h; MQTT is always compiled in). */


/* ------------------------------------------------------------------------ */
/* GLOBALS -- module-level variable definitions                                */
/* ------------------------------------------------------------------------ */


/* --- Task engine -------------------------------------------------------- */

uint32_t   TimeNow;

/* --- Shared WiFi status cache --------------------------------------------
 * bool/uint32_t definitions below back the extern globals declared in DEF.h;
 * NET_CHECK_INTERVAL is defined in DEF.h, NET::Connected_Cached() below. */
bool       NET_Connected   = false;
uint32_t   NET_LastCheck   = 0;
TASKx      TASK;
TaskJockeyMod _TASK;
__testmode TestMode     = _testmode_none;
taskId_t   TestMode_tID = TASK_ID_NONE;


/* --- LED ---------------------------------------------------------------- */

namespace LED { LEDx State; }

/* NeoPixel strips for each physical LED group */
Adafruit_NeoPixel stripFront(LED_START_I_UCOM + LED_UCOM_NUM, LED_PIN_FRONT, NEO_BRG + NEO_KHZ800);
Adafruit_NeoPixel stripBack(LED_BED_NUM + LED_LAMP_NUM, LED_PIN_BACK, NEO_BRG + NEO_KHZ800);
Adafruit_NeoPixel stripHB(LED_HB_NUM, LED_PIN_HB, NEO_GRB + NEO_KHZ800);


/* --- Light sensor ------------------------------------------------------- */

const unsigned int LIGHT_SENS_LUX[3] = {45, 75, 125};
namespace LISENS { LISENSx State; }


/* --- Heartbeat ---------------------------------------------------------- */

namespace HB { HBx State; }


/* --- TV ----------------------------------------------------------------- */

namespace TV { TVx State; }


/* --- Motion ------------------------------------------------------------- */

namespace MOTION { MOTIONx State; }


/* --- App ---------------------------------------------------------------- */

namespace APP { ARDx Ard; }
namespace APP { AMx Am; }
namespace APP { APPx State = {{0}, {0}, 0, true};   /* selectedCacheDirty = true */ }
WiFiUDP   APP_UDP;
IPAddress APP_RECV_IP(0, 0, 0, 0);


/* --- Diffuser (DIF) ----------------------------------------------------- */

namespace DIF { DIFx State = {};   /* value-init: all scalars 0, IdlePulseActive false, LOG[].epoch == 0 */ }
WiFiUDP   DIF_UDP;
IPAddress DIF_TARGET_IP(192, 168, 1, 203);


/* --- EEPROM ------------------------------------------------------------- */

/* fps:                             15   25   30   60   90   120  150  200  240 */
const uint8_t LED_FPS_TABLE[LED_FPS_OPTIONS_TOTAL] PROGMEM = { 67,  40,  33,  17,  11,  8,   7,   5,   4  };

namespace EE { EEx State; }
uint8_t EE_SET[EE_MEM_X];
uint8_t EE_Changed[(EE_MEM_X + 7) / 8] = {0};
uint8_t EE_ColorChanged[(LED_NUM + 7) / 8] = {0};  /* bitfield: 1 bit per LED */
uint8_t EE_AmbientChanged[(LED_NUM + 7) / 8] = {0}; /* bitfield: 1 bit per LED */
bool    EE_UdpChanged    = false;
bool    EE_MotionChanged = false;


/* --- UDPRAW ------------------------------------------------------------- */

const int UDPRAW_BuffSize = 100;
uint8_t   UDPRAW_Buffer[100];
namespace UDPRAW { UDPRAWx State; }
WiFiUDP   UDPRAW_UDP;


/* --- BME280 ------------------------------------------------------------- */

namespace BME { BME280x State; }
BME280I2C::Settings settings(
    BME280::OSR_X16, BME280::OSR_X1, BME280::OSR_X1,
    BME280::Mode_Normal, BME280::StandbyTime_500us,
    BME280::Filter_16, BME280::SpiEnable_False,
    BME280I2C::I2CAddr_0x76
);
BME280I2C bme280sensor;


/* --- Network ------------------------------------------------------------ */

const char WIFI_SSID[] PROGMEM = "FuZz";
const char WIFI_PASS[] PROGMEM = "REDACTED-WIFI-PASSWORD";
namespace NET { NETx Wifi; }
namespace NET { DATEx Date; }
WiFiUDP   RTC_UDP;
NTPClient RTC_TimeClient(RTC_UDP);
uint8_t   NET_WifiSt = netWifiOK;
uint8_t   RTC_Status = rtcOK;


/* --- MQTT (HiveMQ Cloud) ------------------------------------------------ */
namespace MQTT { MQTTx State = {};                 /* value-init: Up=false, no pending */ }
WiFiSSLClient MQTT_Net;                  /* TLS socket (ESP32-S3 handles CA) */
PubSubClient  MQTT_Cli(MQTT_Net);        /* MQTT over the TLS socket         */
namespace MQTTCRED { MQTTCREDx State = {};         /* value-init: empty strings, valid=false until Load()/verify */ }


/* ------------------------------------------------------------------------ */
/* SETUP / LOOP                                                               */
/* Sketch entry points                                                        */
/* ------------------------------------------------------------------------ */

void setup() {
    Serial.begin(SERIAL_BAUD);
    randomSeed(analogRead(A0));
    delay(2000);   // wait for Serial Monitor

    PRNT::_print(PRNT::formatMSG("~ SETUP ~" NL));

    NET::Setup();           // WiFi connect (10 s) + async NTP
    NET::RTC_Begin();       // NTP sync (must follow WiFi)
    UDPRAW::Setup();         // ambilight UDP socket port 5568
    APP::Setup();            // phone UDP socket port 8472
    DIF::Setup();            // diffuser UDP socket port 8439
    MQTTCRED::Load();        // cached MQTT user/pass, own EEPROM anchor (see MQTTCRED_BLOCK_SIZE)
    MQTT::Setup();           // HiveMQ Cloud TLS link (parallel to UDP)
    BME::Setup();         // I^2C temperature/humidity
    LISENS::Setup();         // analog ambient light sensor
    EE::Read();              // load persisted settings

    LED::Setup();            // NeoPixel strips + shuffle + refresh task

    pinMode(MOTION_PIN_COM, INPUT);
    pinMode(MOTION_PIN_BED, INPUT_PULLUP);

    TSK::AddTask("Setup", "APP_Watchdog",  APP::T_WatchdogCheck, TASK_MS, APP_WATCHDOG_MS, 0, true);
    // KeepAlive is NOT registered here -- it's added on the app's welcome
    // (Connected) and killed when the app is suspended (T_KeepAlive).

    delay(1000);

    /* Uncomment for full EEPROM dump on startup:
    _print(formatMSG("EEPROM MEM %d" NL, EEPROM.length()));
    for (int i = 0; i < EEPROM.length(); i++)
        _print(formatMSG("EEPROM POS: %d - Val %d" NL, i, EEPROM.read(i)));
    _Debug(Serial);
    */

    PRNT::_print(PRNT::formatMSG("~ LOOP ~" NL));
}

void loop() {
    #ifdef ENABLE_EXECUTIONTIME
        uint32_t startLoop = micros();
        uint32_t t_app, t_dif, t_mqtt, t_task, t_udp, t_tv, t_mot, t_show;
    #endif

    TimeNow = millis();   // sync global time snapshot

    #ifdef ENABLE_EXECUTIONTIME
        uint32_t s = micros();
        APP::Loop();
        t_app = micros() - s;

        s = micros();
        DIF::Loop();
        DIF::TickAsyncSend();   // advance any staged diffuser send one non-blocking step - see DIF::Send()
        t_dif = micros() - s;

        s = micros();
        MQTT::Loop();
        t_mqtt = micros() - s;

        s = micros();
        _TASK.runTasks();
        t_task = micros() - s;
    #else
        APP::Loop();
        DIF::Loop();
        DIF::TickAsyncSend();   // advance any staged diffuser send one non-blocking step - see DIF::Send()
        MQTT::Loop();
        _TASK.runTasks();
    #endif

    if (LED::State.Enabled) {
        #ifdef ENABLE_EXECUTIONTIME
            s = micros();
            UDPRAW::Loop();
            t_udp = micros() - s;

            s = micros();
            TV::Status();
            t_tv = micros() - s;
        #else
            UDPRAW::Loop();
            TV::Status();
        #endif

        if (!UDPRAW::State.Status) {
            #ifdef ENABLE_EXECUTIONTIME
                s = micros();
                MOTION::Status();
                t_mot = micros() - s;

                t_show = 0;
                if (LED::ShouldRefresh(TimeNow)) {
                    s = micros();
                    LED::Refresh(TimeNow);
                    t_show = micros() - s;
                }
            #else
                MOTION::Status();
                if (LED::ShouldRefresh(TimeNow)) {
                    LED::Refresh(TimeNow);
                }
            #endif
        }
    }

    // Coalesced app colour sync: flush at most one 'LK' packet per FPS period,
    // draining every LED marked dirty since the last flush (see updDeltaColors).
    APP::drainColorSync(TimeNow);

    #ifdef ENABLE_LOG_RAM_MONITOR
        // Continuous free-RAM watch, independent of the on-demand K0D debug
        // dump - added to catch a leak while stress-testing rapid-fire Dual
        // Color (LD/Ld) commands. baseline = first reading after boot, so
        // "delta" reads negative and keeps growing if something is leaking;
        // it should hover near 0 (noise only) under normal operation, the
        // same way idle RAM was confirmed rock-stable during the earlier
        // Collision-effect investigation.
        static uint32_t ramLogAt   = 0;
        static int      ramBaseline = -1;
        if (TimeNow - ramLogAt >= RAM_MONITOR_INTERVAL_MS) {
            ramLogAt = TimeNow;
            int freeRam = APP::getFreeRam();
            if (ramBaseline < 0) ramBaseline = freeRam;   // first reading = baseline
            PRNT::_print(PRNT::formatMSG(
                "%~32s : free [%d] B of [%d] B | vs baseline [%d] B" NL,
                "RAM_Monitor", freeRam, ARD_RAM_TOTAL, freeRam - ramBaseline));

            #ifdef TASKJOCKEY_ENABLE_DIAGNOSTICS
                // Task-table health alongside RAM, same cadence - a task-count
                // climb (vs its own high-water mark) or a handler max-duration
                // spike points straight at which task is misbehaving, instead
                // of having to guess from RAM alone. See TaskJockeyMod.h.
                PRNT::_print(PRNT::formatMSG(
                    "%~32s : count [%d] | peak [%d] | created [%lu]" NL,
                    "Task_Monitor", _TASK.getTaskCount(), _TASK.getMaxTaskCountEverSeen(), _TASK.getTotalTasksCreated()));
                uint8_t tCount = _TASK.getTaskCount();
                for (uint8_t i = 0; i < tCount; i++) {
                    taskId_t tid = _TASK.getTaskIdAt(i);
                    if (tid == TASK_ID_NONE) continue;
                    PRNT::_print(PRNT::formatMSG(
                        "%~32s :   [%s] last [%lu] us | max [%lu] us" NL,
                        "Task_Monitor", _TASK.getTaskName(tid),
                        _TASK.getTaskLastDurationUs(tid), _TASK.getTaskMaxDurationUs(tid)));
                }
            #endif
        }
    #endif

    #ifdef ENABLE_EXECUTIONTIME
        uint32_t totalLoop = micros() - startLoop;
        static uint32_t lastLog = 0;
        if (millis() - lastLog > 500) {
            PRNT::_print(PRNT::formatMSG("--- EXECUTION TIMES (us) ---" NL));
            PRNT::_print(PRNT::formatMSG("APP: %d | DIF: %d | MQTT: %d | TASK: %d | UDP: %d" NL, t_app, t_dif, t_mqtt, t_task, t_udp));
            PRNT::_print(PRNT::formatMSG("TV:  %d | MOT:  %d | SHOW: %d" NL, t_tv,  t_mot,  t_show));
            PRNT::_print(PRNT::formatMSG("TOTAL LOOP: %d us" NL, totalLoop));
            lastLog = millis();
        }
    #endif
}


/* ------------------------------------------------------------------------ */
/* DEBUG                                                                      */
/* _print - formatMSG - _Debug                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Send a detailed debug report for the given module to the phone app.
 *
 * Prints the current timestamp first, then a formatted block of diagnostics
 * for the module selected by @p d (one of the __debug enum values).
 *
 * @param  d  Debug target -- one of the __debug enum values (DEF.h).
 *
 * @note   Output goes through APP::termMsgSend(), mirrored to both the UDP
 *         peer (APP_RECV_IP) and the MQTT_TOPIC broker topic.
 */
/* Name prefixes used to group the EEPROM dump by owning module -- moved to DEF.h
   (EE_GROUPS[], EE_GROUP_NAMES[], EE_GROUP_COUNT). InAnyGroup()/LogSetting()
   implementations moved to the main EE:: block; forward-declared in DEF.h. */


/* ------------------------------------------------------------------------ */
/* TASK                                                                       */
/* Scheduler helpers - task add / kill / interval / reset / handle logging    */
/* ------------------------------------------------------------------------ */

namespace TSK {

/**
 * @brief  Register a new repeating or one-shot task with the TaskJockey scheduler.
 *
 * @param  source          Label of the calling function (for log output only).
 * @param  call_func_name  Name of the callback function (for log output only).
 * @param  CALL_Func       Function pointer: void(*)(taskId_t) -- the task callback.
 * @param  Interval        Repeat interval in ms. 0 = run once.
 * @param  StartTime       Delay before the first execution in ms. 0 = run immediately.
 * @param  Locked          If true, the task survives KillTasksAvoidLocked().
 *
 * @return Task ID assigned by the scheduler. Store this to kill or reconfigure the task.
 *
 * Called by: ~37 call sites across almost every task-registering function in
 * the file (TV::On/Off, MOTION::Status, DIF::Setup/StatusCheck/IdleCheck,
 * cmdChangeColor/cmdChangeDualColor, etc.) - not enumerated further since the
 * caller already self-reports via the `source` parameter, visible directly
 * in the ENABLE_LOG_TASK_VERBOSE log line below.
 */
// New API: unit before Interval
taskId_t AddTask(const char* source, const char* call_func_name, void (*CALL_Func)(taskId_t), taskUnit_t unit, uint32_t Interval, uint32_t StartTime, bool Locked) {
    // Add the task to the internal tasker with specified unit
    // The name is stored by pointer, not copied: every call site passes a
    // string literal, so it outlives the task.
    taskId_t id = _TASK.addTask(CALL_Func, call_func_name, unit, Interval, StartTime, Locked); // Core logic - Action

    // Enhanced Log: Link the ID to the function name and source
    #ifdef ENABLE_LOG_TASK_VERBOSE
        PRNT::_print(PRNT::formatMSG("%~32s : ID:%d | Func:%s | Src:%s | Int:%d | Locked:%T" NL, "TASK_AddTask", id, call_func_name, source, Interval, Locked));    // Info log - Output
    #endif

    return id;                                                          // Return ID - State
}

/**
 * @brief  Kill a single task by its ID.
 *
 * @param  tID     Task ID returned by AddTask(). Safe to call with an invalid ID.
 * @param  source  Label of the calling function (used in task-info log output).
 *
 * Called by: ~11 call sites (e.g. DIF's relayed-command timeout paths,
 * cmdTestMode()'s T_END_TEST_MODE cleanup) - the caller self-reports via
 * `source`, visible in the ENABLE_LOG_TASK_VERBOSE log line below.
 */
void KillID(taskId_t tID, const char* source) {
    // Enhanced Log: Record which source is killing which task ID
    #ifdef ENABLE_LOG_TASK_VERBOSE
        PRNT::_print(PRNT::formatMSG("%~32s : ID:%d | ReqBy:%s" NL, "TASK_KillID", tID, source)); // Kill log - Output
    #endif
    
    _TASK.killTask(tID);                                                // Core logic - Action
}

/**
 * @brief  Kill all currently running unlocked tasks.
 *
 * Locked tasks (registered with Locked=true) are preserved.
 * Call before launching a new major effect to clear any competing animations.
 *
 * @param  source  Label of the calling function (used in task-info log output).
 *
 * Called by: ~27 call sites - every TV::On/Off, MOTION::Status transition,
 * cmdChangeColor()/cmdChangeDualColor()/cmdSetLed(), DIF turn-on/off, and
 * effect-starting path that needs to clear whatever animation is currently
 * running first. The caller self-reports via `source`, visible in the
 * ENABLE_LOG_TASK_VERBOSE log line below.
 */
void KillTasksAvoidLocked(const char* source) {
	// * LOG
    #ifdef ENABLE_LOG_TASK_VERBOSE
	    PRNT::_print(PRNT::formatMSG("%~32s : kill all tasks requested by: [%s]" NL, "TASK_KillTasksAvoidLocked", source)); // Log the source - Sync
    #endif

	_TASK.killAllTasks_avoidlocked();
}
/**
 * @brief  Report a task handle and whether it is currently registered.
 *
 * @param  label  What the task does.
 * @param  id     Task handle; TASK_ID_NONE means not running.
 */
static void LogHandle(const char* label, taskId_t id) {
	if (id == TASK_ID_NONE) {
		APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "TSK", "LogHandle", "%s [OFF]", label);
		return;
	}
	// A handle pointing at a task the scheduler no longer holds reads back as
	// TASK_NAME_NULL - that mismatch is the interesting case.
	APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "TSK", "LogHandle", "%s [ON] id [%d] task [%s]",
		label, (int)id, _TASK.getTaskName(id));
}

/**
 * @brief  Restart a task's countdown so its next run is a full interval from now.
 *
 * Used to defer a periodic task while activity keeps arriving (e.g. the app
 * keep-alive: every inbound packet pushes the next 'k' out by one interval, so
 * it only fires after a genuine gap). Mirrors LISENS::ResetTime().
 *
 * @param  tID  Task id; TASK_ID_NONE is ignored (safe no-op when not scheduled).
 */
void ResetTime(taskId_t tID) {
    if (tID == TASK_ID_NONE) return;                                // Nothing scheduled - Logic
    #ifdef ENABLE_LOG_TASK_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : ID:[%d] timer reset" NL, "TASK_ResetTime", tID));
    #endif
    _TASK.resetTaskTimer(tID);                                      // Core logic - Action
}


/**
 * @brief  Change the repeat interval of a running task.
 *
 * @param  source    Label of the calling function (for log output only).
 * @param  tID       Task ID to reconfigure.
 * @param  Interval  New repeat interval in ms.
 *
 * @note   Useful inside effect callbacks (T_EFFECT_*) to vary animation speed
 *         between phases (e.g. slow during pause, fast during animation).
 */
// New API: unit before Interval
void setTaskInterval(const char* source, taskId_t tID, taskUnit_t unit, uint32_t Interval) {
    // Enhanced Log: Record who changed the speed and the new interval value
    #ifdef ENABLE_LOG_TASK_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : ID:[%d] interval changed to [%d] requested by [%s]" NL, "TASK_SetTaskInterval", tID, Interval, source));
    #endif
    _TASK.setTaskInterval(tID, unit, Interval);                     // Core logic - Action with specified unit
}
} // namespace TSK

namespace PRNT {
void _Debug(uint8_t d) {

	// -- Report header: firmware identity + device clock --
	APP::termMsgLogSection(APP_SRC_SYS, "PRNT", "_Debug", "%s", FW_NAME);
	APP::termMsgLogSection(APP_SRC_RTC, "PRNT", "_Debug", "Debug report  %s%d:%s%d:%s%d  %s%d-%s%d-%d",
		TIMEFIELD(NET::Date.time, _HH),
		TIMEFIELD(NET::Date.time, _MI),
		TIMEFIELD(NET::Date.time, _SS),
		TIMEFIELD(NET::Date.time, _DD),
		TIMEFIELD(NET::Date.time, _MM),
		NET::Date.time[_YY] + 2000);

	switch (d) {

		case _debug_led_info:
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  wiring");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Front pin [%d]",       LED_PIN_FRONT);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Back pin [%d]",        LED_PIN_BACK);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Heartbeat pin [%d]",   LED_PIN_HB);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Pixel count [%d]",     LED_NUM);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Pixels total [%d]",    LED_NUM_TOTAL);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Heartbeat pixels [%d]", LED_HB_NUM);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Brightness max [%d]",  LED_BRIGHTNESS_MAX);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Refresh [%d] ms",      LED_REFRESH_TIME);

			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Strip enabled [%s]",   (LED::State.Enabled)     ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Needs update [%s]",    (LED::State.NeedsUpdate) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Delta mode [%s]",      (LED::State.DeltaMode)   ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Dirty pixels [%d]",    LED::getChangedCount());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Lux adapt factor [%d] %%", LED::getLuxAdaptFactor());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Last update [%l] ms ago",  TimeNow - LED::State.LastUpdateTime);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "Last refresh [%l] ms ago", TimeNow - LED::State.LastRefreshTime);
		break;

		case _debug_led_selected:
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  selection");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Cached count [%d]",  APP::State.SelectedCount);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Cache dirty [%s]",   (APP::State.SelectedCacheDirty) ? "ON" : "OFF");
			for (int i = 0; i < LED_NUM; i++) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "LED [%~2d] selected [%s]",
					i, (LED::IsSelected(i)) ? "ON" : "OFF");
			}
		break;

		case _debug_led_order:
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  pixel order");
			for (int i = 0; i < LED_NUM - LED_HB_NUM_FAKE; i++) {
				APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Index [%~2d] pixel [%~2d]", i, LED::State.PixelOrder[i]);
			}
			APP::termMsgLogSection(APP_SRC_HB, "PRNT", "_Debug", "HEARTBEAT  pixel order");
			for (int i = 0; i < LED_HB_NUM; i++) {
				APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "PRNT", "_Debug", "Index [%~2d] pixel [%~2d]", i, LED::State.HeartbeatOrder[i]);
			}
		break;

		case _debug_led_color:
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  live colour");
			for (int i = 0; i < LED_NUM_TOTAL; i++) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "LED [%~2d] RGB [%d,%d,%d] brightness [%d]",
					i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b,
					LED::State.CurrentBrightness[i]);
			}
			// Hardware buffer - what NeoPixel actually holds. A mismatch with the
			// values above means a sync or ordering fault, not a colour fault.
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  hardware buffer");
			for (int i = 0; i < LED_NUM_TOTAL; i++) {
				CRGB hw = LED::H_readStripPixel(i);
				APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "Pixel [%~2d] RGB [%d,%d,%d]",
					i, hw.r, hw.g, hw.b);
			}
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  change flags");
			for (int i = 0; i < LED_NUM; i++) {
				APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "PRNT", "_Debug", "LED [%~2d] changed [%s]",
					i, (LED::IsChanged(i)) ? "ON" : "OFF");
			}
		break;

		case _debug_led_tempcolor:
			APP::termMsgLogSection(APP_SRC_LED, "PRNT", "_Debug", "LED  target colour");
			for (int i = 0; i < LED_NUM_TOTAL; i++) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "LED [%~2d] RGB [%d,%d,%d]",
					i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b);
			}
		break;

		case _debug_motion:
			APP::termMsgLogSection(APP_SRC_MOTION, "PRNT", "_Debug", "MOTION  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "PRNT", "_Debug", "Com pin [%d]",        MOTION_PIN_COM);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "PRNT", "_Debug", "Bed pin [%d]",        MOTION_PIN_BED);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "PRNT", "_Debug", "Check every [%d] ms", MOTION_CHECK_TIME);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "PRNT", "_Debug", "Time fix [%d] ms",    MOTION_TIME_FIX);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "PRNT", "_Debug", "Log slots [%d]",      MOTION_LOG_INDEX_MAX);

			APP::termMsgLogSection(APP_SRC_MOTION, "PRNT", "_Debug", "MOTION  settings");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Brightness [%d]",       EE::Get(EE_MOTION_BRIGHTNESS));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "On time [%d] s",        EE::Get(EE_MOTION_ON_TIME));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "On effect [%d]",        EE::Get(EE_MOTION_ON_EFF));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Random colour [%s]",    EE::Get(EE_MOTION_RANDOM_COLOR) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Divide brightness [%s]", EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Renew colour [%d] s",   EE::Get(EE_MOTION_RENEW_COLOR_TIME));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Auto-off after [%d] min", EE::Get(EE_MOTION_AUTO_OFF_TIME));

			APP::termMsgLogSection(APP_SRC_MOTION, "PRNT", "_Debug", "MOTION  state");
			if (!EE::Get(EE_MOTION_RANDOM_COLOR)) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Colour [%d,%d,%d]",
					MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b);
			} else {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Colour [RANDOM]");
			}
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Status [%s]",
				(MOTION::State.Status == motOFF)     ? "OFF"     :
				(MOTION::State.Status == motON)      ? "ON"      :
				(MOTION::State.Status == motAUTOOFF) ? "AUTOOFF" :
				(MOTION::State.Status == motCOM)     ? "COM"     :
				(MOTION::State.Status == motBED)     ? "BED"     : "UNKNOWN");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Triggered [%s] by [%s]",
				(MOTION::State.Trigger) ? "ON" : "OFF",
				(MOTION::State.TriggerBy == motCOM) ? "COM" : (MOTION::State.TriggerBy == motBED) ? "BED" : "none");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Last trigger [%l] ms ago",  TimeNow - MOTION::State.TriggerTime);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Last check [%l] ms ago",    TimeNow - MOTION::State.LastCheck);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Colour renewed [%l] ms ago", TimeNow - MOTION::State.LastChangeColor);
			if (MOTION::State.AutoOffTime > TimeNow) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Auto-off in [%l] ms", MOTION::State.AutoOffTime - TimeNow);
			} else {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Auto-off armed [OFF]");
			}

			APP::termMsgLogSection(APP_SRC_MOTION, "PRNT", "_Debug", "MOTION  trigger log");
			for (int i = 0; i < MOTION_LOG_INDEX_MAX; i++) {
				if (MOTION::State.LOG[i].epoch > 0) {
					const uint32_t _ep = MOTION::State.LOG[i].epoch;
					APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "[%s%d:%s%d:%s%d] [%s%d-%s%d-%d] by [%s]",
						TIMEVAL(hour(_ep)),
						TIMEVAL(minute(_ep)),
						TIMEVAL(second(_ep)),
						TIMEVAL(day(_ep)),
						TIMEVAL(month(_ep)),
						year(_ep),
						(MOTION::State.LOG[i].TriggerBy == motCOM) ? "COM" : "BED");
				}
			}
		break;

		case _debug_tv:
			APP::termMsgLogSection(APP_SRC_TV, "PRNT", "_Debug", "TV  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "PRNT", "_Debug", "Pin [%d]",           TV_PIN);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "PRNT", "_Debug", "Debounce [%d] ms",   TV_DEBOUNCE_TIME);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "PRNT", "_Debug", "Read every [%d] ms", TV_READ_STATUS_TIME);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "PRNT", "_Debug", "Log slots [%d]",     TV_LOG_INDEX_MAX);

			APP::termMsgLogSection(APP_SRC_TV, "PRNT", "_Debug", "TV  settings");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "On effect [%d]",     EE::Get(EE_TV_ON_EFF));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Off effect [%d]",    EE::Get(EE_TV_OFF_EFF));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Off delay [%d] s",   EE::Get(EE_TV_OFF_TIME));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Brightness TV [%d]",   EE::Get(EE_TV_BR_TV));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Brightness com [%d]",  EE::Get(EE_TV_BR_COM));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Brightness ucom [%d]", EE::Get(EE_TV_BR_UCOM));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Brightness bed [%d]",  EE::Get(EE_TV_BR_BED));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Brightness lamp [%d]", EE::Get(EE_TV_BR_LAMP));

			APP::termMsgLogSection(APP_SRC_TV, "PRNT", "_Debug", "TV  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Status [%s]",        (TV::State.Status)     ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Last status [%s]",   (TV::State.LastStatus) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Read pin [%s]",      (TV::State.ReadPin)    ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Pin value [%d] previous [%d]", TV::State.PinValue, TV::State.PrevPinValue);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Last read [%l] ms ago",     TimeNow - TV::State.LastReadStatus);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Last debounce [%l] ms ago", TimeNow - TV::State.LastDebounceTime);
			if (TV::State.CountdownTimer > TimeNow) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Off countdown [%l] ms", TV::State.CountdownTimer - TimeNow);
			} else {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "Off countdown [OFF]");
			}

			APP::termMsgLogSection(APP_SRC_TV, "PRNT", "_Debug", "TV  event log");
			for (int i = 0; i < TV_LOG_INDEX_MAX; i++) {
				if (TV::State.LOG[i].epoch > 0) {
					const uint32_t _ep = TV::State.LOG[i].epoch;
					APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug",
						"[%s%d:%s%d:%s%d] [%s%d-%s%d-%d] event [%s] pin before [%d] at trigger [%d]",
						TIMEVAL(hour(_ep)),
						TIMEVAL(minute(_ep)),
						TIMEVAL(second(_ep)),
						TIMEVAL(day(_ep)),
						TIMEVAL(month(_ep)),
						year(_ep),
						(TV::State.LOG[i].Event) ? "ON" : "OFF",
						TV::State.LOG[i].PinValueBefore,
						TV::State.LOG[i].PinValueAtTrigger);
				}
			}
		break;

		case _debug_ee:
			// Read data from eeprom:
			EE::Read();

			APP::termMsgLogSection(APP_SRC_EE, "PRNT", "_Debug", "EEPROM  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "PRNT", "_Debug", "Start index [%d]",     EE_START_READ_INDEX);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "PRNT", "_Debug", "Slots [%d]",           EE_MEM_X);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "PRNT", "_Debug", "Save after [%d] s",    EE_SAVE_TIME / 1000);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "PRNT", "_Debug", "Chunk delay [%d] ms",  EE_SAVE_DELAY_BETWEEN_CHUNKS);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "PRNT", "_Debug", "Write cursor [%d]",    EE::State.Index);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "PRNT", "_Debug", "Task id [%d]",         (int)EE::State.tID);

			// Pending writes - anything listed here has not reached EEPROM yet.
			APP::termMsgLogSection(APP_SRC_EE, "PRNT", "_Debug", "EEPROM  pending writes");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "PRNT", "_Debug", "Udpraw block [%s]",  (EE_UdpChanged)    ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "PRNT", "_Debug", "Motion block [%s]",  (EE_MotionChanged) ? "ON" : "OFF");
			for (int i = 0; i < EE_MEM_X; i++) {
				if (BIT_TEST(EE_Changed, i)) {
					const EE_SettingDef* def = EE::getDefByIndex(i);
					APP::termMsgLog(APP_LOG_WRN, APP_SRC_EE, "PRNT", "_Debug", "Setting dirty [%s]", def ? def->name : "UNKNOWN");
				}
			}
			for (int i = 0; i < LED_NUM; i++) {
				if (BIT_TEST(EE_ColorChanged, i)) {
					APP::termMsgLog(APP_LOG_WRN, APP_SRC_EE, "PRNT", "_Debug", "Colour dirty [LED %~2d]", i);
				}
				if (BIT_TEST(EE_AmbientChanged, i)) {
					APP::termMsgLog(APP_LOG_WRN, APP_SRC_EE, "PRNT", "_Debug", "Ambient dirty [LED %~2d]", i);
				}
			}

			// Settings, grouped by owning module instead of one flat run.
			for (uint8_t g = 0; g < EE_GROUP_COUNT; g++) {
				const char* groupName = (const char*)pgm_read_ptr(&EE_GROUP_NAMES[g]);
				const char* groupPrefix = (const char*)pgm_read_ptr(&EE_GROUPS[g]);
				APP::termMsgLogSection(APP_SRC_EE, "PRNT", "_Debug", "EEPROM  %s settings", groupName);
				for (int i = 0; i < EE_MEM_X; i++) {
					const EE_SettingDef* def = EE::getDefByIndex(i);
					if (def && strncmp(def->name, groupPrefix, strlen(groupPrefix)) == 0) {
						EE::LogSetting(def);
					}
				}
			}
			APP::termMsgLogSection(APP_SRC_EE, "PRNT", "_Debug", "EEPROM  ungrouped settings");
			for (int i = 0; i < EE_MEM_X; i++) {
				const EE_SettingDef* def = EE::getDefByIndex(i);
				if (def && !EE::InAnyGroup(def->name)) EE::LogSetting(def);
			}

			APP::termMsgLogSection(APP_SRC_EE, "PRNT", "_Debug", "EEPROM  stored colour");
			for (int i = 0; i < LED_NUM; i++) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "PRNT", "_Debug", "LED [%~2d] RGB [%d,%d,%d] brightness [%d]",
					i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b,
					LED::State.StoredBrightness[i]);
			}
		break;

		case _debug_app:
			APP::termMsgLogSection(APP_SRC_APP, "PRNT", "_Debug", "APP  link");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "PRNT", "_Debug", "Brightness cap [%d]", APP_BRIGHT);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "PRNT", "_Debug", "Buffer [%d] bytes",   APP_UDP_MAX_BUFFER_SIZE);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "PRNT", "_Debug", "UDP timeout [%d] ms", APP_UDP_TIMEOUT);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "PRNT", "_Debug", "Port [%d]",           APP_UDP_PORT);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Phone IP [%d.%d.%d.%d]",
				APP_RECV_IP[0], APP_RECV_IP[1], APP_RECV_IP[2], APP_RECV_IP[3]);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Last UDP packet [%l] ms ago", TimeNow - APP::Ard.LastUdpReceive);
			if ((APP::Ard.LastUdpReceive + ARD_TIMEOUT) > TimeNow) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Timeout in [%l] ms",
					(APP::Ard.LastUdpReceive + ARD_TIMEOUT) - TimeNow);
			} else {
				APP::termMsgLog(APP_LOG_WRN, APP_SRC_APP, "PRNT", "_Debug", "App link [TIMED OUT]");
			}

			// Command ACK state machine - what the last command did and why a
			// repeat inside the dedup window gets re-acked instead of re-run.
			APP::termMsgLogSection(APP_SRC_APP, "PRNT", "_Debug", "APP  ack state");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "PRNT", "_Debug", "Dedup window [%d] ms",  APP_ACK_DEDUP_MS);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "In flight [%s] seq [%d]",
				(APP::State.CurSeqValid) ? "ON" : "OFF", APP::State.CurSeq);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Pending result [%d]",   APP::State.LastResult);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Last handled [%s] seq [%d]",
				(APP::State.LastSeqValid) ? "ON" : "OFF", APP::State.LastSeq);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Last result [%s]",
				(APP::State.LastSeqResult == APP_ACK_OK)          ? "OK"          :
				(APP::State.LastSeqResult == APP_ACK_CLAMPED)     ? "CLAMPED"     :
				(APP::State.LastSeqResult == APP_ACK_REJECTED)    ? "REJECTED"    :
				(APP::State.LastSeqResult == APP_ACK_BLOCKED)     ? "BLOCKED"     :
				(APP::State.LastSeqResult == APP_ACK_LOCKED)      ? "LOCKED"      :
				(APP::State.LastSeqResult == APP_ACK_NOWATER)     ? "NOWATER"     :
				(APP::State.LastSeqResult == APP_ACK_UNSUPPORTED) ? "UNSUPPORTED" : "UNKNOWN");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Last handled [%l] ms ago", TimeNow - APP::State.LastSeqTime);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "Term log seq [%d]",       APP::State.LogSeq);
		break;

		case _debug_udpraw: {
			const bool wifiOK = (WiFi.status() == WL_CONNECTED);
			const IPAddress remoteIP = wifiOK ? UDPRAW_UDP.remoteIP() : IPAddress(0, 0, 0, 0);

			APP::termMsgLogSection(APP_SRC_UDPRAW, "PRNT", "_Debug", "UDPRAW  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_UDPRAW, "PRNT", "_Debug", "Check every [%d] ms", UDPRAW_CHECK_TIME);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_UDPRAW, "PRNT", "_Debug", "Movie delay [%d] ms", UDPRAW_LED_MOVIE_DELAY);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_UDPRAW, "PRNT", "_Debug", "Buffer [%d] bytes",   UDPRAW_BuffSize);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Brightness max [%d]", EE::Get(EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX));

			APP::termMsgLogSection(APP_SRC_UDPRAW, "PRNT", "_Debug", "UDPRAW  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Status [%s]", (UDPRAW::State.Status) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Source IP [%d.%d.%d.%d]",
				remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Last frame [%l] ms ago", TimeNow - UDPRAW::State.LastCheck);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "FPS [%~1F]", (UDPRAW::State.Status) ? UDPRAW::State.Fps : 0.0f);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Cached brightness [%d]", UDPRAW::State.CachedBrightness);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Started [%s%d:%s%d:%s%d] [%s%d-%s%d-%d]",
				TIMEFIELD(UDPRAW::State.InitTime, _HH),
				TIMEFIELD(UDPRAW::State.InitTime, _MI),
				TIMEFIELD(UDPRAW::State.InitTime, _SS),
				TIMEFIELD(UDPRAW::State.InitTime, _DD),
				TIMEFIELD(UDPRAW::State.InitTime, _MM),
				UDPRAW::State.InitTime[_YY] + 2000);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Stream RGB [%d,%d,%d] brightness [%d]",
				LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b, LED::State.StreamBrightness);
			break;
		}

		case _debug_bme280:
			APP::termMsgLogSection(APP_SRC_BME, "PRNT", "_Debug", "BME280  sensor");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_BME, "PRNT", "_Debug", "SDA pin [%d]",         SDA);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_BME, "PRNT", "_Debug", "SCL pin [%d]",         SCL);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_BME, "PRNT", "_Debug", "Check every [%d] ms",  CheckTIME);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_BME, "PRNT", "_Debug", "Temperature [%~2F] C", BME::State.Temperature);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_BME, "PRNT", "_Debug", "Humidity [%~2F] %%",   BME::State.Humidity);
		break;

		case _debug_ambientmode:
			APP::termMsgLogSection(APP_SRC_AMBIENT, "PRNT", "_Debug", "AMBIENT  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_AMBIENT, "PRNT", "_Debug", "Status [%s]",      (APP::Am.Status) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_AMBIENT, "PRNT", "_Debug", "Hold time [%d] min", EE::Get(EE_OTHER_AMBIENT_MODE_TIME));

			APP::termMsgLogSection(APP_SRC_AMBIENT, "PRNT", "_Debug", "AMBIENT  background colour");
			for (int i = 0; i < LED_NUM; i++) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_AMBIENT, "PRNT", "_Debug", "LED [%~2d] RGB [%d,%d,%d] brightness [%d]",
					i, LED::State.AmbientBackgroundColor[i].r, LED::State.AmbientBackgroundColor[i].g,
					LED::State.AmbientBackgroundColor[i].b, LED::State.AmbientBackgroundBrightness[i]);
			}
		break;

		case _debug_wifi: {
			const bool wifiOK = (WiFi.status() == WL_CONNECTED);
			const IPAddress ip = wifiOK ? WiFi.localIP()    : IPAddress(0, 0, 0, 0);
			const IPAddress gw = wifiOK ? WiFi.gatewayIP()  : IPAddress(0, 0, 0, 0);
			const IPAddress sn = wifiOK ? WiFi.subnetMask() : IPAddress(0, 0, 0, 0);
			uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
			WiFi.macAddress(mac);

			APP::termMsgLogSection(APP_SRC_NET, "PRNT", "_Debug", "WIFI  config");
			char ssidBuf[64], passBuf[64];
			strcpy_P(ssidBuf, WIFI_SSID);
			strcpy_P(passBuf, WIFI_PASS);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "SSID [%s]",           ssidBuf);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Pass [%s]",           passBuf);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Check every [%d] ms", Check_TIME);

			APP::termMsgLogSection(APP_SRC_NET, "PRNT", "_Debug", "WIFI  state");
			APP::termMsgLog(wifiOK ? APP_LOG_INF : APP_LOG_ERR, APP_SRC_NET, "PRNT", "_Debug",
				"Status [%s]", wifiOK ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Link state [%s]",
				(NET_WifiSt == netWifiOK)       ? "OK"       :
				(NET_WifiSt == netWifiLost)     ? "LOST"     :
				(NET_WifiSt == netWifiRetrying) ? "RETRYING" : "UNKNOWN");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Cached connected [%s]",   (NET_Connected) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Last check [%l] ms ago",  TimeNow - NET_LastCheck);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Signal [%d] dBm",         (int)WiFi.RSSI());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "IP [%d.%d.%d.%d]",        ip[0], ip[1], ip[2], ip[3]);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Gateway [%d.%d.%d.%d]",   gw[0], gw[1], gw[2], gw[3]);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Netmask [%d.%d.%d.%d]",   sn[0], sn[1], sn[2], sn[3]);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "MAC [%X:%X:%X:%X:%X:%X]",
				mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Connected [%s%d:%s%d:%s%d] [%s%d-%s%d-%d]",
				TIMEFIELD(NET::Wifi.ConnectTime, _HH),
				TIMEFIELD(NET::Wifi.ConnectTime, _MI),
				TIMEFIELD(NET::Wifi.ConnectTime, _SS),
				TIMEFIELD(NET::Wifi.ConnectTime, _DD),
				TIMEFIELD(NET::Wifi.ConnectTime, _MM),
				NET::Wifi.ConnectTime[_YY] + 2000);
			break;
		}

		case _debug_arduino: {
			const uint32_t up = millis() / 1000;

			APP::termMsgLogSection(APP_SRC_SYS, "PRNT", "_Debug", "ARDUINO  build");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "PRNT", "_Debug", "Firmware build details");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "PRNT", "_Debug", "Built [%s] [%s]", __DATE__, __TIME__);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_SYS, "PRNT", "_Debug", "Uptime [%l] h [%l] m [%l] s",
				up / 3600UL, (up / 60UL) % 60UL, up % 60UL);

			APP::termMsgLogSection(APP_SRC_SYS, "PRNT", "_Debug", "ARDUINO  memory");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "PRNT", "_Debug", "RAM total [%d] B",     ARD_RAM_TOTAL);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_SYS, "PRNT", "_Debug", "RAM free [%d] B",      APP::getFreeRam());
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "PRNT", "_Debug", "App timeout [%d] ms",  ARD_TIMEOUT);
			break;
		}

		case _debug_lisens:
			APP::termMsgLogSection(APP_SRC_LUX, "PRNT", "_Debug", "LUX  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Pin [%d]",            LIGHT_SENSOR_PIN);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Check every [%d] ms", LIGHT_SENSOR_CHECK_TIME);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Samples [%d]",        LIGHT_SENSOR_SAMPLES);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Lux fix [%d]",        LIGHT_SENSOR_LUX_FIX);

			APP::termMsgLogSection(APP_SRC_LUX, "PRNT", "_Debug", "LUX  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Level [%d]",       LISENS::State.Lux);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Average [%d]",     LISENS::State.Average);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Raw ADC [%d]",     analogRead(LIGHT_SENSOR_PIN));
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Samples taken [%l]", LISENS::State.SampleCount);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Sample sum [%l]",    LISENS::State.SampleSum);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Task id [%d]",       (int)LISENS::State.TaskID);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Adapt factor [%d] %%", LED::getLuxAdaptFactor());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Auto brightness [%s]", EE::Get(EE_OTHER_BRIGHTNESS_AUTO) ? "ON" : "OFF");
			// A forced lux level freezes the sensor - say so, or the readings
			// above look like a broken ADC.
			if (TestMode == _testmode_lux) {
				APP::termMsgLog(APP_LOG_WRN, APP_SRC_LUX, "PRNT", "_Debug", "Sensor held by test mode [LUX]");
			}

			APP::termMsgLogSection(APP_SRC_LUX, "PRNT", "_Debug", "LUX  thresholds");
			{
				static const uint8_t ARR_LEN = sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0]);
				for (int i = 0; i < ARR_LEN; i++) {
					APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "PRNT", "_Debug", "Level [%d] below [%d]", i + 1, LIGHT_SENS_LUX[i]);
				}
			}
		break;

		case _debug_heartbeat:
			APP::termMsgLogSection(APP_SRC_HB, "PRNT", "_Debug", "HEARTBEAT  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "PRNT", "_Debug", "Pin [%d]",     LED_PIN_HB);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "PRNT", "_Debug", "Pixels [%d]",  LED_HB_NUM);

			APP::termMsgLogSection(APP_SRC_HB, "PRNT", "_Debug", "HEARTBEAT  settings");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Effect [%d]",       EE::Get(EE_HB_EFFECT));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Effect speed [%d]", EE::Get(EE_HB_EFFECT_SPEED));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Dual colour [%s]",  EE::Get(EE_HB_DUAL_COLOR) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "TV-on effect [%d]", EE::Get(EE_TV_ON_HB_EFF));

			APP::termMsgLogSection(APP_SRC_HB, "PRNT", "_Debug", "HEARTBEAT  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Phase [%d]",           HB::State.Phase);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Param A [%d]",         HB::State.ParamA);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_HB, "PRNT", "_Debug", "Base brightness [%d]", LED::HB_GetBaseBr());
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "PRNT", "_Debug", "Task id [%d]",         (int)HB::State.TaskID);
		break;

		case _debug_dif: {
			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  link");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Target IP [%d.%d.%d.%d]",
				DIF_TARGET_IP[0], DIF_TARGET_IP[1], DIF_TARGET_IP[2], DIF_TARGET_IP[3]);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Port [%d]",            DIF_UDP_PORT);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "UDP timeout [%d] ms",  DIF_UDP_TIMEOUT);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Status poll [%d] s",   DIF_STATUS_CHECK_S);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Idle poll [%d] s",     DIF_IDLE_CHECK_S);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Mode max [%d]",        DIF_MODE_MAX);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Effect count [%d]",    DIF_EFFECT_COUNT);

			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Mode [%s]",   DIF::getModeName(DIF::State.Mode));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Strip [%s]",  DIF::getStripStatusName(DIF::State.StripStatus));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Last status [%l] ms ago",  TimeNow - DIF::State.LastStatusTime);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Last request [%l] ms ago", TimeNow - DIF::State.LastRequestTime);

			const uint8_t _ams = DIF::ActiveModeSetting();
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Active source [%s]",
				(_ams == EE_DIF_MODE_TV)      ? "TV"      :
				(_ams == EE_DIF_MODE_UDPRAW)  ? "UDPRAW"  :
				(_ams == EE_DIF_MODE_MOTION)  ? "MOTION"  :
				(_ams == EE_DIF_MODE_AMBIENT) ? "AMBIENT" : "none");

			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  parfum");
			if (DIF::State.ParfumMin > 0) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Parfum window [ON] remaining [%d] min", DIF::State.ParfumMin);
			} else {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Parfum window [OFF]");
			}

			// Usage/refill stats -- from the extended "Ds"/"Dc" reply, see ParseStatus().
			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  usage");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Since refill [%d] min",  DIF::State.UsageAccumMin);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Avg cycle [%d] min",     DIF::State.UsageAvgMin);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "History [%d] cycles",    DIF::State.UsageRefillCount);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Lifetime refills [%d]",  DIF::State.UsageTotalRefills);

			// Command relay - our seq out, whose app seq is waiting on the ack.
			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  command relay");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Command seq [%d]",   DIF::State.CmdSeq);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Awaiting ack [%d] pending",
				DIF::State.PendingCount);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Last ack result [%d]", DIF::State.CmdResult);

			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  settings");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Effect [%s]",      DIF::getEffectName(EE::Get(EE_DIF_EFFECT)));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Brightness [%d]",  EE::Get(EE_DIF_BRIGHTNESS));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Speed [%d] ms",    EE::Get(EE_DIF_SPEED));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Mode TV [%s]",     DIF::getModeName(EE::Get(EE_DIF_MODE_TV)));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Mode motion [%s]", DIF::getModeName(EE::Get(EE_DIF_MODE_MOTION)));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Mode UDPRAW [%s]", DIF::getModeName(EE::Get(EE_DIF_MODE_UDPRAW)));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Mode ambient [%s]",DIF::getModeName(EE::Get(EE_DIF_MODE_AMBIENT)));

			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  idle pulse");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Wait [%d] min",   EE::Get(EE_DIF_IDLE_WAIT_MIN));
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "On for [%d] min", EE::Get(EE_DIF_IDLE_ON_MIN));
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "PRNT", "_Debug", "Mode [%s]",       DIF::getModeName(EE::Get(EE_DIF_IDLE_MODE)));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Timer reset [%l] ms ago", TimeNow - DIF::State.IdleTimerReset);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Pulsing [%s]", DIF::State.IdlePulseActive ? "ON" : "OFF");
			if (DIF::State.IdlePulseActive) {
				APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Pulse started [%l] ms ago", TimeNow - DIF::State.IdlePulseStart);
			}

			APP::termMsgLogSection(APP_SRC_DIF, "PRNT", "_Debug", "DIFFUSER  auto-on log");
			for (int i = 0; i < DIF_LOG_INDEX_MAX; i++) {
				if (DIF::State.LOG[i].epoch > 0) {
					const uint32_t _ep = DIF::State.LOG[i].epoch;
					APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug",
						"[%s%d:%s%d:%s%d] [%s%d-%s%d-%d] by [%s] mode [%s] effect [%s]",
						TIMEVAL(hour(_ep)),
						TIMEVAL(minute(_ep)),
						TIMEVAL(second(_ep)),
						TIMEVAL(day(_ep)),
						TIMEVAL(month(_ep)),
						year(_ep),
						(DIF::State.LOG[i].TriggerBy == difTrigTV)      ? "TV"      :
						(DIF::State.LOG[i].TriggerBy == difTrigUDPRAW)  ? "UDPRAW"  :
						(DIF::State.LOG[i].TriggerBy == difTrigMotion)  ? "MOTION"  :
						(DIF::State.LOG[i].TriggerBy == difTrigAmbient) ? "AMBIENT" : "IDLE",
						DIF::getModeName(DIF::State.LOG[i].Mode),
						DIF::getEffectName(DIF::State.LOG[i].Effect));
				}
			}
			break;
		}

		case _debug_task: {
			APP::termMsgLogSection(APP_SRC_TASK, "PRNT", "_Debug", "TASK  shared step state");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "PRNT", "_Debug", "Phase [%d]",   TASK.Phase);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "PRNT", "_Debug", "Param A [%d]", TASK.ParamA);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "PRNT", "_Debug", "Param B [%d]", TASK.ParamB);
			// Live table straight from the scheduler. Every task now carries the
			// name AddTask() was called with, so this lists whatever is
			// actually running rather than the handles the sketch happens to keep.
			const uint8_t count = _TASK.getTaskCount();
			APP::termMsgLogSection(APP_SRC_TASK, "PRNT", "_Debug", "TASK  running [%d]", count);

			for (uint8_t i = 0; i < count; i++) {
				const taskId_t id = _TASK.getTaskIdAt(i);
				if (id == 0) continue;

				const int8_t   iter = _TASK.getTaskIterationsRemaining(id);
				const uint32_t ms   = (uint32_t)(_TASK.getTaskInterval(id) / 1000ULL);

				// Built into a local buffer, NOT with a nested formatMSG(): that
				// shares one static buffer with the vformatMSG() inside Log,
				// so the argument would be overwritten while being read.
				char iterTxt[12];
				if (iter < 0) strncpy(iterTxt, "forever", sizeof(iterTxt) - 1);
				else          snprintf(iterTxt, sizeof(iterTxt), "%d", (int)iter);
				iterTxt[sizeof(iterTxt) - 1] = '\0';

				APP::termMsgLog(APP_LOG_INF, APP_SRC_TASK, "PRNT", "_Debug",
					"%s id [%d] state [%s] locked [%s] every [%l] ms runs left [%s]",
					_TASK.getTaskName(id),
					(int)id,
					_TASK.isTaskActive(id) ? "ACTIVE" : (_TASK.isTaskPaused(id) ? "PAUSED" : "DEAD"),
					_TASK.isTaskLocked(id) ? "ON" : "OFF",
					ms,
					iterTxt);
			}

			// The handles the sketch tracks by hand, so a stale one is visible
			// against the live table above.
			APP::termMsgLogSection(APP_SRC_TASK, "PRNT", "_Debug", "TASK  tracked handles");
			TSK::LogHandle("Heartbeat",    HB::State.TaskID);
			TSK::LogHandle("Light sensor", LISENS::State.TaskID);
			TSK::LogHandle("EEPROM save",  EE::State.tID);
			TSK::LogHandle("Test mode",    TestMode_tID);
			break;
		}

		case _debug_rtc: {
			const uint32_t ep = NET::RTC_EpochUTC();

			APP::termMsgLogSection(APP_SRC_RTC, "PRNT", "_Debug", "RTC  clock");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "PRNT", "_Debug", "Device time [%s%d:%s%d:%s%d]",
				TIMEFIELD(NET::Date.time, _HH),
				TIMEFIELD(NET::Date.time, _MI),
				TIMEFIELD(NET::Date.time, _SS));
			APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "PRNT", "_Debug", "Device date [%s%d-%s%d-%d]",
				TIMEFIELD(NET::Date.time, _DD),
				TIMEFIELD(NET::Date.time, _MM),
				NET::Date.time[_YY] + 2000);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "PRNT", "_Debug", "Epoch UTC [%l]", ep);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "PRNT", "_Debug", "Epoch local [%l]", RTC_TimeClient.getEpochTime());
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_RTC, "PRNT", "_Debug", "UTC offset [%d] h", NET_TIMEZONE);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "PRNT", "_Debug", "NTP sync [%s]",
				(RTC_Status == rtcOK)       ? "OK"       :
				(RTC_Status == rtcRETRY)    ? "RETRY"    :
				(RTC_Status == rtcRETRYING) ? "RETRYING" : "UNKNOWN");
			if (ep == 0) {
				APP::termMsgLog(APP_LOG_ERR, APP_SRC_RTC, "PRNT", "_Debug", "Clock never synced [ON]");
			}
			break;
		}

		case _debug_testmode:
			APP::termMsgLogSection(APP_SRC_TEST, "PRNT", "_Debug", "TEST MODE  state");
			APP::termMsgLog((TestMode == _testmode_none) ? APP_LOG_INF : APP_LOG_WRN, APP_SRC_TEST, "PRNT", "_Debug",
				"Active [%s]", APP::_TestModeName(TestMode));
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_TEST, "PRNT", "_Debug", "Exit task id [%d]", (int)TestMode_tID);
			if (TestMode != _testmode_none) {
				APP::termMsgLog(APP_LOG_WRN, APP_SRC_TEST, "PRNT", "_Debug", "Real sensors are [OVERRIDDEN]");
			}
		break;

		case _debug_all: {
			// One line per module - the report to open first when something is
			// wrong and you do not yet know which module owns it.
			const bool wifiOK = (WiFi.status() == WL_CONNECTED);
			const IPAddress ip = wifiOK ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
			const uint32_t up  = millis() / 1000;

			APP::termMsgLogSection(APP_SRC_SYS, "PRNT", "_Debug", "SUMMARY  system");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_SYS, "PRNT", "_Debug", "Firmware uptime [%l] h [%l] m",
				up / 3600UL, (up / 60UL) % 60UL);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_SYS, "PRNT", "_Debug", "RAM free [%d] B of [%d] B", APP::getFreeRam(), ARD_RAM_TOTAL);

			APP::termMsgLogSection(APP_SRC_SYS, "PRNT", "_Debug", "SUMMARY  modules");
			APP::termMsgLog(wifiOK ? APP_LOG_INF : APP_LOG_ERR, APP_SRC_NET, "PRNT", "_Debug",
				"WiFi [%s] ip [%d.%d.%d.%d] signal [%d] dBm",
				wifiOK ? "ON" : "OFF", ip[0], ip[1], ip[2], ip[3], (int)WiFi.RSSI());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_APP, "PRNT", "_Debug", "App last packet [%l] ms ago", TimeNow - APP::Ard.LastReceive);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LED, "PRNT", "_Debug", "LED strip [%s] dirty [%d]",
				(LED::State.Enabled) ? "ON" : "OFF", LED::getChangedCount());
			APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "PRNT", "_Debug", "TV [%s] pin [%d]", (TV::State.Status) ? "ON" : "OFF", TV::State.PinValue);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_MOTION, "PRNT", "_Debug", "Motion [%s]",
				(MOTION::State.Status == motOFF)     ? "OFF"     :
				(MOTION::State.Status == motON)      ? "ON"      :
				(MOTION::State.Status == motAUTOOFF) ? "AUTOOFF" :
				(MOTION::State.Status == motCOM)     ? "COM"     :
				(MOTION::State.Status == motBED)     ? "BED"     : "UNKNOWN");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_LUX, "PRNT", "_Debug", "Lux level [%d] average [%d]", LISENS::State.Lux, LISENS::State.Average);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_UDPRAW, "PRNT", "_Debug", "Ambilight [%s]",  (UDPRAW::State.Status) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_AMBIENT, "PRNT", "_Debug", "Ambient mode [%s]", (APP::Am.Status) ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_DIF, "PRNT", "_Debug", "Diffuser [%s] strip [%s] parfum [%d] min",
				DIF::getModeName(DIF::State.Mode), DIF::getStripStatusName(DIF::State.StripStatus), DIF::State.ParfumMin);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_BME, "PRNT", "_Debug", "Climate [%~2F] C [%~2F] %%", BME::State.Temperature, BME::State.Humidity);
			APP::termMsgLog(MQTT::State.Up ? APP_LOG_INF : APP_LOG_WRN, APP_SRC_NET, "PRNT", "_Debug",
				"MQTT [%s] last try [%l] ms ago", MQTT::State.Up ? "ON" : "OFF", TimeNow - MQTT::State.LastTry);
			APP::termMsgLog((TestMode == _testmode_none) ? APP_LOG_INF : APP_LOG_WRN, APP_SRC_TEST, "PRNT", "_Debug",
				"Test mode [%s]", APP::_TestModeName(TestMode));
			break;
		}

		case _debug_mqtt: {
			const bool cliOK = MQTT_Cli.connected();

			APP::termMsgLogSection(APP_SRC_NET, "PRNT", "_Debug", "MQTT  config");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Host [%s]",        MQTT_HOST);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Port [%d]",        MQTT_PORT);
			APP::termMsgLog(MQTTCRED::State.valid ? APP_LOG_DBG : APP_LOG_WRN, APP_SRC_NET, "PRNT", "_Debug",
				"User [%s]", MQTTCRED::State.valid ? MQTTCRED::State.user : "(not set)");
			APP::termMsgLog(MQTTCRED::State.valid ? APP_LOG_DBG : APP_LOG_WRN, APP_SRC_NET, "PRNT", "_Debug",
				"Pass [%s]", MQTTCRED::State.valid ? "********" : "(not set)");  // never print the real password, even here
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Topic [%s]",       MQTT_TOPIC);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Keepalive [%d] s", MQTT_KEEPALIVE);
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Retry every [%d] ms", MQTT_RETRY_MS);

			APP::termMsgLogSection(APP_SRC_NET, "PRNT", "_Debug", "MQTT  state");
			APP::termMsgLog(cliOK ? APP_LOG_INF : APP_LOG_ERR, APP_SRC_NET, "PRNT", "_Debug",
				"Client [%s]", cliOK ? "ON" : "OFF");
			APP::termMsgLog(MQTT::State.Up ? APP_LOG_INF : APP_LOG_WRN, APP_SRC_NET, "PRNT", "_Debug",
				"Cached up [%s]", MQTT::State.Up ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Last (re)connect [%l] ms ago",
				TimeNow - MQTT::State.LastTry);
			APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "PRNT", "_Debug", "Rx pending [%s]",
				MQTT::State.RxPending ? "ON" : "OFF");
			APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "PRNT", "_Debug", "Rx len [%d]", MQTT::State.RxLen);
			break;
		}

		default:
			APP::termMsgLog(APP_LOG_ERR, APP_SRC_APP, "PRNT", "_Debug", "Unknown debug target [%d]", d);
		break;
	}

	// Trailing spacer so the next report starts on clean ground
	APP::termMsgLogGap("PRNT", "_Debug");
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* FORMAT_MSG  * * * * * * * * * * * * * * * * * */
/**
 * @brief  Custom printf-style formatter that writes into a static 512-byte buffer.
 *
 * Supports a subset of format specifiers plus FuZz-specific extensions:
 *   %s   -- C-string (trailing-space padded when width given, e.g. %24s)
 *   %~s  -- C-string (leading-space padded, e.g. %~24s)
 *   %d   -- signed integer (zero-padded when width given, e.g. %~3d)
 *   %X   -- hexadecimal  (zero-padded, e.g. %2X)
 *   %T   -- boolean      (outputs "True" or "False")
 *   %F   -- float/double (decimal places = width, e.g. %~2F)
 *   %l   -- unsigned long / uint32_t (e.g. millis(), intervals)
 *
 * @param  format  Format string using the specifiers above.
 * @param  ...     Variadic arguments matching each specifier in order.
 *
 * @return Pointer to the internal static buffer containing the formatted string.
 *
 * @note   The buffer is static and shared -- the result must be consumed
 *         (e.g. passed to _print or Send) before the next formatMSG call.
 */
char* formatMSG(const char *format, ...) {
	va_list args;                                                       // Argument list - Setup
	va_start(args, format);                                             // Start processing - Setup
	char *out = vformatMSG(format, args);                               // Delegate to core - Action
	va_end(args);                                                       // Cleanup - Setup
	return out;                                                         // Return pointer - Action
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* PRINT_SERIAL  * * * * * * * * * * * * * * * * */
/**
 * @brief  Write a C-string to the hardware Serial port if it is connected.
 *
 * @param  msg  Null-terminated string to print. May be NULL -- checked before use.
 *
 * @note   No newline is appended automatically; include NL ("\n") in the string
 *         or use formatMSG with NL.
 */
void _print(const char *msg) {
	if (Serial) {
		Serial.print(msg);
	}
}

/**
 * @brief  va_list core of formatMSG() - the actual formatter.
 *
 * Split out so other variadic helpers (Log, LogSection) can forward
 * their own argument list without re-implementing the specifier parser.
 *
 * @param  format  Format string using the formatMSG() specifiers.
 * @param  args    Caller's va_list, already va_start()ed. Not consumed here -
 *                 the caller still owns the matching va_end().
 *
 * @return Pointer to the internal static buffer containing the formatted string.
 */
char* vformatMSG(const char *format, va_list args) {
	/**
	 * ======================================================================
	 * EXAMPLES (PERFECT ALIGNMENT):
	 * ----------------------------------------------------------------------
	 * TRAILING: formatMSG("%24s # # begin", "WIFI_Setup")
	 * -> Output: "WIFI_Setup               # # begin" (Exactly 24 chars wide)
	 * * LEADING:  formatMSG("%~24s", "SYSTEM") 
	 * -> Output: "                  SYSTEM"
	 * * NUMERIC:  formatMSG("Lux: [%~3d]", 1)
	 * -> Output: "Lux: [001]"
	 * ======================================================================
	 */

	static char _FORMAT_MSG_[256];										// Local static buffer - Setup (reduced from 512)
	int _FORMAT_MSG_I = 0;												// Local index tracker - State
	_FORMAT_MSG_[0] = '\0';												// Clear buffer - Setup

	
	static const size_t FORMAT_SIZE = sizeof(_FORMAT_MSG_) - 1; // Max chars we can write (leave space for null terminator)
	while (*format != '\0' && _FORMAT_MSG_I < FORMAT_SIZE) {						// Process until end of format or buffer full - Logic
            if (*format == '%') {											// Specifier found - Logic
                format++;													// Move past '%' - Action
                // Literal percent: '%%' -> emit single '%' and continue
                if (*format == '%') {
                    _FORMAT_MSG_[_FORMAT_MSG_I++] = '%';				// Append '%' - Action
                    format++; /* consume second % */
                    continue;
                }
				
                bool leadingPadding = false;								// Prefix flag - State
                int width = 0;												// Padding value - Setup
                bool widthSpecified = false;								// Padding flag - State

			if (*format == '~') {										// Custom leading trigger - Logic
				leadingPadding = true;									// Set direction to Leading - State
				format++;												// Skip '~' - Action
			}

			if (*format >= '0' && *format <= '9') {						// Width number found - Logic
				width = atoi(format);									// Convert to int - Action
				widthSpecified = true;									// Mark active - State
				while (*format >= '0' && *format <= '9') format++;		// Skip digits - Action
			}

			switch (*format) {											// Handle data type - Logic
				case 's': {												// String - Logic
					const char *str = va_arg(args, const char *);		// Extract string - Action
					int strLen = strlen(str);							// Get text length - Action
					int spacesToFill = (widthSpecified && width > strLen) ? (width - strLen) : 0; // Gap - Math

					if (widthSpecified && leadingPadding) {				// LEADING CASE (%~24s) - Logic
						for (int s = 0; s < spacesToFill; s++) {
							if (_FORMAT_MSG_I < 511) _FORMAT_MSG_[_FORMAT_MSG_I++] = ' '; // Add space - Action
						}
					}
					
					_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%s", str);

					if (widthSpecified && !leadingPadding) {				// TRAILING CASE (%24s) - Logic
						for (int s = 0; s < spacesToFill; s++) {
							if (_FORMAT_MSG_I < 511) _FORMAT_MSG_[_FORMAT_MSG_I++] = ' '; // Add space - Action
						}
					}
					break;
				}
				case 'd': {												// Integer - Logic
					int i = va_arg(args, int);							// Extract int - Action
					if (widthSpecified) {								// Standard numeric padding - Logic
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%0*d", width, i);
					} else {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%d", i);
					}
					break;
				}
				case 'X': {												// Hexadecimal - Logic
					int hex = va_arg(args, int);						// Extract hex - Action
					if (widthSpecified) {								// Width logic - Logic
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%0*X", width, hex);
					} else {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%02X", hex);
					}
					break;
				}
				case 'T': {												// Boolean - Logic
					bool b = (bool)va_arg(args, int);					// Extract bool - Action
					_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%s", b ? "True" : "False");
					break;
				}
				case 'F': {                                                 // Float - Logic
					double f = va_arg(args, double);                        // Extract float (promoted to double) - Action
					if (widthSpecified) {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%.*f", width, f);
					} else {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%f", f);
					}
					break;
				}
				case 'l': {												// Unsigned long / uint32_t - Logic
					unsigned long ul = va_arg(args, unsigned long);		// Extract ulong - Action
					if (widthSpecified) {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%0*lu", width, ul);
					} else {
						_FORMAT_MSG_I += snprintf(&_FORMAT_MSG_[_FORMAT_MSG_I], sizeof(_FORMAT_MSG_) - _FORMAT_MSG_I, "%lu", ul);
					}
					break;
				}
				default: {												// Literal - Logic
					_FORMAT_MSG_[_FORMAT_MSG_I++] = '%';				// Append '%' - Action
					if (_FORMAT_MSG_I < 511) _FORMAT_MSG_[_FORMAT_MSG_I++] = *format; // Append unknown - Action
					break;
				}
			}
		} else {
			_FORMAT_MSG_[_FORMAT_MSG_I++] = *format;					// Regular character - Action
		}
		format++;														// Next format char - Action
	}

	_FORMAT_MSG_[_FORMAT_MSG_I] = '\0';									// Null-terminate - Setup
	return _FORMAT_MSG_;												// Return pointer - Action
}
} // namespace PRNT


namespace LED {
/* ------------------------------------------------------------------------ */
/* LED                                                                        */
/* Zone helpers - pixel ops - change tracking - LED transition tasks          */
/* ------------------------------------------------------------------------ */

/* -- Setup / Refresh --------------------------------------------------- */

void Setup() {
    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%32s : initializing NeoPixel strips" NL, "LED_Setup"));
    #endif

    /* Initialize NeoPixel strips */
    stripFront.begin();
    stripBack.begin();
    stripHB.begin();
    
    /* Clear and set initial brightness */
    stripFront.show();
    stripBack.show();
    stripHB.show();

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%32s : strips initialized [TV:%d COM:%d UCOM:%d BED:%d LAMP:%d HB:%d TOTAL:%d]" NL, "LED_Setup", LED_TV_NUM, LED_COM_NUM, LED_UCOM_NUM, LED_BED_NUM, LED_LAMP_NUM, LED_HB_NUM, LED_NUM_TOTAL));
    #endif

    setAll(0, 0, 0, 0);

    for (int i = 0; i < LED_NUM - LED_HB_NUM_FAKE; i++) LED::State.PixelOrder[i]   = i;
    for (int i = 0; i < LED_HB_NUM;               i++) LED::State.HeartbeatOrder[i] = i;

    shuffleArray(LED::State.PixelOrder,   LED_NUM - LED_HB_NUM_FAKE);
    shuffleArray(LED::State.HeartbeatOrder, LED_HB_NUM);

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%32s : shuffle order and LED refresh task registered [%d ms]" NL, "LED_Setup", LED_REFRESH_TIME));
    #endif

    TSK::AddTask("LED_Setup", "LED_Refresh", Refresh, TASK_MS, LED_REFRESH_TIME, 0, true);
}

void Refresh(uint32_t now) {   /* hardware push -- prototype in _DEF.h; do NOT make static (must match declaration) */
    stripFront.show();
    stripBack.show();
    stripHB.show();
    LED::State.NeedsUpdate = false;
    LED::State.LastUpdateTime = now;
    LED::State.LastRefreshTime = now;
}

void Refresh(taskId_t taskId) {
    if (UDPRAW::State.Status) return;
    if (LED::State.LastRefreshTime + LED_REFRESH_TIME > TimeNow) return;
    Show();
}

/**
 * @brief  Flag the LED strip for a hardware refresh on the next loop iteration.
 */
void Show() {
    LED::State.NeedsUpdate = true;

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # called" NL, "LED_Show"));
    #endif
}

/**
 * @brief  Push the current pixel buffer to the strips immediately, once.
 *
 * The periodic Refresh() only runs inside loop()'s `if (LED::State.Enabled)`
 * gate, so a blank written while disabling never reaches the hardware - the
 * strip keeps showing its last frame. Anything that has to change the strip
 * from OUTSIDE that gate (disable, and the test/stream teardowns) calls this
 * to force the frame out regardless of NeedsUpdate or the refresh interval.
 */
static inline void ForceShow() {
	stripFront.show();
	stripBack.show();
	stripHB.show();
	LED::State.NeedsUpdate = false;
	LED::State.LastUpdateTime = TimeNow;
	LED::State.LastRefreshTime = TimeNow;
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* LED SMOOTH TRANSITION TASKS  * * * * * * * * * */
/* * * * * * * * * * * * * * * * * * * * * * * * */

/**
 * @brief  Dynamic ambient light adjustment task -- re-apply Lux-scaled brightness to all LEDs.
 */
void T_LUX_BR_CHANGE(taskId_t taskId) {
    #ifdef ENABLE_LOG_LED_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : adjusting brightness to ambient lux level" NL, "T_LUX_BR_CHANGE"));
    #endif

    bool anyChanged = false;
    const int limit   = LED_NUM_TOTAL;
    const int brInc   = EE::Get(EE_TV_ON_BR_CL_INC);                     // Raw EE step - lux change is NOT speed-adapted - Setup
    const int hbTgtBr = HB_GetBaseBr();  // pre-calc once for all HB pixels

    for (int i = 0; i < limit; i++) {
        const int targetBr = IsHB(i) ? hbTgtBr : getLuxBrightness(LED::State.StoredBrightness[i]);

        if (TG_BRIGHTNESS(i, targetBr, brInc, false)) {
            setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
            anyChanged = true;
        }
    }

    if (anyChanged) {
        Show();
    } else {
        #ifdef ENABLE_LOG_LED_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : lux adjustment complete" NL, "T_LUX_BR_CHANGE"));
        #endif
        TSK::KillTasksAvoidLocked("T_LUX_BR_CHANGE");
        HB::EndTask();
        HB::StartEffect(true, false, false);
    }
}

/**
 * @brief  Versatile smooth transition task -- animate selected LEDs to a new colour or brightness.
 */
void T_SMOOTH_CHANGE(taskId_t taskId) {
    #ifdef ENABLE_LOG_LED_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : mode [%s] - animating selected LEDs" NL, "T_SMOOTH_CHANGE", (TASK.ParamA == 1) ? "BRIGHTNESS" : "COLOR"));
    #endif

    bool moving = false;
    const int  inc              = EE::Get(EE_TV_ON_BR_CL_INC);
    const int  animationMode    = TASK.ParamA;
    const bool isBrightnessMode = (animationMode == 1);

    if (TASK.Phase == 0) TASK.Phase = 1;

    // Use selected LED cache for better performance
    if (APP::State.SelectedCacheDirty) {
        APP::RefreshSelectedCache();
    }

    for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
        const int ledN = APP::State.SelectedLedCache[cacheIdx];

        const bool isHB       = IsHB(ledN);
        const int  iterations = isHB ? LED_HB_NUM : 1;
        const int  targetBr   = isBrightnessMode ? getLuxBrightness(LED::State.StoredBrightness[ledN]) : 0;

        for (int i = 0; i < iterations; i++) {
            const int idx       = isHB ? HB(i) : ledN;
            bool      ledChanged = false;

            if (isBrightnessMode) {
                ledChanged = TG_BRIGHTNESS(idx, targetBr, inc, false);
            } else {
                ledChanged = TG_COLOR(idx,
                                          LED::State.TargetColor[ledN].r,
                                          LED::State.TargetColor[ledN].g,
                                          LED::State.TargetColor[ledN].b, inc);
            }

            if (ledChanged) {
                // Brightness mode: render from LED::State.StoredColor[ledN] (the true
                // persisted hue), NOT LED::State.CurrentColor[idx] -- setPixel() forces
                // CurrentColor to black whenever brightness is 0 (by design), so once a
                // fade-to-0 passes through here once, feeding CurrentColor back into
                // itself would keep re-storing that black forever, even as brightness
                // climbs back up afterward. StoredColor[] is untouched by that and
                // always holds the real colour. Colour mode is unaffected -- it already
                // uses CurrentColor[idx], which TG_COLOR() just stepped toward the
                // real TargetColor two lines up.
                const CRGB &renderColor = isBrightnessMode ? LED::State.StoredColor[ledN] : LED::State.CurrentColor[idx];
                setPixel(idx, renderColor.r, renderColor.g, renderColor.b, LED::State.CurrentBrightness[idx], false);
                moving = true;
            }
        }
    }

    if (moving) {
        Show();
    } else {
        #ifdef ENABLE_LOG_LED_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : animation complete - chaining next task" NL, "T_SMOOTH_CHANGE"));
        #endif

        TSK::KillTasksAvoidLocked("T_SMOOTH_CHANGE");

        if (APP::Am.Status) {
            memcpy(LED::State.AmbientBackgroundColor,      LED::State.CurrentColor,      LED_NUM * sizeof(CRGB));
            memcpy(LED::State.AmbientBackgroundBrightness, LED::State.CurrentBrightness, LED_NUM * sizeof(uint8_t));
            memset(EE_AmbientChanged, true, sizeof(EE_AmbientChanged));              // Track all ambient color changes
        } else if (!isBrightnessMode) {
            memcpy(LED::State.StoredColor, LED::State.CurrentColor, LED_NUM * sizeof(CRGB));
            memset(EE_ColorChanged, true, sizeof(EE_ColorChanged));                  // Track all LED color changes
            if (LED_NUM > LED_START_I_HB) {
                CRGB hbColor = LED::State.CurrentColor[LED_NUM - 1];
                for (int j = 0; j < LED_HB_NUM; j++) LED::State.TargetColor[HB(j)] = hbColor;
            }
        } else {
            // Bug fix: brightness-only completion never flagged EE_ColorChanged[],
            // so StoredBrightness[] updates applied live but were silently lost on
            // reboot. Mark only the entry APP::setBrightnessToSelected() actually
            // wrote - for HB, ledN IS LED_START_I_HB (never expanded per sub-pixel),
            // so looping LED_HB_NUM here just flags 29 untouched slots and persists
            // their stale StoredBrightness[] bytes for nothing. Matches the
            // clamp-to-marker pattern used by TG_BRIGHTNESS/TG_COLOR/setPixel.
            for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
                EE::MarkColorChanged(APP::State.SelectedLedCache[cacheIdx]);
            }
        }

        if (APP::Am.Status) {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : chaining -> T_AMBIENT_MODE_ON" NL, "T_SMOOTH_CHANGE"));
            #endif
            TASK.Phase = 3;
            TSK::AddTask("T_SMOOTH_CHANGE", "T_AMBIENT_MODE_ON", T_AMBIENT_MODE_ON, TASK_MS,
                         EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                         (uint32_t)EE::Get(EE_OTHER_AMBIENT_MODE_TIME) * 60000, false);
        } else if (!TV::State.Status) {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : chaining -> T_LEDS_TO_OFF" NL, "T_SMOOTH_CHANGE"));
            #endif
            TSK::AddTask("T_SMOOTH_CHANGE", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, TASK_MS,
                         EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                         S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
        }

        EE::WriteTime();
        APP::updDeltaColors();  // Send only changed LEDs (delta optimization)
        HB::StartEffect(true, false, false);
    }
}

/**
 * @brief  Dual-colour zone transition task -- fade out, map two colours, fade back in.
 */
void T_DUAL_COLOR(taskId_t taskId) {
    #ifdef ENABLE_LOG_LED_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : phase [%d] - dual color transition" NL, "T_DUAL_COLOR", TASK.Phase));
    #endif

    if (TASK.Phase == 1) {
        // --- PHASE 1: DIM ALL TO ZERO ---
        if (FadeAllToZero(EE::Get(EE_TV_ON_BR_CL_INC))) {
            Show();
        } else {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : phase 1 complete - applying dual color mapping" NL, "T_DUAL_COLOR"));
            #endif
            // --- PREPARE NEW COLORS ---
            uint8_t r1 = LED::State.TargetColor[0].r, g1 = LED::State.TargetColor[0].g, b1 = LED::State.TargetColor[0].b;
            uint8_t r2 = LED::State.TargetColor[1].r, g2 = LED::State.TargetColor[1].g, b2 = LED::State.TargetColor[1].b;

            setDualColorMapping(r1, g1, b1, r2, g2, b2);

            // Split HB symmetrically
            const int halfHb = LED_HB_NUM >> 1;
            for (int i = 0; i < LED_HB_NUM; i++) {
                LED::State.CurrentColor[HB(i)].r = LED::State.TargetColor[(i < halfHb) ? 0 : 1].r;
                LED::State.CurrentColor[HB(i)].g = LED::State.TargetColor[(i < halfHb) ? 0 : 1].g;
                LED::State.CurrentColor[HB(i)].b = LED::State.TargetColor[(i < halfHb) ? 0 : 1].b;
            }

            APP::updDeltaColors();
            TASK.Phase = 2;
            TSK::setTaskInterval("T_DUAL_COLOR", taskId, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL));
        }
    }
    else if (TASK.Phase == 2) {
        // --- PHASE 2: FADE TO NEW TARGET ---
        bool      moving    = false;
        const int fadeInc   = EE::Get(EE_TV_ON_BR_CL_INC);
        const int hbStart   = LED_START_I_HB;
        const int hbTgtBr   = getLuxBrightness(LED::State.StoredBrightness[hbStart]);

        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            const bool isHB    = IsHB(i);
            const int  targetBr = isHB ? hbTgtBr : getLuxBrightness(LED::State.StoredBrightness[i]);

            if (TG_BRIGHTNESS(i, targetBr, fadeInc, false)) {
                uint8_t r = isHB ? LED::State.CurrentColor[i].r : LED::State.StoredColor[i].r;
                uint8_t g = isHB ? LED::State.CurrentColor[i].g : LED::State.StoredColor[i].g;
                uint8_t b = isHB ? LED::State.CurrentColor[i].b : LED::State.StoredColor[i].b;
                setPixel(i, r, g, b, LED::State.CurrentBrightness[i], false);
                moving = true;
            }
        }

        if (moving) {
            Show();
        } else {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : phase 2 complete - dual color transition finished" NL, "T_DUAL_COLOR"));
            #endif

            TSK::KillTasksAvoidLocked("T_DUAL_COLOR");

            if (!TV::State.Status) {
                TSK::AddTask("T_DUAL_COLOR", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, TASK_MS,
                             EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                             S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
            }


            EE::WriteTime();
            APP::updDeltaColors();

            // Cache final state for breathing/heartbeat effects
            memcpy(LED::State.TargetColor, LED::State.CurrentColor, LED_NUM_TOTAL * sizeof(CRGB));

            HB::StartEffect(true, false, false);
            
        }
    }
}

/**
 * @brief  Shake dual-colour task -- chaotic strobe then smooth dual-colour fade.
 */
void T_SHAKE_DUAL_COLOR(taskId_t taskId) {
    #ifdef ENABLE_LOG_LED_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : phase [%d] - shake dual color" NL, "T_SHAKE_DUAL_COLOR", TASK.Phase));
    #endif

    const int brInc    = EE::Get(EE_TV_ON_BR_CL_INC);
    const int totalLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int halfHb   = LED_HB_NUM >> 1;

    // --- PHASE 1: SHAKE ---
    if (TASK.Phase == 1) {
        if (TASK.ParamB < 20) {
            byte randR[2] = { (byte)random(256), (byte)random(256) };
            byte randG[2] = { (byte)random(256), (byte)random(256) };
            byte randB[2] = { (byte)random(256), (byte)random(256) };

            // Main strip: TV zone split, all others default to side 0
            uint8_t tvSide[LED_NUM];
            for (int i = 0; i < LED_NUM; i++) {
                tvSide[i] = 0;
            }
            for (int i = 0; i < LED_TV_NUM; i++) {
                int ledIdx = TV(i);
                tvSide[ledIdx] = (i < (LED_TV_NUM >> 1)) ? 0 : 1;
            }

            for (int ledN = 0; ledN < totalLeds; ledN++) {
                int side = tvSide[ledN];
                setPixel(ledN, randR[side], randG[side], randB[side], LED::State.CurrentBrightness[ledN], false);
            }

            // HB strip: symmetric split
            for (int i = 0; i < LED_HB_NUM; i++) {
                int hIdx = HB(i);
                int side = (i < halfHb) ? 0 : 1;
                setPixel(hIdx, randR[side], randG[side], randB[side], LED::State.CurrentBrightness[hIdx], false);
            }

            Show();
            TASK.ParamB++;
            TSK::setTaskInterval("T_SHAKE_DUAL_COLOR", taskId, TASK_MS, 50);
        } else {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : phase 1 (shake) complete - transitioning to phase 2" NL, "T_SHAKE_DUAL_COLOR"));
            #endif
            TASK.Phase = 2; TASK.ParamB = 0;
            TSK::setTaskInterval("T_SHAKE_DUAL_COLOR", taskId, TASK_MS, 1);
        }
    }
    // --- PHASE 2: SET MAIN TARGETS ---
    else if (TASK.Phase == 2) {
        #ifdef ENABLE_LOG_LED_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : phase 2 - applying dual color mapping" NL, "T_SHAKE_DUAL_COLOR"));
        #endif

        setDualColorMapping(
            LED::State.TargetColor[0].r, LED::State.TargetColor[0].g, LED::State.TargetColor[0].b,
            LED::State.TargetColor[1].r, LED::State.TargetColor[1].g, LED::State.TargetColor[1].b);

        TASK.Phase = 3;
        TSK::setTaskInterval("T_SHAKE_DUAL_COLOR", taskId, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL));
    }
    // --- PHASE 3: SMOOTH TRANSITION ---
    else if (TASK.Phase == 3) {
        bool moving = false;
        const int hbTgtBr = getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);

        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            int tarR, tarG, tarB, targetBr;

            if (i >= LED_START_I_HB && i < LED_START_I_HB + LED_HB_NUM) {
                const int hLocal = i - LED_START_I_HB;
                const int side   = (hLocal < halfHb) ? 0 : 1;
                tarR     = LED::State.TargetColor[side].r;
                tarG     = LED::State.TargetColor[side].g;
                tarB     = LED::State.TargetColor[side].b;
                targetBr = hbTgtBr;
            } else {
                tarR     = LED::State.StoredColor[i].r;
                tarG     = LED::State.StoredColor[i].g;
                tarB     = LED::State.StoredColor[i].b;
                targetBr = getLuxBrightness(LED::State.StoredBrightness[i]);
            }

            bool cC = TG_COLOR(i, tarR, tarG, tarB, brInc);
            bool cB = TG_BRIGHTNESS(i, targetBr, brInc, false);

            if (cC || cB) {
                setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                moving = true;
            }
        }

        if (moving) {
            Show();
        } else {
            #ifdef ENABLE_LOG_LED_VERBOSE
                PRNT::_print(PRNT::formatMSG("%32s : phase 3 complete - shake dual color finished" NL, "T_SHAKE_DUAL_COLOR"));
            #endif

            TSK::KillTasksAvoidLocked("T_SHAKE_DUAL_COLOR");

            if (!TV::State.Status) {
                TSK::AddTask("T_SHAKE_DUAL_COLOR", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, TASK_MS,
                             EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                             S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
            }

            EE::WriteTime();
            APP::updDeltaColors();  // Send only changed LEDs (delta optimization)

            memcpy(LED::State.TargetColor, LED::State.CurrentColor, LED_NUM_TOTAL * sizeof(CRGB));
            HB::StartEffect(true, false, false);
        }
    }
}

/**
 * @brief  Ambient Mode animation task -- fade to ambient colours then auto-shutdown.
 *
 * Phase 1 (TASK.Phase=1): fade all LEDs to black.
 * Phase 2 (TASK.Phase=2): fade all LEDs up to LED::State.AmbientBackgroundColor at LED::State.AmbientBackgroundBrightness.
 *          On completion: waits EE_OTHER_AMBIENT_MODE_TIME minutes, then schedules Phase 3.
 * Phase 3 (TASK.Phase=3): fade all LEDs to black, reset AM/Motion/TV state, clear all LEDs.
 *
 * @param  taskId  Task handle used to reschedule each phase.
 *
 * @note   Launched by APP::AmbientMode() and chained from T_SMOOTH_CHANGE.
 */
void T_AMBIENT_MODE_ON(taskId_t taskId) { // Low-Power Environmental Lighting Mode
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    
    #ifdef ENABLE_LOG_TASK
        PRNT::_print(PRNT::formatMSG("%32s : executing phase [%d]" NL, "T_AMBIENT_MODE_ON", phase));
    #endif
    const int inc = EE::Get(EE_OTHER_BR_CL_INC);                         // Transition speed - Setup
    bool active = false;                                                // Activity tracker - State

    // --- PHASE 1 & 3: Fading to Black ---
    // Phase 1: Clear the room before starting Ambient Mode.
    // Phase 3: Shut down the room after the Ambient timer expires.
    if (phase == 1 || phase == 3) {
        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            if (LED::TG_BRIGHTNESS(i, 0, inc, false)) {                  // Fade toward zero - Logic
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                active = true;                                          // Still dimming - State
            }
        }

        if (active) {
            LED::Show();                                                 // Push intermediate frame - Sync
        } else {
            if (phase == 1) {
                #ifdef ENABLE_LOG_TASK
                    PRNT::_print(PRNT::formatMSG("%32s : phase 1 complete - transitioning to phase 2 (color fade)" NL, "T_AMBIENT_MODE_ON"));
                #endif
                TASK.Phase = 2;                                          // Transition to color phase - State
                TSK::setTaskInterval("T_AMBIENT_MODE_ON", taskId, TASK_MS, EE::Get(EE_OTHER_BR_CL_DEL)); 
            } else { 
                // --- SHUTDOWN FINALIZATION (End of Phase 3) ---
                #ifdef ENABLE_LOG_TASK
                    PRNT::_print(PRNT::formatMSG("%32s : phase 3 complete - shutting down ambient mode" NL, "T_AMBIENT_MODE_ON"));
                #endif
                APP::Am.Status = false;                                      // Ambient Mode flag OFF - State
                MOTION::State.Status = motON;                                  // Arm motion sensor - State
                TV::State.Status = false;                                      // Ensure TV logic is reset - State
                
                TSK::KillTasksAvoidLocked("T_AMBIENT_MODE_ON");             // Clean up task tree - Logic
                LED::setAll(0, 0, 0, 0);                                 // Hard reset buffer - Output
                LED::Show();                                             // Final black frame - Sync
                
                APP::updStatus("LED::T_AMBIENT_MODE_ON");                                    // Sync status to mobile - Sync
                APP::updDeltaColors();                                // Sync only changed colors - Sync
                DIF::AutoOff();                                          // Diffuser off if all sources idle - Action
            }
        }
        return;                                                         // Exit current execution - Logic
    }

    // --- PHASE 2: Fading to Ambient Color ---
    // Gradually brings the room up to the predefined "Ambient" color scheme.
    if (phase == 2) {
        for (int i = 0; i < LED_NUM; i++) {
            const bool isHB = LED::IsHB(i);                              // Identify Heartbeat - Logic
            const int subLoop = isHB ? LED_HB_NUM : 1;                  // Block iteration for HB - Setup
            const int targetBr = LED::getLuxBrightness(LED::State.AmbientBackgroundBrightness[i]);     // Lux-adjusted target - Logic

            for (int j = 0; j < subLoop; j++) {
                const int idx = isHB ? LED::HB(j) : i;                   // Hardware mapping - Mapping
                
                if (LED::TG_BRIGHTNESS(idx, targetBr, inc, false)) {     // Step toward brightness - Action
                    // Apply the specific Ambient RGB values while the brightness steps.
                    LED::setPixel(idx, LED::State.AmbientBackgroundColor[i].r, LED::State.AmbientBackgroundColor[i].g, LED::State.AmbientBackgroundColor[i].b, LED::State.CurrentBrightness[idx], false); 
                    active = true;                                      // Maintain animation - State
                }
            }
        }

        if (active) {
            LED::Show();                                                 // Render frame - Sync
        } else {
            // Ambient transition complete. Now, wait for the user-defined duration.
            #ifdef ENABLE_LOG_TASK
                PRNT::_print(PRNT::formatMSG("%32s : phase 2 complete - scheduling phase 3 after [%d] minutes" NL, "T_AMBIENT_MODE_ON", EE::Get(EE_OTHER_AMBIENT_MODE_TIME)));
            #endif
            TSK::KillTasksAvoidLocked("T_AMBIENT_MODE_ON");                 // Clear old timing - Logic
            TASK.Phase = 3;                                              // Set next phase to Shutdown - State
            
            // Convert user setting (minutes) to milliseconds for the scheduler.
            uint32_t delayMs = (uint32_t)EE::Get(EE_OTHER_AMBIENT_MODE_TIME) * 60000; 
            
            // Schedule Phase 3 (Fade to Off) to trigger after the delay.
            TSK::AddTask("T_AMBIENT_MODE_ON", "T_AMBIENT_MODE_ON", T_AMBIENT_MODE_ON, TASK_MS, EE::Get(EE_OTHER_BR_CL_DEL), delayMs, false); 
        
            APP::updStatus("LED::T_AMBIENT_MODE_ON");                                        // Sync status - Sync
            APP::updDeltaColors();                                   // Sync only changed colors - Sync
        }
    }
}



/**
 * @brief  Auto-off task -- smoothly fade all LEDs to black (sleep timer).
 */
void T_LEDS_TO_OFF(taskId_t taskId) {
    #ifdef ENABLE_LOG_LED_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : fading all LEDs to off" NL, "T_LEDS_TO_OFF"));
    #endif

    if (FadeAllToZero(EE::Get(EE_TV_ON_BR_CL_INC))) {
        Show();
    } else {
        #ifdef ENABLE_LOG_LED_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : fade complete - all LEDs off" NL, "T_LEDS_TO_OFF"));
        #endif

        setAll(0, 0, 0, 0);
        Show();

        MOTION::State.Status = motON;
        APP::updStatus("LED::T_LEDS_TO_OFF");
        APP::updColors_Force();
        TSK::KillTasksAvoidLocked("T_LEDS_TO_OFF");
    }
}

uint16_t BED(uint8_t i) {
    if (i >= LED_BED_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_BED", i));
        return LED_START_I_BED + (LED_BED_NUM - 1);
    }
    return LED_START_I_BED + i;
}

uint16_t COM(uint8_t i) {
    if (i >= LED_COM_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_COM", i));
        return LED_START_I_COM + (LED_COM_NUM - 1);
    }
    return LED_START_I_COM + i;
}

/**
 * @brief  Step every LED toward brightness 0. Returns true while any LED is still fading.
 *
 * Does NOT call Show() -- caller decides when to push.
 */
bool FadeAllToZero(uint8_t brInc) {
    bool anyChanged = false;
    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        if (TG_BRIGHTNESS(i, 0, brInc, false)) {
            setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
            anyChanged = true;
        }
    }
    return anyChanged;
}

/**
 * @brief  Count how many LEDs are marked as changed.
 */
uint8_t getChangedCount() {
    int count = 0;
    int bytes = (LED_NUM + 7) >> 3;  // (LED_NUM + 7) / 8
    for (int i = 0; i < bytes; i++) {
        count += __builtin_popcount(LED::State.Changed[i]);  // Hardware bit count - 7x faster
    }
    return count;
}

/**
 * @brief Generates a harmonious CHSV color pair using random harmony rules.
 * @param outA  Primary color output (HSV)
 * @param outB  Secondary color output (HSV), harmonically derived from A
 * Harmony types: complementary (+128), split-comp (+96/+160), triadic (+85/+170), tetradic (+64)
 */
void getHarmoniousPair(CHSV &outA, CHSV &outB) {
    // hue offsets (8-bit, 256 = full circle)
    static const uint8_t offsets[] = { 128, 96, 160, 85, 170, 64 };
    
    uint8_t hA  = random(256);                              // any hue
    uint8_t off = offsets[random(sizeof(offsets))];        // random harmony rule
    uint8_t hB  = hA + off;                                 // derived hue

    // saturation: 180-255 (vivid), value: 180-240 (bright, not blown)
    outA = CHSV(hA, random(180, 255), random(180, 240));
    outB = CHSV(hB, random(180, 255), random(180, 240));
}

/**
 * @brief  Lux auto-speed task delay -- higher lux level -> shorter delay -> faster colours.
 *
 * Divides @p del by getLuxAdaptFactor(). At lux level 1 with no pending
 * jump the factor is 1, so the raw EE value passes through unchanged.
 *
 * @param  del  Base delay from EE_SET (ms).
 *
 * @return Adapted delay, floored at 1 ms.
 */
uint16_t getLuxAdaptDelay(uint16_t del) {
    uint16_t d = del / getLuxAdaptFactor();                         // Shrink with lux level + jump - Math
    return (d < 1) ? 1 : d;                                             // Floor at 1 ms - Logic
}

/**
 * @brief  Lux auto-speed factor -- the single scaler behind Auto Inc / Auto Speed.
 *
 * factor = LISENS::State.Lux (1..4): more ambient light -> higher factor -> the
 * TV ON / TV OFF / MOTION animations move faster at brighter levels.
 * Level 1 keeps the raw EE values. Applies ONLY to those event animations --
 * the lux brightness-change transition (T_LUX_BR_CHANGE) and UDPRAW
 * streaming (T_UDPRAW_SET_COLOR) always run at the raw EE speed.
 *
 * @return Scaling factor, never below 1.
 */
uint16_t getLuxAdaptFactor() {
    int f = LISENS::State.Lux;                                                 // Current lux level - Math
    return (f < 1) ? 1 : (uint16_t)f;                                   // Floor at 1 - Logic
}

/**
 * @brief  Lux auto-inc brightness step -- higher lux level -> bigger step -> faster colours.
 *
 * Multiplies @p inc by getLuxAdaptFactor(). At lux level 1 with no
 * pending jump the factor is 1, so the raw EE value passes through unchanged.
 *
 * @param  inc  Base incremental step from EE_SET.
 *
 * @return Adapted increment, capped at 255.
 */
uint16_t getLuxAdaptInc(uint16_t inc) {
    uint32_t v = (uint32_t)inc * getLuxAdaptFactor();               // Grow with lux level + jump - Math
    return (v > 255) ? 255 : (uint16_t)v;                               // Cap at 255 - Logic
}



/**
 * @brief  Map a raw brightness value through the current ambient light level.
 *
 * Scales @p b from the range [0, APP_BRIGHT] to [3, LED_BRIGHTNESS_MAX],
 * then adds an offset proportional to LISENS::State.Lux and EE_SET[EE_OTHER_BRIGHTNESS_AUTO].
 * Result is clamped to [0, 255].
 *
 * @param  b  Raw brightness in [0, APP_BRIGHT] (from EEPROM or app command).
 *
 * @return Final hardware brightness in [0, 255], lux-compensated.
 */
uint8_t getLuxBrightness(int b) {
    static const int numThresholds = sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0]);
    static const int maxLuxLevel   = numThresholds + 1;

    int baseBr    = map(b, 0, APP_BRIGHT, 3, LED_BRIGHTNESS_MAX);
    int luxOffset = map(LISENS::State.Lux, 1, maxLuxLevel, 0, EE::Get(EE_OTHER_BRIGHTNESS_AUTO));

    return (uint8_t)constrain(baseBr + luxOffset, 0, 255);
}

/**
 * @brief  Generate a vivid random colour as a 0x00RRGGBB 32-bit value.
 */
uint32_t getRandomColor() {
    uint16_t randSeed16 = random(65536);
    uint8_t  rand8      = (uint8_t)((randSeed16 * 2053 + 13849 + (randSeed16 >> 8)) & 0xFF);

    uint8_t randIndex = 0, x = 0, y = 0, d = 0;
    while (d < 42) {
        randIndex = random(256);
        x = abs(rand8 - randIndex);
        y = 255 - x;
        d = (x < y) ? x : y;
    }

    randIndex = 255 - randIndex;
    if (randIndex < 85) {
        return ((uint32_t)(255 - randIndex * 3) << 16) | (uint32_t)(randIndex * 3);
    } else if (randIndex < 170) {
        randIndex -= 85;
        return ((uint32_t)(randIndex * 3) << 8) | (255 - randIndex * 3);
    } else {
        randIndex -= 170;
        return ((uint32_t)(randIndex * 3) << 16) | ((uint32_t)(255 - randIndex * 3) << 8);
    }
}

uint16_t HB(uint16_t i) {
    if (i >= LED_HB_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_HB", i));
        return LED_START_I_HB + (LED_HB_NUM - 1);
    }
    return LED_START_I_HB + i;
}

/* -- Helper functions (under setPixel per spec) ------------------- */

/**
 * @brief  Return the Lux-adjusted base brightness for the HB strip, capped to
 *         HB_BRIGHTNESS_MAX (same ceiling TG_BRIGHTNESS()/setPixel() enforce -
 *         callers that skip TG_BRIGHTNESS() and compare against this value
 *         directly still get a reachable target).
 *
 * Called by: APP::_Debug() (K0D debug dump), T_LUX_BR_CHANGE(), and 13 of
 * the 14 idle HB effects - T_EFFECT_HB_2_Heartbeat() through
 * T_EFFECT_HB_14_RainbowWavePulse() (only _1_WhiteMove doesn't use it).
 */
int HB_GetBaseBr() {
    int br = getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
    return (br > HB_BRIGHTNESS_MAX) ? HB_BRIGHTNESS_MAX : br;
}

/**
 * @brief  Set every HB pixel to the same colour and brightness.
 */
void HB_SetAll(uint8_t r, uint8_t g, uint8_t b, uint8_t br, bool show) {
    for (int i = 0; i < LED_HB_NUM; i++) {
        setPixel(HB(i), r, g, b, br, false);
    }
    if (show) Show();
}

/**
 * @brief  Set every HB pixel from its preColor at a given brightness.
 */
void HB_SetFromPreColor(uint8_t br, bool show) {
    for (int i = 0; i < LED_HB_NUM; i++) {
        int p = HB(i);
        setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
    }
    if (show) Show();
}

/**
 * @brief  Set a single HB pixel (by local index) from its preColor at a given brightness.
 */
void HB_SetPixelFromPreColor(int i, uint8_t br) {
    int p = HB(i);
    setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
}

/* Read one logical pixel back from its strip buffer (debug/verification only).
   NeoPixel getPixelColor() returns 0x00RRGGBB regardless of wire order. */
CRGB H_readStripPixel(int gIdx) {
    uint32_t c;
    if (gIdx < (LED_START_I_UCOM + LED_UCOM_NUM)) c = stripFront.getPixelColor(gIdx);
    else if (gIdx < LED_START_I_HB)               c = stripBack.getPixelColor(gIdx - LED_START_I_BED);
    else                                          c = stripHB.getPixelColor(gIdx - LED_START_I_HB);
    return CRGB((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

/**
 * @brief  Fast 3-phase HSV colour wheel -> RGB (integer only, no float, no library).
 */
void HsvToRgb(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (h < 85) {
        r = 255 - h * 3; g = h * 3;       b = 0;
    } else if (h < 170) {
        h -= 85;
        r = 0;           g = 255 - h * 3; b = h * 3;
    } else {
        h -= 170;
        r = h * 3;       g = 0;           b = 255 - h * 3;
    }
}

/* Write one logical pixel straight into its owning NeoPixel strip buffer.
   Global index -> (strip, local index). Replaces the old duplicate Pixel[]
   buffer + SyncToNeoPixel() copy: colours now land in the strip on write. */
void H_writeStripPixel(int gIdx, uint8_t r, uint8_t g, uint8_t b) {
    if (gIdx < (LED_START_I_UCOM + LED_UCOM_NUM)) {              // 0..41  -> front (TV+COM+UCOM)
        stripFront.setPixelColor(gIdx, stripFront.Color(r, g, b));
    } else if (gIdx < LED_START_I_HB) {                         // 42..59 -> back (BED+LAMP)
        stripBack.setPixelColor(gIdx - LED_START_I_BED, stripBack.Color(r, g, b));
    } else {                                                    // 60..   -> HB
        stripHB.setPixelColor(gIdx - LED_START_I_HB, stripHB.Color(r, g, b));
    }
}

/**
 * @brief  Check if a specific LED is marked as changed.
 */
bool IsChanged(int ledIdx) {
    if (ledIdx >= 0 && ledIdx < LED_NUM) {
        return BIT_TEST(LED::State.Changed, ledIdx);
    } else {
        PRNT::_print(PRNT::formatMSG("%32s ! LED index [%d] out of range [0-%d]" NL, "LED_IsChanged", ledIdx, LED_NUM - 1));
    }
    return false;
}

bool IsHB(int l) {
    return (l >= LED_START_I_HB);
}

bool IsSelected(int l) {
    if (l >= LED_NUM) return false;
    return BIT_TEST(LED::State.Selected, l);
}

uint16_t LAMP(uint8_t i) {
    if (i >= LED_LAMP_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_LAMP", i));
        return LED_START_I_LAMP + (LED_LAMP_NUM - 1);
    }
    return LED_START_I_LAMP + i;
}

/* -- Change Tracking Helpers (for Delta Updates) ---------------------- */

/**
 * @brief  Mark a single LED as changed since last app sync.
 */
void MarkChanged(int ledIdx) {
    if (ledIdx >= 0 && ledIdx < LED_NUM) {
        BIT_SET(LED::State.Changed, ledIdx);
    } else {
        PRNT::_print(PRNT::formatMSG("%32s ! LED index [%d] out of range [0-%d]" NL, "MarkChanged", ledIdx, LED_NUM - 1));
    }
}

/**
 * @brief  Mark a range of LEDs as changed (inclusive end).
 */
void MarkChangedRange(int start, int end) {
    bool overflow = false;
    if (start < 0) {
        PRNT::_print(PRNT::formatMSG("%32s ! start index [%d] < 0, clamped to 0" NL, "MarkChangedRange", start));
        overflow = true;
        start = 0;
    }
    if (end >= LED_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! end index [%d] >= LED_NUM [%d], clamped to [%d]" NL, "MarkChangedRange", end, LED_NUM, LED_NUM - 1));
        overflow = true;
        end = LED_NUM - 1;
    }
    // Direct bit marking without per-LED function call overhead (40% faster)
    for (int i = start; i <= end; i++) {
        BIT_SET(LED::State.Changed, i);
    }
}

/**
 * @brief  Apply a single colour to motion zones (COM/BED/HB) and cache the result.
 */
void MOTION_SetColor(int r, int g, int b, int br) {
    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [r:%d] [g:%d] [b:%d] [br:%d]" NL, "LED_MOTION_SetColor", r, g, b, br));
    #endif

    const int  brVal   = getLuxBrightness(br);
    const bool skipCOM = (MOTION::State.Status == motBED);

    if (!skipCOM) {
        for (int i = 0; i < LED_COM_NUM; i++) setPixel(COM(i), r, g, b, brVal, false);
    }
    for (int i = 0; i < LED_BED_NUM; i++) setPixel(BED(i), r, g, b, brVal, false);
    HB_SetAll(r, g, b, brVal, false);

    Show();

    MOTION::State.Color.r = r;
    MOTION::State.Color.g = g;
    MOTION::State.Color.b = b;
    EE::MarkMotionChanged();  // Track motion color change

    EE::WriteTime();
}

void Select(int l, bool state) {
    if (l >= LED_NUM) return;
    if (state) BIT_SET(LED::State.Selected, l);
    else       BIT_CLEAR(LED::State.Selected, l);
}

/**
 * @brief  Set every LED to the same colour and brightness, then push to hardware.
 */
void setAll(int r, int g, int b, int br) {
    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [r:%d] [g:%d] [b:%d] [br:%d]" NL, "LED_SetAll", r, g, b, br));
    #endif
    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        setPixel(i, r, g, b, br, false);
    }
    Show();
}

/**
 * @brief  Split all LED zones into a left-half / right-half dual-colour mapping.
 */
void setDualColorMapping(uint8_t r1, uint8_t g1, uint8_t b1,
                               uint8_t r2, uint8_t g2, uint8_t b2) {
    // Direct zone-range walk -- no search, no function pointers, no goto.
    // Each zone's LED indices are contiguous and known at compile time.
    struct ZoneInfo {
        int     start;   // physical start index (e.g. LED_START_I_TV = 0)
        int     count;   // number of LEDs in this zone
        int     brIdx;   // EE_Settings index for this zone's brightness
    };

    static const ZoneInfo zones[] = {
        { LED_START_I_TV,   LED_TV_NUM,   EE_TV_BR_TV   },
        { LED_START_I_COM,  LED_COM_NUM,  EE_TV_BR_COM  },
        { LED_START_I_UCOM, LED_UCOM_NUM, EE_TV_BR_UCOM },
        { LED_START_I_BED,  LED_BED_NUM,  EE_TV_BR_BED  },
        { LED_START_I_LAMP, LED_LAMP_NUM, EE_TV_BR_LAMP },
    };
    static const int NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

    for (int z = 0; z < NUM_ZONES; z++) {
        const ZoneInfo &zone = zones[z];
        const int       half = zone.count >> 1;
        const uint8_t   br   = EE::Get(zone.brIdx);

        for (int i = 0; i < zone.count; i++) {
            int n;
            if (zone.start == LED_START_I_UCOM) {
                // UCOM helper has inverse mapping for physical wiring,
                // so use it here to keep left/right semantics consistent.
                n = UCOM(i);
            } else {
                n = zone.start + i;   // direct physical index
            }
            const bool left = (i < half);        // left half gets color1

            LED::State.StoredColor[n]      = left ? CRGB(r1, g1, b1)
                                       : CRGB(r2, g2, b2);
            LED::State.StoredBrightness[n] = br;
            EE::MarkColorChanged(n);              // Track this LED's color change
        }
    }
    // Total iterations: 30+10+2+8+10 = 60
    // vs original: up to 3,600 comparisons with function pointer calls
}

/**
 * @brief  Set the colour and brightness of one LED and optionally push to hardware.
 *
 * Called by: the most-called function in the file (~115 call sites) -
 * every effect that changes a pixel calls this right after TG_BRIGHTNESS()/
 * TG_COLOR() report a step, across every T_EFFECT_TV_ON_x / TV_OFF_x / H_x,
 * T_EFFECT_MOTION_ON_x / MOTION_OFF, T_EFFECT_HB_1..14, T_SMOOTH_CHANGE,
 * T_DUAL_COLOR/T_SHAKE_DUAL_COLOR, T_AMBIENT_MODE_ON, T_LEDS_TO_OFF, plus
 * setAll()/HB_SetAll()/HB_SetPixelFromPreColor() and a handful of command
 * handlers. Not enumerated further - see each family's own "Called by:"
 * note instead.
 */
void setPixel(int p, int r, int g, int b, int brVal, bool s) {
    if (p >= LED_NUM_TOTAL) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid LED index [%d]" NL, "LED_SetPixel", p));
        return;
    }

    // Hard cap: HB (heartbeat) strip must never exceed HB_BRIGHTNESS_MAX, and
    // the main strip (TV/COM/UCOM/BED/LAMP) must never exceed
    // LED_BRIGHTNESS_MAX, no matter which effect/bloom/lux computation
    // produced brVal upstream. TG_BRIGHTNESS() already clamps its own target
    // to the same ceilings before stepping, so this is the backstop for any
    // caller that writes brightness directly without going through it.
    if (IsHB(p)) {
        if (brVal > HB_BRIGHTNESS_MAX) brVal = HB_BRIGHTNESS_MAX;
    } else {
        if (brVal > LED_BRIGHTNESS_MAX) brVal = LED_BRIGHTNESS_MAX;
    }

    uint8_t scaledR = (uint8_t)((r * brVal + 1) >> 8);
    uint8_t scaledG = (uint8_t)((g * brVal + 1) >> 8);
    uint8_t scaledB = (uint8_t)((b * brVal + 1) >> 8);

    uint8_t stR = (brVal > 0) ? (uint8_t)r : 0;
    uint8_t stG = (brVal > 0) ? (uint8_t)g : 0;
    uint8_t stB = (brVal > 0) ? (uint8_t)b : 0;

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [p:%d] [r:%d(%d)] [g:%d(%d)] [b:%d(%d)] [brVal:%d] [show:%T]" NL,
               "LED_SetPixel", p, scaledR, r, scaledG, g, scaledB, b, brVal, s));
    #endif

    // Check if the reported COLOUR actually changed before marking for app-sync.
    // Deliberately NOT comparing brightness here -- a brightness-only step (same
    // hue, different intensity) shouldn't spam the app's colour view on every
    // tick of a fade; stR/stG/stB already collapses to black at brVal==0
    // regardless of hue, so on/off transitions still show up as a colour change
    // here without any extra special-casing. See TG_BRIGHTNESS() for the same
    // reasoning applied to its own (redundant safety-net) MarkChanged() call.
    bool changed = (LED::State.CurrentColor[p].r != stR || LED::State.CurrentColor[p].g != stG || LED::State.CurrentColor[p].b != stB);

    LED::State.CurrentColor[p]      = CRGB(stR, stG, stB);
    LED::State.CurrentBrightness[p] = brVal;

    if (p == LED_START_I_HB) { scaledR = 0; scaledG = 0; scaledB = 0; }

    LED::H_writeStripPixel(p, scaledR, scaledG, scaledB);

    if (changed) {
        int markIdx = (p >= LED_START_I_HB) ? LED_START_I_HB : p;  // Clamp HB indices to marker
        MarkChanged(markIdx);
    }

    if (s) Show();
}

/**
 * @brief  Enable or disable the random-colour-on-TV-on feature.
 */
void setRandomColor(bool z) {
    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [%T]" NL, "LED_SetRandomColor", z));
    #endif
    EE::Set(EE_TV_RANDOM_COLOR_START, z);
}

/**
 * @brief  Set the target color for all selected LEDs.
 *
 * Recomputes the selected LED cache if needed, then stores the target
 * RGB values into LED::State.TargetColor[] for each selected LED::State.
 */
void setColorToSelected(uint8_t r, uint8_t g, uint8_t b) {
    if (APP::State.SelectedCacheDirty) {
        APP::RefreshSelectedCache();
    }
    for (int i = 0; i < APP::State.SelectedCount; i++) {
        int idx = APP::State.SelectedLedCache[i];
        LED::State.TargetColor[idx].r = r;
        LED::State.TargetColor[idx].g = g;
        LED::State.TargetColor[idx].b = b;
    }
}

/**
 * @brief  Set EEPROM brightness values for all selected LEDs.
 *
 * Recomputes the selected LED cache if needed, then updates the
 * persisted brightness values in LED::State.StoredBrightness[] for the selected set.
 */
void setBrightnessToSelected(uint8_t br) {
    if (APP::State.SelectedCacheDirty) {
        APP::RefreshSelectedCache();
    }
    for (int i = 0; i < APP::State.SelectedCount; i++) {
        LED::State.StoredBrightness[APP::State.SelectedLedCache[i]] = br;
    }
}

/**
 * @brief  Current LED refresh-rate cap, ms, from EE_OTHER_LED_FPS (Settings > OTHER > LED FPS).
 *
 * @return Refresh period in ms, looked up in LED_FPS_TABLE by EEPROM index;
 *         falls back to LED_FPS_DEFAULT_INDEX (120 fps) if the stored index is out of range.
 */
uint16_t getFpsLimitMs() {
    uint8_t idx = EE::Get(EE_OTHER_LED_FPS);
    if (idx >= LED_FPS_OPTIONS_TOTAL) idx = LED_FPS_DEFAULT_INDEX;      // Guard unset/bad EEPROM value - Logic
    return pgm_read_byte(&LED_FPS_TABLE[idx]);                          // ms period for that fps - Mapping
}

bool ShouldRefresh(uint32_t now) {
    return LED::State.NeedsUpdate && (LED::State.LastUpdateTime == 0 || (uint32_t)(now - LED::State.LastUpdateTime) >= getFpsLimitMs());
}

/**
 * @brief  Fisher-Yates in-place shuffle of a uint8_t array.
 */
void shuffleArray(uint8_t *array, int size) {
    for (int n = size - 1; n > 0; n--) {
        uint8_t r    = random(n + 1);
        uint8_t t    = array[n];
        array[n]     = array[r];
        array[r]     = t;
    }
}

/**
 * @brief  Step LED::State.CurrentBrightness[led] one increment toward a target brightness.
 *
 * @note   brVal is clamped to the zone's hardware ceiling (HB_BRIGHTNESS_MAX
 *         for HB, LED_BRIGHTNESS_MAX for the main strip) before stepping.
 *         Without this, a caller passing a raw lux-boosted target above the
 *         ceiling (StoredBrightness + LISENS auto-brightness offset can push
 *         well past 120/100) would step CurrentBrightness up while
 *         setPixel() silently clamps every write back down - current never
 *         reaches that unreachable target, TG_Step() never reports
 *         convergence, and the calling effect's "done expanding/blooming"
 *         check never fires. Confirmed live: this is exactly what froze the
 *         TV-on HB Quad-Point and Center-Bloom effects partway through
 *         (only the first ~4 anchor pixels ever lit, forever) whenever the
 *         configured HB brightness setting's lux-adjusted value exceeded
 *         HB_BRIGHTNESS_MAX. Clamping here fixes every current and future
 *         caller in one place, rather than each effect needing to know and
 *         apply the right zone ceiling itself before computing its target.
 *
 * Called by: the single most shared LED primitive in the file (~74 call
 * sites) - effectively every brightness-changing effect goes through this,
 * including T_SMOOTH_CHANGE, T_DUAL_COLOR, T_SHAKE_DUAL_COLOR,
 * T_AMBIENT_MODE_ON, T_LEDS_TO_OFF, every T_EFFECT_TV_ON_x / TV_OFF_x / H_x
 * sub-effect, every T_EFFECT_MOTION_ON_x / MOTION_OFF sub-effect, every
 * T_EFFECT_HB_1..14 idle effect, plus setBrightnessToSelected() and
 * cmdChangeBrightness(). Not enumerated further - see each family's own
 * "Called by:" note for its specific callers instead.
 */
bool TG_BRIGHTNESS(int led, uint8_t brVal, uint8_t inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid LED index [%d]" NL, "LED_TG_BRIGHTNESS", led));
        return false;
    }
    {
        const uint8_t cap = IsHB(led) ? HB_BRIGHTNESS_MAX : LED_BRIGHTNESS_MAX;
        if (brVal > cap) brVal = cap;
    }
    inc = (inc > 0) ? inc : 1;
    const uint8_t before = LED::State.CurrentBrightness[led];              // Snapshot pre-step, for the on/off check below - Setup
    bool changed = TG_Step(LED::State.CurrentBrightness[led], brVal, inc);

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [led:%d] [brVal:%d] [inc:%d] [changed:%T]" NL,
               "LED_TG_BRIGHTNESS", led, brVal, inc, changed));
    #endif

    // Only flag the app-sync on an on/off boundary crossing (0 -> nonzero or
    // nonzero -> 0) -- an in-between step (e.g. 20 -> 100 at the same colour)
    // doesn't change what LED::State.CurrentColor[] reports, so the app's colour
    // view doesn't need to hear about every tick of a brightness-only fade.
    // setPixel()'s own colour-only "changed" check (see below) independently
    // catches the same boundary too, since it stores black at brightness 0
    // regardless of hue -- this is just a safety net for callers that step
    // brightness without a following setPixel() call.
    if (changed) {
        const bool wasOff = (before == 0);
        const bool isOff  = (LED::State.CurrentBrightness[led] == 0);
        if (wasOff != isOff) {
            int markIdx = (led >= LED_START_I_HB) ? LED_START_I_HB : led;  // Clamp HB indices to marker
            MarkChanged(markIdx);
        }
    }
    if (lisensReset) LISENS::ResetTime();
    return changed;
}

/**
 * @brief  Step LED::State.CurrentColor[led] one increment toward a target RGB in the live colour buffer.
 *
 * Called by: T_SMOOTH_CHANGE(), T_SHAKE_DUAL_COLOR(), T_EFFECT_TV_ON_2_MidToOutSep(),
 * T_EFFECT_TV_ON_3_MidToOutAll(), T_MOTION_CHANGE_COLOR().
 */
bool TG_COLOR(int led, int r, int g, int b, int inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid LED index [%d]" NL, "LED_TG_COLOR", led));
        return false;
    }
    inc = (inc > 0) ? inc : 1;
    bool changed = false;

    if (TG_Step(LED::State.CurrentColor[led].r, r, inc)) changed = true;
    if (TG_Step(LED::State.CurrentColor[led].g, g, inc)) changed = true;
    if (TG_Step(LED::State.CurrentColor[led].b, b, inc)) changed = true;

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [led:%d] [r:%d] [g:%d] [b:%d] [inc:%d] [changed:%T]" NL,
               "LED_TG_COLOR", led, r, g, b, inc, changed));
    #endif

    if (changed) {
        int markIdx = (led >= LED_START_I_HB) ? LED_START_I_HB : led;  // Clamp HB indices to marker
        MarkChanged(markIdx);
    }
    if (lisensReset) LISENS::ResetTime();
    return changed;
}

/* -- Core pixel operations --------------------------------------------- */

/**
 * @brief  Step a single uint8_t value one increment toward a target.
 *
 * Called by: internal only - TG_BRIGHTNESS(), TG_COLOR(), TG_TEMPCOLOR()
 * (once per colour channel for the latter two).
 */
bool TG_Step(uint8_t &current, uint8_t target, uint8_t inc) {
    if (current == target) return false;
    int diff = target - current;
    if (abs(diff) <= inc) current = target;
    else                  current += (diff > 0) ? inc : -(int)inc;
    return true;
}

/**
 * @brief  Step LED::State.TargetColor[led] one increment toward a target RGB in the temp buffer.
 *
 * Called by: T_EFFECT_TV_ON_1_RandomStatic(), T_EFFECT_TV_ON_10_LiquidFill(),
 * T_EFFECT_TV_ON_11_PixelBoot().
 */
bool TG_TEMPCOLOR(int led, int r, int g, int b, int inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid LED index [%d]" NL, "LED_TG_TEMPCOLOR", led));
        return false;
    }
    inc = (inc > 0) ? inc : 1;
    bool changed = false;

    if (TG_Step(LED::State.TargetColor[led].r, r, inc)) changed = true;
    if (TG_Step(LED::State.TargetColor[led].g, g, inc)) changed = true;
    if (TG_Step(LED::State.TargetColor[led].b, b, inc)) changed = true;

    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [led:%d] [r:%d] [g:%d] [b:%d] [inc:%d] [changed:%T]" NL,
               "LED_TG_TEMPCOLOR", led, r, g, b, inc, changed));
    #endif

    if (lisensReset) LISENS::ResetTime();
    return changed;
}


/* -- Zone index helpers ------------------------------------------------ */

uint16_t TV(uint8_t i) {
    if (i >= LED_TV_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_TV", i));
        return LED_START_I_TV + (LED_TV_NUM - 1);
    }
    return LED_START_I_TV + i;
}

uint16_t UCOM(uint8_t i) {
    if (i >= LED_UCOM_NUM) {
        PRNT::_print(PRNT::formatMSG("%32s ! value out of range [%d]" NL, "in_LED_UCOM", i));
        return LED_START_I_UCOM;
    }
    return LED_START_I_UCOM + (LED_UCOM_NUM - 1u - i);
}

/* -- Zone color helpers ------------------------------------------------ */

/**
 * @brief  Apply a single colour to all non-TV zones (COM, BED, LAMP, HB) for UDPRAW mode.
 */
void UDPRAW_SetColor(int r, int g, int b, int br) {
    #ifdef ENABLE_LOG_LED
        PRNT::_print(PRNT::formatMSG("%~32s # [r:%d] [g:%d] [b:%d] [br:%d]" NL, "LED_UDPRAW_SetColor", r, g, b, br));
    #endif

    const int brVal = getLuxBrightness(br);

    for (int i = 0; i < LED_COM_NUM;  i++) setPixel(COM(i),  r, g, b, brVal, false);
    for (int i = 0; i < LED_BED_NUM;  i++) setPixel(BED(i),  r, g, b, brVal, false);
    for (int i = 0; i < LED_LAMP_NUM; i++) setPixel(LAMP(i), r, g, b, brVal, false);
    HB_SetAll(r, g, b, brVal, false);

    Show();

    LED::State.StreamColor.r    = r;
    LED::State.StreamColor.g    = g;
    LED::State.StreamColor.b    = b;
    LED::State.StreamBrightness = br;
    EE::MarkUdpChanged();  // Track UDP color change

    EE::WriteTime();
}
} // namespace LED


namespace LISENS {
/* ------------------------------------------------------------------------ */
/* LISENS                                                                     */
/* Ambient light sensor - lux callbacks                                       */
/* ------------------------------------------------------------------------ */


/**
 * @brief  Initialise the ambient light sensor and start the sampling task.
 *
 * Sets ADC resolution to 12-bit and registers Check as a repeating
 * task running every (LIGHT_SENSOR_CHECK_TIME / LIGHT_SENSOR_SAMPLES) ms
 * so that LIGHT_SENSOR_SAMPLES readings are collected per CHECK_TIME window.
 * Call once in setup().
 */
void Setup() {
	// * Analog Read Resolution
	analogReadResolution(12);

	// Read initial ambient light value immediately so setup can report a real sensor state.
	uint16_t sensorValue = analogRead(LIGHT_SENSOR_PIN);
	LISENS::State.Average = sensorValue;

	int initialLux = 1;
	const int numThresholds = sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0]);
	for (int i = 0; i < numThresholds; i++) {
		if (sensorValue < LIGHT_SENS_LUX[i]) {
			initialLux = i + 1;
			break;
		}
	}
	if (sensorValue >= LIGHT_SENS_LUX[numThresholds - 1]) {
		initialLux = numThresholds + 1;
	}
	LISENS::State.Lux = initialLux;
    
	PRNT::_print(PRNT::formatMSG("%32s : ambient sensor init raw [%d] lux [%d] avg [%d]" NL,
		"LISENS_Setup", sensorValue, LISENS::State.Lux, LISENS::State.Average));

	// TASK
    LISENS::State.TaskID = TSK::AddTask("LISENS_Setup", "LISENS_Check", Check, TASK_MS, (LIGHT_SENSOR_CHECK_TIME / LIGHT_SENSOR_SAMPLES), 0, true);
}

/**
 * @brief  Accumulate ADC samples and update LISENS::State.Lux when a full window completes.
 *
 * Reads LIGHT_SENSOR_PIN, accumulates into LISENS::State.SampleSum, and increments
 * LISENS::State.SampleCount. When SampleCount reaches LIGHT_SENSOR_SAMPLES, the average
 * is computed and compared against the LIGHT_SENS_LUX[] threshold table with
 * hysteresis (LIGHT_SENSOR_LUX_FIX). If Lux changes, triggers LISENS_Change_TV()
 * or LISENS_Change_UDPRAW() depending on current mode, and re-pushes the
 * lux-compensated diffuser brightness via DIF::PushLiveIfActive() (no-op if
 * nothing's currently driving the diffuser).
 *
 * @param  taskId  Task handle supplied by the scheduler (unused).
 *
 * @note   Do not call directly -- registered via AddTask in Setup().
 */
void Check(taskId_t taskId) {
    uint16_t sensorValue = analogRead(LIGHT_SENSOR_PIN);                // Read sensor - Input
    LISENS::State.SampleSum += sensorValue;                                    // Accumulate - Math
    LISENS::State.SampleCount++;                                               // Increment - State

    if (LISENS::State.SampleCount >= LIGHT_SENSOR_SAMPLES) {
        LISENS::State.Average = LISENS::State.SampleSum / LISENS::State.SampleCount;         // Calculate average - Math

        const int avg = LISENS::State.Average;                                 // Cache average - Setup
        const int fix = LIGHT_SENSOR_LUX_FIX;                           // Cache base hysteresis - Setup
        const int numThresholds = sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0]); // Size - Logic
        
        int nowLux = LISENS::State.Lux;                                        // Default to current - State
        int rawTarget = 1;                                              // Baseline level - Setup

        // --- PHASE 1: DETERMINE RAW BRACKET ---
        if (avg >= LIGHT_SENS_LUX[numThresholds - 1]) {
            rawTarget = numThresholds + 1;                              // Max level - Logic
        } else {
            for (int i = 0; i < numThresholds; i++) {
                if (avg < LIGHT_SENS_LUX[i]) {
                    rawTarget = i + 1;                                  // Found bracket - Logic
                    break;
                }
            }
        }

        // --- PHASE 2: APPLY HYSTERESIS LOGIC ---
        if (rawTarget > LISENS::State.Lux) {                                   // MOVING UP - Logic
            // Need to exceed threshold + base fix to climb
            int thresholdIdx = LISENS::State.Lux - 1;                          // Index of level we are leaving - Mapping
            if (avg >= (LIGHT_SENS_LUX[thresholdIdx] + fix)) {
                nowLux = rawTarget;                                     // Allow climb - State
            }
        } 
        else if (rawTarget < LISENS::State.Lux) {                              // MOVING DOWN - Logic
            // Requirement: Level 2->1 needs (Thresh[0] - Fix)
            // Level 3->2 needs (Thresh[1] - Fix * 2), etc.
            int dropIdx = rawTarget - 1;                                // Threshold index to drop into - Mapping
            int multiplier = rawTarget;                                 // Multiplier equals target level - Math
            int dropLimit = LIGHT_SENS_LUX[dropIdx] - (fix * multiplier); // Calculate floor - Math

            if (avg <= dropLimit) {
                nowLux = rawTarget;                                     // Allow drop - State
            }
        }

        // * LOG - lux window summary: average, raw bracket, hysteresis result
        #ifdef ENABLE_LOG_LUX
            PRNT::_print(PRNT::formatMSG("%32s : avg [%d] raw [%d] lux [%d]->[%d]%s" NL, "LUX_Window",
                             avg, rawTarget, LISENS::State.Lux, nowLux,
                             (TestMode == _testmode_lux) ? " (HELD by lux test)" : ""));
        #endif

        // --- PHASE 3: TRIGGER UPDATES ---
        // Held while the app forces a lux level ('@Lvv') -- the sensor must not
        // override the test value until T_END_TEST_MODE releases it.
        if (TestMode != _testmode_lux) setLux(nowLux);         // Apply level + adaptive transition - Action
        APP::updLux();                                               // Update App UI - Action

        LISENS::State.SampleSum = 0;                                           // Reset sum - Setup
        LISENS::State.SampleCount = 0;                                         // Reset count - Setup
    }
}

/**
 * @brief  Apply a new lux level -- the single choke point for every lux change.
 *
 * Saves the new level (which drives LED::getLuxAdaptFactor() for the TV/MOTION
 * event animations), registers T_UDPRAW_SET_COLOR or T_LUX_BR_CHANGE at the
 * RAW EE speed (those two transitions are deliberately not speed-adapted) and
 * re-pushes the lux-compensated diffuser brightness. No-op when unchanged.
 * Called by Check() (sensor) and APP::TestLux() ('@Lvv' test).
 *
 * @param  newLux  Target lux level, 1..(threshold count + 1).
 */
void setLux(int newLux) {
    if (LISENS::State.Lux == newLux) return;                                   // Nothing changed - Logic

    const int oldLux = LISENS::State.Lux;                                      // For debug - State
    LISENS::State.Lux      = newLux;                                           // Save state first (tasks read it) - State

    // * LOG - lux change: new auto-speed factor and the TV/MOTION values it produces
    #ifdef ENABLE_LOG_LUX
        PRNT::_print(PRNT::formatMSG("%~32s # lux [%d]->[%d] factor [%d] : TVdel/inc [%d/%d] MOTIONdel/inc [%d/%d]" NL,
                         "LUX_Apply", oldLux, newLux, LED::getLuxAdaptFactor(),
                         LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)),  LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC)),
                         LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC))));
    #endif

    if (LED::State.Enabled) {                                                  // Only animate when strip is on - Logic
        if (UDPRAW::State.Status) {
            // Deliberately NOT re-armed here while a stream is active - each
            // re-arm makes T_UDPRAW_SET_COLOR ramp 206 non-TV pixels and
            // re-latch the 238-LED back/HB strips (see UDPRAW::Loop()'s
            // g_nonTvDirty) for the rest of the ramp, purely because ambient
            // room lux ticked over - not worth the extra per-packet show()
            // cost during a low-latency ambilight stream. LISENS::State.Lux
            // is already updated above, so getLuxBrightness() reads the
            // current value the next time anything else re-arms this task
            // (e.g. the next stream's Init()) - this just skips forcing it
            // to happen mid-stream, not the value itself going stale.
            #ifdef ENABLE_LOG_LUX
                PRNT::_print(PRNT::formatMSG("%32s : path UDPRAW - re-arm skipped (stream active)" NL, "LUX_Apply"));
            #endif
        } else if (TV::State.Status) {                                         // Update TV - Action
            if (TV::State.Transitioning) {
                // The on/off transition task (T_EFFECT_TV_ON/_OFF) is still
                // running -- most commonly right after the TV switches on, which
                // is itself a real ambient-light change and can trigger this
                // exact lux re-arm mid-fade. KillTasksAvoidLocked() below kills
                // EVERY unlocked task (the string is a log label, not a filter),
                // and T_EFFECT_TV_ON/_OFF run unlocked -- so without this guard,
                // the transition gets killed right as it's running, leaving HB
                // (and the TV strip) stuck at whatever partial brightness
                // they'd reached. LISENS::State.Lux is already updated above,
                // so the next re-arm once the transition finishes picks up the
                // current value correctly -- this only skips forcing it mid-fade.
                //
                // NOTE: this used to check LED::IsTransitioning() (any LED whose
                // CurrentColor differs from TargetColor) instead, which looked
                // right in a quick live test but doesn't hold up: HB's default
                // fade-on only steps CurrentBrightness, never CurrentColor, so
                // TargetColor (set once by TV::On()) never reconciles with
                // CurrentColor for as long as the TV stays on -- confirmed live
                // via added diagnostics (LED index 237 stuck cur=#000000 vs
                // tgt=#002AD5 25+ seconds after the transition had finished).
                // That would have silently blocked this re-arm indefinitely,
                // not just during the transition. TV::State.Transitioning is an
                // explicit, correctly-scoped flag instead.
                #ifdef ENABLE_LOG_LUX
                    PRNT::_print(PRNT::formatMSG("%32s : path TV - re-arm skipped (transition in progress)" NL, "LUX_Apply"));
                #endif
            } else {
                TSK::KillTasksAvoidLocked("LISENS_Change_TV");
                TSK::AddTask("LISENS_Change_TV", "T_LUX_BR_CHANGE", LED::T_LUX_BR_CHANGE, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false); // Raw EE - not adapted
                // * LOG
                #ifdef ENABLE_LOG_LUX
                    PRNT::_print(PRNT::formatMSG("%32s : path TV - T_LUX_BR_CHANGE re-armed" NL, "LUX_Apply"));
                #endif
            }
        }
        // * LOG
        #ifdef ENABLE_LOG_LUX
            else PRNT::_print(PRNT::formatMSG("%32s : path none - no active source to animate" NL, "LUX_Apply"));
        #endif
    }

    // Lux level shifted -- re-push the lux-compensated diffuser brightness right now
    // if a source is currently driving it (no-op otherwise). LISENS::State.Lux is already
    // updated above, so LED::getLuxBrightness() inside DIF::TurnOn() picks up the new
    // level immediately instead of waiting for the next auto-on trigger.
    DIF::PushLiveIfActive();                                             // Sync diffuser - Action
}

/**
 * @brief  Reset the sample accumulator and reschedule the LISENS task immediately.
 *
 * Clears SampleSum and SampleCount, then calls _TASK.resetTaskTimer(LISENS::State.TaskID)
 * to restart the sampling window from now. Call after any LED brightness transition
 * to prevent stale readings from triggering an unwanted lux change.
 */
void ResetTime() {
    LISENS::State.SampleSum = 0;                                           // Reset sum - Setup
    LISENS::State.SampleCount = 0; 
	_TASK.resetTaskTimer(LISENS::State.TaskID);
}
} // namespace LISENS


namespace HB {
/* ------------------------------------------------------------------------ */
/* HB                                                                         */
/* Heartbeat strip effects 1-14                                               */
/* ------------------------------------------------------------------------ */


/* -- Function pointer table for heartbeat effect dispatch -- moved to DEF.h -- */
/* (HBEffectHandler, HB_EFFECT_HANDLERS[], HB_EFFECT_HANDLERS_COUNT) */

/* -------------------------------------------------------------------------- */

/**
 * @brief  Heartbeat effect dispatcher -- routes to the selected sub-effect each tick.
 */
void T_EFFECT_HB(taskId_t taskId) {
    int hbEffIdx = EE::Get(EE_HB_EFFECT);
    if (hbEffIdx > 0 && hbEffIdx <= HB_EFFECT_HANDLERS_COUNT) {
        HB_EFFECT_HANDLERS[hbEffIdx - 1](taskId);
    } else {
        TSK::KillID(taskId, "T_EFFECT_HB");
    }
}

/* -- Common interval helper (DRY) ------------------------------------- */
// Each effect calls setTaskInterval with its own name string.
// Wrap it as a tiny inline to reduce call-site boilerplate.

/* -- Effect 1 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 1 -- moving 3-pixel white highlight across the strip.
 */
void T_EFFECT_HB_1_WhiteMove(taskId_t taskId) {
    const int num     = LED_HB_NUM;
    const int rawHbBr = LED::State.StoredBrightness[LED_START_I_HB];
    int dotBr = LED::getLuxBrightness(rawHbBr) * 2;
    if (dotBr > 255) dotBr = 255;
    const int bgBr = LED::getLuxBrightness(rawHbBr);

    if (TASK.Phase <= 1) {
        TASK.Phase = 1;
        const int d1 = TASK.ParamA;

        for (int i = 0; i < num; i++) {
            int p = LED::HB(i);
            if (i == d1 || i == d1 + 1 || i == d1 + 2) {
                LED::setPixel(p, 255, 255, 255, dotBr, false);
            } else {
                LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, bgBr, false);
            }
        }
        LED::Show();

        if (d1 >= num) {
            TASK.Phase = 2;
            TSK::setTaskInterval("T_EFFECT_HB_1_WhiteMove", taskId, TASK_MS, random(5000, 10000));
        } else {
            TASK.ParamA++;
            TSK::setTaskInterval("T_EFFECT_HB_1_WhiteMove", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
        }
    }
    else if (TASK.Phase == 2) {
        TASK.ParamA   = 0;
        TASK.Phase = 1;
        TSK::setTaskInterval("T_EFFECT_HB_1_WhiteMove", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 2 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 2 -- sine-wave pulse mimicking a heartbeat.
 */
void T_EFFECT_HB_2_Heartbeat(taskId_t taskId) {
    const int  num     = LED_HB_NUM;
    const bool dual    = EE::Get(EE_HB_DUAL_COLOR);
    const int  baseLux = LED::HB_GetBaseBr();
    int        curBr   = baseLux;
    int        animationAngle = TASK.ParamA;

    if (TASK.Phase == 0 && animationAngle == 0) HB::State.Phase = random(2, 5);

    if (TASK.Phase < (int)HB::State.Phase) {
        float angle = animationAngle * (PI / 180.0f);
        curBr = baseLux + (int)(baseLux * sin(angle));
    }

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int ref = (dual && i >= (num >> 1)) ? LED::UCOM(1)
                : (dual                    ? LED::UCOM(0)
                                           : LED_START_I_HB);
        LED::setPixel(p, LED::State.StoredColor[ref].r, LED::State.StoredColor[ref].g, LED::State.StoredColor[ref].b, curBr, false);
    }
    LED::Show();

    if (TASK.Phase < (int)HB::State.Phase) {
        animationAngle += 20;
        if (animationAngle >= 180) {
            animationAngle = 0;
            TASK.Phase++;
            TSK::setTaskInterval("T_EFFECT_HB_2_Heartbeat", taskId, TASK_MS, random(60, 150));
        } else {
            TSK::setTaskInterval("T_EFFECT_HB_2_Heartbeat", taskId, TASK_MS, 15);
        }
    } else {
        TASK.Phase = 0; animationAngle = 0;
        TSK::setTaskInterval("T_EFFECT_HB_2_Heartbeat", taskId, TASK_MS, random(1500, 4500));
    }

    TASK.ParamA = animationAngle;
}

/* -- Effect 3 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 3 -- sine-pulse on a randomly selected segment of the strip.
 */
void T_EFFECT_HB_3_RandomFade(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.ParamA   = 1;
        TASK.ParamB   = 0;
        int wide  = random(3, 26);
        int pos   = random(0, num - wide);
        TASK.Phase = (pos << 8) | wide;
    }

    const int pos   = TASK.Phase >> 8;
    const int wide  = TASK.Phase & 0xFF;
    float     angle = TASK.ParamB * (PI / 180.0f);
    int       curBr = (int)(maxBr * sin(angle));

    for (int i = 0; i < num; i++) {
        bool isPart = (i >= pos && i < pos + wide);
        int  tgtBr  = isPart ? curBr : 0;
        int  p      = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    TASK.ParamB += 4;
    if (TASK.ParamB >= 180) {
        TASK.ParamB   = 0;
        int nWide = random(3, 26);
        int nPos  = random(0, num - nWide);
        TASK.Phase = (nPos << 8) | nWide;
        TSK::setTaskInterval("T_EFFECT_HB_3_RandomFade", taskId, TASK_MS, random(100, 400));
    } else {
        TSK::setTaskInterval("T_EFFECT_HB_3_RandomFade", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 4 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 4 -- moving dark shadow window travelling across the strip.
 */
void T_EFFECT_HB_4_TravelingShadow(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();
    const int minBr = (maxBr * 40) / 100;

    if (TASK.ParamA == 0) {
        TASK.ParamA   = 1;
        TASK.Phase = random(5, 50);
        TASK.ParamB   = 0;
    }

    const int shadowWidth = TASK.Phase;
    const int shadowPos   = TASK.ParamB - shadowWidth;

    for (int i = 0; i < num; i++) {
        bool isShadow = (i >= shadowPos && i < shadowPos + shadowWidth);
        int  tgtBr    = isShadow ? minBr : maxBr;
        int  p        = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    TASK.ParamB++;
    if (TASK.ParamB > num + shadowWidth) {
        TASK.ParamA = 0;
        TSK::setTaskInterval("T_EFFECT_HB_4_TravelingShadow", taskId, TASK_MS, random(2000, 5000));
    } else {
        TSK::setTaskInterval("T_EFFECT_HB_4_TravelingShadow", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 5 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 5 -- ripple expanding outward from a random drop centre.
 */
void T_EFFECT_HB_5_ExpandingRaindrops(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.Phase = random(0, num);
        TASK.ParamB   = 0;
        TASK.ParamA   = 1;
    }

    const int dropCenter = TASK.Phase;

    for (int i = 0; i < num; i++) {
        int dist   = abs(i - dropCenter);
        int tgtBr  = 0;
        if (dist <= TASK.ParamB) {
            int dropFade = maxBr - (dist * (maxBr / 10));
            tgtBr = (dropFade < 0) ? 0 : dropFade;
        }
        int p = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    TASK.ParamB++;
    if (TASK.ParamB > 10) {
        TASK.ParamA = 0;
        TSK::setTaskInterval("T_EFFECT_HB_5_ExpandingRaindrops", taskId, TASK_MS, random(200, 800));
    } else {
        TSK::setTaskInterval("T_EFFECT_HB_5_ExpandingRaindrops", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 6 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 6 -- animated flowing colour palette wipe.
 */
void T_EFFECT_HB_6_Colors(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.ParamA   = 1;
        TASK.ParamB   = 0;
        TASK.Phase = 0;
        randomSeed(millis());
    }

    const int colorCount = 8;
    const int totalScale = colorCount * 256;

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int pos = (i * 60 - TASK.Phase) % totalScale;
        if (pos < 0) pos += totalScale;

        int idx1     = pos >> 8;
        int idx2     = (idx1 + 1) % colorCount;
        int fraction = pos & 0xFF;

        byte r1 = (byte)(idx1 * 31 + 50);
        byte g1 = (byte)(idx1 * 77 + 100);
        byte b1 = (byte)(idx1 * 133);
        byte r2 = (byte)(idx2 * 31 + 50);
        byte g2 = (byte)(idx2 * 77 + 100);
        byte b2 = (byte)(idx2 * 133);

        byte r = (r1 * (255 - fraction) + r2 * fraction) >> 8;
        byte g = (g1 * (255 - fraction) + g2 * fraction) >> 8;
        byte b = (b1 * (255 - fraction) + b2 * fraction) >> 8;

        LED::setPixel(p, r, g, b, (i <= TASK.ParamB) ? maxBr : 0, false);
    }
    LED::Show();

    if (TASK.ParamB < num) TASK.ParamB++;

    TASK.Phase += 12;
    if (TASK.Phase >= totalScale) TASK.Phase = 0;

    TSK::setTaskInterval("T_EFFECT_HB_6_Colors", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/* -- Effect 7 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 7 -- shooting star with a random-length fading tail.
 */
void T_EFFECT_HB_7_ShootingStarRandom(taskId_t taskId) {
    const int num    = LED_HB_NUM;
    const int maxLen = num >> 1;
    const int maxBr  = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.ParamA   = 1;
        TASK.ParamB   = 0;
        HB::State.Phase   = random(5, maxLen + 1);
    }

    const int tailLen = (int)HB::State.Phase;

    for (int i = 0; i < num; i++) {
        int p      = LED::HB(i);
        int tgtBr  = 0;

        if (i <= (int)TASK.ParamB && i > (int)TASK.ParamB - tailLen) {
            int dist        = (int)TASK.ParamB - i;
            int totalDrop   = (maxBr * 60) / 100;
            int intensityDrop = (dist * totalDrop) / (tailLen - 1);
            tgtBr = maxBr - intensityDrop;
            if (tgtBr < 0) tgtBr = 0;
        }

        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, tgtBr, false);
    }
    LED::Show();

    TASK.ParamB++;
    if (TASK.ParamB > (uint32_t)(num + tailLen)) {
        TASK.ParamA = 0;
        TSK::setTaskInterval("T_EFFECT_HB_7_ShootingStarRandom", taskId, TASK_MS, random(1000, 3000));
    } else {
        TSK::setTaskInterval("T_EFFECT_HB_7_ShootingStarRandom", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 8 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 8 -- random single-pixel colour burst sparkles.
 */
void T_EFFECT_HB_8_RandomSparklingPop(taskId_t taskId) {
    const int num     = LED_HB_NUM;
    const int inc     = EE::Get(EE_TV_ON_BR_CL_INC);
    const int baseLux = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.Phase = random(0, num);
        TASK.ParamB   = random(0, 7);
        TASK.ParamA   = 1;
    }

    // Colour presets for sparkle pixel
    static const uint8_t sparkColors[7][3] = {
        {255,   0,   0},  // Red
        {  0, 255,   0},  // Green
        {  0,   0, 255},  // Blue
        {255, 255,   0},  // Yellow
        {255,   0, 255},  // Magenta
        {  0, 255, 255},  // Cyan
        {255, 255, 255},  // White
    };

    for (int i = 0; i < num; i++) {
        int p = LED::HB(i);
        if (i == (int)TASK.Phase) {
            const uint8_t *c = sparkColors[TASK.ParamB];
            LED::TG_BRIGHTNESS(p, 255, inc, false);
            LED::setPixel(p, c[0], c[1], c[2], LED::State.CurrentBrightness[p], false);
        } else {
            LED::TG_BRIGHTNESS(p, baseLux, inc, false);
            LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
        }
    }
    LED::Show();

    if (LED::State.CurrentBrightness[LED::HB(TASK.Phase)] >= 250) {
        TASK.ParamA = 0;
        TSK::setTaskInterval("T_EFFECT_HB_8_RandomSparklingPop", taskId, TASK_MS, random(50, 200));
    } else {
        TSK::setTaskInterval("T_EFFECT_HB_8_RandomSparklingPop", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/* -- Effect 9 ---------------------------------------------------------- */

/**
 * @brief  HB Effect 9 -- subtle random per-pixel twinkle on a static colour base.
 */
void T_EFFECT_HB_9_GlidingAurora(taskId_t taskId) {
    const int num     = LED_HB_NUM;
    const int baseLux = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        LED::HB_SetFromPreColor(baseLux, false);
        TASK.ParamA = 1;
    }

    // Nudge 3 random pixels
    for (int j = 0; j < 3; j++) {
        int i       = random(0, num);
        int p       = LED::HB(i);
        int twinkBr = constrain(baseLux + random(-30, 30), 0, 255);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, twinkBr, false);
    }

    // Occasionally restore one pixel to stable base
    LED::HB_SetPixelFromPreColor(random(0, num), baseLux);

    LED::Show();
    TSK::setTaskInterval("T_EFFECT_HB_9_GlidingAurora", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/* -- Effect 10 --------------------------------------------------------- */

/**
 * @brief  HB Effect 10 -- Matrix-style glitch: dim ambient glow with random bright blocks.
 */
void T_EFFECT_HB_10_TheGlitchMatrix(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) TASK.ParamA = 1;

    // Ambient glow -- all pixels at 5% brightness from preColor
    LED::HB_SetFromPreColor(maxBr / 20, false);

    // Random glitch block
    const int blockStart = random(0, num);
    const int blockSize  = random(1, 5);
    for (int j = 0; j < blockSize; j++) {
        int idx = blockStart + j;
        if (idx < num) {
            int p = LED::HB(idx);
            LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, maxBr, false);
        }
    }
    LED::Show();

    int baseSpeed   = EE::Get(EE_HB_EFFECT_SPEED);
    int randomDelay = constrain(baseSpeed + random(-10, 50), 10, 32767);
    TSK::setTaskInterval("T_EFFECT_HB_10_TheGlitchMatrix", taskId, TASK_MS, randomDelay);
}

/* -- Effect 11 --------------------------------------------------------- */

/**
 * @brief  HB Effect 11 -- flowing colour plasma with per-pixel brightness jitter.
 */
void T_EFFECT_HB_11_StochasticPlasma(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) {
        TASK.ParamA   = 1;
        TASK.Phase = random(0, 256);
    }

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int cur = LED::State.CurrentBrightness[p];
        int rS  = random(0, 100);
        int nBr = (rS > 85) ? cur + 10 : (rS < 15) ? cur - 10 : cur;
        nBr = constrain(nBr, maxBr / 5, maxBr);

        uint8_t h = (uint8_t)(TASK.Phase - (i * 2));
        uint8_t r, g, b;
        LED::HsvToRgb(h, r, g, b);

        LED::setPixel(p, r, g, b, nBr, false);
    }
    LED::Show();

    TASK.Phase++;
    TSK::setTaskInterval("T_EFFECT_HB_11_StochasticPlasma", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/* -- Effect 12 --------------------------------------------------------- */

/**
 * @brief  HB Effect 12 -- Matrix digital rain: brightness values fall down the strip.
 */
void T_EFFECT_HB_12_DigitalRain(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    // Gravity shift: propagate brightness downward
    for (int i = num - 1; i > 0; i--) {
        int p     = LED::HB(i);
        int pPrev = LED::HB(i - 1);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b,
                     LED::State.CurrentBrightness[pPrev], false);
    }

    // New drop at pixel 0 with 8% probability
    int  firstP = LED::HB(0);
    uint8_t b   = (random(0, 100) > 92) ? maxBr : 0;
    LED::setPixel(firstP, LED::State.TargetColor[firstP].r, LED::State.TargetColor[firstP].g, LED::State.TargetColor[firstP].b, b, false);

    LED::Show();
    TSK::setTaskInterval("T_EFFECT_HB_12_DigitalRain", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/* -- Effect 13 --------------------------------------------------------- */

/**
 * @brief  HB Effect 13 -- two interfering sine waves creating a dual-pulse pattern.
 */
void T_EFFECT_HB_13_DualPulse(taskId_t taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) { TASK.ParamA = 1; TASK.Phase = 0; }

    const float phase1 = (float)TASK.Phase *  0.10f;
    const float phase2 = (float)TASK.Phase * -0.08f;

    for (int i = 0; i < num; i++) {
        int p  = LED::HB(i);
        int ri = (num - 1 - i);                                 // Reversed spatial index - Mapping

        float waveA = sin(phase1 + (ri * 0.5f)) * 0.5f + 0.5f;
        float waveB = sin(phase2 + (ri * 0.3f)) * 0.5f + 0.5f;
        int   br    = (int)((waveA + waveB) * 0.5f * maxBr);
        if (br > maxBr) br = maxBr;

        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
    }

    LED::Show();
    TASK.Phase++;
    TSK::setTaskInterval("T_EFFECT_HB_13_DualPulse", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}


/* -- Effect 14 --------------------------------------------------------- */

/**
 * @brief  HB Effect 14 -- smooth rainbow spectrum with full-strip breathing pulse.
 *
 * Each pixel receives a hue derived from its position + a rolling phase offset,
 * producing a continuously scrolling rainbow. A sine-wave breathing envelope
 * modulates the overall brightness so the strip pulses in and out smoothly.
 * Speed is controlled by EE_HB_EFFECT_SPEED (ms per frame).
 */
void T_EFFECT_HB_14_RainbowWavePulse(taskId_t taskId) {
    const int  num    = LED_HB_NUM;
    const int  maxBr  = LED::HB_GetBaseBr();

    if (TASK.ParamA == 0) TASK.ParamA = 1;

    /* Breathing envelope: sin(phase) mapped to [30%, 100%] of maxBr */
    float envAngle = (float)TASK.Phase * (PI / 90.0f);           // 0->2pi in 180 steps
    float envelope = 0.35f + 0.65f * (sinf(envAngle) * 0.5f + 0.5f);
    int   envBr    = (int)(maxBr * envelope);
    if (envBr < 1) envBr = 1;

    /* Rolling hue offset -- one full rainbow scroll every 256 frames */
    uint8_t hueOffset = (uint8_t)(TASK.ParamB & 0xFF);

    for (int i = 0; i < num; i++) {
        int     p = LED::HB(i);
        uint8_t h = (uint8_t)(hueOffset + (i * 255 / num));     // per-pixel hue
        uint8_t r, g, b;
        LED::HsvToRgb(h, r, g, b);
        LED::setPixel(p, r, g, b, envBr, false);
    }
    LED::Show();

    TASK.Phase = (TASK.Phase + 1) % 180;   // breathing cycle
    TASK.ParamB++;                            // hue scroll
    TSK::setTaskInterval("T_EFFECT_HB_14_RainbowWavePulse", taskId, TASK_MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/**
 * @brief  Kill the running heartbeat effect task and reset HB::State.TaskID.
 */
void EndTask() {
    if (HB::State.TaskID == TASK_ID_NONE) return;
    TSK::KillID(HB::State.TaskID, "HB_EndTask");
    HB::State.TaskID = TASK_ID_NONE;
}

/**
 * @brief  Register the T_EFFECT_HB task to begin the configured heartbeat animation.
 */
void StartEffect(bool silent, bool reset, bool fast) {
    if (UDPRAW::State.Status || APP::Am.Status) return;

    if (reset) { TASK.Phase = 0; TASK.ParamA = 0; TASK.ParamB = 0; }

    HB::State.TaskID = TSK::AddTask("HB_StartEffect", "T_EFFECT_HB", T_EFFECT_HB, TASK_MS,
                           EE::Get(EE_HB_EFFECT_SPEED), fast ? 0 : random(2000, 5000), false);

    if (!silent) {
        PRNT::_print(PRNT::formatMSG("%~32s # start effect [%d] with speed [%dms]" NL,
               "HB_StartEffect", EE::Get(EE_HB_EFFECT), EE::Get(EE_HB_EFFECT_SPEED)));
    }
}
} // namespace HB

namespace TV {
/* ------------------------------------------------------------------------ */
/* TV                                                                         */
/* Detection - On/Off - 9 on-effects - 7 off-effects - HB helpers          */
/* ------------------------------------------------------------------------ */


/* -- Function pointer tables for TV/HB effect dispatch -- moved to DEF.h ---- */
/* (TVEffectHandler, TV_ON_HANDLERS[], HB_ON_HANDLERS[], TV_OFF_HANDLERS[] + counts) */

/* ------------------------------------------------------------------------------ */
/*
 * @brief  Read the TV power pin each loop and fire On() / Off() on stable transitions.
 *
 * Reads TV_PIN analog value (/15 to convert to 0-68 range), applies a
 * TV_DEBOUNCE_TIME ms debounce and a TV_READ_STATUS_TIME ms minimum interval
 * between consecutive status checks. When a stable state change is detected
 * and UDPRAW is idle, calls On() or Off().
 *
 * TestMode _testmode_tvOn / _testmode_tvOff override the pin reading for testing.
 *
 * @note   Call every loop() iteration when LED::State.Enabled is true.
 */
void Status() {
	TV::State.PrevPinValue = TV::State.PinValue;                                      // Snapshot prior reading for TV_LOG "before" value - Setup

	// Read pin status
	TV::State.PinValue = analogRead(TV_PIN) / TV_ADC_DIVIDER;

    // ~ Test Mode Simulation
    if (TestMode == _testmode_tvOn) {
        TV::State.PinValue = TV_TEST_PIN_ON; // Simulate TV ON
    } else if (TestMode == _testmode_tvOff) {
        TV::State.PinValue = TV_TEST_PIN_OFF; // Simulate TV OFF
    }

	TV::State.ReadPin = true;
	if (TV::State.PinValue >= 15) {
		TV::State.ReadPin = false;
	}

	// Check if pin status changed
	if (TV::State.ReadPin != TV::State.LastStatus) {
		// reset debounce time 
		TV::State.LastDebounceTime = TimeNow;
	}

	// Check if keeping new status
	if ((TV::State.LastDebounceTime + TV_DEBOUNCE_TIME) < TimeNow) {
		if ((TV::State.LastReadStatus + TV_READ_STATUS_TIME) < TimeNow) {
			// Check if pin status stabilized
			if (TV::State.ReadPin != TV::State.Status) {
				TV::State.Status = TV::State.ReadPin;
				
				if (!UDPRAW::State.Status) {
					if (TV::State.Status) {
						On();
					} else {
						Off();
					}
				}
			}

			TV::State.LastReadStatus = TimeNow;
		}
	}

	TV::State.LastStatus = TV::State.ReadPin;
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* TV ON / OFF EFFECT TASKS  * * * * * * * * * * */
/* * * * * * * * * * * * * * * * * * * * * * * * */

/**
 * @brief  TV-on master task -- fade to black, dispatch sub-effect, drive HB helper inline.
 *
 * Phase 0: fades all LEDs to black (EE_TV_ON_BR_CL_INC), then sets TASK.Phase=1
 *   and HB::State.Phase=1 to start HB animation on the same tick as the TV sub-effect.
 * Phase 1+: each tick calls the TV-strip sub-effect AND the matching T_EFFECT_H_xx
 *   helper back-to-back -- both advance their own state machines (TASK.Phase / HB::State.Phase)
 *   independently, so whichever finishes first simply idles until the other catches up.
 * Finalization barrier: when both TASK.Phase==taskDone and HB::State.Phase==taskDone, syncs
 *   app, kills this task, and hands off to HB::StartEffect().
 *
 * @param  taskId  Task handle supplied by the scheduler.
 *
 * Called by: TV::On() (TSK::AddTask, registered unlocked, TASK_MS interval).
 */
void T_EFFECT_TV_ON(taskId_t taskId) {

    if (TASK.Phase == 0) { // --- PHASE 0: PRE-CLEANUP (fade current state to black) ---
        const int increment = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));               // Get step speed - Setup
        bool anyChanged = LED::FadeAllToZero(increment);                  // Fade all LEDs to zero - Action

        if (anyChanged) {
            LED::Show();                                                  // Update strip during fade-out - Output
        } else {
            TASK.Phase = 1; TASK.ParamA = 0; TASK.ParamB = 0;                    // Advance to animation phase - State
            HB::State.Phase   = 1; HB::State.ParamA   = 0;                                 // Start HB animation next tick - State

            TSK::setTaskInterval("T_EFFECT_TV_ON", taskId, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL))); // Apply animation delay - Timing
            APP::updDeltaColors();                                    // Sync app UI state - Sync

            #ifdef ENABLE_LOG_ANIME_INFO
                {
                    int tvEffIdx = EE::Get(EE_TV_ON_EFF);
                    int hbEffIdx = EE::Get(EE_TV_ON_HB_EFF);
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # dispatching TV effect [%d] + HB effect [%d]" NL,
                        "T_EFFECT_TV_ON", tvEffIdx, hbEffIdx));
                }
            #endif
        }
        return;
    }

    // --- PHASE 1+: TV sub-effect + HB helper, both called each tick ---

    if (TASK.Phase != taskDone) {                                         // TV strip still animating - Logic
        int tvEffIdx = EE::Get(EE_TV_ON_EFF);
        if (tvEffIdx < 0 || tvEffIdx >= TV_ON_HANDLERS_COUNT) tvEffIdx = 0;
        
        // Handle special TASK.ParamB pre-configuration for specific effects
        if (tvEffIdx == 6) TASK.ParamB = 0;       // Mid to Ext Vertical
        else if (tvEffIdx == 7) TASK.ParamB = 1;  // Mid to Ext Horizontal
        else if (tvEffIdx == 8) TASK.ParamB = 220; // Com Effect
        
        TV_ON_HANDLERS[tvEffIdx](taskId);
    }

    if (HB::State.Phase != taskDone) {                                           // HB still animating - Logic
        int hbEffIdx = EE::Get(EE_TV_ON_HB_EFF);
        if (hbEffIdx < 0 || hbEffIdx >= HB_ON_HANDLERS_COUNT) hbEffIdx = 0;
        HB_ON_HANDLERS[hbEffIdx](taskId);
    }

    LED::Show();                                                          // Output combined TV + HB frame - Output
    LISENS::ResetTime();                                                  // Prevent lux changes during transition - Action

    // --- FINALIZATION BARRIER: both TV strip and HB must be taskDone ---
    if (TASK.Phase == taskDone && HB::State.Phase == taskDone) {
        #ifdef ENABLE_LOG_TASK
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # TV full on" NL, "T_EFFECT_TV_ON")); // Log completion - Debug
        #endif

        APP::updDeltaColors();                                        // Final UI sync - Sync
        TSK::KillTasksAvoidLocked("T_EFFECT_TV_ON");                     // Kill task - State
        TV::State.Transitioning = false;                                 // On-transition finished - State

        HB::StartEffect(false, true, false);                              // Start idle HB mode - State
    }
}

/**
 * @brief  TV-on default sub-effect -- fade all main zone LEDs to EEPROM target brightness.
 *
 * Steps all main LEDs (excluding HB range) from 0 toward Lux-adjusted EEPROM brightness.
 * HB driven inline by T_EFFECT_H_FadeOn called from T_EFFECT_TV_ON each tick.
 * Sets TASK.Phase = taskDone when main strip is complete.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[0]).
 */
void T_EFFECT_TV_ON_Default(taskId_t tID) {
    bool LedChanged = false;                                             // Track normal LED changes - State
    const uint8_t inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                     // Cache increment speed - Setup

    for (int i = 0; i < LED_NUM - LED_HB_NUM_FAKE; i++) {               // Loop through main strip LEDs - Logic
        uint8_t targetLux = LED::getLuxBrightness(LED::State.StoredBrightness[i]);  // Get target lux - Logic
        if (LED::TG_BRIGHTNESS(i, targetLux, inc, false)) {               // Step brightness toward target - Logic
            LED::setPixel(i,
                LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b,
                LED::State.CurrentBrightness[i], false);
            LedChanged = true;                                           // At least one pixel still transitioning - State
        }
    }

    if (!LedChanged) {
        APP::updDeltaColors();                                        // Send delta to app - Sync
        TASK.Phase = taskDone;                                            // Main strip done - State
        #ifdef ENABLE_LOG_ANIME_INFO
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # main strip done" NL, "TV_ON_Default"));
        #endif
    } else {
        LED::Show();
    }
}

/**
 * @brief  TV-on effect 1 -- random-order brightness ramp then colour sync (TV strip only).
 *
 * Phase 1: reveals LEDs in LED::State.PixelOrder[] sequence, each stepping to EEPROM brightness.
 * Phase 2: transitions all LEDs toward their final EEPROM eeColor.
 * HB driven inline by T_EFFECT_H_FadeOn called from T_EFFECT_TV_ON each tick.
 * Sets TASK.Phase = taskDone when both phases complete.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[1]).
 */
void T_EFFECT_TV_ON_1_RandomStatic(taskId_t tID) {
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;                       // Cache bounds - Setup

    // --- PHASE 1: Progressive Brightness Ramp-up (random order) ---
    if (TASK.Phase == 1) {
        if (TASK.ParamA < maxLeds) {
            const int inc      = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));            // Cache increment - Setup
            const int l        = LED::State.PixelOrder[TASK.ParamA];                    // Next randomised LED index - Mapping
            const int targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[l]); // Target brightness - Setup
            bool stillFading = false;                                    // State tracker - State

            if (LED::TG_BRIGHTNESS(l, targetBr, inc, false)) {
                LED::setPixel(l, LED::State.TargetColor[l].r, LED::State.TargetColor[l].g, LED::State.TargetColor[l].b, LED::State.CurrentBrightness[l], false);
                stillFading = true;
            }

            if (!stillFading) {
                APP::updDeltaColors();                                // Sync UI for this LED - Sync
                TASK.ParamA++;
            }
            LED::Show();
        } else {
            TASK.Phase = 2; TASK.ParamA = 0; TASK.ParamB = 0;                    // All pixels ramped, move to color phase - State
        }
        return;
    }

    // --- PHASE 2: Color Transition Sync ---
    if (TASK.Phase == 2) {
        bool changed = false;
        const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }

        if (changed) {
            LED::Show();
        } else {
            APP::updDeltaColors();
            TASK.Phase = taskDone;                                        // TV animation complete - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_1_RandomStatic"));
            #endif
        }
        return;
    }
}

/**
 * @brief  TV-on effect 2 -- mid-to-out zone-by-zone expansion (TV strip only).
 *
 * Steps 1-5: bloom each zone (TV, COM, UCOM, BED, LAMP) to 150% from center outward.
 * Steps 6-10: adopt final EEPROM colours per zone.
 * Step 11: normalise all zones to 100% EEPROM brightness.
 * HB runs in parallel via T_EFFECT_H_CenterBloom.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[2]).
 */
void T_EFFECT_TV_ON_2_MidToOutSep(taskId_t tID) {
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    const int ta    = TASK.ParamA;                                           // Cache animation index - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                       // Cache increment - Setup

    // --- PHASES 1-10: Zone-by-zone bloom and colour adoption ---
    if (phase >= 1 && phase <= 10) {
        int halfLeds, sMin, sMax;
        int activeStep = (phase > 5) ? (phase - 5) : phase;                // Normalize to 1-5 - Logic
        bool tvDone = false;

        switch (activeStep) {
            case 1: halfLeds = LED_TV_NUM   >> 1; sMin = halfLeds;      sMax = halfLeds - 1;              break;
            case 2: halfLeds = LED_COM_NUM  >> 1; sMin = LED::COM(0);    sMax = LED::COM(LED_COM_NUM - 1);   break;
            case 3: halfLeds = LED_UCOM_NUM >> 1; sMin = LED::UCOM(0);   sMax = LED::UCOM(LED_UCOM_NUM - 1); break;
            case 4: halfLeds = LED_BED_NUM  >> 1; sMin = LED::BED(0);    sMax = LED::BED(LED_BED_NUM - 1);   break;
            case 5: halfLeds = LED_LAMP_NUM >> 1; sMin = LED::LAMP(0);   sMax = LED::LAMP(LED_LAMP_NUM - 1); break;
            default: tvDone = true; break;
        }

        if (!tvDone) {
            if (ta < halfLeds) {
                bool moved = false;
                int led[2] = { sMax - ta, sMin + ta };                  // Expansion pair - Mapping

                for (int i = 0; i < 2; i++) {
                    int p = led[i];
                    bool changed = false;

                    if (phase <= 5) {
                        int bloomBr = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10; // +50% - Logic
                        changed = LED::TG_BRIGHTNESS(p, bloomBr, brInc);
                        if (changed) LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
                    } else {
                        changed = LED::TG_COLOR(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, brInc);
                        if (changed) LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false);
                    }
                    if (changed) moved = true;
                }

                if (moved) { LED::Show(); }
                else { APP::updDeltaColors(); TASK.ParamA++; TASK.ParamB = 0; }
            } else {
                TASK.Phase++; TASK.ParamA = 0; TASK.ParamB = 0;
            }
        }
        return;
    }

    // --- PHASE 3 (Step 11): Final Normalization to 100% ---
    if (phase == 11) {
        bool tvLedsDone = true;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                tvLedsDone = false;
            }
        }
        LED::Show();
        if (tvLedsDone) {
            APP::updDeltaColors();
            TASK.Phase = taskDone;                                        // TV finished - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_2_MidToOutSep"));
            #endif
        }
    }
}

/**
 * @brief  TV-on effect 3 -- all zones expand simultaneously from center outward (TV strip only).
 *
 * Phase 1: all zones bloom simultaneously to 150% from center outward.
 * Phases 2-3: adopt EEPROM colours then normalise to 100% brightness.
 * HB runs in parallel via T_EFFECT_H_CenterBloom.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[3]).
 */
void T_EFFECT_TV_ON_3_MidToOutAll(taskId_t tID) {
    const int phase = TASK.Phase;
    const int ta    = TASK.ParamA;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    // --- PHASE 1: Simultaneous all-zone bloom ---
    if (phase == 1) {
        struct Zone { int sMin, sMax, half; };
        const Zone zones[] = {
            { LED::TV(LED_TV_NUM >> 1),    LED::TV((LED_TV_NUM >> 1) - 1),   LED_TV_NUM >> 1   },
            { LED::COM(LED_COM_NUM >> 1),  LED::COM((LED_COM_NUM >> 1) - 1), LED_COM_NUM >> 1  },
            { LED::UCOM(0),                LED::UCOM(LED_UCOM_NUM - 1),       1                 },
            { LED::BED(LED_BED_NUM >> 1),  LED::BED((LED_BED_NUM >> 1) - 1), LED_BED_NUM >> 1  },
            { LED::LAMP(0),                LED::LAMP(LED_LAMP_NUM - 1),       LED_LAMP_NUM >> 1 }
        };

        bool tvActive = false;
        int maxHalf = 0;

        for (int i = 0; i < 5; i++) {
            if (zones[i].half > maxHalf) maxHalf = zones[i].half;
            if (ta < zones[i].half) {
                int led[2] = { zones[i].sMax - ta, zones[i].sMin + ta };
                for (int j = 0; j < 2; j++) {
                    int p = led[j];
                    int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                    if (LED::TG_BRIGHTNESS(p, tvBloom, brInc)) {
                        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
                        tvActive = true;
                    }
                }
            }
        }

        if (!tvActive) {
            APP::updDeltaColors();
            if (ta >= maxHalf) { TASK.Phase = 2; TASK.ParamA = 0; }
            else                { TASK.ParamA++;                  }
        }
        if (tvActive) LED::Show();
        return;
    }

    // --- PHASES 2-3: Color sync then brightness normalisation ---
    if (phase == 2 || phase == 3) {
        bool mainMoving = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            bool changed = (phase == 2)
                ? LED::TG_COLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, brInc)
                : LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc);
            if (changed) {
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                mainMoving = true;
            }
        }
        if (!mainMoving) {
            TASK.Phase = (phase == 2) ? 3 : taskDone;
            TASK.ParamA = 0;
            APP::updDeltaColors();
            #ifdef ENABLE_LOG_ANIME_INFO
                if (TASK.Phase == taskDone) PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_3_MidToOutAll"));
            #endif
        }
        if (mainMoving) LED::Show();
    }
}

/**
 * @brief  TV-on effects 4 & 5 -- circular half-run around the TV ring (TV strip only).
 *
 * Effect 4 starts at offset 0; effect 5 at a random offset.
 * Two points travel 180deg apart around the TV ring (+50% bloom). Phase 3: normalise.
 * HB runs in parallel via T_EFFECT_H_CenterBloom.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[4] and [5]).
 */
void T_EFFECT_TV_ON_4_5_HalfRun(taskId_t tID) {
    const int phase    = TASK.Phase;
    const int ta      = TASK.ParamA;
    const int offset  = TASK.ParamB;
    const int brInc   = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int tvCount = LED_TV_NUM;
    const int halfTv  = tvCount >> 1;

    // --- STEP 1: Initialization ---
    if (phase == 1) {
        TASK.ParamB   = (EE::Get(EE_TV_ON_EFF) == 4) ? 0 : random(0, halfTv); // Set start offset - Logic
        TASK.Phase = 2;
        return;
    }

    // --- STEP 2: Circular expansion (Bloom +50%) ---
    if (phase == 2) {
        if (ta < halfTv) {
            bool tvActive = false;
            int indices[2] = {
                (offset + ta) % tvCount,
                (offset + halfTv + ta) % tvCount
            };
            for (int i = 0; i < 2; i++) {
                int p = indices[i];
                int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                if (LED::TG_BRIGHTNESS(p, tvBloom, brInc)) {
                    LED::setPixel(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, LED::State.CurrentBrightness[p], false);
                    tvActive = true;
                }
            }
            if (!tvActive) { APP::updDeltaColors(); TASK.ParamA++; }
            LED::Show();
        } else {
            TASK.Phase = 3; TASK.ParamA = 0; TASK.ParamB = 0;
        }
        return;
    }

    // --- STEP 3: Normalise main strip to 100% ---
    if (phase == 3) {
        bool mainMoving = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                LED::setPixel(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, LED::State.CurrentBrightness[i], false);
                mainMoving = true;
            }
        }
        LED::Show();
        if (!mainMoving) {
            APP::updDeltaColors();
            TASK.Phase = taskDone;                                        // TV finished - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_4_5_HalfRun"));
            #endif
        }
    }
}

/**
 * @brief  TV-on effects 6 & 7 -- multi-anchor expansion from fixed midpoints (TV strip only).
 *
 * Effect 6: 4 anchors at LED::TV(14/15/0/29). Effect 7: at LED::TV(7/22).
 * Expands outward (+50% bloom). Phases 2-3: fill then normalise.
 * HB runs in parallel via T_EFFECT_H_CenterBloom.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[6] and [7]).
 */
void T_EFFECT_TV_ON_6_7_MidToExt(taskId_t tID) {
    const int phase  = TASK.Phase;
    const int ta    = TASK.ParamA;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    // --- STEP 1: Multi-anchor bloom ---
    if (phase == 1) {
        int anchors[4];
        int maxIter;

        if (EE::Get(EE_TV_ON_EFF) == 6) {
            anchors[0] = LED::TV(14); anchors[1] = LED::TV(15);
            anchors[2] = LED::TV(0);  anchors[3] = LED::TV(29);
            maxIter = 5;
        } else {
            anchors[0] = LED::TV(7);  anchors[1] = LED::TV(22);
            anchors[2] = LED::TV(7);  anchors[3] = LED::TV(22);
            maxIter = 4;
        }

        if (ta < maxIter) {
            bool tvActive = false;
            int targets[4] = { anchors[0] - ta, anchors[1] + ta, anchors[2] + ta, anchors[3] - ta };

            for (int i = 0; i < 4; i++) {
                int p = targets[i];
                if (p < LED::TV(0) || p > LED::TV(LED_TV_NUM - 1)) continue;
                int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                if (LED::TG_BRIGHTNESS(p, tvBloom, brInc)) {
                    LED::setPixel(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, LED::State.CurrentBrightness[p], false);
                    tvActive = true;
                }
            }
            if (!tvActive) { APP::updDeltaColors(); TASK.ParamA++; }
            LED::Show();
        } else {
            TASK.Phase = 2; TASK.ParamA = 0;
        }
        return;
    }

    // --- STEPS 2-3: Fill and normalise ---
    if (phase == 2 || phase == 3) {
        bool mainMoving = false;
        const int limit = (phase == 2) ? LED_TV_NUM : (LED_NUM - LED_HB_NUM_FAKE);

        for (int i = 0; i < limit; i++) {
            int p = (phase == 2) ? LED::TV(i) : i;
            if (LED::TG_BRIGHTNESS(p, LED::getLuxBrightness(LED::State.StoredBrightness[p]), brInc)) {
                LED::setPixel(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, LED::State.CurrentBrightness[p], false);
                mainMoving = true;
            }
        }
        if (!mainMoving) {
            APP::updDeltaColors();
            TASK.Phase = (phase == 2) ? 3 : taskDone;
            #ifdef ENABLE_LOG_ANIME_INFO
                if (TASK.Phase == taskDone) PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_6_7_MidToExt"));
            #endif
        }
        if (mainMoving) LED::Show();
    }
}

/**
 * @brief  TV-on effect 8 -- COM scanner: center pop -> scan -> fill (TV strip only).
 *
 * Step 1: center two COM pixels rapid-pop to target. Step 2: scanning dot outward.
 * Step 3: fill COM back to target. Step 4: global normalisation.
 * HB runs in parallel via T_EFFECT_H_LinearSweep.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[8]).
 */
void T_EFFECT_TV_ON_8_ComEffect(taskId_t tID) {
    const int phase     = TASK.Phase;
    const int ta       = TASK.ParamA;
    const int brInc    = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int targetBr = LED::getLuxBrightness(TASK.ParamB);
    const int halfCom  = LED_COM_NUM >> 1;
    const int lL       = halfCom - 1;
    const int lR       = halfCom;

    // --- STEP 1: Center pop ---
    if (phase == 1) {
        bool centerBusy = false;
        int centers[2] = { LED::COM(lL), LED::COM(lR) };

        for (int i = 0; i < 2; i++) {
            int p = centers[i];
            if (LED::TG_BRIGHTNESS(p, targetBr, 20)) {
                LED::setPixel(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, LED::State.CurrentBrightness[p], false);
                centerBusy = true;
            }
        }
        if (!centerBusy) { APP::updDeltaColors(); TASK.Phase = 2; TASK.ParamA = 0; }
        LED::Show();
        return;
    }

    // --- STEP 2: Outward scan ---
    if (phase == 2) {
        if (ta < halfCom) {
            int activeL = LED::COM(lL - ta), activeR = LED::COM(lR + ta);
            LED::setPixel(activeL, LED::State.StoredColor[activeL].r, LED::State.StoredColor[activeL].g, LED::State.StoredColor[activeL].b, targetBr, false);
            LED::setPixel(activeR, LED::State.StoredColor[activeR].r, LED::State.StoredColor[activeR].g, LED::State.StoredColor[activeR].b, targetBr, false);
            if (ta > 0) {
                int prevL = LED::COM(lL - (ta - 1)), prevR = LED::COM(lR + (ta - 1));
                LED::setPixel(prevL, LED::State.StoredColor[prevL].r, LED::State.StoredColor[prevL].g, LED::State.StoredColor[prevL].b, 10, false); // Trail dim - Action
                LED::setPixel(prevR, LED::State.StoredColor[prevR].r, LED::State.StoredColor[prevR].g, LED::State.StoredColor[prevR].b, 10, false); // Trail dim - Action
            }
            APP::updDeltaColors(); TASK.ParamA++;
            LED::Show();
        } else {
            TASK.Phase = 3; TASK.ParamA = 0;
        }
        return;
    }

    // --- STEP 3: COM fill back ---
    if (phase == 3) {
        if (ta < halfCom) {
            int pL = LED::COM(ta), pR = LED::COM(LED_COM_NUM - 1 - ta);
            bool movingL = LED::TG_BRIGHTNESS(pL, targetBr, 15);
            bool movingR = LED::TG_BRIGHTNESS(pR, targetBr, 15);
            if (movingL) LED::setPixel(pL, LED::State.StoredColor[pL].r, LED::State.StoredColor[pL].g, LED::State.StoredColor[pL].b, LED::State.CurrentBrightness[pL], false);
            if (movingR) LED::setPixel(pR, LED::State.StoredColor[pR].r, LED::State.StoredColor[pR].g, LED::State.StoredColor[pR].b, LED::State.CurrentBrightness[pR], false);
            if (!movingL && !movingR) TASK.ParamA++;
        } else {
            APP::updDeltaColors(); TASK.Phase = 4; TASK.ParamA = 0;
        }
        LED::Show();
        return;
    }

    // --- STEP 4: Global normalisation ---
    if (phase == 4) {
        bool mainBusy = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                LED::setPixel(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, LED::State.CurrentBrightness[i], false);
                mainBusy = true;
            }
        }
        if (!mainBusy) {
            APP::updDeltaColors(); TASK.Phase = taskDone;
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_8_ComEffect"));
            #endif
        }
        else            { LED::Show(); }
    }
}

/**
 * @brief  TV-on effect 9 -- TV mid-to-edge sweep then room normalisation (TV strip only).
 *
 * Step 1: TV LEDs sweep outward from center to edges.
 * Step 2: TV normalises; waits for T_EFFECT_H_QuadPoint to finish HB expand (HB::State.Phase >= 2).
 * Step 3: remaining room zones normalise to EEPROM brightness.
 * HB driven inline by T_EFFECT_H_QuadPoint called from T_EFFECT_TV_ON each tick.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[9]).
 */
void T_EFFECT_TV_ON_9_QuadPointHB(taskId_t tID) {
    const int phase      = TASK.Phase;
    const int brInc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL));
    const int tvCount   = LED_START_I_COM;

    // --- STEP 1: TV center-to-edge sweep ---
    if (phase == 1) {
        bool moving = false;
        int ta = TASK.ParamA, tvMid = tvCount >> 1;

        if (ta <= tvMid) {
            int pL = (tvMid - 1) - ta, pR = tvMid + ta;
            int pixels[] = { pL, pR };

            for (int i = 0; i < 2; i++) {
                int p = pixels[i];
                if (p >= 0 && p < tvCount) {
                    if (LED::TG_BRIGHTNESS(p, LED::getLuxBrightness(LED::State.StoredBrightness[p]), brInc, false)) {
                        LED::setPixel(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, LED::State.CurrentBrightness[p], false);
                        moving = true;
                    }
                }
            }
            if (!moving && ta < tvMid) { APP::updDeltaColors(); TASK.ParamA++; moving = true; }
        }

        if (moving) {
            TSK::setTaskInterval("T_EFFECT_TV_ON", tID, TASK_MS, animDelay);
        } else {
            APP::updDeltaColors();
            TASK.Phase = 2; TASK.ParamA = 0;
            TSK::setTaskInterval("T_EFFECT_TV_ON", tID, TASK_MS, 0);             // Jump to next frame immediately - Logic
        }
        return;
    }

    // --- STEP 2: TV stabilise + wait for HB quad-expand to complete ---
    if (phase == 2) {
        bool tvDone = true;
        for (int i = 0; i < tvCount; i++) {
            if (LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc, false)) {
                LED::setPixel(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, LED::State.CurrentBrightness[i], false);
                tvDone = false;
            }
        }
        // Advance only when TV is stable AND HB has passed its expand phase
        if (tvDone && HB::State.Phase >= 2) {
            APP::updDeltaColors(); TASK.Phase = 3;
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # step2->3 (TV stable, HB past expand)" NL, "TV_ON_9_QuadPointHB"));
            #endif
        }
        LED::Show();
        return;
    }

    // --- STEP 3: Room normalisation ---
    if (phase == 3) {
        bool roomBusy = false;
        for (int i = tvCount; i < (LED_NUM - LED_HB_NUM_FAKE); i++) {
            if (LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc, false)) {
                LED::setPixel(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, LED::State.CurrentBrightness[i], false);
                roomBusy = true;
            }
        }
        if (!roomBusy) {
            APP::updDeltaColors(); TASK.Phase = taskDone;
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_9_QuadPointHB"));
            #endif
        }
        LED::Show();
    }
}

/**
 * @brief  TV-on effect 10 -- liquid fill: all zones rise from bottom to top like water.
 *
 * Phase 1: steps LED indices from the last pixel down to 0 (bottom-to-top reveal),
 *   fading each one to its EEPROM brightness.  TASK.ParamA is the current fill level
 *   (decrements each settled step).
 * Phase 2: colour-sync pass -- all pixels adopt their EEPROM colour.
 * HB driven inline by T_EFFECT_H_FadeOn called from T_EFFECT_TV_ON each tick.
 * Sets TASK.Phase = taskDone when both phases complete.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 */
/**
 * @brief  TV-on effect 10 -- liquid fill: water rises through the room bottom to top.
 *
 * Fill order (floor -> ceiling):
 *   Phase 1 -- LAMP  (floor strip)
 *   Phase 2 -- BED   (bed level)
 *   Phase 3 -- UCOM  (under-desk)
 *   Phase 4 -- COM   (desk)
 *   Phase 5 -- TV    bottom row simultaneously   (indices 0-4, 25-29)
 *   Phase 6 -- TV    side rows, one row at a time rising upward:
 *               row 1: TV[5]  + TV[24]
 *               row 2: TV[6]  + TV[23]
 *               row 3: TV[7]  + TV[22]
 *               row 4: TV[8]  + TV[21]
 *               row 5: TV[9]  + TV[20]
 *             TASK.ParamA tracks which side-row (0..4)
 *   Phase 7 -- TV    top row simultaneously      (indices 10-19)
 *   Phase 8 -- colour-sync pass across all zones
 *
 * TV physical layout (LED_TV_NUM = 30):
 *   10 11 12 13 14  15 16 17 18 19   <- top row
 *   09                          20
 *   08                          21
 *   07                          22   <- sides
 *   06                          23
 *   05                          24
 *   04 03 02 01 00  29 28 27 26 25   <- bottom row
 *
 * HB driven inline by T_EFFECT_H_FadeOn called from T_EFFECT_TV_ON each tick.
 * Sets TASK.Phase = taskDone when complete.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[10]).
 */
void T_EFFECT_TV_ON_10_LiquidFill(taskId_t tID) {
    const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                         // Brightness step - Setup

    /* -- Helper macro: fade an array of absolute LED indices, sets busy=true if any still moving -- */
    #define FADE_LEDS(idxArr, cnt, busyFlag) \
        for (int _i = 0; _i < (cnt); _i++) { \
            int _l = (idxArr)[_i]; \
            int _br = LED::getLuxBrightness(LED::State.StoredBrightness[_l]); \
            if (LED::TG_BRIGHTNESS(_l, _br, inc, false)) { \
                LED::setPixel(_l, LED::State.StoredColor[_l].r, LED::State.StoredColor[_l].g, \
                             LED::State.StoredColor[_l].b, LED::State.CurrentBrightness[_l], false); \
                (busyFlag) = true; \
            } \
        }

    /* -- Phase 1: LAMP --------------------------------------------------------- */
    if (TASK.Phase == 1) {
        int idx[LED_LAMP_NUM];
        for (int i = 0; i < LED_LAMP_NUM; i++) idx[i] = LED_START_I_LAMP + i;
        bool busy = false;
        FADE_LEDS(idx, LED_LAMP_NUM, busy);
        if (!busy) { APP::updDeltaColors(); TASK.Phase = 2; }
        else LED::Show();
        return;
    }

    /* -- Phase 2: BED ---------------------------------------------------------- */
    if (TASK.Phase == 2) {
        int idx[LED_BED_NUM];
        for (int i = 0; i < LED_BED_NUM; i++) idx[i] = LED_START_I_BED + i;
        bool busy = false;
        FADE_LEDS(idx, LED_BED_NUM, busy);
        if (!busy) { APP::updDeltaColors(); TASK.Phase = 3; }
        else LED::Show();
        return;
    }

    /* -- Phase 3: UCOM --------------------------------------------------------- */
    if (TASK.Phase == 3) {
        int idx[LED_UCOM_NUM];
        for (int i = 0; i < LED_UCOM_NUM; i++) idx[i] = LED_START_I_UCOM + i;
        bool busy = false;
        FADE_LEDS(idx, LED_UCOM_NUM, busy);
        if (!busy) { APP::updDeltaColors(); TASK.Phase = 4; }
        else LED::Show();
        return;
    }

    /* -- Phase 4: COM ---------------------------------------------------------- */
    if (TASK.Phase == 4) {
        int idx[LED_COM_NUM];
        for (int i = 0; i < LED_COM_NUM; i++) idx[i] = LED_START_I_COM + i;
        bool busy = false;
        FADE_LEDS(idx, LED_COM_NUM, busy);
        if (!busy) { APP::updDeltaColors(); TASK.Phase = 5; }
        else LED::Show();
        return;
    }

    /* -- Phase 5: TV bottom row -- indices 0-4 (right half) and 25-29 (left half) */
    if (TASK.Phase == 5) {
        //  layout: 04 03 02 01 00 | 29 28 27 26 25
        const int idx[] = { LED::TV(0), LED::TV(1), LED::TV(2), LED::TV(3), LED::TV(4),
                             LED::TV(25), LED::TV(26), LED::TV(27), LED::TV(28), LED::TV(29) };
        bool busy = false;
        FADE_LEDS(idx, 10, busy);
        if (!busy) {
            APP::updDeltaColors();
            TASK.Phase = 6; TASK.ParamA = 0;                            // Enter side-rows - State
        } else LED::Show();
        return;
    }

    /* -- Phase 6: TV sides, rising row by row ---------------------------------- */
    /*   row 0: TV[5]  + TV[24]                                                   */
    /*   row 1: TV[6]  + TV[23]                                                   */
    /*   row 2: TV[7]  + TV[22]                                                   */
    /*   row 3: TV[8]  + TV[21]                                                   */
    /*   row 4: TV[9]  + TV[20]                                                   */
    if (TASK.Phase == 6) {
        const int sideRows = 5;                                          // Number of side-row pairs - Setup
        if (TASK.ParamA < sideRows) {
            const int idx[] = { LED::TV(5 + TASK.ParamA), LED::TV(24 - TASK.ParamA) };
            bool busy = false;
            FADE_LEDS(idx, 2, busy);
            if (!busy) {
                APP::updDeltaColors();
                TASK.ParamA++;                                           // Rise one row - State
            } else LED::Show();
        } else {
            TASK.Phase = 7; TASK.ParamA = 0;                            // All side rows done - State
        }
        return;
    }

    /* -- Phase 7: TV top row -- indices 10-19 ---------------------------------- */
    if (TASK.Phase == 7) {
        int idx[10];
        for (int i = 0; i < 10; i++) idx[i] = LED::TV(10 + i);
        bool busy = false;
        FADE_LEDS(idx, 10, busy);
        if (!busy) { APP::updDeltaColors(); TASK.Phase = 8; }
        else LED::Show();
        return;
    }

    /* -- Phase 8: colour-sync across all zones --------------------------------- */
    if (TASK.Phase == 8) {
        const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;
        bool changed = false;
        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i,
                    LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i,
                    LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b,
                    LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }
        if (changed) { LED::Show(); }
        else          {
            APP::updDeltaColors(); TASK.Phase = taskDone;
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_10_LiquidFill"));
            #endif
        }
        return;
    }

    #undef FADE_LEDS
}

/**
 * @brief  TV-on effect 11 -- pixel boot sequence: random pixels blink on until all are lit.
 *
 * Uses LED::State.PixelOrder[] (pre-shuffled in Setup) to reveal LEDs one at a time
 * in random order, stepping each to its EEPROM brightness.  After all pixels are
 * revealed a final colour-sync pass aligns every pixel to its stored colour.
 * HB driven inline by T_EFFECT_H_FadeOn called from T_EFFECT_TV_ON each tick.
 * Sets TASK.Phase = taskDone when both phases complete.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (TV_ON_HANDLERS[11]).
 */
void T_EFFECT_TV_ON_11_PixelBoot(taskId_t tID) {
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;                       // Exclude HB placeholder - Setup
    const int inc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                     // Cache increment - Setup

    // --- PHASE 1: Random-order pixel reveal ---
    if (TASK.Phase == 1) {
        if (TASK.ParamA < maxLeds) {
            int l        = LED::State.PixelOrder[TASK.ParamA];                  // Shuffled pixel index - Mapping
            int targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[l]);// Lux-adjusted target - Setup
            bool fading  = LED::TG_BRIGHTNESS(l, targetBr, inc, false);   // Step brightness - Logic

            if (fading) {
                LED::setPixel(l, LED::State.StoredColor[l].r, LED::State.StoredColor[l].g, LED::State.StoredColor[l].b,
                             LED::State.CurrentBrightness[l], false);
            } else {
                APP::updDeltaColors();                                // Pixel settled - Sync
                TASK.ParamA++;                                           // Next pixel - State
            }
            LED::Show();
        } else {
            TASK.Phase = 2; TASK.ParamA = 0; TASK.ParamB = 0;           // All pixels on, move to colour-sync - State
        }
        return;
    }

    // --- PHASE 2: Colour-sync pass ---
    if (TASK.Phase == 2) {
        bool changed = false;
        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b,
                             LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }
        if (changed) { LED::Show(); }
        else          {
            APP::updDeltaColors(); TASK.Phase = taskDone;
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_ON_11_PixelBoot"));
            #endif
        }
        return;
    }
}


/* * * * * * * * * * * * * * * * * * * * * * * * */
/* TV ON HB HELPERS  * * * * * * * * * * * * * * */
/* * * * * * * * * * * * * * * * * * * * * * * * */

/**
 * @brief  HB helper -- plain linear fade-in from 0 -> EEPROM target brightness.
 *
 * Fades all HB pixels simultaneously. Respects EE_HB_DUAL_COLOR: left half
 * uses LED::UCOM(0) color, right half LED::UCOM(1). Sets HB::State.Phase = taskDone
 * when all pixels have reached target.
 *
 * Used by: EE_TV_ON_HB_EFF = 0 (default).
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (HB_ON_HANDLERS[0]).
 */
void T_EFFECT_H_FadeOn(taskId_t tID) {
    const uint8_t inc      = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));               // Fade step speed - Setup
    const bool    dual     = EE::Get(EE_HB_DUAL_COLOR);                 // Dual-zone toggle - Setup
    const int     targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]); // EE target - Setup
    const int     halfPt   = LED_HB_NUM >> 1;                          // Midpoint for dual zones - Setup
    bool          busy     = false;                                     // Activity tracker - State

    for (int i = 0; i < LED_HB_NUM; i++) {
        int ledIdx = LED::HB(i);                                         // Hardware index - Mapping
        if (LED::TG_BRIGHTNESS(ledIdx, targetBr, inc, false)) {          // Step brightness - Logic
            uint8_t r, g, b;
            if (dual) {
                int ref = (i < halfPt) ? LED::UCOM(0) : LED::UCOM(1);    // Zone color ref - Mapping
                r = LED::State.StoredColor[ref].r; g = LED::State.StoredColor[ref].g; b = LED::State.StoredColor[ref].b;
            } else {
                r = LED::State.StoredColor[LED_START_I_HB].r;
                g = LED::State.StoredColor[LED_START_I_HB].g;
                b = LED::State.StoredColor[LED_START_I_HB].b;
            }
            LED::setPixel(ledIdx, r, g, b, LED::State.CurrentBrightness[ledIdx], false);
            busy = true;                                                // Still animating - State
        }
    }

    if (!busy) {
        APP::updDeltaColors();                                       // Notify app - Sync
        HB::State.Phase = taskDone;                                             // HB complete - State
        #ifdef ENABLE_LOG_ANIME_INFO
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "H_FadeOn"));
        #endif
    }
}

/**
 * @brief  HB helper -- center-outward bloom (+50%) then settle to EEPROM target.
 *
 * HB::State.Phase==1 (expansion): expands a symmetric pair from HB midpoint outward,
 *   each pixel stepping to 150% EEPROM brightness. HB::State.ParamA advances per settled pair.
 * HB::State.Phase==2 (settle): all pixels step from 150% down to 100% EEPROM brightness.
 * Sets HB::State.Phase = taskDone when fully settled.
 *
 * Used by: Effects 2, 3, 4/5, 6/7 (all share identical HB bloom behaviour).
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (HB_ON_HANDLERS[1]).
 */
void T_EFFECT_H_CenterBloom(taskId_t tID) {
    const uint8_t inc    = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                 // Step increment - Setup
    const int     hbHalf = LED_HB_NUM >> 1;                            // Midpoint - Setup

    // --- PHASE 1: Expand center-out at +50% bloom ---
    if (HB::State.Phase == 1) {
        if (HB::State.ParamA < hbHalf) {
            int idxL    = LED::HB(hbHalf - 1 - HB::State.ParamA);                  // Left pixel - Mapping
            int idxR    = LED::HB(hbHalf + HB::State.ParamA);                      // Right pixel - Mapping
            int pairs[] = { idxL, idxR };
            bool active = false;
            const int bloom = (LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]) * 15) / 10; // +50% - Setup

            for (int i = 0; i < 2; i++) {
                int p = pairs[i];
                if (LED::TG_BRIGHTNESS(p, bloom, inc, false)) {
                    LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
                    active = true;                                      // Pair still fading - State
                }
            }
            if (!active) HB::State.ParamA++;                                      // Pair settled, advance - State
        } else {
            APP::updDeltaColors();                                   // Bloom done - Sync
            HB::State.Phase = 2; HB::State.ParamA = 0;                                    // Enter settle phase - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # phase1->2 (bloom done)" NL, "H_CenterBloom"));
            #endif
        }
        return;
    }

    // --- PHASE 2: Settle all pixels from +50% -> 100% EEPROM target ---
    if (HB::State.Phase == 2) {
        const int target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]); // Final target - Setup
        bool busy = false;

        for (int i = 0; i < LED_HB_NUM; i++) {
            int ledIdx = LED::HB(i);
            if (LED::TG_BRIGHTNESS(ledIdx, target, inc, false)) {
                LED::setPixel(ledIdx, LED::State.CurrentColor[ledIdx].r, LED::State.CurrentColor[ledIdx].g, LED::State.CurrentColor[ledIdx].b, LED::State.CurrentBrightness[ledIdx], false);
                busy = true;                                            // Still settling - State
            }
        }
        if (!busy) {
            APP::updDeltaColors();                                   // Notify app - Sync
            HB::State.Phase = taskDone;                                         // HB complete - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "H_CenterBloom"));
            #endif
        }
    }
}

/**
 * @brief  HB helper -- sequential linear sweep from pixel 0 upward.
 *
 * HB::State.Phase==1 (sweep): each tick expands the active window [0..HB::State.ParamA] by one,
 *   fading all pixels in the window toward EEPROM target.
 * HB::State.Phase==2 (settle): all pixels step to final EEPROM target.
 * Sets HB::State.Phase = taskDone when settled.
 *
 * Used by: Effect 8 (ComEffect).
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (HB_ON_HANDLERS[2]).
 */
void T_EFFECT_H_LinearSweep(taskId_t tID) {
    const uint8_t inc    = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                 // Step increment - Setup
    const int     target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]); // EE target - Setup

    // --- PHASE 1: Sweep pixel 0..tA into view ---
    if (HB::State.Phase == 1) {
        if (HB::State.ParamA < LED_HB_NUM) HB::State.ParamA++;                               // Expand active window - State
        bool active = false;

        for (int i = 0; i < HB::State.ParamA; i++) {
            int p = LED::HB(i);
            if (LED::TG_BRIGHTNESS(p, target, inc, false)) {
                LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
                active = true;                                          // Pixel still fading - State
            }
        }
        if (HB::State.ParamA >= LED_HB_NUM && !active) {
            APP::updDeltaColors();                                   // Sweep done - Sync
            HB::State.Phase = 2;                                                // Enter settle phase - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # phase1->2 (sweep done)" NL, "H_LinearSweep"));
            #endif
        }
        return;
    }

    // --- PHASE 2: Settle to final EEPROM target ---
    if (HB::State.Phase == 2) {
        bool busy = false;
        for (int i = 0; i < LED_HB_NUM; i++) {
            int p = LED::HB(i);
            if (LED::TG_BRIGHTNESS(p, target, inc, false)) {
                LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false);
                busy = true;                                            // Still settling - State
            }
        }
        if (!busy) {
            APP::updDeltaColors();                                   // Notify app - Sync
            HB::State.Phase = taskDone;                                         // HB complete - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "H_LinearSweep"));
            #endif
        }
    }
}

/**
 * @brief  HB helper -- quad-anchor expansion from 4 evenly spaced strip points.
 *
 * Divides HB strip into 4 equal segments; each expands symmetrically from its
 * center anchor. HB::State.Phase==1: expand. HB::State.Phase==2: settle to EEPROM target.
 * Sets HB::State.Phase = taskDone when settled.
 *
 * Used by: Effect 9 (QuadPointHB).
 * @param  tID  Task handle (passed from T_EFFECT_TV_ON).
 *
 * Called by: T_EFFECT_TV_ON() (HB_ON_HANDLERS[3]).
 */
void T_EFFECT_H_QuadPoint(taskId_t tID) {
    const uint8_t inc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                // Step increment - Setup
    const int     hbTotal = LED_HB_NUM;                                 // Total HB LEDs - Setup
    const int     segment = hbTotal >> 2;                               // Quarter segment length - Setup
    const int     reach   = segment >> 1;                               // Max expand radius per anchor - Setup

    const int p1 = (segment * 0) + (segment >> 1);                     // Q1 center - Setup
    const int p2 = (segment * 1) + (segment >> 1);                     // Q2 center - Setup
    const int p3 = (segment * 2) + (segment >> 1);                     // Q3 center - Setup
    const int p4 = (segment * 3) + (segment >> 1);                     // Q4 center - Setup

    // --- PHASE 1: Expand from 4 anchors simultaneously ---
    if (HB::State.Phase == 1) {
        const int target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]); // EE target - Setup
        bool active = false;

        if (HB::State.ParamA <= reach) {
            int pts[] = {
                p1 - HB::State.ParamA, p1 + HB::State.ParamA,                                // Anchor 1 pair - Mapping
                p2 - HB::State.ParamA, p2 + HB::State.ParamA,                                // Anchor 2 pair - Mapping
                p3 - HB::State.ParamA, p3 + HB::State.ParamA,                                // Anchor 3 pair - Mapping
                p4 - HB::State.ParamA, p4 + HB::State.ParamA                                 // Anchor 4 pair - Mapping
            };
            for (int i = 0; i < 8; i++) {
                int p = pts[i];
                if (p >= 0 && p < hbTotal) {
                    int realIdx = LED::HB(p);
                    if (LED::TG_BRIGHTNESS(realIdx, target, inc, false)) {
                        LED::setPixel(realIdx, LED::State.TargetColor[realIdx].r, LED::State.TargetColor[realIdx].g, LED::State.TargetColor[realIdx].b, LED::State.CurrentBrightness[realIdx], false);
                        active = true;                                  // Still fading - State
                    }
                }
            }
            if (!active && HB::State.ParamA < reach)  { HB::State.ParamA++; }               // Advance expansion - State
            else if (!active)              {
                HB::State.Phase = 2; HB::State.ParamA = 0; // Expansion complete - State
                #ifdef ENABLE_LOG_ANIME_INFO
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # phase1->2 (expand done)" NL, "H_QuadPoint"));
                #endif
            }
        }
        return;
    }

    // --- PHASE 2: Settle all HB pixels to final EEPROM target ---
    if (HB::State.Phase == 2) {
        const int finalT = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]); // Final target - Setup
        bool busy = false;
        for (int i = 0; i < hbTotal; i++) {
            int id = LED::HB(i);
            if (LED::TG_BRIGHTNESS(id, finalT, inc, false)) {
                LED::setPixel(id, LED::State.CurrentColor[id].r, LED::State.CurrentColor[id].g, LED::State.CurrentColor[id].b, LED::State.CurrentBrightness[id], false);
                busy = true;                                            // Still settling - State
            }
        }
        if (!busy) {
            APP::updDeltaColors();                                   // Final UI sync - Sync
            HB::State.Phase = taskDone;                                         // HB complete - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "H_QuadPoint"));
            #endif
        }
    }
}


/**
 * @brief  TV-off master task -- dispatch the configured TV-off sub-effect.
 *
 * Routes to the sub-effect selected by EE_SET[EE_TV_OFF_EFF]:
 *   0/default = Default (global fade), 1 = DelayWTvOff, 2 = DelayAll,
 *   3 = SlowTvSequential, 4/5 = Countdown, 6 = RandomHalf.
 * On completion (TASK.Phase == taskDone): restores MOTION::State.Status = motON,
 * syncs status and colours to the app, kills all tasks.
 *
 * @param  taskId  Task handle supplied by the scheduler.
 *
 * Called by: TV::Off() (TSK::AddTask, registered unlocked, TASK_MS interval).
 */
void T_EFFECT_TV_OFF(taskId_t taskId) {
    if (TASK.Phase != taskDone) {                                        // Check if task is active - Logic
        int tvEffIdx = EE::Get(EE_TV_OFF_EFF);
        if (tvEffIdx < 0 || tvEffIdx >= TV_OFF_HANDLERS_COUNT) tvEffIdx = 0;
        
        // Handle special TASK.ParamB pre-configuration for specific effects
        if (tvEffIdx == 4) TASK.ParamB = 0;       // Countdown (normal)
        else if (tvEffIdx == 5) TASK.ParamB = 1;  // Bomb Countdown
        
        TV_OFF_HANDLERS[tvEffIdx](taskId);
    }

    // --- TERMINATION SEQUENCE ---
    if (TASK.Phase == taskDone) {                 // Ensure everything is off - State
        #ifdef ENABLE_LOG_TASK
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # TV full off" NL, "T_EFFECT_TV_OFF"));     // Debug logging - Logic
        #endif
        
        MOTION::State.Status = motON;                                          // Re-enable motion detection - State
        APP::updStatus("LED::T_EFFECT_TV_OFF");                                            // Update mobile dashboard - Sync
        APP::updDeltaColors();                                     // Final UI color sync - Sync
        TSK::KillTasksAvoidLocked("T_EFFECT_TV_OFF");                        // Cleanup task memory - Action
        TV::State.Transitioning = false;                                 // Off-transition finished - State
    } 
    else if (TASK.Phase != taskDone) {                                   // Still animating? - Logic
        LED::Show();                                                     // Push frame to hardware - Output
    }
}

/**
 * @brief  TV-off default -- fade all main and HB LEDs uniformly to black.
 *
 * Steps every LED in LED_NUM (main) and LED_HB_NUM (heartbeat) toward 0 brightness
 * using EE_TV_ON_BR_CL_INC. Sets taskDone when all LEDs are off.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[0]).
 */
void T_EFFECT_TV_OFF_Default(taskId_t tID) { // Standard Fade-Off Animation
    bool moving = false;                                                // Activity tracker - State
    
    const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));                         // Cache fade speed - Setup
    const int mainLimit = LED_NUM - LED_HB_NUM_FAKE;                    // Main strip bounds - Setup

    // 1. Process Main Strip (TV, Com, Bed, etc.)
    // Iterates through all non-heartboard pixels to fade them to black.
    for (int i = 0; i < mainLimit; i++) {
        if (LED::TG_BRIGHTNESS(i, 0, inc, false)) {                      // Target 0 (Off) - Logic
            LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false); 
            moving = true;                                              // Pixel still fading - State
        }
    }

    // 2. Process Heartboard (HB) Strip
    // Specifically targets heartboard mapping to ensure they turn off in sync.
    for (int i = 0; i < LED_HB_NUM; i++) {
        int idx = LED::HB(i);                                            // Hardware index - Mapping
        if (LED::TG_BRIGHTNESS(idx, 0, inc, false)) {                    // Target 0 (Off) - Logic
            LED::setPixel(idx, LED::State.CurrentColor[idx].r, LED::State.CurrentColor[idx].g, LED::State.CurrentColor[idx].b, LED::State.CurrentBrightness[idx], false); 
            moving = true;                                              // HB still fading - State
        }
    }

    // 3. Hardware Sync and Task Management
    if (moving) {
        LED::Show();                                                     // Push changes to hardware - Output
    } else {
        TASK.Phase = taskDone;                                           // Mark task complete - State
        APP::updDeltaColors();                                     // Final UI sync - Sync
        #ifdef ENABLE_LOG_ANIME_INFO
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_Default"));
        #endif
    }
}

/**
 * @brief  TV-off effect 1 -- TV strip fades first, then room zones fade sequentially.
 *
 * Phase 1: TV LEDs fade to 0. Then a stepDelay pause (EE_TV_OFF_TIME seconds).
 * Phases 2-5: one zone at a time (COM, UCOM, BED, LAMP) using LED::State.PixelOrder[] sequence,
 * each with an EE_TV_OFF_TIME pause between. HB pixels track each zone's progress.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[1]).
 */
void T_EFFECT_TV_OFF_1_DelayWTvOff(taskId_t tID) { // Delayed Zone Shutdown
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                      // Cache brightness speed - Setup
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));                  // Cache animation speed - Setup
    const uint32_t stepDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));      // Cache long delay - Setup
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;                      // Main bounds - Setup
    const int maxHbLeds = LED_HB_NUM;                                   // HB count - Setup

    // --- PHASE 1: IMMEDIATE TV DIMMING ---
    // This phase acts as the "trigger" visual, turning off the TV strip before the long delay.
    if (phase == 1) { 
        bool tvMoving = false;                                          // Track TV activity - State
        
        for (int i = 0; i < LED_TV_NUM; i++) {
            int l = LED::TV(i);                                          // Hardware index - Mapping
            if (LED::TG_BRIGHTNESS(l, 0, brInc, false)) {                 // Fade to zero - Logic
                LED::setPixel(l, LED::State.CurrentColor[l].r, LED::State.CurrentColor[l].g, LED::State.CurrentColor[l].b, LED::State.CurrentBrightness[l], false); 
                tvMoving = true;                                        // TV still active - State
            }
        }

        if (tvMoving) {
            TSK::setTaskInterval("T_EFFECT_TV_OFF_1_DelayWTvOff", tID, TASK_MS, animDelay);    // Fast update during fade - Setup
        } else {
            APP::updDeltaColors();                              // Sync TV zone UI - Sync
            TASK.Phase = 2; TASK.ParamA = 0;                                 // Proceed to Sequential Zones - State
            HB::State.ParamA = 0; HB::State.Phase = 0;                                     // Reset HB tracking - Setup
            TSK::setTaskInterval("T_EFFECT_TV_OFF_1_DelayWTvOff", tID, TASK_MS, stepDelay);    // Apply long delay before next zone - Setup
        }
        return;                                                         // Exit for this tick - Logic
    }

    // --- PHASE 2: SEQUENTIAL ZONE SHUTDOWN & LINKED HB DIMMING ---
    // Cycles through zones (COM, UCOM, BED, LAMP) with delays between each.
    if (phase >= 2 && phase <= 5) {
        if (TASK.ParamA < maxLeds) {
            bool moving = false;                                        // Local tracker - State
            const int l = LED::State.PixelOrder[TASK.ParamA];                           // Get LED by programmed order - Mapping
            
            // Detect Zone (1:TV, 2:COM, 3:UCOM, 4:BED, 5:LAMP)
            int lZone = (l < LED_TV_NUM) ? 1 : 
                        (l < LED_TV_NUM + LED_COM_NUM) ? 2 : 
                        (l < LED_TV_NUM + LED_COM_NUM + LED_UCOM_NUM) ? 3 : 
                        (l < LED_TV_NUM + LED_COM_NUM + LED_UCOM_NUM + LED_BED_NUM) ? 4 : 5;

            if (lZone == phase) {                                        // Process active zone - Logic
                // 1. Dim the Main LED
                if (LED::TG_BRIGHTNESS(l, 0, brInc, false)) {
                    LED::setPixel(l, LED::State.CurrentColor[l].r, LED::State.CurrentColor[l].g, LED::State.CurrentColor[l].b, LED::State.CurrentBrightness[l], true); 
                    moving = true;                                      // Main still fading - State
                }

                // 2. Dim the Linked HB Block
                // Calculates which HB pixels correspond to the current main strip progress.
                int hbCountForZone = round((float)LED_HB_NUM / (maxLeds - LED_TV_NUM)); 
                int hbStart = HB::State.ParamA * hbCountForZone;                   // Precise start - Mapping
                int hbEnd = (HB::State.ParamA == maxLeds - 1) ? maxHbLeds : (hbStart + hbCountForZone); 
                if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;               // Clamp - Safety

                for (int i = hbStart; i < hbEnd; i++) {
                    int hbIdx = LED::HB(LED::State.HeartbeatOrder[i]);                 // HB hardware index - Mapping
                    if (LED::TG_BRIGHTNESS(hbIdx, 0, brInc, false)) {
                        LED::setPixel(hbIdx, LED::State.CurrentColor[hbIdx].r, LED::State.CurrentColor[hbIdx].g, LED::State.CurrentColor[hbIdx].b, LED::State.CurrentBrightness[hbIdx], true);
                        moving = true;                                  // HB block still fading - State
                    }
                }

                if (moving) {
                    TSK::setTaskInterval("T_EFFECT_TV_OFF_1_DelayWTvOff", tID, TASK_MS, animDelay); 
                } else {
                    APP::updDeltaColors();                               // Notify UI - Sync
                    TASK.ParamA++;                                          // Next LED - State
                    HB::State.ParamA++;                                            // Move HB tracking - State
                    TSK::setTaskInterval("T_EFFECT_TV_OFF_1_DelayWTvOff", tID, TASK_MS, stepDelay); 
                }
            } else {
                TASK.ParamA++;                                              // Skip non-active zone - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_1_DelayWTvOff", tID, TASK_MS, 0);        // Immediate skip - Logic
            }
        } else {
            TASK.ParamA = 0; TASK.Phase++;                                   // Move to next zone index - State
            if (TASK.Phase > 5) {
                TASK.Phase = taskDone;                    // Finish effect - State
                #ifdef ENABLE_LOG_ANIME_INFO
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_1_DelayWTvOff"));
                #endif
            }
        }
    }
}

/**
 * @brief  TV-off effect 2 -- sequential "domino" shutdown of every LED in order.
 *
 * Iterates through LED::State.PixelOrder[], fading each LED and its proportional HB block to 0,
 * waiting EE_TV_OFF_TIME seconds between each LED::State. Produces a spreading darkness effect.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[2]).
 */
void T_EFFECT_TV_OFF_2_DelayAll(taskId_t tID) { // Sequential "Domino" Shutdown
    const int totalLeds = LED_NUM - LED_HB_NUM_FAKE;                    // Total pixels to process - Setup
    const int maxHbLeds = LED_HB_NUM;                                   // HB count - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                      // Fade speed per step - Setup
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));                  // Interval while fading - Setup
    const uint32_t nextLedDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME)); // Delay between different LEDs - Setup

    if (TASK.Phase == 1) { // --- SEQUENTIAL SHUTDOWN ---
        if (TASK.ParamA < totalLeds) {
            bool moving = false;                                        // Track if current group is fading - State
            const int l = LED::State.PixelOrder[TASK.ParamA];                           // Current LED hardware index - Mapping
            
            // 1. Dim the Main LED (TV, Com, Bed, etc.)
            // Follows the user-defined order to turn off pixels one by one.
            if (LED::TG_BRIGHTNESS(l, 0, brInc, false)) {
                LED::setPixel(l, LED::State.CurrentColor[l].r, LED::State.CurrentColor[l].g, LED::State.CurrentColor[l].b, LED::State.CurrentBrightness[l], false); 
                moving = true;                                          // Main still fading - State
            }

            // 2. Dim the Linked HB Block
            // Calculates a proportional slice of the Heartboard to turn off alongside each main LED::State.
            const float hbRatio = (float)maxHbLeds / totalLeds;         // Proportion - Setup
            int hbStart = round(TASK.ParamA * hbRatio);                     // Precise start index - Mapping
            int hbEnd = round((TASK.ParamA + 1) * hbRatio);                 // Precise end index - Mapping
            
            if (TASK.ParamA == totalLeds - 1) hbEnd = maxHbLeds;            // Catch remainder - Logic
            if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;                   // Safety clamp - Logic

            for (int i = hbStart; i < hbEnd; i++) {
                int hbIdx = LED::HB(LED::State.HeartbeatOrder[i]);                     // HB hardware index - Mapping
                if (LED::TG_BRIGHTNESS(hbIdx, 0, brInc, false)) {
                    LED::setPixel(hbIdx, LED::State.CurrentColor[hbIdx].r, LED::State.CurrentColor[hbIdx].g, LED::State.CurrentColor[hbIdx].b, LED::State.CurrentBrightness[hbIdx], false);
                    moving = true;                                      // HB block still fading - State
                }
            }

            // --- TACT LOGIC ---
            if (moving) {
                // If the current LED or its HB block are still fading, stay on this index.
                TSK::setTaskInterval("T_EFFECT_TV_OFF_2_DelayAll", tID, TASK_MS, animDelay); 
            } else {
                // Current group has reached 0. Sync UI and pause before the next one.
                APP::updDeltaColors();                                   // Sync mobile app UI - Sync
                TASK.ParamA++;                                              // Move to next LED in order - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_2_DelayAll", tID, TASK_MS, nextLedDelay); 
            }
        } else {
            TASK.Phase = taskDone;                                       // Entire sequence complete - State
            APP::updDeltaColors();                                 // Final cleanup sync - Sync
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_2_DelayAll"));
            #endif
        }
    }
}

/**
 * @brief  TV-off effect 3 -- zone-by-zone symmetric shutdown with HB center-out.
 *
 * Fades each zone (TV, COM, UCOM, BED, LAMP) to 0 in sequence, pausing
 * EE_TV_OFF_TIME seconds between zones. HB pixels are divided into 5 mirrored
 * segments that expand outward as each zone shuts down.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[3]).
 */
void T_EFFECT_TV_OFF_3_SlowTvSequential(taskId_t tID) { // Symmetrical Zone Shutdown
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                      // Cache brightness speed - Setup
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));                  // Cache animation interval - Setup
    const uint32_t stepDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));  // Cache delay between zones - Setup

    if (phase >= 1 && phase <= 5) {
        bool moving = false;                                            // Track if any LED is still dimming - State
        int mI = 0;                                                     // Main zone LED count - Setup

        // 1. Identify Main Zone Size
        // Determines how many LEDs are in the current functional group (TV, COM, etc.)
        switch (phase) {
            case 1: mI = LED_TV_NUM;   break;                           // TV Zone - Setup
            case 2: mI = LED_COM_NUM;  break;                           // COM Zone - Setup
            case 3: mI = LED_UCOM_NUM; break;                           // UCOM Zone - Setup
            case 4: mI = LED_BED_NUM;  break;                           // BED Zone - Setup
            case 5: mI = LED_LAMP_NUM; break;                           // LAMP Zone - Setup
        }

        // 2. Process Main Zone LEDs
        // Iterates through the specific hardware mapping for the active zone.
        for (int i = 0; i < mI; i++) {
            int l;                                                      // Hardware index - Mapping
            if      (phase == 1) l = LED::TV(i);
            else if (phase == 2) l = LED::COM(i);
            else if (phase == 3) l = LED::UCOM(i);
            else if (phase == 4) l = LED::BED(i);
            else                l = LED::LAMP(i);

            if (LED::TG_BRIGHTNESS(l, 0, brInc, false)) {                // Lower toward zero - Logic
                LED::setPixel(l, LED::State.CurrentColor[l].r, LED::State.CurrentColor[l].g, LED::State.CurrentColor[l].b, LED::State.CurrentBrightness[l], false); 
                moving = true;                                          // Active - State
            }
        }

        // 3. Process HB Symmetrical Segment (Center-to-Out)
        // Splits the Heartboard into 5 mirrored pairs that expand outward as zones shut down.
        const float hbPerZone = (float)LED_HB_NUM / 5.0f;               // Total LEDs per zone - Setup
        int halfHb    = LED_HB_NUM >> 1;                                // Center point - Setup
        int startIdx  = round((phase - 1) * (hbPerZone / 2.0f));         // Start offset from center - Logic
        int endIdx    = round(phase * (hbPerZone / 2.0f));               // End offset - Logic
        
        if (phase == 5) endIdx = halfHb;                                 // Catch remainder - Logic

        for (int i = startIdx; i < endIdx; i++) {
            // Mirror indices: one moving Left, one moving Right from center
            int hbL = LED::HB(halfHb - 1 - i);                           // Inner to Start - Mapping
            int hbR = LED::HB(halfHb + i);                               // Inner to End - Mapping

            int pair[2] = {hbL, hbR};                                   // Mirror pair - Setup
            for (int j = 0; j < 2; j++) {
                int p = pair[j];                                        // Target pixel - Logic
                if (LED::TG_BRIGHTNESS(p, 0, brInc, false)) {             // Fade toward 0 - Logic
                    LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false);
                    moving = true;                                      // HB still active - State
                }
            }
        }
            
        // --- CONTROL LOGIC ---
        if (moving) { 
                TSK::setTaskInterval("T_EFFECT_TV_OFF_3_SlowTvSequential", tID, TASK_MS, animDelay);    // Stay on current zone - Setup
            } else {
                APP::updDeltaColors();                                 // Sync UI - Sync
                TASK.Phase++;                                                // Move to next zone group - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_3_SlowTvSequential", tID, TASK_MS, stepDelay);    // Pause before next zone - Setup
            if (TASK.Phase > 5) {
                TASK.Phase = taskDone;                                   // Finish sequence - State
                #ifdef ENABLE_LOG_ANIME_INFO
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_3_SlowTvSequential"));
                #endif
            }
        }
        LED::Show();                                                     // Render current frame - Output
    }
}

/**
 * @brief  TV-off effects 4 & 5 -- countdown flicker-off (bomb/fuse effect).
 *
 * Phase 1: set start pixel = last TV LED, begin countdown timer.
 * Phase 2: the current TV LED and its linked HB block flicker randomly while a
 *          countdown timer runs (EE_TV_OFF_TIME seconds). On expiry, the LED turns
 *          off permanently and the effect moves to the previous TV LED::State. Effect 5
 *          uses TASK.ParamB=1 to enable the flicker overlay.
 * Phase 3: all remaining zones fade globally to 0.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[4] and [5]).
 */
void T_EFFECT_TV_OFF_4_5_Countdown(taskId_t tID) { // Countdown Flicker Off
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                      // Cache brightness speed - Setup
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;                      // Cache main bounds - Setup
    const int maxHbLeds = LED_HB_NUM;                                   // Cache HB count - Setup

    // --- PHASE 1: INITIALIZATION ---
    // Sets the starting point at the end of the TV strip and prepares the first timer.
    if (TASK.Phase == 1) { 
        TASK.ParamA = LED::TV(LED_TV_NUM - 1);                               // Start at last TV LED - Setup
        TV::State.CountdownTimer = TimeNow + S_TO_MS(EE::Get(EE_TV_OFF_TIME));   // Countdown timer - Setup
        TSK::setTaskInterval("T_EFFECT_TV_OFF_4_5", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL))); 
        TASK.Phase = 2;                                                  // Move to Countdown - State
        return;                                                         // Exit for this tick - Logic
    }

    // --- PHASE 2: "BOMB" COUNTDOWN (TV & HB BLINKING) ---
    // Simulates a failing light or countdown by flickering a TV pixel and its linked HB block.
    if (TASK.Phase == 2) {
        randomSeed(analogRead(A0));                                     // Noise for flicker - Setup
        
        // Calculate which HB block corresponds to current TV LED (Right to Left)
        const float hbRatio = (float)maxHbLeds / LED_TV_NUM;            // Proportion - Setup
        int tvIdxInZone = TASK.ParamA - LED::TV(0);                          // Relative index - Logic
        int hbStart = round(tvIdxInZone * hbRatio);                     // Start of block - Mapping
        int hbEnd = round((tvIdxInZone + 1) * hbRatio);                 // End of block - Mapping
        
        if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;                       // Clamp - Safety

        // 1. Flicker Effect
        // If tB is used as a toggle or flag, it creates a random brightness drop.
        if (TASK.ParamB == 1) { 
            uint8_t targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[TASK.ParamA]);  // Normal brightness - Setup
            uint8_t rndBr = (uint8_t)random(2, targetBr);               // Random dimming - Logic
            
            // Flicker TV LED
            LED::setPixel(TASK.ParamA, LED::State.StoredColor[TASK.ParamA].r, LED::State.StoredColor[TASK.ParamA].g, LED::State.StoredColor[TASK.ParamA].b, rndBr, true);
            
            // Flicker corresponding HB block
            for (int i = hbStart; i < hbEnd; i++) {
                int hIdx = LED::HB(i);                                   // HB hardware index - Mapping
                LED::setPixel(hIdx, LED::State.StoredColor[hIdx].r, LED::State.StoredColor[hIdx].g, LED::State.StoredColor[hIdx].b, rndBr, true);
            }
        }

        TSK::setTaskInterval("T_EFFECT_TV_OFF_4_5", tID, TASK_MS, (uint32_t)random(30, 50)); // Random jitter - Setup
        
        // 2. Kill LED when timer expires
        // Once the countdown for the current pixel hits zero, it turns off permanently.
        if (TV::State.CountdownTimer < TimeNow) {
            TV::State.CountdownTimer = TimeNow + S_TO_MS(EE::Get(EE_TV_OFF_TIME)); // Reset timer - State

            // Turn OFF TV LED
            LED::setPixel(TASK.ParamA, LED::State.StoredColor[TASK.ParamA].r, LED::State.StoredColor[TASK.ParamA].g, LED::State.StoredColor[TASK.ParamA].b, 0, true);

            // Turn OFF HB Block
            for (int i = hbStart; i < hbEnd; i++) {
                int hIdx = LED::HB(i);                                   // HB hardware index - Mapping
                LED::setPixel(hIdx, LED::State.StoredColor[hIdx].r, LED::State.StoredColor[hIdx].g, LED::State.StoredColor[hIdx].b, 0, true);
            }

            APP::updDeltaColors();                                 // Sync UI - Sync

            if (TASK.ParamA == LED::TV(0)) {                                 // All blocks extinguished - Logic
                TASK.Phase = 3;                                          // Move to Global Fade - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_4_5", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL))); 
            } else {
                TASK.ParamA--;                                              // Move to previous LED - State
            }
        }
        LED::Show();                                                     // Render flicker - Output
        return; 
    }

    // --- PHASE 3: GLOBAL FADE OUT (REMAINING ZONES) ---
    // Fades all other room lights (Com, Bed, etc.) to black simultaneously.
    if (TASK.Phase == 3) {
        bool moving = false;                                            // Activity tracker - State

        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_BRIGHTNESS(i, 0, brInc, false)) {                // Target 0 (Off) - Logic
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                moving = true;                                          // Still dimming - State
            }
        }

        if (moving) {
            TSK::setTaskInterval("T_EFFECT_TV_OFF_4_5", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL))); 
        } else {
            TASK.Phase = taskDone;                                       // Effect complete - State
            APP::updDeltaColors();                                 // Final cleanup - Sync
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_4_5_Countdown"));
            #endif
        }
        LED::Show();                                                     // Render final fade - Output
    }
}

/**
 * @brief  TV-off effect 6 -- dual circular wipe with HB center-to-out unzip.
 *
 * Phase 1: pick a random start offset on the TV ring.
 * Phase 2: two wipe points travel around the TV ring 180deg apart, while HB pixels
 *          unzip outward from center in proportion to the wipe progress.
 * Phase 3: remaining room zones (COM, BED, LAMP) fade globally to 0.
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[6]).
 */
void T_EFFECT_TV_OFF_6_RandomHalf(taskId_t tID) { // Circular Wipe Shutdown
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                      // Cache brightness speed - Setup
    const int tvCount = LED_TV_NUM;                                     // Cache TV LED count - Setup
    const int halfTv = tvCount >> 1;                                    // Bitshift for /2 - Setup
    const int maxHbLeds = LED_HB_NUM;                                   // Cache HB count - Setup
    const int halfHb = maxHbLeds >> 1;                                  // HB center point - Setup

    // --- STEP 1: INITIALIZATION ---
    // Picks a random starting point on the TV ring to begin the dual-wipe.
    if (phase == 1) { 
        TASK.ParamB = random(0, halfTv);                                    // Set random start offset - Logic
        TASK.Phase = 2;                                                  // Transition to wipe - State
        return;                                                         // Exit tick - Logic
    }

    // --- STEP 2: CIRCULAR WIPE & HB CENTER-TO-OUT ---
    // Two points start at opposite sides of the TV and "wipe" around until they meet.
    // Simultaneously, the Heartboard "unzips" from the center.
    if (phase == 2) { 
        const int ta = TASK.ParamA;                                         // Cache wipe progress - Setup
        const int offset = TASK.ParamB;                                     // Cache random start - Setup

        TSK::setTaskInterval("T_EFFECT_TV_OFF_6_RandomHalf", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)) * 2); 

        if (ta < halfTv) {
            bool moving = false;                                        // Track active dimming - State
            
            // 1. Calculate TV Wipe points
            // These two points travel in the same direction but start 180 degrees apart.
            int targets[2] = {
                (offset - ta + tvCount) % tvCount,                      // Wipe point 1 - Mapping
                (offset + halfTv - ta + tvCount) % tvCount              // Wipe point 2 (Opposite) - Mapping
            };

            for (int i = 0; i < 2; i++) {
                int p = LED::TV(targets[i]);                             // Get hardware index - Mapping
                if (LED::TG_BRIGHTNESS(p, 0, brInc, false)) {             // Target 0 brightness - Logic
                    LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false); 
                    moving = true;                                      // Still dimming - State
                }
            }

            // 2. Calculate HB Center-to-Out points
            // Proportional mapping ensures HB finishes exactly with the TV wipe.
            float hbRatio = (float)halfHb / (float)halfTv;              // Progress ratio - Setup
            int hbStart = round((float)ta * hbRatio);                   // Dynamic start - Mapping
            int hbEnd = round((float)(ta + 1) * hbRatio);               // Dynamic end - Mapping
            if (hbEnd > halfHb) hbEnd = halfHb;                         // Safety clamp - Logic

            for (int i = hbStart; i < hbEnd; i++) {
                int hbL = LED::HB(halfHb - 1 - i);                       // Mirror Left - Mapping
                int hbR = LED::HB(halfHb + i);                           // Mirror Right - Mapping
                
                int pair[2] = {hbL, hbR};
                for (int j = 0; j < 2; j++) {
                    if (LED::TG_BRIGHTNESS(pair[j], 0, brInc, false)) {
                        LED::setPixel(pair[j], LED::State.CurrentColor[pair[j]].r, LED::State.CurrentColor[pair[j]].g, LED::State.CurrentColor[pair[j]].b, LED::State.CurrentBrightness[pair[j]], false);
                        moving = true;                                  // HB active - State
                    }
                }
            }

            if (!moving)  {
                APP::updDeltaColors();                          // Sync UI - Sync
                TASK.ParamA++;                                              // Move to next pixel pair - State
            }
        } else {
            TASK.Phase = 3; TASK.ParamA = 0; TASK.ParamB = 0;                    // Transition to global off - State
            TSK::setTaskInterval("T_EFFECT_TV_OFF_6_RandomHalf", tID, TASK_S, (uint32_t)EE::Get(EE_TV_OFF_TIME)); 
        }
        LED::Show();                                                     // Render frame - Output
        return; 
    }

    // --- STEP 3: GLOBAL FADE TO BLACK ---
    // After the TV/HB wipe finishes, all other room zones (Com, Bed, Lamp) fade out together.
    if (phase == 3) { 
        bool moving = false;                                            // Change flag - State
        const int limit = LED_NUM - LED_HB_NUM_FAKE;                    // Cache global limit - Setup

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS(i, 0, brInc, false)) {                // Dim everything - Logic
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false); 
                moving = true;                                          // Keep loop running - State
            }
        }

        if (moving) {                                                   
            TSK::setTaskInterval("T_EFFECT_TV_OFF_6_RandomHalf", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL))); 
        } else {
            APP::updDeltaColors();                                 // Final sync - Sync
            TASK.Phase = taskDone;                                       // Finish task - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_6_RandomHalf"));
            #endif
        }
        LED::Show();                                                     // Final render - Output
    }
}


/**
 * @brief  TV-off effect 7 -- Quad-Point HB collapse + TV extremes-to-centre wipe, with per-step delays.
 *
 * Three-phase shutdown (reverse mirror of T_EFFECT_TV_ON_9_QuadPointHB):
 *   Step 1 -- Room zones (COM, BED, LAMP) fade to 0 together at animDelay pace.
 *   Step 2 -- HB quad-point collapse: four anchors retract one ring at a time;
 *             each ring pair waits EE_TV_OFF_TIME seconds before the next ring dims.
 *   Step 3 -- TV extremes-to-centre wipe: left/right pointers converge toward mid;
 *             each pixel pair waits EE_TV_OFF_TIME seconds before the next pair dims.
 *
 * Uses EE_TV_OFF_BR_CL_INC (fade speed), EE_TV_OFF_BR_CL_DEL (frame delay),
 * EE_TV_OFF_TIME (hold seconds between individual LED steps in phases 2 and 3).
 *
 * @param  tID  Task handle (passed from T_EFFECT_TV_OFF).
 *
 * Called by: T_EFFECT_TV_OFF() (TV_OFF_HANDLERS[7]).
 */
void T_EFFECT_TV_OFF_7_QuadPointHB(taskId_t tID) {
    const int phase    = TASK.Phase;                                      // Cache current phase - Setup
    const int brInc   = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));                   // Cache fade speed - Setup
    const int animDel = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));                   // Frame delay while fading - Setup
    const uint32_t nextDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME)); // Hold between LED steps - Setup

    const int tvCount = LED_TV_NUM;                                     // TV strip length - Setup
    const int tvMid   = tvCount >> 1;                                   // TV midpoint - Setup

    const int hbStart = LED_START_I_HB;                                 // HB physical start index - Setup
    const int hbTotal = LED_HB_NUM;                                     // Total HB LEDs - Setup

    // HB Quad-Point Mapping -- same anchor layout as T_EFFECT_TV_ON_9_QuadPointHB
    const int segment    = hbTotal >> 2;                                // One quarter length - Setup
    const int p1         = (segment * 0) + (segment >> 1);              // Center of 1st quarter - Setup
    const int p2         = (segment * 1) + (segment >> 1);              // Center of 2nd quarter - Setup
    const int p3         = (segment * 2) + (segment >> 1);              // Center of 3rd quarter - Setup
    const int p4         = (segment * 3) + (segment >> 1);              // Center of 4th quarter - Setup
    const int hbMaxReach = segment >> 1;                                // Half-segment expansion limit - Setup

    // --- STEP 1: ROOM ZONES FADE TO 0 (COM / BED / LAMP) ---
    // All room LEDs dim together at normal animation pace before HB and TV begin.
    if (phase == 1) {
        bool moving = false;                                            // Activity flag - State

        for (int i = LED_START_I_COM; i < (LED_NUM - LED_HB_NUM_FAKE); i++) {
            if (LED::TG_BRIGHTNESS(i, 0, brInc, false)) {               // Dim to 0 - Logic
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                moving = true;                                          // Still fading - State
            }
        }

        if (moving) {
            TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, animDel); // Pace - Timing
        } else {
            APP::updDeltaColors();                         // Sync UI after room off - Sync
            TASK.Phase = 2; TASK.ParamA = 0;                                 // Start HB collapse - State
            TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, 0); // Jump immediately - Logic
        }
        LED::Show();                                                     // Render frame - Output
    }

    // --- STEP 2: HB QUAD-POINT COLLAPSE (per-ring delay) ---
    // TASK.ParamA = current ring index (0 = outermost ring, hbMaxReach = fully collapsed).
    // Each tick: fade the current ring's 8 pixels to 0, then wait nextDelay before next ring.
    else if (phase == 2) {
        int ring = TASK.ParamA;                                             // Current collapse ring - Setup

        if (ring <= hbMaxReach) {
            int liveRadius = hbMaxReach - ring;                         // Distance from anchor center - Setup
            bool moving    = false;                                     // Activity flag - State

            int pts[] = {
                p1 - liveRadius, p1 + liveRadius,                       // Anchor 1 outer pair - Mapping
                p2 - liveRadius, p2 + liveRadius,                       // Anchor 2 outer pair - Mapping
                p3 - liveRadius, p3 + liveRadius,                       // Anchor 3 outer pair - Mapping
                p4 - liveRadius, p4 + liveRadius                        // Anchor 4 outer pair - Mapping
            };

            for (int i = 0; i < 8; i++) {
                int p = pts[i];                                         // Local HB index - Logic
                if (p >= 0 && p < hbTotal) {                            // Bounds check - Logic
                    int realIdx = LED::HB(p);                            // Hardware map - Mapping
                    if (LED::TG_BRIGHTNESS(realIdx, 0, brInc, false)) {  // Target 0 - Logic
                        LED::setPixel(realIdx, LED::State.CurrentColor[realIdx].r, LED::State.CurrentColor[realIdx].g, LED::State.CurrentColor[realIdx].b, LED::State.CurrentBrightness[realIdx], false);
                        moving = true;                                  // Still fading - State
                    }
                }
            }

            if (moving) {
                TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, animDel); // Pace fade - Timing
            } else {
                // Ring fully off -- hold nextDelay before collapsing next ring
                APP::updDeltaColors();                             // Sync UI - Sync
                TASK.ParamA++;                                              // Next ring - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, nextDelay); // Hold - Timing
            }
        } else {
            // All HB rings collapsed -- move to TV wipe
            TASK.Phase = 3; TASK.ParamA = 0;                                 // Start TV wipe - State
            TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, 0); // Jump - Logic
        }
        LED::Show();                                                     // Render frame - Output
    }

    // --- STEP 3: TV EXTREMES-TO-CENTRE WIPE (per-pair delay) ---
    // TASK.ParamA = wipe progress (0 = outermost pair, tvMid = centre).
    // Each tick: fade left/right pixel pair to 0, then wait nextDelay before the next pair.
    else if (phase == 3) {
        int ta = TASK.ParamA;                                               // Wipe progress - Setup

        if (ta <= tvMid) {
            int pL = LED::TV(ta);                                        // Left pointer -- hardware mapped (0 -> mid) - Mapping
            int pR = LED::TV((tvCount - 1) - ta);                        // Right pointer -- hardware mapped (end -> mid) - Mapping
            int pixels[] = {pL, pR};                                    // Converging pair (hardware indices) - Logic
            bool moving = false;                                        // Activity flag - State

            for (int i = 0; i < 2; i++) {
                int p = pixels[i];                                      // Current hardware pixel index - Logic
                if (LED::TG_BRIGHTNESS(p, 0, brInc, false)) {            // Target 0 - Logic
                    LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false);
                    moving = true;                                      // Still dimming - State
                }
            }

            if (moving) {
                TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, animDel); // Pace fade - Timing
            } else {
                // Pair fully off -- hold nextDelay before moving to next pair
                APP::updDeltaColors();                                  // Sync UI (hardware index) - Sync
                TASK.ParamA++;                                              // Next pair - State
                TSK::setTaskInterval("T_EFFECT_TV_OFF_7_QuadPointHB", tID, TASK_MS, nextDelay); // Hold - Timing
            }
        } else {
            APP::updDeltaColors();                                 // Final UI sync - Sync
            TASK.Phase = taskDone;                                       // Signal completion - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "TV_OFF_7_QuadPointHB"));
            #endif
        }
        LED::Show();                                                     // Render frame - Output
    }
}

/**
 * @brief  Push a new on/off event into TV::State.LOG ring-buffer (newest at [0]).
 *
 * @param  event         true = TV turned ON, false = TV turned OFF.
 * @param  pinBefore     TV::State.PrevPinValue -- reading from the prior Status() cycle.
 * @param  pinAtTrigger  TV::State.PinValue -- reading at the moment the transition was confirmed.
 */
void LogPush(bool event, int pinBefore, int pinAtTrigger) {
    for (int i = TV_LOG_INDEX_MAX - 1; i > 0; i--) {                    // Shift entries down - Action
        TV::State.LOG[i] = TV::State.LOG[i - 1];                                      // Ring shift - Mapping
    }
    TV::State.LOG[0].epoch               = RTC_TimeClient.getEpochTime();      // Epoch - Mapping
    TV::State.LOG[0].Event                = event;                             // Event - Mapping
    TV::State.LOG[0].PinValueBefore      = pinBefore;                          // Pin value before - Mapping
    TV::State.LOG[0].PinValueAtTrigger   = pinAtTrigger;                       // Pin value at trigger - Mapping
}





/**
 * @brief  Handle the TV turning OFF -- shuffle order arrays and launch the TV-off effect task.
 *
 * Shuffles both LED::State.PixelOrder and LED::State.HeartbeatOrder, resets MOTION and AM state,
 * kills all tasks, and launches T_EFFECT_TV_OFF with the configured effect
 * (EE_SET[EE_TV_OFF_EFF]) and transition speed.
 *
 * @note   Called automatically by Status(). Can be called manually for testing.
 */
void Off() {
	LogPush(false, TV::State.PrevPinValue, TV::State.PinValue);                    // Record OFF event - Action
	PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # with effect [%d], inc [%d], delay [%d]" NL, "TV_Off", EE::Get(EE_TV_OFF_EFF), LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC)), LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)))); // Log start, lux-adapted speed - Sync
	
	LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE);
	LED::shuffleArray(LED::State.HeartbeatOrder, LED_HB_NUM);

	MOTION::State.Status = motOFF;
	APP::Am.Status     = false;
	APP::updStatus("TV::Off");
	
	TASK.Phase = 1; TASK.ParamA = 0; TASK.ParamB = 0;
	HB::State.Phase   = 0; HB::State.ParamA   = 0;
	TV::State.Transitioning = true;                                      // Cleared by T_EFFECT_TV_OFF's finalization - State

	HB::EndTask();
	TSK::KillTasksAvoidLocked("TV_Off");
    DIF::AutoOff();                                                      // Diffuser off if all sources idle - Action
    TSK::AddTask("TV_Off", "T_EFFECT_TV_OFF", T_EFFECT_TV_OFF, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)), 1, false);
}

/**
 * @brief  Handle the TV turning ON -- prepare colours and launch the TV-on effect tasks.
 *
 * Shuffles LED::State.PixelOrder, prepares LED::State.StoredColor / LED::State.TargetColor according to
 * EE_SET[EE_TV_RANDOM_COLOR_START] (0=off, 1=random per LED, 2=random dual),
 * resets MOTION and AM state, kills all tasks, then launches T_EFFECT_TV_ON
 * each tick -- T_EFFECT_TV_ON calls both the TV sub-effect and the matching T_EFFECT_H_xx helper inline
 * independently so TV strip and HB animate in true parallel.
 *
 * @note   Called automatically by Status(). Can be called manually for testing.
 */
void On() {
    LogPush(true, TV::State.PrevPinValue, TV::State.PinValue);                     // Record ON event - Action
    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # with effect [%d], inc [%d], delay [%d]" NL, "TV_On", EE::Get(EE_TV_ON_EFF), LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC)), LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)))); // Log start, lux-adapted speed - Sync
    
    LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE);                 // Randomize LED sequence - Action

    const int ledlimit  = LED_NUM - LED_HB_NUM_FAKE;                    // Boundary for standard LEDs - Setup
    const int hbLimit   = LED_NUM_TOTAL;                                // Total hardware iterations - Setup
    const int startMode = EE::Get(EE_TV_RANDOM_COLOR_START);             // 0:Off, 1:RandomEvery, 2:RandomDual - Setup
    const int hbHalf    = LED_HB_NUM >> 1;                              // Cache half-point - Setup
    const bool hbDual    = EE::Get(EE_HB_DUAL_COLOR);                      // Cache dual color setting - Setup
    DIF_Colorx difColor;                                                  // Diffuser colour match - Setup
    bool       haveDifColor = false;                                     // true = use difColor, false = derive live - Setup

    // --- PRE-LOOP PREPARATION ---
    if (startMode == 2) {
        CHSV colorA, colorB;
        LED::getHarmoniousPair(colorA, colorB);                           // Gen harmonious color pair - Action
        
        uint8_t r1, g1, b1, r2, g2, b2;
        LED::HsvToRgb(colorA.h, r1, g1, b1);                             // Convert primary HSV->RGB - Logic
        LED::HsvToRgb(colorB.h, r2, g2, b2);                             // Convert secondary HSV->RGB - Logic
        
        LED::setDualColorMapping(r1, g1, b1, r2, g2, b2);              // Apply harmonious pair mapping - Action
        difColor = { r1, g1, b1, r2, g2, b2 };                          // Match diffuser to the same pair - Logic
        haveDifColor = true;
    }

    // --- PREP MAIN STRIP COLORS ---
    if (startMode == 1) {
        uint16_t sumR = 0, sumG = 0, sumB = 0;                          // Accumulate for diffuser match - Setup
        for (int i = 0; i < ledlimit; i++) {
            const uint32_t rgb = LED::getRandomColor();
            LED::State.StoredColor[i] = CRGB((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
            sumR += LED::State.StoredColor[i].r; sumG += LED::State.StoredColor[i].g; sumB += LED::State.StoredColor[i].b; // Track running sum - Logic
        }
        const uint8_t avgR = sumR / ledlimit, avgG = sumG / ledlimit, avgB = sumB / ledlimit;
        difColor = { avgR, avgG, avgB, avgR, avgG, avgB };              // Match diffuser to the average (mirrored if dual) - Logic
        haveDifColor = true;
    }

    // --- STATIC (NON-RANDOM) DIFFUSER MATCH ---
    if (!haveDifColor) {
        if (hbDual) {
            const CRGB &cL = LED::State.StoredColor[LED::UCOM(0)];              // Left dual zone - Setup
            const CRGB &cR = LED::State.StoredColor[LED::UCOM(1)];              // Right dual zone - Setup
            difColor = { cL.r, cL.g, cL.b, cR.r, cR.g, cR.b };          // Match diffuser to dual zone colors - Logic
        } else {
            uint16_t sumR = 0, sumG = 0, sumB = 0;                      // Accumulate COM zone - Setup
            for (int i = 0; i < LED_COM_NUM; i++) {
                const CRGB &c = LED::State.StoredColor[LED::COM(i)];
                sumR += c.r; sumG += c.g; sumB += c.b;
            }
            const uint8_t avgR = sumR / LED_COM_NUM, avgG = sumG / LED_COM_NUM, avgB = sumB / LED_COM_NUM;
            difColor = { avgR, avgG, avgB, avgR, avgG, avgB };          // Match diffuser to COM zone average - Logic
        }
        haveDifColor = true;
    }

    memcpy(LED::State.TargetColor, LED::State.StoredColor, ledlimit * sizeof(CRGB));         // Fast block copy - Action

    // --- HEARTBEAT (HB) Colors ---
    for (int i = ledlimit; i < hbLimit; i++) {
        int ref = LED_START_I_HB;                                       // Default ref - Setup
        if (startMode == 1 && i == LED_START_I_HB) {
            const uint32_t rgb = LED::getRandomColor();
            LED::State.StoredColor[ref] = CRGB((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
        }
        if (hbDual) {
            ref = (i < (LED_START_I_HB + hbHalf)) ? LED::UCOM(0) : LED::UCOM(1);
        }
        LED::State.TargetColor[i] = LED::State.StoredColor[ref];
    }

    // --- STATE RESET ---
    MOTION::State.Status = motOFF;                                             // Stop motion - State
    APP::Am.Status     = false;                                              // Exit ambient - State
    APP::updStatus("TV::On");                                                // Sync UI - Sync

    TASK.Phase = 0; TASK.ParamA = 0; TASK.ParamB = 0;                           // Reset task state - State
    HB::State.Phase   = 0; HB::State.ParamA   = 0;                                        // HB::State.Phase=0 = hold until phase-0 finishes - State
    TV::State.Transitioning = true;                                      // Cleared by T_EFFECT_TV_ON's finalization barrier - State

    TSK::KillTasksAvoidLocked("TV_On");                                 // Clean tasks - Action
    HB::EndTask();                                                       // Stop idle HB - Action

    DIF::AutoOn(EE_DIF_MODE_TV, haveDifColor ? &difColor : NULL);            // Diffuser on, matched to random color if active - Action
    TSK::AddTask("TV_On", "T_EFFECT_TV_ON", T_EFFECT_TV_ON, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)), 1, false); // TV+HB - Action
}
} // namespace TV


namespace MOTION {
/* ------------------------------------------------------------------------ */
/* MOTION                                                                     */
/* PIR detection - 5 on-effects - off task - color change task                */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Main motion sensor loop -- debounce, detect, and dispatch effects.
 *
 * Reads MOTION_PIN_COM and MOTION_PIN_BED (with TestMode override), applies a
 * MOTION_TIME_FIX ms debounce, then handles two cases:
 *   - First detection (motON): sets motCOM or motBED status, shuffles LED order,
 *     resets TASK state, and launches T_EFFECT_MOTION_ON.
 *   - Re-trigger while active: resets the auto-off countdown, optionally renews
 *     the random colour (EE_MOTION_RENEW_COLOR_TIME), and reschedules T_EFFECT_MOTION_OFF.
 * Also manages the auto-off timer (EE_MOTION_AUTO_OFF_TIME minutes).
 *
 * @note   Call every loop() iteration when LED::State.Enabled and !UDPRAW::State.Status are true.
 */
void Status() {
    if (MOTION::State.Status > motOFF) { 
        bool motionOn = false;                                          // Trigger flag - State

        // ~ Test Mode Simulation Override
        int simulatedCom = 0;                                           // Temp holder - Setup
        int simulatedBed = 0;                                           // Temp holder - Setup

        if (TestMode == _testmode_motionCom) {
            simulatedCom = 1;                                           // Force COM pin HIGH - Setup
        } else if (TestMode == _testmode_motionBed) {
            simulatedBed = 1;                                           // Force BED pin HIGH - Setup
        } else {
            simulatedCom = PinStatus(MOTION_PIN_COM);            // Real hardware - Hardware
            simulatedBed = PinStatus(MOTION_PIN_BED);            // Real hardware - Hardware
        }

        // 1. PIN DEBOUNCING LOGIC
        if ((simulatedCom + simulatedBed) && !MOTION::State.Trigger) {
            MOTION::State.Trigger = true;                                      // Start trigger debounce - State
            MOTION::State.TriggerTime = TimeNow;                               // Mark start time - State
            #ifdef ENABLE_LOG_MOTION
                PRNT::_print(PRNT::formatMSG("%32s : motion detected [COM:%d BED:%d], debouncing" NL, "MOTION_Status", simulatedCom, simulatedBed));
            #endif
        } else {
            if (MOTION::State.Trigger && (simulatedCom + simulatedBed)) { 
                if (((TimeNow - MOTION::State.TriggerTime) > MOTION_TIME_FIX)) { 
                    MOTION::State.Trigger = false;                             // Clear trigger - State
                    motionOn = true;                                    // Valid motion confirmed - State
                    #ifdef ENABLE_LOG_MOTION
                        PRNT::_print(PRNT::formatMSG("%32s : motion confirmed - debounce complete" NL, "MOTION_Status"));
                    #endif
                }
            } else {
                MOTION::State.Trigger = false;                                 // Reset signal - State
            }
        }

        // 2. MOTION HANDLER
        if (motionOn && ((MOTION::State.LastCheck + MOTION_CHECK_TIME) < TimeNow)) {
            // --- NEW: Pull references here to avoid repeated pointer chasing ---
            if (MOTION::State.Status > motON) { // --- CASE: ALREADY ON (KEEP ALIVE) ---
                if (!MOTION::State.AutoOffTime && simulatedBed) { 
                    MOTION::State.AutoOffTime = TimeNow + ((uint32_t)EE::Get(EE_MOTION_AUTO_OFF_TIME) * 60000);
                    #ifdef ENABLE_LOG_MOTION
                        PRNT::_print(PRNT::formatMSG("%32s : auto-off timer set [%d minutes]" NL, "MOTION_Status", EE::Get(EE_MOTION_AUTO_OFF_TIME)));
                    #endif
                } else if (MOTION::State.AutoOffTime && TimeNow >= MOTION::State.AutoOffTime) {
                    MOTION::State.Status = motAUTOOFF;                         // Set state - State
                    #ifdef ENABLE_LOG_MOTION
                        PRNT::_print(PRNT::formatMSG("%32s : auto-off timeout triggered" NL, "MOTION_Status"));
                    #endif
                    TSK::KillTasksAvoidLocked("MOTION_Status"); 
                    PRNT::_print(PRNT::formatMSG("%~32s # fade off, inc [%d], delay [%d]" NL, "MOTION_Off", LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC)), LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)))); // Log start, lux-adapted speed - Sync
                    TSK::AddTask("MOTION_Status", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false); 
                } else {
                    TSK::KillTasksAvoidLocked("MOTION_Status"); 
                    TSK::AddTask("MOTION_Status", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), S_TO_MS(EE::Get(EE_MOTION_ON_TIME)), false);

                    if (simulatedBed) {
                        if (EE::Get(EE_MOTION_RANDOM_COLOR) && ((MOTION::State.LastChangeColor + S_TO_MS(EE::Get(EE_MOTION_RENEW_COLOR_TIME))) < TimeNow)) {
                            uint16_t sumR = 0, sumG = 0, sumB = 0, litCount = 0; // Accumulate for diffuser match - Setup

                            for (int i=0; i < LED_NUM_TOTAL; i++) {
                                // Optimized check using the LED struct directly
                                if ((LED::State.CurrentColor[i].r + LED::State.CurrentColor[i].g + LED::State.CurrentColor[i].b) > 0) {
                                    LED::State.PackedRGBValue = LED::getRandomColor();       // Get color - Logic
                                    // Optimized assignment using LED struct directly
                                    LED::State.TargetColor[i].r = (LED::State.PackedRGBValue >> 16) & 255; 
                                    LED::State.TargetColor[i].g = (LED::State.PackedRGBValue >> 8) & 255;  
                                    LED::State.TargetColor[i].b = LED::State.PackedRGBValue & 255;         

                                    sumR += LED::State.TargetColor[i].r; sumG += LED::State.TargetColor[i].g; sumB += LED::State.TargetColor[i].b; // Track running sum - Logic
                                    litCount++;
                                }
                            }
                            #ifdef ENABLE_LOG_MOTION
                                PRNT::_print(PRNT::formatMSG("%32s : renewing random colors" NL, "MOTION_Status"));
                            #endif
                            TSK::AddTask("MOTION_Status", "T_MOTION_CHANGE_COLOR", T_MOTION_CHANGE_COLOR, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false); 
                            MOTION::State.LastChangeColor = TimeNow;           // Update timer - State

                            if (litCount > 0) {                                              // Push matching diffuser color - Action
                                const uint8_t avgR = sumR / litCount, avgG = sumG / litCount, avgB = sumB / litCount;
                                DIF_Colorx motionColor = { avgR, avgG, avgB, avgR, avgG, avgB }; // Mirrored if dual is on - Logic
                                DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);
                            }
                        }
                    } else {
                        MOTION::State.AutoOffTime = 0;                         // Reset if COM triggered - State
                    }

                    // Update History Log
                    MOTION::State.LOG[0].epoch = RTC_TimeClient.getEpochTime();
                }
            } else { // --- CASE: INITIAL DETECTION ---
                MOTION::State.Status = (simulatedCom ? motCOM : motBED);
                PRNT::_print(PRNT::formatMSG("%~32s # motion detected [%s]" NL, "MOTION_Status", (simulatedCom ? "COM" : "BED")));

                MOTION::State.LastChangeColor = TimeNow; 
                LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE); 

                TASK.Phase = 0; TASK.ParamA = 0; TASK.ParamB = 0; 
                TSK::KillTasksAvoidLocked("MOTION_Status"); 
                PRNT::_print(PRNT::formatMSG("%~32s # with effect [%d], inc [%d], delay [%d]" NL, "MOTION_On", EE::Get(EE_MOTION_ON_EFF), LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC)), LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)))); // Log start, lux-adapted speed - Sync
                TSK::AddTask("MOTION_Status", "T_EFFECT_MOTION_ON", T_EFFECT_MOTION_ON, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false); 

                if (EE::Get(EE_MOTION_RANDOM_COLOR)) {
                    const uint32_t rgb = LED::getRandomColor();              // Fresh random color - Action
                    const uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8), b = (uint8_t)rgb;
                    DIF_Colorx motionColor = { r, g, b, r, g, b };          // Mirrored if dual is on - Logic
                    DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);           // Diffuser on, random color match - Action
                } else {
                    DIF_Colorx motionColor = { MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b,
                                                MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b }; // Mirrored static color - Logic
                    DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);           // Diffuser on, static color match - Action
                }

                MOTION::State.AutoOffTime = 0; 
                // Shift motion log: move entries down to make room for new entry
                for (int i = MOTION_LOG_INDEX_MAX - 1; i > 0; i--) {
                    MOTION::State.LOG[i] = MOTION::State.LOG[i - 1];
                }
                MOTION::State.LOG[0].epoch = RTC_TimeClient.getEpochTime();
                MOTION::State.LOG[0].TriggerBy = MOTION::State.Status; 
            }

            APP::updStatus("MOTION::Status"); 
            MOTION::State.LastCheck = TimeNow; 
        }
    } else if (MOTION::State.Status == motAUTOOFF && (TestMode == _testmode_motionBed ? false : !PinStatus(MOTION_PIN_BED))) { 
        MOTION::State.AutoOffTime = 0; 
        MOTION::State.Status = motON; 
        APP::updStatus("MOTION::Status"); 
    }
}


/* -- Function pointer table for motion effect dispatch -- moved to DEF.h ---- */
/* (MotionEffectHandler, MOTION_ON_HANDLERS[], MOTION_ON_HANDLERS_COUNT) */

static void T_EFFECT_MOTION_ON_Default_Wrapper(taskId_t taskId) {
    if (!T_EFFECT_MOTION_ON_Default()) {
        TASK.Phase = taskDone;
        #ifdef ENABLE_LOG_ANIME_INFO
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_Default"));
        #endif
    }
}

/**
 * @brief  Motion-on master task -- fade out current state, set colours, run sub-effect.
 *
 * Step 0: fades all LEDs to black, then fills LED::State.TargetColor[] with either the static
 *         MOTION::State.Color or random colours based on EE_MOTION_RANDOM_COLOR.
 * Steps 1+: routes to the configured sub-effect (EE_SET[EE_MOTION_ON_EFF]):
 *   0/default = T_EFFECT_MOTION_ON_Default (symmetric center-out)
 *   1 = FromMiddle, 2 = LineMoving, 3 = Random, 4 = Cascade, 5 = TheCollision.
 * On completion: chains T_EFFECT_MOTION_OFF with EE_MOTION_ON_TIME delay.
 *
 * @param  taskId  Task handle supplied by the scheduler.
 *
 * @note   Do not call directly -- registered via AddTask in Status().
 *
 * Called by: MOTION::Status() (TSK::AddTask, registered unlocked, TASK_MS interval).
 */
void T_EFFECT_MOTION_ON(taskId_t taskId) {
    if (TASK.Phase == 0) {                                               // STEP 0: RESET & PREP - Logic
        #ifdef ENABLE_LOG_MOTION_VERBOSE
            PRNT::_print(PRNT::formatMSG("[ANIME] %24s : phase 0 - fading current state to black" NL, "T_EFFECT_MOTION_ON"));
        #endif
        bool fading = false;                                                // Track if fade-out is ongoing - State
        const int fadeInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                    // Step size for reset - Setup

        // --- PART A: FADE DOWN CURRENT STATE ---
        for (int i = 0; i < LED_NUM_TOTAL; i++) {                           // Iterate all pixels - Logic
            if (LED::TG_BRIGHTNESS(i, 0, fadeInc, false)) {                   // Move toward zero - Action
                // Maintain existing temp colors while forcing brightness to zero
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, 0, false); 
                fading = true;                                              // Mark as still active - State
            }
        }

        // --- PART B: PREPARE NEW COLORS IF FADE DONE ---
        if (!fading) {                                                      // Once screen is black - Logic
            const bool useRandom = EE::Get(EE_MOTION_RANDOM_COLOR);          // Check color mode - Setup
            
            for (int i = 0; i < LED_NUM_TOTAL; i++) {                       // Apply colors to temp buffer - Logic
                if (useRandom) {
                    const uint32_t rgb = LED::getRandomColor();                  // Generate random - Action
                    LED::State.TargetColor[i].r = (uint8_t)(rgb >> 16);              // Map R - Mapping
                    LED::State.TargetColor[i].g = (uint8_t)(rgb >> 8);               // Map G - Mapping
                    LED::State.TargetColor[i].b = (uint8_t)rgb;                      // Map B - Mapping
                } else {
                    LED::State.TargetColor[i].r = MOTION::State.Color.r;                  // Map static R - Mapping
                    LED::State.TargetColor[i].g = MOTION::State.Color.g;                  // Map static G - Mapping
                    LED::State.TargetColor[i].b = MOTION::State.Color.b;                  // Map static B - Mapping
                }
            }
            APP::updDeltaColors();                                     // Sync App UI state - Sync
            #ifdef ENABLE_LOG_MOTION_VERBOSE
                PRNT::_print(PRNT::formatMSG("[ANIME] %24s : phase 0 complete - colors prepared [random:%d]" NL, "T_EFFECT_MOTION_ON", useRandom));
            #endif
        }
        
        if (!fading) {                                 // Run preparation - Action
            TASK.Phase = 1; TASK.ParamA = 0; TASK.ParamB = 0;                    // Prep done, move to animation - State
        }
    } 
    else if (TASK.Phase != taskDone) {                                   // STEPS 1+: ANIMATION PHASE - Logic
        int effect = EE::Get(EE_MOTION_ON_EFF);                       // Get selected effect - Setup
        #ifdef ENABLE_LOG_MOTION_VERBOSE
            if (TASK.Phase == 1) {
                PRNT::_print(PRNT::formatMSG("[ANIME] %24s : phase 1 - running effect [%d]" NL, "T_EFFECT_MOTION_ON", effect));
            }
        #endif

        int motionEffIdx = effect;
        if (motionEffIdx < 0 || motionEffIdx >= MOTION_ON_HANDLERS_COUNT) motionEffIdx = 0;
        MOTION_ON_HANDLERS[motionEffIdx](taskId);
    }

    MOTION::State.LastCheck = TimeNow;                                         // Timestamp for motion - State
    LED::Show();                                                         // Update strip - Output

    if (TASK.Phase == taskDone) {                                        // COMPLETION - Logic
        #ifdef ENABLE_LOG_MOTION_VERBOSE
            PRNT::_print(PRNT::formatMSG("[ANIME] %24s : animation complete - scheduling fade off" NL, "T_EFFECT_MOTION_ON"));
        #endif
        APP::updColors_Force();                                     // Sync UI colors - Sync
        TASK.Phase = 0; TASK.ParamA = 0; TASK.ParamB = 0;                        // Reset local counters - State
        TSK::KillTasksAvoidLocked("T_EFFECT_MOTION_ON");                    // End this task - Action
        PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # fade off scheduled, inc [%d], delay [%d]" NL, "MOTION_Off", LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC)), LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)))); // Log, lux-adapted speed - Sync
        TSK::AddTask("T_EFFECT_MOTION_ON", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), S_TO_MS(EE::Get(EE_MOTION_ON_TIME)), false); // Schedule OFF - Action
    }
}

/**
 * @brief  Default motion-on sub-effect -- symmetric center-out brightness bloom.
 *
 * Expands brightness from the center of each active zone (BED, optionally COM, HB)
 * outward simultaneously. Supports divide-brightness mode (EE_MOTION_DIVIDE_BRIGHTNESS)
 * which reduces brightness by 15% per step from the center.
 *
 * @return true if any pixel is still fading toward its target (animation ongoing),
 *         false when all pixels have reached their target brightness.
 *
 * @note   Called by T_EFFECT_MOTION_ON when EE_MOTION_ON_EFF is 0 or unrecognised.
 */
bool T_EFFECT_MOTION_ON_Default() {
    bool active = false;                                                // Motion tracker - State
    
    const bool useDivide = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);                 // Check division flag - Setup
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));          // Target brightness - Logic
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Fade speed - Setup
    
    // Determine the clamp limit based on the motion trigger source
    const int divLim = (MOTION::State.Status == motBED) ? (LED_BED_NUM >> 1) : (LED_COM_NUM >> 1); // Clamp limit - Logic

    for (int z = 0; z < 3; z++) {                                       // Loop Bed, Com, HB zones - Setup
        if (z == 1 && MOTION::State.Status == motBED) continue;                // Skip Com if Bed-only mode - Logic

        int totalNum = (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM); // Zone count - Mapping
        int half = totalNum >> 1;                                       // Center point for mirror - Setup

        for (int i = 0; i < half; i++) {
            int L, R;                                                   // Side indices - Mapping
            
            // Mirror mapping: expands from the center outward
            if (z == 0)      { L = LED::BED(half - 1 - i); R = LED::BED(half + i); } // Bed Map - Mapping
            else if (z == 1) { L = LED::COM(half - 1 - i); R = LED::COM(half + i); } // Com Map - Mapping
            else             { L = LED::HB(half - 1 - i);  R = LED::HB(half + i);  } // HB Map - Mapping

            int br = baseBr;                                            // Default target brightness - Logic
            if (useDivide) {
                int step = (i > divLim) ? divLim : i;                   // Cap step distance - Logic
                br = baseBr - ((baseBr * (step * 15)) / 100);           // Linear reduction math - Math
                if (br < 0) br = 0;                                     // Safety floor - Logic
            }

            // Move Left side toward target brightness
            if (LED::TG_BRIGHTNESS(L, br, brInc, false)) {               // Fade transition - Action
                LED::setPixel(L, LED::State.TargetColor[L].r, LED::State.TargetColor[L].g, LED::State.TargetColor[L].b, LED::State.CurrentBrightness[L], false); 
                active = true;                                          // Flag ongoing change - State
            }
            
            // Move Right side toward target brightness
            if (LED::TG_BRIGHTNESS(R, br, brInc, false)) {               // Fade transition - Action
                LED::setPixel(R, LED::State.TargetColor[R].r, LED::State.TargetColor[R].g, LED::State.TargetColor[R].b, LED::State.CurrentBrightness[R], false); 
                active = true;                                          // Flag ongoing change - State
            }
        }
    }
    
    return active;                                                      // Return true if any pixel is still fading - State
}

/**
 * @brief  Motion-on effect 1 -- center-to-edge expansion with HB proportional sync.
 *
 * Expands from the center of BED outward, step by step. COM is also expanded
 * if MOTION::State.Status != motBED. HB pixels are mapped proportionally to the master
 * zone. Supports divide-brightness (15% reduction per step from center).
 *
 * @param  tID  Task handle supplied by the scheduler.
 *
 * Called by: T_EFFECT_MOTION_ON() (MOTION_ON_HANDLERS[1]).
 */
void T_EFFECT_MOTION_ON_1_FromMiddle(taskId_t tID) { // From Middle
    bool N = false;                                                      // Track if any LED changed - State
    bool Done = true;                                                    // Assume animation is finished - State
    const int step = TASK.ParamA;                                            // Cache current animation step - Setup

    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool effect1Started = false;
        if (!effect1Started) {
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s : FromMiddle effect started" NL, "MOTION_ON_1"));
            effect1Started = true;
        }
    #endif
    
    // 1. Pre-calculate values
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));           // Get base brightness once - Logic
    const int brInc  = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Cache increment setting - Logic
    
    // Calculate 15% reduction per step: baseBr - (15% * step)
    int br = baseBr;                                                     // Default to full - Logic
    if (EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS)) {
        int reduction = (baseBr * (step * 15)) / 100;                    // 15% reduction per step - Logic
        br = baseBr - reduction;                                         // Apply reduction - Logic
        if (br < 0) br = 0;                                              // Clamp to zero - Logic
    }

    // --- BED ZONE (Standard Symmetrical) ---
    const int halfBED = LED_BED_NUM >> 1;                                // Cache BED center - Mapping
    if (step < halfBED) {
        int idxL = LED::BED(halfBED - 1 - step);                         // Left index - Mapping
        int idxR = LED::BED(halfBED + step);                             // Right index - Mapping
        int pixels[2] = {idxL, idxR};                                   // Indices to check - Logic
        for (int i = 0; i < 2; i++) {
            if (LED::TG_BRIGHTNESS(pixels[i], br, brInc, false)) {
                LED::setPixel(pixels[i], LED::State.TargetColor[pixels[i]].r, LED::State.TargetColor[pixels[i]].g, LED::State.TargetColor[pixels[i]].b, LED::State.CurrentBrightness[pixels[i]], false); 
                N = true;                                                // Set changed flag - State
            }
        }
        Done = false;                                                    // BED still animating - State
    }

    // --- HB & COM LOGIC ---
    const int halfHB = LED_HB_NUM >> 1;                                  // Cache HB center - Mapping
    const int halfCOM = LED_COM_NUM >> 1;                                // Cache COM center - Mapping
    const bool isBedOnly = (MOTION::State.Status == motBED);                    // Check mode - Setup

    // 2. Determine Scaling Ratio and Limits
    float hbRatio = isBedOnly ? ((float)LED_HB_NUM / (float)LED_BED_NUM) 
                              : ((float)LED_HB_NUM / (float)LED_COM_NUM); // Scaling ratio - Setup
    int limit = isBedOnly ? halfBED : halfCOM;                           // Step limit for HB - Setup

    // A. Process COM Zone (Only if NOT Bed Only)
    if (!isBedOnly && step < halfCOM) {
        int cIdxL = LED::COM(halfCOM - 1 - step);                        // COM Left - Mapping
        int cIdxR = LED::COM(halfCOM + step);                            // COM Right - Mapping
        int cPixels[2] = {cIdxL, cIdxR};
        for (int i = 0; i < 2; i++) {
            if (LED::TG_BRIGHTNESS(cPixels[i], br, brInc, false)) {
                LED::setPixel(cPixels[i], LED::State.TargetColor[cPixels[i]].r, LED::State.TargetColor[cPixels[i]].g, LED::State.TargetColor[cPixels[i]].b, LED::State.CurrentBrightness[cPixels[i]], false); 
                N = true;
            }
        }
        Done = false;                                                    // COM still animating - State
    }

    // B. Process HB Zone (Always active, scaled to primary motion zone)
    if (step < limit) {
        int hbStart = (int)(step * hbRatio);                             // HB Start offset - Logic
        int hbEnd   = (int)((step + 1) * hbRatio);                       // HB End offset - Logic

        for (int h = hbStart; h < hbEnd && h < halfHB; h++) {
            int hIdxL = LED::HB(halfHB - 1 - h);                          // HB Left - Mapping
            int hIdxR = LED::HB(halfHB + h);                              // HB Right - Mapping
            int hPixels[2] = {hIdxL, hIdxR};
            for (int i = 0; i < 2; i++) {
                if (LED::TG_BRIGHTNESS(hPixels[i], br, brInc, false)) {
                    LED::setPixel(hPixels[i], LED::State.TargetColor[hPixels[i]].r, LED::State.TargetColor[hPixels[i]].g, LED::State.TargetColor[hPixels[i]].b, LED::State.CurrentBrightness[hPixels[i]], false); 
                    N = true;
                }
            }
        }
        Done = false;                                                    // HB still animating - State
    }
    
    // 3. State Management
    if (!N) {
        if (Done) {
            TASK.Phase = taskDone;                                        // Animation finished - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_1_FromMiddle"));
            #endif
        } else {
            APP::updDeltaColors();                                  // Sync App Color - Sync
            TASK.ParamA++;                                                   // Advance step - State
        }
    }
}

/**
 * @brief  Motion-on effect 2 -- left-to-right scan then full brightness bloom.
 *
 * Pass 1 (step 2): scans L->R at either 15% brightness or full (depending on
 * divide-brightness mode). Pass 2 (step 3): scans R->L at full brightness.
 * Step 4: final bloom to 100% with smooth transition. HB pixels track the master zone.
 *
 * @param  tID  Task handle supplied by the scheduler.
 *
 * Called by: T_EFFECT_MOTION_ON() (MOTION_ON_HANDLERS[2]).
 */
void T_EFFECT_MOTION_ON_2_LineMoving(taskId_t tID) { // Line Moving
    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool effect2Started = false;
        if (!effect2Started) {
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s : LineMoving effect started" NL, "MOTION_ON_2"));
            effect2Started = true;
        }
    #endif
    const int phase = TASK.Phase;                                         // Cache phase - Setup
    const int ta = TASK.ParamA;                                             // Cache animation index - Setup
    
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));          // Get target brightness - Logic
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Cache increment - Logic
    const bool isBedOnly = (MOTION::State.Status == motBED);                   // Check mode - Setup
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);                    // Division setting - Setup

    if (phase == 1) { // --- STEP 1: INITIALIZE ---
        TASK.ParamA = 0;                                                    // Start from beginning - Setup
        TASK.Phase++;                                                    // Move to next step - State
    } 
    else if (phase == 2 || phase == 3) { // --- STEP 2: L>R (15%) | STEP 3: R>L (FULL) ---
        int masterNum = isBedOnly ? LED_BED_NUM : (LED_BED_NUM + LED_COM_NUM); // Scan length master (BED+COM if not bed-only) - Logic
        float hbRatio = (float)LED_HB_NUM / (float)masterNum;           // Scale HB to master - Setup

        if (ta < masterNum) {
            int scanIdx = (phase == 2) ? ta : (masterNum - 1 - ta);      // Directional index - Mapping
            // Zone Selection: if bed-only use BED; if not bed-only map to BED+COM combined
            int currentPixel;
            if (isBedOnly) {
                currentPixel = LED::BED(scanIdx);                        // Use BED zone - Mapping
            } else {
                // Map combined BED+COM range: first part is BED, second part is COM
                if (scanIdx < LED_BED_NUM) {
                    currentPixel = LED::BED(scanIdx);                    // BED range - Mapping
                } else {
                    currentPixel = LED::COM(scanIdx - LED_BED_NUM);      // COM range - Mapping
                }
            }

            // 1. Determine Brightness for this scan
            int target;
            if (useDiv) {
                // If Divide is ON: Apply 15% reduction logic immediately
                int dist = abs(scanIdx - (masterNum >> 1));             // Distance from center - Logic
                int reduction = (baseBr * (dist * 15)) / 100;           // 15% reduction - Logic
                target = baseBr - reduction;                            // Final target - Logic
                if (target < 0) target = 0;                             // Clamp - Logic
            } else {
                // If Divide is OFF: Step 2 is 15% Br, Step 3 is Full-Br
                target = (phase == 2) ? (baseBr * 15 / 100) : baseBr;    // Use 15% for first pass - Logic
            }

            // 2. Update Master Strip state
            LED::State.CurrentBrightness[currentPixel] = target;                    // Apply calculated target - Logic
            
            // 3. Update HB Strip state
            int hbStart = (int)(scanIdx * hbRatio);                     // HB block start - Logic
            int hbEnd   = (int)((scanIdx + 1) * hbRatio) + 3;           // HB block end + 3 pixels - Logic
            for (int h = hbStart; h < hbEnd && h < LED_HB_NUM; h++) {
                int p = LED::HB(h);                                      // HB pixel - Mapping
                LED::State.CurrentBrightness[p] = target;                            // Follow master - Logic
            }

            // Refresh buffer for all pixels to reflect state changes
            for (int i = 0; i < LED_NUM_TOTAL; i++) {                   // Refresh buffer - Setup
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, LED::State.CurrentBrightness[i], false); 
            }
            LED::Show();                                                 // Output - Output
            TASK.ParamA++;                                                  // Advance - State
        } else {
            // Logic Gate: If using Divide, we are done after Step 2. Skip Step 3.
            if (useDiv && phase == 2) {
                TASK.Phase = taskDone;                                   // Finish early - State
                TASK.ParamA = 0;                                            // Reset - Setup
                #ifdef ENABLE_LOG_ANIME_INFO
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done (early, divide mode)" NL, "MOTION_ON_2_LineMoving"));
                #endif
            } else {
                TASK.Phase++;                                            // Next phase (Step 3 or 4) - State
                TASK.ParamA = 0;                                            // Reset - Setup
            }
        }
    }
    else if (phase == 4) { // --- STEP 4: FINAL BLOOM ---
        bool N = false;                                                 // Activity flag - State
        for (int z = 0; z < 3; z++) {
            if (z == 1 && isBedOnly) continue;
            int num = (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM); 
            for (int i = 0; i < num; i++) {
                int p = (z == 0) ? LED::BED(i) : (z == 1 ? LED::COM(i) : LED::HB(i)); 
                if (LED::TG_BRIGHTNESS(p, baseBr, brInc, false)) {       // Fade to 100% - Logic
                    LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false); 
                    N = true;                                           // Still changing - State
                }
            }
        }
        if (!N) {
            TASK.Phase = taskDone;                                       // Finish - State
            APP::updDeltaColors();                                   // Update color after LED br is off - Sync
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_2_LineMoving"));
            #endif
        } else {
            LED::Show();                                                 // Refresh strip - Output
            APP::updDeltaColors();                                   // Sync App UI - Sync
        }
    }
    else {
        TASK.Phase = taskDone;                                           // Safety - State
    }
}

/**
 * @brief  Motion-on effect 3 -- random-order LED-by-LED fade-in from LED::State.PixelOrder[].
 *
 * Iterates through LED::State.PixelOrder (pre-shuffled), skipping LEDs outside BED/COM zones.
 * For each valid LED, fades it and its proportional HB block to the target brightness.
 * Supports divide-brightness (15% reduction based on distance from zone center).
 *
 * @param  tID  Task handle used to set the per-step interval.
 *
 * Called by: T_EFFECT_MOTION_ON() (MOTION_ON_HANDLERS[3]).
 */
void T_EFFECT_MOTION_ON_3_Random(taskId_t tID) { // Random
    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool effect3Started = false;
        if (!effect3Started) {
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s : Random effect started" NL, "MOTION_ON_3"));
            effect3Started = true;
        }
    #endif
    
    if (TASK.Phase == 1) { // --- STEP 1: FIND NEXT VALID RANDOM LED ---
        if (TASK.ParamA < LED_NUM - LED_HB_NUM_FAKE) {                           // PixelOrder bound - Setup
            int led = LED::State.PixelOrder[TASK.ParamA];                                // Get current random LED from order - Setup
            bool Find = false;                                           // Reset search flag - State
            int activeIdx = -1;                                          // Index for HB scaling - Logic

            // 1. Check Bed Zone
            for (int i = 0; i < LED_BED_NUM; i++) {
                if (led == LED::BED(i)) {
                    Find = true; activeIdx = i; break;                   // Found in BED - State
                }
            }

            // 2. Check COM Zone (only if NOT Bed-Only mode)
            if (!Find && MOTION::State.Status != motBED) {
                for (int i = 0; i < LED_COM_NUM; i++) {
                    if (led == LED::COM(i)) {
                        Find = true; 
                        activeIdx = LED_BED_NUM + i;                     // Offset by BED count - Logic
                        break;
                    }
                }
            }

            if (Find) {
                TASK.ParamB = activeIdx;                                     // Store position for Step 2 - State
                TSK::setTaskInterval("T_EFFECT_MOTION_ON_3_Random", tID, TASK_MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)));  // Wait before next random drop - Logic
                TASK.Phase = 2;                                           // Proceed to fade - State
            } else {
                TSK::setTaskInterval("T_EFFECT_MOTION_ON_3_Random", tID, TASK_MS, 1);                            // Skip non-functional LEDs immediately - Logic
                TASK.ParamA++;                                               // Advance to next index in Order - State
            }
        } else {
            TASK.Phase = taskDone;                                        // All LEDs processed - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_3_Random"));
            #endif
        }
        return;
    }
    else if (TASK.Phase == 2) { // --- STEP 2: CALCULATE TARGET & FADE ---
        int mainLed = LED::State.PixelOrder[TASK.ParamA];                                // Retrieve current target LED - Setup
        int activeIdx = TASK.ParamB;                                         // Retrieve logic position - Setup
        
        const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));       // Base target brightness - Logic
        const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                   // Fade speed - Logic
        
        // --- Calculate 15% Reduction based on Distance ---
        int targetBr = baseBr;                                           // Default to full brightness - Logic
        if (EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS)) {
            int dist = 0;
            // Determine distance from center based on the active zone
            if (activeIdx < LED_BED_NUM) {
                dist = abs(activeIdx - (LED_BED_NUM >> 1));              // Distance from BED center - Logic
            } else {
                int comLocalIdx = activeIdx - LED_BED_NUM;               // Local offset for COM - Logic
                dist = abs(comLocalIdx - (LED_COM_NUM >> 1));            // Distance from COM center - Logic
            }
            int reduction = (baseBr * (dist * 15)) / 100;                // 15% reduction math - Logic
            targetBr = baseBr - reduction;                               // Set final target - Logic
            if (targetBr < 0) targetBr = 0;                              // Safety floor - Logic
        }

        bool stillFading = false;                                        // Tracking flag for this LED pair - State

        // A. Fade the Main LED (Bed or Com)
        if (LED::TG_BRIGHTNESS(mainLed, targetBr, brInc, false)) {
            LED::setPixel(mainLed, LED::State.TargetColor[mainLed].r, LED::State.TargetColor[mainLed].g, LED::State.TargetColor[mainLed].b, LED::State.CurrentBrightness[mainLed], false);
            stillFading = true;                                          // Still transitioning - State
        }

        // B. Fade the corresponding Headboard (HB) block
        int totalActiveMain = (MOTION::State.Status == motBED) ? LED_BED_NUM : (LED_BED_NUM + LED_COM_NUM); 
        float ratio = (float)LED_HB_NUM / (float)totalActiveMain;        // Scale HB to main zone - Logic
        int hbStart = (int)(activeIdx * ratio);                          // HB block start - Logic
        int hbEnd   = (int)((activeIdx + 1) * ratio);                    // HB block end - Logic

        for (int h = hbStart; h < hbEnd && h < LED_HB_NUM; h++) {
            int hbLed = LED::HB(h);                                       // Map HB hardware index - Mapping
            if (LED::TG_BRIGHTNESS(hbLed, targetBr, brInc, false)) {
                LED::setPixel(hbLed, LED::State.TargetColor[hbLed].r, LED::State.TargetColor[hbLed].g, LED::State.TargetColor[hbLed].b, LED::State.CurrentBrightness[hbLed], false);
                stillFading = true;                                      // HB still transitioning - State
            }
        }

        if (!stillFading) {
            TSK::setTaskInterval("T_EFFECT_MOTION_ON_3_Random", tID, TASK_MS, 1);                                // Current pair reached target - Logic
            TASK.ParamA++;                                                   // Prepare for next random index - State
            TASK.Phase = 1;                                               // Return to search phase - State
            APP::updDeltaColors();                                  // Sync App UI state - Sync
        }
        return;
    }
}

/**
 * @brief  Motion-on effect 4 -- HB fast inward scan followed by floor bloom outward.
 *
 * Step 1: init. Step 2: HB pixels expand outward from center at double speed
 * for a snappy start. Step 3: BED (and optionally COM) bloom outward from center
 * with optional divide-brightness gradient. Completes when all zones reach target.
 *
 * @param  tID  Task handle supplied by the scheduler.
 *
 * Called by: T_EFFECT_MOTION_ON() (MOTION_ON_HANDLERS[4]).
 */
void T_EFFECT_MOTION_ON_4_Cascade(taskId_t tID) { // Cascade
    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool effect4Started = false;
        if (!effect4Started) {
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s : Cascade effect started" NL, "MOTION_ON_4"));
            effect4Started = true;
        }
    #endif
    const int phase = TASK.Phase;                                         // Cache current phase - Setup
    const int ta = TASK.ParamA;                                             // Cache animation index - Setup
    
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));          // Target brightness - Logic
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Increment speed - Logic
    const bool isBedOnly = (MOTION::State.Status == motBED);                   // Check mode - Setup
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);                    // Gradient toggle - Setup

    if (phase == 1) { // --- STEP 1: INITIALIZE ---
        TASK.ParamA = 0;                                                    // Reset index - Setup
        TASK.Phase++;                                                    // Move to HB phase - State
    } 
    else if (phase == 2) { // --- STEP 2: FAST HEADBOARD SCAN ---
        const int halfHB = LED_HB_NUM >> 1;                             // HB Center - Mapping
        
        // Use ta * 2 to process HB pixels twice as fast for a snappy start
        int headIdx = ta * 2;                                           // Fast index - Logic
        bool stillMapping = false;                                      // Tracking - State

        for (int i = 0; i <= headIdx && i < halfHB; i++) {
            int L = LED::HB(halfHB - 1 - i);                             // Left side - Mapping
            int R = LED::HB(halfHB + i);                                 // Right side - Mapping
            int pixels[2] = {L, R};

            for (int s = 0; s < 2; s++) {
                if (LED::TG_BRIGHTNESS(pixels[s], baseBr, brInc, false)) { // Fade HB to full - Logic
                    LED::setPixel(pixels[s], LED::State.TargetColor[pixels[s]].r, LED::State.TargetColor[pixels[s]].g, LED::State.TargetColor[pixels[s]].b, LED::State.CurrentBrightness[pixels[s]], false); 
                    stillMapping = true;                                // Still fading - State
                }
            }
        }

        if (!stillMapping && headIdx >= halfHB) {                       // If HB is finished - State
            TASK.Phase++;                                                // Move to Bed/Com phase - State
            TASK.ParamA = 0;                                                // Reset for next zone - Setup

            APP::updDeltaColors();                                  // Sync App UI - Sync
        } else {
            TASK.ParamA++;                                                  // Advance fast scan - State
            LED::Show();                                                 // Update strips - Output
        }
    }
    else if (phase == 3) { // --- STEP 3: FLOOR BLOOM (WITH 15% DIVIDE) ---
        bool N = false;                                                 // Activity flag - State
        int floorLimit = isBedOnly ? (LED_BED_NUM >> 1) : (LED_COM_NUM >> 1); // Max distance - Logic

        for (int z = 0; z < 2; z++) {                                   // Process BED and COM only - Setup
            if (z == 1 && isBedOnly) continue;                           // Skip COM if needed - Logic

            int num = (z == 0) ? LED_BED_NUM : LED_COM_NUM;             // Zone length - Mapping
            int mid = num >> 1;                                         // Center - Mapping

            // Bloom out to the current 'ta' distance
            for (int i = 0; i <= ta && i < mid; i++) {
                int L = (z == 0) ? LED::BED(mid - 1 - i) : LED::COM(mid - 1 - i); 
                int R = (z == 0) ? LED::BED(mid + i)     : LED::COM(mid + i);
                
                int target = baseBr;                                    // Default - Logic
                if (useDiv) {
                    int reduction = (baseBr * (i * 15)) / 100;           // 15% per pixel distance - Logic
                    target = baseBr - reduction;                        // Apply drop - Logic
                    if (target < 0) target = 0;                         // Clamp - Logic
                }

                int pix[2] = {L, R};
                for (int s = 0; s < 2; s++) {
                    if (LED::TG_BRIGHTNESS(pix[s], target, brInc, false)) { // Transition to target - Logic
                        LED::setPixel(pix[s], LED::State.TargetColor[pix[s]].r, LED::State.TargetColor[pix[s]].g, LED::State.TargetColor[pix[s]].b, LED::State.CurrentBrightness[pix[s]], false); 
                        N = true;                                       // Active change - State
                    }
                }
            }
        }

        if (!N && ta >= floorLimit) {                                   // If floor is fully lit - State
            TASK.Phase = taskDone;                                       // Animation finished - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_4_Cascade"));
            #endif
        } else {
            if (ta < floorLimit) TASK.ParamA++;                             // Expand bloom - State
            LED::Show();                                                 // Update strips - Output
            APP::updDeltaColors();                                 // Sync UI - Sync
        }
    }
    else {
        TASK.Phase = taskDone;                                           // Safety exit - State
    }
}

/**
 * @brief  Motion-on effect 5 -- edges zip to center then bloom to final brightness.
 *
 * Step 1: init. Step 2 (ZIP): pixels approach the center from both edges simultaneously
 * across BED, COM, and HB, with optional gradient. Step 3 (BLOOM): all pixels
 * transition smoothly to their final target brightness.
 *
 * @param  tID  Task handle supplied by the scheduler.
 *
 * Called by: T_EFFECT_MOTION_ON() (MOTION_ON_HANDLERS[5]).
 */
void T_EFFECT_MOTION_ON_5_TheCollision(taskId_t tID) {
    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool effect5Started = false;
        if (!effect5Started) {
            PRNT::_print(PRNT::formatMSG("[ANIME] %~24s : TheCollision effect started" NL, "MOTION_ON_5"));
            effect5Started = true;
        }
    #endif
    const int phase = TASK.Phase;                                         // Current phase - Setup
    const int ta = TASK.ParamA;                                             // Animation index - Setup
    
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));          // Target brightness - Logic
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Speed - Logic
    const bool isBedOnly = (MOTION::State.Status == motBED);                   // Mode - Setup
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);                    // Toggle - Setup

    // --- DYNAMIC RATIO CALCULATION ---
    // Ratio used to normalize the gradient slope across different strip lengths
    const int hbScale = (LED_COM_NUM > 0) ? (int)round((float)LED_HB_NUM / LED_COM_NUM) : 1; // Ratio - Logic

    if (phase == 1) { 
        TASK.ParamA = 0; TASK.Phase++;                                       // Init - State
    } 
    else if (phase == 2) { // --- STEP 2: ZIP (Closing from Edges to Center) ---
        bool stillMoving = false;                                       // Tracking - State

        for (int z = 0; z < 3; z++) { 
            if (z == 1 && isBedOnly) continue;                          // Skip COM - Logic

            int num = (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM); // Count - Mapping
            int half = num >> 1;                                        // Center point - Mapping

            if (ta < half) {
                int L = (z == 0) ? LED::BED(ta) : (z == 1 ? LED::COM(ta) : LED::HB(ta)); // Left side - Mapping
                int R = (z == 0) ? LED::BED(num - 1 - ta) : (z == 1 ? LED::COM(num - 1 - ta) : LED::HB(num - 1 - ta)); // Right side - Mapping
                
                // --- TARGET CALCULATION ---
                int dist = half - 1 - ta;                               // Distance from center - Logic
                int target = baseBr;                                    // Default to full - Logic

                if (useDiv) {
                    // If Zone is HB (z=2), we divide distance by hbScale to match the slope of the other zones
                    int effectiveDist = (z == 2) ? (dist / hbScale) : dist; // Normalized distance - Logic
                    int reduction = (baseBr * (effectiveDist * 15)) / 100;  // 15% reduction per effective step - Logic
                    target = baseBr - reduction;                        // Set final brightness - Logic
                    if (target < 0) target = 0;                         // Safety clamp - Logic
                }

                LED::setPixel(L, LED::State.TargetColor[L].r, LED::State.TargetColor[L].g, LED::State.TargetColor[L].b, target, false); // Action
                LED::setPixel(R, LED::State.TargetColor[R].r, LED::State.TargetColor[R].g, LED::State.TargetColor[R].b, target, false); // Action
                stillMoving = true;                                     // Animation still active - State
            }
        }

        if (!stillMoving) {
            TASK.Phase++; TASK.ParamA = 0; TASK.ParamB = 0;                  // Move to Bloom - State (ParamB: bloom-tick counter, see phase 3)
        } else {
            TASK.ParamA++; LED::Show();                                      // Step inward and update - State
            APP::updDeltaColors();                                 // Sync UI - Sync
        }
    }
    else if (phase == 3) { // --- STEP 3: BLOOM (Stabilizing Brightness) ---
        bool N = false;                                                 // Change detection - State
        for (int z = 0; z < 3; z++) {
            if (z == 1 && isBedOnly) continue;
            int num = (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM);
            int mid = num >> 1;                                         // Center for distance calculation - Mapping

            for (int i = 0; i < num; i++) {
                int p = (z == 0) ? LED::BED(i) : (z == 1 ? LED::COM(i) : LED::HB(i));
                int dist = abs(i - mid);                                // Distance from center - Logic
                
                int target = baseBr;                                    // Default - Logic
                if (useDiv) {
                    // Apply normalized scaling to ensure the gradient looks uniform across all strips
                    int effectiveDist = (z == 2) ? (dist / hbScale) : dist; // Normalized distance - Logic
                    int reduction = (baseBr * (effectiveDist * 15)) / 100;  // 15% reduction - Logic
                    target = baseBr - reduction;                        // Set target - Logic
                    if (target < 0) target = 0;                         // Clamp - Logic
                }

                if (LED::TG_BRIGHTNESS(p, target, brInc, false)) {       // Transition pixel state - Logic
                    LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false); 
                    N = true;                                           // Still changing - State
                }
            }
        }

        if (!N) {
            TASK.Phase = taskDone;                                       // Collision complete - State
            #ifdef ENABLE_LOG_ANIME_INFO
                PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done" NL, "MOTION_ON_5_TheCollision"));
            #endif
        } else {
            TASK.ParamB++;                                               // Bloom-tick counter - State
            if (TASK.ParamB > MOTION_COLLISION_BLOOM_MAX_TICKS) {
                // Safety cap: at least one pixel (confirmed live: a single HB
                // pixel, reproducibly) can fail to converge here and leave N
                // true forever. That stalls this task in an endless per-tick
                // colour-sync resend -- confirmed live to both starve the
                // command socket (multi-second command delays) and leak
                // ~40-50B/s of RAM (own-buffer never freed on the repeated
                // send), eventually crashing the board. Whatever pixel hasn't
                // converged by now gets snapped straight to its target instead
                // of stepped, so this phase always terminates.
                for (int z = 0; z < 3; z++) {
                    if (z == 1 && isBedOnly) continue;
                    int num = (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM);
                    int mid = num >> 1;
                    for (int i = 0; i < num; i++) {
                        int p = (z == 0) ? LED::BED(i) : (z == 1 ? LED::COM(i) : LED::HB(i));
                        int dist = abs(i - mid);
                        int target = baseBr;
                        if (useDiv) {
                            int effectiveDist = (z == 2) ? (dist / hbScale) : dist;
                            int reduction = (baseBr * (effectiveDist * 15)) / 100;
                            target = baseBr - reduction;
                            if (target < 0) target = 0;
                        }
                        LED::State.CurrentBrightness[p] = target;
                        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, target, false);
                    }
                }
                LED::Show();
                APP::updDeltaColors();
                TASK.Phase = taskDone;
                #ifdef ENABLE_LOG_ANIME_INFO
                    PRNT::_print(PRNT::formatMSG("[ANIME] %~24s # done (safety cap - see comment above)" NL, "MOTION_ON_5_TheCollision"));
                #endif
            } else {
                LED::Show();                                                 // Output changes - Output
                APP::updDeltaColors();                                 // Update UI - Sync
            }
        }
    }
}

/**
 * @brief  Motion-off task -- sequentially fade motion zones to black.
 *
 * This task operates as a state machine (via TASK.Phase) to dim zones in a 
 * specific order: Heartbeat (HB) -> Common (COM) -> Bed (BED). 
 * * Each zone must reach zero brightness before the next zone begins its transition.
 * LAMP zones are excluded from this sequence.
 *
 * Sets TASK.Phase = taskDone once the final zone (BED) is off, restores 
 * MOTION::State.Status to motON (unless motAUTOOFF), and synchronizes the 
 * application state via APP::updStatus() and APP::updDeltaColors().
 *
 * @param  taskId  Task handle supplied by the scheduler.
 *
 * @note   Scheduled automatically when motion is no longer detected or 
 * the auto-off timer expires.
 */
void T_EFFECT_MOTION_OFF(taskId_t taskId) {
    if (TASK.Phase != taskDone) {
        // We use TASK.Phase as our sequencer: 0 = HB, 1 = COM, 2 = BED, 3 = Finalizing
        const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                   // Cache increment - Setup
        bool zoneStillActive = false;                                    // Track if current zone is dimming - Logic

        int startIdx = 0;                                                // Loop start - Setup
        int endIdx = 0;                                                  // Loop end - Setup

        // 1. Select the current zone based on the step
        if (TASK.Phase == 0) {                                            // Step 0: Heartbeat - Logic
            startIdx = 0;                                                // HB Start - Mapping
            endIdx = LED_HB_NUM;                                         // HB End - Mapping
            #ifdef ENABLE_LOG_MOTION_VERBOSE
                static bool hbStartLogged = false;
                if (!hbStartLogged) {
                    PRNT::_print(PRNT::formatMSG("%32s : fading HB zone to black" NL, "T_EFFECT_MOTION_OFF"));
                    hbStartLogged = true;
                }
            #endif
        } else if (TASK.Phase == 1) {                                     // Step 1: Common - Logic
            startIdx = 0;                                                // COM Start - Mapping
            endIdx = LED_COM_NUM;                                        // COM End - Mapping
            #ifdef ENABLE_LOG_MOTION_VERBOSE
                static bool comStartLogged = false;
                if (!comStartLogged) {
                    PRNT::_print(PRNT::formatMSG("%32s : HB complete - fading COM zone" NL, "T_EFFECT_MOTION_OFF"));
                    comStartLogged = true;
                }
            #endif
        } else if (TASK.Phase == 2) {                                     // Step 2: Bed - Logic
            startIdx = 0;                                                // BED Start - Mapping
            endIdx = LED_BED_NUM;                                        // BED End - Mapping
            #ifdef ENABLE_LOG_MOTION_VERBOSE
                static bool bedStartLogged = false;
                if (!bedStartLogged) {
                    PRNT::_print(PRNT::formatMSG("%32s : COM complete - fading BED zone" NL, "T_EFFECT_MOTION_OFF"));
                    bedStartLogged = true;
                }
            #endif
        }

        // 2. Process only the active zone
        for (int i = startIdx; i < endIdx; i++) {                        // Loop through selected zone - Action
            int targetLed;                                               // Variable for mapped LED - Logic
            
            if (TASK.Phase == 0)      targetLed = LED::HB(i);              // Map HB - Mapping
            else if (TASK.Phase == 1) targetLed = LED::COM(i);             // Map COM - Mapping
            else                     targetLed = LED::BED(i);             // Map BED - Mapping

            if (LED::TG_BRIGHTNESS(targetLed, 0, brInc, false)) {         // Try to dim - Action
                LED::setPixel(targetLed, LED::State.CurrentColor[targetLed].r, LED::State.CurrentColor[targetLed].g, LED::State.CurrentColor[targetLed].b, LED::State.CurrentBrightness[targetLed], false); // Update LED - Update
                zoneStillActive = true;                                  // Zone is not finished - State
            }
        }

        // 2b. Sync partial zone changes to app after each frame (not just when zone completes)
        if (TASK.Phase > 0) APP::updDeltaColors();                                 // Sync changed LEDs to app - Sync

        // 3. Sequence Logic: Move to next zone if current one is done
        if (!zoneStillActive) {                                          // If current zone is fully black - Logic
            // Special: Ensure app receives final confirmation when HB zone finishes
            if (TASK.Phase == 0) {                                        // HB zone just completed - Logic
                APP::updDeltaColors();                                // Send final HB-complete sync - Sync
            }
            
            TASK.Phase++;                                                 // Increment to next zone - State
            if (TASK.Phase > 2) {                                         // If Bed (last zone) is finished - Logic
                TASK.Phase = taskDone;                                    // Mark task as complete - State
            }
        }
    }

    if (MOTION::State.Status != motAUTOOFF) {                                   // Standard motion check - Logic
        MOTION::State.Status = motON;                                           // Reset status - State
        MOTION::State.AutoOffTime = 0;                                          // Reset timer - State
    }
    
    LED::Show();                                                          // Update hardware - Output

    if (TASK.Phase == taskDone) {                                         // Final Cleanup - Logic
        #ifdef ENABLE_LOG_MOTION_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : fade complete - all zones off" NL, "T_EFFECT_MOTION_OFF"));
        #endif
        APP::updStatus("LED::T_EFFECT_MOTION_OFF");                                             // Sync - Sync
        APP::updDeltaColors();                                        // Sync - Sync
        DIF::AutoOff();                                                   // Diffuser off if all sources idle - Action
        TSK::KillTasksAvoidLocked("T_EFFECT_MOTION_OFF");                // Kill task - State
    }
}

/**
 * @brief  Task that smoothly transitions all LEDs toward their motion target colors.
 *
 * Iterates through every LED and moves its current RGB values toward the stored
 * target colour in LED::State.TargetColor[]. When no LEDs still need a transition, the task
 * kills itself and triggers an app delta color update.
 *
 * @param  taskId  Scheduler task identifier used for self-termination.
 */
void T_MOTION_CHANGE_COLOR(taskId_t taskId) {
    // Setup variables

    bool N = false;                                                     // Activity flag - State
    const int limit = LED_NUM_TOTAL;                                    // Cache loop limit - Setup
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));                      // Cache increment setting - Setup

    #ifdef ENABLE_LOG_MOTION_VERBOSE
        static bool colorTransitionStarted = false;
        if (!colorTransitionStarted) {
            PRNT::_print(PRNT::formatMSG("%32s : starting color transition" NL, "T_MOTION_CHANGE_COLOR"));
            colorTransitionStarted = true;
        }
    #endif

    for (int ledN = 0; ledN < limit; ledN++) {
        // 1. Check if this specific LED needs a color update
        // We pass the target colors from LED::State.TargetColor and the increment step
        if (LED::TG_COLOR(ledN, LED::State.TargetColor[ledN].r, LED::State.TargetColor[ledN].g, LED::State.TargetColor[ledN].b, brInc)) {
            
            // 2. Only update the pixel if TG_COLOR actually changed the values
            LED::setPixel(ledN, 
                        LED::State.CurrentColor[ledN].r,                                // Use updated Red - Sync
                        LED::State.CurrentColor[ledN].g,                                // Use updated Green - Sync
                        LED::State.CurrentColor[ledN].b,                                // Use updated Blue - Sync
                        LED::State.CurrentBrightness[ledN],                            // Keep current brightness - Sync
                        false);                                         // Wait for bulk Show - Sync
            
            N = true;                                                   // Flag that at least one LED changed - State
        }
    }

    if (N) {
        LED::Show();                                                     // Push color transition to hardware - Output
    } else {
        // Cleanup when all pixels reach their target colors
        #ifdef ENABLE_LOG_MOTION_VERBOSE
            PRNT::_print(PRNT::formatMSG("%32s : color transition complete" NL, "T_MOTION_CHANGE_COLOR"));
        #endif
        TSK::KillID(taskId, "T_MOTION_CHANGE_COLOR");                     // Terminate task - State

        // Update App Color state once transition is finalized
        APP::updDeltaColors();                                       // Send only changed LEDs (delta) - Sync
    }
}


/**
 * @brief  Read the active/inactive state of a motion sensor pin.
 *
 * @param  p  Pin number -- MOTION_PIN_COM (active HIGH) or MOTION_PIN_BED (active LOW, inverted).
 *
 * @return 1 if motion is detected on the given pin, 0 otherwise.
 *         Returns 0 for any unrecognised pin.
 */
uint8_t PinStatus(int p) {
	// p = 0  | BED
	// p = 1  | TV
	if (p == MOTION_PIN_COM) {
		return (digitalRead(MOTION_PIN_COM)) ? 1 : 0;
	} else 
	if (p == MOTION_PIN_BED) {
		return (!digitalRead(MOTION_PIN_BED)) ? 1 : 0;
	}

	return 0;
}
} // namespace MOTION


namespace APP {

/**
 * @brief  Initialise the app UDP communication socket.
 *
 * Opens APP_UDP on APP_UDP_PORT (8472) if WiFi is connected.
 * Call once in setup().
 */
void Setup() {
	// Boot session id - the app compares this to spot a restart it never saw.
	APP::State.SessionId = (uint16_t)random(0x0001, 0xFFFF);                   // Per-boot identity - Setup
	APP::State.Suspended = false;                                              // Assume app present until proven silent - State
	APP::State.KeepAliveID = TASK_ID_NONE;                                    // Keep-alive not scheduled until first welcome - State
	PRNT::_print(PRNT::formatMSG("%~32s # setup" NL, "APP_Setup"));

	if (NET::IsConnected()) {
		UdpSet();

		PRNT::_print(PRNT::formatMSG("%~32s # UDP port set to (%d)" NL, "APP_Setup", APP_UDP_PORT));
	} else {
		PRNT::_print(PRNT::formatMSG("%~32s # WiFi not connected" NL, "APP_Setup"));
	}
}

/**
 * @brief  Receive and dispatch one incoming UDP command packet per loop iteration.
 *
 * Caches WiFi status every 500 ms to reduce overhead. Exits early if not connected
 * or if no packet is waiting. Validates packet size (< APP_UDP_MAX_BUFFER_SIZE),
 * strips trailing newline/carriage return, then calls Exec() to dispatch.
 * Updates APP::Ard.LastReceive for the inactivity timeout.
 *
 * @note   Call every loop() iteration.
 */
void Loop() {
    // 1. WiFi status: use the shared cache updated once per loop() cycle
    if (!NET::Connected_Cached()) return;                                // Exit if no network - Logic

    // 2. Round-robin UDP polling - only poll on loop 0 of 3-cycle pattern
    static uint8_t udpPollPhase = 0;
    if (++udpPollPhase % 3 != 0) return;                                // Poll only every 3rd loop - Logic

    // 3. Fast exit for no packets
    int pkSize = APP_UDP.parsePacket();                                 // Check for incoming data - Action
    if (pkSize <= 0) return;                                            // Exit if empty - Logic

    // 3. Prevent buffer overflow and read efficiently
    if (pkSize >= APP_UDP_MAX_BUFFER_SIZE) {                            // Bounds check - Logic
        PRNT::_print(PRNT::formatMSG("%32s ! packet too large: [%d] bytes" NL, "APP_Loop", pkSize)); // Log overflow attempt - Output
        APP_UDP.flush();                                                // Clear the buffer - Action
        return;
    }

    APP_RECV_IP = APP_UDP.remoteIP();                                   // Cache sender IP - Mapping

    // 4. Zero-copy / Direct Read
    int len = APP_UDP.read(APP::State.RecvBuff, APP_UDP_MAX_BUFFER_SIZE - 1);  // Read packet - Action
    if (len > 0) {                                                      // If data read - Logic
        // Remove trailing newline or carriage return without String objects
        if (APP::State.RecvBuff[len - 1] == '\n' || APP::State.RecvBuff[len - 1] == '\r') { 
			len--;                                                      // Strip newline - Logic
		}
		APP::State.RecvBuff[len] = '\0';                                       // Null-terminate - Mapping

        #ifdef ENABLE_LOG_APP                                         // Logging block - Logic
			// Avoid String(APP::State.RecvBuff).c_str(), use buffer directly
			PRNT::_print(PRNT::formatMSG("%32s : recv [%s] (%d)" NL, "APP_Loop", APP::State.RecvBuff, len));
		#endif

        APP::Ard.LastReceive    = TimeNow;                                   // Update activity timer - State
        APP::Ard.LastUdpReceive = TimeNow;                                   // UDP-specific - drives APP_RECV_IP expiry - State
        APP::State.RxProtocol   = APP_TRANSPORT_UDP;                        // Tag source for Exec()'s protocol gate - State
        Exec(APP::State.RecvBuff, len);                                    // Execute command - Action
    }
}

void Exec(char *buff, int len) {
	// * Wake from suspend -- the app just spoke, so it is alive again. Only clear
	//   the flag here (TX + keep-alive resume); the resync itself is driven by
	//   the app's welcome 'Z' below, so we don't double-push. APP::Ard.LastReceive is
	//   refreshed by the caller before we get here.
	if (APP::State.Suspended) {
		APP::State.Suspended = false;                                          // Resume TX + keep-alive - State
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : app back, resuming TX" NL, "APP_Exec"));
		#endif
	}

	// * Any inbound proves the app is alive -- defer the next keep-alive ping by
	//   a full interval, so 'k' only goes out after a genuine gap of silence.
	TSK::ResetTime(APP::State.KeepAliveID);

	// * Keep-alive reply -- a bare "k" carries no payload: liveness only, no
	//   status push, no ACK. Handled before the envelope/dispatch below.
	if (buff[0] == 'k' && len == 1) return;                             // Alive tick - Logic

	// * LOG
	#ifdef ENABLE_LOG_APP
        PRNT::_print(PRNT::formatMSG("%32s : exec [%s] size [%d]" NL, "APP_Exec", buff, len)); // Log command - Sync
	#endif

	// * ACK envelope -- unwrap optional "#SS<cmd>" (2-hex seq); reply "#SSR" at the tail
	g_difRelaySeq   = 0xFF;               // re-armed per app-originated diffuser send below
	APP::State.CurSeqValid = false;
	APP::State.LastResult  = APP_ACK_OK;
	if (len >= APP_ACK_HDR && buff[0] == APP_ACK_CHAR) {
		APP::State.CurSeq      = HexByte(&buff[1]);
		APP::State.CurSeqValid = true;
		buff += APP_ACK_HDR;
		len  -= APP_ACK_HDR;
	}

	// * De-dup retransmits: a repeated seq within the window is never re-executed
	//   (prevents slow commands like the K debug dump from running 2-4 times when
	//   the app retransmits before our ACK arrives). Re-ack immediately unless the
	//   original is still awaiting its diffuser relay -- then let the relay ack it.
	if (APP::State.CurSeqValid && APP::State.LastSeqValid && APP::State.CurSeq == APP::State.LastSeq
	        && (uint32_t)(TimeNow - APP::State.LastSeqTime) < APP_ACK_DEDUP_MS) {
		if (!DIF::IsAppSeqPending(APP::State.CurSeq))
			termMsgAck(APP::State.CurSeq, APP::State.LastSeqResult);
		return;
	}

	// Cache the first character to a local register
	const char cmd = buff[0];

	// * Single active transport -- a command arriving on the transport that is
	//   NOT currently active is dropped, except 'Z': that's the only way the
	//   idle transport is allowed to speak, and cmdConnected() below is what
	//   flips ActiveProtocol to match it (i.e. how a protocol switch happens).
	if (cmd != 'Z' && APP::State.RxProtocol != APP::State.ActiveProtocol) {
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : dropped [%c], inactive transport [%s]" NL,
				"APP_Exec", cmd, TransportName(APP::State.RxProtocol)));
		#endif
		return;
	}

	switch (cmd) {
		case 'Z': cmdConnected();      break; // Welcome Packet
		case 'X': cmdEnableDisable(); break; // Enable Disable leds
		case 'S': cmdSettings(buff, len);    break; // Settings
		case 'A': cmdAmbientMode(buff, len); break; // Ambient Mode
		case 'K': cmdDebug(buff, len);       break; // Debug
        case '@': cmdTestMode(buff, len);      break; // Test Mode
        case '$': MQTTCRED::cmdSetCredentials(buff, len); break; // MQTT credential provisioning (b64 user,pass)

		case 'D': // Diffuser sub-commands -- forwarded to the DIF UDP link
			switch (buff[1]) {
				case 's': DIF::RequestStatus();
				          if (DIF::State.ParfumMin > 0) termMsgSend(PRNT::formatMSG("p%4X", DIF::State.ParfumMin)); // Parfum popup asked - answer time left
				          break; // Ask diffuser for status
				case 'h': DIF::RequestHistory(); break; // Ask diffuser for full refill history - relayed straight through on reply
				case 'r': g_difRelaySeq = APP::State.CurSeqValid ? APP::State.CurSeq : 0xFF; DIF::ManualRefill(); break; // Manual refill - banks + resets usage now
				case 'f': g_difRelaySeq = APP::State.CurSeqValid ? APP::State.CurSeq : 0xFF; DIF::Shutdown(); break; // Shutdown diffuser
				case 'n': cmdDiffuserTurnOn(buff, len); break; // Turn on (mode + effect)
				case 'p': cmdDiffuserParfum(buff, len); break; // Parfum mode (minutes; 0 = cancel)
				default: APP::State.LastResult = APP_ACK_UNSUPPORTED; PRNT::_print(PRNT::formatMSG("%32s ! invalid DIF subcommand [%c] from [%d.%d.%d.%d]" NL, "APP_Exec", buff[1], APP_RECV_IP[0], APP_RECV_IP[1], APP_RECV_IP[2], APP_RECV_IP[3])); break;
			}
			break;

		case 'L': // LED Zone sub-commands
			switch (buff[1]) {
				case 'B': cmdChangeBrightness(buff, len);       break; // Brightness
				case 'C': cmdChangeColor(buff, len);            break; // Color
				case 'D': cmdChangeDualColor(buff, len, false); break; // Dual Color
				case 'd': cmdChangeDualColor(buff, len, true);  break; // Shake Dual Color
				case 'O': cmdSetLed(buff, len);                 break; // Led Selected
				default: APP::State.LastResult = APP_ACK_UNSUPPORTED; PRNT::_print(PRNT::formatMSG("%32s ! invalid LED subcommand [%c] from [%d.%d.%d.%d]" NL, "APP_Exec", buff[1], APP_RECV_IP[0], APP_RECV_IP[1], APP_RECV_IP[2], APP_RECV_IP[3])); break;
			}
			break;

		default: // Invalid command
			APP::State.LastResult = APP_ACK_UNSUPPORTED;
			PRNT::_print(PRNT::formatMSG("%32s ! invalid command [%s] from [%d.%d.%d.%d]" NL, "APP_Exec", buff, APP_RECV_IP[0], APP_RECV_IP[1], APP_RECV_IP[2], APP_RECV_IP[3]));
			break;
	}

	updStatus("APP::Exec");

	// * ACK the command. Diffuser-relayed commands (Dn/Dp/Df/@D) defer until the
	//   diffuser answers (relay pending for THIS seq) -- see DIF::AckParse(); the relay
	//   arm is cleared so an autonomous diffuser send never relays to a stale app seq.
	// * Remember this seq so retransmits are de-duped (re-acked, not re-run).
	if (APP::State.CurSeqValid) {
		APP::State.LastSeq       = APP::State.CurSeq;
		APP::State.LastSeqValid  = true;
		APP::State.LastSeqResult = APP::State.LastResult;
		APP::State.LastSeqTime   = TimeNow;
	}
	if (APP::State.CurSeqValid && !DIF::IsAppSeqPending(APP::State.CurSeq))
		termMsgAck(APP::State.CurSeq, APP::State.LastResult);
	g_difRelaySeq = 0xFF;
}


/**
 * @brief  Respond to a 'Z' welcome packet -- send a full state sync to the app.
 *
 * Sends max brightness (LM), all settings (S...), current status (s...),
 * all LED colours (LC...), then stops the loading bar.
 *
 * @note   Call to force a full re-sync with the phone app at any time.
 */
void cmdConnected() {
	// * Adopt the transport this welcome arrived on as the sole active one --
	//   every reply below (and every future push) now goes out on it only.
	APP::State.ActiveProtocol = APP::State.RxProtocol;

	// * LOG
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : connected, active transport [%s]" NL,
			"APP_Connected", TransportName(APP::State.ActiveProtocol)));
	#endif

	// Max brightness (LM) -- welcome-only, sets the app's slider range
	updBrMax();

	// All settings dump
	updSettings();

	// Force every dirty-gated group to resend, then push a full snapshot.
	// Covers s (core) / H (climate) / E (enable) / M (lux) / w (link) and
	// parfum-if-active in one pass. Faults are transition-only and are NOT
	// force-resent here -- the app only ever wants them when one occurs.
	TxCacheReset();
	updStatus("APP::cmdConnected");

	// State the app can only learn by being told
	updTestMode();

	// Update Colors (force: full sync on initial connect)
	updColors_Force();

	// Keep-alive lives only while an app session is up. The welcome starts it;
	// KeepAlive kills it if the app later goes silent. Add once -- a repeat
	// welcome (e.g. the app re-announcing on resume) just restarts its timer.
	APP::State.Suspended = false;                                              // Resume TX - State
	if (APP::State.KeepAliveID == TASK_ID_NONE)
		APP::State.KeepAliveID = TSK::AddTask("APP_Connected", "T_KeepAlive", T_KeepAlive,
		                               TASK_MS, KeepAlive_MS, KeepAlive_MS, true); // locked, first fire after one interval
	else
		TSK::ResetTime(APP::State.KeepAliveID);                               // already running -- defer next ping
}

/**
 * @brief  Handle '@' commands -- set the global TestMode enum, all values integrated.
 *
 * Command forms (all handled in ONE flow -- every valid command ends at the
 * classic tail below, which is the ONLY place the auto-cancel task is armed):
 *   '@ii'  -> plain enum value (hex):
 *              0x00 = _testmode_none  (disable test mode)
 *              0x01 = _testmode_tvOn
 *              0x02 = _testmode_tvOff
 *              0x03 = _testmode_udpraw
 *              0x04 = _testmode_motionCom
 *              0x05 = _testmode_motionBed
 *   '@Dvv' -> TestDiffuser(vv) action (00 off - 01-04 mode - FF random
 *            colour), then latches _testmode_dif (0x06); '00' drops the latch.
 *   '@Lvv' -> TestLux(vv) forced lux level 01-04, then latches
 *            _testmode_lux (0x07).
 * _testmode_dif/_testmode_lux carry a parameter, so a plain '@ii' can never
 * select them directly.
 *
 * Classic X-second exit pattern for EVERY non-none value: T_END_TEST_MODE is
 * (re)armed here and auto-cancels after TESTMODE_DURATION (120 s), running the
 * per-mode exit cleanup (dif -> DIF::AutoOff(), lux -> LISENS::ResetTime()). The
 * same cleanup runs here when switching AWAY from dif/lux before the timeout.
 *
 * @param  buff  Command buffer ('@' at [0]).
 * @param  len   Length of the command (>= 2, >= 4 for '@Dvv'/'@Lvv').
 *
 * Called by: APP::Exec()'s dispatch switch, case '@'.
 */
void cmdTestMode(char *buff, int len) {
    // * LOG
    #ifdef ENABLE_LOG_APP
        PRNT::_print(PRNT::formatMSG("%32s : test mode command received: [%s]" NL, "cmdTestMode", buff));
    #endif

    if (len < 2 || ((buff[1] == 'D' || buff[1] == 'L') && len < 4)) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid test mode command length [%d]" NL, "cmdTestMode", len));
        termMsgLog(APP_LOG_ERR, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode command too short [%d] bytes", len);
        return;
    }

    uint8_t modeValue = _testmode_none;                                 // Target enum value - State
    bool    valid     = true;                                          // Command validity - State

    // --- PARSE: map the command onto a __testmode enum value ---
    if (buff[1] == 'D') {
        const uint8_t v = (uint8_t)strtoul(&buff[2], NULL, 16);         // Diffuser test value - Setup
        valid = cmdTestMode_Diffuser(v);                             // Send Df/Dn now - Action
        termMsgLog(valid ? APP_LOG_WRN : APP_LOG_ERR, APP_SRC_TEST, "APP", "cmdTestMode",
            "Diffuser test [%X] %s", v, valid ? "sent" : "rejected");
        // '00' (off) drops the latch -- but only when the dif test owns it,
        // so it can't cancel an unrelated running TV/motion/lux test.
        modeValue = (v == 0x00)
                  ? ((TestMode == _testmode_dif) ? _testmode_none : TestMode)
                  : _testmode_dif;                                      // Latch diffuser test - Logic
    } else if (buff[1] == 'L') {
        {
            const uint8_t lv = (uint8_t)strtoul(&buff[2], NULL, 16);
            valid = cmdTestMode_Lux(lv);                              // Apply forced lux now - Action
            termMsgLog(valid ? APP_LOG_WRN : APP_LOG_ERR, APP_SRC_TEST, "APP", "cmdTestMode",
                "Lux forced to level [%d] %s", lv, valid ? "held" : "rejected");
        }
        modeValue = _testmode_lux;                                      // Latch lux test - Logic
    } else {
        modeValue = strtoul(&buff[1], NULL, 16);                        // Convert hex string to integer - Setup
        valid = (modeValue <= _testmode_motionBed);                     // dif/lux need a parameter -> '@D'/'@L' only - Logic
    }

    // --- APPLY: single classic tail -- exit cleanup, set enum, (re)arm the task ---
    if (valid) {
        // Exit cleanup for the mode being left (mirrors T_END_TEST_MODE)
        if (TestMode != modeValue) {
            // UDPRAW had no exit cleanup, so cancelling left UDPRAW::State.Status set.
            // The only thing that cleared it was the 5 s watchdog in
            // UDPRAW::Loop() - and that is unreachable when WiFi is down, since
            // the loop returns early unless the udpraw test is still active.
            // Result: ambilight stayed on for good. End it here instead.
            if      (TestMode == _testmode_udpraw) UDPRAW::End(false);   // Stop the simulated stream, no handover - Action
            else if (TestMode == _testmode_dif) DIF::AutoOff();          // Off unless a real source drives it - Action
            else if (TestMode == _testmode_lux) LISENS::ResetTime();     // Fresh sample window - sensor resumes - Action
        }

        TestMode = (__testmode)modeValue;                               // Set the global TestMode variable - State

        if (TestMode_tID != TASK_ID_NONE) { // If a test mode task is already running, kill it before starting a new one
            TSK::KillID(TestMode_tID, "cmdTestMode");                  // Kill existing test mode task - Logic
            TestMode_tID = TASK_ID_NONE;                                // Clear handle - State
        }

        if (TestMode != _testmode_none) {
            TestMode_tID = TSK::AddTask("cmdTestMode", "T_END_TEST_MODE", T_END_TEST_MODE, TASK_MS, 1, TESTMODE_DURATION, true); // Start test mode task

            // * LOG
            PRNT::_print(PRNT::formatMSG("%~32s # test mode set to [%d] for [%d] ms" NL, "cmdTestMode", TestMode, TESTMODE_DURATION));

            termMsgLog(APP_LOG_WRN, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode [%s] armed for [%d] s",
                _TestModeName(TestMode), TESTMODE_DURATION / 1000);
        } else {
            // * LOG
            PRNT::_print(PRNT::formatMSG("%~32s # test mode disabled" NL, "cmdTestMode"));

            termMsgLog(APP_LOG_INF, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode [CANCELLED] sensors released");
        }

        // Either direction is news to the app - it can set a mode but was
        // never told when one ended, and every mode self-cancels.
        updTestMode();                                          // Sync forced state - Sync
    } else {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid test mode value [%s]" NL, "cmdTestMode", &buff[1]));

        termMsgLog(APP_LOG_ERR, APP_SRC_TEST, "APP", "cmdTestMode", "Test mode rejected [%s]", &buff[1]);
    }

    // Update Status
	updStatus("APP::cmdTestMode"); // *
}

/**
 * @brief  Handle the '@Dvv' action -- exercise the diffuser from the Test Mode UI.
 *
 * Action-only helper: sends the diffuser command and reports validity --
 * cmdTestMode() owns the enum latch and the X-second auto-cancel task.
 * Unlike 'Dn' (DiffuserTurnOn), this is a raw test path: it bypasses the
 * DIF::ActiveModeSetting() source guard so the diffuser can be exercised while
 * everything is idle.
 *
 * @param  val  0x00 = DIF::Shutdown(), 0x01..DIF_MODE_MAX = DIF::TurnOn() in that
 *              mode, 0xFF = re-push current/last valid mode with a fresh random
 *              colour pair (dual-aware -- DIF::TurnOn() only sends c2 when dual).
 *
 * @return true when the value was valid and the command was sent.
 */
bool cmdTestMode_Diffuser(uint8_t val) {
    // * LOG
    PRNT::_print(PRNT::formatMSG("%~32s # diffuser test [%02X]" NL, "TestDiffuser", val));

    g_difRelaySeq = APP::State.CurSeqValid ? APP::State.CurSeq : 0xFF;                // relay the diffuser's real ack to the app

    if (val == 0x00) {
        DIF::Shutdown();                                                 // Force off - Action
    } else if (val == 0xFF) {
        const uint32_t c1 = LED::getRandomColor();                       // Fresh vivid colour - Setup
        const uint32_t c2 = LED::getRandomColor();                       // Second colour for dual - Setup
        DIF_Colorx c = { (uint8_t)(c1 >> 16), (uint8_t)(c1 >> 8), (uint8_t)c1,
                         (uint8_t)(c2 >> 16), (uint8_t)(c2 >> 8), (uint8_t)c2 };
        const uint8_t mode = (DIF::State.Mode >= 1 && DIF::State.Mode <= DIF_MODE_MAX) ? DIF::State.Mode : 1; // Keep running mode, else CONT - Logic
        DIF::TurnOn(mode, EE::Get(EE_DIF_EFFECT), &c);                    // Push random colour - Action
    } else if (val <= DIF_MODE_MAX) {
        DIF::TurnOn(val, EE::Get(EE_DIF_EFFECT));                         // Turn on in requested mode - Action
    } else {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid diffuser test value [%d]" NL, "TestDiffuser", val));
        return false;                                                   // Reject - Logic
    }

    return true;                                                        // Command sent - Logic
}

/**
 * @brief  Handle the '@Lvv' action -- force LISENS::State.Lux to a fixed level for testing.
 *
 * Action-only helper: applies the level through LISENS::setLux() -- the exact
 * same adaptive-speed transition path a real ambient-light change takes
 * (level drives LED::getLuxAdaptFactor() for TV/MOTION animations) -- and reports
 * validity. cmdTestMode() owns the _testmode_lux latch (which makes
 * LISENS::Check() hold sensor-driven changes) and the X-second auto-cancel;
 * on timeout T_END_TEST_MODE runs LISENS::ResetTime(), so the sensor resumes
 * on a fresh sample window.
 *
 * @param  level  Target lux level, 1..(threshold count + 1).
 *
 * @return true when the level was valid and applied.
 */
bool cmdTestMode_Lux(uint8_t level) {
    static const int maxLuxLevel = (sizeof(LIGHT_SENS_LUX) / sizeof(LIGHT_SENS_LUX[0])) + 1;

    if (level < 1 || level > maxLuxLevel) {
        PRNT::_print(PRNT::formatMSG("%32s ! invalid lux test level [%d]" NL, "APP_TestLux", level));
        return false;                                                   // Reject - Logic
    }

    // * LOG
    PRNT::_print(PRNT::formatMSG("%~32s # lux forced to [%d] (was [%d])" NL, "APP_TestLux", level, LISENS::State.Lux));

    LISENS::setLux(level);                                             // Same adaptive path as a real lux change - Action
    updLux();                                                   // Update App UI - Sync

    return true;                                                        // Applied - Logic
}

/**
 * @brief  Handle 'DnXXee' command -- relay a "turn on" request to the diffuser.
 *
 * @param  buff  Command buffer. buff[2..3] = target MODE (hex), buff[4..5] = target EFFECT (hex).
 * @param  len   Length of the command (must be 6: "Dn" + 2-hex MODE + 2-hex EFFECT).
 *
 * Only forwarded while at least one activity source is active (TV, motion,
 * UDPRAW, or Ambient Mode) -- DIF::ActiveModeSetting() != 0xFF. Otherwise ignored, so
 * the app can't switch the diffuser's mode/effect while everything is idle.
 * Parses the two hex bytes with HexByte() and forwards them to DIF::TurnOn(),
 * which validates the ranges, derives the colour live from the current LEDs
 * (or splits it in two if the TV is in Dual Color mode), and sends the "Dn"
 * packet on to the diffuser over the DIF UDP link (port DIF_UDP_PORT).
 */
void cmdDiffuserTurnOn(char *buff, int len) {
    if (len != 6) {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "DiffuserTurnOn", len));
        return;
    }

    if (DIF::ActiveModeSetting() == 0xFF) {                               // Nothing active to justify the change - Logic
        APP::State.LastResult = APP_ACK_BLOCKED;
        PRNT::_print(PRNT::formatMSG("%~32s # no active source, command ignored [tv:%T] [udpraw:%T] [motion:%d] [am:%T]" NL,
            "DiffuserTurnOn", TV::State.Status, UDPRAW::State.Status, MOTION::State.Status, APP::Am.Status));
        return;
    }

    uint8_t mode   = HexByte(&buff[2]);
    uint8_t effect = HexByte(&buff[4]);

    if (mode > DIF_MODE_MAX || effect > DIF_EFFECT_COUNT) {              // Validate here - DIF::TurnOn() also
        APP::State.LastResult = APP_ACK_REJECTED;                        // checks this but silently no-ops on
        PRNT::_print(PRNT::formatMSG("%32s ! invalid mode/effect [mode:%d] [effect:%d]" NL,   // failure, which would
            "DiffuserTurnOn", mode, effect));                            // otherwise ack the app OK for nothing - Logic
        return;
    }

    g_difRelaySeq = APP::State.CurSeqValid ? APP::State.CurSeq : 0xFF;                 // relay the diffuser's real ack to the app
    DIF::TurnOn(mode, effect);                                            // Colour derived live from current LEDs - Action
}

/**
 * @brief  Handle 'DpMMMME' command -- relay a parfum-mode request to the diffuser.
 *
 * @param  buff  Command buffer. buff[2..5] = duration in minutes (4-digit hex),
 *               buff[6] = dispense mode (1-digit hex).
 * @param  len   Length of the command (must be 7: "Dp" + 4-hex MINUTES + 1-hex MODE).
 *
 * MMMM = 0001..DIF_PARFUM_MAX_MIN starts a timed "insist" run on the diffuser
 * (it ignores Df/Dn until expiry, violet PULSE strip cue) in the given
 * dispense MODE; MMMM = 0000 cancels the run and shuts the diffuser down
 * (MODE ignored). Unlike 'Dn'
 * this is NOT gated on an active source -- parfum is a deliberate user
 * action, valid any time. The diffuser itself validates the ranges too;
 * remaining minutes come back in the extended "DsMMSSTTTT" status handled
 * by DIF::ParseStatus().
 */
void cmdDiffuserParfum(char *buff, int len) {
    if (len != 7) {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "DiffuserParfum", len));
        return;
    }

    uint16_t minutes = ((uint16_t)HexByte(&buff[2]) << 8) | HexByte(&buff[4]); // 4-hex big-endian - Mapping
    uint8_t  mode     = HexNibble(buff[6]);                                        // 1-hex dispense mode - Mapping

    if (minutes > DIF_PARFUM_MAX_MIN) {                                  // Range guard - Logic
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid minutes (%d) max (%d)" NL, "DiffuserParfum", minutes, DIF_PARFUM_MAX_MIN));
        return;
    }

    if (minutes > 0 && (mode < 1 || mode > DIF_MODE_MAX)) {              // Mode only matters while starting a run - Logic
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! invalid mode (%d) max (%d)" NL, "DiffuserParfum", mode, DIF_MODE_MAX));
        return;
    }

    g_difRelaySeq = APP::State.CurSeqValid ? APP::State.CurSeq : 0xFF;                 // relay the diffuser's real ack to the app
    DIF::Parfum(minutes, mode);                                           // Forward to diffuser - Action
}

/**
 * @brief  Toggle LED::State.Enabled -- enable or disable the entire LED system.
 *
 * On disable: resets TV and Motion state, kills all tasks, stops UDPRAW,
 * and turns all LEDs off. On enable: does nothing extra (state resumes on next loop).
 * Sends updated status to the app and stops the loading bar.
 */
void cmdEnableDisable() {
	// * Enable / Disable
	LED::State.Enabled = !LED::State.Enabled;

	if (!LED::State.Enabled) { // Led's Disabled
		// Disabled means dark and staying dark - not "handed to motion". These
		// two presets were doing what End's handover did: motON re-arms
		// motion, which relights the strip on the next MOTION::Status(), and any
		// transition task left running ramps back up because LED::setAll() does
		// not touch LED::State.TargetColor. So force everything idle and clear the
		// targets, then blank and kill - blank-before-kill, same order as
		// T_LEDS_TO_OFF, so nothing re-arms after the strip goes dark.
		TV::State.Status     = false;                                          // TV idle - State
		MOTION::State.Status = motOFF;                                         // Do NOT re-arm - State
		UDPRAW::State.Status = false;                                          // Ambilight off - State

		for (int i = 0; i < LED_NUM_TOTAL; i++) {
			LED::State.TargetColor[i] = CRGB(0, 0, 0);                         // Nothing left to ramp toward - State
		}

		LED::setAll(0, 0, 0, 0);                                         // Blank the buffer - Action
		TSK::KillTasksAvoidLocked("Enable_Disable");                // Cancel every ramp - Action
		LED::ForceShow();                                                // Push the blank NOW - the periodic
		                                                                // refresh is gated on LED::State.Enabled and
		                                                                // will never run once disabled - Action
		DIF::Shutdown();                                                 // Force diffuser off - Action

		// * Delta led update to app
		updDeltaColors();
	} else {
		// Led's Enabled - re-arm motion so it doesn't stay offline
		MOTION::State.Status = motON;                                         // Re-arm motion - State
	}

	// * LOG
	PRNT::_print(PRNT::formatMSG("%~32s # LEDs [%s]" NL, "Enable_Disable", (LED::State.Enabled) ? "enabled" : "disabled"));

	// Update Status
	updStatus("APP::cmdEnableDisable"); // *
}

/**
 * @brief  Handle 'LBvv' command -- set brightness for all selected LEDs.
 *
 * @param  buff  Command buffer. buff[1..2] = 2-digit hex brightness value.
 * @param  len   Must equal 4 (prefix + 2 hex digits + null).
 *
 * If UDPRAW active: applies brightness to UDPRAW colour directly.
 * If Motion active (> motON): ignores the command.
 * Otherwise: writes to LED::State.StoredBrightness[] for selected LEDs and launches
 * T_SMOOTH_CHANGE in brightness mode (TASK.ParamA=1).
 *
 * Called by: APP::Exec()'s dispatch switch, case 'L'->'B'.
 */
void cmdChangeBrightness(char *buff, int len) {
	// only brightness
	if (len != 4) {
		APP::State.LastResult = APP_ACK_REJECTED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "ChangeBrightness", len));
		return;
	}

	// Fast hex parsing instead of strtol (2x faster)
	uint8_t br = HexByte(&buff[2]);
	br = constrain(br, 0, APP_BRIGHT);

	// * LOG
	PRNT::_print(PRNT::formatMSG("%~32s # set brightness [%d]" NL, "ChangeBrightness", br));

	// Cache status flags to avoid repeated struct reads
	bool isUdpraw = UDPRAW::State.Status;
	bool isMotion = MOTION::State.Status > motON;

	// * UDPRAW
	if (isUdpraw) {
		LED::UDPRAW_SetColor(LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b, br);
		updDeltaColors();
        #ifdef ENABLE_LOG_APP
            PRNT::_print(PRNT::formatMSG("%32s : UDPRAW active, brightness set [%d]" NL, "ChangeBrightness", br));
        #endif
		return;
	}

	// * Motion
	if (isMotion) {
		APP::State.LastResult = APP_ACK_BLOCKED;
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : motion active, brightness change ignored" NL, "ChangeBrightness"));
		#endif
		return;
	}

	// * Normal - use optimized batch function (avoids 200+ loop iterations)
	LED::setBrightnessToSelected(br);

	// * Task
	TASK.ParamA = 1; // brightness
	TSK::KillTasksAvoidLocked("ChangeBrightness");
    TSK::AddTask("ChangeBrightness", "T_SMOOTH_CHANGE", LED::T_SMOOTH_CHANGE, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL), 50, false);

	// * Motion
	MOTION::State.Status = motOFF;
}

/**
 * @brief  Handle 'LC' / 'LCrrGGbb' command -- request colours or set a new colour.
 *
 * @param  buff  Command buffer. len==2: send colour info back. len==8: set new colour.
 * @param  len   2 = colour request; 8 = 2-digit hex R + G + B.
 *
 * If UDPRAW active: applies colour to UDPRAW colour directly.
 * If Motion active (> motON): applies colour to the motion colour.
 * Otherwise: writes to LED::State.TargetColor[] for selected LEDs and launches T_SMOOTH_CHANGE
 * in colour mode (TASK.ParamA=0). Also disables random colour (EE_TV_RANDOM_COLOR_START=0).
 *
 * Called by: APP::Exec()'s dispatch switch, case 'L'->'C'.
 */
void cmdChangeColor(char *buff, int len) {
	if (len == 2) {
		// * LOG
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : send color info" NL, "ChangeColor"));
		#endif

		// Update colors (force: phone is requesting current state)
		updColors_Force(); // *

		return;
	}
	if (len != 8) {
		APP::State.LastResult = APP_ACK_REJECTED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "ChangeColor", len));
		return;
	}

	// Fast hex parsing (replaces 3x strtol calls - significant speedup)
	uint8_t r = HexByte(&buff[2]);
	uint8_t g = HexByte(&buff[4]);
	uint8_t b = HexByte(&buff[6]);

	// * LOG
	PRNT::_print(PRNT::formatMSG("%~32s # set [r:%d] [g:%d] [b:%d]" NL, "ChangeColor", r, g, b));

	// Cache status flags (avoid repeated struct reads)
	bool isUdpraw = UDPRAW::State.Status;
	bool isMotion = MOTION::State.Status > motON;

	// * UDPRAW
	if (isUdpraw) {
		LED::UDPRAW_SetColor(r, g, b, LED::State.StreamBrightness);
		updDeltaColors();
		return;
	}

	// * Motion
	if (isMotion) {
		EE::Set(EE_MOTION_RANDOM_COLOR, 0);
		LED::MOTION_SetColor(r, g, b, EE::Get(EE_MOTION_BRIGHTNESS));
		updDeltaColors();
		return;
	}

	// * Normal - use optimized batch function (avoids 200+ loop iterations)
	LED::setRandomColor(false);
	LED::setColorToSelected(r, g, b);

	// * Task
	TASK.ParamA = 0; //  color
	TSK::KillTasksAvoidLocked("ChangeColor");
    TSK::AddTask("ChangeColor", "T_SMOOTH_CHANGE", LED::T_SMOOTH_CHANGE, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL), 20, false);
	
	// * HB Enable DualColor -- only touch the setting (and resync the app's
	// full settings view) when it's actually flipping; every plain colour
	// command used to do this unconditionally, which meant a 200+ byte
	// full-settings resend on every single colour change even when dual
	// colour was already off.
	if (EE::Get(EE_HB_DUAL_COLOR) != 0) {
		EE::Set(EE_HB_DUAL_COLOR, 0);
		updSettings(); // *
	}

	// * Push to diffuser now if a source is active -- same reasoning as ChangeDualColor:
	// LED::State.CurrentColor hasn't faded to the new colour yet, so pass it straight through.
	DIF_Colorx colorOverride = { r, g, b, r, g, b };
	DIF::PushLiveIfActive(&colorOverride);

	// * Motion - disable
	MOTION::State.Status = motOFF;
}

/**
 * @brief  Handle 'LD'/'Ld' command -- request dual colour or set a new dual colour.
 *
 * @param  buff       Command buffer. len==2: send dual-colour info back. len==14: set new colour.
 * @param  len        2 = dual-colour request; 14 = 2x (2-digit hex R + G + B).
 * @param  shakeMode  true = 'Ld' (shake-triggered dual colour, launches
 *                    T_SHAKE_DUAL_COLOR); false = 'LD' (plain, launches T_DUAL_COLOR).
 *
 * Rejected/blocked while UDPRAW, Ambient Mode, or Motion (COM/BED) is active -
 * dual colour only applies to the idle TV-on state. Otherwise: writes
 * LED::State.TargetColor[0/1], enables EE_HB_DUAL_COLOR (only when it's
 * actually flipping, to avoid a full settings resend on every call), pushes
 * the new colour to the diffuser if a source is active, and launches the
 * matching task.
 *
 * Called by: APP::Exec()'s dispatch switch, case 'L'->'D' (shakeMode=false)
 * and case 'L'->'d' (shakeMode=true).
 */
void cmdChangeDualColor(char *buff, int len, bool shakeMode) {
	if (len == 2) {
		// * Send Dual Color
		termMsgSend(PRNT::formatMSG("LD%X%X%X%X%X%X",
			LED::State.CurrentColor[LED::UCOM(0)].r, LED::State.CurrentColor[LED::UCOM(0)].g, LED::State.CurrentColor[LED::UCOM(0)].b,
			LED::State.CurrentColor[LED::UCOM(1)].r, LED::State.CurrentColor[LED::UCOM(1)].g, LED::State.CurrentColor[LED::UCOM(1)].b));

		// * LOG
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%~32s # dual color info [r:%d] [g:%d] [b:%d] <> [r:%d] [g:%d] [b:%d]" NL, "ChangeDualColor",
			LED::State.CurrentColor[LED::UCOM(0)].r, LED::State.CurrentColor[LED::UCOM(0)].g, LED::State.CurrentColor[LED::UCOM(0)].b,
			LED::State.CurrentColor[LED::UCOM(1)].r, LED::State.CurrentColor[LED::UCOM(1)].g, LED::State.CurrentColor[LED::UCOM(1)].b));
		#endif
		return;
	}
	if (len != 14) {
		APP::State.LastResult = APP_ACK_REJECTED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "ChangeDualColor", len));
		return;
	}

	if (UDPRAW::State.Status) {
		APP::State.LastResult = APP_ACK_BLOCKED;
		// * LOG
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%~32s # UDPRAW active, command ignored" NL, "ChangeDualColor"));
		#endif
		return;
	}

	if (APP::Am.Status) {
		APP::State.LastResult = APP_ACK_BLOCKED;
		// * LOG
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%~32s # Ambient Mode active, command ignored" NL, "ChangeDualColor"));
		#endif
		return;
	}

	if (MOTION::State.Status > motON) {                                   // COM or BED currently triggered
		APP::State.LastResult = APP_ACK_BLOCKED;
		// * LOG
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%~32s # motion active, command ignored" NL, "ChangeDualColor"));
		#endif
		return;
	}

	// * disable random color
	LED::setRandomColor(false);

	// * Motion
	MOTION::State.Status = motOFF;

	// Split color
	uint32_t leftColor  = H_ParseHexColor(&buff[2]);
	LED::State.TargetColor[0].r = (uint8_t)(leftColor >> 16); // Extract Red - LEFT
	LED::State.TargetColor[0].g = (uint8_t)(leftColor >> 8 & 0xFF); // Extract Green - LEFT
	LED::State.TargetColor[0].b = (uint8_t)(leftColor & 0xFF); // Extract Blue - LEFT

	uint32_t rightColor = H_ParseHexColor(&buff[8]);
	LED::State.TargetColor[1].r = (uint8_t)(rightColor >> 16); // Extract Red - RIGHT
	LED::State.TargetColor[1].g = (uint8_t)(rightColor >> 8 & 0xFF); // Extract Green - RIGHT
	LED::State.TargetColor[1].b = (uint8_t)(rightColor & 0xFF); // Extract Blue - RIGHT


	// * Kill all tasks
	TSK::KillTasksAvoidLocked("ChangeDualColor");

	// * HB Enable DualColor -- only when it's actually flipping (see the
	// matching note in cmdChangeColor); once dual colour is already on,
	// repeated LD/Ld calls (e.g. shake) don't need to re-flag it and
	// re-send the full 50-setting dump every time.
	if (EE::Get(EE_HB_DUAL_COLOR) != 1) {
		EE::Set(EE_HB_DUAL_COLOR, 1);
		updSettings(); // *
	}

	// * Push to diffuser now if a source is active -- LED::State.CurrentColor hasn't faded
	// to the new colours yet, so pass them straight through instead of deriving live.
	DIF_Colorx dualOverride = {
		LED::State.TargetColor[0].r, LED::State.TargetColor[0].g, LED::State.TargetColor[0].b,
		LED::State.TargetColor[1].r, LED::State.TargetColor[1].g, LED::State.TargetColor[1].b
	};
	DIF::PushLiveIfActive(&dualOverride);
	
	// * Task
	if (shakeMode) {
		// * Task 
		TASK.Phase = 1;
		TASK.ParamA = 0; //
		TASK.ParamB = 0; //
		
        TSK::AddTask("ChangeDualColor", "T_SHAKE_DUAL_COLOR", LED::T_SHAKE_DUAL_COLOR, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false);
	} else {
		// * Task 
		TASK.Phase = 1;

        TSK::AddTask("ChangeDualColor", "T_DUAL_COLOR", LED::T_DUAL_COLOR, TASK_MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false);
	}
	
	// * LOG
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%~32s #%s dual color set [r:%d] [g:%d] [b:%d] <> [r:%d] [g:%d] [b:%d]" NL, "ChangeDualColor",
			shakeMode ? " shake" : "",
			LED::State.TargetColor[0].r, LED::State.TargetColor[0].g, LED::State.TargetColor[0].b,
			LED::State.TargetColor[1].r, LED::State.TargetColor[1].g, LED::State.TargetColor[1].b));
	#endif
}

/**
 * @brief  Handle 'LOvv...' command -- update the LED selection bitmask.
 *
 * @param  buff  Command buffer. buff[2..2+LED_NUM-1] = '0' or '1' per LED::State.
 * @param  len   Must equal LED_NUM + 2.
 *
 * Sets or clears the selection bit for each LED based on the '0'/'1' characters.
 * Stops the loading bar after applying.
 *
 * Called by: APP::Exec()'s dispatch switch, case 'L'->'O'.
 */
void cmdSetLed(char *buff, int len) {
	if (len != LED_NUM+2) {
		APP::State.LastResult = APP_ACK_REJECTED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "SetLed", len));
		return;
	}

	// * LED
	bool selectionChanged = false;
	for (int i = 2; i < len; i++) {
		if (i-2 < LED_NUM) {
			//
			LED::Select(i - 2, (buff[i] == '1'));
			selectionChanged = true;

			// * LOG
			#ifdef ENABLE_LOG_APP
				PRNT::_print(PRNT::formatMSG("%32s : LED [%d] [%s]" NL, "SetLed", i - 2, (LED::IsSelected(i - 2)) ? "selected" : "not selected"));
			#endif
		} else {
			// * LOG
			PRNT::_print(PRNT::formatMSG("%32s : invalid LED index [%d]" NL, "SetLed", i - 2));
		}
	}

	// Invalidate selected LED cache (will recalculate on next command)
	if (selectionChanged) {
        APP::State.SelectedCacheDirty = true;
        #ifdef ENABLE_LOG_APP
            PRNT::_print(PRNT::formatMSG("%32s : LED selection changed, cache marked dirty" NL, "SetLed"));
        #endif
    }
}

/**
 * @brief  Handle 'S' / 'SiiVV...' command -- read or write EEPROM settings.
 *
 * @param  buff  Command buffer. len==1: send all settings back.
 *               len > 1: series of 4-char groups (2-digit index + 2-digit value, hex).
 * @param  len   1 = request; multiple of 4 + 1 = write one or more settings.
 *
 * After writing, enables EE_HB_DUAL_COLOR if EE_TV_RANDOM_COLOR_START is set to 2.
 * If EE_DIF_EFFECT or any EE_DIF_MODE_* setting was written and a source is currently
 * active (DIF::ActiveModeSetting() != 0xFF), pushes the new mode/effect to the diffuser
 * immediately via DIF::TurnOn() rather than waiting for the next auto-on trigger --
 * the mode value used is whichever EE_DIF_MODE_* matches the active source, via
 * DIF::ActiveModeSetting().
 * Schedules an EEPROM save via EE::WriteTime().
 */
void cmdSettings(char *buff, int len) {
	if (len == 1) {
		// Update settings
		updSettings();

		// Update status
		updStatus("APP::cmdSettings"); // *

		return;
	}
	
	if ((len -1) % 4 != 0) {
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "APP_Settings", len));
		return;
	}

	bool difSettingChanged = false;                                     // Tracks EE_DIF_EFFECT/MODE_* writes - State
	bool anyRejected       = false;                                     // >=1 pair skipped (bad index/length/range) - State

	// * Settings
	for (int i = 1; i < len; i += 4) {
		if (i + 3 >= len) {
			anyRejected = true;
			// * LOG
			PRNT::_print(PRNT::formatMSG("%32s ! invalid length for setting pair at index (%d)" NL, "APP_Settings", i));
			break;
		}

		// Fast direct hex parsing (replaces 2x strtol - 3x faster)
		uint8_t item  = HexByte(&buff[i]);
		uint8_t value = HexByte(&buff[i+2]);

		if (item >= EE_MEM_X) {
			anyRejected = true;
			// * LOG
			PRNT::_print(PRNT::formatMSG("%32s ! invalid item index (%d)" NL, "APP_Settings", item));
			continue;
		}

		// * Range validation for effect index settings -- ceiling derived from handler arrays
		bool rejected = false;
		switch (item) {
			case EE_TV_ON_EFF:     if (value >= TV_ON_HANDLERS_COUNT)     rejected = true; break; // Logic
			case EE_TV_OFF_EFF:    if (value >= TV_OFF_HANDLERS_COUNT)    rejected = true; break; // Logic
			case EE_TV_ON_HB_EFF:  if (value >= HB_ON_HANDLERS_COUNT)     rejected = true; break; // Logic
			case EE_MOTION_ON_EFF: if (value >= MOTION_ON_HANDLERS_COUNT) rejected = true; break; // Logic
			case EE_HB_EFFECT:     if (value > HB_EFFECT_HANDLERS_COUNT)  rejected = true; break; // 1-based, 0=off - Logic
			case EE_DIF_EFFECT:    if (value > DIF_EFFECT_COUNT)         rejected = true; break; // 0=static, 1-4=animated - Logic
			case EE_DIF_BRIGHTNESS: if (value > APP_BRIGHT)              rejected = true; break; // 0-APP_BRIGHT range - Logic
			default: break;
		}
		if (rejected) {
			anyRejected = true;
			PRNT::_print(PRNT::formatMSG("%32s ! setting [%s] value [%d] out of range, rejected" NL,
				"APP_Settings", EE::getName(item), value));
			continue;
		}

		EE::Set(item, value);

		// * LOG
		if (item == EE_DIF_EFFECT) {                                        // Show effect name instead of raw index - Logic
			PRNT::_print(PRNT::formatMSG("%~32s # set [%s] to [%s]" NL, "APP_Settings", EE::getName(item), DIF::getEffectName(value)));
		} else {
			PRNT::_print(PRNT::formatMSG("%~32s # set [%s] to [%d]" NL, "APP_Settings", EE::getName(item), value));
		}

		// Reset HB effect parameters when effect changes
		if (item == EE_HB_EFFECT) {
			HB::EndTask();                                                  // Kill current effect task
			HB::StartEffect(true, true, false);                             // Restart with reset=true to clear Phase/ParamA/ParamB
			PRNT::_print(PRNT::formatMSG("%~32s # HB effect changed, restarted with reset" NL, "APP_Settings"));
		}

        // If EE_TV_RANDOM_COLOR_START
        if (item == EE_TV_RANDOM_COLOR_START && value == 2) { // If TV Random Color is set to 2 (which means enabled)
            EE::Set(EE_HB_DUAL_COLOR, 1); // Enable HB Dual Color when TV Random Color is set
            difSettingChanged = true;    // Dual color just changed too -- flag for live push below - State
        
            // * LOG
            PRNT::_print(PRNT::formatMSG("%~32s # TV Random Color enabled, HB Dual Color also enabled" NL, "APP_Settings", item, value));
        }

        if (item == EE_DIF_EFFECT || item == EE_DIF_MODE_TV || item == EE_DIF_MODE_MOTION
                || item == EE_DIF_MODE_UDPRAW || item == EE_DIF_MODE_AMBIENT
                || item == EE_DIF_IDLE_MODE || item == EE_HB_DUAL_COLOR
                || item == EE_DIF_BRIGHTNESS || item == EE_DIF_SPEED) { // Diffuser effect/mode/dual-color/brightness/speed setting - Logic
            difSettingChanged = true;                                   // Flag for live push below - State
        }
	}

	if (anyRejected) APP::State.LastResult = APP_ACK_REJECTED;                  // one or more items skipped; app should re-read settings - Sync

	// Diffuser effect/mode setting changed -- apply live if a source is currently active,
	// instead of waiting for the next DIF::AutoOn() trigger (TV/motion/UDPRAW/AM start).
	if (difSettingChanged) DIF::PushLiveIfActive();

	// EEPROM Update
	EE::WriteTime();

}

/**
 * @brief  Handle 'A0' / 'A1' command -- toggle Ambient Mode off or on.
 *
 * @param  buff  Command buffer. buff[1] = '0' (off) or '1' (on).
 * @param  len   Must equal 2.
 *
 * Ambient Mode ON conditions: UDPRAW must be off, BED pin must be inactive,
 * TV must be off. On success: disables Motion and random colour, launches
 * T_AMBIENT_MODE_ON.
 * Ambient Mode OFF: resets TV, Motion, AM state and turns all LEDs off.
 * Aborts if UDPRAW is running.
 */
void cmdAmbientMode(char *buff, int len) {
	// 0 - turn off, 1 - turn on
	if (UDPRAW::State.Status) {
		APP::State.LastResult = APP_ACK_BLOCKED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%~32s # UDPRAW active, command ignored" NL, "APP_AmbientMode"));

		return;
	}

	if (len == 2) {
		if (buff[1] == '1') { // turn on ambient mode
			if (!MOTION::PinStatus(MOTION_PIN_BED)) {
				if (!TV::State.Status) {
					// AM Status
					APP::Am.Status = true;

					// * disable motion
					MOTION::State.Status = motOFF;

					// * disable random color
					LED::setRandomColor(false);

					// * Task 
					TASK.Phase = 1;
					TSK::KillTasksAvoidLocked("T_AMBIENT_MODE_ON");
                    TSK::AddTask("APP_AmbientMode", "T_AMBIENT_MODE_ON", LED::T_AMBIENT_MODE_ON, TASK_MS, EE::Get(EE_OTHER_BR_CL_DEL), 0, false);

					uint16_t sumR = 0, sumG = 0, sumB = 0, litCount = 0; // Accumulate ambient zones - Setup
					for (int i = 0; i < LED_NUM; i++) {
						const CRGB &c = LED::State.AmbientBackgroundColor[i];
						if ((c.r + c.g + c.b) == 0) continue;           // Skip unlit zones - Logic
						sumR += c.r; sumG += c.g; sumB += c.b; litCount++;
					}
					DIF_Colorx amColor  = { 0, 0, 0, 0, 0, 0 };
					const bool haveColor = (litCount > 0);
					if (haveColor) {
						const uint8_t avgR = sumR / litCount, avgG = sumG / litCount, avgB = sumB / litCount;
						amColor = DIF_Colorx{ avgR, avgG, avgB, avgR, avgG, avgB }; // Match diffuser to ambient colour - Logic
					}

					const uint8_t amMode = EE::Get(EE_DIF_MODE_AMBIENT);  // Ambient source mode (0 = disabled) - Setup
					const bool    difOn  = (DIF::State.Mode >= 1 && DIF::State.Mode <= DIF_MODE_MAX); // Diffuser already running (e.g. from motion) - Logic

					if (amMode != 0) {
						DIF::AutoOn(EE_DIF_MODE_AMBIENT, haveColor ? &amColor : NULL); // Ambient source enabled -- auto-on + colour - Action
					} else if (difOn) {
						// Ambient source is OFF, but a prior source (motion/TV/UDPRAW) left the
						// diffuser running -- hand it over by recolouring to the ambient colour,
						// keeping its current spray mode so the spray behaviour doesn't change.
						DIF::IdleTimerReset();                            // Ambient is now the active source - Action
						DIF::TurnOn(DIF::State.Mode, EE::Get(EE_DIF_EFFECT), haveColor ? &amColor : NULL); // Recolour to ambient - Action
					}
					// else: ambient source disabled and diffuser already off -- leave it off.

					// * LOG
					PRNT::_print(PRNT::formatMSG("%~32s # turn on ambient mode for [%d] minutes" NL, "APP_AmbientMode", EE::Get(EE_OTHER_AMBIENT_MODE_TIME)));

					return;
				} else {
					APP::State.LastResult = APP_ACK_BLOCKED;
					// * LOG
					PRNT::_print(PRNT::formatMSG("%~32s # TV on, command ignored" NL, "APP_AmbientMode", TV::State.Status));
				}
			} else {
				APP::State.LastResult = APP_ACK_BLOCKED;
				// * LOG
				PRNT::_print(PRNT::formatMSG("%~32s # motion active, command ignored [motion:%d bed:%l]" NL, "APP_AmbientMode", MOTION::State.Status, MOTION::PinStatus(MOTION_PIN_BED)));
			}
		} else { // turn off ambient mode
			// AM Status
			APP::Am.Status = false;

			// * Kill the ambient animation task -- otherwise it keeps running in the
			//   background and silently overwrites any manual LC/LD color command
			//   sent after ambient mode is reported OFF. Mirrors the kill-before-start
			//   already done in the ON branch above.
			TSK::KillTasksAvoidLocked("T_AMBIENT_MODE_ON");

			// * preset motion status
			MOTION::State.Status = motON;

			// * preset tv status
			TV::State.Status = false; // TV Status

			// * turn off led's
			LED::setAll(0, 0, 0, 0);

			// Update app status
			updStatus("APP::cmdAmbientMode");

			// Update App Color
			updDeltaColors(); // * (delta: all LEDs set to black)

			DIF::AutoOff();                                              // Diffuser off if all sources idle - Action

			// * LOG
			PRNT::_print(PRNT::formatMSG("%~32s # turn off ambient mode" NL, "APP_AmbientMode"));
		}
	} else {
		APP::State.LastResult = APP_ACK_REJECTED;
		// * LOG
		PRNT::_print(PRNT::formatMSG("%32s ! invalid length (%d)" NL, "APP_AmbientMode", len));
		return;
	}
}

/**
 * @brief  Handle 'Kii' command -- trigger a debug dump for the specified module.
 *
 * @param  buff  Command buffer. buff[1..2] = 2-digit hex debug item index.
 * @param  len   Length of the command (must be >= 2).
 *
 * Calls _Debug() with the parsed item index (see enum __debug in DEF.h).
 */
void cmdDebug(char *buff, int len) {
	// Get Int number from buff using fast hex parsing (replaces strtol - 2x faster)
	int item = HexByte(&buff[1]);
	PRNT::_Debug(item);

	// * LOG
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : debug [%d]" NL, "Debug", item));
	#endif

}

/**
 * @brief  Send the maximum brightness value to the app ("LMvv" packet).
 *
 * Sends APP_BRIGHT as a 2-digit hex value so the app can set its slider range.
 */
void updBrMax() {
	// * LOG
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : update br max to (%d)" NL, "updBrMax", APP_BRIGHT));
	#endif

	// * Send
	termMsgSend(PRNT::formatMSG("LM%X", APP_BRIGHT));
}

/**
 * @brief  Send all EE_SET[] values to the app as a single "S..." packet.
 *
 * Encodes all EE_MEM_X settings as pairs of 2-digit hex values (index + value)
 * concatenated into one UDP packet.
 */
void updSettings() {
    // * LOG
    #ifdef ENABLE_LOG_APP
        PRNT::_print(PRNT::formatMSG("%32s : update settings" NL, "updSettings")); // Keep original log format
    #endif

    // * Prepare
    memset(_sharedTxBuffer, 0, sizeof(_sharedTxBuffer)); // Clear shared buffer
    int tBuffIndex = 0; // Initialize index tracker for the buffer

    // Add 'S' prefix manually to avoid formatMSG in the final Send
    _sharedTxBuffer[tBuffIndex++] = 'S'; // Place 'S' at the start of the buffer

    for (int i = 0; i < EE_MEM_X; i++) {
        // Cache the EEPROM value to avoid duplicate reads
        const int eeValue = EE::Get(i);
        
        // Append to shared buffer starting at tBuffIndex
        int intsWritten = snprintf(&_sharedTxBuffer[tBuffIndex],
                                APP_UDP_MAX_BUFFER_SIZE - tBuffIndex,
                                "%02x%02x", i, eeValue); // Format hex pairs into the buffer

        #ifdef ENABLE_LOG_APP
            PRNT::_print(PRNT::formatMSG("%32s : setting [%d] = %d (%02X)" NL, "updSettings", i, eeValue, eeValue)); // Use cached value
        #endif

        if (intsWritten < 0 || tBuffIndex + intsWritten >= APP_UDP_MAX_BUFFER_SIZE) {
            // Handle buffer overflow or snprintf error
            #ifdef ENABLE_LOG_APP
                PRNT::_print(PRNT::formatMSG("%32s : buffer overflow" NL, "updSettings")); // Log overflow error
            #endif
            break; // Exit loop to prevent memory corruption
        }
        tBuffIndex += intsWritten;  // Update the current index in the buffer
    }

    // * Send
    termMsgSend(_sharedTxBuffer); // Send the buffer directly without wrapping in formatMSG
}

/**
 * @brief  Force synchronize ALL LED colors to the app (full sync using delta mechanism).
 *
 * Marks all LEDs as changed, then delegates to updDeltaColors() for
 * transmission. This ensures a guaranteed full sync while reusing the efficient
 * delta buffer-building and sending logic.
 *
 * Use this for:
 *   - Initial connection sync (Connected)
 *   - Force refresh when needed
 *   - Explicit color info requests from phone ('LC' command)
 *
 * More efficient than duplicating buffer logic; sends via delta mechanism.
 *
 * @note  Call this INSTEAD of updDeltaColors() when you need a
 *        guaranteed full sync. During normal animation, use delta updates directly.
 */
void updColors_Force() {
    #ifdef ENABLE_LOG_APP
        PRNT::_print(PRNT::formatMSG("%32s : mark all [%d LEDs] as changed for force sync" NL, "APP_Update_Colors_Force", LED_NUM_TOTAL));
    #endif

    // * Mark ALL LEDs as changed (up to LED_NUM only, not HB section)
    LED::MarkChangedRange(0, LED_NUM - 1);

    // * Full sync is a one-shot the app is blocking on (welcome / 'LC' request),
    //   so flush immediately and ignore the FPS gate. flushColorSync() still
    //   chunks across frames if the snapshot won't fit one packet.
    flushColorSync(true);
}

/**
 * @brief  Send only CHANGED LEDs to the app (delta update -- efficient).
 *
 * Scans LED::State.Changed bitset and transmits only LEDs that have changed since
 * the last sync. Dramatically reduces network traffic:
 *   - Static display: 95%+ reduction (just header)
 *   - Animations: 70-80% reduction (only moving/fading LEDs)
 *
 * Clears LED::State.Changed after transmission. Uses same "LC..." format as full update.
 * If delta mode is disabled or no changes detected, silently returns.
 *
 * @note  Call this INSTEAD of APP_Update_Colors(LED_NUM) when animating or
 *        when many operations happen between app syncs. Reduces WiFi overhead.
 */
void updDeltaColors() {
    // BATCHING: the ~80 animation call sites used to each build+send a packet,
    // so one frame could fire several partial 'LC' packets. Now they only flag a
    // pending sync; drainColorSync() coalesces every mark made this frame into a
    // single 'LK' packet, gated to the LED FPS so we never send faster than the
    // strip actually updates. Cheap enough to leave every call site untouched.
    APP::State.ColorSyncPending = true;
}

/**
 * @brief  Loop hook -- flush one coalesced colour sync, at most once per FPS period.
 *
 * Rate-matched to the strip via getFpsLimitMs() (the same cap gating
 * LED::Refresh), so bursts of mid-animation marks collapse into one packet.
 * @param  now  TimeNow snapshot from loop().
 */
void drainColorSync(uint32_t now) {
    if (!APP::State.ColorSyncPending) return;                            // Nothing owed - Logic
    if ((uint32_t)(now - APP::State.ColorSyncLast) < LED::getFpsLimitMs()) return; // FPS gate - Logic
    flushColorSync(false);
}

/**
 * @brief  Build + send ONE 'LK' compressed colour delta from the LED::Changed[] set.
 *
 * Wire is raw binary (see LK_* in _DEF.h): contiguous equal-colour runs become
 * a 5-byte FILL, scattered pixels a 4-byte-each SETN batch. Each LED's dirty bit
 * is cleared ONLY as it is written into the packet -- so if the snapshot doesn't
 * fit one datagram, the un-shipped LEDs stay dirty and drain on the next frame
 * (no silent loss, unlike the old blanket ClearChanges()).
 *
 * @param  force  true = ignore the FPS gate / pending flag (welcome/full resync).
 */
void flushColorSync(bool force) {
    if (!LED::State.DeltaMode) { APP::State.ColorSyncPending = false; return; } // Delta off globally
    if (LED::getChangedCount() == 0) { APP::State.ColorSyncPending = false; return; }

    uint8_t *buf = _binTxBuffer;
    int      n   = 0;
    buf[n++] = LK_HDR0;                                                 // 'L'
    buf[n++] = LK_HDR1;                                                 // 'K'

    // Keep room for the largest single record we might still emit (SETN header
    // + one pixel = 5 bytes, FILL = 6). Stop before that margin so we never
    // half-write a record; remaining dirty LEDs carry over to the next frame.
    const int LIMIT = APP_UDP_MAX_BUFFER_SIZE - 6;

    int  i        = 0;
    bool overflow = false;
    int  shipped  = 0;

    while (i < LED_NUM && !overflow) {
        if (!LED::IsChanged(i)) { i++; continue; }

        const CRGB c = LED::State.CurrentColor[i];                      // Run colour - Setup

        // Measure the contiguous run of changed LEDs sharing this exact colour.
        int run = 1;
        while (i + run < LED_NUM && run < 255
               && LED::IsChanged(i + run)
               && LED::State.CurrentColor[i + run].r == c.r
               && LED::State.CurrentColor[i + run].g == c.g
               && LED::State.CurrentColor[i + run].b == c.b) {
            run++;
        }

        if (run >= LK_MIN_RUN) {
            // Worth a FILL: one record paints the whole run.
            if (n + 6 > LIMIT) { overflow = true; break; }
            buf[n++] = LK_OP_FILL;
            buf[n++] = (uint8_t)i;
            buf[n++] = (uint8_t)run;
            buf[n++] = c.r; buf[n++] = c.g; buf[n++] = c.b;
            for (int k = 0; k < run; k++) BIT_CLEAR(LED::State.Changed, i + k); // clear-shipped
            shipped += run;
            i += run;
        } else {
            // Gather a SETN batch: scattered / short runs, 4 bytes each. Reserve
            // the count byte, fill pixels until the buffer margin, backpatch count.
            if (n + 5 > LIMIT) { overflow = true; break; }
            int cntPos = n;
            buf[n++] = LK_OP_SETN;
            buf[n++] = 0;                                              // count placeholder - Setup
            uint8_t cnt = 0;
            while (i < LED_NUM && cnt < 255 && n + 4 <= LIMIT) {
                if (!LED::IsChanged(i)) { i++; continue; }
                // Only single/short-run pixels belong in SETN; if a long same-
                // colour run starts here, break so the FILL branch handles it.
                const CRGB p = LED::State.CurrentColor[i];
                int look = 1;
                while (i + look < LED_NUM && look < LK_MIN_RUN
                       && LED::IsChanged(i + look)
                       && LED::State.CurrentColor[i + look].r == p.r
                       && LED::State.CurrentColor[i + look].g == p.g
                       && LED::State.CurrentColor[i + look].b == p.b) look++;
                if (look >= LK_MIN_RUN) break;                          // hand off to FILL

                buf[n++] = (uint8_t)i;
                buf[n++] = p.r; buf[n++] = p.g; buf[n++] = p.b;
                BIT_CLEAR(LED::State.Changed, i);                       // clear-shipped
                cnt++; shipped++; i++;
            }
            buf[cntPos + 1] = cnt;                                      // backpatch - Action
            if (cnt == 0) n = cntPos;                                   // nothing landed -> drop empty record
        }
    }

    // Nothing actually encoded (e.g. everything deferred by overflow margin on a
    // pathological frame) -- keep the pending flag so we retry next frame.
    if (n <= 2) { if (!force) APP::State.ColorSyncPending = force; return; }

    #ifdef ENABLE_LOG_APP
        PRNT::_print(PRNT::formatMSG("%32s : LK sync [%d LEDs] [%d bytes]%s" NL,
            "flushColorSync", shipped, n, overflow ? " (chunked, more next frame)" : ""));
    #endif

    termMsgSendBin(_binTxBuffer, (size_t)n);
    APP::State.ColorSyncLast = TimeNow;

    // Pending stays set only if we chunked (dirty bits remain) -- forces the
    // remainder out next frame. Force flushes loop here until fully drained.
    APP::State.ColorSyncPending = overflow;
    if (force && overflow) flushColorSync(true);
}

/**
 * @brief  Report link quality -- "w" + |RSSI| + WiFi state (both 2-hex).
 *
 * Dirty-gated on the signal bucket + wifi state, so RSSI jitter never spams the
 * app. The app draws bars from the raw |RSSI|. RTC/clock ('wC') was dropped.
 * @return true if a packet was sent this call.
 */
bool updLink() {
	const bool ok   = (WiFi.status() == WL_CONNECTED);                  // Link up? - Logic
	const int  rssi = ok ? (int)WiFi.RSSI() : 0;                        // dBm (<=0) - Action
	uint8_t    mag  = (uint8_t)((rssi < 0) ? -rssi : rssi);             // Magnitude - Logic
	if (!ok) mag = 0;                                                   // Down = 0 - Logic

	const uint8_t bar = H_RssiBars(mag);                             // Bucket - Logic
	if (bar == _txW_bar && (int)NET_WifiSt == _txW_wf) return false;   // No change - Logic
	_txW_bar = bar; _txW_wf = NET_WifiSt;                              // Cache - State
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : rssi -%d dBm  wifi %d  bars %d" NL, "updLink", mag, NET_WifiSt, bar));
	#endif
	termMsgSend(PRNT::formatMSG("w%02X%02X", mag, NET_WifiSt));                 // Send - Output
	return true;
}

/**
 * @brief  Report the current lux level -- "M" + 4-hex classified level (1..5).
 * @note   Dirty-gated; the rolling average ('mA') was dropped.
 * @return true if a packet was sent this call.
 */
bool updLux() {
	if ((int)LISENS::State.Lux == _txM_lux) return false;                     // No change - Logic
	_txM_lux = LISENS::State.Lux;                                             // Cache - State
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : lux %d" NL, "APP_Update_Lux", LISENS::State.Lux));
	#endif
	termMsgSend(PRNT::formatMSG("M%4X", LISENS::State.Lux));                           // Send - Output
	return true;
}

/**
 * @brief  Report the active test mode ("@mm", 2-hex). Sent on entry and exit.
 */
void updTestMode() {
	termMsgSend(PRNT::formatMSG("@%02X", (uint8_t)TestMode));                   // fixed 2-hex, app reads [1:3]
}

/**
 * @brief  Report active fault flags ("f" + 4-hex mask) -- TRANSITION ONLY.
 *
 * Fires only when the mask changes (a fault raised or cleared), so the app's
 * Term console surfaces faults exactly when something is/was wrong. Deliberately
 * NOT part of the welcome/resync snapshot.
 * @return true if a packet was sent this call.
 */
bool updFaults() {
	uint16_t f = 0;

	if (WiFi.status() != WL_CONNECTED || NET_WifiSt != netWifiOK) f |= APP_FAULT_WIFI;
	if (RTC_Status != rtcOK || NET::RTC_EpochUTC() == 0)               f |= APP_FAULT_NTP;
	if (APP::State.SaveFailed)                                           f |= APP_FAULT_EEPROM;
	if (isnan(BME::State.Temperature) || isnan(BME::State.Humidity))      f |= APP_FAULT_BME;
	if (DIF::State.Mode == DIF_MODE_NO_RESPONSE)                         f |= APP_FAULT_DIF_NORESP;
	if (DIF::State.Mode == DIF_MODE_OUT_OF_WATER)                        f |= APP_FAULT_DIF_NOWATER;

	if (_txF_init && f == _txF_mask) return false;                    // No transition - Logic
	_txF_init = true; _txF_mask = f;                                  // Cache - State
	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : faults %4X" NL, "updFaults", f));
	#endif
	termMsgSend(PRNT::formatMSG("f%4X", f));                                   // Term prints raised/cleared - Output
	return true;
}

/**
 * @brief  Push every dirty-gated app group. Called from ~all state changes; the
 *         welcome/resume path calls TxCacheReset() first to force a full sync.
 *
 * Each group sends only when its own value(s) changed, so nothing re-sends the
 * same value. Groups: s (core), H (climate), E (enable), p (parfum, only while
 * active), M (lux), w (link), f (faults, transition-only).
 *
 * @param  by  Short tag naming the caller/trigger (e.g. "APP_Settings"),
 *             passed by each call site and shown in the ENABLE_LOG_APP trace so
 *             every push is traceable to its cause. Logged only when a group was
 *             actually sent.
 *
 * Removed vs earlier builds: 'd' diffuser detail, 'c' countdowns, 'x' lux
 * factor, 't' device time, 'V' identity -- all parsed-but-unused by the app.
 */
void updStatus(const char *by) {
	bool sent = false;                                                 // any group actually pushed?

	// -- s : core status (TV, Motion, UDPRAW, AmbientMode, Diffuser summary) --
	const uint8_t tv = (TV::State.Status) ? 1 : 0;
	const uint8_t mo = MOTION::State.Status;
	const uint8_t ur = (UDPRAW::State.Status) ? 1 : 0;
	const uint8_t am = (APP::Am.Status) ? 1 : ((!UDPRAW::State.Status && (!MOTION::PinStatus(MOTION_PIN_BED)) && !TV::State.Status && LED::State.Enabled) ? 2 : 0); // 2 = ready
	const uint8_t ds = DIF::State.Mode == DIF_MODE_NO_RESPONSE ? 3 : (DIF::State.Mode == DIF_MODE_OUT_OF_WATER ? 2 : (DIF::State.ParfumMin > 0 ? 4 : (DIF::State.Mode == 0 ? 0 : 1))); // parfum shown only while reachable

	if (tv!=_txS_tv || mo!=_txS_mo || ur!=_txS_ur || am!=_txS_am || ds!=_txS_ds) {
		_txS_tv=tv; _txS_mo=mo; _txS_ur=ur; _txS_am=am; _txS_ds=ds;
		termMsgSend(PRNT::formatMSG("s%02X%02X%02X%02X%02X", tv, mo, ur, am, ds)); // core status - Output
		sent = true;
	}

	// -- H : climate (temperature, humidity) --
	const uint8_t tp = H_ClampByte(BME::State.Temperature);
	const uint8_t hu = H_ClampByte(BME::State.Humidity);
	if ((int)tp!=_txH_tp || (int)hu!=_txH_hu) {
		_txH_tp=tp; _txH_hu=hu;
		termMsgSend(PRNT::formatMSG("H%02X%02X", tp, hu));                        // climate - Output
		sent = true;
	}

	// -- E : LED enable / disable --
	const uint8_t en = (LED::State.Enabled) ? 1 : 0;
	if ((int)en != _txE_en) {
		_txE_en = en;
		termMsgSend(PRNT::formatMSG("E%1X", en));                                 // enable - Output
		sent = true;
	}

	// -- p : parfum minutes -- only while active (plus the final drop to 0) --
	if ((int)DIF::State.ParfumMin != _txP_min && (DIF::State.ParfumMin > 0 || _txP_min > 0)) {
		termMsgSend(PRNT::formatMSG("p%4X", DIF::State.ParfumMin));                      // parfum time left - Output
		sent = true;
	}
	_txP_min = DIF::State.ParfumMin;

	// -- u : diffuser usage/refill stats -- only while reachable, only when changed --
	if (DIF::State.Mode != DIF_MODE_NO_RESPONSE &&
	    ((int)DIF::State.UsageAccumMin != _txU_accum || (int)DIF::State.UsageAvgMin != _txU_avg ||
	     (int)DIF::State.UsageRefillCount != _txU_cnt || (int)DIF::State.UsageTotalRefills != _txU_tot)) {
		termMsgSend(PRNT::formatMSG("u%4X%4X%2X%4X", DIF::State.UsageAccumMin, DIF::State.UsageAvgMin,
		            DIF::State.UsageRefillCount, DIF::State.UsageTotalRefills));  // usage/refill stats - Output
		sent = true;
	}
	_txU_accum = DIF::State.UsageAccumMin; _txU_avg = DIF::State.UsageAvgMin;
	_txU_cnt   = DIF::State.UsageRefillCount; _txU_tot = DIF::State.UsageTotalRefills;

	// -- M / w / f : each self-gates internally and reports if it pushed --
	if (updLux())    sent = true;
	if (updLink())   sent = true;
	if (updFaults()) sent = true;

	(void)by;                                                          // referenced only by the log below

	// * LOG -- only when we actually pushed something, tagged with the caller
	#ifdef ENABLE_LOG_APP
	    if (sent)
	        PRNT::_print(PRNT::formatMSG("%32s : by %s : status TV:%s MOT:%s UDPRAW:%s AM:%s LED:%s DIF:%s" NL, "APP_Update_Status", by,
	            (TV::State.Status) ? "ON" : "OFF",
	            (MOTION::State.Status == motOFF) ? "OFF" : (MOTION::State.Status == motON) ? "ON" : (MOTION::State.Status == motCOM) ? "COM" : (MOTION::State.Status == motBED) ? "BED" : (MOTION::State.Status == motAUTOOFF) ? "AUTOOFF" : "UNK",
	            (UDPRAW::State.Status) ? "ON" : "OFF", (APP::Am.Status) ? "ON" : "OFF",
	            (LED::State.Enabled) ? "EN" : "DIS", DIF::getModeName(DIF::State.Mode)));
	#endif
}

/**
 * @brief  Periodic task -- detect an inactive UDP peer and drop its cached IP.
 *
 * Resets APP_RECV_IP to INADDR_NONE if no UDP packet has been received for
 * ARD_TIMEOUT ms, so a stale IP doesn't keep swallowing UDP sends. Gated on
 * LastUdpReceive (not the shared LastReceive) so an app talking over MQTT
 * only -- off the local subnet, cached IP unreachable -- still expires it;
 * otherwise MQTT traffic would keep the dead UDP address alive forever.
 *
 * @param  tID  Task handle (unused).
 *
 * @note   Registered as a locked repeating task in setup(). Do not call directly.
 */
void T_WatchdogCheck(taskId_t tID) {
	if (APP_RECV_IP != INADDR_NONE && APP_RECV_IP != IPAddress(0, 0, 0, 0)) {
		if ((APP::Ard.LastUdpReceive + ARD_TIMEOUT) <= TimeNow) {
			APP_RECV_IP = INADDR_NONE; // Reset IP

			// * LOG
			#ifdef ENABLE_LOG_APP
				PRNT::_print(PRNT::formatMSG("%32s : UDP peer timeout, resetting recv IP" NL, "WatchdogCheck"));
			#endif
		}
	}
}

/**
 * @brief  Keep-alive task -- added on the app's welcome, killed on suspend.
 *
 * Every inbound app packet calls ResetTime() on this task, so it only fires
 * after KeepAlive_MS of true silence. When it does fire:
 *   - still within the give-up window  -> send a "k" to poke the quiet app;
 *   - silent past APP_ALIVE_TIMEOUT_MS -> suspend (Send goes fully silent on
 *     UDP + MQTT) and kill this task. The board then waits for the app's next
 *     welcome ('Z' -> cmdConnected) to re-add the task and resume.
 *
 * @param  tID  This task's id (used to self-kill on give-up).
 * @note   Added by cmdConnected(); never registered permanently in setup().
 */
void T_KeepAlive(taskId_t tID) {
	// App gone quiet past the give-up window -> suspend and remove ourselves.
	if ((uint32_t)(TimeNow - APP::Ard.LastReceive) > APP_ALIVE_TIMEOUT_MS) {
		APP::State.Suspended   = true;                                        // Stop all TX until next welcome - State
		APP::State.KeepAliveID = TASK_ID_NONE;                               // Will be re-added on welcome - State
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : app silent > %lu ms, suspending + killing task" NL, "T_KeepAlive", (unsigned long)APP_ALIVE_TIMEOUT_MS));
		#endif
		TSK::KillID(tID, "T_KeepAlive");                            // Remove self - Action
		return;
	}

	termMsgSend("k");                                                     // quiet-but-alive -> poke - Output
}

/**
 * @brief  Auto-cancel task -- resets TestMode to _testmode_none after TESTMODE_DURATION.
 *
 * Runs the per-mode exit cleanup for the mode being left (mirrors
 * APP::cmdTestMode(): _testmode_dif -> DIF::AutoOff(), _testmode_lux ->
 * LISENS::ResetTime()), then sets TestMode = _testmode_none, clears
 * TestMode_tID, and self-kills its task handle.
 *
 * @param  taskId  Task handle used to self-kill.
 *
 * @note   Registered by APP::cmdTestMode() whenever a non-none test mode is activated.
 */
void T_END_TEST_MODE(taskId_t taskId) {
    // * LOG
    #ifdef ENABLE_LOG_TASK
        PRNT::_print(PRNT::formatMSG("%32s : test mode timeout - resetting to none" NL, "T_END_TEST_MODE"));
    #endif

    // Per-mode exit cleanup before dropping back to none
    switch (TestMode) {
        case _testmode_udpraw: UDPRAW::End(false); break;                      // Simulated stream must not outlive the test - Action
        case _testmode_dif: DIF::AutoOff();      break;                      // Off unless a real source drives it - Action
        case _testmode_lux:
            LISENS::ResetTime();                                             // Fresh sample window - sensor resumes - Action
            // * LOG
            #ifdef ENABLE_LOG_LUX
                PRNT::_print(PRNT::formatMSG("%32s : lux test released - sensor resumes from level [%d]" NL, "T_END_TEST_MODE", LISENS::State.Lux));
            #endif
            break;
        default:                                break;
    }

    // Set TestMode to none and kill this task
    APP::termMsgLog(APP_LOG_INF, APP_SRC_TEST, "APP", "T_END_TEST_MODE", "Test mode [%s] expired after [%d] s",
        _TestModeName(TestMode), TESTMODE_DURATION / 1000);

    TestMode     = _testmode_none;
    APP::updTestMode();                                              // Mode expired - tell the app - Sync                                          // Reset test mode - State
    TestMode_tID = TASK_ID_NONE;                                            // Clear auto-cancel handle - State

    // TaskKill
    TSK::KillID(taskId, "T_END_TEST_MODE");                                // Terminate self - Logic
}


// Shared static buffer to prevent stack overflow during nested calls
// Used by updSettings(), updDeltaColors(), termMsgSend(), and MQTT::Publish()
char _sharedTxBuffer[APP_UDP_MAX_BUFFER_SIZE]; // static removed: now forward-declared extern in DEF.h
uint8_t _binTxBuffer[APP_UDP_MAX_BUFFER_SIZE]; // binary staging for the 'LK' compressed colour packet (extern in DEF.h)

/**
 * @brief  Calculate the current amount of free heap/stack RAM in bytes.
 *
 * Uses the difference between the current stack pointer and the heap end
 * (returned by sbrk(0)) as an estimate of available RAM.
 *
 * @return Approximate free RAM in bytes.
 */
int getFreeRam() {
	char stackPointer;
	int freeRam = &stackPointer - sbrk(0); // Difference between stack and heap

	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : free RAM [%d] bytes" NL, "getFreeRam", freeRam));
	#endif

	return freeRam;
}


/**
 * @brief  Fit a sensor reading into one 2-hex-char status field.
 * @param  v  Raw sensor value.
 * @return 0..255, clamped; a NaN reading collapses to 0.
 */
uint8_t H_ClampByte(float v) {
	if (isnan(v)) return 0;                                             // Failed sensor - Logic
	if (v <= 0)   return 0;                                             // Below range - Logic
	if (v >= 255) return 255;                                           // Above range - Logic
	return (uint8_t)v;                                                  // In range - Action
}

/**
 * @brief  Handle 'LD...' / 'Ld...' command -- set a two-colour zone split.
 *
 * @param  buff      Command buffer. len==2: send current dual colours back.
 *                   len==14: 2-digit hex Left(RRGGBB) + Right(RRGGBB).
 * @param  len       2 = info request; 14 = full dual colour assignment.
 * @param  shakeMode If true, launches LED::T_SHAKE_DUAL_COLOR(chaotic strobe first).
 *                   If false, launches LED::T_DUAL_COLOR(clean fade transition).
 *
 * Stores left colour in LED::State.TargetColor[0] and right in LED::State.TargetColor[1].
 * Enables EE_HB_DUAL_COLOR. Aborts if UDPRAW or Ambient Mode is active.
 */

/**
 * @brief  Parse a 6-character RGB hex string into a 24-bit color value.
 *
 * Expects the string to be in the form RRGGBB.
 */
static uint32_t H_ParseHexColor(const char *hex) {
    uint32_t value = 0;
    for (int i = 0; i < 6; i++) {
        char c = hex[i];
        uint8_t digit;

        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else {
            digit = 0;
        }

        value = (value << 4) | digit;
    }
    return value;
}

/**
 * @brief  Signal-strength bucket for the link dirty test (also what the app draws).
 * @param  mag  |RSSI| in dBm (0 = link down).
 * @return 0 = down, 1 = weak .. 4 = full.
 */
uint8_t H_RssiBars(uint8_t mag) {
	if (mag == 0)  return 0;                                            // Down - Logic
	if (mag <= 55) return 4;                                            // Strong - Logic
	if (mag <= 65) return 3;                                            // Good   - Logic
	if (mag <= 75) return 2;                                            // Fair   - Logic
	return 1;                                                           // Weak   - Logic
}
/* ------------------------------------------------------------------------ */
/* APP                                                                        */
/* UDP command socket - all handlers - RAM helpers                            */
/* ------------------------------------------------------------------------ */

/**
 * @brief  2-char hex string to byte converter (no strtol overhead).
 *         Used extensively by APP command handlers.
 */
uint8_t HexByte(const char *hex) {
    uint8_t h = (*hex     >= '0' && *hex     <= '9') ? *hex     - '0' :
                (*hex     >= 'A' && *hex     <= 'F') ? *hex     - 'A' + 10 :
                (*hex     >= 'a' && *hex     <= 'f') ? *hex     - 'a' + 10 : 0;
    uint8_t l = (*(hex+1) >= '0' && *(hex+1) <= '9') ? *(hex+1) - '0' :
                (*(hex+1) >= 'A' && *(hex+1) <= 'F') ? *(hex+1) - 'A' + 10 :
                (*(hex+1) >= 'a' && *(hex+1) <= 'f') ? *(hex+1) - 'a' + 10 : 0;
    return (h << 4) | l;
}

/**
 * @brief  1-char hex digit to nibble converter.
 *         Used for single-hex-char command fields (e.g. the parfum
 *         dispense-mode digit in "DpMMMME").
 */
uint8_t HexNibble(char c) {
    return (c >= '0' && c <= '9') ? c - '0' :
           (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
           (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
}

/**
 * @brief  Confirm a scheduled EEPROM write actually landed ("e" + 2-hex result).
 * @param  result  0 = written, non-zero = write did not complete.
 */
void Notify_Saved(uint8_t result) {
	APP::State.SaveFailed = (result != 0);                                    // Feeds APP_FAULT_EEPROM - State
	termMsgSend(PRNT::formatMSG("e%02X", result));                              // fixed 2-hex, app reads [1:3]
}


/**
 * @brief  Rebuild the selected-LED cache from the current selection bitmask.
 *
 * Scans all LEDs and stores selected indices in APP::State.SelectedLedCache.
 * Updates APP::State.SelectedCount and clears the cache dirty flag.
 * This avoids repeated full scans when processing selected LED commands.
 */
void RefreshSelectedCache() {
    APP::State.SelectedCount = 0;
    for (int i = 0; i < LED_NUM; i++) {
        if (LED::IsSelected(i)) {
            APP::State.SelectedLedCache[APP::State.SelectedCount++] = i;
        }
    }
    APP::State.SelectedCacheDirty = false;
}


/**
 * @brief  Emit a per-command ACK back to the phone app: "#SSR".
 * @param  seq     Sequence id echoed from the incoming "#SS<cmd>" command.
 * @param  result  APP_AckResult code (0..6).
 */
void termMsgAck(uint8_t seq, uint8_t result) {
	char msg[6];                         // '#' + 2 hex seq + 1 hex result + NUL
	snprintf(msg, sizeof(msg), "%c%02x%1x", APP_ACK_CHAR, seq, result & 0x0F);
	termMsgSend(msg);
}

/**
 * @brief  Send a C-string as a UDP packet to the connected phone app.
 *
 * @param  msg  Null-terminated string to transmit. NULL is safely ignored.
 *
 * Sends to APP_RECV_IP:APP_UDP_PORT. Retries beginPacket() and endPacket()
 * up to APP_UDP_TIMEOUT ms each. Silently drops the packet if:
 *   - WiFi is not connected
 *   - APP_RECV_IP is 0.0.0.0 (no phone connected yet)
 *   - The UDP socket times out
 */
/**
 * @brief  Send one Term-console log line to the phone app.
 *
 * Wire format (no spaces): '*' + level(1 hex) + source(2 hex) + seq(2 hex) + text.
 * The app colours the line from @p level, tags it from @p source, and renders
 * every [bracketed] token as a value chip - so the text itself carries no
 * colour markup any more.
 *
 * @param  level   APP_LogLevel - ERR/WRN/INF/DBG (SEC/GAP are emitted by the
 *                 helpers below, not by hand).
 * @param  source  APP_LogSource - which module is speaking.
 * @param  ns      Namespace of the calling function (e.g., "APP", "LED").
 * @param  func    Name of the calling function (e.g., "Setup", "Loop").
 * @param  format  formatMSG()-style format string. Wrap every value the reader
 *                 actually needs in [ ] so it becomes a chip.
 * @param  ...     Variadic arguments matching the format specifiers.
 *
 * @note   seq increments per packet and wraps at 0xFF; the app uses it to spot
 *         datagrams lost in flight and prints a gap marker in their place.
 */
void termMsgLog(uint8_t level, uint8_t source, const char* ns, const char* func, const char *format, ...) {
	static char _APP_LOG_[128];               // Envelope + payload - Setup (reduced from 247)
	static char _APP_CALLER_[64];                                       // Caller info buffer - Setup

	// Build caller info prefix: [namespace::functionName]
	snprintf(_APP_CALLER_, sizeof(_APP_CALLER_), "[%s::%s] ", ns, func);

	snprintf(_APP_LOG_, sizeof(_APP_LOG_), "%c%1x%02x%02x",             // Header - Action
		APP_LOG_CHAR, level & 0x0F, source & 0xFF, APP::State.LogSeq);

	// Prepend caller info to payload
	strncat(_APP_LOG_, _APP_CALLER_, sizeof(_APP_LOG_) - strlen(_APP_LOG_) - 1);

	va_list args;                                                       // Argument list - Setup
	va_start(args, format);                                             // Start processing - Setup
	strncat(_APP_LOG_, PRNT::vformatMSG(format, args),                        // Payload - Action
		sizeof(_APP_LOG_) - strlen(_APP_LOG_) - 1);
	va_end(args);                                                       // Cleanup - Setup

	APP::State.LogSeq++;                                                       // Roll seq - State
	termMsgSend(_APP_LOG_);                                                // Transmit - Action
}

/**
 * @brief  Emit a blank spacer record - the app renders it as vertical gap.
 *
 * Replaces the old termMsgSend(formatMSG("*" NL)) spacing lines: a GAP record
 * carries no text, so the app can collapse repeats instead of stacking
 * empty rows.
 *
 * @param  ns      Namespace of the calling function (e.g., "APP", "LED").
 * @param  func    Name of the calling function (e.g., "Setup", "Loop").
 */
void termMsgLogGap(const char* ns, const char* func) {
	termMsgLog(APP_LOG_GAP, APP_SRC_SYS, ns, func, "");                              // Empty payload - Action
}

/**
 * @brief  Emit a section header - the app draws it as a titled divider row.
 *
 * @param  source  APP_LogSource the section belongs to.
 * @param  ns      Namespace of the calling function (e.g., "APP", "LED").
 * @param  func    Name of the calling function (e.g., "Setup", "Loop").
 * @param  format  formatMSG()-style title. Keep it short, it is a heading.
 * @param  ...     Variadic arguments matching the format specifiers.
 */
void termMsgLogSection(uint8_t source, const char* ns, const char* func, const char *format, ...) {
	static char _APP_SEC_[128];                             // Title buffer - Setup (reduced from 241)

	va_list args;                                                       // Argument list - Setup
	va_start(args, format);                                             // Start processing - Setup
	strncpy(_APP_SEC_, PRNT::vformatMSG(format, args), sizeof(_APP_SEC_) - 1);// Render title - Action
	_APP_SEC_[sizeof(_APP_SEC_) - 1] = '\0';                            // Null-terminate - Setup
	va_end(args);                                                       // Cleanup - Setup

	termMsgLog(APP_LOG_SEC, source, ns, func, "%s", _APP_SEC_);                      // Send as SEC record - Action
}

void termMsgSend(const char *msg) {
	if (msg == NULL) { 
		PRNT::_print(PRNT::formatMSG("%32s ! null message" NL, "APP_Send"));
		return;
	}

	// * Suspend gate -- while the app is presumed dead we go fully silent on both
	//   transports (UDP + the MQTT mirror below), including the 'k' keep-alive.
	//   We resume only when the app speaks again (its next welcome); see Exec.
	if (APP::State.Suspended) return;

	if (!NET::IsConnected()) {
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : WiFi not connected" NL, "APP_Send"));
		#endif

		return;
	}

	// Snapshot msg into a local, stable buffer before MQTT::Publish(): when
	// ENABLE_LOG_MQTT is on, Publish() logs via PRNT::formatMSG(), which writes
	// into the SAME shared static buffer that msg may already point into (any
	// caller that builds its packet with a direct formatMSG() call, bypassing
	// termMsgLog()'s separately-buffered _APP_LOG_). Without this snapshot, the
	// APP_UDP.write(msg) below (after Publish() returns) sends that overwritten
	// debug text instead of the real packet -- this is the source of the
	// "MQTT_Publish : sent [...] size [...]" garbage the app logs as
	// "SIZE NOT VALID" on the local UDP path.
	strncpy(_sharedTxBuffer, msg, sizeof(_sharedTxBuffer) - 1);
	_sharedTxBuffer[sizeof(_sharedTxBuffer) - 1] = '\0';
	msg = _sharedTxBuffer;

	// Single active transport -- send on whichever one the last 'Z' picked,
	// never both (see APP::State.ActiveProtocol / cmdConnected()).
	if (APP::State.ActiveProtocol == APP_TRANSPORT_MQTT) {
		MQTT::Publish(msg);
		return;
	}

	if (APP_RECV_IP == INADDR_NONE || APP_RECV_IP == IPAddress(0, 0, 0, 0)) {
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : IP not set" NL, "APP_Send"));
		#endif

		return;
	}

	unsigned long startTime = millis();
	bool started = false;

	// Try to beginPacket() with timeout


	//
	while (millis() - startTime < APP_UDP_TIMEOUT) {
		if (APP_UDP.beginPacket(APP_RECV_IP, APP_UDP_PORT)) {
			started = true;
			break;
		}
		// Removed delay(1) to avoid blocking CPU
	}

	if (!started) {
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : beginPacket() timeout after %lu ms" NL, "APP_Send", APP_UDP_TIMEOUT));
		#endif
		return;
	}

	APP_UDP.write(msg);
	
	// end packet with timeout
    bool sent = false;
    unsigned long endStart = millis();
    while (millis() - endStart < APP_UDP_TIMEOUT) {
        int res = APP_UDP.endPacket();
        if (res == 1) {  // success
            sent = true;
            break;
        }
        // Removed delay(1) to avoid blocking CPU
    }

    if (!sent) {
        #ifdef ENABLE_LOG_APP
            PRNT::_print(PRNT::formatMSG("%32s : endPacket() timeout after %lu ms" NL, "APP_Send", APP_UDP_TIMEOUT));
        #endif
        return;
    }

	// VERBOSE, not the base tier: one line per packet, including every LK
	// color-sync flush during an active animation - too frequent for lite.
	#ifdef ENABLE_LOG_APP_VERBOSE
		PRNT::_print(PRNT::formatMSG("%32s : sent [%s] size [%d]" NL, "APP_Send", msg, strlen(msg)));
	#endif
}

/**
 * @brief  Send a raw byte payload (length-aware) to the active transport.
 *
 * The binary sibling of termMsgSend(): used by the 'LK' colour packet, whose
 * body can contain 0x00 and bytes >= 0x80, so strlen()/C-string sends would
 * truncate or corrupt it. Same suspend / link / active-protocol gates as the
 * text path. No _sharedTxBuffer snapshot needed -- the caller (flushColorSync)
 * owns a dedicated _binTxBuffer that no logging call reuses.
 *
 * @param  buf  Raw payload (already framed, e.g. 'L''K' + records).
 * @param  len  Payload length in bytes.
 */
void termMsgSendBin(const uint8_t *buf, size_t len) {
	if (buf == NULL || len == 0) return;
	if (APP::State.Suspended)   return;                                 // App presumed dead - Logic
	if (!NET::IsConnected())    return;                                 // No link - Logic

	// MQTT path -- binary overload prepends the sender tag itself.
	if (APP::State.ActiveProtocol == APP_TRANSPORT_MQTT) {
		MQTT::Publish(buf, len);
		return;
	}

	// UDP path.
	if (APP_RECV_IP == INADDR_NONE || APP_RECV_IP == IPAddress(0, 0, 0, 0)) return; // No peer - Logic

	unsigned long startTime = millis();
	bool started = false;
	while (millis() - startTime < APP_UDP_TIMEOUT) {
		if (APP_UDP.beginPacket(APP_RECV_IP, APP_UDP_PORT)) { started = true; break; }
	}
	if (!started) {
		#ifdef ENABLE_LOG_APP
			PRNT::_print(PRNT::formatMSG("%32s : beginPacket() timeout" NL, "APP_SendBin"));
		#endif
		return;
	}

	APP_UDP.write(buf, len);                                            // Raw bytes - Action

	unsigned long endStart = millis();
	while (millis() - endStart < APP_UDP_TIMEOUT) {
		if (APP_UDP.endPacket() == 1) break;
	}

	// VERBOSE, not the base tier - see termMsgSend()'s note above; this is
	// the binary path used by every LK color-sync flush.
	#ifdef ENABLE_LOG_APP_VERBOSE
		PRNT::_print(PRNT::formatMSG("%32s : sent [%d] bytes" NL, "APP_SendBin", (int)len));
	#endif
}

/** @return Human name for a __testmode value. */
const char* _TestModeName(uint8_t m) {
	return (m == _testmode_none)       ? "none"       :
	       (m == _testmode_tvOn)       ? "TV ON"      :
	       (m == _testmode_tvOff)      ? "TV OFF"     :
	       (m == _testmode_udpraw)     ? "UDPRAW"     :
	       (m == _testmode_motionCom)  ? "MOTION COM" :
	       (m == _testmode_motionBed)  ? "MOTION BED" :
	       (m == _testmode_dif)        ? "DIFFUSER"   :
	       (m == _testmode_lux)        ? "LUX"        : "UNKNOWN";
}

/**
 * @brief  Command dispatcher -- parse the first byte and route to the appropriate handler.
 *
 * @param  buff  Null-terminated command string received over UDP.
 * @param  len   Length of the command string (excluding null terminator).
 *
 * Command routing:
 *   'Z' -> cmdConnected()       (welcome/reconnect)
 *   'X' -> cmdEnableDisable()  (toggle LED enable)
 *   'S' -> cmdSettings()        (get/set EEPROM settings)
 *   'A' -> cmdAmbientMode()     (ambient mode on/off)
 *   'K' -> cmdDebug()           (debug dump)
 *   '@' -> cmdTestMode()        (test mode)
 *   'D' + 's' -> DIF::RequestStatus()        (ask diffuser for status)
 *   'D' + 'f' -> DIF::Shutdown()             (shutdown diffuser)
 *   'D' + 'n' -> cmdDiffuserTurnOn()        (parse hex MODE+EFFECT, forward "Dn" to diffuser)
 *   'D' + 'p' -> cmdDiffuserParfum()        (parse 4-hex MINUTES + 1-hex MODE, forward "Dp" parfum command to diffuser)
 *   'L' + 'B' -> cmdChangeBrightness()
 *   'L' + 'C' -> cmdChangeColor()
 *   'L' + 'D' -> cmdChangeDualColor(shakeMode=false)
 *   'L' + 'd' -> cmdChangeDualColor(shakeMode=true)
 *   'L' + 'O' -> cmdSetLed()
 *
 * Always calls updStatus() at the end.
 */
/* App seq to relay for the next app-originated diffuser send (0xFF = none/autonomous).
   Armed by the diffuser command handlers, consumed by DIF::SendMaybeAck(), and
   reset at the tail of Exec so autonomous sends never relay to a stale seq. */

/**
 * @brief  Display name for an APP_TRANSPORT_* value, for log lines.
 * @param  proto  APP_TRANSPORT_UDP or APP_TRANSPORT_MQTT.
 */
const char* TransportName(uint8_t proto) {
	return proto == APP_TRANSPORT_MQTT ? "MQTT" : "UDP";
}

/* -- App push dirty caches ------------------------------------------------
   Declarations moved to DEF.h (_txS_, _txH_, _txE_, _txM_, _txW_, _txP_, _txF_ groups). */

/**
 * @brief  Arm every dirty-gated group (except faults) to resend on next push.
 * @note   Called before updStatus() on welcome/resume for a full sync.
 */
void TxCacheReset() {
	_txS_tv=_txS_mo=_txS_ur=_txS_am=_txS_ds=-1;                         // core
	_txH_tp=_txH_hu=-1;                                                 // climate
	_txE_en=-1;                                                         // enable
	_txM_lux=-1;                                                        // lux
	_txW_bar=_txW_wf=-1;                                                // link
	_txP_min=-1;                                                        // parfum
	_txU_accum=_txU_avg=_txU_cnt=_txU_tot=-1;                           // dif usage
}



/**
 * @brief  (Re-)open the APP UDP socket on APP_UDP_PORT.
 *
 * Call after any WiFi reconnect to re-bind the port.
 */
void UdpSet() {
	// UDP Port
	APP_UDP.begin(APP_UDP_PORT);

	#ifdef ENABLE_LOG_APP
		PRNT::_print(PRNT::formatMSG("%32s : UDP socket opened on port [%d]" NL, "APP_UdpSet", APP_UDP_PORT));
	#endif
}
} // namespace APP

namespace DIF {

/**
 * @brief  Initialise the diffuser UDP socket.
 *
 * Opens DIF_UDP on DIF_UDP_PORT (8439) if WiFi is connected.
 * Call once in setup().
 */
void Setup() {
	PRNT::_print(PRNT::formatMSG("%~32s # setup" NL, "DIF_Setup"));

	if (NET::IsConnected()) {
		UdpSet();

		PRNT::_print(PRNT::formatMSG("%~32s # UDP port set to (%d)" NL, "DIF_Setup", DIF_UDP_PORT));
	} else {
		PRNT::_print(PRNT::formatMSG("%~32s # WiFi not connected" NL, "DIF_Setup"));
	}

	// TASK -- locked, repeating: polls "Ds" every DIF_STATUS_CHECK_S and detects no-reply timeouts
	TSK::AddTask("DIF_Setup", "StatusCheck", StatusCheck, TASK_S, DIF_STATUS_CHECK_S, 0, true);
	// TASK -- locked, repeating: fires idle pulse when all sources idle >= EE_DIF_IDLE_WAIT_MIN minutes
	TSK::AddTask("DIF_Setup", "IdleCheck", IdleCheck, TASK_S, DIF_IDLE_CHECK_S, 0, true);
}

/**
 * @brief  Receive and dispatch one incoming UDP packet from the diffuser per loop iteration.
 *
 * Caches WiFi status every ~500 ms to reduce overhead. Exits early if not connected
 * or if no packet is waiting. Validates packet size (< DIF_UDP_MAX_BUFFER_SIZE),
 * strips trailing newline/carriage return, then calls Exec() to dispatch.
 *
 * @note   Call every loop() iteration.
 */
void Loop() {
    // 1. WiFi status: use the shared cache updated once per loop() cycle
    if (!NET::Connected_Cached()) return;                                // Exit if no network - Logic

    // 2. Round-robin UDP polling - only poll on loop 1 of 3-cycle pattern
    static uint8_t difPollPhase = 1;
    if (++difPollPhase % 3 != 1) return;                                // Poll only on phase 1 of 3 - Logic

    // 3. Fast exit for no packets
    int pkSize = DIF_UDP.parsePacket();                                 // Check for incoming data - Action
    if (pkSize <= 0) return;                                            // Exit if empty - Logic

    // 3. Prevent buffer overflow and read efficiently
    if (pkSize >= DIF_UDP_MAX_BUFFER_SIZE) {                            // Bounds check - Logic
        PRNT::_print(PRNT::formatMSG("%32s ! packet too large: [%d] bytes" NL, "DIF_Loop", pkSize)); // Log overflow attempt - Output
        DIF_UDP.flush();                                                // Clear the buffer - Action
        return;
    }

    // 4. Read packet
    static char recvBuff[DIF_UDP_MAX_BUFFER_SIZE];                      // Local receive buffer - State
    int len = DIF_UDP.read(recvBuff, DIF_UDP_MAX_BUFFER_SIZE - 1);       // Read packet - Action
    if (len > 0) {                                                      // If data read - Logic
        // Remove trailing newline or carriage return without String objects
        if (recvBuff[len - 1] == '\n' || recvBuff[len - 1] == '\r') {
			len--;                                                      // Strip newline - Logic
		}
		recvBuff[len] = '\0';                                           // Null-terminate - Mapping

        #ifdef ENABLE_LOG_DIF                                         // Logging block - Logic
			PRNT::_print(PRNT::formatMSG("%32s : recv [%s] (%d)" NL, "DIF_Loop", recvBuff, len));
		#endif

        Exec(recvBuff, len);                                       // Execute command - Action
    }
}

/**
 * @brief  Command dispatcher -- parse the first two bytes and route to the appropriate handler.
 *
 * @param  buff  Null-terminated command string received over UDP from the diffuser.
 * @param  len   Length of the command string (excluding null terminator).
 *
 * Command routing:
 *   'D' + 's' (len 6) -> ParseStatus()   ("DsMMSS" status reply)
 */
void Exec(char *buff, int len) {
	// * LOG
	#ifdef ENABLE_LOG_DIF
        PRNT::_print(PRNT::formatMSG("%32s : exec [%s] size [%d]" NL, "Exec", buff, len)); // Log command - Sync
	#endif

	// * ACK from the diffuser? "#SSR" -- relay to the app if it answers a relayed command
	if (len >= 1 && buff[0] == APP_ACK_CHAR) { AckParse(buff, len); return; }

	const char cmd = buff[0];
	const char sub = (len > 1) ? buff[1] : '\0';

	switch (cmd) {
		case 'D':
			switch (sub) {
				case 's': ParseStatus(buff, len); break; // Status reply from diffuser
				case 'h': RelayHistoryToApp(buff, len); break; // Full refill history - relay straight to app, no caching
				default:
					PRNT::_print(PRNT::formatMSG("%32s ! invalid DIF subcommand [%c]" NL, "Exec", sub));
				break;
			}
		break;

		default: // Invalid command
			PRNT::_print(PRNT::formatMSG("%32s ! invalid DIF command [%s]" NL, "Exec", buff));
		break;
	}
}

/**
 * @brief  Handle a "DsMMSS" status reply from the diffuser.
 *
 * @param  buff  Command buffer. buff[2..3] = MODE (hex), buff[4..5] = strip STATUS (hex),
 *               buff[6..9] = parfum remaining MINUTES (4-hex, 0000 = inactive), and (extended
 *               reply only) buff[10..13] = usage accum minutes, buff[14..17] = avg cycle
 *               minutes, buff[18..19] = refill history count, buff[20..23] = lifetime refills.
 * @param  len   24 ("Ds" + MODE + STATUS + PARFUM + the 4 usage fields above) for current
 *               firmware; 10 (no usage fields) or legacy 6 ("DsMMSS") still accepted, with
 *               whichever trailing fields are missing left at their previous value.
 *
 * Updates DIF::State.Mode, DIF::State.StripStatus, DIF::State.ParfumMin, the Usage* fields,
 * and DIF::State.LastStatusTime, then pushes the app status packet (via updStatus) so it
 * stays in sync with the diffuser's real state without having to poll separately.
 */
void ParseStatus(char *buff, int len) {
	if (len != 6 && len != 10 && len != 24) {
		PRNT::_print(PRNT::formatMSG("%32s ! invalid status length (%d)" NL, "DIF_ParseStatus", len));
		return;
	}

	// Fast hex parsing (shared helper, see HexByte)
	uint8_t mode   = APP::HexByte(&buff[2]);
	uint8_t status = APP::HexByte(&buff[4]);

	DIF::State.Mode        = mode;
	DIF::State.StripStatus = status;
	DIF::State.ParfumMin   = (len >= 10)
		? (((uint16_t)APP::HexByte(&buff[6]) << 8) | APP::HexByte(&buff[8]))  // 4-hex big-endian - Mapping
		: 0;                                                                 // Legacy reply -- no parfum field - Logic
	if (len == 24) {
		DIF::State.UsageAccumMin     = ((uint16_t)APP::HexByte(&buff[10]) << 8) | APP::HexByte(&buff[12]);
		DIF::State.UsageAvgMin       = ((uint16_t)APP::HexByte(&buff[14]) << 8) | APP::HexByte(&buff[16]);
		DIF::State.UsageRefillCount  = APP::HexByte(&buff[18]);
		DIF::State.UsageTotalRefills = ((uint16_t)APP::HexByte(&buff[20]) << 8) | APP::HexByte(&buff[22]);
	}
	DIF::State.LastStatusTime = TimeNow;

	// * LOG
    #if defined(ENABLE_LOG_DIF) || defined(ENABLE_LOG_APP)
        PRNT::_print(PRNT::formatMSG("%32s : status [mode:%d-%s] [strip:%d-%s]" NL, "DIF_ParseStatus", mode, DIF::getModeName(mode), status, DIF::getStripStatusName(status)));
	#endif

	// * Push the full "s..." status packet too, so the diffuser dot updates immediately
	APP::updStatus("DIF::ParseStatus");
}

/**
 * @brief  Parse a "#SSR" ACK from the diffuser; relay the result up to the app
 *         when it answers the command we sent on the app's behalf.
 * @param  buff  Diffuser reply payload ("#" + 2 hex seq + 1 hex result).
 * @param  len   Payload length (>= 4).
 */
void AckParse(char *buff, int len) {
	if (len < 4 || buff[0] != APP_ACK_CHAR) return;
	uint8_t seq    = APP::HexByte(&buff[1]);
	uint8_t result = APP::HexNibble(buff[3]);
	DIF::State.CmdResult  = result;

	#if defined(ENABLE_LOG_DIF) || defined(ENABLE_LOG_APP)
		PRNT::_print(PRNT::formatMSG("%32s : ack seq [%d] result [%d]" NL, "DIF_AckParse", seq, result));
	#endif

	uint8_t appSeq;
	if (PopPendingByCmdSeq(seq, appSeq)) {
		APP::termMsgAck(appSeq, result);   // forward the diffuser's real result to the app
	}
}

/**
 * @brief  Locked, repeating task -- single task drives the entire idle-pulse
 *         state machine (wait -> on -> off). No nested/one-shot task is
 *         spawned for the on-duration; everything is timed off TimeNow
 *         snapshots checked on every tick of this same task.
 *
 * State is just the IdlePulseActive flag plus two timestamps:
 *
 *   IDLE (IdlePulseActive == false) -- waiting out the X-minute quiet period:
 *     - No-op if EE_DIF_IDLE_WAIT_MIN or EE_DIF_IDLE_MODE is 0 (feature off).
 *     - Any source active (ActiveModeSetting() != 0xFF) keeps restarting
 *       the countdown via IdleTimerReset() -- a pulse never starts while
 *       a source is running.
 *     - Once idle elapsed >= EE_DIF_IDLE_WAIT_MIN minutes (and
 *       EE_DIF_IDLE_ON_MIN is non-zero), starts the pulse: TurnOn() in
 *       EE_DIF_IDLE_MODE, static effect, colour forced to black (strip off,
 *       scent-only pulse), IdlePulseActive = true, IdlePulseStart = TimeNow.
 *
 *   PULSING (IdlePulseActive == true) -- running out the Y-minute on-time:
 *     - Each tick just compares (TimeNow - IdlePulseStart) against
 *       EE_DIF_IDLE_ON_MIN minutes; once elapsed, sends Df, clears
 *       IdlePulseActive, and restarts the idle countdown.
 *
 * IdleTimerReset() (already called by AutoOn/AutoOff on any
 * source event) clears IdlePulseActive immediately, so a source going
 * active mid-pulse aborts the pulse on the spot rather than waiting for
 * this task's next tick.
 *
 * @param  taskId  Unused -- required by AddTask() callback signature.
 */
void IdleCheck(taskId_t taskId) {
    if (DIF::State.ParfumMin > 0) {                                             // Parfum window running -- no idle pulses, keep countdown fresh - Logic
        IdleTimerReset();
        return;
    }
    if (DIF::State.IdlePulseActive) {                                           // --- PULSING: watch for on-duration elapsed - Logic
        const uint8_t  onMin = EE::Get(EE_DIF_IDLE_ON_MIN);              // Y minutes on duration - Setup
        const uint32_t onMs  = (uint32_t)onMin * 60000UL;               // Convert to ms - Setup
        if ((TimeNow - DIF::State.IdlePulseStart) < onMs) return;              // Still pulsing -- nothing to do yet - Logic

        // * Always printed (not gated behind ENABLE_LOG_DIF) -- idle pulse turning off
        PRNT::_print(PRNT::formatMSG("%~32s # idle pulse off, on-time elapsed [%d min]" NL, "IdleCheck", onMin));
        Shutdown();                                                  // Send Df command - Action
        DIF::State.IdlePulseActive = false;                                     // Pulse finished -- back to IDLE - State
        IdleTimerReset();                                            // Start a fresh idle countdown post-pulse - Action
        return;
    }

    // --- IDLE: not pulsing yet -- check whether it's time to start one ---
    const uint8_t waitMin = EE::Get(EE_DIF_IDLE_WAIT_MIN);               // X minutes idle threshold - Setup
    if (waitMin == 0) return;                                            // Feature disabled - Logic
    const uint8_t idleMode = EE::Get(EE_DIF_IDLE_MODE);                  // Diffuser mode for idle pulse - Setup
    if (idleMode == 0) return;                                           // Mode disabled -- feature off - Logic

    if (ActiveModeSetting() != 0xFF) {                              // Source active -- not idle - Logic
        IdleTimerReset();                                            // Keep timer fresh while active - Action
        return;
    }

    const uint32_t idleMs = (uint32_t)waitMin * 60000UL;                // Convert to ms - Setup
    if ((TimeNow - DIF::State.IdleTimerReset) < idleMs) return;                // Not yet - Logic

    const uint8_t onMin = EE::Get(EE_DIF_IDLE_ON_MIN);                   // Y minutes on duration - Setup
    if (onMin == 0) return;                                              // On-duration zero -- nothing to do - Logic

    DIF::State.IdlePulseActive = true;                                          // Enter PULSING state - State
    DIF::State.IdlePulseStart  = TimeNow;                                       // Anchor on-duration countdown - State
    LogPush(difTrigIdle, idleMode, 0);                               // Record idle-pulse start -- effect 0 = static/off - Action
    // * Always printed (not gated behind ENABLE_LOG_DIF) -- idle pulse turning on
    PRNT::_print(PRNT::formatMSG("%~32s # idle pulse on, [%d min] idle elapsed, mode [%d] for [%d min] (strip off)" NL,
        "IdleCheck", waitMin, idleMode, onMin));
    DIF_Colorx black = { 0, 0, 0, 0, 0, 0 };                             // Scent-only pulse -- strip stays off - Setup
    TurnOn(idleMode, 0, &black);                                     // EE mode, static effect, forced black - Action
}


/**
 * @brief  Locked, repeating task -- polls "Dc" every DIF_STATUS_CHECK_S and detects no-reply timeouts.
 *
 * Before sending a fresh "Dc" request, checks whether a "DsMMSS" reply arrived (via
 * ParseStatus) since the *previous* request was sent. If not, the diffuser is
 * considered unreachable and DIF::State.Mode is set to DIF_MODE_NO_RESPONSE so logs and the
 * app reflect it immediately. This only ever touches the status mirror, DIF::State.Mode --
 * TurnOn() independently caps outgoing "Dn" commands at DIF_MODE_MAX, so 5/6 can
 * never be transmitted as a command.
 *
 * @param  taskId  Unused -- required by the AddTask() callback signature.
 *
 * @note   Registered once in Setup() with Locked=true, so it survives
 *         KillTasksAvoidLocked() and keeps polling for the life of the sketch.
 */
void StatusCheck(taskId_t taskId) {
	// * Did the previous "Dc" request get answered before this tick?
	if (DIF::State.LastRequestTime != 0 && DIF::State.LastStatusTime < DIF::State.LastRequestTime) {
		DIF::State.Mode = DIF_MODE_NO_RESPONSE;                                    // No "DsMMSS" reply arrived in time - State
		DIF::State.ParfumMin = 0;                                                  // Unreachable -- drop stale parfum mirror so guards release - State

		// * LOG
		#if defined(ENABLE_LOG_DIF) || defined(ENABLE_LOG_APP)
			PRNT::_print(PRNT::formatMSG("%32s ! no reply since last request, marking NO_RESPONSE" NL, "StatusCheck"));
		#endif

		APP::updStatus("DIF::StatusCheck");                                                // Push the change to the app right away - Sync

		FlushPending(APP_ACK_NOWATER);                                             // Flush every pending app relay so the phone isn't left waiting - Sync
	}

	DIF::State.LastRequestTime = TimeNow;                                          // Snapshot before sending - State
	PollStatus();                                                       // Send "Dc" - Action
}

/**
 * @brief  Resolve which EE_DIF_MODE_* setting matches the currently active
 *         trigger source, so live setting pushes (see APP::Settings()) use the
 *         right mode value while one source is active.
 *
 * Priority TV > UDPRAW > MOTION > APP::Am. Returns 0xFF when nothing is active,
 * which also serves as the "no source active" predicate (replaces DIF_AnySourceActive).
 * @return EE_DIF_MODE_* setting index, or 0xFF if no source is currently active.
 */
uint8_t ActiveModeSetting() {
    if (TV::State.Status)     return EE_DIF_MODE_TV;                              // Logic
    if (UDPRAW::State.Status) return EE_DIF_MODE_UDPRAW;                          // Logic
    const bool motionActive = (MOTION::State.Status == motCOM || MOTION::State.Status == motBED);
    if (motionActive)  return EE_DIF_MODE_MOTION;                          // Logic
    if (APP::Am.Status)      return EE_DIF_MODE_AMBIENT;                        // Logic
    return 0xFF;                                                            // No source active - Logic
}

/**
 * @brief  Shut diffuser off only when ALL activity sources have gone idle.
 *
 * Sources: TV::State.Status, UDPRAW::State.Status, MOTION (lit/winding-down), APP::Am.Status --
 * detected via ActiveModeSetting() == 0xFF. No-op if any source is active.
 * Called by TV::Off(), UDPRAW::End(), MOTION::T_EFFECT_MOTION_OFF(taskDone), APP::AmbientMode() OFF,
 * T_AMBIENT_MODE_ON phase-3 completion.
 */
void AutoOff() {
    if (ActiveModeSetting() != 0xFF) {                                  // Another source still active - Logic
        #ifdef ENABLE_LOG_DIF
            PRNT::_print(PRNT::formatMSG("%~32s # other source still active, staying on [tv:%T] [udpraw:%T] [motion:%d] [am:%T]" NL,
                "DIF_AutoOff", TV::State.Status, UDPRAW::State.Status, MOTION::State.Status, APP::Am.Status));
        #endif
        return;
    }
    IdleTimerReset();                                                // All sources idle -- start fresh idle countdown - ActionDIF
    #ifdef ENABLE_LOG_DIF
        if (DIF::State.ParfumMin > 0)                                           // Diffuser queues it instead of applying now - Logic
            PRNT::_print(PRNT::formatMSG("%~32s # parfum active [%d min], diffuser will queue this Df" NL, "DIF_AutoOff", DIF::State.ParfumMin));
        PRNT::_print(PRNT::formatMSG("%~32s # all sources idle, shutting down" NL, "DIF_AutoOff"));
    #endif
    Shutdown();                                                      // Send Df command -- queued if parfum is active - Action
}


/**
 * @brief  Turn diffuser on via EE settings when any activity source becomes active.
 *
 * Called by TV::On() with EE_DIF_MODE_TV, UDPRAW::Init() with EE_DIF_MODE_UDPRAW,
 * MOTION::Status() initial trigger with EE_DIF_MODE_MOTION, APP::AmbientMode() ON
 * with EE_DIF_MODE_AMBIENT. Effect always comes from the single shared EE_DIF_EFFECT
 * setting. Colour comes from colorOverride when given (TV/Motion random-color paths
 * that already computed a fresh colour before LED::State.CurrentColor catches up); otherwise
 * TurnOn() derives it live from whatever's actually lit right now.
 * No-op if mode stored in EE is 0 (disabled by user setting for that source).
 *
 * @param  eeModeSetting  One of EE_DIF_MODE_TV / EE_DIF_MODE_MOTION /
 *                        EE_DIF_MODE_UDPRAW / EE_DIF_MODE_AMBIENT -- identifies
 *                        which source is triggering the auto-on.
 * @param  colorOverride  Explicit colour(s) to use, or NULL (default) to derive
 *                        live from the currently-lit LEDs.
 */
void AutoOn(uint8_t eeModeSetting, const DIF_Colorx *colorOverride) {
    const uint8_t mode   = EE::Get(eeModeSetting);                        // EE mode for this source - Setup
    const uint8_t effect = EE::Get(EE_DIF_EFFECT);                        // Shared strip effect setting - Setup
    if (mode == 0) return;                                               // User disabled for this source - Logic
    IdleTimerReset();                                                // Source active -- restart idle countdown - Action
    #ifdef ENABLE_LOG_DIF
        PRNT::_print(PRNT::formatMSG("%~32s # auto-on [src:%s] [mode:%d] [effect:%d]" NL, "DIF_AutoOn", EE::getName(eeModeSetting), mode, effect));
    #endif
    const uint8_t trig = (eeModeSetting == EE_DIF_MODE_TV)      ? difTrigTV      :
                         (eeModeSetting == EE_DIF_MODE_UDPRAW)   ? difTrigUDPRAW  :
                         (eeModeSetting == EE_DIF_MODE_MOTION)   ? difTrigMotion  : difTrigAmbient; // Source mapping - Mapping
    LogPush(trig, mode, effect);                                     // Record auto-on event - Action
    TurnOn(mode, effect, colorOverride);                             // Send Dn command - Action
}

/**
 * @brief  Average whatever the LED strip is currently showing into one or two RGB colours.
 *
 * Scans LED::State.CurrentColor[] for lit pixels (same "non-black" test the old light-stage
 * matching used), averaging the whole strip into out.r1/g1/b1. When dual is true, the
 * array is additionally split at its midpoint and the second half is averaged into
 * out.r2/g2/b2 -- giving the diffuser a left/right pair when the TV is in dual-colour
 * mode. The raw colour is used directly -- no fixed set of stages to map down to.
 *
 * @param  dual  true = also average the second half into r2/g2/b2.
 * @param  out   Result -- r1/g1/b1 always set; r2/g2/b2 only meaningful when dual=true.
 */
void ColorFromCurrentLEDs(bool dual, DIF_Colorx &out) {
    uint16_t sumR = 0, sumG = 0, sumB = 0, cnt = 0;
    uint16_t sumR2 = 0, sumG2 = 0, sumB2 = 0, cnt2 = 0;
    const int half = LED_NUM_TOTAL / 2;

    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        const CRGB &c = LED::State.CurrentColor[i];
        if ((c.r + c.g + c.b) == 0) continue;                               // Skip unlit pixels - Logic
        if (dual && i >= half) { sumR2 += c.r; sumG2 += c.g; sumB2 += c.b; cnt2++; }
        else                   { sumR  += c.r; sumG  += c.g; sumB  += c.b; cnt++;  }
    }

    out.r1 = cnt  ? (uint8_t)(sumR  / cnt)  : 255;                          // Fall back to white - Logic
    out.g1 = cnt  ? (uint8_t)(sumG  / cnt)  : 255;
    out.b1 = cnt  ? (uint8_t)(sumB  / cnt)  : 255;
    if (dual) {
        out.r2 = cnt2 ? (uint8_t)(sumR2 / cnt2) : out.r1;                  // Nothing lit on 2nd half -- mirror 1st - Logic
        out.g2 = cnt2 ? (uint8_t)(sumG2 / cnt2) : out.g1;
        out.b2 = cnt2 ? (uint8_t)(sumB2 / cnt2) : out.b1;
    }
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

/**
 * @brief  Reset the idle-pulse countdown to now.
 *
 * Call whenever any source becomes active (AutoOn) or all sources have
 * just gone idle (AutoOff). Clears IdlePulseActive so a finished pulse
 * can re-trigger after the next full X-minute window.
 */
void IdleTimerReset() {
    DIF::State.IdleTimerReset  = TimeNow;                                       // Anchor countdown to now - State
    DIF::State.IdlePulseActive = false;                                         // Allow next pulse - State
    #ifdef ENABLE_LOG_DIF
        PRNT::_print(PRNT::formatMSG("%~32s # idle timer reset" NL, "DIF_IdleTimerReset"));
    #endif
}

/**
 * @brief  Push a new auto-on event into DIF::State.LOG ring-buffer (newest at [0]).
 *
 * @param  triggerBy  DIF_TriggerBy source: difTrigTV/UDPRAW/Motion/Ambient/Idle.
 * @param  mode       Diffuser mode sent (1..DIF_MODE_MAX).
 * @param  effect     Diffuser strip effect sent (0..DIF_EFFECT_COUNT).
 */
void LogPush(uint8_t triggerBy, uint8_t mode, uint8_t effect) {
    for (int i = DIF_LOG_INDEX_MAX - 1; i > 0; i--) {                   // Shift entries down - Action
        DIF::State.LOG[i] = DIF::State.LOG[i - 1];                                     // Ring shift - Mapping
    }
    DIF::State.LOG[0].epoch     = RTC_TimeClient.getEpochTime();               // Epoch - Mapping
    DIF::State.LOG[0].TriggerBy = triggerBy;                                    // Source - Mapping
    DIF::State.LOG[0].Mode      = mode;                                         // Mode sent - Mapping
    DIF::State.LOG[0].Effect    = effect;                                       // Effect sent - Mapping
}

/**
 * @brief  Send "DpMMMME" -- start or cancel the diffuser's parfum mode.
 *
 * @param  minutes  1..DIF_PARFUM_MAX_MIN = timed "insist" run of that many
 *                  minutes (diffuser ignores Df/Dn until expiry, violet
 *                  PULSE strip cue); 0 = cancel the run + shutdown (mode
 *                  is ignored/sent as 0).
 * @param  mode     Dispense pattern for the run, 1..DIF_MODE_MAX
 *                  (1=CONT, 2=10 SEC -- same enum as TurnOn's mode param).
 *                  Ignored when minutes == 0.
 *
 * Remaining minutes come back in every "DsMMSSTTTT" status reply (see
 * ParseStatus()), so DIF::State.ParfumMin tracks the window and the app is
 * informed via the "p" packet in APP::updStatus().
 */
void Parfum(uint16_t minutes, uint8_t mode) {
	if (minutes > DIF_PARFUM_MAX_MIN) {
		PRNT::_print(PRNT::formatMSG("%32s ! invalid minutes (%d)" NL, "DIF_Parfum", minutes));
		return;
	}

	// * LOG
	PRNT::_print(PRNT::formatMSG("%~32s # parfum [%d min] mode [%d]%s" NL, "DIF_Parfum", minutes, mode, (minutes == 0) ? " (cancel)" : ""));

	// NOTE: no RequestStatus() here (unlike the old code) - Send() only has a
	// single staged-message slot and a newer Send() call unconditionally
	// overwrites whatever's still in flight (see TickAsyncSend()'s doc
	// comment - correct for "only the latest state matters" pushes like
	// Dual Color, wrong for a one-shot command). Calling RequestStatus()
	// (Send("Ds")) immediately after SendMaybeAck() here clobbered the just-
	// staged "#SSDpMMMME" before TickAsyncSend() ever got a chance to
	// transmit it - the Parfum command silently never reached the diffuser
	// at all, which is exactly why the app saw "NO ACK" (there was nothing
	// to ack) while every other diffuser command worked fine. The diffuser's
	// own periodic StatusCheck poll refreshes the mirror shortly anyway, and
	// the "#SSR" ack (once the command actually arrives) drives ParseStatus()
	// via the ack-triggered path too - the immediate extra refresh wasn't
	// needed for correctness, only for shaving a couple seconds off the
	// app's mirror update, at the cost of dropping the command entirely.
	if (minutes == 0) {
		SendMaybeAck("Dp0000");                                    // Cancel -- diffuser expects exactly 6 chars, no mode digit - Action
	} else {
		SendMaybeAck(PRNT::formatMSG("Dp%4X%1X", minutes, mode));        // Start -- 4-hex minutes + 1-hex mode - Action
	}
}

/**
 * @brief  Send "Dc" -- poll the diffuser's current status (used by StatusCheck).
 *
 * Same effect as "Ds" -- diffuser still replies with a "DsMMSS" packet, handled by
 * ParseStatus() -- but "Dc" tells the diffuser this is a routine periodic poll
 * rather than a one-off manual request, so it skips its own debug logging for it.
 */
void PollStatus() {
	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : poll status" NL, "PollStatus"));
	#endif

	Send("Dc");
}

/**
 * @brief  Re-push mode/effect to the diffuser right now, if any source is active.
 *
 * Colour comes from colorOverride when given (callers that just computed a fresh
 * colour before LED::State.CurrentColor catches up, e.g. a dual/single colour just set by
 * the app); otherwise TurnOn() derives it live from whatever's actually lit.
 * Call this after anything that changes EE_HB_DUAL_COLOR, EE_DIF_EFFECT, or any
 * EE_DIF_MODE_* while a source may already be running, so the diffuser reflects
 * the new state immediately instead of waiting for the next AutoOn() trigger.
 * No-op if nothing's active.
 *
 * @param  colorOverride  Explicit colour(s) to use, or NULL to derive from current LEDs.
 *
 * Called by: LED::setLux() (lux-bucket brightness change), cmdChangeColor(),
 * cmdChangeDualColor(), cmdSettings() (when a DIF-relevant setting changed).
 */
void PushLiveIfActive(const DIF_Colorx *colorOverride) {
    const uint8_t activeModeSetting = ActiveModeSetting();             // Which EE_DIF_MODE_* matches the active source - Logic
    if (activeModeSetting != 0xFF) {
        TurnOn(EE::Get(activeModeSetting), EE::Get(EE_DIF_EFFECT), colorOverride); // Push new mode/effect/colour now - Action
    }
}

/**
 * @brief  Send "Ds" -- request the diffuser's current status.
 *
 * The diffuser is expected to reply with a "DsMMSS" packet, handled by ParseStatus().
 */
void RequestStatus() {
	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : request status" NL, "DIF_RequestStatus"));
	#endif

	Send("Ds");
}

/**
 * @brief  Send "Dh" -- ask the diffuser for its full refill-cycle history.
 *
 * Unlike RequestStatus()/PollStatus() (which ride the periodic Ds/Dc poll and
 * whose reply is cached in DIF::State), this is on-demand only: sent when the
 * app explicitly asks for it (see APP::Exec()'s "Dh" case), and the reply
 * (handled by RelayHistoryToApp()) is relayed straight through to the app
 * without being cached here - it's bulkier and rarely needed, so there's no
 * reason to keep a stale copy of it in DIF::State between requests.
 */
void RequestHistory() {
	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : request full refill history" NL, "DIF_RequestHistory"));
	#endif

	Send("Dh");
}

/**
 * @brief  'Dh...' full refill-history reply from the diffuser - relayed
 *         straight to the app unchanged, on the spot. Deliberately NOT
 *         stored in DIF::State (see RequestHistory()) - this firmware is
 *         just a pass-through for this one on-demand payload.
 *
 * @param  buff  Full reply from the diffuser ("Dh" + 2-hex count + 10x4-hex minutes).
 * @param  len   Byte-length of buff (informational only here).
 */
void RelayHistoryToApp(char *buff, int len) {
	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : relaying history to app [%s] (%d)" NL, "DIF_RelayHistory", buff, len));
	#else
		(void)len;
	#endif

	APP::termMsgSend(buff);
}

/**
 * @brief  Send "Dr" -- tell the diffuser a manual refill just happened.
 *
 * Unlike Shutdown()/Parfum(), this isn't a physical action -- the diffuser
 * applies it instantly (banks the current usage cycle into its refill
 * history, resets the accumulator, clears any out-of-water alert) and
 * replies with the same "DsMMSSTTTT..." status format as Ds/Df, handled by
 * ParseStatus() like any other status reply. See APP::Exec()'s "Dr" case.
 */
void ManualRefill() {
	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : manual refill" NL, "DIF_ManualRefill"));
	#endif

	SendMaybeAck("Dr");
}

/**
 * @brief  Async diffuser-send state: staged payload + which step is in flight.
 *
 * Send() used to busy-wait up to DIF_UDP_TIMEOUT ms TWICE in a row (once for
 * beginPacket(), once for endPacket()) with no yield - up to ~200ms where the
 * ENTIRE board is frozen: no UDP, no MQTT, no LED refresh, nothing. Every
 * Dual Color command calls PushLiveIfActive() -> Send() whenever the diffuser
 * has an active source, so rapid-fire LD/Ld commands compounded this into a
 * real, reproduced bug: 150 back-to-back Dual Color commands wedged the
 * board's UDP responsiveness completely, with no self-recovery (confirmed
 * live - see the Dual-Color-spam investigation).
 *
 * Fix: Send() now only stages the message; TickAsyncSend() (called every
 * loop() - see the top-level loop()) advances the handshake one non-blocking
 * step at a time. A newer Send() call simply overwrites whatever's still
 * staged/in-flight - only the latest diffuser state ever matters for these
 * "push current state" messages, so no queue is needed (mirrors
 * APP::drainColorSync's coalescing for the 'LK' colour-sync path). Spam
 * naturally collapses to "send whatever's freshest, as fast as the link
 * allows" instead of piling up a backlog that outruns real time.
 */
static char          difAsyncMsg[DIF_MSG_MAX_LEN];   // staged payload, copied in on every Send()
static bool          difAsyncPending = false;         // a send is staged or in flight
static uint8_t       difAsyncPhase   = 0;             // 0=idle 1=awaiting beginPacket() 2=awaiting endPacket()
static unsigned long difAsyncPhaseAt = 0;             // millis() the current phase started, for the DIF_UDP_TIMEOUT ceiling

/**
 * @brief  Stage a C-string to be sent as a UDP packet to the diffuser.
 *
 * @param  msg  Null-terminated string to transmit (copied - safe to reuse/free
 *              the caller's buffer immediately after this returns). NULL is
 *              safely ignored. Truncated to DIF_MSG_MAX_LEN-1 chars (every
 *              real caller's payload fits well under that, see SendCmd()).
 *
 * Non-blocking: returns immediately. The actual beginPacket()/write()/
 * endPacket() handshake is driven by TickAsyncSend(), one non-blocking
 * attempt per loop() iteration, up to DIF_UDP_TIMEOUT ms per step before
 * giving up - same "silently drop on timeout" behavior as before, just
 * without freezing everything else while it waits.
 *
 * Called by: PollStatus() ("Dc"), RequestStatus() ("Ds"), RequestHistory()
 * ("Dh"), and SendCmd() (enveloped relay commands - itself called by
 * SendMaybeAck(), used from ManualRefill()/Parfum()/Shutdown()/TurnOn() and
 * the app-originated diffuser command path in APP::Exec()).
 */
void Send(const char *msg) {
	if (msg == NULL) {
		PRNT::_print(PRNT::formatMSG("%32s ! null message" NL, "Send"));
		return;
	}

	if (!NET::IsConnected()) {
		#ifdef ENABLE_LOG_DIF
			PRNT::_print(PRNT::formatMSG("%32s : WiFi not connected" NL, "Send"));
		#endif

		return;
	}

	strncpy(difAsyncMsg, msg, DIF_MSG_MAX_LEN - 1);
	difAsyncMsg[DIF_MSG_MAX_LEN - 1] = '\0';
	if (strlen(msg) >= DIF_MSG_MAX_LEN) {
		PRNT::_print(PRNT::formatMSG("%32s ! message truncated, was [%d] bytes, max [%d]" NL, "Send", strlen(msg), DIF_MSG_MAX_LEN - 1));
	}

	// (Re)start the handshake fresh with the newest payload. If a send was
	// already mid-flight (phase 1/2), this abandons it in favour of the
	// fresher one - beginPacket() safely resets any half-written buffer, so
	// there's nothing to clean up first.
	difAsyncPending = true;
	difAsyncPhase   = 0;
}

/**
 * @brief  Advance the staged diffuser send by one non-blocking step.
 *
 * @note   Call every loop() iteration (see the top-level loop()). No-op
 *         when nothing is staged. See Send() for the full story.
 */
void TickAsyncSend() {
	if (!difAsyncPending) return;

	if (difAsyncPhase == 0) {
		difAsyncPhase   = 1;
		difAsyncPhaseAt = millis();
	}

	if (difAsyncPhase == 1) {
		if (DIF_UDP.beginPacket(DIF_TARGET_IP, DIF_UDP_PORT)) {
			DIF_UDP.write(difAsyncMsg);
			difAsyncPhase   = 2;
			difAsyncPhaseAt = millis();
			return;   // endPacket() gets its own tick(s) below
		}
		if (millis() - difAsyncPhaseAt >= DIF_UDP_TIMEOUT) {
			#ifdef ENABLE_LOG_DIF
				PRNT::_print(PRNT::formatMSG("%32s : beginPacket() timeout after %lu ms" NL, "Send", DIF_UDP_TIMEOUT));
			#endif
			difAsyncPending = false;
			difAsyncPhase   = 0;
		}
		return;   // one non-blocking attempt per tick, never busy-waits
	}

	if (difAsyncPhase == 2) {
		if (DIF_UDP.endPacket() == 1) {
			#ifdef ENABLE_LOG_DIF
				PRNT::_print(PRNT::formatMSG("%32s : sent [%s] size [%d]" NL, "Send", difAsyncMsg, strlen(difAsyncMsg)));
			#endif
			difAsyncPending = false;
			difAsyncPhase   = 0;
			return;
		}
		if (millis() - difAsyncPhaseAt >= DIF_UDP_TIMEOUT) {
			#ifdef ENABLE_LOG_DIF
				PRNT::_print(PRNT::formatMSG("%32s : endPacket() timeout after %lu ms" NL, "Send", DIF_UDP_TIMEOUT));
			#endif
			difAsyncPending = false;
			difAsyncPhase   = 0;
		}
	}
}

/**
 * @brief  Queue a newly-sent relayed command's {CmdSeq, AppSeq} pair so its
 *         eventual "#SSR" ack (matched in AckParse()) can be forwarded to the
 *         right app seq, even if another relayed command is sent before this
 *         one is answered -- see the DIFx::PendingCmdSeq/PendingAppSeq comment.
 * @note   If the queue is already full, the oldest entry is dropped (and
 *         logged) - it simply never gets its app-side ack, same failure mode
 *         as the old single-slot overwrite, but only after genuinely
 *         exhausting DIF_PENDING_ACK_MAX in-flight relays instead of on the
 *         very next send.
 */
void PushPending(uint8_t cmdSeq, uint8_t appSeq) {
	if (appSeq == 0xFF) return;    // Nothing to relay - not a real pending entry
	if (DIF::State.PendingCount >= DIF_PENDING_ACK_MAX) {
		PRNT::_print(PRNT::formatMSG("%32s ! pending ack queue full, dropping oldest [seq:%d]" NL,
			"DIF_PushPending", DIF::State.PendingAppSeq[0]));
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

/**
 * @brief  Look up and remove the queued entry matching a diffuser "#SSR" cmdSeq.
 * @param  cmdSeq    Sequence id echoed back by the diffuser.
 * @param  outAppSeq Set to the matching app seq on success.
 * @return true if a matching entry was found (and removed).
 */
bool PopPendingByCmdSeq(uint8_t cmdSeq, uint8_t &outAppSeq) {
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

/** @brief  True if `appSeq` currently has a relayed diffuser command awaiting its ack. */
bool IsAppSeqPending(uint8_t appSeq) {
	for (uint8_t i = 0; i < DIF::State.PendingCount; i++) {
		if (DIF::State.PendingAppSeq[i] == appSeq) return true;
	}
	return false;
}

/** @brief  Flush every still-pending relayed command with the given result (e.g. on a "Ds" no-reply timeout) and empty the queue. */
void FlushPending(uint8_t result) {
	for (uint8_t i = 0; i < DIF::State.PendingCount; i++) {
		APP::termMsgAck(DIF::State.PendingAppSeq[i], result);
	}
	DIF::State.PendingCount = 0;
}

/**
 * @brief  Wrap a diffuser command in the ACK envelope ("#SS<body>") and remember
 *         an app seq to relay the diffuser's reply to.
 * @param  body         Diffuser command without envelope (e.g. "Dn0102...").
 * @param  relayAppSeq  App seq to relay the diffuser's "#SSR" to, or 0xFF for none.
 */
void SendCmd(const char *body, uint8_t relayAppSeq) {
	DIF::State.CmdSeq = (uint8_t)(DIF::State.CmdSeq + 1);
	PushPending(DIF::State.CmdSeq, relayAppSeq);

	char msg[48];                        // "#SS" + longest Dn dual body fits well under 48
	snprintf(msg, sizeof(msg), "%c%02X%s", APP_ACK_CHAR, DIF::State.CmdSeq, body);
	Send(msg);
}

/**
 * @brief  Send a diffuser command -- enveloped + relayed when g_difRelaySeq is armed
 *         (an app-originated command), otherwise a plain bare send.
 * @param  body  Diffuser command without envelope.
 */
void SendMaybeAck(const char *body) {
	if (g_difRelaySeq != 0xFF) { SendCmd(body, g_difRelaySeq); g_difRelaySeq = 0xFF; }
	else                         Send(body);
}

/**
 * @brief  Send "Df" -- shut the diffuser down.
 *
 * @note   While a parfum window is active, the diffuser queues this instead of
 *         applying it immediately -- it shuts down at the window's natural
 *         expiry only if no later Dn superseded it (see parfumStop() in the
 *         diffuser firmware). IdleCheck()'s own idle-pulse feature still
 *         skips itself entirely while DIF::State.ParfumMin > 0 -- unrelated, see there.
 */
void Shutdown() {
	// * LOG
    #ifdef ENABLE_LOG_DIF
        PRNT::_print(PRNT::formatMSG("%32s : shutdown diffuser" NL, "DIF_Shutdown"));
    #endif

	SendMaybeAck("Df");
}

/**
 * @brief  Send "DnXXrrggbbBREESP" (or dual "DnXXrrggbbRRGGBBBREESP") -- turn the diffuser on.
 *
 * Dual vs single is decided by EE_HB_DUAL_COLOR -- the diffuser only ever splits into
 * two colours when the TV strip itself is currently in Dual Color mode. Colour comes
 * from colorOverride when given (TV/Motion random-colour paths that computed a fresh
 * colour before LED::State.CurrentColor catches up); otherwise it's derived live via
 * ColorFromCurrentLEDs() -- i.e. "whatever's actually lit right now", any RGB value.
 * Brightness is EE_DIF_BRIGHTNESS run through LED::getLuxBrightness() -- the same
 * lux-compensation helper the TV strip itself uses, so ambient light changes are
 * reflected on the diffuser strip too (see LISENS::Check() calling
 * PushLiveIfActive() on every lux-bucket change) -- then rescaled from
 * [0, LED_BRIGHTNESS_MAX] to [0, 255] so the diffuser gets its full range instead
 * of being capped at LED_BRIGHTNESS_MAX (120). It's sent as a colour-scale factor,
 * never a NeoPixel hardware brightness register, so the diffuser always fades it
 * in smoothly.
 *
 * @param  mode          Target mode  -- 0 = off, 1..DIF_MODE_MAX = DIF_MODE_NAMES index.
 * @param  effect        Target effect -- 0=static 1=breathe 2=fade 3=pulse 4=random.
 * @param  colorOverride Explicit colour(s) to send, or NULL to derive from current LEDs.
 *
 * All values are validated before sending; out-of-range values are rejected and logged.
 * While a parfum window is active on the diffuser (DIF::State.ParfumMin > 0), it queues this
 * Dn instead of applying it now -- the *last* one received is applied once the window
 * naturally expires (see parfumStop() in the diffuser firmware), so it's still sent here.
 *
 * Called by: cmdTestMode_Diffuser() (test mode + random-colour path),
 * cmdDiffuserTurnOn() (app "Dn" command), cmdAmbientMode() (ambient recolour),
 * DIF::IdleCheck() (idle pulse), DIF::AutoOn() (source-activated auto-on),
 * DIF::PushLiveIfActive() (live re-push on setting change).
 */
void TurnOn(uint8_t mode, uint8_t effect, const DIF_Colorx *colorOverride) {
	#ifdef ENABLE_LOG_DIF
		if (DIF::State.ParfumMin > 0)                                          // Diffuser queues it instead of applying now - Logic
			PRNT::_print(PRNT::formatMSG("%~32s # parfum active [%d min], diffuser will queue this Dn" NL, "DIF_TurnOn", DIF::State.ParfumMin));
	#endif
	if (mode > DIF_MODE_MAX) {
		PRNT::_print(PRNT::formatMSG("%32s ! invalid mode (%d)" NL, "DIF_TurnOn", mode));
		return;
	}
	if (effect > DIF_EFFECT_COUNT) {
		PRNT::_print(PRNT::formatMSG("%32s ! invalid effect (%d)" NL, "DIF_TurnOn", effect));
		return;
	}

	const bool    dual       = EE::Get(EE_HB_DUAL_COLOR);                // Diffuser only splits when TV does - Setup
	// getLuxBrightness() is documented as clamped to [0,255], but its usual
	// output range for this call is [0, LED_BRIGHTNESS_MAX] (120) - the map()
	// below assumes that ceiling to rescale onto the diffuser's full 0-255
	// range. Clamp explicitly first so an edge case (e.g. EE_OTHER_BRIGHTNESS_AUTO
	// pushed above its normal 0/1 range) can never overshoot map()'s assumed
	// input ceiling and get non-linearly compressed - full brightness always
	// reaches a clean 255, never more, never a distorted value.
	const uint8_t rawBr      = (uint8_t)constrain(LED::getLuxBrightness(EE::Get(EE_DIF_BRIGHTNESS)), 0, LED_BRIGHTNESS_MAX);
	const uint8_t brightness = (uint8_t)constrain(map(rawBr, 0, LED_BRIGHTNESS_MAX, 0, 255), 0, 255); // Lux-compensated, rescaled to full 0-255 - Setup
	const uint8_t speedMs    = EE::Get(EE_DIF_SPEED);                    // Animated-effect frame period, ms - Setup
	DIF_Colorx c;
	if (colorOverride) c = *colorOverride;
	else                ColorFromCurrentLEDs(dual, c);              // Derive from what's actually lit - Action

	// * LOG
	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%~32s # turn on [mode:%d-%s] [effect:%d-%s] [dual:%T] [br:%d] [speed:%d]" NL,
			"DIF_TurnOn", mode, DIF::getModeName(mode), effect, DIF::getEffectName(effect), dual, brightness, speedMs));
	#endif

	if (dual) {
		SendMaybeAck(PRNT::formatMSG("Dn%2X%2X%2X%2X%2X%2X%2X%2X%2X%2X", mode, c.r1, c.g1, c.b1, c.r2, c.g2, c.b2, brightness, effect, speedMs));
	} else {
		SendMaybeAck(PRNT::formatMSG("Dn%2X%2X%2X%2X%2X%2X%2X", mode, c.r1, c.g1, c.b1, brightness, effect, speedMs));
	}
}

/**
 * @brief  (Re-)open the DIF UDP socket on DIF_UDP_PORT.
 *
 * Call after any WiFi reconnect to re-bind the port.
 */
void UdpSet() {
	// UDP Port
	DIF_UDP.begin(DIF_UDP_PORT);

	#ifdef ENABLE_LOG_DIF
		PRNT::_print(PRNT::formatMSG("%32s : UDP socket opened on port [%d]" NL, "DIF_UdpSet", DIF_UDP_PORT));
	#endif
}
} // namespace DIF


namespace EE {

// True if any w() call during the current chunked write cycle failed its
// readback verification. Reset at the start of each cycle in WriteTime(),
// checked at completion in Write() so a real EEPROM failure is reported to
// the app via Notify_Saved() instead of always claiming success.
static bool g_writeHadError = false;

/**
 * @brief  Schedule (or re-schedule) the chunked EEPROM write after EE_SAVE_TIME ms.
 *
 * Resets EE::State.Index to EE_START_READ_INDEX, kills any existing Write task,
 * and registers a new Write task to start after EE_SAVE_TIME ms (3 minutes).
 *
 * @note   Call after any change to EE_SET[], LED colours, or motion colour that
 *         should be persisted across power cycles.
 */
void WriteTime() {
	// --- EARLY EXIT: Skip scheduling if nothing changed ---
	// Check if ANY data needs writing before even scheduling the task
	bool hasChanges = false;

	// Check settings changes (any bit set in EE_Changed)
	for (int i = 0; i < 5; i++) {
		if (EE_Changed[i] != 0) {
			hasChanges = true;
			break;
		}
	}

	// Check color changes
	if (!hasChanges) {
		for (int i = 0; i < LED_NUM; i++) {
			if (BIT_TEST(EE_ColorChanged, i) || BIT_TEST(EE_AmbientChanged, i)) {
				hasChanges = true;
				break;
			}
		}
	}

	// Check UDP and Motion changes
	if (!hasChanges) {
		hasChanges = EE_UdpChanged || EE_MotionChanged;
	}

	// If nothing changed, skip write entirely
	if (!hasChanges) {
		#ifdef ENABLE_LOG_EEPROM
			PRNT::_print(PRNT::formatMSG("%~32s # No changes to persist, write skipped" NL, "WriteTime"));
		#endif
		return;
	}

	// * LOG
	#ifdef ENABLE_LOG_EEPROM
		PRNT::_print(PRNT::formatMSG("%~32s # Scheduling EEPROM Write Task" NL, "WriteTime"));
	#endif

    if (EE::State.tID != TASK_ID_NONE) { // If a write task is already scheduled, kill it before scheduling a new one
        TSK::KillID(EE::State.tID, "WriteTime"); // Kill existing write task to prevent overlap
    }

    // Schedule the write task to process data in chunks to prevent CPU lag
    EE::State.Index = EE_START_READ_INDEX;                                     // Reset pointer to start of memory - State
    g_writeHadError = false;                                                   // Fresh cycle - clear any prior failure - State

    EE::State.tID = TSK::AddTask("Write", "Write", Write, TASK_MS, EE_SAVE_DELAY_BETWEEN_CHUNKS, EE_SAVE_TIME, true); // Schedule write - Setup
}

/**
 * @brief  Chunked EEPROM write task -- delta mode: writes only changed data per invocation.
 *
 * Optimized to write ONLY changed settings and colors:
 *   - Settings: Only write indices with changed bit set
 *   - LED colors: Only write LEDs with EE_ColorChanged[n]=true
 *   - Ambient: Only write LEDs with EE_AmbientChanged[n]=true
 *   - UDP: Only write if EE_UdpChanged=true
 *   - Motion: Only write if EE_MotionChanged=true
 *
 * Uses EE::State.Index as a state machine pointer. Advances by 1 byte each call
 * with EE_SAVE_DELAY_BETWEEN_CHUNKS ms between calls. Self-terminates when done.
 *
 * @param  taskId  Task handle used to self-terminate when done.
 *
 * @note   Do not call directly -- scheduled by WriteTime().
 */
void Write(taskId_t taskId) {

    // --- EARLY EXIT: If nothing changed, skip write entirely ---
    if (EE::State.Index == 0) {  // Only check on first invocation
        // Check if ANY data needs writing
        bool hasChanges = false;
        
        // Check settings changes (any bit set in EE_Changed)
        for (int i = 0; i < 5; i++) {
            if (EE_Changed[i] != 0) {
                hasChanges = true;
                break;
            }
        }
        
        // Check color changes
        if (!hasChanges) {
            for (int i = 0; i < LED_NUM; i++) {
                if (BIT_TEST(EE_ColorChanged, i) || BIT_TEST(EE_AmbientChanged, i)) {
                    hasChanges = true;
                    break;
                }
            }
        }
        
        // Check UDP and Motion changes
        if (!hasChanges) {
            hasChanges = EE_UdpChanged || EE_MotionChanged;
        }
        
        // If nothing changed, abort the entire write cycle
        if (!hasChanges) {
            #ifdef ENABLE_LOG_EEPROM
                PRNT::_print(PRNT::formatMSG("%~32s # No changes detected, skipping write" NL, "Write"));
            #endif
            TSK::KillID(EE::State.tID, "Write");
            return;
        }
    }

    const int idx = EE::State.Index;                                           // Current pointer - Setup

    // --- ABSOLUTE MEMORY MAP ---
    const int ADDR_SET      = EE_START_READ_INDEX;                      // Settings Start - Mapping
    const int ADDR_COLOR    = ADDR_SET   + EE_MEM_X;                    // Color Start - Mapping
    const int ADDR_AMBIENT  = ADDR_COLOR + (LED_NUM << 2);              // Ambient Start - Mapping
    const int ADDR_UDP      = ADDR_AMBIENT + (LED_NUM << 2);            // UDP Start - Mapping
    const int ADDR_MOTION   = ADDR_UDP   + 4;                           // Motion Start - Mapping
    const int ADDR_MAX      = ADDR_MOTION + 3;                          // Total End - Mapping

    // 1. Settings -- Delta: write only changed
    if (idx < ADDR_COLOR) {
        int offset = idx - ADDR_SET;                                    // Calculate array index - Logic
        
        // Skip if this setting didn't change
        if (!IsChanged(offset)) {
            EE::State.Index++;                                                 // Skip to next - State
            return;
        }
        
        if (!w(idx, EE_SET[offset])) g_writeHadError = true;         // Write 1 byte - Action
        
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # Delta Write Setting [%d] Value [%d] @ EEPROM [%d]" NL, 
                "Write", offset, EE_SET[offset], idx));              // Log write - Sync
        #endif
        
        EE::State.Index++;                                                     // Advance pointer - State
        return;
    }

    // 2. LED Color Data -- Delta: write only changed LEDs
    if (idx < ADDR_AMBIENT) {
        int offset    = idx - ADDR_COLOR;                               // Distance from block start - Logic
        int n         = offset >> 2;                                    // LED index - Logic
        int component = offset % 4;                                     // Byte (R=0, G=1, B=2, Br=3) - Logic

        // Skip entire LED if not changed
        if (!BIT_TEST(EE_ColorChanged, n)) {
            EE::State.Index += 4 - component;                                  // Skip remaining bytes for this LED - State
            return;
        }

        // Write this byte
        uint8_t value;
        if (component == 0) {
            value = LED::State.StoredColor[n].r;                                   // Read R - Logic
        } else if (component == 1) {
            value = LED::State.StoredColor[n].g;                                   // Read G - Logic
        } else if (component == 2) {
            value = LED::State.StoredColor[n].b;                                   // Read B - Logic
        } else {
            value = LED::State.StoredBrightness[n];                                // Read Brightness - Logic
        }
        if (!w(idx, value)) g_writeHadError = true;                  // Save 1 byte - Action

        #ifdef ENABLE_LOG_EEPROM
            if (component == 3) {                                       // Log after the 4th byte (Br) - Logic
                PRNT::_print(PRNT::formatMSG("%~32s # Delta LED Color [%d] [R:%d G:%d B:%d Br:%d]" NL, 
                    "Write", n,                                      // Output
                    LED::State.StoredColor[n].r, LED::State.StoredColor[n].g, 
                    LED::State.StoredColor[n].b, LED::State.StoredBrightness[n]));            // Values - Output
            }
        #endif

        EE::State.Index++;                                                     // Advance pointer - State
        return;
    }

    // 3. Ambient Data -- Delta: write only changed ambient
    if (idx < ADDR_UDP) {
        int offset    = idx - ADDR_AMBIENT;                             // Distance from block start - Logic
        int n         = offset >> 2;                                    // LED index - Logic
        int component = offset % 4;                                     // Byte position - Logic

        // Skip entire LED if not changed
        if (!BIT_TEST(EE_AmbientChanged, n)) {
            EE::State.Index += 4 - component;                                  // Skip remaining bytes for this LED - State
            return;
        }

        // Write this byte
        uint8_t value;
        if (component == 0) {
            value = LED::State.AmbientBackgroundColor[n].r;                                 // Read R - Logic
        } else if (component == 1) {
            value = LED::State.AmbientBackgroundColor[n].g;                                 // Read G - Logic
        } else if (component == 2) {
            value = LED::State.AmbientBackgroundColor[n].b;                                 // Read B - Logic
        } else {
            value = LED::State.AmbientBackgroundBrightness[n];                             // Read Brightness - Logic
        }
        if (!w(idx, value)) g_writeHadError = true;                  // Save 1 byte - Action

        #ifdef ENABLE_LOG_EEPROM
            if (component == 3) {                                       // Log after full block - Logic
                PRNT::_print(PRNT::formatMSG("%~32s # Delta Ambient [%d] [R:%d G:%d B:%d Br:%d]" NL, 
                    "Write", n,                                      // Output
                    LED::State.AmbientBackgroundColor[n].r, LED::State.AmbientBackgroundColor[n].g, 
                    LED::State.AmbientBackgroundColor[n].b, LED::State.AmbientBackgroundBrightness[n]));  // Values - Output
            }
        #endif

        EE::State.Index++;                                                     // Advance pointer - State
        return;
    }

    // 4. Ambilight (UDPRAW - 4 bytes) -- Delta: write only if changed
    if (idx < ADDR_MOTION) {
        // Skip entire section if UDP didn't change
        if (!EE_UdpChanged) {
            EE::State.Index = ADDR_MOTION;                                     // Jump to motion section - State
            return;
        }

        int component = idx - ADDR_UDP;                                 // Byte position (0-3) - Logic
        uint8_t value;
        if (component == 0) {
            value = LED::State.StreamColor.r;                                     // Read R - Logic
        } else if (component == 1) {
            value = LED::State.StreamColor.g;                                     // Read G - Logic
        } else if (component == 2) {
            value = LED::State.StreamColor.b;                                     // Read B - Logic
        } else {
            value = LED::State.StreamBrightness;                                  // Read Brightness - Logic
        }
        if (!w(idx, value)) g_writeHadError = true;                  // Save 1 byte - Action

        #ifdef ENABLE_LOG_EEPROM
            if (component == 3) {                                       // Final byte log - Logic
                PRNT::_print(PRNT::formatMSG("%~32s # Delta UDP [R:%d G:%d B:%d Br:%d]" NL, 
                    "Write",                                         // Output
                    LED::State.StreamColor.r, LED::State.StreamColor.g, 
                    LED::State.StreamColor.b, LED::State.StreamBrightness));                // Values - Output
            }
        #endif

        EE::State.Index++;                                                     // Advance pointer - State
        return;
    }

    // 5. Motion Color (3 bytes) -- Delta: write only if changed
    if (idx < ADDR_MAX) {
        // Skip entire section if Motion didn't change
        if (!EE_MotionChanged) {
            EE::State.Index = ADDR_MAX;                                        // Jump to end - State
            return;
        }

        int component = idx - ADDR_MOTION;                              // Byte position (0-2) - Logic
        uint8_t colorByte = (component == 0) ? MOTION::State.Color.r : 
                            (component == 1) ? MOTION::State.Color.g : MOTION::State.Color.b;
        if (!w(idx, colorByte)) g_writeHadError = true;               // Save 1 byte - Action

        #ifdef ENABLE_LOG_EEPROM
            if (component == 2) {                                       // Final byte log - Logic
                PRNT::_print(PRNT::formatMSG("%~32s # Delta Motion [R:%d G:%d B:%d]" NL, 
                    "Write",                                         // Output
                    MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b));  // Values - Output
            }
        #endif

        EE::State.Index++;                                                     // Advance pointer - State
        return;
    }

    // --- DONE ---
    TSK::KillID(EE::State.tID, "Write");                                    // Stop task - Logic
    
    // Clear all change flags -- always, even on a partial failure, so a bad
    // byte doesn't get retried forever and starve the rest of the settings.
    ClearAllChanges();
    if (g_writeHadError) {
        PRNT::_print(PRNT::formatMSG("%32s ! one or more bytes failed readback verification" NL, "Write"));
    }
    APP::Notify_Saved(g_writeHadError ? 1 : 0);                          // Report the real outcome, not always success - Sync
    
    // Log completion
    PRNT::_print(PRNT::formatMSG("%~32s # Completed Delta EEPROM Write" NL, "Write")); // Log completion - Sync
}

/* ======================================================================= */
/* SETTINGS TABLE & HELPER FUNCTIONS                                       */
/* ======================================================================= */

/**
 * @brief  Settings metadata table (SIMPLIFIED)
 *
 * Each setting is defined by:
 *   - id: Enum value (maps to EE_Settings)
 *   - name: Human-readable name (stored in code flash, not EEPROM)
 *   - type: EE_TYPE_ONOFF (boolean) or EE_TYPE_NUMERIC (0-255)
 *   - index: EEPROM array index (0-39) where value is stored in EE_SET[]
 *
 * No default/min/max: Simplified for fast lookup and delta tracking.
 */
const EE_SettingDef EE_SETTINGS_TABLE[EE_MEM_X] PROGMEM = {
    { .id = EE_TV_ON_EFF, .name = "TV_ON_EFF", .type = EE_TYPE_NUMERIC, .index = 0 },
    { .id = EE_TV_OFF_EFF, .name = "TV_OFF_EFF", .type = EE_TYPE_NUMERIC, .index = 1 },
    { .id = EE_TV_OFF_TIME, .name = "TV_OFF_TIME", .type = EE_TYPE_NUMERIC, .index = 2 },
    { .id = EE_TV_BR_COM, .name = "TV_BR_COM", .type = EE_TYPE_NUMERIC, .index = 3 },
    { .id = EE_TV_BR_UCOM, .name = "TV_BR_UCOM", .type = EE_TYPE_NUMERIC, .index = 4 },
    { .id = EE_TV_BR_BED, .name = "TV_BR_BED", .type = EE_TYPE_NUMERIC, .index = 5 },
    { .id = EE_TV_BR_LAMP, .name = "TV_BR_LAMP", .type = EE_TYPE_NUMERIC, .index = 6 },
    { .id = EE_MOTION_BRIGHTNESS, .name = "MOTION_BRIGHTNESS", .type = EE_TYPE_NUMERIC, .index = 7 },
    { .id = EE_MOTION_ON_TIME, .name = "MOTION_ON_TIME", .type = EE_TYPE_NUMERIC, .index = 8 },
    { .id = EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX, .name = "UDPRAW_AMBILIGHT_BR_MAX", .type = EE_TYPE_NUMERIC, .index = 9 },
    { .id = EE_MOTION_RANDOM_COLOR, .name = "MOTION_RANDOM_COLOR", .type = EE_TYPE_ONOFF, .index = 10 },
    { .id = EE_OTHER_BR_CL_DEL, .name = "OTHER_BR_CL_DEL", .type = EE_TYPE_NUMERIC, .index = 11 },
    { .id = EE_OTHER_BR_CL_INC, .name = "OTHER_BR_CL_INC", .type = EE_TYPE_NUMERIC, .index = 12 },
    { .id = EE_OTHER_BRIGHTNESS_AUTO, .name = "OTHER_BRIGHTNESS_AUTO", .type = EE_TYPE_ONOFF, .index = 13 },
    { .id = EE_OTHER_TO_OFF_TIME, .name = "OTHER_TO_OFF_TIME", .type = EE_TYPE_NUMERIC, .index = 14 },
    { .id = EE_TV_RANDOM_COLOR_START, .name = "TV_RANDOM_COLOR_START", .type = EE_TYPE_NUMERIC, .index = 15 },
    { .id = EE_MOTION_DIVIDE_BRIGHTNESS, .name = "MOTION_DIVIDE_BRIGHTNESS", .type = EE_TYPE_ONOFF, .index = 16 },
    { .id = EE_TV_BR_TV, .name = "TV_BR_TV", .type = EE_TYPE_NUMERIC, .index = 17 },
    { .id = EE_MOTION_RENEW_COLOR_TIME, .name = "MOTION_RENEW_COLOR_TIME", .type = EE_TYPE_NUMERIC, .index = 18 },
    { .id = EE_MOTION_AUTO_OFF_TIME, .name = "MOTION_AUTO_OFF_TIME", .type = EE_TYPE_NUMERIC, .index = 19 },
    { .id = EE_MOTION_ON_EFF, .name = "MOTION_ON_EFFECT", .type = EE_TYPE_NUMERIC, .index = 20 },
    { .id = EE_OTHER_AMBIENT_MODE_TIME, .name = "OTHER_AMBIENT_MODE_TIME", .type = EE_TYPE_NUMERIC, .index = 21 },
    { .id = EE_OTHER_LED_FPS, .name = "OTHER_LED_FPS", .type = EE_TYPE_NUMERIC, .index = 22 },
    { .id = EE_TV_ON_BR_CL_DEL, .name = "TV_ON_BR_CL_DEL", .type = EE_TYPE_NUMERIC, .index = 23 },
    { .id = EE_TV_ON_BR_CL_INC, .name = "TV_ON_BR_CL_INC", .type = EE_TYPE_NUMERIC, .index = 24 },
    { .id = EE_TV_OFF_BR_CL_DEL, .name = "TV_OFF_BR_CL_DEL", .type = EE_TYPE_NUMERIC, .index = 25 },
    { .id = EE_TV_OFF_BR_CL_INC, .name = "TV_OFF_BR_CL_INC", .type = EE_TYPE_NUMERIC, .index = 26 },
    { .id = EE_MOTION_BR_CL_DEL, .name = "MOTION_BR_CL_DEL", .type = EE_TYPE_NUMERIC, .index = 27 },
    { .id = EE_MOTION_BR_CL_INC, .name = "MOTION_BR_CL_INC", .type = EE_TYPE_NUMERIC, .index = 28 },
    { .id = EE_UDPRAW_BR_CL_DEL, .name = "UDPRAW_BR_CL_DEL", .type = EE_TYPE_NUMERIC, .index = 29 },
    { .id = EE_UDPRAW_BR_CL_INC, .name = "UDPRAW_BR_CL_INC", .type = EE_TYPE_NUMERIC, .index = 30 },
    { .id = EE_HB_DUAL_COLOR, .name = "HB_DUAL_COLOR", .type = EE_TYPE_ONOFF, .index = 31 },
    { .id = EE_HB_EFFECT, .name = "HB_EFFECT", .type = EE_TYPE_NUMERIC, .index = 32 },
    { .id = EE_HB_EFFECT_SPEED, .name = "HB_EFFECT_SPEED", .type = EE_TYPE_NUMERIC, .index = 33 },
    { .id = EE_TV_ON_HB_EFF, .name = "TV_ON_HB_EFFECT", .type = EE_TYPE_NUMERIC, .index = 34 },
    { .id = EE_DIF_EFFECT,      .name = "DIF_EFFECT",      .type = EE_TYPE_NUMERIC, .index = 35 },
    { .id = EE_DIF_MODE_TV,      .name = "DIF_MODE_TV",      .type = EE_TYPE_NUMERIC, .index = 36 },
    { .id = EE_DIF_MODE_MOTION,  .name = "DIF_MODE_MOTION",  .type = EE_TYPE_NUMERIC, .index = 37 },
    { .id = EE_DIF_MODE_UDPRAW,  .name = "DIF_MODE_UDPRAW",  .type = EE_TYPE_NUMERIC, .index = 38 },
    { .id = EE_DIF_MODE_AMBIENT, .name = "DIF_MODE_AMBIENT", .type = EE_TYPE_NUMERIC, .index = 39 },
    /* -- expansion block 40-49 -- */
    { .id = EE_DIF_IDLE_WAIT_MIN, .name = "DIF_IDLE_WAIT_MIN", .type = EE_TYPE_NUMERIC, .index = 40 },
    { .id = EE_DIF_IDLE_ON_MIN,   .name = "DIF_IDLE_ON_MIN",   .type = EE_TYPE_NUMERIC, .index = 41 },
    { .id = EE_DIF_IDLE_MODE,     .name = "DIF_IDLE_MODE",     .type = EE_TYPE_NUMERIC, .index = 42 },
    { .id = EE_DIF_BRIGHTNESS, .name = "DIF_BRIGHTNESS", .type = EE_TYPE_NUMERIC, .index = 43 },
    { .id = EE_DIF_SPEED,      .name = "DIF_SPEED",      .type = EE_TYPE_NUMERIC, .index = 44 },
    { .id = 45, .name = "RESERVED_45", .type = EE_TYPE_NUMERIC, .index = 45 },
    { .id = 46, .name = "RESERVED_46", .type = EE_TYPE_NUMERIC, .index = 46 },
    { .id = 47, .name = "RESERVED_47", .type = EE_TYPE_NUMERIC, .index = 47 },
    { .id = 48, .name = "RESERVED_48", .type = EE_TYPE_NUMERIC, .index = 48 },
    { .id = 49, .name = "RESERVED_49", .type = EE_TYPE_NUMERIC, .index = 49 }
};

/**
 * @brief  Clear all EEPROM change tracking after successful write.
 */
void ClearAllChanges() {
    #ifdef ENABLE_LOG_EEPROM
        int changedCount = getChangedCount();
        int ledCount = 0, ambientCount = 0;
        for (int i = 0; i < LED_NUM; i++) {
            if (BIT_TEST(EE_ColorChanged, i)) ledCount++;
            if (BIT_TEST(EE_AmbientChanged, i)) ambientCount++;
        }
        PRNT::_print(PRNT::formatMSG("%~32s # CLEAR_ALL_CHANGES Cleared [%d] settings, [%d] LED colors, [%d] ambient, UDP [%s], Motion [%s]" NL,
            "ClearAllChanges", changedCount, ledCount, ambientCount,
            EE_UdpChanged ? "yes" : "no", EE_MotionChanged ? "yes" : "no"));
    #endif

    memset(EE_Changed, 0, sizeof(EE_Changed));
    BIT_CLEAR_ALL(EE_ColorChanged, sizeof(EE_ColorChanged));
    BIT_CLEAR_ALL(EE_AmbientChanged, sizeof(EE_AmbientChanged));
    EE_UdpChanged = false;
    EE_MotionChanged = false;
}

/**
 * @brief  Get setting value with type checking
 *
 * Retrieves the value from EE_SET[] array using the setting's defined index.
 * If the setting ID is not found, returns the default value from the definition.
 *
 * @param  settingId  Setting ID from EE_Settings enum
 * @return Setting value (0-255), or default value if not found
 */
uint8_t Get(uint8_t settingId) {
    if (settingId >= EE_MEM_X) {
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%32s ! GET error, ID [%d] out of range" NL, "EE_Get", settingId));
        #endif
        return 0;
    }
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # GET [%d] -> [%d]" NL, "EE_Get", settingId, EE_SET[settingId]));
    #endif
    return EE_SET[settingId];
}

/**
 * @brief  Get count of changed settings for logging/debug.
 *
 * @return Number of settings marked as changed
 */
int getChangedCount() {
    int count = 0;
    for (int i = 0; i < EE_MEM_X; i++) {
        if (IsChanged(i)) count++;
    }
    
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # GET_CHANGED_COUNT Total changed settings [%d]" NL, 
            "getChangedCount", count));
    #endif
    
    return count;
}

/**
 * @brief  Get setting definition by ID
 *
 * @param  settingId  Setting ID from EE_Settings enum
 * @return Pointer to EE_SettingDef, or nullptr if not found
 *
 * Time Complexity: O(EE_MEM_X) -- linear search
 */
const EE_SettingDef* getDef(uint8_t settingId) {
    for (int i = 0; i < EE_MEM_X; i++) {
        const EE_SettingDef* def = &EE_SETTINGS_TABLE[i];
        if (def->id == settingId) {
            return def;
        }
    }
    return nullptr;
}

/**
 * @brief  Get setting definition by array index
 *
 * @param  index  Array index (0-39)
 * @return Pointer to EE_SettingDef at that index
 */
const EE_SettingDef* getDefByIndex(uint8_t index) {
    if (index >= EE_MEM_X) return nullptr;
    return &EE_SETTINGS_TABLE[index];
}

/**
 * @brief  Get setting name from metadata
 *
 * @param  settingId  Setting ID
 * @return Pointer to setting name string (in PROGMEM), or nullptr if not found
 */
const char* getName(uint8_t settingId) {
    const EE_SettingDef* def = getDef(settingId);
    return def ? def->name : nullptr;
}

/**
 * @brief  True when a setting name starts with one of EE_GROUPS.
 *
 * @param  name  EE_SettingDef name, e.g. "TV_OFF_TIME".
 */
bool InAnyGroup(const char* name) {
    for (uint8_t g = 0; g < EE_GROUP_COUNT; g++) {
        const char* groupPrefix = (const char*)pgm_read_ptr(&EE_GROUPS[g]);
        if (strncmp(name, groupPrefix, strlen(groupPrefix)) == 0) return true;
    }
    return false;
}

/**
 * @brief  Check if a setting has been marked as changed.
 *
 * @param  index  Setting index (0-39)
 * @return true if changed, false otherwise
 */
inline bool IsChanged(uint8_t index) {
    if (index >= EE_MEM_X) {
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%32s ! IS_CHANGED error, index [%d] out of bounds (max [%d])" NL, 
                "IsChanged", index, EE_MEM_X - 1));
        #endif
        return false;
    }
    bool changed = BIT_TEST(EE_Changed, index);
    
    #ifdef ENABLE_LOG_EEPROM
        const EE_SettingDef* def = getDefByIndex(index);
        const char* name = def ? def->name : "UNKNOWN";
        PRNT::_print(PRNT::formatMSG("%~32s # IS_CHANGED Index [%d] Name [%s] Changed [%s]" NL, 
            "IsChanged", index, name, changed ? "YES" : "NO"));
    #endif
    
    return changed;
}

/**
 * @brief  Print one EEPROM setting with its type-appropriate value.
 *
 * @param  def  Setting metadata; ignored when null.
 */
void LogSetting(const EE_SettingDef* def) {
    if (!def) return;
    uint8_t value = EE::Get(def->id);
    if (def->type == EE_TYPE_ONOFF) {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "LogSetting", "%s [%s]", def->name, (value) ? "ON" : "OFF");
    } else {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "LogSetting", "%s [%d]", def->name, value);
    }
}

/**
 * @brief  Mark ambient color as changed
 * @param  ledIndex  LED index (0 to LED_NUM-1)
 */
void MarkAmbientChanged(uint8_t ledIndex) {
    if (ledIndex < LED_NUM) {
        BIT_SET(EE_AmbientChanged, ledIndex);
        
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # MARK_AMBIENT_CHANGED LED [%d]" NL, 
                "MarkAmbientChanged", ledIndex));
        #endif
    }
}

/**
 * @brief  Mark a setting as changed for delta tracking.
 *
 * @param  index  Setting index (0-39)
 */
inline void MarkChanged(uint8_t index) {
    if (index < EE_MEM_X) {
        BIT_SET(EE_Changed, index);
        
        #ifdef ENABLE_LOG_EEPROM
            const EE_SettingDef* def = getDefByIndex(index);
            const char* name = def ? def->name : "UNKNOWN";
            uint8_t byteIdx = index / 8;
            uint8_t bitIdx = index % 8;
            PRNT::_print(PRNT::formatMSG("%~32s # MARK_CHANGED Index [%d] Name [%s] (Byte[%d] Bit[%d])" NL, 
                "MarkChanged", index, name, byteIdx, bitIdx));
        #endif
    }
}

/**
 * @brief  Mark LED color as changed
 * @param  ledIndex  LED index (0 to LED_NUM-1)
 */
void MarkColorChanged(uint8_t ledIndex) {
    if (ledIndex < LED_NUM) {
        BIT_SET(EE_ColorChanged, ledIndex);
        
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # MARK_COLOR_CHANGED LED [%d]" NL, 
                "EE_MarkColorChanged", ledIndex));
        #endif
    }
}

/**
 * @brief  Mark motion color as changed
 */
void MarkMotionChanged() {
    EE_MotionChanged = true;
    
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # MARK_MOTION_CHANGED [R:%d G:%d B:%d]" NL, 
            "EE_MarkMotionChanged", MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b));
    #endif
}


/**
 * @brief  Mark UDP/Ambilight color and brightness as changed
 */
void MarkUdpChanged() {
    EE_UdpChanged = true;
    
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # MARK_UDP_CHANGED [R:%d G:%d B:%d Br:%d]" NL, 
            "EE_MarkUdpChanged", LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b, LED::State.StreamBrightness));
    #endif
}
/* ------------------------------------------------------------------------ */
/* EE                                                                         */
/* EEPROM persistence -- settings table read/write, LED/ambient/motion colour  */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Read all persisted data from EEPROM into live structs on startup.
 *
 * Reads in order:
 *   1. Settings bytes -> EE_SET[0..EE_MEM_X-1]
 *   2. LED colours/brightness -> LED::State.StoredColor[], LED::State.StoredBrightness[]
 *   3. Ambient colours/brightness -> LED::State.AmbientBackgroundColor[], LED::State.AmbientBackgroundBrightness[]
 *   4. UDPRAW colour -> LED::State.StreamColor, LED::State.StreamBrightness
 *   5. Motion colour -> MOTION::State.Color
 *
 * Call once in setup() after LED::Setup() and before the main loop.
 *
 * @note   Does not write to hardware LEDs -- call LED::Setup() first so arrays are sized.
 */
void Read() {
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # Reading EEPROM Data" NL, "EE_Read")); // Log start - Sync
    #endif

    int sEntry = EE_START_READ_INDEX;                                   // Map Start - Mapping
    int tEntry = sEntry + EE_MEM_X;                                     // Map End Settings - Mapping


	#ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # Reading Settings from EEPROM [%d to %d]" NL, 
            "EE_Read", sEntry, tEntry - 1)); // Log settings range - Sync
    #endif

    // 1. Settings
    for (int i = sEntry; i < tEntry; i++) {
        EE_SET[i - sEntry] = EEPROM.read(i);                            // Read single byte - Logic
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # Read index [%d] - Value [%d]" NL, 
                "EE_Read", i, EE_SET[i - sEntry])); // Log each setting - Sync
        #endif
    }

    // 2. Color Data (4-Byte Chunks)
    sEntry = tEntry - 1;                                                // Shift to Colors - Mapping
    tEntry = sEntry + (LED_NUM << 2);                                   // LED_NUM * 4 - Mapping
    
	for (int i = sEntry, n = 0; i < tEntry; i += 4, n++) {
        LED::State.StoredColor[n].r = EEPROM.read(i + 1);                          // Read R - Logic
        LED::State.StoredColor[n].g = EEPROM.read(i + 2);                          // Read G - Logic
        LED::State.StoredColor[n].b = EEPROM.read(i + 3);                          // Read B - Logic
        LED::State.StoredBrightness[n] = EEPROM.read(i + 4);                       // Read Brightness - Logic
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # Read Color for LED [%d] from EEPROM [%d to %d] - [R=%d G=%d B=%d Br=%d]" NL, 
                "EE_Read", n, i + 1, i + 4, LED::State.StoredColor[n].r, LED::State.StoredColor[n].g, LED::State.StoredColor[n].b, LED::State.StoredBrightness[n])); // Log each color - Sync
        #endif
    }

    // 3. Ambient Data (4-Byte Chunks)
    sEntry = tEntry;                                                    // Shift to Ambient - Mapping
    tEntry = sEntry + (LED_NUM << 2);                                   // LED_NUM * 4 - Mapping
    
	for (int i = sEntry, n = 0; i < tEntry; i += 4, n++) {
        LED::State.AmbientBackgroundColor[n].r = EEPROM.read(i + 1);                        // Read R - Logic
        LED::State.AmbientBackgroundColor[n].g = EEPROM.read(i + 2);                        // Read G - Logic
        LED::State.AmbientBackgroundColor[n].b = EEPROM.read(i + 3);                        // Read B - Logic
        LED::State.AmbientBackgroundBrightness[n] = EEPROM.read(i + 4);                    // Read Brightness - Logic
		
		#ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%~32s # Read Ambient for LED [%d] from EEPROM [%d to %d] - [R=%d G=%d B=%d BrVal=%d]" NL, 
                "EE_Read", n, i + 1, i + 4, LED::State.AmbientBackgroundColor[n].r, LED::State.AmbientBackgroundColor[n].g, LED::State.AmbientBackgroundColor[n].b, LED::State.AmbientBackgroundBrightness[n])); // Log each ambient color - Sync
		#endif
	}

    // 4. Ambilight (UDPRAW)
    sEntry = tEntry;                                                    // Shift to UDP section - Mapping
    
    for (int i = 0; i < 4; i++) {                                       // Start at 0, end at 3 - Logic
        // Map EEPROM 4-byte UDP color data to CRGB + brightness
        if (i == 0) LED::State.StreamColor.r = EEPROM.read(sEntry + i + 1);       // Read R - Mapping
        else if (i == 1) LED::State.StreamColor.g = EEPROM.read(sEntry + i + 1);  // Read G - Mapping
        else if (i == 2) LED::State.StreamColor.b = EEPROM.read(sEntry + i + 1);  // Read B - Mapping
        else LED::State.StreamBrightness = EEPROM.read(sEntry + i + 1);           // Read Brightness - Mapping
    }

    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # Read Ambilight (UDPRAW) from EEPROM [%d to %d] - [R=%d G=%d B=%d Br=%d]" NL, 
            "EE_Read", sEntry + 1, sEntry + 4, LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b, LED::State.StreamBrightness)); // Log data - Sync 
    #endif

    // 5. Motion Color
    sEntry += 4;                                                        // Shift to Motion section - Mapping
    
    uint8_t colorComponents[3];
    for (int i = 0; i < 3; i++) {                                       // Start at 0, end at 2 - Logic
        colorComponents[i] = EEPROM.read(sEntry + i + 1);               // Load from EEPROM (index + 1) - Mapping
    }
    MOTION::State.Color.r = colorComponents[0];
    MOTION::State.Color.g = colorComponents[1];
    MOTION::State.Color.b = colorComponents[2];
    
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # Read Motion Color from EEPROM [%d to %d] - [R=%d G=%d B=%d]" NL, 
            "EE_Read", sEntry + 1, sEntry + 3, MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b)); // Log data - Sync
    #endif


    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # Completed EEPROM Read" NL, "EE_Read")); // Log completion - Sync
    #endif
}

/**
 * @brief  Set setting value and mark as changed for delta sync.
 *
 * Updates EE_SET[index] and marks the setting in EE_Changed bitset.
 * No bounds validation (user responsibility). Call WriteTime() to persist.
 *
 * @param  settingId  Setting ID from EE_Settings enum
 * @param  value      New value to set (0-255)
 * @return true if setting exists, false if not found
 *
 * @note   After calling, use WriteTime() to schedule EEPROM write.
 */
bool Set(uint8_t settingId, uint8_t value) {
    if (settingId >= EE_MEM_X) {
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%32s ! SET error, ID [%d] out of range" NL, "EE_Set", settingId));
        #endif
        return false;
    }
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%~32s # SET [%d] [%d] -> [%d]" NL, "EE_Set", settingId, EE_SET[settingId], value));
    #endif
    EE_SET[settingId] = value;
    MarkChanged(settingId);
    return true;
}

/**
 * @brief  Write one byte to EEPROM with readback verification.
 *
 * Uses EEPROM.update() (only writes if the value changed) then reads back
 * to verify the write succeeded.
 *
 * @param  index  EEPROM address to write.
 * @param  value  Byte value to write.
 *
 * @return true if the readback matches, false on write failure (logs an error).
 */
bool w(int index, int value) {
    EEPROM.update(index, value);

    
    const int readback = EEPROM.read(index);
    if (readback != value) {
        PRNT::_print(PRNT::formatMSG("%32s ! error at index [%d], wanted [%d], readback [%d]" NL, "w", index, value, readback));
        #ifdef ENABLE_LOG_EEPROM
            PRNT::_print(PRNT::formatMSG("%32s : WRITE_FAILED EEPROM [%d] expected [%d] got [%d]" NL, "w", index, value, readback));
        #endif
        return false;
    }
    
    #ifdef ENABLE_LOG_EEPROM
        PRNT::_print(PRNT::formatMSG("%32s : WRITE_OK EEPROM [%d] value [%d] verified" NL, "w", index, value));
    #endif
    
    return true;
}
} // namespace EE


namespace UDPRAW {
/* ------------------------------------------------------------------------ */
/* UDPRAW                                                                     */
/* Ambilight UDP stream receiver - T_UDPRAW_SET_COLOR                         */
/* ------------------------------------------------------------------------ */

/**
 * True whenever the non-TV zones (COM/BED/LAMP/HB, pushed via stripBack/
 * stripHB) actually need to be re-latched - they only change during the
 * one-shot T_UDPRAW_SET_COLOR ramp (or the Init()/End() blank), not on every
 * ambilight packet, so Loop() skips those two show() calls (238 LEDs total,
 * dominated by stripHB's 178) whenever nothing in them moved since the last
 * push. stripFront (the 30 live TV pixels) always shows - that's the whole
 * point of the stream. Starts true so Init()'s blank always gets pushed on
 * the very first packet of a new stream.
 */
static bool g_nonTvDirty = true;

/**
 * FPS tracking for the live ambilight stream - see Loop()'s frame counter and
 * the _debug_udpraw dump. Reset on Init()/End() so a stream restart doesn't
 * carry over a stale window from before the gap.
 */
static uint32_t g_fpsFrameCount  = 0;
static uint32_t g_fpsWindowStart = 0;


/**
 * @brief  Initialise the UDPRAW ambilight UDP listener.
 *
 * Opens UDPRAW_UDP on port 5568 if WiFi is connected.
 * Call once in setup() after WiFi is connected.
 *
 * @note   If WiFi is not connected yet, UdpSet() will be retried
 *         by Check() when the connection is established.
 */
void Setup() {
	PRNT::_print(PRNT::formatMSG("%~32s # begin" NL, "UDPRAW_Setup"));

	if (NET::IsConnected()) {
		UdpSet();

		#ifdef ENABLE_LOG_UDPRAW
			PRNT::_print(PRNT::formatMSG("%32s : UDP port set to [5568]" NL, "UDPRAW_Setup"));
		#endif
	} else {
		PRNT::_print(PRNT::formatMSG("%~32s # WiFi not connected" NL, "UDPRAW_Setup"));
	}
}

/**
 * @brief  Receive and apply one ambilight UDP packet per loop iteration.
 *
 * Checks for a packet of exactly (LED_TV_NUM x 3) bytes on port 5568.
 * Maps each RGB triplet directly to the corresponding TV LED via LED::H_writeStripPixel()
 * using Lux-adjusted brightness (fast bit-shift scaling).
 * Calls Init() on the first packet after idle, and End() if
 * no packet is received for UDPRAW_CHECK_TIME ms.
 *
 * TestMode _testmode_udpraw simulates a full-white packet for testing.
 *
 * @note   Call every loop() iteration when LED::State.Enabled is true.
 */
void Loop() {
    // 1. WiFi status: use the shared cache updated once per loop() cycle
    bool isConnected = NET::Connected_Cached();                          // Read shared cache - Logic
    
    if (!isConnected && TestMode != _testmode_udpraw) return;           // Skip if offline - Logic

    // 2. UDPRAW stays unthrottled - ambilight stream needs fast response
    int packetSize = UDPRAW_UDP.parsePacket();                          // Hardware check - Action
    bool isSimulated = (TestMode == _testmode_udpraw);                  // Flag - State

    if (isSimulated) packetSize = (LED_TV_NUM * 3);                     // Override for test - Setup

    if (!packetSize) {                                                  // No new data - Logic
        if (UDPRAW::State.Status && (TimeNow - UDPRAW::State.LastCheck > UDPRAW_CHECK_TIME)) { 
            End();                                               // Timeout - Action
        }
        return;                                                         // Immediate Exit (Saves time) - Logic
    }

    // 3. Validation
    if (packetSize != (LED_TV_NUM * 3)) {                               // Strict check - Logic
        #ifdef ENABLE_LOG_UDPRAW
            PRNT::_print(PRNT::formatMSG("%32s : invalid packet size [%d] expected [%d]" NL, "UDPRAW_Loop", packetSize, (LED_TV_NUM * 3)));
        #endif
        UDPRAW_UDP.flush();                                             // Clear buffer - Action
        return;                                                         // Exit - Logic
    }

    // 4. Ingestion
    if (isSimulated) {
        memset(UDPRAW_Buffer, 255, packetSize);                         // Fast fill (Faster than loop) - Action
    } else {
        UDPRAW_UDP.read(UDPRAW_Buffer, packetSize);                     // Bulk read - Action
    }
    
    UDPRAW::State.LastCheck = TimeNow;                                         // Reset watchdog - State
    if (!UDPRAW::State.Status) Init();                                  // Start mode - Action

    // FPS tracking - recomputed once per ~1s rolling window, exposed via
    // UDPRAW::State.Fps for the debug dump (see _debug_udpraw). Counts every
    // valid frame reaching this point, real or test-simulated.
    g_fpsFrameCount++;
    if (TimeNow - g_fpsWindowStart >= 1000) {
        UDPRAW::State.Fps = (g_fpsWindowStart == 0) ? 0.0f
                           : (g_fpsFrameCount * 1000.0f) / (float)(TimeNow - g_fpsWindowStart);
        g_fpsFrameCount  = 0;
        g_fpsWindowStart = TimeNow;
    }

    // 5. High-Speed Pixel Mapping
    // Cache brightness for performance - refresh every 100 packets or when status changes
    static uint32_t packetCount = 0;
    if (packetCount++ % 100 == 0 || UDPRAW::State.CachedBrightness == 0) {
        UDPRAW::State.CachedBrightness = LED::getLuxBrightness(EE::Get(EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX));
    }
    const uint8_t br = UDPRAW::State.CachedBrightness;
    uint8_t* buf = UDPRAW_Buffer;                                       // Pointer - Setup

    #ifdef ENABLE_LOG_UDPRAW
        PRNT::_print(PRNT::formatMSG("%32s : packet received [%d bytes] lux brightness [%d]" NL, "UDPRAW_Loop", packetSize, br));
    #endif

    for (int i = 0; i < LED_TV_NUM; i++) {                              // Iterate - Logic
        int ledIdx = LED::TV(i);                                         // Hardware mapping - Mapping
        
        // Optimized color scaling for NeoPixel
        // Simple 8-bit addition with brightness scaling
        uint8_t r = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));             // Optimized R - Logic
        uint8_t g = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));             // Optimized G - Logic
        uint8_t b = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));
        LED::H_writeStripPixel(ledIdx, r, g, b);             // Optimized B - Logic
    }

    // 6. Push to strip
    stripFront.show();                                                     // Always - these 30 TV pixels are what the stream is for
    if (g_nonTvDirty) {                                                    // Only re-latch COM/BED/LAMP/HB (238 LEDs) if they actually moved
        stripBack.show();
        stripHB.show();
        g_nonTvDirty = false;
    }
}

/**
 * @brief  Smooth-fade task: transition non-TV zones to the current UDP colour.
 *
 * Steps all COM, BED, LAMP, and HB LEDs toward LED::State.StreamColor at
 * LED::State.StreamBrightness (Lux-adjusted) using EE_SET[EE_UDPRAW_BR_CL_INC].
 * Self-terminates when all LEDs have reached their target.
 *
 * @param  taskId  Task handle used to self-terminate on completion.
 *
 * @note   Registered by Init() with UDPRAW_LED_MOVIE_DELAY start delay.
 */
void T_UDPRAW_SET_COLOR(taskId_t taskId) { // Remote Network Control Transition
    bool moving = false;                                                // Track transition status - State
    
    // Pre-fetch values once to save CPU cycles during the iteration
    const int targetBr = LED::getLuxBrightness(LED::State.StreamBrightness);           // Cache universal target brightness - Setup
    const int inc = EE::Get(EE_UDPRAW_BR_CL_INC);                        // Raw EE step - UDPRAW is NOT speed-adapted - Setup
    const uint8_t r = LED::State.StreamColor.r;                                   // Cache target Red - Setup
    const uint8_t g = LED::State.StreamColor.g;                                   // Cache target Green - Setup
    const uint8_t b = LED::State.StreamColor.b;                                   // Cache target Blue - Setup

    // 1. Process Main Room Zones (COM, BED, LAMP)
    // This loop flattens the zone mapping into a single pass for efficiency.
    for (int i = 0; i < LED_COM_NUM + LED_BED_NUM + LED_LAMP_NUM; i++) {
        int currentLed;
        
        // Map abstract index to specific hardware zone functions
        if (i < LED_COM_NUM) {
            currentLed = LED::COM(i);                                    // Map to COM - Mapping
        } else if (i < LED_COM_NUM + LED_BED_NUM) {
            currentLed = LED::BED(i - LED_COM_NUM);                      // Map to BED - Mapping
        } else {
            currentLed = LED::LAMP(i - LED_COM_NUM - LED_BED_NUM);       // Map to LAMP - Mapping
        }

        // Shift brightness toward the UDP-requested target
        if (LED::TG_BRIGHTNESS(currentLed, targetBr, inc, false)) {      
            LED::setPixel(currentLed, r, g, b, LED::State.CurrentBrightness[currentLed], false); 
            moving = true;                                              // Mark frame as active - State
        }
    }

    // 2. Process Heartbeat (HB) Zone
    // HB follows the same network-provided brightness and color for room-wide sync.
    for (int i = 0; i < LED_HB_NUM; i++) {
        int hIdx = LED::HB(i);                                           // Map to HB hardware - Mapping
        
        if (LED::TG_BRIGHTNESS(hIdx, targetBr, inc, false)) {             
            LED::setPixel(hIdx, r, g, b, LED::State.CurrentBrightness[hIdx], false);    // Apply UDP color - Output
            moving = true;                                              // Mark frame as active - State
        }
    }

    // 3. Finalize Frame
    if (moving) {
        g_nonTvDirty = true;   // COM/BED/LAMP/HB actually moved this tick - Loop() must re-latch them next packet
    } else {
        #ifdef ENABLE_LOG_UDPRAW
            PRNT::_print(PRNT::formatMSG("%32s : transition complete - all LEDs at target [R:%d G:%d B:%d] brightness [%d]" NL, "T_UDPRAW_SET_COLOR", r, g, b, targetBr));
        #endif

        APP::updDeltaColors();                                     // Update application UI - Sync
        TSK::KillTasksAvoidLocked("T_UDPRAW_SET_COLOR");                  // Terminate task - Logic
    }
}


/**
 * @brief  Deactivate UDPRAW streaming mode on timeout.
 *
 * Resets MOTION::State.Status = motON and UDPRAW::State.Status = false, clears all LEDs,
 * and pushes updated status and colour state to the app. Deliberately leaves
 * TV::State.Status untouched -- it's continuously pin-tracked by TV::Status() even
 * while UDPRAW is active, so it already reflects the real TV state; forcing
 * it false here would wrongly report TV off (and let DIF::AutoOff() kill the
 * diffuser) while the TV is actually still on.
 * Called when no packet is received for UDPRAW_CHECK_TIME ms.
 *
 * @note   Called internally by Loop(). Can also be called manually
 *         to force-stop ambilight mode.
 */
void End(bool handover) {
	// * LOG
	#ifdef ENABLE_LOG_UDPRAW
		PRNT::_print(PRNT::formatMSG("%32s : stopping - stream timeout [%lu ms]" NL, "UDPRAW_End", (TimeNow - UDPRAW::State.LastCheck)));
	#endif
	PRNT::_print(PRNT::formatMSG("%~32s # stream end, inc [%d], delay [%d]" NL, "UDPRAW_End", EE::Get(EE_UDPRAW_BR_CL_INC), EE::Get(EE_UDPRAW_BR_CL_DEL))); // Log, raw speed (not adapted) - Sync

    // * preset tv status / motion handover
	//
	// motON is the armed-idle state, not a lit one: T_LEDS_TO_OFF sets it
	// immediately after blanking the strip. Motion has to be left armed here or
	// it simply stops responding once the stream ends.
	//
	// The difference between the two paths is only the TV preset. A real stream
	// ending implies the TV went quiet, so clearing TV::State.Status is right; a test
	// teardown observed nothing real, so it must not fake that - it only re-arms
	// motion, and only when no other source already owns the strip.
	if (handover) {
		TV::State.Status     = false;
		MOTION::State.Status = motON;
	} else if (!TV::State.Status && !APP::Am.Status) {
		MOTION::State.Status = motON;                                           // Re-arm, nothing else owns the strip - State
	}

	// * UDPRAW status
	UDPRAW::State.Status = false; // disable  UDPRAW
	UDPRAW::State.CachedBrightness = 0; // Reset brightness cache
	UDPRAW::State.Fps = 0.0f; // Stream stopped - no rate to report
	
	// * seg + turn off led's
	LED::setAll(0, 0, 0, 0);

	// Update app status
	APP::updStatus("UDPRAW::End"); // *

	// Update App Color
	APP::updDeltaColors(); // *

	DIF::AutoOff();                                                       // Diffuser off if all sources idle - Action

	// Test teardown: kill the ramp and force the strip dark, LAST.
	//
	// Order matters more than the individual steps here. Killing tasks first
	// does not hold, because everything above - APP::updStatus(),
	// APP::updDeltaColors(), DIF::AutoOff() - can arm a fresh transition on
	// its way out, and that new task then ramps brightness back up out of an
	// "off" strip. T_LEDS_TO_OFF has the same shape for the same reason: it
	// blanks, then kills, never the other way round.
	//
	// This is what ends the strip dark - not the motion state above, which
	// stays armed so the sensors keep working.
	if (!handover) {
		for (int i = 0; i < LED_NUM_TOTAL; i++) {
			LED::State.TargetColor[i] = CRGB(0, 0, 0);                          // No lit destination left - State
		}
		LED::setAll(0, 0, 0, 0);                                          // Blank the buffer - Action
		TSK::KillTasksAvoidLocked("UDPRAW_End");                         // Cancel every ramp - Action
		LED::ForceShow();                                                // Push the blank immediately - Action
	}
}

/**
 * @brief  Activate UDPRAW streaming mode on the first valid packet.
 *
 * Records UDPRAW::State.InitTime from DATE, sets UDPRAW::State.Status = true, disables
 * MOTION and AM, clears all LEDs, updates app status and colours, kills all
 * tasks, and launches T_UDPRAW_SET_COLOR with UDPRAW_LED_MOVIE_DELAY ms delay.
 *
 * @note   Called internally by Loop(). Do not call directly.
 */
void Init() {
	// * LOG
	#ifdef ENABLE_LOG_UDPRAW
		PRNT::_print(PRNT::formatMSG("%32s : starting with lux [%d]" NL, "UDPRAW_Init", LISENS::State.Lux));
	#endif
	PRNT::_print(PRNT::formatMSG("%~32s # stream start, inc [%d], delay [%d]" NL, "UDPRAW_Init", EE::Get(EE_UDPRAW_BR_CL_INC), EE::Get(EE_UDPRAW_BR_CL_DEL))); // Log start, raw speed (not adapted) - Sync
	
	// Init time
	UDPRAW::State.InitTime[_HH] = NET::Date.time[_HH];
	UDPRAW::State.InitTime[_MI] = NET::Date.time[_MI];
	UDPRAW::State.InitTime[_SS] = NET::Date.time[_SS];
	UDPRAW::State.InitTime[_DD] = NET::Date.time[_DD];
	UDPRAW::State.InitTime[_MM] = NET::Date.time[_MM];
	UDPRAW::State.InitTime[_YY] = NET::Date.time[_YY];
	
	// UDPRAW enable
	UDPRAW::State.Status = true;
	UDPRAW::State.CachedBrightness = LED::getLuxBrightness(EE::Get(EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX)); // Cache brightness for performance
	g_nonTvDirty = true; // Blank below must actually reach the strip on the very next packet

	// Fresh FPS window - don't let a stale count from before the gap skew the first reading
	g_fpsFrameCount  = 0;
	g_fpsWindowStart = TimeNow;
	UDPRAW::State.Fps = 0.0f;

	#ifdef ENABLE_LOG_UDPRAW
		PRNT::_print(PRNT::formatMSG("%32s : init time [%02d:%02d:%02d %02d.%02d.%02d]" NL, "UDPRAW_Init", 
			UDPRAW::State.InitTime[_HH], UDPRAW::State.InitTime[_MI], UDPRAW::State.InitTime[_SS],
			UDPRAW::State.InitTime[_DD], UDPRAW::State.InitTime[_MM], UDPRAW::State.InitTime[_YY]));
	#endif

	// disable motion
	MOTION::State.Status = motOFF;

	// AM Status
	APP::Am.Status = false;

	// * turn off led's
	LED::setAll(0, 0, 0, 0);
	
	// Update app status
	APP::updStatus("UDPRAW::Init"); // *

	// Update App Color
	APP::updDeltaColors(); // *

	DIF_Colorx udprawColor = { LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b,
	                           LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b }; // Match last stream color - Logic
	DIF::AutoOn(EE_DIF_MODE_UDPRAW, &udprawColor);                        // Diffuser on, matched to stream color - Action

	// * Task
	TSK::KillTasksAvoidLocked("UDPRAW_Init");
    TSK::AddTask("UDPRAW_Init", "T_UDPRAW_SET_COLOR", T_UDPRAW_SET_COLOR, TASK_MS, EE::Get(EE_UDPRAW_BR_CL_DEL), UDPRAW_LED_MOVIE_DELAY, false); // Raw EE - not adapted - Timing
}

/**
 * @brief  (Re-)open the UDPRAW UDP socket on port 5568.
 *
 * Call this after any WiFi reconnect to re-bind the port.
 * Also called by Setup() on first init.
 */
void UdpSet() {
	// UDP Port
	UDPRAW_UDP.begin(5568);

	PRNT::_print(PRNT::formatMSG("%~32s # UDP socket opened on port [5568]" NL, "UDPRAW_UdpSet"));
}
} // namespace UDPRAW


namespace BME {
/* =========================================================================== */
/* BME280                                                                     */
/* Temperature / humidity sensor (I^2C 0x76)                                   */
/* =========================================================================== */


/**
 * @brief  Initialise the BME280 temperature/humidity sensor and start the polling task.
 *
 * Calls Wire.begin(), attempts bme280sensor.begin(), and registers Check
 * as a repeating task every CheckTIME ms.
 * Call once in setup().
 *
 * @note   Logs a warning if the sensor is not found on I^2C address 0x76.
 */
void Setup() {
	#ifdef ENABLE_LOG_BME280
		PRNT::_print(PRNT::formatMSG("%32s : initializing" NL, "BME280_Setup"));
	#endif
    // Ensure I2C is running and try to detect the sensor at common addresses
    Wire.begin();
    uint8_t detectedAddr = 0;
    // quick probe for 0x76 and 0x77
    for (uint8_t a = 0x76; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            detectedAddr = a;
            break;
        }
    }

    if (detectedAddr) {
        // Build a settings object with the detected address and attempt init
        BME280I2C::Settings s(
            BME280::OSR_X16, BME280::OSR_X1, BME280::OSR_X1,
            BME280::Mode_Normal, BME280::StandbyTime_500us,
            BME280::Filter_16, BME280::SpiEnable_False,
            (detectedAddr == 0x76) ? BME280I2C::I2CAddr_0x76 : BME280I2C::I2CAddr_0x77
        );

        bme280sensor.setSettings(s);
        if (bme280sensor.begin()) {
            float temp = 0.0f, hum = 0.0f, pres = 0.0f;
            BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
            BME280::PresUnit presUnit(BME280::PresUnit_Pa);
            bme280sensor.read(pres, temp, hum, tempUnit, presUnit);

            BME::State.Temperature = temp;
            BME::State.Humidity = hum;

            PRNT::_print(PRNT::formatMSG("%32s : BME280 sensor found on I2C address [0x%02X] temperature [%1F C] humidity [%1F %%]" NL,
                "BME280_Setup", detectedAddr, temp, hum));
        } else {
            PRNT::_print(PRNT::formatMSG("%~32s # BME280 ACKed at I2C 0x%02X but library init() failed" NL, "BME280_Setup", detectedAddr));
        }
    } else {
        // No ACK at 0x76/0x77 -- try library default begin() to be safe, then print full I2C scan
        // use library default settings (already set globally) and attempt init
        if (bme280sensor.begin()) {
            float temp = 0.0f, hum = 0.0f, pres = 0.0f;
            BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
            BME280::PresUnit presUnit(BME280::PresUnit_Pa);
            bme280sensor.read(pres, temp, hum, tempUnit, presUnit);

            BME::State.Temperature = temp;
            BME::State.Humidity = hum;

            PRNT::_print(PRNT::formatMSG("%32s : BME280 sensor found (library default) temperature [%1F C] humidity [%1F %%]" NL,
                "BME280_Setup", temp, hum));
        } else {
            PRNT::_print(PRNT::formatMSG("%~32s # BME280 sensor NOT found on I2C (no ACK on 0x76/0x77)" NL, "BME280_Setup"));
            // Print a brief I2C scan to aid debugging
            PRNT::_print(PRNT::formatMSG("%32s : I2C scan: " , "BME280_Setup"));
            for (uint8_t addr = 1; addr < 0x78; addr++) {
                Wire.beginTransmission(addr);
                if (Wire.endTransmission() == 0) {
                    PRNT::_print(PRNT::formatMSG("0x%02X ", addr));
                }
            }
            PRNT::_print(NL);
        }
    }

	// TASK
	#ifdef ENABLE_LOG_BME280
		PRNT::_print(PRNT::formatMSG("%32s : periodic task registered every [%d] s" NL, "BME280_Setup", CheckTIME));
	#endif

    TSK::AddTask("BME280_Setup", "Check", Check, TASK_S, CheckTIME, 1, true);
}

/**
 * @brief  Periodic sensor read task -- updates BME::State.Temperature and BME::State.Humidity.
 *
 * Called automatically by the task scheduler every CheckTIME ms.
 * If either Temperature or Humidity has changed since the last read,
 * calls APP::updStatus() to push the new values to the phone app.
 *
 * @param  taskId  Task handle supplied by the scheduler (not used internally).
 *
 * @note   Do not call directly -- registered via AddTask in Setup().
 */
void Check(taskId_t taskId) {
	float temp(NAN), hum(NAN), pres(NAN);
	BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
	BME280::PresUnit presUnit(BME280::PresUnit_Pa);
	bme280sensor.read(pres, temp, hum, tempUnit, presUnit);
	
    #ifdef ENABLE_LOG_BME280
        PRNT::_print(PRNT::formatMSG("%32s : raw read temperature [%1F C] humidity [%1F %%]" NL, "Check", temp, hum));
    #endif
	
	bool n = false;
	// Temp Changed, send to app
	if (BME::State.Temperature != temp) {
        #ifdef ENABLE_LOG_BME280
            PRNT::_print(PRNT::formatMSG("%32s : temperature changed [%1F C] -> [%1F C]" NL, "Check", BME::State.Temperature, temp));
        #endif

		BME::State.Temperature = temp;
		n = true;
	}
	// Hum Changed, send to app
	if (BME::State.Humidity != hum) {
        #ifdef ENABLE_LOG_BME280
            PRNT::_print(PRNT::formatMSG("%32s : humidity changed [%1F %%] -> [%1F %%]" NL, "Check", BME::State.Humidity, hum));
        #endif

		BME::State.Humidity = hum;
		n = true;
	}
	
	if (n) {
        #ifdef ENABLE_LOG_BME280
            PRNT::_print(PRNT::formatMSG("%32s : update sent to app - temperature [%1F C] humidity [%1F %%]" NL, "Check", BME::State.Temperature, BME::State.Humidity));
        #endif
		APP::updStatus("NET::Check");
	}
}
} // namespace BME


namespace NET {

/* ==================================================================== */
/*  Setup -- single entry point, call once in setup()               */
/* ==================================================================== */

/**
 * @brief  Initialise the network stack (WiFi + NTP).
 *
 * Calls Connect(), blocks up to NET_CONNECT_TIMEOUT ms for
 * WL_CONNECTED. On success: saves connect timestamp, triggers the first
 * RTC_Begin(). On failure: schedules a reconnect via
 * Reconnect after NET_RETRY_DELAY ms.
 * Registers Check as a repeating background task regardless of
 * outcome, so the system self-heals after setup.
 */
void Setup() {
    PRNT::_print(PRNT::formatMSG("%~32s # Init NET module" NL, "NET_Setup"));
    char ssidBuf[64];
    strcpy_P(ssidBuf, WIFI_SSID);
    PRNT::_print(PRNT::formatMSG("%~32s # Connecting to [%s]" NL, "NET_Setup", ssidBuf));

    Connect();                                     // Start handshake - Action

    /* Blocking wait -- NET_CONNECT_TIMEOUT / 0.5 s ticks (convert s to 500ms units) */
    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT * 2);    // tick count - Setup
    uint8_t elapsed = 0;                                    // attempts - State

    while (!IsConnected() && ticks > 0) {
        WaitWithYield(500);                             // Hardware sync with background yielding
        ticks--;
        elapsed++;
    }

    if (IsConnected()) {
        NET_WifiSt = netWifiOK;                             // Mark connected - State
        const IPAddress ip = getIP();                   // Poll until DHCP ready - Action
        PRNT::_print(PRNT::formatMSG("%~32s # Connected [%d.%d.%d.%d] after %d polls" NL,
            "NET_Setup", ip[0], ip[1], ip[2], ip[3], elapsed));

        RTC_Begin();                                    // Sync time now - Action
        setConnectTime(false);                          // Stamp after RTC attempt - State
    } else {
        NET_WifiSt = netWifiRetrying;                       // Mark retry mode - State
        PRNT::_print(PRNT::formatMSG("%~32s # Timeout, retry in [%l] ms" NL,
            "NET_Setup", NET_RETRY_DELAY));

        /* One-shot reconnect -- unlocked, self-kills on completion */
        TSK::AddTask("NET_Setup", "Reconnect",
            Reconnect, TASK_S, NET_RETRY_DELAY, NET_RETRY_DELAY, false); // Unlocked one-shot - Logic
    }

    /* Health check -- LOCKED, survives any KillTasksAvoidLocked() call */
    TSK::AddTask("NET_Setup", "NET_Check",
        Check, TASK_S, NET_CHECK_TIME, 0, true);   // Locked - Logic
}

/**
 * @brief  Periodic background health check -- runs every NET_CHECK_TIME ms, LOCKED.
 *
 * Registered as a locked task in Setup() -- survives KillTasksAvoidLocked().
 * If UDPRAW stream is active, returns immediately without killing or modifying any task.
 * On disconnect: issues an immediate Connect(), clears connect stamp,
 *   schedules one unlocked Reconnect one-shot task (self-kills on completion).
 * On regain (connectTime[_YY] == 0 sentinel): restores UDPRAW + APP ports,
 *   re-arms NTP if needed, saves connect stamp.
 *
 * @param  taskId  Task handle (unused -- task is permanent/locked).
 */
void Check(taskId_t taskId) {
    if (UDPRAW::State.Status) return;                              // Stream active -- skip, never kill - Logic

    if (!IsConnected()) {
        if (NET_WifiSt != netWifiRetrying) {                // Avoid double-trigger - Logic
            NET_WifiSt = netWifiRetrying;                   // Update state - State
            PRNT::_print(PRNT::formatMSG("%~32s # Connection lost, reconnecting" NL, "NET_Check"));
            Connect();                             // Immediate attempt - Action
            setConnectTime(true);                       // Clear stamp - State

            /* One-shot reconnect -- unlocked, self-kills on completion */
            TSK::AddTask("NET_Check", "Reconnect",
                Reconnect, TASK_S, NET_RETRY_DELAY, NET_RETRY_DELAY, false); // Unlocked one-shot - Logic
        }
    } else {
        /* Already connected -- check if this is a fresh regain (sentinel == 0) */
        if (NET::Wifi.ConnectTime[_YY] == 0) {
            NET_WifiSt = netWifiOK;                         // Cleared - State
            PRNT::_print(PRNT::formatMSG("%~32s # Connection restored, refreshing stack" NL, "NET_Check"));
            UDPRAW::UdpSet();                                // Restore UDPRAW port - Action
            APP::UdpSet();                                   // Restore APP port - Action
            DIF::UdpSet();                                   // Restore DIF port - Action

            if (RTC_Status == rtcRETRY) {
                PRNT::_print(PRNT::formatMSG("%~32s # Re-arming NTP sync" NL, "NET_Check"));
                RTC_Begin();                            // Re-sync NTP - Action
            }

            setConnectTime(false);                      // Save stamp - State
        }
    }
}


/* ==================================================================== */
/*  WiFi layer                                                          */
/* ==================================================================== */

/**
 * @brief  Disconnect then re-issue WiFi.begin(). Non-blocking.
 */
void Connect() {
    #ifdef ENABLE_LOG_NET
        PRNT::_print(PRNT::formatMSG("%32s : resetting adapter" NL, "NET_Connect"));
    #endif
    WiFi.disconnect();                                      // Reset radio state - Action
    char ssidBuf[64], passBuf[64];
    strcpy_P(ssidBuf, WIFI_SSID);
    strcpy_P(passBuf, WIFI_PASS);
    WiFi.begin(ssidBuf, passBuf);                           // Start handshake - Hardware
}

/**
 * @brief  Shared WiFi status cache -- IsConnected() used to be polled
 *         independently (each on its own 500ms timer) inside APP::Loop(),
 *         DIF::Loop() AND UDPRAW::Loop() -- up to 3 separate calls per cycle
 *         instead of 1. Now updated once per loop() and simply read by all three.
 */
bool Connected_Cached() {
    if (TimeNow - NET_LastCheck >= NET_CHECK_INTERVAL) {                // Periodic check logic - Logic
        NET_Connected = IsConnected();                              // Update cache - Sync
        NET_LastCheck = TimeNow;                                        // Reset timer - State
    }
    return NET_Connected;
}

/**
 * @brief  One-shot retry task -- blocks NET_CONNECT_TIMEOUT ms then
 *         restores the full network stack or reschedules itself.
 *
 * @param  taskId  Task handle -- self-killed before blocking wait.
 */

/**
 * @brief  Poll WiFi.localIP() until a valid (non-zero) address is assigned.
 *
 * After WL_CONNECTED the DHCP lease may take another second or two.
 * Polls every 500 ms for up to NET_IP_TIMEOUT ms before giving up.
 *
 * @return Assigned IPAddress, or IPAddress(0,0,0,0) on timeout.
 */
IPAddress getIP() {
    uint8_t ticks = (uint8_t)(NET_IP_TIMEOUT / 500);           // poll budget - Setup
    IPAddress ip  = WiFi.localIP();                             // First read - Action

    while (ip == IPAddress(0, 0, 0, 0) && ticks > 0) {
        WaitWithYield(500);                                 // DHCP settling with background yielding
        ticks--;
        ip = WiFi.localIP();                                    // Re-read - Action
        #ifdef ENABLE_LOG_NET
            PRNT::_print(PRNT::formatMSG("%32s : waiting for IP [%d ticks left]" NL,
                "getIP", ticks));
        #endif
    }

    if (ip == IPAddress(0, 0, 0, 0)) {
        PRNT::_print(PRNT::formatMSG("%32s ! IP timeout, DHCP may have failed" NL, "getIP"));
    }
    return ip;                                                  // Return result - State
}

/* ==================================================================== */
/*  Helper functions                                                    */
/* ==================================================================== */

/**
 * @brief  WiFi connectivity test -- use in all modules.
 */
bool IsConnected() { return WiFi.status() == WL_CONNECTED; }

void Reconnect(taskId_t taskId) {
    TSK::KillID(taskId, "Reconnect");              // Remove self - State

    char ssidBuf[64];
    strcpy_P(ssidBuf, WIFI_SSID);
    PRNT::_print(PRNT::formatMSG("%~32s # Connecting to [%s]" NL, "Reconnect", ssidBuf));

    Connect();                                     // Issue handshake - Action

    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT / 500);  // 10 s ceiling - Setup
    // Don't starve an active ambilight stream for the full ceiling - WaitWithYield()
    // never services UDPRAW::Loop(), so every tick here is dead time for the LEDs.
    // Bail fast instead; this task's own reschedule (NET_RETRY_DELAY) tries again soon.
    if (UDPRAW::State.Status) ticks = 1;
    while (!IsConnected() && ticks > 0) {
        WaitWithYield(500);                             // Hardware sync with background yielding
        ticks--;
    }

    if (IsConnected()) {
        NET_WifiSt = netWifiOK;                             // Fully recovered - State
        const IPAddress ip = getIP();                   // Poll until DHCP ready - Action
        PRNT::_print(PRNT::formatMSG("%~32s # Connected [%d.%d.%d.%d]" NL,
            "Reconnect", ip[0], ip[1], ip[2], ip[3]));

        RTC_Begin();                                    // Re-sync NTP - Action
        setConnectTime(false);                          // Stamp connect time - State
        UDPRAW::UdpSet();                                    // Restore UDP ports - Action
        APP::UdpSet();                                       // Restore APP ports - Action
        DIF::UdpSet();                                       // Restore DIF port - Action
    } else {
        PRNT::_print(PRNT::formatMSG("%~32s # Timeout, retry in [%l] ms" NL,
            "Reconnect", NET_RETRY_DELAY));

        /* Re-arm self -- still unlocked one-shot */
        TSK::AddTask("Reconnect", "Reconnect",
            Reconnect, TASK_S, NET_RETRY_DELAY, NET_RETRY_DELAY, false); // Unlocked one-shot - Logic
    }
}


/* ==================================================================== */
/*  NTP / RTC layer                                                     */
/* ==================================================================== */

/**
 * @brief  Perform a blocking NTP sync (10 s ceiling) and arm tick/resync tasks.
 *
 * Guards against re-entrant calls with a static flag. Requires WiFi.
 * On success: sets timezone, calls RTC_Parse(0), registers
 *   RTC_Parse  every 1 s and
 *   RTC_Resync every NET_RTC_RESYNC ms (6 h).
 * On failure: schedules RTC_RetryTask after NET_RTC_RETRY ms.
 */
void RTC_Begin() {
    static bool syncing = false;                            // Re-entrancy guard - Setup
    if (syncing) return;                                    // Already in progress - Logic

    /* Already have a valid time -- NTPClient throttles at 60 s anyway,
     * a second call immediately after success will always fail. Skip it. */
    if (NET::Date.time[_YY] > 0 && RTC_Status == rtcOK) {
        PRNT::_print(PRNT::formatMSG("%~32s # Time already set [%02d:%02d:%02d], skipping" NL,
            "NET_RTC_Begin",
            NET::Date.time[_HH], NET::Date.time[_MI], NET::Date.time[_SS]));
        return;
    }

    if (!IsConnected()) {                    // Pre-check - Logic
        PRNT::_print(PRNT::formatMSG("%32s ! No WiFi, scheduling retry in [%d] s" NL,
            "NET_RTC_Begin", NET_RTC_RETRY));
        RTC_Status = rtcRETRY;                              // Mark retry - State
        TSK::AddTask("NET_RTC_Begin", "RTC_RetryTask",
            RTC_RetryTask, TASK_S, NET_RTC_RETRY, NET_RTC_RETRY, false); // Logic
        return;
    }

    syncing = true;                                         // Lock - State
    PRNT::_print(PRNT::formatMSG("%~32s # Starting NTP sync (timeout %d s)" NL,
        "NET_RTC_Begin", NET_CONNECT_TIMEOUT));

    RTC_TimeClient.begin();                                 // Open UDP - Action

    /* Blocking NTP poll -- NET_CONNECT_TIMEOUT ceiling */
    bool ok      = false;
    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT * 2);    // 10 s * 2 = 20 ticks of 0.5 s - Setup
    // Same reasoning as Reconnect() - don't hold an active ambilight stream
    // hostage for the full ceiling; RTC_RetryTask reschedules this shortly anyway.
    if (UDPRAW::State.Status) ticks = 1;

    while (ticks-- > 0) {
        if (RTC_TimeClient.update() &&
            RTC_TimeClient.getEpochTime() > 100000UL) {
            ok = true;                                      // Got valid epoch - State
            break;
        }
        WaitWithYield(500);                             // Wait for NTP reply with background yielding
    }

    if (!ok) {
        PRNT::_print(PRNT::formatMSG("%32s ! NTP failed, retry in [%d] s" NL,
            "NET_RTC_Begin", NET_RTC_RETRY));

        /* Zero DATE on failure so sentinel logic stays consistent */
        NET::Date.time[_HH] = NET::Date.time[_MI] = NET::Date.time[_SS] = 0; // State
        NET::Date.time[_DD] = NET::Date.time[_MM] = NET::Date.time[_YY] = 0; // State

        RTC_Status = rtcRETRY;                              // Update status - State
        syncing    = false;                                 // Unlock - State
        TSK::AddTask("NET_RTC_Begin", "RTC_RetryTask",
            RTC_RetryTask, TASK_S, NET_RTC_RETRY, NET_RTC_RETRY, false); // Logic
        return;
    }

    RTC_Status = rtcOK;                                     // Success - State
    RTC_TimeClient.setTimeOffset(NET_TIMEZONE * 3600L);     // Apply UTC offset - Setup
    RTC_Parse(0);                                       // Immediate parse - Action

    PRNT::_print(PRNT::formatMSG("%~32s # Time [%02d:%02d:%02d] [%02d/%02d/%02d]" NL,
        "NET_RTC_Begin",
        NET::Date.time[_HH], NET::Date.time[_MI], NET::Date.time[_SS],
        NET::Date.time[_DD], NET::Date.time[_MM], NET::Date.time[_YY]));

    if (NET::Date.time[_YY] > 0) {
        TSK::KillTasksAvoidLocked("NET_RTC_Begin");         // Remove stale tasks - State
        TSK::AddTask("NET_RTC_Begin", "RTC_Parse",
            RTC_Parse, TASK_S, 1UL, 0, true);           // 1 s ticker - Logic
        TSK::AddTask("NET_RTC_Begin", "RTC_Resync",
            RTC_Resync, TASK_S, NET_RTC_RESYNC, NET_RTC_RESYNC, true); // 6 h resync - Logic

        setConnectTime(false);                          // Set WiFi connect stamp after RTC ready - State
    }

    syncing = false;                                        // Unlock - State
}

/**
 * @brief  True UTC epoch, local offset removed. Kept for the fault NTP check.
 * @return Seconds since the Unix epoch in UTC, or 0 when never synced.
 */
uint32_t RTC_EpochUTC() {
	const uint32_t local = RTC_TimeClient.getEpochTime();               // Local seconds - Action
	if (local < (uint32_t)RTC_EPOCH_SANE) return 0;                     // Never synced - Logic
	return local - (uint32_t)(NET_TIMEZONE * 3600L);                    // Strip the offset - Logic
}

/**
 * @brief  1-second repeating task -- unpack NTP epoch into NET::Date.time[].
 *
 * @param  taskId  Task handle (unused). Pass 0 for manual one-shot call.
 */
void RTC_Parse(taskId_t taskId) {
    const time_t raw = RTC_TimeClient.getEpochTime();       // NTP epoch - Action

    NET::Date.time[_HH] = hour(raw);                             // Mapping
    NET::Date.time[_MI] = minute(raw);                           // Mapping
    NET::Date.time[_SS] = second(raw);                           // Mapping
    NET::Date.time[_DD] = day(raw);                              // Mapping
    NET::Date.time[_MM] = month(raw);                            // Mapping
    NET::Date.time[_YY] = (uint8_t)(year(raw) - 2000);          // Mapping
}

/**
 * @brief  6-hour repeating task -- force NTP re-sync to prevent drift,
 *         then refresh the WiFi connect timestamp.
 *
 * @param  taskId  Task handle (unused).
 */
void RTC_Resync(taskId_t taskId) {
    if (!IsConnected()) return;              // No WiFi, skip - Logic

    PRNT::_print(PRNT::formatMSG("%~32s # 6-hour NTP re-sync" NL, "RTC_Resync"));
    RTC_TimeClient.forceUpdate();                           // Correct drift - Action
    setConnectTime(false);                              // Refresh connect stamp - State
}

/**
 * @brief  One-shot retry task that re-attempts NTP sync.
 *
 * @param  taskId  Task handle -- self-killed before calling RTC_Begin().
 */
void RTC_RetryTask(taskId_t taskId) {
    TSK::KillID(taskId, "RTC_RetryTask");               // Self-cleanup - State
    RTC_Status = rtcOK;                                     // Allow re-entry - State
    RTC_Begin();                                        // Retry sync - Action
}

/**
 * @brief  Save or clear the WiFi connect timestamp in NET::Wifi.ConnectTime[].
 *
 * @param  reset  true -> zero all fields.
 *                false + RTC synced (NET::Date.time[_YY] > 0) -> copy NET::Date.time[].
 *                false + not synced -> also zero (sentinel stays 0).
 *
 * @note  connectTime[_YY] == 0 is the sentinel used by Check()
 *        to detect a fresh-reconnect event requiring port re-init.
 */
void setConnectTime(bool reset) {
    const bool synced = (NET::Date.time[_YY] > 0);              // RTC validity - Logic
    const bool set    = (!reset && synced);                 // Write condition - Logic

    NET::Wifi.ConnectTime[_HH] = set ? NET::Date.time[_HH] : 0;      // Hour - Mapping
    NET::Wifi.ConnectTime[_MI] = set ? NET::Date.time[_MI] : 0;       // Minute - Mapping
    NET::Wifi.ConnectTime[_SS] = set ? NET::Date.time[_SS] : 0;       // Second - Mapping
    NET::Wifi.ConnectTime[_DD] = set ? NET::Date.time[_DD] : 0;       // Day - Mapping
    NET::Wifi.ConnectTime[_MM] = set ? NET::Date.time[_MM] : 0;       // Month - Mapping
    NET::Wifi.ConnectTime[_YY] = set ? NET::Date.time[_YY] : 0;       // Year - Mapping

    #ifdef ENABLE_LOG_NET
        if (reset) {
            PRNT::_print(PRNT::formatMSG("%32s : timestamp cleared" NL, "NET_SetConnectTime"));
        } else if (synced) {
            PRNT::_print(PRNT::formatMSG("%32s : timestamp saved [%02u:%02u:%02u %02u/%02u/%02u]" NL,
                "NET_SetConnectTime",
                NET::Wifi.ConnectTime[_HH], NET::Wifi.ConnectTime[_MI], NET::Wifi.ConnectTime[_SS],
                NET::Wifi.ConnectTime[_DD], NET::Wifi.ConnectTime[_MM], NET::Wifi.ConnectTime[_YY]));
        } else {
            PRNT::_print(PRNT::formatMSG("%32s : RTC not synced, zeroed" NL, "NET_SetConnectTime"));
        }
    #endif
}

/* ------------------------------------------------------------------------ */
/* NET                                                                        */
/* WiFi + NTP lifecycle - reconnect - RTC parse/resync                        */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Pause for a short interval while allowing background tasks to run.
 *
 * Uses yield() inside a tight loop to avoid blocking the scheduler during
 * short waits. Intended for network retry and DHCP polling delays.
 *
 * @param  ms  Milliseconds to wait.
 */
static void WaitWithYield(uint32_t ms) {
    unsigned long start = millis();
    while ((millis() - start) < ms) {
        yield();
    }
}
} // namespace NET


namespace MQTT {

/**
 * @brief  One-time client config + first connect. Call once from setup().
 */
void Setup() {
    PRNT::_print(PRNT::formatMSG("%~32s # setup" NL, "MQTT_Setup"));
    MQTT_Cli.setServer(MQTT_HOST, MQTT_PORT);                   // Broker - Setup
    MQTT_Cli.setCallback(Callback);                        // RX hook - Setup
    MQTT_Cli.setBufferSize(MQTT_BUF_SIZE);                      // 512 to fit packets - Setup
    MQTT_Cli.setKeepAlive(MQTT_KEEPALIVE);                      // Keepalive - Setup

    // Both default to effectively-unbounded blocking on this board: the raw
    // TCP+TLS handshake goes through the ESP32-S3 co-processor (WiFiSSLClient::
    // connect() -- _connectionTimeout defaults to 0 = "let the modem decide"),
    // and PubSubClient's own CONNACK wait defaults to MQTT_SOCKET_TIMEOUT=15s.
    // Reconnect() calls both synchronously from loop(), so while the broker is
    // unreachable/slow (e.g. right after a fresh flash, before WiFi/cloud have
    // settled) every retry can freeze loop() -- and therefore LED refresh, the
    // lux sampling task, and TV on/off detection -- for a long, unbounded
    // stretch, repeating every MQTT_RETRY_MS. Capping both bounds the worst
    // case to a few seconds per attempt instead.
    MQTT_Net.setConnectionTimeout(4000);                    // ms - TCP+TLS handshake cap - Setup
    MQTT_Cli.setSocketTimeout(4);                           // s  - CONNACK wait cap - Setup

    if (NET::IsConnected()) Reconnect();                    // First attempt - Action
}

/**
 * @brief  Service the broker each loop(): reconnect if down, else pump + dispatch.
 *
 * Self-heals on WiFi regain (gated on the shared NET cache), so no hook is
 * needed in NET::Check() -- the throttle just resumes reconnecting.
 */
void Loop() {
    if (!NET::Connected_Cached()) { MQTT::State.Up = false; return; }   // Needs WiFi - Logic
    if (!MQTT_Cli.connected()) { MQTT::State.Up = false; Reconnect(); return; } // Down - Logic
    
    // Throttle TLS pumping to every 30ms - MQTT keepalive is 10s, so this has huge slack
    static uint32_t lastMqttPump = 0;
    if (TimeNow - lastMqttPump >= 30) {
        MQTT_Cli.loop();                                            // Pump (fires callback) - Action
        Dispatch();                                            // Run staged cmd - Action
        lastMqttPump = TimeNow;
    }
}
/* ------------------------------------------------------------------------ */
/* MQTT -- HiveMQ Cloud link (TLS 8883). Mirrors the UDP app protocol over a   */
/* SINGLE duplex topic (MQTT_TOPIC) -- both ends publish AND subscribe there:  */
/*   RX on MQTT_TOPIC -> APP::Exec()  (same dispatcher as UDP, tag stripped)   */
/*   TX via APP::termMsgSend()  -> Publish() (tagged, mirrored alongside UDP)  */
/* No LWT / retained presence flag: online-offline is inferred from traffic   */
/* on MQTT_TOPIC -- the welcome ('Z') resync on connect + the existing 'k'      */
/* keep-alive already double as presence, same as the UDP side.               */
/* Connect/reconnect/failure are always logged (see NET::Reconnect for the    */
/* same precedent); per-command RX tracing is opt-in via ENABLE_LOG_MQTT,     */
/* matching ENABLE_LOG_APP's "recv" lines on the UDP side. Per-packet TX      */
/* ("sent [...]") is deeper still -- ENABLE_LOG_MQTT_VERBOSE -- same split as */
/* APP::termMsgSend()/termMsgSendBin() on the UDP side.                      */
/* ------------------------------------------------------------------------ */

/**
 * @brief  Broker RX callback -- drop our own echo, stage the rest, never dispatch here.
 *
 * PubSubClient runs this inside MQTT_Cli.loop(); publishing from within it is
 * unsafe, so we only copy the bytes and let Loop() dispatch afterwards. Both
 * ends share one topic, so every message WE publish also arrives back here --
 * byte 0 is a sender tag that lets us drop our own traffic instead of
 * re-executing it as a bogus command.
 *
 * @param  topic    Topic the message arrived on (single duplex topic -- unused).
 * @param  payload  Raw bytes (NOT null-terminated); byte 0 is the sender tag.
 * @param  length   Byte count, tag included.
 */
void Callback(char* topic, byte* payload, unsigned int length) {
    if (length < 1 || payload[0] == MQTT_TAG_DEV) return;               // Empty or our own echo - Logic
    if (MQTT::State.RxPending) return;                                  // Prev cmd not yet run - Logic
    unsigned int n = length - 1;                                        // Drop the sender tag - Mapping
    if (n >= APP_UDP_MAX_BUFFER_SIZE) n = APP_UDP_MAX_BUFFER_SIZE - 1;   // Clamp to buffer - Logic
    memcpy(APP::State.RecvBuff, payload + 1, n);                        // Stage into shared app buffer - Action
    MQTT::State.RxLen     = (int)n;                                     // Stash length - State
    MQTT::State.RxPending = true;                                       // Arm dispatch - State
}

/**
 * @brief  Run a staged command through the shared dispatcher (post-loop).
 *
 * Strips a trailing CR/LF and null-terminates exactly like APP::Loop(), then
 * feeds APP::Exec() -- so the ACK envelope, de-dup and every handler are reused.
 */
static void Dispatch() {
    if (!MQTT::State.RxPending) return;                                 // Nothing waiting - Logic
    int len = MQTT::State.RxLen;
    if (len > 0 && (APP::State.RecvBuff[len-1] == '\n' || APP::State.RecvBuff[len-1] == '\r'))
        len--;                                                  // Strip newline - Logic
    APP::State.RecvBuff[len] = '\0';                                    // Null-terminate - Mapping
    MQTT::State.RxPending    = false;                                   // Clear before exec - State

    #ifdef ENABLE_LOG_MQTT
        PRNT::_print(PRNT::formatMSG("%32s : recv [%s] (%d)" NL, "MQTT_Dispatch", APP::State.RecvBuff, len));
    #endif

    APP::Ard.LastReceive  = TimeNow;                                  // Activity timer - State
    APP::State.RxProtocol = APP_TRANSPORT_MQTT;                       // Tag source for Exec()'s protocol gate - State
    APP::Exec(APP::State.RecvBuff, len);                                 // SAME path + buffer as UDP - Action
}

/**
 * @brief  Tag + publish one outbound line on the single duplex topic (called from Send).
 * @param  msg  Null-terminated line already built by the caller.
 * @note   Byte 0 of the wire payload is MQTT_TAG_DEV, so our own subscription
 *         (same MQTT_TOPIC) drops the echo in Callback() instead of feeding it
 *         back through APP::Exec() as a bogus command. Sent via begin/write/end
 *         so no extra full-size buffer is needed just to prepend one byte.
 * @note   msg is almost always the return value of PRNT::formatMSG() (same
 *         caller chain as termMsgSend()), which lives in formatMSG's single
 *         shared static buffer. Debug-logging it here via ANOTHER formatMSG()
 *         call would read msg back out of that same buffer while this call is
 *         overwriting it -- self-aliasing corruption (garbled/nested log
 *         lines). Snapshot msg into a local copy first so the two calls never
 *         touch the same memory.
 */
void Publish(const char* msg) {
    // _sharedTxBuffer lives in namespace APP (see APP::_sharedTxBuffer in the
    // .ino) - needs the explicit qualifier here since MQTT is a sibling
    // namespace, not an enclosing one; plain _sharedTxBuffer doesn't resolve.
    #ifdef ENABLE_LOG_MQTT
        strncpy(APP::_sharedTxBuffer, msg, sizeof(APP::_sharedTxBuffer) - 1);
        APP::_sharedTxBuffer[sizeof(APP::_sharedTxBuffer) - 1] = '\0';
    #endif

    if (!MQTT::State.Up) {
        #ifdef ENABLE_LOG_MQTT
            PRNT::_print(PRNT::formatMSG("%32s : link down, dropped [%s]" NL, "MQTT_Publish", APP::_sharedTxBuffer));
        #endif
        return;
    }
    size_t n = strlen(msg);                                             // Payload length, tag excluded - Setup
    MQTT_Cli.beginPublish(MQTT_TOPIC, n + 1, false);                    // Tag + payload, single topic - Action
    MQTT_Cli.write((uint8_t)MQTT_TAG_DEV);                              // Sender tag first - Action
    MQTT_Cli.write((const uint8_t*)msg, n);                             // Payload - Action
    MQTT_Cli.endPublish();                                              // Fire-and-forget - Action
    #ifdef ENABLE_LOG_MQTT_VERBOSE
        PRNT::_print(PRNT::formatMSG("%32s : sent [%s] size [%d]" NL, "MQTT_Publish", APP::_sharedTxBuffer, strlen(APP::_sharedTxBuffer)));
    #endif
}

/**
 * @brief  Binary overload -- tag byte + raw payload on the single duplex topic.
 * @param  buf  Raw payload bytes (e.g. the 'LK' colour packet body incl. header).
 * @param  len  Payload length. The wire adds one leading MQTT_TAG_DEV byte so
 *              our own subscription drops the echo, exactly like the text path.
 */
void Publish(const uint8_t* buf, size_t len) {
    if (!MQTT::State.Up || buf == NULL || len == 0) return;
    MQTT_Cli.beginPublish(MQTT_TOPIC, len + 1, false);                 // Tag + payload - Action
    MQTT_Cli.write((uint8_t)MQTT_TAG_DEV);                             // Sender tag first - Action
    MQTT_Cli.write(buf, len);                                          // Raw payload - Action
    MQTT_Cli.endPublish();                                             // Fire-and-forget - Action
    #ifdef ENABLE_LOG_MQTT
        PRNT::_print(PRNT::formatMSG("%32s : sent [%d] bin bytes" NL, "MQTT_Publish", (int)len));
    #endif
}

/**
 * @brief  Throttled (re)connect. On success: subscribe the single topic.
 *
 * Client id carries APP::State.SessionId so a reboot presents a fresh id and HiveMQ
 * never kicks us for a duplicate. No LWT / retained presence flag -- online-offline
 * is inferred from traffic on MQTT_TOPIC instead (welcome + keep-alive). Does NOT
 * resync or claim the active transport by itself -- the app's own 'Z' welcome
 * (Exec() -> cmdConnected()) is what does that, once the app decides MQTT should
 * carry commands.
 */
void Reconnect() {
    // MQTT_Cli.connect() below is a blocking TLS handshake of unbounded duration -
    // defer entirely while an ambilight stream is active (MQTT isn't LED-critical),
    // or while the TV on/off transition task is mid-flight - a stall here would
    // otherwise freeze that ramp for 1-2+ seconds and can leave it visibly
    // incomplete once loop() resumes (same reasoning as LISENS::setLux()'s
    // guard). Skipped before touching LastTry, so the very next attempt once
    // things settle isn't held back by an extra throttle wait it never used.
    //
    // NOTE: this used to check LED::IsTransitioning() (any LED whose
    // CurrentColor differs from TargetColor), which is NOT a reliable
    // "transition in progress" signal in this codebase - HB's default fade-on
    // only steps CurrentBrightness, never CurrentColor, so it stayed true
    // indefinitely once the TV was on (confirmed live), which would have
    // blocked MQTT reconnects for the entire viewing session, not just the
    // brief transition. TV::State.Transitioning is explicit and correctly scoped.
    if (UDPRAW::State.Status || TV::State.Transitioning) return;
    // No credentials cached yet (never provisioned by the app) -- nothing to
    // try. Avoids burning the retry throttle on a connect() that's certain to
    // fail, and would otherwise need MQTT_USER/MQTT_PASS defines that no
    // longer exist (see MQTTCRED::cmdSetCredentials()).
    if (!MQTTCRED::State.valid) return;
    if (TimeNow - MQTT::State.LastTry < MQTT_RETRY_MS) return;         // Throttle - Logic
    MQTT::State.LastTry = TimeNow;

    char cid[32];
    snprintf(cid, sizeof(cid), "SmartTV-R4-%04X", APP::State.SessionId); // Unique id - Setup

    PRNT::_print(PRNT::formatMSG("%~32s # connecting to [%s:%d]" NL, "Reconnect", MQTT_HOST, MQTT_PORT));

    bool ok = MQTT_Cli.connect(cid, MQTTCRED::State.user, MQTTCRED::State.pass); // No LWT - Action
    if (ok) {
        MQTT::State.Up = true;                                          // Link live - State
        MQTT_Cli.subscribe(MQTT_TOPIC);                          // Single duplex topic - Action
        PRNT::_print(PRNT::formatMSG("%~32s # connected, subscribed [%s]" NL, "Reconnect", MQTT_TOPIC));
        // No auto resync here: a live broker session does NOT make MQTT the
        // active transport by itself -- only the app's own 'Z' welcome (routed
        // through Exec()/cmdConnected()) does that. The app sends it once it
        // decides MQTT should carry commands (see MqttTransport.connect()).
    } else {
        MQTT::State.Up = false;                                         // Still down - State
        PRNT::_print(PRNT::formatMSG("%32s ! connect failed, state [%d]" NL, "Reconnect", MQTT_Cli.state()));
    }
}
} // namespace MQTT


/* ------------------------------------------------------------------------ */
/* MQTT CREDENTIALS -- EEPROM-backed, app-provisioned                        */
/* User/pass are never hardcoded. The app pushes them over the local UDP    */
/* link (protocol letter '$', b64-encoded, see DataSend.sendMqttCredentials */
/* on the Android side); Exec() routes here before ACKing. A candidate pair */
/* is verified with a REAL blocking connect() against the broker before it */
/* is trusted or persisted -- so a typo/garbage pair can never silently     */
/* "stick", and the app finds out (APP_ACK_UNAUTHORIZED) in the same ACK    */
/* round-trip it already waits on for every other command.                 */
/* ------------------------------------------------------------------------ */

namespace MQTTCRED {

/**
 * @brief  First EEPROM address of the credential block (opposite end of the
 *         region from the settings array at EE_START_READ_INDEX -- see the
 *         layout comment on MQTTCRED_BLOCK_SIZE in DEF.h).
 */
static int EEBase() {
    return EEPROM.length() - MQTTCRED_BLOCK_SIZE;
}

/**
 * @brief  Reverse base64-alphabet lookup for one character.
 * @return 0-63 for a valid alphabet character, -1 for '=' or anything else.
 */
static int8_t b64Val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/**
 * @brief  Minimal standard-alphabet base64 decoder (no library on core).
 *
 * @param  in      Input characters (NOT null-terminated by caller's contract --
 *                  @p inLen is authoritative; trailing '=' padding is tolerated).
 * @param  inLen   Number of input characters to consume.
 * @param  out     Output buffer.
 * @param  outMax  Output buffer capacity in bytes.
 *
 * @return Decoded byte count, or -1 on an invalid character / output overflow.
 */
static int b64Decode(const char* in, int inLen, uint8_t* out, int outMax) {
    int outLen = 0;
    int val = 0, bits = -8;
    for (int i = 0; i < inLen; i++) {
        char c = in[i];
        if (c == '=') break;                       // Padding -- end of data - Logic
        int8_t d = b64Val(c);
        if (d < 0) return -1;                       // Invalid character - Guard
        val = (val << 6) + d;
        bits += 6;
        if (bits >= 0) {
            if (outLen >= outMax) return -1;        // Overflow - Guard
            out[outLen++] = (uint8_t)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return outLen;
}

/**
 * @brief  Persist State.user/State.pass to their EEPROM anchor, byte-verified.
 *
 * Only called after a live broker verify has already succeeded (see
 * cmdSetCredentials()) and only when the pair actually differs from what's
 * cached, so an unchanged force-resend (the app's 5s-hold gesture) never
 * wears EEPROM for nothing.
 */
static void Save() {
    int base = EEBase();
    EE::w(base, MQTTCRED_MAGIC);
    int p = base + 1;
    for (int i = 0; i < MQTTCRED_USER_MAX + 1; i++) EE::w(p + i, (uint8_t)State.user[i]);
    p += MQTTCRED_USER_MAX + 1;
    for (int i = 0; i < MQTTCRED_PASS_MAX + 1; i++) EE::w(p + i, (uint8_t)State.pass[i]);
    PRNT::_print(PRNT::formatMSG("%~32s # saved, user [%s]" NL, "MQTTCRED_Save", State.user));
}

/**
 * @brief  Load cached credentials from EEPROM. Call once from setup().
 *
 * Leaves State.valid=false (and both strings empty) if the magic byte at
 * EEBase() doesn't match MQTTCRED_MAGIC -- i.e. never configured, a fresh
 * board, or a still-blank/erased region. Reconnect() checks State.valid
 * before ever touching MQTT_Cli.connect().
 */
void Load() {
    int base = EEBase();
    uint8_t magic = EEPROM.read(base);
    if (magic != MQTTCRED_MAGIC) {
        State.valid   = false;
        State.user[0] = '\0';
        State.pass[0] = '\0';
        PRNT::_print(PRNT::formatMSG("%~32s # no stored credentials (magic [%d])" NL, "MQTTCRED_Load", magic));
        return;
    }

    int p = base + 1;
    for (int i = 0; i < MQTTCRED_USER_MAX + 1; i++) State.user[i] = (char)EEPROM.read(p + i);
    State.user[MQTTCRED_USER_MAX] = '\0';                     // Enforce termination - Guard
    p += MQTTCRED_USER_MAX + 1;
    for (int i = 0; i < MQTTCRED_PASS_MAX + 1; i++) State.pass[i] = (char)EEPROM.read(p + i);
    State.pass[MQTTCRED_PASS_MAX] = '\0';                     // Enforce termination - Guard

    State.valid = true;
    PRNT::_print(PRNT::formatMSG("%~32s # loaded, user [%s]" NL, "MQTTCRED_Load", State.user));
}

/**
 * @brief  '$' command handler -- "$<b64user>,<b64pass>".
 *
 * Decodes both fields, then VERIFIES them with a real (blocking) connect()
 * against the broker before trusting or persisting anything -- same
 * blocking-TLS-handshake precedent as MQTT::Reconnect(). On success the new
 * pair is adopted immediately (MQTT::State.Up=true, subscribed) and saved to
 * EEPROM only if it differs from what was already cached. On failure nothing
 * is touched -- any previously-good cached pair is left alone, and the next
 * throttled MQTT::Reconnect() cycle quietly restores that old session.
 *
 * @param  buff  "$<b64user>,<b64pass>", ACK envelope already stripped by Exec().
 * @param  len   Length of buff (excluding null terminator).
 *
 * Sets APP::State.LastResult so Exec()'s existing ACK machinery replies:
 *   APP_ACK_OK           - verified, adopted (and saved if it changed)
 *   APP_ACK_REJECTED     - malformed payload (no comma, empty field, bad b64)
 *   APP_ACK_UNAUTHORIZED - decoded fine, but the broker refused this pair
 */
void cmdSetCredentials(char *buff, int len) {
    char* payload    = buff + 1;               // Skip '$' - Mapping
    int   payloadLen = len - 1;

    char* comma = strchr(payload, ',');
    if (payloadLen < 3 || comma == NULL) {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! malformed payload (no comma)" NL, "MQTTCRED_Set"));
        return;
    }

    int userB64Len = (int)(comma - payload);
    int passB64Len = payloadLen - userB64Len - 1;
    if (userB64Len <= 0 || passB64Len <= 0) {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! malformed payload (empty user/pass)" NL, "MQTTCRED_Set"));
        return;
    }

    uint8_t userBuf[MQTTCRED_USER_MAX + 1];
    uint8_t passBuf[MQTTCRED_PASS_MAX + 1];
    int uLen = b64Decode(payload,    userB64Len, userBuf, MQTTCRED_USER_MAX);
    int pLen = b64Decode(comma + 1,  passB64Len, passBuf, MQTTCRED_PASS_MAX);
    if (uLen <= 0 || pLen <= 0) {
        APP::State.LastResult = APP_ACK_REJECTED;
        PRNT::_print(PRNT::formatMSG("%32s ! base64 decode failed (user [%d] pass [%d])" NL, "MQTTCRED_Set", uLen, pLen));
        return;
    }
    userBuf[uLen] = '\0';
    passBuf[pLen] = '\0';

    PRNT::_print(PRNT::formatMSG("%~32s # verifying candidate credentials, user [%s]" NL, "MQTTCRED_Set", (const char*)userBuf));

    // Force a clean connect attempt -- if a session with the OLD (still-good)
    // credentials happens to be up, drop it first so the result below is
    // unambiguous (PubSubClient::connect() otherwise treats an already-open
    // socket inconsistently across states).
    if (MQTT_Cli.connected()) MQTT_Cli.disconnect();

    char cid[32];
    snprintf(cid, sizeof(cid), "SmartTV-R4-%04X", APP::State.SessionId);
    bool ok = MQTT_Cli.connect(cid, (const char*)userBuf, (const char*)passBuf); // Blocking TLS handshake - Action

    if (!ok) {
        APP::State.LastResult = APP_ACK_UNAUTHORIZED;
        MQTT::State.Up = false;
        PRNT::_print(PRNT::formatMSG("%32s ! verify failed, broker state [%d]" NL, "MQTTCRED_Set", MQTT_Cli.state()));
        // Deliberately not touching State.user/State.pass/EEPROM -- a bad
        // candidate never overwrites a previously-good cached pair, and
        // MQTT::Reconnect() will quietly re-establish it on its own next tick.
        return;
    }

    MQTT_Cli.subscribe(MQTT_TOPIC);
    MQTT::State.Up      = true;
    MQTT::State.LastTry = TimeNow;

    bool changed = !State.valid
        || strcmp(State.user, (const char*)userBuf) != 0
        || strcmp(State.pass, (const char*)passBuf) != 0;

    strncpy(State.user, (const char*)userBuf, MQTTCRED_USER_MAX); State.user[MQTTCRED_USER_MAX] = '\0';
    strncpy(State.pass, (const char*)passBuf, MQTTCRED_PASS_MAX); State.pass[MQTTCRED_PASS_MAX] = '\0';
    State.valid = true;

    if (changed) Save();

    APP::State.LastResult = APP_ACK_OK;
    PRNT::_print(PRNT::formatMSG("%~32s # verified + connected, user [%s]%s" NL,
        "MQTTCRED_Set", State.user, changed ? " (saved)" : " (unchanged)"));
}

} // namespace MQTTCRED




