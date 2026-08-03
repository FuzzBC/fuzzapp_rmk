package com.fuzz.colors;

import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.Uri;
import android.os.Environment;

import androidx.core.content.ContextCompat;

/**
 * Downloads an update APK via DownloadManager and hands it to the system
 * installer once complete. Uses DownloadManager.getUriForDownloadedFile(),
 * which returns a content:// URI backed by the DownloadProvider itself -
 * no FileProvider/manifest <provider> needed to share it with the installer.
 */
public class UpdateInstaller {

    private final Context appCtx;
    private long downloadId = -1;
    private BroadcastReceiver receiver;

    public UpdateInstaller(Context ctx) {
        this.appCtx = ctx.getApplicationContext();
    }

    public void download(String apkUrl, String versionTag) {
        DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
        if (dm == null) return;

        String fileName = "FuZzLED_" + versionTag + ".apk";
        DownloadManager.Request req = new DownloadManager.Request(Uri.parse(apkUrl))
                .setTitle("FuZz LED " + versionTag)
                .setDescription("Downloading update...")
                .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED)
                .setDestinationInExternalFilesDir(appCtx, Environment.DIRECTORY_DOWNLOADS, fileName)
                .setMimeType("application/vnd.android.package-archive");

        registerReceiver();
        downloadId = dm.enqueue(req);
    }

    private void registerReceiver() {
        receiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context c, Intent intent) {
                long id = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1);
                if (id != downloadId) return;
                unregister();
                promptInstall();
            }
        };
        IntentFilter filter = new IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE);
        ContextCompat.registerReceiver(appCtx, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);
    }

    private void promptInstall() {
        DownloadManager dm = (DownloadManager) appCtx.getSystemService(Context.DOWNLOAD_SERVICE);
        if (dm == null) return;

        Uri fileUri = dm.getUriForDownloadedFile(downloadId);
        if (fileUri == null) return; // download failed/removed - nothing to install

        String mime = dm.getMimeTypeForDownloadedFile(downloadId);
        if (mime == null) mime = "application/vnd.android.package-archive";

        Intent installIntent = new Intent(Intent.ACTION_VIEW);
        installIntent.setDataAndType(fileUri, mime);
        installIntent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
        appCtx.startActivity(installIntent);
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
