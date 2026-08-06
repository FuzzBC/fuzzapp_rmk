package com.fuzz.colors;

/*
 * ============================================================
 *  FuzzWidgetProvider.java
 * ============================================================
 *  Home-screen widget (4x1 strip): temp / hum / diffuser refill % on one
 *  row + a connection icon (local WiFi vs MQTT cloud), refreshed by
 *  WidgetUpdateWorker (WorkManager periodic job - see WidgetScheduling,
 *  shared with FuzzWidgetStackProvider's 1x3 variant) since the OS's own
 *  updatePeriodMillis is floored at 30 min and can't go lower (see
 *  widget_fuzz_info.xml). Values are read from a small SharedPreferences
 *  cache the worker writes to - this class never talks to the board
 *  directly, see WidgetStatusFetcher for that.
 *
 *  Tapping the widget body opens the app (same as the launcher icon);
 *  tapping the small refresh glyph instead fires ACTION_REFRESH back at
 *  this same receiver, which flips every placed instance to a
 *  "Refreshing…" state immediately and kicks a one-off fetch - the
 *  network round trip itself can take a few seconds (see
 *  WidgetStatusFetcher's timeouts), and a widget that just sits there
 *  after being tapped reads as broken rather than "working on it".
 * ============================================================
 */

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Build;
import android.util.Log;
import android.widget.RemoteViews;

public class FuzzWidgetProvider extends AppWidgetProvider {

    @Override
    public void onUpdate(Context context, AppWidgetManager manager, int[] widgetIds) {
        // Re-affirms the periodic job (idempotent, KEEP policy) on every system-triggered
        // update, not just the one-time onEnabled() transition - the OS (Doze, OEM battery
        // managers, "unused apps" hibernation) can silently drop the WorkManager job, and
        // onEnabled() alone never notices, leaving the widget stuck showing stale data.
        WidgetScheduling.ensurePeriodicRefresh(context);
        for (int id : widgetIds) _safeUpdate(context, manager, id, false);
    }

    @Override
    public void onEnabled(Context context) {
        WidgetScheduling.ensurePeriodicRefresh(context);
    }

    @Override
    public void onDisabled(Context context) {
        WidgetScheduling.cancelIfNothingPlaced(context);
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        super.onReceive(context, intent);
        if (!WidgetScheduling.ACTION_REFRESH.equals(intent.getAction())) return;

        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, FuzzWidgetProvider.class));
        for (int id : ids) _safeUpdate(context, manager, id, true);   // "Refreshing…" immediately, values unchanged

        WidgetScheduling.triggerManualRefresh(context);
    }

    /** Called by WidgetUpdateWorker once a fetch attempt finishes, to push the fresh (or still-stale) cache onto every placed instance. */
    static void updateAllWidgets(Context context) {
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, FuzzWidgetProvider.class));
        for (int id : ids) _safeUpdate(context, manager, id, false);
    }

    /**
     * updateAppWidget(), guarded: any exception building/pushing the real
     * views falls back to a bare-bones RemoteViews showing the error
     * instead of letting the widget host render its own generic "broken
     * widget" placeholder - much easier to see what actually went wrong
     * from a screenshot, and the widget never just goes blank.
     */
    private static void _safeUpdate(Context context, AppWidgetManager manager, int widgetId, boolean refreshing) {
        try {
            manager.updateAppWidget(widgetId, buildViews(context, refreshing));
        } catch (Throwable t) {
            Log.e("FuzzWidget", "buildViews() failed for widget " + widgetId, t);
            RemoteViews fallback = new RemoteViews(context.getPackageName(), android.R.layout.simple_list_item_1);
            fallback.setTextViewText(android.R.id.text1, "FuZz widget error: " + t.getClass().getSimpleName());
            manager.updateAppWidget(widgetId, fallback);
        }
    }

    private static RemoteViews buildViews(Context context, boolean refreshing) {
        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.widget_fuzz_strip);
        SharedPreferences prefs = context.getSharedPreferences(WidgetScheduling.PREFS_NAME, Context.MODE_PRIVATE);

        int temp = prefs.getInt(WidgetScheduling.KEY_TEMP, Integer.MIN_VALUE);
        int hum  = prefs.getInt(WidgetScheduling.KEY_HUM, Integer.MIN_VALUE);
        int pct  = prefs.getInt(WidgetScheduling.KEY_DIFFUSER_PCT, Integer.MIN_VALUE);
        boolean noWater = prefs.getBoolean(WidgetScheduling.KEY_NO_WATER, false);
        String transport = prefs.getString(WidgetScheduling.KEY_TRANSPORT, null);
        long lastUpdate  = prefs.getLong(WidgetScheduling.KEY_LAST_UPDATE, 0L);

        views.setTextViewText(R.id.widget_temp, temp == Integer.MIN_VALUE ? "--°" : temp + "°c");
        views.setTextViewText(R.id.widget_hum,  hum  == Integer.MIN_VALUE ? "--%" : hum + "%");
        WidgetScheduling.applyDiffuserState(views, R.id.widget_diffuser_icon, R.id.widget_diffuser, context, pct, noWater);
        // ic_wifi_4 (bars glyph, same as the in-app WiFi indicator) vs
        // ic_widget_cloud - picked for how distinct their SHAPES are at a
        // glance; ic_arduino (a chip glyph) used to sit here but didn't
        // read as "WiFi" to anyone glancing at the widget.
        // Both the real icon AND its offset shadow copy (see the icon's
        // FrameLayout wrapper) must switch together - otherwise the
        // shadow keeps showing the OLD shape underneath the new one
        // (e.g. wifi-bars shadow behind a cloud icon) and it looks like
        // two different icons stacked on top of each other.
        int connIconRes = "CLOUD".equals(transport) ? R.drawable.ic_widget_cloud : R.drawable.ic_wifi_4;
        views.setImageViewResource(R.id.widget_conn_icon, connIconRes);
        views.setImageViewResource(R.id.widget_conn_icon_shadow, connIconRes);

        views.setTextViewText(R.id.widget_last_update, refreshing
                ? context.getString(R.string.widget_refreshing)
                : context.getString(R.string.widget_updated_fmt, _formatTime(lastUpdate)));

        int flags = PendingIntent.FLAG_UPDATE_CURRENT
                | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);

        // Tap the widget body -> open the app, same as the launcher icon.
        Intent openApp = new Intent(context, MainActivity.class);
        openApp.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        views.setOnClickPendingIntent(R.id.widget_root,
                PendingIntent.getActivity(context, 0, openApp, flags));

        // Tap the refresh glyph -> ACTION_REFRESH back at this receiver (handled in onReceive() above), not the open-app intent above.
        Intent refresh = new Intent(context, FuzzWidgetProvider.class);
        refresh.setAction(WidgetScheduling.ACTION_REFRESH);
        views.setOnClickPendingIntent(R.id.widget_refresh_btn,
                PendingIntent.getBroadcast(context, 0, refresh, flags));

        return views;
    }

    private static String _formatTime(long millis) {
        if (millis <= 0) return "--:--";
        return new java.text.SimpleDateFormat("HH:mm", java.util.Locale.getDefault()).format(new java.util.Date(millis));
    }
}
