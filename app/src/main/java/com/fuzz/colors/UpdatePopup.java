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
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.core.content.ContextCompat;

/**
 * ============================================================
 *  UpdatePopup.java
 * ============================================================
 *  In-app update prompt + live download progress, styled to match
 *  the app's existing card language (see DiffuserUsagePopup/ParfumPopup:
 *  accent header bar on a rounded card, dimmed backdrop, centered,
 *  scale/fade entrance) instead of a stock system AlertDialog.
 *
 *  Two entry points:
 *    showAvailable(...) - "a new version is ready" prompt, Later/Update.
 *    showProgress(...)  - live percent/speed while downloading, with
 *                         Cancel; returns a Handle the caller pushes
 *                         progress updates into.
 * ============================================================
 */
public class UpdatePopup {

    private static final int CARD_WIDTH_DP = 260;
    private static final float DIM_AMOUNT = 0.5f;

    private final Context ctx;
    private PopupWindow popup;

    public UpdatePopup(Context ctx) {
        this.ctx = ctx;
    }

    /** Live handle into a showing progress card, for the caller to push updates into. */
    public interface Handle {
        /** @param percent 0-100. downloaded/total in bytes, speedBps in bytes/sec (0 if unknown). */
        void setProgress(int percent, long downloaded, long total, double speedBps);
        void setStatus(String text);
        void dismiss();
    }

    // ========================================================
    //  Update available
    // ========================================================

    public void showAvailable(View anchor, String displayVersion, String releaseNotes, Runnable onUpdate) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        LinearLayout card = _newCard(dp, "UPDATE AVAILABLE", ContextCompat.getColor(ctx, R.color.status_ready_yellow));

        LinearLayout body = _newBody(dp);

        TextView big = new TextView(ctx);
        big.setText("Version " + displayVersion);
        big.setTypeface(null, Typeface.BOLD);
        big.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
        big.setTextColor(_titleColor());
        big.setGravity(Gravity.CENTER);
        big.setPadding(0, 0, 0, (int) (4 * dp));
        body.addView(big);

        // "What's new in this version" - the release notes for THIS version only
        // (see AGENTS.md / CHANGELOG.md), not the full history. Falls back to a
        // generic line if the release was published without a changelog entry.
        boolean hasNotes = releaseNotes != null && !releaseNotes.trim().isEmpty()
                && !releaseNotes.trim().toLowerCase(java.util.Locale.ROOT).startsWith("no changelog entry");
        if (hasNotes) {
            ScrollView scroll = new ScrollView(ctx);
            LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, (int) (180 * dp));
            scrollLp.setMargins(0, 0, 0, (int) (14 * dp));
            scroll.setLayoutParams(scrollLp);

            LinearLayout notes = new LinearLayout(ctx);
            notes.setOrientation(LinearLayout.VERTICAL);
            for (String raw : releaseNotes.split("\n")) {
                String line = raw.trim();
                if (line.isEmpty()) continue;
                if (line.startsWith("- ") || line.startsWith("* ")) {
                    notes.addView(_changelogLine("• " + line.substring(2).trim(), dp, false, true));
                } else {
                    notes.addView(_changelogLine(line, dp, false, false));
                }
            }
            scroll.addView(notes);
            body.addView(scroll);
        } else {
            TextView sub = new TextView(ctx);
            sub.setText("A new version is ready to download.");
            sub.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            sub.setTextColor(_titleColor());
            sub.setAlpha(0.75f);
            sub.setGravity(Gravity.CENTER);
            sub.setPadding(0, 0, 0, (int) (16 * dp));
            body.addView(sub);
        }

        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        row.setLayoutParams(rowLp);

        TextView later = _outlineButton(dp, "LATER");
        LinearLayout.LayoutParams laterLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        laterLp.setMargins(0, 0, (int) (6 * dp), 0);
        later.setLayoutParams(laterLp);
        later.setOnClickListener(v -> dismiss());
        row.addView(later);

        TextView update = _filledButton(dp, "UPDATE NOW", ContextCompat.getColor(ctx, R.color.status_ready_yellow));
        LinearLayout.LayoutParams updateLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        updateLp.setMargins((int) (6 * dp), 0, 0, 0);
        update.setLayoutParams(updateLp);
        update.setOnClickListener(v -> {
            dismiss();
            if (onUpdate != null) onUpdate.run();
        });
        row.addView(update);

        body.addView(row);
        card.addView(body);
        _show(anchor, card, dp);
    }

    // ========================================================
    //  Download progress
    // ========================================================

    public Handle showProgress(View anchor, String displayVersion, Runnable onCancel) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int accent = ContextCompat.getColor(ctx, R.color.status_on_green);
        LinearLayout card = _newCard(dp, "DOWNLOADING UPDATE", accent);

        LinearLayout body = _newBody(dp);

        TextView title = new TextView(ctx);
        title.setText("Version " + displayVersion);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        title.setTextColor(_titleColor());
        title.setAlpha(0.75f);
        title.setGravity(Gravity.CENTER);
        title.setPadding(0, 0, 0, (int) (12 * dp));
        body.addView(title);

        TextView percentTv = new TextView(ctx);
        percentTv.setText("0%");
        percentTv.setTypeface(null, Typeface.BOLD);
        percentTv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 28);
        percentTv.setTextColor(accent);
        percentTv.setGravity(Gravity.CENTER);
        body.addView(percentTv);

        // Track + fill, same left-anchored scaleX technique as the refill-hold
        // button elsewhere in the app - full control over styling, no
        // ProgressBar theming to fight with.
        FrameLayout track = new FrameLayout(ctx);
        GradientDrawable trackBg = new GradientDrawable();
        trackBg.setColor(_rowFill());
        trackBg.setCornerRadius(8 * dp);
        track.setBackground(trackBg);
        track.setClipToOutline(true);
        LinearLayout.LayoutParams trackLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (10 * dp));
        trackLp.setMargins(0, (int) (10 * dp), 0, (int) (10 * dp));
        track.setLayoutParams(trackLp);

        View fill = new View(ctx);
        fill.setBackgroundColor(accent);
        fill.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        fill.setPivotX(0f);
        fill.setScaleX(0f);
        track.addView(fill);
        body.addView(track);

        TextView statusTv = new TextView(ctx);
        statusTv.setText("Starting...");
        statusTv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        statusTv.setTextColor(_titleColor());
        statusTv.setAlpha(0.7f);
        statusTv.setGravity(Gravity.CENTER);
        statusTv.setPadding(0, 0, 0, (int) (14 * dp));
        body.addView(statusTv);

        TextView cancel = _outlineButton(dp, "CANCEL");
        LinearLayout.LayoutParams cancelLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        cancel.setLayoutParams(cancelLp);
        final boolean[] cancelled = {false};
        cancel.setOnClickListener(v -> {
            cancelled[0] = true;
            if (onCancel != null) onCancel.run();
            dismiss();
        });
        body.addView(cancel);

        card.addView(body);
        _show(anchor, card, dp);

        return new Handle() {
            @Override
            public void setProgress(int percent, long downloaded, long total, double speedBps) {
                if (cancelled[0]) return;
                int p = Math.max(0, Math.min(100, percent));
                percentTv.setText(p + "%");
                fill.setScaleX(p / 100f);
                String sizeText = total > 0
                        ? (_fmtBytes(downloaded) + " / " + _fmtBytes(total))
                        : _fmtBytes(downloaded);
                String speedText = speedBps > 0 ? (" · " + _fmtBytes((long) speedBps) + "/s") : "";
                statusTv.setText(sizeText + speedText);
            }

            @Override
            public void setStatus(String text) {
                if (!cancelled[0]) statusTv.setText(text);
            }

            @Override
            public void dismiss() {
                UpdatePopup.this.dismiss();
            }
        };
    }

    // ========================================================
    //  What's new / changelog
    // ========================================================

    /**
     * Shows the bundled CHANGELOG.md (see AGENTS.md) in a scrollable card -
     * "## 1.XXX" lines render as version headings, "- " lines as bullets,
     * everything else as plain text. Deliberately simple - the changelog
     * itself is written in plain prose, not full markdown.
     */
    public void showChangelog(View anchor, String markdown) {
        float dp = ctx.getResources().getDisplayMetrics().density;
        int accent = ThemeManager.getColor(ctx, R.color.tab_active_tint);
        LinearLayout card = _newCard(dp, "WHAT'S NEW", accent);

        ScrollView scroll = new ScrollView(ctx);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (320 * dp));
        scroll.setLayoutParams(scrollLp);

        LinearLayout body = _newBody(dp);
        body.setPadding((int) (16 * dp), (int) (12 * dp), (int) (16 * dp), (int) (4 * dp));

        if (markdown == null || markdown.trim().isEmpty()) {
            body.addView(_changelogLine("No changelog available yet.", dp, false, false));
        } else {
            for (String raw : markdown.split("\n")) {
                String line = raw.trim();
                if (line.isEmpty()) continue;
                if (line.startsWith("## ")) {
                    body.addView(_changelogLine(line.substring(3).trim(), dp, true, false));
                } else if (line.startsWith("#")) {
                    continue; // top-level "# Changelog" title / stray headings - skip
                } else if (line.startsWith("- ") || line.startsWith("* ")) {
                    body.addView(_changelogLine("• " + line.substring(2).trim(), dp, false, true));
                } else {
                    body.addView(_changelogLine(line, dp, false, false));
                }
            }
        }
        scroll.addView(body);
        card.addView(scroll);

        LinearLayout footer = _newBody(dp);
        footer.setPadding((int) (16 * dp), 0, (int) (16 * dp), (int) (16 * dp));
        TextView close = _outlineButton(dp, "CLOSE");
        close.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));
        close.setOnClickListener(v -> dismiss());
        footer.addView(close);
        card.addView(footer);

        _show(anchor, card, dp);
    }

    private TextView _changelogLine(String text, float dp, boolean isHeading, boolean isBullet) {
        TextView tv = new TextView(ctx);
        tv.setText(text);
        tv.setTextColor(_titleColor());
        if (isHeading) {
            tv.setTypeface(null, Typeface.BOLD);
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
            tv.setAllCaps(true);
            tv.setLetterSpacing(0.06f);
            tv.setPadding(0, (int) (10 * dp), 0, (int) (4 * dp));
        } else {
            tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            tv.setAlpha(isBullet ? 0.85f : 0.7f);
            tv.setPadding(isBullet ? (int) (4 * dp) : 0, (int) (2 * dp), 0, (int) (2 * dp));
        }
        return tv;
    }

    public void dismiss() {
        if (popup != null && popup.isShowing()) popup.dismiss();
        popup = null;
    }

    // ========================================================
    //  Shared card chrome
    // ========================================================

    private LinearLayout _newCard(float dp, String headerText, int accent) {
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        TextView header = new TextView(ctx);
        header.setText(headerText);
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(_titleColor());
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(ThemeManager.getColor(ctx, R.color.tab_active_tint));
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});
        header.setBackground(headerBg);
        card.addView(header);

        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(accent);
        card.addView(bar);

        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(ThemeManager.getColor(ctx, R.color.skbar_whole_settings));
        cardBg.setCornerRadius(16 * dp);
        cardBg.setStroke((int) dp, ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.setBackground(cardBg);
        card.setClipToOutline(true);
        return card;
    }

    private LinearLayout _newBody(float dp) {
        LinearLayout body = new LinearLayout(ctx);
        body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding((int) (16 * dp), (int) (16 * dp), (int) (16 * dp), (int) (16 * dp));
        return body;
    }

    private TextView _outlineButton(float dp, String text) {
        TextView btn = new TextView(ctx);
        btn.setText(text);
        btn.setAllCaps(true);
        btn.setLetterSpacing(0.06f);
        btn.setTypeface(null, Typeface.BOLD);
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        btn.setTextColor(_titleColor());
        btn.setGravity(Gravity.CENTER);
        btn.setPadding((int) (12 * dp), (int) (10 * dp), (int) (12 * dp), (int) (10 * dp));
        btn.setClickable(true);
        btn.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(_rowFill());
        bg.setCornerRadius(10 * dp);
        bg.setStroke((int) dp, ThemeManager.getColor(ctx, R.color.stroke_line_color));
        btn.setBackground(bg);
        return btn;
    }

    private TextView _filledButton(float dp, String text, int accent) {
        TextView btn = new TextView(ctx);
        btn.setText(text);
        btn.setAllCaps(true);
        btn.setLetterSpacing(0.06f);
        btn.setTypeface(null, Typeface.BOLD);
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        btn.setTextColor(0xFF14181D);
        btn.setGravity(Gravity.CENTER);
        btn.setPadding((int) (12 * dp), (int) (10 * dp), (int) (12 * dp), (int) (10 * dp));
        btn.setClickable(true);
        btn.setFocusable(true);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(accent);
        bg.setCornerRadius(10 * dp);
        btn.setBackground(bg);
        return btn;
    }

    private void _show(View anchor, LinearLayout card, float dp) {
        int widthPx = (int) (CARD_WIDTH_DP * dp);
        popup = new PopupWindow(card, widthPx, LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(false); // update prompts shouldn't dismiss on a stray tap
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);
        _dimBehind(popup);
        _animateIn(card);
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
        card.animate().scaleX(1f).scaleY(1f).alpha(1f)
                .setDuration(160).setInterpolator(new DecelerateInterpolator()).start();
    }

    private int _titleColor() {
        return ThemeManager.getColor(ctx, R.color.skbar_settings_title);
    }

    private int _rowFill() {
        int base = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        return _blend(base, 0xFFFFFFFF, 0.07f);
    }

    private int _blend(int a, int b, float t) {
        int aA = (a >> 24) & 0xFF, aR = (a >> 16) & 0xFF, aG = (a >> 8) & 0xFF, aB = a & 0xFF;
        int bA = (b >> 24) & 0xFF, bR = (b >> 16) & 0xFF, bG = (b >> 8) & 0xFF, bB = b & 0xFF;
        return ((int) (aA + (bA - aA) * t) << 24)
                | ((int) (aR + (bR - aR) * t) << 16)
                | ((int) (aG + (bG - aG) * t) << 8)
                | (int) (aB + (bB - aB) * t);
    }

    private String _fmtBytes(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return String.format(java.util.Locale.US, "%.0f KB", bytes / 1024.0);
        return String.format(java.util.Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0));
    }
}
