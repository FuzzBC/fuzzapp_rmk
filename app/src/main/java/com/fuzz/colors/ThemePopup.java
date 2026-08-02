package com.fuzz.colors;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView;

import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;
import androidx.core.content.ContextCompat;

import java.util.ArrayList;
import java.util.List;

/**
 * ============================================================
 *  ThemePopup.java  —  variant 37 "Sunburst"
 * ============================================================
 *  THEME button popup. Themes fan out as coloured rays around a live
 *  centre preview: the inner ring holds the HUES, the outer ring the
 *  PALETTES. Tapping a ray applies that theme instantly (old ray
 *  de-rings -> new ray gets a white ring + scales up), morphs the
 *  centre disc to the new gradient/name, cross-fades the panel
 *  background, and keeps the popup open. Same public surface as the
 *  old list version, so SettingsManager needs no changes.
 *
 *  Usage (see SettingsManager._setupButtons()):
 *      new ThemePopup(Main).show(SET_BTN_Theme, chosen -> applyTheme(chosen));
 * ============================================================
 */
public class ThemePopup {

    public interface OnThemePicked {
        void onPicked(ThemeManager.Theme theme);
    }

    private final Context ctx;
    private PopupWindow popup;

    /** Outer-frame drawable - re-tinted live on every theme pick. */
    private GradientDrawable panelBg;
    /** Cached density multiplier (set in show(), reused throughout). */
    private float dp;

    /** Currently-active theme (single-element mutable holder). */
    private final ThemeManager.Theme[] active = {null};

    /** Themes split by ring, filled once in show(). */
    private final List<ThemeManager.Theme> hues = new ArrayList<>();
    private final List<ThemeManager.Theme> pals = new ArrayList<>();

    /** Live view refs updated on pick. */
    private View             selectedRay;   // the highlighted ray
    private GradientDrawable  centreBg;      // centre disc gradient
    private TextView          centreLabel;   // centre disc name

    public ThemePopup(Context ctx) {
        this.ctx = ctx;
    }

    /**
     * @param anchor   The THEME button - popup is centred over the window.
     * @param callback Fired with the tapped theme; popup stays open until outside tap.
     */
    public void show(View anchor, OnThemePicked callback) {
        dp        = ctx.getResources().getDisplayMetrics().density;
        active[0] = ThemeManager.getCurrentTheme(ctx);

        for (ThemeManager.Theme t : ThemeManager.Theme.values())
            (t.isPalette() ? pals : hues).add(t);

        // -- Sunburst wheel (fixed square, rays positioned by trig) -------
        int wheelPx = (int) (220 * dp);
        float cx = wheelPx / 2f, cy = wheelPx / 2f;

        FrameLayout wheel = new FrameLayout(ctx);
        wheel.setLayoutParams(new LinearLayout.LayoutParams(wheelPx, wheelPx));

        // Centre preview disc (built first so rays overlap toward it, not over).
        wheel.addView(_buildCentre(cx, cy));

        // Inner ring = HUES.
        int nH = hues.size();
        for (int i = 0; i < nH; i++)
            wheel.addView(_buildRay(hues.get(i), (int) (10 * dp), (int) (21 * dp),
                    360f / nH * i, 56 * dp, cx, cy));

        // Outer ring = PALETTES.
        int nP = pals.size();
        for (int i = 0; i < nP; i++)
            wheel.addView(_buildRay(pals.get(i), (int) (15 * dp), (int) (26 * dp),
                    360f / nP * i, 90 * dp, cx, cy));

        _updateCentre(active[0]);   // seed the centre with the active theme

        // Scrub across the wheel: tap OR drag over any ray applies it
        // instantly, live, as the finger moves - not just on release. Rays
        // themselves aren't clickable (no OnClickListener), so this parent
        // touch listener always sees the gesture instead of a child ray
        // claiming the DOWN event first.
        wheel.setOnTouchListener((v, event) -> {
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                case MotionEvent.ACTION_MOVE: {
                    View ray = _findNearestRay(wheel, event.getX(), event.getY());
                    if (ray != null) {
                        ThemeManager.Theme t = (ThemeManager.Theme) ray.getTag();
                        if (t != active[0]) _pickTheme(ray, t, callback);
                    }
                    return true;
                }
                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    return true;
            }
            return false;
        });

        // -- Hint line ----------------------------------------------------
        TextView hint = new TextView(ctx);
        hint.setText("TAP OR DRAG  \u00B7  INNER HUES  \u00B7  OUTER PALETTES");
        hint.setAllCaps(true);
        hint.setGravity(Gravity.CENTER);
        hint.setLetterSpacing(0.05f);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8);
        hint.setTextColor(ContextCompat.getColor(ctx, R.color.skbar_section_text_color));
        LinearLayout.LayoutParams hlp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        hlp.topMargin = (int) (6 * dp);
        hint.setLayoutParams(hlp);

        // -- Static outer frame -------------------------------------------
        LinearLayout frame = new LinearLayout(ctx);
        frame.setOrientation(LinearLayout.VERTICAL);
        frame.setGravity(Gravity.CENTER_HORIZONTAL);
        frame.setPadding((int) (10 * dp), (int) (12 * dp), (int) (10 * dp), (int) (12 * dp));
        frame.addView(wheel);
        frame.addView(hint);

        panelBg = new GradientDrawable();
        panelBg.setColor(ThemeManager.getColor(ctx, R.color.skbar_whole_settings));
        panelBg.setCornerRadius(12 * dp);
        panelBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        frame.setBackground(panelBg);

        int widthPx = (int) (244 * dp);
        popup = new PopupWindow(frame,
                widthPx, LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);
        _dimBehind(popup);
    }

    /* ============================================================
     *  Centre preview disc
     * ============================================================ */

    /** Builds the live centre disc, positioned dead-centre of the wheel. */
    private View _buildCentre(float cx, float cy) {
        int size = (int) (80 * dp);

        FrameLayout disc = new FrameLayout(ctx);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(size, size);
        lp.leftMargin = Math.round(cx - size / 2f);
        lp.topMargin  = Math.round(cy - size / 2f);
        disc.setLayoutParams(lp);

        centreBg = new GradientDrawable();
        centreBg.setShape(GradientDrawable.OVAL);
        centreBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        disc.setBackground(centreBg);
        disc.setElevation(2 * dp);

        centreLabel = new TextView(ctx);
        centreLabel.setAllCaps(true);
        centreLabel.setGravity(Gravity.CENTER);
        centreLabel.setMaxLines(2);
        centreLabel.setLetterSpacing(0.05f);
        centreLabel.setTypeface(null, Typeface.BOLD);
        centreLabel.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        centreLabel.setTextColor(0xFFFFFFFF);
        centreLabel.setShadowLayer(3f, 0f, 1f, 0x99000000);
        centreLabel.setPadding((int) (6 * dp), 0, (int) (6 * dp), 0);
        disc.addView(centreLabel, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        return disc;
    }

    /** Morphs the centre disc (gradient + name) to the given theme. */
    private void _updateCentre(ThemeManager.Theme t) {
        int colA = t.accent(ctx);
        int colB = ThemeManager.rotate(ctx, R.color.skbar_settings_title, t.hueB);
        centreBg.setColors(new int[]{colA, colB});
        centreBg.setOrientation(GradientDrawable.Orientation.TL_BR);
        centreLabel.setText(t.label());
    }

    /* ============================================================
     *  Rays
     * ============================================================ */

    /**
     * One coloured ray. Positioned on its ring by polar maths, then rotated
     * so its rounded tip points outward.
     *
     * @param t        theme this ray represents
     * @param wPx,hPx  ray footprint
     * @param angleDeg clockwise from top
     * @param radius   ring radius (px)
     * @param cx,cy    wheel centre (px)
     */
    private View _buildRay(ThemeManager.Theme t, int wPx, int hPx,
                           float angleDeg, float radius, float cx, float cy) {
        boolean sel = (t == active[0]);
        int accent  = t.accent(ctx);

        View ray = new View(ctx);
        GradientDrawable g = new GradientDrawable();
        g.setColor(accent);
        float r = 9 * dp, s = 3 * dp;      // rounded tip (outer) / squarer base (inner)
        g.setCornerRadii(new float[]{r, r, r, r, s, s, s, s});
        if (sel) g.setStroke((int) (2 * dp), 0xFFFFFFFF);
        ray.setBackground(g);
        ray.setTag(t);

        // Polar placement around the centre.
        double rad = Math.toRadians(angleDeg);
        float x = cx + (float) (radius * Math.sin(rad));
        float y = cy - (float) (radius * Math.cos(rad));
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(wPx, hPx);
        lp.leftMargin = Math.round(x - wPx / 2f);
        lp.topMargin  = Math.round(y - hPx / 2f);
        ray.setLayoutParams(lp);
        ray.setRotation(angleDeg);         // point the tip radially outward
        if (sel) {
            ray.setScaleX(1.18f);
            ray.setScaleY(1.18f);
            ray.setElevation(4 * dp);
            selectedRay = ray;
        }

        return ray;
    }

    /**
     * Applies a newly-picked theme: re-rings the rays, morphs the centre
     * disc, cross-fades the panel, and fires the callback. Shared by both a
     * plain tap and a drag-scrub move, since both just mean "this ray is now
     * under the finger".
     */
    private void _pickTheme(View ray, ThemeManager.Theme t, OnThemePicked callback) {
        ThemeManager.Theme prev = active[0];
        if (selectedRay != null) _setRaySelected(selectedRay, false);
        active[0] = t;
        selectedRay = ray;
        _setRaySelected(ray, true);
        _updateCentre(t);
        _animatePopupBg(prev, t);          // re-tint panel (before callback flips theme)
        if (callback != null) callback.onPicked(t);
        _pulseCentre();
    }

    /**
     * Finds the ray whose centre is nearest the given point (in wheel-local
     * coordinates), ignoring rotation - good enough for hit-testing since
     * rays are thin and evenly spaced around each ring. Returns null if
     * nothing is within a generous radius (finger off the wheel, e.g. over
     * the hint text below it).
     */
    private View _findNearestRay(ViewGroup wheel, float touchX, float touchY) {
        View nearest = null;
        float bestDistSq = Float.MAX_VALUE;

        for (int i = 0; i < wheel.getChildCount(); i++) {
            View child = wheel.getChildAt(i);
            if (!(child.getTag() instanceof ThemeManager.Theme)) continue; // skip centre disc

            float ccx = child.getX() + child.getWidth()  / 2f;
            float ccy = child.getY() + child.getHeight() / 2f;
            float dx = touchX - ccx, dy = touchY - ccy;
            float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                nearest = child;
            }
        }

        float maxDist = 130 * dp; // generous - comfortably covers the outer ring
        return (nearest != null && bestDistSq < maxDist * maxDist) ? nearest : null;
    }

    /** Toggles a ray's selected look: white ring + scale-up + lift. */
    private void _setRaySelected(View ray, boolean selected) {
        if (ray.getBackground() instanceof GradientDrawable) {
            GradientDrawable g = (GradientDrawable) ray.getBackground().mutate();
            g.setStroke(selected ? (int) (2 * dp) : 0, selected ? 0xFFFFFFFF : Color.TRANSPARENT);
        }
        ray.animate().cancel();
        ray.animate()
                .scaleX(selected ? 1.18f : 1f)
                .scaleY(selected ? 1.18f : 1f)
                .setDuration(140)
                .start();
        ray.setElevation(selected ? 4 * dp : 0f);
    }

    /* ============================================================
     *  Effects
     * ============================================================ */

    /** Scale-bounce (1->1.08->1) on the centre disc for pick feedback. */
    private void _pulseCentre() {
        View disc = (View) centreLabel.getParent();
        if (disc == null) return;
        disc.animate().cancel();
        disc.setScaleX(1f);
        disc.setScaleY(1f);
        disc.animate()
                .scaleX(1.08f).scaleY(1.08f)
                .setDuration(120)
                .withEndAction(() -> disc.animate()
                        .scaleX(1f).scaleY(1f)
                        .setDuration(150)
                        .start())
                .start();
    }

    /**
     * Cross-fades the panel bg (fill + stroke) from prev -> next theme colours.
     * Colours resolve via absolute-hue rotate(), so this is independent of
     * whichever theme SettingsManager sets from the callback.
     *
     * @param prev theme active before the pick
     * @param next theme just selected
     */
    private void _animatePopupBg(ThemeManager.Theme prev, ThemeManager.Theme next) {
        if (panelBg == null) return;

        int oldFill   = ThemeManager.rotate(ctx, R.color.skbar_whole_settings, prev.hueB);
        int newFill   = ThemeManager.rotate(ctx, R.color.skbar_whole_settings, next.hueB);
        int oldStroke = ThemeManager.rotate(ctx, R.color.stroke_line_color,    prev.hueB);
        int newStroke = ThemeManager.rotate(ctx, R.color.stroke_line_color,    next.hueB);

        if (prev == ThemeManager.Theme.AMBER) {   // AMBER = no rotation, pull raw
            oldFill   = ContextCompat.getColor(ctx, R.color.skbar_whole_settings);
            oldStroke = ContextCompat.getColor(ctx, R.color.stroke_line_color);
        }
        if (next == ThemeManager.Theme.AMBER) {
            newFill   = ContextCompat.getColor(ctx, R.color.skbar_whole_settings);
            newStroke = ContextCompat.getColor(ctx, R.color.stroke_line_color);
        }

        final int fF = oldFill, tF = newFill, fS = oldStroke, tS = newStroke;
        ArgbEvaluator argb = new ArgbEvaluator();
        ValueAnimator anim = ValueAnimator.ofFloat(0f, 1f);
        anim.setDuration(350);
        anim.addUpdateListener(a -> {
            float f = (float) a.getAnimatedValue();
            panelBg.setColor( (int) argb.evaluate(f, fF, tF));
            panelBg.setStroke((int) (1 * dp), (int) argb.evaluate(f, fS, tS));
        });
        anim.start();
    }

    /**
     * Dim the window behind the card, matching the other footer popups.
     *
     * @param p popup whose host window should be dimmed
     */
    private void _dimBehind(PopupWindow p) {
        View container = p.getContentView().getRootView();
        android.view.WindowManager wm =
                (android.view.WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);
        android.view.WindowManager.LayoutParams lp =
                (android.view.WindowManager.LayoutParams) container.getLayoutParams();
        lp.flags |= android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND;
        lp.dimAmount = 0.45f;
        wm.updateViewLayout(container, lp);
    }

    public void dismiss() {
        if (popup != null && popup.isShowing()) popup.dismiss();
    }
}
