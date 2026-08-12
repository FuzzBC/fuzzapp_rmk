#pragma once
// Motion.h - PIR detection/debounce, motion-on master task + 6 on-effects
// (index 0 is a thin wrapper around the bool-returning Default effect,
// matching the original's file-local wrapper), the off-fade sequencer, and
// two standalone colour/lux-nudge tasks. Ported 1:1 from
// _FuZzAPP_SmartTV_R4.ino's MOTION namespace (~1224 lines).
#include "Scheduler.h"

namespace MOTION {

void Status();
uint8_t PinStatus(int p);

void T_EFFECT_MOTION_ON(SCHED::TaskId taskId);
bool T_EFFECT_MOTION_ON_Default();
void T_EFFECT_MOTION_ON_Default_Wrapper(SCHED::TaskId taskId);
void T_EFFECT_MOTION_ON_1_FromMiddle(SCHED::TaskId tID);
void T_EFFECT_MOTION_ON_2_LineMoving(SCHED::TaskId tID);
void T_EFFECT_MOTION_ON_3_Random(SCHED::TaskId tID);
void T_EFFECT_MOTION_ON_4_Cascade(SCHED::TaskId tID);
void T_EFFECT_MOTION_ON_5_TheCollision(SCHED::TaskId tID);

void T_EFFECT_MOTION_OFF(SCHED::TaskId taskId);
void T_MOTION_CHANGE_COLOR(SCHED::TaskId taskId);
void T_MOTION_LUX_BR_CHANGE(SCHED::TaskId taskId);

// Registration table (replaces MOTION_ON_HANDLERS[] in the original DEF.h).
typedef void (*EffectHandler)(SCHED::TaskId);
extern const EffectHandler MOTION_ON_HANDLERS[];
extern const int MOTION_ON_HANDLERS_COUNT;

} // namespace MOTION
