#pragma once
// Tv.h - TV power-pin detection, on/off master tasks, 12 on-effects, 4 HB
// on-transition helpers (distinct from Hb.cpp's 14 IDLE effects - these run
// only during the TV-on animation), and 8 off-effects. Ported 1:1 from
// _FuZzAPP_SmartTV_R4.ino's TV namespace (~2065 lines).
//
// The #ifdef ENABLE_LOG_ANIME_INFO / PRNT::_print(...) diagnostic blocks
// present throughout the original are dropped, matching the precedent set
// in Led.cpp's port - they were debug-only (compiled out by default) and
// carry no externally observable behaviour (00_PLAN.md SS9, 02_REGRESSION.md
// fidelity bar covers observable behaviour only).
#include "Scheduler.h"

namespace TV {

void Status();
void On();
void Off();
void LogPush(bool event, int pinBefore, int pinAtTrigger);

void T_EFFECT_TV_ON(SCHED::TaskId taskId);
void T_EFFECT_TV_ON_Default(SCHED::TaskId tID);
void T_EFFECT_TV_ON_1_RandomStatic(SCHED::TaskId tID);
void T_EFFECT_TV_ON_2_MidToOutSep(SCHED::TaskId tID);
void T_EFFECT_TV_ON_3_MidToOutAll(SCHED::TaskId tID);
void T_EFFECT_TV_ON_4_5_HalfRun(SCHED::TaskId tID);
void T_EFFECT_TV_ON_6_7_MidToExt(SCHED::TaskId tID);
void T_EFFECT_TV_ON_8_ComEffect(SCHED::TaskId tID);
void T_EFFECT_TV_ON_9_QuadPointHB(SCHED::TaskId tID);
void T_EFFECT_TV_ON_10_LiquidFill(SCHED::TaskId tID);
void T_EFFECT_TV_ON_11_PixelBoot(SCHED::TaskId tID);

// HB on-transition helpers - run only while T_EFFECT_TV_ON is active,
// dispatched by HB_ON_HANDLERS[] alongside the TV sub-effect each tick.
void T_EFFECT_H_FadeOn(SCHED::TaskId tID);
void T_EFFECT_H_CenterBloom(SCHED::TaskId tID);
void T_EFFECT_H_LinearSweep(SCHED::TaskId tID);
void T_EFFECT_H_QuadPoint(SCHED::TaskId tID);

void T_EFFECT_TV_OFF(SCHED::TaskId taskId);
void T_EFFECT_TV_OFF_Default(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_1_DelayWTvOff(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_2_DelayAll(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_3_SlowTvSequential(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_4_5_Countdown(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_6_RandomHalf(SCHED::TaskId tID);
void T_EFFECT_TV_OFF_7_QuadPointHB(SCHED::TaskId tID);

// Registration tables (replace TV_ON_HANDLERS[]/HB_ON_HANDLERS[]/
// TV_OFF_HANDLERS[] in the original DEF.h).
typedef void (*EffectHandler)(SCHED::TaskId);
extern const EffectHandler TV_ON_HANDLERS[];
extern const int TV_ON_HANDLERS_COUNT;
extern const EffectHandler HB_ON_HANDLERS[];
extern const int HB_ON_HANDLERS_COUNT;
extern const EffectHandler TV_OFF_HANDLERS[];
extern const int TV_OFF_HANDLERS_COUNT;

} // namespace TV
