package com.fuzz.colors.protocol;

/**
 * Frame build/parse for the binary v1 wire protocol (01_PROTOCOL.md) -
 * byte-for-byte identical layout to the C++/Python/JS siblings:
 * MAGIC(1) FLAGS(1) SEQ(1) OPCODE(1) payload(0-247) CRC8(1). Hand-written,
 * not generated, same reasoning as every other language's Frame - keep all
 * of them in lockstep by hand if the framing itself ever changes.
 */
public final class Frame {
    private Frame() {}

    public static final class Parsed {
        public final boolean ok;
        public final int flags;
        public final int seq;
        public final int opcode;
        public final byte[] payload;   // never null when ok=true (zero-length array if no payload)
        public final String reason;    // set only when ok=false

        private Parsed(boolean ok, int flags, int seq, int opcode, byte[] payload, String reason) {
            this.ok = ok; this.flags = flags; this.seq = seq; this.opcode = opcode;
            this.payload = payload; this.reason = reason;
        }
        static Parsed fail(String reason) { return new Parsed(false, 0, 0, 0, null, reason); }
    }

    /** @return the built frame, or null if it wouldn't fit FRAME_MAX_BYTES. */
    public static byte[] build(byte opcode, int seq, int flags, byte[] payload) {
        int payloadLen = payload == null ? 0 : payload.length;
        int total = ProtocolOpcodes.FRAME_OVERHEAD_BYTES + payloadLen;
        if (total > ProtocolOpcodes.FRAME_MAX_BYTES) return null;

        byte[] out = new byte[total];
        out[0] = ProtocolOpcodes.FRAME_MAGIC;
        out[1] = (byte) flags;
        out[2] = (byte) seq;
        out[3] = opcode;
        if (payloadLen > 0) System.arraycopy(payload, 0, out, 4, payloadLen);
        out[4 + payloadLen] = ProtocolOpcodes.crc8(out, 0, 4 + payloadLen);
        return out;
    }

    public static Parsed parse(byte[] data, int len) {
        if (data == null || len < ProtocolOpcodes.FRAME_OVERHEAD_BYTES) return Parsed.fail("too short");
        if (data[0] != ProtocolOpcodes.FRAME_MAGIC) return Parsed.fail("bad magic");

        int bodyLen = len - 1;   // everything but the trailing CRC byte
        if (ProtocolOpcodes.crc8(data, 0, bodyLen) != data[len - 1]) return Parsed.fail("CRC mismatch");

        int flags = data[1] & 0xFF;
        int seq = data[2] & 0xFF;
        int opcode = data[3] & 0xFF;
        int payloadLen = bodyLen - 4;
        byte[] payload = new byte[payloadLen];
        System.arraycopy(data, 4, payload, 0, payloadLen);
        return new Parsed(true, flags, seq, opcode, payload, null);
    }
}
