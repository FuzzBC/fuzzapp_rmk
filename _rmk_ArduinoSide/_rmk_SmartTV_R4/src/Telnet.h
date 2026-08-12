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
#include <Arduino.h>

namespace TELNET {

void Setup();     // starts the WiFiServer listening, does NOT enable mirroring
void Loop();      // accepts/evicts clients, call every loop() iteration
void SetEnabled(bool on);
bool IsEnabled();
void Mirror(const char *line);   // called by Debug::print() - no-op while disabled/no client

} // namespace TELNET
