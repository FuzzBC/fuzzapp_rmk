#include "Led.h"
#include "Globals.h"
#include "Scheduler.h"
#include "Lisens.h"
#include "Eeprom.h"
#include "AppLink.h"
#include "DifLink.h"
#include "Hb.h"
#include "Motion.h"
#include "Tv.h"
#include "Debug.h"

namespace LED {

/* -- Setup / Refresh --------------------------------------------------- */

void Setup() {
    stripFront.begin();
    stripBack.begin();
    stripHB.begin();
    stripFront.show();
    stripBack.show();
    stripHB.show();

    setAll(0, 0, 0, 0);

    for (int i = 0; i < LED_NUM - LED_HB_NUM_FAKE; i++) LED::State.PixelOrder[i]   = i;
    for (int i = 0; i < LED_HB_NUM;               i++) LED::State.HeartbeatOrder[i] = i;

    shuffleArray(LED::State.PixelOrder,   LED_NUM - LED_HB_NUM_FAKE);
    shuffleArray(LED::State.HeartbeatOrder, LED_HB_NUM);

    SCHED::AddTask("LED_Setup", "LED_Refresh", Refresh, SCHED::Unit::MS, LED_REFRESH_TIME, 0, true);
}

void Refresh(uint32_t now) {
    stripFront.show();
    stripBack.show();
    stripHB.show();
    LED::State.NeedsUpdate = false;
    LED::State.LastUpdateTime = now;
    LED::State.LastRefreshTime = now;
}

void Refresh(SCHED::TaskId taskId) {
    (void)taskId;
    if (UDPRAW::State.Status) return;
    if (LED::State.LastRefreshTime + LED_REFRESH_TIME > TimeNow) return;
    Show();
}

void Show() {
    LED::State.NeedsUpdate = true;
}

static inline void ForceShow() {
    stripFront.show();
    stripBack.show();
    stripHB.show();
    LED::State.NeedsUpdate = false;
    LED::State.LastUpdateTime = TimeNow;
    LED::State.LastRefreshTime = TimeNow;
}

/** Shared final step for "stop actively driving the strip": clear every
    pending ramp target, blank the buffer, kill every non-locked task, then
    force an immediate hardware push - this exact ordering was the subject
    of six separate bugfix cycles in production before converging here. */
void ForceOffAndKillTasks(const char *killTag) {
    (void)killTag;
    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        LED::State.TargetColor[i] = CRGB(0, 0, 0);
    }
    LED::setAll(0, 0, 0, 0);
    SCHED::KillAllUnlocked();
    LED::ForceShow();
}

/* -- Zone index helpers ------------------------------------------------ */

uint16_t TV(uint8_t i) {
    if (i >= LED_TV_NUM) return LED_START_I_TV + (LED_TV_NUM - 1);
    return LED_START_I_TV + i;
}
uint16_t COM(uint8_t i) {
    if (i >= LED_COM_NUM) return LED_START_I_COM + (LED_COM_NUM - 1);
    return LED_START_I_COM + i;
}
uint16_t UCOM(uint8_t i) {
    if (i >= LED_UCOM_NUM) return LED_START_I_UCOM;
    return LED_START_I_UCOM + (LED_UCOM_NUM - 1u - i);
}
uint16_t BED(uint8_t i) {
    if (i >= LED_BED_NUM) return LED_START_I_BED + (LED_BED_NUM - 1);
    return LED_START_I_BED + i;
}
uint16_t LAMP(uint8_t i) {
    if (i >= LED_LAMP_NUM) return LED_START_I_LAMP + (LED_LAMP_NUM - 1);
    return LED_START_I_LAMP + i;
}
uint16_t HB(uint16_t i) {
    if (i >= LED_HB_NUM) return LED_START_I_HB + (LED_HB_NUM - 1);
    return LED_START_I_HB + i;
}
bool IsHB(int l) { return (l >= LED_START_I_HB); }
bool IsSelected(int l) {
    if (l >= LED_NUM) return false;
    return BIT_TEST(LED::State.Selected, l);
}

int MotionZoneCount(int z) {
    return (z == 0) ? LED_BED_NUM : (z == 1 ? LED_COM_NUM : LED_HB_NUM);
}
uint16_t MotionZonePixel(int z, uint16_t i) {
    return (z == 0) ? BED((uint8_t)i) : (z == 1 ? COM((uint8_t)i) : HB(i));
}

/* -- Core pixel operations ------------------------------------------------ */

bool TG_Step(uint8_t &current, uint8_t target, uint8_t inc) {
    if (current == target) return false;
    int diff = target - current;
    if (abs(diff) <= inc) current = target;
    else                  current += (diff > 0) ? inc : -(int)inc;
    return true;
}

void H_writeStripPixel(int gIdx, uint8_t r, uint8_t g, uint8_t b) {
    if (gIdx < (LED_START_I_UCOM + LED_UCOM_NUM)) {
        stripFront.setPixelColor(gIdx, stripFront.Color(r, g, b));
    } else if (gIdx < LED_START_I_HB) {
        stripBack.setPixelColor(gIdx - LED_START_I_BED, stripBack.Color(r, g, b));
    } else {
        stripHB.setPixelColor(gIdx - LED_START_I_HB, stripHB.Color(r, g, b));
    }
}

CRGB H_readStripPixel(int gIdx) {
    uint32_t c;
    if (gIdx < (LED_START_I_UCOM + LED_UCOM_NUM)) c = stripFront.getPixelColor(gIdx);
    else if (gIdx < LED_START_I_HB)               c = stripBack.getPixelColor(gIdx - LED_START_I_BED);
    else                                          c = stripHB.getPixelColor(gIdx - LED_START_I_HB);
    return CRGB((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

/** Called by ~115 call sites - every effect that changes a pixel calls this
    right after TG_BRIGHTNESS()/TG_COLOR() report a step. */
void setPixel(int p, int r, int g, int b, int brVal, bool s) {
    if (p >= LED_NUM_TOTAL) return;

    // Hard cap: HB must never exceed HB_BRIGHTNESS_MAX, main strip never
    // exceeds LED_BRIGHTNESS_MAX, no matter what produced brVal upstream -
    // the backstop for any caller that writes brightness directly.
    if (IsHB(p)) {
        if (brVal > HB_BRIGHTNESS_MAX) brVal = HB_BRIGHTNESS_MAX;
    } else {
        if (brVal > LED_BRIGHTNESS_MAX) brVal = LED_BRIGHTNESS_MAX;
    }

    uint8_t scaledR = (uint8_t)((r * brVal + 1) >> 8);
    uint8_t scaledG = (uint8_t)((g * brVal + 1) >> 8);
    uint8_t scaledB = (uint8_t)((b * brVal + 1) >> 8);

    uint8_t stR = (brVal > 0) ? (uint8_t)r : 0;
    uint8_t stG = (brVal > 0) ? (uint8_t)g : 0;
    uint8_t stB = (brVal > 0) ? (uint8_t)b : 0;

    // Colour-changed check deliberately ignores brightness-only steps - a
    // fade at constant hue shouldn't spam the app's colour view every tick;
    // stR/stG/stB already collapse to black at brVal==0 so on/off
    // transitions still register as a colour change here.
    bool changed = (LED::State.CurrentColor[p].r != stR || LED::State.CurrentColor[p].g != stG || LED::State.CurrentColor[p].b != stB);

    LED::State.CurrentColor[p]      = CRGB(stR, stG, stB);
    LED::State.CurrentBrightness[p] = brVal;

    if (p == LED_START_I_HB) { scaledR = 0; scaledG = 0; scaledB = 0; }

    LED::H_writeStripPixel(p, scaledR, scaledG, scaledB);

    if (changed) {
        int markIdx = (p >= LED_START_I_HB) ? LED_START_I_HB : p;
        MarkChanged(markIdx);
    }

    if (s) Show();
}

void setAll(int r, int g, int b, int br) {
    for (int i = 0; i < LED_NUM_TOTAL; i++) setPixel(i, r, g, b, br, false);
    Show();
}

void Select(int l, bool state) {
    if (l >= LED_NUM) return;
    if (state) BIT_SET(LED::State.Selected, l);
    else       BIT_CLEAR(LED::State.Selected, l);
}

/** Backstop: HB must never exceed HB_BRIGHTNESS_MAX, main strip never
    exceeds LED_BRIGHTNESS_MAX (mirrors setPixel()'s own cap) - without
    clamping here, a caller's lux-boosted target above the ceiling would step
    CurrentBrightness toward an unreachable value forever (this froze the
    TV-on HB Quad-Point/Center-Bloom effects partway through in production). */
bool TG_BRIGHTNESS(int led, uint8_t brVal, uint8_t inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) return false;
    {
        const uint8_t cap = IsHB(led) ? HB_BRIGHTNESS_MAX : LED_BRIGHTNESS_MAX;
        if (brVal > cap) brVal = cap;
    }
    inc = (inc > 0) ? inc : 1;
    const uint8_t before = LED::State.CurrentBrightness[led];
    bool changed = TG_Step(LED::State.CurrentBrightness[led], brVal, inc);

    if (changed) {
        const bool wasOff = (before == 0);
        const bool isOff  = (LED::State.CurrentBrightness[led] == 0);
        if (wasOff != isOff) {
            int markIdx = (led >= LED_START_I_HB) ? LED_START_I_HB : led;
            MarkChanged(markIdx);
        }
    }
    if (lisensReset) LISENS::ResetTime();
    return changed;
}

bool TG_BRIGHTNESS_ToTargetColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset) {
    if (TG_BRIGHTNESS(led, brVal, inc, lisensReset)) {
        setPixel(led, LED::State.TargetColor[led].r, LED::State.TargetColor[led].g, LED::State.TargetColor[led].b, LED::State.CurrentBrightness[led], false);
        return true;
    }
    return false;
}

bool TG_BRIGHTNESS_ToCurrentColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset) {
    if (TG_BRIGHTNESS(led, brVal, inc, lisensReset)) {
        setPixel(led, LED::State.CurrentColor[led].r, LED::State.CurrentColor[led].g, LED::State.CurrentColor[led].b, LED::State.CurrentBrightness[led], false);
        return true;
    }
    return false;
}

bool TG_BRIGHTNESS_ToStoredColor(int led, uint8_t brVal, uint8_t inc, bool lisensReset) {
    if (TG_BRIGHTNESS(led, brVal, inc, lisensReset)) {
        setPixel(led, LED::State.StoredColor[led].r, LED::State.StoredColor[led].g, LED::State.StoredColor[led].b, LED::State.CurrentBrightness[led], false);
        return true;
    }
    return false;
}

bool TG_COLOR(int led, int r, int g, int b, int inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) return false;
    inc = (inc > 0) ? inc : 1;
    bool changed = false;
    if (TG_Step(LED::State.CurrentColor[led].r, r, inc)) changed = true;
    if (TG_Step(LED::State.CurrentColor[led].g, g, inc)) changed = true;
    if (TG_Step(LED::State.CurrentColor[led].b, b, inc)) changed = true;
    if (changed) {
        int markIdx = (led >= LED_START_I_HB) ? LED_START_I_HB : led;
        MarkChanged(markIdx);
    }
    if (lisensReset) LISENS::ResetTime();
    return changed;
}

bool TG_TEMPCOLOR(int led, int r, int g, int b, int inc, bool lisensReset) {
    if (led >= LED_NUM_TOTAL) return false;
    inc = (inc > 0) ? inc : 1;
    bool changed = false;
    if (TG_Step(LED::State.TargetColor[led].r, r, inc)) changed = true;
    if (TG_Step(LED::State.TargetColor[led].g, g, inc)) changed = true;
    if (TG_Step(LED::State.TargetColor[led].b, b, inc)) changed = true;
    if (lisensReset) LISENS::ResetTime();
    return changed;
}

bool FadeAllToZero(uint8_t brInc) {
    bool anyChanged = false;
    for (int i = 0; i < LED_NUM_TOTAL; i++) {
        if (TG_BRIGHTNESS(i, 0, brInc, false)) {
            setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
            anyChanged = true;
        }
    }
    return anyChanged;
}

/* -- change tracking ------------------------------------------------------ */

void MarkChanged(int ledIdx) {
    if (ledIdx >= 0 && ledIdx < LED_NUM) BIT_SET(LED::State.Changed, ledIdx);
}

void MarkChangedRange(int start, int end) {
    if (start < 0) start = 0;
    if (end >= LED_NUM) end = LED_NUM - 1;
    for (int i = start; i <= end; i++) BIT_SET(LED::State.Changed, i);
}

uint8_t getChangedCount() {
    int count = 0;
    int bytes = (LED_NUM + 7) >> 3;
    for (int i = 0; i < bytes; i++) count += __builtin_popcount(LED::State.Changed[i]);
    return count;
}

bool IsChanged(int ledIdx) {
    if (ledIdx >= 0 && ledIdx < LED_NUM) return BIT_TEST(LED::State.Changed, ledIdx);
    return false;
}

/* -- lux compensation ------------------------------------------------------ */

uint8_t getLuxBrightness(int b) {
    static const int numThresholds = 3;
    static const int maxLuxLevel   = numThresholds + 1;
    int baseBr    = map(b, 0, APP_BRIGHT, 3, LED_BRIGHTNESS_MAX);
    int luxOffset = map(LISENS::State.Lux, 1, maxLuxLevel, 0, EE::Get(EE_OTHER_BRIGHTNESS_AUTO));
    return (uint8_t)constrain(baseBr + luxOffset, 0, 255);
}

uint16_t getLuxAdaptFactor() {
    int f = LISENS::State.Lux;
    return (f < 1) ? 1 : (uint16_t)f;
}

uint16_t getLuxAdaptDelay(uint16_t del) {
    uint16_t d = del / getLuxAdaptFactor();
    return (d < 1) ? 1 : d;
}

uint16_t getLuxAdaptInc(uint16_t inc) {
    uint32_t v = (uint32_t)inc * getLuxAdaptFactor();
    return (v > 255) ? 255 : (uint16_t)v;
}

/* -- HB helpers ------------------------------------------------------------ */

/** Called by _debug_all, T_LUX_BR_CHANGE, and 13/14 idle HB effects. */
int HB_GetBaseBr() {
    int br = getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);
    return (br > HB_BRIGHTNESS_MAX) ? HB_BRIGHTNESS_MAX : br;
}

void HB_SetAll(uint8_t r, uint8_t g, uint8_t b, uint8_t br, bool show) {
    for (int i = 0; i < LED_HB_NUM; i++) setPixel(HB(i), r, g, b, br, false);
    if (show) Show();
}

void HB_SetFromPreColor(uint8_t br, bool show) {
    for (int i = 0; i < LED_HB_NUM; i++) {
        int p = HB(i);
        setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
    }
    if (show) Show();
}

void HB_SetPixelFromPreColor(int i, uint8_t br) {
    int p = HB(i);
    setPixel(p, LED::State.TargetColor[p].r, LED::State.TargetColor[p].g, LED::State.TargetColor[p].b, br, false);
}

/* -- misc -------------------------------------------------------------------- */

void HsvToRgb(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (h < 85)       { r = 255 - h * 3; g = h * 3;        b = 0; }
    else if (h < 170) { h -= 85;  r = 0; g = 255 - h * 3;  b = h * 3; }
    else              { h -= 170; r = h * 3; g = 0;        b = 255 - h * 3; }
}

void getHarmoniousPair(CHSV &outA, CHSV &outB) {
    static const uint8_t offsets[] = { 128, 96, 160, 85, 170, 64 };
    uint8_t hA  = random(256);
    uint8_t off = offsets[random(sizeof(offsets))];
    uint8_t hB  = hA + off;
    outA = CHSV(hA, random(180, 255), random(180, 240));
    outB = CHSV(hB, random(180, 255), random(180, 240));
}

uint32_t getRandomColor() {
    uint16_t randSeed16 = random(65536);
    uint8_t  rand8      = (uint8_t)((randSeed16 * 2053 + 13849 + (randSeed16 >> 8)) & 0xFF);
    uint8_t randIndex = 0, x = 0, y = 0, d = 0;
    while (d < 42) {
        randIndex = random(256);
        x = abs(rand8 - randIndex);
        y = 255 - x;
        d = (x < y) ? x : y;
    }
    randIndex = 255 - randIndex;
    if (randIndex < 85) {
        return ((uint32_t)(255 - randIndex * 3) << 16) | (uint32_t)(randIndex * 3);
    } else if (randIndex < 170) {
        randIndex -= 85;
        return ((uint32_t)(randIndex * 3) << 8) | (255 - randIndex * 3);
    } else {
        randIndex -= 170;
        return ((uint32_t)(randIndex * 3) << 16) | ((uint32_t)(255 - randIndex * 3) << 8);
    }
}

void setRandomColor(bool z) {
    EE::Set(EE_TV_RANDOM_COLOR_START, z);
}

void setColorToSelected(uint8_t r, uint8_t g, uint8_t b) {
    if (APP::State.SelectedCacheDirty) APP::RefreshSelectedCache();
    for (int i = 0; i < APP::State.SelectedCount; i++) {
        int idx = APP::State.SelectedLedCache[i];
        LED::State.TargetColor[idx].r = r;
        LED::State.TargetColor[idx].g = g;
        LED::State.TargetColor[idx].b = b;
    }
}

void setBrightnessToSelected(uint8_t br) {
    if (APP::State.SelectedCacheDirty) APP::RefreshSelectedCache();
    for (int i = 0; i < APP::State.SelectedCount; i++) {
        LED::State.StoredBrightness[APP::State.SelectedLedCache[i]] = br;
    }
}

uint16_t getFpsLimitMs() {
    uint8_t idx = EE::Get(EE_OTHER_LED_FPS);
    if (idx >= LED_FPS_OPTIONS_TOTAL) idx = LED_FPS_DEFAULT_INDEX;
    return pgm_read_byte(&LED_FPS_TABLE[idx]);
}

bool ShouldRefresh(uint32_t now) {
    return LED::State.NeedsUpdate && (LED::State.LastUpdateTime == 0 || (uint32_t)(now - LED::State.LastUpdateTime) >= getFpsLimitMs());
}

void shuffleArray(uint8_t *array, int size) {
    for (int n = size - 1; n > 0; n--) {
        uint8_t r = random(n + 1);
        uint8_t t = array[n];
        array[n]  = array[r];
        array[r]  = t;
    }
}

/* -- zone colour helpers ---------------------------------------------------- */

void MOTION_SetColor(int r, int g, int b, int br) {
    const int  brVal   = getLuxBrightness(br);
    const bool skipCOM = (MOTION::State.Status == motBED);

    if (!skipCOM) for (int i = 0; i < LED_COM_NUM; i++) setPixel(COM(i), r, g, b, brVal, false);
    for (int i = 0; i < LED_BED_NUM; i++) setPixel(BED(i), r, g, b, brVal, false);
    HB_SetAll(r, g, b, brVal, false);
    Show();

    MOTION::State.Color.r = r;
    MOTION::State.Color.g = g;
    MOTION::State.Color.b = b;
    EE::MarkMotionChanged();
    EE::WriteTime();
}

void UDPRAW_SetColor(int r, int g, int b, int br) {
    const int brVal = getLuxBrightness(br);
    for (int i = 0; i < LED_COM_NUM;  i++) setPixel(COM(i),  r, g, b, brVal, false);
    for (int i = 0; i < LED_BED_NUM;  i++) setPixel(BED(i),  r, g, b, brVal, false);
    for (int i = 0; i < LED_LAMP_NUM; i++) setPixel(LAMP(i), r, g, b, brVal, false);
    HB_SetAll(r, g, b, brVal, false);

    // Show() alone wouldn't reach hardware here - Refresh() bails while
    // UDPRAW is active and this only fires on a discrete change, not per
    // packet, so an immediate push is cheap and correct.
    stripBack.show();
    stripHB.show();

    LED::State.StreamColor.r    = r;
    LED::State.StreamColor.g    = g;
    LED::State.StreamColor.b    = b;
    LED::State.StreamBrightness = br;
    EE::MarkUdpChanged();
    EE::WriteTime();
}

void setDualColorMapping(uint8_t r1, uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2) {
    struct ZoneInfo { int start; int count; int brIdx; };
    static const ZoneInfo zones[] = {
        { LED_START_I_TV,   LED_TV_NUM,   EE_TV_BR_TV   },
        { LED_START_I_COM,  LED_COM_NUM,  EE_TV_BR_COM  },
        { LED_START_I_UCOM, LED_UCOM_NUM, EE_TV_BR_UCOM },
        { LED_START_I_BED,  LED_BED_NUM,  EE_TV_BR_BED  },
        { LED_START_I_LAMP, LED_LAMP_NUM, EE_TV_BR_LAMP },
    };
    static const int NUM_ZONES = sizeof(zones) / sizeof(zones[0]);

    for (int z = 0; z < NUM_ZONES; z++) {
        const ZoneInfo &zone = zones[z];
        const int       half = zone.count >> 1;
        const uint8_t   br   = EE::Get(zone.brIdx);
        for (int i = 0; i < zone.count; i++) {
            int n = (zone.start == LED_START_I_UCOM) ? UCOM(i) : (zone.start + i);
            const bool left = (i < half);
            LED::State.StoredColor[n]      = left ? CRGB(r1, g1, b1) : CRGB(r2, g2, b2);
            LED::State.StoredBrightness[n] = br;
            EE::MarkColorChanged(n);
        }
    }
}

/* -- transition tasks -------------------------------------------------------- */

void T_LUX_BR_CHANGE(SCHED::TaskId taskId) {
    (void)taskId;
    bool anyChanged = false;
    const int limit   = LED_NUM_TOTAL;
    const int brInc   = EE::Get(EE_TV_ON_BR_CL_INC);   // raw EE - lux change is NOT speed-adapted
    const int hbTgtBr = HB_GetBaseBr();

    for (int i = 0; i < limit; i++) {
        const int targetBr = IsHB(i) ? hbTgtBr : getLuxBrightness(LED::State.StoredBrightness[i]);
        if (TG_BRIGHTNESS(i, targetBr, brInc, false)) {
            // setPixel() forces CurrentColor to black at brVal==0 - this call
            // only intends to re-render the SAME colour at a new brightness,
            // so snapshot+restore it around the call (see 00_PLAN.md's
            // "setPixel black-at-zero" problem note).
            const CRGB steppedColor = LED::State.CurrentColor[i];
            setPixel(i, steppedColor.r, steppedColor.g, steppedColor.b, LED::State.CurrentBrightness[i], false);
            LED::State.CurrentColor[i] = steppedColor;
            anyChanged = true;
        }
    }

    if (anyChanged) {
        Show();
    } else {
        SCHED::KillAllUnlocked();
        HB::EndTask();
        HB::StartEffect(true, false, false);
    }
}

void T_SMOOTH_CHANGE(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    bool moving = false;
    const int  inc              = EE::Get(EE_TV_ON_BR_CL_INC);
    const int  animationMode    = S.paramA;
    const bool isBrightnessMode = (animationMode == 1);

    if (S.phase == 0) S.phase = 1;

    if (APP::State.SelectedCacheDirty) APP::RefreshSelectedCache();

    for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
        const int ledN = APP::State.SelectedLedCache[cacheIdx];
        const bool isHB       = IsHB(ledN);
        const int  iterations = isHB ? LED_HB_NUM : 1;
        const int  targetBr   = isBrightnessMode ? getLuxBrightness(LED::State.StoredBrightness[ledN]) : 0;
        const bool  hbDual = isHB && isBrightnessMode && EE::Get(EE_HB_DUAL_COLOR);
        const int   halfPt = LED_HB_NUM >> 1;

        for (int i = 0; i < iterations; i++) {
            const int idx       = isHB ? HB(i) : ledN;
            bool      ledChanged = false;

            if (isBrightnessMode) {
                ledChanged = TG_BRIGHTNESS(idx, targetBr, inc, false);
            } else {
                ledChanged = TG_COLOR(idx, LED::State.TargetColor[ledN].r, LED::State.TargetColor[ledN].g, LED::State.TargetColor[ledN].b, inc);
            }

            if (ledChanged) {
                CRGB renderColor;
                if (hbDual) {
                    const int ref = (i < halfPt) ? UCOM(0) : UCOM(1);
                    renderColor = LED::State.StoredColor[ref];
                } else {
                    renderColor = isBrightnessMode ? LED::State.StoredColor[ledN] : LED::State.CurrentColor[idx];
                }
                const CRGB steppedColor = LED::State.CurrentColor[idx];
                setPixel(idx, renderColor.r, renderColor.g, renderColor.b, LED::State.CurrentBrightness[idx], false);
                if (!isBrightnessMode) LED::State.CurrentColor[idx] = steppedColor;
                moving = true;
            }
        }
    }

    if (moving) {
        Show();
        return;
    }

    SCHED::KillAllUnlocked();

    if (APP::Am.Status) {
        // Copy only the LEDs actually in THIS fade - a blanket copy would
        // silently confirm a transiently-wrong CurrentColor (e.g. from
        // T_LUX_BR_CHANGE's brightness-0 self-refresh) into permanent storage.
        for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
            const int ledN = APP::State.SelectedLedCache[cacheIdx];
            LED::State.AmbientBackgroundColor[ledN]      = LED::State.CurrentColor[ledN];
            LED::State.AmbientBackgroundBrightness[ledN] = LED::State.CurrentBrightness[ledN];
            BIT_SET(EE_AmbientChanged, ledN);
        }
    } else if (!isBrightnessMode) {
        for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
            const int ledN = APP::State.SelectedLedCache[cacheIdx];
            LED::State.StoredColor[ledN] = LED::State.CurrentColor[ledN];
            BIT_SET(EE_ColorChanged, ledN);
        }
        if (LED_NUM > LED_START_I_HB) {
            CRGB hbColor = LED::State.CurrentColor[LED_NUM - 1];
            for (int j = 0; j < LED_HB_NUM; j++) LED::State.TargetColor[HB(j)] = hbColor;
        }
    } else {
        for (int cacheIdx = 0; cacheIdx < APP::State.SelectedCount; cacheIdx++) {
            EE::MarkColorChanged(APP::State.SelectedLedCache[cacheIdx]);
        }
    }

    if (APP::Am.Status) {
        S.phase = 3;
        SCHED::AddTask("T_SMOOTH_CHANGE", "T_AMBIENT_MODE_ON", T_AMBIENT_MODE_ON, SCHED::Unit::MS,
                     EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                     (uint32_t)EE::Get(EE_OTHER_AMBIENT_MODE_TIME) * 60000, false);
    } else if (!TV::State.Status) {
        SCHED::AddTask("T_SMOOTH_CHANGE", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, SCHED::Unit::MS,
                     EE::Get(EE_OTHER_BR_CL_DEL) * 3,
                     S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
    }

    EE::WriteTime();
    APP::updDeltaColors();
    HB::StartEffect(true, false, false);
}

void T_DUAL_COLOR(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);

    if (S.phase == 1) {
        if (FadeAllToZero(EE::Get(EE_TV_ON_BR_CL_INC))) {
            Show();
            return;
        }
        uint8_t r1 = LED::State.TargetColor[0].r, g1 = LED::State.TargetColor[0].g, b1 = LED::State.TargetColor[0].b;
        uint8_t r2 = LED::State.TargetColor[1].r, g2 = LED::State.TargetColor[1].g, b2 = LED::State.TargetColor[1].b;
        setDualColorMapping(r1, g1, b1, r2, g2, b2);

        const int halfHb = LED_HB_NUM >> 1;
        for (int i = 0; i < LED_HB_NUM; i++) {
            LED::State.CurrentColor[HB(i)].r = LED::State.TargetColor[(i < halfHb) ? 0 : 1].r;
            LED::State.CurrentColor[HB(i)].g = LED::State.TargetColor[(i < halfHb) ? 0 : 1].g;
            LED::State.CurrentColor[HB(i)].b = LED::State.TargetColor[(i < halfHb) ? 0 : 1].b;
        }

        APP::updDeltaColors();
        S.phase = 2;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL));
        return;
    }

    if (S.phase == 2) {
        bool      moving    = false;
        const int fadeInc   = EE::Get(EE_TV_ON_BR_CL_INC);
        const int hbTgtBr   = getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);

        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            const bool isHB    = IsHB(i);
            const int  targetBr = isHB ? hbTgtBr : getLuxBrightness(LED::State.StoredBrightness[i]);
            if (TG_BRIGHTNESS(i, targetBr, fadeInc, false)) {
                uint8_t r = isHB ? LED::State.CurrentColor[i].r : LED::State.StoredColor[i].r;
                uint8_t g = isHB ? LED::State.CurrentColor[i].g : LED::State.StoredColor[i].g;
                uint8_t b = isHB ? LED::State.CurrentColor[i].b : LED::State.StoredColor[i].b;
                setPixel(i, r, g, b, LED::State.CurrentBrightness[i], false);
                moving = true;
            }
        }

        if (moving) { Show(); return; }

        SCHED::KillAllUnlocked();
        if (!TV::State.Status) {
            SCHED::AddTask("T_DUAL_COLOR", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, SCHED::Unit::MS,
                         EE::Get(EE_OTHER_BR_CL_DEL) * 3, S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
        }
        EE::WriteTime();
        APP::updDeltaColors();
        memcpy(LED::State.TargetColor, LED::State.CurrentColor, LED_NUM_TOTAL * sizeof(CRGB));
        HB::StartEffect(true, false, false);
    }
}

void T_SHAKE_DUAL_COLOR(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int totalLeds = LED_NUM - LED_HB_NUM_FAKE;
    const int halfHb   = LED_HB_NUM >> 1;

    if (S.phase == 1) {
        if (S.paramB < 20) {
            byte randR[2] = { (byte)random(256), (byte)random(256) };
            byte randG[2] = { (byte)random(256), (byte)random(256) };
            byte randB[2] = { (byte)random(256), (byte)random(256) };

            uint8_t tvSide[LED_NUM];
            for (int i = 0; i < LED_NUM; i++) tvSide[i] = 0;
            for (int i = 0; i < LED_TV_NUM; i++) {
                int ledIdx = TV(i);
                tvSide[ledIdx] = (i < (LED_TV_NUM >> 1)) ? 0 : 1;
            }

            for (int ledN = 0; ledN < totalLeds; ledN++) {
                int side = tvSide[ledN];
                setPixel(ledN, randR[side], randG[side], randB[side], LED::State.CurrentBrightness[ledN], false);
            }
            for (int i = 0; i < LED_HB_NUM; i++) {
                int hIdx = HB(i);
                int side = (i < halfHb) ? 0 : 1;
                setPixel(hIdx, randR[side], randG[side], randB[side], LED::State.CurrentBrightness[hIdx], false);
            }

            Show();
            S.paramB++;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, 50);
        } else {
            S.phase = 2; S.paramB = 0;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, 1);
        }
        return;
    }

    if (S.phase == 2) {
        setDualColorMapping(
            LED::State.TargetColor[0].r, LED::State.TargetColor[0].g, LED::State.TargetColor[0].b,
            LED::State.TargetColor[1].r, LED::State.TargetColor[1].g, LED::State.TargetColor[1].b);
        S.phase = 3;
        SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_TV_ON_BR_CL_DEL));
        return;
    }

    if (S.phase == 3) {
        bool moving = false;
        const int brInc = EE::Get(EE_TV_ON_BR_CL_INC);
        const int hbTgtBr = getLuxBrightness(LED::State.StoredBrightness[LED_START_I_HB]);

        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            int tarR, tarG, tarB, targetBr;
            if (i >= LED_START_I_HB && i < LED_START_I_HB + LED_HB_NUM) {
                const int hLocal = i - LED_START_I_HB;
                const int side   = (hLocal < halfHb) ? 0 : 1;
                tarR = LED::State.TargetColor[side].r;
                tarG = LED::State.TargetColor[side].g;
                tarB = LED::State.TargetColor[side].b;
                targetBr = hbTgtBr;
            } else {
                tarR = LED::State.StoredColor[i].r;
                tarG = LED::State.StoredColor[i].g;
                tarB = LED::State.StoredColor[i].b;
                targetBr = getLuxBrightness(LED::State.StoredBrightness[i]);
            }
            bool cC = TG_COLOR(i, tarR, tarG, tarB, brInc);
            bool cB = TG_BRIGHTNESS(i, targetBr, brInc, false);
            if (cC || cB) {
                setPixel(i, LED::State.CurrentColor[i].r, LED::State.CurrentColor[i].g, LED::State.CurrentColor[i].b, LED::State.CurrentBrightness[i], false);
                moving = true;
            }
        }

        if (moving) { Show(); return; }

        SCHED::KillAllUnlocked();
        if (!TV::State.Status) {
            SCHED::AddTask("T_SHAKE_DUAL_COLOR", "T_LEDS_TO_OFF", T_LEDS_TO_OFF, SCHED::Unit::MS,
                         EE::Get(EE_OTHER_BR_CL_DEL) * 3, S_TO_MS(EE::Get(EE_OTHER_TO_OFF_TIME)), false);
        }
        EE::WriteTime();
        APP::updDeltaColors();
        memcpy(LED::State.TargetColor, LED::State.CurrentColor, LED_NUM_TOTAL * sizeof(CRGB));
        HB::StartEffect(true, false, false);
    }
}

void T_AMBIENT_MODE_ON(SCHED::TaskId taskId) {
    SCHED::Scratch &S = SCHED::ScratchFor(taskId);
    const int phase = S.phase;
    const int inc = EE::Get(EE_OTHER_BR_CL_INC);
    bool active = false;

    if (phase == 1 || phase == 3) {
        for (int i = 0; i < LED_NUM_TOTAL; i++) {
            if (LED::TG_BRIGHTNESS_ToCurrentColor(i, 0, inc, false)) active = true;
        }
        if (active) { LED::Show(); return; }

        if (phase == 1) {
            S.phase = 2;
            SCHED::SetInterval(taskId, SCHED::Unit::MS, EE::Get(EE_OTHER_BR_CL_DEL));
        } else {
            APP::Am.Status = false;
            MOTION::State.Status = motON;
            TV::State.Status = false;
            SCHED::KillAllUnlocked();
            LED::setAll(0, 0, 0, 0);
            LED::Show();
            APP::updStatus("LED::T_AMBIENT_MODE_ON");
            APP::updDeltaColors();
            DIF::AutoOff();
        }
        return;
    }

    if (phase == 2) {
        for (int i = 0; i < LED_NUM; i++) {
            const bool isHB = LED::IsHB(i);
            const int subLoop = isHB ? LED_HB_NUM : 1;
            const int targetBr = LED::getLuxBrightness(LED::State.AmbientBackgroundBrightness[i]);
            for (int j = 0; j < subLoop; j++) {
                const int idx = isHB ? LED::HB(j) : i;
                if (LED::TG_BRIGHTNESS(idx, targetBr, inc, false)) {
                    LED::setPixel(idx, LED::State.AmbientBackgroundColor[i].r, LED::State.AmbientBackgroundColor[i].g, LED::State.AmbientBackgroundColor[i].b, LED::State.CurrentBrightness[idx], false);
                    active = true;
                }
            }
        }

        if (active) { LED::Show(); return; }

        SCHED::KillAllUnlocked();
        S.phase = 3;
        uint32_t delayMs = (uint32_t)EE::Get(EE_OTHER_AMBIENT_MODE_TIME) * 60000;
        SCHED::AddTask("T_AMBIENT_MODE_ON", "T_AMBIENT_MODE_ON", T_AMBIENT_MODE_ON, SCHED::Unit::MS, EE::Get(EE_OTHER_BR_CL_DEL), delayMs, false);
        APP::updStatus("LED::T_AMBIENT_MODE_ON");
        APP::updDeltaColors();
    }
}

void T_LEDS_TO_OFF(SCHED::TaskId taskId) {
    (void)taskId;
    if (FadeAllToZero(EE::Get(EE_TV_ON_BR_CL_INC))) {
        Show();
    } else {
        MOTION::State.Status = motON;
        APP::updStatus("LED::T_LEDS_TO_OFF");
        APP::updColors_Force();
        ForceOffAndKillTasks("T_LEDS_TO_OFF");
    }
}

} // namespace LED
