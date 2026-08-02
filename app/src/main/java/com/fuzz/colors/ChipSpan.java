package com.fuzz.colors;

/*
 * ============================================================
 *  CHIPSPAN – rounded value pill drawn inline in a console line
 * ============================================================
 *  Replaces one [bracketed] token from an Arduino log line with a
 *  filled, rounded chip. Built once per line by ConsoleAdapter at
 *  append() time and cached inside the line's Spannable - the row
 *  only ever calls setText() on bind, so draw() runs on scroll only.
 *
 *  Colours are decided by the caller (ConsoleAdapter._chipColors),
 *  not here: this class is pure geometry + paint.
 * ============================================================
 */

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.text.style.ReplacementSpan;

import androidx.annotation.NonNull;

final class ChipSpan extends ReplacementSpan {

    // --------------------------------------------------------
    // Geometry (dp – scaled by the density passed in)
    // --------------------------------------------------------
    /** Horizontal padding inside the pill, dp. */
    private static final float PAD_H = 4f;

    /** Vertical padding above/below the text baseline box, dp. */
    private static final float PAD_V = 1.5f;

    /** Corner radius, dp. */
    private static final float RADIUS = 3f;

    /** Gap left outside the pill so adjacent chips don't touch, dp. */
    private static final float GAP = 1.5f;

    // --------------------------------------------------------
    // Shared scratch – reused across every chip, never nested
    // --------------------------------------------------------
    /** Fill paint for the pill body (no text). */
    private static final Paint FILL = new Paint(Paint.ANTI_ALIAS_FLAG);

    /** Pill bounds, recomputed per draw. */
    private static final RectF BOX = new RectF();

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------
    /** Chip label – the bracket contents, brackets already stripped. */
    private final String text;

    /** Pill fill colour, ARGB. */
    private final int bg;

    /** Label colour, ARGB. */
    private final int fg;

    /** Display density, used to convert the dp constants above. */
    private final float d;

    /**
     * @param text     Chip label (bracket contents).
     * @param bg       Pill fill colour, ARGB.
     * @param fg       Label colour, ARGB.
     * @param density  Display density (Resources.getDisplayMetrics().density).
     */
    ChipSpan(String text, int bg, int fg, float density) {
        this.text = text;
        this.bg   = bg;
        this.fg   = fg;
        this.d    = density;
    }

    /**
     * Advance width of the whole pill, and the line metrics it needs.
     *
     * Filling in @p fm is not optional. A line whose text is entirely covered
     * by ReplacementSpans has no plain characters left to supply ascent and
     * descent, so leaving fm untouched collapses that row to zero height and
     * the line renders blank. That is exactly what happens to a message like
     * "{#R}NOT AVAILABLE{##}", where the colour run covers everything and is
     * promoted to a single chip.
     */
    @Override
    public int getSize(@NonNull Paint paint, CharSequence t, int start, int end,
                       Paint.FontMetricsInt fm) {
        if (fm != null) {
            Paint.FontMetricsInt pm = paint.getFontMetricsInt();
            int pad = Math.round(PAD_V * d);
            fm.ascent = fm.top    = pm.ascent  - pad;
            fm.descent = fm.bottom = pm.descent + pad;
        }
        return Math.round(paint.measureText(text) + (PAD_H * 2 + GAP * 2) * d);
    }

    /** Draws the pill body then the label, restoring the caller's paint colour. */
    @Override
    public void draw(@NonNull Canvas canvas, CharSequence t, int start, int end,
                     float x, int top, int y, int bottom, @NonNull Paint paint) {
        Paint.FontMetrics fm = paint.getFontMetrics();

        BOX.left   = x + GAP * d;
        BOX.right  = BOX.left + paint.measureText(text) + PAD_H * 2 * d;
        BOX.top    = y + fm.ascent  - PAD_V * d;
        BOX.bottom = y + fm.descent + PAD_V * d;

        FILL.setColor(bg);
        canvas.drawRoundRect(BOX, RADIUS * d, RADIUS * d, FILL);

        int prev = paint.getColor();
        paint.setColor(fg);
        canvas.drawText(text, BOX.left + PAD_H * d, y, paint);
        paint.setColor(prev);
    }
}
