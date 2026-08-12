package com.fuzz.colors;

/*
 * ============================================================
 *  DataSend.java  (alias: DATAs)
 * ============================================================
 *  Responsibility:
 *      Shared outgoing command layer for the Arduino controller - builds
 *      every binary v1 protocol frame (01_PROTOCOL.md) and hands it to the
 *      private _send() worker, which runs on a background thread so the UI
 *      is never blocked. NOT UDP-only: _transmit() picks whichever
 *      transport is actually live, local UDP directly or MQTT (see
 *      MqttTransport/MQTT) over the HiveMQ cloud link when off-WiFi.
 *      sendWelcomeUdpDirect() / _transmitUdpDirect() are the one deliberate
 *      exception - they always go out over the raw local socket, bypassing
 *      the transport pick, because that's what probes whether local UDP is
 *      reachable at all.
 *
 *  PROTOCOL ADAPTATION (from the original ASCII "#SS<cmd>" scheme):
 *      Every public sendX() method here keeps its ORIGINAL name and
 *      signature - every caller (LEDManager, SettingsManager,
 *      StatusManager, MainActivity, popups) is unchanged. What changed is
 *      internal only: each method now builds a binary Frame (MAGIC/FLAGS/
 *      SEQ/OPCODE/payload/CRC8) via com.fuzz.colors.protocol.Frame and the
 *      GENERATED com.fuzz.colors.protocol.ProtocolOpcodes codec, instead of
 *      an ASCII command string. The sequence id that used to be hand-
 *      encoded into a "#SS" envelope is now just the frame's own SEQ byte -
 *      no separate envelope needed. ACK result codes are numerically
 *      IDENTICAL 0-7 to the old scheme (0 ok/1 clamped/2 rejected/3
 *      blocked/4 locked/5 no-water-or-unreachable/6 unsupported/7 mqtt-
 *      auth-failed-or-unauthorized) plus a new 8 (VERSION_MISMATCH) - see
 *      protocol_table.json's ack_results, deliberately kept ordinal-
 *      compatible when the binary protocol was designed.
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
import android.util.Log;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import com.fuzz.colors.protocol.Frame;
import com.fuzz.colors.protocol.ProtocolOpcodes;

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
    // ACK reliability layer
    // Every command goes out as a binary frame with ACK_REQ set and a real
    // SEQ byte; the controller answers an ACK frame (opcode=ACK, IS_ACK
    // flag set, same seq, payload[0]=result). Un-ACKed commands are
    // retransmitted, then reported as NO ACK. Set ACK_ENABLED = false to
    // restore legacy fire-and-forget.
    // Result codes: 0 ok · 1 clamped · 2 rejected · 3 blocked · 4 locked ·
    //               5 unreachable (no water/no resp) · 6 unsupported ·
    //               7 unauthorized (mqtt auth failed) · 8 version mismatch.
    // Diffuser-relayed opcodes and the test-diffuser/test-lux opcodes get a
    // wider per-try window - see EXT_ACK_TIMEOUT_MS below.
    // --------------------------------------------------------
    /** Master switch – false = legacy fire-and-forget. */
    public static final boolean ACK_ENABLED     = true;
    /** Milliseconds to wait for an ACK before retransmitting. */
    public static final int     ACK_TIMEOUT_MS  = 350;
    /** Extra sends after the first before a command is declared un-ACKed. */
    public static final int     ACK_MAX_RETRIES = 2;

    /**
     * Per-try ACK wait for opcodes the controller can't answer inside the
     * generic budget: every DIFFUSER_* opcode (relayed through the diffuser
     * unit - the controller doesn't ACK these itself, it waits for the
     * diffuser's own reply and forwards that) plus SET_TEST_DIFFUSER/
     * SET_TEST_LUX (handled locally but share a path that can briefly block
     * the controller's loop past the generic budget). Same retry count as
     * everything else, just a wider per-try window so a merely-slow reply
     * doesn't misreport NO ACK.
     */
    private static final int    EXT_ACK_TIMEOUT_MS = 3000;

    /**
     * Per-try ACK wait for SET_MQTT_CREDENTIALS alone - wider than
     * EXT_ACK_TIMEOUT_MS because the firmware handler doesn't just apply
     * locally, it performs a REAL blocking MQTT connect() against the
     * broker to verify the candidate pair before ACKing at all. Worst case
     * ~8s for the TLS handshake + CONNACK wait, plus round-trip margin.
     */
    private static final int    MQTTCRED_ACK_TIMEOUT_MS = 9000;

    /**
     * True for opcodes needing the wider EXT_ACK_TIMEOUT_MS budget instead
     * of the generic one. See EXT_ACK_TIMEOUT_MS above.
     */
    private static boolean _needsExtendedAckWindow(byte opcode) {
        if (opcode == ProtocolOpcodes.Opcode.SET_TEST_DIFFUSER || opcode == ProtocolOpcodes.Opcode.SET_TEST_LUX) return true;
        return opcode == ProtocolOpcodes.Opcode.DIFFUSER_STATUS_QUERY
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_HISTORY_QUERY
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_HISTORY_REMOVE
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_MANUAL_REFILL
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_SHUTDOWN
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_TURN_ON
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_PARFUM_START
            || opcode == ProtocolOpcodes.Opcode.DIFFUSER_PARFUM_CANCEL;
    }

    /** Per-try ACK timeout for an opcode - the widest applicable budget. */
    private static int _ackTimeoutFor(byte opcode) {
        if (opcode == ProtocolOpcodes.Opcode.SET_MQTT_CREDENTIALS) return MQTTCRED_ACK_TIMEOUT_MS;
        return _needsExtendedAckWindow(opcode) ? EXT_ACK_TIMEOUT_MS : ACK_TIMEOUT_MS;
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

    /**
     * Single persistent worker backing every outgoing send (see _transmit()/
     * _transmitUdpDirect()), replacing a dedicated "new Thread()" per packet.
     * A UDP sock.send() never blocks meaningfully (no round-trip, just handing
     * the datagram to the kernel), so serializing all sends through one
     * long-lived thread costs nothing in practice while removing the
     * create/run/teardown overhead of spinning up a fresh Thread for every
     * colour tap, brightness drag tick, and settings change - and, as a
     * side effect, makes send order deterministic instead of racing N
     * independently-started threads against each other. Same pattern as
     * BackgroundPopup.IO.
     */
    private final ExecutorService ioExecutor = Executors.newSingleThreadExecutor();

    // --------------------------------------------------------
    // ACK bookkeeping (all touched on the main looper only)
    // --------------------------------------------------------
    /** Rolling command sequence id, 0-255. Started at 1 to avoid seq-0 confusion. */
    private int seq = 1;

    /** In-flight commands awaiting an ACK, keyed by sequence id. */
    private final HashMap<Integer, Pending> pending = new HashMap<>();

    /** Main-thread handler for timeouts, retries and pending-map access. */
    private final Handler ack = new Handler(Looper.getMainLooper());

    /** One in-flight command awaiting its ACK. */
    private static final class Pending {
        final int seq;          // sequence id echoed by the controller
        final byte[] frame;     // full wire frame (for retransmit)
        final String label;     // human label for console/log (the opcode name)
        final int timeoutMs;    // per-try wait before retransmit/give-up, see _ackTimeoutFor()
        final AckCallback callback; // optional result hook beyond the generic console log, or null
        int triesLeft;          // remaining retransmits
        Runnable timeout;       // scheduled timeout runnable

        Pending(int seq, byte[] frame, String label, int timeoutMs, int triesLeft, AckCallback callback) {
            this.seq = seq;
            this.frame = frame;
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

    /**
     * Stop ioExecutor's worker thread. Without this, an Activity recreation
     * that doesn't go through process death (e.g. a config change not listed
     * in AndroidManifest's android:configChanges) would leak the old
     * DataSend's executor thread every time a new one is constructed.
     *
     * @note   Does not touch the shared socket - DATAr (DataReceive) owns
     *         and closes that.
     *
     * Called by: MainActivity.onDestroy().
     */
    public void shutdown() {
        ioExecutor.shutdown();
    }

    // --------------------------------------------------------
    // Private core sender
    // --------------------------------------------------------
    /**
     * Background-thread UDP send worker, via the shared ioExecutor so the
     * UI thread is never blocked. Shows the loading spinner on success.
     *
     * Called by: internal only - every ACK'd public sendX() method below.
     */
    private void _send(final byte opcode, final byte[] payload) {
        _send(opcode, payload, null);
    }

    /**
     * Same as _send(opcode, payload), but with an optional callback invoked
     * with the ACK result code once resolved - for commands whose caller
     * needs to react to a specific result beyond the generic console log
     * (e.g. reopening the credentials popup on APP_ACK_UNAUTHORIZED).
     *
     * Called by: internal only - _send(byte,byte[]) (callback=null) and
     * sendMqttCredentials() (callback set).
     */
    private void _send(final byte opcode, final byte[] payload, final AckCallback callback) {
        Log.v("DATA_S", "Sending: " + ProtocolOpcodes.opcodeName(opcode));

        // Guard – do nothing if socket/wifi not available
        if (!Main._TransportAvailable()) {
            Log.i("DATA_ERR", "NOT AVAILABLE");
            Log.v("DATA_S", "Transport not available");
            Main._Console(false, "--", "{#R}NOT AVAILABLE{##}");
            return;
        }

        // Legacy mode – no sequencing, no retry
        if (!ACK_ENABLED) {
            _transmit(Frame.build(opcode, 0, 0, payload));
            Main.runOnUiThread(() -> Main._LoadingBar(true));
            return;
        }

        // Sequenced send: build the frame with ACK_REQ + a real SEQ byte,
        // register as pending, arm retransmit. Bookkeeping is marshalled
        // onto the main looper so the pending map needs no locking.
        ack.post(() -> {
            final int s = seq = (seq + 1) & 0xFF;
            final byte[] frame = Frame.build(opcode, s, ProtocolOpcodes.Flag.ACK_REQ, payload);
            final int timeoutMs = _ackTimeoutFor(opcode);
            final String label = ProtocolOpcodes.opcodeName(opcode);

            Log.v("DATA_S", "Sequenced send - seq: " + s + " opcode: " + label + " timeout: " + timeoutMs);

            final Pending p = new Pending(s, frame, label, timeoutMs, ACK_MAX_RETRIES, callback);
            p.timeout = () -> _onTimeout(s);
            pending.put(s, p);

            _transmit(frame);
            ack.postDelayed(p.timeout, timeoutMs);
            Main._LoadingBar(true);
        });
    }

    /**
     * Fire-and-forget send for query/dump commands: no seq, no retransmit,
     * no ACK. Their streamed telemetry reply is the confirmation, so they
     * never raise a false "NO ACK" even when the reply is large/slow.
     *
     * Called by: internal only - sendGetColor() (LED_GET_COLOR), the "get"
     * branch of sendDualColor() (LED_GET_DUAL_COLOR), sendSetting()'s "get
     * all" branch (SETTINGS_READ_ALL), sendWelcome()/sendWelcomeUdpDirect()
     * (HELLO), sendKeepAlive() (KEEPALIVE), sendDiffuserStatus()/
     * sendDiffuserHistory().
     */
    private void _sendBare(final byte opcode, final byte[] payload) {
        Log.v("DATA_S", "Sending bare: " + ProtocolOpcodes.opcodeName(opcode));
        if (!Main._TransportAvailable()) {
            Log.i("DATA_ERR", "NOT AVAILABLE");
            Log.v("DATA_S", "Transport not available for bare send");
            Main._Console(false, "--", "{#R}NOT AVAILABLE{##}");
            return;
        }
        _transmit(Frame.build(opcode, 0, 0, payload));
    }

    /**
     * Fire one datagram on a background thread using the shared socket
     * (Java DatagramSocket tolerates a concurrent send while DATAr blocks
     * in receive() on the same socket) — no more per-packet ephemeral socket.
     *
     * @param frame  Exact bytes to put on the wire (already built).
     *
     * Called by: internal only - _send() and _sendBare(). This is the one
     * place that picks the actual transport (local UDP vs MQTT).
     */
    private void _transmit(final byte[] frame) {
        if (frame == null) {
            Log.e("DATA_ERR", "Frame.build() returned null (payload too large?)");
            return;
        }
        // Cloud path: no usable local UDP link -> publish over MQTT (HiveMQ).
        // Same frame bytes, so the ACK layer round-trips over the broker too.
        if (!Main._UDP_Available() && Main._MqttConnected()) {
            Log.v("DATA_S", "MQTT : Transmitting " + frame.length + " bytes");
            Main._MqttPublish(frame);
            return;
        }
        final DatagramSocket sock = socket;
        if (sock == null || sock.isClosed()) {
            Log.e("DATA_ERR", "UDP : NO SOCKET");
            Log.v("DATA_S", "UDP : No socket available");
            return;
        }
        ioExecutor.execute(() -> {
            try {
                InetAddress address = InetAddress.getByName(ARDUINO_IP);
                DatagramPacket dp = new DatagramPacket(frame, frame.length, address, ARDUINO_PORT);
                sock.send(dp);
                Log.i("DATA_S", "UDP : SEND size [" + frame.length + "]");
                Log.v("DATA_S", "UDP : Transmitted to " + ARDUINO_IP + ":" + ARDUINO_PORT);
            } catch (Exception e) {
                e.printStackTrace();
                Log.e("DATA_ERR", "UDP : Exception in _transmit: " + e.getMessage());
                Log.v("DATA_S", "UDP : Transmit exception: " + e.getMessage());
            }
        });
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
            _transmit(p.frame);
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
     * Resolve a pending command from an incoming ACK frame.
     * Called by DATAr; re-posted to the ack looper so all pending-map access
     * stays on one thread.
     *
     * @param s       Sequence id echoed by the controller.
     * @param result  Result code (0 ok · 1 clamped · 2 rejected · 3 blocked ·
     *                4 locked · 5 unreachable · 6 unsupported ·
     *                7 unauthorized · 8 version mismatch).
     *
     * Called by: DataReceive._recvAck() - fed from an ACK frame, whether it
     * arrived over local UDP or (via DataReceive.onCloudMessage()) MQTT.
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
     * Surface a non-trivial result to the console. Code 0 (ok) is silent
     * on screen – the spinner's own color is already the positive confirmation.
     *
     * @param label   Opcode name.
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
            case 5:  tag = "{#R}UNREACHABLE / NO WATER{##}"; break;
            case 7:  tag = "{#R}UNAUTHORIZED{##}";       break;
            case 8:  tag = "{#R}VERSION MISMATCH{##}";   break;
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
     * Called by: internal only - ackResolve().
     */
    private int _colorForResult(int result) {
        int colorRes;
        switch (result) {
            case 0:  colorRes = R.color.status_on_green;      break; // ok
            case 1:  colorRes = R.color.status_ready_yellow;  break; // clamped
            case 3:  colorRes = R.color.status_autooff_grey;  break; // blocked - no active source
            case 4:  colorRes = R.color.status_parfum_violet; break; // locked - parfum busy
            case 5:  colorRes = R.color.status_no_water_blue; break; // unreachable / no water
            default: colorRes = R.color.status_off_red;       break; // 2 rejected · 6 unsupported · 7 unauthorized · 8 version mismatch
        }
        return ThemeManager.getColor(Main, colorRes);
    }

    // ========================================================
    //  Public send methods
    //  Each method builds the binary opcode + payload, calls _send(),
    //  and logs the action to Logcat. Names/signatures unchanged from the
    //  original ASCII-era API - see this file's header comment.
    // ========================================================

    // --------------------------------------------------------
    // Test / Debug
    // --------------------------------------------------------
    /**
     * Send a test-mode command. Opcode: SET_TEST_MODE, payload=[mode].
     *
     * @param mode  Test mode index (0 = NONE, 1 = TV ON, …).
     *              See SET_TestMode[] in SET (SettingsManager).
     *
     * Called by: SettingsManager (TestMode picker callbacks), via the
     * TestModePopup UI.
     */
    public void sendTestMode(int mode) {
        Log.v("DATA_S", "Sending test mode: " + mode);
        _send(ProtocolOpcodes.Opcode.SET_TEST_MODE, new byte[]{ (byte) mode });
        Log.i("DATA_S", "TEST MODE [" + mode + "]");
    }

    /**
     * Send a one-shot diffuser test action. Opcode: SET_TEST_DIFFUSER,
     * payload=[val] where val = 00 off · 01-04 mode · FF random color.
     *
     * @param val  Diffuser test value (0-255).
     *
     * Called by: SettingsManager (TestMode picker, DIFFUSER slot).
     */
    public void sendTestDiffuser(int val) {
        Log.v("DATA_S", "Sending test diffuser: " + val);
        _send(ProtocolOpcodes.Opcode.SET_TEST_DIFFUSER, new byte[]{ (byte) val });
        Log.i("DATA_S", "TEST DIFFUSER [" + val + "]");
    }

    // --------------------------------------------------------
    // Diffuser parfum mode
    // --------------------------------------------------------
    /**
     * Start the diffuser's Parfum mode - a timed "insist" run during
     * which the diffuser ignores every stop/reconfigure command until
     * the window expires, dispensing in the chosen pattern.
     * Opcode: DIFFUSER_PARFUM_START, payload = minutes(u16) + mode(u8).
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
        ProtocolOpcodes.DiffuserParfumStartPayload p = new ProtocolOpcodes.DiffuserParfumStartPayload();
        p.minutes = minutes; p.mode = wireVal;
        _send(ProtocolOpcodes.Opcode.DIFFUSER_PARFUM_START, p.pack());
        Log.i("DATA_S", "PARFUM ON [" + minutes + " min] MODE [" + PARFUM_MODES[mode] + "]");
    }

    /**
     * Cancel the diffuser's Parfum mode and shut the diffuser down.
     * Opcode: DIFFUSER_PARFUM_CANCEL, no payload.
     *
     * Called by: StatusManager, via the ParfumPopup UI's "stop" callback.
     */
    public void sendParfumOff() {
        Log.v("DATA_S", "Sending parfum off");
        _send(ProtocolOpcodes.Opcode.DIFFUSER_PARFUM_CANCEL, null);
        Log.i("DATA_S", "PARFUM OFF");
    }

    /**
     * Force a lux level on the Arduino (held for the test-mode duration,
     * then the light sensor takes over again). Uses the extended ACK
     * window (EXT_ACK_TIMEOUT_MS) - see _needsExtendedAckWindow().
     * Opcode: SET_TEST_LUX, payload=[level].
     *
     * @param level  Lux level (1..4).
     *
     * Called by: SettingsManager (TestMode picker, lux slot).
     */
    public void sendTestLux(int level) {
        Log.v("DATA_S", "Sending test lux: " + level);
        _send(ProtocolOpcodes.Opcode.SET_TEST_LUX, new byte[]{ (byte) level });
        Log.i("DATA_S", "TEST LUX [" + level + "]");
    }

    /**
     * Request a health-summary diagnostic dump. Opcode: DIAG_HEALTH.
     * Replaces the old per-module ASCII debug dump ('K' + index) - the new
     * firmware only exposes this one aggregate health check over the app
     * protocol; per-module dumps are superseded by direct Telnet access
     * (see MainActivity's Telnet toggle, added alongside this adaptation).
     *
     * @param debug  Unused, kept for call-site compatibility (the old debug
     *               dropdown's selected index no longer has per-item wire
     *               opcodes to target).
     *
     * Called by: MainActivity (debug dropdown selection).
     */
    public void sendDebug(int debug) {
        Log.v("DATA_S", "Sending diag health (debug index " + debug + " - no longer per-module, see DIAG_HEALTH)");
        _sendBare(ProtocolOpcodes.Opcode.DIAG_HEALTH, null);
        Log.i("DATA_S", "[DIAG_HEALTH] # DEBUG");
    }

    // --------------------------------------------------------
    // Welcome / handshake
    // --------------------------------------------------------
    /**
     * Send the welcome / handshake packet so the Arduino knows
     * the app is alive. Opcode: HELLO, payload=[proto_version=1].
     *
     * Called by: MainActivity (init handshake, and again from onResume() on
     * every resume/reconnect) and MqttTransport (re-announce over cloud
     * after a fresh broker connect, when local UDP isn't the active link).
     */
    public void sendWelcome(String caller) {
        Log.v("DATA_S", "Sending welcome from: " + caller);
        _sendBare(ProtocolOpcodes.Opcode.HELLO, new byte[]{ 1 });
        Log.i("DATA_S", "[HELLO] # WELCOME (" + caller + ")");
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
        _transmitUdpDirect(Frame.build(ProtocolOpcodes.Opcode.HELLO, 0, 0, new byte[]{ 1 }));
        Log.i("DATA_S", "UDP : [HELLO] # WELCOME (udp-probe)");
    }

    /**
     * Fire one datagram over the shared socket unconditionally (no cloud
     * routing, no availability guard). Backs sendWelcomeUdpDirect().
     *
     * @param frame  Exact bytes to put on the wire.
     *
     * Called by: internal only - sendWelcomeUdpDirect().
     */
    private void _transmitUdpDirect(final byte[] frame) {
        Log.v("DATA_S", "UDP : Transmitting direct, " + (frame == null ? 0 : frame.length) + " bytes");
        if (frame == null) return;
        final DatagramSocket sock = socket;
        if (sock == null || sock.isClosed()) {
            Log.v("DATA_S", "UDP : Socket not available for direct send");
            return;
        }
        ioExecutor.execute(() -> {
            try {
                InetAddress address = InetAddress.getByName(ARDUINO_IP);
                sock.send(new DatagramPacket(frame, frame.length, address, ARDUINO_PORT));
                Log.v("DATA_S", "UDP : direct sent to " + ARDUINO_IP + ":" + ARDUINO_PORT);
            } catch (Exception e) {
                Log.e("DATA_ERR", "UDP : Exception in _transmitUdpDirect: " + e.getMessage());
                Log.v("DATA_S", "UDP : direct exception: " + e.getMessage());
            }
        });
    }

    /**
     * Answer the board's keep-alive ping. Opcode: KEEPALIVE, bare (no ack -
     * the board treats it as liveness only). Cloud-aware via _transmit, so
     * it round-trips over MQTT when the local UDP link is down.
     *
     * Called by: DataReceive._recvKeepAlive(), answering the board's
     * KEEPALIVE ping (whichever transport it arrived on).
     */
    public void sendKeepAlive() {
        Log.v("DATA_S", "Sending keep-alive");
        _sendBare(ProtocolOpcodes.Opcode.KEEPALIVE, null);
    }

    /**
     * Ask the diffuser for its current status. Opcode: DIFFUSER_STATUS_QUERY,
     * payload=[verbose=0]. Sent when the Parfum popup opens so the board
     * answers with the live parfum minutes (TELEM_PARFUM_REMAINING) if a
     * window is running.
     *
     * Called by: StatusManager, when the Parfum popup opens.
     */
    public void sendDiffuserStatus() {
        Log.v("DATA_S", "Sending diffuser status request");
        _sendBare(ProtocolOpcodes.Opcode.DIFFUSER_STATUS_QUERY, new byte[]{ 0 });
        Log.i("DATA_S", "[DIFFUSER_STATUS_QUERY] # DIFFUSER STATUS REQUEST");
    }

    /**
     * Ask for the diffuser's full refill-cycle history. Opcode:
     * DIFFUSER_HISTORY_QUERY - on demand only, unlike sendDiffuserStatus()
     * (which rides the periodic poll). SmartTV relays the diffuser's reply
     * straight through without caching it, so call this whenever the
     * history is actually needed (e.g. opening a history view) rather than
     * polling it continuously.
     *
     * Called by: MainActivity, when the diffuser history view is opened.
     */
    public void sendDiffuserHistory() {
        Log.v("DATA_S", "Sending diffuser history request");
        _sendBare(ProtocolOpcodes.Opcode.DIFFUSER_HISTORY_QUERY, null);
        Log.i("DATA_S", "[DIFFUSER_HISTORY_QUERY] # DIFFUSER HISTORY REQUEST");
    }

    /**
     * Tell the diffuser a manual refill just happened. Opcode:
     * DIFFUSER_MANUAL_REFILL - e.g. the user topped it off before it ran
     * dry. The diffuser banks the current usage cycle into its refill
     * history, resets the usage accumulator to 0, and clears any
     * out-of-water alert, then replies with a normal status push reflecting
     * the fresh state.
     *
     * Called by: DiffuserUsagePopup, on the manual-refill button.
     */
    public void sendDiffuserManualRefill() {
        Log.v("DATA_S", "Sending diffuser manual refill");
        _send(ProtocolOpcodes.Opcode.DIFFUSER_MANUAL_REFILL, null);
        Log.i("DATA_S", "[DIFFUSER_MANUAL_REFILL] # DIFFUSER MANUAL REFILL");
    }

    // --------------------------------------------------------
    // LED colour & selection
    // --------------------------------------------------------
    /**
     * Request the current LED colours from the Arduino.
     * Opcode: LED_GET_COLOR - Arduino replies with TELEM_COLOR_SYNC.
     *
     * Called by: MainActivity._ResyncDeviceState() (after a reconnect) and
     * page-navigation refreshes.
     */
    public void sendGetColor() {
        Log.v("DATA_S", "Sending get color request");
        _sendBare(ProtocolOpcodes.Opcode.LED_GET_COLOR, null);
        Log.i("DATA_S", "[LED_GET_COLOR] # GET COLOR");
    }

    /**
     * Set a solid colour for currently selected LEDs.
     * Opcode: LED_SET_COLOR, payload=[r,g,b].
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
        _send(ProtocolOpcodes.Opcode.LED_SET_COLOR, new byte[]{ (byte) r, (byte) g, (byte) b });
        Log.i("DATA_S", "[LED_SET_COLOR] # COLOR [" + r + " " + g + " " + b + "]");
    }

    /**
     * Send or request dual-colour (left/right split).
     *
     * When ALL six parameters are -1 the Arduino is asked to return the
     * current dual colour (opcode: LED_GET_DUAL_COLOR). Otherwise a new
     * dual colour is set (opcode: LED_SET_DUAL_COLOR, shake=0).
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
            Log.v("DATA_S", "Sending get dual color request");
            _sendBare(ProtocolOpcodes.Opcode.LED_GET_DUAL_COLOR, null);
            Log.i("DATA_S", "[LED_GET_DUAL_COLOR] # GET DUAL COLOR");
        } else {
            Log.v("DATA_S", "Sending dual color - L:" + r1 + "," + g1 + "," + b1 + " R:" + r2 + "," + g2 + "," + b2);
            ProtocolOpcodes.LedSetDualColorPayload p = new ProtocolOpcodes.LedSetDualColorPayload();
            p.shake = 0; p.r1 = r1; p.g1 = g1; p.b1 = b1; p.r2 = r2; p.g2 = g2; p.b2 = b2;
            _send(ProtocolOpcodes.Opcode.LED_SET_DUAL_COLOR, p.pack());
            Log.i("DATA_S", "[LED_SET_DUAL_COLOR] # DUAL COLOR ["
                    + r1 + " " + g1 + " " + b1 + "] <> ["
                    + r2 + " " + g2 + " " + b2 + "]");
        }
    }

    /**
     * Send a shake-triggered dual colour.
     * Opcode: LED_SET_DUAL_COLOR, shake=1.
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
        ProtocolOpcodes.LedSetDualColorPayload p = new ProtocolOpcodes.LedSetDualColorPayload();
        p.shake = 1; p.r1 = r1; p.g1 = g1; p.b1 = b1; p.r2 = r2; p.g2 = g2; p.b2 = b2;
        _send(ProtocolOpcodes.Opcode.LED_SET_DUAL_COLOR, p.pack());
        Log.i("DATA_S", "[LED_SET_DUAL_COLOR shake] # SHAKE DUAL COLOR ["
                + r1 + " " + g1 + " " + b1 + "] <> ["
                + r2 + " " + g2 + " " + b2 + "]");
    }

    /**
     * Set LED brightness for currently selected LEDs.
     * Opcode: LED_SET_BRIGHTNESS, payload=[brightness].
     *
     * @param brightness  0-255 (capped by Arduino to LED_MAX_BAR_BRIGHTNESS)
     *
     * Called by: LEDManager (brightness slider).
     */
    public void sendBrightness(int brightness) {
        Log.v("DATA_S", "Sending brightness: " + brightness);
        _send(ProtocolOpcodes.Opcode.LED_SET_BRIGHTNESS, new byte[]{ (byte) brightness });
        Log.i("DATA_S", "[LED_SET_BRIGHTNESS] # BRIGHTNESS [" + brightness + "]");
    }

    /**
     * Send LED selection bitmask to the Arduino.
     * Opcode: LED_SET_SELECTION, payload = 8-byte bitmask (bit i of byte
     * i/8 = LED i selected) - packed from the flat boolean[] the UI keeps.
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
        byte[] mask = new byte[8];
        StringBuilder logMsg = new StringBuilder("[ ");
        for (int i = 0; i < ledTotal && i < 64; i++) {
            if (ledSelected[i]) mask[i / 8] |= (byte) (1 << (i % 8));
            logMsg.append("(").append(i)
                  .append(ledSelected[i] ? "•" : "◯").append(") ");
        }
        logMsg.append("] # SELECT LED");
        Log.i("DATA_S", logMsg.toString());
        ProtocolOpcodes.LedSetSelectionPayload p = new ProtocolOpcodes.LedSetSelectionPayload();
        p.mask = mask;
        _send(ProtocolOpcodes.Opcode.LED_SET_SELECTION, p.pack());
    }

    // --------------------------------------------------------
    // Settings
    // --------------------------------------------------------
    /**
     * Send a settings command to the Arduino.
     *
     *   setting == -1        → request all settings (opcode: SETTINGS_READ_ALL)
     *   setting == SET_TOTAL → send all default values (opcode: SETTINGS_WRITE,
     *                          one frame carrying every {id,val} pair)
     *   otherwise            → send one setting (opcode: SETTINGS_WRITE, one pair)
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
            Log.v("DATA_S", "Requesting all settings");
            Log.i("DATA_S", "[SETTINGS_READ_ALL] # GET SETTINGS INFO");
            _sendBare(ProtocolOpcodes.Opcode.SETTINGS_READ_ALL, null);

        } else if (setting == setTotal) {
            // Send all default values in one SETTINGS_WRITE frame - "N x
            // {id(u8),val(u8)}" is the raw/variable layout protocol_table.json
            // documents for this opcode.
            Log.v("DATA_S", "Sending all default settings");
            byte[] payload = new byte[setInfo.size() * 2];
            StringBuilder logMsg = new StringBuilder("[");
            int i = 0;
            for (SettingInfo si : setInfo) {
                payload[i++] = (byte) si.id();
                payload[i++] = (byte) si.defaultValue();
                logMsg.append(String.format("(%d:%d)", si.id(), si.defaultValue()));
            }
            logMsg.append("] # SETTINGS");
            Log.i("DATA_S", logMsg.toString());
            _send(ProtocolOpcodes.Opcode.SETTINGS_WRITE, payload);

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
            _send(ProtocolOpcodes.Opcode.SETTINGS_WRITE, new byte[]{ (byte) setting, (byte) progress });
            Log.i("DATA_S", "[SETTINGS_WRITE] # SETTING [" + setting + " " + progress + "]");
        }
    }

    // --------------------------------------------------------
    // Ambient / Enable-Disable
    // --------------------------------------------------------
    /**
     * Enable or disable ambient mode on the Arduino.
     * Opcode: SET_AMBIENT_MODE, payload=[1] (on) or [0] (off).
     *
     * @param mode  true = ON, false = OFF
     *
     * Called by: StatusManager (ambient icon tap).
     */
    public void sendAmbientMode(boolean mode) {
        Log.v("DATA_S", "Sending ambient mode: " + mode);
        _send(ProtocolOpcodes.Opcode.SET_AMBIENT_MODE, new byte[]{ (byte) (mode ? 1 : 0) });
        Log.i("DATA_S", "[SET_AMBIENT_MODE] # AMBIENT MODE [" + mode + "]");
    }

    /**
     * Toggle LED strip enable/disable.
     * Opcode: LED_SET_ENABLE, no payload (Arduino toggles its internal state).
     *
     * @param stsEnableDisable  Current enable/disable status from STS.
     *                          Used only for logging – the Arduino toggles itself.
     *
     * Called by: SettingsManager (LED enable/disable switch).
     */
    public void sendEnableDisable(StatusManager.STS_EnableDisable stsEnableDisable) {
        Log.v("DATA_S", "Sending enable/disable: " + stsEnableDisable);
        _send(ProtocolOpcodes.Opcode.LED_SET_ENABLE, null);
        Log.i("DATA_S", "[LED_SET_ENABLE] # LED "
                + (stsEnableDisable == StatusManager.STS_EnableDisable.ON
                   ? "DISABLE" : "ENABLE"));
    }

    // --------------------------------------------------------
    // Remote Telnet (new capability - not present in the ASCII-era protocol)
    // --------------------------------------------------------
    /**
     * Remotely enable or disable the SmartTV's own Telnet server.
     * Opcode: SET_TELNET_ENABLE, payload=[1]/[0]. Flashed OFF, no EEPROM
     * persistence on the firmware side - never survives a reboot/reflash
     * unattended (01_PROTOCOL.md / 00_PLAN.md SS9 R1).
     *
     * @param on  true = enable, false = disable.
     *
     * Called by: MainActivity's new Telnet toggle button (debug/settings area).
     */
    public void sendTelnetEnable(boolean on) {
        Log.v("DATA_S", "Sending telnet enable: " + on);
        _send(ProtocolOpcodes.Opcode.SET_TELNET_ENABLE, new byte[]{ (byte) (on ? 1 : 0) });
        Log.i("DATA_S", "[SET_TELNET_ENABLE] # TELNET [" + on + "]");
    }

    // --------------------------------------------------------
    // MQTT credential provisioning
    // --------------------------------------------------------
    /**
     * Provision (or force re-verify) the MQTT broker credentials cached on
     * the controller. Opcode: SET_MQTT_CREDENTIALS, payload =
     * userLen(u8)+user+passLen(u8)+pass - raw UTF-8 bytes, no base64 (v1
     * frames aren't ASCII-constrained, unlike the old "$<b64u>,<b64p>"
     * scheme). The controller does NOT trust this pair blindly: it
     * live-verifies it with a real connect() against the broker before
     * ACKing, and only persists it to EEPROM if that succeeds AND it
     * differs from what's already cached. A rejected pair (result 7 /
     * UNAUTHORIZED) leaves any previously-good credentials untouched on the
     * controller side - the callback is the caller's cue to reprompt.
     *
     * Uses a wider-than-normal ACK window (MQTTCRED_ACK_TIMEOUT_MS) since the
     * firmware-side verify is itself a blocking network round trip, not just
     * a local apply.
     *
     * @param user      Plain-text MQTT username.
     * @param pass      Plain-text MQTT password.
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
        byte[] userBytes = user.getBytes(StandardCharsets.UTF_8);
        byte[] passBytes = pass.getBytes(StandardCharsets.UTF_8);
        byte[] payload = new byte[1 + userBytes.length + 1 + passBytes.length];
        int i = 0;
        payload[i++] = (byte) userBytes.length;
        System.arraycopy(userBytes, 0, payload, i, userBytes.length); i += userBytes.length;
        payload[i++] = (byte) passBytes.length;
        System.arraycopy(passBytes, 0, payload, i, passBytes.length);
        _send(ProtocolOpcodes.Opcode.SET_MQTT_CREDENTIALS, payload, callback);
        Log.i("DATA_S", "[SET_MQTT_CREDENTIALS] # MQTT CREDENTIALS [user=" + user + "]"); // never log the password
    }
}
