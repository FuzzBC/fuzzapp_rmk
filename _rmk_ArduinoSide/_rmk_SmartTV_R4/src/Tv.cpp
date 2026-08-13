#include "Tv.h"
#include "Globals.h"
#include "Led.h"
#include "Lisens.h"
#include "Eeprom.h"
#include "Hb.h"
#include "AppLink.h"
#include "DifLink.h"
#include "Motion.h"
#include "Udpraw.h"
#include <string.h>

namespace TV {

/** Reads TV_PIN each loop and fires On()/Off() on a stable, debounced
    transition. TestMode _testmode_tvOn/_testmode_tvOff override the pin
    reading. Skips dispatch while UDPRAW owns the strip. */
void Status() {
    TV::State.PrevPinValue = TV::State.PinValue;

    TV::State.PinValue = analogRead(TV_PIN) / TV_ADC_DIVIDER;
    DLOGV_TV("raw pin [%d] -> scaled [%d]", TV::State.PinValue * TV_ADC_DIVIDER, TV::State.PinValue);

    if (TestMode == _testmode_tvOn) {
        TV::State.PinValue = TV_TEST_PIN_ON;
    } else if (TestMode == _testmode_tvOff) {
        TV::State.PinValue = TV_TEST_PIN_OFF;
    }

    TV::State.ReadPin = true;
    if (TV::State.PinValue >= 15) {
        TV::State.ReadPin = false;
    }

    if (TV::State.ReadPin != TV::State.LastStatus) {
        TV::State.LastDebounceTime = TimeNow;
    }

    if ((TV::State.LastDebounceTime + TV_DEBOUNCE_TIME) < TimeNow) {
        if ((TV::State.LastReadStatus + TV_READ_STATUS_TIME) < TimeNow) {
            if (TV::State.ReadPin != TV::State.Status) {
                DLOG_TV("debounced status change [%s] -> [%s]",
                    TV::State.Status ? "ON" : "OFF", TV::State.ReadPin ? "ON" : "OFF");
                TV::State.Status = TV::State.ReadPin;

                if (!UDPRAW::State.Status) {
                    if (TV::State.Status) {
                        On();
                    } else {
                        Off();
                    }
                }
            }

            TV::State.LastReadStatus = TimeNow;
        }
    }

    TV::State.LastStatus = TV::State.ReadPin;
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* TV ON / OFF EFFECT TASKS  * * * * * * * * * * */
/* * * * * * * * * * * * * * * * * * * * * * * * */

/** TV-on master task: fades to black, then dispatches the configured TV
    sub-effect and HB helper back-to-back each tick until both report
    SCHED::TASK_DONE, then hands off to HB::StartEffect(). */
void T_EFFECT_TV_ON(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);

    if (S.phase == 0) {
        const int increment = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
        bool anyChanged = LED::FadeAllToZero(increment);

        if (anyChanged) {
            LED::Show();
        } else {
            S.phase = 1; S.paramA = 0; S.paramB = 0;
            HB::State.Phase = 1; HB::State.ParamA = 0;

            SCHED::SetInterval(taskId, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)));
            APP::updDeltaColors();
        }
        return;
    }

    if (S.phase != SCHED::TASK_DONE) {
        int tvEffIdx = EE::Get(EE_TV_ON_EFF);
        if (tvEffIdx < 0 || tvEffIdx >= TV_ON_HANDLERS_COUNT) tvEffIdx = 0;

        if (tvEffIdx == 6) S.paramB = 0;
        else if (tvEffIdx == 7) S.paramB = 1;
        else if (tvEffIdx == 8) S.paramB = 220;

        TV_ON_HANDLERS[tvEffIdx](taskId);
    }

    if (HB::State.Phase != SCHED::TASK_DONE) {
        int hbEffIdx = EE::Get(EE_TV_ON_HB_EFF);
        if (hbEffIdx < 0 || hbEffIdx >= HB_ON_HANDLERS_COUNT) hbEffIdx = 0;
        HB_ON_HANDLERS[hbEffIdx](taskId);
    }

    LED::Show();
    LISENS::ResetTime();

    if (S.phase == SCHED::TASK_DONE && HB::State.Phase == SCHED::TASK_DONE) {
        APP::updDeltaColors();
        SCHED::KillAllUnlocked();
        TV::State.Transitioning = false;

        HB::StartEffect(false, true, false);
    }
}

/** TV-on default: fades all main-zone LEDs to their lux-adjusted EEPROM
    brightness. HB driven inline by T_EFFECT_H_FadeOn. */
void T_EFFECT_TV_ON_Default(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    bool LedChanged = false;
    const uint8_t inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    for (int i = 0; i < LED_NUM - LED_HB_NUM_FAKE; i++) {
        uint8_t targetLux = LED::getLuxBrightness(LED::State.StoredBrightness[i]);
        if (LED::TG_BRIGHTNESS_ToStoredColor(i, targetLux, inc, false)) {
            LedChanged = true;
        }
    }

    if (!LedChanged) {
        APP::updDeltaColors();
        S.phase = SCHED::TASK_DONE;
    } else {
        LED::Show();
    }
}

/** TV-on effect 1: random-order brightness ramp (via LED::State.PixelOrder[])
    then a colour-sync pass. HB driven inline by T_EFFECT_H_FadeOn. */
void T_EFFECT_TV_ON_1_RandomStatic(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;

    if (S.phase == 1) {
        if (S.paramA < maxLeds) {
            const int inc      = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
            const int l        = LED::State.PixelOrder[S.paramA];
            const int targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[l]);
            bool stillFading = false;

            if (LED::TG_BRIGHTNESS_ToTargetColor(l, targetBr, inc, false)) {
                stillFading = true;
            }

            if (!stillFading) {
                APP::updDeltaColors();
                S.paramA++;
            }
            LED::Show();
        } else {
            S.phase = 2; S.paramA = 0; S.paramB = 0;
        }
        return;
    }

    if (S.phase == 2) {
        bool changed = false;
        const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b, LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }

        if (changed) {
            LED::Show();
        } else {
            APP::updDeltaColors();
            S.phase = SCHED::TASK_DONE;
        }
        return;
    }
}

/** TV-on effect 2: mid-to-out zone-by-zone bloom (+50%), then colour adopt,
    then normalise to 100%. HB runs in parallel via T_EFFECT_H_CenterBloom. */
void T_EFFECT_TV_ON_2_MidToOutSep(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta    = S.paramA;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    if (phase >= 1 && phase <= 10) {
        int halfLeds, sMin, sMax;
        int activeStep = (phase > 5) ? (phase - 5) : phase;
        bool tvDone = false;

        switch (activeStep) {
            case 1: halfLeds = LED_TV_NUM   >> 1; sMin = halfLeds;      sMax = halfLeds - 1;              break;
            case 2: halfLeds = LED_COM_NUM  >> 1; sMin = LED::COM(0);    sMax = LED::COM(LED_COM_NUM - 1);   break;
            case 3: halfLeds = LED_UCOM_NUM >> 1; sMin = LED::UCOM(0);   sMax = LED::UCOM(LED_UCOM_NUM - 1); break;
            case 4: halfLeds = LED_BED_NUM  >> 1; sMin = LED::BED(0);    sMax = LED::BED(LED_BED_NUM - 1);   break;
            case 5: halfLeds = LED_LAMP_NUM >> 1; sMin = LED::LAMP(0);   sMax = LED::LAMP(LED_LAMP_NUM - 1); break;
            default: tvDone = true; break;
        }

        if (!tvDone) {
            if (ta < halfLeds) {
                bool moved = false;
                int led[2] = { sMax - ta, sMin + ta };

                for (int i = 0; i < 2; i++) {
                    int p = led[i];
                    bool changed = false;

                    if (phase <= 5) {
                        int bloomBr = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                        changed = LED::TG_BRIGHTNESS(p, bloomBr, brInc);
                        if (changed) LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
                    } else {
                        changed = LED::TG_COLOR(p, LED::State.StoredColor[p].r, LED::State.StoredColor[p].g, LED::State.StoredColor[p].b, brInc);
                        if (changed) LED::setPixel(p, LED::State.CurrentColor[p].r, LED::State.CurrentColor[p].g, LED::State.CurrentColor[p].b, LED::State.CurrentBrightness[p], false);
                    }
                    if (changed) moved = true;
                }

                if (moved) { LED::Show(); }
                else { APP::updDeltaColors(); S.paramA++; S.paramB = 0; }
            } else {
                S.phase++; S.paramA = 0; S.paramB = 0;
            }
        }
        return;
    }

    if (phase == 11) {
        bool tvLedsDone = true;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS_ToCurrentColor(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                tvLedsDone = false;
            }
        }
        LED::Show();
        if (tvLedsDone) {
            APP::updDeltaColors();
            S.phase = SCHED::TASK_DONE;
        }
    }
}

/** TV-on effect 3: all zones bloom (+50%) simultaneously from center, then
    colour adopt, then normalise. HB runs in parallel via T_EFFECT_H_CenterBloom. */
void T_EFFECT_TV_ON_3_MidToOutAll(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta    = S.paramA;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    if (phase == 1) {
        struct Zone { int sMin, sMax, half; };
        const Zone zones[] = {
            { LED::TV(LED_TV_NUM >> 1),    LED::TV((LED_TV_NUM >> 1) - 1),   LED_TV_NUM >> 1   },
            { LED::COM(LED_COM_NUM >> 1),  LED::COM((LED_COM_NUM >> 1) - 1), LED_COM_NUM >> 1  },
            { LED::UCOM(0),                LED::UCOM(LED_UCOM_NUM - 1),       1                 },
            { LED::BED(LED_BED_NUM >> 1),  LED::BED((LED_BED_NUM >> 1) - 1), LED_BED_NUM >> 1  },
            { LED::LAMP(0),                LED::LAMP(LED_LAMP_NUM - 1),       LED_LAMP_NUM >> 1 }
        };

        bool tvActive = false;
        int maxHalf = 0;

        for (int i = 0; i < 5; i++) {
            if (zones[i].half > maxHalf) maxHalf = zones[i].half;
            if (ta < zones[i].half) {
                int led[2] = { zones[i].sMax - ta, zones[i].sMin + ta };
                for (int j = 0; j < 2; j++) {
                    int p = led[j];
                    int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                    if (LED::TG_BRIGHTNESS_ToTargetColor(p, tvBloom, brInc)) {
                        tvActive = true;
                    }
                }
            }
        }

        if (!tvActive) {
            APP::updDeltaColors();
            if (ta >= maxHalf) { S.phase = 2; S.paramA = 0; }
            else                { S.paramA++;                }
        }
        if (tvActive) LED::Show();
        return;
    }

    if (phase == 2 || phase == 3) {
        bool mainMoving = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            bool changed = (phase == 2)
                ? LED::TG_COLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, brInc)
                : LED::TG_BRIGHTNESS(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc);
            if (changed) {
                LED::setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                mainMoving = true;
            }
        }
        if (!mainMoving) {
            S.phase = (phase == 2) ? 3 : SCHED::TASK_DONE;
            S.paramA = 0;
            APP::updDeltaColors();
        }
        if (mainMoving) LED::Show();
    }
}

/** TV-on effects 4 & 5: two points travel 180deg apart around the TV ring
    (+50% bloom), then normalise. Effect 4 starts at offset 0, effect 5 at a
    random offset. HB runs in parallel via T_EFFECT_H_CenterBloom. */
void T_EFFECT_TV_ON_4_5_HalfRun(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase   = S.phase;
    const int ta      = S.paramA;
    const int offset  = S.paramB;
    const int brInc   = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int tvCount = LED_TV_NUM;
    const int halfTv  = tvCount >> 1;

    if (phase == 1) {
        S.paramB = (EE::Get(EE_TV_ON_EFF) == 4) ? 0 : random(0, halfTv);
        S.phase = 2;
        return;
    }

    if (phase == 2) {
        if (ta < halfTv) {
            bool tvActive = false;
            int indices[2] = {
                (offset + ta) % tvCount,
                (offset + halfTv + ta) % tvCount
            };
            for (int i = 0; i < 2; i++) {
                int p = indices[i];
                int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                if (LED::TG_BRIGHTNESS_ToStoredColor(p, tvBloom, brInc)) {
                    tvActive = true;
                }
            }
            if (!tvActive) { APP::updDeltaColors(); S.paramA++; }
            LED::Show();
        } else {
            S.phase = 3; S.paramA = 0; S.paramB = 0;
        }
        return;
    }

    if (phase == 3) {
        bool mainMoving = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS_ToStoredColor(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                mainMoving = true;
            }
        }
        LED::Show();
        if (!mainMoving) {
            APP::updDeltaColors();
            S.phase = SCHED::TASK_DONE;
        }
    }
}

/** TV-on effects 6 & 7: multi-anchor expansion from fixed TV midpoints
    (+50% bloom), then fill, then normalise. HB runs in parallel via
    T_EFFECT_H_CenterBloom. */
void T_EFFECT_TV_ON_6_7_MidToExt(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int ta    = S.paramA;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    if (phase == 1) {
        int anchors[4];
        int maxIter;

        if (EE::Get(EE_TV_ON_EFF) == 6) {
            anchors[0] = LED::TV(14); anchors[1] = LED::TV(15);
            anchors[2] = LED::TV(0);  anchors[3] = LED::TV(29);
            maxIter = 5;
        } else {
            anchors[0] = LED::TV(7);  anchors[1] = LED::TV(22);
            anchors[2] = LED::TV(7);  anchors[3] = LED::TV(22);
            maxIter = 4;
        }

        if (ta < maxIter) {
            bool tvActive = false;
            int targets[4] = { anchors[0] - ta, anchors[1] + ta, anchors[2] + ta, anchors[3] - ta };

            for (int i = 0; i < 4; i++) {
                int p = targets[i];
                if (p < LED::TV(0) || p > LED::TV(LED_TV_NUM - 1)) continue;
                int tvBloom = (LED::getLuxBrightness(LED::State.StoredBrightness[p]) * 15) / 10;
                if (LED::TG_BRIGHTNESS_ToStoredColor(p, tvBloom, brInc)) {
                    tvActive = true;
                }
            }
            if (!tvActive) { APP::updDeltaColors(); S.paramA++; }
            LED::Show();
        } else {
            S.phase = 2; S.paramA = 0;
        }
        return;
    }

    if (phase == 2 || phase == 3) {
        bool mainMoving = false;
        const int limit = (phase == 2) ? LED_TV_NUM : (LED_NUM - LED_HB_NUM_FAKE);

        for (int i = 0; i < limit; i++) {
            int p = (phase == 2) ? LED::TV(i) : i;
            if (LED::TG_BRIGHTNESS_ToStoredColor(p, LED::getLuxBrightness(LED::State.StoredBrightness[p]), brInc)) {
                mainMoving = true;
            }
        }
        if (!mainMoving) {
            APP::updDeltaColors();
            S.phase = (phase == 2) ? 3 : SCHED::TASK_DONE;
        }
        if (mainMoving) LED::Show();
    }
}

/** TV-on effect 8: COM scanner - center pop, outward scan, fill back, then
    global normalise. HB runs in parallel via T_EFFECT_H_LinearSweep. */
void T_EFFECT_TV_ON_8_ComEffect(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase     = S.phase;
    const int ta        = S.paramA;
    const int brInc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int targetBr  = LED::getLuxBrightness(S.paramB);
    const int halfCom    = LED_COM_NUM >> 1;
    const int lL         = halfCom - 1;
    const int lR         = halfCom;

    if (phase == 1) {
        bool centerBusy = false;
        int centers[2] = { LED::COM(lL), LED::COM(lR) };

        for (int i = 0; i < 2; i++) {
            int p = centers[i];
            if (LED::TG_BRIGHTNESS_ToStoredColor(p, targetBr, 20)) {
                centerBusy = true;
            }
        }
        if (!centerBusy) { APP::updDeltaColors(); S.phase = 2; S.paramA = 0; }
        LED::Show();
        return;
    }

    if (phase == 2) {
        if (ta < halfCom) {
            int activeL = LED::COM(lL - ta), activeR = LED::COM(lR + ta);
            LED::setPixel(activeL, LED::State.StoredColor[activeL].r, LED::State.StoredColor[activeL].g, LED::State.StoredColor[activeL].b, targetBr, false);
            LED::setPixel(activeR, LED::State.StoredColor[activeR].r, LED::State.StoredColor[activeR].g, LED::State.StoredColor[activeR].b, targetBr, false);
            if (ta > 0) {
                int prevL = LED::COM(lL - (ta - 1)), prevR = LED::COM(lR + (ta - 1));
                LED::setPixel(prevL, LED::State.StoredColor[prevL].r, LED::State.StoredColor[prevL].g, LED::State.StoredColor[prevL].b, 10, false);
                LED::setPixel(prevR, LED::State.StoredColor[prevR].r, LED::State.StoredColor[prevR].g, LED::State.StoredColor[prevR].b, 10, false);
            }
            APP::updDeltaColors(); S.paramA++;
            LED::Show();
        } else {
            S.phase = 3; S.paramA = 0;
        }
        return;
    }

    if (phase == 3) {
        if (ta < halfCom) {
            int pL = LED::COM(ta), pR = LED::COM(LED_COM_NUM - 1 - ta);
            bool movingL = LED::TG_BRIGHTNESS(pL, targetBr, 15);
            bool movingR = LED::TG_BRIGHTNESS(pR, targetBr, 15);
            if (movingL) LED::setPixel(pL, LED::State.StoredColor[pL].r, LED::State.StoredColor[pL].g, LED::State.StoredColor[pL].b, LED::State.CurrentBrightness[pL], false);
            if (movingR) LED::setPixel(pR, LED::State.StoredColor[pR].r, LED::State.StoredColor[pR].g, LED::State.StoredColor[pR].b, LED::State.CurrentBrightness[pR], false);
            if (!movingL && !movingR) S.paramA++;
        } else {
            APP::updDeltaColors(); S.phase = 4; S.paramA = 0;
        }
        LED::Show();
        return;
    }

    if (phase == 4) {
        bool mainBusy = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS_ToStoredColor(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc)) {
                mainBusy = true;
            }
        }
        if (!mainBusy) {
            APP::updDeltaColors(); S.phase = SCHED::TASK_DONE;
        }
        else { LED::Show(); }
    }
}

/** TV-on effect 9: TV strip sweeps center-to-edges, then waits for HB's
    quad-expand (T_EFFECT_H_QuadPoint) to pass its expand phase, then room
    zones normalise. HB driven inline. */
void T_EFFECT_TV_ON_9_QuadPointHB(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase      = S.phase;
    const int brInc      = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int animDelay  = LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL));
    const int tvCount    = LED_START_I_COM;

    if (phase == 1) {
        bool moving = false;
        int ta = S.paramA, tvMid = tvCount >> 1;

        if (ta <= tvMid) {
            int pL = (tvMid - 1) - ta, pR = tvMid + ta;
            int pixels[] = { pL, pR };

            for (int i = 0; i < 2; i++) {
                int p = pixels[i];
                if (p >= 0 && p < tvCount) {
                    if (LED::TG_BRIGHTNESS_ToStoredColor(p, LED::getLuxBrightness(LED::State.StoredBrightness[p]), brInc, false)) {
                        moving = true;
                    }
                }
            }
            if (!moving && ta < tvMid) { APP::updDeltaColors(); S.paramA++; moving = true; }
        }

        if (moving) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, animDelay);
        } else {
            APP::updDeltaColors();
            S.phase = 2; S.paramA = 0;
            SCHED::SetInterval(tID, SCHED::Unit::MS, 0);
        }
        return;
    }

    if (phase == 2) {
        bool tvDone = true;
        for (int i = 0; i < tvCount; i++) {
            if (LED::TG_BRIGHTNESS_ToStoredColor(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc, false)) {
                tvDone = false;
            }
        }

        if (tvDone && HB::State.Phase >= 2) {
            APP::updDeltaColors(); S.phase = 3;
        }
        LED::Show();
        return;
    }

    if (phase == 3) {
        bool roomBusy = false;
        for (int i = tvCount; i < (LED_NUM - LED_HB_NUM_FAKE); i++) {
            if (LED::TG_BRIGHTNESS_ToStoredColor(i, LED::getLuxBrightness(LED::State.StoredBrightness[i]), brInc, false)) {
                roomBusy = true;
            }
        }
        if (!roomBusy) {
            APP::updDeltaColors(); S.phase = SCHED::TASK_DONE;
        }
        LED::Show();
    }
}

/** TV-on effect 10: liquid fill - all zones rise floor to ceiling
    (LAMP, BED, UCOM, COM, TV bottom row, TV sides row-by-row, TV top row),
    then a final colour-sync pass. HB driven inline by T_EFFECT_H_FadeOn.

    TV physical layout (LED_TV_NUM = 30):
      10 11 12 13 14  15 16 17 18 19   <- top row
      09                          20
      08                          21
      07                          22   <- sides
      06                          23
      05                          24
      04 03 02 01 00  29 28 27 26 25   <- bottom row */
void T_EFFECT_TV_ON_10_LiquidFill(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    #define FADE_LEDS(idxArr, cnt, busyFlag) \
        for (int _i = 0; _i < (cnt); _i++) { \
            int _l = (idxArr)[_i]; \
            int _br = LED::getLuxBrightness(LED::State.StoredBrightness[_l]); \
            if (LED::TG_BRIGHTNESS(_l, _br, inc, false)) { \
                LED::setPixel(_l, LED::State.StoredColor[_l].r, LED::State.StoredColor[_l].g, \
                             LED::State.StoredColor[_l].b, LED::State.CurrentBrightness[_l], false); \
                (busyFlag) = true; \
            } \
        }

    if (S.phase == 1) {
        int idx[LED_LAMP_NUM];
        for (int i = 0; i < LED_LAMP_NUM; i++) idx[i] = LED_START_I_LAMP + i;
        bool busy = false;
        FADE_LEDS(idx, LED_LAMP_NUM, busy);
        if (!busy) { APP::updDeltaColors(); S.phase = 2; }
        else LED::Show();
        return;
    }

    if (S.phase == 2) {
        int idx[LED_BED_NUM];
        for (int i = 0; i < LED_BED_NUM; i++) idx[i] = LED_START_I_BED + i;
        bool busy = false;
        FADE_LEDS(idx, LED_BED_NUM, busy);
        if (!busy) { APP::updDeltaColors(); S.phase = 3; }
        else LED::Show();
        return;
    }

    if (S.phase == 3) {
        int idx[LED_UCOM_NUM];
        for (int i = 0; i < LED_UCOM_NUM; i++) idx[i] = LED_START_I_UCOM + i;
        bool busy = false;
        FADE_LEDS(idx, LED_UCOM_NUM, busy);
        if (!busy) { APP::updDeltaColors(); S.phase = 4; }
        else LED::Show();
        return;
    }

    if (S.phase == 4) {
        int idx[LED_COM_NUM];
        for (int i = 0; i < LED_COM_NUM; i++) idx[i] = LED_START_I_COM + i;
        bool busy = false;
        FADE_LEDS(idx, LED_COM_NUM, busy);
        if (!busy) { APP::updDeltaColors(); S.phase = 5; }
        else LED::Show();
        return;
    }

    if (S.phase == 5) {
        const int idx[] = { LED::TV(0), LED::TV(1), LED::TV(2), LED::TV(3), LED::TV(4),
                             LED::TV(25), LED::TV(26), LED::TV(27), LED::TV(28), LED::TV(29) };
        bool busy = false;
        FADE_LEDS(idx, 10, busy);
        if (!busy) {
            APP::updDeltaColors();
            S.phase = 6; S.paramA = 0;
        } else LED::Show();
        return;
    }

    if (S.phase == 6) {
        const int sideRows = 5;
        if (S.paramA < sideRows) {
            const int idx[] = { LED::TV(5 + S.paramA), LED::TV(24 - S.paramA) };
            bool busy = false;
            FADE_LEDS(idx, 2, busy);
            if (!busy) {
                APP::updDeltaColors();
                S.paramA++;
            } else LED::Show();
        } else {
            S.phase = 7; S.paramA = 0;
        }
        return;
    }

    if (S.phase == 7) {
        int idx[10];
        for (int i = 0; i < 10; i++) idx[i] = LED::TV(10 + i);
        bool busy = false;
        FADE_LEDS(idx, 10, busy);
        if (!busy) { APP::updDeltaColors(); S.phase = 8; }
        else LED::Show();
        return;
    }

    if (S.phase == 8) {
        const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;
        bool changed = false;
        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i,
                    LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i,
                    LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b,
                    LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }
        if (changed) { LED::Show(); }
        else          {
            APP::updDeltaColors(); S.phase = SCHED::TASK_DONE;
        }
        return;
    }

    #undef FADE_LEDS
}

/** TV-on effect 11: pixel boot sequence - LED::State.PixelOrder[] reveals LEDs
    one at a time in random order, then a colour-sync pass. HB driven inline
    by T_EFFECT_H_FadeOn. */
void T_EFFECT_TV_ON_11_PixelBoot(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int inc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));

    if (S.phase == 1) {
        if (S.paramA < maxLeds) {
            int l        = LED::State.PixelOrder[S.paramA];
            int targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[l]);
            bool fading  = LED::TG_BRIGHTNESS(l, targetBr, inc, false);

            if (fading) {
                LED::setPixel(l, LED::State.StoredColor[l].r, LED::State.StoredColor[l].g, LED::State.StoredColor[l].b,
                             LED::State.CurrentBrightness[l], false);
            } else {
                APP::updDeltaColors();
                S.paramA++;
            }
            LED::Show();
        } else {
            S.phase = 2; S.paramA = 0; S.paramB = 0;
        }
        return;
    }

    if (S.phase == 2) {
        bool changed = false;
        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_TEMPCOLOR(i, LED::State.StoredColor[i].r, LED::State.StoredColor[i].g, LED::State.StoredColor[i].b, inc)) {
                LED::setPixel(i, LED::State.TargetColor[i].r, LED::State.TargetColor[i].g, LED::State.TargetColor[i].b,
                             LED::State.CurrentBrightness[i], false);
                changed = true;
            }
        }
        if (changed) { LED::Show(); }
        else          {
            APP::updDeltaColors(); S.phase = SCHED::TASK_DONE;
        }
        return;
    }
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/* TV ON HB HELPERS  * * * * * * * * * * * * * * */
/* * * * * * * * * * * * * * * * * * * * * * * * */

/** HB on-transition helper - plain linear fade-in 0 -> EEPROM target.
    Respects EE_HB_DUAL_COLOR. Used by EE_TV_ON_HB_EFF = 0 (default). */
void T_EFFECT_H_FadeOn(SCHED::TaskId tID) {
    (void)tID;
    const uint8_t inc      = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const bool    dual     = EE::Get(EE_HB_DUAL_COLOR);
    const int     targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
    const int     halfPt   = LED_HB_NUM >> 1;
    bool          busy     = false;

    for (int i = 0; i < LED_HB_NUM; i++) {
        int ledIdx = LED::HB(i);
        if (LED::TG_BRIGHTNESS(ledIdx, targetBr, inc, false)) {
            uint8_t r, g, b;
            if (dual) {
                int ref = (i < halfPt) ? LED::UCOM(0) : LED::UCOM(1);
                r = LED::State.StoredColor[ref].r; g = LED::State.StoredColor[ref].g; b = LED::State.StoredColor[ref].b;
            } else {
                r = LED::State.StoredColor[LED_START_I_HB].r;
                g = LED::State.StoredColor[LED_START_I_HB].g;
                b = LED::State.StoredColor[LED_START_I_HB].b;
            }
            LED::setPixel(ledIdx, r, g, b, LED::State.CurrentBrightness[ledIdx], false);
            busy = true;
        }
    }

    if (!busy) {
        APP::updDeltaColors();
        HB::State.Phase = SCHED::TASK_DONE;
    }
}

/** HB on-transition helper - center-outward bloom (+50%) then settle to
    100% EEPROM target. Used by effects 2, 3, 4/5, 6/7. */
void T_EFFECT_H_CenterBloom(SCHED::TaskId tID) {
    (void)tID;
    const uint8_t inc    = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int     hbHalf = LED_HB_NUM >> 1;

    if (HB::State.Phase == 1) {
        if (HB::State.ParamA < hbHalf) {
            int idxL    = LED::HB(hbHalf - 1 - HB::State.ParamA);
            int idxR    = LED::HB(hbHalf + HB::State.ParamA);
            int pairs[] = { idxL, idxR };
            bool active = false;
            const int bloom = (LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]) * 15) / 10;

            for (int i = 0; i < 2; i++) {
                int p = pairs[i];
                if (LED::TG_BRIGHTNESS_ToTargetColor(p, bloom, inc, false)) {
                    active = true;
                }
            }
            if (!active) HB::State.ParamA++;
        } else {
            APP::updDeltaColors();
            HB::State.Phase = 2; HB::State.ParamA = 0;
        }
        return;
    }

    if (HB::State.Phase == 2) {
        const int target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
        bool busy = false;

        for (int i = 0; i < LED_HB_NUM; i++) {
            int ledIdx = LED::HB(i);
            if (LED::TG_BRIGHTNESS_ToCurrentColor(ledIdx, target, inc, false)) {
                busy = true;
            }
        }
        if (!busy) {
            APP::updDeltaColors();
            HB::State.Phase = SCHED::TASK_DONE;
        }
    }
}

/** HB on-transition helper - sequential linear sweep from pixel 0 upward,
    then settle. Used by effect 8 (ComEffect). */
void T_EFFECT_H_LinearSweep(SCHED::TaskId tID) {
    (void)tID;
    const uint8_t inc    = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int     target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);

    if (HB::State.Phase == 1) {
        if (HB::State.ParamA < LED_HB_NUM) HB::State.ParamA++;
        bool active = false;

        for (int i = 0; i < HB::State.ParamA; i++) {
            int p = LED::HB(i);
            if (LED::TG_BRIGHTNESS_ToTargetColor(p, target, inc, false)) {
                active = true;
            }
        }
        if (HB::State.ParamA >= LED_HB_NUM && !active) {
            APP::updDeltaColors();
            HB::State.Phase = 2;
        }
        return;
    }

    if (HB::State.Phase == 2) {
        bool busy = false;
        for (int i = 0; i < LED_HB_NUM; i++) {
            int p = LED::HB(i);
            if (LED::TG_BRIGHTNESS_ToCurrentColor(p, target, inc, false)) {
                busy = true;
            }
        }
        if (!busy) {
            APP::updDeltaColors();
            HB::State.Phase = SCHED::TASK_DONE;
        }
    }
}

/** HB on-transition helper - quad-anchor expansion from 4 evenly spaced
    strip points, then settle. Used by effect 9 (QuadPointHB). */
void T_EFFECT_H_QuadPoint(SCHED::TaskId tID) {
    (void)tID;
    const uint8_t inc     = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int     hbTotal = LED_HB_NUM;
    const int     segment = hbTotal >> 2;
    const int     reach   = segment >> 1;

    const int p1 = (segment * 0) + (segment >> 1);
    const int p2 = (segment * 1) + (segment >> 1);
    const int p3 = (segment * 2) + (segment >> 1);
    const int p4 = (segment * 3) + (segment >> 1);

    if (HB::State.Phase == 1) {
        const int target = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
        bool active = false;

        if (HB::State.ParamA <= reach) {
            int pts[] = {
                p1 - HB::State.ParamA, p1 + HB::State.ParamA,
                p2 - HB::State.ParamA, p2 + HB::State.ParamA,
                p3 - HB::State.ParamA, p3 + HB::State.ParamA,
                p4 - HB::State.ParamA, p4 + HB::State.ParamA
            };
            for (int i = 0; i < 8; i++) {
                int p = pts[i];
                if (p >= 0 && p < hbTotal) {
                    int realIdx = LED::HB(p);
                    if (LED::TG_BRIGHTNESS_ToTargetColor(realIdx, target, inc, false)) {
                        active = true;
                    }
                }
            }
            if (!active && HB::State.ParamA < reach)  { HB::State.ParamA++; }
            else if (!active)              {
                HB::State.Phase = 2; HB::State.ParamA = 0;
            }
        }
        return;
    }

    if (HB::State.Phase == 2) {
        const int finalT = LED::getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
        bool busy = false;
        for (int i = 0; i < hbTotal; i++) {
            int id = LED::HB(i);
            if (LED::TG_BRIGHTNESS_ToCurrentColor(id, finalT, inc, false)) {
                busy = true;
            }
        }
        if (!busy) {
            APP::updDeltaColors();
            HB::State.Phase = SCHED::TASK_DONE;
        }
    }
}

/** TV-off master task: dispatches the configured TV-off sub-effect. On
    completion, restores MOTION::State.Status = motON, syncs the app, and
    kills all tasks. */
void T_EFFECT_TV_OFF(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);

    if (S.phase != SCHED::TASK_DONE) {
        int tvEffIdx = EE::Get(EE_TV_OFF_EFF);
        if (tvEffIdx < 0 || tvEffIdx >= TV_OFF_HANDLERS_COUNT) tvEffIdx = 0;

        if (tvEffIdx == 4) S.paramB = 0;
        else if (tvEffIdx == 5) S.paramB = 1;

        TV_OFF_HANDLERS[tvEffIdx](taskId);
    }

    if (S.phase == SCHED::TASK_DONE) {
        MOTION::State.Status = motON;
        APP::updStatus("LED::T_EFFECT_TV_OFF");
        APP::updDeltaColors();
        SCHED::KillAllUnlocked();
        TV::State.Transitioning = false;
    }
    else if (S.phase != SCHED::TASK_DONE) {
        LED::Show();
    }
}

/** TV-off default: fades all main and HB LEDs uniformly to black. */
void T_EFFECT_TV_OFF_Default(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    bool moving = false;

    const int inc = LED::getLuxAdaptInc(EE::Get(EE_TV_ON_BR_CL_INC));
    const int mainLimit = LED_NUM - LED_HB_NUM_FAKE;

    for (int i = 0; i < mainLimit; i++) {
        if (LED::TG_BRIGHTNESS_ToCurrentColor(i, 0, inc, false)) {
            moving = true;
        }
    }

    for (int i = 0; i < LED_HB_NUM; i++) {
        int idx = LED::HB(i);
        if (LED::TG_BRIGHTNESS_ToCurrentColor(idx, 0, inc, false)) {
            moving = true;
        }
    }

    if (moving) {
        LED::Show();
    } else {
        S.phase = SCHED::TASK_DONE;
        APP::updDeltaColors();
    }
}

/** TV-off effect 1: TV strip fades first (with a stepDelay pause), then
    room zones (COM/UCOM/BED/LAMP) fade sequentially in LED::State.PixelOrder[]
    order, each with an EE_TV_OFF_TIME pause; HB pixels track each zone's
    progress via a proportional block. */
void T_EFFECT_TV_OFF_1_DelayWTvOff(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));
    const uint32_t stepDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int maxHbLeds = LED_HB_NUM;

    if (phase == 1) {
        bool tvMoving = false;

        for (int i = 0; i < LED_TV_NUM; i++) {
            int l = LED::TV(i);
            if (LED::TG_BRIGHTNESS_ToCurrentColor(l, 0, brInc, false)) {
                tvMoving = true;
            }
        }

        if (tvMoving) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, animDelay);
        } else {
            APP::updDeltaColors();
            S.phase = 2; S.paramA = 0;
            HB::State.ParamA = 0; HB::State.Phase = 0;
            SCHED::SetInterval(tID, SCHED::Unit::MS, stepDelay);
        }
        return;
    }

    if (phase >= 2 && phase <= 5) {
        if (S.paramA < maxLeds) {
            bool moving = false;
            const int l = LED::State.PixelOrder[S.paramA];

            int lZone = (l < LED_TV_NUM) ? 1 :
                        (l < LED_TV_NUM + LED_COM_NUM) ? 2 :
                        (l < LED_TV_NUM + LED_COM_NUM + LED_UCOM_NUM) ? 3 :
                        (l < LED_TV_NUM + LED_COM_NUM + LED_UCOM_NUM + LED_BED_NUM) ? 4 : 5;

            if (lZone == phase) {
                if (LED::TG_BRIGHTNESS(l, 0, brInc, false)) {
                    LED::setPixel(l, LED::State.CurrentColor[l].r, LED::State.CurrentColor[l].g, LED::State.CurrentColor[l].b, LED::State.CurrentBrightness[l], true);
                    moving = true;
                }

                int hbCountForZone = round((float)LED_HB_NUM / (maxLeds - LED_TV_NUM));
                int hbStart = HB::State.ParamA * hbCountForZone;
                int hbEnd = (HB::State.ParamA == maxLeds - 1) ? maxHbLeds : (hbStart + hbCountForZone);
                if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;

                for (int i = hbStart; i < hbEnd; i++) {
                    int hbIdx = LED::HB(LED::State.HeartbeatOrder[i]);
                    if (LED::TG_BRIGHTNESS(hbIdx, 0, brInc, false)) {
                        LED::setPixel(hbIdx, LED::State.CurrentColor[hbIdx].r, LED::State.CurrentColor[hbIdx].g, LED::State.CurrentColor[hbIdx].b, LED::State.CurrentBrightness[hbIdx], true);
                        moving = true;
                    }
                }

                if (moving) {
                    SCHED::SetInterval(tID, SCHED::Unit::MS, animDelay);
                } else {
                    APP::updDeltaColors();
                    S.paramA++;
                    HB::State.ParamA++;
                    SCHED::SetInterval(tID, SCHED::Unit::MS, stepDelay);
                }
            } else {
                S.paramA++;
                SCHED::SetInterval(tID, SCHED::Unit::MS, 0);
            }
        } else {
            S.paramA = 0; S.phase++;
            if (S.phase > 5) {
                S.phase = SCHED::TASK_DONE;
            }
        }
    }
}

/** TV-off effect 2: sequential "domino" shutdown of every LED in
    LED::State.PixelOrder[] order, each with a proportional HB block, waiting
    EE_TV_OFF_TIME between each. */
void T_EFFECT_TV_OFF_2_DelayAll(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int totalLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int maxHbLeds = LED_HB_NUM;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));
    const uint32_t nextLedDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));

    if (S.phase == 1) {
        if (S.paramA < totalLeds) {
            bool moving = false;
            const int l = LED::State.PixelOrder[S.paramA];

            if (LED::TG_BRIGHTNESS_ToCurrentColor(l, 0, brInc, false)) {
                moving = true;
            }

            const float hbRatio = (float)maxHbLeds / totalLeds;
            int hbStart = round(S.paramA * hbRatio);
            int hbEnd = round((S.paramA + 1) * hbRatio);

            if (S.paramA == totalLeds - 1) hbEnd = maxHbLeds;
            if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;

            for (int i = hbStart; i < hbEnd; i++) {
                int hbIdx = LED::HB(LED::State.HeartbeatOrder[i]);
                if (LED::TG_BRIGHTNESS_ToCurrentColor(hbIdx, 0, brInc, false)) {
                    moving = true;
                }
            }

            if (moving) {
                SCHED::SetInterval(tID, SCHED::Unit::MS, animDelay);
            } else {
                APP::updDeltaColors();
                S.paramA++;
                SCHED::SetInterval(tID, SCHED::Unit::MS, nextLedDelay);
            }
        } else {
            S.phase = SCHED::TASK_DONE;
            APP::updDeltaColors();
        }
    }
}

/** TV-off effect 3: zone-by-zone symmetric shutdown (TV/COM/UCOM/BED/LAMP)
    with HB pixels mirrored center-out per zone, pausing EE_TV_OFF_TIME
    between zones. */
void T_EFFECT_TV_OFF_3_SlowTvSequential(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int animDelay = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));
    const uint32_t stepDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));

    if (phase >= 1 && phase <= 5) {
        bool moving = false;
        int mI = 0;

        switch (phase) {
            case 1: mI = LED_TV_NUM;   break;
            case 2: mI = LED_COM_NUM;  break;
            case 3: mI = LED_UCOM_NUM; break;
            case 4: mI = LED_BED_NUM;  break;
            case 5: mI = LED_LAMP_NUM; break;
        }

        for (int i = 0; i < mI; i++) {
            int l;
            if      (phase == 1) l = LED::TV(i);
            else if (phase == 2) l = LED::COM(i);
            else if (phase == 3) l = LED::UCOM(i);
            else if (phase == 4) l = LED::BED(i);
            else                l = LED::LAMP(i);

            if (LED::TG_BRIGHTNESS_ToCurrentColor(l, 0, brInc, false)) {
                moving = true;
            }
        }

        const float hbPerZone = (float)LED_HB_NUM / 5.0f;
        int halfHb    = LED_HB_NUM >> 1;
        int startIdx  = round((phase - 1) * (hbPerZone / 2.0f));
        int endIdx    = round(phase * (hbPerZone / 2.0f));

        if (phase == 5) endIdx = halfHb;

        for (int i = startIdx; i < endIdx; i++) {
            int hbL = LED::HB(halfHb - 1 - i);
            int hbR = LED::HB(halfHb + i);

            int pair[2] = {hbL, hbR};
            for (int j = 0; j < 2; j++) {
                int p = pair[j];
                if (LED::TG_BRIGHTNESS_ToCurrentColor(p, 0, brInc, false)) {
                    moving = true;
                }
            }
        }

        if (moving) {
                SCHED::SetInterval(tID, SCHED::Unit::MS, animDelay);
            } else {
                APP::updDeltaColors();
                S.phase++;
                SCHED::SetInterval(tID, SCHED::Unit::MS, stepDelay);
            if (S.phase > 5) {
                S.phase = SCHED::TASK_DONE;
            }
        }
        LED::Show();
    }
}

/** TV-off effects 4 & 5: countdown flicker-off (bomb/fuse). Phase 1 sets
    the start pixel and countdown timer; phase 2 flickers the current TV
    LED and its linked HB block until the timer expires, then moves to the
    previous TV LED (effect 5 enables the flicker overlay via paramB=1);
    phase 3 fades all remaining zones globally to 0. */
void T_EFFECT_TV_OFF_4_5_Countdown(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int maxLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int maxHbLeds = LED_HB_NUM;

    if (S.phase == 1) {
        S.paramA = LED::TV(LED_TV_NUM - 1);
        TV::State.CountdownTimer = TimeNow + S_TO_MS(EE::Get(EE_TV_OFF_TIME));
        SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)));
        S.phase = 2;
        return;
    }

    if (S.phase == 2) {
        randomSeed(analogRead(A0));

        const float hbRatio = (float)maxHbLeds / LED_TV_NUM;
        int tvIdxInZone = S.paramA - LED::TV(0);
        int hbStart = round(tvIdxInZone * hbRatio);
        int hbEnd = round((tvIdxInZone + 1) * hbRatio);

        if (hbEnd > maxHbLeds) hbEnd = maxHbLeds;

        if (S.paramB == 1) {
            uint8_t targetBr = LED::getLuxBrightness(LED::State.StoredBrightness[S.paramA]);
            uint8_t rndBr = (uint8_t)random(2, targetBr);

            LED::setPixel(S.paramA, LED::State.StoredColor[S.paramA].r, LED::State.StoredColor[S.paramA].g, LED::State.StoredColor[S.paramA].b, rndBr, true);

            for (int i = hbStart; i < hbEnd; i++) {
                int hIdx = LED::HB(i);
                LED::setPixel(hIdx, LED::State.StoredColor[hIdx].r, LED::State.StoredColor[hIdx].g, LED::State.StoredColor[hIdx].b, rndBr, true);
            }
        }

        SCHED::SetInterval(tID, SCHED::Unit::MS, (uint32_t)random(30, 50));

        if (TV::State.CountdownTimer < TimeNow) {
            TV::State.CountdownTimer = TimeNow + S_TO_MS(EE::Get(EE_TV_OFF_TIME));

            LED::setPixel(S.paramA, LED::State.StoredColor[S.paramA].r, LED::State.StoredColor[S.paramA].g, LED::State.StoredColor[S.paramA].b, 0, true);

            for (int i = hbStart; i < hbEnd; i++) {
                int hIdx = LED::HB(i);
                LED::setPixel(hIdx, LED::State.StoredColor[hIdx].r, LED::State.StoredColor[hIdx].g, LED::State.StoredColor[hIdx].b, 0, true);
            }

            APP::updDeltaColors();

            if (S.paramA == LED::TV(0)) {
                S.phase = 3;
                SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)));
            } else {
                S.paramA--;
            }
        }
        LED::Show();
        return;
    }

    if (S.phase == 3) {
        bool moving = false;

        for (int i = 0; i < maxLeds; i++) {
            if (LED::TG_BRIGHTNESS_ToCurrentColor(i, 0, brInc, false)) {
                moving = true;
            }
        }

        if (moving) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)));
        } else {
            S.phase = SCHED::TASK_DONE;
            APP::updDeltaColors();
        }
        LED::Show();
    }
}

/** TV-off effect 6: dual circular wipe on the TV ring (two points travelling
    180deg apart from a random offset) with HB pixels unzipping outward from
    center in matching proportion, then a global fade of remaining zones. */
void T_EFFECT_TV_OFF_6_RandomHalf(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase = S.phase;
    const int brInc = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int tvCount = LED_TV_NUM;
    const int halfTv = tvCount >> 1;
    const int maxHbLeds = LED_HB_NUM;
    const int halfHb = maxHbLeds >> 1;

    if (phase == 1) {
        S.paramB = random(0, halfTv);
        S.phase = 2;
        return;
    }

    if (phase == 2) {
        const int ta = S.paramA;
        const int offset = S.paramB;

        SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)) * 2);

        if (ta < halfTv) {
            bool moving = false;

            int targets[2] = {
                (offset - ta + tvCount) % tvCount,
                (offset + halfTv - ta + tvCount) % tvCount
            };

            for (int i = 0; i < 2; i++) {
                int p = LED::TV(targets[i]);
                if (LED::TG_BRIGHTNESS_ToCurrentColor(p, 0, brInc, false)) {
                    moving = true;
                }
            }

            float hbRatio = (float)halfHb / (float)halfTv;
            int hbStart = round((float)ta * hbRatio);
            int hbEnd = round((float)(ta + 1) * hbRatio);
            if (hbEnd > halfHb) hbEnd = halfHb;

            for (int i = hbStart; i < hbEnd; i++) {
                int hbL = LED::HB(halfHb - 1 - i);
                int hbR = LED::HB(halfHb + i);

                int pair[2] = {hbL, hbR};
                for (int j = 0; j < 2; j++) {
                    if (LED::TG_BRIGHTNESS_ToCurrentColor(pair[j], 0, brInc, false)) {
                        moving = true;
                    }
                }
            }

            if (!moving)  {
                APP::updDeltaColors();
                S.paramA++;
            }
        } else {
            S.phase = 3; S.paramA = 0; S.paramB = 0;
            SCHED::SetInterval(tID, SCHED::Unit::S, (uint32_t)EE::Get(EE_TV_OFF_TIME));
        }
        LED::Show();
        return;
    }

    if (phase == 3) {
        bool moving = false;
        const int limit = LED_NUM - LED_HB_NUM_FAKE;

        for (int i = 0; i < limit; i++) {
            if (LED::TG_BRIGHTNESS_ToCurrentColor(i, 0, brInc, false)) {
                moving = true;
            }
        }

        if (moving) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)));
        } else {
            APP::updDeltaColors();
            S.phase = SCHED::TASK_DONE;
        }
        LED::Show();
    }
}

/** TV-off effect 7: room zones fade to 0 together, then HB quad-anchors
    retract ring-by-ring (each ring holding EE_TV_OFF_TIME before the next),
    then the TV strip converges extremes-to-centre with the same per-pair
    hold. Reverse mirror of T_EFFECT_TV_ON_9_QuadPointHB. */
void T_EFFECT_TV_OFF_7_QuadPointHB(SCHED::TaskId tID) {
    SCHED::Scratch &S = SCHED::ScratchFor(tID);
    const int phase    = S.phase;
    const int brInc    = LED::getLuxAdaptInc(EE::Get(EE_TV_OFF_BR_CL_INC));
    const int animDel  = LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL));
    const uint32_t nextDelay = S_TO_MS(EE::Get(EE_TV_OFF_TIME));

    const int tvCount = LED_TV_NUM;
    const int tvMid   = tvCount >> 1;

    const int hbTotal = LED_HB_NUM;

    const int segment    = hbTotal >> 2;
    const int p1         = (segment * 0) + (segment >> 1);
    const int p2         = (segment * 1) + (segment >> 1);
    const int p3         = (segment * 2) + (segment >> 1);
    const int p4         = (segment * 3) + (segment >> 1);
    const int hbMaxReach = segment >> 1;

    if (phase == 1) {
        bool moving = false;

        for (int i = LED_START_I_COM; i < (LED_NUM - LED_HB_NUM_FAKE); i++) {
            if (LED::TG_BRIGHTNESS_ToCurrentColor(i, 0, brInc, false)) {
                moving = true;
            }
        }

        if (moving) {
            SCHED::SetInterval(tID, SCHED::Unit::MS, animDel);
        } else {
            APP::updDeltaColors();
            S.phase = 2; S.paramA = 0;
            SCHED::SetInterval(tID, SCHED::Unit::MS, 0);
        }
        LED::Show();
    }

    else if (phase == 2) {
        int ring = S.paramA;

        if (ring <= hbMaxReach) {
            int liveRadius = hbMaxReach - ring;
            bool moving    = false;

            int pts[] = {
                p1 - liveRadius, p1 + liveRadius,
                p2 - liveRadius, p2 + liveRadius,
                p3 - liveRadius, p3 + liveRadius,
                p4 - liveRadius, p4 + liveRadius
            };

            for (int i = 0; i < 8; i++) {
                int p = pts[i];
                if (p >= 0 && p < hbTotal) {
                    int realIdx = LED::HB(p);
                    if (LED::TG_BRIGHTNESS_ToCurrentColor(realIdx, 0, brInc, false)) {
                        moving = true;
                    }
                }
            }

            if (moving) {
                SCHED::SetInterval(tID, SCHED::Unit::MS, animDel);
            } else {
                APP::updDeltaColors();
                S.paramA++;
                SCHED::SetInterval(tID, SCHED::Unit::MS, nextDelay);
            }
        } else {
            S.phase = 3; S.paramA = 0;
            SCHED::SetInterval(tID, SCHED::Unit::MS, 0);
        }
        LED::Show();
    }

    else if (phase == 3) {
        int ta = S.paramA;

        if (ta <= tvMid) {
            int pL = LED::TV(ta);
            int pR = LED::TV((tvCount - 1) - ta);
            int pixels[] = {pL, pR};
            bool moving = false;

            for (int i = 0; i < 2; i++) {
                int p = pixels[i];
                if (LED::TG_BRIGHTNESS_ToCurrentColor(p, 0, brInc, false)) {
                    moving = true;
                }
            }

            if (moving) {
                SCHED::SetInterval(tID, SCHED::Unit::MS, animDel);
            } else {
                APP::updDeltaColors();
                S.paramA++;
                SCHED::SetInterval(tID, SCHED::Unit::MS, nextDelay);
            }
        } else {
            APP::updDeltaColors();
            S.phase = SCHED::TASK_DONE;
        }
        LED::Show();
    }
}

/** Pushes a new on/off event into TV::State.LOG (newest at [0]). */
void LogPush(bool event, int pinBefore, int pinAtTrigger) {
    for (int i = TV_LOG_INDEX_MAX - 1; i > 0; i--) {
        TV::State.LOG[i] = TV::State.LOG[i - 1];
    }
    TV::State.LOG[0].epoch             = RTC_TimeClient.getEpochTime();
    TV::State.LOG[0].Event              = event;
    TV::State.LOG[0].PinValueBefore    = pinBefore;
    TV::State.LOG[0].PinValueAtTrigger = pinAtTrigger;
}

/** Handles the TV turning OFF: shuffles order arrays, resets MOTION/AM
    state, kills all tasks, and launches T_EFFECT_TV_OFF with the
    configured effect. Called automatically by Status(). */
void Off() {
    LogPush(false, TV::State.PrevPinValue, TV::State.PinValue);
    APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "TV", "Off", "with effect [%d], delay [%lu] ms",
        EE::Get(EE_TV_OFF_EFF), (unsigned long)LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)));

    LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE);
    LED::shuffleArray(LED::State.HeartbeatOrder, LED_HB_NUM);

    MOTION::State.Status = motOFF;
    APP::Am.Status     = false;
    APP::updStatus("TV::Off");

    HB::State.Phase   = 0; HB::State.ParamA   = 0;
    TV::State.Transitioning = true;

    HB::EndTask();
    SCHED::KillAllUnlocked();
    DIF::AutoOff();
    SCHED::TaskId newId = SCHED::AddTask("TV_Off", "T_EFFECT_TV_OFF", T_EFFECT_TV_OFF, SCHED::Unit::MS,
                            LED::getLuxAdaptDelay(EE::Get(EE_TV_OFF_BR_CL_DEL)), 1, false);
    SCHED::Scratch &S = SCHED::ScratchFor(newId);
    S.phase = 1; S.paramA = 0; S.paramB = 0;
}

/** Handles the TV turning ON: prepares colours (per EE_TV_RANDOM_COLOR_START),
    resets MOTION/AM state, kills all tasks, then launches T_EFFECT_TV_ON
    which drives the TV sub-effect and matching HB helper in parallel each
    tick. Called automatically by Status(). */
void On() {
    LogPush(true, TV::State.PrevPinValue, TV::State.PinValue);
    APP::termMsgLog(APP_LOG_INF, APP_SRC_TV, "TV", "On", "with effect [%d], delay [%lu] ms",
        EE::Get(EE_TV_ON_EFF), (unsigned long)LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)));

    LED::shuffleArray(LED::State.PixelOrder, LED_NUM - LED_HB_NUM_FAKE);

    const int ledlimit  = LED_NUM - LED_HB_NUM_FAKE;
    const int hbLimit   = LED_NUM_TOTAL;
    const int startMode = EE::Get(EE_TV_RANDOM_COLOR_START);
    const int hbHalf    = LED_HB_NUM >> 1;
    const bool hbDual    = EE::Get(EE_HB_DUAL_COLOR);
    DIF_Colorx difColor;
    bool       haveDifColor = false;

    if (startMode == 2) {
        CHSV colorA, colorB;
        LED::getHarmoniousPair(colorA, colorB);

        uint8_t r1, g1, b1, r2, g2, b2;
        LED::HsvToRgb(colorA.h, r1, g1, b1);
        LED::HsvToRgb(colorB.h, r2, g2, b2);

        LED::setDualColorMapping(r1, g1, b1, r2, g2, b2);
        difColor = { r1, g1, b1, r2, g2, b2 };
        haveDifColor = true;
    }

    if (startMode == 1) {
        uint16_t sumR = 0, sumG = 0, sumB = 0;
        for (int i = 0; i < ledlimit; i++) {
            const uint32_t rgb = LED::getRandomColor();
            LED::State.StoredColor[i] = CRGB((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
            sumR += LED::State.StoredColor[i].r; sumG += LED::State.StoredColor[i].g; sumB += LED::State.StoredColor[i].b;
        }
        const uint8_t avgR = sumR / ledlimit, avgG = sumG / ledlimit, avgB = sumB / ledlimit;
        difColor = { avgR, avgG, avgB, avgR, avgG, avgB };
        haveDifColor = true;
    }

    if (!haveDifColor) {
        if (hbDual) {
            const CRGB &cL = LED::State.StoredColor[LED::UCOM(0)];
            const CRGB &cR = LED::State.StoredColor[LED::UCOM(1)];
            difColor = { cL.r, cL.g, cL.b, cR.r, cR.g, cR.b };
        } else {
            uint16_t sumR = 0, sumG = 0, sumB = 0;
            for (int i = 0; i < LED_COM_NUM; i++) {
                const CRGB &c = LED::State.StoredColor[LED::COM(i)];
                sumR += c.r; sumG += c.g; sumB += c.b;
            }
            const uint8_t avgR = sumR / LED_COM_NUM, avgG = sumG / LED_COM_NUM, avgB = sumB / LED_COM_NUM;
            difColor = { avgR, avgG, avgB, avgR, avgG, avgB };
        }
        haveDifColor = true;
    }

    memcpy(LED::State.TargetColor, LED::State.StoredColor, ledlimit * sizeof(CRGB));

    for (int i = ledlimit; i < hbLimit; i++) {
        int ref = LED_START_I_HB;
        if (startMode == 1 && i == LED_START_I_HB) {
            const uint32_t rgb = LED::getRandomColor();
            LED::State.StoredColor[ref] = CRGB((uint8_t)(rgb >> 16), (uint8_t)(rgb >> 8), (uint8_t)rgb);
        }
        if (hbDual) {
            ref = (i < (LED_START_I_HB + hbHalf)) ? LED::UCOM(0) : LED::UCOM(1);
        }
        LED::State.TargetColor[i] = LED::State.StoredColor[ref];
    }

    MOTION::State.Status = motOFF;
    APP::Am.Status     = false;
    APP::updStatus("TV::On");

    HB::State.Phase   = 0; HB::State.ParamA   = 0;
    TV::State.Transitioning = true;

    SCHED::KillAllUnlocked();
    HB::EndTask();

    DIF::AutoOn(EE_DIF_MODE_TV, haveDifColor ? &difColor : NULL);
    SCHED::TaskId newId = SCHED::AddTask("TV_On", "T_EFFECT_TV_ON", T_EFFECT_TV_ON, SCHED::Unit::MS,
                            LED::getLuxAdaptDelay(EE::Get(EE_TV_ON_BR_CL_DEL)), 1, false);
    SCHED::Scratch &S = SCHED::ScratchFor(newId);
    S.phase = 0; S.paramA = 0; S.paramB = 0;
}

const EffectHandler TV_ON_HANDLERS[] = {
    T_EFFECT_TV_ON_Default,
    T_EFFECT_TV_ON_1_RandomStatic,
    T_EFFECT_TV_ON_2_MidToOutSep,
    T_EFFECT_TV_ON_3_MidToOutAll,
    T_EFFECT_TV_ON_4_5_HalfRun,
    T_EFFECT_TV_ON_4_5_HalfRun,
    T_EFFECT_TV_ON_6_7_MidToExt,
    T_EFFECT_TV_ON_6_7_MidToExt,
    T_EFFECT_TV_ON_8_ComEffect,
    T_EFFECT_TV_ON_9_QuadPointHB,
    T_EFFECT_TV_ON_10_LiquidFill,
    T_EFFECT_TV_ON_11_PixelBoot
};
const int TV_ON_HANDLERS_COUNT = sizeof(TV_ON_HANDLERS) / sizeof(TV_ON_HANDLERS[0]);

const EffectHandler HB_ON_HANDLERS[] = {
    T_EFFECT_H_FadeOn,
    T_EFFECT_H_CenterBloom,
    T_EFFECT_H_LinearSweep,
    T_EFFECT_H_QuadPoint
};
const int HB_ON_HANDLERS_COUNT = sizeof(HB_ON_HANDLERS) / sizeof(HB_ON_HANDLERS[0]);

const EffectHandler TV_OFF_HANDLERS[] = {
    T_EFFECT_TV_OFF_Default,
    T_EFFECT_TV_OFF_1_DelayWTvOff,
    T_EFFECT_TV_OFF_2_DelayAll,
    T_EFFECT_TV_OFF_3_SlowTvSequential,
    T_EFFECT_TV_OFF_4_5_Countdown,
    T_EFFECT_TV_OFF_4_5_Countdown,
    T_EFFECT_TV_OFF_6_RandomHalf,
    T_EFFECT_TV_OFF_7_QuadPointHB
};
const int TV_OFF_HANDLERS_COUNT = sizeof(TV_OFF_HANDLERS) / sizeof(TV_OFF_HANDLERS[0]);

} // namespace TV
