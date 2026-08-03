package com.fuzz.colors;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.widget.LinearLayout;
import android.widget.EditText;
import android.widget.PopupWindow;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * ============================================================
 *  ParfumPopup.java
 * ============================================================
 *  Long-press dialog for the diffuser status icon - starts or
 *  cancels the diffuser's PARFUM mode (a timed "insist" run of
 *  1-360 minutes during which the diffuser ignores every stop
 *  command). Dispenses in the user-picked pattern from
 *  DataSend.PARFUM_MODES (continuous / 10 sec pulses); the status
 *  icon itself still tints violet so the mode is visible at a glance.
 *
 *  Layout (TestModePopup card language - accent header bar on a
 *  rounded card, dimmed backdrop, centered, scale/fade entrance):
 *      [ PARFUM MODE ]                <- accent header
 *      ACTIVE - 27 MIN LEFT           <- only while running
 *      [ - ] [  30  ] [ + ]           <- minutes box + steppers
 *      ─────────●────────             <- 1-360 slider
 *      [CONTINUOUS] [ 10 SEC ]        <- dispense mode, side by side
 *      [        OFF        ]          <- outlined danger, greyed
 *                                        out when parfum inactive
 *      [        OK         ]          <- confirm, sends minutes+mode
 *
 *  Usage (see StatusManager.init()):
 *      new ParfumPopup(Main).show(icon, isActive, remainingMin,
 *              (minutes, mode) -> DATAs.sendParfum(minutes, mode),
 *              ()              -> DATAs.sendParfumOff());
 * ============================================================
 */
public class ParfumPopup {

    /** Fixed card width (dp) - matches the TestModePopup dialogs. */
    private static final int CARD_WIDTH_DP = 250;

    /** Backdrop dim amount behind the card. */
    private static final float DIM_AMOUNT = 0.45f;

    /** Soft destructive tint for the OFF action (theme-independent on purpose). */
    private static final int COLOR_DANGER = 0xFFF0918B;

    /** Parfum accent - matches status_parfum_violet / the strip cue colour. */
    private static final int COLOR_PARFUM = 0xFFA66FD9;

    /** Allowed duration range in minutes - mirrors firmware PARFUM_MAX_MIN. */
    private static final int MIN_MINUTES = 1;
    private static final int MAX_MINUTES = 360;

    /** Suggested default when parfum is not running. */
    private static final int DEFAULT_MINUTES = 30;

    /** Last used minutes - persisted across popup opens. */
    private static int lastUsedMinutes = DEFAULT_MINUTES;

    /** Fired with the confirmed duration and dispense mode when the user taps OK. */
    public interface OnStart { void onStart(int minutes, int mode); }

    /** Fired when the user taps OFF while parfum is active. */
    public interface OnStop  { void onStop(); }

    private final Context ctx;
    private PopupWindow popup;

    /** Live selection, kept in sync between the box, steppers and slider. */
    private int minutes = DEFAULT_MINUTES;

    /** Live dispense-mode selection - index into DataSend.PARFUM_MODES. */
    private int mode = 0;

    /** True while code (not the user) is writing the EditText - blocks feedback loops. */
    private boolean selfEdit = false;

    public ParfumPopup(Context ctx) {
        this.ctx = ctx;
    }

    /**
     * Build and show the parfum dialog dead-center on screen.
     *
     * @param anchor       Any attached view - used only as the window token.
     * @param active       True while parfum is currently running (enables OFF).
     * @param remainingMin Live remaining minutes (shown, and used as the
     *                     initial box value while active); ignored when 0.
     * @param onStart      Fired with the chosen duration and mode on OK.
     * @param onStop       Fired on OFF (only reachable while active).
     */
    public void show(View anchor, boolean active, int remainingMin, OnStart onStart, OnStop onStop) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int accent    = ThemeManager.getColor(ctx, R.color.tab_active_tint);
        int titleCol  = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        int strokeCol = ThemeManager.getColor(ctx, R.color.skbar_track_color);
        int rowFill   = _blend(cardBgCol, 0xFFFFFFFF, 0.07f);

        minutes = active && remainingMin > 0
                ? Math.min(remainingMin, MAX_MINUTES)
                : lastUsedMinutes;
        mode = 0;   // CONTINUOUS - Arduino doesn't relay the running mode, so no live value to restore

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Accent header bar (top corners rounded to match the card) ──
        TextView header = new TextView(ctx);
        header.setText("PARFUM MODE");
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(titleCol);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(accent);
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});
        header.setBackground(headerBg);
        card.addView(header);

        // Parfum-violet underline strip - the mode's signature colour
        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(COLOR_PARFUM);
        card.addView(bar);

        // ── Body ─────────────────────────────────────────────
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (14 * dp), (int) (12 * dp), (int) (14 * dp), (int) (14 * dp));

        // Live state line - only shown while parfum is running
        if (active) {
            TextView state = new TextView(ctx);
            state.setText("ACTIVE — " + Math.max(remainingMin, 1) + " MIN LEFT");
            state.setAllCaps(true);
            state.setLetterSpacing(0.08f);
            state.setTypeface(null, Typeface.BOLD);
            state.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
            state.setTextColor(COLOR_PARFUM);
            state.setGravity(Gravity.CENTER);
            state.setPadding(0, 0, 0, (int) (10 * dp));
            body.addView(state);
        }

        // ── Minus / minutes box / plus row ───────────────────
        LinearLayout stepRow = new LinearLayout(ctx);
        stepRow.setOrientation(LinearLayout.HORIZONTAL);
        stepRow.setGravity(Gravity.CENTER_VERTICAL);

        final EditText box = new EditText(ctx);
        box.setInputType(InputType.TYPE_CLASS_NUMBER);
        box.setText(String.valueOf(minutes));
        box.setTypeface(null, Typeface.BOLD);
        box.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);                  // Big + bold - readable at a glance
        box.setTextColor(ThemeManager.getColor(ctx, R.color.text_default)); // Strongest text colour of the theme
        box.setGravity(Gravity.CENTER);
        box.setSingleLine(true);
        box.setPadding(0, (int) (10 * dp), 0, (int) (10 * dp));
        GradientDrawable boxBg = new GradientDrawable();
        boxBg.setColor(rowFill);
        boxBg.setCornerRadius(10 * dp);
        boxBg.setStroke((int) (1 * dp), strokeCol);
        box.setBackground(boxBg);

        final SeekBar slider = new SeekBar(ctx);

        TextView minus = _buildStepButton("−", rowFill, strokeCol, titleCol, dp,
                () -> _setMinutes(minutes - 1, box, slider));
        TextView plus  = _buildStepButton("+", rowFill, strokeCol, titleCol, dp,
                () -> _setMinutes(minutes + 1, box, slider));

        LinearLayout.LayoutParams boxLp = new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        boxLp.setMargins((int) (10 * dp), 0, (int) (10 * dp), 0);
        box.setLayoutParams(boxLp);

        stepRow.addView(minus);
        stepRow.addView(box);
        stepRow.addView(plus);
        body.addView(stepRow);

        // "MIN (1-360)" caption under the box
        TextView caption = new TextView(ctx);
        caption.setText("MINUTES  ·  " + MIN_MINUTES + " - " + MAX_MINUTES);
        caption.setAllCaps(true);
        caption.setLetterSpacing(0.08f);
        caption.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        caption.setTextColor(titleCol);
        caption.setAlpha(0.65f);                                          // Secondary but still readable
        caption.setGravity(Gravity.CENTER);
        caption.setPadding(0, (int) (6 * dp), 0, (int) (4 * dp));
        body.addView(caption);

        // ── Slider (1-360, progress is minutes-1) ────────────
        slider.setMax(MAX_MINUTES - MIN_MINUTES);
        slider.setProgress(minutes - MIN_MINUTES);
        slider.getProgressDrawable().setTint(COLOR_PARFUM);
        slider.getThumb().setTint(COLOR_PARFUM);
        LinearLayout.LayoutParams sliderLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        sliderLp.setMargins(0, (int) (2 * dp), 0, (int) (10 * dp));
        slider.setLayoutParams(sliderLp);
        body.addView(slider);

        // Box -> minutes/slider (user typing only, clamped on the fly)
        box.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void onTextChanged(CharSequence s, int a, int b, int c) { }
            @Override public void afterTextChanged(Editable s) {
                if (selfEdit) return;
                int v;
                try { v = Integer.parseInt(s.toString()); }
                catch (NumberFormatException e) { return; }              // empty / mid-edit - keep last value
                if (v > MAX_MINUTES) { _setMinutes(MAX_MINUTES, box, slider); return; }
                if (v < MIN_MINUTES) return;                              // let "0" sit while typing e.g. "05"
                minutes = v;
                selfEdit = true;
                slider.setProgress(v - MIN_MINUTES);
                selfEdit = false;
            }
        });

        // Slider -> minutes/box (user drags only)
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (!fromUser || selfEdit) return;
                _setMinutes(progress + MIN_MINUTES, box, slider);
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { }
            @Override public void onStopTrackingTouch(SeekBar sb)  { }
        });

        // ── Mode row: two side-by-side buttons (CONTINUOUS / 10 SEC) ──
        LinearLayout modeRow = new LinearLayout(ctx);
        modeRow.setOrientation(LinearLayout.HORIZONTAL);

        final TextView[] modeBtns = new TextView[DataSend.PARFUM_MODES.length];
        for (int i = 0; i < DataSend.PARFUM_MODES.length; i++) {
            final int idx = i;
            TextView btn = _buildModeButton(DataSend.PARFUM_MODES[i], dp);
            LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            btnLp.setMargins(i == 0 ? 0 : (int) (4 * dp), 0,
                    i == modeBtns.length - 1 ? 0 : (int) (4 * dp), 0);
            btn.setLayoutParams(btnLp);
            btn.setOnClickListener(v -> _setMode(idx, modeBtns, rowFill, strokeCol, titleCol));
            modeBtns[i] = btn;
            modeRow.addView(btn);
        }
        _refreshModeButtons(modeBtns, rowFill, strokeCol, titleCol);   // paint initial selection (index 0)
        body.addView(modeRow);

        // "MODE · DISPENSE PATTERN" caption under the buttons
        TextView modeCaption = new TextView(ctx);
        modeCaption.setText("MODE  ·  DISPENSE PATTERN");
        modeCaption.setAllCaps(true);
        modeCaption.setLetterSpacing(0.08f);
        modeCaption.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        modeCaption.setTextColor(titleCol);
        modeCaption.setAlpha(0.65f);
        modeCaption.setGravity(Gravity.CENTER);
        modeCaption.setPadding(0, (int) (6 * dp), 0, (int) (10 * dp));
        body.addView(modeCaption);

        body.addView(_buildDivider(dp));

        // ── OFF (destructive, greyed out while parfum inactive) ──
        TextView offBtn = _buildActionButton("OFF", dp);
        if (active) {
            offBtn.setTextColor(COLOR_DANGER);
            GradientDrawable offBg = new GradientDrawable();
            offBg.setColor(0x00000000);
            offBg.setCornerRadius(10 * dp);
            offBg.setStroke((int) (1 * dp), COLOR_DANGER);
            offBtn.setBackground(offBg);
            offBtn.setOnClickListener(v -> {
                if (onStop != null) onStop.onStop();
                if (popup != null) popup.dismiss();
            });
        } else {
            int greyed = ThemeManager.getColor(ctx, R.color.text_muted_grey);
            offBtn.setTextColor(greyed);
            GradientDrawable offBg = new GradientDrawable();
            offBg.setColor(0x00000000);
            offBg.setCornerRadius(10 * dp);
            offBg.setStroke((int) (1 * dp), greyed);
            offBtn.setBackground(offBg);
            offBtn.setAlpha(0.45f);                                       // Visible but clearly disabled
            offBtn.setEnabled(false);
        }
        body.addView(offBtn);

        // ── OK (confirm - sends the chosen duration) ─────────
        TextView okBtn = _buildActionButton("OK", dp);
        okBtn.setTextColor(titleCol);
        GradientDrawable okBg = new GradientDrawable();
        okBg.setColor(rowFill);
        okBg.setCornerRadius(10 * dp);
        okBg.setStroke((int) (1 * dp), COLOR_PARFUM);
        okBtn.setBackground(okBg);
        okBtn.setOnClickListener(v -> {
            int v2 = _clamp(_boxValue(box, minutes));                     // Final clamp - box may hold "0"/junk
            lastUsedMinutes = v2;                                         // Remember for next time
            GradientDrawable sel = (GradientDrawable) okBtn.getBackground().mutate();
            sel.setColor(accent);                                          // Selected flash - matches TestModePopup
            if (onStart != null) onStart.onStart(v2, mode);
            if (popup != null) popup.dismiss();
        });
        body.addView(okBtn);

        card.addView(body);

        // ── Card frame ───────────────────────────────────────
        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(cardBgCol);
        cardBg.setCornerRadius(16 * dp);
        cardBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.setBackground(cardBg);
        card.setClipToOutline(true);

        int widthPx = (int) (CARD_WIDTH_DP * dp);
        popup = new PopupWindow(card, widthPx, LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);                                          // Needed for EditText keyboard
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);

        _dimBehind(popup);
        _animateIn(card);
    }

    // ========================================================
    //  Value sync helpers
    // ========================================================

    /**
     * Clamp + commit a new minutes value and push it into both controls.
     *
     * @param v       Requested minutes (any int - clamped here).
     * @param box     The minutes EditText.
     * @param slider  The 1-360 SeekBar.
     */
    private void _setMinutes(int v, EditText box, SeekBar slider) {
        minutes = _clamp(v);
        selfEdit = true;
        box.setText(String.valueOf(minutes));
        box.setSelection(box.getText().length());
        slider.setProgress(minutes - MIN_MINUTES);
        selfEdit = false;
    }

    /**
     * Commit the tapped dispense-mode index and repaint both buttons so
     * only the selected one shows the accent fill.
     *
     * @param idx       Tapped index into DataSend.PARFUM_MODES.
     * @param btns      Both mode buttons, parallel to PARFUM_MODES.
     * @param rowFill   Unselected fill colour.
     * @param strokeCol Unselected border colour.
     * @param titleCol  Unselected text colour.
     */
    private void _setMode(int idx, TextView[] btns, int rowFill, int strokeCol, int titleCol) {
        mode = idx;
        _refreshModeButtons(btns, rowFill, strokeCol, titleCol);
    }

    /**
     * Paint every mode button per current selection state - accent fill
     * for the selected one, neutral outline for the rest.
     *
     * @param btns      Both mode buttons, parallel to PARFUM_MODES.
     * @param rowFill   Unselected fill colour.
     * @param strokeCol Unselected border colour.
     * @param titleCol  Unselected text colour.
     */
    private void _refreshModeButtons(TextView[] btns, int rowFill, int strokeCol, int titleCol) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        for (int i = 0; i < btns.length; i++) {
            boolean selected = (i == mode);
            GradientDrawable bg = (GradientDrawable) btns[i].getBackground().mutate();
            bg.setColor(selected ? COLOR_PARFUM : rowFill);
            bg.setStroke((int) (1 * dp), selected ? COLOR_PARFUM : strokeCol);
            btns[i].setTextColor(selected ? 0xFFFFFFFF : titleCol);
        }
    }

    /**
     * Read the box's current numeric value, falling back on parse failure.
     *
     * @param box       The minutes EditText.
     * @param fallback  Value returned when the box is empty/non-numeric.
     */
    private int _boxValue(EditText box, int fallback) {
        try { return Integer.parseInt(box.getText().toString()); }
        catch (NumberFormatException e) { return fallback; }
    }

    /** Clamp to the MIN_MINUTES..MAX_MINUTES range. */
    private int _clamp(int v) {
        return Math.max(MIN_MINUTES, Math.min(MAX_MINUTES, v));
    }

    // ========================================================
    //  View builders  (TestModePopup styling)
    // ========================================================

    /**
     * Square − / + stepper button.
     *
     * @param label      "−" or "+".
     * @param rowFill    Lifted surface fill colour.
     * @param strokeCol  Border colour.
     * @param titleCol   Glyph colour.
     * @param dp         Display density factor.
     * @param onTap      Fired on every tap.
     */
    private TextView _buildStepButton(String label, int rowFill, int strokeCol,
                                      int titleCol, float dp, Runnable onTap) {
        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);                   // Match the bigger minutes box
        tv.setTextColor(ThemeManager.getColor(ctx, R.color.text_default));
        tv.setGravity(Gravity.CENTER);
        tv.setClickable(true);
        tv.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(rowFill);
        bg.setCornerRadius(10 * dp);
        bg.setStroke((int) (1 * dp), strokeCol);
        tv.setBackground(bg);
        int size = (int) (50 * dp);
        tv.setLayoutParams(new LinearLayout.LayoutParams(size, size));
        tv.setOnClickListener(v -> onTap.run());
        return tv;
    }

    /**
     * Half-width segmented mode button. Built in the neutral (unselected)
     * state - _refreshModeButtons() paints the actual selection right
     * after both buttons are added.
     *
     * @param label  Option text (e.g. "CONTINUOUS").
     * @param dp     Display density factor.
     */
    private TextView _buildModeButton(String label, float dp) {
        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setAllCaps(true);
        tv.setLetterSpacing(0.04f);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        tv.setSingleLine(true);
        tv.setGravity(Gravity.CENTER);
        tv.setPadding((int) (8 * dp), (int) (11 * dp), (int) (8 * dp), (int) (11 * dp));
        tv.setClickable(true);
        tv.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(10 * dp);
        tv.setBackground(bg);
        return tv;
    }

    /**
     * Full-width OFF/OK action row - caller supplies colours/background.
     *
     * @param label  Button text.
     * @param dp     Display density factor.
     */
    private TextView _buildActionButton(String label, float dp) {
        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setAllCaps(true);
        tv.setLetterSpacing(0.06f);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setSingleLine(true);
        tv.setGravity(Gravity.CENTER);
        tv.setPadding((int) (14 * dp), (int) (9 * dp), (int) (14 * dp), (int) (9 * dp));
        tv.setClickable(true);
        tv.setFocusable(true);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, (int) (4 * dp), 0, (int) (4 * dp));
        tv.setLayoutParams(lp);
        return tv;
    }

    /**
     * Hairline divider between the value controls and the action buttons.
     *
     * @param dp  Display density factor.
     */
    private View _buildDivider(float dp) {
        View line = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (1 * dp));
        lp.setMargins(0, (int) (8 * dp), 0, (int) (8 * dp));
        line.setLayoutParams(lp);
        line.setBackgroundColor(ThemeManager.getColor(ctx, R.color.stroke_line_color));
        return line;
    }

    /**
     * Linear ARGB blend from a toward b by t (0..1).
     *
     * @param a  Start color.
     * @param b  Target color.
     * @param t  Blend factor.
     */
    private int _blend(int a, int b, float t) {
        int aA = (a >> 24) & 0xFF, aR = (a >> 16) & 0xFF, aG = (a >> 8) & 0xFF, aB = a & 0xFF;
        int bA = (b >> 24) & 0xFF, bR = (b >> 16) & 0xFF, bG = (b >> 8) & 0xFF, bB = b & 0xFF;
        return ((int) (aA + (bA - aA) * t) << 24)
             | ((int) (aR + (bR - aR) * t) << 16)
             | ((int) (aG + (bG - aG) * t) << 8)
             |  (int) (aB + (bB - aB) * t);
    }

    /**
     * Dim the window behind the popup card (dialog-style backdrop).
     *
     * @param p  The shown PopupWindow whose backdrop gets dimmed.
     */
    private void _dimBehind(PopupWindow p) {
        View root = p.getContentView().getRootView();
        WindowManager wm = (WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);
        if (wm == null || !(root.getLayoutParams() instanceof WindowManager.LayoutParams)) return;
        WindowManager.LayoutParams lp = (WindowManager.LayoutParams) root.getLayoutParams();
        lp.flags |= WindowManager.LayoutParams.FLAG_DIM_BEHIND;
        lp.dimAmount = DIM_AMOUNT;
        wm.updateViewLayout(root, lp);
    }

    /**
     * Dialog-style entrance: quick scale-up + fade-in of the card.
     *
     * @param card  The card view to animate.
     */
    private void _animateIn(View card) {
        card.setScaleX(0.88f);
        card.setScaleY(0.88f);
        card.setAlpha(0f);
        card.animate()
            .scaleX(1f).scaleY(1f).alpha(1f)
            .setDuration(160)
            .setInterpolator(new DecelerateInterpolator())
            .start();
    }
}
