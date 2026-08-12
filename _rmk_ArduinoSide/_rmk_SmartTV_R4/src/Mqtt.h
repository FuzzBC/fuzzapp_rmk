#pragma once
// Mqtt.h - HiveMQ Cloud link (TLS 8883), Link B's automatic fallback when
// UDP isn't live. Adapted from _FuZzAPP_SmartTV_R4.ino's MQTT namespace
// (~220 lines) to the redesigned wire format (01_PROTOCOL.md SS5): two
// one-way topics (fuzz/<deviceId>/b/c2d, .../d2c) carrying raw v1 frame
// bytes, replacing the original's single self-tagged duplex topic + ASCII
// command text. No more sender-tag byte or "is this my own echo" check -
// the topic split makes that class of bug structurally impossible; no more
// base64 - v1 frames are binary already.
#include <Arduino.h>

namespace MQTT {

void Setup();
void Loop();
void Callback(char* topic, byte* payload, unsigned int length);
void Publish(const uint8_t* buf, size_t len);
void Reconnect();

// Topic this board subscribes to (controller->device). Exposed so
// MqttCred::SetCredentials() can (re)subscribe after a credential-verify
// connect, same as Reconnect() does.
const char* Topic_C2D();

} // namespace MQTT
