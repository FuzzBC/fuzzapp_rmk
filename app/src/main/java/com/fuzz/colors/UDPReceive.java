package com.fuzz.colors;

/*
 * ============================================================
 *  UDPReceive.java  (alias: UDPr)
 * ============================================================
 *  Responsibility:
 *      • Opens and owns the shared DatagramSocket.
 *      • Runs a background thread that listens for incoming
 *        UDP packets from the Arduino.
 *      • Parses every packet and dispatches to the correct
 *        handler method (LED update, STS update, etc.).
 *      • Monitors connection health (timeout detection).
 *
 *  How to use:
 *      1.  Instantiate once in Main (MainActivity):
 *              UDPr = new UDPReceive(this, LED, SET, STS);
 *
 *      2.  After construction, share the open socket with UDPs:
 *              UDPs.setSocket(UDPr.getSocket());
 *
 *      3.  Lifecycle calls from Main:
 *              UDPr.init()         – open socket (called in onCreate)
 *              UDPr.setup()        – send welcome, start receive loop
 *              UDPr.startReceiving() – call in onResume
 *              UDPr.stopReceiving()  – call in onPause / onDestroy
 *              UDPr.reconnect()    – re-open socket after failure
 *              UDPr.isAvailable()  – check socket health
 *
 *  Received packet protocol (from Arduino):
 *      LBvv            – LED brightness
 *      LCiiRRggBB…     – LED colour(s)  (one or many)
 *      LDrrggbbRRggBB  – Dual colour
 *      LMvv            – Max brightness
 *      LOi…            – LED selected bitmask
 *      SiiVV…          – Settings value(s)
 *      sAAbbCCddEEffGG – Status packet
 *      MllllAAAA       – Lux + average
 *      pTTTT           – Parfum remaining minutes (0000 = inactive)
 *      uAAAAVVVVRRLLLL – Diffuser usage/refill stats (accum min, avg cycle min,
 *                        history count 0-10, lifetime refill count)
 *      DhRRVVVV...     – Full refill-cycle history (2-hex count + 10x4-hex
 *                        minutes, oldest first) - on demand only, see
 *                        UDPSend.sendDiffuserHistory()
 *      ~               – Stop loading bar
 *      *MSG            – Print message to console
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      UDPs  = UDPSend
 *      UDPr  = UDPReceive   (this class)
 *      LED   = LEDManager
 *      SET   = SettingsManager
 *      STS   = StatusManager
 * ============================================================
 */

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.SocketException;
import java.net.SocketTimeoutException;

public class UDPReceive {

    // --------------------------------------------------------
    // Constants – must match the Arduino firmware
    // --------------------------------------------------------
    // Arduino IP/port live in ONE place only: UDPSend.ARDUINO_IP /
    // UDPSend.ARDUINO_PORT. Referenced below (socket bind + status display) so
    // the address is changed from a single side. Do NOT redeclare here.

    /**
     * How often the connection watchdog re-checks (ms). Kept short so a real
     * drop is noticed promptly; this is the poll cadence, not the loss threshold.
     */
    public static final int UDP_TIMEOUT = 2000;

    /**
     * Silence (ms) after which the link is declared lost. Must exceed the
     * firmware keep-alive interval (APP_KEEPALIVE_MS = 10 s) with margin, or the
     * app would flap to "CONNECTION LOST" during every idle gap between pings.
     * Matches the firmware's own APP_ALIVE_TIMEOUT_MS (25 s) — tolerates ~2
     * missed keep-alives before giving up.
     */
    public static final int CONN_LOST_TIMEOUT = 25000;

    /**
     * First-contact grace (ms): after a UDP welcome/probe is sent, the local
     * link is treated as "available" this long even before any reply, so a
     * fresh connect isn't demoted to cloud instantly. Past this with no UDP
     * packet, isAvailable() returns false and the transport supervisor falls
     * over to cloud. Should match Main.UDP_FALLBACK_MS.
     */
    public static final int UDP_PROBE_GRACE = 4000;

    // --------------------------------------------------------
    // Connection status enum  (used by Main to update UI)
    // --------------------------------------------------------
    public enum Status { NoWifi, Connected, Cloud, ConnectionLost, NoConnection }

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------
    /** Shared socket – created here, injected into UDPs */
    private DatagramSocket socket;

    /** Background receive thread */
    private Thread receiveThread;

    /** Flag that keeps the while-loop alive */
    private boolean isReceiving = false;

    /** Timestamp of the last successfully received packet (ms) */
    private long lastReceiveTime = 0;

    /**
     * Timestamp of the last packet received specifically over the LOCAL UDP
     * socket (ms). Distinct from lastReceiveTime, which cloud traffic also
     * refreshes — using that for isAvailable() would let cloud packets spoof
     * local liveness and flap the transport. Written on the receive thread,
     * read from sender threads, hence volatile.
     */
    private volatile long lastUdpRxTime = 0;

    /** Timestamp of the last UDP welcome/probe sent, arming the first-contact grace. */
    private volatile long udpProbeAt = 0;

    /** Current connection state (kept to avoid redundant UI updates) */
    private Status currentStatus = Status.NoWifi;

    // --------------------------------------------------------
    // Main-thread handler for timeout checks
    // --------------------------------------------------------
    private final Handler handler = new Handler(Looper.getMainLooper());

    /**
     * Periodic runnable: checks whether the last received packet is
     * older than UDP_TIMEOUT and updates the connection status UI.
     * Reschedules itself automatically.
     */
    private final Runnable timeoutRunnable = new Runnable() {
        @Override
        public void run() {
            long now = System.currentTimeMillis();
            if (lastReceiveTime > 0 && (now - lastReceiveTime) > CONN_LOST_TIMEOUT) {
                _setStatus(Status.ConnectionLost);
                lastReceiveTime = 0;   // reset so we don't spam
            }
            // Re-schedule
            handler.postDelayed(this, UDP_TIMEOUT);
        }
    };

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /**
     * Main – used to call:
     *   Main._Console()       log to debug console
     *   Main.runOnUiThread()  dispatch UI work from receive thread
     *   Main.TEXT_ConnectInfo  (updated via _setStatus)
     */
    private final MainActivity Main;

    /**
     * LED – dispatches colour / brightness / selection updates.
     * Alias: LED (LEDManager)
     */
    private final LEDManager LED;

    /**
     * SET – dispatches settings value updates.
     * Alias: SET (SettingsManager)
     */
    private final SettingsManager SET;

    /**
     * STS – dispatches status / sensor data updates.
     * Alias: STS (StatusManager)
     */
    private final StatusManager STS;

    /**
     * UDPs – the sender, used to resolve command ACKs ("#SSR").
     * Wired in from Main via setSender() after both are constructed.
     * Alias: UDPs (UDPSend)
     */
    private UDPSend UDPs;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (alias: Main). Must not be null.
     * @param led   LEDManager instance  (alias: LED).
     * @param set   SettingsManager instance (alias: SET).
     * @param sts   StatusManager instance   (alias: STS).
     */
    public UDPReceive(MainActivity main,
                      LEDManager led,
                      SettingsManager set,
                      StatusManager sts) {
        this.Main = main;
        this.LED  = led;
        this.SET  = set;
        this.STS  = sts;
    }

    /**
     * Wire the sender so incoming "#SSR" ACKs can resolve pending commands.
     * Call once from Main after both UDPr and UDPs are constructed.
     *
     * @param udps  The UDPSend instance (alias: UDPs).
     */
    public void setSender(UDPSend udps) {
        this.UDPs = udps;
    }

    // ========================================================
    //  Socket lifecycle
    // ========================================================

    /**
     * Open the DatagramSocket and start the timeout checker.
     * Call once from Main.onCreate().
     * On failure, status is set to NoConnection.
     */
    public void init() {
        try {
            socket = new DatagramSocket(UDPSend.ARDUINO_PORT);
            socket.setReuseAddress(true);
            socket.setSoTimeout(200);   // short timeout for the receive loop

            Log.v("UDP_R", "Socket initialized on port " + UDPSend.ARDUINO_PORT);
            _setStatus(Status.Connected);
        } catch (Exception e) {
            e.printStackTrace();
            Log.v("UDP_R", "Socket init failed: " + e.getMessage());
            _setStatus(Status.NoConnection);
        }

        // Start periodic timeout checker
        handler.postDelayed(timeoutRunnable, UDP_TIMEOUT);
    }

    /**
     * Post-init setup: clear console, start receive loop, send welcome.
     * Call once from Main.onCreate() AFTER init().
     */
    public void setup() {
        Log.v("UDP_R", "Setup called - clearing console and starting receive");
        Main._Console_Clear();
        startReceiving();
        // UDPs.sendWelcome() is called from Main after setup() returns
    }

    /**
     * Close and re-open the socket (e.g. after a WiFi drop).
     * Call from Main.onResume() or on detected socket failure.
     */
    public void reconnect() {
        Log.v("UDP_R", "Reconnect requested - closing and reopening socket");
        if (socket != null) socket.close();
        init();
    }

    /**
     * Close the socket and stop the timeout handler.
     * Call from Main.onDestroy().
     */
    public void destroy() {
        Log.v("UDP_R", "Destroy called - stopping receive and closing socket");
        stopReceiving();
        handler.removeCallbacks(timeoutRunnable);
        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
    }

    /**
     * @return true if socket is open and WiFi is connected.
     */
    /**
     * Local UDP link usable for the ACTIVE transport decision.
     *
     * True only when WiFi + socket are up AND the board is actually answering
     * over UDP — either a real packet within CONN_LOST_TIMEOUT (proven live) or
     * still inside the first-contact grace after a welcome/probe. A wrong/offline
     * IP produces silence, not a socket error, so without this liveness gate the
     * app would sit on a dead UDP link forever; here it goes false past the
     * grace/timeout and the supervisor falls over to cloud automatically.
     *
     * @return true if the local UDP link is proven (or freshly probed) live.
     */
    public boolean isAvailable() {
        if (!Main._IsWifiConn())              return false;
        if (socket == null || socket.isClosed()) return false;
        long now = System.currentTimeMillis();
        if (lastUdpRxTime > 0 && now - lastUdpRxTime <= CONN_LOST_TIMEOUT) return true; // proven live
        if (udpProbeAt   > 0 && now - udpProbeAt   <= UDP_PROBE_GRACE)    return true;  // first-contact grace
        return false;
    }

    /** @return true if the socket is bound and open (regardless of liveness). */
    public boolean isSocketOpen() {
        return socket != null && !socket.isClosed();
    }

    /** Arm the first-contact grace — call right after sending a UDP welcome/probe. */
    public void markUdpProbe() {
        udpProbeAt = System.currentTimeMillis();
    }

    /**
     * @return The shared DatagramSocket (inject into UDPs after init).
     */
    public DatagramSocket getSocket() {
        return socket;
    }

    // ========================================================
    //  Receive loop
    // ========================================================

    /**
     * Start the background receive thread.
     * Safe to call multiple times – returns immediately if already running.
     */
    public void startReceiving() {
        if (receiveThread != null && receiveThread.isAlive()) {
            Log.v("UDP_R", "Receive thread already running");
            return;
        }

        Log.v("UDP_R", "Starting receive thread");
        isReceiving = true;
        receiveThread = new Thread(() -> {
            byte[]         buffer = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buffer, buffer.length);

            // Set socket timeout for the loop
            try {
                socket.setSoTimeout(100);
                Log.v("UDP_R", "Socket timeout set to 100ms");
            } catch (SocketException e) {
                e.printStackTrace();
                isReceiving = false;
                Log.i("UDP_ERR", "SOCKET EXCEPTION");
                Log.v("UDP_R", "Socket exception: " + e.getMessage());
                Main.runOnUiThread(() ->
                    Main._Console(true, "--", "{#R}SOCKET EXCEPTION{##}"));
                return;
            }

            while (isReceiving) {
                try {
                    socket.receive(packet);
                    final int len = packet.getLength();
                    Log.v("UDP_R", "Packet received, length: " + len);
                    // Any datagram on our socket is the board answering locally,
                    // so this is the authoritative local-liveness signal that
                    // isAvailable() gates on (cloud traffic must not touch it).
                    lastUdpRxTime = System.currentTimeMillis();
                    // The compressed colour packet ('L''K') is RAW BINARY - it can
                    // hold 0x00 and bytes >= 0x80, which new String(...) would mangle
                    // under the platform charset. Snapshot the reused socket buffer
                    // and route it byte-for-byte; everything else stays the ASCII
                    // String path exactly as before.
                    final byte[] data = java.util.Arrays.copyOf(packet.getData(), len);
                    if (len >= 2 && data[0] == 'L' && data[1] == 'K') {
                        Log.v("UDP_R", "Binary color packet detected");
                        Main.runOnUiThread(() -> {
                            lastReceiveTime = System.currentTimeMillis();
                            _recvLedColorBin(data, len);
                        });
                    } else {
                        final String msg = new String(data, 0, len);
                        Log.v("UDP_R", "ASCII packet: " + msg);
                        Main.runOnUiThread(() -> {
                            lastReceiveTime = System.currentTimeMillis();
                            _parsePacket(msg, msg.length());
                        });
                    }

                } catch (SocketTimeoutException e) {
                    // Normal – just means no packet arrived within 100 ms
                    if (!isReceiving) break;

                } catch (IOException e) {
                    if (!isReceiving) break;
                    e.printStackTrace();
                }
            }
        });
        receiveThread.start();
    }

    /**
     * Stop the background receive thread.
     * Call from Main.onPause() and Main.onDestroy().
     */
    public void stopReceiving() {
        Log.v("UDP_R", "Stop receiving called");
        if (receiveThread != null && receiveThread.isAlive()) {
            receiveThread.interrupt();
        }
        isReceiving = false;
    }

    // ========================================================
    //  Packet parser  (runs on UI thread via runOnUiThread)
    // ========================================================

    /**
     * Route an incoming message to the correct handler.
     *
     * @param message  Raw ASCII string received from Arduino.
     * @param size     Byte-length of the message.
     */
    /**
     * Feed a status/reply payload that arrived over the MQTT cloud link
     * (MqttTransport.MQTT_TOPIC, sender tag already stripped) into the SAME
     * parser used for UDP. The wire format is byte-identical, so every
     * _recv* handler and the ACK path work unchanged. Marshalled onto the
     * UI thread to match the UDP receive loop.
     *
     * @param message  Raw payload string from the broker.
     */
    public void onCloudMessage(final String message) {
        if (message == null || message.isEmpty()) return;
        Log.v("UDP_R", "Cloud message received: " + message);
        // Local UDP is the primary link only while it's actually usable.
        // _UDP_Available() (unlike raw _IsWifiConn()) already respects the
        // CLOUD ONLY pin, so a forced-cloud session still applies cloud
        // messages even with WiFi physically connected -- only a genuinely
        // active local link (AUTO/LOCAL ONLY with WiFi + socket up) mutes it.
        if (Main._UDP_Available()) return;
        Main.runOnUiThread(() -> {
            lastReceiveTime = System.currentTimeMillis();
            _parsePacket(message, message.length());
        });
    }

    /**
     * Cloud sibling of the local byte path: a raw 'LK' colour packet that arrived
     * over MQTT (sender tag already stripped by MqttTransport). Same UDP-primary
     * mute rule as onCloudMessage(), then straight into the binary parser.
     *
     * @param data  Payload bytes starting at 'L' 'K'.
     * @param len   Payload length.
     */
    public void onCloudMessageBin(final byte[] data, final int len) {
        if (data == null || len < 2) return;
        Log.v("UDP_R", "Cloud binary message received, length: " + len);
        if (Main._UDP_Available()) return;
        Main.runOnUiThread(() -> {
            lastReceiveTime = System.currentTimeMillis();
            _recvLedColorBin(data, len);
        });
    }

    private void _parsePacket(String message, int size) {
        boolean recognized = false;
        Log.v("UDP_R", "Parsing packet: " + message + " size: " + size);

        switch (message.charAt(0)) {

            case '#': // #SSR – per-command ACK
                Log.v("UDP_R", "ACK packet detected");
                _recvAck(message, size);
                recognized = true;
                break;

            case '*': // *MSG – print to console
                Log.v("UDP_R", "Message packet detected");
                _recvMsg(message.substring(1), size);
                recognized = true;
                break;

            case 'L': // LED sub-commands
                if (message.length() < 2) break;
                Log.v("UDP_R", "LED command: " + message.charAt(1));
                switch (message.charAt(1)) {
                    case 'B': // LBvv – brightness
                        _recvLedBrightness(message, size);
                        recognized = true;
                        break;
                    case 'C': // LCii… – colour
                        _recvLedColor(message, size);
                        recognized = true;
                        break;
                    case 'D': // LD… – dual colour
                        _recvDualColor(message, size);
                        recognized = true;
                        break;
                    case 'M': // LMvv – max brightness
                        _recvMaxBrightness(message, size);
                        recognized = true;
                        break;
                }
                break;

            case 'D': // Diffuser sub-commands relayed from SmartTV (mirrors the diffuser's own D-prefix wire format)
                if (message.length() < 2) break;
                Log.v("UDP_R", "Diffuser command: " + message.charAt(1));
                switch (message.charAt(1)) {
                    case 'h': // Dh + 2-hex count + 10x4-hex minutes – full refill history (on demand only)
                        _recvDiffuserHistory(message, size);
                        recognized = true;
                        break;
                }
                break;

            case 'S': // SiiVV… – settings
                Log.v("UDP_R", "Settings packet detected");
                _recvSettings(message, size);
                recognized = true;
                break;

            case 's': // sTTMMUUAADD – core status (TV/Motion/UDPRAW/Ambient/Diffuser)
                Log.v("UDP_R", "Status packet detected");
                _recvStatus(message, size);
                recognized = true;
                break;

            case 'H': // HTTHH – climate (temperature, humidity)
                Log.v("UDP_R", "Climate packet detected");
                _recvClimate(message, size);
                recognized = true;
                break;

            case 'E': // Ee – LED enable / disable
                Log.v("UDP_R", "Enable packet detected");
                _recvEnable(message, size);
                recognized = true;
                break;

            case 'M': // Mllll – lux level (classified 1..5)
                Log.v("UDP_R", "Lux packet detected");
                _recvLux(message, size);
                recognized = true;
                break;

            case 'p': // pTTTT – parfum remaining minutes (0000 = inactive)
                Log.v("UDP_R", "Parfum packet detected");
                _recvParfum(message, size);
                recognized = true;
                break;

            case 'u': // uAAAAVVVVRRLLLL – diffuser usage/refill stats
                Log.v("UDP_R", "Diffuser usage packet detected");
                _recvDiffuserUsage(message, size);
                recognized = true;
                break;

            case '@': // @mm – active test mode
                Log.v("UDP_R", "Test mode packet detected");
                _recvTestMode(message, size);
                recognized = true;
                break;

            case 'w': // wRRWW – |rssi| dBm + wifi state
                Log.v("UDP_R", "Link packet detected");
                _recvLink(message, size);
                recognized = true;
                break;

            case 'f': // ffff – fault bitmask (transition only)
                Log.v("UDP_R", "Fault packet detected");
                _recvFaults(message, size);
                recognized = true;
                break;

            case 'e': // ee – EEPROM write landed
                Log.v("UDP_R", "EEPROM saved packet detected");
                _recvSaved(message, size);
                recognized = true;
                break;

            case 'k': // k – keep-alive ping from the board; answer + mark live
                Log.v("UDP_R", "Keep-alive packet detected");
                _recvKeepAlive();
                recognized = true;
                break;
        }

        if (recognized) {
            _confirmLive();
        } else {
            Log.i("UDP_R", "MSG [" + message + "] size [" + size + "]");
            Main._Console(true, "◄◄",
                    "COMMAND NOT FOUND [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    // ========================================================
    //  Individual receive handlers
    // ========================================================

    /**
     * '#SSR' – per-command ACK. Resolves the pending command in UDPs so the
     * app learns whether that specific command was applied.
     * SS = 2-hex sequence id, R = 1-hex result code.
     *
     * @param message  Full packet ("#" + seq + result).
     * @param size     Must be 4.
     */
    private void _recvAck(String message, int size) {
        Log.v("UDP_R", "Processing ACK: " + message + " size: " + size);
        if (size != 4) {
            Log.i("UDP_R", "ACK SIZE NOT VALID [" + message + "] SIZE [" + size + "]");
            return;
        }
        try {
            int s      = Integer.parseInt(message.substring(1, 3), 16);
            int result = Integer.parseInt(message.substring(3, 4), 16);
            Log.i("UDP_R", "ACK seq [" + s + "] result [" + result + "]");
            Log.v("UDP_R", "ACK resolved - seq: " + s + " result: " + result);
            if (UDPs != null) UDPs.ackResolve(s, result);
        } catch (NumberFormatException e) {
            Log.i("UDP_R", "ACK PARSE ERROR [" + message + "]");
            Log.v("UDP_R", "ACK parse exception: " + e.getMessage());
        }
    }

    // ========================================================
    //  Device state pushed by the Arduino
    //  Held here so the UI can read it; every packet is also
    //  echoed to the Term console, where [values] chip up.
    // ========================================================

    /** Fault bits, mirroring APP_FAULT_* in the firmware header. */
    public static final int FAULT_WIFI = 0x0001, FAULT_NTP = 0x0002, FAULT_EEPROM = 0x0004,
                            FAULT_BME  = 0x0008, FAULT_DIF_NORESP = 0x0010, FAULT_DIF_NOWATER = 0x0020;

    /** Active __testmode value, 0 = none. */
    public int testMode = 0;

    /** Link health from 'w'. rssi is dBm (negative), 0 when down. */
    public int rssi = 0, wifiState = 0;

    /** Latest fault bitmask from 'f'. */
    public int faults = 0;

    /** @return true when the given FAULT_* bit is currently set. */
    public boolean hasFault(int bit) {
        return (faults & bit) != 0;
    }

    /** '@mm' – the test mode currently forced, 0 = none. */
    private void _recvTestMode(String message, int size) {
        Log.v("UDP_R", "Processing test mode: " + message);
        if (size < 3) return;
        try {
            testMode = Integer.parseInt(message.substring(1, 3), 16);
            Log.v("UDP_R", "Test mode set to: " + testMode);
            Main._Console(true, "\u25c4\u25c4", "TEST MODE ["
                    + ((testMode == 0) ? "none" : SET_TestModeName(testMode)) + "]");
        } catch (NumberFormatException ignored) { }
    }

    /** @return firmware __testmode name for a raw value. */
    private String SET_TestModeName(int m) {
        switch (m) {
            case 1:  return "TV ON";
            case 2:  return "TV OFF";
            case 3:  return "UDPRAW";
            case 4:  return "MOTION COM";
            case 5:  return "MOTION BED";
            case 6:  return "DIFFUSER";
            case 7:  return "LUX";
            default: return "UNKNOWN";
        }
    }

    /** 'HTTHH' – climate: temperature + humidity (each 2-hex, degrees / %). */
    private void _recvClimate(String message, int size) {
        Log.v("UDP_R", "Processing climate: " + message);
        if (size < 5) return;
        try {
            int temp = Integer.parseInt(message.substring(1, 3), 16);
            int hum  = Integer.parseInt(message.substring(3, 5), 16);
            Log.v("UDP_R", "Climate - temp: " + temp + " humidity: " + hum);
            STS.applyClimate(temp, hum);
        } catch (NumberFormatException ignored) { }
    }

    /** 'Ee' – global LED enable (1) / disable (0). */
    private void _recvEnable(String message, int size) {
        Log.v("UDP_R", "Processing enable: " + message);
        if (size < 2) return;
        try {
            int en = Integer.parseInt(message.substring(1, 2), 16);
            Log.v("UDP_R", "Enable state: " + (en != 0));
            STS.applyEnable(en != 0);
        } catch (NumberFormatException ignored) { }
    }

    /**
     * 'wRRWW' – |rssi| dBm + NET_WiFiStatus. The board buckets the signal so
     * this only arrives when the strength band or wifi state actually changes.
     * Feeds the top-left Arduino/WiFi indicator.
     */
    private void _recvLink(String message, int size) {
        Log.v("UDP_R", "Processing link: " + message);
        if (size < 5) return;
        try {
            int mag   = Integer.parseInt(message.substring(1, 3), 16);
            rssi      = (mag == 0) ? 0 : -mag;
            wifiState = Integer.parseInt(message.substring(3, 5), 16);
            Log.v("UDP_R", "Link - rssi: " + rssi + " wifiState: " + wifiState);
            Main.updateWifiSignal(mag, wifiState);
            Log.i("UDP_R", "LINK rssi [" + rssi + "] wifi [" + wifiState + "]");
        } catch (NumberFormatException ignored) { }
    }

    /**
     * 'k' – keep-alive ping from the board (every ~10 s). Reply with a bare 'k'
     * so it knows the app is still here; the reply is un-enveloped on purpose
     * (the firmware treats a 1-char 'k' as liveness only).
     */
    private void _recvKeepAlive() {
        Log.v("UDP_R", "Keep-alive received, sending response");
        if (UDPs != null) UDPs.sendKeepAlive();
    }

    /** 'fffff' – fault bitmask; only transitions are worth a console line. */
    private void _recvFaults(String message, int size) {
        Log.v("UDP_R", "Processing faults: " + message);
        if (size < 5) return;
        try {
            int now = Integer.parseInt(message.substring(1, 5), 16);
            int raised  = now & ~faults;
            int cleared = faults & ~now;
            faults = now;
            Log.v("UDP_R", "Faults - now: " + now + " raised: " + raised + " cleared: " + cleared);

            if (raised  != 0) Main._Console(true, "\u25c4\u25c4", "{#R}FAULT{##} [" + _faultNames(raised) + "]");
            if (cleared != 0) Main._Console(true, "\u25c4\u25c4", "{#G}FAULT CLEARED{##} [" + _faultNames(cleared) + "]");
        } catch (NumberFormatException ignored) { }
    }

    /** @return comma-joined names for the set bits of a fault mask. */
    private String _faultNames(int mask) {
        StringBuilder b = new StringBuilder();
        if ((mask & FAULT_WIFI)         != 0) b.append("WIFI ");
        if ((mask & FAULT_NTP)          != 0) b.append("NTP ");
        if ((mask & FAULT_EEPROM)       != 0) b.append("EEPROM ");
        if ((mask & FAULT_BME)          != 0) b.append("BME ");
        if ((mask & FAULT_DIF_NORESP)   != 0) b.append("DIF-NORESP ");
        if ((mask & FAULT_DIF_NOWATER)  != 0) b.append("DIF-NOWATER ");
        return b.toString().trim();
    }

    /**
     * 'err' – a scheduled EEPROM write finished.
     *
     * A command ack only means "accepted"; settings live in RAM until the
     * delayed write runs, so this is the first point at which a change is
     * safe across a power cut.
     */
    private void _recvSaved(String message, int size) {
        Log.v("UDP_R", "Processing saved: " + message);
        if (size < 3) return;
        try {
            int result = Integer.parseInt(message.substring(1, 3), 16);
            Log.v("UDP_R", "EEPROM save result: " + result);
            Main._Console(true, "\u25c4\u25c4", (result == 0)
                    ? "SETTINGS SAVED [OK]"
                    : "SETTINGS SAVE [{#R}FAILED{##}] code [" + result + "]");
        } catch (NumberFormatException ignored) { }
    }

    /**
     * '*' – Arduino sent one Term-console log line.
     *
     * Wire format (no spaces): level(1 hex) + source(2 hex) + seq(2 hex) + [PRNT(4 chars) + _Debug(6 chars)] + text.
     * The PRNT and _Debug parameters are optional for backward compatibility.
     * A packet that doesn't carry a well-formed header is treated as legacy
     * plain '*TEXT' output, so an older sketch still prints readably.
     *
     * @param message  Text after the leading '*' (envelope header included).
     * @param size     Total packet size.
     */
    private void _recvMsg(String message, int size) {
        Log.v("UDP_R", "Processing message: " + message);
        String clean = message.replace("\n", "").replace("\r", "");

        // Try new format with PRNT and _Debug parameters (header = 1 + 2 + 2 + 4 + 6 = 15 chars)
        if (clean.length() >= 15) {
            try {
                int level = Integer.parseInt(clean.substring(0, 1), 16);
                int src   = Integer.parseInt(clean.substring(1, 3), 16);
                int seq   = Integer.parseInt(clean.substring(3, 5), 16);
                String prnt = clean.substring(5, 9);   // "PRNT"
                String debug = clean.substring(9, 15); // "_Debug"
                String text = clean.substring(15);
                
                // Verify it's the new format by checking for "PRNT"
                if ("PRNT".equals(prnt)) {
                    Log.v("UDP_R", "New format log - level: " + level + " src: " + src + " seq: " + seq);
                    Main._ConsoleLog(level, src, seq, text);
                    Log.i("UDP_R", "LOG lvl [" + level + "] src [" + src + "] seq [" + seq + "] prnt [" + prnt + "] debug [" + debug + "]");
                    return;
                }
            } catch (NumberFormatException ignored) {
                // malformed header - fall through to old format check
            }
        }

        // Try old format (header = 1 + 2 + 2 = 5 chars)
        if (clean.length() >= ConsoleAdapter.LOG_HDR) {
            try {
                int level = Integer.parseInt(clean.substring(0, 1), 16);
                int src   = Integer.parseInt(clean.substring(1, 3), 16);
                int seq   = Integer.parseInt(clean.substring(3, 5), 16);
                Log.v("UDP_R", "Old format log - level: " + level + " src: " + src + " seq: " + seq);
                Main._ConsoleLog(level, src, seq, clean.substring(ConsoleAdapter.LOG_HDR));
                Log.i("UDP_R", "LOG lvl [" + level + "] src [" + src + "] seq [" + seq + "]");
                return;
            } catch (NumberFormatException ignored) {
                // malformed header - fall through to the legacy path
            }
        }

        Log.v("UDP_R", "Legacy format message: " + clean);
        Main._Console(true, "{#C}*{##} ", clean);
        Log.i("UDP_R", "PRINT [" + clean + "] SIZE [" + size + "]");
    }

    /**
     * 'LBvv' – Arduino reports current brightness.
     * Updates the brightness SeekBar in LED (LEDManager).
     *
     * @param message  Full 4-char packet.  e.g. "LB3f"
     * @param size     Must be 4.
     */
    private void _recvLedBrightness(String message, int size) {
        Log.v("UDP_R", "Processing LED brightness: " + message);
        if (size == 4) {
            int brightness = Integer.parseInt(message.substring(2, 4), 16);
            Log.v("UDP_R", "Brightness value: " + brightness);
            LED.setBrightnessProgress(brightness);
            Log.i("UDP_R", "BRIGHTNESS PROGRESS [" + brightness + "]");
            Main._Console(true, "◄◄",
                    "BRIGHTNESS PROGRESS [{#Y}" + brightness + "{##}]");
        }
        // Silently ignore bad size
    }

    /**
     * 'LCiiRRggBB…' – Arduino reports colour for one or more LEDs.
     * Format: LC + (ii RR gg BB)+ where ii = LED index (1 byte hex).
     *
     * @param message  Full packet.
     * @param size     10 (single LED) or 2 + 8*N (multiple LEDs).
     */
    private void _recvLedColor(String message, int size) {
        Log.v("UDP_R", "Processing LED color: " + message + " size: " + size);
        if (size == 10 || ((size - 2) % 8 == 0)) {
            for (int i = 2; i < size; i += 8) {
                int l = Integer.parseInt(message.substring(i,     i + 2), 16);
                int r = Integer.parseInt(message.substring(i + 2, i + 4), 16);
                int g = Integer.parseInt(message.substring(i + 4, i + 6), 16);
                int b = Integer.parseInt(message.substring(i + 6, i + 8), 16);

                if (l < LED.LED_TOTAL) {
                    Log.v("UDP_R", "LED " + l + " color - R:" + r + " G:" + g + " B:" + b);
                    LED.updateColor(l, r, g, b);
                    if (size == 10) {
                        Log.i("UDP_R", "LED [" + l + "] COLOR [R:" + r
                                + "][G:" + g + "][B:" + b + "]");
                    } else if (i == 2) {
                        Log.i("UDP_R", "MULTIPLE COLOR [" + size
                                + "] LEDS [" + (size - 2) / 8 + "]");
                    }
                } else {
                    Log.i("UDP_R", "LED [" + l + "] NOT FOUND");
                    Main._Console(true, "◄◄",
                            "LED [{#Y}" + l + "{##}] {#R}NOT FOUND{##}");
                }
            }
        } else {
            Log.i("UDP_R", "LED COLOR SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            Main._Console(true, "◄◄",
                    "SIZE NOT VALID [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    /**
     * 'LK…' – compressed binary colour delta (replaces the ASCII 'LC' path).
     *
     * Body is a run of records until packet end:
     *   0x01 FILL  start count r g b        – LEDs [start, start+count) = (r,g,b)
     *   0x02 SETN  count {idx r g b}…       – 'count' scattered pixels
     * A solid strip is ~6 bytes here vs ~482 as ASCII 'LC'; nothing is dropped.
     * The legacy _recvLedColor() below stays as a rollback path.
     *
     * @param d    Packet bytes ('L''K' at [0..1]).
     * @param len  Packet length.
     */
    private void _recvLedColorBin(byte[] d, int len) {
        Log.v("UDP_R", "Processing binary LED color, length: " + len);
        int i     = 2;   // skip 'L''K'
        int total = 0;
        while (i < len) {
            int op = d[i++] & 0xFF;
            if (op == 0x01) {                         // FILL start count r g b
                if (i + 5 > len) break;
                int start = d[i++] & 0xFF;
                int count = d[i++] & 0xFF;
                int r = d[i++] & 0xFF, g = d[i++] & 0xFF, b = d[i++] & 0xFF;
                Log.v("UDP_R", "FILL op - start: " + start + " count: " + count + " RGB: " + r + "," + g + "," + b);
                LED.updateColorBulk(start, count, r, g, b);   // one shot, no per-pixel animator
                total += count;
            } else if (op == 0x02) {                  // SETN count {idx r g b}
                if (i >= len) break;
                int count = d[i++] & 0xFF;
                Log.v("UDP_R", "SETN op - count: " + count);
                int[]   idx = new int[count];
                int[][] rgb = new int[count][3];
                int f = 0;                            // valid entries actually read
                for (int k = 0; k < count; k++) {
                    if (i + 4 > len) break;           // truncated record
                    idx[f]    = d[i++] & 0xFF;
                    rgb[f][0] = d[i++] & 0xFF;
                    rgb[f][1] = d[i++] & 0xFF;
                    rgb[f][2] = d[i++] & 0xFF;
                    f++;
                }
                if (f == count) {
                    LED.updateColors(idx, rgb);
                } else if (f > 0) {
                    LED.updateColors(java.util.Arrays.copyOf(idx, f),
                                     java.util.Arrays.copyOf(rgb, f));
                }
                total += f;
            } else {
                Log.i("UDP_R", "LK BAD OPCODE [" + op + "] at [" + (i - 1) + "]");
                break;
            }
        }
        Log.i("UDP_R", "LK COLOR LEDS [" + total + "] BYTES [" + len + "]");
    }

    /**
     * 'LDrrggbbRRggBB' – Arduino returns the saved dual colour.
     * Saves it to the LED dual-colour list and refreshes the UI.
     *
     * @param message  14-char packet.
     * @param size     Must be 14.
     */
    private void _recvDualColor(String message, int size) {
        Log.v("UDP_R", "Processing dual color: " + message);
        if (size == 14) {
            int r1 = Integer.parseInt(message.substring(2,  4),  16);
            int g1 = Integer.parseInt(message.substring(4,  6),  16);
            int b1 = Integer.parseInt(message.substring(6,  8),  16);
            int r2 = Integer.parseInt(message.substring(8,  10), 16);
            int g2 = Integer.parseInt(message.substring(10, 12), 16);
            int b2 = Integer.parseInt(message.substring(12, 14), 16);

            // Delegate save + UI refresh to LED
            Log.v("UDP_R", "Dual color - L: " + r1 + "," + g1 + "," + b1 + " R: " + r2 + "," + g2 + "," + b2);
            LED.addDualColor(r1, g1, b1, r2, g2, b2);

            Log.i("UDP_R", "DUAL COLOR [" + r1 + " " + g1 + " " + b1
                    + "] <> [" + r2 + " " + g2 + " " + b2 + "]");
            Main._Console(true, "◄◄",
                    "DUAL COLOR [" + r1 + "," + g1 + "," + b1
                    + "] -- [" + r2 + "," + g2 + "," + b2 + "]");
        } else {
            Log.i("UDP_R", "DUAL COLOR SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            Main._Console(true, "◄◄",
                    "DUAL COLOR SIZE NOT VALID [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    /**
     * 'LMvv' – Arduino reports the maximum allowed brightness.
     * Updates the brightness SeekBar maximum in LED.
     *
     * @param message  4-char packet.
     * @param size     Must be 4.
     */
    private void _recvMaxBrightness(String message, int size) {
        Log.v("UDP_R", "Processing max brightness: " + message);
        if (size == 4) {
            int brightness = Integer.parseInt(message.substring(2, 4), 16);
            Log.v("UDP_R", "Max brightness value: " + brightness);
            LED.setMaxBrightness(brightness);
            Log.i("UDP_R", "MAX BRIGHTNESS [" + brightness + "]");
            Main._Console(true, "◄◄",
                    "MAX BRIGHTNESS [{#Y}" + brightness + "{##}]");
        } else {
            Log.i("UDP_R", "MAX BRIGHTNESS SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            Main._Console(true, "◄◄",
                    "MAX BRIGHTNESS SIZE NOT VALID [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    /**
     * 'SiiVV…' – Arduino returns one or more setting values.
     * Validates and applies each to SET (SettingsManager).
     *
     * @param message  Packet starting with 'S'.
     * @param size     5 (single) or 1 + 4*N (multiple).
     */
    private void _recvSettings(String message, int size) {
        Log.v("UDP_R", "Processing settings: " + message + " size: " + size);
        if (size == 5 || ((size - 1) % 4 == 0)) {
            StringBuilder validLog = new StringBuilder("SETTING ");
            for (int i = 1; i < size; i += 4) {
                int id    = Integer.parseInt(message.substring(i,     i + 2), 16);
                int value = Integer.parseInt(message.substring(i + 2, i + 4), 16);

                if (SET.isValidId(id)) {
                    // Clamp to valid range
                    SettingInfo si = SET.getSettingInfo(id);
                    if (si.maxValue() > 1) {
                        if (value < si.minValue()) value = si.minValue();
                        else if (value > si.maxValue()) value = si.maxValue();
                    }
                    SET.applyReceivedSetting(id, value);
                    if (id == SET.SET_HB_Dual) LED._refreshHBDualSplit();
                    if (id == SET.SET_HB_Effect || id == SET.SET_HB_EffectSpeed || id == SET.SET_TvOnHBEffect) {
                        LED.notifyHBSettingChanged(id);
                    }
                    validLog.append("[").append(id).append(":")
                            .append(value).append("] ");
                } else {
                    Log.i("UDP_R", "SETTING [" + id + "] NOT FOUND");
                }
            }
            Log.i("UDP_R", validLog.toString());
        } else {
            Log.i("UDP_R", "SETTINGS SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            Main._Console(true, "◄◄",
                    "SETTINGS SIZE NOT VALID [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    /**
     * 'sTTMMUUAADD' – Arduino reports core system status (v8.1).
     * Climate ('H') and enable ('E') were split into their own packets.
     *
     * Format (all 2-char hex fields):
     *   s TT MM UU AA DD
     *     TV Motion UDPRAW(Ambilight) AmbientMode Diffuser
     *
     * @param message  11-char packet.
     * @param size     Must be 11.
     */
    private void _recvStatus(String message, int size) {
        Log.v("UDP_R", "Processing status: " + message);
        if (size != 11) {
            Log.i("UDP_R", "STATUS SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            return;
        }

        Log.i("UDP_R", "STATUS [" + message + "]");
        STS.applyStatus(message);
        LED._refreshHBDualSplit(); // TV on/off can flip HB into/out of dual split
    }

    /**
     * 'pTTTT' – Arduino relays the diffuser's parfum-mode remaining
     * minutes (4-digit hex, 0000 = parfum inactive). Stored in STS so
     * the long-press parfum popup opens with the live value.
     *
     * @param message  Full 5-char packet starting with 'p'.
     * @param size     Byte-length of the message (must be 5).
     */
    private void _recvParfum(String message, int size) {
        Log.v("UDP_R", "Processing parfum: " + message);
        if (size != 5) {
            Log.i("UDP_R", "PARFUM SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            return;
        }

        try {
            int minutes = Integer.parseInt(message.substring(1, 5), 16);
            Log.v("UDP_R", "Parfum minutes: " + minutes);
            Log.i("UDP_R", "PARFUM [" + minutes + " min]");
            STS.applyParfumRemaining(minutes);
        } catch (NumberFormatException e) {
            Log.i("UDP_R", "PARFUM PARSE ERROR [" + message + "]");
        }
    }

    /**
     * 'uAAAAVVVVRRLLLL' – diffuser usage/refill stats, relayed by the SmartTV
     * from the diffuser's extended "Ds"/"Dc" reply: accumulated (mode-weighted)
     * minutes since the last refill, rolling average completed-cycle minutes,
     * refill history count (0-10), and lifetime refill count. Drives the
     * top-left refill badge (icon tint, percent, fill bar) via STS.
     *
     * @param message  Full 15-char packet starting with 'u'.
     * @param size     Byte-length of the message (must be 15).
     */
    private void _recvDiffuserUsage(String message, int size) {
        Log.v("UDP_R", "Processing diffuser usage: " + message);
        if (size != 15) {
            Log.i("UDP_R", "DIFFUSER USAGE SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            return;
        }

        try {
            int accumMin     = Integer.parseInt(message.substring(1, 5), 16);
            int avgMin       = Integer.parseInt(message.substring(5, 9), 16);
            int refillCount  = Integer.parseInt(message.substring(9, 11), 16);
            int totalRefills = Integer.parseInt(message.substring(11, 15), 16);
            Log.i("UDP_R", "DIFFUSER USAGE [accum=" + accumMin + "min avg=" + avgMin
                    + "min refills=" + refillCount + "/10 total=" + totalRefills + "]");
            STS.applyDiffuserUsage(accumMin, avgMin, refillCount, totalRefills);
        } catch (NumberFormatException e) {
            Log.i("UDP_R", "DIFFUSER USAGE PARSE ERROR [" + message + "]");
        }
    }

    /**
     * 'Dh' + 2-hex count + 10x4-hex minutes - full refill-cycle history, sent
     * only on demand (see UDPSend.sendDiffuserHistory()) and never cached by
     * SmartTV, which just relays the diffuser's reply straight through.
     * Oldest cycle first; slots beyond the valid count are 0 (unused).
     *
     * @param message  Full 44-char packet starting with "Dh".
     * @param size     Byte-length of the message (must be 44).
     */
    private void _recvDiffuserHistory(String message, int size) {
        Log.v("UDP_R", "Processing diffuser history: " + message);
        if (size != 44) {
            Log.i("UDP_R", "DIFFUSER HISTORY SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            return;
        }

        try {
            int count = Integer.parseInt(message.substring(2, 4), 16);
            int[] minutes = new int[10];
            for (int i = 0; i < 10; i++) {
                int start = 4 + i * 4;
                minutes[i] = Integer.parseInt(message.substring(start, start + 4), 16);
            }
            Log.i("UDP_R", "DIFFUSER HISTORY [" + count + "/10] " + java.util.Arrays.toString(minutes));
            STS.applyDiffuserHistory(count, minutes);
        } catch (NumberFormatException e) {
            Log.i("UDP_R", "DIFFUSER HISTORY PARSE ERROR [" + message + "]");
        }
    }

    /**
     * Convert 8 hex chars (4 bytes, high octet first) into a dotted-decimal
     * IPv4 string.
     *
     * @param hex  Exactly 8 hex digits, case-insensitive.
     * @return Dotted-decimal address, or null if the input isn't valid hex.
     */
    private String _hexToIp(String hex) {
        try {
            return Integer.parseInt(hex.substring(0, 2), 16) + "."
                 + Integer.parseInt(hex.substring(2, 4), 16) + "."
                 + Integer.parseInt(hex.substring(4, 6), 16) + "."
                 + Integer.parseInt(hex.substring(6, 8), 16);
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /**
     * 'Mllll' – Arduino reports the classified lux level (1..5). The rolling
     * average ('mA') was dropped in v8.1. Delegates the animated update to STS.
     *
     * @param message  5-char packet.
     * @param size     Must be 5.
     */
    private void _recvLux(String message, int size) {
        Log.v("UDP_R", "Processing lux: " + message);
        if (size == 5) {
            int lux = Integer.parseInt(message.substring(1, 5), 16);
            Log.v("UDP_R", "Lux level: " + lux);
            STS.applyLux(lux);
        } else {
            Log.i("UDP_R", "LUX SIZE NOT VALID [" + message
                    + "] SIZE [" + size + "]");
            Main._Console(true, "◄◄",
                    "LUX SIZE NOT VALID [{#Y}" + message
                    + "{##}] SIZE [{#R}" + size + "{##}]");
        }
    }

    // ========================================================
    //  Connection status management
    // ========================================================

    /**
     * Update the connection status display in Main.
     * Only triggers a UI change when the status actually changes,
     * to avoid redundant animations.
     *
     * @param status  New Status enum value.
     */
    /**
     * Live-status flavour: CLOUD MODE when the controller is reached over MQTT
     * (no local WiFi), otherwise the local Connected (IP:port) label. Call on
     * every liveness confirmation so the top label tracks the active transport.
     */
    public void _confirmLive() {
        Log.v("UDP_R", "Confirming live status");
        Main._ClearBootLoading();   // first real confirmation - stop waiting on the 10s timeout
        Main._CancelTransportFallback(); // any transport answered - stop the UDP->MQTT fallback timer
        if (!Main._UDP_Available() && Main._MqttConnected()) _setStatus(Status.Cloud);
        else                                                 _setStatus(Status.Connected);
    }

    public void _setStatus(Status status) {
        if (status == currentStatus) return;   // no change
        Log.v("UDP_R", "Status changing from " + currentStatus + " to " + status);
        currentStatus = status;

        switch (status) {
            case NoWifi:
                Main.TEXT_ConnectInfo.setText("~~ NO WIFI ~~");
                Main._Console(true, "--", "{#R}NO WIFI{##}");
                break;
            case Connected:
                Main.TEXT_ConnectInfo.setText(
                        "~~ " + UDPSend.ARDUINO_IP + ":" + UDPSend.ARDUINO_PORT + " ~~");
                Main._Console(true, "--",
                        "CONNECT {#G}" + UDPSend.ARDUINO_IP
                        + "{##}:{#Y}" + UDPSend.ARDUINO_PORT + "{##}");
                break;
            case Cloud:
                // Reached over the HiveMQ cloud link (local UDP isn't the active transport).
                Main.TEXT_ConnectInfo.setText("~~ CLOUD MODE ~~");
                Main._Console(true, "☁", "{#G}CLOUD MODE{##}");
                break;
            case ConnectionLost:
                Main.TEXT_ConnectInfo.setText("~~ CONNECTION LOST ~~");
                Main._Console(true, "--", "{#R}CONNECTION LOST{##}");
                break;
            case NoConnection:
                Main.TEXT_ConnectInfo.setText("~~ NO CONNECTION ~~");
                Main._Console(true, "--", "{#R}NO CONNECTION{##}");
                break;
        }

        // Always re-apply rainbow animation on status change
        Main._RainbowTextOFF(Main.TEXT_ConnectInfo);
        Main._RainbowTextON(Main.TEXT_ConnectInfo);
    }
}
