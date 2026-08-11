package com.fuzz.colors;

/*
 * ============================================================
 *  HBEffectSimulator.java  (alias: HBFx, owned by LEDManager)
 * ============================================================
 *  Why this exists:
 *      The SmartTV firmware drives 178 physical "heartbeat" (HB) LEDs,
 *      but the wire protocol collapses all 178 into a single logical
 *      slot (LED_HB) and never transmits brightness - only RGB hue at
 *      physical pixel #0. That means chase/sweep/pulse/sparkle HB
 *      effects the firmware is animating are structurally invisible to
 *      the app in realtime. Instead of trying to receive them, this
 *      class locally re-runs the same effect algorithms the firmware
 *      runs (mirrors HB::T_EFFECT_HB_1..14 and TV::T_EFFECT_H_FadeOn/
 *      CenterBloom/LinearSweep/QuadPoint in
 *      _FuZzAPP_SmartTV_R4.ino), seeded only from the
 *      configured effect id/speed settings (32/33/34) and the current
 *      HB colour(s), and paints the result onto HeartbeatPatternView's
 *      18 segments.
 *
 *  Simplifications vs. firmware (no equivalent data available client-side):
 *      - No lux sensor: firmware's getLuxBrightness/getLuxAdaptInc/Delay
 *        adaptive scaling is skipped; BASE_BR is used as a fixed stand-in
 *        for HB::HB_GetBaseBr().
 *      - No per-pixel "TargetColor" buffer: every effect's base hue is
 *        derived on the fly from LED_Color[LED_HB] (or the dual-colour
 *        context LEDs when HB dual-colour is active), matching what the
 *        firmware's TargetColor effectively holds for HB pixels.
 *      - Only 18 of the 178 virtual pixels are ever displayed (the same
 *        proportional sample points HeartbeatPatternView already uses),
 *        so effects that light a single random pixel (sparkle, aurora,
 *        glitch) are visible only when that random pixel happens to land
 *        on a sampled index - an accurate reflection of "look at only
 *        these 18 spots on a 178-LED strip", not an approximation.
 *
 *  Redraw throttling: internal state is stepped every tick at the
 *  effect's real cadence, but the 18-segment view is only repainted
 *  every 5 ticks or 300ms elapsed (whichever first), so LED-by-LED
 *  effects don't repaint the view on every single simulated pixel step.
 *
 *  Motion + RANDOM COLOR: a special case, not one of the mirrored firmware
 *  effects above. T_EFFECT_MOTION_ON's phase-0 prep (_FuZzAPP_SmartTV_R4.ino)
 *  assigns an *independent* random colour to every one of the 178 physical
 *  HB pixels - genuinely random per-pixel, not one colour repeated - and the
 *  single-slot wire protocol only ever reports one of those 178 draws back.
 *  There is no real colour data to mirror here, so onMotionChanged() doesn't
 *  try: it just paints each of the 18 preview segments its own fresh local
 *  random colour once, when motion is triggered (COM/BED) with the RANDOM
 *  COLOR setting on, and leaves that pattern static (no ongoing re-randomize
 *  loop) until motion ends - purely so the app visibly reflects "HB is
 *  random right now" instead of showing a single static (and essentially
 *  arbitrary) swatch.
 * ============================================================
 */

import android.graphics.Color;

import android.os.Handler;
import android.os.Looper;

import java.util.Random;

public class HBEffectSimulator {

    private static final int NUM  = 178;      // LED_HB_NUM
    private static final int HALF = NUM >> 1; // LED_HB_NUM >> 1

    /** Fixed stand-in for the firmware's lux-adapted HB::HB_GetBaseBr() - full intensity so effects read clearly. */
    private static final int BASE_BR = 255;

    private static final int REDRAW_EVERY_N_TICKS = 5;
    private static final long REDRAW_EVERY_MS = 300;

    /** effectKind sentinel for the motion+random local pattern (see class-level note). */
    private static final int EFFECT_KIND_MOTION_RANDOM = -2;

    /**
     * Client-side pacing floor for effects whose per-pixel step (EFFECT SPEED,
     * setting 33) reads too fast to follow on an 18-segment preview: WhiteMove,
     * TravelingShadow, ExpandingRaindrops, Colors, ShootingStarRandom,
     * StochasticPlasma, DigitalRain, DualPulse. Their one-shot pause/settle
     * delays (already several seconds) are left untouched.
     */
    private static final int SLOW_MULTIPLIER = 4;
    private static final int SLOW_MIN_MS = 150;

    private static final int[] WHITE = {255, 255, 255};
    private static final int[][] SPARK_COLORS = {
            {255,   0,   0}, {  0, 255,   0}, {  0,   0, 255}, {255, 255,   0},
            {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
    };

    private final LEDManager LED;
    private final StatusManager STS;
    private final SettingsManager SET;
    private final HeartbeatPatternView view;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Random rnd = new Random();

    /** Persistent virtual-strip brightness (0..255), reused across ticks. */
    private final int[] curBr = new int[NUM];

    // Generic per-effect scratch state, mirrors firmware TASK.Phase/ParamA/ParamB
    // and HB::State.Phase/ParamA - reset whenever a (sub-)effect (re)starts.
    private long taskPhase, taskParamA, taskParamB;
    private long hbPhase, hbParamA;

    /** Which colorAt() branch is active: idle effect id (1-14), or -1 during the TV-on sequence. */
    private int effectKind = -1;

    private boolean running = false;
    private boolean tvOnSequenceActive = false;
    private boolean lastTvOn = false;
    private boolean ambilightOn = false;
    private boolean motionActive = false;
    private int activeIdleEffect = -1;

    private long ticksSinceRedraw = 0;
    private long lastRedrawAt = 0;

    public HBEffectSimulator(LEDManager led, StatusManager sts, SettingsManager set, HeartbeatPatternView view) {
        this.LED = led;
        this.STS = sts;
        this.SET = set;
        this.view = view;
    }

    public boolean isActive() {
        return running;
    }

    // ==================================================================
    // Trigger hooks
    // ==================================================================

    /**
     * Called from StatusManager.applyStatus() with the freshly-decoded TV state.
     * Only the rising edge (TV turning on) is special-cased to replay the TV-on
     * HB sequence - the idle effect itself is NOT gated on TV power state (this
     * matches the firmware: HB::StartEffect() only checks ambilight/UDPRAW, never
     * TV on/off), so it keeps looping/responding to setting changes either way.
     */
    public void onTvOnChanged(boolean tvOn) {
        boolean turnedOn = tvOn && !lastTvOn;
        lastTvOn = tvOn;
        if (ambilightOn) return; // Ambilight/UDPRAW owns HB while capturing

        if (turnedOn) { startTvOnSequence(); return; }
        if (!tvOnSequenceActive) restartIdleIfNeeded();
    }

    /**
     * Called from StatusManager.applyStatus() with the freshly-decoded Ambilight
     * (UDPRAW) state. Firmware disables the HB effect entirely while UDPRAW is
     * capturing (HB::StartEffect() bails on UDPRAW::State.Status), so this stops
     * the simulator outright - LED mirrors HB to the COM/BED/LAMP average instead
     * (see LEDManager._updateHBFromAmbientZones()). Resumes normal behaviour once
     * Ambilight turns back off.
     */
    public void onAmbilightChanged(boolean on) {
        if (on == ambilightOn) return;
        ambilightOn = on;
        if (on) stop();
        else restartIdleIfNeeded();
    }

    /**
     * Called from DataReceive._recvSettings() when a settings packet confirms an
     * HB effect setting was applied. Reacts to id 32 (idle EFFECT) immediately,
     * regardless of TV state, so picking a new effect and getting the device's
     * confirmation echo updates the HB button right away. Speed (33) is read
     * live every tick already; 34 only matters on the next TV-on transition.
     */
    public void onHBSettingChanged(int settingId) {
        if (settingId != SET.SET_HB_Effect) return;
        if (ambilightOn || tvOnSequenceActive) return; // let Ambilight / the one-shot TV-on animation own HB
        restartIdleIfNeeded();
    }

    /**
     * Called from StatusManager.applyStatus() with whether motion is
     * currently triggered (STS_Motion.COM or .BED). See the class-level note
     * for why this shows a local random flicker instead of trying to mirror
     * firmware's real (untransmittable) per-pixel colours.
     */
    public void onMotionChanged(boolean active) {
        motionActive = active;
        if (ambilightOn) return; // Ambilight/UDPRAW owns HB while capturing - firmware doesn't run motion effects then either
        refreshMotionRandomState();
    }

    /**
     * Called from DataReceive._recvSettings() when a settings packet confirms
     * the MOTION RANDOM COLOR setting (id 10) was applied - so toggling it
     * while motion is already active starts/stops the flicker immediately.
     */
    public void onMotionRandomSettingChanged() {
        if (ambilightOn) return;
        refreshMotionRandomState();
    }

    // ==================================================================
    // Lifecycle
    // ==================================================================

    private void restartIdleIfNeeded() {
        if (ambilightOn) return;
        // Motion owns HB right now - onTvOnChanged() re-fires on every status
        // packet (not just a real TV edge, since StatusManager.applyStatus()
        // calls it whenever ANY field in the packet changed), so without this
        // guard an unrelated refresh while motion was still active would fall
        // through to here and reset the motion-random paint back to flat.
        // Only refreshMotionRandomState()'s own teardown may clear it.
        if (running && effectKind == EFFECT_KIND_MOTION_RANDOM) return;
        int idleEff = SET.getHBEffectValue();
        if (idleEff <= 0) {
            if (running) stop(); // effect set to static - stop and fall back to the normal static/dual paint
            return;
        }
        if (running && !tvOnSequenceActive && idleEff == activeIdleEffect) return;
        startIdleLoop();
    }

    /** Starts or stops the motion-random pattern to match current motion + setting state. */
    private void refreshMotionRandomState() {
        boolean wantRandom = motionActive && SET.getMotionRandomColorValue() != 0;
        boolean alreadyRandom = running && effectKind == EFFECT_KIND_MOTION_RANDOM;
        if (wantRandom) {
            if (!alreadyRandom) startMotionRandom();
            return;
        }
        if (alreadyRandom) {
            // Motion ended / setting turned off - explicitly release ownership
            // BEFORE calling restartIdleIfNeeded(), otherwise its own guard
            // above (correctly) refuses to touch an active motion-random view.
            stopInternal();
            effectKind = -1;
            activeIdleEffect = -1;
            restartIdleIfNeeded();
        }
    }

    /**
     * Starts the motion-random pattern, taking over from whatever else is
     * running (matches firmware: motion's phase-0 fades everything to black
     * first, overriding any in-flight effect). Paints each segment its own
     * random colour once and leaves it there - no ongoing animation - until
     * motion ends or the setting changes (see refreshMotionRandomState()).
     */
    private void startMotionRandom() {
        stopInternal();
        resetFrameState();
        effectKind = EFFECT_KIND_MOTION_RANDOM;
        activeIdleEffect = -1; // invalidate so restartIdleIfNeeded() doesn't think the old idle effect is still current
        tvOnSequenceActive = false;
        running = true;
        renderMotionRandomFrame();
    }

    /**
     * Paints each of the 18 preview segments its own fresh random colour at
     * full brightness, once - see the class-level note on why this doesn't
     * try to mirror firmware's actual (untransmittable) per-pixel random
     * draws, just visibly flags "HB is random right now". Keeps
     * LED.LED_Color[LED_HB] updated too (same as maybeRedraw() does for the
     * other effects) so the static/dual fallback has a recent colour instead
     * of a stale one once this stops.
     */
    private void renderMotionRandomFrame() {
        int[] indices = HeartbeatPatternView.PIXEL_INDICES;
        long sumR = 0, sumG = 0, sumB = 0;
        for (int seg = 0; seg < indices.length; seg++) {
            int r = rnd.nextInt(256), g = rnd.nextInt(256), b = rnd.nextInt(256);
            view.setSegmentColor(seg, Color.argb(255, r, g, b));
            sumR += r; sumG += g; sumB += b;
        }
        int n = indices.length;
        LED.LED_Color[LED.LED_HB][LED._R] = (int) (sumR / n);
        LED.LED_Color[LED.LED_HB][LED._G] = (int) (sumG / n);
        LED.LED_Color[LED.LED_HB][LED._B] = (int) (sumB / n);
    }

    private void startTvOnSequence() {
        stopInternal();
        resetFrameState();
        effectKind = -1;
        hbPhase = 1; hbParamA = 0;
        tvOnSequenceActive = true;
        running = true;
        final int hbEff = clamp(SET.getTvOnHBEffectValue(), 0, 3);
        tvOnStep(hbEff);
    }

    private void startIdleLoop() {
        stopInternal();
        int idleEff = SET.getHBEffectValue();
        activeIdleEffect = idleEff;
        if (idleEff <= 0 || idleEff > 14) {
            LED.onHBEffectSimulatorStopped();
            return;
        }
        resetFrameState();
        effectKind = idleEff;
        tvOnSequenceActive = false;
        running = true;
        idleStep(idleEff);
    }

    public void stop() {
        stopInternal();
        activeIdleEffect = -1;
        LED.onHBEffectSimulatorStopped();
    }

    private void stopInternal() {
        handler.removeCallbacksAndMessages(null);
        running = false;
        tvOnSequenceActive = false;
    }

    private void resetFrameState() {
        java.util.Arrays.fill(curBr, 0);
        taskPhase = 0; taskParamA = 0; taskParamB = 0;
        hbPhase = 0; hbParamA = 0;
        ticksSinceRedraw = 0; lastRedrawAt = 0;
    }

    // ==================================================================
    // Tick dispatchers
    // ==================================================================

    private void idleStep(final int effId) {
        if (!running) return;
        int delay;
        switch (effId) {
            case 1:  delay = stepWhiteMove();          break;
            case 2:  delay = stepHeartbeat();           break;
            case 3:  delay = stepRandomFade();           break;
            case 4:  delay = stepTravelingShadow();       break;
            case 5:  delay = stepExpandingRaindrops();     break;
            case 6:  delay = stepColors();                  break;
            case 7:  delay = stepShootingStar();             break;
            case 8:  delay = stepSparklingPop();              break;
            case 9:  delay = stepGlidingAurora();              break;
            case 10: delay = stepGlitchMatrix();                break;
            case 11: delay = stepPlasma();                       break;
            case 12: delay = stepDigitalRain();                   break;
            case 13: delay = stepDualPulse();                      break;
            case 14: delay = stepRainbowWave();                     break;
            default: return;
        }
        maybeRedraw();
        handler.postDelayed(() -> idleStep(effId), Math.max(1, delay));
    }

    private void tvOnStep(final int hbEff) {
        if (!running) return;
        boolean done;
        switch (hbEff) {
            case 1:  done = stepCenterBloom();  break;
            case 2:  done = stepLinearSweep();  break;
            case 3:  done = stepQuadPoint();    break;
            default: done = stepFadeOn();       break;
        }
        maybeRedraw();
        if (done) {
            tvOnSequenceActive = false;
            startIdleLoop(); // firmware hands off to HB::StartEffect() once the TV-on sequence completes
            return;
        }
        int delay = SET.getTvOnBrClDelayValue();
        handler.postDelayed(() -> tvOnStep(hbEff), Math.max(1, delay));
    }

    // ==================================================================
    // Idle HB effects (HB::T_EFFECT_HB_1..14)
    // ==================================================================

    private int stepWhiteMove() {
        int speed = slowSpeed();
        if (taskPhase <= 1) {
            taskPhase = 1;
            long d1 = taskParamA;
            for (int i = 0; i < NUM; i++) curBr[i] = BASE_BR;
            if (d1 >= NUM) {
                taskPhase = 2;
                return rndBetween(5000, 10000);
            }
            taskParamA = d1 + 1;
            return speed;
        }
        taskParamA = 0;
        taskPhase = 1;
        return speed;
    }

    private int stepHeartbeat() {
        if (taskPhase == 0 && taskParamA == 0) hbPhase = rndBetween(2, 5);
        int angle = (int) taskParamA;
        int curBrVal = BASE_BR;
        if (taskPhase < hbPhase) {
            double rad = Math.toRadians(angle);
            curBrVal = BASE_BR / 2 + (int) ((BASE_BR / 2) * Math.sin(rad));
        }
        curBrVal = clamp(curBrVal, 0, 255);
        for (int i = 0; i < NUM; i++) curBr[i] = curBrVal;

        if (taskPhase < hbPhase) {
            angle += 20;
            int delay;
            if (angle >= 180) { angle = 0; taskPhase++; delay = rndBetween(60, 150); }
            else delay = 15;
            taskParamA = angle;
            return delay;
        }
        taskPhase = 0; taskParamA = 0;
        return rndBetween(1500, 4500);
    }

    private int stepRandomFade() {
        int inc = SET.getTvOnBrClIncValue();
        if (taskParamA == 0) {
            taskParamA = 1; taskParamB = 0;
            int wide = rndBetween(3, 26);
            int pos  = rndBetween(0, NUM - wide);
            taskPhase = ((long) pos << 8) | wide;
        }
        int pos  = (int) (taskPhase >> 8);
        int wide = (int) (taskPhase & 0xFF);
        double angle = Math.toRadians((int) taskParamB);
        int curBrVal = (int) (BASE_BR * Math.sin(angle));
        for (int i = 0; i < NUM; i++) {
            boolean isPart = (i >= pos && i < pos + wide);
            stepBr(i, isPart ? curBrVal : 0, inc);
        }
        taskParamB += 4;
        if (taskParamB >= 180) {
            taskParamB = 0;
            int nWide = rndBetween(3, 26);
            int nPos  = rndBetween(0, NUM - nWide);
            taskPhase = ((long) nPos << 8) | nWide;
            return rndBetween(100, 400);
        }
        return SET.getHBEffectSpeedValue();
    }

    private int stepTravelingShadow() {
        int inc = SET.getTvOnBrClIncValue();
        int minBr = (BASE_BR * 40) / 100;
        if (taskParamA == 0) {
            taskParamA = 1;
            taskPhase = rndBetween(5, 50);
            taskParamB = 0;
        }
        int shadowWidth = (int) taskPhase;
        int shadowPos = (int) taskParamB - shadowWidth;
        for (int i = 0; i < NUM; i++) {
            boolean isShadow = (i >= shadowPos && i < shadowPos + shadowWidth);
            stepBr(i, isShadow ? minBr : BASE_BR, inc);
        }
        taskParamB++;
        if (taskParamB > NUM + shadowWidth) {
            taskParamA = 0;
            return rndBetween(2000, 5000);
        }
        return slowSpeed();
    }

    private int stepExpandingRaindrops() {
        int inc = SET.getTvOnBrClIncValue();
        if (taskParamA == 0) {
            taskPhase = rndBetween(0, NUM);
            taskParamB = 0;
            taskParamA = 1;
        }
        int dropCenter = (int) taskPhase;
        for (int i = 0; i < NUM; i++) {
            int target = 0;
            int dist = Math.abs(i - dropCenter);
            if (dist <= taskParamB) {
                int dropFade = BASE_BR - (dist * (BASE_BR / 10));
                target = Math.max(dropFade, 0);
            }
            stepBr(i, target, inc);
        }
        taskParamB++;
        if (taskParamB > 10) {
            taskParamA = 0;
            return rndBetween(200, 800);
        }
        return slowSpeed();
    }

    private int stepColors() {
        if (taskParamA == 0) { taskParamA = 1; taskParamB = 0; taskPhase = 0; }
        for (int i = 0; i < NUM; i++) curBr[i] = (i <= taskParamB) ? BASE_BR : 0;
        if (taskParamB < NUM) taskParamB++;
        taskPhase += 12;
        int totalScale = 8 * 256;
        if (taskPhase >= totalScale) taskPhase = 0;
        return slowSpeed();
    }

    private int stepShootingStar() {
        int maxLen = NUM >> 1;
        if (taskParamA == 0) {
            taskParamA = 1; taskParamB = 0;
            hbPhase = rndBetween(5, maxLen + 1);
        }
        int tailLen = (int) hbPhase;
        for (int i = 0; i < NUM; i++) {
            int target = 0;
            if (i <= taskParamB && i > taskParamB - tailLen) {
                int dist = (int) taskParamB - i;
                int totalDrop = (BASE_BR * 60) / 100;
                int intensityDrop = (dist * totalDrop) / Math.max(1, tailLen - 1);
                target = Math.max(BASE_BR - intensityDrop, 0);
            }
            curBr[i] = target;
        }
        taskParamB++;
        if (taskParamB > NUM + tailLen) {
            taskParamA = 0;
            return rndBetween(1000, 3000);
        }
        return slowSpeed();
    }

    private int stepSparklingPop() {
        int inc = SET.getTvOnBrClIncValue();
        if (taskParamA == 0) {
            taskPhase = rndBetween(0, NUM);
            taskParamB = rndBetween(0, 7);
            taskParamA = 1;
        }
        int flashIdx = (int) taskPhase;
        for (int i = 0; i < NUM; i++) {
            stepBr(i, (i == flashIdx) ? 255 : BASE_BR, inc);
        }
        if (curBr[flashIdx] >= 250) {
            taskParamA = 0;
            return rndBetween(50, 200);
        }
        return SET.getHBEffectSpeedValue();
    }

    private int stepGlidingAurora() {
        if (taskParamA == 0) {
            for (int i = 0; i < NUM; i++) curBr[i] = BASE_BR;
            taskParamA = 1;
        }
        for (int j = 0; j < 3; j++) {
            int i = rnd.nextInt(NUM);
            curBr[i] = clamp(BASE_BR + rndBetween(-30, 30), 0, 255);
        }
        curBr[rnd.nextInt(NUM)] = BASE_BR;
        return SET.getHBEffectSpeedValue();
    }

    private int stepGlitchMatrix() {
        if (taskParamA == 0) taskParamA = 1;
        int ambient = BASE_BR / 20;
        for (int i = 0; i < NUM; i++) curBr[i] = ambient;
        int blockStart = rnd.nextInt(NUM);
        int blockSize  = 1 + rnd.nextInt(4);
        for (int j = 0; j < blockSize; j++) {
            int idx = blockStart + j;
            if (idx < NUM) curBr[idx] = BASE_BR;
        }
        int baseSpeed = SET.getHBEffectSpeedValue();
        return clamp(baseSpeed + rndBetween(-10, 50), 10, 32767);
    }

    private int stepPlasma() {
        if (taskParamA == 0) {
            taskParamA = 1;
            taskPhase = rndBetween(0, 256);
        }
        for (int i = 0; i < NUM; i++) {
            int cur = curBr[i];
            int rS = rnd.nextInt(100);
            int nBr = (rS > 85) ? cur + 10 : (rS < 15) ? cur - 10 : cur;
            curBr[i] = clamp(nBr, BASE_BR / 5, BASE_BR);
        }
        taskPhase++;
        return slowSpeed();
    }

    private int stepDigitalRain() {
        for (int i = NUM - 1; i > 0; i--) curBr[i] = curBr[i - 1];
        curBr[0] = (rnd.nextInt(100) > 92) ? BASE_BR : 0;
        return slowSpeed();
    }

    private int stepDualPulse() {
        if (taskParamA == 0) { taskParamA = 1; taskPhase = 0; }
        double phase1 = taskPhase * 0.10;
        double phase2 = taskPhase * -0.08;
        for (int i = 0; i < NUM; i++) {
            int ri = NUM - 1 - i;
            double waveA = Math.sin(phase1 + ri * 0.5) * 0.5 + 0.5;
            double waveB = Math.sin(phase2 + ri * 0.3) * 0.5 + 0.5;
            int br = (int) ((waveA + waveB) * 0.5 * BASE_BR);
            curBr[i] = Math.min(br, BASE_BR);
        }
        taskPhase++;
        return slowSpeed();
    }

    private int stepRainbowWave() {
        if (taskParamA == 0) taskParamA = 1;
        double envAngle = taskPhase * (Math.PI / 90.0);
        double envelope = 0.35 + 0.65 * (Math.sin(envAngle) * 0.5 + 0.5);
        int envBr = Math.max(1, (int) (BASE_BR * envelope));
        for (int i = 0; i < NUM; i++) curBr[i] = envBr;
        taskPhase = (taskPhase + 1) % 180;
        taskParamB++;
        return SET.getHBEffectSpeedValue();
    }

    // ==================================================================
    // TV-on HB helpers (TV::T_EFFECT_H_FadeOn/CenterBloom/LinearSweep/QuadPoint)
    // ==================================================================

    private boolean stepFadeOn() {
        int inc = SET.getTvOnBrClIncValue();
        boolean busy = false;
        for (int i = 0; i < NUM; i++) if (stepBr(i, BASE_BR, inc)) busy = true;
        return !busy;
    }

    private boolean stepCenterBloom() {
        int inc = SET.getTvOnBrClIncValue();
        if (hbPhase == 1) {
            if (hbParamA < HALF) {
                int idxL = HALF - 1 - (int) hbParamA;
                int idxR = HALF + (int) hbParamA;
                int bloom = Math.min((BASE_BR * 15) / 10, 255);
                boolean active = false;
                if (idxL >= 0 && idxL < NUM && stepBr(idxL, bloom, inc)) active = true;
                if (idxR >= 0 && idxR < NUM && stepBr(idxR, bloom, inc)) active = true;
                if (!active) hbParamA++;
            } else {
                hbPhase = 2; hbParamA = 0;
            }
            return false;
        }
        boolean busy = false;
        for (int i = 0; i < NUM; i++) if (stepBr(i, BASE_BR, inc)) busy = true;
        return !busy;
    }

    private boolean stepLinearSweep() {
        int inc = SET.getTvOnBrClIncValue();
        if (hbPhase == 1) {
            if (hbParamA < NUM) hbParamA++;
            boolean active = false;
            for (int i = 0; i < hbParamA; i++) if (stepBr(i, BASE_BR, inc)) active = true;
            if (hbParamA >= NUM && !active) hbPhase = 2;
            return false;
        }
        boolean busy = false;
        for (int i = 0; i < NUM; i++) if (stepBr(i, BASE_BR, inc)) busy = true;
        return !busy;
    }

    private boolean stepQuadPoint() {
        int inc = SET.getTvOnBrClIncValue();
        int segment = NUM >> 2;
        int reach = segment >> 1;
        int p1 = segment * 0 + segment / 2, p2 = segment + segment / 2,
            p3 = segment * 2 + segment / 2, p4 = segment * 3 + segment / 2;
        if (hbPhase == 1) {
            if (hbParamA <= reach) {
                int[] pts = {
                        p1 - (int) hbParamA, p1 + (int) hbParamA,
                        p2 - (int) hbParamA, p2 + (int) hbParamA,
                        p3 - (int) hbParamA, p3 + (int) hbParamA,
                        p4 - (int) hbParamA, p4 + (int) hbParamA,
                };
                boolean active = false;
                for (int p : pts) if (p >= 0 && p < NUM && stepBr(p, BASE_BR, inc)) active = true;
                if (!active && hbParamA < reach) hbParamA++;
                else if (!active) { hbPhase = 2; hbParamA = 0; }
            }
            return false;
        }
        boolean busy = false;
        for (int i = 0; i < NUM; i++) if (stepBr(i, BASE_BR, inc)) busy = true;
        return !busy;
    }

    // ==================================================================
    // Colour + brightness helpers
    // ==================================================================

    private int[] baseColorAt(int i) {
        boolean dual = lastTvOn && SET.getHBDualValue() != 0;
        int ref = dual
                ? (i < HALF ? LED.LED_DUALCOLOR_CONTEXT[0] : LED.LED_DUALCOLOR_CONTEXT[1])
                : LED.LED_HB;
        return LED.LED_Color[ref];
    }

    private int[] colorAt(int i) {
        switch (effectKind) {
            case 1: { // WhiteMove: moving 3-pixel white dot over the base colour
                long d1 = taskParamA;
                if (i == d1 || i == d1 + 1 || i == d1 + 2) return WHITE;
                return baseColorAt(i);
            }
            case 6: return colorsWipePalette(i);
            case 8: // RandomSparklingPop: flash pixel uses a preset rainbow colour
                if (i == (int) taskPhase) return SPARK_COLORS[(int) taskParamB];
                return baseColorAt(i);
            case 11: return hsvToRgb((int) (taskPhase - i * 2));
            case 14: {
                int hueOffset = (int) (taskParamB & 0xFF);
                return hsvToRgb(hueOffset + (i * 255 / NUM));
            }
            default: return baseColorAt(i);
        }
    }

    /** Firmware's 8-stop flowing palette wipe (HB::T_EFFECT_HB_6_Colors), byte-wrap included. */
    private int[] colorsWipePalette(int i) {
        int colorCount = 8, totalScale = colorCount * 256;
        int pos = (i * 60 - (int) taskPhase) % totalScale;
        if (pos < 0) pos += totalScale;
        int idx1 = pos >> 8;
        int idx2 = (idx1 + 1) % colorCount;
        int fraction = pos & 0xFF;
        int r1 = (idx1 * 31 + 50) & 0xFF, g1 = (idx1 * 77 + 100) & 0xFF, b1 = (idx1 * 133) & 0xFF;
        int r2 = (idx2 * 31 + 50) & 0xFF, g2 = (idx2 * 77 + 100) & 0xFF, b2 = (idx2 * 133) & 0xFF;
        int r = ((r1 * (255 - fraction) + r2 * fraction) >> 8) & 0xFF;
        int g = ((g1 * (255 - fraction) + g2 * fraction) >> 8) & 0xFF;
        int b = ((b1 * (255 - fraction) + b2 * fraction) >> 8) & 0xFF;
        return new int[]{r, g, b};
    }

    /** Firmware's fast 3-phase HSV colour wheel (LED::HsvToRgb), hue 0-255. */
    private static int[] hsvToRgb(int h) {
        h = ((h % 256) + 256) % 256;
        int r, g, b;
        if (h < 85) {
            r = 255 - h * 3; g = h * 3; b = 0;
        } else if (h < 170) {
            h -= 85;
            r = 0; g = 255 - h * 3; b = h * 3;
        } else {
            h -= 170;
            r = h * 3; g = 0; b = 255 - h * 3;
        }
        return new int[]{clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255)};
    }

    /** Mirrors firmware TG_Step: moves curBr[idx] one increment toward target; returns true if it moved. */
    private boolean stepBr(int idx, int target, int inc) {
        int cur = curBr[idx];
        if (cur == target) return false;
        int diff = target - cur;
        cur += (Math.abs(diff) <= inc) ? diff : (diff > 0 ? inc : -inc);
        curBr[idx] = clamp(cur, 0, 255);
        return true;
    }

    private static int scale(int r, int g, int b, int br) {
        br = clamp(br, 0, 255);
        return Color.argb(255, (r * br) / 255, (g * br) / 255, (b * br) / 255);
    }

    private static int clamp(int v, int lo, int hi) {
        return Math.max(lo, Math.min(hi, v));
    }

    /** Arduino-style random(min,max): inclusive of min, exclusive of max (handles negative ranges too). */
    private int rndBetween(int minIncl, int maxExcl) {
        return minIncl + rnd.nextInt(Math.max(1, maxExcl - minIncl));
    }

    /** EFFECT SPEED (33) floored/multiplied for effects that read too fast on the 18-segment preview. */
    private int slowSpeed() {
        return Math.max(SET.getHBEffectSpeedValue() * SLOW_MULTIPLIER, SLOW_MIN_MS);
    }

    // ==================================================================
    // Rendering (throttled: every 5 ticks or 300ms elapsed, whichever first)
    // ==================================================================

    private void maybeRedraw() {
        ticksSinceRedraw++;
        long now = System.currentTimeMillis();
        if (ticksSinceRedraw < REDRAW_EVERY_N_TICKS && (now - lastRedrawAt) < REDRAW_EVERY_MS) return;
        ticksSinceRedraw = 0;
        lastRedrawAt = now;

        int[] indices = HeartbeatPatternView.PIXEL_INDICES;
        long sumR = 0, sumG = 0, sumB = 0;
        for (int seg = 0; seg < indices.length; seg++) {
            int idx = indices[seg];
            int[] rgb = colorAt(idx);
            int br = clamp(curBr[idx], 0, 255);
            view.setSegmentColor(seg, scale(rgb[0], rgb[1], rgb[2], br));
            sumR += (rgb[0] * br) / 255; sumG += (rgb[1] * br) / 255; sumB += (rgb[2] * br) / 255;
        }

        // Keep LED.LED_Color[LED_HB] in sync with what's actually on screen, so
        // that if the simulator stops (e.g. Ambilight taking over, effect set to
        // static), the fallback static/dual repaint uses a real recent colour
        // instead of a stale/black value - HB should never visibly "go off".
        int n = indices.length;
        LED.LED_Color[LED.LED_HB][LED._R] = (int) (sumR / n);
        LED.LED_Color[LED.LED_HB][LED._G] = (int) (sumG / n);
        LED.LED_Color[LED.LED_HB][LED._B] = (int) (sumB / n);
    }
}
