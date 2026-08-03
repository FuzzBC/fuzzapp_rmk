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
 *  NOTE: credentials live here in clear for now - move MQTT_HOST/
 *      USER/PASS to an untracked secrets file before publishing.
 * ============================================================
 */

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
    /** HiveMQ access credential (username). */
    public static final String MQTT_USER = "fuzzleds";
    /** HiveMQ access credential (password). */
    public static final String MQTT_PASS = "smarttvleds";
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
    // Public API
    // --------------------------------------------------------
    /**
     * Connect + subscribe on a background thread. Idempotent: a no-op while a
     * connect is in flight or the session is already live. Paho's automatic
     * reconnect keeps the session up afterwards; connectComplete re-subscribes.
     */
    public void connect() {
        if (connecting) return;                                  // already trying
        if (client != null && client.isConnected()) return;      // already up
        connecting = true;

        new Thread(() -> {
            try {
                if (client == null) {
                    client = new MqttClient(
                            "ssl://" + MQTT_HOST + ":" + MQTT_PORT,
                            _clientId(), new MemoryPersistence());
                    client.setCallback(callback);
                }
                MqttConnectOptions opts = new MqttConnectOptions();
                opts.setUserName(MQTT_USER);
                opts.setPassword(MQTT_PASS.toCharArray());
                opts.setCleanSession(true);
                opts.setAutomaticReconnect(true);                // survive drops
                opts.setConnectionTimeout(CONNECT_TO_S);
                opts.setKeepAliveInterval(KEEPALIVE_S);

                Log.i("MQTT", "connecting to " + MQTT_HOST + ":" + MQTT_PORT);
                client.connect(opts);
                client.subscribe(MQTT_TOPIC, 0);                 // single duplex topic
                Log.i("MQTT", "connected, subscribed " + MQTT_TOPIC);
                Main.runOnUiThread(() -> {
                    Main._Console(false, "☁", "{#G}CLOUD ON{##}");
                    if (DATAr != null) DATAr._confirmLive();   // flip top label to CLOUD MODE
                    // Local UDP already got the boot welcome (Main.onCreate step 8).
                    // If it didn't - starting off-WiFi / pinned CLOUD ONLY - announce
                    // over the cloud link now so the board pushes a fresh full state.
                    if (!Main._UDP_Available()) Main.DATAs.sendWelcome("mqtt connect");
                });
            } catch (Exception e) {
                Log.e("MQTT", "connect failed: " + e.getMessage());
                Main.runOnUiThread(() ->
                        Main._Console(false, "☁", "{#R}CLOUD FAIL{##}"));
            } finally {
                connecting = false;
            }
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
    // Paho callback
    // --------------------------------------------------------
    private final MqttCallbackExtended callback = new MqttCallbackExtended() {
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
