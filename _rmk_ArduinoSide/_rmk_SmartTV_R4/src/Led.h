#pragma once
// Led.h - zone index mapping, pixel buffer ops, change tracking, lux
// compensation, and the generic LED transition tasks. Ported from
// _FuZzAPP_SmartTV_R4.ino's LED namespace (~1467 lines).
//
// TASK.Phase/ParamA/ParamB -> per-task Scratch: every transition task below
// takes SCHED::TaskId and binds `SCHED::Scratch &S = SCHED::ScratchFor(id);`
// as its first line, then uses S.phase/S.paramA/S.paramB where the original
// used the single shared global TASK.Phase/ParamA/ParamB - see Scheduler.h's
// header comment for why (00_PLAN.md SS1/SS9 R14, the #1 fragility fix).
#include <Arduino.h>
#include "Scheduler.h"

struct CRGB {
    uint8_t r, g, b;
    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t _r, uint8_t _g, uint8_t _b) : r(_r), g(_g), b(_b) {}
};
struct CHSV {
    uint8_t h, s, v;
    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t _h, uint8_t _s, uint8_t _v) : h(_h), s(_s), v(_v) {}
};

namespace LED {

// -- zone index helpers --------------------------------------------------
uint16_t TV(uint8_t i);
uint16_t COM(uint8_t i);
uint16_t UCOM(uint8_t i);
uint16_t BED(uint8_t i);
uint16_t LAMP(uint8_t i);
uint16_t HB(uint16_t i);
bool IsHB(int l);
bool IsSelected(int l);

// -- setup / refresh -------------------------------------------------------
void Setup();
void Refresh(SCHED::TaskId id);
void Refresh(uint32_t now);   // hardware-push overload, called directly from loop()
void Show();
void ForceOffAndKillTasks(const char *killTag);

// -- core pixel ops ----------------------------------------------------------
void setPixel(int p, int r, int g, int b, int brVal, bool s);
void setAll(int r, int g, int b, int br);
void Select(int l, bool state);
bool TG_Step(uint8_t &current, uint8_t target, uint8_t inc);
bool TG_BRIGHTNESS(int led, uint8_t brVal, uint8_t inc, bool lisensReset = true);
bool TG_BRIGHTNESS_ToTargetColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset = true);
bool TG_BRIGHTNESS_ToCurrentColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset = true);
bool TG_BRIGHTNESS_ToStoredColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset = true);
bool TG_COLOR(int led, int r, int g, int b, int inc, bool lisensReset = true);
bool TG_TEMPCOLOR(int led, int r, int g, int b, int inc, bool lisensReset = true);
bool FadeAllToZero(uint8_t brInc);

// -- zone colour helpers --------------------------------------------------
void setRandomColor(bool z);
void UDPRAW_SetColor(int r, int g, int b, int br);
void MOTION_SetColor(int r, int g, int b, int br);
void setDualColorMapping(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2);
void setColorToSelected(uint8_t r, uint8_t g, uint8_t b);
void setBrightnessToSelected(uint8_t br);
void getHarmoniousPair(CHSV &outA, CHSV &outB);
uint32_t getRandomColor();
void HsvToRgb(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b);

// -- lux compensation -------------------------------------------------------
uint8_t getLuxBrightness(int b);
uint16_t getLuxAdaptFactor();
uint16_t getLuxAdaptDelay(uint16_t del);
uint16_t getLuxAdaptInc(uint16_t inc);

// -- change tracking (delta sync) --------------------------------------------
void MarkChanged(int ledIdx);
void MarkChangedRange(int start, int end);
uint8_t getChangedCount();
bool IsChanged(int ledIdx);

// -- HB helpers --------------------------------------------------------------
int HB_GetBaseBr();
void HB_SetAll(uint8_t r, uint8_t g, uint8_t b, uint8_t br, bool show = false);
void HB_SetFromPreColor(uint8_t br, bool show = false);
void HB_SetPixelFromPreColor(int i, uint8_t br);

// -- strip buffer plumbing ----------------------------------------------------
void H_writeStripPixel(int gIdx, uint8_t r, uint8_t g, uint8_t b);
CRGB H_readStripPixel(int gIdx);
bool ShouldRefresh(uint32_t now);
uint16_t getFpsLimitMs();
void shuffleArray(uint8_t *array, int size);

// -- motion zone helpers (shared by Motion.cpp's effects) ---------------------
int MotionZoneCount(int z);
uint16_t MotionZonePixel(int z, uint16_t i);

// -- transition tasks ----------------------------------------------------------
void T_LUX_BR_CHANGE(SCHED::TaskId id);
void T_SMOOTH_CHANGE(SCHED::TaskId id);
void T_DUAL_COLOR(SCHED::TaskId id);
void T_SHAKE_DUAL_COLOR(SCHED::TaskId id);
void T_AMBIENT_MODE_ON(SCHED::TaskId id);
void T_LEDS_TO_OFF(SCHED::TaskId id);

// -- state (extern, defined in Globals.cpp - see Globals.h) -------------------

} // namespace LED
