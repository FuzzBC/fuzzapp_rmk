#include "Strip.h"
#include "Globals.h"
#include "Log.h"
#include "../_rmk_Diffuser_DEF.h"

namespace STRIP {

// -- file-local helpers (not part of the public module surface) -----------
static inline StripColor scaleColor(const StripColor &base, uint8_t scale) {
    return { (uint8_t)(((uint16_t)base.r * scale) / 255),
             (uint8_t)(((uint16_t)base.g * scale) / 255),
             (uint8_t)(((uint16_t)base.b * scale) / 255) };
}

static inline StripColor applyBrightness(const StripColor &base) {
    return scaleColor(base, g_stripBrightness);
}

static inline uint8_t centreDistance(uint8_t i) {
    uint8_t mid = STRIP_LED_COUNT / 2;
    return (i < mid) ? (uint8_t)(mid - 1 - i) : (uint8_t)(i - mid);
}

static void fireArmStep(uint8_t *heat, uint8_t armLen) {
    const uint8_t COOLING  = 55;
    const uint8_t SPARKING = 120;
    for (uint8_t i = 0; i < armLen; i++) {
        uint8_t cool = (uint8_t)random(0, ((uint16_t)COOLING * 10) / armLen + 2);
        heat[i] = (uint8_t)max((int)heat[i] - (int)cool, 0);
    }
    for (uint8_t i = armLen - 1; i >= 2; i--) {
        heat[i] = (uint8_t)(((uint16_t)heat[i - 1] + heat[i - 2] + heat[i - 2]) / 3);
    }
    if (random(255) < SPARKING) {
        uint8_t y = (uint8_t)random(2);
        heat[y] = (uint8_t)min((int)heat[y] + (int)random(160, 255), 255);
    }
}

static inline StripColor heatColor(uint8_t heat) {
    uint8_t t192     = (uint8_t)(((uint16_t)heat * 191) / 255);
    uint8_t heatramp = (uint8_t)((t192 & 0x3F) << 2);
    if (t192 < 64)       return { heatramp, 0, 0 };
    else if (t192 < 128) return { 255, heatramp, 0 };
    else                 return { 255, 255, heatramp };
}

static inline void stripDecayAll(uint8_t factor) {
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[i] = scaleColor(g_stripTgt[i], factor);
}

static void stripSetTargetAll(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { r, g, b };
}

static void stripSetTargetDual(uint8_t r1, uint8_t g1, uint8_t b1,
                                uint8_t r2, uint8_t g2, uint8_t b2) {
    uint8_t half = STRIP_LED_COUNT / 2;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[i] = (i < half) ? StripColor{ r1, g1, b1 } : StripColor{ r2, g2, b2 };
}

static bool stripStepChan(uint8_t &cur, uint8_t tgt, uint8_t inc) {
    if (cur == tgt) return false;
    int diff = (int)tgt - (int)cur;
    cur += (abs(diff) <= (int)inc) ? diff : (diff > 0 ? (int)inc : -(int)inc);
    return true;
}

static void stripWheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (h < 85)       { r = 255 - h * 3; g = h * 3;        b = 0; }
    else if (h < 170) { h -= 85;  r = 0; g = 255 - h * 3;  b = h * 3; }
    else              { h -= 170; r = h * 3; g = 0;        b = 255 - h * 3; }
}

static inline uint8_t triWave(uint8_t phase) {
    return (phase < 128) ? (uint8_t)(phase * 2) : (uint8_t)((255 - phase) * 2);
}

static void stripOowChaseFrame() {
    static uint8_t pos      = 0;
    static uint8_t hopTicks = 0;
    if (++hopTicks < 4) return;
    hopTicks = 0;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
    g_stripTgt[pos] = g_effectBase1;
    pos = (pos + 1) % STRIP_LED_COUNT;
}

static void stripParfumBreatheFrame() {
    static uint8_t hopTicks = 0;
    if (++hopTicks < PARFUM_BREATHE_STEP_TICKS) return;
    hopTicks = 0;

    if (g_parfumBreatheDir > 0) {
        if (g_parfumBreatheLevel < STRIP_LED_COUNT) g_parfumBreatheLevel++;
        else                                         g_parfumBreatheDir = -1;
    } else {
        if (g_parfumBreatheLevel > 0) {
            g_parfumBreatheLevel--;
        } else {
            uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
            g_parfumBreatheColor = { r, g, b };
            g_parfumBreatheDir   = 1;
            g_parfumBreatheLevel = 1;
        }
    }

    uint8_t    levelScale = (uint8_t)(((uint16_t)g_parfumBreatheLevel * 255) / STRIP_LED_COUNT);
    StripColor lit        = applyBrightness(scaleColor(g_parfumBreatheColor, levelScale));
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_stripTgt[PARFUM_BREATHE_ORDER[i]] = (i < g_parfumBreatheLevel) ? lit : StripColor{ 0, 0, 0 };
}

static void stripEffectFrame() {
    g_animPhase++;
    uint8_t half = STRIP_LED_COUNT / 2;
    switch (g_animEffect) {
        case 0: {
            uint8_t triL = triWave(g_animPhase);
            uint8_t triR = g_effectDual ? triWave((uint8_t)(g_animPhase + 128)) : triL;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                bool right = g_effectDual && i >= half;
                g_stripTgt[i] = scaleColor(right ? g_effectBase2 : g_effectBase1, right ? triR : triL);
            }
            break;
        }
        case 1: {
            uint8_t maxDist = (STRIP_LED_COUNT + 1) / 2;
            uint8_t width   = (uint8_t)(((uint16_t)triWave(g_animPhase) * maxDist) / 255);
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                const StripColor &base = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;
                g_stripTgt[i] = (centreDistance(i) <= width) ? base : StripColor{ 0, 0, 0 };
            }
            break;
        }
        case 2: {
            static uint8_t hopTicks = 0;
            if (++hopTicks < 8) break;
            hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
            uint8_t led = (uint8_t)random(STRIP_LED_COUNT);
            g_stripTgt[led] = applyBrightness({ (uint8_t)random(256), (uint8_t)random(256), (uint8_t)random(256) });
            break;
        }
        case 3: {
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                uint8_t h = g_animPhase + (uint8_t)(i * (256 / STRIP_LED_COUNT));
                uint8_t r, g, b; stripWheel(h, r, g, b);
                g_stripTgt[i] = applyBrightness({ r, g, b });
            }
            break;
        }
        case 4: {
            static uint8_t hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
                const StripColor &base = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;
                StripColor restLevel = scaleColor(base, 40);
                StripColor decayed   = scaleColor(g_stripTgt[i], 220);
                g_stripTgt[i].r = max(restLevel.r, decayed.r);
                g_stripTgt[i].g = max(restLevel.g, decayed.g);
                g_stripTgt[i].b = max(restLevel.b, decayed.b);
            }
            if (++hopTicks < 3) break;
            hopTicks = 0;
            uint8_t i = (uint8_t)random(STRIP_LED_COUNT);
            g_stripTgt[i] = (g_effectDual && i >= half) ? g_effectBase2 : g_effectBase1;
            break;
        }
        case 5: {
            const uint8_t armLen = STRIP_LED_COUNT / 2;
            static uint8_t heatL[STRIP_LED_COUNT / 2] = {};
            static uint8_t heatR[STRIP_LED_COUNT / 2] = {};
            fireArmStep(heatL, armLen);
            fireArmStep(heatR, armLen);
            for (uint8_t pos = 0; pos < armLen; pos++) {
                g_stripTgt[pos]                       = applyBrightness(heatColor(heatL[pos]));
                g_stripTgt[STRIP_LED_COUNT - 1 - pos] = applyBrightness(heatColor(heatR[pos]));
            }
            break;
        }
        case 6: {
            static int8_t  pos      = 0;
            static int8_t  dir      = 1;
            static uint8_t hopTicks = 0;
            if (++hopTicks < 4) break;
            hopTicks = 0;
            for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) g_stripTgt[i] = { 0, 0, 0 };
            g_stripTgt[pos] = (g_effectDual && pos >= half) ? g_effectBase2 : g_effectBase1;
            if (pos + dir < 0 || pos + dir >= STRIP_LED_COUNT) dir = (int8_t)-dir;
            pos = (int8_t)(pos + dir);
            break;
        }
        case 7: {
            static uint8_t hopTicks = 0;
            stripDecayAll(235);
            if (++hopTicks < 3) break;
            hopTicks = 0;
            uint8_t led = (uint8_t)random(STRIP_LED_COUNT);
            uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
            g_stripTgt[led] = applyBrightness({ r, g, b });
            break;
        }
    }
}

// -- public module surface --------------------------------------------------

void applyDnPayload(const DnPayload &p) {
    g_stripBrightness = p.brightness;
    g_effectTickMs    = (p.speedMs >= STRIP_EFFECT_SPEED_MIN_MS) ? p.speedMs : STRIP_EFFECT_SPEED_MIN_MS;

    // r1/r2 swapped vs received order - corrects a physical L/R inversion.
    // base1 always ends up on LED0-4, base2 on LED5-9.
    StripColor c1 = applyBrightness({ p.r1, p.g1, p.b1 });
    StripColor c2 = applyBrightness({ p.r2, p.g2, p.b2 });
    g_effectBase1 = c2;
    g_effectBase2 = c1;
    g_effectDual  = p.dual;

    if (p.dual) stripApplyDual(c2.r, c2.g, c2.b, c1.r, c1.g, c1.b);
    else        stripApplyStatic(c1.r, c1.g, c1.b);
    if (p.effect != 0) stripApplyEffectSet(p.effect);
}

const char* effectName(uint8_t ee) {
    if (ee <= STRIP_EFFECT_COUNT) return EFFECT_NAMES[ee];
    return "?";
}

void startOowAlert() {
    g_oowAlertActive = true;
    g_effectBase1     = { 0, 90, 255 };
    g_effectDual      = false;
    LOG::logMsg("WATER", "out of water - blue Chase alert until mode-off command");
    g_stripMode = STRIP_OOW_CHASE;
}

void startParfumBreathe() {
    uint8_t r, g, b; stripWheel((uint8_t)random(256), r, g, b);
    g_parfumBreatheColor = { r, g, b };
    g_parfumBreatheLevel = 0;
    g_parfumBreatheDir   = 1;
    g_stripMode = STRIP_PARFUM_BREATHE;
}

void stripApplyDual(uint8_t r1, uint8_t g1, uint8_t b1,
                     uint8_t r2, uint8_t g2, uint8_t b2) {
    g_stripMode = STRIP_DUAL;
    stripSetTargetDual(r1, g1, b1, r2, g2, b2);
    vlogMsg("STRIP", "dual -> #%02X%02X%02X / #%02X%02X%02X", r1, g1, b1, r2, g2, b2);
}

void stripApplyEffectNext() {
    g_stripMode  = STRIP_ANIM;
    g_animEffect = (g_animEffect + 1) % STRIP_EFFECT_COUNT;
    vlogMsg("STRIP", "effect -> #%u (%s)", g_animEffect + 1, effectName(g_animEffect + 1));
}

void stripApplyEffectSet(uint8_t n) {
    if (n == 0) {
        g_stripMode = STRIP_STATIC;
        vlogMsg("STRIP", "effect -> #0 (STATIC)");
        return;
    }
    g_stripMode  = STRIP_ANIM;
    g_animEffect = n - 1;
    vlogMsg("STRIP", "effect -> #%u (%s)", n, effectName(n));
}

void stripApplyStatic(uint8_t r, uint8_t g, uint8_t b) {
    g_stripMode = STRIP_STATIC;
    stripSetTargetAll(r, g, b);
    vlogMsg("STRIP", "static -> #%02X%02X%02X", r, g, b);
}

void stripOff() {
    g_stripMode = STRIP_OFF;
    stripSetTargetAll(0, 0, 0);
}

uint8_t stripStatusCode() {
    switch (g_stripMode) {
        case STRIP_STATIC: return 1;
        case STRIP_DUAL:   return 2;
        case STRIP_ANIM:   return 3 + g_animEffect;
        default:           return 0;
    }
}

const char* stripStatusName(uint8_t code) {
    if (code == 0) return "OFF";
    if (code == 1) return "STATIC";
    if (code == 2) return "DUAL";
    return effectName(code - 2);
}

void tickStripEffect() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_ANIM) return;
    unsigned long now = millis();
    if (now - last < g_effectTickMs) return;
    last = now;
    stripEffectFrame();
}

void tickStripFade() {
    static unsigned long last = 0;
    unsigned long now = millis();
    if (now - last < STRIP_TICK_MS) return;
    last = now;

    bool changed = false;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
        changed |= stripStepChan(g_stripCur[i].r, g_stripTgt[i].r, STRIP_FADE_STEP);
        changed |= stripStepChan(g_stripCur[i].g, g_stripTgt[i].g, STRIP_FADE_STEP);
        changed |= stripStepChan(g_stripCur[i].b, g_stripTgt[i].b, STRIP_FADE_STEP);
    }
    if (!changed) return;

    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++)
        g_strip.setPixelColor(i, g_strip.Color(g_stripCur[i].r, g_stripCur[i].g, g_stripCur[i].b));
    g_strip.show();
}

void tickStripOowChase() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_OOW_CHASE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    stripOowChaseFrame();
}

void tickStripParfumBreathe() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_PARFUM_BREATHE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    stripParfumBreatheFrame();
}

void tickStripWifiCue() {
    static unsigned long last = 0;
    if (g_stripMode != STRIP_WIFI_CUE) return;
    unsigned long now = millis();
    if (now - last < STRIP_EFFECT_TICK_MS) return;
    last = now;
    g_animPhase++;
    for (uint8_t i = 0; i < STRIP_LED_COUNT; i++) {
        uint8_t h = g_animPhase + (uint8_t)(i * (256 / STRIP_LED_COUNT));
        uint8_t r, gc, b; stripWheel(h, r, gc, b);
        g_stripTgt[i] = { r, gc, b };
    }
}

} // namespace STRIP
