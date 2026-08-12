package com.fuzz.colors;

/*
 * ============================================================
 *  StatusManager.java  (alias: STS)
 * ============================================================
 *  Responsibility:
 *      • Stores the current system status received from Arduino.
 *      • Parses the 'sAAbbCCddEEffGG' status packet (via applyStatus).
 *      • Drives all status-indicator ImageViews (TV, Motion,
 *        Ambilight, AmbientMode) with colour filters.
 *      • Plays and stops the Ambilight rainbow animation on
 *        the LED buttons.
 *      • Plays and stops the AmbientMode breathing animation.
 *      • Animates temperature, humidity, and lux TextView updates.
 *      • Provides simple boolean accessors for other classes
 *        to guard their logic (isAmbilightOn, isTvOn, isEnabled).
 *
 *  How to use:
 *      1.  Instantiate in Main.onCreate():
 *              STS = new StatusManager(this, LED, SET);
 *
 *      2.  Bind views (call after setContentView):
 *              STS.init();
 *
 *      3.  Called by DATAr when status packets arrive:
 *              STS.applyStatus(message)   – core status packet
 *              STS.applyClimate(t, h)     – temperature + humidity
 *              STS.applyEnable(on)        – LED enable
 *              STS.applyLux(lux)          – lux level
 *
 *      4.  Boolean guards used by LED and SET:
 *              STS.isAmbilightOn()
 *              STS.isTvOn()
 *              STS.isEnabled()
 *              STS.getEnableDisable()
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      DATAs  = DataSend
 *      DATAr  = DataReceive
 *      LED   = LEDManager
 *      SET   = SettingsManager
 *      STS   = StatusManager    (this class)
 * ============================================================
 */

import android.animation.ValueAnimator;
import android.animation.ArgbEvaluator;
import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.graphics.Color;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.style.ForegroundColorSpan;
import android.text.style.RelativeSizeSpan;
import android.text.style.StyleSpan;
import android.graphics.Typeface;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;

import android.view.animation.LinearInterpolator;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;

import androidx.core.content.ContextCompat;

public class StatusManager {

    // --------------------------------------------------------
    // Status enums  (values must match Arduino hex encoding)
    // --------------------------------------------------------

    /** TV power state */
    public enum STS_Tv            { OFF, ON }

    /** Motion sensor state */
    public enum STS_Motion        { AUTOOFF, OFF, READY, COM, BED }

    /** Ambilight (PC RGB capture) state */
    public enum STS_Ambilight     { OFF, ON }

    /** Ambient mode (automatic lighting) state */
    public enum STS_AmbientMode   { OFF, ON, READY }

    /** LED strip hardware enable/disable state */
    public enum STS_EnableDisable { OFF, ON }

    /** Aromatherapy diffuser state */
    public enum STS_Diffuser     { OFF, ON, NO_WATER, NO_RESPONSE, PARFUM }

    // --------------------------------------------------------
    // Current state  (updated by applyStatus)
    // --------------------------------------------------------
    /** Current TV power state */
    public STS_Tv            currentTv           = STS_Tv.OFF;

    /** Current motion sensor state */
    public STS_Motion        currentMotion       = STS_Motion.OFF;

    /** Current Ambilight state */
    public STS_Ambilight     currentAmbilight    = STS_Ambilight.OFF;

    /** Current AmbientMode state */
    public STS_AmbientMode   currentAmbientMode  = STS_AmbientMode.OFF;

    /**
     * True once at least one status packet has been received.
     * Before that, the AmbientMode badge shows "N/A" rather than a
     * default OFF - OFF is a real state, "never connected" is not.
     */
    private boolean hasAmbientStatus = false;

    /** Current enable/disable state */
    public STS_EnableDisable currentEnableDisable = STS_EnableDisable.OFF;

    /** Current diffuser state */
    public STS_Diffuser      currentDiffuser      = STS_Diffuser.OFF;

    /**
     * Parfum-mode remaining minutes, mirrored from the Arduino's 'pTTTT'
     * packet (0 = parfum inactive). Read by DiffuserUsagePopup's inline
     * PARFUM MODE accordion so it opens pre-filled/expanded showing the
     * live countdown when parfum is already running.
     */
    public int parfumRemainingMin = 0;

    /** Last received temperature in °C (0-255 hex) */
    public int currentTemp = 0;

    /** Last received humidity in % (0-255 hex) */
    public int currentHum  = 0;

    /** Last received lux value */
    public int currentLux        = 0;

    /**
     * Last computed diffuser refill-remaining percent (0-100), or -1 when
     * there's no refill history yet to estimate from ('u' packet with
     * refillCount == 0 or avgMin == 0 - see applyDiffuserUsage()).
     */
    public int diffuserRefillPercent = -1;

    /**
     * Raw fields behind diffuserRefillPercent, from the last 'u' packet -
     * kept around so a long-press on the badge (see STS_Box_DiffuserRefill,
     * DiffuserUsagePopup) can show a proper remaining-time estimate on
     * demand, not just the rounded percent already on screen.
     */
    public int diffuserUsageAccumMin     = 0;
    public int diffuserUsageAvgMin       = 0;
    public int diffuserUsageRefillCount  = 0;
    public int diffuserUsageTotalRefills = 0;

    /**
     * Full refill-cycle history in minutes, oldest first - only populated on
     * demand (see DataSend.sendDiffuserHistory()/applyDiffuserHistory()), not
     * part of the periodic status push. Entries beyond diffuserHistoryCount
     * are stale/unused.
     */
    public int[] diffuserHistoryMin   = new int[10];
    /** Valid entries in diffuserHistoryMin, 0-10 - see applyDiffuserHistory(). */
    public int   diffuserHistoryCount = 0;

    // --------------------------------------------------------
    // Views
    // --------------------------------------------------------
    /** Temperature display label */
    public TextView STS_Text_Temp;

    /** Humidity display label */
    public TextView STS_Text_Hum;

    /** Lux + average display label */
    public TextView STS_Text_Lux;

    /** TV status indicator icon */
    public ImageView STS_IMG_Tv;

    /** Motion sensor status indicator icon */
    public ImageView STS_IMG_Motion;

    /** Ambilight status indicator icon */
    public ImageView STS_IMG_Ambilight;

    /** Diffuser status indicator icon (OFF=yellow, ON=green, NO_WATER=light blue, NO_RESPONSE=red) */
    public ImageView STS_IMG_Diffuser;

    /** Top-left diffuser refill badge icon - tint mirrors the refill-remaining level */
    public ImageView STS_IMG_DiffuserRefillIcon;
    /** Top-left diffuser refill badge percent label - "N/A" until refill history exists */
    public TextView STS_Text_DiffuserRefillPercent;
    /** Top-left diffuser refill badge bottom fill bar - width set to the remaining fraction */
    public View STS_View_DiffuserRefillFill;
    /** Top-left diffuser refill badge - the whole tappable cell (icon+percent+bar), long-press opens DiffuserUsagePopup */
    public View STS_Box_DiffuserRefill;

    /**
     * AmbientMode indicator icon.
     * Click  = enable ambient mode.
     * Long-click = disable ambient mode.
     */
    public ImageView STS_IMG_AmbientMode;
    /** The whole AMB cell (bulb + status + label) — the tap target. */
    public View STS_Cell_AmbientMode;

    /**
     * AmbientMode value label in the badge grid.
     * Reads "N/A" until the first status packet arrives, then
     * ON / READY / OFF - see _updateAmbientStatusText().
     */
    public TextView STS_Text_AmbientStatus;

    // --------------------------------------------------------
    // Animators
    // --------------------------------------------------------
    /** Animates the AmbientMode icon breathing pulse */
    private ValueAnimator ambientModeAnimator;

    /** Animates the Ambilight rainbow on LED buttons */
    private ValueAnimator ambilightAnimator;

    /** Short glow pulse on the diffuser icon accompanying the NO_WATER buzz */
    private ValueAnimator diffuserGlowAnimator;

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /**
     * Main – used for:
     *   Main._Toast()        short user messages
     *   Main._Console()      debug console
     *   Main.runOnUiThread() (not needed here – called on UI thread)
     *   Main.getResources()  colour lookups
     */
    private final MainActivity Main;

    /**
     * LED – used to:
     *   LED.deselectAll()     when ambilight turns on/off
     *   LED.lockSaveDual()    when AmbientMode or Ambilight is ON
     *   LED.unlockSaveDual()  when both are OFF
     *   LED.LED_BTN_Leds[]    for the Ambilight rainbow animation
     *   LED.LED_Color[][]     to restore colours after ambilight off
     *   LED.LED_AMBILIGHT     how many LEDs to animate
     * Alias: LED (LEDManager)
     * Note: injected late via setDependencies() from Main.onCreate()
     */
    private LEDManager LED;

    /**
     * SET – used to:
     *   SET.updateEnableDisableButton()  sync the Enable/Disable button label
     * Alias: SET (SettingsManager)
     * Note: injected late via setDependencies() from Main.onCreate()
     */
    private SettingsManager SET;

    /**
     * DATAs – used to:
     *   DATAs.sendAmbientMode(true/false)  when user taps the icon
     * Alias: DATAs (DataSend)
     */
    private final DataSend DATAs;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (alias: Main).
     * @param led   LEDManager instance  (alias: LED).
     * @param set   SettingsManager instance (alias: SET).
     * @param udps  DataSend instance      (alias: DATAs).
     */
    public StatusManager(MainActivity main, LEDManager led,
                         SettingsManager set, DataSend udps) {
        this.Main = main;
        this.LED  = led;
        this.SET  = set;
        this.DATAs = udps;
    }

    /**
     * Late-inject LED and SET after they have been constructed.
     * Called from Main.onCreate() after all classes are instantiated,
     * because STS is constructed before LED/SET to break the
     * circular dependency chain.
     *
     * @param led  LEDManager instance (alias: LED)
     * @param set  SettingsManager instance (alias: SET)
     */
    public void setDependencies(LEDManager led, SettingsManager set) {
        this.LED = led;
        this.SET = set;
    }

    // ========================================================
    //  Initialisation  (call from Main.onCreate)
    // ========================================================

    /**
     * Bind all STS-related views and attach listeners.
     * Must be called after setContentView() in Main.
     */
    public void init() {
        // Bind views
        STS_Text_Temp      = Main.findViewById(R.id._temperature);
        STS_Text_Hum       = Main.findViewById(R.id._humidity);
        STS_Text_Lux       = Main.findViewById(R.id._lux);
        STS_IMG_Tv         = Main.findViewById(R.id.sts_tv);
        STS_IMG_Motion     = Main.findViewById(R.id.sts_motion);
        STS_IMG_Ambilight  = Main.findViewById(R.id.sts_ambilight);
        STS_IMG_AmbientMode = Main.findViewById(R.id._ambientmode);
        STS_Cell_AmbientMode = Main.findViewById(R.id._ll_ambientmode);
        STS_Text_AmbientStatus = Main.findViewById(R.id._ambientmode_status);
        STS_IMG_Diffuser   = Main.findViewById(R.id.sts_diffuser);
        STS_IMG_DiffuserRefillIcon     = Main.findViewById(R.id._diffuser_refill_icon);
        STS_Text_DiffuserRefillPercent = Main.findViewById(R.id._diffuser_refill_percent);
        STS_View_DiffuserRefillFill    = Main.findViewById(R.id._diffuser_refill_fill);
        STS_Box_DiffuserRefill         = Main.findViewById(R.id._diffuser_refill_box);

        applyThemeColors();

        // Click: enable AmbientMode (only valid from READY status)
        STS_Cell_AmbientMode.setOnClickListener(v -> {
            if (currentAmbientMode != STS_AmbientMode.READY) return;
            DATAs.sendAmbientMode(true);
            LED._deselectAll();
            Main._Toast("{ AMBIENT MODE ON }");
            Main._Console(false, "►►", "AMBIENT MODE {#G}ON{##}");
        });

        // Long-click: disable AmbientMode (only if currently ON)
        STS_Cell_AmbientMode.setOnLongClickListener(v -> {
            if (currentAmbientMode == STS_AmbientMode.ON) {
                DATAs.sendAmbientMode(false);
                Main._Toast("{ AMBIENT MODE OFF }");
                Main._Console(false, "►►", "AMBIENT MODE {#R}OFF{##}");
            } else {
                Main._Toast("{ AMBIENT MODE NOT ON }");
            }
            return true;
        });

        // OFF has no valid action (enable needs READY) - disable the view
        STS_Cell_AmbientMode.setEnabled(currentAmbientMode != STS_AmbientMode.OFF);

        // Long-press on the diffuser refill badge: show a remaining-time estimate,
        // with an inline PARFUM MODE accordion of its own now (see
        // DiffuserUsagePopup's class doc) - the diffuser icon itself no
        // longer opens Parfum directly on long-press, that entry point
        // moved into this popup to keep one obvious place for every
        // diffuser action instead of two different long-press targets.
        // Live parfum minutes requested up front (board replies 'p' only
        // while a window is active) so the accordion opens pre-filled if
        // one's already running.
        STS_Box_DiffuserRefill.setOnLongClickListener(v -> {
            if (DATAs != null) DATAs.sendDiffuserStatus();
            new DiffuserUsagePopup(Main).show(v, diffuserRefillPercent,
                    diffuserUsageAccumMin, diffuserUsageAvgMin,
                    diffuserUsageRefillCount, diffuserUsageTotalRefills,
                    currentDiffuser == STS_Diffuser.PARFUM, parfumRemainingMin,
                    currentDiffuser == STS_Diffuser.NO_WATER);
            return true;
        });
    }

    /**
     * Re-tints the top status row's accent text (temp/hum/lux values)
     * to the active theme. Called once from init(), and again from
     * SettingsManager.applyTheme() after a live theme switch.
     */
    public void applyThemeColors() {
        int textColor = ThemeManager.getColor(Main, R.color.text_default);
        if (STS_Text_Temp != null) STS_Text_Temp.setTextColor(textColor);
        if (STS_Text_Hum  != null) STS_Text_Hum.setTextColor(textColor);
        if (STS_Text_Lux  != null) STS_Text_Lux.setTextColor(textColor);

        // Panel border (XML background _temp_hum) - was never re-tinted,
        // so it stayed AMBER-coloured under every other theme.
        ThemeManager.tintFrameStroke(Main,
                Main.findViewById(R.id._temp_hum_ambient), R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(Main,
                Main.findViewById(R.id._ard_wifi_box), R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(Main,
                Main.findViewById(R.id._diffuser_refill_box), R.color.stroke_line_color);

        // AmbientMode: ON self-corrects next animation frame; READY is static, so refresh it now.
        if (STS_IMG_AmbientMode != null && currentAmbientMode == STS_AmbientMode.READY) {
            STS_IMG_AmbientMode.setColorFilter(textColor);
        }
    }

    // ========================================================
    //  Status packet parser  (called by DATAr)
    // ========================================================

    /**
     * Parse a full status packet and update all STS state + UI.
     * Format: sAAbbCCddHH  (11 chars, all hex pairs)
     *   s  – literal prefix
     *   AA – TV state          (STS_Tv enum ordinal)
     *   bb – Motion state      (STS_Motion enum ordinal)
     *   CC – Ambilight state   (STS_Ambilight enum ordinal)
     *   dd – AmbientMode state (STS_AmbientMode enum ordinal)
     *   HH – Diffuser state    (STS_Diffuser enum ordinal, 4 = PARFUM)
     *
     * Temperature/humidity and Enable/Disable used to ride along in this same
     * packet (older 17-char format) but now arrive in their own packets -
     * climate in 'H', enable in 'E' - so this parser only ever reads the 5
     * fields above; see DataReceive._recvClimate() / the 'E' handler for those.
     *
     * Called by DATAr on the UI thread.
     *
     * PROTOCOL ADAPTATION: was applyStatus(String message), parsing 5 hex
     * pairs out of an ASCII "sAAbbCCddHH" packet. TELEM_STATUS's binary
     * fields (tv, motion, udpraw, ambient, diffuser_summary) are the exact
     * same 5 values in the exact same order, so this now takes them
     * directly - see DataReceive's TELEM_STATUS case.
     *
     * @param tv        STS_Tv ordinal.
     * @param motion    STS_Motion ordinal.
     * @param udpraw    STS_Ambilight ordinal.
     * @param ambient   STS_AmbientMode ordinal.
     * @param diffuser  STS_Diffuser ordinal (4 = PARFUM).
     */
    public void applyStatus(int tv, int motion, int udpraw, int ambient, int diffuser) {
        // Decode each field into locals first, so we can detect a true no-op
        // update before touching any state or UI (avoids spamming icon
        // recolors / lock toggles / animations on every identical packet —
        // e.g. the Arduino-side periodic diffuser status ping).
        // Climate (TELEM_CLIMATE) and enable (TELEM_ENABLE) arrive in their
        // own frames now.
        STS_Tv            newTv          = STS_Tv.values()[tv];
        STS_Motion        newMotion      = STS_Motion.values()[motion];
        STS_Ambilight     newAmbilight   = STS_Ambilight.values()[udpraw];
        STS_AmbientMode   newAmbientMode = STS_AmbientMode.values()[ambient];
        STS_Diffuser      newDiffuser    = STS_Diffuser.values()[diffuser];

        // First packet ever: reveal the real AmbientMode state even if it
        // happens to equal the OFF default (which would otherwise be
        // swallowed by the no-op bail-out right below).
        if (!hasAmbientStatus) {
            hasAmbientStatus = true;
            _updateAmbientStatusText(newAmbientMode);
        }

        // ── Bail out if every value is identical to last time ────
        if (newTv == currentTv
                && newMotion == currentMotion
                && newAmbilight == currentAmbilight
                && newAmbientMode == currentAmbientMode
                && newDiffuser == currentDiffuser) {
            return;
        }

        // Verbose diffuser-specific log - only fires when the diffuser field
        // itself actually changed (not just piggybacking on some other field
        // in this packet), so it's a clean signal for "did the SmartTV<->
        // diffuser link's reachability just change" without noise from
        // TV/motion/ambilight flipping on their own.
        if (newDiffuser != currentDiffuser) {
            Main._Console(false, "◄◄", "DIFFUSER STATUS [{#C}" + currentDiffuser
                    + "{##}] -> [{#C}" + newDiffuser + "{##}]");
        }

        // Commit the decoded values (currentTemp/currentHum are committed
        // further below, right where their animated-update check already
        // lived, so that sub-logic is untouched)
        currentTv          = newTv;
        currentMotion      = newMotion;
        currentAmbilight   = newAmbilight;
        currentAmbientMode = newAmbientMode;
        currentDiffuser    = newDiffuser;

        // Let LED (re)start/stop the local HB effect replay on TV on/off edges -
        // the wire protocol never says "an HB effect is animating", only TV state.
        if (LED != null) LED.notifyTvOnChanged(currentTv == STS_Tv.ON);

        // Ambilight/UDPRAW owns HB (mirrors COM/BED/LAMP) and the TV LED rainbow
        // while capturing - firmware disables the HB effect entirely during this.
        if (LED != null) LED.notifyAmbilightChanged(currentAmbilight == STS_Ambilight.ON);

        // Let LED show a locally-generated random HB pattern while motion is
        // triggered (COM/BED) with RANDOM COLOR on - the wire protocol can't
        // carry the firmware's real per-pixel random draws back (see HBFx).
        if (LED != null) LED.notifyMotionChanged(currentMotion == STS_Motion.COM
                || currentMotion == STS_Motion.BED);

        // ── TV icon ──────────────────────────────────────────
        // Green = ON, Red = OFF
        STS_IMG_Tv.setColorFilter(currentTv == STS_Tv.ON
                ? ContextCompat.getColor(Main, R.color.status_on_green)
                : ContextCompat.getColor(Main, R.color.status_off_red));

        // ── Motion icon ──────────────────────────────────────
        // AUTOOFF = grey, READY = yellow, COM/BED = green, OFF = red
        if      (currentMotion == STS_Motion.AUTOOFF)
            STS_IMG_Motion.setColorFilter(ContextCompat.getColor(Main, R.color.status_autooff_grey));
        else if (currentMotion == STS_Motion.READY)
            STS_IMG_Motion.setColorFilter(ContextCompat.getColor(Main, R.color.status_ready_yellow));
        else if (currentMotion == STS_Motion.COM
                || currentMotion == STS_Motion.BED)
            STS_IMG_Motion.setColorFilter(ContextCompat.getColor(Main, R.color.status_on_green));
        else
            STS_IMG_Motion.setColorFilter(ContextCompat.getColor(Main, R.color.status_off_red));

        // ── Ambilight icon & LED rainbow ─────────────────────
        if (currentAmbilight == STS_Ambilight.ON) {
            // TV icon turns cyan while Ambilight is active
            STS_IMG_Tv.setColorFilter(ContextCompat.getColor(Main, R.color.status_special_cyan));
            STS_IMG_Ambilight.setColorFilter(ContextCompat.getColor(Main, R.color.status_on_green));
            _startAmbilightEffect();
        } else {
            STS_IMG_Ambilight.setColorFilter(ContextCompat.getColor(Main, R.color.status_off_red));
            _stopAmbilightEffect();
        }

        // ── AmbientMode icon & breathing animation ────────────
        // The badge cell itself is always visible now (it's a peer of
        // Temp/Hum/Lux in the grid); only the icon tint and status text
        // change between states, so an OFF reading never leaves a blank
        // cell in the grid.
        if (currentAmbientMode == STS_AmbientMode.ON) {
            _startAmbientModeEffect();
        } else if (currentAmbientMode == STS_AmbientMode.READY) {
            _stopAmbientModeEffect();
            STS_IMG_AmbientMode.setColorFilter(
                    ThemeManager.getColor(Main, R.color.text_default));
        } else {
            // OFF - stop breathing, dim the icon rather than hide it
            _stopAmbientModeEffect();
            STS_IMG_AmbientMode.setColorFilter(
                    ContextCompat.getColor(Main, R.color.text_muted_grey));
        }
        // Enable = READY (can turn ON) or ON (can turn OFF); OFF has no valid action
        STS_Cell_AmbientMode.setEnabled(currentAmbientMode != STS_AmbientMode.OFF);
        _updateAmbientStatusText(currentAmbientMode);

        // ── Diffuser icon ────────────────────────────────────
        // OFF = yellow, ON = green, NO_WATER = light blue,
        // NO_RESPONSE = red, PARFUM = violet
        switch (currentDiffuser) {
            case ON:
                STS_IMG_Diffuser.setColorFilter(ContextCompat.getColor(Main, R.color.status_on_green));
                break;
            case PARFUM:
                STS_IMG_Diffuser.setColorFilter(ContextCompat.getColor(Main, R.color.status_parfum_violet));
                break;
            case NO_WATER:
                STS_IMG_Diffuser.setColorFilter(ContextCompat.getColor(Main, R.color.status_no_water_blue));
                _vibrateNoWater();
                _glowDiffuserIcon();
                break;
            case NO_RESPONSE:
                STS_IMG_Diffuser.setColorFilter(ContextCompat.getColor(Main, R.color.status_off_red));
                break;
            case OFF:
            default:
                STS_IMG_Diffuser.setColorFilter(ContextCompat.getColor(Main, R.color.status_ready_yellow));
                break;
        }

        // ── SaveDual button lock / unlock ────────────────────
        // Locked when AmbientMode or Ambilight is active
        if (currentAmbientMode == STS_AmbientMode.ON
                || currentAmbilight == STS_Ambilight.ON) {
            LED.lockSaveDual();
        } else {
            LED.unlockSaveDual();
        }
    }

    // ========================================================
    //  Climate + enable  (split out of the core status packet)
    // ========================================================

    /**
     * 'HTTHH' – temperature + humidity. Each value animates independently and
     * only when it actually changed.
     *
     * @param rawTemp  Temperature in °C.
     * @param rawHum   Relative humidity in %.
     */
    public void applyClimate(int rawTemp, int rawHum) {
        if (rawTemp != currentTemp) {
            _animateTempHum(STS_Text_Temp, currentTemp, rawTemp, " °c",
                    ContextCompat.getColor(Main, R.color.icon_tint_temperature));
            currentTemp = rawTemp;
        }
        if (rawHum != currentHum) {
            _animateTempHum(STS_Text_Hum, currentHum, rawHum, " %",
                    ContextCompat.getColor(Main, R.color.icon_tint_humidity));
            currentHum = rawHum;
        }
    }

    /**
     * 'Ee' – global LED enable / disable. No-op when unchanged.
     *
     * @param on  true = LEDs enabled.
     */
    public void applyEnable(boolean on) {
        STS_EnableDisable next = on ? STS_EnableDisable.ON : STS_EnableDisable.OFF;
        if (next == currentEnableDisable) return;
        currentEnableDisable = next;
        SET.updateEnableDisableButton(on);
    }

    // ========================================================
    //  Lux update  (called by DATAr)
    // ========================================================

    /**
     * Animate the lux-level display. Called by DATAr when 'Mllll' is received.
     * The rolling average ('mA') was dropped in v8.1.
     *
     * @param lux  New classified lux level (1..5).
     */
    public void applyLux(int lux) {
        _animateLux(currentLux, lux);
        currentLux = lux;
    }

    // ========================================================
    //  Ambilight rainbow animation
    // ========================================================

    /** How many LEDs are lit in the comet's fading tail (out of LED_AMBILIGHT). */
    private static final int COMET_LENGTH = 8;

    /** Time for the comet head to complete one full lap around the ring. */
    private static final long COMET_LAP_MS = 2500;

    /**
     * Start the rainbow "comet" animation on the first LED_AMBILIGHT
     * buttons: a bright, hue-cycling head sweeps around the ring with a
     * fading tail behind it, the rest of the ring left dark. If already
     * running, returns immediately.
     */
    private void _startAmbilightEffect() {
        if (ambilightAnimator != null && ambilightAnimator.isRunning()) return;

        ambilightAnimator = ValueAnimator.ofFloat(0, 1);
        ambilightAnimator.setDuration(COMET_LAP_MS);
        ambilightAnimator.setRepeatCount(ValueAnimator.INFINITE);
        ambilightAnimator.setInterpolator(new LinearInterpolator());

        int n = LED.LED_AMBILIGHT;
        float tailFrac = COMET_LENGTH / (float) n;

        ambilightAnimator.addUpdateListener(anim -> {
            float headPos = (float) anim.getAnimatedValue(); // 0..1 fraction around the ring
            float hue = headPos * 360f; // colour cycles once per lap

            for (int i = 0; i < n; i++) {
                float ledPos = i / (float) n;
                float diff = headPos - ledPos;
                diff -= Math.floor(diff); // wrap into [0,1), distance behind the head

                int color;
                if (diff < tailFrac) {
                    float brightness = 1f - (diff / tailFrac); // 1 at head, fading to 0 at tail end
                    color = Color.HSVToColor(new float[]{hue, 1f, brightness});
                } else {
                    color = Color.BLACK; // dark background behind the tail
                }

                // GradientDrawable ignores setColor() once setColors() has ever run on
                // it (which updateColor()/_setColorNow() do for every LED button, e.g.
                // the random startup paint) - the old gradient stop array keeps
                // rendering. Use setColors() here too, matching that convention,
                // otherwise the ring visibly never leaves its startup colour.
                GradientDrawable dr = (GradientDrawable)
                        LED.LED_BTN_Leds[i].getBackground().mutate();
                dr.setGradientType(GradientDrawable.LINEAR_GRADIENT);
                dr.setOrientation(GradientDrawable.Orientation.LEFT_RIGHT);
                dr.setColors(new int[]{color, color});
            }
        });

        ambilightAnimator.start();
        LED._deselectAll(); // Cannot select while Ambilight is active
    }

    /**
     * Stop the Ambilight rainbow animation and restore each button
     * to the last known LED colour.
     */
    private void _stopAmbilightEffect() {
        if (ambilightAnimator == null || !ambilightAnimator.isRunning()) return;

        ambilightAnimator.cancel();

        // Restore stored colours
        for (int i = 0; i < LED.LED_AMBILIGHT; i++) {
            GradientDrawable dr = (GradientDrawable)
                    LED.LED_BTN_Leds[i].getBackground().mutate();
            int color = Color.argb(255,
                    LED.LED_Color[i][LED._R],
                    LED.LED_Color[i][LED._G],
                    LED.LED_Color[i][LED._B]);
            dr.setGradientType(GradientDrawable.LINEAR_GRADIENT);
            dr.setOrientation(GradientDrawable.Orientation.LEFT_RIGHT);
            dr.setColors(new int[]{color, color});
        }
        LED._deselectAll();
    }

    // ========================================================
    //  AmbientMode breathing animation
    // ========================================================

    /**
     * Start the slow alpha-pulse ("breathing") animation on the
     * AmbientMode icon.  If already running, returns immediately.
     */
    private void _startAmbientModeEffect() {
        if (ambientModeAnimator != null && ambientModeAnimator.isRunning()) return;

        ambientModeAnimator = ValueAnimator.ofFloat(0, 1);
        ambientModeAnimator.setDuration(3000); // 3 s per breath
        ambientModeAnimator.setRepeatCount(ValueAnimator.INFINITE);
        ambientModeAnimator.setInterpolator(new LinearInterpolator());

        ambientModeAnimator.addUpdateListener(anim -> {
            // Resolved fresh every frame so a live theme switch is reflected immediately.
            int defaultColor = ThemeManager.getColor(Main, R.color.text_default);
            float val   = (float) anim.getAnimatedValue();
            // Sinusoidal alpha between 128 and 255
            int   alpha = (int) (255 * (0.5f + 0.5f * Math.sin(val * Math.PI)));
            int   color = (defaultColor & 0x00FFFFFF) | (alpha << 24);
            STS_IMG_AmbientMode.setColorFilter(color);
        });

        ambientModeAnimator.start();
    }

    /**
     * Stop the AmbientMode breathing animation and clean up.
     */
    private void _stopAmbientModeEffect() {
        if (ambientModeAnimator == null) return;

        ambientModeAnimator.removeAllListeners();
        ambientModeAnimator.removeAllUpdateListeners();
        ambientModeAnimator.cancel();
        ambientModeAnimator.end();
        ambientModeAnimator = null;
    }

    /**
     * Store the parfum-mode remaining minutes relayed by the Arduino
     * ('pTTTT' packet, 0 = inactive). Called by DATAr on the UI thread.
     * The value itself is only read when the user long-presses the
     * diffuser icon; the icon tint follows the status packet (PARFUM).
     *
     * @param minutes  Remaining minutes, 0-360.
     */
    public void applyParfumRemaining(int minutes) {
        if (minutes != parfumRemainingMin) {
            Main._Console(false, "◄◄", "DIFFUSER PARFUM [{#C}" + parfumRemainingMin
                    + "min{##}] -> [{#C}" + minutes + "min{##}]");
        }
        parfumRemainingMin = minutes;
    }

    /**
     * Store the diffuser usage/refill stats relayed by the Arduino ('u' packet)
     * and update the top-left refill badge: icon tint, percent text, and the
     * bottom fill bar's width (fraction of the 34dp track defined in
     * activity_main.xml). Called by DATAr on the UI thread.
     *
     * With no refill history yet (refillCount == 0) or no valid average
     * (avgMin == 0) there's no real percentage to show - rather than an
     * alarming empty bar or a cold "N/A", this assumes the best (full bar,
     * healthy tint) and labels it "LEARN": the diffuser hasn't completed a
     * full refill cycle yet, so it's still building the history this
     * estimate depends on.
     *
     * @param accumMin     Effective (mode-weighted) minutes run since the last refill.
     * @param avgMin       Rolling average of completed refill cycles, minutes.
     * @param refillCount  Valid entries in the rolling history, 0-10.
     * @param totalRefills Lifetime refill count (not surfaced in this badge yet).
     */
    public void applyDiffuserUsage(int accumMin, int avgMin, int refillCount, int totalRefills) {
        // No console spam here - this rides every periodic Ds/Dc poll while
        // reachable, so it would fire continuously during normal operation.
        // The same numbers are already shown on demand via the "DIFFUSER
        // usage" section of SmartTV's own debug dump (DBG -> DIFFUSER).

        // Keep the raw fields around too, not just the rounded percent -
        // DiffuserUsagePopup (long-press on the badge) reads these directly.
        diffuserUsageAccumMin     = accumMin;
        diffuserUsageAvgMin       = avgMin;
        diffuserUsageRefillCount  = refillCount;
        diffuserUsageTotalRefills = totalRefills;

        int trackWidthPx = Math.round(34 * Main.getResources().getDisplayMetrics().density);
        android.view.ViewGroup.LayoutParams lp = STS_View_DiffuserRefillFill.getLayoutParams();

        // Out of water overrides the math entirely, and must be checked before
        // anything below. The firmware banks the dry cycle and resets its usage
        // accumulator the moment OOW is actually detected (see the "wasFast"
        // confirmed-dry branch -> USAGE::finalizeRefillCycle() in the diffuser
        // .ino), so the very next 'u' packet after a real OOW event reports
        // accumMin ~= 0 - which the percent formula below reads as "just
        // refilled, 100% left" if left unchecked. That's precisely backwards:
        // the diffuser needs water RIGHT NOW. currentDiffuser is already the
        // ground-truth OOW signal (drives the diffuser icon + vibrate alert
        // elsewhere in this class, from the 's' status push) - trust it over
        // this badge's own accumMin/avgMin math whenever they disagree.
        if (currentDiffuser == STS_Diffuser.NO_WATER) {
            diffuserRefillPercent = 0;
            int red = ContextCompat.getColor(Main, R.color.status_off_red);
            STS_Text_DiffuserRefillPercent.setText(R.string.status_no_water);
            STS_Text_DiffuserRefillPercent.setTextColor(red);
            STS_IMG_DiffuserRefillIcon.setColorFilter(red);
            STS_View_DiffuserRefillFill.setBackgroundColor(red);
            lp.width = 0;
            STS_View_DiffuserRefillFill.setLayoutParams(lp);
            return;
        }

        if (refillCount == 0 || avgMin <= 0) {
            diffuserRefillPercent = -1;
            int healthy = ContextCompat.getColor(Main, R.color.status_on_green);
            int muted   = ContextCompat.getColor(Main, R.color.text_muted_grey);
            STS_Text_DiffuserRefillPercent.setText(R.string.status_learning);
            STS_Text_DiffuserRefillPercent.setTextColor(muted);
            STS_IMG_DiffuserRefillIcon.setColorFilter(healthy);
            STS_View_DiffuserRefillFill.setBackgroundColor(healthy);
            lp.width = trackWidthPx;
            STS_View_DiffuserRefillFill.setLayoutParams(lp);
            return;
        }

        int percent = Math.max(0, Math.min(100, 100 - (accumMin * 100 / avgMin)));
        diffuserRefillPercent = percent;

        int colorRes = (percent > 50) ? R.color.status_on_green
                     : (percent > 20) ? R.color.status_ready_yellow
                     : R.color.status_off_red;
        int color = ContextCompat.getColor(Main, colorRes);

        STS_Text_DiffuserRefillPercent.setText(percent + "%");
        STS_Text_DiffuserRefillPercent.setTextColor(color);
        STS_IMG_DiffuserRefillIcon.setColorFilter(color);
        STS_View_DiffuserRefillFill.setBackgroundColor(color);
        lp.width = Math.round(trackWidthPx * (percent / 100f));
        STS_View_DiffuserRefillFill.setLayoutParams(lp);
    }

    /**
     * Store the diffuser's full refill-cycle history ('Dh' packet, requested
     * on demand via DATAs.sendDiffuserHistory() - not part of the periodic
     * status push). Oldest cycle first; entries beyond count are unused.
     * No dedicated history view exists yet - values are just stored here
     * (and logged to the console) for now.
     *
     * @param count   Valid entries, 0-10.
     * @param minutes 10-slot array, minutes per completed cycle, oldest first.
     */
    public void applyDiffuserHistory(int count, int[] minutes) {
        diffuserHistoryCount = count;
        System.arraycopy(minutes, 0, diffuserHistoryMin, 0, Math.min(minutes.length, diffuserHistoryMin.length));

        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < count; i++) {
            if (i > 0) sb.append(", ");
            sb.append(diffuserHistoryMin[i]).append("min");
        }
        Main._Console(false, "◄◄", "DIFFUSER HISTORY [{#C}" + count + "/10{##}] " + sb);
    }

    /**
     * Flash the diffuser icon brighter for a moment, as a visual
     * companion to {@link #_vibrateNoWater()}'s haptic ping.
     * Pulses base -> white -> base a couple of times, then settles
     * back on the resting NO_WATER tint.
     */
    private void _glowDiffuserIcon() {
        if (diffuserGlowAnimator != null) diffuserGlowAnimator.cancel();

        final int baseColor = ContextCompat.getColor(Main, R.color.status_no_water_blue);
        final ArgbEvaluator evaluator = new ArgbEvaluator();

        diffuserGlowAnimator = ValueAnimator.ofFloat(0, 1);
        diffuserGlowAnimator.setDuration(200);        // one pulse leg
        diffuserGlowAnimator.setRepeatCount(3);        // base->white->base->white->base
        diffuserGlowAnimator.setRepeatMode(ValueAnimator.REVERSE);

        diffuserGlowAnimator.addUpdateListener(anim -> {
            float frac  = (float) anim.getAnimatedValue();
            int   color = (int) evaluator.evaluate(frac, baseColor, Color.WHITE);
            STS_IMG_Diffuser.setColorFilter(color);
        });
        diffuserGlowAnimator.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                STS_IMG_Diffuser.setColorFilter(baseColor);
                diffuserGlowAnimator = null;
            }
        });

        diffuserGlowAnimator.start();
    }

    /**
     * Fire 3 short pulses to alert the user the diffuser is out of
     * water. Called once per transition into NO_WATER (the identical-
     * packet bail-out above prevents repeat buzzing on every ping).
     */
    @SuppressWarnings("deprecation") // vibrate(long[],int) is the only API available below minSdk 21's ceiling of API 26 (O), where VibrationEffect was introduced
    private void _vibrateNoWater() {
        Vibrator vib = ContextCompat.getSystemService(Main, Vibrator.class);
        if (vib == null || !vib.hasVibrator()) return;

        long[] pattern = {0, 80, 120, 80, 120, 80}; // wait, buzz, gap, buzz, gap, buzz

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            vib.vibrate(VibrationEffect.createWaveform(pattern, -1));
        } else {
            vib.vibrate(pattern, -1);
        }
    }

    /**
     * Write the AmbientMode badge's value label.
     * "N/A" is only ever shown before {@link #hasAmbientStatus} flips
     * true (i.e. before the first status packet); after that the badge
     * always reflects the real enum state, including OFF.
     *
     * @param mode  AmbientMode state to render.
     */
    private void _updateAmbientStatusText(STS_AmbientMode mode) {
        if (STS_Text_AmbientStatus == null) return;

        if (!hasAmbientStatus) {
            STS_Text_AmbientStatus.setText("N/A");
            return;
        }

        switch (mode) {
            case ON:    STS_Text_AmbientStatus.setText("ON");    break;
            case READY: STS_Text_AmbientStatus.setText("RDY");   break;
            default:    STS_Text_AmbientStatus.setText("OFF");   break;
        }
    }

    // ========================================================
    //  Temperature / Humidity animated update
    // ========================================================

    /**
     * Animate a temperature or humidity TextView from an old value
     * to a new value over 2 seconds, with a smaller coloured suffix.
     *
     * @param textView     Target TextView to update
     * @param startValue   Previous numeric value (animate from here)
     * @param endValue     New numeric value (animate to here)
     * @param suffix       Unit string e.g. " °c" or " %"
     * @param suffixColor  ARGB colour int for the suffix
     */
    private void _animateTempHum(TextView textView,
                                  int startValue, int endValue,
                                  String suffix, int suffixColor) {
        ValueAnimator anim = ValueAnimator.ofInt(startValue, endValue);
        anim.setDuration(2000);

        anim.addUpdateListener(a -> {
            int   val      = (int) a.getAnimatedValue();
            String full    = val + suffix;
            int   valLen   = String.valueOf(val).length();

            SpannableString sp = new SpannableString(full);
            // Suffix: smaller (85%) and coloured
            sp.setSpan(new RelativeSizeSpan(0.85f),
                    valLen, full.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
            sp.setSpan(new ForegroundColorSpan(suffixColor),
                    valLen, full.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);

            textView.setText(sp);
        });

        anim.start();
    }

    // ========================================================
    //  Lux animated update
    // ========================================================

    /**
     * Animate the lux TextView, showing both the rolling average
     * (smaller text) and the current lux (bold text), both counting
     * up to their new values over 2 seconds.
     *
     * @param luxStart      Previous lux value
     * @param luxEnd        New lux value
     * @param averageStart  Previous rolling average
     * @param averageEnd    New rolling average
     */
    private void _animateLux(int luxStart, int luxEnd) {
        ValueAnimator luxAnim = ValueAnimator.ofInt(luxStart, luxEnd);
        luxAnim.setDuration(2000);
        luxAnim.addUpdateListener(a -> {
            int lux = (int) a.getAnimatedValue();

            String ratingText = getLuxRating(lux);
            SpannableString sp = new SpannableString(ratingText);
            sp.setSpan(new StyleSpan(Typeface.BOLD),
                    0, ratingText.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);

            STS_Text_Lux.setText(sp);
        });
        luxAnim.start();
    }

    // ========================================================    //  Intensity bar visualization helper
    // ========================================================

    /**
     * Convert a lux level (1-4) to a visual brightness rating.
     *
     * 1 -> ▱▱▱
     * 2 -> ▰▱▱
     * 3 -> ▰▰▱
     * 4 -> ▰▰▰
     */
    private String getLuxRating(int lux) {
        int level = Math.max(1, Math.min(4, lux)) - 1;

        StringBuilder sb = new StringBuilder();

        for (int i = 0; i < level; i++) {
            sb.append("▰");
        }

        for (int i = level; i < 3; i++) {
            sb.append("▱");
        }

        return sb.toString();
    }

    // ========================================================    //  Boolean accessors  (used by LED, SET, DATAs, Main)
    // ========================================================

    /**
     * @return true if Ambilight is currently ON.
     *         LED uses this to block selection during rainbow mode.
     */
    public boolean isAmbilightOn() {
        return currentAmbilight == STS_Ambilight.ON;
    }

    /**
     * @return true if TV is currently ON.
     *         LED uses this to decide HB dual-colour rendering.
     */
    public boolean isTvOn() {
        return currentTv == STS_Tv.ON;
    }

    /**
     * @return true if the LED strip is currently enabled (ON).
     *         SET uses this to label the Enable/Disable button.
     */
    public boolean isEnabled() {
        return currentEnableDisable == STS_EnableDisable.ON;
    }

    /**
     * @return current STS_EnableDisable enum value.
     *         DATAs.sendEnableDisable() uses this for logging.
     */
    public STS_EnableDisable getEnableDisable() {
        return currentEnableDisable;
    }
}
