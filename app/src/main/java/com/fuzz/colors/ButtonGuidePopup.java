package com.fuzz.colors;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * ============================================================
 *  ButtonGuidePopup.java
 * ============================================================
 *  Read-only reference card opened from the settings footer's INFO
 *  button (left of RESET - see SettingsManager._setupButtons()).
 *  Lists every button in the app that does something on tap AND on
 *  long-press/hold, grouped by the screen area it lives on - most of
 *  these long-press actions have no other visual hint that they exist.
 *
 *  Same "accent header bar over a rounded card" language as
 *  TestModePopup/DiffuserUsagePopup, just a single scrollable body
 *  instead of tappable option rows - this card has nothing to pick,
 *  only to read, so it opens dead-center, dimmed, and closes on a tap
 *  outside or on the CLOSE row at the bottom.
 *
 *  Keep this in sync by hand whenever a click/long-click listener is
 *  added, removed, or changed elsewhere in the app - there is no way
 *  to derive this list from the code automatically.
 * ============================================================
 */
public class ButtonGuidePopup {

    private static final int CARD_WIDTH_DP = 300;
    private static final float DIM_AMOUNT = 0.45f;

    /** One row: a button/area name plus what tap and (optionally) hold do. */
    private static final class Row {
        final String label, tap, hold;
        Row(String label, String tap, String hold) { this.label = label; this.tap = tap; this.hold = hold; }
    }

    /** One section: a header plus its rows. */
    private static final class Section {
        final String title;
        final Row[] rows;
        Section(String title, Row... rows) { this.title = title; this.rows = rows; }
    }

    private static final Section[] SECTIONS = new Section[]{
        new Section("CONNECTION",
            new Row("Status label (top, \"CONNECTING…\"/host text)", null,
                    "Force a fresh reconnect attempt now, instead of waiting for the next auto-retry. Hold ~5s longer to (re)send MQTT cloud credentials, or open that entry dialog if none are saved yet."),
            new Row("V: build-number label", "Show the changelog (\"What's new\").", null)
        ),
        new Section("STATUS BADGES",
            new Row("AmbientMode icon (2x2 status grid)", "Enable AmbientMode (only while status reads READY).",
                    "Disable AmbientMode (only while it's ON)."),
            new Row("Diffuser refill badge", null, "Open the diffuser usage/Parfum popup, refreshed with the live remaining %.")
        ),
        new Section("LED / COLOR PAGE",
            new Row("Individual LED cell", "Toggle that LED's selection (ignored while Ambilight is on).",
                    "Copy that LED's current color into the R/G/B fields."),
            new Row("Heartbeat pattern strip", "Toggle its selection (ignored while Ambilight is on).",
                    "Copy its color into the R/G/B fields."),
            new Row("SELECT ALL / DESELECT ALL", "Select or clear every LED (ignored while Ambilight is on).", null),
            new Row("LEDs OFF", "Send black (off) to the currently selected LEDs.", null),
            new Row("SEND (custom RGB)", "Push the typed R/G/B values to the selected LEDs.", null),
            new Row("R / G / B value field", "Open a slider popup for just that channel.", null),
            new Row("Color picker (wheel icon)", "Open the full color-wheel dialog.", null),
            new Row("Dual-color random swatch", "Apply the shown pair, then roll a new random pair.",
                    "Open the color wheel to hand-dial the random pair."),
            new Row("Saved dual-color swatch", "Apply that saved pair.", "Delete that saved entry."),
            new Row("SAVE DUAL COLORS", "Save the current pair of selected LEDs' colors to the list.", null),
            new Row("Brightness − / +", "Step brightness down/up by one.", null)
        ),
        new Section("SETTINGS FOOTER",
            new Row("RESET", "Show a reminder to hold instead.", "Reset all settings to default (after a ~1.5s hold)."),
            new Row("ENABLE / DISABLE", "Show a reminder to hold instead.", "Toggle the LED strip enabled/disabled (after a ~0.5s hold)."),
            new Row("THEME", "Open the theme picker.", null),
            new Row("BG", "Pick a background image/GIF.", "Open the folder picker (SAF) to grant persistent background access."),
            new Row("TEST", "Open the Test Mode popup.", null)
        ),
        new Section("TELNET / DEBUG CONSOLE",
            new Row("Floating \">_\" launcher", "Open the diffuser telnet console window.", null),
            new Row("Console window ✕", "Close the console window.", null),
            new Row("Title bar", "Drag to move the window.", null)
        ),
        new Section("PAGE NAVIGATION",
            new Row("TERM / LEDS / SET", "Switch to that page.", null),
            new Row("DUAL COLOR (footer)", "Open the Dual Color popup.", null)
        ),
    };

    private final Context ctx;
    private PopupWindow popup;

    public ButtonGuidePopup(Context ctx) {
        this.ctx = ctx;
    }

    /** @param anchor Any attached view - used only as the window token; the card always opens dead-center. */
    public void show(View anchor) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int titleCol  = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        int accent    = ThemeManager.getColor(ctx, R.color.tab_active_tint);
        int labelCol  = ThemeManager.getColor(ctx, R.color.text_default);
        int bodyCol   = ThemeManager.getColor(ctx, R.color.skbar_upper_text);
        int sectionCol = ThemeManager.getColor(ctx, R.color.skbar_section_text_color);

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Accent header bar ──
        TextView header = new TextView(ctx);
        header.setText("BUTTON GUIDE");
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(titleCol);
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(accent);
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});
        header.setBackground(headerBg);
        card.addView(header);

        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(titleCol);
        card.addView(bar);

        // ── Scrollable body: one block per section ──
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (14 * dp), (int) (10 * dp), (int) (14 * dp), (int) (6 * dp));

        for (Section section : SECTIONS) {
            TextView sectionHeader = new TextView(ctx);
            sectionHeader.setText(section.title);
            sectionHeader.setAllCaps(true);
            sectionHeader.setLetterSpacing(0.10f);
            sectionHeader.setTypeface(null, Typeface.BOLD);
            sectionHeader.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
            sectionHeader.setTextColor(titleCol);
            sectionHeader.setAlpha(0.9f);
            LinearLayout.LayoutParams sectionLp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
            sectionLp.setMargins(0, (int) (10 * dp), 0, (int) (4 * dp));
            sectionHeader.setLayoutParams(sectionLp);
            body.addView(sectionHeader);

            for (Row row : section.rows) {
                body.addView(_buildRow(row, dp, labelCol, bodyCol, sectionCol));
            }
        }
        card.addView(_wrapScroll(body, dp));

        // ── CLOSE row ──
        TextView close = new TextView(ctx);
        close.setText("CLOSE");
        close.setAllCaps(true);
        close.setLetterSpacing(0.10f);
        close.setTypeface(null, Typeface.BOLD);
        close.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        close.setTextColor(titleCol);
        close.setGravity(Gravity.CENTER);
        close.setClickable(true);
        close.setFocusable(true);
        close.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        View closeDivider = new View(ctx);
        closeDivider.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (1 * dp)));
        closeDivider.setBackgroundColor(ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.addView(closeDivider);
        close.setOnClickListener(v -> { if (popup != null) popup.dismiss(); });
        card.addView(close);

        // ── Card frame ──
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

    /** Wrap the section list in a ScrollView capped at ~65% of screen height, so a tall guide scrolls instead of overflowing off-screen. */
    private ScrollView _wrapScroll(LinearLayout body, float dp) {
        ScrollView scroll = new ScrollView(ctx);
        DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
        int maxHeight = (int) (dm.heightPixels * 0.65f);
        scroll.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, maxHeight));
        scroll.addView(body);
        return scroll;
    }

    /** One "LABEL — Tap: … / Hold: …" block. */
    private LinearLayout _buildRow(Row row, float dp, int labelCol, int bodyCol, int sectionCol) {
        LinearLayout wrap = new LinearLayout(ctx);
        wrap.setOrientation(LinearLayout.VERTICAL);
        LinearLayout.LayoutParams wrapLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        wrapLp.setMargins(0, 0, 0, (int) (7 * dp));
        wrap.setLayoutParams(wrapLp);

        TextView label = new TextView(ctx);
        label.setText(row.label);
        label.setTypeface(null, Typeface.BOLD);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f);
        label.setTextColor(labelCol);
        wrap.addView(label);

        if (row.tap != null) {
            TextView tap = new TextView(ctx);
            tap.setText("Tap: " + row.tap);
            tap.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f);
            tap.setTextColor(bodyCol);
            tap.setPadding((int) (4 * dp), (int) (2 * dp), 0, 0);
            wrap.addView(tap);
        }
        if (row.hold != null) {
            TextView hold = new TextView(ctx);
            hold.setText("Hold: " + row.hold);
            hold.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.5f);
            hold.setTextColor(sectionCol);
            hold.setPadding((int) (4 * dp), (int) (2 * dp), 0, 0);
            wrap.addView(hold);
        }
        return wrap;
    }

    private void _dimBehind(PopupWindow p) {
        View root = p.getContentView().getRootView();
        WindowManager wm = (WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);
        if (wm == null || !(root.getLayoutParams() instanceof WindowManager.LayoutParams)) return;
        WindowManager.LayoutParams lp = (WindowManager.LayoutParams) root.getLayoutParams();
        lp.flags |= WindowManager.LayoutParams.FLAG_DIM_BEHIND;
        lp.dimAmount = DIM_AMOUNT;
        wm.updateViewLayout(root, lp);
    }

    private void _animateIn(View card) {
        card.setScaleX(0.88f);
        card.setScaleY(0.88f);
        card.setAlpha(0f);
        card.animate().scaleX(1f).scaleY(1f).alpha(1f).setDuration(160).start();
    }
}
