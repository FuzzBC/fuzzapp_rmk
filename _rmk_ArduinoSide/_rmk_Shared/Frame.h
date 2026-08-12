#pragma once
// Frame.h - generic frame build/parse, shared by both boards. Hand-written
// (not generated - framing is fixed/structural, not opcode-driven; only the
// opcode table itself is generated, see protocol_opcodes.h/.py's own
// GENERATED banner and gen_protocol.py). Mirrors rmk_testmode.py's
// build_frame()/parse_frame() byte for byte - keep the two in lockstep by
// hand if either changes; there's no single generator for this piece yet
// (see _rmk_docs/01_PROTOCOL.md SS8 for the long-term plan).
#include "protocol_opcodes.h"

namespace Proto {

struct ParsedFrame {
    bool ok;
    uint8_t flags;
    uint8_t seq;
    uint8_t opcode;
    const uint8_t* payload;
    size_t payloadLen;
};

// Builds MAGIC+FLAGS+SEQ+OPCODE+payload+CRC8 into out[]. Returns the total
// frame length, or 0 if it wouldn't fit (payloadLen too large for outCap or
// for FRAME_MAX_BYTES).
size_t BuildFrame(Opcode opcode, uint8_t seq, uint8_t flags,
                   const uint8_t* payload, size_t payloadLen,
                   uint8_t* out, size_t outCap);

// Parses data[0..len). ok=false on a length/magic/CRC failure - payload and
// payloadLen are unset in that case, don't trust the seq/opcode either.
ParsedFrame ParseFrame(const uint8_t* data, size_t len);

} // namespace Proto
