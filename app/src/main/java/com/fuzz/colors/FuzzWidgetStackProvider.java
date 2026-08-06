package com.fuzz.colors;

/*
 * ============================================================
 *  FuzzWidgetStackProvider.java
 * ============================================================
 *  Home-screen widget (1x4 stack): temp / hum / diffuser refill %, one
 *  row under another, a connection-status icon badged top-right with a
 *  refresh button right under it, and the last-refresh time under the
 *  diffuser row. Shares FuzzWidgetProvider's data cache and background-
 *  refresh job (WidgetScheduling/WidgetUpdateWorker) - this is just a
 *  second, narrower shape of the same numbers.
 *
 *  Tapping the widget body opens the app; tapping the refresh glyph
 *  instead fires ACTION_REFRESH back at this receiver - same two-step
 *  "show Refreshing… immediately, then enqueue the real fetch" pattern
 *  as FuzzWidgetProvider, see that class's doc comment for why.
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

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class FuzzWidgetStackProvider extends AppWidgetProvider {

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
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, FuzzWidgetStackProvider.class));
        for (int id : ids) _safeUpdate(context, manager, id, true);   // "Refreshing…" immediately, values unchanged

        WidgetScheduling.triggerManualRefresh(context);
    }

    /** Called by WidgetUpdateWorker once a fetch attempt finishes, to push the fresh (or still-stale) cache onto every placed instance. */
    static void updateAllWidgets(Context context) {
        AppWidgetManager manager = AppWidgetManager.getInstance(context);
        int[] ids = manager.getAppWidgetIds(new ComponentName(context, FuzzWidgetStackProvider.class));
        for (int id : ids) _safeUpdate(context, manager, id, false);
    }

    /** updateAppWidget(), guarded - see FuzzWidgetProvider._safeUpdate() for why. */
    private static void _safeUpdate(Context context, AppWidgetManager manager, int widgetId, boolean refreshing) {
        try {
            manager.updateAppWidget(widgetId, buildViews(context, refreshing));
        } catch (Throwable t) {
            Log.e("FuzzWidget", "buildViews() failed for stack widget " + widgetId, t);
            RemoteViews fallback = new RemoteViews(context.getPackageName(), android.R.layout.simple_list_item_1);
            fallback.setTextViewText(android.R.id.text1, "FuZz widget error: " + t.getClass().getSimpleName());
            manager.updateAppWidget(widgetId, fallback);
        }
    }

    private static RemoteViews buildViews(Context context, boolean refreshing) {
        RemoteViews views = new RemoteViews(context.getPackageName(), R.layout.widget_fuzz_stack);
        SharedPreferences prefs = context.getSharedPreferences(WidgetScheduling.PREFS_NAME, Context.MODE_PRIVATE);

        int temp = prefs.getInt(WidgetScheduling.KEY_TEMP, Integer.MIN_VALUE);
        int hum  = prefs.getInt(WidgetScheduling.KEY_HUM, Integer.MIN_VALUE);
        int pct  = prefs.getInt(WidgetScheduling.KEY_DIFFUSER_PCT, Integer.MIN_VALUE);
        boolean noWater = prefs.getBoolean(WidgetScheduling.KEY_NO_WATER, false);
        String transport = prefs.getString(WidgetScheduling.KEY_TRANSPORT, null);
        long lastUpdate  = prefs.getLong(WidgetScheduling.KEY_LAST_UPDATE, 0L);

        views.setTextViewText(R.id.widget_stack_temp, temp == Integer.MIN_VALUE ? "--°" : temp + "°c");
        views.setTextViewText(R.id.widget_stack_hum,  hum  == Integer.MIN_VALUE ? "--%" : hum + "%");
        WidgetScheduling.applyDiffuserState(views, R.id.widget_stack_diffuser_icon, R.id.widget_stack_diffuser, context, pct, noWater);
        // Both the real icon AND its offset shadow copy must switch
        // together - see FuzzWidgetProvider.buildViews() for why.
        int connIconRes = "CLOUD".equals(transport) ? R.drawable.ic_widget_cloud : R.drawable.ic_wifi_4;
        views.setImageViewResource(R.id.widget_stack_conn_icon, connIconRes);
        views.setImageViewResource(R.id.widget_stack_conn_icon_shadow, connIconRes);

        views.setTextViewText(R.id.widget_stack_last_update, refreshing
                ? context.getString(R.string.widget_refreshing)
                : context.getString(R.string.widget_updated_fmt, _formatTime(lastUpdate)));

        int flags = PendingIntent.FLAG_UPDATE_CURRENT
                | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);

        // Tap the widget body -> open the app, same as the launcher icon.
        Intent openApp = new Intent(context, MainActivity.class);
        openApp.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        views.setOnClickPendingIntent(R.id.widget_stack_root,
                PendingIntent.getActivity(context, 0, openApp, flags));

        // Tap the refresh glyph -> ACTION_REFRESH back at this receiver (handled in onReceive() above), not the open-app intent above.
        Intent refresh = new Intent(context, FuzzWidgetStackProvider.class);
        refresh.setAction(WidgetScheduling.ACTION_REFRESH);
        views.setOnClickPendingIntent(R.id.widget_stack_refresh_btn,
                PendingIntent.getBroadcast(context, 0, refresh, flags));

        return views;
    }

    private static String _formatTime(long millis) {
        if (millis <= 0) return "--:--";
        return new SimpleDateFormat("HH:mm", Locale.getDefault()).format(new Date(millis));
    }
}
