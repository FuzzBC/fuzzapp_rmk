// _rmk_Diffuser.ino
// WeMos D1 Mini - WiFi + OTA + UDP (binary, see _rmk_docs/01_PROTOCOL.md) +
// Telnet + diffuser virtual buttons. Clean-room rewrite of
// _ArduinoSide/_FuZzAPP_Diffuser - see _rmk_docs/00_PLAN.md for the target
// architecture and _rmk_docs/02_REGRESSION.md SS B for the behaviour this
// must preserve.
//
// Module split (was one file - see src/*.h/*.cpp):
//   Log        debug output sink (Serial + Telnet)
//   Strip      WS2812 rendering/effects
//   ModeButton virtual MODE button state machine
//   Buzzer     buzzer confirmation detection
//   Usage      refill tracking + EEPROM (now schema-versioned + CRC16)
//   Parfum     timed insist dispense mode
//   Diag       Link A read-only diagnostics
//   Wifi       WiFi connect/reconnect sequencing + OTA
//   ProtoLink  Link A binary protocol (replaces the old ASCII "Dx"/"#SS" scheme)
//   Telnet     console (port 23) - unchanged, this one already worked
//   Test       self-test ("T", DIF_TEST_MODE builds only)
//
// GPIO bridge <-> diffuser (unchanged from production):
//   OUT MODE_PIN D6   short 100ms = advance mode    long 2000ms = shutdown
//   IN  BUZZER  A0    1 beep = mode confirmed        >1 beep = shutdown
// LED: WiFi OK -> solid OFF (active-LOW), WiFi lost -> blink (500 ms)
// STRIP: WS2812 x10 on STRIP_PIN

#include "_rmk_Diffuser_DEF.h"
#include "src/Globals.h"
#include "src/Log.h"
#include "src/Strip.h"
#include "src/ModeButton.h"
#include "src/Buzzer.h"
#include "src/Usage.h"
#include "src/Parfum.h"
#include "src/Wifi.h"
#include "src/ProtoLink.h"
#include "src/Telnet.h"
#include <ArduinoOTA.h>
#include <EEPROM.h>

void setup() {
    Serial.begin(115200);
    delay(100);
    LOG::logMsg("BOOT", "fuZz D1 Mini (rmk) starting...");

    pinMode(LED_PIN,   OUTPUT); digitalWrite(LED_PIN, HIGH);
    pinMode(MODE_PIN,  INPUT);

    g_strip.begin();
    g_strip.show();

    WIFI::connectWiFi();
    WIFI::setupOTA();

    PROTOLINK::udpOpen();

    long sum = 0;
    for (int i = 0; i < BUZZER_CAL_SAMPLES; i++) { sum += analogRead(BUZZER_PIN); delay(2); }
    g_buzBaseline = sum / BUZZER_CAL_SAMPLES;
    LOG::logMsg("BUZZ", "baseline: %d", g_buzBaseline);

    EEPROM.begin(EE_USAGE_SIZE);
    USAGE::loadUsageFromEeprom();

    unsigned long now = millis();
    g_tWifi = g_tLed = g_tBuzzer = g_tUsage = now;
    g_usageLastTickMs = g_usageLastSaveMs = g_usageLastCheckMs = now;
    if (g_powered) g_powerOnMs = now;

    LOG::logMsg("BOOT", "done - modes 0-%u, strip x%u LEDs", MODE_MAX, STRIP_LED_COUNT);
    LOG::logMsg("DIF", "powered=%s mode=%s strip=%s",
              g_powered ? "true" : "false",
              MODE::modeName(g_mode),
              g_stripMode == STRIP_OFF ? "off" : "on");
}

void loop() {
    unsigned long now = millis();

    ArduinoOTA.handle();
    PROTOLINK::tick();

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
