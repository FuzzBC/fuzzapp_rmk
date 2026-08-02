package com.fuzz.colors;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ValueAnimator;
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
import android.widget.TextView;

/**
 * ============================================================
 *  DiffuserUsagePopup.java
 * ============================================================
 *  Long-press dialog for the top-left diffuser refill badge -
 *  purely informational (no inputs, unlike ParfumPopup): shows
 *  an estimated remaining time until the next refill, using the
 *  same rolling-average data already driving the badge's percent
 *  and fill bar (see StatusManager.applyDiffuserUsage()).
 *
 *  Layout (same card language as ParfumPopup - accent header bar
 *  on a rounded card, dimmed backdrop, centered, scale/fade entrance):
 *      [ DIFFUSER REFILL ]            <- accent header
 *      ~2h 15m REMAINING              <- big estimate line
 *      64% of average cycle left      <- secondary line
 *      ─────────────────────
 *      Since refill    1h 45m
 *      Average cycle   4h 05m
 *      History         3/10 cycles
 *      Lifetime        7 refills
 *      [        CLOSE      ]
 *
 *  Usage (see StatusManager.init()):
 *      new DiffuserUsagePopup(Main).show(anchor, diffuserRefillPercent,
 *              accumMin, avgMin, refillCount, totalRefills);
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
     * @param anchor       Any attached view - used only as the window token.
     * @param percent      Current badge percent (0-100), or -1 while still
     *                     learning (no refill history yet).
     * @param accumMin     Effective minutes run since the last refill.
     * @param avgMin       Rolling average of completed refill cycles, minutes.
     * @param refillCount  Valid entries in the rolling history, 0-10.
     * @param totalRefills Lifetime refill count.
     */
    public void show(View anchor, int percent, int accumMin, int avgMin, int refillCount, int totalRefills) {
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

        statsBox.addView(_statRow("Since refill", _fmtDuration(accumMin), titleCol, dp));
        statsBox.addView(_statRow("Average cycle", avgMin > 0 ? _fmtDuration(avgMin) : "-", titleCol, dp));
        statsBox.addView(_statRow("History", refillCount + "/10 cycles", titleCol, dp));
        statsBox.addView(_statRow("Lifetime", totalRefills + " refill" + (totalRefills == 1 ? "" : "s"), titleCol, dp));
        body.addView(statsBox);

        // ── MARK AS REFILLED (hold 5s to confirm) ────────────
        // Manual refill: tells the diffuser to bank the current usage cycle
        // into its refill history right now and reset the accumulator, for
        // when the user tops it off proactively rather than waiting for an
        // actual out-of-water drop (the only other thing that finalizes a
        // cycle). See UDPSend.sendDiffuserManualRefill() / diffuser "Dr".
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
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);

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
                    main.UDPs.sendDiffuserManualRefill();
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
        valueTv.setTextColor(titleCol);
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
