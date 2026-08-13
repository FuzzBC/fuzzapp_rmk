package com.fuzz.colors;

/*
 * ============================================================
 *  MqttTransport.java  (alias: MQTT)
 * ============================================================
 *  Responsibility:
 *      Cloud transport to the controller over HiveMQ Cloud (MQTT over TLS,
 *      port 8883). PROTOCOL ADAPTATION: the old single duplex topic
 *      ("LEDs/cmd") + 1-byte sender-tag echo filter is replaced by TWO
 *      one-way topics per 01_PROTOCOL.md SS5 - the topic split removes the
 *      self-echo problem structurally, so there's no tag byte to strip
 *      anymore:
 *
 *        OUT  publish(frame)   -> fuzz/<deviceId>/b/c2d (controller<-app)
 *        IN   messageArrived() -> fuzz/<deviceId>/b/d2c (controller->app)
 *
 *      Payloads are raw binary v1 frames now (see com.fuzz.colors.protocol),
 *      byte-identical to the UDP variant, so the same command builders
 *      (DATAs) and the same parser (DATAr.onCloudMessage) are reused
 *      unchanged - MQTT only swaps the pipe. The ACK layer in DATAs
 *      therefore works over the cloud too: ACK frames come back on the d2c
 *      topic and are fed into the same DATAr -> DataSend.ackResolve() path.
 *
 *      <deviceId> was originally the SmartTV's own WiFi MAC, auto-learned
 *      by the app over local UDP (TELEM_DEVICE_ID, see DataReceive) since
 *      there was no other discovery channel - but that requires one local
 *      connection to bootstrap, which isn't always possible (e.g. away from
 *      the home network). DEFAULT_DEVICE_ID below is now a FIXED value
 *      (MQTT_DEVICE_ID_VALUE in the firmware's WiFiCredentials.h) both ends
 *      know from the start, so there's no bootstrap step at all -
 *      setDeviceId()/getDeviceId() and the TELEM_DEVICE_ID path are still
 *      here as a harmless no-op safety net in case the two ever drift.
 *
 *  Transport choice (see DATAs._transmit / Main._TransportAvailable):
 *      WiFi up   -> local UDP  (fast, LAN)
 *      WiFi down -> this MQTT  (works off the controller's network)
 *
 *  Dependency (app/build.gradle):
 *      implementation 'org.eclipse.paho:org.eclipse.paho.client.mqttv3:1.2.5'
 *
 *  TLS: HiveMQ uses a Let's Encrypt chain, trusted by Android's
 *      system store, so the JVM default SSL socket factory is used
 *      (no bundled CA needed).
 *
 *  Wire-level tracing (Debug dialog "MQTT LOG" toggle - see
 *  MainActivity._MqttLogEnabled()): every publish/receive prints as
 *  "MQTT S [...]" / "MQTT R [...]" in the in-app console when ON.
 *  OFF (default) keeps the console to summary lines only (CLOUD ON/LOST/...).
 *
 *  Credentials: MQTT_USER/MQTT_PASS are provisioned per-device:
 *  MainActivity pops a username/password dialog the first time cloud mode
 *  is needed and none are saved, caches them in SharedPreferences
 *  (FuZz_MqttCred, plain MODE_PRIVATE like the rest of the app's local
 *  prefs), and pushes them to the controller over local UDP
 *  (DataSend.sendMqttCredentials()) so both ends agree - see
 *  MQTTCRED::cmdSetCredentials() in the firmware. connect() below is
 *  simply a no-op until hasCredentials() is true.
 * ============================================================
 */

import android.content.Context;
import android.content.SharedPreferences;
import android.provider.Settings;
import android.util.Log;

import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttException;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;

public class MqttTransport {

    // --------------------------------------------------------
    // Broker config - must match the firmware MQTT_* defines
    // --------------------------------------------------------
    /** Cluster URL, no scheme (built into an ssl:// URI below). */
    public static final String MQTT_HOST = "cb6c04d1ec6d4bf7b31ec5533ff91102.s1.eu.hivemq.cloud";
    /** TLS port. */
    public static final int    MQTT_PORT = 8883;
    public static final String TOPIC_BASE = "fuzz/";
    public static final String TOPIC_C2D_SUFFIX = "/b/c2d";   // this app publishes here
    public static final String TOPIC_D2C_SUFFIX = "/b/d2c";   // this app subscribes here
    /** Must match MQTT_DEVICE_ID_VALUE in the firmware's WiFiCredentials.h exactly - see the class doc above. */
    public static final String DEFAULT_DEVICE_ID = "smarttvr4a";

    /** Keep-alive / connection-timeout seconds. */
    private static final int KEEPALIVE_S = 30;
    private static final int CONNECT_TO_S = 15;

    // --------------------------------------------------------
    // Credential storage (SharedPreferences - see class doc above)
    // --------------------------------------------------------
    // Package-private (not private): WidgetStatusFetcher reads the same
    // cached credentials for its own short-lived MQTT connection - same
    // single source of truth as the topic constants above.
    static final String PREFS_NAME  = "FuZz_MqttCred";
    static final String KEY_USER    = "user";
    static final String KEY_PASS    = "pass";
    static final String KEY_DEVICE_ID = "device_id";
    /** True once the user has explicitly dismissed the credentials dialog ("Not now" / back/outside-tap). */
    private static final String KEY_DECLINED = "declined";

    // --------------------------------------------------------
    // Collaborators
    // --------------------------------------------------------
    /** Back-reference for console logging + UI thread. */
    private final MainActivity Main;
    /** Inbound sink - cloud payloads go through the same parser as UDP. */
    private final DataReceive   DATAr;

    // --------------------------------------------------------
    // State (client touched from a single worker thread)
    // --------------------------------------------------------
    private MqttClient client;
    /** true while a connect() attempt is running - keeps connect() idempotent. */
    private volatile boolean connecting = false;
    /** True once a CLOUD FAIL/LOST line has already been printed for the
     * current down-streak - the background supervisor (every SUPERVISE_MS,
     * see MainActivity) retries silently behind the scenes, but the console
     * only needs to say "it broke" once, not on every single retry.
     * Cleared on the next successful connect (see CLOUD ON below). */
    private boolean cloudDownLogged = false;
    /** Guards `client` + `connectGeneration`, AND serializes the actual
     * dial-the-broker section of tryConnect() (see there) - not just the
     * bookkeeping around it. Two overlapping tryConnect() calls (background
     * supervisor vs. an explicit dialog Save, two retries firing close
     * together, ...) build a client with the SAME _clientId() every time;
     * if their connect() calls to the broker were ever allowed to run
     * concurrently, the broker's own duplicate-client-id takeover rule
     * kicks whichever CONNECT packet arrives second, independent of which
     * attempt this app's own bookkeeping had already declared the winner.
     * That produced exactly the "connects fine, then ~1s later Connection
     * lost (reasonCode 32109)" symptom that survived an earlier fix which
     * only serialized the post-connect bookkeeping and not the network
     * call itself. Holding this lock across the whole connect()+subscribe()
     * call guarantees only one CONNECT with this client ID is ever in
     * flight to the broker at a time - a genuinely newer request just
     * waits its turn instead of racing on the wire. connectGeneration lets
     * a request that queued up behind the lock notice a newer one already
     * superseded it, so it can skip dialing the broker entirely instead of
     * connecting and immediately disconnecting again. */
    private final Object connectLock = new Object();
    private int connectGeneration = 0;
    /** Detail from the most recent tryConnect() failure (reason code +
     * message for an MqttException, exception class + message otherwise) -
     * null after a successful connect. Read by MainActivity to show a
     * specific cause instead of a one-size-fits-all "Rejected by broker"
     * hint, which used to cover bad credentials, network errors, and TLS
     * failures identically. */
    public volatile String lastConnectError;
    /** Wall-clock time (System.currentTimeMillis()) of the most recent
     * successful connect+subscribe, or 0 if never connected this app run.
     * Read by connectionLost() to log how long the session actually lived
     * before dropping - the single most useful number for telling "died
     * instantly" apart from "ran fine for a while then dropped", which
     * in-app Log.i()/Log.e() alone (invisible without a PC+adb) couldn't
     * surface at all. See _detailLog() for where this shows up. */
    private volatile long lastConnectSuccessAtMs = 0;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (console + UI thread + ANDROID_ID).
     * @param udpr  Receiver whose parser handles cloud status packets.
     */
    public MqttTransport(MainActivity main, DataReceive udpr) {
        this.Main = main;
        this.DATAr = udpr;
    }

    // --------------------------------------------------------
    // Credential accessors
    // --------------------------------------------------------
    private SharedPreferences _prefs() {
        return Main.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    /** @return true once a username AND password are cached locally. */
    public boolean hasCredentials() {
        SharedPreferences p = _prefs();
        String u = p.getString(KEY_USER, null);
        String pw = p.getString(KEY_PASS, null);
        return u != null && !u.isEmpty() && pw != null && !pw.isEmpty();
    }

    /** @return the cached username, or "" if never saved. */
    public String getUser() {
        return _prefs().getString(KEY_USER, "");
    }

    /** @return the cached password, or "" if never saved. */
    public String getPass() {
        return _prefs().getString(KEY_PASS, "");
    }

    /** @return the cached device id, or DEFAULT_DEVICE_ID (already correct - see class doc) if never overridden. */
    public String getDeviceId() {
        return _prefs().getString(KEY_DEVICE_ID, DEFAULT_DEVICE_ID);
    }

    /**
     * @param deviceId  Override for the controller's MQTT topic id.
     * DEFAULT_DEVICE_ID is already correct out of the box (fixed, configured
     * value - see class doc), so this only matters if TELEM_DEVICE_ID (see
     * DataReceive._recvDeviceId()) ever reports something different, e.g.
     * the firmware's WiFiCredentials.h was reconfigured. If a session is
     * already live under the old topic, drop and reconnect so the change
     * takes effect immediately rather than waiting for the next app
     * restart - same reasoning tryConnect() uses for a credential change.
     */
    public void setDeviceId(String deviceId) {
        if (deviceId.equals(getDeviceId())) return;
        _prefs().edit().putString(KEY_DEVICE_ID, deviceId).apply();
        if (isConnected()) { disconnect(); connect(); }
    }

    /**
     * Cache a verified username/password pair locally. Does NOT push to the
     * controller or touch the live connection - that's MainActivity's job
     * (via DataSend.sendMqttCredentials(), called once the controller has
     * verified the same pair). Called only after that verify succeeds, so
     * what's cached here always matches what the controller has stored.
     *
     * @param user  Username to cache.
     * @param pass  Password to cache.
     */
    public void setCredentials(String user, String pass) {
        _prefs().edit().putString(KEY_USER, user).putString(KEY_PASS, pass).apply();
    }

    /**
     * @return true once the user has explicitly dismissed the credentials
     * dialog without saving anything. MainActivity's automatic trigger
     * (_connectMqttOrPrompt()) checks this before popping the dialog again -
     * an explicit decline means "leave me alone", not "ask again in 5
     * seconds". Persisted (survives app restart) since "don't show again"
     * should actually mean that. The 5-second-hold gesture is the only way
     * back in - it clears this via setPromptDeclined(false).
     */
    public boolean isPromptDeclined() {
        return _prefs().getBoolean(KEY_DECLINED, false);
    }

    /** @param declined  See isPromptDeclined(). */
    public void setPromptDeclined(boolean declined) {
        _prefs().edit().putBoolean(KEY_DECLINED, declined).apply();
    }

    /** Result hook for tryConnect() - invoked on the main thread. */
    public interface ConnectCallback {
        void onResult(boolean ok);
    }

    /** Yellow-tagged, always-on (not gated by the MQTT LOG toggle) console
     * line for the connect/disconnect lifecycle specifically - separate
     * from the wire-level pub/sub tracing (which stays opt-in, see
     * publish()/messageArrived()) because lifecycle events are rare
     * (only on an actual connect attempt or drop) and are exactly the
     * detail needed to diagnose "Connection lost" without a PC+adb: which
     * generation, how long each stage took, and the real exception/reason
     * code, not just a final ok/fail summary. */
    private void _detailLog(String msg) {
        Log.i("MQTT", msg);
        Main.runOnUiThread(() -> Main._Console(false, "☁", "{#Y}mqtt{##} " + msg));
    }

    // --------------------------------------------------------
    // Public API
    // --------------------------------------------------------
    /**
     * Connect + subscribe on a background thread. Idempotent: a no-op while a
     * connect is in flight or the session is already live. A drop is NOT
     * auto-recovered by Paho itself (see tryConnect()'s opts) - the
     * always-on transport supervisor (MainActivity, every SUPERVISE_MS)
     * calls this again on its own regular cadence, which is what actually
     * brings the session back up, serialized through tryConnect() like
     * every other caller. No-op (see hasCredentials() guard below) until the
     * app has provisioned a verified username/password pair. Thin wrapper
     * over tryConnect() using whatever's currently cached.
     */
    public void connect() {
        if (connecting) return;                                  // already trying
        if (client != null && client.isConnected()) return;      // already up
        if (!hasCredentials()) {
            // Nothing to connect with - MainActivity is responsible for
            // popping the credentials dialog (and provisioning the
            // controller) before cloud mode can ever come up. Not logged to
            // the in-app console here to avoid spamming it every time the
            // transport supervisor retries while off-WiFi and unprovisioned.
            Log.v("MQTT", "connect skipped - no credentials saved yet");
            return;
        }
        tryConnect(getUser(), getPass(), null);
    }

    /**
     * Attempt a fresh session with a SPECIFIC candidate user/pass - not
     * necessarily what's cached in prefs. Drops any existing session first
     * (a single MqttClient can only hold one live connection, and this is
     * also what makes a credential CHANGE actually take effect immediately
     * instead of silently keeping a stale prior session). On success the new
     * session becomes the live client from then on (isConnected()/publish()
     * etc. all reflect it) - the caller decides whether/when to persist the
     * pair via setCredentials(); this method only tests+connects, it never
     * touches SharedPreferences itself.
     *
     * Used two ways: (1) MainActivity pre-verifies a freshly-typed pair
     * directly against the broker, before ever bothering the controller with
     * it (see _submitMqttCredentials()); (2) that same call, off-WiFi, is
     * also what brings Cloud Mode up immediately with the new pair rather
     * than waiting for WiFi to return.
     *
     * @param user      Username to try.
     * @param pass      Password to try.
     * @param callback  Invoked on the main thread with true/false once the
     *                  attempt resolves. May be null.
     */
    public void tryConnect(final String user, final String pass, final ConnectCallback callback) {
        final int myGen;
        synchronized (connectLock) {
            connectGeneration++;          // supersede any attempt already in flight
            myGen = connectGeneration;
        }
        connecting = true;
        final String topicD2C = TOPIC_BASE + getDeviceId() + TOPIC_D2C_SUFFIX;
        final String cid = _clientId();
        _detailLog("gen=" + myGen + " tryConnect() called, thread=" + Thread.currentThread().getId()
                + " clientId=" + cid + " user=" + user);

        new Thread(() -> {
            boolean ok = false;
            boolean superseded = false;
            // Whole dial-the-broker section is serialized on connectLock, not
            // just the bookkeeping after it: two tryConnect() calls close
            // together (dialog Save + background retry, a double-tap, etc.)
            // both build a client with the SAME _clientId() - if their
            // connect() calls to the broker were allowed to overlap on the
            // wire, the broker's own duplicate-client-id takeover rule kicks
            // whichever CONNECT packet lost the race, independent of which
            // one this app's generation counter had already declared the
            // winner. That produced exactly the observed symptom: connect
            // succeeds, CLOUD ON fires, then ~1s later the broker's takeover
            // of the OTHER attempt's later-arriving CONNECT kills this one
            // out from under it (Paho reasonCode 32109). Holding this lock
            // across the actual connect()+subscribe() call guarantees only
            // one CONNECT with this client ID is ever in flight to the
            // broker at a time - a genuinely newer request simply waits its
            // turn instead of racing on the wire.
            synchronized (connectLock) {
                if (myGen != connectGeneration) {
                    superseded = true;    // a newer request queued up while we waited for the lock
                    _detailLog("gen=" + myGen + " superseded before dialing broker (current gen=" + connectGeneration + ")");
                } else {
                    final MqttClient staleClient = client;
                    client = null;
                    if (staleClient != null) {
                        boolean staleWasConnected;
                        try { staleWasConnected = staleClient.isConnected(); } catch (Exception e) { staleWasConnected = false; }
                        _detailLog("gen=" + myGen + " dropping stale client (was connected=" + staleWasConnected + ")");
                        try { if (staleWasConnected) staleClient.disconnect(); staleClient.close(); } catch (Exception ignored) { }
                    }
                    final long tStart = System.currentTimeMillis();
                    try {
                        MqttClient localClient = new MqttClient(
                                "ssl://" + MQTT_HOST + ":" + MQTT_PORT,
                                cid, new MemoryPersistence());
                        localClient.setCallback(pahoCallback);
                        MqttConnectOptions opts = new MqttConnectOptions();
                        opts.setUserName(user);
                        opts.setPassword(pass.toCharArray());
                        opts.setCleanSession(true);
                        // Deliberately NOT opts.setAutomaticReconnect(true): Paho's
                        // automatic reconnect runs its own internal background
                        // thread that redials the broker with this SAME client id
                        // the instant a connection drops for ANY reason (a brief
                        // mobile-data hiccup, a keepalive miss) - entirely outside
                        // tryConnect()/connectLock. If the app's own supervisor
                        // (every SUPERVISE_MS, see MainActivity) also notices the
                        // drop and calls connect() around the same time, that's
                        // the exact same duplicate-client-id collision the
                        // connectLock serialization above exists to prevent - just
                        // via a path the lock can never see or serialize against,
                        // since it's internal to Paho, not a call through this
                        // class. Confirmed live: the connectLock fix alone (8.020/
                        // 8.021) did NOT resolve "Connection lost", which pointed
                        // straight back at this. Recovery after a drop now goes
                        // exclusively through the supervisor's own connect() call,
                        // which IS routed through tryConnect()/connectLock like
                        // every other attempt - so only one reconnect path exists,
                        // period.
                        opts.setConnectionTimeout(CONNECT_TO_S);
                        opts.setKeepAliveInterval(KEEPALIVE_S);

                        _detailLog("gen=" + myGen + " dialing " + MQTT_HOST + ":" + MQTT_PORT + " ...");
                        localClient.connect(opts);
                        long connectMs = System.currentTimeMillis() - tStart;
                        _detailLog("gen=" + myGen + " CONNACK ok in " + connectMs + "ms, subscribing " + topicD2C + " ...");

                        localClient.subscribe(topicD2C, 0);
                        long subMs = System.currentTimeMillis() - tStart;
                        client = localClient;
                        ok = true;
                        lastConnectError = null;
                        lastConnectSuccessAtMs = System.currentTimeMillis();
                        _detailLog("gen=" + myGen + " SUBACK ok, total " + subMs + "ms - session live");
                    } catch (Exception e) {
                        long failMs = System.currentTimeMillis() - tStart;
                        String detail = (e instanceof MqttException)
                                ? ("reasonCode=" + ((MqttException) e).getReasonCode() + " " + e.getMessage())
                                : (e.getClass().getSimpleName() + ": " + e.getMessage());
                        lastConnectError = detail;
                        _detailLog("gen=" + myGen + " {#R}FAILED{##} after " + failMs + "ms: " + detail
                                + (e.getCause() != null ? (" | cause: " + e.getCause().getClass().getSimpleName() + ": " + e.getCause().getMessage()) : ""));
                        ok = false;
                    }
                }
            }
            connecting = false;

            if (superseded) {
                // A newer tryConnect() queued up while we were waiting for
                // the lock - this attempt never touched the broker at all,
                // nothing to clean up. The superseding attempt owns the
                // outcome and the callback.
                return;
            }

            final boolean finalOk = ok;
            Main.runOnUiThread(() -> {
                if (finalOk) {
                    cloudDownLogged = false;
                    Main._Console(false, "☁", "{#G}CLOUD ON{##}");
                    if (DATAr != null) DATAr._confirmLive();   // flip top label to CLOUD MODE
                    // Local UDP already got the boot welcome (Main.onCreate step 8).
                    // If it didn't - starting off-WiFi / pinned CLOUD ONLY - announce
                    // over the cloud link now so the board pushes a fresh full state.
                    if (!Main._UDP_Available()) Main.DATAs.sendWelcome("mqtt connect");
                } else if (!cloudDownLogged) {
                    cloudDownLogged = true;
                    Main._Console(false, "☁", "{#R}CLOUD FAIL{##} (retrying quietly)");
                }
                if (callback != null) callback.onResult(finalOk);
            });
        }).start();
    }


    /**
     * Publish one already-built binary v1 frame to the c2d topic on a
     * background thread. No sender-tag byte anymore - the topic split
     * itself prevents self-echo.
     *
     * @param frame  Exact wire bytes (already built by DATAs/Frame.build()).
     */
    public void publish(final byte[] frame) {
        final MqttClient c = client;
        if (c == null || !c.isConnected()) {
            Log.e("MQTT", "PUB dropped, not connected [" + frame.length + " bytes]");
            return;
        }
        final String topicC2D = TOPIC_BASE + getDeviceId() + TOPIC_C2D_SUFFIX;
        new Thread(() -> {
            try {
                c.publish(topicC2D, new MqttMessage(frame));
                Log.i("MQTT_S", "PUB [" + frame.length + " bytes]");
                if (Main._MqttLogEnabled())
                    Main.runOnUiThread(() -> Main._Console(false, "☁", "MQTT S [{#C}" + frame.length + " bytes{##}]"));
            } catch (Exception e) {
                Log.e("MQTT", "PUB failed: " + e.getMessage());
            }
        }).start();
    }

    /** @return true while the broker session is live. */
    public boolean isConnected() {
        return client != null && client.isConnected();
    }

    /** Disconnect and release the client. Call from Main.onDestroy(). Also
     * bumps connectGeneration so an in-flight tryConnect() (if any) notices
     * it's been superseded and backs off instead of resurrecting a session
     * right after this explicit disconnect - see tryConnect()'s doc. */
    public void disconnect() {
        final MqttClient c;
        final int gen;
        synchronized (connectLock) {
            connectGeneration++;
            gen = connectGeneration;
            c = client;
            client = null;
        }
        if (c == null) return;
        _detailLog("gen=" + gen + " explicit disconnect() called");
        new Thread(() -> {
            try {
                if (c.isConnected()) c.disconnect();
                c.close();
            } catch (Exception ignored) { }
        }).start();
    }

    // --------------------------------------------------------
    // Paho callback (renamed from `callback` to avoid shadowing the
    // ConnectCallback parameter of the same name in tryConnect())
    // --------------------------------------------------------
    private final MqttCallbackExtended pahoCallback = new MqttCallbackExtended() {
        @Override
        public void connectComplete(boolean reconnect, String serverURI) {
            // `reconnect` is only ever true for Paho's OWN automatic reconnect,
            // which is deliberately disabled (see tryConnect()'s opts) - every
            // real reconnect goes through tryConnect() instead, which already
            // subscribes itself before handing off the client. Nothing to do
            // here; kept only because MqttCallbackExtended requires an override.
            if (reconnect) {
                Log.w("MQTT", "unexpected Paho-internal reconnect (should be disabled)");
            }
        }

        @Override
        public void messageArrived(String topic, MqttMessage message) {
            byte[] raw = message.getPayload();
            if (raw.length == 0) return;

            // No sender-tag byte to strip anymore (the c2d/d2c topic split
            // itself prevents self-echo) - hand the frame straight to the
            // shared binary parser, same one the UDP receive loop uses.
            if (Main._MqttLogEnabled())
                Main.runOnUiThread(() -> Main._Console(true, "☁", "MQTT R [{#C}" + raw.length + " bytes{##}]"));
            if (DATAr != null) DATAr.onCloudMessage(raw, raw.length);   // marshals to UI thread itself
        }

        @Override
        public void connectionLost(Throwable cause) {
            // Age of the session at the moment it dropped - the single most
            // useful number for telling "never really connected" apart from
            // "connected, ran fine, then genuinely dropped later" (e.g. a
            // mobile-network idle timeout below KEEPALIVE_S=30s). 0/negative
            // if this fires before any successful connect this app run.
            long ageMs = lastConnectSuccessAtMs > 0 ? (System.currentTimeMillis() - lastConnectSuccessAtMs) : -1;
            String causeDetail = (cause == null) ? "?"
                    : cause.getClass().getSimpleName() + ": " + cause.getMessage()
                    + (cause.getCause() != null ? (" | cause: " + cause.getCause().getClass().getSimpleName() + ": " + cause.getCause().getMessage()) : "");
            _detailLog("{#Y}connectionLost{##} after " + ageMs + "ms since last successful connect - " + causeDetail);
            Main.runOnUiThread(() -> {
                if (!cloudDownLogged) {
                    cloudDownLogged = true;
                    Main._Console(false, "☁", "{#Y}CLOUD LOST{##} (retrying quietly)");
                }
                // Cloud dropped while off-WiFi -> drop the CLOUD MODE label.
                if (!Main._IsWifiConn())
                    DATAr._setStatus(DataReceive.Status.NoWifi);
            });
        }

        @Override
        public void deliveryComplete(IMqttDeliveryToken token) { /* QoS 0 - unused */ }
    };

    // --------------------------------------------------------
    // Helpers
    // --------------------------------------------------------
    /**
     * Stable, unique client id so the broker never kicks us for a duplicate and
     * we don't pile up sessions across app restarts. Falls back to a random id
     * if ANDROID_ID is unavailable.
     *
     * "-rmk" suffix (not in the frozen original's "ControlRGB-Android-<id>")
     * is deliberate, not cosmetic: this app shares the same package name and
     * signing key as the frozen original, so ANDROID_ID - and therefore the
     * WHOLE client id string - is identical on any device with both
     * installed. MQTT brokers kick the OLDER session the instant a second
     * client connects with the same id; with both apps' automaticReconnect
     * enabled, that's a connect/kick ping-pong forever, not a one-time
     * event - confirmed live as the "reasonCode=32109 Connection lost"
     * that kept happening even after fixing an unrelated concurrency bug
     * in tryConnect(). The frozen original is never getting this fix (it's
     * untouched on purpose), so this side has to be the one that changes.
     */
    private String _clientId() {
        String id;
        try {
            id = Settings.Secure.getString(
                    Main.getContentResolver(), Settings.Secure.ANDROID_ID);
        } catch (Exception e) {
            id = null;
        }
        if (id == null || id.isEmpty()) {
            id = Integer.toHexString((int) (Math.random() * 0x7FFFFFFF));
        }
        return "ControlRGB-Android-rmk-" + id;
    }
}
