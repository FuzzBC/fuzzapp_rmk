package com.fuzz.colors;

/*
 * ============================================================
 *  SettingsManager.java  (alias: SET)
 * ============================================================
 *  Responsibility:
 *      • Defines and stores all 40 Arduino settings (SET_Info list),
 *        each tagged with a CATEGORY (which section it lives in) and a
 *        TYPE (which widget renders it).
 *      • Defines and stores all category sections (CAT_Info list), each
 *        a title + an empty row container. Builds every section
 *        PROGRAMMATICALLY from CAT_Info - inflating one
 *        item_settings_category per category into cat_sections_root -
 *        same as settings rows, no more one hand-written <LinearLayout>
 *        block per category in the XML.
 *      • Builds every settings row PROGRAMMATICALLY from SET_Info -
 *        inflating one of three small row templates per row
 *        (item_setting_bar / item_setting_switch / item_setting_selectable)
 *        into the matching category container. Selectable rows with >5
 *        options open a centred modal popup (dimmed overlay + card, one
 *        row per named option) on tap; see _showOptionsPopup(). No more
 *        one hand-written XML block per setting.
 *      • Handles the background-image picker SeekBar.
 *      • Handles the TestMode SeekBar.
 *      • Handles the Default / Enable-Disable buttons.
 *      • Provides applyReceivedSetting() for DATAr to push
 *        incoming Arduino values into the UI.
 *      • Exposes getHBDualValue() for LED colour logic.
 *
 *  How to use:
 *      1.  Instantiate in Main before calling init():
 *              SET = new SettingsManager(this, DATAs, STS);
 *
 *      2.  Must be initialised before LED (because LED reads SET):
 *              SET.init();
 *
 *      3.  Called by DATAr when 'SiiVV' packets arrive:
 *              SET.applyReceivedSetting(id, value)
 *              SET.getSettingInfo(id)
 *              SET.getSettingCount()
 *
 *  4.  Read by LED for HB dual-colour logic:
 *      SET.getHBDualValue()
 *
 *  Adding / removing a setting:
 *      Add (or remove) one line in _populateSettings() - the row is
 *      built and placed automatically into its category's container,
 *      wherever that category lands among CAT_Info.
 *
 *  Adding / removing / reordering a CATEGORY (section):
 *      Add, remove, or reorder one line in _populateCategories() - the
 *      whole section (title + container + separator) is built and placed
 *      automatically. Nothing in the XML layout needs to change; only
 *      cat_sections_root needs to exist.
 *
 *  Reordering settings on screen WITHOUT touching Arduino ids:
 *      Every SettingInfo carries its own explicit Arduino EEPROM id as the
 *      first constructor argument. The on-screen order of rows (within a
 *      category) is simply the order of the .add() calls in
 *      _populateSettings() - but the id travels with the SettingInfo object,
 *      not with its position in that list. So moving an .add() line up or
 *      down only changes where it's drawn; it is sent/received with the
 *      same id as before. The only rule: never reuse an id, and never
 *      change an existing setting's id unless the Arduino firmware's
 *      EEPROM layout changes to match.
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      DATAs  = DataSend
 *      DATAr  = DataReceive
 *      LED   = LEDManager
 *      SET   = SettingsManager   (this class)
 *      STS   = StatusManager
 * ============================================================
 */

import android.content.Intent;
import android.graphics.Typeface;
import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.widget.Button;
import androidx.appcompat.widget.SwitchCompat;
import android.widget.TextView;

import androidx.core.content.ContextCompat;
import androidx.core.graphics.drawable.DrawableCompat;

import com.xw.repo.BubbleSeekBar;

import java.util.ArrayList;
import java.util.List;

public class SettingsManager {

    // --------------------------------------------------------
    // Constants – must match Arduino firmware
    // --------------------------------------------------------
    /**
     * Total number of settings stored in Arduino EEPROM.
     * Sending setting index == SET_TOTAL triggers a full reset.
     */
    public final int SET_TOTAL = 50;

    /**
     * Arduino EEPROM id of the "DUAL COLOR" on/off setting.
     * Used by LED to decide whether the HB LED shows dual-colour.
     */
    public final int SET_HB_Dual   = 31;

    /**
     * Arduino EEPROM ids of the HB effect settings, read by
     * HBEffectSimulator to locally replay the firmware's HB animation.
     */
    public final int SET_HB_Effect        = 32;  /* idle HB effect, 0=static 1-14=animated (HB_EFFECT_HANDLERS) */
    public final int SET_HB_EffectSpeed   = 33;  /* idle HB effect frame period, ms */
    public final int SET_TvOnHBEffect     = 34;  /* HB effect played while TV is turning on (HB_ON_HANDLERS) */
    public final int SET_TvOnBrClInc      = 24;  /* brightness step used by TV-on & several idle HB effects */
    public final int SET_TvOnBrClDelay    = 23;  /* tick delay (ms) driving the TV-on HB helpers */

    /** Maximum brightness the app allows on the SeekBar */
    private final int LED_MAX_BAR_BRIGHTNESS    = 120;

    /** Maximum time (seconds) for the off-effect timer */
    private final int LED_MAX_BAR_MAX_TIME      = 180;

    /** Maximum brightness/clear delay (ms) for the SeekBar */
    private final int LED_MAX_BAR_BRCL_DELAY    = 150;

    /** Maximum brightness/clear incremental step */
    private final int LED_MAX_BAR_BRCL_INCREMENTAL = 10;

    // --------------------------------------------------------
    // Debug / TestMode string tables  (read-only)
    // --------------------------------------------------------
    /**
     * Debug item names shown in the debug-list dialog.
     * Index sent to Arduino via DATAs.sendDebug(index) - the order here MUST
     * match the firmware's __debug enum in _FuZzAPP_SmartTV_R4_DEF.h, or the
     * picker fires the wrong dump.
     */
    public final String[] SET_Debug = {
        "LED_INFO", "LED_SELECTED", "LED_ORDER", "LED_COLOR", "LED_PRECOLOR",
        "MOTION", "TV", "EEPROM", "APP", "AMBILIGHT", "BME280",
        "AMBIENT_MODE", "WIFI", "ARDUINO", "LIGHT_SENSOR", "HEARTBEAT", "DIFFUSER",
        "TASK", "TELNET", "RTC", "TEST_MODE", "ALL", "MQTT"
    };

    /**
     * One-line subtitles parallel to SET_Debug (grid UI only).
     * SET_Debug, SET_DebugSub and SET_DebugIcon are index-parallel and MUST
     * stay the same length - DebugGridAdapter.getView() indexes all three with
     * the same position and throws if any one is short.
     */
    public final String[] SET_DebugSub = {
        "strip state", "selection", "pixel order", "color buf", "pre-color",
        "sensor", "ADC / state", "settings", "UDP link", "UDPRAW", "temp / hum",
        "AM state", "net status", "RAM / time", "LUX ADC", "HB effect", "DIF UDP",
        "scheduler", "console link", "clock / NTP", "forced state", "everything", "broker link"
    };

    /** Emoji icons parallel to SET_Debug (grid UI only). */
    public final String[] SET_DebugIcon = {
        "\uD83D\uDCA1", "\uD83C\uDFAF", "\uD83D\uDD32", "\uD83C\uDFA8", "\u270F\uFE0F",
        "\uD83C\uDFC3", "\uD83D\uDCFA", "\uD83D\uDCBE", "\uD83D\uDCF1", "\uD83C\uDF08", "\uD83C\uDF21\uFE0F",
        "\uD83C\uDF19", "\uD83D\uDCF6", "\uD83E\uDD16", "\uD83D\uDFE1", "\u2764\uFE0F", "\uD83D\uDCA7",
        "\u2699\uFE0F", "\u2328\uFE0F", "\uD83D\uDD52", "\uD83E\uDDEA", "\uD83D\uDCCB", "\u2601\uFE0F"
    };

    /**
     * TestMode chip names. NONE sends the cancel command directly; every
     * other chip opens a TestModePopup with its own option list (see
     * _showTestModePopup()) which sends the matching @-command.
     */
    public final String[] SET_TestMode = {
        "NONE", "TV", "MOTION", "UDPRAW", "DIFFUSER", "LUX"
    };

    // --------------------------------------------------------
    // Settings metadata list
    // --------------------------------------------------------
    /**
     * SettingInfo for every setting 00-39.
     * Order here is purely VISUAL (top-to-bottom within a category) -
     * it has no bearing on which Arduino EEPROM id each entry uses.
     * Each entry stores: id, category, type, defaultValue, minValue,
     * maxValue, description, and optional named options[].
     */
    public List<SettingInfo> SET_Info = new ArrayList<>();

    /**
     * Same SettingInfo objects as SET_Info, but indexed by their Arduino
     * EEPROM id rather than by add()-order. This is the lookup DATAr/DATAs
     * and every send/receive path use - id is the only thing the Arduino
     * ever sees, so it's the only thing that should ever be used to find
     * a setting again.
     */
    private final SettingInfo[] SET_InfoById = new SettingInfo[SET_TOTAL];

    /**
     * CategoryInfo for every section (title + container), one per
     * SettingInfo.CAT_* constant. Order here is purely VISUAL - it's the
     * top-to-bottom order sections are inflated into cat_sections_root.
     */
    public List<CategoryInfo> CAT_Info = new ArrayList<>();

    /**
     * The empty row container LinearLayout that _buildCategorySections()
     * inflated for each category, indexed by CategoryInfo id (a
     * SettingInfo.CAT_* constant). This is the lookup _buildSettingsRows()
     * uses to find which container a given setting's rows go into - it
     * replaces the old per-category switch + 6 hardcoded _findContainer()
     * calls.
     */
    private final ViewGroup[] CAT_ContainerById = new ViewGroup[SettingInfo.CAT_TOTAL];

    // --------------------------------------------------------
    // Views
    // --------------------------------------------------------
    /**
     * One RowViews per setting (index-matched to SET_Info), holding
     * whichever views that setting's row actually has. Built fresh by
     * _buildSettingsRows() every time init() runs.
     */
    private final RowViews[] SET_Rows = new RowViews[SET_TOTAL];

    /** Background-image label (tap to toggle filmstrip) */
    public TextView SET_BG_Text;


    /** Scroll wrapper around the filmstrip */

    /** TestMode chip-row container – chips added by _setupTestModeChips() */
    public android.widget.LinearLayout SET_TESTMODE_ChipRow;

    /** Scroll wrapper around the TestMode chip row – GONE until user taps _testModeLabel */


    /** Resets all settings to their default values */
    public Button SET_BTN_Default;

    /** Toggles the LED strip on/off (hardware enable/disable) */
    public Button SET_BTN_EnableDisable;

    /** Opens the theme-picker popup (Crimson … Violet accent presets) */
    public Button SET_BTN_Theme;

    /**
     * Category title + stripe Views built by _buildCategorySections(),
     * kept around so applyTheme() can re-tint them in place without
     * rebuilding (and losing) any live setting values.
     */
    private final List<TextView> _catTitleViews  = new ArrayList<>();
    private final List<View>     _catStripeViews = new ArrayList<>();
    private final List<View>     _catLineViews   = new ArrayList<>();

    /** The two "fake line" separators flanking the BG/TestMode block, and the TestMode label itself. */
    private TextView _testModeLabel;

    /** Index of the currently-selected TestMode chip (0 = NONE), tracked so
     *  applyTheme() can re-highlight the right chip without a rebuild. */
    private int _activeTestMode = 0;

    /**
     * Holds whichever views a single setting row actually uses.
     * Only the fields relevant to si.type() are populated - the rest
     * stay null.
     */
    private static class RowViews {
        int type;
        TextView label;
        /** Bar rows: numeric value on the right. Selectable rows: option name in the middle. */
        TextView value;
        BubbleSeekBar seekBar;          // bar rows only
        View stepMinus, stepPlus;       // bar rows only - ±1 nudge buttons
        android.widget.CompoundButton checkBox; // switch rows only (CheckBox or SwitchCompat)
        View header;                    // selectable rows only - tap to open the options popup
        TextView chevron;               // selectable rows only - static ▾ dropdown indicator
        ViewGroup optionsContainer;      // selectable rows only - unused placeholder from the shared row layout
        TextView[] optionViews;          // chip rows only - one per chip, for highlighting
        View divider;                    // the 1dp line above this row - re-tinted with the rest
        /** Guards CheckBox's listener from firing on programmatic setChecked() calls. */
        boolean suppressSwitchEvent = false;
    }

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /**
     * Main – used for:
     *   Main._Toast()         short user messages
     *   Main._Console()       debug console
     *   Main._ShakeButton()   button shake animation
     *   Main.startActivityForResult() – folder picker
     *   Main.bgImageList / bgImagesLoaded / bgImageSet  – BG image state
     *   Main._SetBackgroundImage()  – apply GIF background
     */
    private final MainActivity Main;

    /**
     * DATAs – sends setting changes to the Arduino.
     * Alias: DATAs (DataSend)
     */
    private final DataSend DATAs;

    /**
     * STS – read for enable/disable LED state label.
     * Alias: STS (StatusManager)
     */
    private final StatusManager STS;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (alias: Main).
     * @param udps  DataSend instance      (alias: DATAs).
     * @param sts   StatusManager instance (alias: STS).
     */
    public SettingsManager(MainActivity main, DataSend udps, StatusManager sts) {
        this.Main = main;
        this.DATAs = udps;
        this.STS  = sts;
    }

    // ========================================================
    //  Initialisation  (call from Main.onCreate)
    // ========================================================

    /**
     * Populate SET_Info, build all setting rows, and attach all listeners.
     * Call once, AFTER setContentView() and BEFORE LED.init().
     */
    public void init() {
        _bindViews();
        _populateCategories();
        _buildCategorySections();
        _populateSettings();
        _buildSettingsRows();
        _setupCategoryTabs();
        _setupBackgroundButton();
        _setupTestModeButton();
        _setupButtons();
    }

    // --------------------------------------------------------
    // View binding
    // --------------------------------------------------------
    private void _bindViews() {
        SET_BTN_Default       = Main.findViewById(R.id._btn_setting_default);
        SET_BTN_EnableDisable = Main.findViewById(R.id._btn_en_dis);
        SET_BTN_Theme         = Main.findViewById(R.id._btn_theme);
        // The BG label became the footer BG button; Button extends TextView,
        // so MainActivity's existing setText() feedback still lands on it.
        SET_BG_Text           = Main.findViewById(R.id._btn_bg);

        _testModeLabel        = Main.findViewById(R.id._btn_testmode);
        _tintStaticChrome();
    }

    /** Re-tints the handful of static (non-row) settings-panel chrome: TestMode label + the two fake-line separators. */
    private void _tintStaticChrome() {
        // TEST is a plain footer button now, same as RESET / THEME / BG - it
        // must not keep the filled chip-pill background the inline label had.
        if (_testModeLabel != null) {
            _testModeLabel.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
        }

        // sv_setting / effInfo are now borderless (_stroke_line_settings / _panel_flat) -
        // do NOT tintFrameStroke them, it would just grow a 2dp border back on.
    }

    // --------------------------------------------------------
    // Category section definitions
    // --------------------------------------------------------
    /**
     * Define all category sections, in on-screen (top-to-bottom) order.
     * Adding, removing, or reordering a line here is all it takes to
     * add, remove, or reorder a settings section - same as adding a
     * setting is just one line in _populateSettings().
     */
    private void _populateCategories() {
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_TV,        "TV",            "\uD83D\uDCFA"));
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_MOTION,    "MOTION",        "\uD83C\uDFC3"));
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_HB,        "HEARTBEAT",     "\u2764\uFE0F"));
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_AMBILIGHT, "AMBILIGHT",     "\uD83C\uDF08"));
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_DIFFUSER,  "AROMA DIFFUSER","\uD83D\uDCA7"));
        CAT_Info.add(new CategoryInfo(SettingInfo.CAT_OTHER,     "OTHER",         "\u2699\uFE0F"));

        Log.d("CAT_Info", "CAT_Info size: " + CAT_Info.size());
    }

    // --------------------------------------------------------
    // Category section building  (replaces the old per-category XML blocks)
    // --------------------------------------------------------
    /**
     * Walk CAT_Info, and for every category inflate one
     * item_settings_category section (title + empty row container +
     * separator) into cat_sections_root, in CAT_Info order. Stashes each
     * section's row container into CAT_ContainerById, so
     * _buildSettingsRows() can find it by category id afterwards.
     */
    private void _buildCategorySections() {
        LayoutInflater inflater = LayoutInflater.from(Main);
        ViewGroup root = _findContainer("cat_sections_root");

        for (CategoryInfo cat : CAT_Info) {
            View section = inflater.inflate(R.layout.item_settings_category, root, false);

            TextView title       = section.findViewById(R.id.cat_title);
            TextView icon        = section.findViewById(R.id.cat_icon);
            View     stripe      = section.findViewById(R.id.cat_stripe);
            View     bottomLine  = section.findViewById(R.id.cat_bottom_line);
            ViewGroup container  = section.findViewById(R.id.cat_container);

            title.setText(cat.title());
            title.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
            if (icon != null && cat.icon() != null) icon.setText(cat.icon());
            CAT_ContainerById[cat.id()] = container;

            _catTitleViews.add(title);
            if (stripe != null) {
                _catStripeViews.add(stripe);
                _tintCatStripe(stripe);
            }
            if (bottomLine != null) {
                _catLineViews.add(bottomLine);
                _tintGlowDivider(bottomLine);
            }

            root.addView(section);
        }
    }

    /** Re-tints a category header's gradient fade stripe to the active theme's accent. */
    private void _tintCatStripe(View stripe) {
        _tintCatStripe(stripe, ThemeManager.getColor(Main, R.color.skbar_settings_title));
    }

    /** Re-tints a category header's gradient fade stripe to an explicit color (crossfade preview). */
    private void _tintCatStripe(View stripe, int color) {
        android.graphics.drawable.Drawable d = stripe.getBackground();
        if (d == null) return;
        d = d.mutate();
        DrawableCompat.setTint(d, color);
        stripe.setBackground(d);
    }

    /** Re-tints a glow divider (@layout/_divider_glow include root: halo + core) to the active theme's accent. */
    private void _tintGlowDivider(View dividerRoot) {
        _tintGlowDivider(dividerRoot, ThemeManager.getColor(Main, R.color.skbar_settings_title));
    }

    /** Re-tints a glow divider's halo + core sub-views to an explicit color. */
    private void _tintGlowDivider(View dividerRoot, int color) {
        if (dividerRoot == null) return;
        _tintDrawable(dividerRoot.findViewById(R.id.glow_halo), color);
        _tintDrawable(dividerRoot.findViewById(R.id.glow_core),  color);
    }

    private void _tintDrawable(View v, int color) {
        if (v == null) return;
        android.graphics.drawable.Drawable d = v.getBackground();
        if (d == null) return;
        d = d.mutate();
        DrawableCompat.setTint(d, color);
        v.setBackground(d);
    }


    /**
     * Registers one setting: adds it to SET_Info (visual order = call
     * order) and indexes it by id into SET_InfoById (Arduino-facing
     * lookup). Use this instead of SET_Info.add() directly, or the two
     * structures will drift apart.
     */
    private void _addSetting(SettingInfo si) {
        int id = si.id();
        if (id < 0 || id >= SET_TOTAL) {
            Log.e("SET_Info", "Setting [" + si.description()
                    + "] has out-of-range id " + id + " - SKIPPED");
            return;
        }
        if (SET_InfoById[id] != null) {
            Log.e("SET_Info", "Duplicate id " + id + " - [" + si.description()
                    + "] collides with [" + SET_InfoById[id].description()
                    + "] - SKIPPED");
            return;
        }
        SET_Info.add(si);
        SET_InfoById[id] = si;
    }

    /** Looks up a setting by its Arduino EEPROM id (the only key DATAr/DATAs ever use). */
    private SettingInfo _byId(int id) {
        return SET_InfoById[id];
    }

    /**
     * Define all 40 settings.
     * SettingInfo(id, category, type, default, min, max, description [, options…])
     *   id        → the Arduino EEPROM id - this is what's sent/received over
     *               UDP. Reordering these .add() calls changes ONLY the
     *               on-screen row order within a category; it never changes
     *               which id a setting is wired to, because id lives on the
     *               SettingInfo object itself, not on its list position.
     *   max == 0  → options list (bar/selectable max = options.length-1)
     *   max == 1  → ON/OFF toggle (TYPE_SWITCH)
     *   max >  1  → numeric range [min..max]
     */
    private void _populateSettings() {
        // CAT_TV
        _addSetting(new SettingInfo(0, SettingInfo.CAT_TV, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "ON EFFECT", "default","random + static","mid to out (sep)", "mid to out (all)","half run","half run random", "4-anchor expand","2-anchor expand","COM scanner","TV center sweep", "liquid fill","pixel boot"));
        _addSetting(new SettingInfo(1, SettingInfo.CAT_TV, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "OFF EFFECT", "default","delay (w tv off)","delay (all)", "Slow TV ~ C ~ UC ~ B ~ L","countdown", "bomb countdown","random half", "TV center sweep"));
        _addSetting(new SettingInfo(2, SettingInfo.CAT_TV, SettingInfo.TYPE_TIME_S, 0, 0, LED_MAX_BAR_MAX_TIME, "OFF EFFECT TIME"));
        _addSetting(new SettingInfo(17, SettingInfo.CAT_TV, SettingInfo.TYPE_BRIGHTNESS, 7, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS TV"));
        _addSetting(new SettingInfo(3, SettingInfo.CAT_TV, SettingInfo.TYPE_BRIGHTNESS, 7, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS COM"));
        _addSetting(new SettingInfo(4, SettingInfo.CAT_TV, SettingInfo.TYPE_BRIGHTNESS, 40, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS uCOM"));
        _addSetting(new SettingInfo(5, SettingInfo.CAT_TV, SettingInfo.TYPE_BRIGHTNESS, 40, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS BED"));
        _addSetting(new SettingInfo(6, SettingInfo.CAT_TV, SettingInfo.TYPE_BRIGHTNESS, 5, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS LAMP"));
        _addSetting(new SettingInfo(15, SettingInfo.CAT_TV, SettingInfo.TYPE_SELECTABLE, 2, 0, 0, "RANDOM COLOR START", "off","random colors","random dual"));
        _addSetting(new SettingInfo(23, SettingInfo.CAT_TV, SettingInfo.TYPE_TIME_MS, 15, 1, LED_MAX_BAR_BRCL_DELAY, "TV ON # BR/CL DELAY"));
        _addSetting(new SettingInfo(24, SettingInfo.CAT_TV, SettingInfo.TYPE_INCREMENTAL, 4, 1, LED_MAX_BAR_BRCL_INCREMENTAL, "TV ON # BR/CL INCREMENTAL"));
        _addSetting(new SettingInfo(25, SettingInfo.CAT_TV, SettingInfo.TYPE_TIME_MS, 8, 1, LED_MAX_BAR_BRCL_DELAY, "TV OFF # BR/CL DELAY"));
        _addSetting(new SettingInfo(26, SettingInfo.CAT_TV, SettingInfo.TYPE_INCREMENTAL, 3, 1, LED_MAX_BAR_BRCL_INCREMENTAL, "TV OFF # BR/CL INCREMENTAL"));

        // CAT_MOTION
        _addSetting(new SettingInfo(20, SettingInfo.CAT_MOTION, SettingInfo.TYPE_SELECTABLE, 3, 0, 0, "ON EFFECT", "default","from middle","line moving","random", "cascade","the collision", "right slide"));
        _addSetting(new SettingInfo(7, SettingInfo.CAT_MOTION, SettingInfo.TYPE_BRIGHTNESS, 40, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS"));
        _addSetting(new SettingInfo(8, SettingInfo.CAT_MOTION, SettingInfo.TYPE_TIME_S, 120, 0, LED_MAX_BAR_MAX_TIME, "ON TIME"));
        _addSetting(new SettingInfo(10, SettingInfo.CAT_MOTION, SettingInfo.TYPE_SWITCH, 1, 0, 1, "RANDOM COLOR","off","on"));
        _addSetting(new SettingInfo(16, SettingInfo.CAT_MOTION, SettingInfo.TYPE_SWITCH, 1, 0, 1, "DIVIDE BRIGHTNESS","off","on"));
        _addSetting(new SettingInfo(18, SettingInfo.CAT_MOTION, SettingInfo.TYPE_TIME_S, 60, 1, LED_MAX_BAR_MAX_TIME, "RENEW COLOR TIME"));
        _addSetting(new SettingInfo(19, SettingInfo.CAT_MOTION, SettingInfo.TYPE_TIME_M, 30, 1, LED_MAX_BAR_MAX_TIME, "AUTO OFF TIME"));
        _addSetting(new SettingInfo(27, SettingInfo.CAT_MOTION, SettingInfo.TYPE_TIME_MS, 2, 1, LED_MAX_BAR_BRCL_DELAY, "BR/CL DELAY"));
        _addSetting(new SettingInfo(28, SettingInfo.CAT_MOTION, SettingInfo.TYPE_INCREMENTAL, 4, 1, LED_MAX_BAR_BRCL_INCREMENTAL, "BR/CL INCREMENTAL"));

        // CAT_HB
        _addSetting(new SettingInfo(31, SettingInfo.CAT_HB, SettingInfo.TYPE_SWITCH, 0, 0, 1, "DUAL COLOR","off","on"));
        _addSetting(new SettingInfo(32, SettingInfo.CAT_HB, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "EFFECT", "static","white move","heartbeat","random fade", "traveling shadow","expanding raindrops","colors", "shooting star random","random sparkling pop", "gliding aurora","the glitch matrix", "stochastic plasma","digital rain", "dual pulse sinusoidal", "rainbow wave pulse"));
        _addSetting(new SettingInfo(33, SettingInfo.CAT_HB, SettingInfo.TYPE_TIME_MS, 8, 1, LED_MAX_BAR_BRCL_DELAY, "EFFECT SPEED"));
        _addSetting(new SettingInfo(34, SettingInfo.CAT_HB, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "TV ON HB EFFECT", "fade on", "center bloom", "linear sweep", "quad point"));

        // CAT_AMBILIGHT
        _addSetting(new SettingInfo(9, SettingInfo.CAT_AMBILIGHT, SettingInfo.TYPE_BRIGHTNESS, 60, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS"));
        _addSetting(new SettingInfo(29, SettingInfo.CAT_AMBILIGHT, SettingInfo.TYPE_TIME_MS, 60, 1, LED_MAX_BAR_BRCL_DELAY, "BR/CL DELAY"));
        _addSetting(new SettingInfo(30, SettingInfo.CAT_AMBILIGHT, SettingInfo.TYPE_INCREMENTAL, 1, 1, LED_MAX_BAR_BRCL_INCREMENTAL, "BR/CL INCREMENTAL"));

        // CAT_DIFFUSER
        _addSetting(new SettingInfo(35, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "EFFECT", "Static", "Fade", "Pulse", "Random", "Rainbow", "Sparkle", "Fire", "Bounce", "Confetti"));
        _addSetting(new SettingInfo(43, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_BRIGHTNESS, 40, 0, LED_MAX_BAR_BRIGHTNESS, "BRIGHTNESS"));
        _addSetting(new SettingInfo(44, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_TIME_MS, 30, 1, LED_MAX_BAR_BRCL_DELAY, "EFFECT SPEED"));
        _addSetting(new SettingInfo(36, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "MODE TV", "off", "continuous", "10 sec", "2h after sleep", "4h after sleep"));
        _addSetting(new SettingInfo(37, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "MODE MOTION", "off", "continuous", "10 sec", "2h after sleep", "4h after sleep"));
        _addSetting(new SettingInfo(38, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "MODE UDPRAW", "off", "continuous", "10 sec", "2h after sleep", "4h after sleep"));
        _addSetting(new SettingInfo(39, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "MODE AMBIENT", "off", "continuous", "10 sec", "2h after sleep", "4h after sleep"));
        _addSetting(new SettingInfo(40, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_TIME_M, 30, 1, 180, "IDLE WAIT MIN"));
        _addSetting(new SettingInfo(41, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_TIME_M, 10, 1, 180, "IDLE ON MIN"));
        _addSetting(new SettingInfo(42, SettingInfo.CAT_DIFFUSER, SettingInfo.TYPE_SELECTABLE, 0, 0, 0, "MODE IDLE", "off", "continuous", "10 sec", "2h after sleep", "4h after sleep"));

        // CAT_OTHER
        _addSetting(new SettingInfo(11, SettingInfo.CAT_OTHER, SettingInfo.TYPE_TIME_MS, 3, 1, LED_MAX_BAR_BRCL_DELAY, "BR/CL DELAY"));
        _addSetting(new SettingInfo(12, SettingInfo.CAT_OTHER, SettingInfo.TYPE_INCREMENTAL, 3, 1, LED_MAX_BAR_BRCL_INCREMENTAL, "BR/CL INCREMENTAL"));
        _addSetting(new SettingInfo(13, SettingInfo.CAT_OTHER, SettingInfo.TYPE_INCREMENTAL, 150, 1, 150, "BRIGHTNESS LUX INCREASE"));
        _addSetting(new SettingInfo(14, SettingInfo.CAT_OTHER, SettingInfo.TYPE_TIME_S, 30, 0, LED_MAX_BAR_MAX_TIME, "TO OFF TIME"));
        _addSetting(new SettingInfo(21, SettingInfo.CAT_OTHER, SettingInfo.TYPE_TIME_M, 30, 1, LED_MAX_BAR_MAX_TIME, "AMBIENT MODE TIME"));
        _addSetting(new SettingInfo(22, SettingInfo.CAT_OTHER, SettingInfo.TYPE_SELECTABLE, 5, 0, 0, "LED FPS",
                "15", "25", "30", "60", "90", "120", "150", "200", "240"));

        Log.d("SET_Info", "SET_Info size: " + SET_Info.size());
    }

    // --------------------------------------------------------
    // Settings row building  (replaces the old per-setting XML blocks)
    // --------------------------------------------------------
    /**
     * Walk SET_Info, and for every setting inflate the row template that
     * matches its type into the container that matches its category.
     * Containers come from CAT_ContainerById, built by
     * _buildCategorySections() - must run AFTER that, and after
     * _populateCategories().
     */
    private void _buildSettingsRows() {
        LayoutInflater inflater = LayoutInflater.from(Main);

        for (int i = 0; i < SET_Info.size(); i++) {
            SettingInfo s = SET_Info.get(i);
            final int id = s.id(); // Arduino EEPROM id - NOT the loop/list position

            ViewGroup container = (s.category() >= 0 && s.category() < CAT_ContainerById.length)
                    ? CAT_ContainerById[s.category()]
                    : null;
            if (container == null) {
                Log.d("SET_Info", "No container for setting id " + id
                        + " (" + s.description() + "), category=" + s.category());
                continue;
            }

            View row;
            switch (s.type()) {
                case SettingInfo.TYPE_SWITCH:
                    row = _buildSwitchRow(inflater, container, id, s);
                    break;
                case SettingInfo.TYPE_SELECTABLE:
                    row = (s.options().length <= 5)
                            ? _buildChipSelectableRow(inflater, container, id, s)
                            : _buildSelectableRow(inflater, container, id, s);
                    break;
                default: // TYPE_TIME, TYPE_DELAY, TYPE_INCREMENTAL, TYPE_BRIGHTNESS
                    row = _buildBarRow(inflater, container, id, s);
                    break;
            }
            container.addView(row);
        }
    }

    // --------------------------------------------------------
    // Category tab bar
    // --------------------------------------------------------
    /** Active tab index — tracks which category section is currently shown. */
    private int _activeTab = 0;

    /** In-flight theme-switch crossfade, if any — cancelled if a new theme is picked mid-animation. */
    private ValueAnimator _themeAnim;

    /**
     * Builds one tab button per CategoryInfo, injects them into
     * settings_tab_row, and wires click to show/hide category sections.
     * Only the selected section is VISIBLE; all others GONE.
     */
    private void _setupCategoryTabs() {
        android.widget.LinearLayout tabRow = Main.findViewById(R.id.settings_tab_row);
        if (tabRow == null) return;

        float dp  = Main.getResources().getDisplayMetrics().density;
        int padH  = (int)(8  * dp);
        int padV  = (int)(4  * dp);
        int gap   = (int)(4  * dp);

        int colorActive   = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int colorInactive = ThemeManager.getColor(Main, R.color.skbar_upper_text);

        android.graphics.drawable.GradientDrawable bgActive = new android.graphics.drawable.GradientDrawable();
        bgActive.setCornerRadius(4 * dp);
        bgActive.setColor(ThemeManager.getColor(Main, R.color.tab_active_tint));
        bgActive.setStroke((int)(1 * dp), colorActive);

        android.graphics.drawable.GradientDrawable bgInactive = new android.graphics.drawable.GradientDrawable();
        bgInactive.setCornerRadius(4 * dp);
        bgInactive.setColor(android.graphics.Color.TRANSPARENT);

        /* Spacer at start */
        android.widget.Space startSpacer = new android.widget.Space(Main);
        startSpacer.setLayoutParams(new android.widget.LinearLayout.LayoutParams(0,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        tabRow.addView(startSpacer);

        TextView[] tabs = new TextView[CAT_Info.size()];

        for (int i = 0; i < CAT_Info.size(); i++) {
            final int idx  = i;
            CategoryInfo cat = CAT_Info.get(i);

            TextView tab = new TextView(Main);
            android.widget.LinearLayout.LayoutParams lp = new android.widget.LinearLayout.LayoutParams(
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT,
                    android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
            lp.setMarginEnd(gap);
            tab.setLayoutParams(lp);
            tab.setPadding(padH, padV, padH, padV);
            tab.setText(cat.title());
            tab.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 10);
            tab.setLetterSpacing(0.08f);
            tab.setTypeface(null, android.graphics.Typeface.BOLD);
            tab.setClickable(true);
            tab.setFocusable(true);

            /* Initial state - reflects whichever tab is actually active */
            if (i == _activeTab) {
                android.graphics.drawable.GradientDrawable a = new android.graphics.drawable.GradientDrawable();
                a.setCornerRadius(4 * dp);
                a.setColor(ThemeManager.getColor(Main, R.color.tab_active_tint));
                a.setStroke((int)(1 * dp), colorActive);
                tab.setBackground(a);
                tab.setTextColor(colorActive);
            } else {
                android.graphics.drawable.GradientDrawable n = new android.graphics.drawable.GradientDrawable();
                n.setCornerRadius(4 * dp);
                n.setColor(android.graphics.Color.TRANSPARENT);
                tab.setBackground(n);
                tab.setTextColor(colorInactive);
            }

            tab.setOnClickListener(v -> {
                /* Recompute fresh each tap so a theme change since setup is honoured */
                int active   = ThemeManager.getColor(Main, R.color.skbar_settings_title);
                int inactive = ThemeManager.getColor(Main, R.color.skbar_upper_text);
                int activeBg = ThemeManager.getColor(Main, R.color.tab_active_tint);

                /* Update tab visuals */
                for (int j = 0; j < tabs.length; j++) {
                    boolean sel = (j == idx);
                    android.graphics.drawable.GradientDrawable bg =
                            new android.graphics.drawable.GradientDrawable();
                    bg.setCornerRadius(4 * dp);
                    bg.setColor(sel ? activeBg : android.graphics.Color.TRANSPARENT);
                    if (sel) bg.setStroke((int)(1 * dp), active);
                    tabs[j].setBackground(bg);
                    tabs[j].setTextColor(sel ? active : inactive);
                }
                /* Show only the selected section, hide rest */
                _activeTab = idx;
                _applyCategoryTabVisibility();
            });

            tabs[i] = tab;
            tabRow.addView(tab);
        }

        /* Spacer at end */
        android.widget.Space endSpacer = new android.widget.Space(Main);
        endSpacer.setLayoutParams(new android.widget.LinearLayout.LayoutParams(0,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        tabRow.addView(endSpacer);

        _applyCategoryTabVisibility();
    }

    /** Re-tints every category tab to the active theme, using _activeTab to know which is selected. */
    private void _refreshCategoryTabColors() {
        android.widget.LinearLayout tabRow = Main.findViewById(R.id.settings_tab_row);
        if (tabRow == null) return;

        float dp = Main.getResources().getDisplayMetrics().density;
        int colorActive   = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int colorInactive = ThemeManager.getColor(Main, R.color.skbar_upper_text);
        int activeBg      = ThemeManager.getColor(Main, R.color.tab_active_tint);

        int tabIndex = 0;
        for (int i = 0; i < tabRow.getChildCount(); i++) {
            View child = tabRow.getChildAt(i);
            if (!(child instanceof TextView)) continue; // skip start/end Space
            boolean sel = (tabIndex == _activeTab);
            android.graphics.drawable.GradientDrawable bg = new android.graphics.drawable.GradientDrawable();
            bg.setCornerRadius(4 * dp);
            bg.setColor(sel ? activeBg : android.graphics.Color.TRANSPARENT);
            if (sel) bg.setStroke((int)(1 * dp), colorActive);
            child.setBackground(bg);
            ((TextView) child).setTextColor(sel ? colorActive : colorInactive);
            tabIndex++;
        }
    }

    /** Shows only the active tab's category section; hides the rest, crossfading between them. */
    private void _applyCategoryTabVisibility() {
        for (CategoryInfo cat : CAT_Info) {
            ViewGroup container = CAT_ContainerById[cat.id()];
            if (container == null) continue;
            /* container's parent is the inflated item_settings_category section */
            android.view.View section = (android.view.View) container.getParent();
            if (section == null) continue;
            boolean show = (CAT_Info.indexOf(cat) == _activeTab);
            _crossfadeSectionVisibility(section, show);
        }
    }

    /**
     * Crossfades one category section between GONE and VISIBLE instead of an
     * instant cut. The outgoing and incoming sections fade concurrently
     * (each on its own ViewPropertyAnimator), so it reads as a single
     * crossfade rather than two separate pops.
     */
    private void _crossfadeSectionVisibility(View section, boolean show) {
        section.animate().cancel();
        final int duration = 180;

        if (show) {
            section.setAlpha(0f);
            section.setVisibility(View.VISIBLE);
            section.animate().alpha(1f).setDuration(duration).start();
        } else {
            section.animate()
                    .alpha(0f)
                    .setDuration(duration)
                    .withEndAction(() -> {
                        section.setVisibility(View.GONE);
                        section.setAlpha(1f);
                    })
                    .start();
        }
    }

        private ViewGroup _findContainer(String idName) {
        int id = Main.getResources().getIdentifier(idName, "id", Main.getPackageName());
        return Main.findViewById(id);
    }

    // --------------------------------------------------------
    // Theme re-tinting helpers (shared by every row type)
    // --------------------------------------------------------
    /**
     * Re-applies the active theme's accent colors to one row's chrome
     * (label, value, chevron, steppers, switch track) WITHOUT touching
     * its current value/selection state. Called once at row-build time
     * and again by applyTheme() after a live theme switch.
     */
    private void _tintRowChrome(RowViews rv) {
        if (rv == null) return;
        int labelColor = ThemeManager.getColor(Main, R.color.skbar_upper_text);
        int accent     = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int valueColor = ThemeManager.getColor(Main, R.color.text_default);

        if (rv.label   != null) rv.label.setTextColor(labelColor);
        if (rv.value   != null) {
            rv.value.setTextColor(valueColor);
            android.graphics.drawable.Drawable valueBg = rv.value.getBackground();
            if (valueBg instanceof android.graphics.drawable.GradientDrawable) {
                ((android.graphics.drawable.GradientDrawable) valueBg.mutate())
                        .setStroke((int) (1 * Main.getResources().getDisplayMetrics().density),
                                ThemeManager.getColor(Main, R.color.skbar_track_color));
            }
        }
        if (rv.chevron != null) rv.chevron.setTextColor(accent);
        if (rv.stepMinus instanceof TextView) ((TextView) rv.stepMinus).setTextColor(accent);
        if (rv.stepPlus  instanceof TextView) ((TextView) rv.stepPlus).setTextColor(accent);
        if (rv.checkBox  != null) _tintSwitchTrack(rv.checkBox);
        if (rv.divider   != null) rv.divider.setBackgroundColor(
                ThemeManager.getColor(Main, R.color.skbar_line_between));
        if (rv.optionsContainer != null) rv.optionsContainer.setBackgroundColor(
                ThemeManager.getColor(Main, R.color.skbar_whole_settings));
        if (rv.seekBar   != null) {
            rv.seekBar.setTrackColor(ThemeManager.getColor(Main, R.color.skbar_track_color));
            rv.seekBar.setSecondTrackColor(ThemeManager.getColor(Main, R.color.skbar_second_track_color));
            rv.seekBar.setThumbColor(valueColor);
        }
    }

    /** Re-tints a SwitchCompat row's ON/OFF track to the active theme via tint, leaving its shape alone. */
    private void _tintSwitchTrack(android.widget.CompoundButton cb) {
        if (!(cb instanceof SwitchCompat)) return;
        SwitchCompat sw = (SwitchCompat) cb;

        int[][] states = new int[][]{
                new int[]{android.R.attr.state_checked},
                new int[]{}
        };
        int[] colors = new int[]{
                ThemeManager.getColor(Main, R.color.skbar_settings_title),
                ThemeManager.getColor(Main, R.color.skbar_track_color)
        };
        sw.setTrackTintList(new android.content.res.ColorStateList(states, colors));
        sw.setTrackTintMode(android.graphics.PorterDuff.Mode.SRC_IN);
    }

    /**
     * TYPE_TIME / TYPE_DELAY / TYPE_INCREMENTAL / TYPE_BRIGHTNESS row.
     * Name on the left, − / value / + steppers for exact ±1 nudges, a clean
     * BubbleSeekBar fill underneath for broad drags - no numbers/bubble on
     * the bar itself. Steppers dim out (disabled) at the first/last value.
     */
    private View _buildBarRow(LayoutInflater inflater, ViewGroup parent, int id, SettingInfo s) {
        View row = inflater.inflate(R.layout.item_setting_bar, parent, false);

        TextView label     = row.findViewById(R.id.row_label);
        TextView value     = row.findViewById(R.id.row_value);
        BubbleSeekBar bar  = row.findViewById(R.id.row_seekbar);
        View stepMinus     = row.findViewById(R.id.row_step_minus);
        View stepPlus      = row.findViewById(R.id.row_step_plus);
        View divider       = row.findViewById(R.id.row_divider);

        label.setText(s.description());

        int max = _effectiveMax(s);
        bar.getConfigBuilder().min(s.minValue()).max(Math.max(max, s.minValue() + 1))
                .progress(s.currentValue()).build();

        RowViews rv = new RowViews();
        rv.type = s.type();
        rv.label = label;
        rv.value = value;
        rv.seekBar = bar;
        rv.stepMinus = stepMinus;
        rv.stepPlus = stepPlus;
        rv.divider = divider;
        SET_Rows[id] = rv;
        _tintRowChrome(rv);

        _updateBarValueLabel(id, s.currentValue());

        bar.setOnProgressChangedListener(new BubbleSeekBar.OnProgressChangedListenerAdapter() {
            @Override
            public void onProgressChanged(BubbleSeekBar b, int progress, float pf, boolean fromUser) {
                SettingInfo si = _byId(id);
                if (progress < si.minValue()) {
                    b.setProgress(si.minValue());
                    progress = si.minValue();
                }
                _updateBarValueLabel(id, progress);
            }

            @Override
            public void getProgressOnActionUp(BubbleSeekBar b, int progress, float pf) {
                SettingInfo si = _byId(id);
                if (progress < si.minValue()) {
                    b.setProgress(si.minValue());
                    progress = si.minValue();
                }
                _commitBarValue(id, progress);
            }
        });

        stepMinus.setOnClickListener(v -> _stepBarValue(id, -1));
        stepPlus.setOnClickListener(v -> _stepBarValue(id, 1));

        return row;
    }

    /** Real upper bound for a bar setting: maxValue(), or options.length-1 when options-driven. */
    private int _effectiveMax(SettingInfo s) {
        return (s.maxValue() == 0 && s.options() != null && s.options().length > 0)
                ? s.options().length - 1
                : s.maxValue();
    }

    /** Nudges a bar setting one step down (-1) or up (+1), clamped to its range. */
    private void _stepBarValue(int id, int direction) {
        RowViews rv = SET_Rows[id];
        SettingInfo si = _byId(id);
        int max = Math.max(_effectiveMax(si), si.minValue() + 1);

        int next = rv.seekBar.getProgress() + direction;
        if (next < si.minValue() || next > max) return; // already at an edge

        rv.seekBar.setProgress(next);
        _commitBarValue(id, next);
    }

    /** Applies a new bar value: stores it, refreshes the label/steppers, and sends it over UDP. */
    private void _commitBarValue(int id, int progress) {
        SettingInfo si = _byId(id);
        si.setCurrentValue(progress);
        _updateBarValueLabel(id, progress);

        String valueText = _formatBarValue(si, progress);
        Main._Toast(si.description() + " > " + valueText);
        Main._Console(false, "►►",
                "SETTING {{#C}" + si.description()
                + "{##}} > {{#Y}" + valueText + "{##}}");

        DATAs.sendSetting(id, SET_Info, SET_TOTAL);
    }

    /** Formats a bar-type value with the unit that matches its TYPE. */
    private String _formatBarValue(SettingInfo s, int value) {
        switch (s.type()) {
            case SettingInfo.TYPE_TIME_MS: return value + " ms";
            case SettingInfo.TYPE_TIME_S:  return value + " s";
            case SettingInfo.TYPE_TIME_M:  return value + " m";
            default:                     return String.valueOf(value); // INCREMENTAL / BRIGHTNESS
        }
    }

    /** Refreshes a bar row's value label and the enabled/dim state of its ± steppers. */
    private void _updateBarValueLabel(int id, int progress) {
        SettingInfo s = _byId(id);
        RowViews rv = SET_Rows[id];
        if (rv == null || rv.value == null) return;

        String newText = _formatBarValue(s, progress);
        boolean changed = !newText.equals(rv.value.getText().toString());
        rv.value.setText(newText);
        if (changed) _pulseValueChip(rv.value);

        if (rv.stepMinus == null || rv.stepPlus == null) return;
        int max = Math.max(_effectiveMax(s), s.minValue() + 1);
        boolean canDec = progress > s.minValue();
        boolean canInc = progress < max;
        rv.stepMinus.setEnabled(canDec);
        rv.stepPlus.setEnabled(canInc);
        rv.stepMinus.setAlpha(canDec ? 1f : 0.3f);
        rv.stepPlus.setAlpha(canInc ? 1f : 0.3f);
    }

    /**
     * Tiny scale-pulse on a value chip whenever its number actually
     * changes — a cheap "live" cue for bar-row values updated via drag,
     * steppers, or an incoming UDP status packet.
     */
    private void _pulseValueChip(View chip) {
        chip.animate().cancel();
        chip.setScaleX(1f);
        chip.setScaleY(1f);
        chip.animate()
                .scaleX(1.15f).scaleY(1.15f)
                .setDuration(70)
                .withEndAction(() -> chip.animate()
                        .scaleX(1f).scaleY(1f)
                        .setDuration(90)
                        .start())
                .start();
    }

    /**
     * TYPE_SWITCH row. Name on the left, checkbox on the right.
     */
    private View _buildSwitchRow(LayoutInflater inflater, ViewGroup parent, int id, SettingInfo s) {
        View row = inflater.inflate(R.layout.item_setting_switch, parent, false);

        TextView label = row.findViewById(R.id.row_label);
        android.widget.CompoundButton cb = row.findViewById(R.id.row_checkbox);
        View divider = row.findViewById(R.id.row_divider);

        label.setText(s.description());

        RowViews rv = new RowViews();
        rv.type = s.type();
        rv.label = label;
        rv.checkBox = cb;
        rv.divider = divider;
        SET_Rows[id] = rv;
        _tintRowChrome(rv);

        cb.setChecked(s.currentValue() == 1);

        cb.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (rv.suppressSwitchEvent) return;

            int value = isChecked ? 1 : 0;
            _byId(id).setCurrentValue(value);

            String state = isChecked ? "ON" : "OFF";
            Main._Toast(s.description() + " > " + state);
            Main._Console(false, "►►",
                    "SETTING {{#C}" + s.description()
                    + "{##}} > {" + (isChecked ? "{#G}ON{##}" : "{#R}OFF{##}") + "}");

            DATAs.sendSetting(id, SET_Info, SET_TOTAL);
        });

        return row;
    }

    /**
     * TYPE_SELECTABLE with ≤5 options — inline chip row instead of accordion.
     * Reuses item_setting_selectable for the label; hides chevron + value text;
     * appends a HorizontalScrollView chip row into the parent container.
     *
     * @param container  Category container (chip row appended here after the header row)
     */
    private View _buildChipSelectableRow(LayoutInflater inflater, ViewGroup container,
                                          int id, SettingInfo s) {
        View row = inflater.inflate(R.layout.item_setting_selectable, container, false);

        TextView label      = row.findViewById(R.id.row_label);
        TextView optionText = row.findViewById(R.id.row_option_value);
        TextView chevron    = row.findViewById(R.id.row_chevron);
        View header         = row.findViewById(R.id.row_header);
        ViewGroup optionsBox= row.findViewById(R.id.row_options_container);
        View divider        = row.findViewById(R.id.row_divider);

        label.setText(s.description());
        optionText.setVisibility(View.GONE); // chips replace the value text
        chevron.setVisibility(View.GONE);    // no accordion indicator needed
        header.setClickable(false);          // header is label-only, not a toggle

        // Build chip row (appended programmatically to container after header row)
        float dp    = Main.getResources().getDisplayMetrics().density;
        int padH    = (int)(12 * dp);
        int padV    = (int)( 6 * dp);
        int cornerR = (int)(16 * dp);
        int margin  = (int)( 3 * dp);

        int selBg   = ThemeManager.getColor(Main, R.color.tab_active_tint);
        int selText = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int defText = ThemeManager.getColor(Main, R.color.skbar_section_text_color);
        int defBg   = android.graphics.Color.TRANSPARENT;

        android.widget.HorizontalScrollView scroll =
                new android.widget.HorizontalScrollView(Main);
        scroll.setHorizontalScrollBarEnabled(false);
        android.widget.LinearLayout.LayoutParams scrollLp =
                new android.widget.LinearLayout.LayoutParams(
                        android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                        android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
        scrollLp.setMargins(0, 0, 0, (int)(4 * dp));
        scroll.setLayoutParams(scrollLp);

        android.widget.LinearLayout chipRow = new android.widget.LinearLayout(Main);
        chipRow.setOrientation(android.widget.LinearLayout.HORIZONTAL);
        chipRow.setPadding((int)(8 * dp), (int)(2 * dp), (int)(8 * dp), (int)(3 * dp));
        scroll.addView(chipRow);

        RowViews rv = new RowViews();
        rv.type = s.type();
        rv.label = label;
        rv.value = optionText;          // kept (not shown) so _updateSelectableDisplay doesn't NPE
        rv.header = header;
        rv.chevron = chevron;
        rv.optionsContainer = optionsBox;
        rv.divider = divider;

        String[] options = s.options();
        rv.optionViews = new android.widget.TextView[options.length];

        for (int i = 0; i < options.length; i++) {
            android.widget.TextView chip = new android.widget.TextView(Main);
            chip.setText(options[i]);
            chip.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 11);
            chip.setAllCaps(true);
            chip.setTypeface(null, android.graphics.Typeface.BOLD);
            chip.setPadding(padH, padV, padH, padV + (int)(2 * dp));

            boolean selected = (i == s.currentValue());
            chip.setTextColor(selected ? selText : defText);

            android.graphics.drawable.GradientDrawable bg =
                    new android.graphics.drawable.GradientDrawable();
            bg.setCornerRadius(cornerR);
            bg.setColor(selected ? selBg : defBg);
            bg.setStroke((int)(1 * dp),
                    ThemeManager.getColor(Main, R.color.skbar_track_color));
            chip.setBackground(bg);

            android.widget.LinearLayout.LayoutParams lp =
                    new android.widget.LinearLayout.LayoutParams(
                            android.widget.LinearLayout.LayoutParams.WRAP_CONTENT,
                            android.widget.LinearLayout.LayoutParams.WRAP_CONTENT);
            lp.setMargins(margin, 0, margin, 0);
            chip.setLayoutParams(lp);

            rv.optionViews[i] = chip;
            chipRow.addView(chip);

            final int optIdx = i;
            chip.setOnClickListener(v -> _selectOption(id, optIdx));
        }

        SET_Rows[id] = rv;
        _tintRowChrome(rv);

        // Attach the chip scroll to the category container right after this row
        // (posted so the row is added first)
        final android.widget.HorizontalScrollView finalScroll = scroll;
        container.post(() -> {
            int rowPos = container.indexOfChild(row);
            if (rowPos >= 0) container.addView(finalScroll, rowPos + 1);
        });

        return row;
    }

    // ========================================================
    // TYPE_SELECTABLE options popup — centred modal
    // ========================================================
    /** Currently-open options popup (selectable rows only) — dismissed on outside tap or selection. */
    private android.widget.PopupWindow _optionsPopup;

    /**
     * TYPE_SELECTABLE with >5 options. Tapping the header opens a centred
     * modal listing every named option (see _showOptionsPopup()) instead of
     * the old inline accordion.
     */
    private View _buildSelectableRow(LayoutInflater inflater, ViewGroup parent, int id, SettingInfo s) {
        View row = inflater.inflate(R.layout.item_setting_selectable, parent, false);

        TextView label       = row.findViewById(R.id.row_label);
        TextView optionText  = row.findViewById(R.id.row_option_value);
        TextView chevron     = row.findViewById(R.id.row_chevron);
        View header          = row.findViewById(R.id.row_header);
        ViewGroup optionsBox = row.findViewById(R.id.row_options_container);
        View divider         = row.findViewById(R.id.row_divider);

        label.setText(s.description());

        RowViews rv = new RowViews();
        rv.type = s.type();
        rv.label = label;
        rv.value = optionText;
        rv.header = header;
        rv.chevron = chevron;
        rv.optionsContainer = optionsBox;
        rv.divider = divider;
        SET_Rows[id] = rv;
        _tintRowChrome(rv);

        _updateSelectableDisplay(id);

        header.setOnClickListener(v -> _showOptionsPopup(id));

        return row;
    }

    /**
     * Opens the centred-modal options popup for setting id: a dimmed
     * overlay behind a titled card listing every named option. Tapping an
     * option applies it via _selectOption() and dismisses; tapping outside
     * the card dismisses with no change made.
     *
     * @param id  setting id whose options are listed
     */
    private void _showOptionsPopup(int id) {
        if (_optionsPopup != null && _optionsPopup.isShowing()) _optionsPopup.dismiss();

        SettingInfo s = _byId(id);
        String[] options = s.options();
        int val = s.currentValue();

        int width = _optPx(300);
        android.widget.LinearLayout list = _optList();
        View selectedRow = null;

        for (int i = 0; i < options.length; i++) {
            final int optionIndex = i;
            boolean selected = (i == val);

            android.widget.LinearLayout row = new android.widget.LinearLayout(Main);
            row.setOrientation(android.widget.LinearLayout.HORIZONTAL);
            row.setGravity(android.view.Gravity.CENTER_VERTICAL);
            row.setPadding(_optPx(16), _optPx(8), _optPx(16), _optPx(8));
            row.setBackground(_optRipple(new android.graphics.drawable.ColorDrawable(0)));

            TextView txt = _optLabel(options[i], selected, 13, android.view.Gravity.START | android.view.Gravity.CENTER_VERTICAL);
            android.widget.LinearLayout.LayoutParams tlp = new android.widget.LinearLayout.LayoutParams(
                    0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f);

            TextView dot = _optLabel("●", true, 11, android.view.Gravity.CENTER);
            dot.setVisibility(selected ? View.VISIBLE : View.INVISIBLE);
            dot.setWidth(_optPx(16));

            row.addView(txt, tlp);
            row.addView(dot);
            row.setOnClickListener(v -> _optPick(id, optionIndex));
            list.addView(row);
            if (selected) selectedRow = row;

            if (i < options.length - 1) _optAddDivider(list);
        }

        android.widget.LinearLayout card = _optList();
        card.setBackground(_optPanelBg(16));
        _optAddTitle(card, s.description());
        android.widget.ScrollView scroll = _optScroll(list, width, 380);
        card.addView(scroll);

        _optShowOverlay(card, width);
        _optScrollToRow(scroll, selectedRow);
    }

    // --------------------------------------------------------
    // Options popup — shared helpers
    // --------------------------------------------------------
    /** dp → px for the current display. */
    private int _optPx(float dp) {
        return (int) (dp * Main.getResources().getDisplayMetrics().density);
    }

    /** Accent colour used for the currently-selected option. */
    private int _optAccent()  { return ThemeManager.getColor(Main, R.color.skbar_settings_title); }
    /** Normal (unselected) option text colour. */
    private int _optText()    { return ThemeManager.getColor(Main, R.color.skbar_section_text_color); }
    /** Popup panel background colour. */
    private int _optPanel()   { return ThemeManager.getColor(Main, R.color.skbar_whole_settings); }
    /** Popup panel border colour. */
    private int _optStroke()  { return ThemeManager.getColor(Main, R.color.stroke_line_color); }
    /** Thin separator colour used between rows. */
    private int _optLine()    { return ThemeManager.getColor(Main, R.color.skbar_line_between); }
    /** Popup title / header text colour. */
    private int _optTitle()   { return ThemeManager.getColor(Main, R.color.skbar_upper_text); }

    /** Rounded, stroked card background in the current theme. */
    private android.graphics.drawable.GradientDrawable _optPanelBg(float radiusDp) {
        android.graphics.drawable.GradientDrawable d = new android.graphics.drawable.GradientDrawable();
        d.setColor(_optPanel());
        d.setCornerRadius(_optPx(radiusDp));
        d.setStroke(_optPx(1), _optStroke());
        return d;
    }

    /**
     * Wraps a drawable in a ripple so programmatic option rows still give
     * touch feedback. Falls back to the bare drawable below API 21.
     */
    private android.graphics.drawable.Drawable _optRipple(android.graphics.drawable.Drawable base) {
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
            return new android.graphics.drawable.RippleDrawable(
                    android.content.res.ColorStateList.valueOf((_optAccent() & 0x00FFFFFF) | 0x55000000),
                    base, null);
        }
        return base;
    }

    /** Builds a plain option TextView in the shared option style. */
    private TextView _optLabel(String text, boolean selected, float sizeSp, int gravity) {
        TextView t = new TextView(Main);
        t.setText(text);
        t.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, sizeSp);
        t.setMaxLines(1);
        t.setEllipsize(android.text.TextUtils.TruncateAt.END);
        t.setGravity(gravity);
        t.setTextColor(selected ? _optAccent() : _optText());
        t.setTypeface(null, selected ? Typeface.BOLD : Typeface.NORMAL);
        return t;
    }

    /** Empty vertical LinearLayout used as the option list / card container. */
    private android.widget.LinearLayout _optList() {
        android.widget.LinearLayout l = new android.widget.LinearLayout(Main);
        l.setOrientation(android.widget.LinearLayout.VERTICAL);
        return l;
    }

    /** Thin 1dp separator inserted between two option rows (not after the last one), tinted to the accent colour at low opacity. */
    private void _optAddDivider(android.widget.LinearLayout list) {
        View divider = new View(Main);
        divider.setBackgroundColor((_optAccent() & 0x00FFFFFF) | 0x40000000);
        android.widget.LinearLayout.LayoutParams lp = new android.widget.LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, _optPx(1));
        lp.setMargins(_optPx(16), 0, _optPx(16), 0);
        list.addView(divider, lp);
    }

    /**
     * Puts the option list in a ScrollView capped at maxDp so long lists
     * (14+ effects) stay on screen.
     *
     * @param widthPx  measuring width; 0 = unbounded
     */
    private android.widget.ScrollView _optScroll(View list, int widthPx, float maxDp) {
        int wSpec = widthPx > 0
                ? View.MeasureSpec.makeMeasureSpec(widthPx, View.MeasureSpec.AT_MOST)
                : View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED);
        list.measure(wSpec, View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));

        android.widget.ScrollView sv = new android.widget.ScrollView(Main);
        sv.setVerticalScrollBarEnabled(false);
        sv.addView(list, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        int h = Math.min(list.getMeasuredHeight(), _optPx(maxDp));
        sv.setLayoutParams(new android.widget.LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, h));
        return sv;
    }

    /**
     * Centres the selected row in the scroll viewport as soon as the popup
     * has laid out, so a long options list opens already scrolled to the
     * current value instead of at the top.
     *
     * @param scroll        the option list's ScrollView
     * @param selectedRow   the row for the current value, or null (nothing to do)
     */
    private void _optScrollToRow(android.widget.ScrollView scroll, View selectedRow) {
        if (selectedRow == null) return;
        scroll.post(() -> {
            int target = selectedRow.getTop() - (scroll.getHeight() - selectedRow.getHeight()) / 2;
            scroll.scrollTo(0, Math.max(0, target));
        });
    }

    /**
     * Shows the card centred over a dimmed full-screen overlay; tapping the
     * dim area dismisses.
     *
     * @param card     card content view (already backgrounded)
     * @param widthPx  card width
     */
    private void _optShowOverlay(View card, int widthPx) {
        android.widget.FrameLayout root = new android.widget.FrameLayout(Main);
        root.setBackgroundColor(0xAA000000);

        android.widget.FrameLayout.LayoutParams lp = new android.widget.FrameLayout.LayoutParams(
                widthPx, ViewGroup.LayoutParams.WRAP_CONTENT, android.view.Gravity.CENTER);
        root.addView(card, lp);

        card.setClickable(true);                 // swallow taps so the card never self-dismisses
        root.setOnClickListener(v -> { if (_optionsPopup != null) _optionsPopup.dismiss(); });

        _optionsPopup = new android.widget.PopupWindow(root,
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT, true);
        _optionsPopup.setOutsideTouchable(true);
        _optionsPopup.setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(0));
        _optionsPopup.showAtLocation(Main.getWindow().getDecorView(),
                android.view.Gravity.NO_GRAVITY, 0, 0);

        root.setAlpha(0f);
        root.animate().alpha(1f).setDuration(160).start();
        card.setTranslationY(_optPx(14));
        card.animate().translationY(0f).setDuration(200)
                .setInterpolator(new DecelerateInterpolator()).start();
    }

    /** Applies the tapped option and closes the popup. */
    private void _optPick(int id, int optionIndex) {
        _selectOption(id, optionIndex);
        if (_optionsPopup != null) _optionsPopup.dismiss();
    }

    /** Popup title strip (label + underline) at the top of the card. */
    private void _optAddTitle(android.widget.LinearLayout box, String text) {
        TextView title = new TextView(Main);
        title.setText(text);
        title.setAllCaps(true);
        title.setLetterSpacing(0.09f);
        title.setTextSize(android.util.TypedValue.COMPLEX_UNIT_SP, 12);
        title.setTypeface(null, Typeface.BOLD_ITALIC);
        title.setTextColor(_optTitle());
        title.setPadding(_optPx(16), _optPx(12), _optPx(16), _optPx(10));
        box.addView(title);

        View line = new View(Main);
        line.setBackgroundColor(_optLine());
        box.addView(line, new android.widget.LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, _optPx(1)));
    }

    /** Applies a newly tapped option and sends the change. */
    private void _selectOption(int id, int optionIndex) {
        SettingInfo s = _byId(id);
        s.setCurrentValue(optionIndex);
        _updateSelectableDisplay(id);

        String opt = s.options()[optionIndex];
        Main._Toast(s.description() + " > " + opt);
        Main._Console(false, "►►",
                "SETTING {{#C}" + s.description() + "{##}} > {{#Y}" + opt + "{##}}");

        DATAs.sendSetting(id, SET_Info, SET_TOTAL);
    }

    /** Refreshes a selectable row's header text and highlights the selected option. */
    private void _updateSelectableDisplay(int id) {
        SettingInfo s = _byId(id);
        RowViews rv = SET_Rows[id];
        if (rv == null) return;

        int val = s.currentValue();

        // Chip rows: optionsContainer is empty (chips live in a sibling scroll view)
        boolean isChipRow = (rv.optionsContainer != null
                && rv.optionsContainer.getChildCount() == 0
                && rv.chevron != null
                && rv.chevron.getVisibility() == View.GONE);

        if (isChipRow) {
            // Highlight selected chip, dim others
            int selBg   = ThemeManager.getColor(Main, R.color.tab_active_tint);
            int selText = ThemeManager.getColor(Main, R.color.skbar_settings_title);
            int defText = ThemeManager.getColor(Main, R.color.skbar_section_text_color);
            int stroke  = ThemeManager.getColor(Main, R.color.skbar_track_color);
            float dp    = Main.getResources().getDisplayMetrics().density;
            for (int i = 0; i < rv.optionViews.length; i++) {
                android.graphics.drawable.GradientDrawable d =
                        (android.graphics.drawable.GradientDrawable) rv.optionViews[i].getBackground();
                d.setColor(i == val
                        ? selBg
                        : android.graphics.Color.TRANSPARENT);
                d.setStroke((int)(1 * dp), stroke);
                rv.optionViews[i].setTextColor(i == val ? selText : defText);
            }
        } else {
            // Dropdown row: just refresh the header's value text. The popup
            // itself is rebuilt fresh (with correct highlighting) every time
            // it's opened, so there's no persistent list to re-highlight here.
            rv.value.setText(s.options()[val]);
        }
    }

    // --------------------------------------------------------
    // Background image filmstrip
    // --------------------------------------------------------
    /**
     * Style the footer BG button and wire it to the background selector.
     *
     * Tap opens BackgroundPopup - a vertical, scrolling list where the file
     * name sits beside its thumbnail. Long-press still jumps straight to the
     * folder picker, same as the old inline label did.
     *
     * The button doubles as the status label: MainActivity writes the active
     * GIF name onto it through SET_BG_Text, which now points here.
     */
    private void _setupBackgroundButton() {
        if (SET_BG_Text == null) return;

        SET_BG_Text.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));

        SET_BG_Text.setOnLongClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            Main.startActivityForResult(intent, 100);
            return true;
        });

        SET_BG_Text.setOnClickListener(v -> {
            Main._ShakeButton(SET_BG_Text, 300);
            String current = Main.getSharedPreferences("FuZz_Background", android.content.Context.MODE_PRIVATE)
                    .getString("BG_FILE", null);
            new BackgroundPopup(Main).show(SET_BG_Text, current);
        });
    }

    // --------------------------------------------------------
    // TestMode segmented chip row
    // --------------------------------------------------------
    /**
     * Style the footer TEST button and wire it to the tabbed test-mode popup.
     *
     * The old inline chip row needed two taps to reach an option - one for the
     * chip, one in the popup it opened. One tabbed surface replaces both: the
     * tab strip carries what the chips used to, and switching tabs only swaps
     * the option list.
     */
    private void _setupTestModeButton() {
        if (_testModeLabel == null) return;

        _testModeLabel.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
        _testModeLabel.setOnClickListener(v -> {
            Main._ShakeButton(_testModeLabel, 300);
            _showTestModePopup(_testModeLabel);
        });
    }

    /**
     * Open the tabbed test-mode picker.
     *
     * Tabs mirror SET_TestMode; the option lists and their command values are
     * unchanged from the old per-chip popups (firmware @-protocol):
     *   TV       - ON (@01) / OFF (@02)
     *   MOTION   - COM (@04) / BED (@05)
     *   UDPRAW   - ON (@03) / OFF (@00, stream stops and test mode cancels)
     *   DIFFUSER - one-shot @Dvv: OFF (00), modes (01-04), RANDOM COLOR (FF)
     *   LUX      - forced level @Lvv (01-04), held ~120 s on the Arduino
     *
     * @param anchor View used as the popup's window token.
     */
    private void _showTestModePopup(View anchor) {
        // NONE is no longer a tab - CANCEL is pinned in the popup footer, so
        // tab indices here are shifted by one against SET_TestMode, whose
        // entry 0 is still NONE. _sendTestModePick() is fed tab + 1 to keep
        // the highlight aligned with that array.
        TestModePopup.Tab[] tabs = new TestModePopup.Tab[]{
                new TestModePopup.Tab("TV",     new String[]{"ON", "OFF"},   new int[]{1, 2}),
                new TestModePopup.Tab("MOTION", new String[]{"COM", "BED"},  new int[]{4, 5}),
                new TestModePopup.Tab("UDPRAW", new String[]{"ON", "OFF"},   new int[]{3, 0}),
                new TestModePopup.Tab("DIFFUSER", new String[]{"OFF", "CONTINUOUS", "10 SEC",
                        "2H AFTER SLEEP", "4H AFTER SLEEP", "RANDOM COLOR"},
                        new int[]{0, 1, 2, 3, 4, 0xFF}),
                new TestModePopup.Tab("LUX",    new String[]{"LUX 1", "LUX 2", "LUX 3", "LUX 4"},
                        new int[]{1, 2, 3, 4})
        };

        new TestModePopup(Main).showTabs(anchor, tabs, _activeTestMode - 1, (tab, value, label) -> {
            final int slot = tab + 1;                       // index into SET_TestMode
            switch (tab) {
                case TestModePopup.TAB_CANCEL:
                    _sendTestModePick(0, "NONE", () -> DATAs.sendTestMode(0));
                    break;
                case 0: _sendTestModePick(slot, "TV " + label,     () -> DATAs.sendTestMode(value)); break;
                case 1: _sendTestModePick(slot, "MOTION " + label, () -> DATAs.sendTestMode(value)); break;
                // UDPRAW OFF drops the whole test latch, so it reports as NONE
                case 2: _sendTestModePick(value == 0 ? 0 : slot, "UDPRAW " + label,
                                () -> DATAs.sendTestMode(value)); break;
                case 3: _sendTestModePick(slot, "DIFFUSER " + label, () -> DATAs.sendTestDiffuser(value)); break;
                case 4: _sendTestModePick(slot, label, () -> DATAs.sendTestLux(value)); break;
            }
        });
    }

        /**
     * Shared tail of every test-mode pick: highlight chip, toast, send.
     *
     * @param activeIdx Chip index to highlight (0 resets to NONE).
     * @param toast     Text shown after "TEST MODE > ".
     * @param sender    The UDP send call for this pick.
     */
    private void _sendTestModePick(int activeIdx, String toast, Runnable sender) {
        _activeTestMode = activeIdx;
        Main._Toast("TEST MODE > " + toast);
        sender.run();
        _refreshTestModeChipColors();
    }

    /** Re-tints every TestMode chip to the active theme, using _activeTestMode to know which is selected. */
    private void _refreshTestModeChipColors() {
        if (SET_TESTMODE_ChipRow == null) return;
        int selBg   = ThemeManager.getColor(Main, R.color.tab_active_tint);
        int selText = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int defText = ThemeManager.getColor(Main, R.color.skbar_section_text_color);
        int defBg   = android.graphics.Color.TRANSPARENT;
        int stroke  = ThemeManager.getColor(Main, R.color.skbar_track_color);
        float dp    = Main.getResources().getDisplayMetrics().density;

        for (int i = 0; i < SET_TESTMODE_ChipRow.getChildCount(); i++) {
            View child = SET_TESTMODE_ChipRow.getChildAt(i);
            if (!(child instanceof TextView)) continue;
            boolean sel = (i == _activeTestMode);
            android.graphics.drawable.Drawable bgDraw = child.getBackground();
            if (bgDraw instanceof android.graphics.drawable.GradientDrawable) {
                android.graphics.drawable.GradientDrawable g =
                        (android.graphics.drawable.GradientDrawable) bgDraw.mutate();
                g.setColor(sel ? selBg : defBg);
                g.setStroke((int)(1 * dp), stroke);
            }
            ((TextView) child).setTextColor(sel ? selText : defText);
        }
    }

    // --------------------------------------------------------
    // Settings buttons
    // --------------------------------------------------------
    /**
     * Attach listeners to the Default and Enable/Disable buttons.
     */
    private void _setupButtons() {

        // ── Default (reset all settings) - press & hold to confirm ──
        SET_BTN_Default.setTextColor(ThemeManager.getColor(Main, R.color.text_default));
        SET_BTN_Default.setOnClickListener(v ->
                Main._Toast("HOLD to reset all settings to default"));
        SET_BTN_Default.setOnLongClickListener(v -> {
            Main._ShakeButton(SET_BTN_Default, 1500);
            Main._Toast("[RESET SETTINGS]");
            Main._Console(false, "►►", "{#R}RESET SETTINGS{##}");

            for (int i = 0; i < SET_Info.size(); i++) {
                SettingInfo si = SET_Info.get(i);
                Main._Console(false, "►►",
                        "set default [{#C}" + si.description()
                        + "{##}] to [{#Y}" + si.defaultValue() + "{##}]");
            }

            // Send all defaults, then request fresh values from Arduino
            DATAs.sendSetting(SET_TOTAL, SET_Info, SET_TOTAL);
            DATAs.sendSetting(-1,        SET_Info, SET_TOTAL);
            return true;
        });

        // ── Enable / Disable LED strip - press & hold to confirm ──
        SET_BTN_EnableDisable.setOnClickListener(v ->
                Main._Toast("Hold for " + (STS.isEnabled() ? "Disable" : "Enable") + " System"));
        SET_BTN_EnableDisable.setOnLongClickListener(v -> {
            Main._ShakeButton(SET_BTN_EnableDisable, 500);

            // 'X' is a toggle, so the resulting state is the opposite of the
            // current one. Go through STS.applyEnable() (the same entry point
            // the real firmware status packet uses) so currentEnableDisable and
            // the button label update together immediately, rather than only
            // touching the label here and leaving STS stale until the next
            // status round-trip - that mismatch was why every press reported
            // the same "ENABLED" toast regardless of direction.
            boolean wasEnabled = STS.isEnabled();
            DATAs.sendEnableDisable(STS.getEnableDisable());
            STS.applyEnable(!wasEnabled);

            Main._Toast("LED's  " + (wasEnabled ? "DISABLED" : "ENABLED"));
            Main._Console(false, "►►",
                    "LED's " + (wasEnabled
                        ? "{#R}DISABLED{##}" : "{#G}ENABLED{##}"));
            return true;
        });

        // ── Theme picker ───────────────────────────────────────
        SET_BTN_Theme.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
        SET_BTN_Theme.setOnClickListener(v -> {
            Main._ShakeButton(SET_BTN_Theme, 300);
            new ThemePopup(Main).show(SET_BTN_Theme, this::applyTheme);
        });
    }

    // ========================================================
    //  Public API  (used by DATAr and LED)
    // ========================================================

    /**
     * Apply a setting value received from the Arduino to the UI.
     * Validates range, updates SettingInfo, and refreshes whichever
     * widget that setting's row actually has.
     * Called by DATAr._recvSettings().
     *
     * @param id     Arduino EEPROM setting id 0..SET_TOTAL-1 (NOT a list position)
     * @param value  Raw value received (already hex-decoded by DATAr)
     */
    public void applyReceivedSetting(int id, int value) {
        SettingInfo si = _byId(id);
        if (si == null) return; // unknown id - nothing registered for it

        // Clamp to valid range if numeric (not options-mode)
        if (si.maxValue() > 1) {
            if (value < si.minValue()) value = si.minValue();
            else if (value > si.maxValue()) value = si.maxValue();
        }

        si.setCurrentValue(value);

        RowViews rv = SET_Rows[id];
        if (rv == null) return;

        switch (si.type()) {
            case SettingInfo.TYPE_SWITCH:
                rv.suppressSwitchEvent = true;
                rv.checkBox.setChecked(value == 1);
                rv.suppressSwitchEvent = false;
                break;
            case SettingInfo.TYPE_SELECTABLE:
                _updateSelectableDisplay(id);
                break;
            default: // TYPE_TIME, TYPE_DELAY, TYPE_INCREMENTAL, TYPE_BRIGHTNESS
                rv.seekBar.setProgress(value);
                _updateBarValueLabel(id, value);
                break;
        }
    }

    /**
     * @param id  Arduino EEPROM setting id (NOT a list position)
     * @return SettingInfo registered for that id, or null if none (read by DATAr for clamping)
     */
    public SettingInfo getSettingInfo(int id) {
        return _byId(id);
    }

    /**
     * @param id  Arduino EEPROM setting id to check
     * @return true if a setting is registered for that id. Use this (not
     *         getSettingCount()) to validate an incoming id, since
     *         SET_Info.size() no longer has any fixed relationship to id.
     */
    public boolean isValidId(int id) {
        return id >= 0 && id < SET_TOTAL && SET_InfoById[id] != null;
    }

    /**
     * @return Total number of settings in SET_Info
     */
    public int getSettingCount() {
        return SET_Info.size();
    }

    /**
     * Read the current value of the HB_Dual setting (id 31).
     * 0 = dual colour off, non-zero = on.
     * Called by LED.updateColor() to decide the HB LED split rendering.
     *
     * @return currentValue of the setting whose id is SET_HB_Dual
     */
    public int getHBDualValue() {
        return _byId(SET_HB_Dual).currentValue();
    }

    /** Currently configured idle HB effect id (0=static, 1-14=animated). */
    public int getHBEffectValue() {
        return _byId(SET_HB_Effect).currentValue();
    }

    /** Idle HB effect frame period, in ms. */
    public int getHBEffectSpeedValue() {
        return _byId(SET_HB_EffectSpeed).currentValue();
    }

    /** HB effect id (0-3) played while the TV is turning on. */
    public int getTvOnHBEffectValue() {
        return _byId(SET_TvOnHBEffect).currentValue();
    }

    /** Brightness step used by the TV-on HB helpers and several idle HB effects. */
    public int getTvOnBrClIncValue() {
        return _byId(SET_TvOnBrClInc).currentValue();
    }

    /** Tick delay (ms) driving the TV-on HB helpers. */
    public int getTvOnBrClDelayValue() {
        return _byId(SET_TvOnBrClDelay).currentValue();
    }

    /**
     * Update the Enable/Disable button label and colour.
     * Called by STS after a status packet changes STS_EnableDisable.
     *
     * @param enabled  true = currently enabled, false = disabled
     */
    public void updateEnableDisableButton(boolean enabled) {
        SET_BTN_EnableDisable.setText(enabled ? "DISABLE" : "ENABLE");
        SET_BTN_EnableDisable.setTextColor(
                enabled ? ContextCompat.getColor(Main, R.color.action_disable_warn)
                        : ThemeManager.getColor(Main, R.color.text_default));
    }

    // ========================================================
    //  Theming
    // ========================================================

    /**
     * Persists + activates a new accent theme. The handful of accent-tinted
     * views (theme button, category titles/stripes, selected tab, selected
     * chips, row chevrons/steppers) crossfade from the old accent to the
     * new one over ~450ms; _repaintFullTheme() then does one exact final
     * repaint of every themed view (no rebuild → no risk of losing any live
     * setting value or scroll/expand state). The crossfade is purely a
     * preview - correctness always comes from the final repaint.
     * Called from the THEME button's ThemePopup callback.
     *
     * @param theme  The newly-picked theme (CRIMSON … VIOLET).
     */
    public void applyTheme(ThemeManager.Theme theme) {
        int oldAccent   = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int oldActiveBg = ThemeManager.getColor(Main, R.color.tab_active_tint);

        ThemeManager.setCurrentTheme(Main, theme);

        int newAccent   = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int newActiveBg = ThemeManager.getColor(Main, R.color.tab_active_tint);

        if (_themeAnim != null) _themeAnim.cancel();

        ValueAnimator anim = ValueAnimator.ofFloat(0f, 1f);
        anim.setDuration(450);
        anim.setInterpolator(new DecelerateInterpolator(1.2f));
        final ArgbEvaluator argb = new ArgbEvaluator();

        anim.addUpdateListener(a -> {
            float f = (float) a.getAnimatedValue();
            int blendAccent   = (int) argb.evaluate(f, oldAccent,   newAccent);
            int blendActiveBg = (int) argb.evaluate(f, oldActiveBg, newActiveBg);
            _paintAccentBlend(blendAccent, blendActiveBg);
        });

        anim.addListener(new AnimatorListenerAdapter() {
            boolean cancelled = false;

            @Override public void onAnimationCancel(Animator animation) {
                cancelled = true;
            }

            @Override public void onAnimationEnd(Animator animation) {
                if (cancelled) return; // a newer theme pick already took over
                _repaintFullTheme();
                Main._Toast("THEME > " + theme.label());
            }
        });

        _themeAnim = anim;
        anim.start();
    }

    /** Exact, non-animated repaint of every themed view - the single source of correctness. */
    private void _repaintFullTheme() {
        // Buttons
        SET_BTN_Default.setTextColor(ThemeManager.getColor(Main, R.color.text_default));
        SET_BTN_Theme.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
        updateEnableDisableButton(STS.isEnabled());

        // Category headers (title text + gradient stripe)
        for (TextView t : _catTitleViews) {
            t.setTextColor(ThemeManager.getColor(Main, R.color.skbar_settings_title));
        }
        for (View stripe : _catStripeViews) _tintCatStripe(stripe);
        for (View line   : _catLineViews) _tintGlowDivider(line);
        _tintStaticChrome();

        // Every row: chrome (label/value/steppers/switch track) + selectable highlighting
        for (int id = 0; id < SET_TOTAL; id++) {
            RowViews rv = SET_Rows[id];
            if (rv == null) continue;
            _tintRowChrome(rv);
            if (rv.optionViews != null) _updateSelectableDisplay(id);
        }

        // Category tabs, TestMode chips, BG label pill
        _refreshCategoryTabColors();
        _refreshTestModeChipColors();
        _setupBackgroundButton();     // idempotent re-style + listener re-bind, no state to lose

        // Cross-module chrome that lives outside the Settings panel
        STS.applyThemeColors();
        Main.applyThemeColors();
    }

    /**
     * Paints only the accent-tinted subset of settings chrome with the
     * given (possibly blended) colors - the crossfade preview shown while
     * a theme switch animates. Never the source of truth for correctness;
     * see _repaintFullTheme().
     *
     * @param accent    blended skbar_settings_title color for this frame
     * @param activeBg  blended tab_active_tint color for this frame
     */
    private void _paintAccentBlend(int accent, int activeBg) {
        if (SET_BTN_Theme != null) SET_BTN_Theme.setTextColor(accent);

        for (TextView t : _catTitleViews) t.setTextColor(accent);
        for (View stripe : _catStripeViews) _tintCatStripe(stripe, accent);

        _blendCategoryTabAccent(accent, activeBg);
        _blendTestModeChipAccent(accent, activeBg);

        for (int id = 0; id < SET_TOTAL; id++) {
            RowViews rv = SET_Rows[id];
            if (rv == null) continue;
            if (rv.chevron   != null) rv.chevron.setTextColor(accent);
            if (rv.stepMinus instanceof TextView) ((TextView) rv.stepMinus).setTextColor(accent);
            if (rv.stepPlus  instanceof TextView) ((TextView) rv.stepPlus).setTextColor(accent);
            if (rv.optionViews != null) _blendSelectableAccent(id, rv, accent, activeBg);
        }
    }

    /** Crossfade preview: re-tints just the selected category tab's fill/border/text. */
    private void _blendCategoryTabAccent(int accent, int activeBg) {
        android.widget.LinearLayout tabRow = Main.findViewById(R.id.settings_tab_row);
        if (tabRow == null) return;
        float dp = Main.getResources().getDisplayMetrics().density;

        int tabIndex = 0;
        for (int i = 0; i < tabRow.getChildCount(); i++) {
            View child = tabRow.getChildAt(i);
            if (!(child instanceof TextView)) continue; // skip start/end Space
            if (tabIndex == _activeTab) {
                android.graphics.drawable.GradientDrawable bg =
                        new android.graphics.drawable.GradientDrawable();
                bg.setCornerRadius(4 * dp);
                bg.setColor(activeBg);
                bg.setStroke((int) (1 * dp), accent);
                child.setBackground(bg);
                ((TextView) child).setTextColor(accent);
            }
            tabIndex++;
        }
    }

    /** Crossfade preview: re-tints just the selected TestMode chip's fill/text. */
    private void _blendTestModeChipAccent(int accent, int activeBg) {
        if (SET_TESTMODE_ChipRow == null) return;
        for (int i = 0; i < SET_TESTMODE_ChipRow.getChildCount(); i++) {
            if (i != _activeTestMode) continue;
            View child = SET_TESTMODE_ChipRow.getChildAt(i);
            if (!(child instanceof TextView)) continue;

            android.graphics.drawable.Drawable bgDraw = child.getBackground();
            if (bgDraw instanceof android.graphics.drawable.GradientDrawable) {
                ((android.graphics.drawable.GradientDrawable) bgDraw.mutate()).setColor(activeBg);
            }
            ((TextView) child).setTextColor(accent);
        }
    }

    /** Crossfade preview: re-tints just the currently-selected option's accent highlight. */
    private void _blendSelectableAccent(int id, RowViews rv, int accent, int activeBg) {
        SettingInfo s = _byId(id);
        int val = s.currentValue();
        if (val < 0 || val >= rv.optionViews.length) return;

        boolean isChipRow = (rv.optionsContainer != null
                && rv.optionsContainer.getChildCount() == 0
                && rv.chevron != null
                && rv.chevron.getVisibility() == View.GONE);

        if (isChipRow) {
            android.graphics.drawable.Drawable bgDraw = rv.optionViews[val].getBackground();
            if (bgDraw instanceof android.graphics.drawable.GradientDrawable) {
                ((android.graphics.drawable.GradientDrawable) bgDraw.mutate()).setColor(activeBg);
            }
            rv.optionViews[val].setTextColor(accent);
        } else {
            rv.optionViews[val].setTextColor(accent);
        }
    }
}
