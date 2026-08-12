#include "Telnet.h"
#include "Globals.h"
// APP::termMsgLog() for the one-line trace below - kept in (not "TEMP") on
// purpose: live-tested and confirmed this client-accept branch never fires
// on this board at all, even after a full TCP handshake AND the client
// sending data - WiFiS3's WiFiServer::available() appears to just never
// recognize a pending client here, unlike the identical pattern on the
// Diffuser's ESP8266 core (confirmed working there). Root cause is inside
// the WiFiS3 library, not this code - not chased further since the UDP/
// MQTT LOG frame termMsgLog() always sends (see AppLink.cpp) already
// covers live diagnostics without depending on this. Left wired in case a
// future WiFiS3 update fixes it - this trace is how you'd confirm that.
#include "AppLink.h"

namespace TELNET {

/** Starts the WiFiServer listening on TELNET_PORT. Mirroring stays off
    (TELNETx::Enabled defaults false, never persisted) until SetEnabled(true)
    is called - see AppLink.cpp's SET_TELNET_ENABLE handler. Call once in
    setup(), after WiFi connects. */
void Setup() {
    TELNET_Srv.begin();
}

/** Accepts a new client (evicting whatever was connected before - single
    slot, same policy as the Diffuser's Telnet) and drops a dead one.
    Call every loop() iteration. */
void Loop() {
    if (!TELNET::State.Enabled) {
        if (TELNET_Cli.connected()) TELNET_Cli.stop();
        return;
    }

    // WiFiS3's WiFiServer has no hasClient() (unlike ESP8266/ESP32) -
    // available() itself is both the "is one waiting" check and the accept
    // step; it returns an unconnected WiFiClient when nothing's pending.
    WiFiClient incoming = TELNET_Srv.available();
    if (incoming) {
        if (TELNET_Cli.connected()) TELNET_Cli.stop();
        TELNET_Cli = incoming;
        APP::termMsgLog(APP_LOG_WRN, APP_SRC_SYS, "APP", "Telnet::Loop", "client accepted, connected=[%d]", (int)TELNET_Cli.connected());
    }
}

void SetEnabled(bool on) {
    TELNET::State.Enabled = on;
    if (!on && TELNET_Cli.connected()) TELNET_Cli.stop();
}

bool IsEnabled() {
    return TELNET::State.Enabled;
}

void SetVerbosity(uint8_t level) {
    TELNET::State.Verbosity = level;
}

uint8_t Verbosity() {
    return TELNET::State.Verbosity;
}

/** Mirrors one line to the connected client - no-op while disabled or no
    client is attached. Called by Debug::print(). */
void Mirror(const char *line) {
    if (!TELNET::State.Enabled) return;
    if (!TELNET_Cli || !TELNET_Cli.connected()) return;
    TELNET_Cli.print(line);
}

} // namespace TELNET
