#include "protocol_opcodes.h"
/*
 * GENERATED FILE - DO NOT EDIT.
 * Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json
 * Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py
 * See _rmk_docs/01_PROTOCOL.md for the full protocol specification.
 */

namespace Proto {

uint8_t Crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x01) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);
    }
    return crc;
}

const AckTierSpec& AckTierOf(AckTier t) {
    static const AckTierSpec specs[4] = {
        {300, 2},  // FAST
        {3000, 1},  // RELAY
        {9000, 0},  // SLOW
        {0, 0},  // NONE
    };
    return specs[(uint8_t)t];
}

const char* AckResultName(AckResult r) {
    switch (r) {
        case AckResult::OK: return "OK";
        case AckResult::CLAMPED: return "CLAMPED";
        case AckResult::REJECTED: return "REJECTED";
        case AckResult::BLOCKED: return "BLOCKED";
        case AckResult::LOCKED: return "LOCKED";
        case AckResult::UNREACHABLE: return "UNREACHABLE";
        case AckResult::UNSUPPORTED: return "UNSUPPORTED";
        case AckResult::UNAUTHORIZED: return "UNAUTHORIZED";
        case AckResult::VERSION_MISMATCH: return "VERSION_MISMATCH";
        default: return "UNKNOWN";
    }
}

const char* OpcodeName(Opcode op) {
    switch (op) {
        case Opcode::ACK: return "ACK";
        case Opcode::HELLO: return "HELLO";
        case Opcode::KEEPALIVE: return "KEEPALIVE";
        case Opcode::LOG: return "LOG";
        case Opcode::DIAG_HEALTH: return "DIAG_HEALTH";
        case Opcode::DIAG_PARFUM_TRACE: return "DIAG_PARFUM_TRACE";
        case Opcode::LED_SET_COLOR: return "LED_SET_COLOR";
        case Opcode::LED_GET_COLOR: return "LED_GET_COLOR";
        case Opcode::LED_SET_DUAL_COLOR: return "LED_SET_DUAL_COLOR";
        case Opcode::LED_GET_DUAL_COLOR: return "LED_GET_DUAL_COLOR";
        case Opcode::LED_SET_SELECTION: return "LED_SET_SELECTION";
        case Opcode::LED_SET_BRIGHTNESS: return "LED_SET_BRIGHTNESS";
        case Opcode::LED_SET_ENABLE: return "LED_SET_ENABLE";
        case Opcode::TELEM_COLOR_SYNC: return "TELEM_COLOR_SYNC";
        case Opcode::TELEM_DUAL_COLOR: return "TELEM_DUAL_COLOR";
        case Opcode::TELEM_MAX_BRIGHTNESS: return "TELEM_MAX_BRIGHTNESS";
        case Opcode::TELEM_ENABLE: return "TELEM_ENABLE";
        case Opcode::SETTINGS_READ_ALL: return "SETTINGS_READ_ALL";
        case Opcode::SETTINGS_READ_ONE: return "SETTINGS_READ_ONE";
        case Opcode::SETTINGS_WRITE: return "SETTINGS_WRITE";
        case Opcode::TELEM_SETTINGS_FULL: return "TELEM_SETTINGS_FULL";
        case Opcode::TELEM_SETTINGS_ONE: return "TELEM_SETTINGS_ONE";
        case Opcode::TELEM_SAVE_RESULT: return "TELEM_SAVE_RESULT";
        case Opcode::TELEM_STATUS: return "TELEM_STATUS";
        case Opcode::TELEM_CLIMATE: return "TELEM_CLIMATE";
        case Opcode::TELEM_LUX: return "TELEM_LUX";
        case Opcode::TELEM_LINK: return "TELEM_LINK";
        case Opcode::TELEM_FAULTS: return "TELEM_FAULTS";
        case Opcode::TELEM_TEST_MODE: return "TELEM_TEST_MODE";
        case Opcode::TELEM_DEVICE_ID: return "TELEM_DEVICE_ID";
        case Opcode::SET_AMBIENT_MODE: return "SET_AMBIENT_MODE";
        case Opcode::SET_TEST_MODE: return "SET_TEST_MODE";
        case Opcode::SET_TEST_DIFFUSER: return "SET_TEST_DIFFUSER";
        case Opcode::SET_TEST_LUX: return "SET_TEST_LUX";
        case Opcode::SET_TELNET_ENABLE: return "SET_TELNET_ENABLE";
        case Opcode::SET_TELNET_VERBOSITY: return "SET_TELNET_VERBOSITY";
        case Opcode::SET_MQTT_CREDENTIALS: return "SET_MQTT_CREDENTIALS";
        case Opcode::DIFFUSER_STATUS_QUERY: return "DIFFUSER_STATUS_QUERY";
        case Opcode::DIFFUSER_HISTORY_QUERY: return "DIFFUSER_HISTORY_QUERY";
        case Opcode::DIFFUSER_HISTORY_REMOVE: return "DIFFUSER_HISTORY_REMOVE";
        case Opcode::DIFFUSER_MANUAL_REFILL: return "DIFFUSER_MANUAL_REFILL";
        case Opcode::DIFFUSER_SHUTDOWN: return "DIFFUSER_SHUTDOWN";
        case Opcode::DIFFUSER_TURN_ON: return "DIFFUSER_TURN_ON";
        case Opcode::DIFFUSER_PARFUM_START: return "DIFFUSER_PARFUM_START";
        case Opcode::DIFFUSER_PARFUM_CANCEL: return "DIFFUSER_PARFUM_CANCEL";
        case Opcode::TELEM_DIFFUSER_STATUS: return "TELEM_DIFFUSER_STATUS";
        case Opcode::TELEM_DIFFUSER_HISTORY: return "TELEM_DIFFUSER_HISTORY";
        case Opcode::TELEM_DIFFUSER_USAGE: return "TELEM_DIFFUSER_USAGE";
        case Opcode::TELEM_PARFUM_REMAINING: return "TELEM_PARFUM_REMAINING";
        default: return "UNKNOWN";
    }
}

size_t Pack(const AckPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.result;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, AckPayload& out) {
    if (len < ACK_SIZE) return false;
    size_t i = 0;
    out.result = in[i++];
    return true;
}

size_t Pack(const HelloPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.proto_version;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, HelloPayload& out) {
    if (len < HELLO_SIZE) return false;
    size_t i = 0;
    out.proto_version = in[i++];
    return true;
}

size_t Pack(const LedSetColorPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.r;
    out[i++] = (uint8_t)p.g;
    out[i++] = (uint8_t)p.b;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, LedSetColorPayload& out) {
    if (len < LED_SET_COLOR_SIZE) return false;
    size_t i = 0;
    out.r = in[i++];
    out.g = in[i++];
    out.b = in[i++];
    return true;
}

size_t Pack(const LedSetDualColorPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.shake;
    out[i++] = (uint8_t)p.r1;
    out[i++] = (uint8_t)p.g1;
    out[i++] = (uint8_t)p.b1;
    out[i++] = (uint8_t)p.r2;
    out[i++] = (uint8_t)p.g2;
    out[i++] = (uint8_t)p.b2;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, LedSetDualColorPayload& out) {
    if (len < LED_SET_DUAL_COLOR_SIZE) return false;
    size_t i = 0;
    out.shake = in[i++];
    out.r1 = in[i++];
    out.g1 = in[i++];
    out.b1 = in[i++];
    out.r2 = in[i++];
    out.g2 = in[i++];
    out.b2 = in[i++];
    return true;
}

size_t Pack(const LedSetSelectionPayload& p, uint8_t* out) {
    size_t i = 0;
    for (size_t k = 0; k < 8; k++) out[i++] = p.mask[k];
    return i;
}
bool Unpack(const uint8_t* in, size_t len, LedSetSelectionPayload& out) {
    if (len < LED_SET_SELECTION_SIZE) return false;
    size_t i = 0;
    for (size_t k = 0; k < 8; k++) out.mask[k] = in[i++];
    return true;
}

size_t Pack(const LedSetBrightnessPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.value;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, LedSetBrightnessPayload& out) {
    if (len < LED_SET_BRIGHTNESS_SIZE) return false;
    size_t i = 0;
    out.value = in[i++];
    return true;
}

size_t Pack(const TelemDualColorPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.r1;
    out[i++] = (uint8_t)p.g1;
    out[i++] = (uint8_t)p.b1;
    out[i++] = (uint8_t)p.r2;
    out[i++] = (uint8_t)p.g2;
    out[i++] = (uint8_t)p.b2;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemDualColorPayload& out) {
    if (len < TELEM_DUAL_COLOR_SIZE) return false;
    size_t i = 0;
    out.r1 = in[i++];
    out.g1 = in[i++];
    out.b1 = in[i++];
    out.r2 = in[i++];
    out.g2 = in[i++];
    out.b2 = in[i++];
    return true;
}

size_t Pack(const TelemMaxBrightnessPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.value;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemMaxBrightnessPayload& out) {
    if (len < TELEM_MAX_BRIGHTNESS_SIZE) return false;
    size_t i = 0;
    out.value = in[i++];
    return true;
}

size_t Pack(const TelemEnablePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.value;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemEnablePayload& out) {
    if (len < TELEM_ENABLE_SIZE) return false;
    size_t i = 0;
    out.value = in[i++];
    return true;
}

size_t Pack(const SettingsReadOnePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.id;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SettingsReadOnePayload& out) {
    if (len < SETTINGS_READ_ONE_SIZE) return false;
    size_t i = 0;
    out.id = in[i++];
    return true;
}

size_t Pack(const TelemSettingsFullPayload& p, uint8_t* out) {
    size_t i = 0;
    for (size_t k = 0; k < 50; k++) out[i++] = p.values[k];
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemSettingsFullPayload& out) {
    if (len < TELEM_SETTINGS_FULL_SIZE) return false;
    size_t i = 0;
    for (size_t k = 0; k < 50; k++) out.values[k] = in[i++];
    return true;
}

size_t Pack(const TelemSettingsOnePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.id;
    out[i++] = (uint8_t)p.value;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemSettingsOnePayload& out) {
    if (len < TELEM_SETTINGS_ONE_SIZE) return false;
    size_t i = 0;
    out.id = in[i++];
    out.value = in[i++];
    return true;
}

size_t Pack(const TelemSaveResultPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.result;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemSaveResultPayload& out) {
    if (len < TELEM_SAVE_RESULT_SIZE) return false;
    size_t i = 0;
    out.result = in[i++];
    return true;
}

size_t Pack(const TelemStatusPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.tv;
    out[i++] = (uint8_t)p.motion;
    out[i++] = (uint8_t)p.udpraw;
    out[i++] = (uint8_t)p.ambient;
    out[i++] = (uint8_t)p.diffuser_summary;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemStatusPayload& out) {
    if (len < TELEM_STATUS_SIZE) return false;
    size_t i = 0;
    out.tv = in[i++];
    out.motion = in[i++];
    out.udpraw = in[i++];
    out.ambient = in[i++];
    out.diffuser_summary = in[i++];
    return true;
}

size_t Pack(const TelemClimatePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.temp_c;
    out[i++] = (uint8_t)p.humidity_pct;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemClimatePayload& out) {
    if (len < TELEM_CLIMATE_SIZE) return false;
    size_t i = 0;
    out.temp_c = (int8_t)in[i++];
    out.humidity_pct = in[i++];
    return true;
}

size_t Pack(const TelemLuxPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.level;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemLuxPayload& out) {
    if (len < TELEM_LUX_SIZE) return false;
    size_t i = 0;
    out.level = in[i++];
    return true;
}

size_t Pack(const TelemLinkPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.rssi_bucket;
    out[i++] = (uint8_t)p.wifi_state;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemLinkPayload& out) {
    if (len < TELEM_LINK_SIZE) return false;
    size_t i = 0;
    out.rssi_bucket = in[i++];
    out.wifi_state = in[i++];
    return true;
}

size_t Pack(const TelemFaultsPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)(p.mask >> 8); out[i++] = (uint8_t)(p.mask & 0xFF);
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemFaultsPayload& out) {
    if (len < TELEM_FAULTS_SIZE) return false;
    size_t i = 0;
    out.mask = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    return true;
}

size_t Pack(const TelemTestModePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.mode;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemTestModePayload& out) {
    if (len < TELEM_TEST_MODE_SIZE) return false;
    size_t i = 0;
    out.mode = in[i++];
    return true;
}

size_t Pack(const TelemDeviceIdPayload& p, uint8_t* out) {
    size_t i = 0;
    for (size_t k = 0; k < 12; k++) out[i++] = p.device_id[k];
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemDeviceIdPayload& out) {
    if (len < TELEM_DEVICE_ID_SIZE) return false;
    size_t i = 0;
    for (size_t k = 0; k < 12; k++) out.device_id[k] = in[i++];
    return true;
}

size_t Pack(const SetAmbientModePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.on;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetAmbientModePayload& out) {
    if (len < SET_AMBIENT_MODE_SIZE) return false;
    size_t i = 0;
    out.on = in[i++];
    return true;
}

size_t Pack(const SetTestModePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.mode;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetTestModePayload& out) {
    if (len < SET_TEST_MODE_SIZE) return false;
    size_t i = 0;
    out.mode = in[i++];
    return true;
}

size_t Pack(const SetTestDiffuserPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.value;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetTestDiffuserPayload& out) {
    if (len < SET_TEST_DIFFUSER_SIZE) return false;
    size_t i = 0;
    out.value = in[i++];
    return true;
}

size_t Pack(const SetTestLuxPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.level;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetTestLuxPayload& out) {
    if (len < SET_TEST_LUX_SIZE) return false;
    size_t i = 0;
    out.level = in[i++];
    return true;
}

size_t Pack(const SetTelnetEnablePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.on;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetTelnetEnablePayload& out) {
    if (len < SET_TELNET_ENABLE_SIZE) return false;
    size_t i = 0;
    out.on = in[i++];
    return true;
}

size_t Pack(const SetTelnetVerbosityPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.level;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, SetTelnetVerbosityPayload& out) {
    if (len < SET_TELNET_VERBOSITY_SIZE) return false;
    size_t i = 0;
    out.level = in[i++];
    return true;
}

size_t Pack(const DiffuserStatusQueryPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.verbose;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, DiffuserStatusQueryPayload& out) {
    if (len < DIFFUSER_STATUS_QUERY_SIZE) return false;
    size_t i = 0;
    out.verbose = in[i++];
    return true;
}

size_t Pack(const DiffuserHistoryRemovePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.index;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, DiffuserHistoryRemovePayload& out) {
    if (len < DIFFUSER_HISTORY_REMOVE_SIZE) return false;
    size_t i = 0;
    out.index = in[i++];
    return true;
}

size_t Pack(const DiffuserParfumStartPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)(p.minutes >> 8); out[i++] = (uint8_t)(p.minutes & 0xFF);
    out[i++] = (uint8_t)p.mode;
    return i;
}
bool Unpack(const uint8_t* in, size_t len, DiffuserParfumStartPayload& out) {
    if (len < DIFFUSER_PARFUM_START_SIZE) return false;
    size_t i = 0;
    out.minutes = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.mode = in[i++];
    return true;
}

size_t Pack(const TelemDiffuserStatusPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)p.mode;
    out[i++] = (uint8_t)p.strip;
    out[i++] = (uint8_t)(p.parfum_min >> 8); out[i++] = (uint8_t)(p.parfum_min & 0xFF);
    out[i++] = (uint8_t)(p.usage_min >> 8); out[i++] = (uint8_t)(p.usage_min & 0xFF);
    out[i++] = (uint8_t)(p.avg_min >> 8); out[i++] = (uint8_t)(p.avg_min & 0xFF);
    out[i++] = (uint8_t)p.refill_count;
    out[i++] = (uint8_t)(p.lifetime_refills >> 8); out[i++] = (uint8_t)(p.lifetime_refills & 0xFF);
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemDiffuserStatusPayload& out) {
    if (len < TELEM_DIFFUSER_STATUS_SIZE) return false;
    size_t i = 0;
    out.mode = in[i++];
    out.strip = in[i++];
    out.parfum_min = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.usage_min = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.avg_min = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.refill_count = in[i++];
    out.lifetime_refills = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    return true;
}

size_t Pack(const TelemDiffuserUsagePayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)(p.accum_min >> 8); out[i++] = (uint8_t)(p.accum_min & 0xFF);
    out[i++] = (uint8_t)(p.avg_min >> 8); out[i++] = (uint8_t)(p.avg_min & 0xFF);
    out[i++] = (uint8_t)p.refill_count;
    out[i++] = (uint8_t)(p.lifetime_refills >> 8); out[i++] = (uint8_t)(p.lifetime_refills & 0xFF);
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemDiffuserUsagePayload& out) {
    if (len < TELEM_DIFFUSER_USAGE_SIZE) return false;
    size_t i = 0;
    out.accum_min = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.avg_min = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    out.refill_count = in[i++];
    out.lifetime_refills = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    return true;
}

size_t Pack(const TelemParfumRemainingPayload& p, uint8_t* out) {
    size_t i = 0;
    out[i++] = (uint8_t)(p.minutes >> 8); out[i++] = (uint8_t)(p.minutes & 0xFF);
    return i;
}
bool Unpack(const uint8_t* in, size_t len, TelemParfumRemainingPayload& out) {
    if (len < TELEM_PARFUM_REMAINING_SIZE) return false;
    size_t i = 0;
    out.minutes = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;
    return true;
}

} // namespace Proto
