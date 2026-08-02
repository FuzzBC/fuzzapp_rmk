package com.fuzz.colors;

/*
 * ============================================================
 *  LEDManager.java  (alias: LED)
 * ============================================================
 *  Responsibility:
 *      All LED-related state and UI logic:
 *        • LED colour storage (LED_Color[][3])
 *        • LED selection (LED_Selected[])
 *        • Dual-colour list (load / save / create buttons)
 *        • Brightness SeekBar binding
 *        • Per-LED button click / long-click listeners
 *        • Animated colour transitions on each button
 *        • Random dual-colour generation (shake support)
 *
 *  How to use:
 *      1.  Instantiate in Main.onCreate() AFTER setContentView():
 *              LED = new LEDManager(this, UDPs, STS, SET);
 *
 *      2.  Call from Main.onCreate() – sets up every view:
 *              LED.init();
 *
 *      3.  Methods called by UDPr (UDPReceive) when packets arrive:
 *              LED.updateColor(ledIndex, r, g, b)
 *              LED.setBrightnessProgress(value)
 *              LED.setMaxBrightness(value)
 *              LED.addDualColor(r1, g1, b1, r2, g2, b2)
 *
 *      4.  Methods called by Main (onSensorChanged) for shake:
 *              LED.getRandDualColorHex()
 *              LED.reloadRandDualColor()
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      UDPs  = UDPSend
 *      UDPr  = UDPReceive
 *      LED   = LEDManager   (this class)
 *      SET   = SettingsManager
 *      STS   = StatusManager
 * ============================================================
 */

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.InsetDrawable;
import android.os.Build;
import android.text.InputFilter;
import android.text.InputType;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.flask.colorpicker.ColorPickerView;
import com.flask.colorpicker.builder.ColorPickerDialogBuilder;
import com.xw.repo.BubbleSeekBar;

import java.util.ArrayList;
import java.util.Random;

import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;

import androidx.annotation.RequiresApi;

public class LEDManager {

    // --------------------------------------------------------
    // Constants – must match Arduino firmware
    // --------------------------------------------------------
    /** Total number of logical LEDs (includes 1 fake HB entry) */
    public final int LED_TOTAL         = 61;

    /**
     * Index of the HeartBeat (HB) virtual LED.
     * = LED_TOTAL - 1
     */
    public final int LED_HB            = LED_TOTAL - 1;

    /** Number of LEDs dedicated to Ambilight (first N LEDs) */
    public final int LED_AMBILIGHT     = 30;

    /** Maximum brightness value the SeekBar can reach */
    public final int LED_MAX_BRIGHTNESS = 120;

    /**
     * LED indices used as the Left / Right context for dual-colour mode.
     * [0] = LEFT context LED index
     * [1] = RIGHT context LED index
     */
    public final int[] LED_DUALCOLOR_CONTEXT = {41, 40};

    /**
     * Hard-split angle (degrees) for the circular swatches in
     * LED_BTN_DualColor[]. 0 = left/right, 90 = top/bottom.
     * Needs DualColorDrawable to render - XML <shape><gradient> and
     * GradientDrawable.Orientation are both locked to multiples of 45.
     */
    public final float DUAL_BTN_SPLIT_ANGLE_DEG = 15f;

    /** Diameter (dp) of each circular swatch in LED_BTN_DualColor[]. */
    public final int DUAL_BTN_SIZE_DP = 48;

    // --------------------------------------------------------
    // Channel index helpers
    // --------------------------------------------------------
    /** Index for Red channel in LED_Color[i][channel] */
    public final int _R = 0;
    /** Index for Green channel */
    public final int _G = 1;
    /** Index for Blue channel */
    public final int _B = 2;

    // --------------------------------------------------------
    // State arrays
    // --------------------------------------------------------
    /**
     * Stores the current R/G/B of every LED.
     * Access: LED_Color[ledIndex][_R / _G / _B]
     */
    public int[][] LED_Color    = new int[LED_TOTAL][3];

    /**
     * Selection state of every LED.
     * true = selected (will receive the next colour/brightness command).
     */
    public boolean[] LED_Selected = new boolean[LED_TOTAL];

    // --------------------------------------------------------
    // Selected-LED stroke effect (synchronised breathing pulse)
    // --------------------------------------------------------
    /** Duration (ms) for one half-cycle (dim→bright or bright→dim) of the pulse. */
    private static final long SELECT_STROKE_CYCLE_MS = 1200L;

    /** Stroke width (dp) - fixed, never animated. Same value used for the normal outline. */
    private static final int SELECT_STROKE_WIDTH_DP = 2;

    /** Min/max alpha (0-255) the stroke breathes between. */
    private static final int SELECT_STROKE_ALPHA_MIN = 60;
    private static final int SELECT_STROKE_ALPHA_MAX = 255;

    /** Indices of all currently-selected LEDs whose stroke follows the shared pulse. */
    private final java.util.Set<Integer> LED_SelectPulseSet = new java.util.HashSet<>();

    /**
     * Single shared animator driving every selected LED's stroke alpha in
     * phase. One instance (not one per-button) is what keeps buttons
     * selected at different times in sync - they all read the same
     * animated value on each frame instead of restarting their own clock.
     */
    private ValueAnimator LED_SelectPulseAnim;

    // --------------------------------------------------------
    // Dual-colour list
    // --------------------------------------------------------
    /**
     * Saved dual-colour pairs as hex strings: "RRGGBB.RRGGBB"
     * Loaded from / saved to SharedPreferences ("FuZz_DualColor").
     */
    public ArrayList<String> LED_DualColorList = new ArrayList<>();

    /**
     * Hex string for the next random dual colour (12 chars: LLLLLLRRRRRR).
     * Refreshed after each shake or random-button press.
     */
    public String LED_RandDualColor;

    // --------------------------------------------------------
    // Views
    // --------------------------------------------------------
    /** One button per LED (mapped by resource id "_btn_led_i") */
    public Button[] LED_BTN_Leds        = new Button[LED_TOTAL];

    /** Dual-colour selection buttons (dynamic, rebuilt on change) */
    public Button[] LED_BTN_DualColor   = new Button[LED_TOTAL];

    /** Deselects all LEDs */
    public Button LED_BTN_DeselectALL;

    /** Sets all LEDs to black (off) */
    public Button LED_BTN_LedsOFF;

    /** Selects all LEDs */
    public Button LED_BTN_SelectALL;

    /** Opens the colour-picker dialog */
    public Button LED_BTN_ColorPicker;

    /** Sends the custom RGB values entered in the EditText fields */
    public Button LED_BTN_SendColor;

    /** Saves the current dual-colour pair */
    public Button LED_BTN_SaveDual;

    /**
     * Custom RGB input fields.
     * [_R]=0 red, [_G]=1 green, [_B]=2 blue (0-255)
     */
    public EditText[] LED_EDIT_CustomRGB = new EditText[3];

    /** RGB row container (_rgbS) - fixed anchor so the slider popup always opens in the same spot */
    private LinearLayout LED_ROW_RgbInputs;

    /** Brightness slider (BubbleSeekBar library) */
    public BubbleSeekBar LED_Brightness;

    /** − stepper button next to the brightness SeekBar */
    private TextView LED_BrightnessStepMinus;

    /** + stepper button next to the brightness SeekBar */
    private TextView LED_BrightnessStepPlus;

    /** Numeric value display between the steppers */
    private TextView LED_BrightnessValue;

    /** Text label showing current brightness value */
    public android.widget.TextView LED_BrightnessText;

    /** Alignment-hint icons next to the COM/dual button columns (tinted to match theme) */
    private android.widget.ImageView LED_Icon_Bed;
    private android.widget.ImageView LED_Icon_Lamp;

    /** Heartbeat pattern view for HB LED (replaces button _btn_led_60) */
    public HeartbeatPatternView LED_HeartbeatPattern;

    /** Locally replays the firmware's HB effect animation onto LED_HeartbeatPattern while the TV is on. */
    public HBEffectSimulator HBFx;

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /**
     * Main – used for:
     *   Main._Toast()        user-visible short messages
     *   Main._Console()      debug console log
     *   Main._ShakeButton()  shake animation helper
     *   Main.getResources()  resource look-up
     *   Main.getPackageName() dynamic id resolution
     */
    private final MainActivity Main;

    /**
     * UDPs – all outgoing UDP commands triggered by user interaction.
     * Alias: UDPs (UDPSend)
     */
    private final UDPSend UDPs;

    /**
     * STS – checked to guard actions when Ambilight is active.
     * Alias: STS (StatusManager)
     */
    private final StatusManager STS;

    /**
     * SET – read for HB dual-colour setting (SET_HB_Dual).
     * Alias: SET (SettingsManager)
     */
    private final SettingsManager SET;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (alias: Main).
     * @param udps  UDPSend instance      (alias: UDPs).
     * @param sts   StatusManager instance (alias: STS).
     * @param set   SettingsManager instance (alias: SET).
     */
    public LEDManager(MainActivity main, UDPSend udps,
                      StatusManager sts, SettingsManager set) {
        this.Main = main;
        this.UDPs = udps;
        this.STS  = sts;
        this.SET  = set;
    }

    // ========================================================
    //  Initialisation  (call from Main.onCreate)
    // ========================================================

    /**
     * Bind all LED-related views and attach listeners.
     * Must be called once, after setContentView() in Main.
     */
    public void init() {
        _bindViews();
        HBFx = new HBEffectSimulator(this, STS, SET, LED_HeartbeatPattern);
        _setupRgbInputs();
        _setupButtonListeners();
        _setupLedButtons();
        _setupBrightnessBar();
        applyThemeColors();

        // Load & render the saved dual-colour list
        _dualColorLoad();
        _dualColorCreate();
        _deselectAll();
    }

    // --------------------------------------------------------
    // View binding
    // --------------------------------------------------------
    private void _bindViews() {
        LED_BTN_DeselectALL  = Main.findViewById(R.id._btn_deselectall);
        LED_BTN_SelectALL    = Main.findViewById(R.id._btn_selectall);
        LED_BTN_LedsOFF      = Main.findViewById(R.id._btn_ledsoff);
        LED_BTN_ColorPicker  = Main.findViewById(R.id._btn_colorpicker);
        LED_BTN_SendColor    = Main.findViewById(R.id._btn_sendcolor);
        LED_BTN_SaveDual     = Main.findViewById(R.id._btn_savedual);
        LED_BrightnessStepMinus  = Main.findViewById(R.id._brightness_step_minus);
        LED_BrightnessStepPlus   = Main.findViewById(R.id._brightness_step_plus);
        LED_BrightnessValue      = Main.findViewById(R.id._brightness_value);
        LED_BrightnessText   = Main.findViewById(R.id._brightness_text);
        LED_Brightness       = Main.findViewById(R.id._brightness);
        LED_Icon_Bed         = Main.findViewById(R.id._LedAlign_BED_img);
        LED_Icon_Lamp        = Main.findViewById(R.id._LedAlign_lamp);
        LED_HeartbeatPattern = Main.findViewById(R.id._btn_led_60);

        LED_EDIT_CustomRGB[_R] = Main.findViewById(R.id._rgbprint_0);
        LED_EDIT_CustomRGB[_G] = Main.findViewById(R.id._rgbprint_1);
        LED_EDIT_CustomRGB[_B] = Main.findViewById(R.id._rgbprint_2);
    }

    // --------------------------------------------------------
    // RGB EditText setup (0-255 filter + Enter key)
    // --------------------------------------------------------
    /**
     * Initialise each RGB input field:
     *   • numeric-only input
     *   • InputFilter clamped to 0-255
     *   • Enter key clamps and closes keyboard
     */
    private void _setupRgbInputs() {
        LED_ROW_RgbInputs = Main.findViewById(
                Main.getResources().getIdentifier("_rgbS", "id", Main.getPackageName()));

        for (int i = 0; i < 3; i++) {
            final int z = i;
            int id = Main.getResources().getIdentifier(
                    "_rgbprint_" + i, "id", Main.getPackageName());
            LED_EDIT_CustomRGB[i] = Main.findViewById(id);
            LED_EDIT_CustomRGB[i].setText("0");
            LED_EDIT_CustomRGB[i].setInputType(InputType.TYPE_CLASS_NUMBER);

            // Accept only values 0-255
            LED_EDIT_CustomRGB[i].setFilters(new InputFilter[]{
                (source, start, end, dest, dstart, dend) -> {
                    try {
                        int val = Integer.parseInt(
                                dest.toString() + source.toString());
                        if (val >= 0 && val <= 255) return null; // accept
                    } catch (NumberFormatException ignored) {}
                    return ""; // reject
                }
            });

            // Clamp on Enter key
            LED_EDIT_CustomRGB[i].setOnKeyListener((v, keyCode, event) -> {
                if (event.getAction() == android.view.KeyEvent.ACTION_DOWN
                        && keyCode == android.view.KeyEvent.KEYCODE_ENTER) {
                    String text = LED_EDIT_CustomRGB[z].getText().toString();
                    try {
                        int p = Math.max(0, Math.min(255,
                                Integer.parseInt(text)));
                        LED_EDIT_CustomRGB[z].setText(String.valueOf(p));
                    } catch (NumberFormatException e) {
                        LED_EDIT_CustomRGB[z].setText("0");
                    }
                    return true;
                }
                return false;
            });

            // No soft keyboard - value is only edited via the RgbChannelPopup slider.
            LED_EDIT_CustomRGB[i].setFocusable(false);
            LED_EDIT_CustomRGB[i].setFocusableInTouchMode(false);
            LED_EDIT_CustomRGB[i].setClickable(true);

            // Tap the R / G / B value -> glowing slider popup for that channel only.
            // Anchored to the whole row (fixed position, same spot for R/G/B) so it
            // doesn't jump around depending on which field was tapped. Live-updates
            // this field's text as the thumb moves; never sends UDP - the physical
            // Send button (_btn_sendcolor) is still what dispatches it.
            LED_EDIT_CustomRGB[i].setOnClickListener(v ->
                new RgbChannelPopup(Main).show(LED_ROW_RgbInputs, z, _currentPendingRgb(), value ->
                        LED_EDIT_CustomRGB[z].setText(String.valueOf(value)))
            );
        }
    }

    /** @return {r,g,b} currently sitting in the pending custom-colour fields (0 if unparsable). */
    private int[] _currentPendingRgb() {
        int[] out = new int[3];
        for (int i = 0; i < 3; i++) {
            try {
                out[i] = Math.max(0, Math.min(255,
                        Integer.parseInt(LED_EDIT_CustomRGB[i].getText().toString())));
            } catch (NumberFormatException e) {
                out[i] = 0;
            }
        }
        return out;
    }

    // --------------------------------------------------------
    // Top-level button listeners
    // --------------------------------------------------------
    private void _setupButtonListeners() {

        // ── Send custom RGB ──────────────────────────────────
        LED_BTN_SendColor.setOnClickListener(v -> {
            try {
                int r = Math.max(0, Math.min(255,
                        Integer.parseInt(LED_EDIT_CustomRGB[_R].getText().toString())));
                int g = Math.max(0, Math.min(255,
                        Integer.parseInt(LED_EDIT_CustomRGB[_G].getText().toString())));
                int b = Math.max(0, Math.min(255,
                        Integer.parseInt(LED_EDIT_CustomRGB[_B].getText().toString())));

                Main._Toast("CUSTOM RGB {R:" + r + " G:" + g + " B:" + b + " }");
                UDPs.sendSelectLed(LED_Selected, LED_TOTAL); // send selection first
                UDPs.sendColor(r, g, b);
                Main._Console(false, "►►",
                        "CUSTOM COLOR [R:{#R}" + r + "{##}] [G:{#G}"
                        + g + "{##}] [B:{#B}" + b + "{##}]");
            } catch (NumberFormatException e) {
                Main._Toast("Invalid RGB values entered.");
            }
        });

        // ── Deselect ALL ─────────────────────────────────────
        LED_BTN_DeselectALL.setOnClickListener(v -> {
            if (STS.isAmbilightOn()) {
                Main._Toast("{ AMBILIGHT ON }");
            } else {
                Main._Toast("[DESELECT ALL]");
                _deselectAll();
            }
        });

        // ── LEDs OFF ─────────────────────────────────────────
        LED_BTN_LedsOFF.setOnClickListener(v -> {
            Main._Toast("[LEDs OFF]");
            UDPs.sendSelectLed(LED_Selected, LED_TOTAL);
            UDPs.sendColor(0, 0, 0);
            Main._Console(false, "►►", "LED's {#Y}OFF{##}");
        });

        // ── Select ALL ───────────────────────────────────────
        LED_BTN_SelectALL.setOnClickListener(v -> {
            if (STS.isAmbilightOn()) {
                Main._Toast("{ AMBILIGHT ON }");
            } else {
                Main._Toast("[SELECT ALL]");
                _selectAll();
            }
        });

        // ── Save Dual Colour ─────────────────────────────────
        LED_BTN_SaveDual.setOnClickListener(v -> {
            if (STS.isAmbilightOn()) {
                Main._Toast("{ AMBILIGHT ON }");
            } else {
                Main._ShakeButton(LED_BTN_SaveDual, 500);
                Main._Toast("[SAVE DUAL COLOR]");
                // Request current dual colour from Arduino (all -1 = GET)
                UDPs.sendDualColor(-1, -1, -1, -1, -1, -1);
                _deselectAll();
                Main._Console(false, "►►",
                        "DUAL COLOR [LEFT R:{#R}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R] + "{##} G:{#G}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G] + "{##} B:{#B}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B] + "{##}]"
                        + " - [RIGHT R:{#R}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R] + "{##} G:{#G}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G] + "{##} B:{#B}"
                        + LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B] + "{##}]");
            }
        });

        // ── Colour Picker dialog ─────────────────────────────
        LED_BTN_ColorPicker.setOnClickListener(v ->
            ColorPickerDialogBuilder
                .with(Main, R.style.DialogColorPicker)
                .initialColor(Color.rgb(255, 255, 255))
                .wheelType(ColorPickerView.WHEEL_TYPE.FLOWER)
                .density(85)
                .noSliders()
                .setOnColorChangedListener(selectedColor -> {
                    // Live preview: update EditText fields
                    LED_EDIT_CustomRGB[_R].setText(
                            String.valueOf(Color.red(selectedColor)));
                    LED_EDIT_CustomRGB[_G].setText(
                            String.valueOf(Color.green(selectedColor)));
                    LED_EDIT_CustomRGB[_B].setText(
                            String.valueOf(Color.blue(selectedColor)));
                })
                .setOnColorSelectedListener(selectedColor -> {
                    // Confirmed: send colour via UDP
                    int r = Integer.parseInt(
                            LED_EDIT_CustomRGB[_R].getText().toString());
                    int g = Integer.parseInt(
                            LED_EDIT_CustomRGB[_G].getText().toString());
                    int b = Integer.parseInt(
                            LED_EDIT_CustomRGB[_B].getText().toString());
                    UDPs.sendSelectLed(LED_Selected, LED_TOTAL);
                    UDPs.sendColor(r, g, b);
                    Main._Console(false, "►►",
                            "COLOR SET [R:{#R}" + r + "{##}][G:{#G}"
                            + g + "{##}][B:{#B}" + b + "{##}]");
                })
                .build()
                .show()
        );
    }

    // --------------------------------------------------------
    // Per-LED button setup
    // --------------------------------------------------------
    /**
     * Initialise each LED button with a random preview colour,
     * mark it selected, and attach click / long-click listeners.
     */
    private void _setupLedButtons() {
        for (int i = 0; i < LED_TOTAL; i++) {
            final int z = i;
            
            // Skip HB LED button setup - it's now a HeartbeatPatternView
            if (i == LED_HB) {
                // Assign a random startup colour for HB
                Random rnd = new Random();
                LED_Color[i][_R] = rnd.nextInt(256);
                LED_Color[i][_G] = rnd.nextInt(256);
                LED_Color[i][_B] = rnd.nextInt(256);
                updateColor(i, LED_Color[i][_R], LED_Color[i][_G], LED_Color[i][_B]);
                
                // Start selected
                _selectToggle(i);
                
                // Click: toggle selection (unless Ambilight is active)
                LED_HeartbeatPattern.setOnClickListener(v -> {
                    if (!STS.isAmbilightOn()) _selectToggle(LED_HB);
                });
                
                // Long-click: copy HB LED colour into the RGB fields
                LED_HeartbeatPattern.setOnLongClickListener(v -> {
                    LED_EDIT_CustomRGB[_R].setText(
                            String.valueOf(LED_Color[LED_HB][_R]));
                    LED_EDIT_CustomRGB[_G].setText(
                            String.valueOf(LED_Color[LED_HB][_G]));
                    LED_EDIT_CustomRGB[_B].setText(
                            String.valueOf(LED_Color[LED_HB][_B]));
                    Main._Toast("{ " + LED_Color[LED_HB][_R] + " "
                            + LED_Color[LED_HB][_G] + " " + LED_Color[LED_HB][_B] + " }");
                    return true;
                });
                
                continue;
            }
            
            int id = Main.getResources().getIdentifier(
                    "_btn_led_" + i, "id", Main.getPackageName());
            LED_BTN_Leds[i] = Main.findViewById(id);
            LED_BTN_Leds[i].setBackgroundResource(R.drawable._btn_led);

            // Assign a random startup colour
            Random rnd = new Random();
            LED_Color[i][_R] = rnd.nextInt(256);
            LED_Color[i][_G] = rnd.nextInt(256);
            LED_Color[i][_B] = rnd.nextInt(256);
            updateColor(i, LED_Color[i][_R], LED_Color[i][_G], LED_Color[i][_B]);

            // Start selected
            _selectToggle(i);

            // Click: toggle selection (unless Ambilight is active)
            LED_BTN_Leds[i].setOnClickListener(v -> {
                if (!STS.isAmbilightOn()) _selectToggle(z);
            });

            // Long-click: copy this LED's colour into the RGB fields
            LED_BTN_Leds[i].setOnLongClickListener(v -> {
                LED_EDIT_CustomRGB[_R].setText(
                        String.valueOf(LED_Color[z][_R]));
                LED_EDIT_CustomRGB[_G].setText(
                        String.valueOf(LED_Color[z][_G]));
                LED_EDIT_CustomRGB[_B].setText(
                        String.valueOf(LED_Color[z][_B]));
                Main._Toast("{ " + LED_Color[z][_R] + " "
                        + LED_Color[z][_G] + " " + LED_Color[z][_B] + " }");
                return true;
            });
        }
    }

    // --------------------------------------------------------
    // Brightness SeekBar
    // --------------------------------------------------------
    /**
     * Configure the brightness BubbleSeekBar and attach listeners.
     * On release → sends brightness via UDPs.
     */
    private void _setupBrightnessBar() {
        LED_Brightness.getConfigBuilder()
                .min(0).progress(0).build();

        LED_Brightness.setOnProgressChangedListener(
            new BubbleSeekBar.OnProgressChangedListenerAdapter() {

                /** Live label update while dragging */
                @Override
                public void onProgressChanged(BubbleSeekBar bar,
                                              int progress,
                                              float progressFloat,
                                              boolean fromUser) {
                    _updateBrightnessLabel(progress);
                }

                /** Send brightness when finger is lifted */
                @Override
                public void getProgressOnActionUp(BubbleSeekBar bar,
                                                  int progress,
                                                  float progressFloat) {
                    _commitBrightness(progress);
                }
            });

        LED_BrightnessStepMinus.setOnClickListener(v -> _stepBrightness(-1));
        LED_BrightnessStepPlus .setOnClickListener(v -> _stepBrightness(+1));
        _updateBrightnessLabel(0);
    }

    /** Nudges brightness ±1, clamped to [0, LED_MAX_BRIGHTNESS]. */
    private void _stepBrightness(int direction) {
        int next = LED_Brightness.getProgress() + direction;
        if (next < 0 || next > LED_MAX_BRIGHTNESS) return;
        LED_Brightness.setProgress(next);
        _commitBrightness(next);
    }

    /** Sends brightness over UDP and refreshes label + stepper dim state. */
    private void _commitBrightness(int progress) {
        UDPs.sendSelectLed(LED_Selected, LED_TOTAL);
        UDPs.sendBrightness(progress);
        Main._Console(false, "►►",
                "BR PROGRESS {{#Y}" + progress + "{##}}");
        _updateBrightnessLabel(progress);
    }

    /** Refreshes the BRIGHTNESS label and dims steppers at edges. */
    @RequiresApi(api = Build.VERSION_CODES.M)
    private void _updateBrightnessLabel(int progress) {
        String prefix   = "BRIGHTNESS";
        String fullText = prefix;
        SpannableStringBuilder sb = new SpannableStringBuilder(fullText);
        ForegroundColorSpan cs = new ForegroundColorSpan(
                Main.getResources().getColor(
                        R.color.skbar_section_text_color, null));
        sb.setSpan(cs, prefix.length(), fullText.length(),
                Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
        LED_BrightnessText.setText(sb);
        LED_BrightnessValue.setText(String.valueOf(progress));

        boolean canDec = progress > 0;
        boolean canInc = progress < LED_MAX_BRIGHTNESS;
        LED_BrightnessStepMinus.setEnabled(canDec);
        LED_BrightnessStepPlus .setEnabled(canInc);
        LED_BrightnessStepMinus.setAlpha(canDec ? 1f : 0.3f);
        LED_BrightnessStepPlus .setAlpha(canInc ? 1f : 0.3f);
    }

    // ========================================================
    //  Dual-colour management
    // ========================================================

    /**
     * Load the saved dual-colour list from SharedPreferences.
     * Key namespace: "FuZz_DualColor" – integer keys "0", "1", …
     */
    private void _dualColorLoad() {
        SharedPreferences sp = Main.getSharedPreferences(
                "FuZz_DualColor", Context.MODE_PRIVATE);
        LED_DualColorList.clear();
        int size = sp.getAll().size();
        for (int i = 0; i < size; i++) {
            LED_DualColorList.add(i, sp.getString(String.valueOf(i), null));
        }
    }

    /**
     * Persist the current dual-colour list to SharedPreferences.
     * Called after every add or delete.
     */
    private void _dualColorSave() {
        SharedPreferences.Editor ed = Main.getSharedPreferences(
                "FuZz_DualColor", Context.MODE_PRIVATE).edit().clear();
        for (int i = 0; i < LED_DualColorList.size(); i++) {
            ed.putString(String.valueOf(i), LED_DualColorList.get(i));
        }
        ed.commit();
    }

    /**
     * Rebuild the horizontal row of dual-colour buttons from
     * LED_DualColorList.  Button 0 is always the random button.
     * Buttons 1..N are the saved pairs, shown in reverse order
     * (newest first).
     */
    private void _dualColorCreate() {
        LinearLayout layout = Main.findViewById(R.id._dual_buttons);
        layout.setOrientation(LinearLayout.HORIZONTAL);
        layout.removeAllViews();

        float dp = Main.getResources().getDisplayMetrics().density;
        int   sizePx   = (int) (DUAL_BTN_SIZE_DP * dp);
        int   marginPx = (int) (6 * dp);

        int listSize = LED_DualColorList.size();

        for (int i = 0; i <= listSize; i++) {
            final int i_   = i;
            // REVERSE index: newest colour appears closest to index 0
            final int listIndex = listSize - i;

            // Fixed square so the oval drawable below actually renders as
            // a circle, not a stretched pill like the old MATCH_PARENT width.
            LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                    sizePx, sizePx);
            params.setMarginEnd(marginPx);

            LED_BTN_DualColor[i] = new Button(Main);
            LED_BTN_DualColor[i].setId(i);

            DualColorDrawable dr = new DualColorDrawable(DUAL_BTN_SPLIT_ANGLE_DEG);
            dr.setStroke(2 * dp, ThemeManager.getColor(Main, R.color.stroke_line_color));
            LED_BTN_DualColor[i].setBackground(dr);

            if (i > 0) {
                // Saved pair: split colour
                String[] cols = LED_DualColorList.get(listIndex).split("\\.");
                int c1 = Color.parseColor("#" + cols[0]);
                int c2 = Color.parseColor("#" + cols[1]);
                dr.setColors(c1, c2);
            } else {
                // Random button: venn pair, not a split circle - the only
                // non-solid button in the row, so it reads as "two colours,
                // randomized" with no label needed.
                dr.setVennMode(true);
                reloadRandDualColor();
            }

            layout.addView(LED_BTN_DualColor[i], params);

            // Long-click: delete this entry (1..N), or open the colour
            // wheel to hand-dial the random pair (button 0)
            LED_BTN_DualColor[i].setOnLongClickListener(v -> {
                if (i_ != 0) {
                    LED_DualColorList.remove(listIndex);
                    Main._Toast("[DELETED]");
                    _dualColorSave();
                    _dualColorCreate();
                } else {
                    _openRandColorWheel(LED_BTN_DualColor[0], dr);
                }
                return true;
            });

            // Click: apply this dual colour via UDP
            LED_BTN_DualColor[i].setOnClickListener(v -> {
                if (STS.isAmbilightOn()) {
                    Main._Toast("{ AMBILIGHT ON }");
                    return;
                }
                if (i_ != 0) {
                    // Saved pair
                    String[] sp2 = LED_DualColorList.get(listIndex).split("\\.");
                    UDPs.sendDualColor(
                        Integer.parseInt(sp2[0].substring(0, 2), 16),
                        Integer.parseInt(sp2[0].substring(2, 4), 16),
                        Integer.parseInt(sp2[0].substring(4, 6), 16),
                        Integer.parseInt(sp2[1].substring(0, 2), 16),
                        Integer.parseInt(sp2[1].substring(2, 4), 16),
                        Integer.parseInt(sp2[1].substring(4, 6), 16));
                    Main._Toast("[DUAL COLOR " + i_ + "]");
                } else {
                    // Random
                    int[] rgbL = _hexToRgb(LED_RandDualColor.substring(0, 6));
                    int[] rgbR = _hexToRgb(LED_RandDualColor.substring(6, 12));
                    UDPs.sendDualColor(
                            rgbL[_R], rgbL[_G], rgbL[_B],
                            rgbR[_R], rgbR[_G], rgbR[_B]);
                    Main._Toast("[RANDOM DUAL COLOR]");
                    reloadRandDualColor();
                }
                _deselectAll();
            });
        }
    }

    /**
     * Generate a new pair of high-contrast random colours for the
     * random dual-colour button.  Stores the result in LED_RandDualColor
     * and updates button[0]'s gradient.
     *
     * Algorithm:
     *   1. Pick a random HSV hue (0-360)
     *   2. Second hue = first + 120-180° (complementary/triadic)
     *   3. Both colours use high saturation (0.85) and brightness (0.90)
     */
    public void reloadRandDualColor() {
        if (LED_BTN_DualColor[0] == null) return;
        DualColorDrawable dr = (DualColorDrawable)
                LED_BTN_DualColor[0].getBackground();

        Random rnd   = new Random();
        float  hue1  = rnd.nextFloat() * 360f;
        float  offset = 120f + (rnd.nextFloat() * 60f);
        float  hue2  = (hue1 + offset) % 360f;

        int c1 = Color.HSVToColor(new float[]{hue1, 0.85f, 0.9f});
        int c2 = Color.HSVToColor(new float[]{hue2, 0.85f, 0.9f});

        LED_RandDualColor = String.format("%06X%06X",
                c1 & 0xFFFFFF, c2 & 0xFFFFFF);

        dr.setColors(c1, c2);
    }

    /**
     * Opens the hand-dial colour wheel for the random button, positioned
     * above the dual-colour button bar. While open: dragging a marker or
     * hitting SPIN updates the button LIVE (via dr.setColors, same drawable
     * instance the button is already showing). Closing the popup - whether
     * by tapping outside or hitting SET - commits the final pair to
     * LED_RandDualColor, sends it over UDP exactly like a normal tap would,
     * then rerolls for next time - same "tap = send" contract, just
     * hand-dialled instead of purely random.
     */
    private void _openRandColorWheel(Button anchor, DualColorDrawable dr) {
        int[] rgbL = _hexToRgb(LED_RandDualColor.substring(0, 6));
        int[] rgbR = _hexToRgb(LED_RandDualColor.substring(6, 12));
        int c1 = Color.rgb(rgbL[_R], rgbL[_G], rgbL[_B]);
        int c2 = Color.rgb(rgbR[_R], rgbR[_G], rgbR[_B]);

        new ColorWheelPopup(Main).show(anchor, c1, c2,
                (liveC1, liveC2) -> dr.setColors(liveC1, liveC2),
                (finalC1, finalC2) -> {
                    if (STS.isAmbilightOn()) {
                        Main._Toast("{ AMBILIGHT ON }");
                        return;
                    }
                    LED_RandDualColor = String.format("%06X%06X",
                            finalC1 & 0xFFFFFF, finalC2 & 0xFFFFFF);
                    int[] L = _hexToRgb(LED_RandDualColor.substring(0, 6));
                    int[] R = _hexToRgb(LED_RandDualColor.substring(6, 12));
                    UDPs.sendDualColor(L[_R], L[_G], L[_B], R[_R], R[_G], R[_B]);
                    Main._Toast("[RANDOM DUAL COLOR]");
                    reloadRandDualColor();
                    _deselectAll();
                });
    }

    /**
     * Called by UDPr when a 'LDrrggbbRRggBB' packet arrives.
     * Appends the received pair to the list, saves, and rebuilds the UI.
     *
     * @param r1  Left  Red   0-255
     * @param g1  Left  Green 0-255
     * @param b1  Left  Blue  0-255
     * @param r2  Right Red   0-255
     * @param g2  Right Green 0-255
     * @param b2  Right Blue  0-255
     */
    public void addDualColor(int r1, int g1, int b1, int r2, int g2, int b2) {
        String left  = String.format("%02x%02x%02x", r1, g1, b1);
        String right = String.format("%02x%02x%02x", r2, g2, b2);
        LED_DualColorList.add(left + "." + right);
        _dualColorSave();
        _dualColorCreate();
    }

    // ========================================================
    //  LED selection helpers
    // ========================================================

    /**
     * Toggle selection state of a single LED.
     * Updates both LED_Selected[] and the button label ("•" / "").
     *
     * @param led  LED index 0 … LED_TOTAL-1
     */
    private void _selectToggle(int led) {
        LED_Selected[led] = !LED_Selected[led];
        
        // HB LED is now a HeartbeatPatternView, not a Button - skip setText
        if (led != LED_HB) {
            LED_BTN_Leds[led].setText(LED_Selected[led] ? "•" : "");
        }
        
        if (LED_Selected[led]) _startSelectStroke(led);
        else                   _stopSelectStroke(led);
    }

    /**
     * Select ALL LEDs.
     * Sets every LED_Selected[i] = true and marks button with "•".
     */
    private void _selectAll() {
        for (int i = 0; i < LED_TOTAL; i++) {
            LED_Selected[i] = true;
            // HB LED is now a HeartbeatPatternView, not a Button - skip setText
            if (i != LED_HB) {
                LED_BTN_Leds[i].setText("•");
            }
            _startSelectStroke(i);
        }
    }

    /**
     * Deselect ALL LEDs.
     * Sets every LED_Selected[i] = false and clears button text.
     * Called by Main, STS, and internally.
     */
    public void _deselectAll() {
        for (int i = 0; i < LED_TOTAL; i++) {
            LED_Selected[i] = false;
            // HB LED is now a HeartbeatPatternView, not a Button - skip setText
            if (LED_BTN_Leds[i] != null) {
                LED_BTN_Leds[i].setText("");
            }
            _stopSelectStroke(i);
        }
    }

    // --------------------------------------------------------
    // Selected-LED stroke effect (synchronised breathing pulse)
    // --------------------------------------------------------
    /**
     * Marks a LED button as following the shared breathing-pulse stroke
     * and (re)starts the shared animator if it isn't already running.
     * Colour is fixed (theme accent) - only alpha breathes, width never
     * changes - so newly-added buttons immediately match the in-flight
     * phase of every other selected button instead of starting their own.
     *
     * @param led  LED index 0 … LED_TOTAL-1
     */
    private void _startSelectStroke(int led) {
        if (LED_BTN_Leds[led] == null && led != LED_HB) return;
        LED_SelectPulseSet.add(led);

        if (LED_SelectPulseAnim == null) {
            int strokeColor = ThemeManager.getColor(Main, R.color.skbar_settings_title);

            LED_SelectPulseAnim = ValueAnimator.ofInt(SELECT_STROKE_ALPHA_MIN, SELECT_STROKE_ALPHA_MAX);
            LED_SelectPulseAnim.setDuration(SELECT_STROKE_CYCLE_MS);
            LED_SelectPulseAnim.setRepeatCount(ValueAnimator.INFINITE);
            LED_SelectPulseAnim.setRepeatMode(ValueAnimator.REVERSE);
            LED_SelectPulseAnim.addUpdateListener(a -> {
                int alpha = (int) a.getAnimatedValue();
                int strokeArgb = Color.argb(alpha,
                        Color.red(strokeColor), Color.green(strokeColor), Color.blue(strokeColor));
                int strokePx = Math.round(SELECT_STROKE_WIDTH_DP
                        * Main.getResources().getDisplayMetrics().density);

                // Single pass over the synced set on every frame - all
                // selected buttons read the same alpha, which is what
                // keeps them in phase regardless of when each was selected.
                for (int i : LED_SelectPulseSet) {
                    if (LED_BTN_Leds[i] != null) {
                        GradientDrawable dr = (GradientDrawable)
                                LED_BTN_Leds[i].getBackground().mutate();
                        dr.setStroke(strokePx, strokeArgb);
                    }
                    // HB has no button background - feed the same color/alpha/width
                    // straight into its pattern outline so it blinks in identical
                    // phase instead of running a separate animation.
                    if (i == LED_HB && LED_HeartbeatPattern != null) {
                        LED_HeartbeatPattern.setSelectStroke(strokeArgb, strokePx);
                    }
                }
            });
            LED_SelectPulseAnim.start();
        }
        
        // Immediately set heartbeat pattern selection if HB LED
        if (led == LED_HB && LED_HeartbeatPattern != null) {
            LED_HeartbeatPattern.setSelected(true);
        }
    }

    /**
     * Removes a LED button from the shared breathing pulse and restores
     * its normal static outline colour. Stops the shared animator once no
     * button is selected, to avoid animating in the background for nothing.
     *
     * @param led  LED index 0 … LED_TOTAL-1
     */
    private void _stopSelectStroke(int led) {
        LED_SelectPulseSet.remove(led);

        if (LED_SelectPulseSet.isEmpty() && LED_SelectPulseAnim != null) {
            LED_SelectPulseAnim.cancel();
            LED_SelectPulseAnim = null;
        }

        if (LED_BTN_Leds[led] != null) {
            int strokePx = Math.round(SELECT_STROKE_WIDTH_DP
                    * Main.getResources().getDisplayMetrics().density);
            int strokeColor = ThemeManager.getColor(Main, R.color.stroke_line_color);
            GradientDrawable dr = (GradientDrawable)
                    LED_BTN_Leds[led].getBackground().mutate();
            dr.setStroke(strokePx, strokeColor);
        }
        
        // Handle heartbeat pattern deselection
        if (led == LED_HB && LED_HeartbeatPattern != null) {
            LED_HeartbeatPattern.setSelected(false);
        }
    }

    // ========================================================
    //  Colour update (called by UDPr)
    // ========================================================

    /**
     * Apply a new colour to one or all LED buttons with a smooth
     * 300 ms animated transition.
     *
     * Special cases:
     *   • If led == LED_TOTAL → update ALL LEDs.
     *   • If Ambilight is ON for leds 0..LED_AMBILIGHT-1, skip those.
     *   • If led == LED_HB AND TV is ON AND HB_Dual setting != 0,
     *     the button is rendered as a left/right split (dual colour).
     *
     * @param led  Target LED index, or LED_TOTAL for "all LEDs".
     * @param r    Red   0-255
     * @param g    Green 0-255
     * @param b    Blue  0-255
     */
    public void updateColor(int led, int r, int g, int b) {
        for (int i = 0; i < LED_TOTAL; i++) {
            if (led != LED_TOTAL && i != led) continue;
            // Stream owns the Ambilight LEDs, and HB is mirrored from COM/BED/LAMP
            // (_updateHBFromAmbientZones) and must never be clobbered back to
            // whatever raw value the firmware reports for the disabled HB effect.
            if (STS.isAmbilightOn() && (i < LED_AMBILIGHT || i == LED_HB)) continue;

            int startColor = Color.argb(255,
                    LED_Color[i][_R], LED_Color[i][_G], LED_Color[i][_B]);

            // Determine target colours (solid or dual split)
            final int leftTarget;
            final int rightTarget;
            int hbDual = SET.getHBDualValue();

            if (i == LED_HB && STS.isTvOn() && hbDual != 0) {
                leftTarget  = Color.argb(255,
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
                rightTarget = Color.argb(255,
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);
            } else {
                leftTarget  = Color.argb(255, r, g, b);
                rightTarget = Color.argb(255, r, g, b);
            }

            // HB LED is now a HeartbeatPatternView, not a Button - skip button animation
            if (i == LED_HB) {
                // Persist new colour
                LED_Color[i][_R] = r;
                LED_Color[i][_G] = g;
                LED_Color[i][_B] = b;
                
                // Update heartbeat pattern directly
                updateHeartbeatPattern();
                continue;
            }

            // Animate the gradient on the button background
            GradientDrawable dr = (GradientDrawable)
                    LED_BTN_Leds[i].getBackground().mutate();
            dr.setGradientType(GradientDrawable.LINEAR_GRADIENT);
            dr.setOrientation(GradientDrawable.Orientation.LEFT_RIGHT);

            ArgbEvaluator ev   = new ArgbEvaluator();
            ValueAnimator  anim = ValueAnimator.ofFloat(0f, 1f);
            anim.setDuration(300);

            final int fi = i; // capture for lambda
            anim.addUpdateListener(a -> {
                float f = (float) a.getAnimatedValue();
                int cL  = (int) ev.evaluate(f, startColor, leftTarget);
                int cR  = (int) ev.evaluate(f, startColor, rightTarget);
                dr.setColors(new int[]{cL, cL, cL, cL, cR, cR, cR, cR});
            });
            anim.start();

            // Persist new colour
            LED_Color[i][_R] = r;
            LED_Color[i][_G] = g;
            LED_Color[i][_B] = b;

            if (STS.isAmbilightOn() && _isAmbientZoneLed(fi)) {
                _updateHBFromAmbientZones();
            }

            // Update SaveDual button if a dual-context LED changed
            if (fi == LED_DUALCOLOR_CONTEXT[0] || fi == LED_DUALCOLOR_CONTEXT[1]) {
                GradientDrawable drSave = _getGradient(LED_BTN_SaveDual);
                if (drSave != null) {
                    int c0 = Color.argb(255,
                            LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                            LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                            LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
                    int c1 = Color.argb(255,
                            LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                            LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                            LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);
                    drSave.setColors(new int[]{c0, c0, c0, c0, c1, c1, c1, c1});
                }
                _refreshHBDualSplit();
            }

            // Update heartbeat pattern if HB LED changed
            if (fi == LED_HB) {
                updateHeartbeatPattern();
            }
        }
    }

    // ========================================================
    //  Bulk colour apply (called by UDPr for device-pushed 'LK' frames)
    // ========================================================

    /**
     * Apply a colour to ONE button with the same 300 ms crossfade as
     * updateColor(), skipped entirely when the incoming colour already
     * matches LED_Color[i] (nothing changed, nothing to animate).
     *
     * Same target logic, persistence and SaveDual refresh as updateColor(),
     * just scoped to a single LED index instead of scanning/looping over
     * LED_TOTAL. Used for device→app state mirroring ('LK' bulk/scattered
     * frames); the interactive updateColor() is left untouched for user taps.
     *
     * @param i  LED index (bounds/ambilight guarded here).
     */
    private void _setColorNow(int i, int r, int g, int b) {
        if (i < 0 || i >= LED_TOTAL) return;
        // Stream owns the Ambilight LEDs; HB is mirrored from COM/BED/LAMP
        // (_updateHBFromAmbientZones) and must not be overwritten by a raw
        // firmware value here (e.g. the disabled HB effect reporting "off").
        if (STS.isAmbilightOn() && (i < LED_AMBILIGHT || i == LED_HB)) return;

        if (r == LED_Color[i][_R] && g == LED_Color[i][_G] && b == LED_Color[i][_B]) {
            return;                                                    // same colour already set
        }

        int startColor = Color.argb(255,
                LED_Color[i][_R], LED_Color[i][_G], LED_Color[i][_B]);

        final int leftTarget;
        final int rightTarget;
        int hbDual = SET.getHBDualValue();

        if (i == LED_HB && STS.isTvOn() && hbDual != 0) {              // HB dual split
            leftTarget  = Color.argb(255,
                    LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                    LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                    LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
            rightTarget = Color.argb(255,
                    LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                    LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                    LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);
        } else {
            leftTarget  = Color.argb(255, r, g, b);
            rightTarget = Color.argb(255, r, g, b);
        }

        // HB LED is now a HeartbeatPatternView - update it directly
        if (i == LED_HB) {
            LED_Color[i][_R] = r;
            LED_Color[i][_G] = g;
            LED_Color[i][_B] = b;
            updateHeartbeatPattern();
            return;
        }

        GradientDrawable dr = (GradientDrawable)
                LED_BTN_Leds[i].getBackground().mutate();
        dr.setGradientType(GradientDrawable.LINEAR_GRADIENT);
        dr.setOrientation(GradientDrawable.Orientation.LEFT_RIGHT);

        ArgbEvaluator ev   = new ArgbEvaluator();
        ValueAnimator  anim = ValueAnimator.ofFloat(0f, 1f);
        anim.setDuration(300);
        anim.addUpdateListener(a -> {
            float f = (float) a.getAnimatedValue();
            int cL  = (int) ev.evaluate(f, startColor, leftTarget);
            int cR  = (int) ev.evaluate(f, startColor, rightTarget);
            dr.setColors(new int[]{cL, cL, cL, cL, cR, cR, cR, cR});
        });
        anim.start();

        LED_Color[i][_R] = r;
        LED_Color[i][_G] = g;
        LED_Color[i][_B] = b;

        if (STS.isAmbilightOn() && _isAmbientZoneLed(i)) {
            _updateHBFromAmbientZones();
        }

        if (i == LED_DUALCOLOR_CONTEXT[0] || i == LED_DUALCOLOR_CONTEXT[1]) {
            GradientDrawable drSave = _getGradient(LED_BTN_SaveDual);
            if (drSave != null) {
                int c0 = Color.argb(255,
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
                int c1 = Color.argb(255,
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);
                drSave.setColors(new int[]{c0, c0, c0, c0, c1, c1, c1, c1});
            }
            _refreshHBDualSplit();
        }
    }

    /**
     * Repaints LED_HB's own button to reflect the current
     * LED_DUALCOLOR_CONTEXT[0]/[1] split, if HB dual mode is active.
     *
     * Needed because a colour update targeting a context LED (40/41) only
     * ever refreshed the SaveDual swatch - HB's own button kept showing
     * whatever it last rendered until a colour update targeted HB directly,
     * so it looked "stuck" on one solid colour instead of the live split.
     *
     * Also called by UDPr directly (STS.applyStatus() / SET_HB_Dual setting
     * changes) - turning TV or HB_Dual on/off doesn't send any colour packet
     * on its own, so without this HB stayed solid until the next unrelated
     * colour update happened to touch it.
     */
    public void _refreshHBDualSplit() {
        if (!(STS.isTvOn() && SET.getHBDualValue() != 0)) return;
        if (STS.isAmbilightOn()) return; // Ambilight/UDPRAW owns HB (COM/BED/LAMP mirror) - no dual split
        if (HBFx != null && HBFx.isActive()) return; // simulator owns the view while an HB effect is animating

        int leftTarget  = Color.argb(255,
                LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
        int rightTarget = Color.argb(255,
                LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);

        // HB LED is now a HeartbeatPatternView - update it directly
        if (LED_HeartbeatPattern != null) {
            LED_HeartbeatPattern.setDualColor(leftTarget, rightTarget);
        }
    }

    // ========================================================
    //  HB effect simulation (called by STS/UDPr)
    // ========================================================

    /** Zone index bounds - must match Arduino firmware (LED_START_I_COM/BED/LAMP). */
    private static final int ZONE_COM_START = 30, ZONE_COM_END = 40;   // 10 LEDs
    private static final int ZONE_BED_LAMP_START = 42, ZONE_BED_LAMP_END = 60; // BED(8) + LAMP(10), contiguous

    private boolean _isAmbientZoneLed(int i) {
        return (i >= ZONE_COM_START && i < ZONE_COM_END)
                || (i >= ZONE_BED_LAMP_START && i < ZONE_BED_LAMP_END);
    }

    /** Below this (per max colour channel) the COM/BED/LAMP average counts as "no real colour yet". */
    private static final int AMBIENT_MIRROR_MIN_BR = 20;

    /**
     * While Ambilight/UDPRAW is capturing, the firmware disables the HB effect
     * entirely (HB::StartEffect() bails when UDPRAW is active) and TV LEDs get
     * a local rainbow instead of the raw per-pixel capture stream - so HB has
     * no meaningful colour of its own either. Mirror it to the average of the
     * COM/BED/LAMP ambient zones instead, recomputed whenever any of them change.
     *
     * Never dims HB to black: if those zones haven't produced a real (bright
     * enough) colour yet, HB just keeps showing whatever it last had - it
     * should stay visibly lit the whole time Ambilight is active, never "off".
     */
    private void _updateHBFromAmbientZones() {
        if (!STS.isAmbilightOn()) return;
        long sumR = 0, sumG = 0, sumB = 0;
        int count = 0;
        for (int i = ZONE_COM_START; i < ZONE_COM_END; i++) {
            sumR += LED_Color[i][_R]; sumG += LED_Color[i][_G]; sumB += LED_Color[i][_B]; count++;
        }
        for (int i = ZONE_BED_LAMP_START; i < ZONE_BED_LAMP_END; i++) {
            sumR += LED_Color[i][_R]; sumG += LED_Color[i][_G]; sumB += LED_Color[i][_B]; count++;
        }
        if (count == 0) return;
        int r = (int) (sumR / count), g = (int) (sumG / count), b = (int) (sumB / count);
        if (Math.max(r, Math.max(g, b)) < AMBIENT_MIRROR_MIN_BR) return; // too dark - keep HB as-is, don't turn it off

        LED_Color[LED_HB][_R] = r;
        LED_Color[LED_HB][_G] = g;
        LED_Color[LED_HB][_B] = b;
        updateHeartbeatPattern();
    }

    /** Forwards the TV on/off edge to HBFx so it can (re)start/stop the local HB effect replay. */
    public void notifyTvOnChanged(boolean tvOn) {
        if (HBFx != null) HBFx.onTvOnChanged(tvOn);
    }

    /**
     * Forwards the Ambilight/UDPRAW on/off edge to HBFx (which stops the
     * simulator while active, matching firmware) and syncs HB to the current
     * COM/BED/LAMP average right away.
     */
    public void notifyAmbilightChanged(boolean on) {
        if (HBFx != null) HBFx.onAmbilightChanged(on);
        if (on) _updateHBFromAmbientZones();
    }

    /** Forwards a changed HB effect setting (32/33/34) to HBFx. */
    public void notifyHBSettingChanged(int settingId) {
        if (HBFx != null) HBFx.onHBSettingChanged(settingId);
    }

    /** Called by HBFx once it stops animating, so the view falls back to the normal static/dual paint. */
    public void onHBEffectSimulatorStopped() {
        updateHeartbeatPattern();
    }

    /**
     * Paint a contiguous run [start, start+count) to one colour, each pixel
     * crossfading over 300 ms via _setColorNow() (skipped per-pixel if the
     * colour is unchanged). Backs the 'LK' FILL record.
     *
     * @param start  first LED index
     * @param count  run length
     */
    public void updateColorBulk(int start, int count, int r, int g, int b) {
        int end = start + count;
        for (int i = start; i < end && i < LED_TOTAL; i++) {
            if (i < 0) continue;
            _setColorNow(i, r, g, b);
        }
    }

    /**
     * Paint a set of scattered LEDs, each its own colour, crossfading over
     * 300 ms via _setColorNow() (skipped per-pixel if unchanged).
     * Backs the 'LK' SETN record.
     *
     * @param idx  LED indices
     * @param rgb  matching [n][3] colours ({r,g,b} per entry)
     */
    public void updateColors(int[] idx, int[][] rgb) {
        if (idx == null || rgb == null) return;
        int n = Math.min(idx.length, rgb.length);
        for (int k = 0; k < n; k++) {
            _setColorNow(idx[k], rgb[k][_R], rgb[k][_G], rgb[k][_B]);
        }
    }

    // ========================================================
    //  Brightness helpers (called by UDPr)
    // ========================================================

    /**
     * Update the brightness SeekBar to a received value.
     * Called by UDPr when 'LBvv' is received from Arduino.
     *
     * @param brightness  0-255
     */
    public void setBrightnessProgress(int brightness) {
        LED_Brightness.setProgress(brightness);
        _updateBrightnessLabel(brightness);
    }

    /**
     * Set the maximum allowed brightness on the SeekBar.
     * Called by UDPr when 'LMvv' is received from Arduino.
     *
     * @param maxBrightness  Maximum brightness (hardware limit).
     */
    public void setMaxBrightness(int maxBrightness) {
        LED_Brightness.getConfigBuilder()
                .min(0).max(maxBrightness).progress(0).build();
    }

    // ========================================================
    //  SaveDual button lock / unlock  (called by STS)
    // ========================================================

    /**
     * Lock the SaveDual button (used when AmbientMode or Ambilight is ON).
     * Applies strikethrough text and disables interaction.
     */
    public void lockSaveDual() {
        LED_BTN_SaveDual.setPaintFlags(
                LED_BTN_SaveDual.getPaintFlags() | Paint.STRIKE_THRU_TEXT_FLAG);
        LED_BTN_SaveDual.setEnabled(false);
    }

    /**
     * Unlock the SaveDual button (normal operating state).
     */
    public void unlockSaveDual() {
        LED_BTN_SaveDual.setPaintFlags(0);
        LED_BTN_SaveDual.setEnabled(true);
    }

    // ========================================================
    //  Theming
    // ========================================================
    /**
     * Re-tints every theme-aware accent on the LEDS screen: the action
     * buttons (DeselectAll/Off/SelectAll/ColorPicker/Send/SaveDual),
     * the brightness bar + its label/value/steppers, the bed/lamp
     * alignment-hint icons, the RGB input glow, and the per-LED +
     * saved/random dual-colour swatch borders. Called once from
     * init(), and again by SettingsManager.applyTheme() after a live
     * theme switch.
     */
    public void applyThemeColors() {
        int muted  = ThemeManager.getColor(Main, R.color.text_muted_grey);
        int accent = ThemeManager.getColor(Main, R.color.skbar_settings_title);
        int value  = ThemeManager.getColor(Main, R.color.text_default);
        int label  = ThemeManager.getColor(Main, R.color.skbar_upper_text);
        int glow   = ThemeManager.getColor(Main, R.color.text_glow_shadow);

        if (LED_BTN_DeselectALL != null) LED_BTN_DeselectALL.setTextColor(muted);
        if (LED_BTN_SelectALL   != null) LED_BTN_SelectALL.setTextColor(muted);
        if (LED_BTN_LedsOFF     != null) LED_BTN_LedsOFF.setTextColor(muted);
        if (LED_BTN_ColorPicker != null) LED_BTN_ColorPicker.setTextColor(muted);
        if (LED_BTN_SendColor   != null) LED_BTN_SendColor.setTextColor(muted);
        if (LED_BTN_SaveDual    != null) LED_BTN_SaveDual.setTextColor(muted);

        if (LED_BrightnessText      != null) LED_BrightnessText.setTextColor(label);
        if (LED_BrightnessValue     != null) LED_BrightnessValue.setTextColor(value);
        if (LED_BrightnessStepMinus != null) LED_BrightnessStepMinus.setTextColor(accent);
        if (LED_BrightnessStepPlus  != null) LED_BrightnessStepPlus.setTextColor(accent);
        if (LED_Brightness          != null) {
            LED_Brightness.setTrackColor(ThemeManager.getColor(Main, R.color.skbar_track_color));
            LED_Brightness.setSecondTrackColor(ThemeManager.getColor(Main, R.color.skbar_second_track_color));
            LED_Brightness.setThumbColor(value);
        }

        if (LED_Icon_Bed  != null) LED_Icon_Bed.setColorFilter(value);
        if (LED_Icon_Lamp != null) LED_Icon_Lamp.setColorFilter(value);

        // DeselectAll/SelectAll/LedsOFF use md_transparent (a GradientDrawable,
        // transparent fill, no stroke) on purpose - they sit borderless inside
        // _btns_and_colors, which already owns the single panel stroke below.
        // Do NOT tintFrameStroke them - md_transparent IS a GradientDrawable,
        // so it would silently grow its own 2dp border -> double-stroke/grid look.
        ThemeManager.tintFrameStroke(Main, LED_BTN_ColorPicker, R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(Main, LED_BTN_SaveDual,    R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(Main, Main.findViewById(R.id._btns_and_colors), R.color.stroke_line_color);

        android.view.View innerDivider = Main.findViewById(R.id._fake_line);
        if (innerDivider != null) {
            innerDivider.setBackgroundColor(ThemeManager.getColor(Main, R.color.stroke_line_color));
        }

        // SEND button's background is a SOLID fill (no stroke) using stroke_line_color
        if (LED_BTN_SendColor != null) {
            android.graphics.drawable.Drawable sendBg = LED_BTN_SendColor.getBackground();
            if (sendBg instanceof android.graphics.drawable.GradientDrawable) {
                android.graphics.drawable.GradientDrawable g =
                        (android.graphics.drawable.GradientDrawable) sendBg.mutate();
                g.setColor(ThemeManager.getColor(Main, R.color.stroke_line_color));
                LED_BTN_SendColor.setBackground(g);
            }
        }

        // Every LED colour-swatch button has its own 2dp stroke (border) - theme that too,
        // independently of its solid/gradient fill which already reflects the actual LED colour.
        // Selected LEDs are skipped here: their stroke is owned by the shared
        // breathing-pulse ValueAnimator (_startSelectStroke) and would otherwise be
        // overwritten on every theme switch, fighting the animation each frame.
        int swatchStroke = ThemeManager.getColor(Main, R.color.stroke_line_color);
        float strokeDp = Main.getResources().getDisplayMetrics().density;
        for (int i = 0; i < LED_BTN_Leds.length; i++) {
            Button ledBtn = LED_BTN_Leds[i];
            if (ledBtn == null) continue;
            if (LED_SelectPulseSet.contains(i)) continue; // shared pulse owns this stroke
            android.graphics.drawable.Drawable bg = ledBtn.getBackground();
            if (bg instanceof android.graphics.drawable.GradientDrawable) {
                ((android.graphics.drawable.GradientDrawable) bg.mutate())
                        .setStroke(Math.round(2 * strokeDp), swatchStroke);
            }
        }

        // Saved/random dual-colour swatches (LED_BTN_DualColor) paint their own
        // border by hand (DualColorDrawable - see its class doc for why a plain
        // GradientDrawable stroke can't be reused there), so it needs the same
        // theme colour pushed into it explicitly here.
        for (Button dualBtn : LED_BTN_DualColor) {
            if (dualBtn == null) continue;
            android.graphics.drawable.Drawable dBg = dualBtn.getBackground();
            if (dBg instanceof DualColorDrawable) {
                ((DualColorDrawable) dBg).setStroke(2 * strokeDp, swatchStroke);
            }
        }

        for (EditText e : LED_EDIT_CustomRGB) {
            if (e != null) e.setShadowLayer(4, 3, 3, glow);
        }
    }

    // ========================================================
    //  Accessors
    // ========================================================

    /**
     * @return Current random dual-colour hex string (12 chars: LLLLLLRRRRRR)
     */
    public String getRandDualColorHex() {
        return LED_RandDualColor;
    }

    // ========================================================
    //  Utility
    // ========================================================

    /**
     * Convert a 6-character hex colour string to an int[3] {R, G, B}.
     *
     * @param hex  6-char hex string, e.g. "FF3C00"
     * @return     int[] { R, G, B } each 0-255
     */
    private int[] _hexToRgb(String hex) {
        return new int[]{
            Integer.parseInt(hex.substring(0, 2), 16),
            Integer.parseInt(hex.substring(2, 4), 16),
            Integer.parseInt(hex.substring(4, 6), 16)
        };
    }

    /**
     * Safely extracts a GradientDrawable from a View's background, even if it's
     * wrapped in an InsetDrawable (e.g. from _stroke_line.xml).
     */
    private GradientDrawable _getGradient(View v) {
        if (v == null) return null;
        Drawable d = v.getBackground();
        if (d == null) return null;
        d = d.mutate();
        if (d instanceof GradientDrawable) {
            return (GradientDrawable) d;
        } else if (d instanceof InsetDrawable) {
            Drawable inner = ((InsetDrawable) d).getDrawable();
            if (inner instanceof GradientDrawable) {
                return (GradientDrawable) inner;
            }
        }
        return null;
    }

    // ========================================================
    //  Heartbeat Pattern Color Updates
    // ========================================================

    /**
     * Update the heartbeat pattern view with the current HB LED color.
     * Handles static color and dual color modes.
     */
    public void updateHeartbeatPattern() {
        if (LED_HeartbeatPattern == null) return;
        if (HBFx != null && HBFx.isActive()) return; // simulator owns the view while an HB effect is animating

        int hbIndex = LED_HB;

        // While Ambilight/UDPRAW is capturing, HB always shows the single
        // COM/BED/LAMP-mirrored colour written by _updateHBFromAmbientZones() -
        // dual split doesn't apply here since the context LEDs (40/41) aren't
        // part of that mirror and would just show a stale/frozen half.
        if (STS.isTvOn() && SET.getHBDualValue() != 0 && !STS.isAmbilightOn()) {
            // Dual color mode: split 9/9, driven by the LEFT/RIGHT context LEDs
            // (matches _refreshHBDualSplit() / updateColor()'s dual branch) -
            // hbIndex itself is not one of the two halves.
            int leftColor = Color.argb(255, LED_Color[LED_DUALCOLOR_CONTEXT[0]][_R],
                                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_G],
                                        LED_Color[LED_DUALCOLOR_CONTEXT[0]][_B]);
            int rightColor = Color.argb(255, LED_Color[LED_DUALCOLOR_CONTEXT[1]][_R],
                                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_G],
                                        LED_Color[LED_DUALCOLOR_CONTEXT[1]][_B]);
            LED_HeartbeatPattern.setDualColor(leftColor, rightColor);
        } else {
            // Static color mode
            int color = Color.argb(255, LED_Color[hbIndex][_R], LED_Color[hbIndex][_G], LED_Color[hbIndex][_B]);
            LED_HeartbeatPattern.setStaticColor(color);
        }

        // Update selection state
        if (LED_Selected[hbIndex]) {
            LED_HeartbeatPattern.setSelected(true);
        } else {
            LED_HeartbeatPattern.setSelected(false);
        }
    }
}
