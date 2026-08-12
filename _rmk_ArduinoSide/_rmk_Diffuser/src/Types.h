#pragma once
// Types.h - structs/enums shared across modules. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's GLOBALS section (see _rmk_docs/00_PLAN.md SS9 for
// the two behaviour-preserving comment fixes vs the original).
#include <Arduino.h>

// -- UDP "Dn"-equivalent strip payload (DIFFUSER_TURN_ON opcode) -----------
struct DnPayload {
    bool    dual;             // false = single colour, true = dual colour
    uint8_t effect;           // 0=static 1-8=animated - see EFFECT_NAMES
    uint8_t r1, g1, b1;       // primary colour   (always set)
    uint8_t r2, g2, b2;       // secondary colour (dual only)
    uint8_t brightness;       // 00-FF - colour scale factor, see applyBrightness()
    uint8_t speedMs;          // 00-FF - animated-effect frame period, ms
};

enum UdpStatusReplyMode : uint8_t {
    UDP_STATUS_NONE,
    UDP_STATUS_AFTER_TARGET,
    UDP_STATUS_AFTER_SHUTDOWN
};

enum WifiSeqPhase : uint8_t { SEQ_NONE, SEQ_MODE_STEP, SEQ_DONE };

enum ParfumPendingKind : uint8_t { PARFUM_PEND_NONE, PARFUM_PEND_ON, PARFUM_PEND_OFF };

struct StripColor { uint8_t r, g, b; };

enum StripMode : uint8_t {
    STRIP_OFF, STRIP_STATIC, STRIP_DUAL, STRIP_ANIM,
    STRIP_WIFI_CUE, STRIP_OOW_CHASE, STRIP_PARFUM_BREATHE
};
