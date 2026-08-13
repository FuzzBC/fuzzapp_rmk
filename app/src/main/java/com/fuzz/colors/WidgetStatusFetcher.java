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
 *      - the HELLO handshake (DataSend.sendWelcomeUdpDirect())
 *      - TELEM_CLIMATE/TELEM_DIFFUSER_USAGE/TELEM_STATUS frame decoding via
 *        com.fuzz.colors.protocol.Frame/ProtocolOpcodes (byte-identical to
 *        DataReceive._recvClimate()/_recvDiffuserUsage()/_recvStatus()),
 *        the same refill-percent formula as
 *        StatusManager.applyDiffuserUsage(), and the same STS_Diffuser
 *        ordinal layout as StatusManager.applyStatus() for NO_WATER
 *      - MqttTransport's wire framing (two one-way topics, same
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
import android.util.Log;

import com.fuzz.colors.protocol.Frame;
import com.fuzz.colors.protocol.ProtocolOpcodes;

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
        /** true/false only when a TELEM_STATUS frame was actually seen this fetch; null = not received, caller should leave the cached value alone. */
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
        Log.d(TAG, "fetch() starting - trying local UDP first");
        Result local = _fetchLocalUdp();
        if (local != null) {
            Log.d(TAG, "fetch() resolved over WIFI");
            return local;
        }
        Log.d(TAG, "local UDP produced nothing - falling back to MQTT cloud");
        Result cloud = _fetchMqttCloud(ctx);
        Log.d(TAG, cloud != null ? "fetch() resolved over CLOUD" : "fetch() FAILED - neither transport answered");
        return cloud;
    }

    // --------------------------------------------------------
    // Local UDP
    // --------------------------------------------------------
    private static Result _fetchLocalUdp() {
        // Bound to ARDUINO_PORT, not an ephemeral port - the firmware doesn't
        // do symmetric UDP (reply to whatever port asked). It replies to a
        // FIXED port (APP_UDP_PORT, same constant as ARDUINO_PORT) because it
        // also uses that same channel for unsolicited pushes with no request
        // to reply to. DataReceive.init() already binds the live app's own
        // socket there for exactly this reason; this fetcher needs to match
        // it or the board's reply lands on a port nothing is listening on -
        // which is why this always silently timed out and fell through to
        // MQTT cloud instead, even with the board on the same LAN.
        //
        // setReuseAddress lets this short-lived socket coexist with the live
        // app's own long-lived one when both happen to be bound at once - but
        // "coexist" only means bind() doesn't throw. With plain SO_REUSEADDR
        // (no SO_REUSEPORT) a later bind to the same port can silently steal
        // subsequent datagrams from the earlier-bound socket instead of both
        // receiving them, so if the app is actively awaiting a reply on this
        // exact port when this runs, ITS reply can land here instead and the
        // app sees a false "NO ACK". DataReceive.isReceivingActively() is the
        // narrow guard for that specific collision window (foregrounded app,
        // mid-command) - skip straight to MQTT cloud instead of racing it.
        if (DataReceive.isReceivingActively()) {
            Log.v(TAG, "skipping local UDP - live app is actively receiving on this port");
            return null;
        }
        try (DatagramSocket socket = new DatagramSocket(null)) {
            socket.setReuseAddress(true);
            socket.bind(new java.net.InetSocketAddress(DataSend.ARDUINO_PORT));
            socket.setSoTimeout(UDP_TIMEOUT_MS);
            byte[] hello = Frame.build(ProtocolOpcodes.Opcode.HELLO, 0, 0, new byte[]{ 1 });
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
                byte[] data = java.util.Arrays.copyOf(packet.getData(), packet.getLength());
                Frame.Parsed f = Frame.parse(data, data.length);
                if (!f.ok) { Log.v(TAG, "UDP: malformed frame, [" + data.length + "] bytes"); continue; }
                Log.v(TAG, "UDP: opcode [" + f.opcode + "] [" + data.length + "] bytes");
                if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_CLIMATE & 0xFF)) {
                    ProtocolOpcodes.TelemClimatePayload c = ProtocolOpcodes.TelemClimatePayload.unpack(f.payload, 0);
                    temp = c.temp_c; hum = c.humidity_pct;
                } else if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_DIFFUSER_USAGE & 0xFF)) {
                    Integer pct = _diffuserPercent(f.payload);
                    if (pct != null) diffuserPct = pct;
                } else if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_STATUS & 0xFF)) {
                    ProtocolOpcodes.TelemStatusPayload s = ProtocolOpcodes.TelemStatusPayload.unpack(f.payload, 0);
                    noWater = s.diffuser_summary == DIFFUSER_ORDINAL_NO_WATER;
                }
            }
            if (temp == null && diffuserPct == null && noWater == null) {
                Log.d(TAG, "UDP: timed out with nothing usable - board not reachable on the LAN");
                return null;
            }
            Log.d(TAG, "UDP: got temp=[" + temp + "] hum=[" + hum + "] diffuserPct=[" + diffuserPct + "] noWater=[" + noWater + "]");
            return new Result(temp, hum, diffuserPct, noWater, "WIFI");
        } catch (IOException e) {
            Log.d(TAG, "UDP: IOException - " + e.getMessage());
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
        if (user == null || user.isEmpty() || pass == null || pass.isEmpty()) {
            Log.d(TAG, "MQTT: no cloud credentials provisioned - skipping");
            return null;   // see MqttTransport class doc
        }
        String deviceId = prefs.getString(MqttTransport.KEY_DEVICE_ID, MqttTransport.DEFAULT_DEVICE_ID);
        String topicC2D = MqttTransport.TOPIC_BASE + deviceId + MqttTransport.TOPIC_C2D_SUFFIX;
        String topicD2C = MqttTransport.TOPIC_BASE + deviceId + MqttTransport.TOPIC_D2C_SUFFIX;

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
            // answers HELLO with separate TELEM_CLIMATE/TELEM_DIFFUSER_USAGE/
            // TELEM_STATUS frames that don't all land at once; counting down
            // on the FIRST frame of ANY kind (the old behaviour) meant a
            // status frame arriving first returned immediately, permanently
            // missing climate/diffuser data that was still in flight - this
            // is what caused the widget to show a real "Updated" timestamp
            // with blank temp/hum/diffuser values every time.
            final CountDownLatch gotEnough = new CountDownLatch(1);

            client.setCallback(new MqttCallback() {
                @Override public void connectionLost(Throwable cause) { }

                @Override public void messageArrived(String topic, MqttMessage message) {
                    byte[] raw = message.getPayload();
                    if (raw.length == 0) return;
                    Frame.Parsed f = Frame.parse(raw, raw.length);
                    if (!f.ok) return;
                    if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_CLIMATE & 0xFF)) {
                        ProtocolOpcodes.TelemClimatePayload c = ProtocolOpcodes.TelemClimatePayload.unpack(f.payload, 0);
                        tempHolder[0] = c.temp_c; humHolder[0] = c.humidity_pct;
                    } else if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_DIFFUSER_USAGE & 0xFF)) {
                        Integer pct = _diffuserPercent(f.payload);
                        if (pct != null) pctHolder[0] = pct;
                    } else if (f.opcode == (ProtocolOpcodes.Opcode.TELEM_STATUS & 0xFF)) {
                        ProtocolOpcodes.TelemStatusPayload s = ProtocolOpcodes.TelemStatusPayload.unpack(f.payload, 0);
                        noWaterHolder[0] = s.diffuser_summary == DIFFUSER_ORDINAL_NO_WATER;
                    }
                    Log.v(TAG, "MQTT: rx opcode [" + f.opcode + "] on [" + topic + "] [" + raw.length + "] bytes");
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
            client.subscribe(topicD2C);

            byte[] hello = Frame.build(ProtocolOpcodes.Opcode.HELLO, 0, 0, new byte[]{ 1 });
            client.publish(topicC2D, hello, 0, false);

            // Waits out the full timeout unless both arrive sooner - same
            // trade-off _fetchLocalUdp() makes, so a diffuser that's still
            // "learning" (see _diffuserPercent's guard) doesn't cut this
            // short either; whatever DID arrive by then is still used.
            gotEnough.await(MQTT_TIMEOUT_MS, TimeUnit.MILLISECONDS);

            if (tempHolder[0] == null && pctHolder[0] == null && noWaterHolder[0] == null) {
                Log.d(TAG, "MQTT: timed out with nothing usable");
                return null;
            }
            Log.d(TAG, "MQTT: got temp=[" + tempHolder[0] + "] hum=[" + humHolder[0] + "] diffuserPct=[" + pctHolder[0] + "] noWater=[" + noWaterHolder[0] + "]");
            return new Result(tempHolder[0], humHolder[0], pctHolder[0], noWaterHolder[0], "CLOUD");
        } catch (Exception e) {
            Log.d(TAG, "MQTT: " + e.getClass().getSimpleName() + ": " + e.getMessage());
            return null;
        } finally {
            if (client != null) {
                try { client.disconnect(); client.close(); } catch (Exception ignored) { }
            }
        }
    }

    // --------------------------------------------------------
    // Shared parsing
    // --------------------------------------------------------

    /**
     * TELEM_DIFFUSER_USAGE payload -> refill percent, or null if not enough
     * history yet to compute one (same "still learning" guard as
     * StatusManager.applyDiffuserUsage()).
     */
    private static Integer _diffuserPercent(byte[] payload) {
        if (payload.length < 7) return null;
        ProtocolOpcodes.TelemDiffuserUsagePayload u = ProtocolOpcodes.TelemDiffuserUsagePayload.unpack(payload, 0);
        if (u.refill_count == 0 || u.avg_min <= 0) return null;
        return Math.max(0, Math.min(100, 100 - (u.accum_min * 100 / u.avg_min)));
    }
}
