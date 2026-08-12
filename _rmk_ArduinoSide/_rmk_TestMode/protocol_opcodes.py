"""
GENERATED FILE - DO NOT EDIT.
Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json
Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py
See _rmk_docs/01_PROTOCOL.md for the full protocol specification.
"""
import struct

FRAME_MAGIC = 0xF2
FRAME_OVERHEAD_BYTES = 5
FRAME_MAX_BYTES = 255

class Flag:
    ACK_REQ = 1 << 0
    IS_ACK = 1 << 1
    IS_TELEMETRY = 1 << 2
    FRAGMENTED = 1 << 3
    LAST_FRAG = 1 << 4

class AckResult:
    OK = 0
    CLAMPED = 1
    REJECTED = 2
    BLOCKED = 3
    LOCKED = 4
    UNREACHABLE = 5
    UNSUPPORTED = 6
    UNAUTHORIZED = 7
    VERSION_MISMATCH = 8

ACK_RESULT_NAMES = {
    0: "OK",
    1: "CLAMPED",
    2: "REJECTED",
    3: "BLOCKED",
    4: "LOCKED",
    5: "UNREACHABLE",
    6: "UNSUPPORTED",
    7: "UNAUTHORIZED",
    8: "VERSION_MISMATCH",
}

ACK_TIERS = {
    "FAST": {"timeout_ms": 300, "max_retries": 2},
    "RELAY": {"timeout_ms": 3000, "max_retries": 1},
    "SLOW": {"timeout_ms": 9000, "max_retries": 0},
    "NONE": {"timeout_ms": 0, "max_retries": 0},
}

class Opcode:
    ACK = 0x00  # link=both dir=reply ack=NONE
    HELLO = 0x01  # link=both dir=cmd ack=FAST
    KEEPALIVE = 0x02  # link=both dir=cmd ack=NONE
    LOG = 0x03  # link=both dir=telem ack=NONE
    DIAG_HEALTH = 0x04  # link=both dir=cmd ack=FAST
    DIAG_PARFUM_TRACE = 0x05  # link=A dir=cmd ack=FAST
    LED_SET_COLOR = 0x10  # link=B dir=cmd ack=FAST
    LED_GET_COLOR = 0x11  # link=B dir=cmd ack=NONE
    LED_SET_DUAL_COLOR = 0x12  # link=B dir=cmd ack=FAST
    LED_GET_DUAL_COLOR = 0x13  # link=B dir=cmd ack=NONE
    LED_SET_SELECTION = 0x14  # link=B dir=cmd ack=FAST
    LED_SET_BRIGHTNESS = 0x15  # link=B dir=cmd ack=FAST
    LED_SET_ENABLE = 0x16  # link=B dir=cmd ack=FAST
    TELEM_COLOR_SYNC = 0x1A  # link=B dir=telem ack=NONE
    TELEM_DUAL_COLOR = 0x1B  # link=B dir=telem ack=NONE
    TELEM_MAX_BRIGHTNESS = 0x1C  # link=B dir=telem ack=NONE
    TELEM_ENABLE = 0x1D  # link=B dir=telem ack=NONE
    SETTINGS_READ_ALL = 0x20  # link=B dir=cmd ack=NONE
    SETTINGS_READ_ONE = 0x21  # link=B dir=cmd ack=NONE
    SETTINGS_WRITE = 0x22  # link=B dir=cmd ack=FAST
    TELEM_SETTINGS_FULL = 0x23  # link=B dir=telem ack=NONE
    TELEM_SETTINGS_ONE = 0x24  # link=B dir=telem ack=NONE
    TELEM_SAVE_RESULT = 0x25  # link=B dir=telem ack=NONE
    TELEM_STATUS = 0x30  # link=B dir=telem ack=NONE
    TELEM_CLIMATE = 0x31  # link=B dir=telem ack=NONE
    TELEM_LUX = 0x32  # link=B dir=telem ack=NONE
    TELEM_LINK = 0x33  # link=B dir=telem ack=NONE
    TELEM_FAULTS = 0x34  # link=B dir=telem ack=NONE
    TELEM_TEST_MODE = 0x35  # link=B dir=telem ack=NONE
    TELEM_DEVICE_ID = 0x36  # link=B dir=telem ack=NONE
    SET_AMBIENT_MODE = 0x40  # link=B dir=cmd ack=FAST
    SET_TEST_MODE = 0x41  # link=B dir=cmd ack=FAST
    SET_TEST_DIFFUSER = 0x42  # link=B dir=cmd ack=RELAY
    SET_TEST_LUX = 0x43  # link=B dir=cmd ack=FAST
    SET_TELNET_ENABLE = 0x44  # link=B dir=cmd ack=FAST
    SET_TELNET_VERBOSITY = 0x45  # link=B dir=cmd ack=FAST
    SET_MQTT_CREDENTIALS = 0x50  # link=B dir=cmd ack=SLOW
    DIFFUSER_STATUS_QUERY = 0x60  # link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_QUERY = 0x61  # link=both dir=cmd ack=RELAY
    DIFFUSER_HISTORY_REMOVE = 0x62  # link=both dir=cmd ack=RELAY
    DIFFUSER_MANUAL_REFILL = 0x63  # link=both dir=cmd ack=RELAY
    DIFFUSER_SHUTDOWN = 0x64  # link=both dir=cmd ack=RELAY
    DIFFUSER_TURN_ON = 0x65  # link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_START = 0x66  # link=both dir=cmd ack=RELAY
    DIFFUSER_PARFUM_CANCEL = 0x67  # link=both dir=cmd ack=RELAY
    TELEM_DIFFUSER_STATUS = 0x69  # link=both dir=telem ack=NONE
    TELEM_DIFFUSER_HISTORY = 0x6A  # link=both dir=telem ack=NONE
    TELEM_DIFFUSER_USAGE = 0x6B  # link=both dir=telem ack=NONE
    TELEM_PARFUM_REMAINING = 0x6C  # link=both dir=telem ack=NONE

OPCODE_NAMES = {
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
    0x45: "SET_TELNET_VERBOSITY",
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
    0x6C: "TELEM_PARFUM_REMAINING",
}

def crc8(data):
    """Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() in protocol_opcodes.cpp bit for bit."""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0x8C) if (crc & 0x01) else (crc >> 1)
    return crc & 0xFF

# --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---
def pack_ack(result):
    return struct.pack(">B", result)
def unpack_ack(data):
    return dict(zip(['result'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_hello(proto_version):
    return struct.pack(">B", proto_version)
def unpack_hello(data):
    return dict(zip(['proto_version'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_led_set_color(r, g, b):
    return struct.pack(">BBB", r, g, b)
def unpack_led_set_color(data):
    return dict(zip(['r', 'g', 'b'], struct.unpack(">BBB", data[:struct.calcsize(">BBB")])))

def pack_led_set_dual_color(shake, r1, g1, b1, r2, g2, b2):
    return struct.pack(">BBBBBBB", shake, r1, g1, b1, r2, g2, b2)
def unpack_led_set_dual_color(data):
    return dict(zip(['shake', 'r1', 'g1', 'b1', 'r2', 'g2', 'b2'], struct.unpack(">BBBBBBB", data[:struct.calcsize(">BBBBBBB")])))

def pack_led_set_selection(mask):
    return struct.pack(">8s", mask)
def unpack_led_set_selection(data):
    return dict(zip(['mask'], struct.unpack(">8s", data[:struct.calcsize(">8s")])))

def pack_led_set_brightness(value):
    return struct.pack(">B", value)
def unpack_led_set_brightness(data):
    return dict(zip(['value'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_dual_color(r1, g1, b1, r2, g2, b2):
    return struct.pack(">BBBBBB", r1, g1, b1, r2, g2, b2)
def unpack_telem_dual_color(data):
    return dict(zip(['r1', 'g1', 'b1', 'r2', 'g2', 'b2'], struct.unpack(">BBBBBB", data[:struct.calcsize(">BBBBBB")])))

def pack_telem_max_brightness(value):
    return struct.pack(">B", value)
def unpack_telem_max_brightness(data):
    return dict(zip(['value'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_enable(value):
    return struct.pack(">B", value)
def unpack_telem_enable(data):
    return dict(zip(['value'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_settings_read_one(id):
    return struct.pack(">B", id)
def unpack_settings_read_one(data):
    return dict(zip(['id'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_settings_full(values):
    return struct.pack(">50s", values)
def unpack_telem_settings_full(data):
    return dict(zip(['values'], struct.unpack(">50s", data[:struct.calcsize(">50s")])))

def pack_telem_settings_one(id, value):
    return struct.pack(">BB", id, value)
def unpack_telem_settings_one(data):
    return dict(zip(['id', 'value'], struct.unpack(">BB", data[:struct.calcsize(">BB")])))

def pack_telem_save_result(result):
    return struct.pack(">B", result)
def unpack_telem_save_result(data):
    return dict(zip(['result'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_status(tv, motion, udpraw, ambient, diffuser_summary):
    return struct.pack(">BBBBB", tv, motion, udpraw, ambient, diffuser_summary)
def unpack_telem_status(data):
    return dict(zip(['tv', 'motion', 'udpraw', 'ambient', 'diffuser_summary'], struct.unpack(">BBBBB", data[:struct.calcsize(">BBBBB")])))

def pack_telem_climate(temp_c, humidity_pct):
    return struct.pack(">bB", temp_c, humidity_pct)
def unpack_telem_climate(data):
    return dict(zip(['temp_c', 'humidity_pct'], struct.unpack(">bB", data[:struct.calcsize(">bB")])))

def pack_telem_lux(level):
    return struct.pack(">B", level)
def unpack_telem_lux(data):
    return dict(zip(['level'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_link(rssi_bucket, wifi_state):
    return struct.pack(">BB", rssi_bucket, wifi_state)
def unpack_telem_link(data):
    return dict(zip(['rssi_bucket', 'wifi_state'], struct.unpack(">BB", data[:struct.calcsize(">BB")])))

def pack_telem_faults(mask):
    return struct.pack(">H", mask)
def unpack_telem_faults(data):
    return dict(zip(['mask'], struct.unpack(">H", data[:struct.calcsize(">H")])))

def pack_telem_test_mode(mode):
    return struct.pack(">B", mode)
def unpack_telem_test_mode(data):
    return dict(zip(['mode'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_telem_device_id(device_id):
    return struct.pack(">12s", device_id)
def unpack_telem_device_id(data):
    return dict(zip(['device_id'], struct.unpack(">12s", data[:struct.calcsize(">12s")])))

def pack_set_ambient_mode(on):
    return struct.pack(">B", on)
def unpack_set_ambient_mode(data):
    return dict(zip(['on'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_set_test_mode(mode):
    return struct.pack(">B", mode)
def unpack_set_test_mode(data):
    return dict(zip(['mode'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_set_test_diffuser(value):
    return struct.pack(">B", value)
def unpack_set_test_diffuser(data):
    return dict(zip(['value'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_set_test_lux(level):
    return struct.pack(">B", level)
def unpack_set_test_lux(data):
    return dict(zip(['level'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_set_telnet_enable(on):
    return struct.pack(">B", on)
def unpack_set_telnet_enable(data):
    return dict(zip(['on'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_set_telnet_verbosity(level):
    return struct.pack(">B", level)
def unpack_set_telnet_verbosity(data):
    return dict(zip(['level'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_diffuser_status_query(verbose):
    return struct.pack(">B", verbose)
def unpack_diffuser_status_query(data):
    return dict(zip(['verbose'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_diffuser_history_remove(index):
    return struct.pack(">B", index)
def unpack_diffuser_history_remove(data):
    return dict(zip(['index'], struct.unpack(">B", data[:struct.calcsize(">B")])))

def pack_diffuser_parfum_start(minutes, mode):
    return struct.pack(">HB", minutes, mode)
def unpack_diffuser_parfum_start(data):
    return dict(zip(['minutes', 'mode'], struct.unpack(">HB", data[:struct.calcsize(">HB")])))

def pack_telem_diffuser_status(mode, strip, parfum_min, usage_min, avg_min, refill_count, lifetime_refills):
    return struct.pack(">BBHHHBH", mode, strip, parfum_min, usage_min, avg_min, refill_count, lifetime_refills)
def unpack_telem_diffuser_status(data):
    return dict(zip(['mode', 'strip', 'parfum_min', 'usage_min', 'avg_min', 'refill_count', 'lifetime_refills'], struct.unpack(">BBHHHBH", data[:struct.calcsize(">BBHHHBH")])))

def pack_telem_diffuser_usage(accum_min, avg_min, refill_count, lifetime_refills):
    return struct.pack(">HHBH", accum_min, avg_min, refill_count, lifetime_refills)
def unpack_telem_diffuser_usage(data):
    return dict(zip(['accum_min', 'avg_min', 'refill_count', 'lifetime_refills'], struct.unpack(">HHBH", data[:struct.calcsize(">HHBH")])))

def pack_telem_parfum_remaining(minutes):
    return struct.pack(">H", minutes)
def unpack_telem_parfum_remaining(data):
    return dict(zip(['minutes'], struct.unpack(">H", data[:struct.calcsize(">H")])))
