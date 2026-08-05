package com.fuzz.colors;

/*
 * ============================================================
 *  WidgetStatusFetcher.java
 * ============================================================
 *  Headless equivalent of DataSend/DataReceive/MqttTransport's welcome +
 *  climate/diffuser handling, deliberately NOT reusing those classes
 *  directly - they're all built around a live MainActivity (toasts,
 *  console lines, UI-thread posts) that doesn't exist while
 *  WidgetUpdateWorker runs in the background with no Activity alive.
 *  This duplicates just the handful of lines actually needed:
 *
 *      - the bare "Z" welcome (DataSend.sendWelcomeUdpDirect())
 *      - the 'H'/'u'/'s' packet parsing (byte-identical to
 *        DataReceive._recvClimate()/_recvDiffuserUsage()/_recvStatus(),
 *        the same refill-percent formula as
 *        StatusManager.applyDiffuserUsage(), and the same STS_Diffuser
 *        ordinal layout as StatusManager.applyStatus() for NO_WATER)
 *      - MqttTransport's wire framing (TAG_APP/TAG_DEV byte, same
 *        host/topic/credential-prefs constants - reused directly, not
 *        copied, so there is exactly one source of truth for those)
 *
 *  Same fallback order the live app already uses (see
 *  DataSend._transmit()): try local UDP first, only fall back to MQTT
 *  cloud if nothing answers on the LAN.
 * ============================================================
 */

import android.content.Context;
import android.content.SharedPreferences;

import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken;
import org.eclipse.paho.client.mqttv3.MqttCallback;
import org.eclipse.paho.client.mqttv3.MqttClient;
import org.eclipse.paho.client.mqttv3.MqttConnectOptions;
import org.eclipse.paho.client.mqttv3.MqttMessage;
import org.eclipse.paho.client.mqttv3.persist.MemoryPersistence;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

class WidgetStatusFetcher {

    private static final String TAG = "WIDGET_FETCH";

    /** STS_Diffuser ordinal for NO_WATER - must match StatusManager.STS_Diffuser's declaration order {OFF, ON, NO_WATER, NO_RESPONSE, PARFUM}. */
    private static final int DIFFUSER_ORDINAL_NO_WATER = 2;

    /** One fetch attempt's outcome - fields are null wherever that particular reading wasn't in the reply. */
    static final class Result {
        final Integer tempC, humPct, diffuserPct;
        /** true/false only when an 's' status packet was actually seen this fetch; null = not received, caller should leave the cached value alone. */
        final Boolean noWater;
        /** "WIFI" (answered over local UDP) or "CLOUD" (answered over MQTT). Never null on a non-null Result. */
        final String transport;

        Result(Integer tempC, Integer humPct, Integer diffuserPct, Boolean noWater, String transport) {
            this.tempC = tempC;
            this.humPct = humPct;
            this.diffuserPct = diffuserPct;
            this.noWater = noWater;
            this.transport = transport;
        }
    }

    private static final int UDP_TIMEOUT_MS  = 3000;
    private static final int MQTT_TIMEOUT_MS = 8000;

    /**
     * Try local UDP, then MQTT cloud. Returns null if neither transport
     * produced a usable reply within its own timeout (board fully
     * unreachable - caller should keep whatever it last cached).
     */
    static Result fetch(Context ctx) {
        Result local = _fetchLocalUdp();
        if (local != null) return local;
        return _fetchMqttCloud(ctx);
    }

    // --------------------------------------------------------
    // Local UDP
    // --------------------------------------------------------
    private static Result _fetchLocalUdp() {
        try (DatagramSocket socket = new DatagramSocket()) {
            socket.setSoTimeout(UDP_TIMEOUT_MS);
            byte[] hello = "Z".getBytes();
            socket.send(new DatagramPacket(hello, hello.length,
                    InetAddress.getByName(DataSend.ARDUINO_IP), DataSend.ARDUINO_PORT));

            Integer temp = null, hum = null, diffuserPct = null;
            Boolean noWater = null;
            long deadline = System.currentTimeMillis() + UDP_TIMEOUT_MS;
            byte[] buf = new byte[512];
            while (System.currentTimeMillis() < deadline && (temp == null || diffuserPct == null)) {
                DatagramPacket packet = new DatagramPacket(buf, buf.length);
                try {
                    socket.receive(packet);
                } catch (SocketTimeoutException e) {
                    break;
                }
                String msg = new String(packet.getData(), 0, packet.getLength());
                int[] climate = _parseClimate(msg);
                if (climate != null) { temp = climate[0]; hum = climate[1]; }
                Integer pct = _parseDiffuserPercent(msg);
                if (pct != null) diffuserPct = pct;
                Boolean nw = _parseDiffuserNoWater(msg);
                if (nw != null) noWater = nw;
            }
            if (temp == null && diffuserPct == null && noWater == null) return null;   // nothing arrived - not reachable on the LAN
            return new Result(temp, hum, diffuserPct, noWater, "WIFI");
        } catch (IOException e) {
            return null;
        }
    }

    // --------------------------------------------------------
    // MQTT cloud
    // --------------------------------------------------------
    private static Result _fetchMqttCloud(Context ctx) {
        SharedPreferences prefs = ctx.getSharedPreferences(MqttTransport.PREFS_NAME, Context.MODE_PRIVATE);
        String user = prefs.getString(MqttTransport.KEY_USER, null);
        String pass = prefs.getString(MqttTransport.KEY_PASS, null);
        if (user == null || user.isEmpty() || pass == null || pass.isEmpty()) return null;   // no cloud credentials provisioned - see MqttTransport class doc

        MqttClient client = null;
        try {
            String uri = "ssl://" + MqttTransport.MQTT_HOST + ":" + MqttTransport.MQTT_PORT;
            client = new MqttClient(uri, "widget-" + System.currentTimeMillis(), new MemoryPersistence());

            final Integer[] tempHolder = {null};
            final Integer[] humHolder  = {null};
            final Integer[] pctHolder  = {null};
            final Boolean[] noWaterHolder = {null};
            // Counts down only once BOTH temp and diffuserPct are in hand -
            // matches _fetchLocalUdp()'s own loop condition. The board
            // answers "Z" with separate H/u/s packets that don't all land
            // at once; counting down on the FIRST packet of ANY kind (the
            // old behaviour) meant a status ('s') packet arriving first
            // returned immediately, permanently missing climate/diffuser
            // data that was still in flight - this is what caused the
            // widget to show a real "Updated" timestamp with blank
            // temp/hum/diffuser values every time.
            final CountDownLatch gotEnough = new CountDownLatch(1);

            client.setCallback(new MqttCallback() {
                @Override public void connectionLost(Throwable cause) { }

                @Override public void messageArrived(String topic, MqttMessage message) {
                    byte[] payload = message.getPayload();
                    if (payload.length < 2 || payload[0] != MqttTransport.TAG_DEV) return;   // drop our own echo / malformed
                    String msg = new String(payload, 1, payload.length - 1);
                    int[] climate = _parseClimate(msg);
                    if (climate != null) { tempHolder[0] = climate[0]; humHolder[0] = climate[1]; }
                    Integer pct = _parseDiffuserPercent(msg);
                    if (pct != null) pctHolder[0] = pct;
                    Boolean nw = _parseDiffuserNoWater(msg);
                    if (nw != null) noWaterHolder[0] = nw;
                    if (tempHolder[0] != null && pctHolder[0] != null) gotEnough.countDown();
                }

                @Override public void deliveryComplete(IMqttDeliveryToken token) { }
            });

            MqttConnectOptions opts = new MqttConnectOptions();
            opts.setUserName(user);
            opts.setPassword(pass.toCharArray());
            opts.setCleanSession(true);
            opts.setConnectionTimeout(MQTT_TIMEOUT_MS / 1000);
            client.connect(opts);
            client.subscribe(MqttTransport.MQTT_TOPIC);

            byte[] payload = new byte[]{MqttTransport.TAG_APP, 'Z'};
            client.publish(MqttTransport.MQTT_TOPIC, payload, 0, false);

            // Waits out the full timeout unless both arrive sooner - same
            // trade-off _fetchLocalUdp() makes, so a diffuser that's still
            // "learning" (see _parseDiffuserPercent's guard) doesn't cut
            // this short either; whatever DID arrive by then is still used.
            gotEnough.await(MQTT_TIMEOUT_MS, TimeUnit.MILLISECONDS);

            if (tempHolder[0] == null && pctHolder[0] == null && noWaterHolder[0] == null) return null;
            return new Result(tempHolder[0], humHolder[0], pctHolder[0], noWaterHolder[0], "CLOUD");
        } catch (Exception e) {
            return null;
        } finally {
            if (client != null) {
                try { client.disconnect(); client.close(); } catch (Exception ignored) { }
            }
        }
    }

    // --------------------------------------------------------
    // Shared parsing - same wire format DataReceive/StatusManager use
    // --------------------------------------------------------

    /** 'HTTHH' -> {temp, hum} (both 2-hex), or null if not a climate packet / malformed. Mirrors DataReceive._recvClimate(). */
    private static int[] _parseClimate(String msg) {
        if (msg.length() < 5 || msg.charAt(0) != 'H') return null;
        try {
            int temp = Integer.parseInt(msg.substring(1, 3), 16);
            int hum  = Integer.parseInt(msg.substring(3, 5), 16);
            return new int[]{temp, hum};
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /**
     * 'uAAAAVVVVRRLLLL' -> refill percent, or null if not a diffuser-usage
     * packet, malformed, or not enough history yet to compute a percent
     * (same "still learning" guard as StatusManager.applyDiffuserUsage()).
     */
    private static Integer _parseDiffuserPercent(String msg) {
        if (msg.length() < 15 || msg.charAt(0) != 'u') return null;
        try {
            int accumMin    = Integer.parseInt(msg.substring(1, 5), 16);
            int avgMin      = Integer.parseInt(msg.substring(5, 9), 16);
            int refillCount = Integer.parseInt(msg.substring(9, 11), 16);
            if (refillCount == 0 || avgMin <= 0) return null;
            return Math.max(0, Math.min(100, 100 - (accumMin * 100 / avgMin)));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /**
     * 'sTTMMUUAADD' -> true if the diffuser sub-field (last 2-hex, DD)
     * decodes to STS_Diffuser.NO_WATER, false for any other valid
     * diffuser state, or null if not a status packet / malformed.
     * Mirrors DataReceive._recvStatus() -> StatusManager.applyStatus().
     */
    private static Boolean _parseDiffuserNoWater(String msg) {
        if (msg.length() != 11 || msg.charAt(0) != 's') return null;
        try {
            int diffuserOrdinal = Integer.parseInt(msg.substring(9, 11), 16);
            return diffuserOrdinal == DIFFUSER_ORDINAL_NO_WATER;
        } catch (NumberFormatException e) {
            return null;
        }
    }
}
