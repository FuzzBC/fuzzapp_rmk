#pragma once
// Scheduler.h - minimal cooperative task scheduler, replacing the external
// TaskJockeyMod dependency (see _rmk_docs/00_PLAN.md SS9 R10: its internals
// were never reviewed - task-count limits, RAM footprint, and AddTask
// failure modes were all unknowns this 32KB-budget rewrite shouldn't
// inherit blind).
//
// Fixes the #1 architectural fragility the audit found (00_PLAN.md SS1,
// SS9 R14): the original firmware shares ONE global TASK/HB::State step
// struct across nearly every animation/transition function, relying
// entirely on every effect-start site remembering to call
// KillTasksAvoidLocked() first. That contract was violated repeatedly in
// production (stuck-lit motion, brightness climbing back after a test,
// LEDs going silently black - see the changelog trail). Here, every task
// gets its OWN scratch slot (indexed by TaskId), so two tasks literally
// cannot stomp each other's step state even if the kill-discipline is ever
// violated again.
//
// Callback signature intentionally kept as void(*)(TaskId) - same shape as
// the original - to keep the ported effect bodies close to their source;
// each callback's first line binds a local reference to its own scratch via
// Scratch(id) instead of touching a bare global, which is the only change
// most effect functions need (TASK.Phase -> S.phase etc).
#include <stdint.h>

namespace SCHED {

typedef uint8_t TaskId;
constexpr TaskId TASK_ID_NONE = 255;
constexpr uint8_t MAX_TASKS = 32;

enum class Unit : uint8_t { MS, S };

// Sentinel phase value effect state-machines set on their own Scratch.phase
// (or on HB::State.Phase) to mean "this animation has finished" - ported
// unchanged from the original TASK_Status::taskDone enumerator.
constexpr int32_t TASK_DONE = 99;

typedef void (*TaskFunc)(TaskId id);

// Per-task private step state - replaces the shared TASK/HB::State globals.
// Sized to the largest variant any ported effect actually needs (today:
// three ints, matching the original TASKx{Phase,ParamA,ParamB}).
struct Scratch {
    int32_t phase;
    int32_t paramA;
    int32_t paramB;
};

// Register a new repeating or one-shot task.
// @param interval  Repeat interval (0 = run once, then auto-remove).
// @param startDelay  Delay before the first execution.
// @param locked  If true, survives KillAllUnlocked().
// @return Task id, or TASK_ID_NONE if the table is full (see MAX_TASKS).
TaskId AddTask(const char *source, const char *name, TaskFunc fn,
               Unit unit, uint32_t interval, uint32_t startDelay, bool locked);

void KillId(TaskId id);
void KillAllUnlocked();
void ResetTime(TaskId id);              // restart the interval countdown from now
void SetInterval(TaskId id, Unit unit, uint32_t interval);
bool IsTaskNameActive(const char *name);
Scratch& ScratchFor(TaskId id);         // this task's private step state
void RunTasks(uint32_t now);            // call once per loop() iteration

// Diagnostics (mirrors the debug info TaskJockeyMod exposed - see PRNT::_Debug's _debug_task target).
uint8_t TaskCount();
uint8_t MaxTaskCountEverSeen();
uint32_t TotalTasksCreated();
TaskId TaskIdAt(uint8_t slot);
const char* TaskName(TaskId id);

} // namespace SCHED
