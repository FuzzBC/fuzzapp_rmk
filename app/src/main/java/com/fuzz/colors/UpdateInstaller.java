package com.fuzz.colors;

import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;

import androidx.core.content.ContextCompat;

/**
 * Downloads an update APK via DownloadManager, reporting live progress
 * (percent/bytes/speed) to a listener, and hands the finished file to the
 * system installer via DownloadManager.getUriForDownloadedFile() - a
 * content:// URI backed by the DownloadProvider itself, so no
 * FileProvider/manifest <provider> is needed to share it.
 */
public class UpdateInstaller {

    private static final long POLL_MS = 400;

    public interface ProgressListener {
        /** percent 0-100 (-1 if not yet known); speedBps in bytes/sec (0 if not yet known). */
        void onProgress(int percent, long downloaded, long total, double speedBps);
        void onComplete();
        void onFailed(String reason);
    }

    private final Context appCtx;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private long downloadId = -1;
    private BroadcastReceiver receiver;
    private ProgressListener listener;
    private long lastPollBytes = 0;
    private long lastPollTime = 0;
    private final Runnable pollRunnable = this::poll;

    public UpdateInstaller(Context ctx) {
        this.appCtx = ctx.getApplicationContext();
    }

    public void download(String apkUrl, String versionTag, ProgressListener listener) {
        this.listener = listener;
        DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
        if (dm == null) {
            if (listener != null) listener.onFailed("DownloadManager unavailable");
            return;
        }

        String fileName = "FuZzLED_" + versionTag + ".apk";
        DownloadManager.Request req = new DownloadManager.Request(Uri.parse(apkUrl))
                .setTitle("FuZz LED " + versionTag)
                .setDescription("Downloading update...")
                .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                .setDestinationInExternalFilesDir(appCtx, Environment.DIRECTORY_DOWNLOADS, fileName)
                .setMimeType("application/vnd.android.package-archive");

        registerReceiver();
        downloadId = dm.enqueue(req);
        lastPollBytes = 0;
        lastPollTime = System.currentTimeMillis();
        mainHandler.postDelayed(pollRunnable, POLL_MS);
    }

    /** Aborts the in-flight download; safe to call even if it already finished. */
    public void cancel() {
        mainHandler.removeCallbacks(pollRunnable);
        unregister();
        if (downloadId != -1) {
            DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
            if (dm != null) dm.remove(downloadId);
        }
        listener = null;
    }

    private void poll() {
        if (downloadId == -1) return;
        DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
        if (dm == null) return;

        try (Cursor c = dm.query(new DownloadManager.Query().setFilterById(downloadId))) {
            if (c == null || !c.moveToFirst()) {
                mainHandler.postDelayed(pollRunnable, POLL_MS);
                return;
            }
            int statusIdx = c.getColumnIndex(DownloadManager.COLUMN_STATUS);
            int soFarIdx = c.getColumnIndex(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR);
            int totalIdx = c.getColumnIndex(DownloadManager.COLUMN_TOTAL_SIZE_BYTES);
            int status = statusIdx >= 0 ? c.getInt(statusIdx) : -1;
            long downloaded = soFarIdx >= 0 ? c.getLong(soFarIdx) : 0;
            long total = totalIdx >= 0 ? c.getLong(totalIdx) : -1;

            if (status == DownloadManager.STATUS_FAILED) {
                int reasonIdx = c.getColumnIndex(DownloadManager.COLUMN_REASON);
                int reason = reasonIdx >= 0 ? c.getInt(reasonIdx) : -1;
                if (listener != null) listener.onFailed("Download failed (reason " + reason + ")");
                return; // receiver still fires ACTION_DOWNLOAD_COMPLETE on failure too, but stop polling now
            }

            long now = System.currentTimeMillis();
            double elapsedSec = Math.max(0.001, (now - lastPollTime) / 1000.0);
            double speedBps = Math.max(0, downloaded - lastPollBytes) / elapsedSec;
            lastPollBytes = downloaded;
            lastPollTime = now;

            int percent = total > 0 ? (int) ((downloaded * 100L) / total) : -1;
            if (listener != null) listener.onProgress(percent, downloaded, total, speedBps);

            if (status != DownloadManager.STATUS_SUCCESSFUL) {
                mainHandler.postDelayed(pollRunnable, POLL_MS);
            }
            // on STATUS_SUCCESSFUL, stop polling - the completion broadcast (below) drives onComplete()/install
        }
    }

    private void registerReceiver() {
        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                long id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1);
                if (id != downloadId) return;
                unregister();
                mainHandler.removeCallbacks(pollRunnable);
                boolean installed = promptInstall();
                if (listener != null) {
                    if (installed) listener.onComplete();
                    else listener.onFailed("Download did not complete successfully");
                }
            }
        };
        // ACTION_DOWNLOAD_COMPLETE is sent by the system's DownloadManager
        // service (a different process/UID), not by this app itself - it
        // needs RECEIVER_EXPORTED or it's silently never delivered (no
        // error, the receiver just never fires). Harmless to export: the
        // handler already ignores anything whose id doesn't match our own
        // in-flight downloadId.
        IntentFilter filter = new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE);
        ContextCompat.registerReceiver(appCtx, receiver, filter, ContextCompat.RECEIVER_EXPORTED);
    }

    private boolean promptInstall() {
        DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
        if (dm == null) return false;

        Uri fileUri = dm.getUriForDownloadedFile(downloadId);
        if (fileUri == null) return false; // download failed/removed - nothing to install

        String mime = dm.getMimeTypeForDownloadedFile(downloadId);
        if (mime == null) mime = "application/vnd.android.package-archive";

        Intent installIntent = new Intent(Intent.ACTION_VIEW);
        installIntent.setDataAndType(fileUri, mime);
        installIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        appCtx.startActivity(installIntent);
        return true;
    }

    public void unregister() {
        if (receiver != null) {
            try {
                appCtx.unregisterReceiver(receiver);
            } catch (IllegalArgumentException ignored) {
                // already unregistered
            }
            receiver = null;
        }
    }
}
