#pragma once
// Telnet.h - SmartTV debug console (port 23). OFF at every boot/reflash by
// design - no EEPROM persistence, no auto-enable. The only way to turn it
// on is the new SET_TELNET_ENABLE opcode (00_PLAN.md SS9 R1: this
// capability didn't exist in the original firmware; added fresh here, and
// the opcode itself was added to protocol_table.json during this port -
// see 01_PROTOCOL.md's opcode table history). Read-only: mirrors every
// Debug::print() line to the connected client - no command input is
// parsed from this socket, unlike the Diffuser's Telnet (a real console).
// Single client slot: a new connection evicts whatever was there, same
// eviction policy as the Diffuser's Telnet (WiFiClient::connected() can
// lag on a dead peer).
//
// KNOWN BROKEN (live-tested, not chased further - see Telnet.cpp's Loop()
// comment): WiFiS3's WiFiServer::available() never recognizes a connecting
// client on this board, even after a full TCP handshake and the client
// sending data - looks like a library-level gap on this specific core, not
// a bug in this file (the identical pattern works on the Diffuser's
// ESP8266 core). SET_TELNET_ENABLE/_VERBOSITY are still real, ACKed
// commands and termMsgLog()'s UDP/MQTT LOG frame is unaffected either way
// - only the actual Telnet text mirror doesn't work right now.
#include <Arduino.h>

namespace TELNET {

void Setup();     // starts the WiFiServer listening, does NOT enable mirroring
void Loop();      // accepts/evicts clients, call every loop() iteration
void SetEnabled(bool on);
bool IsEnabled();
void SetVerbosity(uint8_t level);   // 0 normal, 1 debug, 2 verbose - see Globals.h TELNETx
uint8_t Verbosity();
void Mirror(const char *line);   // called by Debug::print() - no-op while disabled/no client

} // namespace TELNET
