#include "Telnet.h"
#include "Globals.h"

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
    }
}

void SetEnabled(bool on) {
    TELNET::State.Enabled = on;
    if (!on && TELNET_Cli.connected()) TELNET_Cli.stop();
}

bool IsEnabled() {
    return TELNET::State.Enabled;
}

/** Mirrors one line to the connected client - no-op while disabled or no
    client is attached. Called by Debug::print(). */
void Mirror(const char *line) {
    if (!TELNET::State.Enabled) return;
    if (!TELNET_Cli || !TELNET_Cli.connected()) return;
    TELNET_Cli.print(line);
}

} // namespace TELNET
