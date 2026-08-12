#pragma once
/*
 * GENERATED FILE - DO NOT EDIT.
 * Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json
 * Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py
 * See _rmk_docs/01_PROTOCOL.md for the full protocol specification.
 */
#include <stdint.h>
#include <stddef.h>

namespace Proto {

constexpr uint8_t  FRAME_MAGIC          = 0xF2;
constexpr uint8_t  FRAME_OVERHEAD_BYTES = 5;
constexpr uint16_t FRAME_MAX_BYTES      = 255;

enum class Flag : uint8_t {
    ACK_REQ        = 1 << 0,
    IS_ACK         = 1 << 1,
    IS_TELEMETRY   = 1 << 2,
    FRAGMENTED     = 1 << 3,
    LAST_FRAG      = 1 << 4,
};

enum class AckResult : uint8_t {
    OK                 = 0,
    CLAMPED            = 1,
    REJECTED           = 2,
    BLOCKED            = 3,
    LOCKED             = 4,
    UNREACHABLE        = 5,
    UNSUPPORTED        = 6,
    UNAUTHORIZED       = 7,
    VERSION_MISMATCH   = 8,
};
const char* AckResultName(AckResult r);

enum class AckTier : uint8_t { FAST, RELAY, SLOW, NONE };
struct AckTierSpec { uint16_t timeoutMs; uint8_t maxRetries; };
const AckTierSpec& AckTierOf(AckTier t);

enum class Opcode : uint8_t {
    ACK                        = 0x00,  // link=both dir=reply ack=NONE
    HELLO                      = 0x01,  // link=both dir=cmd ack=FAST
    KEEPALIVE                  = 0x02,  // link=both dir=cmd ack=NONE
    LOG                        = 0x03,  // link=both dir=telem ack=NONE
    DIAG_HEALTH                = 0x04,  // link=both dir=cmd ack=FAST
    DIAG_PARFUM_TRACE          = 0x05,  // link=A dir=cmd ack=FAST
    LED_SET_COLOR              = 0x10,  // link=B dir=cmd ack=FAST
    LED_GET_COLOR              = 0x11,  // link=B dir=cmd ack=NONE
    LED_SET_DUAL_COLOR         = 0x12,  // link=B dir=cmd ack=FAST
    LED_GET_DUAL_COLOR         = 0x13,  // link=B dir=cmd ack=NONE
    LED_SET_SELECTION          = 0x14,  // link=B dir=cmd ack=FAST
    LED_SET_BRIGHTNESS         = 0x15,  // link=B dir=cmd ack=FAST
    LED_SET_ENABLE             = 0x16,  // link=B dir=cmd ack=FAST
    TELEM_COLOR_SYNC           = 0x1A,  // link=B dir=telem ack=NONE
    TELEM_DUAL_COLOR           = 0x1B,  // link=B dir=telem ack=NONE
    TELEM_MAX_BRIGHTNESS       = 0x1C,  // link=B dir=telem ack=NONE
    TELEM_ENABLE               = 0x1D,  // link=B dir=telem ack=NONE
    SETTINGS_READ_ALL          = 0x20,  // link=B dir=cmd ack=NONE
    SETTINGS_READ_ONE          = 0x21,  // link=B dir=cmd ack=NONE
    SETTINGS_WRITE             = 0x22,  // link=B dir=cmd ack=FAST
    TELEM_SETTINGS_FULL        = 0x23,  // link=B dir=telem ack=NONE
    TELEM_SETTINGS_ONE         = 0x24,  // link=B dir=telem ack=NONE
    TELEM_SAVE_RESULT          = 0x25,  // link=B dir=telem ack=NONE
    TELEM_STATUS               = 0x30,  // link=B dir=telem ack=NONE
    TELEM_CLIMATE              = 0x31,  // link=B dir=telem ack=NONE
    TELEM_LUX                  = 0x32,  // link=B dir=telem ack=NONE
    TELEM_LINK                 = 0x33,  // link=B dir=telem ack=NONE
    TELEM_FAULTS               = 0x34,  // link=B dir=telem ack=NONE
    TELEM_TEST_MODE            = 0x35,  // link=B dir=telem ack=NONE
    SET_AMBIENT_MODE           = 0x40,  // link=B dir=cmd ack=FAST
    SET_TEST_MODE              = 0x41,  // link=B dir=cmd ack=FAST
    SET_TEST_DIFFUSER          = 0x42,  // link=B dir=cmd ack=RELAY
    SET_TEST_LUX               = 0x43,  // link=B dir=cmd ack=FAST
    SET_TELNET_ENABLE          = 0x44,  // link=B dir=cmd ack=FAST
    SET_MQTT_CREDENTIALS       = 0x50,  // link=B dir=cmd ack=SLOW
    DIFFUSER_STATUS_QUERY      = 0x60,  // link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_QUERY     = 0x61,  // link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_REMOVE    = 0x62,  // link=both dir=cmd ack=RELAY
    DIFFUSER_MANUAL_REFILL     = 0x63,  // link=both dir=cmd ack=RELAY
    DIFFUSER_SHUTDOWN          = 0x64,  // link=both dir=cmd ack=RELAY
    DIFFUSER_TURN_ON           = 0x65,  // link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_START      = 0x66,  // link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_CANCEL     = 0x67,  // link=both dir=cmd ack=RELAY
    TELEM_DIFFUSER_STATUS      = 0x69,  // link=both dir=telem ack=NONE
    TELEM_DIFFUSER_HISTORY     = 0x6A,  // link=both dir=telem ack=NONE
    TELEM_DIFFUSER_USAGE       = 0x6B,  // link=both dir=telem ack=NONE
    TELEM_PARFUM_REMAINING     = 0x6C,  // link=both dir=telem ack=NONE
};
const char* OpcodeName(Opcode op);

uint8_t Crc8(const uint8_t* data, size_t len);  // Dallas/Maxim, poly 0x31 - see .cpp

// --- Fixed-layout opcode payloads: generated struct + Pack()/Unpack() pair ---
// Opcodes with a variable/conditional payload ("raw" fields in the table) are
// NOT generated here - hand-write their codec next to AppLink/DifLink, using the
// Opcode:: constant above and the matching table entry's "note" for the layout.

struct AckPayload {
    uint8_t result;
};
constexpr size_t ACK_SIZE = 1;
size_t Pack(const AckPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, AckPayload& out);

struct HelloPayload {
    uint8_t proto_version;
};
constexpr size_t HELLO_SIZE = 1;
size_t Pack(const HelloPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, HelloPayload& out);

struct LedSetColorPayload {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
constexpr size_t LED_SET_COLOR_SIZE = 1 + 1 + 1;
size_t Pack(const LedSetColorPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, LedSetColorPayload& out);

struct LedSetDualColorPayload {
    uint8_t shake;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;
    uint8_t r2;
    uint8_t g2;
    uint8_t b2;
};
constexpr size_t LED_SET_DUAL_COLOR_SIZE = 1 + 1 + 1 + 1 + 1 + 1 + 1;
size_t Pack(const LedSetDualColorPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, LedSetDualColorPayload& out);

struct LedSetSelectionPayload {
    uint8_t mask[8];
};
constexpr size_t LED_SET_SELECTION_SIZE = 8;
size_t Pack(const LedSetSelectionPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, LedSetSelectionPayload& out);

struct LedSetBrightnessPayload {
    uint8_t value;
};
constexpr size_t LED_SET_BRIGHTNESS_SIZE = 1;
size_t Pack(const LedSetBrightnessPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, LedSetBrightnessPayload& out);

struct TelemDualColorPayload {
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;
    uint8_t r2;
    uint8_t g2;
    uint8_t b2;
};
constexpr size_t TELEM_DUAL_COLOR_SIZE = 1 + 1 + 1 + 1 + 1 + 1;
size_t Pack(const TelemDualColorPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemDualColorPayload& out);

struct TelemMaxBrightnessPayload {
    uint8_t value;
};
constexpr size_t TELEM_MAX_BRIGHTNESS_SIZE = 1;
size_t Pack(const TelemMaxBrightnessPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemMaxBrightnessPayload& out);

struct TelemEnablePayload {
    uint8_t value;
};
constexpr size_t TELEM_ENABLE_SIZE = 1;
size_t Pack(const TelemEnablePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemEnablePayload& out);

struct SettingsReadOnePayload {
    uint8_t id;
};
constexpr size_t SETTINGS_READ_ONE_SIZE = 1;
size_t Pack(const SettingsReadOnePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SettingsReadOnePayload& out);

struct TelemSettingsFullPayload {
    uint8_t values[50];
};
constexpr size_t TELEM_SETTINGS_FULL_SIZE = 50;
size_t Pack(const TelemSettingsFullPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemSettingsFullPayload& out);

struct TelemSettingsOnePayload {
    uint8_t id;
    uint8_t value;
};
constexpr size_t TELEM_SETTINGS_ONE_SIZE = 1 + 1;
size_t Pack(const TelemSettingsOnePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemSettingsOnePayload& out);

struct TelemSaveResultPayload {
    uint8_t result;
};
constexpr size_t TELEM_SAVE_RESULT_SIZE = 1;
size_t Pack(const TelemSaveResultPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemSaveResultPayload& out);

struct TelemStatusPayload {
    uint8_t tv;
    uint8_t motion;
    uint8_t udpraw;
    uint8_t ambient;
    uint8_t diffuser_summary;
};
constexpr size_t TELEM_STATUS_SIZE = 1 + 1 + 1 + 1 + 1;
size_t Pack(const TelemStatusPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemStatusPayload& out);

struct TelemClimatePayload {
    int8_t temp_c;
    uint8_t humidity_pct;
};
constexpr size_t TELEM_CLIMATE_SIZE = 1 + 1;
size_t Pack(const TelemClimatePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemClimatePayload& out);

struct TelemLuxPayload {
    uint8_t level;
};
constexpr size_t TELEM_LUX_SIZE = 1;
size_t Pack(const TelemLuxPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemLuxPayload& out);

struct TelemLinkPayload {
    uint8_t rssi_bucket;
    uint8_t wifi_state;
};
constexpr size_t TELEM_LINK_SIZE = 1 + 1;
size_t Pack(const TelemLinkPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemLinkPayload& out);

struct TelemFaultsPayload {
    uint16_t mask;
};
constexpr size_t TELEM_FAULTS_SIZE = 2;
size_t Pack(const TelemFaultsPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemFaultsPayload& out);

struct TelemTestModePayload {
    uint8_t mode;
};
constexpr size_t TELEM_TEST_MODE_SIZE = 1;
size_t Pack(const TelemTestModePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemTestModePayload& out);

struct SetAmbientModePayload {
    uint8_t on;
};
constexpr size_t SET_AMBIENT_MODE_SIZE = 1;
size_t Pack(const SetAmbientModePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SetAmbientModePayload& out);

struct SetTestModePayload {
    uint8_t mode;
};
constexpr size_t SET_TEST_MODE_SIZE = 1;
size_t Pack(const SetTestModePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SetTestModePayload& out);

struct SetTestDiffuserPayload {
    uint8_t value;
};
constexpr size_t SET_TEST_DIFFUSER_SIZE = 1;
size_t Pack(const SetTestDiffuserPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SetTestDiffuserPayload& out);

struct SetTestLuxPayload {
    uint8_t level;
};
constexpr size_t SET_TEST_LUX_SIZE = 1;
size_t Pack(const SetTestLuxPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SetTestLuxPayload& out);

struct SetTelnetEnablePayload {
    uint8_t on;
};
constexpr size_t SET_TELNET_ENABLE_SIZE = 1;
size_t Pack(const SetTelnetEnablePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, SetTelnetEnablePayload& out);

struct DiffuserStatusQueryPayload {
    uint8_t verbose;
};
constexpr size_t DIFFUSER_STATUS_QUERY_SIZE = 1;
size_t Pack(const DiffuserStatusQueryPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, DiffuserStatusQueryPayload& out);

struct DiffuserHistoryRemovePayload {
    uint8_t index;
};
constexpr size_t DIFFUSER_HISTORY_REMOVE_SIZE = 1;
size_t Pack(const DiffuserHistoryRemovePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, DiffuserHistoryRemovePayload& out);

struct DiffuserParfumStartPayload {
    uint16_t minutes;
    uint8_t mode;
};
constexpr size_t DIFFUSER_PARFUM_START_SIZE = 2 + 1;
size_t Pack(const DiffuserParfumStartPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, DiffuserParfumStartPayload& out);

struct TelemDiffuserStatusPayload {
    uint8_t mode;
    uint8_t strip;
    uint16_t parfum_min;
    uint16_t usage_min;
    uint16_t avg_min;
    uint8_t refill_count;
    uint16_t lifetime_refills;
};
constexpr size_t TELEM_DIFFUSER_STATUS_SIZE = 1 + 1 + 2 + 2 + 2 + 1 + 2;
size_t Pack(const TelemDiffuserStatusPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemDiffuserStatusPayload& out);

struct TelemDiffuserUsagePayload {
    uint16_t accum_min;
    uint16_t avg_min;
    uint8_t refill_count;
    uint16_t lifetime_refills;
};
constexpr size_t TELEM_DIFFUSER_USAGE_SIZE = 2 + 2 + 1 + 2;
size_t Pack(const TelemDiffuserUsagePayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemDiffuserUsagePayload& out);

struct TelemParfumRemainingPayload {
    uint16_t minutes;
};
constexpr size_t TELEM_PARFUM_REMAINING_SIZE = 2;
size_t Pack(const TelemParfumRemainingPayload& p, uint8_t* out);
bool Unpack(const uint8_t* in, size_t len, TelemParfumRemainingPayload& out);

} // namespace Proto
