#include "Frame.h"

namespace Proto {

size_t BuildFrame(Opcode opcode, uint8_t seq, uint8_t flags,
                   const uint8_t* payload, size_t payloadLen,
                   uint8_t* out, size_t outCap) {
    size_t total = FRAME_OVERHEAD_BYTES + payloadLen;
    if (total > FRAME_MAX_BYTES || total > outCap) return 0;

    out[0] = FRAME_MAGIC;
    out[1] = flags;
    out[2] = seq;
    out[3] = (uint8_t)opcode;
    if (payloadLen > 0 && payload) {
        for (size_t i = 0; i < payloadLen; i++) out[4 + i] = payload[i];
    }
    out[4 + payloadLen] = Crc8(out, 4 + payloadLen);
    return total;
}

ParsedFrame ParseFrame(const uint8_t* data, size_t len) {
    ParsedFrame f{};
    f.ok = false;
    if (len < FRAME_OVERHEAD_BYTES) return f;
    if (data[0] != FRAME_MAGIC) return f;

    size_t bodyLen = len - 1;  // everything but the trailing CRC byte
    if (Crc8(data, bodyLen) != data[len - 1]) return f;

    f.ok = true;
    f.flags = data[1];
    f.seq = data[2];
    f.opcode = data[3];
    f.payload = data + 4;
    f.payloadLen = bodyLen - 4;
    return f;
}

} // namespace Proto
