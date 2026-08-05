package com.fuzz.colors;

/*
 * ============================================================
 *  DataSend.java  (alias: DATAs)
 * ============================================================
 *  Responsibility:
 *      Shared outgoing command layer for the Arduino controller - builds
 *      every protocol string and hands it to the private _send() worker,
 *      which runs on a background thread so the UI is never blocked.
 *      NOT UDP-only: _transmit() picks whichever transport is actually
 *      live, local UDP directly or MQTT (see MqttTransport/MQTT) over the
 *      HiveMQ cloud link when off-WiFi. sendWelcomeUdpDirect() /
 *      _transmitUdpDirect() are the one deliberate exception - they always
 *      go out over the raw local socket, bypassing the transport pick,
 *      because that's what probes whether local UDP is reachable at all.
 *
 *  How to use:
 *      1.  Instantiate once inside Main (MainActivity):
 *              DATAs = new DataSend(this);
 *
 *      2.  Pass the shared DatagramSocket AFTER it is created
 *          by DATAr (DataReceive):
 *              DATAs.setSocket(DATAr.getSocket());
 *
 *      3.  Call any public method from Main, LED, SET, or STS:
 *              DATAs.sendColor(r, g, b);
 *              DATAs.sendBrightness(progress);
 *
 *  Protocol strings (hex values, uppercase unless noted):
 *      LBvv            – Set LED brightness (vv = hex byte)
 *      LCrrggbb        – Set LED colour  R G B (hex bytes)
 *      LC              – Request current colour from Arduino
 *      LDrrggbbRRggBB  – Set dual colour Left+Right (hex)
 *      LD              – Request current dual colour
 *      Ldrrggbb...     – Shake dual colour (same format as LD)
 *      LOvv            – Select LED index (hex byte)  [repeated per LED as 0/1]
 *      SiiVV           – Set setting id ii to value VV (hex)
 *      S               – Request all settings
 *      Kii             – Send debug item index (hex)
 *      A0 / A1         – Ambient mode OFF / ON
 *      X               – Toggle LED enable/disable
 *      Z               – Welcome / handshake packet
 *      @ii             – TestMode index (hex)
 *      @Dvv            – TestMode diffuser action (00 off · 01-04 mode · FF random color)
 *      DpMMMME         – Parfum mode (MMMM = 4-hex minutes 0001-0168 on · 0000 off,
 *                        E = 1-hex dispense mode: 1=continuous · 2=10 sec pulses
 *                        - same values as the MODE TV/MOTION/UDPRAW/AMBIENT/IDLE
 *                        settings enum, see PARFUM_MODE_VALUES)
 *      @Lvv            – TestMode forced lux level (01-04)
 *      $<b64u>,<b64p>  – Provision/verify MQTT broker credentials (both
 *                        fields base64, standard alphabet, NO_WRAP; see
 *                        sendMqttCredentials())
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      DATAs  = DataSend      (this class)
 *      DATAr  = DataReceive
 *      LED   = LEDManager
 *      SET   = SettingsManager
 *      STS   = StatusManager
 * ============================================================
 */

import android.os.Handler;
import android.os.Looper;
import android.util.Base64;
import android.util.Log;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;

public class DataSend {

    // --------------------------------------------------------
    // Constants – must match the Arduino firmware
    // --------------------------------------------------------
    /**
     * SINGLE SOURCE OF TRUTH for the board's address. Change the Arduino IP/port
     * HERE ONLY — DataReceive (socket bind + status display) references these,
     * so there is no second copy to keep in sync.
     */
    /** Target Arduino IP address */
    public static final String ARDUINO_IP   = "192.168.1.202";

    /** Target Arduino UDP port (also the local receive-bind port) */
    public static final int    ARDUINO_PORT = 8472;

    /**
     * Parfum-mode dispense patterns, parallel to PARFUM_MODE_VALUES.
     * "off" and the "after sleep" entries from the MODE TV/MOTION/UDPRAW/
     * AMBIENT/IDLE settings enum don't apply to a manual timed Parfum run,
     * so only the two live choices are exposed here.
     */
    public static final String[] PARFUM_MODES = { "CONTINUOUS", "10 SEC" };

    /**
     * Wire value per PARFUM_MODES entry - matches the shared dispense-mode
     * enum (0=off · 1=continuous · 2=10 sec · 3=2h after sleep · 4=4h after
     * sleep) so the Arduino can reuse its existing MODE TV/MOTION handling.
     */
    public static final int[] PARFUM_MODE_VALUES = { 1, 2 };

    // --------------------------------------------------------
    // ACK reliability layer (R4.10)
    // Every command goes out wrapped as "#SS<cmd>" (SS = 2-hex seq);
    // the controller answers "#SSR" (R = 1-hex result). Un-ACKed
    // commands are retransmitted, then reported as NO ACK.
    // Set ACK_ENABLED = false to restore legacy fire-and-forget.
    // Result codes: 0 applied · 1 clamped · 2 rejected · 3 blocked ·
    //               4 locked (parfum) · 5 no water/no resp · 6 unsupported ·
    //               7 mqtt auth failed ($ credential verify only).
    // Commands whose Arduino-side handler can't ACK immediately
    // ("@D", "Dp"/"Dn"/"Df", "@L") get a wider per-try window -
    // see EXT_ACK_TIMEOUT_MS below.
    // --------------------------------------------------------
    /** Envelope marker (must match firmware APP_ACK_CHAR). */
    public static final char    ACK_CHAR        = '#';
    /** Master switch – false = legacy fire-and-forget. */
    public static final boolean ACK_ENABLED     = true;
    /** Milliseconds to wait for an ACK before retransmitting. */
    public static final int     ACK_TIMEOUT_MS  = 350;
    /** Extra sends after the first before a command is declared un-ACKed. */
    public static final int     ACK_MAX_RETRIES = 2;

    /**
     * Per-try ACK wait for commands the controller can't answer inside the
     * generic budget:
     *   - "@D" test action, "Dp"/"Dn"/"Df" diffuser control - relayed
     *     through the diffuser unit. The controller doesn't ACK these
     *     itself, it waits for the diffuser's own reply and forwards that
     *     (see DIF_AckParse() in the firmware); if the diffuser stays
     *     silent, DIF_StatusCheck() only flushes a fallback ack every
     *     ~5 s, far longer than ACK_TIMEOUT_MS allows for.
     *   - "@L" forced test lux - handled locally, but shares the same
     *     light-sensor read/recalculate path as live adaptation, which
     *     can briefly block the controller's loop past the generic budget.
     * Same retry count as everything else, just a wider per-try window so
     * a merely-slow reply doesn't misreport NO ACK.
     */
    private static final int    EXT_ACK_TIMEOUT_MS = 3000;

    /**
     * Per-try ACK wait for "$" (MQTT credential provisioning) alone - wider
     * than EXT_ACK_TIMEOUT_MS because the firmware handler doesn't just relay
     * to another device, it performs a REAL blocking MQTT connect() against
     * the broker to verify the candidate pair before ACKing at all (see
     * MQTTCRED::cmdSetCredentials()): up to MQTT_Net.setConnectionTimeout(4000)
     * for the TLS handshake plus MQTT_Cli.setSocketTimeout(4) (4s) for the
     * CONNACK wait, worst case ~8s, plus normal round-trip margin.
     */
    private static final int    MQTTCRED_ACK_TIMEOUT_MS = 9000;

    /**
     * True for wire messages needing the wider EXT_ACK_TIMEOUT_MS budget
     * instead of the generic one - diffuser-relayed commands (mirrors the
     * firmware's 'D' subcommand dispatch: s=status is synchronous, f/n/p
     * relay) plus the '@D' test action and the '@L' forced-lux test mode.
     * See EXT_ACK_TIMEOUT_MS above.
     *
     * @param message  Unenveloped protocol string about to be sent.
     */
    private static boolean _needsExtendedAckWindow(String message) {
        if (message.startsWith("@D") || message.startsWith("@L")) return true;
        if (message.length() >= 2 && message.charAt(0) == 'D') {
            char sub = message.charAt(1);
            return sub == 'f' || sub == 'n' || sub == 'p';
        }
        return false;
    }

    /**
     * Per-try ACK timeout for a wire message - the widest applicable budget:
     * MQTTCRED_ACK_TIMEOUT_MS for "$" credential provisioning, the extended
     * budget for other slow-to-answer commands, the generic one otherwise.
     *
     * @param message  Unenveloped protocol string about to be sent.
     */
    private static int _ackTimeoutFor(String message) {
        if (message.startsWith("$")) return MQTTCRED_ACK_TIMEOUT_MS;
        return _needsExtendedAckWindow(message) ? EXT_ACK_TIMEOUT_MS : ACK_TIMEOUT_MS;
    }

    /**
     * Callback for a command whose ACK result needs to drive follow-up UI
     * logic beyond the generic console log (e.g. reopening the credentials
     * popup on APP_ACK_UNAUTHORIZED). Optional - most sendX() methods don't
     * pass one, and _reportResult()'s generic console line always still runs.
     */
    public interface AckCallback {
        void onResult(int result);
    }

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /**
     * Back-reference to Main so we can call:
     *   Main._LoadingBar()       – show spinner after every send
     *   Main._LoadingBarResult() – stop spinner tinted per ACK result
     *   Main._Console()          – log messages to the debug view
     *   Main._UDP_Available()    – guard against sending without socket
     */
    private final MainActivity Main;

    /**
     * Shared DatagramSocket.
     * Created by DATAr (DataReceive) and injected via setSocket().
     * DATAs never owns or closes this socket.
     */
    private DatagramSocket socket;

    // --------------------------------------------------------
    // ACK bookkeeping (all touched on the main looper only)
    // --------------------------------------------------------
    /** Rolling command sequence id, 0-255. Started at 1 to avoid "L0000" confusion. */
    private int seq = 1;

    /** In-flight commands awaiting an ACK, keyed by sequence id. */
    private final HashMap<Integer, Pending> pending = new HashMap<>();

    /** Main-thread handler for timeouts, retries and pending-map access. */
    private final Handler ack = new Handler(Looper.getMainLooper());

    /** One in-flight command awaiting its ACK. */
    private static final class Pending {
        final int seq;          // sequence id echoed by the controller
        final String packet;    // full wire packet (#SS + command) for retransmit
        final String label;     // human label for console/log (the command string)
        final int timeoutMs;    // per-try wait before retransmit/give-up, see _ackTimeoutFor()
        final AckCallback callback; // optional result hook beyond the generic console log, or null
        int triesLeft;          // remaining retransmits
        Runnable timeout;       // scheduled timeout runnable

        Pending(int seq, String packet, String label, int timeoutMs, int triesLeft, AckCallback callback) {
            this.seq = seq;
            this.packet = packet;
            this.label = label;
            this.timeoutMs = timeoutMs;
            this.triesLeft = triesLeft;
            this.callback = callback;
        }
    }

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  The running MainActivity instance (alias: Main).
     *              Must not be null.
     */
    public DataSend(MainActivity main) {
        this.Main = main;
    }

    // --------------------------------------------------------
    // Socket management
    // --------------------------------------------------------
    /**
     * Inject the shared DatagramSocket created by DATAr.
     * Call this after DATAr has successfully opened the socket.
     *
     * @param socket  Open, non-null DatagramSocket.
     *
     * Called by: MainActivity (onCreate init, and again on every local
     * reconnect in _connectTransport()).
     */
    public void setSocket(DatagramSocket socket) {
        Log.v("DATA_S", "Socket injected");
        this.socket = socket;
    }

    // --------------------------------------------------------
    // Private core sender
    // --------------------------------------------------------
    /**
     * Background-thread UDP send worker.
     * Spawns a new Thread for every packet so the UI thread
     * is never blocked.  Shows the loading spinner on success.
     *
     * @param message  Protocol string to send (ASCII, UTF-8).
     *                 Example: "LC" or "LBff"
     *
     * Called by: internal only - every ACK'd public sendX() method below
     * (sendColor, sendBrightness, sendDualColor, sendSelectLed, sendSetting,
     * sendAmbientMode, sendEnableDisable, sendTestMode, sendTestDiffuser,
     * sendParfum/sendParfumOff, sendTestLux).
     */
    private void _send(final String message) {
        _send(message, null);
    }

    /**
     * Same as _send(String), but with an optional callback invoked with the
     * ACK result code once resolved - for commands whose caller needs to
     * react to a specific result beyond the generic console log (e.g.
     * reopening the credentials popup on APP_ACK_UNAUTHORIZED).
     *
     * @param message   Protocol string to send.
     * @param callback  Result hook, or null for the common fire-and-log case.
     *
     * Called by: internal only - _send(String) (callback=null) and
     * sendMqttCredentials() (callback set).
     */
    private void _send(final String message, final AckCallback callback) {
        Log.v("DATA_S", "Sending: " + message);

        // Guard – do nothing if socket/wifi not available
        if (!Main._TransportAvailable()) {
            Log.i("DATA_ERR", "NOT AVAILABLE");
            Log.v("DATA_S", "Transport not available");
            Main._Console(false, "--", "{#R}NOT AVAILABLE{##}");
            return;
        }

        // Legacy mode – no sequencing, no retry
        if (!ACK_ENABLED) {
            _transmit(message);
            Main.runOnUiThread(() -> Main._LoadingBar(true));
            return;
        }

        // Sequenced send: wrap "#SS<cmd>", register as pending, arm retransmit.
        // Bookkeeping is marshalled onto the main looper so the pending map
        // needs no locking.
        ack.post(() -> {
            final int s = seq = (seq + 1) & 0xFF;
            final String packet = ACK_CHAR + String.format("%02x", s) + message;
            final int timeoutMs = _ackTimeoutFor(message);

            Log.v("DATA_S", "Sequenced send - seq: " + s + " packet: " + packet + " timeout: " + timeoutMs);

            final Pending p = new Pending(s, packet, message, timeoutMs, ACK_MAX_RETRIES, callback);
            p.timeout = () -> _onTimeout(s);
            pending.put(s, p);

            _transmit(packet);
            ack.postDelayed(p.timeout, timeoutMs);
            Main._LoadingBar(true);
        });
    }

    /**
     * Fire-and-forget send for query/dump commands (K, S-get, LC-get, LD-get,
     * T, Z): no seq, no retransmit, no ACK. Their streamed reply is the
     * confirmation, so they never raise a false "NO ACK" even when the reply is
     * large and slow (e.g. the K debug dump).
     *
     * @param message  Protocol string, sent as-is (un-enveloped).
     *
     * Called by: internal only - sendDebug() (K dump), sendGetColor() (LC-get),
     * the "get dual color"/"get settings" branches of sendDualColor()/
     * sendSetting(), and sendWelcome()/sendWelcomeUdpDirect() (Z).
     */
    private void _sendBare(final String message) {
        Log.v("DATA_S", "Sending bare: " + message);
        if (!Main._TransportAvailable()) {
            Log.i("DATA_ERR", "NOT AVAILABLE");
            Log.v("DATA_S", "Transport not available for bare send");
            Main._Console(false, "--", "{#R}NOT AVAILABLE{##}");
            return;
        }
        _transmit(message);
    }

    /**
     * Fire one datagram on a background thread using the shared socket
     * (Java DatagramSocket tolerates a concurrent send while DATAr blocks
     * in receive() on the same socket) — no more per-packet ephemeral socket.
     *
     * @param packet  Exact bytes to put on the wire (already enveloped).
     *
     * Called by: internal only - _send() and _sendBare(). This is the one
     * place that picks the actual transport (local UDP vs MQTT).
     */
    private void _transmit(final String packet) {
        // Cloud path: no usable local UDP link -> publish over MQTT (HiveMQ).
        // Same enveloped bytes, so the ACK layer round-trips over the broker too.
        if (!Main._UDP_Available() && Main._MqttConnected()) {
            Log.v("DATA_S", "MQTT : Transmitting: " + packet);
            Main._MqttPublish(packet);
            return;
        }
        final DatagramSocket sock = socket;
        if (sock == null || sock.isClosed()) {
            Log.e("DATA_ERR", "UDP : NO SOCKET for [" + packet + "]");
            Log.v("DATA_S", "UDP : No socket available for: " + packet);
            return;
        }
        new Thread(() -> {
            try {
                InetAddress address = InetAddress.getByName(ARDUINO_IP);
                byte[] buffer = packet.getBytes();
                DatagramPacket dp = new DatagramPacket(
                        buffer, buffer.length, address, ARDUINO_PORT);
                sock.send(dp);
                Log.i("DATA_S", "UDP : SEND [" + packet + "] size [" + buffer.length + "]");
                Log.v("DATA_S", "UDP : Transmitted to " + ARDUINO_IP + ":" + ARDUINO_PORT);
            } catch (Exception e) {
                e.printStackTrace();
                Log.e("DATA_ERR", "UDP : Exception in _transmit: " + e.getMessage());
                Log.v("DATA_S", "UDP : Transmit exception: " + e.getMessage());
            }
        }).start();
    }

    /**
     * Timeout for a pending command: retransmit while retries remain,
     * otherwise drop it and report NO ACK. Runs on the main looper.
     *
     * @param s  Sequence id whose timeout elapsed.
     *
     * Called by: internal only - scheduled via ack.postDelayed() from
     * _send() (first arm) and from itself on each retry.
     */
    private void _onTimeout(int s) {
        final Pending p = pending.get(s);
        if (p == null) return;                       // already ACKed

        if (p.triesLeft > 0) {
            p.triesLeft--;
            Log.i("DATA_S", "RETRY [" + p.label + "] left [" + p.triesLeft + "]");
            Log.v("DATA_S", "Retrying seq " + s + " tries left: " + p.triesLeft);
            _transmit(p.packet);
            ack.postDelayed(p.timeout, p.timeoutMs);
        } else {
            pending.remove(s);
            Main._LoadingBar(false);
            Main._Console(true, "◄◄", "{#R}NO ACK{##} [" + p.label + "]");
            Log.i("DATA_S", "NO ACK [" + p.label + "]");
            Log.v("DATA_S", "No ACK for seq " + s + " after all retries");
        }
    }

    /**
     * Resolve a pending command from an incoming "#SSR" ACK.
     * Called by DATAr; re-posted to the ack looper so all pending-map access
     * stays on one thread.
     *
     * @param s       Sequence id echoed by the controller.
     * @param result  Result code (0 applied · 1 clamped · 2 rejected ·
     *                3 blocked · 4 locked · 5 no water/no resp · 6 unsupported ·
     *                7 mqtt auth failed).
     *
     * Called by: DataReceive._recvAck() - fed from a "#SSR" packet, whether
     * it arrived over local UDP or (via DataReceive.onCloudMessage()) MQTT.
     */
    public void ackResolve(final int s, final int result) {
        Log.v("DATA_S", "ACK resolve - seq: " + s + " result: " + result);
        ack.post(() -> {
            final Pending p = pending.remove(s);
            if (p == null) return;                   // unknown / duplicate ACK
            Log.v("DATA_S", "ACK resolved for seq " + s);
            ack.removeCallbacks(p.timeout);
            Main._LoadingBarResult(_colorForResult(result));
            _reportResult(p.label, result);
            if (p.callback != null) p.callback.onResult(result);
        });
    }

    /**
     * Surface a non-trivial result to the console. Code 0 (applied) is silent
     * on screen – the spinner's own color is already the positive confirmation.
     *
     * @param label   Command label (the command string).
     * @param result  Result code from the controller.
     *
     * Called by: internal only - ackResolve().
     */
    private void _reportResult(String label, int result) {
        Log.v("DATA_S", "Reporting result for: " + label + " result: " + result);
        String tag;
        switch (result) {
            case 0:  Log.i("DATA_R", "ACK OK [" + label + "]"); return;
            case 1:  tag = "{#Y}CLAMPED{##}";            break;
            case 2:  tag = "{#R}REJECTED{##}";           break;
            case 3:  tag = "{#R}BLOCKED{##}";            break;
            case 4:  tag = "{#Y}LOCKED{##}";             break;
            case 5:  tag = "{#R}NO WATER / NO RESP{##}"; break;
            case 7:  tag = "{#R}MQTT AUTH FAILED{##}";   break;
            default: tag = "{#R}UNSUPPORTED{##}";        break;
        }
        Log.i("DATA_R", "ACK [" + result + "] [" + label + "]");
        Main._Console(true, "◄◄", tag + " [" + label + "]");
    }

    /**
     * Resolve the loading-bar tint for an ACK result, reusing the app's
     * existing status_* semantic palette so the color means the same
     * thing here as it does everywhere else in the UI.
     *
     * @param result  Result code (0 applied · 1 clamped · 2 rejected ·
     *                3 blocked · 4 locked · 5 no water/no resp · 6 unsupported ·
     *                7 mqtt auth failed).
     *
     * Called by: internal only - ackResolve().
     */
    private int _colorForResult(int result) {
        int colorRes;
        switch (result) {
            case 0:  colorRes = R.color.status_on_green;      break; // applied
            case 1:  colorRes = R.color.status_ready_yellow;  break; // clamped
            case 3:  colorRes = R.color.status_autooff_grey;  break; // blocked - no active source
            case 4:  colorRes = R.color.status_parfum_violet; break; // locked - parfum busy
            case 5:  colorRes = R.color.status_no_water_blue; break; // no water / no response
            default: colorRes = R.color.status_off_red;       break; // 2 rejected · 6 unsupported · 7 mqtt auth failed
        }
        return ThemeManager.getColor(Main, colorRes);
    }

    // ========================================================
    //  Public send methods
    //  Each method builds the protocol string, calls _send(),
    //  and logs the action to Logcat.
    // ========================================================

    // --------------------------------------------------------
    // Test / Debug
    // --------------------------------------------------------
    /**
     * Send a test-mode command.
     * Protocol: @ii  where ii = 2-digit hex of mode index.
     *
     * @param mode  Test mode index (0 = NONE, 1 = TV ON, …).
     *              See SET_TestMode[] in SET (SettingsManager).
     *
     * Called by: SettingsManager (TestMode picker callbacks), via the
     * TestModePopup UI.
     */
    public void sendTestMode(int mode) {
        Log.v("DATA_S", "Sending test mode: " + mode);
        _send("@" + String.format("%02x", mode));
        Log.i("DATA_S", "TEST MODE [" + mode + "]");
    }

    /**
     * Send a one-shot diffuser test action.
     * Protocol: @Dvv  where vv = 00 off · 01-04 mode · FF random color.
     *
     * @param val  Diffuser test value (hex byte).
     *
     * Called by: SettingsManager (TestMode picker, DIFFUSER slot).
     */
    public void sendTestDiffuser(int val) {
        Log.v("DATA_S", "Sending test diffuser: " + val);
        _send("@D" + String.format("%02x", val));
        Log.i("DATA_S", "TEST DIFFUSER [" + val + "]");
    }

    // --------------------------------------------------------
    // Diffuser parfum mode
    // --------------------------------------------------------
    /**
     * Start the diffuser's Parfum mode - a timed "insist" run during
     * which the diffuser ignores every stop/reconfigure command until
     * the window expires, dispensing in the chosen pattern.
     * Protocol: DpMMMME  where MMMM = 4-digit hex minutes (1-360),
     *                          E    = 1-digit hex dispense mode
     *                                 (PARFUM_MODE_VALUES[mode]).
     *
     * @param minutes  Duration in minutes, clamped to 1-360.
     * @param mode     Index into PARFUM_MODES, clamped to 0..PARFUM_MODES.length-1.
     *
     * Called by: StatusManager, via the ParfumPopup UI's "start" callback.
     */
    public void sendParfum(int minutes, int mode) {
        Log.v("DATA_S", "Sending parfum - minutes: " + minutes + " mode: " + mode);
        if (minutes < 1)   minutes = 1;
        if (minutes > 360) minutes = 360;
        mode = Math.max(0, Math.min(PARFUM_MODES.length - 1, mode));
        int wireVal = PARFUM_MODE_VALUES[mode];
        _send("Dp" + String.format("%04x", minutes) + Integer.toHexString(wireVal));
        Log.i("DATA_S", "PARFUM ON [" + minutes + " min] MODE [" + PARFUM_MODES[mode] + "]");
    }

    /**
     * Cancel the diffuser's Parfum mode and shut the diffuser down.
     * Protocol: Dp00000  (fixed-width - minutes + mode digit both zero)
     *
     * Called by: StatusManager, via the ParfumPopup UI's "stop" callback.
     */
    public void sendParfumOff() {
        Log.v("DATA_S", "Sending parfum off");
        _send("Dp00000");
        Log.i("DATA_S", "PARFUM OFF");
    }

    /**
     * Force a lux level on the Arduino (held for the test-mode duration,
     * then the light sensor takes over again). Uses the extended ACK
     * window (EXT_ACK_TIMEOUT_MS) - see _needsExtendedAckWindow().
     * Protocol: @Lvv  where vv = lux level 01-04.
     *
     * @param level  Lux level (1..4).
     *
     * Called by: SettingsManager (TestMode picker, lux slot).
     */
    public void sendTestLux(int level) {
        Log.v("DATA_S", "Sending test lux: " + level);
        _send("@L" + String.format("%02x", level));
        Log.i("DATA_S", "TEST LUX [" + level + "]");
    }

    /**
     * Send a debug-item request.
     * Protocol: Kii  where ii = 2-digit hex of debug index.
     *
     * @param debug  Debug index (0 = LED_INFO, 1 = LED_SELECTED, …).
     *               See SET_Debug[] in SET (SettingsManager).
     *
     * Called by: MainActivity (debug dropdown selection).
     */
    public void sendDebug(int debug) {
        Log.v("DATA_S", "Sending debug: " + debug);
        _sendBare("K" + String.format("%02x", debug));
        Log.i("DATA_S", "[K" + String.format("%02x", debug)
                + "] # DEBUG [" + debug + "]");
    }

    // --------------------------------------------------------
    // Welcome / handshake
    // --------------------------------------------------------
    /**
     * Send the welcome / handshake packet so the Arduino knows
     * the app is alive.
     * Protocol: Z
     *
     * Called by: MainActivity (init handshake, and again from onResume() on
     * every resume/reconnect) and MqttTransport (re-announce over cloud
     * after a fresh broker connect, when local UDP isn't the active link).
     */
    // Method signature updated to accept the caller or context tag
    public void sendWelcome(String caller) {
        Log.v("DATA_S", "Sending welcome from: " + caller);
        _sendBare("Z"); // Sends the base payload as before
        Log.i("DATA_S", "[Z] # WELCOME (" + caller + ")"); // Logcat output with dynamic caller context
    }

    /**
     * Send the welcome straight over the local UDP socket, bypassing the
     * cloud router in _transmit(). Used by the transport supervisor to PROBE
     * whether the board is reachable on the LAN while the app is on cloud: if
     * it answers, the reply lands on the UDP socket and promotes the link back
     * to local. Harmless no-op if the board isn't on the LAN. Never routed to
     * MQTT, so it can never be mistaken for a cloud welcome.
     *
     * Called by: MainActivity - _connectTransport() (first probe on WiFi)
     * and _superviseTransport() (repeated LAN probe while parked on cloud).
     */
    public void sendWelcomeUdpDirect() {
        Log.v("DATA_S", "UDP : Sending welcome direct probe");
        _transmitUdpDirect("Z");
        Log.i("DATA_S", "UDP : [Z] # WELCOME (udp-probe)");
    }

    /**
     * Fire one datagram over the shared socket unconditionally (no cloud
     * routing, no availability guard). Backs sendWelcomeUdpDirect().
     *
     * @param packet  Exact bytes to put on the wire.
     *
     * Called by: internal only - sendWelcomeUdpDirect().
     */
    private void _transmitUdpDirect(final String packet) {
        Log.v("DATA_S", "UDP : Transmitting direct: " + packet);
        final DatagramSocket sock = socket;
        if (sock == null || sock.isClosed()) {
            Log.v("DATA_S", "UDP : Socket not available for direct send");
            return;
        }
        new Thread(() -> {
            try {
                InetAddress address = InetAddress.getByName(ARDUINO_IP);
                byte[] buffer = packet.getBytes();
                sock.send(new DatagramPacket(buffer, buffer.length, address, ARDUINO_PORT));
                Log.v("DATA_S", "UDP : direct sent to " + ARDUINO_IP + ":" + ARDUINO_PORT);
            } catch (Exception e) {
                Log.e("DATA_ERR", "UDP : Exception in _transmitUdpDirect: " + e.getMessage());
                Log.v("DATA_S", "UDP : direct exception: " + e.getMessage());
            }
        }).start();
    }

    /**
     * Answer the board's keep-alive ping. A bare, un-enveloped "k" (the board
     * treats a 1-char 'k' as liveness only – no ACK, no status). Cloud-aware via
     * _transmit, so it round-trips over MQTT when the local UDP link is down.
     *
     * Called by: DataReceive._recvKeepAlive(), answering the board's 'k'
     * ping (whichever transport it arrived on).
     */
    public void sendKeepAlive() {
        Log.v("DATA_S", "Sending keep-alive");
        _sendBare("k");
    }

    /**
     * Ask the diffuser for its current status ("Ds"). Sent when the Parfum popup
     * opens so the board answers with the live parfum minutes ('p') if a window
     * is running.
     *
     * Called by: StatusManager, when the Parfum popup opens.
     */
    public void sendDiffuserStatus() {
        Log.v("DATA_S", "Sending diffuser status request");
        _sendBare("Ds");
        Log.i("DATA_S", "[Ds] # DIFFUSER STATUS REQUEST");
    }

    /**
     * Ask for the diffuser's full refill-cycle history ("Dh") - on demand
     * only, unlike sendDiffuserStatus()'s Ds (which rides the periodic
     * poll). SmartTV relays the diffuser's reply straight through without
     * caching it, so call this whenever the history is actually needed
     * (e.g. opening a history view) rather than polling it continuously.
     *
     * Called by: MainActivity, when the diffuser history view is opened.
     */
    public void sendDiffuserHistory() {
        Log.v("DATA_S", "Sending diffuser history request");
        _sendBare("Dh");
        Log.i("DATA_S", "[Dh] # DIFFUSER HISTORY REQUEST");
    }

    /**
     * Tell the diffuser a manual refill just happened ("Dr") - e.g. the user
     * topped it off before it ran dry. The diffuser banks the current usage
     * cycle into its refill history, resets the usage accumulator to 0, and
     * clears any out-of-water alert, then replies with a normal status ("Ds")
     * reflecting the fresh state - handled like any other status push.
     *
     * Called by: DiffuserUsagePopup, on the manual-refill button.
     */
    public void sendDiffuserManualRefill() {
        Log.v("DATA_S", "Sending diffuser manual refill");
        _send("Dr");
        Log.i("DATA_S", "[Dr] # DIFFUSER MANUAL REFILL");
    }

    // --------------------------------------------------------
    // LED colour & selection
    // --------------------------------------------------------
    /**
     * Request the current LED colours from the Arduino.
     * Protocol: LC  (no parameters – Arduino replies with LCiiRRggBB…)
     *
     * Called by: MainActivity._ResyncDeviceState() (after a reconnect) and
     * page-navigation refreshes.
     */
    public void sendGetColor() {
        Log.v("DATA_S", "Sending get color request");
        _sendBare("LC");
        Log.i("DATA_S", "[LC] # GET COLOR");
    }

    /**
     * Set a solid colour for currently selected LEDs.
     * Protocol: LCrrggbb
     *
     * @param r  Red   channel 0-255
     * @param g  Green channel 0-255
     * @param b  Blue  channel 0-255
     *
     * Called by: LEDManager (colour picker / swatch taps), typically paired
     * with a preceding sendSelectLed().
     */
    public void sendColor(int r, int g, int b) {
        Log.v("DATA_S", "Sending color - R:" + r + " G:" + g + " B:" + b);
        String hex = String.format("%02x%02x%02x", r, g, b);
        _send("LC" + hex);
        Log.i("DATA_S", "[LC" + hex + "] # COLOR [" + r + " " + g + " " + b + "]");
    }

    /**
     * Send or request dual-colour (left/right split).
     *
     * When ALL six parameters are -1 the Arduino is asked to
     * return the current dual colour (protocol: LD).
     * Otherwise a new dual colour is set (protocol: LDrrggbbRRggBB).
     *
     * @param r1  Left  Red   (-1 to request current value)
     * @param g1  Left  Green (-1 to request current value)
     * @param b1  Left  Blue  (-1 to request current value)
     * @param r2  Right Red   (-1 to request current value)
     * @param g2  Right Green (-1 to request current value)
     * @param b2  Right Blue  (-1 to request current value)
     *
     * Called by: LEDManager (dual-colour swatch row and the RgbChannelPopup
     * "set" callback), and MainActivity._ResyncDeviceState() (GET form,
     * after a reconnect).
     */
    public void sendDualColor(int r1, int g1, int b1, int r2, int g2, int b2) {
        if (r1 == -1 && g1 == -1 && b1 == -1 && r2 == -1 && g2 == -1 && b2 == -1) {
            // GET request
            Log.v("DATA_S", "Sending get dual color request");
            _sendBare("LD");
            Log.i("DATA_S", "[LD] # GET DUAL COLOR");
        } else {
            // SET command
            Log.v("DATA_S", "Sending dual color - L:" + r1 + "," + g1 + "," + b1 + " R:" + r2 + "," + g2 + "," + b2);
            String hex = String.format("%02x%02x%02x%02x%02x%02x", r1, g1, b1, r2, g2, b2);
            _send("LD" + hex);
            Log.i("DATA_S", "[LD" + hex + "] # DUAL COLOR ["
                    + r1 + " " + g1 + " " + b1 + "] <> ["
                    + r2 + " " + g2 + " " + b2 + "]");
        }
    }

    /**
     * Send a shake-triggered dual colour.
     * Protocol: Ldrrggbb RRggBB  (lowercase 'd')
     *
     * @param r1  Left  Red   0-255
     * @param g1  Left  Green 0-255
     * @param b1  Left  Blue  0-255
     * @param r2  Right Red   0-255
     * @param g2  Right Green 0-255
     * @param b2  Right Blue  0-255
     *
     * Called by: MainActivity's shake-sensor handler.
     */
    public void sendShakeDualColor(int r1, int g1, int b1, int r2, int g2, int b2) {
        Log.v("DATA_S", "Sending shake dual color - L:" + r1 + "," + g1 + "," + b1 + " R:" + r2 + "," + g2 + "," + b2);
        String hex = String.format("%02x%02x%02x%02x%02x%02x", r1, g1, b1, r2, g2, b2);
        _send("Ld" + hex);
        Log.i("DATA_S", "[Ld" + hex + "] # SHAKE DUAL COLOR ["
                + r1 + " " + g1 + " " + b1 + "] <> ["
                + r2 + " " + g2 + " " + b2 + "]");
    }

    /**
     * Set LED brightness for currently selected LEDs.
     * Protocol: LBvv  where vv = 2-digit hex brightness value.
     *
     * @param brightness  0-255 (capped by Arduino to LED_MAX_BAR_BRIGHTNESS)
     *
     * Called by: LEDManager (brightness slider).
     */
    public void sendBrightness(int brightness) {
        Log.v("DATA_S", "Sending brightness: " + brightness);
        _send("LB" + String.format("%02x", brightness));
        Log.i("DATA_S", "[LB" + String.format("%02x", brightness)
                + "] # BRIGHTNESS [" + brightness + "]");
    }

    /**
     * Send LED selection bitmask to the Arduino.
     * Protocol: LO  followed by one '1' or '0' per LED (LED_TOTAL chars).
     * Example:   LO1100…0  means LED 0 and LED 1 are selected.
     *
     * @param ledSelected   boolean[] from LED (LEDManager).
     *                      Length must equal LED_TOTAL.
     * @param ledTotal      Total number of LEDs (LED.LED_TOTAL).
     *
     * Called by: LEDManager (every selection change) and
     * MainActivity._ResyncDeviceState() (re-push after a reconnect, since
     * "selected" is app-local and the Arduino can't re-derive it).
     */
    public void sendSelectLed(boolean[] ledSelected, int ledTotal) {
        Log.v("DATA_S", "Sending LED selection for " + ledTotal + " LEDs");
        StringBuilder msg    = new StringBuilder("LO");
        StringBuilder logMsg = new StringBuilder("[ ");
        for (int i = 0; i < ledTotal; i++) {
            msg.append(ledSelected[i] ? "1" : "0");
            logMsg.append("(").append(i)
                  .append(ledSelected[i] ? "•" : "◯").append(") ");
        }
        logMsg.append("] # SELECT LED");
        Log.i("DATA_S", logMsg.toString());
        _send(msg.toString());
    }

    // --------------------------------------------------------
    // Settings
    // --------------------------------------------------------
    /**
     * Send a settings command to the Arduino.
     *
     *   setting == -1        → request all settings (protocol: S)
     *   setting == SET_TOTAL → send all default values (protocol: SiiVV…)
     *   otherwise            → send one setting (protocol: SiiVV)
     *
     * @param setting     Setting id, or -1 (get all), or SET_TOTAL (defaults).
     *                    This is the Arduino EEPROM id (SettingInfo.id()),
     *                    NOT a position in setInfo - the list can be in any
     *                    order, lookups below always match by id.
     * @param setInfo     List of SettingInfo objects from SET (SettingsManager).
     * @param setTotal    SET_TOTAL constant from SET (SettingsManager).
     *
     * Called by: SettingsManager (slider/switch changes and "reset to
     * defaults") and MainActivity._ResyncDeviceState() (GET form, after a
     * reconnect).
     */
    public void sendSetting(int setting,
                            java.util.List<SettingInfo> setInfo,
                            int setTotal) {
        Log.v("DATA_S", "Sending setting - id: " + setting);
        if (setting == -1) {
            // Request all settings
            Log.v("DATA_S", "Requesting all settings");
            Log.i("DATA_S", "[S] # GET SETTINGS INFO");
            _sendBare("S");

        } else if (setting == setTotal) {
            // Send all default values in one packet - each SettingInfo
            // contributes its own id, regardless of list order.
            Log.v("DATA_S", "Sending all default settings");
            StringBuilder msg    = new StringBuilder("S");
            StringBuilder logMsg = new StringBuilder("[");
            for (SettingInfo si : setInfo) {
                msg.append(String.format("%02x%02x", si.id(), si.defaultValue()));
                logMsg.append(String.format("(%d:%d)", si.id(), si.defaultValue()));
            }
            logMsg.append("] # SETTINGS");
            Log.i("DATA_S", logMsg.toString());
            _send(msg.toString());

        } else {
            // Send one setting - find it by id (not list position), since
            // not every setting is backed by a BubbleSeekBar anymore (some
            // are switches or selectable steppers) and id never equals
            // list position once settings get reordered on screen.
            SettingInfo target = null;
            for (SettingInfo si : setInfo) {
                if (si.id() == setting) { target = si; break; }
            }
            if (target == null) {
                Log.i("DATA_S", "[SETTING id " + setting + "] NOT FOUND - not sent");
                return;
            }
            int progress = target.currentValue();
            Log.v("DATA_S", "Setting " + setting + " value: " + progress);
            _send("S" + String.format("%02x%02x", setting, progress));
            Log.i("DATA_S", "[S" + String.format("%02x%02x", setting, progress)
                    + "] # SETTING [" + setting + " " + progress + "]");
        }
    }

    // --------------------------------------------------------
    // Ambient / Enable-Disable
    // --------------------------------------------------------
    /**
     * Enable or disable ambient mode on the Arduino.
     * Protocol: A1 (on) or A0 (off).
     *
     * @param mode  true = ON, false = OFF
     *
     * Called by: StatusManager (ambient icon tap).
     */
    public void sendAmbientMode(boolean mode) {
        Log.v("DATA_S", "Sending ambient mode: " + mode);
        _send("A" + (mode ? "1" : "0"));
        Log.i("DATA_S", "[A" + (mode ? "1" : "0")
                + "] # AMBIENT MODE [" + mode + "]");
    }

    /**
     * Toggle LED strip enable/disable.
     * Protocol: X  (Arduino toggles its internal state)
     *
     * @param stsEnableDisable  Current enable/disable status from STS.
     *                          Used only for logging – the Arduino toggles itself.
     *
     * Called by: SettingsManager (LED enable/disable switch).
     */
    public void sendEnableDisable(StatusManager.STS_EnableDisable stsEnableDisable) {
        Log.v("DATA_S", "Sending enable/disable: " + stsEnableDisable);
        _send("X");
        Log.i("DATA_S", "[X] # LED "
                + (stsEnableDisable == StatusManager.STS_EnableDisable.ON
                   ? "DISABLE" : "ENABLE"));
    }

    // --------------------------------------------------------
    // MQTT credential provisioning
    // --------------------------------------------------------
    /**
     * Provision (or force re-verify) the MQTT broker credentials cached on
     * the controller. Protocol: "$<b64user>,<b64pass>" - both fields
     * standard-alphabet base64 (Base64.NO_WRAP - no embedded newlines, no
     * URL-safe substitution). The controller does NOT trust this pair
     * blindly: it live-verifies it with a real connect() against the broker
     * before ACKing, and only persists it to EEPROM if that succeeds AND it
     * differs from what's already cached (see MQTTCRED::cmdSetCredentials()
     * in the firmware). A rejected pair (result 7 / APP_ACK_UNAUTHORIZED)
     * leaves any previously-good credentials untouched on the controller
     * side - the callback is the caller's cue to reprompt.
     *
     * Uses a wider-than-normal ACK window (MQTTCRED_ACK_TIMEOUT_MS) since the
     * firmware-side verify is itself a blocking network round trip, not just
     * a local apply.
     *
     * @param user      Plain-text MQTT username (never sent in the clear -
     *                  base64-encoded here; NOT encryption, just wire-safe
     *                  framing for the ',' delimiter).
     * @param pass      Plain-text MQTT password (same caveat as user).
     * @param callback  Invoked with the ACK result code once resolved (0 =
     *                  verified + adopted, 2 = malformed, 7 = broker
     *                  rejected the pair). May be null.
     *
     * Called by: MainActivity - the credential popup's "save" action, and
     * the 5-second long-press force-resend gesture on the Connect text
     * (resending the already-saved pair, no popup).
     */
    public void sendMqttCredentials(String user, String pass, AckCallback callback) {
        Log.v("DATA_S", "Sending MQTT credentials for user: " + user);
        String b64User = Base64.encodeToString(user.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
        String b64Pass = Base64.encodeToString(pass.getBytes(StandardCharsets.UTF_8), Base64.NO_WRAP);
        _send("$" + b64User + "," + b64Pass, callback);
        Log.i("DATA_S", "[$...] # MQTT CREDENTIALS [user=" + user + "]"); // never log the password
    }
}
