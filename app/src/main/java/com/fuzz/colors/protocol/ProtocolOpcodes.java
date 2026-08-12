package com.fuzz.colors.protocol;

/*
 * GENERATED FILE - DO NOT EDIT.
 * Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json
 * Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py
 * See _rmk_docs/01_PROTOCOL.md for the full protocol specification.
 */
public final class ProtocolOpcodes {
    private ProtocolOpcodes() {}

    public static final byte FRAME_MAGIC = (byte) 0xF2;
    public static final int FRAME_OVERHEAD_BYTES = 5;
    public static final int FRAME_MAX_BYTES = 255;

    public static final class Flag {
        private Flag() {}
        public static final int ACK_REQ = 1 << 0;
        public static final int IS_ACK = 1 << 1;
        public static final int IS_TELEMETRY = 1 << 2;
        public static final int FRAGMENTED = 1 << 3;
        public static final int LAST_FRAG = 1 << 4;
    }

    public static final class AckResult {
        private AckResult() {}
        public static final int OK = 0;
        public static final int CLAMPED = 1;
        public static final int REJECTED = 2;
        public static final int BLOCKED = 3;
        public static final int LOCKED = 4;
        public static final int UNREACHABLE = 5;
        public static final int UNSUPPORTED = 6;
        public static final int UNAUTHORIZED = 7;
        public static final int VERSION_MISMATCH = 8;
    }

    public enum AckTier { FAST(300, 2), RELAY(3000, 1), SLOW(9000, 0), NONE(0, 0);
        public final int timeoutMs; public final int maxRetries;
        AckTier(int t, int r) { timeoutMs = t; maxRetries = r; }
    }

    public static final class Opcode {
        private Opcode() {}
        public static final byte ACK = (byte) 0x00;  // link=both dir=reply ack=NONE
        public static final byte HELLO = (byte) 0x01;  // link=both dir=cmd ack=FAST
        public static final byte KEEPALIVE = (byte) 0x02;  // link=both dir=cmd ack=NONE
        public static final byte LOG = (byte) 0x03;  // link=both dir=telem ack=NONE
        public static final byte DIAG_HEALTH = (byte) 0x04;  // link=both dir=cmd ack=FAST
        public static final byte DIAG_PARFUM_TRACE = (byte) 0x05;  // link=A dir=cmd ack=FAST
        public static final byte LED_SET_COLOR = (byte) 0x10;  // link=B dir=cmd ack=FAST
        public static final byte LED_GET_COLOR = (byte) 0x11;  // link=B dir=cmd ack=NONE
        public static final byte LED_SET_DUAL_COLOR = (byte) 0x12;  // link=B dir=cmd ack=FAST
        public static final byte LED_GET_DUAL_COLOR = (byte) 0x13;  // link=B dir=cmd ack=NONE
        public static final byte LED_SET_SELECTION = (byte) 0x14;  // link=B dir=cmd ack=FAST
        public static final byte LED_SET_BRIGHTNESS = (byte) 0x15;  // link=B dir=cmd ack=FAST
        public static final byte LED_SET_ENABLE = (byte) 0x16;  // link=B dir=cmd ack=FAST
        public static final byte TELEM_COLOR_SYNC = (byte) 0x1A;  // link=B dir=telem ack=NONE
        public static final byte TELEM_DUAL_COLOR = (byte) 0x1B;  // link=B dir=telem ack=NONE
        public static final byte TELEM_MAX_BRIGHTNESS = (byte) 0x1C;  // link=B dir=telem ack=NONE
        public static final byte TELEM_ENABLE = (byte) 0x1D;  // link=B dir=telem ack=NONE
        public static final byte SETTINGS_READ_ALL = (byte) 0x20;  // link=B dir=cmd ack=NONE
        public static final byte SETTINGS_READ_ONE = (byte) 0x21;  // link=B dir=cmd ack=NONE
        public static final byte SETTINGS_WRITE = (byte) 0x22;  // link=B dir=cmd ack=FAST
        public static final byte TELEM_SETTINGS_FULL = (byte) 0x23;  // link=B dir=telem ack=NONE
        public static final byte TELEM_SETTINGS_ONE = (byte) 0x24;  // link=B dir=telem ack=NONE
        public static final byte TELEM_SAVE_RESULT = (byte) 0x25;  // link=B dir=telem ack=NONE
        public static final byte TELEM_STATUS = (byte) 0x30;  // link=B dir=telem ack=NONE
        public static final byte TELEM_CLIMATE = (byte) 0x31;  // link=B dir=telem ack=NONE
        public static final byte TELEM_LUX = (byte) 0x32;  // link=B dir=telem ack=NONE
        public static final byte TELEM_LINK = (byte) 0x33;  // link=B dir=telem ack=NONE
        public static final byte TELEM_FAULTS = (byte) 0x34;  // link=B dir=telem ack=NONE
        public static final byte TELEM_TEST_MODE = (byte) 0x35;  // link=B dir=telem ack=NONE
        public static final byte SET_AMBIENT_MODE = (byte) 0x40;  // link=B dir=cmd ack=FAST
        public static final byte SET_TEST_MODE = (byte) 0x41;  // link=B dir=cmd ack=FAST
        public static final byte SET_TEST_DIFFUSER = (byte) 0x42;  // link=B dir=cmd ack=RELAY
        public static final byte SET_TEST_LUX = (byte) 0x43;  // link=B dir=cmd ack=FAST
        public static final byte SET_TELNET_ENABLE = (byte) 0x44;  // link=B dir=cmd ack=FAST
        public static final byte SET_MQTT_CREDENTIALS = (byte) 0x50;  // link=B dir=cmd ack=SLOW
        public static final byte DIFFUSER_STATUS_QUERY = (byte) 0x60;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_HISTORY_QUERY = (byte) 0x61;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_HISTORY_REMOVE = (byte) 0x62;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_MANUAL_REFILL = (byte) 0x63;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_SHUTDOWN = (byte) 0x64;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_TURN_ON = (byte) 0x65;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_PARFUM_START = (byte) 0x66;  // link=both dir=cmd ack=RELAY
        public static final byte DIFFUSER_PARFUM_CANCEL = (byte) 0x67;  // link=both dir=cmd ack=RELAY
        public static final byte TELEM_DIFFUSER_STATUS = (byte) 0x69;  // link=both dir=telem ack=NONE
        public static final byte TELEM_DIFFUSER_HISTORY = (byte) 0x6A;  // link=both dir=telem ack=NONE
        public static final byte TELEM_DIFFUSER_USAGE = (byte) 0x6B;  // link=both dir=telem ack=NONE
        public static final byte TELEM_PARFUM_REMAINING = (byte) 0x6C;  // link=both dir=telem ack=NONE
    }

    public static String opcodeName(byte op) {
        switch (op) {
            case Opcode.ACK: return "ACK";
            case Opcode.HELLO: return "HELLO";
            case Opcode.KEEPALIVE: return "KEEPALIVE";
            case Opcode.LOG: return "LOG";
            case Opcode.DIAG_HEALTH: return "DIAG_HEALTH";
            case Opcode.DIAG_PARFUM_TRACE: return "DIAG_PARFUM_TRACE";
            case Opcode.LED_SET_COLOR: return "LED_SET_COLOR";
            case Opcode.LED_GET_COLOR: return "LED_GET_COLOR";
            case Opcode.LED_SET_DUAL_COLOR: return "LED_SET_DUAL_COLOR";
            case Opcode.LED_GET_DUAL_COLOR: return "LED_GET_DUAL_COLOR";
            case Opcode.LED_SET_SELECTION: return "LED_SET_SELECTION";
            case Opcode.LED_SET_BRIGHTNESS: return "LED_SET_BRIGHTNESS";
            case Opcode.LED_SET_ENABLE: return "LED_SET_ENABLE";
            case Opcode.TELEM_COLOR_SYNC: return "TELEM_COLOR_SYNC";
            case Opcode.TELEM_DUAL_COLOR: return "TELEM_DUAL_COLOR";
            case Opcode.TELEM_MAX_BRIGHTNESS: return "TELEM_MAX_BRIGHTNESS";
            case Opcode.TELEM_ENABLE: return "TELEM_ENABLE";
            case Opcode.SETTINGS_READ_ALL: return "SETTINGS_READ_ALL";
            case Opcode.SETTINGS_READ_ONE: return "SETTINGS_READ_ONE";
            case Opcode.SETTINGS_WRITE: return "SETTINGS_WRITE";
            case Opcode.TELEM_SETTINGS_FULL: return "TELEM_SETTINGS_FULL";
            case Opcode.TELEM_SETTINGS_ONE: return "TELEM_SETTINGS_ONE";
            case Opcode.TELEM_SAVE_RESULT: return "TELEM_SAVE_RESULT";
            case Opcode.TELEM_STATUS: return "TELEM_STATUS";
            case Opcode.TELEM_CLIMATE: return "TELEM_CLIMATE";
            case Opcode.TELEM_LUX: return "TELEM_LUX";
            case Opcode.TELEM_LINK: return "TELEM_LINK";
            case Opcode.TELEM_FAULTS: return "TELEM_FAULTS";
            case Opcode.TELEM_TEST_MODE: return "TELEM_TEST_MODE";
            case Opcode.SET_AMBIENT_MODE: return "SET_AMBIENT_MODE";
            case Opcode.SET_TEST_MODE: return "SET_TEST_MODE";
            case Opcode.SET_TEST_DIFFUSER: return "SET_TEST_DIFFUSER";
            case Opcode.SET_TEST_LUX: return "SET_TEST_LUX";
            case Opcode.SET_TELNET_ENABLE: return "SET_TELNET_ENABLE";
            case Opcode.SET_MQTT_CREDENTIALS: return "SET_MQTT_CREDENTIALS";
            case Opcode.DIFFUSER_STATUS_QUERY: return "DIFFUSER_STATUS_QUERY";
            case Opcode.DIFFUSER_HISTORY_QUERY: return "DIFFUSER_HISTORY_QUERY";
            case Opcode.DIFFUSER_HISTORY_REMOVE: return "DIFFUSER_HISTORY_REMOVE";
            case Opcode.DIFFUSER_MANUAL_REFILL: return "DIFFUSER_MANUAL_REFILL";
            case Opcode.DIFFUSER_SHUTDOWN: return "DIFFUSER_SHUTDOWN";
            case Opcode.DIFFUSER_TURN_ON: return "DIFFUSER_TURN_ON";
            case Opcode.DIFFUSER_PARFUM_START: return "DIFFUSER_PARFUM_START";
            case Opcode.DIFFUSER_PARFUM_CANCEL: return "DIFFUSER_PARFUM_CANCEL";
            case Opcode.TELEM_DIFFUSER_STATUS: return "TELEM_DIFFUSER_STATUS";
            case Opcode.TELEM_DIFFUSER_HISTORY: return "TELEM_DIFFUSER_HISTORY";
            case Opcode.TELEM_DIFFUSER_USAGE: return "TELEM_DIFFUSER_USAGE";
            case Opcode.TELEM_PARFUM_REMAINING: return "TELEM_PARFUM_REMAINING";
            default: return "UNKNOWN";
        }
    }

    /** Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() in protocol_opcodes.cpp bit for bit. */
    public static byte crc8(byte[] data, int offset, int len) {
        int crc = 0;
        for (int i = offset; i < offset + len; i++) {
            crc ^= (data[i] & 0xFF);
            for (int b = 0; b < 8; b++)
                crc = ((crc & 0x01) != 0) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);
        }
        return (byte) crc;
    }

    // --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---
    public static final class AckPayload {
        public int result;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) result;
            return out;
        }
        public static AckPayload unpack(byte[] in, int offset) {
            AckPayload p = new AckPayload();
            int i = offset;
            p.result = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class HelloPayload {
        public int proto_version;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) proto_version;
            return out;
        }
        public static HelloPayload unpack(byte[] in, int offset) {
            HelloPayload p = new HelloPayload();
            int i = offset;
            p.proto_version = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class LedSetColorPayload {
        public int r;
        public int g;
        public int b;
        public static final int SIZE = 1 + 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) r;
            out[i++] = (byte) g;
            out[i++] = (byte) b;
            return out;
        }
        public static LedSetColorPayload unpack(byte[] in, int offset) {
            LedSetColorPayload p = new LedSetColorPayload();
            int i = offset;
            p.r = in[i++] & 0xFF;
            p.g = in[i++] & 0xFF;
            p.b = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class LedSetDualColorPayload {
        public int shake;
        public int r1;
        public int g1;
        public int b1;
        public int r2;
        public int g2;
        public int b2;
        public static final int SIZE = 1 + 1 + 1 + 1 + 1 + 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) shake;
            out[i++] = (byte) r1;
            out[i++] = (byte) g1;
            out[i++] = (byte) b1;
            out[i++] = (byte) r2;
            out[i++] = (byte) g2;
            out[i++] = (byte) b2;
            return out;
        }
        public static LedSetDualColorPayload unpack(byte[] in, int offset) {
            LedSetDualColorPayload p = new LedSetDualColorPayload();
            int i = offset;
            p.shake = in[i++] & 0xFF;
            p.r1 = in[i++] & 0xFF;
            p.g1 = in[i++] & 0xFF;
            p.b1 = in[i++] & 0xFF;
            p.r2 = in[i++] & 0xFF;
            p.g2 = in[i++] & 0xFF;
            p.b2 = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class LedSetSelectionPayload {
        public byte[] mask;
        public static final int SIZE = 8;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            System.arraycopy(mask, 0, out, i, 8); i += 8;
            return out;
        }
        public static LedSetSelectionPayload unpack(byte[] in, int offset) {
            LedSetSelectionPayload p = new LedSetSelectionPayload();
            int i = offset;
            p.mask = new byte[8]; System.arraycopy(in, i, p.mask, 0, 8); i += 8;
            return p;
        }
    }
    public static final class LedSetBrightnessPayload {
        public int value;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) value;
            return out;
        }
        public static LedSetBrightnessPayload unpack(byte[] in, int offset) {
            LedSetBrightnessPayload p = new LedSetBrightnessPayload();
            int i = offset;
            p.value = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemDualColorPayload {
        public int r1;
        public int g1;
        public int b1;
        public int r2;
        public int g2;
        public int b2;
        public static final int SIZE = 1 + 1 + 1 + 1 + 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) r1;
            out[i++] = (byte) g1;
            out[i++] = (byte) b1;
            out[i++] = (byte) r2;
            out[i++] = (byte) g2;
            out[i++] = (byte) b2;
            return out;
        }
        public static TelemDualColorPayload unpack(byte[] in, int offset) {
            TelemDualColorPayload p = new TelemDualColorPayload();
            int i = offset;
            p.r1 = in[i++] & 0xFF;
            p.g1 = in[i++] & 0xFF;
            p.b1 = in[i++] & 0xFF;
            p.r2 = in[i++] & 0xFF;
            p.g2 = in[i++] & 0xFF;
            p.b2 = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemMaxBrightnessPayload {
        public int value;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) value;
            return out;
        }
        public static TelemMaxBrightnessPayload unpack(byte[] in, int offset) {
            TelemMaxBrightnessPayload p = new TelemMaxBrightnessPayload();
            int i = offset;
            p.value = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemEnablePayload {
        public int value;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) value;
            return out;
        }
        public static TelemEnablePayload unpack(byte[] in, int offset) {
            TelemEnablePayload p = new TelemEnablePayload();
            int i = offset;
            p.value = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SettingsReadOnePayload {
        public int id;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) id;
            return out;
        }
        public static SettingsReadOnePayload unpack(byte[] in, int offset) {
            SettingsReadOnePayload p = new SettingsReadOnePayload();
            int i = offset;
            p.id = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemSettingsFullPayload {
        public byte[] values;
        public static final int SIZE = 50;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            System.arraycopy(values, 0, out, i, 50); i += 50;
            return out;
        }
        public static TelemSettingsFullPayload unpack(byte[] in, int offset) {
            TelemSettingsFullPayload p = new TelemSettingsFullPayload();
            int i = offset;
            p.values = new byte[50]; System.arraycopy(in, i, p.values, 0, 50); i += 50;
            return p;
        }
    }
    public static final class TelemSettingsOnePayload {
        public int id;
        public int value;
        public static final int SIZE = 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) id;
            out[i++] = (byte) value;
            return out;
        }
        public static TelemSettingsOnePayload unpack(byte[] in, int offset) {
            TelemSettingsOnePayload p = new TelemSettingsOnePayload();
            int i = offset;
            p.id = in[i++] & 0xFF;
            p.value = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemSaveResultPayload {
        public int result;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) result;
            return out;
        }
        public static TelemSaveResultPayload unpack(byte[] in, int offset) {
            TelemSaveResultPayload p = new TelemSaveResultPayload();
            int i = offset;
            p.result = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemStatusPayload {
        public int tv;
        public int motion;
        public int udpraw;
        public int ambient;
        public int diffuser_summary;
        public static final int SIZE = 1 + 1 + 1 + 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) tv;
            out[i++] = (byte) motion;
            out[i++] = (byte) udpraw;
            out[i++] = (byte) ambient;
            out[i++] = (byte) diffuser_summary;
            return out;
        }
        public static TelemStatusPayload unpack(byte[] in, int offset) {
            TelemStatusPayload p = new TelemStatusPayload();
            int i = offset;
            p.tv = in[i++] & 0xFF;
            p.motion = in[i++] & 0xFF;
            p.udpraw = in[i++] & 0xFF;
            p.ambient = in[i++] & 0xFF;
            p.diffuser_summary = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemClimatePayload {
        public int temp_c;
        public int humidity_pct;
        public static final int SIZE = 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) temp_c;
            out[i++] = (byte) humidity_pct;
            return out;
        }
        public static TelemClimatePayload unpack(byte[] in, int offset) {
            TelemClimatePayload p = new TelemClimatePayload();
            int i = offset;
            p.temp_c = in[i++] & 0xFF;
            p.humidity_pct = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemLuxPayload {
        public int level;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) level;
            return out;
        }
        public static TelemLuxPayload unpack(byte[] in, int offset) {
            TelemLuxPayload p = new TelemLuxPayload();
            int i = offset;
            p.level = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemLinkPayload {
        public int rssi_bucket;
        public int wifi_state;
        public static final int SIZE = 1 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) rssi_bucket;
            out[i++] = (byte) wifi_state;
            return out;
        }
        public static TelemLinkPayload unpack(byte[] in, int offset) {
            TelemLinkPayload p = new TelemLinkPayload();
            int i = offset;
            p.rssi_bucket = in[i++] & 0xFF;
            p.wifi_state = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemFaultsPayload {
        public int mask;
        public static final int SIZE = 2;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) ((mask >> 8) & 0xFF); out[i++] = (byte) (mask & 0xFF);
            return out;
        }
        public static TelemFaultsPayload unpack(byte[] in, int offset) {
            TelemFaultsPayload p = new TelemFaultsPayload();
            int i = offset;
            p.mask = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            return p;
        }
    }
    public static final class TelemTestModePayload {
        public int mode;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) mode;
            return out;
        }
        public static TelemTestModePayload unpack(byte[] in, int offset) {
            TelemTestModePayload p = new TelemTestModePayload();
            int i = offset;
            p.mode = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SetAmbientModePayload {
        public int on;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) on;
            return out;
        }
        public static SetAmbientModePayload unpack(byte[] in, int offset) {
            SetAmbientModePayload p = new SetAmbientModePayload();
            int i = offset;
            p.on = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SetTestModePayload {
        public int mode;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) mode;
            return out;
        }
        public static SetTestModePayload unpack(byte[] in, int offset) {
            SetTestModePayload p = new SetTestModePayload();
            int i = offset;
            p.mode = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SetTestDiffuserPayload {
        public int value;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) value;
            return out;
        }
        public static SetTestDiffuserPayload unpack(byte[] in, int offset) {
            SetTestDiffuserPayload p = new SetTestDiffuserPayload();
            int i = offset;
            p.value = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SetTestLuxPayload {
        public int level;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) level;
            return out;
        }
        public static SetTestLuxPayload unpack(byte[] in, int offset) {
            SetTestLuxPayload p = new SetTestLuxPayload();
            int i = offset;
            p.level = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class SetTelnetEnablePayload {
        public int on;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) on;
            return out;
        }
        public static SetTelnetEnablePayload unpack(byte[] in, int offset) {
            SetTelnetEnablePayload p = new SetTelnetEnablePayload();
            int i = offset;
            p.on = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class DiffuserStatusQueryPayload {
        public int verbose;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) verbose;
            return out;
        }
        public static DiffuserStatusQueryPayload unpack(byte[] in, int offset) {
            DiffuserStatusQueryPayload p = new DiffuserStatusQueryPayload();
            int i = offset;
            p.verbose = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class DiffuserHistoryRemovePayload {
        public int index;
        public static final int SIZE = 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) index;
            return out;
        }
        public static DiffuserHistoryRemovePayload unpack(byte[] in, int offset) {
            DiffuserHistoryRemovePayload p = new DiffuserHistoryRemovePayload();
            int i = offset;
            p.index = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class DiffuserParfumStartPayload {
        public int minutes;
        public int mode;
        public static final int SIZE = 2 + 1;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) ((minutes >> 8) & 0xFF); out[i++] = (byte) (minutes & 0xFF);
            out[i++] = (byte) mode;
            return out;
        }
        public static DiffuserParfumStartPayload unpack(byte[] in, int offset) {
            DiffuserParfumStartPayload p = new DiffuserParfumStartPayload();
            int i = offset;
            p.minutes = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.mode = in[i++] & 0xFF;
            return p;
        }
    }
    public static final class TelemDiffuserStatusPayload {
        public int mode;
        public int strip;
        public int parfum_min;
        public int usage_min;
        public int avg_min;
        public int refill_count;
        public int lifetime_refills;
        public static final int SIZE = 1 + 1 + 2 + 2 + 2 + 1 + 2;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) mode;
            out[i++] = (byte) strip;
            out[i++] = (byte) ((parfum_min >> 8) & 0xFF); out[i++] = (byte) (parfum_min & 0xFF);
            out[i++] = (byte) ((usage_min >> 8) & 0xFF); out[i++] = (byte) (usage_min & 0xFF);
            out[i++] = (byte) ((avg_min >> 8) & 0xFF); out[i++] = (byte) (avg_min & 0xFF);
            out[i++] = (byte) refill_count;
            out[i++] = (byte) ((lifetime_refills >> 8) & 0xFF); out[i++] = (byte) (lifetime_refills & 0xFF);
            return out;
        }
        public static TelemDiffuserStatusPayload unpack(byte[] in, int offset) {
            TelemDiffuserStatusPayload p = new TelemDiffuserStatusPayload();
            int i = offset;
            p.mode = in[i++] & 0xFF;
            p.strip = in[i++] & 0xFF;
            p.parfum_min = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.usage_min = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.avg_min = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.refill_count = in[i++] & 0xFF;
            p.lifetime_refills = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            return p;
        }
    }
    public static final class TelemDiffuserUsagePayload {
        public int accum_min;
        public int avg_min;
        public int refill_count;
        public int lifetime_refills;
        public static final int SIZE = 2 + 2 + 1 + 2;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) ((accum_min >> 8) & 0xFF); out[i++] = (byte) (accum_min & 0xFF);
            out[i++] = (byte) ((avg_min >> 8) & 0xFF); out[i++] = (byte) (avg_min & 0xFF);
            out[i++] = (byte) refill_count;
            out[i++] = (byte) ((lifetime_refills >> 8) & 0xFF); out[i++] = (byte) (lifetime_refills & 0xFF);
            return out;
        }
        public static TelemDiffuserUsagePayload unpack(byte[] in, int offset) {
            TelemDiffuserUsagePayload p = new TelemDiffuserUsagePayload();
            int i = offset;
            p.accum_min = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.avg_min = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            p.refill_count = in[i++] & 0xFF;
            p.lifetime_refills = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            return p;
        }
    }
    public static final class TelemParfumRemainingPayload {
        public int minutes;
        public static final int SIZE = 2;
        public byte[] pack() {
            byte[] out = new byte[SIZE];
            int i = 0;
            out[i++] = (byte) ((minutes >> 8) & 0xFF); out[i++] = (byte) (minutes & 0xFF);
            return out;
        }
        public static TelemParfumRemainingPayload unpack(byte[] in, int offset) {
            TelemParfumRemainingPayload p = new TelemParfumRemainingPayload();
            int i = offset;
            p.minutes = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;
            return p;
        }
    }
}
