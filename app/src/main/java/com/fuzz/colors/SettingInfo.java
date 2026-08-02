package com.fuzz.colors;

/**
 * Metadata + live value for a single Arduino setting.
 *
 * id - the Arduino/EEPROM identity of this setting (0..SET_TOTAL-1).
 *      THIS is what gets sent over UDP and what incoming 'SiiVV' packets
 *      are keyed by - never the position of this object inside SET_Info.
 *      Set once, explicitly, at construction, and never changes.
 *      Reordering the .add() calls in SettingsManager._populateSettings()
 *      only changes where a row is drawn on screen (top-to-bottom inside
 *      its category) - it can NEVER change which id is wired to which
 *      Arduino EEPROM slot, because that id is carried right here on the
 *      object, not inferred from list position.
 *
 * Every setting also carries two extra pieces of information, used purely
 * by the Android UI layer (never sent to the Arduino - only id +
 * currentValue ever go over UDP):
 *
 *   category - which settings section (LinearLayout) it belongs to.
 *              One of the CAT_* constants.
 *   type     - which widget should render it.
 *              One of the TYPE_* constants.
 *
 * Usage (see SettingsManager._populateSettings()):
 *
 *   SET_Info.add(new SettingInfo(31, CAT_HB, TYPE_SWITCH, 0, 0, 1,
 *           "DUAL COLOR", "off", "on"));
 *           // ^^ id=31 - stays 31 no matter where this .add() line moves to.
 */
public class SettingInfo {

    // --------------------------------------------------------
    // Categories - one per settings section (LinearLayout)
    // --------------------------------------------------------
    public static final int CAT_TV        = 0;
    public static final int CAT_MOTION    = 1;
    public static final int CAT_HB        = 2;
    public static final int CAT_AMBILIGHT = 3;
    public static final int CAT_DIFFUSER  = 4;
    public static final int CAT_OTHER     = 5;
    /** Number of CAT_* constants above - sizes SettingsManager's CAT_ContainerById[]. */
    public static final int CAT_TOTAL     = 6;

    // --------------------------------------------------------
    // Types - one per UI widget style
    // --------------------------------------------------------
    /** Named options, shown as an inline accordion - tap to expand/collapse the option list. */
    public static final int TYPE_SELECTABLE  = 1;
    /** Numeric bar, value labeled in seconds. */
    public static final int TYPE_TIME_S = 2;
    /** Numeric bar, plain unitless step count. */
    public static final int TYPE_INCREMENTAL = 3;
    /** Numeric bar, value labeled in milliseconds. */
    public static final int TYPE_TIME_MS = 4;
    /** Numeric bar, plain brightness value. */
    public static final int TYPE_BRIGHTNESS  = 5;
    /** Boolean, shown as an ON/OFF switch. */
    public static final int TYPE_SWITCH      = 6;
    /** Numeric bar, value labeled in minutes. */
    public static final int TYPE_TIME_M       = 7;

    int id;
    int category;
    int type;
    int defValue;
    int minValue;
    int maxValue;
    int currentValue;
    String description;
    String[] options;

    /**
     * @param id           Arduino/EEPROM setting id (0..SET_TOTAL-1). This is
     *                     the value sent in 'SiiVV' UDP packets and the value
     *                     incoming packets are matched against - it has
     *                     nothing to do with where this SettingInfo sits in
     *                     SettingsManager's SET_Info list. Two settings must
     *                     never share an id.
     * @param category     One of the CAT_* constants.
     * @param type         One of the TYPE_* constants.
     * @param defaultValue Default value (also the EEPROM-reset value).
     * @param minValue     Minimum allowed value.
     * @param maxValue     Maximum allowed value. 0 means "options-driven":
     *                     the real max is options.length-1.
     * @param description  Label shown in the UI.
     * @param options      Named options (TYPE_SELECTABLE) or "off"/"on"
     *                     (TYPE_SWITCH). Empty for plain numeric settings.
     */
    public SettingInfo(int id, int category, int type, int defaultValue, int minValue, int maxValue,
                        String description, String... options) {
        this.id = id;
        this.category = category;
        this.type = type;
        this.defValue = defaultValue;
        this.minValue = minValue;
        this.maxValue = maxValue;
        this.currentValue = defaultValue;
        this.description = description;
        this.options = options;
    }

    // Getters
    public int id() { return id; }
    public int category() { return category; }
    public int type() { return type; }
    public int defaultValue() { return defValue; }
    public int minValue() { return minValue; }
    public int maxValue() { return maxValue; }
    public String description() { return description; }
    public String[] options() { return options; }
    public int currentValue() { return currentValue; }

    /**
     * Update the live value, clamped to the setting's valid range.
     *
     * Note: when maxValue == 0 (options-driven settings), the real upper
     * bound is options.length-1, not maxValue itself - otherwise every
     * option past index 0 would be silently rejected.
     */
    public void setCurrentValue(int value) {
        int effectiveMax = (maxValue == 0 && options != null && options.length > 0)
                ? options.length - 1
                : maxValue;
        if (value >= minValue && value <= effectiveMax) {
            this.currentValue = value;
        }
    }
}
