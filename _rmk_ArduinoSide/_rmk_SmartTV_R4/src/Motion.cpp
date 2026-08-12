#include "Motion.h"
#include "Globals.h"
#include "Led.h"
#include "Lisens.h"
#include "Eeprom.h"
#include "AppLink.h"
#include "DifLink.h"
#include "Udpraw.h"
#include <math.h>

namespace MOTION {

/** Debounces MOTION_PIN_COM/MOTION_PIN_BED (TestMode-overridable), then on a
    confirmed trigger either starts a fresh motion-on cycle or keeps an
    already-lit cycle alive (renewing colour / arming the auto-off timer as
    configured). Also clears motAUTOOFF back to motON once BED goes idle. */
void Status() {
    if (MOTION::State.Status > motOFF) {
        bool motionOn = false;

        int simulatedCom = 0;
        int simulatedBed = 0;

        if (TestMode == _testmode_motionCom) {
            simulatedCom = 1;
        } else if (TestMode == _testmode_motionBed) {
            simulatedBed = 1;
        } else {
            simulatedCom = PinStatus(MOTION_PIN_COM);
            simulatedBed = PinStatus(MOTION_PIN_BED);
        }

        if ((simulatedCom + simulatedBed) && !MOTION::State.Trigger) {
            MOTION::State.Trigger = true;
            MOTION::State.TriggerTime = TimeNow;
        } else {
            if (MOTION::State.Trigger && (simulatedCom + simulatedBed)) {
                if (((TimeNow - MOTION::State.TriggerTime) > MOTION_TIME_FIX)) {
                    MOTION::State.Trigger = false;
                    motionOn = true;
                }
            } else {
                MOTION::State.Trigger = false;
            }
        }

        if (motionOn && ((MOTION::State.LastCheck + MOTION_CHECK_TIME) < TimeNow)) {
            if (MOTION::State.Status > motON) {
                if (!MOTION::State.AutoOffTime && simulatedBed) {
                    MOTION::State.AutoOffTime = TimeNow + ((uint32_t)EE::Get(EE_MOTION_AUTO_OFF_TIME) * 60000);
                } else if (MOTION::State.AutoOffTime && TimeNow >= MOTION::State.AutoOffTime) {
                    MOTION::State.Status = motAUTOOFF;
                    SCHED::KillAllUnlocked();
                    SCHED::TaskId offId = SCHED::AddTask("MOTION_Status", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, SCHED::Unit::MS,
                                              LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false);
                    SCHED::ScratchFor(offId).phase = 0;
                    MOTION::State.Transitioning = true;
                } else {
                    SCHED::KillAllUnlocked();
                    SCHED::TaskId offId = SCHED::AddTask("MOTION_Status", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, SCHED::Unit::MS,
                                              LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), S_TO_MS(EE::Get(EE_MOTION_ON_TIME)), false);
                    SCHED::ScratchFor(offId).phase = 0;
                    MOTION::State.Transitioning = false;

                    if (simulatedBed) {
                        if (EE::Get(EE_MOTION_RANDOM_COLOR) && ((MOTION::State.LastChangeColor + S_TO_MS(EE::Get(EE_MOTION_RENEW_COLOR_TIME))) < TimeNow)) {
                            uint16_t sumR = 0, sumG = 0, sumB = 0, litCount = 0;

                            for (int i = 0; i < LED_NUM_TOTAL; i++) {
                                if ((LED::State.CurrentColor[i].r + LED::State.CurrentColor[i].g + LED::State.CurrentColor[i].b) > 0) {
                                    LED::State.PackedRGBValue = LED::getRandomColor();
                                    LED::State.TargetColor[i].r = (LED::State.PackedRGBValue >> 16) & 255;
                                    LED::State.TargetColor[i].g = (LED::State.PackedRGBValue >> 8) & 255;
                                    LED::State.TargetColor[i].b = LED::State.PackedRGBValue & 255;

                                    sumR += LED::State.TargetColor[i].r; sumG += LED::State.TargetColor[i].g; sumB += LED::State.TargetColor[i].b;
                                    litCount++;
                                }
                            }
                            SCHED::AddTask("MOTION_Status", "T_MOTION_CHANGE_COLOR", T_MOTION_CHANGE_COLOR, SCHED::Unit::MS,
                                          LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false);
                            MOTION::State.LastChangeColor = TimeNow;

                            if (litCount > 0) {
                                const uint8_t avgR = sumR / litCount, avgG = sumG / litCount, avgB = sumB / litCount;
                                DIF_Colorx motionColor = { avgR, avgG, avgB, avgR, avgG, avgB };
                                DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);
                            }
                        }
                    } else {
                        MOTION::State.AutoOffTime = 0;
                    }

                    MOTION::State.LOG[0].epoch = RTC_TimeClient.getEpochTime();
                }
            } else {
                MOTION::State.Status = (simulatedCom ? motCOM : motBED);

                MOTION::State.LastChangeColor = TimeNow;
                LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE);

                MOTION::State.Transitioning = true;
                SCHED::KillAllUnlocked();
                SCHED::TaskId onId = SCHED::AddTask("MOTION_Status", "T_EFFECT_MOTION_ON", T_EFFECT_MOTION_ON, SCHED::Unit::MS,
                                          LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), 1, false);
                SCHED::Scratch &S0 = SCHED::ScratchFor(onId);
                S0.phase = 0; S0.paramA = 0; S0.paramB = 0;

                if (EE::Get(EE_MOTION_RANDOM_COLOR)) {
                    const uint32_t rgb = LED::getRandomColor();
                    const uint8_t r = (uint8_t)(rgb >> 16), g = (uint8_t)(rgb >> 8), b = (uint8_t)rgb;
                    DIF_Colorx motionColor = { r, g, b, r, g, b };
                    DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);
                } else {
                    DIF_Colorx motionColor = { MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b,
                                                MOTION::State.Color.r, MOTION::State.Color.g, MOTION::State.Color.b };
                    DIF::AutoOn(EE_DIF_MODE_MOTION, &motionColor);
                }

                MOTION::State.AutoOffTime = 0;
                for (int i = MOTION_LOG_INDEX_MAX - 1; i > 0; i--) {
                    MOTION::State.LOG[i] = MOTION::State.LOG[i - 1];
                }
                MOTION::State.LOG[0].epoch = RTC_TimeClient.getEpochTime();
                MOTION::State.LOG[0].TriggerBy = MOTION::State.Status;
            }

            APP::updStatus("MOTION::Status");
            MOTION::State.LastCheck = TimeNow;
        }
    } else if (MOTION::State.Status == motAUTOOFF && (TestMode == _testmode_motionBed ? false : !PinStatus(MOTION_PIN_BED))) {
        MOTION::State.AutoOffTime = 0;
        MOTION::State.Status = motON;
        APP::updStatus("MOTION::Status");
    }
}

/** Thin wrapper so MOTION_ON_HANDLERS[0] shares the same void(TaskId)
    signature as every other slot, even though the default effect itself
    (unlike every other on-effect) reports completion via a bool return
    rather than writing its own Scratch.phase. */
void T_EFFECT_MOTION_ON_Default_Wrapper(SCHED::TaskId taskId) {
    if (!T_EFFECT_MOTION_ON_Default()) {
        SCHED::ScratchFor(taskId).phase = SCHED::TASK_DONE;
    }
}

/** Motion-on master task. Phase 0: fades everything to black, then fills
    LED::State.TargetColor[] (static MOTION::State.Color or fresh random per
    EE_MOTION_RANDOM_COLOR). Phases 1+: dispatches the configured sub-effect
    each tick. On completion, chains T_EFFECT_MOTION_OFF after
    EE_MOTION_ON_TIME. */
void T_EFFECT_MOTION_ON(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);

    if (S.phase == 0) {
        bool fading = false;
        const int fadeInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));

        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            if (LED::TG_BRIGHTNESS(i, 0, fadeInc, false)) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, 0, false);
                fading = true;
            }
        }

        if (!fading) {
            const bool useRandom = EE::Get(EE_MOTION_RANDOM_COLOR);

            for (int i = 0; i < LED_NUM_TOTAL; i++) {
                if (useRandom) {
                    const uint32_t rgb = LED::getRandomColor();
                    LED::State.TargetColor[i].r = (uint8_t)(rgb >> 16);
                    LED::State.TargetColor[i].g = (uint8_t)(rgb >> 8);
                    LED::State.TargetColor[i].b = (uint8_t)rgb;
                } else {
                    LED::State.TargetColor[i].r = MOTION::State.Color.r;
                    LED::State.TargetColor[i].g = MOTION::State.Color.g;
                    LED::State.TargetColor[i].b = MOTION::State.Color.b;
                }
            }
            APP::updDeltaColors();
        }

        if (!fading) {
            S.phase = 1; S.paramA = 0; S.paramB = 0;
        }
    }
    else if (S.phase != SCHED::TASK_DONE) {
        int effect = EE::Get(EE_MOTION_ON_EFF);

        int motionEffIdx = effect;
        if (motionEffIdx < 0 || motionEffIdx >= MOTION_ON_HANDLERS_COUNT) motionEffIdx = 0;
        MOTION_ON_HANDLERS[motionEffIdx](taskId);
    }

    MOTION::State.LastCheck = TimeNow;
    LED::Show();
    LISENS::ResetTime();

    if (S.phase == SCHED::TASK_DONE) {
        APP::updColors_Force();
        S.phase = 0; S.paramA = 0; S.paramB = 0;
        MOTION::State.Transitioning = false;
        SCHED::KillAllUnlocked();
        SCHED::TaskId offId = SCHED::AddTask("T_EFFECT_MOTION_ON", "T_EFFECT_MOTION_OFF", T_EFFECT_MOTION_OFF, SCHED::Unit::MS,
                                  LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)), S_TO_MS(EE::Get(EE_MOTION_ON_TIME)), false);
        SCHED::ScratchFor(offId).phase = 0;
    }
}

/** Default motion-on sub-effect: symmetric center-out brightness bloom
    across BED (+COM unless bed-only) and HB, with optional 15%-per-step
    divide-brightness falloff.
    @return true while any pixel is still fading toward its target. */
bool T_EFFECT_MOTION_ON_Default() {
    bool active = false;

    const bool useDivide = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);
    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));

    const int divLim = (MOTION::State.Status == motBED) ? (LED_BED_NUM >> 1) : (LED_COM_NUM >> 1);

    for (int z = 0; z < 3; z++) {
        if (z == 1 && MOTION::State.Status == motBED) continue;

        int totalNum = LED::MotionZoneCount(z);
        int half = totalNum >> 1;

        for (int i = 0; i < half; i++) {
            int L = LED::MotionZonePixel(z, half - 1 - i);
            int R = LED::MotionZonePixel(z, half + i);

            int br = baseBr;
            if (useDivide) {
                int step = (i > divLim) ? divLim : i;
                br = baseBr - ((baseBr * (step * 15)) / 100);
                if (br < 0) br = 0;
            }

            if (LED::TG_BRIGHTNESS_ToTargetColor(L, br, brInc, false)) {
                active = true;
            }

            if (LED::TG_BRIGHTNESS_ToTargetColor(R, br, brInc, false)) {
                active = true;
            }
        }
    }

    return active;
}

/** Motion-on effect 1: BED (and COM unless bed-only) expand center-out one
    step at a time, HB mapped proportionally to whichever zone is master;
    optional 15%-per-step divide-brightness falloff. */
void T_EFFECT_MOTION_ON_1_FromMiddle(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    bool N = false;
    bool Done = true;
    const int step = S.paramA;

    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const int brInc  = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));

    int br = baseBr;
    if (EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS)) {
        int reduction = (baseBr * (step * 15)) / 100;
        br = baseBr - reduction;
        if (br < 0) br = 0;
    }

    const int halfBED = LED_BED_NUM >> 1;
    if (step < halfBED) {
        int idxL = LED::BED(halfBED - 1 - step);
        int idxR = LED::BED(halfBED + step);
        int pixels[2] = {idxL, idxR};
        for (int i = 0; i < 2; i++) {
            if (LED::TG_BRIGHTNESS_ToTargetColor(pixels[i], br, brInc, false)) {
                N = true;
            }
        }
        Done = false;
    }

    const int halfHB = LED_HB_NUM >> 1;
    const int halfCOM = LED_COM_NUM >> 1;
    const bool isBedOnly = (MOTION::State.Status == motBED);

    float hbRatio = isBedOnly ? ((float)LED_HB_NUM / (float)LED_BED_NUM)
                              : ((float)LED_HB_NUM / (float)LED_COM_NUM);
    int limit = isBedOnly ? halfBED : halfCOM;

    if (!isBedOnly && step < halfCOM) {
        int cIdxL = LED::COM(halfCOM - 1 - step);
        int cIdxR = LED::COM(halfCOM + step);
        int cPixels[2] = {cIdxL, cIdxR};
        for (int i = 0; i < 2; i++) {
            if (LED::TG_BRIGHTNESS_ToTargetColor(cPixels[i], br, brInc, false)) {
                N = true;
            }
        }
        Done = false;
    }

    if (step < limit) {
        int hbStart = (int)(step * hbRatio);
        int hbEnd   = (int)((step + 1) * hbRatio);

        for (int h = hbStart; h < hbEnd && h < halfHB; h++) {
            int hIdxL = LED::HB(halfHB - 1 - h);
            int hIdxR = LED::HB(halfHB + h);
            int hPixels[2] = {hIdxL, hIdxR};
            for (int i = 0; i < 2; i++) {
                if (LED::TG_BRIGHTNESS_ToTargetColor(hPixels[i], br, brInc, false)) {
                    N = true;
                }
            }
        }
        Done = false;
    }

    if (!N) {
        if (Done) {
            S.phase = SCHED::TASK_DONE;
        } else {
            APP::updDeltaColors();
            S.paramA++;
        }
    }
}

/** Motion-on effect 2: scans BED(+COM) left-to-right at 15% (or full when
    dividing), then right-to-left at full, then a final bloom to 100%; HB
    tracks the master scan proportionally. Divide-brightness mode skips the
    second pass. */
void T_EFFECT_MOTION_ON_2_LineMoving(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta = S.paramA;

    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));
    const bool isBedOnly = (MOTION::State.Status == motBED);
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);

    if (phase == 1) {
        S.paramA = 0;
        S.phase++;
    }
    else if (phase == 2 || phase == 3) {
        int masterNum = isBedOnly ? LED_BED_NUM : (LED_BED_NUM + LED_COM_NUM);
        float hbRatio = (float)LED_HB_NUM / (float)masterNum;

        if (ta < masterNum) {
            int scanIdx = (phase == 2) ? ta : (masterNum - 1 - ta);
            int currentPixel;
            if (isBedOnly) {
                currentPixel = LED::BED(scanIdx);
            } else {
                if (scanIdx < LED_BED_NUM) {
                    currentPixel = LED::BED(scanIdx);
                } else {
                    currentPixel = LED::COM(scanIdx - LED_BED_NUM);
                }
            }

            int target;
            if (useDiv) {
                int dist = abs(scanIdx - (masterNum >> 1));
                int reduction = (baseBr * (dist * 15)) / 100;
                target = baseBr - reduction;
                if (target < 0) target = 0;
            } else {
                target = (phase == 2) ? (baseBr * 15 / 100) : baseBr;
            }

            LED::State.CurrentBrightness[currentPixel] = target;

            int hbStart = (int)(scanIdx * hbRatio);
            int hbEnd   = (int)((scanIdx + 1) * hbRatio) + 3;
            for (int h = hbStart; h < hbEnd && h < LED_HB_NUM; h++) {
                int p = LED::HB(h);
                LED::State.CurrentBrightness[p] = target;
            }

            for (int i = 0; i < LED_NUM_TOTAL; i++) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, LED::State.CurrentBrightness[i], false);
            }
            LED::Show();
            S.paramA++;
        } else {
            if (useDiv && phase == 2) {
                S.phase = SCHED::TASK_DONE;
                S.paramA = 0;
            } else {
                S.phase++;
                S.paramA = 0;
            }
        }
    }
    else if (phase == 4) {
        bool N = false;
        for (int z = 0; z < 3; z++) {
            if (z == 1 && isBedOnly) continue;
            int num = LED::MotionZoneCount(z);
            for (int i = 0; i < num; i++) {
                int p = LED::MotionZonePixel(z, i);
                if (LED::TG_BRIGHTNESS_ToTargetColor(p, baseBr, brInc, false)) {
                    N = true;
                }
            }
        }
        if (!N) {
            S.phase = SCHED::TASK_DONE;
            APP::updDeltaColors();
        } else {
            LED::Show();
            APP::updDeltaColors();
        }
    }
    else {
        S.phase = SCHED::TASK_DONE;
    }
}

/** Motion-on effect 3: random-order LED-by-LED fade-in via
    LED::State.PixelOrder[], skipping LEDs outside BED/COM, with a
    proportional HB block per LED and optional divide-brightness falloff by
    distance from zone center. */
void T_EFFECT_MOTION_ON_3_Random(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);

    if (S.phase == 1) {
        if (S.paramA < LED_NUM - LED_HB_NUM_FAKE) {
            int led = LED::State.PixelOrder[S.paramA];
            bool Find = false;
            int activeIdx = -1;

            for (int i = 0; i < LED_BED_NUM; i++) {
                if (led == LED::BED(i)) {
                    Find = true; activeIdx = i; break;
                }
            }

            if (!Find && MOTION::State.Status != motBED) {
                for (int i = 0; i < LED_COM_NUM; i++) {
                    if (led == LED::COM(i)) {
                        Find = true;
                        activeIdx = LED_BED_NUM + i;
                        break;
                    }
                }
            }

            if (Find) {
                S.paramB = activeIdx;
                SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_MOTION_BR_CL_DEL)));
                S.phase = 2;
            } else {
                SCHED::SetInterval(tID, SCHED::Unit::MS, 1);
                S.paramA++;
            }
        } else {
            S.phase = SCHED::TASK_DONE;
        }
        return;
    }
    else if (S.phase == 2) {
        int mainLed = LED::State.PixelOrder[S.paramA];
        int activeIdx = S.paramB;

        const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
        const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));

        int targetBr = baseBr;
        if (EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS)) {
            int dist = 0;
            if (activeIdx < LED_BED_NUM) {
                dist = abs(activeIdx - (LED_BED_NUM >> 1));
            } else {
                int comLocalIdx = activeIdx - LED_BED_NUM;
                dist = abs(comLocalIdx - (LED_COM_NUM >> 1));
            }
            int reduction = (baseBr * (dist * 15)) / 100;
            targetBr = baseBr - reduction;
            if (targetBr < 0) targetBr = 0;
        }

        bool stillFading = false;

        if (LED::TG_BRIGHTNESS_ToTargetColor(mainLed, targetBr, brInc, false)) {
            stillFading = true;
        }

        int totalActiveMain = (MOTION::State.Status == motBED) ? LED_BED_NUM : (LED_BED_NUM + LED_COM_NUM);
        float ratio = (float)LED_HB_NUM / (float)totalActiveMain;
        int hbStart = (int)(activeIdx * ratio);
        int hbEnd   = (int)((activeIdx + 1) * ratio);

        for (int h = hbStart; h < hbEnd && h < LED_HB_NUM; h++) {
            int hbLed = LED::HB(h);
            if (LED::TG_BRIGHTNESS_ToTargetColor(hbLed, targetBr, brInc, false)) {
                stillFading = true;
            }
        }

        if (!stillFading) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, 1);
            S.paramA++;
            S.phase = 1;
            APP::updDeltaColors();
        }
        return;
    }
}

/** Motion-on effect 4: HB fast inward scan (double speed) followed by
    BED(+COM) bloom outward from center, with optional 15%-per-step
    divide-brightness falloff. */
void T_EFFECT_MOTION_ON_4_Cascade(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta = S.paramA;

    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));
    const bool isBedOnly = (MOTION::State.Status == motBED);
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);

    if (phase == 1) {
        S.paramA = 0;
        S.phase++;
    }
    else if (phase == 2) {
        const int halfHB = LED_HB_NUM >> 1;

        int headIdx = ta * 2;
        bool stillMapping = false;

        for (int i = 0; i <= headIdx && i < halfHB; i++) {
            int L = LED::HB(halfHB - 1 - i);
            int R = LED::HB(halfHB + i);
            int pixels[2] = {L, R};

            for (int s = 0; s < 2; s++) {
                if (LED::TG_BRIGHTNESS_ToTargetColor(pixels[s], baseBr, brInc, false)) {
                    stillMapping = true;
                }
            }
        }

        if (!stillMapping && headIdx >= halfHB) {
            S.phase++;
            S.paramA = 0;

            APP::updDeltaColors();
        } else {
            S.paramA++;
            LED::Show();
        }
    }
    else if (phase == 3) {
        bool N = false;
        int floorLimit = isBedOnly ? (LED_BED_NUM >> 1) : (LED_COM_NUM >> 1);

        for (int z = 0; z < 2; z++) {
            if (z == 1 && isBedOnly) continue;

            int num = LED::MotionZoneCount(z);
            int mid = num >> 1;

            for (int i = 0; i <= ta && i < mid; i++) {
                int L = LED::MotionZonePixel(z, mid - 1 - i);
                int R = LED::MotionZonePixel(z, mid + i);

                int target = baseBr;
                if (useDiv) {
                    int reduction = (baseBr * (i * 15)) / 100;
                    target = baseBr - reduction;
                    if (target < 0) target = 0;
                }

                int pix[2] = {L, R};
                for (int s = 0; s < 2; s++) {
                    if (LED::TG_BRIGHTNESS_ToTargetColor(pix[s], target, brInc, false)) {
                        N = true;
                    }
                }
            }
        }

        if (!N && ta >= floorLimit) {
            S.phase = SCHED::TASK_DONE;
        } else {
            if (ta < floorLimit) S.paramA++;
            LED::Show();
            APP::updDeltaColors();
        }
    }
    else {
        S.phase = SCHED::TASK_DONE;
    }
}

/** Motion-on effect 5: BED/COM/HB edges zip inward to center (optional
    15%-per-effective-step falloff, HB distance normalised by
    LED_HB_NUM/LED_COM_NUM so its gradient slope matches the other zones),
    then all pixels bloom to their final target. A tick-count safety cap
    (MOTION_COLLISION_BLOOM_MAX_TICKS) snaps any pixel that fails to
    converge straight to target instead of stepping it — see the inline
    comment for the production incident this fixes. */
void T_EFFECT_MOTION_ON_5_TheCollision(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta = S.paramA;

    const int baseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));
    const bool isBedOnly = (MOTION::State.Status == motBED);
    const bool useDiv = EE::Get(EE_MOTION_DIVIDE_BRIGHTNESS);

    const int hbScale = (LED_COM_NUM > 0) ? (int)round((float)LED_HB_NUM / LED_COM_NUM) : 1;

    if (phase == 1) {
        S.paramA = 0; S.phase++;
    }
    else if (phase == 2) {
        bool stillMoving = false;

        for (int z = 0; z < 3; z++) {
            if (z == 1 && isBedOnly) continue;

            int num = LED::MotionZoneCount(z);
            int half = num >> 1;

            if (ta < half) {
                int L = LED::MotionZonePixel(z, ta);
                int R = LED::MotionZonePixel(z, num - 1 - ta);

                int dist = half - 1 - ta;
                int target = baseBr;

                if (useDiv) {
                    int effectiveDist = (z == 2) ? (dist / hbScale) : dist;
                    int reduction = (baseBr * (effectiveDist * 15)) / 100;
                    target = baseBr - reduction;
                    if (target < 0) target = 0;
                }

                LED::setPixel(L, LED::State.TargetColor[L].r, LED::State.TargetColor[L].g, LED::State.TargetColor[L].b, target, false);
                LED::setPixel(R, LED::State.TargetColor[R].r, LED::State.TargetColor[R].g, LED::State.TargetColor[R].b, target, false);
                stillMoving = true;
            }
        }

        if (!stillMoving) {
            S.phase++; S.paramA = 0; S.paramB = 0;
        } else {
            S.paramA++; LED::Show();
            APP::updDeltaColors();
        }
    }
    else if (phase == 3) {
        bool N = false;
        for (int z = 0; z < 3; z++) {
            if (z == 1 && isBedOnly) continue;
            int num = LED::MotionZoneCount(z);
            int mid = num >> 1;

            for (int i = 0; i < num; i++) {
                int p = LED::MotionZonePixel(z, i);
                int dist = abs(i - mid);

                int target = baseBr;
                if (useDiv) {
                    int effectiveDist = (z == 2) ? (dist / hbScale) : dist;
                    int reduction = (baseBr * (effectiveDist * 15)) / 100;
                    target = baseBr - reduction;
                    if (target < 0) target = 0;
                }

                if (LED::TG_BRIGHTNESS_ToTargetColor(p, target, brInc, false)) {
                    N = true;
                }
            }
        }

        if (!N) {
            S.phase = SCHED::TASK_DONE;
        } else {
            S.paramB++;
            if (S.paramB > MOTION_COLLISION_BLOOM_MAX_TICKS) {
                // Safety cap: at least one pixel (confirmed live: a single HB
                // pixel, reproducibly) can fail to converge here and leave N
                // true forever. That stalls this task in an endless per-tick
                // colour-sync resend - confirmed live to both starve the
                // command socket (multi-second command delays) and leak
                // ~40-50B/s of RAM (own-buffer never freed on the repeated
                // send), eventually crashing the board. Whatever pixel hasn't
                // converged by now gets snapped straight to its target instead
                // of stepped, so this phase always terminates.
                for (int z = 0; z < 3; z++) {
                    if (z == 1 && isBedOnly) continue;
                    int num = LED::MotionZoneCount(z);
                    int mid = num >> 1;
                    for (int i = 0; i < num; i++) {
                        int p = LED::MotionZonePixel(z, i);
                        int dist = abs(i - mid);
                        int target = baseBr;
                        if (useDiv) {
                            int effectiveDist = (z == 2) ? (dist / hbScale) : dist;
                            int reduction = (baseBr * (effectiveDist * 15)) / 100;
                            target = baseBr - reduction;
                            if (target < 0) target = 0;
                        }
                        LED::State.CurrentBrightness[p] = target;
                        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, target, false);
                    }
                }
                LED::Show();
                APP::updDeltaColors();
                S.phase = SCHED::TASK_DONE;
            } else {
                LED::Show();
                APP::updDeltaColors();
            }
        }
    }
}

/** Off-fade sequencer: dims HB -> COM -> BED in order (LAMP excluded),
    each zone reaching zero before the next starts. On completion, restores
    MOTION::State.Status to motON (unless motAUTOOFF) and turns the
    diffuser off if no other source is active. */
void T_EFFECT_MOTION_OFF(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);

    if (S.phase != SCHED::TASK_DONE) {
        MOTION::State.Transitioning = true;
        const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));
        bool zoneStillActive = false;

        int startIdx = 0;
        int endIdx = 0;

        if (S.phase == 0) {
            startIdx = 0;
            endIdx = LED_HB_NUM;
        } else if (S.phase == 1) {
            startIdx = 0;
            endIdx = LED_COM_NUM;
        } else if (S.phase == 2) {
            startIdx = 0;
            endIdx = LED_BED_NUM;
        }

        for (int i = startIdx; i < endIdx; i++) {
            int targetLed;

            if (S.phase == 0)      targetLed = LED::HB(i);
            else if (S.phase == 1) targetLed = LED::COM(i);
            else                   targetLed = LED::BED(i);

            if (LED::TG_BRIGHTNESS_ToCurrentColor(targetLed, 0, brInc, false)) {
                zoneStillActive = true;
            }
        }

        if (S.phase > 0) APP::updDeltaColors();

        if (!zoneStillActive) {
            if (S.phase == 0) {
                APP::updDeltaColors();
            }

            S.phase++;
            if (S.phase > 2) {
                S.phase = SCHED::TASK_DONE;
            }
        }
    }

    if (MOTION::State.Status != motAUTOOFF) {
        MOTION::State.Status = motON;
        MOTION::State.AutoOffTime = 0;
    }

    LED::Show();
    LISENS::ResetTime();

    if (S.phase == SCHED::TASK_DONE) {
        APP::updStatus("LED::T_EFFECT_MOTION_OFF");
        APP::updDeltaColors();
        MOTION::State.Transitioning = false;
        DIF::AutoOff();
        SCHED::KillAllUnlocked();
    }
}

/** Smoothly transitions every LED toward LED::State.TargetColor[]; self-kills
    once no pixel needs further stepping. */
void T_MOTION_CHANGE_COLOR(SCHED::TaskId taskId) {
    bool N = false;
    const int limit = LED_NUM_TOTAL;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_MOTION_BR_CL_INC));

    for (int ledN = 0; ledN < limit; ledN++) {
        if (LED::TG_COLOR(ledN, LED::State.TargetColor[ledN].r, LED::State.TargetColor[ledN].g, LED::State.TargetColor[ledN].b, brInc)) {
            LED::setPixel(ledN,
                        LED::State.CurrentColor[ledN].r,
                        LED::State.CurrentColor[ledN].g,
                        LED::State.CurrentColor[ledN].b,
                        LED::State.CurrentBrightness[ledN],
                        false);

            N = true;
        }
    }

    if (N) {
        LED::Show();
    } else {
        SCHED::KillId(taskId);
        APP::updDeltaColors();
    }
}

/** Live lux-adaptation nudge for motion's currently-lit zone (HB/COM/BED).
    Only ever armed by LISENS::setLux() while MOTION::State.Transitioning is
    false. Flat-targets every currently-lit pixel to the fresh
    getLuxBrightness(EE_MOTION_BRIGHTNESS) ceiling rather than reconstructing
    each on-effect's own falloff shape (picked back up on the next real
    on-effect/off-fade). Deliberately a scoped self-kill only, never
    KillAllUnlocked() - see 00_PLAN.md/02_REGRESSION.md's account of the
    "motion stuck lit" incidents this design fixes. */
void T_MOTION_LUX_BR_CHANGE(SCHED::TaskId taskId) {
    if (MOTION::State.Transitioning) {
        SCHED::KillId(taskId);
        return;
    }

    const int  brInc     = EE::Get(EE_MOTION_BR_CL_INC);
    const int  newBaseBr = LED::getLuxBrightness(EE::Get(EE_MOTION_BRIGHTNESS));
    const bool isBedOnly = (MOTION::State.Status == motBED);
    bool anyChanged = false;

    for (int z = 0; z < 3; z++) {
        if (z == 1 && isBedOnly) continue;
        const int totalNum = LED::MotionZoneCount(z);

        for (int i = 0; i < totalNum; i++) {
            const int p = LED::MotionZonePixel(z, i);
            if (LED::State.CurrentBrightness[p] == 0) continue;

            if (LED::TG_BRIGHTNESS_ToCurrentColor(p, newBaseBr, brInc, false)) {
                anyChanged = true;
            }
        }
    }

    if (anyChanged) {
        LED::Show();
    } else {
        SCHED::KillId(taskId);
    }
}

/** Reads one motion sensor pin. MOTION_PIN_COM is active-HIGH,
    MOTION_PIN_BED is active-LOW (inverted). */
uint8_t PinStatus(int p) {
    if (p == MOTION_PIN_COM) {
        return (digitalRead(MOTION_PIN_COM)) ? 1 : 0;
    } else
    if (p == MOTION_PIN_BED) {
        return (!digitalRead(MOTION_PIN_BED)) ? 1 : 0;
    }

    return 0;
}

const EffectHandler MOTION_ON_HANDLERS[] = {
    T_EFFECT_MOTION_ON_Default_Wrapper,
    T_EFFECT_MOTION_ON_1_FromMiddle,
    T_EFFECT_MOTION_ON_2_LineMoving,
    T_EFFECT_MOTION_ON_3_Random,
    T_EFFECT_MOTION_ON_4_Cascade,
    T_EFFECT_MOTION_ON_5_TheCollision
};
const int MOTION_ON_HANDLERS_COUNT = sizeof(MOTION_ON_HANDLERS) / sizeof(MOTION_ON_HANDLERS[0]);

} // namespace MOTION
