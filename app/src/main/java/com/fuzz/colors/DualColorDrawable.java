package com.fuzz.colors;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;

/**
 * Circular dual-colour swatch with a HARD split at an arbitrary angle.
 *
 * Why this exists instead of the old <shape><gradient> / GradientDrawable
 * approach (R.drawable._btn_dualcolor): both android:angle in XML and
 * GradientDrawable.Orientation are restricted to multiples of 45° (0/45/
 * 90/.../315). A 15° split is not one of those, so it has to be built by
 * hand with a Paint + LinearGradient Shader instead, which accepts any
 * angle.
 *
 * Drop-in usage, same shape as the old pattern:
 *   DualColorDrawable dr = new DualColorDrawable(15f);
 *   button.setBackground(dr);
 *   dr.setColors(c1, c2);   // instead of dr.setColors(new int[]{c1,c1,c1,c1,c2,c2,c2,c2})
 */
public class DualColorDrawable extends Drawable {

    /** Split angle in degrees. 0 = left/right, 90 = top/bottom. */
    private float angleDeg;

    /**
     * When true, draws two separate overlapping circles (venn pair)
     * instead of one circle with a hard split. Used for the random
     * button only - the venn pair is what makes it visually distinct
     * from every solid saved-colour circle in the row, no label needed.
     */
    private boolean vennMode = false;

    private int color1 = 0xFF6B5F52; // warm grey placeholder — overwritten by setColors() before first draw
    private int color2 = 0xFF8C8073; // warm grey placeholder — overwritten by setColors() before first draw

    /**
     * Themed border, mirroring the 2dp stroke every other LED/colour
     * button has via GradientDrawable's android:stroke. GradientDrawable
     * can't be reused here (see class doc - arbitrary split angle), so
     * the same outline has to be painted by hand.
     */
    private float strokeWidthPx = 0f;
    private int   strokeColor   = 0x00000000;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint strokePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF ovalBounds = new RectF();

    public DualColorDrawable(float angleDeg) {
        this.angleDeg = angleDeg;
        strokePaint.setStyle(Paint.Style.STROKE);
    }

    /**
     * Sets the themed border colour/width. Call again after every theme
     * switch (ThemeManager.getColor() result changes) - width is in
     * pixels, e.g. 2 * density for a 2dp border matching _btn_led/etc.
     */
    public void setStroke(float widthPx, int color) {
        this.strokeWidthPx = widthPx;
        this.strokeColor = color;
        strokePaint.setStrokeWidth(widthPx);
        strokePaint.setColor(color);
        invalidateSelf();
    }

    /** Sets both halves of the split at once and redraws. */
    public void setColors(int color1, int color2) {
        this.color1 = color1;
        this.color2 = color2;
        _rebuildShader();
        invalidateSelf();
    }

    /** Switches between split-circle (false) and venn-pair (true) rendering. */
    public void setVennMode(boolean vennMode) {
        this.vennMode = vennMode;
        invalidateSelf();
    }

    @Override
    protected void onBoundsChange(Rect bounds) {
        super.onBoundsChange(bounds);
        ovalBounds.set(bounds);
        _rebuildShader();
    }

    /**
     * Builds a LinearGradient running ALONG angleDeg, with both colours
     * stacked right at the midpoint (0.4999 / 0.5001) instead of blending -
     * that's what turns a smooth gradient into a hard edge, same visual
     * result the old 8-stop GradientDrawable trick gave, just at any angle.
     */
    private void _rebuildShader() {
        if (ovalBounds.isEmpty()) return;

        float cx = ovalBounds.centerX();
        float cy = ovalBounds.centerY();
        // Half-diagonal: long enough that the split line always crosses
        // the whole circle, regardless of angle.
        float r = (float) Math.hypot(ovalBounds.width(), ovalBounds.height()) / 2f;

        double rad = Math.toRadians(angleDeg);
        float dx = (float) (Math.cos(rad) * r);
        float dy = (float) (Math.sin(rad) * r);

        Shader shader = new LinearGradient(
                cx - dx, cy - dy, cx + dx, cy + dy,
                new int[]{color1, color1, color2, color2},
                new float[]{0f, 0.4999f, 0.5001f, 1f},
                Shader.TileMode.CLAMP);
        paint.setShader(shader);
    }

    @Override
    public void draw(Canvas canvas) {
        if (vennMode) {
            float r       = Math.min(ovalBounds.width(), ovalBounds.height()) * 0.34f;
            float overlap = r * 0.55f;
            float cy      = ovalBounds.centerY();
            float cx1     = ovalBounds.centerX() - overlap;
            float cx2     = ovalBounds.centerX() + overlap;

            paint.setShader(null);
            paint.setColor(color1);
            canvas.drawCircle(cx1, cy, r, paint);
            paint.setColor(color2);
            canvas.drawCircle(cx2, cy, r, paint);

            if (strokeWidthPx > 0f) {
                float strokeR = r - strokeWidthPx / 2f;
                canvas.drawCircle(cx1, cy, strokeR, strokePaint);
                canvas.drawCircle(cx2, cy, strokeR, strokePaint);
            }
        } else {
            canvas.drawOval(ovalBounds, paint);

            if (strokeWidthPx > 0f) {
                // Inset by half the stroke width so the border is drawn
                // fully inside the bounds, same as GradientDrawable's
                // android:stroke behaviour on every other LED button.
                RectF strokeBounds = new RectF(ovalBounds);
                strokeBounds.inset(strokeWidthPx / 2f, strokeWidthPx / 2f);
                canvas.drawOval(strokeBounds, strokePaint);
            }
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
    @SuppressWarnings("deprecation") // still abstract on Drawable - every subclass must implement it, no replacement exists
    public int getOpacity() {
        return PixelFormat.TRANSLUCENT;
    }
}
