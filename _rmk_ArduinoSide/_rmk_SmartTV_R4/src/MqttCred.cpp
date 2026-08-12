#include "MqttCred.h"
#include "Globals.h"
#include "Eeprom.h"
#include "Net.h"
#include "Mqtt.h"
#include "AppLink.h"
#include "Debug.h"
#include <EEPROM.h>
#include <string.h>
#include <stdio.h>

namespace MQTTCRED {

/** First EEPROM address of the credential block - the opposite end of the
    region from the settings array at EE_START_READ_INDEX (see
    MQTTCRED_BLOCK_SIZE's layout comment in Globals.h). */
static int EEBase() {
    return EEPROM.length() - MQTTCRED_BLOCK_SIZE;
}

/** Persists State.user/State.pass to their EEPROM anchor, byte-verified.
    Only called after a live broker verify has already succeeded (see
    SetCredentials()) and only when the pair actually differs from what's
    cached, so an unchanged resend never wears EEPROM for nothing. */
static void Save() {
    int base = EEBase();
    EE::w(base, MQTTCRED_MAGIC);
    int p = base + 1;
    for (int i = 0; i < MQTTCRED_USER_MAX + 1; i++) EE::w(p + i, (uint8_t)State.user[i]);
    p += MQTTCRED_USER_MAX + 1;
    for (int i = 0; i < MQTTCRED_PASS_MAX + 1; i++) EE::w(p + i, (uint8_t)State.pass[i]);
}

/** Loads cached credentials from EEPROM. Call once from setup(). Leaves
    State.valid=false (and both strings empty) if the magic byte at
    EEBase() doesn't match MQTTCRED_MAGIC - never configured, a fresh
    board, or a still-blank/erased region. MQTT::Reconnect() checks
    State.valid before ever touching MQTT_Cli.connect(). */
void Load() {
    int base = EEBase();
    uint8_t magic = EEPROM.read(base);
    if (magic != MQTTCRED_MAGIC) {
        State.valid   = false;
        State.user[0] = '\0';
        State.pass[0] = '\0';
        return;
    }

    int p = base + 1;
    for (int i = 0; i < MQTTCRED_USER_MAX + 1; i++) State.user[i] = (char)EEPROM.read(p + i);
    State.user[MQTTCRED_USER_MAX] = '\0';
    p += MQTTCRED_USER_MAX + 1;
    for (int i = 0; i < MQTTCRED_PASS_MAX + 1; i++) State.pass[i] = (char)EEPROM.read(p + i);
    State.pass[MQTTCRED_PASS_MAX] = '\0';

    State.valid = true;
}

/** SET_MQTT_CREDENTIALS opcode handler. Verifies the candidate pair with a
    real (blocking) connect() against the broker before trusting or
    persisting anything - same blocking-TLS-handshake precedent as
    MQTT::Reconnect(). On success the new pair is adopted immediately
    (MQTT::State.Up=true, subscribed) and saved to EEPROM only if it
    differs from what was already cached. On failure nothing is touched -
    any previously-good cached pair is left alone, and the next throttled
    MQTT::Reconnect() cycle quietly restores that old session.
    @return AckResult::OK (verified+adopted), REJECTED (empty user/pass),
    or UNAUTHORIZED (broker refused this pair). */
Proto::AckResult SetCredentials(const char* user, const char* pass) {
    if (!user || !pass || user[0] == '\0' || pass[0] == '\0') {
        return Proto::AckResult::REJECTED;
    }

    // Force a clean connect attempt - if a session with the OLD (still-good)
    // credentials happens to be up, drop it first so the result below is
    // unambiguous (PubSubClient::connect() otherwise treats an already-open
    // socket inconsistently across states).
    if (MQTT_Cli.connected()) MQTT_Cli.disconnect();

    char cid[32];
    snprintf(cid, sizeof(cid), "SmartTV-R4-%s", NET::DeviceId());
    bool ok = MQTT_Cli.connect(cid, user, pass);

    if (!ok) {
        MQTT::State.Up = false;
        // Deliberately not touching State.user/State.pass/EEPROM - a bad
        // candidate never overwrites a previously-good cached pair, and
        // MQTT::Reconnect() will quietly re-establish it on its own next tick.
        return Proto::AckResult::UNAUTHORIZED;
    }

    MQTT_Cli.subscribe(MQTT::Topic_C2D());
    MQTT::State.Up      = true;
    MQTT::State.LastTry = TimeNow;

    bool changed = !State.valid
        || strcmp(State.user, user) != 0
        || strcmp(State.pass, pass) != 0;

    strncpy(State.user, user, MQTTCRED_USER_MAX); State.user[MQTTCRED_USER_MAX] = '\0';
    strncpy(State.pass, pass, MQTTCRED_PASS_MAX); State.pass[MQTTCRED_PASS_MAX] = '\0';
    State.valid = true;

    if (changed) Save();

    return Proto::AckResult::OK;
}

} // namespace MQTTCRED
