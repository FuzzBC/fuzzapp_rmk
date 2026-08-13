#include "Hb.h"
#include "Globals.h"
#include "Led.h"
#include "Eeprom.h"
#include "Udpraw.h"
#include "AppLink.h"
#include "Debug.h"
#include <math.h>

namespace HB {

void T_EFFECT_HB(SCHED::TaskId taskId) {
    int hbEffIdx = EE::Get(EE_HB_EFFECT);
    DLOGV_HB("tick, effect index [%d]", hbEffIdx);
    if (hbEffIdx > 0 && hbEffIdx <= EFFECT_HANDLERS_COUNT) {
        EFFECT_HANDLERS[hbEffIdx - 1](taskId);
    } else {
        SCHED::KillId(taskId);
    }
}

/** Effect 1 - moving 3-pixel white highlight across the strip. */
void T_EFFECT_HB_1_WhiteMove(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num     = LED_HB_NUM;
    const int rawHbBr = LED::State.StoredBrightness[LED_START_I_HB];
    int dotBr = LED::getLuxBrightness(rawHbBr) * 2;
    if (dotBr > 255) dotBr = 255;
    const int bgBr = LED::getLuxBrightness(rawHbBr);

    if (S.phase <= 1) {
        S.phase = 1;
        const int d1 = S.paramA;
        for (int i = 0; i < num; i++) {
            int p = LED::HB(i);
            if (i == d1 || i == d1 + 1 || i == d1 + 2) {
                LED::setPixel(p, 255, 255, 255, dotBr, false);
            } else {
                LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, bgBr, false);
            }
        }
        LED::Show();

        if (d1 >= num) {
            S.phase = 2;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, random(5000, 10000));
        } else {
            S.paramA++;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
        }
    } else if (S.phase == 2) {
        S.paramA = 0;
        S.phase = 1;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 2 - sine-wave pulse mimicking a heartbeat. */
void T_EFFECT_HB_2_Heartbeat(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int  num     = LED_HB_NUM;
    const bool dual    = EE::Get(EE_HB_DUAL_COLOR);
    const int  baseLux = LED::HB_GetBaseBr();
    int        curBr   = baseLux;
    int        animationAngle = S.paramA;

    if (S.phase == 0 && animationAngle == 0) HB::State.Phase = random(2, 5);

    if (S.phase < HB::State.Phase) {
        float angle = animationAngle * (PI / 180.0f);
        curBr = baseLux + (int)(baseLux * sin(angle));
    }

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int ref = (dual && i >= (num >> 1)) ? LED::UCOM(1)
                : (dual                    ? LED::UCOM(0)
                                           : LED_START_I_HB);
        LED::setPixel(p, LED::State.StoredColor[ref].r, LED::State.StoredColor[ref].g, LED::State.StoredColor[ref].b, curBr, false);
    }
    LED::Show();

    if (S.phase < HB::State.Phase) {
        animationAngle += 20;
        if (animationAngle >= 180) {
            animationAngle = 0;
            S.phase++;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, random(60, 150));
        } else {
            SCHED::SetInterval(taskId, SCHED::Unit::MS, 15);
        }
    } else {
        S.phase = 0; animationAngle = 0;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(1500, 4500));
    }

    S.paramA = animationAngle;
}

/** Effect 3 - sine-pulse on a randomly selected segment of the strip. */
void T_EFFECT_HB_3_RandomFade(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.paramA = 1;
        S.paramB = 0;
        int wide = random(3, 26);
        int pos  = random(0, num - wide);
        S.phase = (pos << 8) | wide;
    }

    const int pos   = S.phase >> 8;
    const int wide  = S.phase & 0xFF;
    float     angle = S.paramB * (PI / 180.0f);
    int       curBr = (int)(maxBr * sin(angle));

    for (int i = 0; i < num; i++) {
        bool isPart = (i >= pos && i < pos + wide);
        int  tgtBr  = isPart ? curBr : 0;
        int  p      = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    S.paramB += 4;
    if (S.paramB >= 180) {
        S.paramB = 0;
        int nWide = random(3, 26);
        int nPos  = random(0, num - nWide);
        S.phase = (nPos << 8) | nWide;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(100, 400));
    } else {
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 4 - moving dark shadow window travelling across the strip. */
void T_EFFECT_HB_4_TravelingShadow(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();
    const int minBr = (maxBr * 40) / 100;

    if (S.paramA == 0) {
        S.paramA = 1;
        S.phase  = random(5, 50);
        S.paramB = 0;
    }

    const int shadowWidth = S.phase;
    const int shadowPos   = S.paramB - shadowWidth;

    for (int i = 0; i < num; i++) {
        bool isShadow = (i >= shadowPos && i < shadowPos + shadowWidth);
        int  tgtBr    = isShadow ? minBr : maxBr;
        int  p        = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    S.paramB++;
    if (S.paramB > num + shadowWidth) {
        S.paramA = 0;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(2000, 5000));
    } else {
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 5 - ripple expanding outward from a random drop centre. */
void T_EFFECT_HB_5_ExpandingRaindrops(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int inc   = EE::Get(EE_TV_ON_BR_CL_INC);
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.phase  = random(0, num);
        S.paramB = 0;
        S.paramA = 1;
    }

    const int dropCenter = S.phase;

    for (int i = 0; i < num; i++) {
        int dist  = abs(i - dropCenter);
        int tgtBr = 0;
        if (dist <= S.paramB) {
            int dropFade = maxBr - (dist * (maxBr / 10));
            tgtBr = (dropFade < 0) ? 0 : dropFade;
        }
        int p = LED::HB(i);
        LED::TG_BRIGHTNESS(p, tgtBr, inc, false);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
    }
    LED::Show();

    S.paramB++;
    if (S.paramB > 10) {
        S.paramA = 0;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(200, 800));
    } else {
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 6 - animated flowing colour palette wipe. */
void T_EFFECT_HB_6_Colors(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.paramA = 1;
        S.paramB = 0;
        S.phase  = 0;
        randomSeed(millis());
    }

    const int colorCount = 8;
    const int totalScale = colorCount * 256;

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int pos = (i * 60 - S.phase) % totalScale;
        if (pos < 0) pos += totalScale;

        int idx1     = pos >> 8;
        int idx2     = (idx1 + 1) % colorCount;
        int fraction = pos & 0xFF;

        byte r1 = (byte)(idx1 * 31 + 50);
        byte g1 = (byte)(idx1 * 77 + 100);
        byte b1 = (byte)(idx1 * 133);
        byte r2 = (byte)(idx2 * 31 + 50);
        byte g2 = (byte)(idx2 * 77 + 100);
        byte b2 = (byte)(idx2 * 133);

        byte r = (r1 * (255 - fraction) + r2 * fraction) >> 8;
        byte g = (g1 * (255 - fraction) + g2 * fraction) >> 8;
        byte b = (b1 * (255 - fraction) + b2 * fraction) >> 8;

        LED::setPixel(p, r, g, b, (i <= S.paramB) ? maxBr : 0, false);
    }
    LED::Show();

    if (S.paramB < num) S.paramB++;
    S.phase += 12;
    if (S.phase >= totalScale) S.phase = 0;

    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/** Effect 7 - shooting star with a random-length fading tail. */
void T_EFFECT_HB_7_ShootingStarRandom(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num    = LED_HB_NUM;
    const int maxLen = num >> 1;
    const int maxBr  = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.paramA = 1;
        S.paramB = 0;
        HB::State.Phase = random(5, maxLen + 1);
    }

    const int tailLen = HB::State.Phase;

    for (int i = 0; i < num; i++) {
        int p     = LED::HB(i);
        int tgtBr = 0;
        if (i <= S.paramB && i > S.paramB - tailLen) {
            int dist          = S.paramB - i;
            int totalDrop     = (maxBr * 60) / 100;
            int intensityDrop = (dist * totalDrop) / (tailLen - 1);
            tgtBr = maxBr - intensityDrop;
            if (tgtBr < 0) tgtBr = 0;
        }
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, tgtBr, false);
    }
    LED::Show();

    S.paramB++;
    if (S.paramB > num + tailLen) {
        S.paramA = 0;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(1000, 3000));
    } else {
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 8 - random single-pixel colour burst sparkles. */
void T_EFFECT_HB_8_RandomSparklingPop(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num     = LED_HB_NUM;
    const int inc     = EE::Get(EE_TV_ON_BR_CL_INC);
    const int baseLux = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.phase  = random(0, num);
        S.paramB = random(0, 7);
        S.paramA = 1;
    }

    static const uint8_t sparkColors[7][3] = {
        {255,   0,   0}, {  0, 255,   0}, {  0,   0, 255}, {255, 255,   0},
        {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
    };

    for (int i = 0; i < num; i++) {
        int p = LED::HB(i);
        if (i == S.phase) {
            const uint8_t *c = sparkColors[S.paramB];
            LED::TG_BRIGHTNESS(p, 255, inc, false);
            LED::setPixel(p, c[0], c[1], c[2], LED::State.CurrentBrightness[p], false);
        } else {
            LED::TG_BRIGHTNESS(p, baseLux, inc, false);
            LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, LED::State.CurrentBrightness[p], false);
        }
    }
    LED::Show();

    if (LED::State.CurrentBrightness[LED::HB(S.phase)] >= 250) {
        S.paramA = 0;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, random(50, 200));
    } else {
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
    }
}

/** Effect 9 - subtle random per-pixel twinkle on a static colour base. */
void T_EFFECT_HB_9_GlidingAurora(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num     = LED_HB_NUM;
    const int baseLux = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        LED::HB_SetFromPreColor(baseLux, false);
        S.paramA = 1;
    }

    for (int j = 0; j < 3; j++) {
        int i       = random(0, num);
        int p       = LED::HB(i);
        int twinkBr = constrain(baseLux + random(-30, 30), 0, 255);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, twinkBr, false);
    }

    LED::HB_SetPixelFromPreColor(random(0, num), baseLux);

    LED::Show();
    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/** Effect 10 - Matrix-style glitch: dim ambient glow with random bright blocks. */
void T_EFFECT_HB_10_TheGlitchMatrix(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) S.paramA = 1;

    LED::HB_SetFromPreColor(maxBr / 20, false);

    const int blockStart = random(0, num);
    const int blockSize  = random(1, 5);
    for (int j = 0; j < blockSize; j++) {
        int idx = blockStart + j;
        if (idx < num) {
            int p = LED::HB(idx);
            LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, maxBr, false);
        }
    }
    LED::Show();

    int baseSpeed   = EE::Get(EE_HB_EFFECT_SPEED);
    int randomDelay = constrain(baseSpeed + random(-10, 50), 10, 32767);
    SCHED::SetInterval(taskId, SCHED::Unit::MS, randomDelay);
}

/** Effect 11 - flowing colour plasma with per-pixel brightness jitter. */
void T_EFFECT_HB_11_StochasticPlasma(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) {
        S.paramA = 1;
        S.phase  = random(0, 256);
    }

    for (int i = 0; i < num; i++) {
        int p   = LED::HB(i);
        int cur = LED::State.CurrentBrightness[p];
        int rS  = random(0, 100);
        int nBr = (rS > 85) ? cur + 10 : (rS < 15) ? cur - 10 : cur;
        nBr = constrain(nBr, maxBr / 5, maxBr);

        uint8_t h = (uint8_t)(S.phase - (i * 2));
        uint8_t r, g, b;
        LED::HsvToRgb(h, r, g, b);
        LED::setPixel(p, r, g, b, nBr, false);
    }
    LED::Show();

    S.phase++;
    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/** Effect 12 - Matrix digital rain: brightness values fall down the strip. */
void T_EFFECT_HB_12_DigitalRain(SCHED::TaskId taskId) {
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    for (int i = num - 1; i > 0; i--) {
        int p     = LED::HB(i);
        int pPrev = LED::HB(i - 1);
        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b,
                     LED::State.CurrentBrightness[pPrev], false);
    }

    int  firstP = LED::HB(0);
    uint8_t b   = (random(0, 100) > 92) ? maxBr : 0;
    LED::setPixel(firstP, LED::State.TargetColor[firstP].r, LED::State.TargetColor[firstP].g, LED::State.TargetColor[firstP].b, b, false);

    LED::Show();
    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/** Effect 13 - two interfering sine waves creating a dual-pulse pattern. */
void T_EFFECT_HB_13_DualPulse(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int num   = LED_HB_NUM;
    const int maxBr = LED::HB_GetBaseBr();

    if (S.paramA == 0) { S.paramA = 1; S.phase = 0; }

    const float phase1 = (float)S.phase *  0.10f;
    const float phase2 = (float)S.phase * -0.08f;

    for (int i = 0; i < num; i++) {
        int p  = LED::HB(i);
        int ri = (num - 1 - i);

        float waveA = sin(phase1 + (ri * 0.5f)) * 0.5f + 0.5f;
        float waveB = sin(phase2 + (ri * 0.3f)) * 0.5f + 0.5f;
        int   br    = (int)((waveA + waveB) * 0.5f * maxBr);
        if (br > maxBr) br = maxBr;

        LED::setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
    }

    LED::Show();
    S.phase++;
    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

/** Effect 14 - smooth rainbow spectrum with full-strip breathing pulse. */
void T_EFFECT_HB_14_RainbowWavePulse(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int  num    = LED_HB_NUM;
    const int  maxBr  = LED::HB_GetBaseBr();

    if (S.paramA == 0) S.paramA = 1;

    float envAngle = (float)S.phase * (PI / 90.0f);
    float envelope = 0.35f + 0.65f * (sinf(envAngle) * 0.5f + 0.5f);
    int   envBr    = (int)(maxBr * envelope);
    if (envBr < 1) envBr = 1;

    uint8_t hueOffset = (uint8_t)(S.paramB & 0xFF);

    for (int i = 0; i < num; i++) {
        int     p = LED::HB(i);
        uint8_t h = (uint8_t)(hueOffset + (i * 255 / num));
        uint8_t r, g, b;
        LED::HsvToRgb(h, r, g, b);
        LED::setPixel(p, r, g, b, envBr, false);
    }
    LED::Show();

    S.phase = (S.phase + 1) % 180;
    S.paramB++;
    SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_HB_EFFECT_SPEED));
}

void EndTask() {
    if (HB::State.TaskID == SCHED::TASK_ID_NONE) return;
    DLOG_HB("ending task id [%d]", (int)HB::State.TaskID);
    SCHED::KillId(HB::State.TaskID);
    HB::State.TaskID = SCHED::TASK_ID_NONE;
}

void StartEffect(bool silent, bool reset, bool fast) {
    (void)silent;
    if (UDPRAW::State.Status || APP::Am.Status) return;

    if (reset) HB::State.Phase = 0;   // fresh cycle - the task's own Scratch resets itself on registration
    DLOG_HB("starting effect [%d] reset=[%d] fast=[%d]", EE::Get(EE_HB_EFFECT), (int)reset, (int)fast);

    HB::State.TaskID = SCHED::AddTask("HB_StartEffect", "T_EFFECT_HB", T_EFFECT_HB, SCHED::Unit::MS,
                           EE::Get(EE_HB_EFFECT_SPEED), fast ? 0 : random(2000, 5000), false);
}

const EffectHandler EFFECT_HANDLERS[] = {
    T_EFFECT_HB_1_WhiteMove, T_EFFECT_HB_2_Heartbeat, T_EFFECT_HB_3_RandomFade,
    T_EFFECT_HB_4_TravelingShadow, T_EFFECT_HB_5_ExpandingRaindrops, T_EFFECT_HB_6_Colors,
    T_EFFECT_HB_7_ShootingStarRandom, T_EFFECT_HB_8_RandomSparklingPop, T_EFFECT_HB_9_GlidingAurora,
    T_EFFECT_HB_10_TheGlitchMatrix, T_EFFECT_HB_11_StochasticPlasma, T_EFFECT_HB_12_DigitalRain,
    T_EFFECT_HB_13_DualPulse, T_EFFECT_HB_14_RainbowWavePulse,
};
const int EFFECT_HANDLERS_COUNT = sizeof(EFFECT_HANDLERS) / sizeof(EFFECT_HANDLERS[0]);

} // namespace HB
