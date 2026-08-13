/* ------------------------------------------------------------------------ */
/* _rmk_SmartTV_R4.ino -- module-split rebuild                              */
/* FuZzAPP SmartTV R4 -- Arduino UNO R4 WiFi                                 */
/* ------------------------------------------------------------------------ */
/*
 * Clean-room rewrite of _ArduinoSide/_FuZzAPP_SmartTV_R4 -- see
 * _rmk_docs/00_PLAN.md for the full rebuild plan and _rmk_docs/01_PROTOCOL.md
 * for the binary wire protocol this firmware now speaks on both links.
 * setup()/loop() just wire together each module's own Setup()/Loop() -
 * all real behaviour lives in src/*.
 */
#include "src/Globals.h"
#include "src/Scheduler.h"
#include "src/Led.h"
#include "src/Lisens.h"
#include "src/Hb.h"
#include "src/Tv.h"
#include "src/Motion.h"
#include "src/Eeprom.h"
#include "src/Udpraw.h"
#include "src/Bme.h"
#include "src/Net.h"
#include "src/AppLink.h"
#include "src/DifLink.h"
#include "src/Mqtt.h"
#include "src/MqttCred.h"
#include "src/Telnet.h"
#include "src/Debug.h"

void setup() {
    Serial.begin(SERIAL_BAUD);
    randomSeed(analogRead(A0));
    delay(2000);   // wait for Serial Monitor

    Debug::log("~ SETUP ~ %s", FW_NAME);

    NET::Setup();           // WiFi connect (blocking, bounded) + first NTP attempt
    NET::RTC_Begin();       // NTP sync (must follow WiFi) - no-op if Setup() already got it
    UDPRAW::UdpSet();       // ambilight UDP socket, port 5568 (see UDPRAW::Setup()'s own WiFi guard)
    UDPRAW::Setup();
    APP::Setup();           // phone UDP socket, port 8472
    DIF::Setup();           // diffuser UDP socket, port 8439 + its two locked tasks
    MQTTCRED::Load();       // cached MQTT user/pass, own EEPROM anchor
    MQTT::Setup();          // HiveMQ Cloud TLS link (parallel to UDP)
    BME::Setup();           // I2C temperature/humidity
    LISENS::Setup();        // analog ambient light sensor
    EE::Read();             // load persisted settings (schema+CRC gated, see Eeprom.cpp)
    EE::SelfTest();         // verify last boot's EEPROM pattern actually survived

    LED::Setup();           // NeoPixel strips + pixel-order shuffle + refresh task

    TELNET::Setup();        // listens on port 23, mirroring stays OFF until SET_TELNET_ENABLE

    pinMode(MOTION_PIN_COM, INPUT);
    pinMode(MOTION_PIN_BED, INPUT_PULLUP);

    delay(1000);

    Debug::log("~ LOOP ~");
}

void loop() {
    TimeNow = millis();

    // USB-CDC Serial has no connect callback, but `(bool)Serial` reflects
    // the host's DTR line - true only once an actual terminal (Serial
    // Monitor, the FuZzAPP FlashConsole tool, etc.) has opened the port,
    // same signal Debug::printSerial() already gates every other Serial
    // write on. Watching it for a false->true edge here is the closest
    // equivalent to Telnet's real accept() event (see TELNET::Loop()) -
    // fires the welcome banner exactly once per fresh connection, not on
    // every loop() tick.
    static bool wasSerialUp = false;
    bool isSerialUp = (bool)Serial;
    if (isSerialUp && !wasSerialUp) {
        APP::sendSerialWelcome();
    }
    wasSerialUp = isSerialUp;

    APP::Loop();
    DIF::Loop();
    DIF::TickAsyncSend();   // advance any staged diffuser send one non-blocking step
    MQTT::Loop();
    TELNET::Loop();
    SCHED::RunTasks(TimeNow);

    if (LED::State.Enabled) {
        UDPRAW::Loop();
        TV::Status();

        if (!UDPRAW::State.Status) {
            MOTION::Status();
            if (LED::ShouldRefresh(TimeNow)) {
                LED::Refresh(TimeNow);
            }
        }
    }

    // Coalesced app colour sync: flush at most one TELEM_COLOR_SYNC frame
    // per FPS period, draining every LED marked dirty since the last flush.
    APP::TickColorSync(TimeNow);
}
