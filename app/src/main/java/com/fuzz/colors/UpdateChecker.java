package com.fuzz.colors;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Checks a public "releases-only" GitHub repo for a newer version than the
 * one currently installed, and hands back the APK asset URL to download.
 *
 * Release tags on that repo must look like "vNN" where NN is the numeric
 * versionCode (e.g. "v3") - this is compared directly against
 * BuildConfig/PackageInfo versionCode, sidestepping any semantic-version
 * string parsing.
 */
public class UpdateChecker {

    private static final String OWNER = "FuzzBC";
    private static final String REPO  = "fuzzapp-releases";
    private static final String API_URL =
            "https://api.github.com/repos/" + OWNER + "/" + REPO + "/releases/latest";

    private static final ExecutorService IO = Executors.newSingleThreadExecutor();
    private static final Handler MAIN = new Handler(Looper.getMainLooper());

    public interface Callback {
        void onUpdateAvailable(String tagName, int versionCode, String apkUrl, String releaseNotes);
        void onUpToDate();
        void onError(String message);
    }

    public static void check(Context ctx, Callback cb) {
        final Context appCtx = ctx.getApplicationContext();
        Log.d("UpdateChecker", "check() starting, GET " + API_URL);
        IO.execute(() -> {
            try {
                HttpURLConnection conn = (HttpURLConnection) new URL(API_URL).openConnection();
                conn.setRequestProperty("Accept", "application/vnd.github+json");
                conn.setConnectTimeout(8000);
                conn.setReadTimeout(8000);

                int code = conn.getResponseCode();
                Log.d("UpdateChecker", "HTTP response code: " + code);
                if (code != 200) {
                    postError(cb, "Update check failed (HTTP " + code + ")");
                    return;
                }
                String body = readAll(conn.getInputStream());
                JSONObject json = new JSONObject(body);
                String tagName = json.optString("tag_name", "");
                int remoteCode = parseVersionCode(tagName);
                int localCode = getLocalVersionCode(appCtx);
                Log.d("UpdateChecker", "tag=" + tagName + " remoteCode=" + remoteCode + " localCode=" + localCode);

                if (remoteCode <= 0 || remoteCode <= localCode) {
                    Log.d("UpdateChecker", "up to date (or unparseable tag) - not offering update");
                    postUpToDate(cb);
                    return;
                }

                String apkUrl = findApkUrl(json.optJSONArray("assets"));
                Log.d("UpdateChecker", "apkUrl=" + apkUrl);
                if (apkUrl == null) {
                    postError(cb, "Release " + tagName + " has no APK asset");
                    return;
                }

                String notes = json.optString("body", "");
                Log.d("UpdateChecker", "update available, posting to callback");
                postAvailable(cb, tagName, remoteCode, apkUrl, notes);
            } catch (Exception e) {
                Log.w("UpdateChecker", "check failed", e);
                postError(cb, e.getMessage());
            } catch (Throwable t) {
                Log.e("UpdateChecker", "check failed with Throwable (not Exception)", t);
                postError(cb, String.valueOf(t.getMessage()));
            }
        });
    }

    @SuppressWarnings("deprecation") // PackageInfo.versionCode is the only API available below minSdk 21's ceiling of API 28 (P), where getLongVersionCode() was introduced
    private static int getLocalVersionCode(Context appCtx) {
        try {
            PackageInfo info = appCtx.getPackageManager().getPackageInfo(appCtx.getPackageName(), 0);
            // versionCode here is always a small counter (see version.properties) - safe to
            // narrow the 28+ long form back to int rather than plumb a long through the caller.
            return Build.VERSION.SDK_INT >= Build.VERSION_CODES.P
                    ? (int) info.getLongVersionCode()
                    : info.versionCode;
        } catch (PackageManager.NameNotFoundException e) {
            return Integer.MAX_VALUE; // unknown -> never offer an update
        }
    }

    private static String findApkUrl(JSONArray assets) {
        if (assets == null) return null;
        for (int i = 0; i < assets.length(); i++) {
            JSONObject a = assets.optJSONObject(i);
            if (a == null) continue;
            String name = a.optString("name", "");
            if (name.toLowerCase(java.util.Locale.ROOT).endsWith(".apk")) {
                return a.optString("browser_download_url", null);
            }
        }
        return null;
    }

    /**
     * Extracts the numeric versionCode from a release tag. Current scheme is
     * "V<versionName>" (e.g. "V1.003") - the versionCode is the run of digits
     * after the last '.'. Falls back to the first digit run for older-style
     * tags (e.g. a bare "v3") so it isn't brittle if the scheme changes again.
     */
    private static int parseVersionCode(String tag) {
        int lastDot = tag.lastIndexOf('.');
        String segment = (lastDot >= 0 && lastDot < tag.length() - 1) ? tag.substring(lastDot + 1) : tag;

        StringBuilder digits = new StringBuilder();
        boolean started = false;
        for (int i = 0; i < segment.length(); i++) {
            char c = segment.charAt(i);
            if (Character.isDigit(c)) {
                digits.append(c);
                started = true;
            } else if (started) {
                break;
            }
        }
        if (digits.length() == 0) return -1;
        try {
            return Integer.parseInt(digits.toString());
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    private static String readAll(InputStream is) throws Exception {
        StringBuilder sb = new StringBuilder();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(is, StandardCharsets.UTF_8))) {
            String line;
            while ((line = r.readLine()) != null) sb.append(line);
        }
        return sb.toString();
    }

    private static void postAvailable(Callback cb, String tag, int code, String url, String notes) {
        MAIN.post(() -> cb.onUpdateAvailable(tag, code, url, notes));
    }

    private static void postUpToDate(Callback cb) {
        MAIN.post(cb::onUpToDate);
    }

    private static void postError(Callback cb, String msg) {
        MAIN.post(() -> cb.onError(msg));
    }
}
