package com.fuzz.colors;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.TextView;

/**
 * ============================================================
 *  TestModePopup.java
 * ============================================================
 *  Modern option dialog for the TestMode chip row — "accent header
 *  bar" style: solid accent title strip on top of a rounded card,
 *  filled option rows below, and any "OFF" option separated at the
 *  bottom as an outlined destructive action. Dimmed backdrop,
 *  screen-centered in a FIXED position and FIXED width for every
 *  chip, scale/fade entrance. Theme-aware via ThemeManager.
 *
 *  Usage (see SettingsManager._showTestModePopup()):
 *      new TestModePopup(Main).show(chip, "TV",
 *              new String[]{"ON", "OFF"}, new int[]{1, 2},
 *              (value, label) -> DATAs.sendTestMode(value));
 * ============================================================
 */
public class TestModePopup {

    /** Fixed card width (dp) - identical for every chip, fits the longest option text. */
    private static final int CARD_WIDTH_DP = 250;

    /** Wider card for the tabbed variant - the tab strip needs the room. */
    private static final int TABS_WIDTH_DP = 320;

    /** Backdrop dim amount behind the card. */
    private static final float DIM_AMOUNT = 0.45f;

    /** Soft destructive tint for "OFF" actions (kept theme-independent on purpose). */
    private static final int COLOR_DANGER = 0xFFF0918B;

    public interface OnOptionPicked {
        void onPicked(int value, String label);
    }

    private final Context ctx;
    private PopupWindow popup;

    public TestModePopup(Context ctx) {
        this.ctx = ctx;
    }

    /**
     * @param anchor   Any attached view - used only as the window token;
     *                 the card always opens dead-center on screen.
     * @param title    Header text (e.g. "DIFFUSER") - shown on the accent bar.
     * @param labels   Option labels, parallel to values. "OFF" entries are
     *                 split into the outlined destructive section at the bottom.
     * @param values   Command value per option, handed to the callback.
     * @param selectedValue Value of the currently active option - that row
     *                 renders filled/highlighted so the picker shows the
     *                 current setting, not just a blank list. Pass -1 (or any
     *                 value absent from values[]) for no highlight.
     * @param callback Fired with the tapped option; popup closes after.
     */
    public void show(View anchor, String title, String[] labels, int[] values, int selectedValue, OnOptionPicked callback) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int accent   = ThemeManager.getColor(ctx, R.color.tab_active_tint);       // Raw 10% tint - by design - Setup
        int titleCol = ThemeManager.getColor(ctx, R.color.skbar_settings_title);   // App-wide text/title color - Setup
        int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Accent header bar (top corners rounded to match the card) ──
        TextView header = new TextView(ctx);
        header.setText(title);
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(titleCol);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(accent);                                       // 10% tint over the card - chip-selected look
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});    // Top corners only
        header.setBackground(headerBg);
        card.addView(header);

        // Accent underline strip below the header - the variant-8 bar, in the
        // title color itself so it follows every theme with full visibility
        View bar = new View(ctx);
        LinearLayout.LayoutParams barLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp));
        bar.setLayoutParams(barLp);
        bar.setBackgroundColor(titleCol);
        card.addView(bar);

        // ── Body: normal rows, then a divided outlined "OFF" section ──
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (12 * dp), (int) (10 * dp), (int) (12 * dp), (int) (12 * dp));

        int rowFill = _blend(cardBgCol, 0xFFFFFFFF, 0.07f);              // Slightly lifted row surface

        // Two passes: normal rows first, "OFF" entries always rendered last
        // in the divided destructive section - regardless of input order.
        for (int pass = 0; pass < 2; pass++) {
            final boolean destructivePass = (pass == 1);
            boolean dividerAdded = false;

            for (int i = 0; i < labels.length; i++) {
                final int     value       = values[i];
                final String  label       = labels[i];
                final boolean destructive = "OFF".equalsIgnoreCase(label);
                if (destructive != destructivePass) continue;

                if (destructivePass && !dividerAdded) {
                    body.addView(_buildDivider(dp));                     // Hairline before the OFF section
                    dividerAdded = true;
                }

                boolean selected = !destructive && value == selectedValue;
                body.addView(_buildOption(label, destructive, rowFill, accent, selected, titleCol, () -> {
                    if (callback != null) callback.onPicked(value, label);
                    if (popup != null) popup.dismiss();
                }));
            }
        }
        card.addView(body);

        // ── Card frame ───────────────────────────────────────
        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(cardBgCol);
        cardBg.setCornerRadius(16 * dp);
        cardBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.setBackground(cardBg);
        card.setClipToOutline(true);                                     // Keep header inside rounded corners

        int widthPx = (int) (CARD_WIDTH_DP * dp);                        // Fixed width for ALL popups

        popup = new PopupWindow(card, widthPx, LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);

        // Fixed position for ALL chips: dead-center on screen
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);

        _dimBehind(popup);
        _animateIn(card);
    }

    /**
     * One tab of the tabbed popup: a heading plus its own option list.
     *
     * @param title   Tab label shown on the strip (e.g. "MOTION").
     * @param labels  Option labels for this tab, parallel to values.
     * @param values  Command value per option, handed to the callback.
     */
    public static final class Tab {
        final String   title;
        final String[] labels;
        final int[]    values;

        public Tab(String title, String[] labels, int[] values) {
            this.title = title; this.labels = labels; this.values = values;
        }
    }

    /** Fired with the tab index alongside the picked option. */
    public interface OnTabOptionPicked {
        void onPicked(int tabIndex, int value, String label);
    }

    /** Tab index reported when the fixed CANCEL action is used. */
    public static final int TAB_CANCEL = -1;

    /**
     * Tabbed variant: one card, a scrollable tab strip across the top and the
     * selected tab's options below it.
     *
     * Replaces the old in-page chip row, where picking a mode meant tapping a
     * chip and then a second popup. Everything lives in one surface now, and
     * switching tabs only swaps the body - the card never closes and reopens.
     *
     * CANCEL is deliberately not a tab. Dropping a forced mode is the one
     * action wanted from any tab, and hiding it behind its own tab meant
     * navigating away from whatever you were looking at to reach it - so it
     * sits pinned below the option list instead, visible on every page.
     *
     * @param anchor    Any attached view, used only for the window token.
     * @param tabs      Tabs in display order.
     * @param activeTab Tab to open on, clamped into range.
     * @param callback  Fired with the tapped option, or with TAB_CANCEL when
     *                  the pinned CANCEL action is used; the popup closes after.
     */
    public void showTabs(View anchor, Tab[] tabs, int activeTab, OnTabOptionPicked callback) {
        if (tabs == null || tabs.length == 0) return;
        final float dp = ctx.getResources().getDisplayMetrics().density;
        // accent is the SOLID theme accent - skbar_settings_title. The old code
        // read tab_active_tint here, which is a ~10% alpha wash; used as a fill
        // it left the header nearly transparent and the title unreadable. That
        // is theme-wide, so it looked fine only on the copper theme it was
        // eyeballed against and broke on every other one.
        final int accent    = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        final int onAccent  = _idealInk(accent);          // black or white, whichever reads on accent
        final int titleCol  = accent;
        final int dimCol    = ThemeManager.getColor(ctx, R.color.skbar_section_text_color);
        final int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        final int rowFill   = _blend(cardBgCol, 0xFFFFFFFF, 0.07f);
        final int selected  = Math.max(0, Math.min(activeTab, tabs.length - 1));

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Title, on the accent band ──
        TextView title = new TextView(ctx);
        title.setText("TEST MODE");
        title.setAllCaps(true);
        title.setLetterSpacing(0.14f);
        title.setTypeface(null, Typeface.BOLD);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        title.setTextColor(onAccent);                      // black or white, picked to read on the band
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, (int) (11 * dp), 0, (int) (9 * dp));
        GradientDrawable titleBg = new GradientDrawable();
        titleBg.setColor(accent);          // solid accent - the header band
        float tr = 16 * dp;
        titleBg.setCornerRadii(new float[]{tr, tr, tr, tr, 0, 0, 0, 0});
        title.setBackground(titleBg);
        card.addView(title);

        // ── Tab strip: fills the card width, one equal share per tab ──
        // Weighted rather than scrolling - with this many tabs everything fits,
        // and a strip that ends short of the card edge looks unfinished. Sits
        // on the same accent band as the title, squared off since the title
        // already rounds the top.
        final TextView[] tabViews = new TextView[tabs.length];
        LinearLayout strip = new LinearLayout(ctx);
        strip.setOrientation(LinearLayout.HORIZONTAL);
        strip.setGravity(Gravity.CENTER_VERTICAL);
        strip.setWeightSum(tabs.length);
        strip.setPadding((int) (5 * dp), 0, (int) (5 * dp), (int) (6 * dp));
        strip.setBackgroundColor(accent); // same solid band as the title
        card.addView(strip);

        // Thin seam between the copper band and the body.
        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(titleCol);
        card.addView(bar);

        // ── Body: rebuilt in place whenever the tab changes ──
        final LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (12 * dp), (int) (10 * dp), (int) (12 * dp), (int) (12 * dp));

        final int[] current = {selected};

        final Runnable[] paintTabs = new Runnable[1];
        paintTabs[0] = () -> {
            for (int i = 0; i < tabViews.length; i++) {
                boolean on = (i == current[0]);
                // All derived from the live theme, so it holds on any accent.
                // Selected: chip FILLED with the accent, labelled in the ink
                //   that reads on it. Accent-as-text on the dark panel failed on
                //   darker accents (crimson/violet ~3:1); accent-as-fill does not.
                // Unselected: chip lightly lifted from the panel, dim label -
                //   clearly a tab, clearly not the current one.
                int unselFill = _blend(cardBgCol, 0xFFFFFFFF, 0.10f);
                tabViews[i].setTextColor(on ? _idealInk(accent) : dimCol);
                GradientDrawable bg = new GradientDrawable();
                bg.setCornerRadius(9 * dp);
                bg.setColor(on ? accent : unselFill);
                bg.setStroke((int) (1 * dp), on ? 0x00000000 : _blend(cardBgCol, 0xFFFFFFFF, 0.20f));
                tabViews[i].setBackground(bg);
            }
        };

        final Runnable[] fillBody = new Runnable[1];
        fillBody[0] = () -> {
            body.removeAllViews();
            Tab t = tabs[current[0]];

            // Same two-pass rule as show(): "OFF" always lands in the divided
            // destructive section at the bottom, whatever order it arrives in.
            for (int pass = 0; pass < 2; pass++) {
                final boolean destructivePass = (pass == 1);
                boolean dividerAdded = false;

                for (int i = 0; i < t.labels.length; i++) {
                    final int     value       = t.values[i];
                    final String  label       = t.labels[i];
                    final boolean destructive = "OFF".equalsIgnoreCase(label);
                    if (destructive != destructivePass) continue;

                    if (destructivePass && !dividerAdded) {
                        body.addView(_buildDivider(dp));
                        dividerAdded = true;
                    }
                    body.addView(_buildOption(label, destructive, rowFill, accent, false, 0, () -> {
                        if (callback != null) callback.onPicked(current[0], value, label);
                        if (popup != null) popup.dismiss();
                    }));
                }
            }
        };

        for (int i = 0; i < tabs.length; i++) {
            TextView tab = new TextView(ctx);
            tab.setText(tabs[i].title);
            tab.setAllCaps(true);
            tab.setTypeface(null, Typeface.BOLD);
            tab.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
            tab.setTypeface(null, Typeface.BOLD);
            tab.setGravity(Gravity.CENTER);
            tab.setMaxLines(1);
            tab.setEllipsize(android.text.TextUtils.TruncateAt.END);
            tab.setPadding((int) (3 * dp), (int) (7 * dp), (int) (3 * dp), (int) (7 * dp));

            // 0dp + weight: each tab takes an equal slice of the full width
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
            lp.setMargins((int) (2 * dp), 0, (int) (2 * dp), 0);
            tab.setLayoutParams(lp);

            final int idx = i;
            tab.setOnClickListener(v -> {
                if (current[0] == idx) return;
                current[0] = idx;
                paintTabs[0].run();
                fillBody[0].run();
            });

            tabViews[i] = tab;
            strip.addView(tab);
        }
        paintTabs[0].run();
        fillBody[0].run();
        card.addView(body);

        // Pinned CANCEL - always reachable, never scrolls out with the tab body
        card.addView(_buildDivider(dp));
        LinearLayout footer = new LinearLayout(ctx);
        footer.setOrientation(LinearLayout.VERTICAL);
        footer.setPadding((int) (12 * dp), (int) (6 * dp), (int) (12 * dp), (int) (12 * dp));
        footer.addView(_buildOption("CANCEL", true, rowFill, accent, false, 0, () -> {
            if (callback != null) callback.onPicked(TAB_CANCEL, 0, "CANCEL");
            if (popup != null) popup.dismiss();
        }));
        card.addView(footer);

        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(cardBgCol);
        cardBg.setCornerRadius(16 * dp);
        cardBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.setBackground(cardBg);
        card.setClipToOutline(true);

        popup = new PopupWindow(card, (int) (TABS_WIDTH_DP * dp),
                LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);

        _dimBehind(popup);
        _animateIn(card);
    }

    /**
     * One tappable full-width option row.
     * Normal rows: filled lifted surface with a subtle stroke.
     * Destructive rows ("OFF"): transparent fill, soft-red outline and text.
     *
     * @param label       Text shown on the row.
     * @param destructive Styles the row as the outlined danger action.
     * @param rowFill     Fill color for normal rows.
     * @param accent      Theme accent, used for the tap flash.
     * @param selected    True to render this row as the currently active pick
     *                    (solid selectedFill, instead of the plain lifted fill).
     * @param selectedFill Solid fill color for the selected row (e.g. titleCol).
     * @param onTap       Runnable fired on tap (callback + dismiss).
     */
    private View _buildOption(String label, boolean destructive, int rowFill, int accent,
                               boolean selected, int selectedFill, Runnable onTap) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int colorDanger = ThemeManager.getColor(ctx, R.color.color_danger_soft);
        int transparent = ThemeManager.getColor(ctx, R.color.color_transparent);

        TextView tv = new TextView(ctx);
        tv.setText(label);
        tv.setAllCaps(true);
        tv.setLetterSpacing(0.06f);
        tv.setTypeface(null, Typeface.BOLD);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        tv.setSingleLine(true);
        tv.setTextColor(destructive ? colorDanger
                                    : selected ? _idealInk(selectedFill)
                                    : ThemeManager.getColor(ctx, R.color.skbar_settings_title));
        tv.setGravity(Gravity.CENTER);
        tv.setPadding((int) (14 * dp), (int) (9 * dp), (int) (14 * dp), (int) (9 * dp));
        tv.setClickable(true);
        tv.setFocusable(true);

        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(10 * dp);
        if (destructive) {
            bg.setColor(transparent);                                     // Transparent - outlined action
            bg.setStroke((int) (1 * dp), colorDanger);
        } else if (selected) {
            bg.setColor(selectedFill);                                   // Solid fill - marks the current pick
            bg.setStroke((int) (1 * dp), transparent);
        } else {
            bg.setColor(rowFill);                                        // Lifted surface fill
            bg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.skbar_track_color));
        }
        tv.setBackground(bg);

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, (int) (4 * dp), 0, (int) (4 * dp));
        tv.setLayoutParams(lp);

        tv.setOnClickListener(v -> {
            // Selected feedback: full accent (or danger) flash before dismissing
            GradientDrawable sel = (GradientDrawable) tv.getBackground().mutate();
            sel.setColor(destructive ? (colorDanger & 0x33FFFFFF) : accent);
            onTap.run();
        });

        return tv;
    }

    /**
     * Hairline divider between the normal options and the "OFF" section.
     *
     * @param dp  Display density factor.
     */
    private View _buildDivider(float dp) {
        View line = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (1 * dp));
        lp.setMargins(0, (int) (8 * dp), 0, (int) (8 * dp));
        line.setLayoutParams(lp);
        line.setBackgroundColor(ThemeManager.getColor(ctx, R.color.stroke_line_color));
        return line;
    }

    /**
     * Linear ARGB blend from a toward b by t (0..1).
     *
     * @param a  Start color.
     * @param b  Target color.
     * @param t  Blend factor.
     */
    /**
     * Pick black or white text for a background, by perceived luminance.
     *
     * Used so the header band and tab chips stay legible whatever the theme
     * accent is - a hardcoded ink only works for the one accent it was chosen
     * against.
     *
     * @param bg Background colour, ARGB (alpha ignored).
     * @return 0xFF111111 on light backgrounds, 0xFFF3ECE2 on dark ones.
     */
    private int _idealInk(int bg) {
        int r = (bg >> 16) & 0xFF, g = (bg >> 8) & 0xFF, b = bg & 0xFF;
        int luma = (r * 299 + g * 587 + b * 114) / 1000;
        return (luma > 128) ? 0xFF111111 : 0xFFF3ECE2;
    }

    private int _blend(int a, int b, float t) {
        int aA = (a >> 24) & 0xFF, aR = (a >> 16) & 0xFF, aG = (a >> 8) & 0xFF, aB = a & 0xFF;
        int bA = (b >> 24) & 0xFF, bR = (b >> 16) & 0xFF, bG = (b >> 8) & 0xFF, bB = b & 0xFF;
        return ((int) (aA + (bA - aA) * t) << 24)
             | ((int) (aR + (bR - aR) * t) << 16)
             | ((int) (aG + (bG - aG) * t) << 8)
             |  (int) (aB + (bB - aB) * t);
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
}
