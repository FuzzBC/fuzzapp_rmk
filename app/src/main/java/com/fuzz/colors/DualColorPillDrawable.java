package com.fuzz.colors;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.drawable.Drawable;

/**
 * Rounded-pill background split into two solid halves (left/right) - the
 * DUAL COLOR button's own fill literally reads as "1 colour left, 1 right",
 * same idea as DualColorDrawable but for a normal rectangular text button
 * instead of a circular swatch (DualColorDrawable draws an oval that only
 * fills part of a wide rect - wrong shape here).
 *
 * Built by drawing the SAME full rounded-rect path twice, clipped to the
 * left half then the right half in turn - simpler than hand-building two
 * asymmetric rounded paths, and the single continuous stroke drawn last
 * (unclipped) keeps one unbroken border around the whole pill.
 *
 * Colours are meant to be driven by a slow ValueAnimator that continuously
 * drifts to a fresh random pair - see MainActivity._startDualColorButtonCycle().
 */
public class DualColorPillDrawable extends Drawable {

    private int color1;
    private int color2;

    private float strokeWidthPx = 0f;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF bounds = new RectF();

    public DualColorPillDrawable(int color1, int color2) {
        this.color1 = color1;
        this.color2 = color2;
        strokePaint.setStyle(Paint.Style.STROKE);
    }

    /** Themed border, same convention as DualColorDrawable.setStroke(). */
    public void setStroke(float widthPx, int color) {
        strokeWidthPx = widthPx;
        strokePaint.setStrokeWidth(widthPx);
        strokePaint.setColor(color);
        invalidateSelf();
    }

    /** Sets both halves at once and redraws - called every animation frame. */
    public void setColors(int color1, int color2) {
        this.color1 = color1;
        this.color2 = color2;
        invalidateSelf();
    }

    @Override
    protected void onBoundsChange(Rect b) {
        super.onBoundsChange(b);
        bounds.set(b);
    }

    @Override
    public void draw(Canvas canvas) {
        if (bounds.isEmpty()) return;

        float r    = bounds.height() / 2f; // fully rounded pill (radius = half height)
        float midX = bounds.centerX();

        canvas.save();
        canvas.clipRect(bounds.left, bounds.top, midX, bounds.bottom);
        paint.setColor(color1);
        canvas.drawRoundRect(bounds, r, r, paint);
        canvas.restore();

        canvas.save();
        canvas.clipRect(midX, bounds.top, bounds.right, bounds.bottom);
        paint.setColor(color2);
        canvas.drawRoundRect(bounds, r, r, paint);
        canvas.restore();

        if (strokeWidthPx > 0f) {
            RectF strokeBounds = new RectF(bounds);
            strokeBounds.inset(strokeWidthPx / 2f, strokeWidthPx / 2f);
            float strokeR = strokeBounds.height() / 2f;
            canvas.drawRoundRect(strokeBounds, strokeR, strokeR, strokePaint);
        }
    }

    @Override
    public void setAlpha(int alpha) {
        paint.setAlpha(alpha);
        invalidateSelf();
    }

    @Override
    public void setColorFilter(ColorFilter colorFilter) {
        paint.setColorFilter(colorFilter);
        invalidateSelf();
    }

    @Override
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
