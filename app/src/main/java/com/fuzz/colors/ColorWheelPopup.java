package com.fuzz.colors;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.SweepGradient;
import android.graphics.drawable.ColorDrawable;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.DecelerateInterpolator;
import android.widget.PopupWindow;

import androidx.core.content.ContextCompat;

/**
 * Press-and-hold popup: hue ring + 2 draggable markers + split centre circle
 * (top half = SPIN, bottom half = SET). Outside tap closes silently.
 *
 * Usage (see LEDManager._openRandColorWheel()):
 *   new ColorWheelPopup(Main).show(anchorButton, c1, c2,
 *       (liveC1, liveC2)   -> { ... update button live ... },
 *       (finalC1, finalC2) -> { ... SET pressed, send + persist ... });
 */
public class ColorWheelPopup {

    public interface OnLiveChange { void onChange(int color1, int color2); }
    public interface OnClosed     { void onClosed(int color1, int color2); }

    private static final int WHEEL_SIZE_DP = 190;
    private static final int OVERLAP_DP    = 22; // overlaps anchor corner
    private static final int RAISE_DP      = 25; // extra upward offset

    private final Context ctx;
    private PopupWindow popup;
    private WheelView   wheelView;

    public ColorWheelPopup(Context ctx) { this.ctx = ctx; }

    /**
     * @param anchor The random button — wheel pops up at its top-right corner.
     * @param color1 Initial left colour.
     * @param color2 Initial right colour.
     * @param live   Fired continuously while dragging a marker or spinning.
     * @param closed Fired only when SET is tapped; outside tap closes silently.
     */
    public void show(View anchor, int color1, int color2, OnLiveChange live, OnClosed closed) {
        float dp      = ctx.getResources().getDisplayMetrics().density;
        int   sizePx  = (int) (WHEEL_SIZE_DP * dp);
        int   overlap = (int) (OVERLAP_DP    * dp);
        int   raise   = (int) (RAISE_DP      * dp);

        wheelView = new WheelView(ctx, color1, color2, live, () -> {
            if (popup != null) popup.dismiss(); // committed=true set before this
        });
        wheelView.setLayoutParams(new ViewGroup.LayoutParams(sizePx, sizePx));

        popup = new PopupWindow(wheelView, sizePx, sizePx, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setOnDismissListener(() -> {
            if (closed != null && wheelView.isCommitted())
                closed.onClosed(wheelView.getColor1(), wheelView.getColor2());
        });

        int[] loc = new int[2];
        anchor.getLocationOnScreen(loc);
        int x = loc[0] + anchor.getWidth() - overlap;
        int y = loc[1] - sizePx + overlap - raise;

        popup.showAtLocation(anchor, Gravity.NO_GRAVITY, x, y);
    }

    public void dismiss() {
        if (popup != null && popup.isShowing()) popup.dismiss();
    }

    // ================================================================
    //  Wheel view — ring + markers + split-circle (top=SPIN, bot=SET)
    // ================================================================
    private static class WheelView extends View {

        private final float[] hsv1 = {40f,  0.85f, 0.9f};
        private final float[] hsv2 = {220f, 0.85f, 0.9f};
        private final OnLiveChange live;
        private final Runnable     onSet;

        private final Paint ringPaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint holePaint    = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint spinPaint    = new Paint(Paint.ANTI_ALIAS_FLAG); // top half
        private final Paint setPaint     = new Paint(Paint.ANTI_ALIAS_FLAG); // bottom half
        private final Paint dividerPaint = new Paint(Paint.ANTI_ALIAS_FLAG); // divider line
        private final Paint spinTxtPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint setTxtPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint markerFill   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint markerBorder = new Paint(Paint.ANTI_ALIAS_FLAG);

        private float cx, cy, radius, markerRadius, holeRadius, spinRadius;

        private int     draggingMarker  = 0;
        private boolean spinDownValid   = false;
        private boolean setDownValid    = false;
        private boolean committed       = false; // true only when SET tapped

        private ValueAnimator spinAnim;

        WheelView(Context ctx, int color1, int color2, OnLiveChange live, Runnable onSet) {
            super(ctx);
            this.live  = live;
            this.onSet = onSet;

            float[] tmp = new float[3];
            Color.colorToHSV(color1, tmp); hsv1[0] = tmp[0];
            Color.colorToHSV(color2, tmp); hsv2[0] = tmp[0];

            holePaint.setColor(ThemeManager.getColor(ctx, R.color.skbar_whole_settings));
            spinPaint.setColor(ThemeManager.getColor(ctx, R.color.skbar_settings_title)); // copper — SPIN top, theme-aware
            setPaint.setColor(ContextCompat.getColor(ctx, R.color.wheel_set_color));        // green — SET bottom
            dividerPaint.setColor(ContextCompat.getColor(ctx, R.color.wheel_divider_color));
            dividerPaint.setStrokeWidth(2f);

            spinTxtPaint.setColor(ContextCompat.getColor(ctx, R.color.wheel_label_text));
            spinTxtPaint.setTextAlign(Paint.Align.CENTER);
            spinTxtPaint.setFakeBoldText(true);

            setTxtPaint.setColor(ContextCompat.getColor(ctx, R.color.wheel_label_text));
            setTxtPaint.setTextAlign(Paint.Align.CENTER);
            setTxtPaint.setFakeBoldText(true);

            markerFill.setStyle(Paint.Style.FILL);
            markerBorder.setStyle(Paint.Style.FILL);
            markerBorder.setColor(ContextCompat.getColor(ctx, R.color.wheel_divider_color));
        }

        int     getColor1()   { return Color.HSVToColor(hsv1); }
        int     getColor2()   { return Color.HSVToColor(hsv2); }
        boolean isCommitted() { return committed; }

        @Override
        protected void onSizeChanged(int w, int h, int oldw, int oldh) {
            super.onSizeChanged(w, h, oldw, oldh);
            cx = w / 2f;
            cy = h / 2f;
            radius       = Math.min(w, h) / 2f - 4f;
            holeRadius   = radius * 0.62f;
            spinRadius   = holeRadius * 0.85f;
            markerRadius = radius * 0.105f;

            int[] colors = new int[37];
            for (int i = 0; i <= 36; i++)
                colors[i] = Color.HSVToColor(new float[]{i * 10f, 0.85f, 0.95f});
            ringPaint.setShader(new SweepGradient(cx, cy, colors, null));

            float txtSize = spinRadius * 0.42f;
            spinTxtPaint.setTextSize(txtSize);
            setTxtPaint.setTextSize(txtSize);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            // ring + hole base
            canvas.drawCircle(cx, cy, radius, ringPaint);
            canvas.drawCircle(cx, cy, holeRadius, holePaint);

            // split centre circle — clip top/bottom halves
            RectF bounds = new RectF(cx - spinRadius, cy - spinRadius,
                                     cx + spinRadius, cy + spinRadius);

            // top half — SPIN
            Path topHalf = new Path();
            topHalf.addArc(bounds, 180f, 180f); // 180→360 = top arc
            topHalf.lineTo(cx, cy);
            topHalf.close();
            canvas.drawPath(topHalf, spinPaint);

            // bottom half — SET
            Path botHalf = new Path();
            botHalf.addArc(bounds, 0f, 180f);   // 0→180 = bottom arc
            botHalf.lineTo(cx, cy);
            botHalf.close();
            canvas.drawPath(botHalf, setPaint);

            // divider line across the middle
            canvas.drawLine(cx - spinRadius, cy, cx + spinRadius, cy, dividerPaint);

            // labels — SPIN centred in top half, SET in bottom half
            float q = spinRadius * 0.38f; // quarter offset from centre
            canvas.drawText("SPIN", cx, cy - q + spinTxtPaint.getTextSize() * 0.35f, spinTxtPaint);
            canvas.drawText("SET",  cx, cy + q + setTxtPaint.getTextSize()  * 0.35f, setTxtPaint);

            drawMarker(canvas, hsv1[0]);
            drawMarker(canvas, hsv2[0]);
        }

        private void drawMarker(Canvas canvas, float angleDeg) {
            float[] p = markerPos(angleDeg);
            markerFill.setColor(Color.HSVToColor(new float[]{angleDeg, 0.85f, 0.9f}));
            canvas.drawCircle(p[0], p[1], markerRadius + 3f, markerBorder);
            canvas.drawCircle(p[0], p[1], markerRadius, markerFill);
        }

        private float[] markerPos(float angleDeg) {
            double rad = Math.toRadians(angleDeg);
            float  mr  = (radius + holeRadius) / 2f;
            return new float[]{
                cx + (float) (Math.cos(rad) * mr),
                cy + (float) (Math.sin(rad) * mr)};
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            float x = event.getX(), y = event.getY();
            float distCenter = (float) Math.hypot(x - cx, y - cy);

            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: {
                    float d1  = distToMarker(x, y, hsv1[0]);
                    float d2  = distToMarker(x, y, hsv2[0]);
                    float hit = markerRadius * 2.2f;

                    if (distCenter <= spinRadius) {
                        if (y < cy) spinDownValid = true; // top half → SPIN
                        else        setDownValid  = true; // bottom half → SET
                    } else if (d1 <= hit && d1 <= d2) {
                        draggingMarker = 1;
                    } else if (d2 <= hit) {
                        draggingMarker = 2;
                    }
                    return true;
                }
                case MotionEvent.ACTION_MOVE: {
                    if (draggingMarker != 0) {
                        float angle = norm((float) Math.toDegrees(Math.atan2(y - cy, x - cx)));
                        if (draggingMarker == 1) hsv1[0] = angle; else hsv2[0] = angle;
                        invalidate();
                        notifyLive();
                    }
                    return true;
                }
                case MotionEvent.ACTION_UP: {
                    if (spinDownValid && distCenter <= spinRadius && y < cy) {
                        spin();
                    } else if (setDownValid && distCenter <= spinRadius && y >= cy) {
                        committed = true;
                        if (onSet != null) onSet.run();
                    }
                    spinDownValid  = false;
                    setDownValid   = false;
                    draggingMarker = 0;
                    return true;
                }
            }
            return super.onTouchEvent(event);
        }

        private float distToMarker(float x, float y, float angleDeg) {
            float[] p = markerPos(angleDeg);
            return (float) Math.hypot(x - p[0], y - p[1]);
        }

        private float norm(float deg) {
            float m = deg % 360f;
            return m < 0 ? m + 360f : m;
        }

        private void spin() {
            if (spinAnim != null) spinAnim.cancel();
            final float startA1  = hsv1[0], startA2  = hsv2[0];
            final float targetA1 = (float) (Math.random() * 360) + 720f;
            final float targetA2 = (float) (Math.random() * 360) + 720f;

            spinAnim = ValueAnimator.ofFloat(0f, 1f);
            spinAnim.setDuration(800);
            spinAnim.setInterpolator(new DecelerateInterpolator(1.6f));
            spinAnim.addUpdateListener(a -> {
                float t = (float) a.getAnimatedValue();
                hsv1[0] = norm(startA1 + t * (targetA1 - startA1));
                hsv2[0] = norm(startA2 + t * (targetA2 - startA2));
                invalidate();
                notifyLive();
            });
            spinAnim.start();
        }

        private void notifyLive() {
            if (live != null) live.onChange(getColor1(), getColor2());
        }
    }
}
