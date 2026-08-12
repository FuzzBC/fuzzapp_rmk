#include "Wifi.h"
#include "Globals.h"
#include "Log.h"
#include "Strip.h"
#include "ModeButton.h"
#include "ProtoLink.h"
#include "../_rmk_Diffuser_DEF.h"
#include <ArduinoOTA.h>

namespace WIFI {

static void beginWifiConnect() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    g_wifiConnecting = true;
    g_wifiConnNextMs = millis();
    g_stripMode      = STRIP_WIFI_CUE;
    g_animPhase      = 0;
}

/** Kick off step-by-step MODE detection; call once per successful WiFi
    (re)connect. Only the FIRST connect of a power-on cycle runs this - see
    g_coldBoot's declaration in Globals.h. */
static void startWifiSequence() {
    if (g_parfumActive) {
        vlogMsg("SEQ", "parfum active - calibration deferred");
        g_seqPhase = SEQ_DONE;
        return;
    }
    g_modeTargetActive = false;
    g_modeTargetWraps  = false;
    g_modePending      = false;

    if (!g_powered) {
        vlogMsg("SEQ", "already OFF - calibration not needed");
        g_seqPhase = SEQ_DONE;
        return;
    }
    g_seqPhase        = SEQ_MODE_STEP;
    g_seqModeAttempts = 0;
    g_seqNextMs       = millis();
    vlogMsg("SEQ", "WiFi connected - stepping MODE until shutdown beep detected");
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    LOG::logMsg("WIFI", "connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    beginWifiConnect();
    unsigned long deadline = millis() + WIFI_BOOT_CONNECT_TIMEOUT_MS;
    while (g_wifiConnecting) {
        MODE::tickPinRelease();
        tickWifiConnecting();
        STRIP::tickStripWifiCue();
        STRIP::tickStripFade();
        if (g_wifiConnecting && (long)(millis() - deadline) >= 0) {
            LOG::logMsg("WIFI", "connect timed out after %lums - continuing boot, retrying in background", WIFI_BOOT_CONNECT_TIMEOUT_MS);
            g_wifiConnecting = false;
            return;
        }
        yield();
    }
}

void finishWifiConnect() {
    g_wifiConnecting = false;
    STRIP::stripOff();
    LOG::logMsg("WIFI", "OK: %s", WiFi.localIP().toString().c_str());
    g_telnetSrv.begin();
    g_telnetSrv.setNoDelay(true);
    PROTOLINK::udpOpen();
    if (g_coldBoot) {
        g_coldBoot = false;
        startWifiSequence();
    } else {
        vlogMsg("SEQ", "reconnect - skipping MODE resync (not a fresh boot)");
    }
}

void setupOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        LOG::logMsg("OTA", "start: %s",
            (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem");
    });
    ArduinoOTA.onEnd([]()                     { LOG::logMsg("OTA", "done - rebooting"); ESP.restart(); });
    ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
#ifdef DIF_DEBUG_VERBOSE
        LOG::dbgPrintf("[OTA   ] %u%%\r", p / (t / 100));
#else
        (void)p; (void)t;
#endif
    });
    ArduinoOTA.onError([](ota_error_t e) {
        const char* reason =
            e == OTA_AUTH_ERROR    ? "Auth Failed"    :
            e == OTA_BEGIN_ERROR   ? "Begin Failed"   :
            e == OTA_CONNECT_ERROR ? "Connect Failed" :
            e == OTA_RECEIVE_ERROR ? "Receive Failed" :
            e == OTA_END_ERROR     ? "End Failed"     : "Unknown";
        LOG::logMsg("OTA", "error[%u]: %s", e, reason);
    });
    ArduinoOTA.begin();
    LOG::logMsg("OTA", "ready - hostname: %s", OTA_HOSTNAME);
}

void tickLed() {
    if (WiFi.status() == WL_CONNECTED) digitalWrite(LED_PIN, HIGH);
    else                               digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

void tickWifi() {
    if (WiFi.status() == WL_CONNECTED) return;
    if (g_wifiConnecting) return;
    LOG::logMsg("WIFI", "lost - reconnecting...");
    WiFi.disconnect();
    beginWifiConnect();
}

void tickWifiConnecting() {
    if (!g_wifiConnecting) return;
    if (WiFi.status() == WL_CONNECTED) { finishWifiConnect(); return; }
    unsigned long now = millis();
    if (now < g_wifiConnNextMs) return;
    LOG::dbgWrite(".");
    g_wifiConnNextMs = now + WIFI_SEQ_STEP_MS;
}

void tickWifiSequence() {
    if (!wifiSequenceActive()) return;
    if (g_modePending) return;
    if (g_modePinPending) return;

    unsigned long now = millis();
    if (now < g_seqNextMs) return;

    if (g_seqModeAttempts >= WIFI_SEQ_MODE_MAX_ATTEMPTS) {
        LOG::logMsg("SEQ", "MODE step cap reached - forcing long-press shutdown");
        MODE::pulseModeRaw(BTN_LONG_MS);
        MODE::applyShutdown();
        g_seqPhase = SEQ_DONE;
        return;
    }

    MODE::pulseModeShort();
    g_modePending = true;
    g_modePendMs  = now;
    g_seqModeAttempts++;
    vlogMsg("SEQ", "MODE step press #%u", g_seqModeAttempts);
}

bool wifiSequenceActive() {
    return g_seqPhase != SEQ_NONE && g_seqPhase != SEQ_DONE;
}

} // namespace WIFI
