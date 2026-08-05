package com.fuzz.colors;

/*
 * ============================================================
 *  MainActivity.java  (alias: Main)
 * ============================================================
 *  Responsibility:
 *      Orchestrates all subsystem classes.  Main owns the
 *      lifecycle (onCreate / onResume / onPause / onDestroy),
 *      the global UI helpers (Toast, Console, LoadingBar,
 *      layout switching, background images, shake sensor),
 *      and the Debug dialog.  All real work is delegated to:
 *
 *          DATAs  – DataSend       (outgoing UDP packets)
 *          DATAr  – DataReceive    (incoming UDP packets)
 *          LED   – LEDManager    (LED state & UI)
 *          SET   – SettingsManager (settings state & UI)
 *          STS   – StatusManager  (system status & sensors)
 *
 *  Instantiation order in onCreate():
 *      1. setContentView
 *      2. _Init_Global()  ← binds Main's own views
 *      3. DATAs = new DataSend(this)
 *      4. STS  = new StatusManager(this, null, null, DATAs)  ← LED/SET injected later
 *      5. SET  = new SettingsManager(this, DATAs, STS)
 *      6. LED  = new LEDManager(this, DATAs, STS, SET)
 *      7. STS.setDependencies(LED, SET)  ← complete circular injection
 *      8. SET.init()  ← must come before LED.init() (LED reads SET)
 *      9. LED.init()
 *     10. STS.init()
 *     11. DATAr = new DataReceive(this, LED, SET, STS)
 *     12. DATAr.init()         ← open socket
 *     13. DATAs.setSocket(DATAr.getSocket())
 *     14. DATAr.setup()        ← start receive loop
 *     15. DATAs.sendWelcome()  ← handshake
 *
 *  Reconnect resync:
 *      onResume() calls DATAr.reconnect() whenever the socket was lost
 *      (WiFi drop, app backgrounded long enough to be torn down, etc.).
 *      Right after, _ResyncDeviceState() re-sends sendGetColor(),
 *      sendSetting(-1, …) and sendSelectLed(…) so the app's local
 *      LED/settings/selection state is realigned with the Arduino
 *      instead of silently staying stale. sendWelcome() is NOT
 *      repeated - the Arduino re-learns the sender's IP/port per
 *      packet, so the one-time onCreate() handshake is enough.
 *
 *  UDP communication protocol summary:
 *      ── Received from Arduino ──────────────────────────────
 *      LBvv            LED brightness (hex byte)
 *      LCiiRRggBB…     LED colour(s)  (index + R G B, hex)
 *      LDrrggbbRRggBB  Dual colour    (Left + Right, hex)
 *      LMvv            Max brightness (hex byte)
 *      LOi…            LED selected   (bitmask 0/1 per LED)
 *      SiiVV…          Settings       (index + value, hex)
 *      sAAbbCCddEEffGG Status packet  (all fields hex)
 *      MllllAAAA       Lux + average  (hex)
 *      ~               Stop loading bar
 *      *LSSQQ<text>    Term log line (level, source, seq - all hex)
 *      Vssssvv<ver>   Identity: boot session, protocol version, firmware
 *      @mm            Active test mode (0 = none)
 *      dmmsseeff      Diffuser mode / strip status / effect
 *      cttttmmmmaaaadddd  Countdowns: TV off, motion off, ambient, dif idle (s)
 *      wrrwwrr        |RSSI| dBm, WiFi state, RTC state
 *      teeeeeeee      Device epoch (0 = never synced)
 *      xffff          Lux adaptation factor, percent
 *      fffff          Fault bitmask (see DataReceive.FAULT_*)
 *      err            EEPROM write completed (00 = OK)
 *
 *      ── Sent to Arduino ────────────────────────────────────
 *      LBvv            Set brightness
 *      LC              Get colour
 *      LCrrggbb        Set colour
 *      LD              Get dual colour
 *      LDrrggbbRRggBB  Set dual colour
 *      Ldrrggbb…       Shake dual colour
 *      LOvv…           Select LEDs
 *      S               Get all settings
 *      SiiVV…          Set setting(s)
 *      Kii             Debug item
 *      A0/A1           Ambient mode OFF/ON
 *      X               Toggle enable/disable
 *      Z               Welcome packet
 *      @ii             Test mode
 *
 *  Short aliases used in comments across all files:
 *      Main  = MainActivity   (this file)
 *      DATAs  = DataSend
 *      DATAr  = DataReceive
 *      LED   = LEDManager
 *      SET   = SettingsManager
 *      STS   = StatusManager
 * ============================================================
 */

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ArgbEvaluator;
import android.animation.Keyframe;
import android.animation.ObjectAnimator;
import android.animation.PropertyValuesHolder;
import android.animation.ValueAnimator;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.provider.Settings;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.LayoutInflater;
import android.annotation.SuppressLint;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.animation.AccelerateDecelerateInterpolator;
import android.view.animation.AlphaAnimation;
import android.view.animation.DecelerateInterpolator;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.TextView;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.constraintlayout.widget.ConstraintLayout;
import android.widget.ImageView;
import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;
import androidx.documentfile.provider.DocumentFile;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.wang.avi.AVLoadingIndicatorView;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

import pl.droidsonroids.gif.GifDrawable;
import pl.droidsonroids.gif.GifImageView;


public class MainActivity extends AppCompatActivity implements SensorEventListener {

    /* ====================================================== */
    /*  Subsystem instances  (the 5 classes)                  */
    /* ====================================================== */

    /**
     * DATAs – DataSend
     * Handles all outgoing UDP packets to the Arduino.
     * Created first because all other classes depend on it for sending.
     */
    public DataSend DATAs;

    /**
     * DATAr – DataReceive
     * Owns the DatagramSocket and runs the background receive loop.
     * Parses every incoming packet and dispatches to LED / SET / STS.
     */
    public DataReceive DATAr;

    /**
     * LED – LEDManager
     * All LED state (colours, selection, dual-colour list) and
     * the LED section of the UI.
     */
    public LEDManager LED;

    /**
     * SET – SettingsManager
     * All 40 setting definitions, SeekBars, background picker,
     * TestMode, Default/EnableDisable buttons.
     */
    public SettingsManager SET;

    /**
     * STS – StatusManager
     * TV/Motion/Ambilight/AmbientMode state, temperature, humidity,
     * lux, and their animated indicators.
     */
    public StatusManager STS;

    /**
     * Launches the SAF folder-tree picker for the background GIF folder.
     * Replaces the old startActivityForResult(intent, 100) + onActivityResult
     * pair (deprecated) - registered as a field initializer since
     * ActivityResultLauncher must be registered before the activity reaches
     * STARTED, and field initializers run at construction time, well before
     * onCreate(). Call _openBackgroundFolderPicker() to use it - see
     * BackgroundPopup's "CHOOSE FOLDER" button and SettingsManager's
     * background-label long-press, the two entry points that used to build
     * this same intent independently.
     */
    private final ActivityResultLauncher<Intent> bgFolderPickerLauncher =
            registerForActivityResult(new ActivityResultContracts.StartActivityForResult(),
                    this::_onBackgroundFolderPicked);

    /**
     * Callback for bgFolderPickerLauncher. Pulled out of the field
     * initializer above (rather than inlined as a lambda body) because it
     * touches bgImageList, which is declared later in the class - Java
     * disallows a field initializer referencing an instance field that
     * hasn't been declared yet ("illegal forward reference"), even though
     * this only actually runs long after construction.
     */
    private void _onBackgroundFolderPicked(androidx.activity.result.ActivityResult result) {
        if (result.getResultCode() != RESULT_OK || result.getData() == null) return;
        Uri treeUri = result.getData().getData();
        if (treeUri == null) return;

        // Keep permission across reboots
        getContentResolver().takePersistableUriPermission(treeUri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION);

        // Persist the chosen directory URI
        getSharedPreferences("FuZz_Background", MODE_PRIVATE).edit()
                .putString("BG_DIR", treeUri.toString())
                .apply();

        // Load GIFs from the new directory
        _LoadBackgroundImages();

        _Toast("FOUND " + bgImageList.size() + " GIF'S");
    }

    /** Opens the SAF folder-tree picker for choosing the background GIF folder. */
    public void _openBackgroundFolderPicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        bgFolderPickerLauncher.launch(intent);
    }

    /* ====================================================== */
    /*  Layout views  (owned by Main)                         */
    /* ====================================================== */

    /** The tab layouts – only one is VISIBLE at a time */
    ConstraintLayout LAY_SET, LAY_LEDS, LAY_CONSOLE, LAY_TELNET;

    /**
     * Left-to-right swipe order of the pages:
     *   TELNET (leftmost, only while telnetEnabled) – TERM – LEDS (default) – SET.
     * Rebuilt by _RebuildNavPages() whenever the TELNET toggle flips.
     */
    private ConstraintLayout[] NAV_PAGES;

    /** Index into NAV_PAGES of the page currently on screen (default = LEDS) */
    private int navPageIndex = 1;

    /** Dual telnet console (App/R4 top + Diffuser bottom) - owns TelnetLayout. */
    public TelnetConsole TELNET;

    /** Floating-window launcher button for the telnet console (shown when ON). */
    private View telnetFab;

    /** TELNET debug toggle state - persisted in FuZz_Telnet prefs. */
    private boolean telnetEnabled = false;
    /** MQTT wire-level trace toggle (Debug dialog grid) -- shows every broker publish/receive in the console. */
    private boolean mqttLogEnabled = false;
    /** SmartTV debug toggle - enables detailed logging for SmartTV diffuser relay operations. 
     *  Set to true to enable SmartTV-specific debug logs in UDPReceive.java for troubleshooting
     *  communication between Android app, SmartTV, and Diffuser. */
    public static boolean smartTvDebugEnabled = false;

    /** Clear the debug console */
    Button BTN_ClearConsole;

    /** Opens the Debug dialog (Term tab) */
    Button BTN_Debug;

    /** TERM / LEDS / SET footer nav buttons, index-matched to NAV_PAGES - see _updatePageButtonStates(). */
    private Button[] PAGE_NAV_BTNS;

    /** Opens the Dual Color popup - footer button, left of the page-nav group. */
    private Button BTN_DualColorOpen;

    /** Background of BTN_DualColorOpen - colours driven live by dualColorCycleAnim, see _startDualColorButtonCycle(). */
    private DualColorPillDrawable dualColorBtnBg;

    /** Drives BTN_DualColorOpen's endless slow colour drift - see _startDualColorButtonCycle(). */
    private ValueAnimator dualColorCycleAnim;

    /** Term console list - chronological (oldest -> newest), bottom-append. */
    public RecyclerView RV_Console;

    /** Backing adapter for RV_Console - owns line storage + colour parsing. */
    private ConsoleAdapter consoleAdapter;

    /** True once the user drags the console away from the bottom; see the
     *  RV_Console scroll listener in onCreate() and _TermAppend() below. */
    private boolean termUserScrolledAway = false;
    private UpdateInstaller updateInstaller;

    /**
     * Seq of the last framed log packet, for loss detection. UDP gives no
     * ordering guarantee, so a jump of more than 1 means datagrams died in
     * flight and a gap marker is printed in their place. -1 until the first
     * packet arrives (and after a reconnect, where a jump is expected).
     */
    private int lastLogSeq = -1;

    /** Connection status / Arduino IP:port label (neon flicker / scanline animated - see _NeonFlickerON()/_ScanlineON()) */
    public TextView TEXT_ConnectInfo;

    /** Scanline container (shown only while NOT connected) + its sliding fill bar - see _ScanlineON()/_ScanlineOFF() */
    public FrameLayout LAY_ConnectScanline;
    public View VIEW_ConnectScanlineFill;

    /** Top-left link indicator: Arduino board icon + WiFi signal-strength icon. */
    public ImageView IMG_ArdIcon, IMG_WifiIcon;

    /** Voltage readout, top-left of TopItems - set by DATAr._recvVoltage() */
    public TextView TEXT_VoltInfo;

    /** Animated loading spinner (AVLoadingIndicatorView library) */
    AVLoadingIndicatorView AVLoading;

    /**
     * Handler that auto-hides the loading spinner after 10 s
     * if no response has been received.
     */
    Handler AVLoading_Handler = new Handler(Looper.getMainLooper());

    /** Animated toast overlay layout */
    ConstraintLayout LAY_TOAST;

    /** Toast message text label */
    TextView TEXT_ToastView;

    /** True while a toast is currently showing (prevents overlap) */
    boolean ToastVisible = false;

    /** Handler that auto-hides the toast after 2.5 s */
    Handler ToastHandler = new Handler(Looper.getMainLooper());

    /** Animator driving the neon-flicker alpha/glow effect on TEXT_ConnectInfo while connected */
    ObjectAnimator ConnectNeonAnimator;

    /** Animator sliding VIEW_ConnectScanlineFill back and forth while NOT connected */
    ValueAnimator ConnectScanlineAnimator;

    /* ====================================================== */
    /*  Background image state  (shared with SET)             */
    /* ====================================================== */

    /** All .gif files found in the selected background directory */
    public List<DocumentFile> bgImageList;

    /**
     * File name of the currently displayed background GIF.
     * Compared by SET to avoid reloading the same image.
     */
    public String bgImageSet = "";

    /** True once bgImageList has been successfully populated */
    public Boolean bgImagesLoaded = false;

    /* ====================================================== */
    /*  Shake sensor  (owned by Main)                         */
    /* ====================================================== */

    /** Android sensor service */
    private SensorManager SHAKE_SensManager;

    /** Accelerometer sensor */
    private Sensor SHAKE_Accelerometer;

    /**
     * Minimum acceleration magnitude (m/s²) above Earth gravity
     * needed to trigger a shake event.
     */
    private static final float SHAKE_Threshold = 30.0f;

    /** Timestamp of the last accepted shake (prevents rapid re-trigger) */
    private long SHAKE_LastTime = 0;

    /* ====================================================== */
    /*  Activity lifecycle                                    */
    /* ====================================================== */

    /** Cloud transport (HiveMQ MQTT over TLS). Only connected while it's actually needed - see _connectTransport(). */
    public MqttTransport MQTT;

    /* ------------------------------------------------------ */
    /*  MQTT credential provisioning (see _promptMqttCredentials()) */
    /* ------------------------------------------------------ */
    /** True while the credentials dialog is on screen - guards against a second popup stacking from a rapid supervisor retick. */
    private boolean mqttCredDialogShowing = false;
    /**
     * Username/password entered (or cached, for a force-resend) but not yet
     * confirmed-verified by the controller. Set by _submitMqttCredentials(),
     * cleared once its ACK callback resolves OK. Held here so a pair typed in
     * while off-WiFi survives until local UDP comes back (see
     * _pushPendingMqttCredentialsIfDue()).
     */
    private String pendingMqttUser = null;
    private String pendingMqttPass = null;
    /** Timestamp of the last push attempt (successful or not) - throttles retries. */
    private long   pendingMqttPushAt = 0;
    /** Minimum gap between automatic pending-credential push attempts. */
    private static final long MQTTCRED_PUSH_RETRY_MS = 20000;
    /**
     * How long to wait, once the phone's own MQTT connect succeeds off-WiFi,
     * for the controller to answer over the cloud link before reporting "no
     * reply yet" - see _submitMqttCredentials()'s off-WiFi branch. Needs to
     * cover a full cmdConnected() resync burst, not just one packet.
     */
    private static final long MQTTCRED_CLOUD_PROBE_MS = 6000;

    /**
     * How long local UDP gets to answer the welcome before falling back to
     * MQTT. Only one transport is ever active at a time - see _connectTransport().
     */
    private static final long UDP_FALLBACK_MS = 4000;   // first fall-to-cloud delay after a fresh welcome
    private static final long SUPERVISE_MS    = 5000;   // supervisor re-evaluation cadence

    /** Marshals the always-on transport supervisor (automatic local <-> cloud switching). */
    private final Handler transportHandler = new Handler(Looper.getMainLooper());

    /** Last transport we resynced on: 0 = none, 1 = local UDP, 2 = cloud MQTT. */
    private int lastActiveTransport = 0;

    /**
     * Persistent supervisor: re-selects the transport from live reachability and
     * reschedules itself. There is no manual local/cloud choice — the link is
     * chosen and switched automatically (see _superviseTransport()).
     */
    private final Runnable transportSupervisor = new Runnable() {
        @Override public void run() {
            _superviseTransport();
            transportHandler.postDelayed(this, SUPERVISE_MS);
        }
    };

    /** True once the boot-time "connecting..." spinner has been dismissed by a real confirmation. */
    private boolean bootLoadingCleared = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState); // Call Activity superclass onCreate
        setContentView(R.layout.activity_main); // Inflate main layout view

        // ── 1. Bind Main's own views ─────────────────────────
        _Init_Global(); // Bind UI elements and internal variables

        // ── 2. Construct subsystems (order matters) ──────────
        // DATAs must exist before STS / SET / LED so they can call send methods
        DATAs = new DataSend(this); // Instantiate UDP sending manager

        // STS needs DATAs for sendAmbientMode; LED and SET injected later
        STS  = new StatusManager(this, null, null, DATAs); // Instantiate status manager

        // SET needs DATAs and STS
        SET  = new SettingsManager(this, DATAs, STS); // Instantiate settings manager

        // LED needs DATAs, STS, SET
        LED  = new LEDManager(this, DATAs, STS, SET); // Instantiate LED manager

        // Now inject LED and SET back into STS to complete the graph
        STS.setDependencies(LED, SET); // Resolve circular dependency references

        // ── 3. Initialise subsystem views & listeners ────────
        SET.init();   // must be before LED (LED reads SET for HB dual value) // Init settings views
        LED.init(); // Init LED control views
        STS.init(); // Init status display views

        // ── 4. Show loading bar while connecting ─────────────
        _LoadingBar(true); // Display loading indicator

        // ── 5. Show LED layout by default ────────────────────
        _SetLayout(LAY_LEDS); // Switch active view layout to LED panel

        // ── 6. Load background GIF ───────────────────────────
        _InitBackgroundImages(); // Render background graphics/animations

        // ── 7. Open UDP socket and start receive loop ────────
        DATAr = new DataReceive(this, LED, SET, STS); // Instantiate UDP receiver listener
        DATAr.init();                          // open DatagramSocket // Bind network socket
        DATAs.setSocket(DATAr.getSocket());     // share socket with DATAs // Provide active socket to sender
        DATAr.setSender(DATAs);                 // route "#SSR" command ACKs back to DATAs // Register back-reference
        DATAr.setup();                         // clear console + start loop // Reset receive state machine

        // ── 8. Initialize MQTT Instance ──────────────────────
        MQTT = new MqttTransport(this, DATAr); // Instantiate MQTT fallback transport
        // NOTE: _connectTransport() removed from here because onResume() executes right after onCreate()!

        // ── 8b. Restore telnet console if it was left ON ──────
        if (telnetEnabled) { // Check if telnet session flag is active
            TELNET.setEnabled(true); // Enable telnet subsystem (connects to static diffuser IP)
        } // End telnet setup check

        // ── 9. Welcome toast ──────────────────────────────────
        _Toast("~ ~ FuZz Show ~ ~"); // Display initial startup toast notification

        // ── 10. Register accelerometer ────────────────────────
        SHAKE_SensManager = (SensorManager) getSystemService(Context.SENSOR_SERVICE); // Fetch system hardware sensor manager
        if (SHAKE_SensManager != null) { // Verify hardware availability
            SHAKE_Accelerometer = SHAKE_SensManager
                    .getDefaultSensor(Sensor.TYPE_ACCELEROMETER); // Acquire default accelerometer sensor
        } // End sensor retrieval check

        // ── 11. Check for a newer app version ─────────────────
        _CheckForUpdate(); // Silent unless a newer release is found
    }

    /* ====================================================== */
    /*  In-app update check (GitHub Releases)                 */
    /* ====================================================== */

    /**
     * Fire-and-forget check against the public releases repo. Only surfaces
     * a dialog when a strictly newer versionCode is found; stays silent on
     * "up to date" or network/parse errors (logged only) so it never nags
     * on a flaky connection.
     */
    private void _CheckForUpdate() {
        UpdateChecker.check(this, new UpdateChecker.Callback() {
            @Override
            public void onUpdateAvailable(String tagName, int versionCode, String apkUrl, String releaseNotes) {
                // Matches app/build.gradle's own versionName formula
                // (versionMajor + "." + versionCode, zero-padded to 3 digits,
                // versionMajor from BuildConfig - see build.gradle's
                // buildConfigField) so what's shown here is the same number
                // the user would see in the system app-info screen after
                // installing it - not the raw GitHub tag, which is just an
                // internal build counter.
                String displayVersion = BuildConfig.VERSION_MAJOR + "." + String.format(java.util.Locale.US, "%03d", versionCode);
                _ShowUpdateDialog(displayVersion, apkUrl, tagName, releaseNotes);
            }

            @Override
            public void onUpToDate() {
                // nothing to do
            }

            @Override
            public void onError(String message) {
                Log.w("UpdateChecker", "update check failed: " + message);
            }
        });
    }

    private void _ShowUpdateDialog(String displayVersion, String apkUrl, String tagName, String releaseNotes) {
        new UpdatePopup(this).showAvailable(getWindow().getDecorView(), displayVersion, releaseNotes,
                () -> _StartUpdateDownload(apkUrl, displayVersion, tagName));
    }

    /**
     * "What's new" - shows the bundled CHANGELOG.md (assets/CHANGELOG.md,
     * copied in from the repo root at build time; see AGENTS.md for the
     * rule on keeping it updated). Tapped from the "V: 1.XXX" label.
     */
    private void _ShowChangelog() {
        String text;
        try (java.io.InputStream in = getAssets().open("CHANGELOG.md")) {
            java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
            byte[] buf = new byte[4096];
            int n;
            while ((n = in.read(buf)) != -1) out.write(buf, 0, n);
            text = out.toString("UTF-8");
        } catch (java.io.IOException e) {
            text = null;
        }
        new UpdatePopup(this).showChangelog(getWindow().getDecorView(), text);
    }

    /**
     * Kicks off the APK download behind a live progress card (percent, size,
     * speed, Cancel). On API 26+, installing from a downloaded file requires
     * the user to have granted "install unknown apps" for this app first -
     * if not granted, sends them straight to that settings screen instead of
     * downloading (they can just tap Update again after).
     *
     * @param displayVersion  User-facing "1.XXX" string shown in the popups.
     * @param tagName         Raw GitHub tag (e.g. "v2") - only used for the
     *                        downloaded file's name, not shown to the user.
     */
    private void _StartUpdateDownload(String apkUrl, String displayVersion, String tagName) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                && !getPackageManager().canRequestPackageInstalls()) {
            _Toast("Allow \"install unknown apps\" for FuZz LED, then tap Update again");
            startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + getPackageName())));
            return;
        }

        if (updateInstaller == null) updateInstaller = new UpdateInstaller(this);
        UpdatePopup progressPopup = new UpdatePopup(this);
        UpdatePopup.Handle handle = progressPopup.showProgress(getWindow().getDecorView(), displayVersion,
                () -> {
                    updateInstaller.cancel();
                    _Toast("Update cancelled");
                });

        updateInstaller.download(apkUrl, tagName, new UpdateInstaller.ProgressListener() {
            @Override
            public void onProgress(int percent, long downloaded, long total, double speedBps) {
                if (percent < 0) {
                    handle.setStatus(_humanBytes(downloaded) + " downloaded...");
                } else {
                    handle.setProgress(percent, downloaded, total, speedBps);
                }
            }

            @Override
            public void onComplete() {
                progressPopup.dismiss();
            }

            @Override
            public void onFailed(String reason) {
                progressPopup.dismiss();
                _Toast("Update download failed: " + reason);
            }
        });
    }

    private String _humanBytes(long bytes) {
        if (bytes < 1024) return bytes + " B";
        if (bytes < 1024 * 1024) return String.format(java.util.Locale.US, "%.0f KB", bytes / 1024.0);
        return String.format(java.util.Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0));
    }

    /**
     * Single entry point for (re)establishing the link. Binds the local socket
     * (when on WiFi), probes the board over UDP directly, and starts the
     * always-on supervisor which handles the actual local/cloud selection and
     * any later failover/recovery. Called from onResume().
     */
    private void _connectTransport(String callerContext) {
        transportHandler.removeCallbacks(transportSupervisor);   // restart cleanly
        lastActiveTransport = 0;                                 // force a resync on the first live transport

        if (_IsWifiConn()) {
            if (!DATAr.isSocketOpen()) {                          // (re)bind the receive socket
                DATAr.reconnect();
                DATAs.setSocket(DATAr.getSocket());
            }
            DATAr.startReceiving();
            DATAs.sendWelcomeUdpDirect();                         // probe local first (never routed to cloud)
            DATAr.markUdpProbe();                                 // arm the first-contact grace
            _pushPendingMqttCredentialsIfDue();                   // WiFi's back - flush any credentials typed in while off it
        } else {
            _connectMqttOrPrompt();                              // off WiFi -> straight to cloud (or prompt if unprovisioned)
        }

        _refreshTransportStatus();
        // First evaluation after the grace window, then every SUPERVISE_MS.
        transportHandler.postDelayed(transportSupervisor, UDP_FALLBACK_MS);
    }

    /**
     * Always-on transport brain. Re-selects local UDP vs cloud MQTT purely from
     * live reachability — no manual mode:
     *   • off WiFi          -> cloud.
     *   • WiFi + UDP live    -> local (the board's UDP replies keep it live).
     *   • WiFi + UDP silent  -> ensure cloud is up AND keep probing the LAN with
     *                           a direct UDP welcome, so the link auto-promotes
     *                           back to local the moment the board is reachable.
     * On any local<->cloud transition it re-pushes app-only state (LED selection,
     * which the board can't re-derive) and resets the console seq for the fresh
     * run. MQTT.connect() and the probe are both idempotent/harmless when unused.
     */
    private void _superviseTransport() {
        final int active;

        if (!_IsWifiConn()) {
            _connectMqttOrPrompt();
            active = _MqttConnected() ? 2 : 0;
        } else if (DATAr.isUdpAvailable()) {
            active = 1;                                          // local proven live
            _pushPendingMqttCredentialsIfDue();                   // WiFi+UDP live - flush any pending credentials
        } else {
            _connectMqttOrPrompt();                             // fall over to cloud (or prompt if unprovisioned)
            // Probe the LAN, but do NOT arm the grace here: while on cloud,
            // availability must flip back to local ONLY on a real UDP reply
            // (lastUdpRxTime), never merely because we just sent a probe -
            // otherwise isUdpAvailable() would read true most of the time and
            // route commands into a dead socket. One probe reaches the board
            // if it's reachable; its reply lands on the socket and promotes us.
            if (DATAr.isSocketOpen()) DATAs.sendWelcomeUdpDirect();
            active = _MqttConnected() ? 2 : 0;
        }

        if (active != 0 && active != lastActiveTransport) {
            lastActiveTransport = active;
            _ConsoleResetSeq();                                 // new transport -> fresh seq run
            _ResyncDeviceState();                               // re-push app-only state over the now-active link
        }

        _refreshTransportStatus();
    }

    /* ====================================================== */
    /*  MQTT credential provisioning (Cloud Mode)              */
    /*  No username/password is ever hardcoded (see             */
    /*  MqttTransport class doc). This section owns: popping    */
    /*  the dialog when cloud mode is needed and nothing is      */
    /*  saved, pushing a candidate pair to the controller for    */
    /*  live verification (DataSend.sendMqttCredentials()),      */
    /*  and the 5-second-hold force-resend gesture.               */
    /* ====================================================== */

    /**
     * Connect MQTT if credentials are already saved; otherwise pop the
     * dialog (unless one is already showing, a pair is already pending a
     * push/ACK, or the user already dismissed this prompt once -
     * MQTT.isPromptDeclined() - since that means "leave me alone", not "ask
     * again in 5 seconds". The 5-second-hold gesture is the only way back in
     * after a decline; it clears the flag itself.
     *
     * Called by: _connectTransport()/_superviseTransport(), in place of a
     * bare MQTT.connect(), everywhere cloud mode is attempted.
     */
    private void _connectMqttOrPrompt() {
        if (MQTT == null) return;
        if (MQTT.hasCredentials()) { MQTT.connect(); return; }
        if (pendingMqttUser != null) return;              // already awaiting push/ACK - _submitMqttCredentials() owns retrying that
        if (MQTT.isPromptDeclined()) return;               // user already said "not now" - don't nag
        if (!mqttCredDialogShowing) _promptMqttCredentials(null, null, null);
    }

    /**
     * If a credential pair is waiting (typed in - or queued for a force
     * resend - while local UDP wasn't reachable), push it now that it is.
     * Throttled by MQTTCRED_PUSH_RETRY_MS so a flaky link never hammers the
     * controller with repeat verify attempts (each one is a real blocking
     * MQTT connect() on the firmware side, not a cheap no-op).
     *
     * Called by: _connectTransport()/_superviseTransport(), everywhere WiFi
     * + local UDP is confirmed live.
     */
    private void _pushPendingMqttCredentialsIfDue() {
        if (pendingMqttUser == null) return;
        if (System.currentTimeMillis() - pendingMqttPushAt < MQTTCRED_PUSH_RETRY_MS) return;
        _submitMqttCredentials(pendingMqttUser, pendingMqttPass, false);  // silent - background retry, never nag with a popup
    }

    /**
     * Pop the MQTT username/password dialog. Shown when cloud mode is needed
     * and nothing is saved yet, or again if the controller rejects a pushed
     * pair (APP_ACK_UNAUTHORIZED) - in which case the rejected values are
     * pre-filled with an error hint rather than making the user retype them.
     *
     * @param prefillUser  Username to pre-fill, or null for blank.
     * @param prefillPass  Password to pre-fill, or null for blank.
     * @param errorHint    Extra red line above the fields, or null for none.
     *
     * Called by: _connectMqttOrPrompt() (first-time prompt) and
     * _submitMqttCredentials()'s ACK callback (re-prompt on rejection).
     */
    private void _promptMqttCredentials(final String prefillUser, final String prefillPass, final String errorHint) {
        if (mqttCredDialogShowing) return;
        mqttCredDialogShowing = true;

        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        android.widget.LinearLayout layout = new android.widget.LinearLayout(this);
        layout.setOrientation(android.widget.LinearLayout.VERTICAL);
        layout.setPadding(pad, pad, pad, pad);

        if (errorHint != null) {
            TextView err = new TextView(this);
            err.setText(errorHint);
            err.setTextColor(ThemeManager.getColor(this, R.color.status_off_red));
            err.setPadding(0, 0, 0, pad / 2);
            layout.addView(err);
        }

        final android.widget.EditText userField = new android.widget.EditText(this);
        userField.setHint("MQTT username");
        // VISIBLE_PASSWORD (not plain TYPE_CLASS_TEXT) - broker usernames are
        // case-sensitive technical strings, not natural-language words, and
        // plain text input lets the keyboard silently auto-capitalize the
        // first letter or offer autocorrect substitutions - either one
        // quietly turns a correct username into a wrong one.
        userField.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD);
        if (prefillUser != null) userField.setText(prefillUser);
        layout.addView(userField);

        final android.widget.EditText passField = new android.widget.EditText(this);
        passField.setHint("MQTT password");
        passField.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD);
        if (prefillPass != null) passField.setText(prefillPass);
        layout.addView(passField);

        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("Cloud Mode Login");
        builder.setMessage("Enter the MQTT broker credentials for this device. " +
                "They're verified live against the broker before being saved.");
        builder.setView(layout);
        builder.setPositiveButton("Save", (dialog, which) -> {
            // Trim BOTH - a stray leading/trailing space from the keyboard's
            // autocomplete/autocapitalize is a far more likely cause of "this
            // exact pair fails" than an intentional space in a broker credential.
            String user = userField.getText().toString().trim();
            String pass = passField.getText().toString().trim();
            mqttCredDialogShowing = false;
            if (MQTT != null) MQTT.setPromptDeclined(false);   // actively engaging - cancel any standing "leave me alone"
            if (user.isEmpty() || pass.isEmpty()) {
                _promptMqttCredentials(user, pass, "Username and password are both required");
                return;
            }
            _submitMqttCredentials(user, pass, true);   // interactive - reopen with an error hint if the broker rejects it
        });
        builder.setNegativeButton("Not now", (dialog, which) -> {
            mqttCredDialogShowing = false;
            // Permanently stop the automatic prompt (_connectMqttOrPrompt())
            // from popping this again - an explicit decline means "leave me
            // alone", not "ask me again in 5 seconds". The only way back in
            // is the deliberate 5-second-hold gesture, which clears this.
            if (MQTT != null) MQTT.setPromptDeclined(true);
        });
        builder.setOnCancelListener(dialog -> {
            mqttCredDialogShowing = false;
            if (MQTT != null) MQTT.setPromptDeclined(true);
        });
        builder.show();
    }

    /**
     * Verify + apply a username/password pair - from either the credentials
     * dialog's Save button, an automatic pending push
     * (_pushPendingMqttCredentialsIfDue()), or the 5-second force-resend
     * gesture.
     *
     * Two-stage, in order:
     *   1. The APP verifies the pair itself first, connecting straight to
     *      the broker with it (MqttTransport.tryConnect()) - faster
     *      feedback than waiting on the controller's own round trip, and
     *      the controller is never bothered with an obviously-bad pair.
     *   2. Only once that succeeds:
     *        - on WiFi (local UDP live)  -> hand it to the controller too,
     *          so its own EEPROM copy gets verified + saved independently.
     *        - off WiFi                  -> nothing to hand it to locally,
     *          so Cloud Mode is brought up with it immediately instead of
     *          waiting for WiFi to return, then this checks whether the
     *          controller itself answers over that link within
     *          MQTTCRED_CLOUD_PROBE_MS - confirming these credentials
     *          actually reach THIS controller, not just some broker
     *          session. If it stays silent, pendingMqttUser/Pass are left
     *          set so the pair still gets pushed over local UDP the next
     *          time WiFi comes back (covers first-time setup while away
     *          from home - the controller can only ever LEARN a new pair
     *          over that local link).
     *
     * @param user             Username to verify + (on success) cache.
     * @param pass             Password to verify + (on success) cache.
     * @param reopenOnFailure  True for a call the user just triggered directly
     *                         (dialog Save, force-resend gesture) - failure
     *                         re-prompts them right away since they're already
     *                         looking at the app. False for the silent
     *                         background retry (_pushPendingMqttCredentialsIfDue(),
     *                         every ~20s) - MUST stay false there, or a still-bad
     *                         saved pair pops the dialog back up every cycle
     *                         forever, even after the user dismissed it.
     */
    private void _submitMqttCredentials(final String user, final String pass, final boolean reopenOnFailure) {
        pendingMqttUser = user;
        pendingMqttPass = pass;

        if (MQTT == null) return;

        _Console(false, "☁", "MQTT CRED -> checking with broker...");
        MQTT.tryConnect(user, pass, brokerOk -> {
            if (!brokerOk) {
                _Toast("MQTT login rejected by broker");
                if (reopenOnFailure) {
                    _promptMqttCredentials(user, pass, "Rejected by broker - check username/password");
                } else {
                    _Console(false, "☁", "{#R}MQTT CRED REJECTED{##} (will retry later)");
                }
                return;
            }

            // Broker accepted these from the phone's side.
            if (_UDP_Available()) {
                // On WiFi - hand off to the controller so its own EEPROM
                // copy gets verified + saved too, independent of the phone's.
                pendingMqttPushAt = System.currentTimeMillis();
                _Console(false, "►►", "MQTT CRED -> verifying with controller...");
                DATAs.sendMqttCredentials(user, pass, result -> {
                    if (result == 0) {
                        pendingMqttUser = null;
                        pendingMqttPass = null;
                        MQTT.setCredentials(user, pass);
                        _Toast("MQTT credentials verified");
                        _Console(false, "☁", "{#G}MQTT CRED OK{##}");
                    } else if (result == 7) {
                        // The broker just accepted this pair for the phone, so
                        // this isn't a bad-password case - most likely the
                        // controller itself can't reach the broker right now.
                        _Toast("Controller couldn't verify with the broker");
                        if (reopenOnFailure) {
                            _promptMqttCredentials(user, pass,
                                    "Broker accepted this pair, but the controller couldn't - check its network");
                        } else {
                            _Console(false, "☁", "{#R}CONTROLLER REJECTED MQTT CRED{##} (will retry later)");
                        }
                    } else if (reopenOnFailure) {
                        _promptMqttCredentials(user, pass, "Send failed - try again");
                    }
                });
            } else {
                // Off WiFi - nothing to push to locally, so use Cloud Mode
                // with this pair right now (tryConnect() above already made
                // it the live session) instead of waiting for WiFi to return.
                MQTT.setCredentials(user, pass);
                _Console(false, "☁", "{#G}CLOUD ON{##} - waiting for controller...");
                transportHandler.postDelayed(() -> {
                    if (DATAr.isMqttArduinoAlive()) {
                        pendingMqttUser = null;
                        pendingMqttPass = null;
                        _Toast("Cloud Mode connected");
                        _Console(false, "☁", "{#G}CONTROLLER ANSWERED{##} over cloud");
                    } else {
                        _Toast("Connected to broker, but the controller hasn't answered yet");
                        _Console(false, "☁", "{#Y}NO REPLY FROM CONTROLLER{##} yet - will keep trying");
                        // pendingMqttUser/Pass stay set: _pushPendingMqttCredentialsIfDue()
                        // pushes this pair over local UDP the next time WiFi is
                        // back, in case the controller never had it cached at all.
                    }
                }, MQTTCRED_CLOUD_PROBE_MS);
            }
        });
    }

    @Override
    protected void onResume() {
        super.onResume(); // Trigger base Activity onResume handler

        // Single call handles cold start, resume from background, and transport selection
        _connectTransport("onResume"); // Execute connection sequence cleanly once

        // Register shake sensor listener
        if (SHAKE_Accelerometer != null) { // Check accelerometer hardware reference
            SHAKE_SensManager.registerListener(
                    this, SHAKE_Accelerometer, SensorManager.SENSOR_DELAY_UI); // Attach motion listener
        } // End accelerometer registration
    }

    /**
     * Re-establishes app/device agreement after a reconnect (WiFi drop,
     * app backgrounded long enough to lose the socket, etc.).
     *
     * Mirrors the per-page refreshes _NavigateToPage() normally sends on
     * first visit to LEDS / SET - bundled here since after a reconnect
     * every locally-held value (colours, settings, selection) may be
     * stale relative to the Arduino, regardless of which page is on
     * screen.
     *
     * A welcome ('Z') IS re-sent on every onResume() (see above): the board
     * now suspends and stops all traffic when an app goes silent, so it must be
     * re-announced to wake it and trigger its own full push. This method still
     * re-pushes the app-only state the board can't re-derive on its own.
     *
     * LED_Selected[] is local-only (the Arduino has no concept of
     * "selected"), so it must be re-pushed rather than re-requested.
     */
    private void _ResyncDeviceState() {
        DATAs.sendGetColor();
        DATAs.sendSetting(-1, SET.SET_Info, SET.SET_TOTAL);
        DATAs.sendSelectLed(LED.LED_Selected, LED.LED_TOTAL);
    }

    @Override
    protected void onPause() {
        super.onPause();
        transportHandler.removeCallbacks(transportSupervisor);   // stop auto-switching while backgrounded
        DATAr.stopReceiving();
        SHAKE_SensManager.unregisterListener(this);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        transportHandler.removeCallbacks(transportSupervisor);
        DATAr.destroy();
        if (MQTT != null) MQTT.disconnect();
        if (TELNET != null) TELNET.setEnabled(false);  // close both telnet sockets/threads
        if (updateInstaller != null) updateInstaller.unregister();
        if (dualColorCycleAnim != null) dualColorCycleAnim.cancel();
        if (navStrokeRunAnim != null) navStrokeRunAnim.cancel();
    }

    /* ====================================================== */
    /*  Sensor events  (shake detection)                      */
    /* ====================================================== */

    /**
     * Shake detection.
     * When a large enough acceleration is detected while the LED layout
     * is visible, a random dual colour is sent to the Arduino and the
     * random-button palette is refreshed.
     *
     * @param event  Raw sensor event from the accelerometer.
     */
    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() != Sensor.TYPE_ACCELEROMETER) return;

        float x = event.values[0];
        float y = event.values[1];
        float z = event.values[2];

        // Acceleration magnitude minus Earth's gravity
        float acc = (float) Math.sqrt(x*x + y*y + z*z)
                    - SensorManager.GRAVITY_EARTH;

        if (acc > SHAKE_Threshold && LAY_LEDS.getVisibility() == View.VISIBLE) {
            long now = System.currentTimeMillis();
            if (now - SHAKE_LastTime > 1000) { // debounce: 1 shake per second
                SHAKE_LastTime = now;

                // Decode current random colour pair
                String randHex = LED.getRandDualColorHex();
                int[] rgbL = _HexToRgb(randHex.substring(0, 6));
                int[] rgbR = _HexToRgb(randHex.substring(6, 12));

                // Send shake dual colour
                DATAs.sendShakeDualColor(
                        rgbL[LED._R], rgbL[LED._G], rgbL[LED._B],
                        rgbR[LED._R], rgbR[LED._G], rgbR[LED._B]);

                _Toast("[SHAKE RANDOM DUAL COLOR]");

                // Pre-generate the next random colour
                LED.reloadRandDualColor();
                LED._deselectAll();

                // Bracketed "r,g,b" triplet - see LEDManager's SAVE DUAL COLOR
                // handler for why (ConsoleAdapter._chipColors() auto-swatches
                // this exact shape; three separately-tinted numbers don't).
                _Console(false, "►►",
                        "SHAKE RANDOM DUAL COLOR ["
                        + rgbL[LED._R] + "," + rgbL[LED._G] + "," + rgbL[LED._B]
                        + "] -- ["
                        + rgbR[LED._R] + "," + rgbR[LED._G] + "," + rgbR[LED._B] + "]");
            }
        }
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        // No action needed
    }

    /* ====================================================== */
    /*  INIT – Main's own views                               */
    /* ====================================================== */

    /**
     * Bind every view that belongs to Main (layouts, console controls,
     * toast, loading bar, connection label) and set up their listeners.
     *
     * Must be called in onCreate() BEFORE subsystem initialisation.
     */
    public void _Init_Global() {
        // Layouts
        LAY_SET     = findViewById(R.id.SetLayout);
        LAY_LEDS    = findViewById(R.id.ColorLayout);
        LAY_CONSOLE = findViewById(R.id.ConsoleLayout);
        LAY_TELNET  = findViewById(R.id.TelnetLayout);

        // Telnet console (Diffuser pane only) - restore persisted toggle
        RecyclerView rvTelDif = findViewById(R.id._recyclertelnet_dif);
        rvTelDif.setLayoutManager(new LinearLayoutManager(this));
        TELNET = new TelnetConsole(this, rvTelDif,
                findViewById(R.id._telnet_dif_status));
        telnetEnabled = getSharedPreferences("FuZz_Telnet", MODE_PRIVATE)
                .getBoolean("ENABLED", false);
        mqttLogEnabled = getSharedPreferences("FuZz_MqttLog", MODE_PRIVATE)
                .getBoolean("ENABLED", false);

        // Telnet floating window: launcher button + close + drag-to-move
        telnetFab = findViewById(R.id._telnet_fab);
        findViewById(R.id._telnet_close).setOnClickListener(v -> _ShowTelnetWindow(false));
        _MakeTelnetWindowDraggable();
        _MakeFabMovable();
        telnetFab.setVisibility(telnetEnabled ? View.VISIBLE : View.GONE);

        // Page order: TERM – LEDS (default) – SET, navigated via the bottom-right buttons
        _RebuildNavPages();
        _Init_PageButtons();

        // Loading spinner
        AVLoading = findViewById(R.id.LoadingBar);

        // Connection status label + top-left Arduino / WiFi-signal indicator
        TEXT_ConnectInfo       = findViewById(R.id._connectinfo);
        LAY_ConnectScanline    = findViewById(R.id._connectinfo_scanline);
        VIEW_ConnectScanlineFill = findViewById(R.id._connectinfo_scanline_fill);
        IMG_ArdIcon      = findViewById(R.id._ard_icon);
        IMG_WifiIcon     = findViewById(R.id._wifi_icon);
        updateWifiSignal(0, 0);   // start greyed-out until the first 'w' packet

        // Long-press the status label to force a fresh reconnect attempt
        // (same sequence as onResume()/a cold start), for when the link is
        // stuck (e.g. CLOUD - NO REPLY) and the user wants to retry now
        // rather than wait for the next supervisor tick.
        TEXT_ConnectInfo.setOnLongClickListener(v -> {
            v.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
            _Toast("Retrying connection...");
            _connectTransport("manual retry");
            return true;
        });

        // Hold the same label for 5 full seconds -> MQTT credential action,
        // independent of the ~500ms long-click above (both can fire off one
        // long hold - that's fine, they do unrelated things). A plain
        // OnLongClickListener can't be reused for this: it only fires once at
        // the OS's short default timeout, so the longer hold needs its own
        // down/up timer via setOnTouchListener. Returning false lets the
        // touch event keep flowing to the view's normal handling, so the
        // existing long-click still fires normally.
        //   - credentials already saved -> force re-send/re-verify them
        //     (re-syncs EEPROM if the controller was ever reflashed/reset).
        //   - nothing saved yet -> pop the entry dialog on demand, same as
        //     the automatic prompt but without waiting for cloud mode to
        //     actually be needed (e.g. to set it up in advance, while still
        //     on WiFi with local UDP working fine).
        final Runnable forceMqttResend = () -> {
            TEXT_ConnectInfo.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS);
            if (MQTT != null && MQTT.hasCredentials()) {
                _Toast("Resending MQTT credentials...");
                _submitMqttCredentials(MQTT.getUser(), MQTT.getPass(), true);   // interactive - if the saved pair turns out stale, tell them now
            } else {
                if (MQTT != null) MQTT.setPromptDeclined(false);   // explicit re-engagement - this gesture is the deliberate way back in after a decline
                _promptMqttCredentials(null, null, null);
            }
        };
        TEXT_ConnectInfo.setOnTouchListener((v, event) -> {
            switch (event.getAction()) {
                case android.view.MotionEvent.ACTION_DOWN:
                    transportHandler.postDelayed(forceMqttResend, 5000);
                    break;
                case android.view.MotionEvent.ACTION_UP:
                case android.view.MotionEvent.ACTION_CANCEL:
                    transportHandler.removeCallbacks(forceMqttResend);
                    break;
            }
            return false;
        });

        // Installed-version label, top-left corner above the Diffuser Usage
        // badge - tap it to see "What's new" (bundled CHANGELOG.md, see AGENTS.md)
        TextView versionLabel = findViewById(R.id._version_label);
        if (versionLabel != null) {
            versionLabel.setText("V: " + BuildConfig.VERSION_NAME);
            versionLabel.setOnClickListener(v -> _ShowChangelog());
        }

        // Toast
        LAY_TOAST     = findViewById(R.id.ToastLayout);
        TEXT_ToastView = findViewById(R.id._toast_view);

        // Console / debug controls
        BTN_ClearConsole = findViewById(R.id._btn_clear_console);
        BTN_Debug       = findViewById(R.id._btn_debug);

        // Console
        RV_Console       = findViewById(R.id._recyclerconsole);

        consoleAdapter = new ConsoleAdapter();
        RV_Console.setLayoutManager(new LinearLayoutManager(this));
        RV_Console.setAdapter(consoleAdapter);
        RV_Console.setItemAnimator(null); // high-frequency log lines - skip insert/remove animations

        // Track manual scroll intent directly instead of re-deriving "was near
        // bottom" from layout state on every appended line (see _TermAppend()).
        // The per-line recompute is racy during a fast burst (a K debug dump can
        // land 40+ lines within ~1s, one every ~20ms): each new line's own
        // auto-scroll-to-bottom can fire and settle before the user's drag has
        // moved the list far enough to register as "not at the bottom" yet, so
        // the view gets snapped back under the user's finger, over and over,
        // making scroll-up feel completely blocked during any burst. Setting
        // termUserScrolledAway the instant a drag begins - rather than waiting to
        // see where it ends up - fixes that: the very first touch permanently
        // suspends auto-scroll for this burst, regardless of how the timing lines up.
        RV_Console.addOnScrollListener(new RecyclerView.OnScrollListener() {
            @Override
            public void onScrollStateChanged(@NonNull RecyclerView rv, int newState) {
                if (newState == RecyclerView.SCROLL_STATE_DRAGGING) {
                    termUserScrolledAway = true;               // User took control - Logic
                } else if (newState == RecyclerView.SCROLL_STATE_IDLE) {
                    // Only re-check "at bottom" once the scroll/fling has FULLY
                    // settled - checking on every onScrolled() delta during an
                    // active drag (the previous version of this listener) was
                    // the actual bug: the very first pixel of a drag-up gesture
                    // hasn't moved the list far enough yet for
                    // findLastVisibleItemPosition() to change, so it still read
                    // "at the bottom" and immediately re-cleared the flag INSIDE
                    // THE SAME GESTURE that had just set it - making the whole
                    // fix a no-op for the ordinary case of scrolling up from the
                    // bottom. Gating on SCROLL_STATE_IDLE instead means this
                    // only re-evaluates once, at rest, after the gesture ends.
                    LinearLayoutManager lm = (LinearLayoutManager) rv.getLayoutManager();
                    if (lm == null) return;
                    int lastVisible  = lm.findLastVisibleItemPosition();
                    int itemCountNow = consoleAdapter.getItemCount();
                    boolean atBottom = (lastVisible == RecyclerView.NO_POSITION)
                            || (lastVisible >= itemCountNow - 1);
                    termUserScrolledAway = !atBottom;
                }
            }
        });

        BTN_ClearConsole.setOnClickListener(v -> _Console_Clear());

        BTN_Debug.setOnClickListener(v -> {
            // DBG → open Debug dialog in console view
            _ShakeButton(BTN_Debug, 500);
            _SetLayout(LAY_CONSOLE);
          //  _Console_Clear();
            _Toast("{ DEBUG }");
            _Debug();
        });
    }

    /* ====================================================== */
    /*  Layout switching                                       */
    /* ====================================================== */

    /**
     * Show one of the three tab layouts. The outgoing page slides off one
     * edge while the incoming page slides in from the other, so both are
     * visible at once and the incoming page's layout previews into view
     * as it slides — rather than an abrupt instant swap.
     * Switches instantly with no animation if the target is already the
     * current page (e.g. the very first call on startup).
     *
     * @param layout  LAY_SET, LAY_LEDS, LAY_CONSOLE, or LAY_TELNET
     */
    public void _SetLayout(ConstraintLayout layout) {
        int toIndex = _NavIndexOf(layout);
        if (toIndex < 0) return; // page not in current NAV_PAGES (telnet toggled off)
        int fromIndex = navPageIndex;
        navPageIndex  = toIndex;
        _updatePageButtonStates(toIndex);

        ConstraintLayout from = NAV_PAGES[fromIndex];
        ConstraintLayout to   = NAV_PAGES[toIndex];

        // Stop anything mid-flight first so rapid swipes/taps never leave
        // a page half-animated or in an inconsistent visibility state.
        for (ConstraintLayout page : NAV_PAGES) page.animate().cancel();

        if (from == to) {
            // No real transition (initial layout on startup, or
            // re-selecting the tab we're already on) — just show it.
            for (ConstraintLayout page : NAV_PAGES) page.setVisibility(View.GONE);
            to.setTranslationX(0);
            to.setVisibility(View.VISIBLE);
            return;
        }

        _SlidePageTransition(from, to, toIndex > fromIndex);
    }

    /**
     * Slide the outgoing page off-screen while the incoming page slides
     * in from the opposite edge, in sync, so the new page's content is
     * visible (“previewed”) throughout the transition.
     *
     * @param from           the page currently on screen
     * @param to              the page to bring on screen
     * @param movingForward  true for the TERM ▸ LEDS ▸ SET direction
     *                        (slides left); false for the reverse
     */
    private void _SlidePageTransition(ConstraintLayout from, ConstraintLayout to, boolean movingForward) {
        int width = from.getWidth();
        if (width <= 0) width = getResources().getDisplayMetrics().widthPixels;

        float outTarget = movingForward ? -width : width;
        float inStart    = movingForward ?  width : -width;

        to.setTranslationX(inStart);
        to.setAlpha(1f);
        to.setVisibility(View.VISIBLE);

        final int duration = 260;
        final DecelerateInterpolator interp = new DecelerateInterpolator(1.5f);

        from.animate()
                .translationX(outTarget)
                .setDuration(duration)
                .setInterpolator(interp)
                .withEndAction(() -> {
                    from.setVisibility(View.GONE);
                    from.setTranslationX(0);
                })
                .start();

        to.animate()
                .translationX(0)
                .setDuration(duration)
                .setInterpolator(interp)
                .start();
    }

    /* ====================================================== */
    /*  Telnet page toggle                                     */
    /* ====================================================== */

    /** @return index of layout in NAV_PAGES, or -1 if not currently a page. */
    private int _NavIndexOf(ConstraintLayout layout) {
        for (int i = 0; i < NAV_PAGES.length; i++) {
            if (NAV_PAGES[i] == layout) return i;
        }
        return -1;
    }

    /**
     * (Re)build NAV_PAGES for the current telnetEnabled state and re-anchor
     * navPageIndex on the page that is on screen. If the telnet page was
     * showing and just got disabled, falls back to the TERM console.
     */
    private void _RebuildNavPages() {
        ConstraintLayout current = (NAV_PAGES != null) ? NAV_PAGES[navPageIndex] : LAY_LEDS;

        // Telnet is now a floating window, not a swipe page.
        NAV_PAGES = new ConstraintLayout[]{ LAY_CONSOLE, LAY_LEDS, LAY_SET };

        if (current == LAY_TELNET) current = LAY_CONSOLE;
        navPageIndex = Math.max(0, _NavIndexOf(current));
    }

    /**
     * TELNET debug toggle (Debug dialog grid, last cell).
     * ON  – persists the flag, reveals the floating launcher button, arms
     *       TELNET (connects to static diffuser IP).
     * OFF – persists the flag, hides the floating window + launcher and
     *       drops the telnet link.
     *
     * @param on  New state.
     */
    private void _TelnetSetEnabled(boolean on) {
        if (telnetEnabled == on) return;

        telnetEnabled = on;
        getSharedPreferences("FuZz_Telnet", MODE_PRIVATE)
                .edit().putBoolean("ENABLED", on).apply();

        TELNET.setEnabled(on);

        if (on) {
            telnetFab.setVisibility(View.VISIBLE);          // reveal the floating launcher
            _Toast("TELNET { ON }");
            _Console(false, "►►", "TELNET -> {{#G}ON{##}} tap the {{#C}>_{##}} button");
        } else {
            _ShowTelnetWindow(false);                       // hide the floating window
            telnetFab.setVisibility(View.GONE);
            _Toast("TELNET { OFF }");
            _Console(false, "►►", "TELNET -> {{#R}OFF{##}}");
        }
    }

    /**
     * MQTT wire-log toggle (Debug dialog grid). ON prints every broker
     * publish ("MQTT S") and receive ("MQTT R") to the console via
     * MqttTransport; OFF returns it to summary-only ("CLOUD ON/LOST/...").
     *
     * @param on  New state.
     */
    private void _MqttLogSetEnabled(boolean on) {
        if (mqttLogEnabled == on) return;

        mqttLogEnabled = on;
        getSharedPreferences("FuZz_MqttLog", MODE_PRIVATE)
                .edit().putBoolean("ENABLED", on).apply();

        _Toast("MQTT LOG { " + (on ? "ON" : "OFF") + " }");
        _Console(false, "►►", "MQTT LOG -> {{#" + (on ? "G}ON" : "R}OFF") + "{##}}");
    }

    /** @return true while MQTT R/S wire tracing is enabled (Debug dialog toggle). */
    public boolean _MqttLogEnabled() {
        return mqttLogEnabled;
    }

    /** Toggle the floating telnet window (only meaningful while telnet is ON). */
    private void _ToggleTelnetWindow() {
        _ShowTelnetWindow(LAY_TELNET.getVisibility() != View.VISIBLE);
    }

    /**
     * Show or hide the floating telnet window.
     * @param show  true to reveal and bring to front, false to hide.
     */
    private void _ShowTelnetWindow(boolean show) {
        if (show) {
            LAY_TELNET.setVisibility(View.VISIBLE);
            LAY_TELNET.bringToFront();
        } else {
            LAY_TELNET.setVisibility(View.GONE);
        }
    }

    /** Drag the floating telnet window by its title bar, clamped to the parent. */
    @SuppressLint("ClickableViewAccessibility")
    private void _MakeTelnetWindowDraggable() {
        View handle = findViewById(R.id._telnet_drag);
        final float[] d = new float[4];   // downRawX, downRawY, startTX, startTY
        handle.setOnTouchListener((v, e) -> {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    d[0] = e.getRawX(); d[1] = e.getRawY();
                    d[2] = LAY_TELNET.getTranslationX();
                    d[3] = LAY_TELNET.getTranslationY();
                    LAY_TELNET.bringToFront();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    View root = (View) LAY_TELNET.getParent();
                    float nx = d[2] + (e.getRawX() - d[0]);
                    float ny = d[3] + (e.getRawY() - d[1]);
                    // keep the window inside the parent bounds
                    float maxX = root.getWidth()  - LAY_TELNET.getWidth()  - LAY_TELNET.getLeft();
                    float maxY = root.getHeight() - LAY_TELNET.getHeight() - LAY_TELNET.getTop();
                    nx = Math.max(-LAY_TELNET.getLeft(), Math.min(nx, maxX));
                    ny = Math.max(-LAY_TELNET.getTop(),  Math.min(ny, maxY));
                    LAY_TELNET.setTranslationX(nx);
                    LAY_TELNET.setTranslationY(ny);
                    return true;
            }
            return false;
        });
    }

    /** Make the launcher button movable; a tap (no drag) toggles the window. */
    @SuppressLint("ClickableViewAccessibility")
    private void _MakeFabMovable() {
        final int slop = ViewConfiguration.get(this).getScaledTouchSlop();
        final float[] d = new float[4];   // downRawX, downRawY, startTX, startTY
        final boolean[] moved = { false };
        telnetFab.setOnTouchListener((v, e) -> {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    d[0] = e.getRawX(); d[1] = e.getRawY();
                    d[2] = telnetFab.getTranslationX();
                    d[3] = telnetFab.getTranslationY();
                    moved[0] = false;
                    return true;
                case MotionEvent.ACTION_MOVE: {
                    float dx = e.getRawX() - d[0], dy = e.getRawY() - d[1];
                    if (Math.abs(dx) > slop || Math.abs(dy) > slop) moved[0] = true;
                    View root = (View) telnetFab.getParent();
                    float nx = d[2] + dx, ny = d[3] + dy;
                    float maxX = root.getWidth()  - telnetFab.getWidth()  - telnetFab.getLeft();
                    float maxY = root.getHeight() - telnetFab.getHeight() - telnetFab.getTop();
                    nx = Math.max(-telnetFab.getLeft(), Math.min(nx, maxX));
                    ny = Math.max(-telnetFab.getTop(),  Math.min(ny, maxY));
                    telnetFab.setTranslationX(nx);
                    telnetFab.setTranslationY(ny);
                    return true;
                }
                case MotionEvent.ACTION_UP:
                    if (!moved[0]) { telnetFab.performClick(); _ToggleTelnetWindow(); }
                    return true;
            }
            return false;
        });
    }

    /* ====================================================== */
    /*  Swipe navigation  ([TELNET] ←→ TERM ←→ LEDS ←→ SET)    */
    /* ====================================================== */

    /**
     * Wire the bottom-right TERM/LEDS/SET buttons, and the DUAL COLOR button
     * on the same footer row. Page navigation used to be an ambient
     * horizontal-swipe gesture detected via a dispatchTouchEvent() override -
     * removed entirely (along with its per-widget exemption list and the
     * settings-ScrollView scroll-steal workaround it needed) because it kept
     * fighting real scroll/drag gestures elsewhere in the app (console log,
     * settings list) instead of only catching deliberate page swipes. Three
     * explicit buttons have no such ambiguity. The dual-colour swatch row
     * that used to sit inline here moved into its own popup for the same
     * reason - see LEDManager.showDualColorPopup().
     *
     * Called by: onCreate(), once NAV_PAGES and LED are both ready.
     */
    private void _Init_PageButtons() {
        Button btnTerm = findViewById(R.id._btn_page_term);
        Button btnLeds = findViewById(R.id._btn_page_leds);
        Button btnSet  = findViewById(R.id._btn_page_set);
        PAGE_NAV_BTNS = new Button[]{ btnTerm, btnLeds, btnSet };

        if (btnTerm != null) btnTerm.setOnClickListener(v -> _NavigateToPage(0)); // TERM - NAV_PAGES[0] (LAY_CONSOLE)
        if (btnLeds != null) btnLeds.setOnClickListener(v -> _NavigateToPage(1)); // LEDS - NAV_PAGES[1]
        if (btnSet  != null) btnSet.setOnClickListener(v -> _NavigateToPage(2));  // SET  - NAV_PAGES[2]

        // Same accent-pill treatment every button/tab in the app already uses for
        // "this one is active" (see SettingsManager._setupCategoryTabs()) - flat
        // outline when idle, tinted+bordered fill + accent text when selected.
        // _SetLayout() ran once already during onCreate (before this method wires
        // the buttons), so sync the visuals to whatever page that landed on now.
        for (Button b : PAGE_NAV_BTNS) {
            if (b != null) b.setTypeface(null, Typeface.BOLD);
        }
        _updatePageButtonStates(navPageIndex);

        BTN_DualColorOpen = findViewById(R.id._btn_dualcolor_open);
        if (BTN_DualColorOpen != null) {
            BTN_DualColorOpen.setTypeface(null, Typeface.BOLD);
            BTN_DualColorOpen.setOnClickListener(v -> LED.showDualColorPopup());
        }
        _startDualColorButtonCycle();
    }

    /** Duration (ms) of one leg of the DUAL COLOR button's colour drift - slow and gentle on purpose. */
    private static final int DUAL_COLOR_CYCLE_MS = 6000;

    /**
     * Starts (once) an endless, slow left/right colour drift on the DUAL
     * COLOR button - a fresh high-contrast random pair every
     * DUAL_COLOR_CYCLE_MS, smoothly cross-faded in rather than cut, so the
     * button's own fill literally reads as "two colours, one left one
     * right" - matching exactly what it opens. Text stays a fixed dark
     * colour (wheel_label_text, the same one already used for labels sat
     * on top of colourful wheel backgrounds elsewhere in the app) since
     * _randomDualColorPair() always keeps brightness high, so it stays
     * legible against every hue without needing to track the fill.
     * Called once from _Init_PageButtons(); runs for the Activity's
     * lifetime and is cancelled in onDestroy().
     */
    private void _startDualColorButtonCycle() {
        if (BTN_DualColorOpen == null || dualColorCycleAnim != null) return;

        float dp = getResources().getDisplayMetrics().density;
        int[] start = _randomDualColorPair();

        dualColorBtnBg = new DualColorPillDrawable(start[0], start[1]);
        dualColorBtnBg.setStroke(1.5f * dp, ThemeManager.getColor(this, R.color.skbar_settings_title));
        BTN_DualColorOpen.setBackground(dualColorBtnBg);
        BTN_DualColorOpen.setTextColor(ThemeManager.getColor(this, R.color.wheel_label_text));

        _dualColorCycleStep(start[0], start[1]);
    }

    /**
     * One leg of the endless drift: smoothly morphs the button's fill from
     * (fromC1, fromC2) to a freshly-rolled random pair over
     * DUAL_COLOR_CYCLE_MS, then immediately rolls and starts the next leg
     * on completion - see _startDualColorButtonCycle().
     */
    private void _dualColorCycleStep(int fromC1, int fromC2) {
        int[] target = _randomDualColorPair();
        ArgbEvaluator eval = new ArgbEvaluator();

        dualColorCycleAnim = ValueAnimator.ofFloat(0f, 1f);
        dualColorCycleAnim.setDuration(DUAL_COLOR_CYCLE_MS);
        dualColorCycleAnim.setInterpolator(new AccelerateDecelerateInterpolator());
        dualColorCycleAnim.addUpdateListener(a -> {
            float f = (float) a.getAnimatedValue();
            int c1 = (int) eval.evaluate(f, fromC1, target[0]);
            int c2 = (int) eval.evaluate(f, fromC2, target[1]);
            if (dualColorBtnBg != null) dualColorBtnBg.setColors(c1, c2);
        });
        // cancel() also fires onAnimationEnd (after onAnimationCancel), so
        // without this flag onDestroy()'s cancel() would immediately chain
        // into a brand-new animator right as the Activity goes away.
        dualColorCycleAnim.addListener(new AnimatorListenerAdapter() {
            private boolean cancelled = false;

            @Override
            public void onAnimationCancel(Animator animation) {
                cancelled = true;
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                if (!cancelled) _dualColorCycleStep(target[0], target[1]);
            }
        });
        dualColorCycleAnim.start();
    }

    /**
     * High-saturation, high-brightness random HSV pair (a random hue plus a
     * 120-180 degree complementary offset) - same recipe as
     * LEDManager.reloadRandDualColor(), duplicated locally rather than
     * shared since this can start before LED is constructed in onCreate.
     * Brightness is kept high on purpose so BTN_DualColorOpen's fixed dark
     * text stays readable against every hue this ever rolls.
     */
    private int[] _randomDualColorPair() {
        Random rnd    = new Random();
        float  hue1   = rnd.nextFloat() * 360f;
        float  offset = 120f + (rnd.nextFloat() * 60f);
        float  hue2   = (hue1 + offset) % 360f;
        int c1 = Color.HSVToColor(new float[]{hue1, 0.85f, 0.9f});
        int c2 = Color.HSVToColor(new float[]{hue2, 0.85f, 0.9f});
        return new int[]{c1, c2};
    }

    /** Drives the selected nav button's marching-dash stroke - see _startNavStrokeRun(). */
    private ValueAnimator navStrokeRunAnim;

    /** Duration (ms) for the dashes to march exactly one full dash+gap cycle - the loop point, so it never visibly "pops". */
    private static final int NAV_STROKE_RUN_MS = 600;

    /**
     * Re-paints the TERM/LEDS/SET footer buttons so the one matching
     * activeIndex reads as "you are here": tinted fill + a marching-dash
     * accent stroke that endlessly runs around its perimeter (see
     * RunningStrokeDrawable), versus a plain static outline + muted text
     * for the other two. Called on every page change (_SetLayout()) and
     * on theme switch (applyThemeColors()) so it never goes stale.
     *
     * @param activeIndex  index into NAV_PAGES / PAGE_NAV_BTNS of the page now on screen.
     */
    private void _updatePageButtonStates(int activeIndex) {
        if (PAGE_NAV_BTNS == null) return;

        float dp = getResources().getDisplayMetrics().density;
        int colorActive   = ThemeManager.getColor(this, R.color.skbar_settings_title);
        int colorInactive = ThemeManager.getColor(this, R.color.text_muted_grey);
        int fillActive    = ThemeManager.getColor(this, R.color.tab_active_tint);
        int strokeIdle    = ThemeManager.getColor(this, R.color.stroke_line_color);

        for (int i = 0; i < PAGE_NAV_BTNS.length; i++) {
            Button b = PAGE_NAV_BTNS[i];
            if (b == null) continue;
            boolean sel = (i == activeIndex);

            if (sel) {
                RunningStrokeDrawable bg = new RunningStrokeDrawable(
                        fillActive, colorActive, 1.5f * dp, 6f * dp, 4f * dp);
                b.setBackground(bg);
            } else {
                GradientDrawable bg = new GradientDrawable();
                bg.setCornerRadius(8 * dp);
                bg.setColor(Color.TRANSPARENT);
                bg.setStroke(Math.round(1f * dp), strokeIdle);
                b.setBackground(bg);
            }
            b.setTextColor(sel ? colorActive : colorInactive);
        }

        _startNavStrokeRun();
    }

    /**
     * Starts (once) a shared, endless ValueAnimator that advances
     * dashPhase through exactly one RunningStrokeDrawable dash+gap cycle
     * per loop, INFINITE repeat - same "one shared animator, not one per
     * button" convention as LEDManager's LED_SelectPulseAnim, so the
     * marching motion is driven from a single clock no matter which
     * button is currently selected. Re-entrant: _updatePageButtonStates()
     * calls this on every page change, but the animator itself is only
     * ever created once (it already applies to whichever button is
     * selected NOW, since it reads PAGE_NAV_BTNS[navPageIndex] fresh on
     * every frame rather than holding a stale reference).
     */
    private void _startNavStrokeRun() {
        if (navStrokeRunAnim != null) return;

        navStrokeRunAnim = ValueAnimator.ofFloat(0f, 1f);
        navStrokeRunAnim.setDuration(NAV_STROKE_RUN_MS);
        navStrokeRunAnim.setRepeatCount(ValueAnimator.INFINITE);
        navStrokeRunAnim.setInterpolator(null); // linear - constant marching speed
        navStrokeRunAnim.addUpdateListener(a -> {
            if (PAGE_NAV_BTNS == null || navPageIndex < 0 || navPageIndex >= PAGE_NAV_BTNS.length) return;
            Button selBtn = PAGE_NAV_BTNS[navPageIndex];
            if (selBtn == null) return;
            android.graphics.drawable.Drawable bg = selBtn.getBackground();
            if (!(bg instanceof RunningStrokeDrawable)) return;
            RunningStrokeDrawable rsd = (RunningStrokeDrawable) bg;
            float f = (float) a.getAnimatedValue();
            rsd.setDashPhase(f * rsd.getCycleLengthPx());
        });
        navStrokeRunAnim.start();
    }

    /**
     * Move to the page at the given index in NAV_PAGES (refresh request
     * included). No-ops silently if index is out of range (shouldn't happen -
     * the three page buttons only ever pass 0/1/2).
     *
     * @param index  Position in NAV_PAGES (TERM / LEDS / SET)
     */
    private void _NavigateToPage(int index) {
        if (index < 0 || index >= NAV_PAGES.length) return;

        ConstraintLayout target = NAV_PAGES[index];

        if (target == LAY_SET) {
            _SetLayout(LAY_SET);
            DATAs.sendSetting(-1, SET.SET_Info, SET.SET_TOTAL);
        } else if (target == LAY_LEDS) {
            _SetLayout(LAY_LEDS);
            DATAs.sendGetColor();
        } else {
            _SetLayout(LAY_CONSOLE);
        }
    }

    /* ====================================================== */
    /*  Background GIF management                             */
    /* ====================================================== */

    /**
     * Load the previously selected background GIF from SharedPreferences
     * and, if valid, move the SET background SeekBar to its index.
     * Called from onCreate.
     */
    public void _InitBackgroundImages() {
        _LoadBackgroundImages();

        if (bgImageList == null || bgImageList.isEmpty()) return;

        String savedName = getSharedPreferences("FuZz_Background", MODE_PRIVATE)
                .getString("BG_FILE", null);

        if (savedName == null || savedName.isEmpty()) {
            Log.e("BG_ACTIVITY", "Background image file is not set in shared preferences.");
            return;
        }

        // Find the saved file and apply it
        DocumentFile found = null;
        for (DocumentFile f : bgImageList) {
            if (f.getName().equals(savedName)) { found = f; break; }
        }

        if (found == null || !found.exists()) {
            getSharedPreferences("FuZz_Background", MODE_PRIVATE)
                    .edit().remove("BG_FILE").apply();
            Log.e("BG_ACTIVITY", "Background image file not found: " + savedName);
        } else {
            _SetBackgroundImage(found);
            Log.d("BG_ACTIVITY", "Background image loaded: " + found.getName());
        }
    }

    /**
    /**
     * Enumerate .gif files from the persisted directory and rebuild the
     * list consumed by BackgroundPopup.
     * Each thumbnail is a 72×72 dp GifImageView; tapping one calls
     * _SetBackgroundImage() and highlights the selection.
     */
    public void _LoadBackgroundImages() {
        bgImagesLoaded = false;

        String bgDir = getSharedPreferences("FuZz_Background", MODE_PRIVATE)
                .getString("BG_DIR", null);

        if (bgDir == null || bgDir.isEmpty()) {
            Log.e("BG_ACTIVITY", "Background image directory is empty/not set.");
            return;
        }

        DocumentFile dir = DocumentFile.fromTreeUri(this, Uri.parse(bgDir));
        if (dir == null || !dir.isDirectory()) {
            Log.e("BG_ACTIVITY", "BG directory does not exist or is not a directory.");
            return;
        }

        bgImageList = new ArrayList<>();
        for (DocumentFile f : dir.listFiles()) {
            if (f.isFile() && f.getName() != null && f.getName().endsWith(".gif")) {
                bgImageList.add(f);
                Log.d("BG_ACTIVITY", "BG image found: " + f.getName());
            }
        }

        if (bgImageList.isEmpty()) {
            _Toast("NO GIF IMAGES FOUND");
            Log.e("BG_ACTIVITY", "No background images found.");
            return;
        }

        // Rebuild filmstrip thumbnails
        bgImagesLoaded = true;
    }

        /**
     * Display a GIF as the app background.
     * Saves the file name to SharedPreferences so it survives restarts.
     *
     * @param bgImage  DocumentFile pointing to the .gif to display.
     *                 Must not be null and must exist.
     */
    public void _SetBackgroundImage(DocumentFile bgImage) {
        GifImageView gifView = findViewById(R.id.gifBackground);

        if (bgImage == null || !bgImage.exists()) {
            Log.e("BG_ACTIVITY", "BG image file does not exist: " + bgImage);
            _Toast("GIF NOT FOUND > " + bgImage.getName());
            return;
        }

        try {
            GifDrawable gif = new GifDrawable(
                    getContentResolver(), bgImage.getUri());
            gifView.setImageDrawable(gif);

            // Update label (strip .gif extension)
            String name = bgImage.getName();
            if (name != null && name.endsWith(".gif")) {
                name = name.substring(0, name.length() - 4);
            }
            // The BG button keeps its own label; the popup shows which GIF
            // is active by outlining its row, so nothing to write here.

            // Persist selected file name
            getSharedPreferences("FuZz_Background", MODE_PRIVATE)
                    .edit().putString("BG_FILE", bgImage.getName()).apply();
            bgImageSet = bgImage.getName();

            Log.d("BG_ACTIVITY", "Background image set: " + bgImage.getName());
        } catch (IOException e) {
            Log.e("BG_ACTIVITY", "Error loading GIF drawable", e);
            _Toast("GIF NOT SET > " + bgImage.getName());
        }
    }

    /* ====================================================== */
    /*  Debug dialog                                          */
    /* ====================================================== */

    /**
     * Show the debug-item list dialog.
     * Tapping an item sends a debug request to the Arduino via DATAs.
     *
     * Items come from SET.SET_Debug[].
     * BTN_Debug (DBG, Term tab) triggers this from _Init_Global().
     */
    private void _Debug() {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        LayoutInflater inflater = getLayoutInflater();
        View view = inflater.inflate(R.layout.lay_debug_list, null);
        builder.setView(view);

        // Fresh inflate every open - never touched by anyone's applyThemeColors(),
        // so paint it here or it always shows the AMBER-original look.
        ThemeManager.tintFrameStroke(this, view, R.color.stroke_line_color);
        TextView header = view.findViewById(R.id.dbg_header_label);
        if (header != null) header.setTextColor(ThemeManager.getColor(this, R.color.text_muted_grey));

        GridView gv = view.findViewById(R.id.grid_debug_item);
        gv.setAdapter(new DebugGridAdapter());

        gv.setOnItemClickListener((parent, v, position, id) -> {
            if (position == SET.SET_Debug.length) {          // virtual TELNET cell
                _TelnetSetEnabled(!telnetEnabled);
                ((android.widget.BaseAdapter) gv.getAdapter()).notifyDataSetChanged();
                return;
            }
            if (position == SET.SET_Debug.length + 1) {       // virtual MQTT LOG cell
                _MqttLogSetEnabled(!mqttLogEnabled);
                ((android.widget.BaseAdapter) gv.getAdapter()).notifyDataSetChanged();
                return;
            }
            DATAs.sendDebug(position);
            _Toast(" DEBUG  {" + SET.SET_Debug[position] + "}");
            _Console(false, "►►",
                    "DEBUG -> {{#C}" + SET.SET_Debug[position] + "{##}}");
            _Console(false, "", "");

            // DIFFUSER debug also pulls the full refill-cycle history (Dh) -
            // SmartTV never caches that on its own (see DIF::RelayHistoryToApp()),
            // so the regular debug dump alone only has the accum/avg/count
            // summary; this is the only way to see the actual per-cycle
            // breakdown from the app. Reply lands in applyDiffuserHistory().
            if ("DIFFUSER".equals(SET.SET_Debug[position])) {
                DATAs.sendDiffuserHistory();
            }
        });

        AlertDialog alert = builder.create();
        alert.setCanceledOnTouchOutside(true);
        alert.show();

        int w = (int) (getResources().getDisplayMetrics().widthPixels  * 0.82);
        alert.getWindow().setLayout(w, android.view.WindowManager.LayoutParams.WRAP_CONTENT);
        alert.getWindow().setBackgroundDrawableResource(
                android.R.color.transparent);
    }

    /**
     * Adapter for the 2-col debug grid (icon + name + subtitle per cell).
     * Two extra virtual cells are appended after SET_Debug[]: TELNET
     * ON/OFF and MQTT LOG ON/OFF, both app-side only (never sent to the
     * Arduino as a K-command) - see _TelnetSetEnabled() / _MqttLogSetEnabled().
     */
    private class DebugGridAdapter extends BaseAdapter {
        @Override public int getCount()             { return SET.SET_Debug.length + 2; }
        @Override public Object getItem(int i)      {
            if (i == SET.SET_Debug.length)     return "TELNET";
            if (i == SET.SET_Debug.length + 1) return "MQTT LOG";
            return SET.SET_Debug[i];
        }
        @Override public long getItemId(int i)      { return i; }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            if (convertView == null) {
                convertView = getLayoutInflater()
                        .inflate(R.layout.lay_debug_item, parent, false);
            }

            // Cells are created/recycled here only - never reached by anyone's
            // applyThemeColors(), so paint them on every bind instead.
            android.graphics.drawable.Drawable cellBg = convertView.getBackground();
            if (cellBg instanceof android.graphics.drawable.GradientDrawable) {
                ((android.graphics.drawable.GradientDrawable) cellBg.mutate())
                        .setColor(ThemeManager.getColor(MainActivity.this, R.color.debug_cell_bg));
            }
            TextView name = convertView.findViewById(R.id.dbg_name);
            TextView sub  = convertView.findViewById(R.id.dbg_sub);
            TextView icon = convertView.findViewById(R.id.dbg_icon);
            name.setTextColor(ThemeManager.getColor(MainActivity.this, R.color.text_default));
            sub.setTextColor(ThemeManager.getColor(MainActivity.this, R.color.text_muted_grey));

            if (position == SET.SET_Debug.length) {          // virtual TELNET cell
                icon.setText("\uD83D\uDDA5\uFE0F");           // 🖥️
                name.setText("TELNET");
                sub.setText(telnetEnabled ? "console ON" : "console OFF");
                sub.setTextColor(telnetEnabled
                        ? ThemeManager.getColor(MainActivity.this, R.color.telnet_status_connected)
                        : ThemeManager.getColor(MainActivity.this, R.color.telnet_status_error));
            } else if (position == SET.SET_Debug.length + 1) { // virtual MQTT LOG cell
                icon.setText("\uD83D\uDCE1");                  // 📡
                name.setText("MQTT LOG");
                sub.setText(mqttLogEnabled ? "trace ON" : "trace OFF");
                sub.setTextColor(mqttLogEnabled
                        ? ThemeManager.getColor(MainActivity.this, R.color.telnet_status_connected)
                        : ThemeManager.getColor(MainActivity.this, R.color.telnet_status_error));
            } else {
                // SET_Debug drives getCount(), so the icon/subtitle tables can
                // fall short if only one of the three is extended. Degrade to a
                // blank cell instead of taking the whole dialog down.
                name.setText(SET.SET_Debug[position]);
                icon.setText(position < SET.SET_DebugIcon.length ? SET.SET_DebugIcon[position] : "");
                sub.setText(position < SET.SET_DebugSub.length  ? SET.SET_DebugSub[position]  : "");
            }
            return convertView;
        }
    }

    /* ====================================================== */
    /*  Console / Debug log                                   */
    /* ====================================================== */

    /**
     * Prepend a formatted message to the debug console.
     * Colour tags in the message are converted to HTML spans.
     *
     * @param rcv      true = received from Arduino (tag coloured green),
     *                 false = sent by app (tag coloured white)
     * @param tag      Short prefix shown in colour, e.g. "◄◄" or "►►"
     * @param message  Message body; supports colour tags:
     *                 {#R} red  {#G} green  {#B} blue  {#Y} yellow
     *                 {#C} cyan {#M} magenta {#O} orange {#W} white
     *                 {##} ends the colour span
     */
    public void _Console(boolean rcv, String tag, String message) {
        // The arrows carried the direction but named no module, which left the
        // source column saying nothing useful. Label the two app-side channels
        // instead - [APP] for anything the device reported back, [CMD] for what
        // we sent - and keep the old green/white tint to show the direction.
        String label = rcv ? "{#G}[APP]{##}" : "{#W}[CMD]{##}";
        _TermAppend(ConsoleAdapter.LVL_LOCAL, ConsoleAdapter.SRC_LOCAL, label, message);
    }

    /**
     * Append one framed log line received from the Arduino.
     *
     * Called by DATAr._recvMsg() once the '*' envelope has been decoded. Any
     * hole in the seq run is reported first, as its own WRN line, so a lost
     * datagram is visible instead of silently missing.
     *
     * @param level  APP_LogLevel (ConsoleAdapter.LVL_*).
     * @param source APP_LogSource id, 0..15.
     * @param seq    Rolling packet seq, 0..255.
     * @param text   Message body; [tokens] render as value chips.
     */
    public void _ConsoleLog(int level, int source, int seq, String text) {
        if (lastLogSeq >= 0) {
            int lost = ((seq - lastLogSeq) & 0xFF) - 1;
            if (lost > 0) {
                _TermAppend(ConsoleAdapter.LVL_WRN, ConsoleAdapter.SRC_LOCAL, null,
                        "\u2500\u2500 [" + lost + "] lines lost \u2500\u2500");
            }
        }
        lastLogSeq = seq;
        _TermAppend(level, source, null, text);
    }

    /** Forget the seq run - the next packet won't be reported as a gap. */
    public void _ConsoleResetSeq() {
        lastLogSeq = -1;
    }

    /**
     * Single append + autoscroll path for every Term line.
     *
     * Only auto-scrolls while termUserScrolledAway is false (see the
     * RV_Console scroll listener in onCreate()). That flag flips true the
     * instant the user starts dragging the list - not once the drag has
     * moved far enough to change the last-visible item - which is what a
     * per-line "was near bottom" recompute here would miss during a fast
     * burst (a debug dump can land 40+ lines within ~1s): each line's own
     * auto-scroll can settle before the user's drag has visibly moved
     * anything, so the old recompute kept reporting "still at the bottom"
     * and snapped the view back under the user's finger on every single
     * line, making scroll-up feel completely blocked.
     */
    private void _TermAppend(int level, int source, String tag, String message) {
        int pos = consoleAdapter.appendLog(level, source, tag, message);
        if (pos >= 0 && !termUserScrolledAway) RV_Console.scrollToPosition(pos);
    }

    /**
     * Clear all lines from the debug console.
     */
    public void _Console_Clear() {
        consoleAdapter.clear();
        lastLogSeq = -1;   // fresh run - don't report the next packet as a gap
    }

    /* ====================================================== */
    /*  Toast  (animated overlay – not Android Toast)         */
    /* ====================================================== */

    /**
     * Show a custom animated toast overlay in the app.
     * Slides in from the top, pulses for 2 s, then fades out.
     * If a toast is already showing, it is cancelled first.
     *
     * @param msg  Message to display.
     */
    /**
     * Re-tints this activity's own theme-aware chrome (ConnectInfo label,
     * debug DBG/CLR buttons) and cascades into LEDManager for
     * the LEDS screen - AVLoading/Toast colors are resolved fresh each
     * time they're shown, so they don't need an explicit re-apply here.
     * Called once at startup and again by SettingsManager.applyTheme().
     */
    public void applyThemeColors() {
        int textColor = ThemeManager.getColor(this, R.color.text_default);
        if (TEXT_ConnectInfo  != null) TEXT_ConnectInfo.setTextColor(textColor);
        if (BTN_Debug         != null) BTN_Debug.setTextColor(textColor);
        if (BTN_ClearConsole  != null) BTN_ClearConsole.setTextColor(textColor);

        ThemeManager.tintFrameStroke(this, BTN_Debug,        R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(this, BTN_ClearConsole, R.color.stroke_line_color);
        ThemeManager.tintFrameStroke(this, TEXT_ConnectInfo, R.color.stroke_line_color);

        _updatePageButtonStates(navPageIndex);

        // Only re-tint the border/text - the fill keeps drifting undisturbed.
        if (dualColorBtnBg != null) {
            float dp = getResources().getDisplayMetrics().density;
            dualColorBtnBg.setStroke(1.5f * dp, ThemeManager.getColor(this, R.color.skbar_settings_title));
        }
        if (BTN_DualColorOpen != null) {
            BTN_DualColorOpen.setTextColor(ThemeManager.getColor(this, R.color.wheel_label_text));
        }

        if (LED != null) LED.applyThemeColors();
    }

    public void _Toast(String msg) {
        if (ToastVisible) {
            LAY_TOAST.setVisibility(View.GONE);
            ToastHandler.removeCallbacksAndMessages(null);
        }
        ToastVisible = true;
        TEXT_ToastView.setText(msg);
        LAY_TOAST.setVisibility(View.VISIBLE);

        TEXT_ToastView.setTextColor(ThemeManager.getColor(this, R.color.toast_text_color));
        TEXT_ToastView.setTypeface(null, Typeface.BOLD);
        TEXT_ToastView.setShadowLayer(2, 1, 1, ThemeManager.getColor(this, R.color.toast_shadow_color));

        // Slide in from top
        ObjectAnimator slideIn = ObjectAnimator.ofFloat(
                TEXT_ToastView, "translationY", -500f, 0f);
        slideIn.setDuration(1000);
        slideIn.start();

        // Start pulse after slide completes
        slideIn.addListener(new AnimatorListenerAdapter() {
            @Override
            public void onAnimationEnd(Animator animation) {
                ObjectAnimator pulse = ObjectAnimator.ofFloat(
                        TEXT_ToastView, "alpha", 1f, 0.5f, 1f);
                pulse.setDuration(1000);
                pulse.setRepeatCount(ValueAnimator.INFINITE);
                pulse.setRepeatMode(ValueAnimator.REVERSE);
                pulse.start();
            }
        });

        // Fade out after 2 s
        AlphaAnimation fadeOut = new AlphaAnimation(1f, 0f);
        fadeOut.setDuration(500);
        fadeOut.setStartOffset(2000);
        TEXT_ToastView.startAnimation(fadeOut);

        // Hide layout after 2.5 s
        ToastHandler.postDelayed(() -> {
            LAY_TOAST.setVisibility(View.GONE);
            ToastVisible = false;
        }, 2500);
    }

    /* ====================================================== */
    /*  Loading bar  (AVLoadingIndicatorView)                 */
    /* ====================================================== */

    /**
     * Show or hide the animated loading spinner.
     *
     * show=true:  Turns the spinner teal and shows it.
     *             Auto-hides in red after 10 s if never cancelled.
     * show=false: Turns the spinner cyan and hides it immediately.
     *
     * @param show  true = show loading, false = hide loading.
     */
    public void _LoadingBar(boolean show) {
        AVLoading_Handler.removeCallbacksAndMessages(null);
        if (show) {
            AVLoading.setIndicatorColor(ThemeManager.getColor(this, R.color.loading_active));
            AVLoading.smoothToShow();
            // Auto-hide with red colour after 10 s (indicates timeout)
            AVLoading_Handler.postDelayed(() -> {
                if (AVLoading.isShown()) {
                    AVLoading.setIndicatorColor(ContextCompat.getColor(this, R.color.loading_timeout));
                    AVLoading.smoothToHide();
                }
            }, 10000);
        } else {
            if (AVLoading.isShown()) {
                AVLoading.setIndicatorColor(ThemeManager.getColor(this, R.color.loading_idle));
                AVLoading.smoothToHide();
            }
        }
    }

    /**
     * Stop the spinner tinted to reflect a specific ACK result, so a
     * clamped/blocked/locked/etc. command reads differently at a glance
     * than a plain success. Called from DATAs.ackResolve().
     *
     * @param color  Resolved color, see DataSend._colorForResult().
     */
    public void _LoadingBarResult(int color) {
        AVLoading_Handler.removeCallbacksAndMessages(null);
        if (AVLoading.isShown()) {
            AVLoading.setIndicatorColor(color);
            AVLoading.smoothToHide();
        }
    }

    /**
     * Dismiss the boot-time "connecting..." spinner the first time any real
     * confirmation arrives (first packet parsed, over UDP or MQTT). Without
     * this it only ever self-clears via _LoadingBar()'s 10 s safety timeout,
     * even though the app is already live well before that. One-shot -
     * later calls are no-ops so it never fights the per-command spinner.
     */
    public void _CancelTransportFallback() {
        // No-op: the transport supervisor is now persistent (it self-selects
        // local/cloud and reschedules), so a first confirmation must NOT cancel
        // it — that would disable later failover/recovery. Kept for the existing
        // DATAr call site. Supervision is torn down only in onPause()/onDestroy().
    }

    public void _ClearBootLoading() {
        if (bootLoadingCleared) return;
        bootLoadingCleared = true;
        _LoadingBar(false);
    }

    /* ====================================================== */
    /*  Shake button animation                                */
    /* ====================================================== */

    /**
     * Play a left-right shake animation on a button.
     * Used for tactile confirmation on button presses.
     *
     * @param button    View to animate. Must not be null.
     * @param duration  Animation duration in milliseconds.
     */
    public void _ShakeButton(View button, int duration) {
        // Takes View rather than Button: the animation is pure translationX,
        // and the footer BG / TEST controls are held as TextView references.
        ObjectAnimator shake = ObjectAnimator.ofFloat(
                button, "translationX", 0f, 10f, -10f, 10f, -10f, 0f);
        shake.setDuration(duration);
        shake.start();
    }

    /* ====================================================== */
    /*  ConnectInfo label effects - neon flicker / scanline    */
    /* ====================================================== */
    /*  Connected (Connected/Cloud) -> neon flicker (this label
     *  IS the good/normal state, so it stays in the theme's own
     *  accent colour - text_default - rather than switching to
     *  status_on_green, which would clash with the amber palette).
     *  Not connected (everything else) -> sliding red scanline
     *  underline, reusing status_off_red so it reads as "trouble"
     *  the same way every other status dot in the app already does.
     *  Both replace the old always-on rainbow gradient - see
     *  DataReceive._setStatus() for what calls these and when. */

    /**
     * Start the neon-flicker effect: an irregular alpha dip (like a
     * neon sign settling) plus a constant soft glow, looping forever.
     * Idempotent - returns immediately if already running.
     *
     * @param textView  The TextView to animate (typically TEXT_ConnectInfo).
     */
    public void _NeonFlickerON(TextView textView) {
        if (ConnectNeonAnimator != null && ConnectNeonAnimator.isRunning()) return;

        int glow = ContextCompat.getColor(this, R.color.text_glow_shadow);
        textView.setShadowLayer(10f, 0f, 0f, glow);

        // Irregular flicker beats (three quick dips near the start of each
        // ~3.2s loop, then a long steady stretch) - keyframe fractions, not
        // a smooth sine, so it actually reads as "flicker" rather than "pulse".
        PropertyValuesHolder pvh = PropertyValuesHolder.ofKeyframe("alpha",
                Keyframe.ofFloat(0f,    1f),
                Keyframe.ofFloat(0.03f, 1f),
                Keyframe.ofFloat(0.04f, 0.4f),
                Keyframe.ofFloat(0.05f, 1f),
                Keyframe.ofFloat(0.06f, 1f),
                Keyframe.ofFloat(0.07f, 0.4f),
                Keyframe.ofFloat(0.08f, 1f),
                Keyframe.ofFloat(0.09f, 1f),
                Keyframe.ofFloat(0.10f, 0.4f),
                Keyframe.ofFloat(0.11f, 1f),
                Keyframe.ofFloat(1f,    1f));

        ConnectNeonAnimator = ObjectAnimator.ofPropertyValuesHolder(textView, pvh);
        ConnectNeonAnimator.setDuration(3200);
        ConnectNeonAnimator.setRepeatCount(ValueAnimator.INFINITE);
        ConnectNeonAnimator.start();
    }

    /**
     * Stop the neon-flicker effect and restore the label to its normal,
     * fully-opaque, un-glowed state.
     *
     * @param textView  The TextView to reset (typically TEXT_ConnectInfo).
     */
    public void _NeonFlickerOFF(TextView textView) {
        if (ConnectNeonAnimator != null && ConnectNeonAnimator.isRunning()) {
            ConnectNeonAnimator.cancel();
        }
        textView.setAlpha(1f);
        textView.setShadowLayer(0f, 0f, 0f, 0);
    }

    /**
     * Start the scanline effect: a short bright bar slides back and forth
     * along the track just inside TEXT_ConnectInfo's bottom edge, sized to
     * whatever the current status text measures (the track's width already
     * follows _connectinfo via ConstraintLayout - see activity_main.xml).
     * Idempotent - returns immediately if already running.
     */
    public void _ScanlineON() {
        if (LAY_ConnectScanline == null || VIEW_ConnectScanlineFill == null) return;
        if (ConnectScanlineAnimator != null && ConnectScanlineAnimator.isRunning()) return;

        LAY_ConnectScanline.setVisibility(View.VISIBLE);

        // Wait one layout pass so LAY_ConnectScanline has picked up
        // _connectinfo's just-changed width before we measure it.
        LAY_ConnectScanline.post(() -> {
            int trackWidth = LAY_ConnectScanline.getWidth();
            if (trackWidth <= 0) return;   // not laid out yet - next status change will retry

            int fillWidth = Math.max((int) (trackWidth * 0.4f), 1);
            ViewGroup.LayoutParams lp = VIEW_ConnectScanlineFill.getLayoutParams();
            lp.width = fillWidth;
            VIEW_ConnectScanlineFill.setLayoutParams(lp);

            float range = trackWidth - fillWidth;
            ConnectScanlineAnimator = ValueAnimator.ofFloat(0f, range);
            ConnectScanlineAnimator.setDuration(1400);
            ConnectScanlineAnimator.setRepeatCount(ValueAnimator.INFINITE);
            ConnectScanlineAnimator.setRepeatMode(ValueAnimator.REVERSE);
            ConnectScanlineAnimator.setInterpolator(new AccelerateDecelerateInterpolator());
            ConnectScanlineAnimator.addUpdateListener(a ->
                    VIEW_ConnectScanlineFill.setTranslationX((float) a.getAnimatedValue()));
            ConnectScanlineAnimator.start();
        });
    }

    /**
     * Stop the scanline effect and hide the track entirely.
     */
    public void _ScanlineOFF() {
        if (ConnectScanlineAnimator != null && ConnectScanlineAnimator.isRunning()) {
            ConnectScanlineAnimator.cancel();
        }
        if (LAY_ConnectScanline != null) LAY_ConnectScanline.setVisibility(View.GONE);
        if (VIEW_ConnectScanlineFill != null) VIEW_ConnectScanlineFill.setTranslationX(0f);
    }

    /* ====================================================== */
    /*  Network helpers                                       */
    /* ====================================================== */

    /**
     * Check whether the device currently has an active WiFi connection.
     * Used by DATAr.isUdpAvailable() and the onResume() flow.
     *
     * @return true if WiFi is connected.
     */
    @SuppressWarnings("deprecation") // NetworkInfo path is the only API available below minSdk 21's ceiling of API 23 (M) - no replacement exists pre-M
    public boolean _IsWifiConn() {
        ConnectivityManager cm = (ConnectivityManager)
                getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Network network = cm.getActiveNetwork();
                if (network != null) {
                    NetworkCapabilities capabilities = cm.getNetworkCapabilities(network);
                    return capabilities != null && capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI);
                }
            } else {
                android.net.NetworkInfo ni = cm.getActiveNetworkInfo();
                return ni != null && ni.getType() == ConnectivityManager.TYPE_WIFI && ni.isConnected();
            }
        }
        return false;
    }

    /**
     * Drive the top-left Arduino / WiFi-signal indicator from a 'w' link packet.
     *
     * The WiFi icon is a 5-level drawable (0 = down .. 4 = full); bars are
     * bucketed from |rssi| the same way the firmware does, so both ends agree.
     * The Arduino icon tints green when the board is reachable, grey/red when
     * the link is down. Marshalled onto the UI thread — 'w' can arrive off it.
     *
     * @param mag        |RSSI| in dBm (0 = link down).
     * @param wifiState  NET_WiFiStatus reported by the board (non-zero = OK).
     */
    public void updateWifiSignal(final int mag, final int wifiState) {
        runOnUiThread(() -> {
            if (IMG_WifiIcon == null || IMG_ArdIcon == null) return;

            final boolean up = (mag != 0) && (wifiState != 0);

            int bars;                                  // match APP_RssiBars() on the board
            if (mag == 0)       bars = 0;
            else if (mag <= 55) bars = 4;
            else if (mag <= 65) bars = 3;
            else if (mag <= 75) bars = 2;
            else                bars = 1;

            IMG_WifiIcon.setImageLevel(bars);          // level-list swaps the glyph
            int wifiTint = !up ? R.color.status_off_red
                    : (bars >= 3 ? R.color.status_on_green
                    : (bars == 2 ? R.color.status_ready_yellow
                                 : R.color.status_autooff_grey));
            IMG_WifiIcon.setColorFilter(ContextCompat.getColor(this, wifiTint));

            IMG_ArdIcon.setColorFilter(ContextCompat.getColor(this,
                    up ? R.color.status_on_green : R.color.text_muted_grey));
        });
    }

    /**
     * Thin wrapper – delegates to DATAr.isUdpAvailable().
     * Kept here so subsystems can call Main._UDP_Available() without
     * needing a direct reference to DATAr.
     *
     * @return true if the UDP socket is open and WiFi is connected.
     */
    public boolean _UDP_Available() {
        return DATAr != null && DATAr.isUdpAvailable();
    }

    /**
     * @return true while the MQTT cloud session is live.
     */
    public boolean _MqttConnected() {
        return MQTT != null && MQTT.isConnected();
    }

    /**
     * A command can go out if EITHER transport is up: local UDP (WiFi) or
     * the MQTT cloud link. Used by DATAs to guard sends.
     *
     * @return true if UDP or MQTT can carry a packet right now.
     */
    public boolean _TransportAvailable() {
        return _UDP_Available() || _MqttConnected();
    }

    /**
     * Publish one already-enveloped command packet over the cloud link.
     * Called by DATAs._transmit() when the local UDP path is unavailable.
     *
     * @param packet  Exact wire string.
     */
    public void _MqttPublish(String packet) {
        if (MQTT != null) MQTT.publish(packet);
    }

    /**
     * Decide + push the status label from the current WiFi/UDP/MQTT state.
     * Single source of truth for onResume() and _connectTransport(), so the
     * two never drift apart. (DATAr._confirmLive() covers the other case:
     * it's called from a receive path where liveness is already proven.)
     */
    private void _refreshTransportStatus() {
        if (DATAr == null) return;
        // Cloud is only reported once the board has actually replied over
        // it (DATAr.isMqttArduinoAlive()) - a live MQTT SESSION alone
        // (_MqttConnected()) just means the phone reached the broker, not
        // that the Arduino is on the other end. Runs every SUPERVISE_MS, so
        // this also self-corrects Cloud -> CloudNoReply if the board goes
        // quiet over cloud without the session itself dropping.
        if (_UDP_Available())                 DATAr._setStatus(DataReceive.Status.Connected);
        else if (DATAr.isMqttArduinoAlive())    DATAr._setStatus(DataReceive.Status.Cloud);
        else if (_MqttConnected())             DATAr._setStatus(DataReceive.Status.CloudNoReply);
        else if (_IsWifiConn())                DATAr._setStatus(DataReceive.Status.NoConnection);
        else                                   DATAr._setStatus(DataReceive.Status.NoWifi);
    }

    /* ====================================================== */
    /*  Utility                                               */
    /* ====================================================== */

    /**
     * Convert a 6-character hex colour string to an int[3] {R, G, B}.
     *
     * @param hex  6-char hex string, e.g. "FF3C00"
     * @return     int[] { R, G, B } each 0-255
     */
    public int[] _HexToRgb(String hex) {
        return new int[]{
            Integer.parseInt(hex.substring(0, 2), 16),
            Integer.parseInt(hex.substring(2, 4), 16),
            Integer.parseInt(hex.substring(4, 6), 16)
        };
    }

} // End of MainActivity
