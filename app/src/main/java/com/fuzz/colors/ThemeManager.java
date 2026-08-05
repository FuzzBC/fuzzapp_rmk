package com.fuzz.colors;

import android.content.Context;
import android.graphics.Color;
import android.view.View;

import androidx.core.content.ContextCompat;

import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * ============================================================
 *  ThemeManager.java  (alias: none — called statically as ThemeManager.X)
 * ============================================================
 *  Responsibility:
 *      • Defines every selectable accent THEME as either:
 *          - a single hue rotation (CRIMSON … VIOLET) - one hue applied
 *            everywhere, a "deviation" of the original Amber/Copper look, or
 *          - a two-hue PALETTE (BLAZE, BUBBLEGUM, …) - a genuine fixed
 *            colour COMBINATION: one hue ("primary") for the bright/active
 *            accents, a different hue ("secondary") for the muted/
 *            structural chrome, instead of one colour rotated everywhere.
 *        Either way, every role's existing saturation/value/alpha comes
 *        straight from colors.xml - only the hue(s) change, so nothing
 *        needs a hand-authored second palette.
 *      • Persists the chosen theme to SharedPreferences ("FuZz_Theme"),
 *        same pattern MainActivity already uses for "FuZz_Background".
 *      • Exposes getColor(ctx, colorResId) as a drop-in replacement for
 *        ContextCompat.getColor() at any accent-bearing call site - the
 *        AMBER theme (the app's original look) always returns the exact
 *        original resource value untouched.
 *
 *  How to use:
 *      Swap any call site that should follow the active theme from
 *          ContextCompat.getColor(Main, R.color.skbar_settings_title)
 *      to
 *          ThemeManager.getColor(Main, R.color.skbar_settings_title)
 *
 *      To change theme (and re-paint already-built views), see
 *      SettingsManager.applyTheme() - that's the single place that
 *      walks every live view and re-applies whatever ThemeManager now
 *      resolves.
 * ============================================================
 */
public class ThemeManager {

    private static final String PREFS_NAME = "FuZz_Theme";
    private static final String PREF_KEY   = "theme_name";

    /**
     * Color-resource roles considered "secondary / structural" chrome:
     * dividers, panel backgrounds, muted labels, tracks. Everything NOT
     * in this set is treated as "primary" (the bright/active accent).
     * For single-hue themes hueA == hueB, so this split has no visible
     * effect on them - it only matters for two-hue PALETTE themes.
     */
    private static final Set<Integer> SECONDARY_ROLES = new HashSet<>(Arrays.asList(
            R.color.skbar_upper_text,
            R.color.skbar_track_color,
            R.color.skbar_second_track_color,
            R.color.skbar_section_text_color,
            R.color.text_setting_label,
            R.color.text_secondary,
            R.color.text_muted_grey,
            R.color.stroke_line_color,
            R.color.skbar_whole_settings,
            R.color.skbar_line_between,
            R.color.debug_cell_bg,
            R.color.toast_shadow_color,
            R.color.loading_idle,
            R.color.colorPrimary,
            R.color.colorPrimaryDark,
            R.color.btn_led_color,
            R.color.btn_menu_leds_color,
            R.color.panel_gradient_start,
            R.color.panel_gradient_end
    ));

    /**
     * Every selectable theme. Single-hue entries come first (on-screen popup
     * order, ≥15° apart — near-duplicates removed); two-hue PALETTE entries
     * follow — ThemePopup inserts a divider between the groups.
     *
     * Removed hues (too close): BLOOD, VERMILION, TANGERINE, HONEY, GOLD,
     *   CHARTREUSE, FERN, MOSS, SAGE, AQUA, AZURE, COBALT.
     * Removed palettes (near-duplicate pairs): CITRUS≡FOREST, COSMOS≡ARCTIC,
     *   PEACOCK, EMBER≡WILDFIRE, DUSK≡DESERT, GALAXY≡BERRY,
     *   MONSOON≡LAGOON, JADE_FIRE≡ORCHARD, INFERNO≡SUNSET.
     */
    public enum Theme {
        // ── Single-hue rotations (≥11° apart) ──────────────────────────────
        CRIMSON  (350f, "CRIMSON"),   // deep red
        ROSE     ( 10f, "ROSE"),      // warm pink-red      gap: CRIMSON→ROSE 20°
        AMBER    ( 29f, "AMBER"),     // original app look — unchanged  gap 19°
        MARIGOLD ( 40f, "MARIGOLD"),  // warm orange        gap 11°
        CITRINE  ( 70f, "CITRINE"),   // yellow-green       gap 30°
        LIME     ( 90f, "LIME"),      // bright green       gap 20°
        JADE     (115f, "JADE"),      // mid-green          gap 25°
        MINT     (130f, "MINT"),      // cool green         gap 15°
        EMERALD  (145f, "EMERALD"),   // rich green         gap 15°
        SEAFOAM  (160f, "SEAFOAM"),   // blue-green         gap 15°
        TEAL     (175f, "TEAL"),      // distinct teal      gap 15°
        CYAN     (195f, "CYAN"),      // pure cyan          gap 20°
        SKY      (210f, "SKY"),       // light blue         gap 15°
        SAPPHIRE (235f, "SAPPHIRE"),  // deep blue          gap 25°
        INDIGO   (255f, "INDIGO"),    // blue-indigo        gap 20°
        AMETHYST (270f, "AMETHYST"),  // purple             gap 15°
        VIOLET   (285f, "VIOLET"),    // violet             gap 15°
        ORCHID   (302f, "ORCHID"),    // red-violet         gap 17°
        MAGENTA  (317f, "MAGENTA"),   // magenta            gap 15° → wrap to CRIMSON 33°

        // ── Two-hue palettes ─────────────────────────────────────────────
        BLAZE     (355f, 215f, "BLAZE"),     // red + azure
        BUBBLEGUM (340f, 300f, "BUBBLEGUM"), // pink + fuchsia
        TROPIC    (190f,  20f, "TROPIC"),    // cyan + coral
        FOREST    (130f,  50f, "FOREST"),    // green + gold
        ROYAL     (255f,  50f, "ROYAL"),     // indigo + gold
        NEON      (310f, 190f, "NEON"),      // magenta + cyan
        SUNSET    ( 25f, 275f, "SUNSET"),    // orange + purple
        ARCTIC    (200f, 260f, "ARCTIC"),    // ice blue + violet
        MIDNIGHT  (240f, 285f, "MIDNIGHT"),  // deep blue + violet
        BERRY     (330f, 255f, "BERRY"),     // pink + indigo
        LAGOON    (185f, 150f, "LAGOON"),    // teal + jade
        SAKURA    (335f, 130f, "SAKURA"),    // cherry pink + green
        WILDFIRE  (  5f,  50f, "WILDFIRE"),  // hot red + amber
        AURORA    (150f, 270f, "AURORA"),    // green + violet
        PRISM     (  0f, 120f, "PRISM"),     // red + green complement
        POISON    (120f, 310f, "POISON"),    // toxic green + orchid
        CARNIVAL  (345f,  55f, "CARNIVAL"),  // hot pink + gold
        ABYSS     (240f, 175f, "ABYSS"),     // deep blue + teal
        ORCHARD   ( 95f,  25f, "ORCHARD"),   // leaf green + orange
        // ── New palette entries ──────────────────────────────────────────
        COPPER    ( 30f, 180f, "COPPER"),    // warm copper + teal
        VENOM     ( 90f, 310f, "VENOM"),     // acid green + orchid
        BONFIRE   ( 15f, 100f, "BONFIRE"),   // fire red + forest green
        GLACIAL   (220f, 350f, "GLACIAL"),   // icy blue + crimson
        NOCTURN   (260f, 170f, "NOCTURN"),   // deep indigo + teal
        PAPAYA    ( 35f, 280f, "PAPAYA"),    // orange + violet
        HAZE      (295f, 145f, "HAZE"),      // violet + emerald
        FLARE     (  5f, 220f, "FLARE"),     // red + azure alarm contrast
        POLLEN    ( 55f, 200f, "POLLEN"),    // gold + cyan
        OXIDE     ( 20f, 150f, "OXIDE"),     // rust + seafoam
        RIPTIDE   (175f, 330f, "RIPTIDE"),   // teal + berry
        ECLIPSE   (250f,  30f, "ECLIPSE");   // deep blue + amber

        /** Hue for "primary" roles (the bright/active accent). */
        public final float hueA;
        /** Hue for "secondary" roles (muted/structural chrome). Equals hueA for single-hue themes. */
        public final float hueB;
        private final String label;

        Theme(float hue, String label) {
            this(hue, hue, label);
        }

        Theme(float hueA, float hueB, String label) {
            this.hueA = hueA;
            this.hueB = hueB;
            this.label = label;
        }

        public String label() { return label; }

        /** True for two-hue combinations (Blaze…Arctic); false for single-hue rotations (Crimson…Violet). */
        public boolean isPalette() { return hueA != hueB; }

        /** Principal accent color under this theme (matches skbar_settings_title, hueA). */
        public int accent(Context ctx) {
            return ThemeManager.rotate(ctx, R.color.skbar_settings_title, hueA);
        }

    }

    /** In-memory cache of the active theme, lazily loaded from prefs once per process. */
    private static Theme current;

    private ThemeManager() {} // static-only

    /** @return the currently active theme (loads + caches from SharedPreferences on first call). */
    public static Theme getCurrentTheme(Context ctx) {
        if (current == null) {
            String saved = ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                    .getString(PREF_KEY, Theme.AMBER.name());
            Theme loaded = Theme.AMBER;
            try {
                loaded = Theme.valueOf(saved);
            } catch (IllegalArgumentException ignored) {
                // Unknown/old saved value - fall back to AMBER
            }
            current = loaded;
        }
        return current;
    }

    /**
     * Persists + activates a new theme. Does NOT touch any already-built
     * view - call SettingsManager.applyTheme() right after this to
     * re-paint the live UI.
     */
    public static void setCurrentTheme(Context ctx, Theme theme) {
        current = theme;
        ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
                .edit().putString(PREF_KEY, theme.name()).apply();
    }

    /**
     * Drop-in replacement for ContextCompat.getColor() at any
     * accent-bearing call site. Under the AMBER theme this returns the
     * exact original resource value (no rotation at all), so the
     * default look is pixel-identical to never having touched theming.
     * For two-hue PALETTE themes, "secondary" roles (see SECONDARY_ROLES)
     * resolve to hueB, everything else to hueA.
     */
    public static int getColor(Context ctx, int colorResId) {
        Theme t = getCurrentTheme(ctx);
        if (t == Theme.AMBER) return ContextCompat.getColor(ctx, colorResId);
        float hue = SECONDARY_ROLES.contains(colorResId) ? t.hueB : t.hueA;
        return rotate(ctx, colorResId, hue);
    }

    /** Resolves colorResId, then replaces its hue only (keeps S/V/alpha). */
    static int rotate(Context ctx, int colorResId, float hue) {
        int base = ContextCompat.getColor(ctx, colorResId);
        float[] hsv = new float[3];
        Color.colorToHSV(base, hsv);
        hsv[0] = hue;
        return Color.HSVToColor(Color.alpha(base), hsv);
    }

    /**
     * Re-tints a View's GradientDrawable background STROKE (border) only,
     * leaving its fill/shape/corners untouched. Used for every container
     * whose XML background is _stroke_line / _stroke_line_settings (their
     * stroke is always 2dp in this app) - effInfo, the Settings panel
     * frame, the LED action-button containers, the debug DBG/CLR/RAM
     * chips, etc. Now handles InsetDrawable wrappers safely.
     * Safe no-op if the background isn't a GradientDrawable or the view doesn't exist.
     */
    public static void tintFrameStroke(Context ctx, View view, int colorResId) {
        if (view == null) return;
        android.graphics.drawable.Drawable d = view.getBackground();
        if (d == null) return;

        // Mutate the top-level drawable (InsetDrawable or GradientDrawable)
        d = d.mutate();

        // Recursively find the GradientDrawable inside (if wrapped in InsetDrawable)
        android.graphics.drawable.GradientDrawable g = _findGradient(d);
        if (g == null) return;

        float dp = ctx.getResources().getDisplayMetrics().density;
        g.setStroke(Math.round(2 * dp), getColor(ctx, colorResId));
        // No need to setBackground() again; mutating and updating the drawable
        // already attached to the view is sufficient.
    }

    private static android.graphics.drawable.GradientDrawable _findGradient(android.graphics.drawable.Drawable d) {
        if (d instanceof android.graphics.drawable.GradientDrawable) {
            return (android.graphics.drawable.GradientDrawable) d;
        } else if (d instanceof android.graphics.drawable.InsetDrawable) {
            return _findGradient(((android.graphics.drawable.InsetDrawable) d).getDrawable());
        }
        return null;
    }
}
