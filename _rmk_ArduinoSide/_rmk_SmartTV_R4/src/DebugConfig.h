#pragma once
// DebugConfig.h - compile-time debug/verbose switches, one pair per module,
// ALL OFF (0) by default. Flip a module's DBG_<NAME> to 1 for normal debug
// detail (state transitions, decisions, results) or DBG_<NAME>_V to 1 for
// verbose detail (per-tick/per-frame/per-byte) while actively diagnosing
// that module, then set both back to 0 - these are NOT meant to ship
// enabled. Mirrors the original firmware's ENABLE_LOG_X / ENABLE_LOG_X_
// VERBOSE #ifdef pattern, adapted to this rebuild's module split and
// routed through the same termMsgLog() channel as everything else (Serial
// mirror, Telnet mirror - gated further by SET_TELNET_VERBOSITY>=1 - and
// the UDP/MQTT LOG frame) instead of a Serial-only path, so enabling a
// flag doesn't need any transport rewiring, just a recompile+reflash.
//
// Zero cost when off: the DLOG_<mod>()/DLOGV_<mod>() macros below expand
// to nothing when their flag is 0, at the preprocessor level - a disabled
// call site's arguments are never evaluated, formatted, or linked in.
//
// Usage in a module's .cpp (already includes AppLink.h for APP::termMsgLog
// and Globals.h for this header, both pulled in by nearly every module):
//   DLOG_TV("pin read [%d] debounced", pin);           // normal tier
//   DLOGV_TV("raw pin sample [%d] at [%lu] ms", pin, millis()); // verbose tier

#define DBG_NET          0   // Net.cpp - WiFi connect/reconnect/RTC sync
#define DBG_NET_V        0
#define DBG_LED          0   // Led.cpp - refresh/shuffle/effects
#define DBG_LED_V        0
#define DBG_TV           0   // Tv.cpp - on/off/status pin debounce
#define DBG_TV_V         0
#define DBG_MOTION       0   // Motion.cpp - sensor state machine
#define DBG_MOTION_V     0
#define DBG_HB           0   // Hb.cpp - heartbeat animation phases
#define DBG_HB_V         0
#define DBG_EE           0   // Eeprom.cpp - read/write/CRC/self-test
#define DBG_EE_V         0
#define DBG_UDPRAW       0   // Udpraw.cpp - ambilight UDP frames
#define DBG_UDPRAW_V     0
#define DBG_BME          0   // Bme.cpp - sensor reads
#define DBG_BME_V        0
#define DBG_LISENS       0   // Lisens.cpp - ambient light sensor
#define DBG_LISENS_V     0
#define DBG_APP          0   // AppLink.cpp - phone command dispatch
#define DBG_APP_V        0
#define DBG_DIF          0   // DifLink.cpp - diffuser relay/commands
#define DBG_DIF_V        0
#define DBG_MQTT         0   // Mqtt.cpp - cloud publish/subscribe
#define DBG_MQTT_V       0
#define DBG_MQTTCRED     0   // MqttCred.cpp - credential provisioning
#define DBG_MQTTCRED_V   0
#define DBG_TELNET       0   // Telnet.cpp - session/command handling
#define DBG_TELNET_V     0
#define DBG_TASK         0   // Scheduler.cpp - task add/kill/run
#define DBG_TASK_V       0

// ---- Macro pairs: real call when the flag is 1, no-op (unevaluated) when 0 ----
#if DBG_NET
  #define DLOG_NET(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "NET", __func__, __VA_ARGS__)
#else
  #define DLOG_NET(...)   ((void)0)
#endif
#if DBG_NET_V
  #define DLOGV_NET(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_NET, "NET", __func__, __VA_ARGS__)
#else
  #define DLOGV_NET(...)  ((void)0)
#endif

#if DBG_LED
  #define DLOG_LED(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "LED", __func__, __VA_ARGS__)
#else
  #define DLOG_LED(...)   ((void)0)
#endif
#if DBG_LED_V
  #define DLOGV_LED(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_LED, "LED", __func__, __VA_ARGS__)
#else
  #define DLOGV_LED(...)  ((void)0)
#endif

#if DBG_TV
  #define DLOG_TV(...)    APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "TV", __func__, __VA_ARGS__)
#else
  #define DLOG_TV(...)    ((void)0)
#endif
#if DBG_TV_V
  #define DLOGV_TV(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_TV, "TV", __func__, __VA_ARGS__)
#else
  #define DLOGV_TV(...)   ((void)0)
#endif

#if DBG_MOTION
  #define DLOG_MOTION(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "MOTION", __func__, __VA_ARGS__)
#else
  #define DLOG_MOTION(...)  ((void)0)
#endif
#if DBG_MOTION_V
  #define DLOGV_MOTION(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_MOTION, "MOTION", __func__, __VA_ARGS__)
#else
  #define DLOGV_MOTION(...) ((void)0)
#endif

#if DBG_HB
  #define DLOG_HB(...)    APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "HB", __func__, __VA_ARGS__)
#else
  #define DLOG_HB(...)    ((void)0)
#endif
#if DBG_HB_V
  #define DLOGV_HB(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_HB, "HB", __func__, __VA_ARGS__)
#else
  #define DLOGV_HB(...)   ((void)0)
#endif

#if DBG_EE
  #define DLOG_EE(...)    APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "EE", __func__, __VA_ARGS__)
#else
  #define DLOG_EE(...)    ((void)0)
#endif
#if DBG_EE_V
  #define DLOGV_EE(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_EE, "EE", __func__, __VA_ARGS__)
#else
  #define DLOGV_EE(...)   ((void)0)
#endif

#if DBG_UDPRAW
  #define DLOG_UDPRAW(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_UDPRAW, "UDPRAW", __func__, __VA_ARGS__)
#else
  #define DLOG_UDPRAW(...)  ((void)0)
#endif
#if DBG_UDPRAW_V
  #define DLOGV_UDPRAW(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_UDPRAW, "UDPRAW", __func__, __VA_ARGS__)
#else
  #define DLOGV_UDPRAW(...) ((void)0)
#endif

#if DBG_BME
  #define DLOG_BME(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_BME, "BME", __func__, __VA_ARGS__)
#else
  #define DLOG_BME(...)   ((void)0)
#endif
#if DBG_BME_V
  #define DLOGV_BME(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_BME, "BME", __func__, __VA_ARGS__)
#else
  #define DLOGV_BME(...)  ((void)0)
#endif

#if DBG_LISENS
  #define DLOG_LISENS(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "LISENS", __func__, __VA_ARGS__)
#else
  #define DLOG_LISENS(...)  ((void)0)
#endif
#if DBG_LISENS_V
  #define DLOGV_LISENS(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_LUX, "LISENS", __func__, __VA_ARGS__)
#else
  #define DLOGV_LISENS(...) ((void)0)
#endif

#if DBG_APP
  #define DLOG_APP(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "APP", __func__, __VA_ARGS__)
#else
  #define DLOG_APP(...)   ((void)0)
#endif
#if DBG_APP_V
  #define DLOGV_APP(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_APP, "APP", __func__, __VA_ARGS__)
#else
  #define DLOGV_APP(...)  ((void)0)
#endif

#if DBG_DIF
  #define DLOG_DIF(...)   APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "DIF", __func__, __VA_ARGS__)
#else
  #define DLOG_DIF(...)   ((void)0)
#endif
#if DBG_DIF_V
  #define DLOGV_DIF(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_DIF, "DIF", __func__, __VA_ARGS__)
#else
  #define DLOGV_DIF(...)  ((void)0)
#endif

#if DBG_MQTT
  #define DLOG_MQTT(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "MQTT", __func__, __VA_ARGS__)
#else
  #define DLOG_MQTT(...)  ((void)0)
#endif
#if DBG_MQTT_V
  #define DLOGV_MQTT(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "MQTT", __func__, __VA_ARGS__)
#else
  #define DLOGV_MQTT(...) ((void)0)
#endif

#if DBG_MQTTCRED
  #define DLOG_MQTTCRED(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "MQTTCRED", __func__, __VA_ARGS__)
#else
  #define DLOG_MQTTCRED(...)  ((void)0)
#endif
#if DBG_MQTTCRED_V
  #define DLOGV_MQTTCRED(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "MQTTCRED", __func__, __VA_ARGS__)
#else
  #define DLOGV_MQTTCRED(...) ((void)0)
#endif

#if DBG_TELNET
  #define DLOG_TELNET(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "TELNET", __func__, __VA_ARGS__)
#else
  #define DLOG_TELNET(...)  ((void)0)
#endif
#if DBG_TELNET_V
  #define DLOGV_TELNET(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_SYS, "TELNET", __func__, __VA_ARGS__)
#else
  #define DLOGV_TELNET(...) ((void)0)
#endif

#if DBG_TASK
  #define DLOG_TASK(...)  APP::termMsgLog(APP_LOG_DBG, APP_SRC_TASK, "TASK", __func__, __VA_ARGS__)
#else
  #define DLOG_TASK(...)  ((void)0)
#endif
#if DBG_TASK_V
  #define DLOGV_TASK(...) APP::termMsgLog(APP_LOG_DBG, APP_SRC_TASK, "TASK", __func__, __VA_ARGS__)
#else
  #define DLOGV_TASK(...) ((void)0)
#endif
