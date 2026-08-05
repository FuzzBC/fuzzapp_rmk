package com.fuzz.colors;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.DashPathEffect;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

/**
 * Rounded-pill background for the "selected" nav button: a light tinted
 * fill plus a dashed accent stroke whose phase is driven live, so the
 * dashes appear to endlessly march/run around the pill's perimeter -
 * "marching ants", same idea as a selection marquee in design tools.
 *
 * The dash+gap length is fixed at construction; a shared ValueAnimator
 * elsewhere (see MainActivity._startNavStrokeRun()) advances dashPhase by
 * exactly one full (dash+gap) cycle per loop, INFINITE repeat, so the
 * motion is seamless - phase 0 and phase (dash+gap) look identical, no
 * visible restart "pop".
 */
public class RunningStrokeDrawable extends Drawable {

    private final float dashLenPx;
    private final float gapLenPx;

    private int fillColor;
    private int strokeColor;
    private float dashPhase = 0f;

    private final Paint fillPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF bounds = new RectF();

    public RunningStrokeDrawable(int fillColor, int strokeColor, float strokeWidthPx, float dashLenPx, float gapLenPx) {
        this.fillColor = fillColor;
        this.strokeColor = strokeColor;
        this.dashLenPx = dashLenPx;
        this.gapLenPx = gapLenPx;

        fillPaint.setStyle(Paint.Style.FILL);
        strokePaint.setStyle(Paint.Style.STROKE);
        strokePaint.setStrokeWidth(strokeWidthPx);
        strokePaint.setColor(strokeColor);
        strokePaint.setStrokeCap(Paint.Cap.ROUND);
    }

    /** Full length (dp-in-px) of one dash+gap cycle - callers step dashPhase through exactly this range per loop. */
    public float getCycleLengthPx() {
        return dashLenPx + gapLenPx;
    }

    /** Re-tints both fill and stroke - call on theme switch. */
    public void setColors(int fillColor, int strokeColor) {
        this.fillColor = fillColor;
        this.strokeColor = strokeColor;
        strokePaint.setColor(strokeColor);
        invalidateSelf();
    }

    /** Advances the marching-dash animation - called every frame by the shared driver animator. */
    public void setDashPhase(float phase) {
        this.dashPhase = phase;
        invalidateSelf();
    }

    @Override
    protected void onBoundsChange(Rect b) {
        super.onBoundsChange(b);
        bounds.set(b);
        // Inset by half the stroke width so the dashed border is drawn
        // fully inside the bounds, same convention as every other custom
        // stroke drawable in this app (DualColorDrawable, DualColorPillDrawable).
        bounds.inset(strokePaint.getStrokeWidth() / 2f, strokePaint.getStrokeWidth() / 2f);
    }

    @Override
    public void draw(Canvas canvas) {
        if (bounds.isEmpty()) return;
        float r = bounds.height() / 2f;

        fillPaint.setColor(fillColor);
        canvas.drawRoundRect(bounds, r, r, fillPaint);

        strokePaint.setPathEffect(new DashPathEffect(new float[]{dashLenPx, gapLenPx}, dashPhase));
        canvas.drawRoundRect(bounds, r, r, strokePaint);
    }

    @Override
    public void setAlpha(int alpha) {
        fillPaint.setAlpha(alpha);
        strokePaint.setAlpha(alpha);
        invalidateSelf();
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        fillPaint.setColorFilter(colorFilter);
        strokePaint.setColorFilter(colorFilter);
        invalidateSelf();
    }

    @Override
    @SuppressWarnings("deprecation") // still abstract on Drawable - every subclass must implement it, no replacement exists
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
