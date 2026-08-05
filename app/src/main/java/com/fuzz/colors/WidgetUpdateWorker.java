package com.fuzz.colors;

/*
 * ============================================================
 *  WidgetUpdateWorker.java
 * ============================================================
 *  WorkManager Worker driving both widgets' background refresh - runs
 *  WidgetStatusFetcher.fetch() (headless local-UDP-then-MQTT-cloud, same
 *  fallback order as the live app), caches whatever it got into
 *  SharedPreferences, then pushes the result onto every placed instance
 *  of BOTH FuzzWidgetProvider (4x1 strip) and FuzzWidgetStackProvider
 *  (1x3 stack) - one fetch, two widget shapes. Scheduled periodically via
 *  WidgetScheduling.ensurePeriodicRefresh() (called from either
 *  provider's onEnabled()) and enqueued once on demand from
 *  FuzzWidgetProvider's ACTION_REFRESH handler.
 * ============================================================
 */

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.NonNull;
import androidx.work.Worker;
import androidx.work.WorkerParameters;

public class WidgetUpdateWorker extends Worker {

    public WidgetUpdateWorker(@NonNull Context context, @NonNull WorkerParameters params) {
        super(context, params);
    }

    @NonNull
    @Override
    public Result doWork() {
        Context ctx = getApplicationContext();
        WidgetStatusFetcher.Result result = WidgetStatusFetcher.fetch(ctx);

        if (result != null) {
            SharedPreferences.Editor editor = ctx.getSharedPreferences(
                    WidgetScheduling.PREFS_NAME, Context.MODE_PRIVATE).edit();
            if (result.tempC != null)       editor.putInt(WidgetScheduling.KEY_TEMP, result.tempC);
            if (result.humPct != null)      editor.putInt(WidgetScheduling.KEY_HUM, result.humPct);
            if (result.diffuserPct != null) editor.putInt(WidgetScheduling.KEY_DIFFUSER_PCT, result.diffuserPct);
            if (result.noWater != null)     editor.putBoolean(WidgetScheduling.KEY_NO_WATER, result.noWater);
            editor.putString(WidgetScheduling.KEY_TRANSPORT, result.transport);
            editor.putLong(WidgetScheduling.KEY_LAST_UPDATE, System.currentTimeMillis());
            editor.apply();
        }
        // Whether or not this attempt actually reached the board: refresh
        // every placed widget (both shapes) so the strip's "Refreshing…"
        // state clears either way. A board that's simply offline isn't a
        // worker failure (that would trigger WorkManager's own retry/
        // backoff for no reason) - it's just next cycle's problem, same as
        // the app's own transport supervisor treats a dropped link.
        FuzzWidgetProvider.updateAllWidgets(ctx);
        FuzzWidgetStackProvider.updateAllWidgets(ctx);

        return Result.success();
    }
}
