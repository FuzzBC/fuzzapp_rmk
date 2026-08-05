package com.fuzz.colors;

/*
 * ============================================================
 *  MqttTransport.java  (alias: MQTT)
 * ============================================================
 *  Responsibility:
 *      Cloud transport to the controller over HiveMQ Cloud
 *      (MQTT over TLS, port 8883). Mirror of the local UDP link,
 *      over a SINGLE duplex topic (MQTT_TOPIC = LEDs/cmd) that both
 *      ends publish AND subscribe to:
 *
 *        OUT  publish(packet)  -> MQTT_TOPIC, tagged TAG_APP
 *        IN   messageArrived() -> drop our own echo (TAG_APP),
 *                                  else strip TAG_DEV -> DATAr.onCloudMessage()
 *
 *      No LWT / retained presence flag - online/offline is inferred from
 *      traffic on MQTT_TOPIC (welcome + keep-alive), same as the existing
 *      UDP watchdog (see DataReceive.CONN_LOST_TIMEOUT).
 *
 *      The wire protocol is byte-identical to the UDP variant (once the
 *      1-byte sender tag is stripped), so the same command builders
 *      (DATAs) and the same parser (DATAr._parsePacket) are reused
 *      unchanged - MQTT only swaps the pipe. The ACK layer in DATAs
 *      therefore works over the cloud too: "#SSR" acks come back on
 *      MQTT_TOPIC and are fed into the same DATAr._recvAck() ->
 *      DATAs.ackResolve() path.
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
 *  Credentials: MQTT_USER/MQTT_PASS are NOT hardcoded (they used to be,
 *  shared by every install of this app+firmware pair - removed). Instead
 *  they're provisioned per-device: MainActivity pops a username/password
 *  dialog the first time cloud mode is needed and none are saved, caches
 *  them in SharedPreferences (FuZz_MqttCred, plain MODE_PRIVATE like the
 *  rest of the app's local prefs - no at-rest encryption here, only the
 *  wire hop to the controller is base64-framed), and pushes them to the
 *  controller over local UDP (DataSend.sendMqttCredentials()) so both ends
 *  agree - see MQTTCRED::cmdSetCredentials() in the firmware. connect()
 *  below is simply a no-op until hasCredentials() is true.
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
    /** Topic root - one per device; must match firmware MQTT_BASE. */
    public static final String MQTT_BASE = "LEDs";
    /** Single duplex topic - both ends publish AND subscribe here; must match firmware MQTT_TOPIC. */
    public static final String MQTT_TOPIC = MQTT_BASE + "/cmd";
    /** Sender tag WE publish with (byte 0 of every payload) - lets our own subscription drop the echo. */
    private static final byte TAG_APP = 'A';
    /** Sender tag expected on inbound device publishes; must match firmware MQTT_TAG_DEV. */
    private static final byte TAG_DEV = 'D';

    /** Keep-alive / connection-timeout seconds. */
    private static final int KEEPALIVE_S = 30;
    private static final int CONNECT_TO_S = 15;

    // --------------------------------------------------------
    // Credential storage (SharedPreferences - see class doc above)
    // --------------------------------------------------------
    private static final String PREFS_NAME  = "FuZz_MqttCred";
    private static final String KEY_USER    = "user";
    private static final String KEY_PASS    = "pass";
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

    // --------------------------------------------------------
    // Public API
    // --------------------------------------------------------
    /**
     * Connect + subscribe on a background thread. Idempotent: a no-op while a
     * connect is in flight or the session is already live. Paho's automatic
     * reconnect keeps the session up afterwards; connectComplete re-subscribes.
     * No-op (see hasCredentials() guard below) until the app has provisioned
     * a verified username/password pair. Thin wrapper over tryConnect() using
     * whatever's currently cached.
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
        if (client != null) disconnect();   // drop any session opened under a previous/different pair
        connecting = true;

        new Thread(() -> {
            boolean ok;
            try {
                client = new MqttClient(
                        "ssl://" + MQTT_HOST + ":" + MQTT_PORT,
                        _clientId(), new MemoryPersistence());
                client.setCallback(pahoCallback);
                MqttConnectOptions opts = new MqttConnectOptions();
                opts.setUserName(user);
                opts.setPassword(pass.toCharArray());
                opts.setCleanSession(true);
                opts.setAutomaticReconnect(true);                // survive drops
                opts.setConnectionTimeout(CONNECT_TO_S);
                opts.setKeepAliveInterval(KEEPALIVE_S);

                Log.i("MQTT", "connecting to " + MQTT_HOST + ":" + MQTT_PORT);
                client.connect(opts);
                client.subscribe(MQTT_TOPIC, 0);                 // single duplex topic
                Log.i("MQTT", "connected, subscribed " + MQTT_TOPIC);
                ok = true;
            } catch (Exception e) {
                Log.e("MQTT", "connect failed: " + e.getMessage());
                ok = false;
            } finally {
                connecting = false;
            }

            final boolean finalOk = ok;
            Main.runOnUiThread(() -> {
                if (finalOk) {
                    Main._Console(false, "☁", "{#G}CLOUD ON{##}");
                    if (DATAr != null) DATAr._confirmLive();   // flip top label to CLOUD MODE
                    // Local UDP already got the boot welcome (Main.onCreate step 8).
                    // If it didn't - starting off-WiFi / pinned CLOUD ONLY - announce
                    // over the cloud link now so the board pushes a fresh full state.
                    if (!Main._UDP_Available()) Main.DATAs.sendWelcome("mqtt connect");
                } else {
                    Main._Console(false, "☁", "{#R}CLOUD FAIL{##}");
                }
                if (callback != null) callback.onResult(finalOk);
            });
        }).start();
    }


    /**
     * Publish one command packet to LEDs/cmd on a background thread. Bytes are
     * already enveloped/built by DATAs, so this is a straight pass-through.
     *
     * @param packet  Exact wire string (e.g. "#a3LBff" or "Z").
     */
    public void publish(final String packet) {
        final MqttClient c = client;
        if (c == null || !c.isConnected()) {
            Log.e("MQTT", "PUB dropped, not connected [" + packet + "]");
            return;
        }
        new Thread(() -> {
            try {
                byte[] body = packet.getBytes();
                byte[] tagged = new byte[body.length + 1];
                tagged[0] = TAG_APP;                              // sender tag - echo filter
                System.arraycopy(body, 0, tagged, 1, body.length);
                c.publish(MQTT_TOPIC, new MqttMessage(tagged));
                Log.i("MQTT_S", "PUB [" + packet + "]");
                if (Main._MqttLogEnabled())
                    Main.runOnUiThread(() -> Main._Console(false, "☁", "MQTT S [{#C}" + packet + "{##}]"));
            } catch (Exception e) {
                Log.e("MQTT", "PUB failed [" + packet + "]: " + e.getMessage());
            }
        }).start();
    }

    /** @return true while the broker session is live. */
    public boolean isConnected() {
        return client != null && client.isConnected();
    }

    /** Disconnect and release the client. Call from Main.onDestroy(). */
    public void disconnect() {
        final MqttClient c = client;
        client = null;
        if (c == null) return;
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
            // Re-subscribe after an automatic reconnect (a fresh session drops subs).
            if (reconnect) {
                try {
                    client.subscribe(MQTT_TOPIC, 0);
                }
                catch (Exception e) { Log.e("MQTT", "resub failed: " + e.getMessage()); }
                Log.i("MQTT", "reconnected");
            }
        }

        @Override
        public void messageArrived(String topic, MqttMessage message) {
            byte[] raw = message.getPayload();
            if (raw.length < 1 || raw[0] == TAG_APP) return;      // empty or our own echo - drop

            // Compressed colour packet ('L''K' right after the tag byte): raw
            // binary, so it must NOT be String-decoded. Hand the tag-stripped
            // bytes to the binary sink; everything else keeps the ASCII path.
            if (raw.length >= 3 && raw[1] == 'L' && raw[2] == 'K') {
                final byte[] body = java.util.Arrays.copyOfRange(raw, 1, raw.length);
                if (DATAr != null) DATAr.onCloudMessageBin(body, body.length);
                return;
            }

            // Same wire format as UDP once the sender tag is stripped - hand
            // straight to the shared parser. No LWT: presence is inferred from
            // this traffic (welcome/keep-alive), same as the local UDP link.
            final String payload = new String(raw, 1, raw.length - 1);
            if (Main._MqttLogEnabled())
                Main.runOnUiThread(() -> Main._Console(true, "☁", "MQTT R [{#C}" + payload + "{##}]"));
            if (DATAr != null) DATAr.onCloudMessage(payload);      // marshals to UI thread itself
        }

        @Override
        public void connectionLost(Throwable cause) {
            Log.w("MQTT", "connection lost: " + (cause != null ? cause.getMessage() : "?"));
            Main.runOnUiThread(() -> {
                Main._Console(false, "☁", "{#Y}CLOUD LOST{##}");
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
        return "ControlRGB-Android-" + id;
    }
}
