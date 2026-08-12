#pragma once
// Strip.h - WS2812 rendering, effects, colour helpers. Ported 1:1 from
// _FuZzAPP_Diffuser.ino's STRIP namespace.
#include "Types.h"

namespace STRIP {

void applyDnPayload(const DnPayload &p);
const char* effectName(uint8_t ee);
void startOowAlert();
void startParfumBreathe();
void stripApplyDual(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
void stripApplyEffectNext();
void stripApplyEffectSet(uint8_t n);
void stripApplyStatic(uint8_t r, uint8_t g, uint8_t b);
void stripOff();
uint8_t stripStatusCode();
const char* stripStatusName(uint8_t code);
void tickStripEffect();
void tickStripFade();
void tickStripOowChase();
void tickStripParfumBreathe();
void tickStripWifiCue();

} // namespace STRIP
