#pragma once
// Hb.h - the 14 heartbeat-strip idle effects + dispatcher/start/end. Ported
// 1:1 from _FuZzAPP_SmartTV_R4.ino's HB namespace (~593 lines). No "ECG-
// trace" effect exists in the source - confirmed absent, see
// _rmk_docs/02_REGRESSION.md A.5; adding one is new work pending a
// reference (00_PLAN.md SS9 R11), not part of this port.
#include "Scheduler.h"

namespace HB {

void StartEffect(bool silent, bool reset, bool fast);
void EndTask();
void T_EFFECT_HB(SCHED::TaskId taskId);
void T_EFFECT_HB_1_WhiteMove(SCHED::TaskId taskId);
void T_EFFECT_HB_2_Heartbeat(SCHED::TaskId taskId);
void T_EFFECT_HB_3_RandomFade(SCHED::TaskId taskId);
void T_EFFECT_HB_4_TravelingShadow(SCHED::TaskId taskId);
void T_EFFECT_HB_5_ExpandingRaindrops(SCHED::TaskId taskId);
void T_EFFECT_HB_6_Colors(SCHED::TaskId taskId);
void T_EFFECT_HB_7_ShootingStarRandom(SCHED::TaskId taskId);
void T_EFFECT_HB_8_RandomSparklingPop(SCHED::TaskId taskId);
void T_EFFECT_HB_9_GlidingAurora(SCHED::TaskId taskId);
void T_EFFECT_HB_10_TheGlitchMatrix(SCHED::TaskId taskId);
void T_EFFECT_HB_11_StochasticPlasma(SCHED::TaskId taskId);
void T_EFFECT_HB_12_DigitalRain(SCHED::TaskId taskId);
void T_EFFECT_HB_13_DualPulse(SCHED::TaskId taskId);
void T_EFFECT_HB_14_RainbowWavePulse(SCHED::TaskId taskId);

// Registration table (replaces HB_EFFECT_HANDLERS[] in the original DEF.h) -
// one row per effect, EE_HB_EFFECT (1-14) indexes it directly (idx-1).
typedef void (*EffectHandler)(SCHED::TaskId);
extern const EffectHandler EFFECT_HANDLERS[];
extern const int EFFECT_HANDLERS_COUNT;

} // namespace HB
