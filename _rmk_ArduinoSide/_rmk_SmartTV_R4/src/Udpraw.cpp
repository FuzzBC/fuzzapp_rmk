#include "Udpraw.h"
#include "Globals.h"
#include "Led.h"
#include "Lisens.h"
#include "Eeprom.h"
#include "Net.h"
#include "AppLink.h"
#include "DifLink.h"
#include <string.h>

namespace UDPRAW {

// True whenever the non-TV zones (COM/BED/LAMP/HB, pushed via stripBack/
// stripHB) actually need to be re-latched - they only change during the
// one-shot T_UDPRAW_SET_COLOR ramp (or the Init()/End() blank), not on
// every ambilight packet, so Loop() skips those two show() calls whenever
// nothing in them moved since the last push. stripFront (the 30 live TV
// pixels) always shows. Starts true so Init()'s blank always reaches the
// strip on the very first packet of a new stream.
static bool g_nonTvDirty = true;

// FPS tracking for the live ambilight stream. Reset on Init()/End() so a
// stream restart doesn't carry over a stale window from before the gap.
static uint32_t g_fpsFrameCount  = 0;
static uint32_t g_fpsWindowStart = 0;

/** Opens UDPRAW_UDP on port 5568 if WiFi is connected. Call once in
    setup() after WiFi is connected - if not yet connected, UdpSet() is
    retried by Loop() once the connection comes up (mirrors the other
    link's Setup()/Loop() split). */
void Setup() {
    if (NET::IsConnected()) {
        UdpSet();
    }
}

/** Drains every queued ambilight UDP packet each tick and applies only the
    newest valid one (LED_TV_NUM*3 bytes) - a backlogged frame is stale by
    the time it would show, so keeping only the freshest holds perceived
    latency constant instead of letting it grow with the queue. Maps each
    RGB triplet straight onto the corresponding TV LED at lux-adjusted
    brightness. Calls Init() on the first frame after idle, End() if no
    frame arrives for UDPRAW_CHECK_TIME ms. TestMode _testmode_udpraw
    simulates a full-white packet. Call every loop() iteration when
    LED::State.Enabled is true. */
void Loop() {
    bool isConnected = NET::Connected_Cached();

    if (!isConnected && TestMode != _testmode_udpraw) return;

    bool isSimulated = (TestMode == _testmode_udpraw);
    int  packetSize  = 0;
    bool gotFrame    = false;

    if (isSimulated) {
        packetSize = (LED_TV_NUM * 3);
        gotFrame   = true;
    } else {
        int pending;
        while ((pending = UDPRAW_UDP.parsePacket()) > 0) {
            if (pending != (LED_TV_NUM * 3)) {
                UDPRAW_UDP.flush();
                continue;
            }
            UDPRAW_UDP.read(UDPRAW_Buffer, pending);
            packetSize = pending;
            gotFrame   = true;
        }
    }

    if (!gotFrame) {
        if (UDPRAW::State.Status && (TimeNow - UDPRAW::State.LastCheck > UDPRAW_CHECK_TIME)) {
            End();
        }
        return;
    }

    if (isSimulated) {
        memset(UDPRAW_Buffer, 255, packetSize);
    }

    DLOGV_UDPRAW("frame [%d] bytes", packetSize);
    UDPRAW::State.LastCheck = TimeNow;
    if (!UDPRAW::State.Status) Init();

    g_fpsFrameCount++;
    if (TimeNow - g_fpsWindowStart >= 1000) {
        UDPRAW::State.Fps = (g_fpsWindowStart == 0) ? 0.0f
                           : (g_fpsFrameCount * 1000.0f) / (float)(TimeNow - g_fpsWindowStart);
        g_fpsFrameCount  = 0;
        g_fpsWindowStart = TimeNow;
    }

    static uint32_t packetCount = 0;
    if (packetCount++ % 100 == 0 || UDPRAW::State.CachedBrightness == 0) {
        UDPRAW::State.CachedBrightness = LED::getLuxBrightness(EE::Get(EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX));
    }
    const uint8_t br = UDPRAW::State.CachedBrightness;
    uint8_t* buf = UDPRAW_Buffer;

    for (int i = 0; i < LED_TV_NUM; i++) {
        int ledIdx = LED::TV(i);

        uint8_t r = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));
        uint8_t g = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));
        uint8_t b = min(255U, (uint16_t)0 + ((*buf++ * br) >> 8));
        LED::H_writeStripPixel(ledIdx, r, g, b);
    }

    stripFront.show();
    if (g_nonTvDirty) {
        stripBack.show();
        stripHB.show();
        g_nonTvDirty = false;
    }
}

/** Smooth-fade task: steps all COM/BED/LAMP/HB LEDs toward
    LED::State.StreamColor at LED::State.StreamBrightness (lux-adjusted)
    using the raw (non-speed-adapted) EE_UDPRAW_BR_CL_INC. Self-terminates
    once every LED has reached its target. Registered by Init() with a
    UDPRAW_LED_MOVIE_DELAY start delay. */
void T_UDPRAW_SET_COLOR(SCHED::TaskId taskId) {
    (void)taskId;
    bool moving = false;

    const int targetBr = LED::getLuxBrightness(LED::State.StreamBrightness);
    const int inc = EE::Get(EE_UDPRAW_BR_CL_INC);
    const uint8_t r = LED::State.StreamColor.r;
    const uint8_t g = LED::State.StreamColor.g;
    const uint8_t b = LED::State.StreamColor.b;

    for (int i = 0; i < LED_COM_NUM + LED_BED_NUM + LED_LAMP_NUM; i++) {
        int currentLed;

        if (i < LED_COM_NUM) {
            currentLed = LED::COM(i);
        } else if (i < LED_COM_NUM + LED_BED_NUM) {
            currentLed = LED::BED(i - LED_COM_NUM);
        } else {
            currentLed = LED::LAMP(i - LED_COM_NUM - LED_BED_NUM);
        }

        if (LED::TG_BRIGHTNESS(currentLed, targetBr, inc, false)) {
            LED::setPixel(currentLed, r, g, b, LED::State.CurrentBrightness[currentLed], false);
            moving = true;
        }
    }

    for (int i = 0; i < LED_HB_NUM; i++) {
        int hIdx = LED::HB(i);

        if (LED::TG_BRIGHTNESS(hIdx, targetBr, inc, false)) {
            LED::setPixel(hIdx, r, g, b, LED::State.CurrentBrightness[hIdx], false);
            moving = true;
        }
    }

    if (moving) {
        g_nonTvDirty = true;
    } else {
        APP::updDeltaColors();
        SCHED::KillAllUnlocked();
    }
}

/** Deactivates UDPRAW streaming. Resets UDPRAW::State.Status and clears
    all LEDs. @param handover true (default, real stream timeout via
    Loop()): clears TV::State.Status and re-arms motion - the stream
    ending implies the TV went quiet. false (explicit test teardown):
    leaves TV::State.Status untouched and only re-arms motion when nothing
    else already owns the strip, then force-kills the ramp task and blanks
    the strip last, after every other call in this function that could
    otherwise re-arm a fresh transition on its way out. */
void End(bool handover) {
    DLOG_UDPRAW("stream ending, handover=[%d], ran [%.1f] fps", (int)handover, UDPRAW::State.Fps);
    if (handover) {
        TV::State.Status     = false;
        MOTION::State.Status = motON;
    } else if (!TV::State.Status && !APP::Am.Status) {
        MOTION::State.Status = motON;
    }

    UDPRAW::State.Status = false;
    UDPRAW::State.CachedBrightness = 0;
    UDPRAW::State.Fps = 0.0f;

    LED::setAll(0, 0, 0, 0);

    APP::updStatus("UDPRAW::End");
    APP::updDeltaColors();

    DIF::AutoOff();

    if (!handover) {
        LED::ForceOffAndKillTasks("UDPRAW_End");
    }
}

/** Activates UDPRAW streaming on the first valid packet: records
    UDPRAW::State.InitTime, disables MOTION/AM, clears all LEDs, syncs the
    app, and launches T_UDPRAW_SET_COLOR after UDPRAW_LED_MOVIE_DELAY ms.
    Called internally by Loop() only. */
void Init() {
    DLOG_UDPRAW("stream starting");
    UDPRAW::State.InitTime[_HH] = NET::Date.time[_HH];
    UDPRAW::State.InitTime[_MI] = NET::Date.time[_MI];
    UDPRAW::State.InitTime[_SS] = NET::Date.time[_SS];
    UDPRAW::State.InitTime[_DD] = NET::Date.time[_DD];
    UDPRAW::State.InitTime[_MM] = NET::Date.time[_MM];
    UDPRAW::State.InitTime[_YY] = NET::Date.time[_YY];

    UDPRAW::State.Status = true;
    UDPRAW::State.CachedBrightness = LED::getLuxBrightness(EE::Get(EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX));
    g_nonTvDirty = true;

    g_fpsFrameCount  = 0;
    g_fpsWindowStart = TimeNow;
    UDPRAW::State.Fps = 0.0f;

    MOTION::State.Status = motOFF;
    APP::Am.Status = false;

    LED::setAll(0, 0, 0, 0);

    APP::updStatus("UDPRAW::Init");
    APP::updDeltaColors();

    DIF_Colorx udprawColor = { LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b,
                               LED::State.StreamColor.r, LED::State.StreamColor.g, LED::State.StreamColor.b };
    DIF::AutoOn(EE_DIF_MODE_UDPRAW, &udprawColor);

    SCHED::KillAllUnlocked();
    SCHED::AddTask("UDPRAW_Init", "T_UDPRAW_SET_COLOR", T_UDPRAW_SET_COLOR, SCHED::Unit::MS,
                  EE::Get(EE_UDPRAW_BR_CL_DEL), UDPRAW_LED_MOVIE_DELAY, false);
}

/** (Re-)opens the UDPRAW UDP socket on port 5568. Call after any WiFi
    reconnect to re-bind the port; also called by Setup() on first init. */
void UdpSet() {
    UDPRAW_UDP.begin(5568);
}

} // namespace UDPRAW
