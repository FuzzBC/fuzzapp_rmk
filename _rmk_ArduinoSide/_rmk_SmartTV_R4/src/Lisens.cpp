#include "Lisens.h"
#include "Globals.h"
#include "Led.h"
#include "Eeprom.h"
#include "Tv.h"
#include "Motion.h"
#include "Udpraw.h"
#include "DifLink.h"
#include "AppLink.h"
#include "Debug.h"

namespace LISENS {

void Setup() {
    analogReadResolution(12);

    uint16_t sensorValue = analogRead(LIGHT_SENSOR_PIN);
    LISENS::State.Average = sensorValue;

    int initialLux = 1;
    const int numThresholds = 3;
    for (int i = 0; i < numThresholds; i++) {
        if (sensorValue < LIGHT_SENS_LUX[i]) { initialLux = i + 1; break; }
    }
    if (sensorValue >= LIGHT_SENS_LUX[numThresholds - 1]) initialLux = numThresholds + 1;
    LISENS::State.Lux = initialLux;

    LISENS::State.TaskID = SCHED::AddTask("LISENS_Setup", "LISENS_Check", Check, SCHED::Unit::MS,
                                            (LIGHT_SENSOR_CHECK_TIME / LIGHT_SENSOR_SAMPLES), 0, true);
}

/** Accumulates ADC samples; when a full window completes, compares the
    average against the LIGHT_SENS_LUX[] threshold table with hysteresis,
    then arms whichever active source's lux-driven transition applies. */
void Check(SCHED::TaskId taskId) {
    (void)taskId;
    uint16_t sensorValue = analogRead(LIGHT_SENSOR_PIN);
    LISENS::State.SampleSum += sensorValue;
    LISENS::State.SampleCount++;

    if (LISENS::State.SampleCount >= LIGHT_SENSOR_SAMPLES) {
        LISENS::State.Average = LISENS::State.SampleSum / LISENS::State.SampleCount;

        const int avg = LISENS::State.Average;
        const int fix = LIGHT_SENSOR_LUX_FIX;
        const int numThresholds = 3;

        int nowLux = LISENS::State.Lux;
        int rawTarget = 1;

        // -- PHASE 1: raw bracket --
        if (avg >= LIGHT_SENS_LUX[numThresholds - 1]) {
            rawTarget = numThresholds + 1;
        } else {
            for (int i = 0; i < numThresholds; i++) {
                if (avg < LIGHT_SENS_LUX[i]) { rawTarget = i + 1; break; }
            }
        }

        // -- PHASE 2: hysteresis --
        if (rawTarget > LISENS::State.Lux) {
            int thresholdIdx = LISENS::State.Lux - 1;
            if (avg >= (LIGHT_SENS_LUX[thresholdIdx] + fix)) nowLux = rawTarget;
        } else if (rawTarget < LISENS::State.Lux) {
            int dropIdx = rawTarget - 1;
            int multiplier = rawTarget;
            int dropLimit = LIGHT_SENS_LUX[dropIdx] - (fix * multiplier);
            if (avg <= dropLimit) nowLux = rawTarget;
        }

        // -- PHASE 3: apply --
        // Held while the app forces a lux level (SET_TEST_LUX) - the sensor
        // must not override the test value until the test-mode timeout releases it.
        DLOGV_LISENS("avg=[%d] rawTarget=[%d] nowLux=[%d] (was [%d])", avg, rawTarget, nowLux, LISENS::State.Lux);
        if (TestMode != _testmode_lux) setLux(nowLux);
        APP::updLux();

        LISENS::State.SampleSum = 0;
        LISENS::State.SampleCount = 0;
    }
}

/** The single choke point for every lux change. Saves the new level (which
    drives LED::getLuxAdaptFactor() for the TV/MOTION event animations),
    arms UDPRAW/TV/MOTION's own lux-driven transition at the RAW EE speed
    (T_UDPRAW_SET_COLOR / T_LUX_BR_CHANGE are deliberately not speed-
    adapted), and re-pushes the lux-compensated diffuser brightness. */
void setLux(int newLux) {
    if (LISENS::State.Lux == newLux) return;
    DLOG_LISENS("lux [%d] -> [%d]", LISENS::State.Lux, newLux);
    LISENS::State.Lux = newLux;

    if (LED::State.Enabled) {
        if (UDPRAW::State.Status) {
            // Re-armed on every lux change, same as the colour/brightness
            // app-command paths - the non-TV zones should reflect lux
            // updates immediately, same as they already do for a manual
            // change while streaming. The per-packet TV zone path stays
            // untouched and unthrottled.
            SCHED::KillAllUnlocked();
            SCHED::AddTask("LISENS_Change_UDPRAW", "T_UDPRAW_SET_COLOR", UDPRAW::T_UDPRAW_SET_COLOR,
                          SCHED::Unit::MS, EE::Get(EE_UDPRAW_BR_CL_DEL), 0, false);
        } else if (TV::State.Status) {
            if (TV::State.Transitioning) {
                // T_EFFECT_TV_ON/_OFF is still running (most commonly right
                // after the TV switches on, itself an ambient-light change
                // that can trigger this same lux re-arm mid-fade).
                // KillAllUnlocked() kills every unlocked task, and the TV
                // transition runs unlocked - without this guard the
                // transition would be killed mid-fade, leaving HB and the
                // TV strip stuck at a partial brightness. Lux is already
                // updated above, so the next re-arm once the transition
                // finishes picks up the current value correctly.
            } else {
                SCHED::KillAllUnlocked();
                SCHED::AddTask("LISENS_Change_TV", "T_LUX_BR_CHANGE", LED::T_LUX_BR_CHANGE,
                              SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL), 0, false);
            }
        } else if (MOTION::State.Status == motCOM || MOTION::State.Status == motBED || MOTION::State.Status == motAUTOOFF) {
            // Lit-and-holding only. Only fires while MOTION::State.Transitioning
            // is false (no on/off animation currently claiming these pixels),
            // and re-checks that flag every tick of its own rather than
            // tracking a task id across motion's kill sites (that tracked-id
            // approach caused two real production bugs - see 02_REGRESSION.md).
            if (MOTION::State.Transitioning) {
                // skip - a real transition owns these pixels right now
            } else if (SCHED::IsTaskNameActive("T_MOTION_LUX_BR_CHANGE")) {
                // Already running - it re-reads LISENS::State.Lux fresh every
                // tick, so it picks up this new value on its own next step.
                // Deliberately not killed here: killing unconditionally on
                // every lux change used to also kill an unrelated, not-yet-
                // fired T_EFFECT_MOTION_OFF sitting in the same table
                // (confirmed live: motion stuck lit).
            } else {
                SCHED::AddTask("LISENS_Change_MOTION", "T_MOTION_LUX_BR_CHANGE", MOTION::T_MOTION_LUX_BR_CHANGE,
                              SCHED::Unit::MS, EE::Get(EE_MOTION_BR_CL_DEL), 0, false);
            }
        }
    }

    // Lux already updated above, so DIF::TurnOn()'s own LED::getLuxBrightness()
    // call picks up the new level immediately instead of waiting for the next
    // auto-on trigger.
    DIF::PushLiveIfActive();
}

/** Reset the sample accumulator and reschedule the sampling task from now -
    call after any LED brightness transition to stop a stale reading from
    triggering an unwanted lux change. */
void ResetTime() {
    LISENS::State.SampleSum = 0;
    LISENS::State.SampleCount = 0;
    SCHED::ResetTime(LISENS::State.TaskID);
}

} // namespace LISENS
