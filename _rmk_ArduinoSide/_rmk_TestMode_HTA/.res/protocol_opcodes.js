/*
 * GENERATED FILE - DO NOT EDIT.
 * Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json
 * Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py
 * See _rmk_docs/01_PROTOCOL.md for the full protocol specification.
 */
var Proto = (function () {
  'use strict';

  var FRAME_MAGIC = 0xF2;
  var FRAME_OVERHEAD_BYTES = 5;
  var FRAME_MAX_BYTES = 255;

  var Flag = {
    ACK_REQ: 1 << 0,
    IS_ACK: 1 << 1,
    IS_TELEMETRY: 1 << 2,
    FRAGMENTED: 1 << 3,
    LAST_FRAG: 1 << 4
  };

  var AckResult = {
    OK: 0,
    CLAMPED: 1,
    REJECTED: 2,
    BLOCKED: 3,
    LOCKED: 4,
    UNREACHABLE: 5,
    UNSUPPORTED: 6,
    UNAUTHORIZED: 7,
    VERSION_MISMATCH: 8
  };
  var ACK_RESULT_NAMES = {
    0: "OK",
    1: "CLAMPED",
    2: "REJECTED",
    3: "BLOCKED",
    4: "LOCKED",
    5: "UNREACHABLE",
    6: "UNSUPPORTED",
    7: "UNAUTHORIZED",
    8: "VERSION_MISMATCH"
  };

  var ACK_TIERS = {
    "FAST": { timeoutMs: 300, maxRetries: 2 },
    "RELAY": { timeoutMs: 3000, maxRetries: 1 },
    "SLOW": { timeoutMs: 9000, maxRetries: 0 },
    "NONE": { timeoutMs: 0, maxRetries: 0 }
  };

  var Opcode = {
    ACK: 0x00, // link=both dir=reply ack=NONE
    HELLO: 0x01, // link=both dir=cmd ack=FAST
    KEEPALIVE: 0x02, // link=both dir=cmd ack=NONE
    LOG: 0x03, // link=both dir=telem ack=NONE
    DIAG_HEALTH: 0x04, // link=both dir=cmd ack=FAST
    DIAG_PARFUM_TRACE: 0x05, // link=A dir=cmd ack=FAST
    LED_SET_COLOR: 0x10, // link=B dir=cmd ack=FAST
    LED_GET_COLOR: 0x11, // link=B dir=cmd ack=NONE
    LED_SET_DUAL_COLOR: 0x12, // link=B dir=cmd ack=FAST
    LED_GET_DUAL_COLOR: 0x13, // link=B dir=cmd ack=NONE
    LED_SET_SELECTION: 0x14, // link=B dir=cmd ack=FAST
    LED_SET_BRIGHTNESS: 0x15, // link=B dir=cmd ack=FAST
    LED_SET_ENABLE: 0x16, // link=B dir=cmd ack=FAST
    TELEM_COLOR_SYNC: 0x1A, // link=B dir=telem ack=NONE
    TELEM_DUAL_COLOR: 0x1B, // link=B dir=telem ack=NONE
    TELEM_MAX_BRIGHTNESS: 0x1C, // link=B dir=telem ack=NONE
    TELEM_ENABLE: 0x1D, // link=B dir=telem ack=NONE
    SETTINGS_READ_ALL: 0x20, // link=B dir=cmd ack=NONE
    SETTINGS_READ_ONE: 0x21, // link=B dir=cmd ack=NONE
    SETTINGS_WRITE: 0x22, // link=B dir=cmd ack=FAST
    TELEM_SETTINGS_FULL: 0x23, // link=B dir=telem ack=NONE
    TELEM_SETTINGS_ONE: 0x24, // link=B dir=telem ack=NONE
    TELEM_SAVE_RESULT: 0x25, // link=B dir=telem ack=NONE
    TELEM_STATUS: 0x30, // link=B dir=telem ack=NONE
    TELEM_CLIMATE: 0x31, // link=B dir=telem ack=NONE
    TELEM_LUX: 0x32, // link=B dir=telem ack=NONE
    TELEM_LINK: 0x33, // link=B dir=telem ack=NONE
    TELEM_FAULTS: 0x34, // link=B dir=telem ack=NONE
    TELEM_TEST_MODE: 0x35, // link=B dir=telem ack=NONE
    TELEM_DEVICE_ID: 0x36, // link=B dir=telem ack=NONE
    SET_AMBIENT_MODE: 0x40, // link=B dir=cmd ack=FAST
    SET_TEST_MODE: 0x41, // link=B dir=cmd ack=FAST
    SET_TEST_DIFFUSER: 0x42, // link=B dir=cmd ack=RELAY
    SET_TEST_LUX: 0x43, // link=B dir=cmd ack=FAST
    SET_TELNET_ENABLE: 0x44, // link=B dir=cmd ack=FAST
    SET_MQTT_CREDENTIALS: 0x50, // link=B dir=cmd ack=SLOW
    DIFFUSER_STATUS_QUERY: 0x60, // link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_QUERY: 0x61, // link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_REMOVE: 0x62, // link=both dir=cmd ack=RELAY
    DIFFUSER_MANUAL_REFILL: 0x63, // link=both dir=cmd ack=RELAY
    DIFFUSER_SHUTDOWN: 0x64, // link=both dir=cmd ack=RELAY
    DIFFUSER_TURN_ON: 0x65, // link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_START: 0x66, // link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_CANCEL: 0x67, // link=both dir=cmd ack=RELAY
    TELEM_DIFFUSER_STATUS: 0x69, // link=both dir=telem ack=NONE
    TELEM_DIFFUSER_HISTORY: 0x6A, // link=both dir=telem ack=NONE
    TELEM_DIFFUSER_USAGE: 0x6B, // link=both dir=telem ack=NONE
    TELEM_PARFUM_REMAINING: 0x6C // link=both dir=telem ack=NONE
  };
  var OPCODE_NAMES = {
    0x00: "ACK",
    0x01: "HELLO",
    0x02: "KEEPALIVE",
    0x03: "LOG",
    0x04: "DIAG_HEALTH",
    0x05: "DIAG_PARFUM_TRACE",
    0x10: "LED_SET_COLOR",
    0x11: "LED_GET_COLOR",
    0x12: "LED_SET_DUAL_COLOR",
    0x13: "LED_GET_DUAL_COLOR",
    0x14: "LED_SET_SELECTION",
    0x15: "LED_SET_BRIGHTNESS",
    0x16: "LED_SET_ENABLE",
    0x1A: "TELEM_COLOR_SYNC",
    0x1B: "TELEM_DUAL_COLOR",
    0x1C: "TELEM_MAX_BRIGHTNESS",
    0x1D: "TELEM_ENABLE",
    0x20: "SETTINGS_READ_ALL",
    0x21: "SETTINGS_READ_ONE",
    0x22: "SETTINGS_WRITE",
    0x23: "TELEM_SETTINGS_FULL",
    0x24: "TELEM_SETTINGS_ONE",
    0x25: "TELEM_SAVE_RESULT",
    0x30: "TELEM_STATUS",
    0x31: "TELEM_CLIMATE",
    0x32: "TELEM_LUX",
    0x33: "TELEM_LINK",
    0x34: "TELEM_FAULTS",
    0x35: "TELEM_TEST_MODE",
    0x36: "TELEM_DEVICE_ID",
    0x40: "SET_AMBIENT_MODE",
    0x41: "SET_TEST_MODE",
    0x42: "SET_TEST_DIFFUSER",
    0x43: "SET_TEST_LUX",
    0x44: "SET_TELNET_ENABLE",
    0x50: "SET_MQTT_CREDENTIALS",
    0x60: "DIFFUSER_STATUS_QUERY",
    0x61: "DIFFUSER_HISTORY_QUERY",
    0x62: "DIFFUSER_HISTORY_REMOVE",
    0x63: "DIFFUSER_MANUAL_REFILL",
    0x64: "DIFFUSER_SHUTDOWN",
    0x65: "DIFFUSER_TURN_ON",
    0x66: "DIFFUSER_PARFUM_START",
    0x67: "DIFFUSER_PARFUM_CANCEL",
    0x69: "TELEM_DIFFUSER_STATUS",
    0x6A: "TELEM_DIFFUSER_HISTORY",
    0x6B: "TELEM_DIFFUSER_USAGE",
    0x6C: "TELEM_PARFUM_REMAINING"
  };

  // Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() bit for bit.
  // `bytes` is a JS string where charCodeAt(i) is wire byte i (0-255).
  function crc8(bytes) {
    var crc = 0;
    for (var i = 0; i < bytes.length; i++) {
      crc ^= bytes.charCodeAt(i) & 0xFF;
      for (var b = 0; b < 8; b++)
        crc = (crc & 0x01) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);
    }
    return crc & 0xFF;
  }

  // --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---
  // packX(...) returns a byte-string (String.fromCharCode per byte); unpackX(data,
  // offset) reads from a byte-string starting at offset (default 0) and returns a
  // plain object keyed by field name. u16/bytes fields are big-endian, matching
  // every other generated language.
  function packAck(result) {
    var s = '';
    s += String.fromCharCode(result & 0xFF);
    return s;
  }
  function unpackAck(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.result = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packHello(proto_version) {
    var s = '';
    s += String.fromCharCode(proto_version & 0xFF);
    return s;
  }
  function unpackHello(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.proto_version = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packLedSetColor(r, g, b) {
    var s = '';
    s += String.fromCharCode(r & 0xFF);
    s += String.fromCharCode(g & 0xFF);
    s += String.fromCharCode(b & 0xFF);
    return s;
  }
  function unpackLedSetColor(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.r = data.charCodeAt(i) & 0xFF; i += 1;
    out.g = data.charCodeAt(i) & 0xFF; i += 1;
    out.b = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packLedSetDualColor(shake, r1, g1, b1, r2, g2, b2) {
    var s = '';
    s += String.fromCharCode(shake & 0xFF);
    s += String.fromCharCode(r1 & 0xFF);
    s += String.fromCharCode(g1 & 0xFF);
    s += String.fromCharCode(b1 & 0xFF);
    s += String.fromCharCode(r2 & 0xFF);
    s += String.fromCharCode(g2 & 0xFF);
    s += String.fromCharCode(b2 & 0xFF);
    return s;
  }
  function unpackLedSetDualColor(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.shake = data.charCodeAt(i) & 0xFF; i += 1;
    out.r1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.g1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.b1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.r2 = data.charCodeAt(i) & 0xFF; i += 1;
    out.g2 = data.charCodeAt(i) & 0xFF; i += 1;
    out.b2 = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packLedSetSelection(mask) {
    var s = '';
    for (var imask = 0; imask < 8; imask++) s += String.fromCharCode(mask.charCodeAt(imask) & 0xFF);
    return s;
  }
  function unpackLedSetSelection(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.mask = data.substr(i, 8); i += 8;
    return out;
  }

  function packLedSetBrightness(value) {
    var s = '';
    s += String.fromCharCode(value & 0xFF);
    return s;
  }
  function unpackLedSetBrightness(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.value = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemDualColor(r1, g1, b1, r2, g2, b2) {
    var s = '';
    s += String.fromCharCode(r1 & 0xFF);
    s += String.fromCharCode(g1 & 0xFF);
    s += String.fromCharCode(b1 & 0xFF);
    s += String.fromCharCode(r2 & 0xFF);
    s += String.fromCharCode(g2 & 0xFF);
    s += String.fromCharCode(b2 & 0xFF);
    return s;
  }
  function unpackTelemDualColor(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.r1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.g1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.b1 = data.charCodeAt(i) & 0xFF; i += 1;
    out.r2 = data.charCodeAt(i) & 0xFF; i += 1;
    out.g2 = data.charCodeAt(i) & 0xFF; i += 1;
    out.b2 = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemMaxBrightness(value) {
    var s = '';
    s += String.fromCharCode(value & 0xFF);
    return s;
  }
  function unpackTelemMaxBrightness(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.value = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemEnable(value) {
    var s = '';
    s += String.fromCharCode(value & 0xFF);
    return s;
  }
  function unpackTelemEnable(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.value = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packSettingsReadOne(id) {
    var s = '';
    s += String.fromCharCode(id & 0xFF);
    return s;
  }
  function unpackSettingsReadOne(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.id = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemSettingsFull(values) {
    var s = '';
    for (var ivalues = 0; ivalues < 50; ivalues++) s += String.fromCharCode(values.charCodeAt(ivalues) & 0xFF);
    return s;
  }
  function unpackTelemSettingsFull(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.values = data.substr(i, 50); i += 50;
    return out;
  }

  function packTelemSettingsOne(id, value) {
    var s = '';
    s += String.fromCharCode(id & 0xFF);
    s += String.fromCharCode(value & 0xFF);
    return s;
  }
  function unpackTelemSettingsOne(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.id = data.charCodeAt(i) & 0xFF; i += 1;
    out.value = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemSaveResult(result) {
    var s = '';
    s += String.fromCharCode(result & 0xFF);
    return s;
  }
  function unpackTelemSaveResult(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.result = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemStatus(tv, motion, udpraw, ambient, diffuser_summary) {
    var s = '';
    s += String.fromCharCode(tv & 0xFF);
    s += String.fromCharCode(motion & 0xFF);
    s += String.fromCharCode(udpraw & 0xFF);
    s += String.fromCharCode(ambient & 0xFF);
    s += String.fromCharCode(diffuser_summary & 0xFF);
    return s;
  }
  function unpackTelemStatus(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.tv = data.charCodeAt(i) & 0xFF; i += 1;
    out.motion = data.charCodeAt(i) & 0xFF; i += 1;
    out.udpraw = data.charCodeAt(i) & 0xFF; i += 1;
    out.ambient = data.charCodeAt(i) & 0xFF; i += 1;
    out.diffuser_summary = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemClimate(temp_c, humidity_pct) {
    var s = '';
    s += String.fromCharCode(temp_c & 0xFF);
    s += String.fromCharCode(humidity_pct & 0xFF);
    return s;
  }
  function unpackTelemClimate(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.temp_c = data.charCodeAt(i) & 0xFF; if (out.temp_c >= 128) out.temp_c -= 256; i += 1;
    out.humidity_pct = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemLux(level) {
    var s = '';
    s += String.fromCharCode(level & 0xFF);
    return s;
  }
  function unpackTelemLux(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.level = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemLink(rssi_bucket, wifi_state) {
    var s = '';
    s += String.fromCharCode(rssi_bucket & 0xFF);
    s += String.fromCharCode(wifi_state & 0xFF);
    return s;
  }
  function unpackTelemLink(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.rssi_bucket = data.charCodeAt(i) & 0xFF; i += 1;
    out.wifi_state = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemFaults(mask) {
    var s = '';
    s += String.fromCharCode((mask >> 8) & 0xFF); s += String.fromCharCode(mask & 0xFF);
    return s;
  }
  function unpackTelemFaults(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.mask = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    return out;
  }

  function packTelemTestMode(mode) {
    var s = '';
    s += String.fromCharCode(mode & 0xFF);
    return s;
  }
  function unpackTelemTestMode(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.mode = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemDeviceId(device_id) {
    var s = '';
    for (var idevice_id = 0; idevice_id < 12; idevice_id++) s += String.fromCharCode(device_id.charCodeAt(idevice_id) & 0xFF);
    return s;
  }
  function unpackTelemDeviceId(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.device_id = data.substr(i, 12); i += 12;
    return out;
  }

  function packSetAmbientMode(on) {
    var s = '';
    s += String.fromCharCode(on & 0xFF);
    return s;
  }
  function unpackSetAmbientMode(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.on = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packSetTestMode(mode) {
    var s = '';
    s += String.fromCharCode(mode & 0xFF);
    return s;
  }
  function unpackSetTestMode(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.mode = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packSetTestDiffuser(value) {
    var s = '';
    s += String.fromCharCode(value & 0xFF);
    return s;
  }
  function unpackSetTestDiffuser(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.value = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packSetTestLux(level) {
    var s = '';
    s += String.fromCharCode(level & 0xFF);
    return s;
  }
  function unpackSetTestLux(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.level = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packSetTelnetEnable(on) {
    var s = '';
    s += String.fromCharCode(on & 0xFF);
    return s;
  }
  function unpackSetTelnetEnable(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.on = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packDiffuserStatusQuery(verbose) {
    var s = '';
    s += String.fromCharCode(verbose & 0xFF);
    return s;
  }
  function unpackDiffuserStatusQuery(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.verbose = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packDiffuserHistoryRemove(index) {
    var s = '';
    s += String.fromCharCode(index & 0xFF);
    return s;
  }
  function unpackDiffuserHistoryRemove(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.index = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packDiffuserParfumStart(minutes, mode) {
    var s = '';
    s += String.fromCharCode((minutes >> 8) & 0xFF); s += String.fromCharCode(minutes & 0xFF);
    s += String.fromCharCode(mode & 0xFF);
    return s;
  }
  function unpackDiffuserParfumStart(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.minutes = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.mode = data.charCodeAt(i) & 0xFF; i += 1;
    return out;
  }

  function packTelemDiffuserStatus(mode, strip, parfum_min, usage_min, avg_min, refill_count, lifetime_refills) {
    var s = '';
    s += String.fromCharCode(mode & 0xFF);
    s += String.fromCharCode(strip & 0xFF);
    s += String.fromCharCode((parfum_min >> 8) & 0xFF); s += String.fromCharCode(parfum_min & 0xFF);
    s += String.fromCharCode((usage_min >> 8) & 0xFF); s += String.fromCharCode(usage_min & 0xFF);
    s += String.fromCharCode((avg_min >> 8) & 0xFF); s += String.fromCharCode(avg_min & 0xFF);
    s += String.fromCharCode(refill_count & 0xFF);
    s += String.fromCharCode((lifetime_refills >> 8) & 0xFF); s += String.fromCharCode(lifetime_refills & 0xFF);
    return s;
  }
  function unpackTelemDiffuserStatus(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.mode = data.charCodeAt(i) & 0xFF; i += 1;
    out.strip = data.charCodeAt(i) & 0xFF; i += 1;
    out.parfum_min = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.usage_min = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.avg_min = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.refill_count = data.charCodeAt(i) & 0xFF; i += 1;
    out.lifetime_refills = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    return out;
  }

  function packTelemDiffuserUsage(accum_min, avg_min, refill_count, lifetime_refills) {
    var s = '';
    s += String.fromCharCode((accum_min >> 8) & 0xFF); s += String.fromCharCode(accum_min & 0xFF);
    s += String.fromCharCode((avg_min >> 8) & 0xFF); s += String.fromCharCode(avg_min & 0xFF);
    s += String.fromCharCode(refill_count & 0xFF);
    s += String.fromCharCode((lifetime_refills >> 8) & 0xFF); s += String.fromCharCode(lifetime_refills & 0xFF);
    return s;
  }
  function unpackTelemDiffuserUsage(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.accum_min = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.avg_min = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    out.refill_count = data.charCodeAt(i) & 0xFF; i += 1;
    out.lifetime_refills = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    return out;
  }

  function packTelemParfumRemaining(minutes) {
    var s = '';
    s += String.fromCharCode((minutes >> 8) & 0xFF); s += String.fromCharCode(minutes & 0xFF);
    return s;
  }
  function unpackTelemParfumRemaining(data, offset) {
    offset = offset || 0;
    var out = {}, i = offset;
    out.minutes = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;
    return out;
  }

  return {
    FRAME_MAGIC: FRAME_MAGIC, FRAME_OVERHEAD_BYTES: FRAME_OVERHEAD_BYTES, FRAME_MAX_BYTES: FRAME_MAX_BYTES,
    Flag: Flag, AckResult: AckResult, ACK_RESULT_NAMES: ACK_RESULT_NAMES, ACK_TIERS: ACK_TIERS,
    Opcode: Opcode, OPCODE_NAMES: OPCODE_NAMES, crc8: crc8,
    packAck: packAck, unpackAck: unpackAck,
    packHello: packHello, unpackHello: unpackHello,
    packLedSetColor: packLedSetColor, unpackLedSetColor: unpackLedSetColor,
    packLedSetDualColor: packLedSetDualColor, unpackLedSetDualColor: unpackLedSetDualColor,
    packLedSetSelection: packLedSetSelection, unpackLedSetSelection: unpackLedSetSelection,
    packLedSetBrightness: packLedSetBrightness, unpackLedSetBrightness: unpackLedSetBrightness,
    packTelemDualColor: packTelemDualColor, unpackTelemDualColor: unpackTelemDualColor,
    packTelemMaxBrightness: packTelemMaxBrightness, unpackTelemMaxBrightness: unpackTelemMaxBrightness,
    packTelemEnable: packTelemEnable, unpackTelemEnable: unpackTelemEnable,
    packSettingsReadOne: packSettingsReadOne, unpackSettingsReadOne: unpackSettingsReadOne,
    packTelemSettingsFull: packTelemSettingsFull, unpackTelemSettingsFull: unpackTelemSettingsFull,
    packTelemSettingsOne: packTelemSettingsOne, unpackTelemSettingsOne: unpackTelemSettingsOne,
    packTelemSaveResult: packTelemSaveResult, unpackTelemSaveResult: unpackTelemSaveResult,
    packTelemStatus: packTelemStatus, unpackTelemStatus: unpackTelemStatus,
    packTelemClimate: packTelemClimate, unpackTelemClimate: unpackTelemClimate,
    packTelemLux: packTelemLux, unpackTelemLux: unpackTelemLux,
    packTelemLink: packTelemLink, unpackTelemLink: unpackTelemLink,
    packTelemFaults: packTelemFaults, unpackTelemFaults: unpackTelemFaults,
    packTelemTestMode: packTelemTestMode, unpackTelemTestMode: unpackTelemTestMode,
    packTelemDeviceId: packTelemDeviceId, unpackTelemDeviceId: unpackTelemDeviceId,
    packSetAmbientMode: packSetAmbientMode, unpackSetAmbientMode: unpackSetAmbientMode,
    packSetTestMode: packSetTestMode, unpackSetTestMode: unpackSetTestMode,
    packSetTestDiffuser: packSetTestDiffuser, unpackSetTestDiffuser: unpackSetTestDiffuser,
    packSetTestLux: packSetTestLux, unpackSetTestLux: unpackSetTestLux,
    packSetTelnetEnable: packSetTelnetEnable, unpackSetTelnetEnable: unpackSetTelnetEnable,
    packDiffuserStatusQuery: packDiffuserStatusQuery, unpackDiffuserStatusQuery: unpackDiffuserStatusQuery,
    packDiffuserHistoryRemove: packDiffuserHistoryRemove, unpackDiffuserHistoryRemove: unpackDiffuserHistoryRemove,
    packDiffuserParfumStart: packDiffuserParfumStart, unpackDiffuserParfumStart: unpackDiffuserParfumStart,
    packTelemDiffuserStatus: packTelemDiffuserStatus, unpackTelemDiffuserStatus: unpackTelemDiffuserStatus,
    packTelemDiffuserUsage: packTelemDiffuserUsage, unpackTelemDiffuserUsage: unpackTelemDiffuserUsage,
    packTelemParfumRemaining: packTelemParfumRemaining, unpackTelemParfumRemaining: unpackTelemParfumRemaining
  };
})();
