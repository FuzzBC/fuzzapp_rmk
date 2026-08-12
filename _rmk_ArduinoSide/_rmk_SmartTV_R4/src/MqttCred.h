#pragma once
// MqttCred.h - EEPROM-backed, app-provisioned MQTT broker credentials.
// Adapted from _FuZzAPP_SmartTV_R4.ino's MQTTCRED namespace (~190 lines):
// the base64 decode is gone (dropped from AppLink's SET_MQTT_CREDENTIALS
// opcode too - see 01_PROTOCOL.md SS3, v1 frames carry raw bytes natively),
// so SetCredentials() takes already-decoded strings instead of parsing a
// "$<b64user>,<b64pass>" ASCII payload itself. Load()/Save() (the EEPROM
// I/O) are otherwise unchanged. A candidate pair is still verified with a
// REAL blocking connect() against the broker before it's trusted or
// persisted, so a typo/garbage pair can never silently "stick".
#include <protocol_opcodes.h>

namespace MQTTCRED {

void Load();
Proto::AckResult SetCredentials(const char* user, const char* pass);

} // namespace MQTTCRED
