#include "Scheduler.h"
#include "Globals.h"
#include "AppLink.h"
#include <Arduino.h>
#include <string.h>

namespace SCHED {

struct TaskSlot {
    bool     active   = false;
    bool     locked   = false;
    bool     oneShot  = false;
    const char *name  = nullptr;
    TaskFunc fn        = nullptr;
    uint32_t intervalMs = 0;
    uint32_t dueAt      = 0;
    Scratch  scratch    = {};
};

static TaskSlot  g_slots[MAX_TASKS];
static uint8_t   g_taskCount = 0;
static uint8_t   g_maxTaskCountEverSeen = 0;
static uint32_t  g_totalTasksCreated = 0;

static inline uint32_t toMs(Unit unit, uint32_t v) {
    return (unit == Unit::S) ? v * 1000UL : v;
}

TaskId AddTask(const char *source, const char *name, TaskFunc fn,
               Unit unit, uint32_t interval, uint32_t startDelay, bool locked) {
    for (TaskId i = 0; i < MAX_TASKS; i++) {
        if (g_slots[i].active) continue;
        TaskSlot &s = g_slots[i];
        s.active     = true;
        s.locked     = locked;
        s.name       = name;
        s.fn         = fn;
        s.intervalMs = toMs(unit, interval);
        s.oneShot    = (interval == 0);
        s.dueAt      = millis() + toMs(unit, startDelay);
        s.scratch    = Scratch{};

        g_taskCount++;
        g_totalTasksCreated++;
        if (g_taskCount > g_maxTaskCountEverSeen) g_maxTaskCountEverSeen = g_taskCount;
        DLOGV_TASK("id=[%d] name=[%s] src=[%s] interval=[%lu] ms locked=[%d] count=[%d]",
            (int)i, name, source, (unsigned long)s.intervalMs, (int)locked, (int)g_taskCount);
        return i;
    }
    DLOG_TASK("table FULL - could not register [%s] requested by [%s]", name, source);
    return TASK_ID_NONE;  // table full - caller should treat like a failed registration
}

void KillId(TaskId id) {
    if (id == TASK_ID_NONE || id >= MAX_TASKS || !g_slots[id].active) return;
    DLOGV_TASK("id=[%d] name=[%s]", (int)id, g_slots[id].name ? g_slots[id].name : "?");
    g_slots[id].active = false;
    g_slots[id].fn = nullptr;
    if (g_taskCount > 0) g_taskCount--;
}

void KillAllUnlocked() {
    for (TaskId i = 0; i < MAX_TASKS; i++) {
        if (g_slots[i].active && !g_slots[i].locked) KillId(i);
    }
}

void ResetTime(TaskId id) {
    if (id == TASK_ID_NONE || id >= MAX_TASKS || !g_slots[id].active) return;
    g_slots[id].dueAt = millis() + g_slots[id].intervalMs;
}

void SetInterval(TaskId id, Unit unit, uint32_t interval) {
    if (id == TASK_ID_NONE || id >= MAX_TASKS || !g_slots[id].active) return;
    g_slots[id].intervalMs = toMs(unit, interval);
}

bool IsTaskNameActive(const char *name) {
    for (TaskId i = 0; i < MAX_TASKS; i++) {
        if (g_slots[i].active && g_slots[i].name && strcmp(g_slots[i].name, name) == 0) return true;
    }
    return false;
}

Scratch& ScratchFor(TaskId id) {
    static Scratch dummy;  // safety net for a stale/invalid id - never written back to a real task
    if (id == TASK_ID_NONE || id >= MAX_TASKS) return dummy;
    return g_slots[id].scratch;
}

void RunTasks(uint32_t now) {
    for (TaskId i = 0; i < MAX_TASKS; i++) {
        TaskSlot &s = g_slots[i];
        if (!s.active) continue;
        if ((int32_t)(now - s.dueAt) < 0) continue;

        bool oneShot = s.oneShot;
        TaskFunc fn = s.fn;
        if (!oneShot) s.dueAt = now + s.intervalMs;   // re-arm before calling - fn may kill/re-add itself

        if (fn) fn(i);

        if (oneShot && g_slots[i].active && g_slots[i].fn == fn) KillId(i);  // still us - fn didn't already self-kill/replace
    }
}

uint8_t TaskCount() { return g_taskCount; }
uint8_t MaxTaskCountEverSeen() { return g_maxTaskCountEverSeen; }
uint32_t TotalTasksCreated() { return g_totalTasksCreated; }

TaskId TaskIdAt(uint8_t slot) {
    uint8_t seen = 0;
    for (TaskId i = 0; i < MAX_TASKS; i++) {
        if (!g_slots[i].active) continue;
        if (seen == slot) return i;
        seen++;
    }
    return TASK_ID_NONE;
}

const char* TaskName(TaskId id) {
    if (id == TASK_ID_NONE || id >= MAX_TASKS || !g_slots[id].active) return "?";
    return g_slots[id].name ? g_slots[id].name : "?";
}

} // namespace SCHED
