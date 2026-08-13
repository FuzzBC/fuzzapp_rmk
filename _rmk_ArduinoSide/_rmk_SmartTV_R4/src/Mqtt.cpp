#include "Mqtt.h"
#include "Globals.h"
#include "Net.h"
#include "Udpraw.h"
#include "AppLink.h"
#include "MqttCred.h"
#include "Debug.h"
#include <string.h>
#include <stdio.h>

namespace MQTT {

static void Dispatch();

static char g_topicC2D[6 + MQTT_DEVICE_ID_MAX + 8];
static char g_topicD2C[6 + MQTT_DEVICE_ID_MAX + 8];
static bool g_topicsBuilt = false;

static void buildTopics() {
    if (g_topicsBuilt) return;
    snprintf(g_topicC2D, sizeof(g_topicC2D), "%s%s%s", MQTT_TOPIC_BASE, NET::DeviceId(), MQTT_TOPIC_C2D_SUFFIX);
    snprintf(g_topicD2C, sizeof(g_topicD2C), "%s%s%s", MQTT_TOPIC_BASE, NET::DeviceId(), MQTT_TOPIC_D2C_SUFFIX);
    g_topicsBuilt = true;
}

/** One-time client config + first connect. Call once from setup(). Both
    the TCP+TLS handshake and PubSubClient's own CONNACK wait are capped
    (4s each) - both default to effectively-unbounded blocking on this
    board otherwise, and Reconnect() calls them synchronously from loop(),
    which would freeze LED refresh/lux sampling/TV detection for an
    unbounded stretch while the broker is unreachable. */
void Setup() {
    buildTopics();

    MQTT_Cli.setServer(MQTT_HOST, MQTT_PORT);
    MQTT_Cli.setCallback(Callback);
    MQTT_Cli.setBufferSize(MQTT_BUF_SIZE);
    MQTT_Cli.setKeepAlive(MQTT_KEEPALIVE);

    MQTT_Net.setConnectionTimeout(4000);
    MQTT_Cli.setSocketTimeout(4);

    if (NET::IsConnected()) Reconnect();
}

/** Services the broker each loop(): reconnects if down, else pumps the
    client + dispatches any staged command every 30ms (keepalive is 10s,
    huge slack). Self-heals on WiFi regain via the shared NET cache - no
    hook needed in NET::Check(). */
void Loop() {
    if (!NET::Connected_Cached()) { MQTT::State.Up = false; return; }
    if (!MQTT_Cli.connected()) { MQTT::State.Up = false; Reconnect(); return; }

    static uint32_t lastMqttPump = 0;
    if (TimeNow - lastMqttPump >= 30) {
        MQTT_Cli.loop();
        Dispatch();
        lastMqttPump = TimeNow;
    }
}

/** Broker RX callback - stages the raw frame bytes for Dispatch() to run
    after MQTT_Cli.loop() returns (publishing from inside this callback is
    unsafe). Only ever fires for the c2d topic (this board never publishes
    there), so unlike the original there's no self-echo to filter and no
    sender-tag byte to strip - the topic split removes that whole problem. */
void Callback(char* topic, byte* payload, unsigned int length) {
    (void)topic;
    if (length < 1) return;
    DLOGV_MQTT("rx [%d] bytes on [%s]%s", length, topic, MQTT::State.RxPending ? " (dropped, previous still pending)" : "");
    if (MQTT::State.RxPending) return;
    unsigned int n = length;
    if (n >= APP_UDP_MAX_BUFFER_SIZE) n = APP_UDP_MAX_BUFFER_SIZE - 1;
    memcpy(APP::State.RecvBuff, payload, n);
    MQTT::State.RxLen     = (int)n;
    MQTT::State.RxPending = true;
}

/** Runs a staged frame through the same binary dispatcher UDP uses
    (APP::dispatchFrame) - both transports carry identical v1 frame
    bytes (01_PROTOCOL.md SS5), so no ASCII newline-stripping or
    null-termination is needed here unlike the original. */
static void Dispatch() {
    if (!MQTT::State.RxPending) return;
    int len = MQTT::State.RxLen;
    MQTT::State.RxPending = false;

    APP::Ard.LastReceive = TimeNow;
    APP::dispatchFrame((const uint8_t*)APP::State.RecvBuff, len, APP_TRANSPORT_MQTT);
}

/** Publishes a raw v1 frame on the d2c topic (device -> controller). No
    sender-tag byte needed - this board never subscribes to d2c, so there's
    no echo to distinguish. */
void Publish(const uint8_t* buf, size_t len) {
    if (!MQTT::State.Up || buf == NULL || len == 0) return;
    buildTopics();
    DLOGV_MQTT("tx [%d] bytes on [%s]", (int)len, g_topicD2C);
    MQTT_Cli.beginPublish(g_topicD2C, len, false);
    MQTT_Cli.write(buf, len);
    MQTT_Cli.endPublish();
}

const char* Topic_C2D() {
    buildTopics();
    return g_topicC2D;
}

/** Throttled (re)connect. Client id carries APP::State.SessionId so a
    reboot presents a fresh id and HiveMQ never kicks us for a duplicate.
    On success, subscribes the c2d topic. Deferred entirely while an
    ambilight stream is active or the TV on/off transition is mid-flight -
    a stall here (the connect() call is a blocking TLS handshake of
    unbounded duration) would otherwise freeze that ramp for 1-2+ seconds.
    Skipped before touching LastTry so the very next attempt isn't held
    back by an extra throttle wait it never used. Does nothing if no
    credentials have ever been provisioned (MqttCred::State.valid).
    Backs the retry interval off after MQTT_BACKOFF_STREAK consecutive
    failures (e.g. a persistently rejected credential) - each attempt is
    still a blocking call of unbounded duration, so a tight failure loop
    burns disproportionately more of loop()'s time than a healthy one ever
    would; spacing failed attempts out further keeps that bounded without
    touching the per-attempt call itself. */
void Reconnect() {
    if (UDPRAW::State.Status || TV::State.Transitioning) return;
    if (!MQTTCRED::State.valid) return;
    uint32_t retryMs = (MQTT::State.FailStreak >= MQTT_BACKOFF_STREAK) ? MQTT_BACKOFF_RETRY_MS : MQTT_RETRY_MS;
    if (TimeNow - MQTT::State.LastTry < retryMs) return;
    MQTT::State.LastTry = TimeNow;

    buildTopics();

    char cid[32];
    snprintf(cid, sizeof(cid), "SmartTV-R4-%s", NET::DeviceId());

    bool ok = MQTT_Cli.connect(cid, MQTTCRED::State.user, MQTTCRED::State.pass);
    if (ok) {
        MQTT::State.Up = true;
        MQTT::State.FailStreak = 0;
        MQTT_Cli.subscribe(g_topicC2D);
        APP::termMsgLog(APP_LOG_INF, APP_SRC_SYS, "APP", "MQTT::Reconnect", "connected, cid=[%s] sub=[%s]", cid, g_topicC2D);
    } else {
        MQTT::State.Up = false;
        if (MQTT::State.FailStreak < 255) MQTT::State.FailStreak++;
        // PubSubClient::state() codes: -4 TIMEOUT, -3 CONNECTION_LOST,
        // -2 CONNECT_FAILED, -1 DISCONNECTED, 1 BAD_PROTOCOL,
        // 2 BAD_CLIENT_ID, 3 UNAVAILABLE, 4 BAD_CREDENTIALS, 5 UNAUTHORIZED.
        APP::termMsgLog(APP_LOG_WRN, APP_SRC_SYS, "APP", "MQTT::Reconnect", "connect FAILED state=[%d] streak=[%d] cid=[%s] user=[%s]", MQTT_Cli.state(), MQTT::State.FailStreak, cid, MQTTCRED::State.user);
    }
}

} // namespace MQTT
