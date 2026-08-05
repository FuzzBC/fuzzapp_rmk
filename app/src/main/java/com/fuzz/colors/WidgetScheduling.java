package com.fuzz.colors;

/*
 * ============================================================
 *  WidgetScheduling.java
 * ============================================================
 *  Shared background-refresh plumbing for every widget this app ships
 *  (currently FuzzWidgetProvider - 4x1 strip - and FuzzWidgetStackProvider
 *  - 1x3 stack). They're just two different shapes of the same data, so
 *  they share one SharedPreferences cache and exactly one periodic
 *  WorkManager job - not one job per provider, which would double the
 *  network/battery cost for no benefit and require its own coordination
 *  to avoid two fetches racing each other.
 *
 *  Centralised here rather than duplicated per-provider so onEnabled()/
 *  onDisabled() from either one can't drift out of sync with each other -
 *  in particular cancelIfNothingPlaced() has to check BOTH providers'
 *  placed instances before actually cancelling, since removing widget
 *  type A while type B is still on a home screen must not kill B's
 *  refreshes.
 * ============================================================
 */

import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.util.Log;
import android.widget.RemoteViews;

import androidx.core.content.ContextCompat;
import androidx.work.ExistingPeriodicWorkPolicy;
import androidx.work.ExistingWorkPolicy;
import androidx.work.OneTimeWorkRequest;
import androidx.work.PeriodicWorkRequest;
import androidx.work.WorkManager;

import java.util.concurrent.TimeUnit;

final class WidgetScheduling {
    private WidgetScheduling() { }

    // ---- Shared data cache (written by WidgetUpdateWorker, read by both providers) ----
    static final String PREFS_NAME       = "FuZz_Widget";
    static final String KEY_TEMP         = "temp";
    static final String KEY_HUM          = "hum";
    static final String KEY_DIFFUSER_PCT = "diffuserPct";
    static final String KEY_NO_WATER     = "diffuserNoWater";   // true only while the board's 's' packet reports STS_Diffuser.NO_WATER
    static final String KEY_TRANSPORT    = "transport";   // "WIFI" or "CLOUD" - see WidgetStatusFetcher.Result
    static final String KEY_LAST_UPDATE  = "lastUpdateMillis";

    /** Broadcast either provider's refresh icon sends back to itself. */
    static final String ACTION_REFRESH = "com.fuzz.colors.ACTION_WIDGET_REFRESH";

    private static final String WORK_PERIODIC = "fuzz_widget_periodic_refresh";
    private static final String WORK_MANUAL   = "fuzz_widget_manual_refresh";
    private static final long   PERIODIC_MINUTES = 30;   // WorkManager's own floor is 15 - 30 matches the "every 15/30 min" ask at the gentler end, battery-wise

    /** Every widget provider class this app ships - checked by cancelIfNothingPlaced() before it tears down the shared job. */
    private static final Class<?>[] PROVIDERS = { FuzzWidgetProvider.class, FuzzWidgetStackProvider.class };

    /** Call from onEnabled() - idempotent (KEEP policy), safe to call from either provider regardless of what's already running. */
    static void ensurePeriodicRefresh(Context context) {
        try {
            PeriodicWorkRequest request = new PeriodicWorkRequest.Builder(
                    WidgetUpdateWorker.class, PERIODIC_MINUTES, TimeUnit.MINUTES)
                    .build();
            WorkManager.getInstance(context).enqueueUniquePeriodicWork(
                    WORK_PERIODIC, ExistingPeriodicWorkPolicy.KEEP, request);
        } catch (Throwable t) {
            Log.e("FuzzWidget", "ensurePeriodicRefresh failed", t);
        }
    }

    /** Call from onDisabled() - only actually cancels once every provider reports zero placed instances. */
    static void cancelIfNothingPlaced(Context context) {
        try {
            AppWidgetManager manager = AppWidgetManager.getInstance(context);
            for (Class<?> provider : PROVIDERS) {
                if (manager.getAppWidgetIds(new ComponentName(context, provider)).length > 0) return;
            }
            WorkManager.getInstance(context).cancelUniqueWork(WORK_PERIODIC);
        } catch (Throwable t) {
            Log.e("FuzzWidget", "cancelIfNothingPlaced failed", t);
        }
    }

    /** Call from onReceive(ACTION_REFRESH) after pushing the "Refreshing…" state - enqueues the one-off fetch that actually refreshes both providers (see WidgetUpdateWorker). */
    static void triggerManualRefresh(Context context) {
        try {
            OneTimeWorkRequest request = new OneTimeWorkRequest.Builder(WidgetUpdateWorker.class).build();
            WorkManager.getInstance(context).enqueueUniqueWork(WORK_MANUAL, ExistingWorkPolicy.REPLACE, request);
        } catch (Throwable t) {
            Log.e("FuzzWidget", "triggerManualRefresh failed", t);
        }
    }

    /** Refill percent at/below which the diffuser reads as "needs a refill soon" - matches StatusManager.applyDiffuserUsage()'s own red threshold. */
    private static final int REFILL_LOW_THRESHOLD = 20;

    /**
     * Shared by both providers' buildViews(): tints the diffuser icon and
     * sets its accompanying text for the current state - out of water
     * (blue, percent unchanged) takes priority over a low refill percent
     * (red, text replaced with "REF"), which in turn overrides the normal
     * violet/percent look. setColorFilter is a genuine RemoteViews-safe
     * reflective call (single-int-arg, matches ImageView.setColorFilter(int)) -
     * that's how a static XML android:tint gets overridden at runtime.
     *
     * @param views    The RemoteViews being built.
     * @param iconId   The diffuser ImageView's id (widget_diffuser_icon / widget_stack_diffuser_icon).
     * @param textId   The accompanying value TextView's id.
     * @param ctx      For resolving the color resources to actual ARGB ints.
     * @param pct      Cached refill percent, or Integer.MIN_VALUE if unknown.
     * @param noWater  Cached "board reports NO_WATER" flag.
     */
    static void applyDiffuserState(RemoteViews views, int iconId, int textId, Context ctx, int pct, boolean noWater) {
        int colorRes;
        String text;
        if (noWater) {
            colorRes = R.color.status_no_water_blue;
            text = (pct == Integer.MIN_VALUE) ? "--%" : pct + "%";
        } else if (pct != Integer.MIN_VALUE && pct <= REFILL_LOW_THRESHOLD) {
            colorRes = R.color.status_off_red;
            text = "REF";
        } else {
            colorRes = R.color.status_parfum_violet;
            text = (pct == Integer.MIN_VALUE) ? "--%" : pct + "%";
        }
        views.setInt(iconId, "setColorFilter", ContextCompat.getColor(ctx, colorRes));
        views.setTextViewText(textId, text);
    }
}
