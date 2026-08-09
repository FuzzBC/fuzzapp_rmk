package com.fuzz.colors;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
import android.app.Activity;
import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.LinearInterpolator;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.SeekBar;
import android.widget.TextView;

/**
 * ============================================================
 *  DiffuserUsagePopup.java
 * ============================================================
 *  Long-press dialog for the top-left diffuser refill badge: an
 *  estimated remaining time until the next refill (same rolling-
 *  average data already driving the badge's percent and fill bar,
 *  see StatusManager.applyDiffuserUsage()), plus every other
 *  diffuser action - manual refill and Parfum mode - in one place.
 *
 *  Layout (same card language as ParfumPopup/TestModePopup - accent
 *  header bar on a rounded card, dimmed backdrop, centered,
 *  scale/fade entrance):
 *      [ DIFFUSER REFILL ]            <- accent header
 *      ~2h 15m REMAINING              <- big estimate line
 *      64% of average cycle left      <- secondary line
 *      ─────────────────────
 *      Since refill    1h 45m
 *      Average cycle   4h 05m
 *      History         3/10 cycles
 *      Lifetime        7 refills
 *      [ HOLD TO FORCE REFILL ]
 *      [ PARFUM MODE          ▾]      <- tap to expand inline (see below)
 *          [ − ][   30   ][ + ]
 *          MINUTES · 1-360
 *          ─────────●─────────
 *          [ CONTINUOUS ][ 10 SEC ]
 *          [    OFF    ][  START  ]
 *      [        CLOSE      ]
 *
 *  The Parfum section is a true inline accordion, not a separate
 *  popup - tapping the header toggles a local expanded flag, fades the
 *  panel in/out, and swaps the header's corner radii so it reads as
 *  one continuous card whether collapsed or open. It opens pre-filled
 *  with live remaining minutes while a run is active, or the last
 *  minutes/mode actually sent via START otherwise - persisted in
 *  SharedPreferences (see _loadLastParfumSettings()/_saveLastParfumSettings()),
 *  so it survives an app restart, not just this session. Controls mirror
 *  ParfumPopup's (minutes stepper + slider + mode chips + OFF/START)
 *  but drive the exact same DataSend calls directly, so ParfumPopup
 *  itself is no longer reachable from anywhere in the app - see its
 *  own class doc.
 *
 *  Usage (see StatusManager.init()):
 *      new DiffuserUsagePopup(Main).show(anchor, diffuserRefillPercent,
 *              accumMin, avgMin, refillCount, totalRefills,
 *              parfumActive, parfumRemainingMin);
 * ============================================================
 */
public class DiffuserUsagePopup {

    /** Fixed card width (dp) - matches ParfumPopup/TestModePopup. */
    private static final int CARD_WIDTH_DP = 250;

    /** Backdrop dim amount behind the card. */
    private static final float DIM_AMOUNT = 0.45f;

    /** Hold duration required on FORCE REFILL before it actually fires. */
    private static final int REFILL_HOLD_MS = 5000;

    /** Fixed height prevents the progress overlay from expanding the popup. */
    private static final int REFILL_BUTTON_HEIGHT_DP = 36;

    /** Allowed Parfum duration range in minutes - mirrors firmware PARFUM_MAX_MIN. */
    private static final int PARFUM_MIN_MINUTES = 1;
    private static final int PARFUM_MAX_MINUTES = 360;

    /** Fallback minutes/mode the very first time this has ever been opened (no saved settings yet). */
    private static final int PARFUM_DEFAULT_MINUTES = 30;
    private static final int PARFUM_DEFAULT_MODE    = 0; // CONTINUOUS

    /**
     * Last minutes/mode actually sent via START - persisted to SharedPreferences
     * (not just in-memory) so it survives an app restart, not just this
     * session. Written by _saveLastParfumSettings(), read by
     * _loadLastParfumSettings() to pre-fill the accordion whenever Parfum
     * isn't currently running (see show()'s doc for the "remaining minutes
     * if ON, last-sent settings if OFF" rule).
     */
    private static final String PARFUM_PREFS_NAME = "FuZz_Parfum";
    private static final String PARFUM_PREF_MINUTES = "minutes";
    private static final String PARFUM_PREF_MODE    = "mode";

    private final Context ctx;
    private PopupWindow popup;

    /** Running hold-to-confirm fill animation, if any - cancelled on early release. */
    private ValueAnimator refillHoldAnimator;

    public DiffuserUsagePopup(Context ctx) {
        this.ctx = ctx;
    }

    /**
     * Build and show the remaining-time dialog dead-center on screen.
     *
     * @param anchor            Any attached view - used only as the window token.
     * @param percent           Current badge percent (0-100), or -1 while still
     *                          learning (no refill history yet).
     * @param accumMin          Effective minutes run since the last refill.
     * @param avgMin            Rolling average of completed refill cycles, minutes.
     * @param refillCount       Valid entries in the rolling history, 0-10.
     * @param totalRefills      Lifetime refill count.
     * @param parfumActive      True while Parfum mode is currently running.
     * @param parfumRemainingMin Live remaining minutes (shown, and used as the
     *                          accordion's initial value while active); ignored when 0.
     */
    public void show(View anchor, int percent, int accumMin, int avgMin, int refillCount, int totalRefills,
                      boolean parfumActive, int parfumRemainingMin) {
        // Guards against WindowManager.BadTokenException if the anchor's
        // activity finished (or the view detached) before this ran - see
        // ColorWheelPopup.show() for the full explanation.
        if (anchor.getWindowToken() == null) return;
        if (ctx instanceof Activity) {
            Activity act = (Activity) ctx;
            if (act.isFinishing() || act.isDestroyed()) return;
        }

        float dp = ctx.getResources().getDisplayMetrics().density;
        int titleCol  = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        int strokeCol = ThemeManager.getColor(ctx, R.color.skbar_track_color);
        int rowFill   = _blend(cardBgCol, 0xFFFFFFFF, 0.07f);

        boolean learning = (percent < 0 || refillCount == 0 || avgMin <= 0);
        int accent = learning
                ? ContextCompat_getColor(R.color.text_muted_grey)
                : (percent > 50) ? ContextCompat_getColor(R.color.status_on_green)
                : (percent > 20) ? ContextCompat_getColor(R.color.status_ready_yellow)
                : ContextCompat_getColor(R.color.status_off_red);

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Accent header bar (top corners rounded to match the card) ──
        TextView header = new TextView(ctx);
        header.setText("DIFFUSER REFILL");
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(titleCol);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(ThemeManager.getColor(ctx, R.color.tab_active_tint));
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});
        header.setBackground(headerBg);
        card.addView(header);

        // Underline strip - tinted to match the badge's current state
        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(accent);
        card.addView(bar);

        // ── Body ─────────────────────────────────────────────
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (14 * dp), (int) (14 * dp), (int) (14 * dp), (int) (14 * dp));

        if (learning) {
            TextView big = new TextView(ctx);
            big.setText("LEARNING");
            big.setTypeface(null, Typeface.BOLD);
            big.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
            big.setTextColor(accent);
            big.setGravity(Gravity.CENTER);
            big.setPadding(0, 0, 0, (int) (6 * dp));
            body.addView(big);

            TextView sub = new TextView(ctx);
            sub.setText("No completed refill cycle yet - still building the history this estimate needs.");
            sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            sub.setTextColor(titleCol);
            sub.setAlpha(0.75f);
            sub.setGravity(Gravity.CENTER);
            sub.setPadding(0, 0, 0, (int) (12 * dp));
            body.addView(sub);
        } else {
            int remainingMin = Math.max(0, avgMin - accumMin);
            TextView big = new TextView(ctx);
            big.setText(remainingMin > 0 ? (_fmtDuration(remainingMin) + " REMAINING") : "REFILL DUE NOW");
            big.setAllCaps(true);
            big.setLetterSpacing(0.04f);
            big.setTypeface(null, Typeface.BOLD);
            big.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
            big.setTextColor(accent);
            big.setGravity(Gravity.CENTER);
            big.setPadding(0, 0, 0, (int) (4 * dp));
            body.addView(big);

            TextView sub = new TextView(ctx);
            sub.setText(percent + "% of average cycle left");
            sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            sub.setTextColor(titleCol);
            sub.setAlpha(0.75f);
            sub.setGravity(Gravity.CENTER);
            sub.setPadding(0, 0, 0, (int) (12 * dp));
            body.addView(sub);
        }

        body.addView(_buildDivider(dp, strokeCol));

        LinearLayout statsBox = new LinearLayout(ctx);
        statsBox.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable statsBg = new GradientDrawable();
        statsBg.setColor(rowFill);
        statsBg.setCornerRadius(10 * dp);
        statsBox.setBackground(statsBg);
        statsBox.setPadding((int) (10 * dp), (int) (8 * dp), (int) (10 * dp), (int) (8 * dp));
        int statsMarginTop = (int) (8 * dp);
        LinearLayout.LayoutParams statsLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        statsLp.setMargins(0, statsMarginTop, 0, 0);
        statsBox.setLayoutParams(statsLp);

        // "Since refill" is the running-time number (how long it's actually been
        // working, as opposed to REMAINING above) - tinted with the same accent
        // as the header/REMAINING line instead of plain titleCol like the other
        // rows, so it still stands out at a glance without needing a duplicate
        // headline of its own outside this box.
        statsBox.addView(_statRow("Since refill", _fmtDuration(accumMin), accent, titleCol, dp));
        statsBox.addView(_statRow("Average cycle", avgMin > 0 ? _fmtDuration(avgMin) : "-", titleCol, dp));
        statsBox.addView(_statRow("History", refillCount + "/10 cycles", titleCol, dp));
        statsBox.addView(_statRow("Lifetime", totalRefills + " refill" + (totalRefills == 1 ? "" : "s"), titleCol, dp));
        body.addView(statsBox);

        // ── MARK AS REFILLED (hold 5s to confirm) ────────────
        // Manual refill: tells the diffuser to bank the current usage cycle
        // into its refill history right now and reset the accumulator, for
        // when the user tops it off proactively rather than waiting for an
        // actual out-of-water drop (the only other thing that finalizes a
        // cycle). See DataSend.sendDiffuserManualRefill() / diffuser "Dr".
        // Gated behind a 5-second press-and-hold (with a filling progress
        // overlay) rather than a plain tap, since it resets tracked usage
        // history and shouldn't be triggerable by an accidental touch.
        FrameLayout refillContainer = new FrameLayout(ctx);
        refillContainer.setClickable(true);
        refillContainer.setFocusable(true);
        GradientDrawable refillBg = new GradientDrawable();
        refillBg.setColor(ContextCompat_getColor(R.color.status_on_green));
        refillBg.setCornerRadius(10 * dp);
        refillBg.setStroke((int) dp, _blend(ContextCompat_getColor(R.color.status_on_green), 0xFFFFFFFF, 0.38f));
        refillContainer.setBackground(refillBg);
        refillContainer.setClipToOutline(true);   // clips the square fill overlay below to these rounded corners
        refillContainer.setElevation(2 * dp);
        LinearLayout.LayoutParams refillLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (REFILL_BUTTON_HEIGHT_DP * dp));
        refillLp.setMargins(0, (int) (14 * dp), 0, 0);
        refillContainer.setLayoutParams(refillLp);

        // Fill overlay - brighter tint, grows left-to-right as the hold progresses.
        // Left-anchored scaleX instead of width/level animation: simplest way to get
        // a clean, jank-free fill with a plain View, and setClipToOutline() above
        // takes care of matching the container's rounded corners without needing
        // this view to carry its own matching corner radius.
        View refillFill = new View(ctx);
        refillFill.setBackgroundColor(_blend(ContextCompat_getColor(R.color.status_on_green), 0xFFFFFFFF, 0.35f));
        refillFill.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        refillFill.setPivotX(0f);
        refillFill.setScaleX(0f);
        refillContainer.addView(refillFill);

        TextView refillBtn = new TextView(ctx);
        refillBtn.setText("HOLD TO FORCE REFILL");
        refillBtn.setAllCaps(true);
        refillBtn.setLetterSpacing(0.06f);
        refillBtn.setTypeface(null, Typeface.BOLD);
        refillBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        refillBtn.setTextColor(0xFFFFFFFF);
        refillBtn.setGravity(Gravity.CENTER);
        refillBtn.setIncludeFontPadding(false);
        refillBtn.setPadding((int) (12 * dp), 0, (int) (12 * dp), 0);
        refillBtn.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        refillContainer.addView(refillBtn);

        refillContainer.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    _startRefillHold(refillFill, refillBtn);
                    return true;
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    _cancelRefillHold(refillFill, refillBtn);
                    return true;
                default:
                    return false;
            }
        });
        body.addView(refillContainer);

        // ── PARFUM MODE (true inline accordion) ───────────────
        // Moved here from the diffuser status icon's own long-press (see
        // StatusManager.init()'s note) so every diffuser action lives
        // behind one obvious entry point instead of two different
        // long-press targets. Tapping the header expands/collapses the
        // panel below it in place - see the class doc for the full layout.
        int parfumViolet = ContextCompat_getColor(R.color.status_parfum_violet);
        int parfumFill   = _blend(parfumViolet, cardBgCol, 0.82f);

        // Remaining minutes from the board while a run is active (live,
        // authoritative); otherwise fall back to whatever was last actually
        // sent via START, persisted across app restarts - see
        // _loadLastParfumSettings(). The board never relays which mode is
        // currently running, so the last-sent mode is the best guess either way.
        int[] lastSettings = _loadLastParfumSettings();
        final int[]     minutesHolder  = { (parfumActive && parfumRemainingMin > 0)
                ? Math.min(parfumRemainingMin, PARFUM_MAX_MINUTES) : lastSettings[0] };
        final int[]     modeHolder     = { lastSettings[1] };
        // Always opens collapsed, even mid-run - see parfumHeaderActive
        // below for the always-visible "ACTIVE" indicator that keeps a
        // running session from going invisible while collapsed.
        final boolean[] expandedHolder = { false };

        LinearLayout parfumGroup = new LinearLayout(ctx);
        parfumGroup.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams parfumGroupLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        parfumGroupLp.setMargins(0, (int) (8 * dp), 0, 0);
        parfumGroup.setLayoutParams(parfumGroupLp);

        // Header - the only always-visible part; toggles the panel below it.
        LinearLayout parfumHeader = new LinearLayout(ctx);
        parfumHeader.setOrientation(LinearLayout.HORIZONTAL);
        parfumHeader.setGravity(Gravity.CENTER);
        parfumHeader.setClickable(true);
        parfumHeader.setFocusable(true);
        parfumHeader.setPadding(0, (int) (10 * dp), 0, (int) (10 * dp));

        TextView parfumLabel = new TextView(ctx);
        parfumLabel.setText("PARFUM MODE");
        parfumLabel.setAllCaps(true);
        parfumLabel.setLetterSpacing(0.06f);
        parfumLabel.setTypeface(null, Typeface.BOLD);
        parfumLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        parfumLabel.setTextColor(parfumViolet);
        parfumHeader.addView(parfumLabel);

        // Small always-visible "running" indicator - the panel itself now
        // always starts collapsed (see expandedHolder above), so without
        // this a Parfum session already in progress would look identical
        // to one that's off until the user thinks to tap the header.
        if (parfumActive) {
            TextView parfumHeaderActive = new TextView(ctx);
            parfumHeaderActive.setText("  •  " + Math.max(parfumRemainingMin, 1) + "m LEFT");
            parfumHeaderActive.setTypeface(null, Typeface.BOLD);
            parfumHeaderActive.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
            parfumHeaderActive.setTextColor(parfumViolet);
            parfumHeaderActive.setAlpha(0.85f);
            parfumHeader.addView(parfumHeaderActive);
        }

        TextView parfumChevron = new TextView(ctx);
        parfumChevron.setText("▾"); // ▾ - rotated 180° when expanded (see toggle handler)
        parfumChevron.setTextColor(parfumViolet);
        parfumChevron.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        LinearLayout.LayoutParams chevronLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        chevronLp.setMargins((int) (5 * dp), 0, 0, 0);
        parfumChevron.setLayoutParams(chevronLp);
        parfumChevron.setRotation(expandedHolder[0] ? 180f : 0f);
        parfumHeader.addView(parfumChevron);

        parfumGroup.addView(parfumHeader);

        // Panel - minutes stepper + slider + mode chips + OFF/START, same
        // controls ParfumPopup had, driving the exact same DataSend calls.
        LinearLayout parfumPanel = new LinearLayout(ctx);
        parfumPanel.setOrientation(LinearLayout.VERTICAL);
        parfumPanel.setPadding((int) (10 * dp), (int) (10 * dp), (int) (10 * dp), (int) (10 * dp));
        parfumPanel.setVisibility(expandedHolder[0] ? View.VISIBLE : View.GONE);
        GradientDrawable parfumPanelBg = new GradientDrawable();
        parfumPanelBg.setColor(_blend(parfumFill, 0xFF000000, 0.12f));
        parfumPanelBg.setCornerRadii(new float[]{0, 0, 0, 0, 10 * dp, 10 * dp, 10 * dp, 10 * dp});
        parfumPanelBg.setStroke((int) dp, parfumViolet);
        parfumPanel.setBackground(parfumPanelBg);
        parfumGroup.addView(parfumPanel);

        if (parfumActive) {
            TextView activeState = new TextView(ctx);
            activeState.setText("ACTIVE — " + Math.max(parfumRemainingMin, 1) + " MIN LEFT");
            activeState.setAllCaps(true);
            activeState.setLetterSpacing(0.06f);
            activeState.setTypeface(null, Typeface.BOLD);
            activeState.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
            activeState.setTextColor(parfumViolet);
            activeState.setGravity(Gravity.CENTER);
            activeState.setPadding(0, 0, 0, (int) (8 * dp));
            parfumPanel.addView(activeState);
        }

        // Minus / minutes value / plus row - centred as a compact trio
        // rather than stretched across the full row width.
        LinearLayout parfumStepRow = new LinearLayout(ctx);
        parfumStepRow.setOrientation(LinearLayout.HORIZONTAL);
        parfumStepRow.setGravity(Gravity.CENTER);

        final TextView minutesValue = new TextView(ctx);
        minutesValue.setText(String.valueOf(minutesHolder[0]));
        minutesValue.setTypeface(null, Typeface.BOLD);
        minutesValue.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        minutesValue.setTextColor(ThemeManager.getColor(ctx, R.color.text_default));
        minutesValue.setGravity(Gravity.CENTER);
        minutesValue.setPadding(0, (int) (4 * dp), 0, (int) (4 * dp));
        GradientDrawable minutesBg = new GradientDrawable();
        minutesBg.setColor(rowFill);
        minutesBg.setCornerRadius(7 * dp);
        minutesValue.setBackground(minutesBg);

        final SeekBar parfumSlider = new SeekBar(ctx);

        TextView parfumMinus = _buildStepButton("−", rowFill, strokeCol, titleCol, dp,
                () -> _setParfumMinutes(minutesHolder[0] - 1, minutesHolder, minutesValue, parfumSlider));
        TextView parfumPlus  = _buildStepButton("+", rowFill, strokeCol, titleCol, dp,
                () -> _setParfumMinutes(minutesHolder[0] + 1, minutesHolder, minutesValue, parfumSlider));

        LinearLayout.LayoutParams minutesValueLp = new LinearLayout.LayoutParams(
                (int) (38 * dp), LinearLayout.LayoutParams.WRAP_CONTENT);
        minutesValueLp.setMargins((int) (6 * dp), 0, (int) (6 * dp), 0);
        minutesValue.setLayoutParams(minutesValueLp);

        parfumStepRow.addView(parfumMinus);
        parfumStepRow.addView(minutesValue);
        parfumStepRow.addView(parfumPlus);
        parfumPanel.addView(parfumStepRow);

        TextView minutesCaption = new TextView(ctx);
        minutesCaption.setText("MINUTES · " + PARFUM_MIN_MINUTES + "-" + PARFUM_MAX_MINUTES);
        minutesCaption.setAllCaps(true);
        minutesCaption.setLetterSpacing(0.08f);
        minutesCaption.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9);
        minutesCaption.setTextColor(titleCol);
        minutesCaption.setAlpha(0.65f);
        minutesCaption.setGravity(Gravity.CENTER);
        minutesCaption.setPadding(0, (int) (5 * dp), 0, (int) (3 * dp));
        parfumPanel.addView(minutesCaption);

        parfumSlider.setMax(PARFUM_MAX_MINUTES - PARFUM_MIN_MINUTES);
        parfumSlider.setProgress(minutesHolder[0] - PARFUM_MIN_MINUTES);
        parfumSlider.getProgressDrawable().setTint(parfumViolet);
        parfumSlider.getThumb().setTint(parfumViolet);
        LinearLayout.LayoutParams parfumSliderLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        parfumSliderLp.setMargins(0, (int) (2 * dp), 0, (int) (10 * dp));
        parfumSlider.setLayoutParams(parfumSliderLp);
        parfumPanel.addView(parfumSlider);

        final boolean[] selfEditHolder = { false };
        parfumSlider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
                if (!fromUser || selfEditHolder[0]) return;
                minutesHolder[0] = progress + PARFUM_MIN_MINUTES;
                minutesValue.setText(String.valueOf(minutesHolder[0]));
            }
            @Override public void onStartTrackingTouch(SeekBar sb) { }
            @Override public void onStopTrackingTouch(SeekBar sb) { }
        });

        // Dispense mode chips - CONTINUOUS / 10 SEC (DataSend.PARFUM_MODES)
        LinearLayout modeRow = new LinearLayout(ctx);
        modeRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams modeRowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        modeRowLp.setMargins(0, 0, 0, (int) (8 * dp));
        modeRow.setLayoutParams(modeRowLp);

        TextView[] modeBtns = new TextView[DataSend.PARFUM_MODES.length];
        for (int m = 0; m < DataSend.PARFUM_MODES.length; m++) {
            final int idx = m;
            TextView chip = new TextView(ctx);
            chip.setText(DataSend.PARFUM_MODES[m]);
            chip.setAllCaps(true);
            chip.setLetterSpacing(0.04f);
            chip.setTypeface(null, Typeface.BOLD);
            chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9);
            chip.setGravity(Gravity.CENTER);
            chip.setPadding(0, (int) (7 * dp), 0, (int) (7 * dp));
            chip.setClickable(true);
            chip.setFocusable(true);
            LinearLayout.LayoutParams chipLp = new LinearLayout.LayoutParams(0,
                    LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            if (m > 0) chipLp.setMargins((int) (6 * dp), 0, 0, 0);
            chip.setLayoutParams(chipLp);
            modeBtns[m] = chip;
            chip.setOnClickListener(v -> {
                modeHolder[0] = idx;
                _refreshParfumModeChips(modeBtns, modeHolder[0], parfumViolet, rowFill, strokeCol, titleCol, dp);
            });
            modeRow.addView(chip);
        }
        _refreshParfumModeChips(modeBtns, modeHolder[0], parfumViolet, rowFill, strokeCol, titleCol, dp);
        parfumPanel.addView(modeRow);

        // OFF / START row
        LinearLayout parfumActionRow = new LinearLayout(ctx);
        parfumActionRow.setOrientation(LinearLayout.HORIZONTAL);

        TextView parfumOff = new TextView(ctx);
        parfumOff.setText("OFF");
        parfumOff.setAllCaps(true);
        parfumOff.setLetterSpacing(0.06f);
        parfumOff.setTypeface(null, Typeface.BOLD);
        parfumOff.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        parfumOff.setGravity(Gravity.CENTER);
        parfumOff.setPadding(0, (int) (8 * dp), 0, (int) (8 * dp));
        parfumOff.setClickable(true);
        parfumOff.setFocusable(true);
        parfumOff.setEnabled(parfumActive);
        parfumOff.setAlpha(parfumActive ? 1f : 0.4f);
        GradientDrawable offBg = new GradientDrawable();
        offBg.setColor(android.graphics.Color.TRANSPARENT);
        offBg.setCornerRadius(8 * dp);
        offBg.setStroke((int) dp, ContextCompat_getColor(R.color.status_off_red));
        parfumOff.setBackground(offBg);
        parfumOff.setTextColor(ContextCompat_getColor(R.color.status_off_red));
        LinearLayout.LayoutParams offLp = new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        offLp.setMargins(0, 0, (int) (6 * dp), 0);
        parfumOff.setLayoutParams(offLp);
        parfumOff.setOnClickListener(v -> {
            if (ctx instanceof MainActivity) {
                MainActivity main = (MainActivity) ctx;
                main.DATAs.sendParfumOff();
                main._Toast("{ PARFUM OFF }");
                main._Console(false, "►►", "PARFUM {#R}OFF{##}");
            }
            if (popup != null) popup.dismiss();
        });
        parfumActionRow.addView(parfumOff);

        TextView parfumStart = new TextView(ctx);
        parfumStart.setText("START");
        parfumStart.setAllCaps(true);
        parfumStart.setLetterSpacing(0.06f);
        parfumStart.setTypeface(null, Typeface.BOLD);
        parfumStart.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        parfumStart.setTextColor(ThemeManager.getColor(ctx, R.color.wheel_label_text));
        parfumStart.setGravity(Gravity.CENTER);
        parfumStart.setPadding(0, (int) (8 * dp), 0, (int) (8 * dp));
        parfumStart.setClickable(true);
        parfumStart.setFocusable(true);
        GradientDrawable startBg = new GradientDrawable();
        startBg.setColor(parfumViolet);
        startBg.setCornerRadius(8 * dp);
        parfumStart.setBackground(startBg);
        LinearLayout.LayoutParams startLp = new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        parfumStart.setLayoutParams(startLp);
        parfumStart.setOnClickListener(v -> {
            if (ctx instanceof MainActivity) {
                MainActivity main = (MainActivity) ctx;
                _saveLastParfumSettings(minutesHolder[0], modeHolder[0]);
                main.DATAs.sendParfum(minutesHolder[0], modeHolder[0]);
                String modeLbl = DataSend.PARFUM_MODES[modeHolder[0]];
                main._Toast("{ PARFUM " + minutesHolder[0] + " MIN · " + modeLbl + " }");
                main._Console(false, "►►", "PARFUM {#G}ON{##} [{#C}" + minutesHolder[0]
                        + " MIN{##}] {#C}" + modeLbl + "{##}");
            }
            if (popup != null) popup.dismiss();
        });
        parfumActionRow.addView(parfumStart);
        parfumPanel.addView(parfumActionRow);

        // Toggle: fade the panel in/out, flip the chevron, swap the
        // header's corner radii so it reads as one seamless card either way.
        parfumHeader.setOnClickListener(v -> {
            boolean nowExpanded = !expandedHolder[0];
            expandedHolder[0] = nowExpanded;
            parfumChevron.animate().rotation(nowExpanded ? 180f : 0f).setDuration(150).start();

            GradientDrawable headerBg2 = new GradientDrawable();
            headerBg2.setColor(parfumFill);
            float rr = 10 * dp;
            headerBg2.setCornerRadii(nowExpanded
                    ? new float[]{rr, rr, rr, rr, 0, 0, 0, 0}
                    : new float[]{rr, rr, rr, rr, rr, rr, rr, rr});
            headerBg2.setStroke((int) dp, parfumViolet);
            parfumHeader.setBackground(headerBg2);

            if (nowExpanded) {
                parfumPanel.setAlpha(0f);
                parfumPanel.setVisibility(View.VISIBLE);
                parfumPanel.animate().alpha(1f).setDuration(150).start();
            } else {
                parfumPanel.animate().alpha(0f).setDuration(120)
                        .withEndAction(() -> parfumPanel.setVisibility(View.GONE))
                        .start();
            }
        });
        // Seed the header's initial background (same corner-radius logic as the toggle above).
        GradientDrawable initialHeaderBg = new GradientDrawable();
        initialHeaderBg.setColor(parfumFill);
        float initialR = 10 * dp;
        initialHeaderBg.setCornerRadii(expandedHolder[0]
                ? new float[]{initialR, initialR, initialR, initialR, 0, 0, 0, 0}
                : new float[]{initialR, initialR, initialR, initialR, initialR, initialR, initialR, initialR});
        initialHeaderBg.setStroke((int) dp, parfumViolet);
        parfumHeader.setBackground(initialHeaderBg);

        body.addView(parfumGroup);

        // ── CLOSE ─────────────────────────────────────────────
        TextView closeBtn = new TextView(ctx);
        closeBtn.setText("CLOSE");
        closeBtn.setAllCaps(true);
        closeBtn.setLetterSpacing(0.06f);
        closeBtn.setTypeface(null, Typeface.BOLD);
        closeBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        closeBtn.setTextColor(titleCol);
        closeBtn.setGravity(Gravity.CENTER);
        closeBtn.setPadding((int) (12 * dp), (int) (6 * dp), (int) (12 * dp), (int) (6 * dp));
        closeBtn.setClickable(true);
        closeBtn.setFocusable(true);
        GradientDrawable closeBg = new GradientDrawable();
        closeBg.setColor(rowFill);
        closeBg.setCornerRadius(10 * dp);
        closeBg.setStroke((int) (1 * dp), strokeCol);
        closeBtn.setBackground(closeBg);
        LinearLayout.LayoutParams closeLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        closeLp.setMargins(0, (int) (10 * dp), 0, 0);
        closeBtn.setLayoutParams(closeLp);
        closeBtn.setOnClickListener(v -> { if (popup != null) popup.dismiss(); });
        body.addView(closeBtn);

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
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        try {
            popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);
        } catch (WindowManager.BadTokenException e) {
            return; // activity finished in the gap between the checks above and here
        }

        _dimBehind(popup);
        _animateIn(card);
    }

    // ========================================================
    //  Helpers
    // ========================================================

    /** Thin ContextCompat.getColor wrapper to keep the accent-color block above readable. */
    private int ContextCompat_getColor(int colorRes) {
        return androidx.core.content.ContextCompat.getColor(ctx, colorRes);
    }

    /**
     * Begin the 5-second hold-to-confirm fill on MARK AS REFILLED. Any
     * previous run is cancelled first (defensive - shouldn't normally
     * overlap since ACTION_DOWN only fires once per press).
     *
     * @param fill  The overlay view whose scaleX tracks hold progress (0-1).
     * @param btn   The button label - swapped to a countdown-style prompt
     *              while held, so the fill isn't the only feedback.
     */
    private void _startRefillHold(View fill, TextView btn) {
        if (refillHoldAnimator != null) refillHoldAnimator.cancel();

        fill.animate().cancel();
        fill.setScaleX(0f);
        btn.setText("KEEP HOLDING...");

        ValueAnimator anim = ValueAnimator.ofFloat(0f, 1f);
        anim.setDuration(REFILL_HOLD_MS);
        anim.setInterpolator(new LinearInterpolator());
        anim.addUpdateListener(a -> fill.setScaleX((float) a.getAnimatedValue()));
        anim.addListener(new AnimatorListenerAdapter() {
            private boolean cancelled = false;

            @Override
            public void onAnimationCancel(Animator animation) {
                cancelled = true;
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                if (cancelled) return;                    // Released early - _cancelRefillHold() already handled it
                refillHoldAnimator = null;
                if (ctx instanceof MainActivity) {
                    MainActivity main = (MainActivity) ctx;
                    main.DATAs.sendDiffuserManualRefill();
                    main._Toast("Diffuser marked as refilled");
                }
                if (popup != null) popup.dismiss();
            }
        });
        refillHoldAnimator = anim;
        anim.start();
    }

    /**
     * Release before the 5s hold completes - abort the pending refill and
     * snap the fill overlay back to empty.
     *
     * @param fill  The overlay view to reset.
     * @param btn   The button label to restore.
     */
    private void _cancelRefillHold(View fill, TextView btn) {
        if (refillHoldAnimator != null) {
            refillHoldAnimator.cancel();
            refillHoldAnimator = null;
        }
        btn.setText("HOLD TO FORCE REFILL");
        fill.animate().scaleX(0f).setDuration(150).start();
    }

    /**
     * Small square +/- stepper button for the Parfum minutes row - compact
     * inline-accordion sizing (28dp, shrunk down from an earlier 32dp so
     * the whole minus/value/plus trio takes up less room).
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
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setTextColor(ThemeManager.getColor(ctx, R.color.text_default));
        tv.setGravity(Gravity.CENTER);
        tv.setClickable(true);
        tv.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(rowFill);
        bg.setCornerRadius(6 * dp);
        bg.setStroke((int) dp, strokeCol);
        tv.setBackground(bg);
        int size = (int) (28 * dp);
        tv.setLayoutParams(new LinearLayout.LayoutParams(size, size));
        tv.setOnClickListener(v -> onTap.run());
        return tv;
    }

    /**
     * Clamp + commit a new Parfum minutes value and push it into both the
     * value display and the slider - mirrors ParfumPopup._setMinutes().
     *
     * @param v             Requested minutes (any int - clamped here).
     * @param minutesHolder Single-element mutable holder for the live value.
     * @param valueView     The minutes TextView to update.
     * @param slider        The 1-360 SeekBar to sync.
     */
    private void _setParfumMinutes(int v, int[] minutesHolder, TextView valueView, SeekBar slider) {
        minutesHolder[0] = Math.max(PARFUM_MIN_MINUTES, Math.min(PARFUM_MAX_MINUTES, v));
        valueView.setText(String.valueOf(minutesHolder[0]));
        slider.setProgress(minutesHolder[0] - PARFUM_MIN_MINUTES);
    }

    /**
     * Re-paints the CONTINUOUS/10 SEC mode chips so only the selected one
     * shows the accent fill - mirrors ParfumPopup._refreshModeButtons().
     *
     * @param chips       All mode chips, parallel to DataSend.PARFUM_MODES.
     * @param selectedIdx Currently-selected index.
     * @param accent      Selected fill/border/text colour.
     * @param rowFill     Unselected fill colour.
     * @param strokeCol   Unselected border colour.
     * @param titleCol    Unselected text colour.
     * @param dp          Display density factor.
     */
    private void _refreshParfumModeChips(TextView[] chips, int selectedIdx, int accent,
                                          int rowFill, int strokeCol, int titleCol, float dp) {
        for (int i = 0; i < chips.length; i++) {
            boolean sel = (i == selectedIdx);
            GradientDrawable bg = new GradientDrawable();
            bg.setCornerRadius(8 * dp);
            bg.setColor(sel ? accent : rowFill);
            bg.setStroke((int) dp, sel ? accent : strokeCol);
            chips[i].setBackground(bg);
            chips[i].setTextColor(sel ? ThemeManager.getColor(ctx, R.color.wheel_label_text) : titleCol);
        }
    }

    /**
     * Reads the last minutes/mode actually sent via START, persisted in
     * SharedPreferences (see PARFUM_PREFS_NAME) - falls back to
     * PARFUM_DEFAULT_MINUTES/PARFUM_DEFAULT_MODE the very first time this
     * has ever been opened.
     *
     * @return {minutes, mode}
     */
    private int[] _loadLastParfumSettings() {
        android.content.SharedPreferences sp =
                ctx.getSharedPreferences(PARFUM_PREFS_NAME, Context.MODE_PRIVATE);
        int minutes = sp.getInt(PARFUM_PREF_MINUTES, PARFUM_DEFAULT_MINUTES);
        int mode    = sp.getInt(PARFUM_PREF_MODE, PARFUM_DEFAULT_MODE);
        minutes = Math.max(PARFUM_MIN_MINUTES, Math.min(PARFUM_MAX_MINUTES, minutes));
        mode    = Math.max(0, Math.min(DataSend.PARFUM_MODES.length - 1, mode));
        return new int[]{minutes, mode};
    }

    /**
     * Persists the minutes/mode just sent via START, so the accordion
     * opens pre-filled with it next time (see _loadLastParfumSettings()).
     *
     * @param minutes  Minutes just sent.
     * @param mode     Mode index just sent.
     */
    private void _saveLastParfumSettings(int minutes, int mode) {
        ctx.getSharedPreferences(PARFUM_PREFS_NAME, Context.MODE_PRIVATE)
                .edit()
                .putInt(PARFUM_PREF_MINUTES, minutes)
                .putInt(PARFUM_PREF_MODE, mode)
                .apply();
    }

    /**
     * Format a minute count as "Xh Ym" (or just "Ym" under an hour).
     *
     * @param totalMin  Duration in minutes, >= 0.
     */
    private String _fmtDuration(int totalMin) {
        if (totalMin < 60) return totalMin + "m";
        int h = totalMin / 60;
        int m = totalMin % 60;
        return m == 0 ? (h + "h") : (h + "h " + m + "m");
    }

    /**
     * One label/value row inside the stats box.
     *
     * @param label     Left-aligned caption.
     * @param value     Right-aligned value.
     * @param titleCol  Text colour for both.
     * @param dp        Display density factor.
     */
    private LinearLayout _statRow(String label, String value, int titleCol, float dp) {
        return _statRow(label, value, titleCol, titleCol, dp);
    }

    /**
     * One label/value row inside the stats box, with the value colour
     * overridable independently of the label - used to tint "Since refill"
     * with the header's accent colour so the running-time number still
     * stands out within the box without needing a separate headline outside it.
     *
     * @param label     Left-aligned caption.
     * @param value     Right-aligned value.
     * @param valueCol  Text colour for the value only.
     * @param titleCol  Text colour for the label (and the value's fallback).
     * @param dp        Display density factor.
     */
    private LinearLayout _statRow(String label, String value, int valueCol, int titleCol, float dp) {
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        row.setPadding(0, (int) (3 * dp), 0, (int) (3 * dp));

        TextView labelTv = new TextView(ctx);
        labelTv.setText(label);
        labelTv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        labelTv.setTextColor(titleCol);
        labelTv.setAlpha(0.65f);
        labelTv.setLayoutParams(new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        row.addView(labelTv);

        TextView valueTv = new TextView(ctx);
        valueTv.setText(value);
        valueTv.setTypeface(null, Typeface.BOLD);
        valueTv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        valueTv.setTextColor(valueCol);
        valueTv.setGravity(Gravity.END);
        row.addView(valueTv);

        return row;
    }

    /**
     * Hairline divider between the estimate and the stats box.
     *
     * @param dp        Display density factor.
     * @param strokeCol Divider colour.
     */
    private View _buildDivider(float dp, int strokeCol) {
        View line = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (1 * dp));
        line.setLayoutParams(lp);
        line.setBackgroundColor(strokeCol);
        return line;
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
}
