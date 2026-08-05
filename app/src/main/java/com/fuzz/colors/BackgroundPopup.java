package com.fuzz.colors;

/*
 * ============================================================
 *  BACKGROUNDPOPUP – vertical GIF background selector
 * ============================================================
 *  Replaces the horizontal filmstrip that used to live inline in the
 *  settings page. Same source of truth (MainActivity.bgImageList, filled
 *  by _LoadBackgroundImages()), same apply path (_SetBackgroundImage),
 *  but presented as a scrolling vertical list where the file name is
 *  readable next to its thumbnail.
 *
 *  The list is rebuilt on every open, so a folder re-pick is reflected
 *  without any separate refresh call.
 *
 *  Card styling deliberately mirrors TestModePopup: accent header, title
 *  underline, lifted rows - the two popups are siblings and should read
 *  as one family.
 * ============================================================
 */

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.content.res.ColorStateList;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.animation.DecelerateInterpolator;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.PopupWindow;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.documentfile.provider.DocumentFile;

import java.io.InputStream;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import android.util.LruCache;

public class BackgroundPopup {

    /** Card width in dp – matches TestModePopup so the two look related. */
    private static final int CARD_WIDTH_DP = 280;

    /** Cap on card height in dp; the list scrolls beyond this. */
    private static final int MAX_LIST_DP = 340;

    /** Thumbnail edge in dp. */
    private static final int THUMB_DP = 56;

    /** Dim applied to everything behind the card. */
    private static final float DIM_AMOUNT = 0.45f;

    /**
     * Decoded first-frame thumbnails, keyed by document URI.
     *
     * Kept static so reopening the picker is instant: the folder rarely
     * changes, and a handful of small bitmaps is cheap to hold.
     */
    private static final LruCache<String, Bitmap> THUMBS = new LruCache<String, Bitmap>(64) {
        @Override protected int sizeOf(String key, Bitmap value) {
            return 1;                                    // count entries, not bytes - all are THUMB_DP square
        }
    };

    /** Two workers: enough to keep decoding off the UI thread without thrashing IO. */
    private static final ExecutorService IO = Executors.newFixedThreadPool(2);

    /** Posts decoded bitmaps back to their ImageView. */
    private static final Handler UI = new Handler(Looper.getMainLooper());

    private final MainActivity ctx;
    private PopupWindow popup;

    /**
     * @param ctx Host activity – needed for bgImageList and the apply path.
     */
    public BackgroundPopup(MainActivity ctx) {
        this.ctx = ctx;
    }

    /**
     * Show the selector.
     *
     * @param anchor      Any attached view; used only as the window token.
     * @param currentName File name of the active background, or null.
     */
    public void show(View anchor, String currentName) {
        // Guards against WindowManager.BadTokenException if the anchor's
        // activity finished (or the view detached) before this ran - see
        // ColorWheelPopup.show() for the full explanation.
        if (anchor.getWindowToken() == null) return;
        if (ctx.isFinishing() || ctx.isDestroyed()) return;

        final float dp   = ctx.getResources().getDisplayMetrics().density;
        // Two different accents on purpose: bandFill is the SOLID theme accent
        // for the header (tab_active_tint is a ~10% wash and renders nearly
        // invisible as a fill), rippleTint is that wash, correct for ripples.
        final int bandFill  = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        final int accent    = ThemeManager.getColor(ctx, R.color.tab_active_tint);
        final int titleCol  = ThemeManager.getColor(ctx, R.color.skbar_settings_title);
        final int dimCol    = ThemeManager.getColor(ctx, R.color.skbar_section_text_color);
        final int cardBgCol = ThemeManager.getColor(ctx, R.color.skbar_whole_settings);
        final int rowFill   = _blend(cardBgCol, 0xFFFFFFFF, 0.07f);

        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);

        // ── Header ──
        TextView header = new TextView(ctx);
        header.setText("BACKGROUND");
        header.setAllCaps(true);
        header.setLetterSpacing(0.14f);
        header.setTypeface(null, Typeface.BOLD);
        header.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        header.setTextColor(_idealInk(bandFill));          // reads on the solid band, any theme
        header.setGravity(Gravity.CENTER);
        header.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));
        GradientDrawable headerBg = new GradientDrawable();
        headerBg.setColor(bandFill);
        float r = 16 * dp;
        headerBg.setCornerRadii(new float[]{r, r, r, r, 0, 0, 0, 0});
        header.setBackground(headerBg);
        card.addView(header);

        View bar = new View(ctx);
        bar.setLayoutParams(new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (2 * dp)));
        bar.setBackgroundColor(titleCol);
        card.addView(bar);

        // ── Scrolling list ──
        LinearLayout list = new LinearLayout(ctx);
        list.setOrientation(LinearLayout.VERTICAL);
        list.setPadding((int) (10 * dp), (int) (8 * dp), (int) (10 * dp), (int) (8 * dp));

        List<DocumentFile> files = ctx.bgImageList;
        if (files == null || files.isEmpty()) {
            TextView empty = new TextView(ctx);
            empty.setText("NO GIFS FOUND\nPick a folder below");
            empty.setGravity(Gravity.CENTER);
            empty.setTextColor(dimCol);
            empty.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
            empty.setPadding(0, (int) (18 * dp), 0, (int) (18 * dp));
            list.addView(empty);
        } else {
            for (int i = 0; i < files.size(); i++) {
                final DocumentFile f = files.get(i);
                final String name = (f.getName() == null) ? "?" : f.getName();
                boolean active = name.equals(currentName);
                list.addView(_buildRow(f, name, active, dp, rowFill, titleCol, dimCol, accent));
            }
        }

        ScrollView scroll = new ScrollView(ctx);
        scroll.setVerticalScrollBarEnabled(true);
        scroll.addView(list);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        scrollLp.weight = 1f;
        scroll.setLayoutParams(scrollLp);
        card.addView(scroll);

        // ── Folder picker, always reachable at the foot of the card ──
        card.addView(_buildDivider(dp));
        card.addView(_buildAction("CHOOSE FOLDER", dp, accent, titleCol, () -> {
            ctx._openBackgroundFolderPicker();
            if (popup != null) popup.dismiss();
        }));

        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(cardBgCol);
        cardBg.setCornerRadius(16 * dp);
        cardBg.setStroke((int) (1 * dp), ThemeManager.getColor(ctx, R.color.stroke_line_color));
        card.setBackground(cardBg);
        card.setClipToOutline(true);

        // Cap the card so a large folder scrolls instead of filling the screen.
        int maxH = (int) (MAX_LIST_DP * dp);
        scroll.getLayoutParams().height = ViewGroup.LayoutParams.WRAP_CONTENT;
        scroll.setOnTouchListener((v, e) -> {
            v.getParent().requestDisallowInterceptTouchEvent(true);
            return false;
        });

        popup = new PopupWindow(card, (int) (CARD_WIDTH_DP * dp),
                LinearLayout.LayoutParams.WRAP_CONTENT, true);
        popup.setOutsideTouchable(true);
        popup.setFocusable(true);
        popup.setBackgroundDrawable(new ColorDrawable(0));
        popup.setElevation(16 * dp);
        try {
            popup.showAtLocation(anchor, Gravity.CENTER, 0, 0);
        } catch (WindowManager.BadTokenException e) {
            return; // activity finished in the gap between the checks above and here
        }

        card.post(() -> {
            if (scroll.getHeight() > maxH) {
                ViewGroup.LayoutParams lp = scroll.getLayoutParams();
                lp.height = maxH;
                scroll.setLayoutParams(lp);
            }
        });

        _dimBehind(popup);
        _animateIn(card);
    }

    /**
     * One list row: GIF thumbnail, file name, and a tap that applies it.
     *
     * @param f        Backing document.
     * @param name     File name shown next to the thumbnail.
     * @param active   True for the background currently applied.
     * @param dp       Display density.
     * @param rowFill  Row surface colour.
     * @param titleCol Accent/selected colour.
     * @param dimCol   Unselected label colour.
     * @param accent   Theme accent, used for the tap ripple.
     */
    private View _buildRow(DocumentFile f, String name, boolean active, float dp,
                           int rowFill, int titleCol, int dimCol, int accent) {
        LinearLayout row = new LinearLayout(ctx);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding((int) (8 * dp), (int) (8 * dp), (int) (10 * dp), (int) (8 * dp));

        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(10 * dp);
        bg.setColor(rowFill);
        bg.setStroke((int) ((active ? 2 : 1) * dp), active ? titleCol : _blend(rowFill, 0xFFFFFFFF, 0.10f));
        row.setBackground(new RippleDrawable(ColorStateList.valueOf(accent), bg, null));

        LinearLayout.LayoutParams rowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        rowLp.setMargins(0, (int) (3 * dp), 0, (int) (3 * dp));
        row.setLayoutParams(rowLp);

        FrameLayout frame = new FrameLayout(ctx);
        int sz = (int) (THUMB_DP * dp);
        frame.setLayoutParams(new LinearLayout.LayoutParams(sz, sz));
        GradientDrawable thumbBg = new GradientDrawable();
        thumbBg.setCornerRadius(6 * dp);
        thumbBg.setColor(0xFF000000);
        frame.setBackground(thumbBg);
        frame.setClipToOutline(true);

        // A still first frame, not a GifDrawable. Decoding every GIF in the
        // folder in full - and then animating all of them at once - was what
        // made the picker slow to open; a picker only needs to show what each
        // file looks like.
        final android.widget.ImageView thumb = new android.widget.ImageView(ctx);
        thumb.setScaleType(android.widget.ImageView.ScaleType.CENTER_CROP);
        thumb.setLayoutParams(new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        frame.addView(thumb);
        _loadThumb(thumb, f, sz);
        row.addView(frame);

        TextView label = new TextView(ctx);
        label.setText(name);
        label.setMaxLines(2);
        label.setEllipsize(android.text.TextUtils.TruncateAt.MIDDLE);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        label.setTypeface(null, active ? Typeface.BOLD : Typeface.NORMAL);
        label.setTextColor(active ? titleCol : dimCol);
        LinearLayout.LayoutParams lLp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
        lLp.setMargins((int) (10 * dp), 0, 0, 0);
        label.setLayoutParams(lLp);
        row.addView(label);

        row.setOnClickListener(v -> {
            ctx._SetBackgroundImage(f);
            if (popup != null) popup.dismiss();
        });
        return row;
    }

    /**
     * Fill one thumbnail, from cache if possible and off the UI thread if not.
     *
     * The popup is shown before any decoding starts, so it opens immediately
     * and the images arrive as they are ready.
     *
     * @param view   Target image view.
     * @param f      Source document.
     * @param sizePx Target edge length in pixels.
     */
    private void _loadThumb(final android.widget.ImageView view, final DocumentFile f, final int sizePx) {
        final String key = f.getUri().toString();

        Bitmap cached = THUMBS.get(key);
        if (cached != null) {
            view.setImageBitmap(cached);
            return;
        }

        IO.execute(() -> {
            final Bitmap bmp = _decodeThumb(f, sizePx);
            if (bmp == null) return;
            THUMBS.put(key, bmp);
            UI.post(() -> {
                // The card may already be gone by the time this lands.
                if (view.isAttachedToWindow()) view.setImageBitmap(bmp);
            });
        });
    }

    /**
     * Decode a GIF's first frame, downsampled to roughly the display size.
     *
     * Two passes: bounds only, then a real decode with inSampleSize. That keeps
     * the decode cost proportional to the thumbnail rather than to the source
     * image, which for a full-screen background GIF is a large difference.
     *
     * @param f      Source document.
     * @param sizePx Target edge length in pixels.
     *
     * @return Decoded bitmap, or null if the file could not be read.
     */
    private Bitmap _decodeThumb(DocumentFile f, int sizePx) {
        try {
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            try (InputStream in = ctx.getContentResolver().openInputStream(f.getUri())) {
                BitmapFactory.decodeStream(in, null, bounds);
            }

            int sample = 1;
            int longest = Math.max(bounds.outWidth, bounds.outHeight);
            while (sizePx > 0 && (longest / sample) > sizePx * 2) sample <<= 1;

            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inSampleSize = sample;
            opts.inPreferredConfig = Bitmap.Config.RGB_565;   // no alpha needed behind an opaque frame
            try (InputStream in = ctx.getContentResolver().openInputStream(f.getUri())) {
                return BitmapFactory.decodeStream(in, null, opts);
            }
        } catch (Exception e) {
            android.util.Log.e("BG_POPUP", "Thumb decode failed: " + f.getName(), e);
            return null;
        }
    }

    /**
     * Full-width outlined action row at the foot of the card.
     *
     * @param text     Row label.
     * @param dp       Display density.
     * @param accent   Ripple colour.
     * @param titleCol Text and outline colour.
     * @param onTap    Fired on tap.
     */
    private View _buildAction(String text, float dp, int accent, int titleCol, Runnable onTap) {
        TextView row = new TextView(ctx);
        row.setText(text);
        row.setAllCaps(true);
        row.setGravity(Gravity.CENTER);
        row.setTypeface(null, Typeface.BOLD);
        row.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        row.setTextColor(titleCol);
        row.setPadding(0, (int) (12 * dp), 0, (int) (12 * dp));

        GradientDrawable bg = new GradientDrawable();
        bg.setCornerRadius(10 * dp);
        bg.setColor(0x00000000);
        bg.setStroke((int) (1 * dp), titleCol);
        row.setBackground(new RippleDrawable(ColorStateList.valueOf(accent), bg, null));

        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins((int) (12 * dp), (int) (8 * dp), (int) (12 * dp), (int) (12 * dp));
        row.setLayoutParams(lp);

        row.setOnClickListener(v -> onTap.run());
        return row;
    }

    /** Hairline separator above the footer action. */
    private View _buildDivider(float dp) {
        View v = new View(ctx);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, (int) (1 * dp));
        lp.setMargins((int) (12 * dp), (int) (4 * dp), (int) (12 * dp), 0);
        v.setLayoutParams(lp);
        v.setBackgroundColor((ThemeManager.getColor(ctx, R.color.stroke_line_color) & 0x00FFFFFF) | 0x50000000);
        return v;
    }

    /**
     * Mix two ARGB colours.
     *
     * @param a First colour.
     * @param b Second colour.
     * @param t Weight of b, 0..1.
     */
    /**
     * Black or white ink for a background, by perceived luminance - keeps the
     * header legible on any theme accent.
     *
     * @param bg Background ARGB (alpha ignored).
     */
    private int _idealInk(int bg) {
        int r = (bg >> 16) & 0xFF, g = (bg >> 8) & 0xFF, b = bg & 0xFF;
        return ((r * 299 + g * 587 + b * 114) / 1000 > 128) ? 0xFF111111 : 0xFFF3ECE2;
    }

    private int _blend(int a, int b, float t) {
        int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
        int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
        return 0xFF000000
                | ((int) (ar + (br - ar) * t) << 16)
                | ((int) (ag + (bg - ag) * t) << 8)
                | ((int) (ab + (bb - ab) * t));
    }

    /** Dim the window behind the card, matching the other popups. */
    private void _dimBehind(PopupWindow p) {
        View container = p.getContentView().getRootView();
        android.view.WindowManager wm =
                (android.view.WindowManager) ctx.getSystemService(Context.WINDOW_SERVICE);
        android.view.WindowManager.LayoutParams lp =
                (android.view.WindowManager.LayoutParams) container.getLayoutParams();
        lp.flags |= android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND;
        lp.dimAmount = DIM_AMOUNT;
        wm.updateViewLayout(container, lp);
    }

    /** Short scale/fade entry so the card does not simply appear. */
    private void _animateIn(View card) {
        card.setAlpha(0f);
        card.setScaleX(0.92f);
        card.setScaleY(0.92f);
        card.animate()
                .alpha(1f).scaleX(1f).scaleY(1f)
                .setDuration(160)
                .setInterpolator(new DecelerateInterpolator())
                .start();
    }
}
