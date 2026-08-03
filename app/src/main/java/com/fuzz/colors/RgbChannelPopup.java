package com.fuzz.colors;

/*
 * ============================================================
 *  RgbChannelPopup.java
 * ============================================================
 *  Press-to-open horizontal slider popup for a single RGB
 *  channel (R, G or B). Always anchored to the RGB row itself
 *  (_rgbS) so it opens in the same fixed spot above the row
 *  regardless of which of the three fields was tapped.
 *
 *  Drag/tap live-updates the 0-255 value on a glass-card slider
 *  with a soft drop shadow, gradient track and a breathing-pulse
 *  thumb, plus a live mixed-colour preview swatch. It NEVER sends
 *  anything over UDP - it only reports the value back to the
 *  caller so it can be written into the pending custom-RGB
 *  fields; the physical SEND button (_btn_sendcolor) still owns
 *  the actual DATAs.sendColor() call.
 *
 *  Usage (see LEDManager._setupRgbInputs()):
 *      new RgbChannelPopup(Main).show(LED_ROW_RgbInputs, _R, currentRgb,
 *          value -> LED_EDIT_CustomRGB[_R].setText(String.valueOf(value)));
 * ============================================================
 */

import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.OvershootInterpolator;
import android.widget.PopupWindow;

import androidx.core.content.ContextCompat;

public class RgbChannelPopup {

    public interface OnLiveChange { void onChange(int value); }

    // Visible card size - the popup itself is larger to leave bleed room for
    // the drawn drop shadow (see MARGIN_DP), so the card doesn't get clipped.
    private static final int CARD_WIDTH_DP  = 236;
    private static final int CARD_HEIGHT_DP = 100;
    private static final int MARGIN_DP      = 12; // shadow bleed on every side
    private static final int SHADOW_DROP_DP = 6;  // extra bleed below (drop direction)
    private static final int RAISE_DP       = 10; // gap kept between card and anchor
    private static final int SCREEN_MARGIN_DP = 8; // min gap to screen edges

    private final Context ctx;
    private PopupWindow popup;
    private SliderView  sliderView;

    public RgbChannelPopup(Context ctx) { this.ctx = ctx; }

    /**
     * @param anchor     Fixed anchor (the RGB row container) - same spot for R/G/B.
     * @param channel    LEDManager._R / _G / _B (0/1/2).
     * @param currentRgb Snapshot {r,g,b} of the pending custom colour at open time.
     * @param live       Fired continuously while dragging - value only, never sends.
     */
    public void show(View anchor, int channel, int[] currentRgb, OnLiveChange live) {
        float dp      = ctx.getResources().getDisplayMetrics().density;
        int   wPx     = (int) ((CARD_WIDTH_DP  + 2 * MARGIN_DP) * dp);
        int   hPx     = (int) ((CARD_HEIGHT_DP + 2 * MARGIN_DP + SHADOW_DROP_DP) * dp);
        int   marginPx = (int) (MARGIN_DP * dp);
        int   rise     = (int) (RAISE_DP  * dp);
        int   screenMargin = (int) (SCREEN_MARGIN_DP * dp);
        int   screenW  = ctx.getResources().getDisplayMetrics().widthPixels;

        sliderView = new SliderView(ctx, channel, currentRgb, live);
        sliderView.setLayoutParams(new ViewGroup.LayoutParams(wPx, hPx));

        popup = new PopupWindow(sliderView, wPx, hPx, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));

        int[] loc = new int[2];
        anchor.getLocationOnScreen(loc);
        int x = loc[0] + anchor.getWidth() / 2 - wPx / 2;
        x = Math.max(screenMargin, Math.min(x, screenW - wPx - screenMargin));
        int y = loc[1] - (hPx - marginPx) - rise;

        popup.showAtLocation(anchor, Gravity.NO_GRAVITY, x, y);

        // Pop-in: scale up from the anchor edge + fade, with a light overshoot bounce.
        sliderView.setAlpha(0f);
        sliderView.setScaleX(0.82f);
        sliderView.setScaleY(0.82f);
        sliderView.setPivotX(loc[0] + anchor.getWidth() / 2f - x);
        sliderView.setPivotY(hPx - marginPx * 0.6f);
        sliderView.animate()
                .alpha(1f).scaleX(1f).scaleY(1f)
                .setDuration(220)
                .setInterpolator(new OvershootInterpolator(1.7f))
                .start();
    }

    public void dismiss() {
        if (popup != null && popup.isShowing()) popup.dismiss();
    }

    // ================================================================
    //  Slider view - soft shadow, gradient glass card, slim track
    // ================================================================
    private static class SliderView extends View {

        private final int   channel;
        private final int[] rgb;  // live mixed colour; [channel] tracks this slider
        private final int   accentColor;
        private final int   darkAccent;
        private final OnLiveChange live;
        private final float dp;

        private final Paint shadowPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint panelPaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint panelStroke   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint badgeFill     = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint badgeStroke   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint trackBgPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fillPaint     = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint glowPaint     = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint thumbPaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint thumbBorder   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint labelPaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint valuePaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint swatchPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint swatchBorder  = new Paint(Paint.ANTI_ALIAS_FLAG);

        private float value; // 0-255, float for smooth drag

        // Card rect (inset from the full view by the shadow margin).
        private float cardLeft, cardTop, cardRight, cardBottom, panelCorner;

        private float badgeCx, badgeCy, badgeRadius;
        private float swatchCx, swatchCy, swatchRadius;
        private float valueCx, valueBaseline;
        private float trackLeft, trackRight, trackY, trackRadius;
        private float thumbRadius;
        private float sliderZoneTop; // generous tap band start (y)

        private boolean dragging   = false;
        private float   thumbScale = 1f;  // animated press-bounce
        private float   breathT    = 0f;  // animated ambient glow 0..1
        private ValueAnimator breathAnim;
        private ValueAnimator scaleAnim;

        SliderView(Context ctx, int channel, int[] rgbSnapshot, OnLiveChange live) {
            super(ctx);
            this.channel = channel;
            this.rgb     = new int[]{rgbSnapshot[0], rgbSnapshot[1], rgbSnapshot[2]};
            this.live    = live;
            this.value   = clamp(rgb[channel]);
            this.dp      = ctx.getResources().getDisplayMetrics().density;

            int colorRes = channel == 0 ? R.color.rgb_pure_red
                         : channel == 1 ? R.color.rgb_pure_green
                                        : R.color.rgb_pure_blue;
            accentColor = ContextCompat.getColor(ctx, colorRes);

            float[] hsv = new float[3];
            Color.colorToHSV(accentColor, hsv);
            hsv[2] = 0.24f;
            darkAccent = Color.HSVToColor(hsv);

            panelStroke.setStyle(Paint.Style.STROKE);
            panelStroke.setStrokeWidth(1.4f * dp);
            panelStroke.setColor(accentColor);
            panelStroke.setAlpha(75);

            badgeFill.setColor(accentColor);
            badgeFill.setAlpha(40);
            badgeStroke.setStyle(Paint.Style.STROKE);
            badgeStroke.setStrokeWidth(1.6f * dp);
            badgeStroke.setColor(accentColor);

            trackBgPaint.setColor(ContextCompat.getColor(ctx, R.color.skbar_track_color));

            thumbBorder.setColor(Color.WHITE);
            thumbBorder.setAlpha(235);

            labelPaint.setColor(accentColor);
            labelPaint.setTextAlign(Paint.Align.CENTER);
            labelPaint.setFakeBoldText(true);
            labelPaint.setShadowLayer(7f * dp, 0, 0, accentColor);

            valuePaint.setColor(Color.WHITE);
            valuePaint.setTextAlign(Paint.Align.CENTER);
            valuePaint.setFakeBoldText(true);

            swatchBorder.setStyle(Paint.Style.STROKE);
            swatchBorder.setStrokeWidth(1.5f * dp);
            swatchBorder.setColor(ThemeManager.getColor(ctx, R.color.stroke_line_color));

            setLayerType(LAYER_TYPE_SOFTWARE, null); // shadow layers need software rendering
            _startBreathing();
        }

        // ------------------------------------------------------------
        // Layout - card inset from the view by MARGIN_DP for shadow bleed
        // ------------------------------------------------------------
        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            super.onSizeChanged(w, h, oldw, oldh);

            float margin = 12f * dp;
            cardLeft   = margin;
            cardTop    = margin;
            cardRight  = w - margin;
            cardBottom = h - margin - 6f * dp; // extra room for the downward shadow drop
            float cardW = cardRight - cardLeft;
            float cardH = cardBottom - cardTop;
            panelCorner = cardH * 0.26f;

            float pad = cardW * 0.055f;

            badgeRadius = cardH * 0.185f;
            badgeCx = cardLeft + pad + badgeRadius;
            badgeCy = cardTop + cardH * 0.30f;
            labelPaint.setTextSize(badgeRadius * 1.15f);

            swatchRadius = badgeRadius * 0.82f;
            swatchCx = cardRight - pad - swatchRadius;
            swatchCy = badgeCy;

            valueCx = cardLeft + cardW / 2f;
            valuePaint.setTextSize(cardH * 0.30f);
            valueBaseline = badgeCy + valuePaint.getTextSize() * 0.36f;

            float trackHeight = cardH * 0.11f;
            trackY      = cardTop + cardH * 0.70f;
            trackRadius = trackHeight / 2f;
            thumbRadius = cardH * 0.155f;
            float glowRadius = thumbRadius * 1.5f;
            float trackInset = Math.max(pad, glowRadius + 3f * dp);
            trackLeft  = cardLeft + trackInset;
            trackRight = cardRight - trackInset;

            sliderZoneTop = cardTop + cardH * 0.5f;

            // Soft glass gradient - a touch lighter at the top, settling to the base tone.
            int baseBg = ThemeManager.getColor(getContext(), R.color.skbar_whole_settings);
            panelPaint.setShader(new LinearGradient(
                    0, cardTop, 0, cardBottom, _lighten(baseBg, 0.07f), baseBg, Shader.TileMode.CLAMP));

            fillPaint.setShader(new LinearGradient(
                    trackLeft, 0, trackRight, 0, darkAccent, accentColor, Shader.TileMode.CLAMP));
        }

        private static int _lighten(int color, float amt) {
            float[] hsv = new float[3];
            Color.colorToHSV(color, hsv);
            hsv[2] = Math.min(1f, hsv[2] + amt);
            return Color.HSVToColor(Color.alpha(color), hsv);
        }

        // ------------------------------------------------------------
        // Drawing
        // ------------------------------------------------------------
        @Override
        protected void onDraw(Canvas canvas) {
            _drawShadow(canvas);

            canvas.drawRoundRect(cardLeft, cardTop, cardRight, cardBottom,
                    panelCorner, panelCorner, panelPaint);
            canvas.drawRoundRect(cardLeft + 0.75f, cardTop + 0.75f, cardRight - 0.75f, cardBottom - 0.75f,
                    panelCorner, panelCorner, panelStroke);

            // Channel badge (soft ring chip).
            canvas.drawCircle(badgeCx, badgeCy, badgeRadius, badgeFill);
            canvas.drawCircle(badgeCx, badgeCy, badgeRadius, badgeStroke);
            String label = channel == 0 ? "R" : channel == 1 ? "G" : "B";
            canvas.drawText(label, badgeCx, badgeCy + labelPaint.getTextSize() * 0.36f, labelPaint);

            // Live value readout, centred.
            canvas.drawText(String.valueOf((int) value), valueCx, valueBaseline, valuePaint);

            // Mixed-colour live preview swatch.
            swatchPaint.setColor(Color.rgb(rgb[0], rgb[1], rgb[2]));
            canvas.drawCircle(swatchCx, swatchCy, swatchRadius, swatchPaint);
            canvas.drawCircle(swatchCx, swatchCy, swatchRadius, swatchBorder);

            // Slim track background + gradient fill up to the thumb.
            canvas.drawRoundRect(trackLeft, trackY - trackRadius, trackRight, trackY + trackRadius,
                    trackRadius, trackRadius, trackBgPaint);
            float thumbX = thumbX();
            canvas.save();
            canvas.clipRect(trackLeft, trackY - trackRadius, thumbX, trackY + trackRadius);
            canvas.drawRoundRect(trackLeft, trackY - trackRadius, trackRight, trackY + trackRadius,
                    trackRadius, trackRadius, fillPaint);
            canvas.restore();

            // Single soft breathing glow behind the thumb.
            glowPaint.setColor(accentColor);
            glowPaint.setAlpha((int) (breathT * 70));
            canvas.drawCircle(thumbX, trackY, thumbRadius * 1.5f * thumbScale, glowPaint);

            // Thumb, coloured by intensity (dark -> full accent as value rises).
            float t = value / 255f;
            int thumbColor = (int) new ArgbEvaluator().evaluate(t, darkAccent, accentColor);
            thumbPaint.setColor(thumbColor);
            canvas.drawCircle(thumbX, trackY, thumbRadius * thumbScale + 2.5f, thumbBorder);
            canvas.drawCircle(thumbX, trackY, thumbRadius * thumbScale, thumbPaint);
        }

        /** Layered, decreasing-alpha rounded rects bled into the view's outer margin. */
        private void _drawShadow(Canvas canvas) {
            int layers = 5;
            for (int i = 0; i < layers; i++) {
                float t = (layers - i) / (float) layers;      // 1f (outer) -> ~0.2f (inner)
                float spread = t * 10f * dp;
                float dy = t * 5f * dp;                        // shadow falls downward
                int alpha = (int) (10 + (1f - t) * 40);
                shadowPaint.setColor(Color.BLACK);
                shadowPaint.setAlpha(alpha);
                canvas.drawRoundRect(
                        cardLeft - spread, cardTop - spread * 0.5f + dy,
                        cardRight + spread, cardBottom + spread * 0.5f + dy,
                        panelCorner + spread * 0.4f, panelCorner + spread * 0.4f, shadowPaint);
            }
        }

        private float thumbX() {
            float p = value / 255f;
            return trackLeft + p * (trackRight - trackLeft);
        }

        // ------------------------------------------------------------
        // Touch - tap-to-jump + drag, anywhere along the slider row
        // ------------------------------------------------------------
        @Override
        public boolean onTouchEvent(MotionEvent event) {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: {
                    if (event.getY() >= sliderZoneTop) {
                        dragging = true;
                        performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                        _animateThumbScale(1.2f);
                        _setValueFromX(event.getX());
                    }
                    return true;
                }
                case MotionEvent.ACTION_MOVE: {
                    if (dragging) _setValueFromX(event.getX());
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL: {
                    if (dragging) {
                        dragging = false;
                        _animateThumbScale(1f);
                        performClick();
                    }
                    return true;
                }
            }
            return super.onTouchEvent(event);
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }

        private void _setValueFromX(float x) {
            float clampedX = Math.max(trackLeft, Math.min(trackRight, x));
            value = 255f * (clampedX - trackLeft) / (trackRight - trackLeft);
            rgb[channel] = (int) value;
            invalidate();
            if (live != null) live.onChange((int) value);
        }

        private static float clamp(int v) { return Math.max(0, Math.min(255, v)); }

        // ------------------------------------------------------------
        // Micro-animations
        // ------------------------------------------------------------
        private void _animateThumbScale(float target) {
            if (scaleAnim != null) scaleAnim.cancel();
            scaleAnim = ValueAnimator.ofFloat(thumbScale, target);
            scaleAnim.setDuration(150);
            scaleAnim.setInterpolator(new OvershootInterpolator(2.5f));
            scaleAnim.addUpdateListener(a -> {
                thumbScale = (float) a.getAnimatedValue();
                invalidate();
            });
            scaleAnim.start();
        }

        private void _startBreathing() {
            breathAnim = ValueAnimator.ofFloat(0.35f, 1f);
            breathAnim.setDuration(900);
            breathAnim.setRepeatCount(ValueAnimator.INFINITE);
            breathAnim.setRepeatMode(ValueAnimator.REVERSE);
            breathAnim.addUpdateListener(a -> {
                breathT = (float) a.getAnimatedValue();
                invalidate();
            });
            breathAnim.start();
        }

        @Override
        protected void onDetachedFromWindow() {
            super.onDetachedFromWindow();
            if (breathAnim != null) breathAnim.cancel();
            if (scaleAnim  != null) scaleAnim.cancel();
        }
    }
}
