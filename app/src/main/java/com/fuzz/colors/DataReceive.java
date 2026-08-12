package com.fuzz.colors;

/*
 * ============================================================
 *  DataReceive.java  (alias: DATAr)
 * ============================================================
 *  Responsibility:
 *      Shared inbound layer for the Arduino controller - NOT UDP-only.
 *      Parses every binary v1 frame and dispatches to the correct handler
 *      (LED update, STS update, etc.) regardless of which transport
 *      delivered it: the local UDP receive loop below feeds _parseFrame()
 *      directly, while MqttTransport's cloud callback feeds the same
 *      parser via onCloudMessage(). Also:
 *      • Opens and owns the shared DatagramSocket (local UDP only).
 *      • Runs the background thread that listens for incoming local UDP
 *        packets from the Arduino.
 *      • Monitors connection health per transport - isUdpAvailable() for
 *        local UDP, isMqttArduinoAlive() for the cloud link - and drives
 *        the status label (see _setStatus()/Status).
 *
 *  PROTOCOL ADAPTATION (from the original ASCII scheme):
 *      Every packet is now a binary v1 frame (MAGIC/FLAGS/SEQ/OPCODE/
 *      payload/CRC8, see com.fuzz.colors.protocol.Frame /
 *      01_PROTOCOL.md), not an ASCII string starting with a marker char.
 *      There is no more ASCII-vs-binary branch in the receive loop (the
 *      old 'LK' compressed colour packet needed one; TELEM_COLOR_SYNC uses
 *      the exact same FILL/SETN record encoding that packet did, so
 *      _recvColorSync() below is nearly a line-for-line port of the old
 *      _recvLedColorBin()). ACK vs telemetry is classified by the frame's
 *      own FLAGS bits (IS_ACK first, else dispatch by OPCODE), not by a
 *      leading '#'/'*' marker character.
 *
 *  How to use:
 *      1.  Instantiate once in Main (MainActivity):
 *              DATAr = new DataReceive(this, LED, SET, STS);
 *
 *      2.  After construction, share the open socket with DATAs:
 *              DATAs.setSocket(DATAr.getSocket());
 *
 *      3.  Lifecycle calls from Main:
 *              DATAr.init()         – open socket (called in onCreate)
 *              DATAr.setup()        – send welcome, start receive loop
 *              DATAr.startReceiving() – call in onResume
 *              DATAr.stopReceiving()  – call in onPause / onDestroy
 *              DATAr.reconnect()    – re-open socket after failure
 *              DATAr.isUdpAvailable()      – check local UDP liveness
 *              DATAr.isMqttArduinoAlive()  – check cloud (MQTT) liveness
 *
 *  References used here (short aliases):
 *      Main  = MainActivity
 *      DATAs  = DataSend
 *      DATAr  = DataReceive   (this class)
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

import com.fuzz.colors.protocol.Frame;
import com.fuzz.colors.protocol.ProtocolOpcodes;

public class DataReceive {

    // --------------------------------------------------------
    // Constants – must match the Arduino firmware
    // --------------------------------------------------------
    // Arduino IP/port live in ONE place only: DataSend.ARDUINO_IP /
    // DataSend.ARDUINO_PORT. Referenced below (socket bind + status display) so
    // the address is changed from a single side. Do NOT redeclare here.

    /**
     * How often the connection watchdog re-checks (ms). Kept short so a real
     * drop is noticed promptly; this is the poll cadence, not the loss threshold.
     */
    public static final int UDP_TIMEOUT = 2000;

    /**
     * Silence (ms) after which the link is declared lost. Must exceed the
     * firmware keep-alive interval with margin, or the app would flap to
     * "CONNECTION LOST" during every idle gap between pings.
     */
    public static final int CONN_LOST_TIMEOUT = 25000;

    /**
     * First-contact grace (ms): after a UDP welcome/probe is sent, the local
     * link is treated as "available" this long even before any reply, so a
     * fresh connect isn't demoted to cloud instantly. Past this with no UDP
     * packet, isUdpAvailable() returns false and the transport supervisor falls
     * over to cloud. Should match Main.UDP_FALLBACK_MS.
     */
    public static final int UDP_PROBE_GRACE = 4000;

    // --------------------------------------------------------
    // Connection status enum  (used by Main to update UI)
    // --------------------------------------------------------
    public enum Status { NoWifi, Connected, Cloud, CloudNoReply, ConnectionLost, NoConnection }

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------
    /** Shared socket – created here, injected into DATAs */
    private DatagramSocket socket;

    /** Background receive thread */
    private Thread receiveThread;

    /** Flag that keeps the while-loop alive */
    private boolean isReceiving = false;

    /**
     * Static mirror of "the app's own receive thread is actively consuming
     * DataSend.ARDUINO_PORT right now" - true only between startReceiving()
     * and stopReceiving()/the socket-exception bailout, i.e. while the app is
     * foregrounded and genuinely listening for a reply. Exists so headless
     * callers with no MainActivity/DataReceive instance (WidgetStatusFetcher,
     * running from WidgetUpdateWorker) can tell whether binding their own
     * short-lived socket to the same port would race the live app for its
     * own replies. See isReceivingActively() / WidgetStatusFetcher._fetchLocalUdp().
     */
    private static volatile boolean sReceivingActively = false;

    /** Timestamp of the last successfully received packet (ms) */
    private long lastReceiveTime = 0;

    /**
     * Timestamp of the last packet received specifically over the LOCAL UDP
     * socket (ms). Distinct from lastReceiveTime, which cloud traffic also
     * refreshes — using that for isUdpAvailable() would let cloud packets spoof
     * local liveness and flap the transport. Written on the receive thread,
     * read from sender threads, hence volatile.
     */
    private volatile long lastUdpRxTime = 0;

    /**
     * Timestamp of the last genuine (non-echo) payload received over the
     * MQTT cloud link (ms). Mirrors lastUdpRxTime for the cloud transport:
     * a live broker SESSION (MqttTransport.isConnected()) only proves the
     * phone reached HiveMQ, not that the board is listening on the other
     * end, so this is the only real proof the Arduino is reachable over
     * cloud. Written from MqttTransport's callback thread, read from the
     * UI/supervisor thread, hence volatile.
     */
    private volatile long lastMqttRxTime = 0;

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
     * DATAs – the sender, used to resolve command ACKs.
     * Wired in from Main via setSender() after both are constructed.
     * Alias: DATAs (DataSend)
     */
    private DataSend DATAs;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main  Running MainActivity (alias: Main). Must not be null.
     * @param led   LEDManager instance  (alias: LED).
     * @param set   SettingsManager instance (alias: SET).
     * @param sts   StatusManager instance   (alias: STS).
     */
    public DataReceive(MainActivity main,
                      LEDManager led,
                      SettingsManager set,
                      StatusManager sts) {
        this.Main = main;
        this.LED  = led;
        this.SET  = set;
        this.STS  = sts;
    }

    /**
     * Wire the sender so incoming ACK frames can resolve pending commands.
     * Call once from Main after both DATAr and DATAs are constructed.
     *
     * @param udps  The DataSend instance (alias: DATAs).
     *
     * Called by: MainActivity (onCreate, once, after both DATAr and DATAs
     * are constructed).
     */
    public void setSender(DataSend udps) {
        this.DATAs = udps;
    }

    // ========================================================
    //  Socket lifecycle
    // ========================================================

    /**
     * Open the DatagramSocket and start the timeout checker.
     * Call once from Main.onCreate().
     * On failure, status is set to NoConnection.
     *
     * Called by: MainActivity (onCreate) and internally by reconnect().
     */
    public void init() {
        try {
            socket = new DatagramSocket(DataSend.ARDUINO_PORT);
            socket.setReuseAddress(true);
            socket.setSoTimeout(200);   // short timeout for the receive loop

            Log.v("DATA_R", "Socket initialized on port " + DataSend.ARDUINO_PORT);
            _setStatus(Status.Connected);
        } catch (Exception e) {
            e.printStackTrace();
            Log.v("DATA_R", "Socket init failed: " + e.getMessage());
            _setStatus(Status.NoConnection);
        }

        // Start periodic timeout checker
        handler.postDelayed(timeoutRunnable, UDP_TIMEOUT);
    }

    /**
     * Post-init setup: clear console, start receive loop, send welcome.
     * Call once from Main.onCreate() AFTER init().
     *
     * Called by: MainActivity (onCreate).
     */
    public void setup() {
        Log.v("DATA_R", "Setup called - clearing console and starting receive");
        Main._Console_Clear();
        startReceiving();
        // DATAs.sendWelcome() is called from Main after setup() returns
    }

    /**
     * Close and re-open the socket (e.g. after a WiFi drop).
     * Call from Main.onResume() or on detected socket failure.
     *
     * Called by: MainActivity._connectTransport() (onResume / manual retry
     * long-press), when on WiFi and the socket isn't already open.
     */
    public void reconnect() {
        Log.v("DATA_R", "Reconnect requested - closing and reopening socket");
        if (socket != null) socket.close();
        init();
    }

    /**
     * Close the socket and stop the timeout handler.
     * Call from Main.onDestroy().
     *
     * Called by: MainActivity (onDestroy).
     */
    public void destroy() {
        Log.v("DATA_R", "Destroy called - stopping receive and closing socket");
        stopReceiving();
        handler.removeCallbacks(timeoutRunnable);
        if (socket != null && !socket.isClosed()) {
            socket.close();
        }
    }

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
     *
     * Called by: MainActivity (Main._UDP_Available() wrapper - used
     * throughout the transport-selection/status logic and by DataSend to
     * pick UDP vs MQTT) and internally by _confirmLive().
     */
    public boolean isUdpAvailable() {
        if (!Main._IsWifiConn())              return false;
        if (socket == null || socket.isClosed()) return false;
        long now = System.currentTimeMillis();
        if (lastUdpRxTime > 0 && now - lastUdpRxTime <= CONN_LOST_TIMEOUT) return true; // proven live
        if (udpProbeAt   > 0 && now - udpProbeAt   <= UDP_PROBE_GRACE)    return true;  // first-contact grace
        return false;
    }

    /**
     * @return true if the socket is bound and open (regardless of liveness).
     * Called by: MainActivity._superviseTransport() (guards the LAN probe
     * while parked on cloud).
     */
    public boolean isSocketOpen() {
        return socket != null && !socket.isClosed();
    }

    /**
     * Arm the first-contact grace — call right after sending a UDP welcome/probe.
     * Called by: MainActivity._connectTransport().
     */
    public void markUdpProbe() {
        udpProbeAt = System.currentTimeMillis();
    }

    /**
     * Cloud (MQTT) link proven live: the Arduino has actually sent something
     * over the broker within CONN_LOST_TIMEOUT. A live MqttTransport
     * session only means the phone reached HiveMQ - it says nothing about
     * whether the board is on the other end, so this (not
     * MqttTransport.isConnected()) is what should gate the "CLOUD MODE"
     * label. See onCloudMessage(), the only writer.
     *
     * @return true if the board has replied over cloud recently.
     *
     * Called by: MainActivity (_refreshTransportStatus(), every supervisor
     * tick) and internally by _confirmLive().
     */
    public boolean isMqttArduinoAlive() {
        long now = System.currentTimeMillis();
        return lastMqttRxTime > 0 && now - lastMqttRxTime <= CONN_LOST_TIMEOUT;
    }

    /**
     * @return The shared DatagramSocket (inject into DATAs after init).
     *
     * Called by: MainActivity, to pass the socket to DataSend.setSocket().
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
     *
     * Called by: setup() (initial start) and MainActivity._connectTransport()
     * (onResume / manual retry, when on WiFi).
     */
    public void startReceiving() {
        if (receiveThread != null && receiveThread.isAlive()) {
            Log.v("DATA_R", "Receive thread already running");
            return;
        }

        Log.v("DATA_R", "Starting receive thread");
        isReceiving = true;
        sReceivingActively = true;
        receiveThread = new Thread(() -> {
            byte[]         buffer = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buffer, buffer.length);

            // Set socket timeout for the loop
            try {
                socket.setSoTimeout(100);
                Log.v("DATA_R", "Socket timeout set to 100ms");
            } catch (SocketException e) {
                e.printStackTrace();
                isReceiving = false;
                sReceivingActively = false;
                Log.i("DATA_ERR", "SOCKET EXCEPTION");
                Log.v("DATA_R", "Socket exception: " + e.getMessage());
                Main.runOnUiThread(() ->
                    Main._Console(true, "--", "{#R}SOCKET EXCEPTION{##}"));
                return;
            }

            while (isReceiving) {
                try {
                    socket.receive(packet);
                    final int len = packet.getLength();
                    Log.v("DATA_R", "UDP : Packet received, length: " + len);
                    // Any datagram on our socket is the board answering locally,
                    // so this is the authoritative local-liveness signal that
                    // isUdpAvailable() gates on (cloud traffic must not touch it).
                    lastUdpRxTime = System.currentTimeMillis();
                    // Every packet is a binary v1 frame now - no more ASCII vs
                    // raw-binary branch, just copy the reused socket buffer and
                    // hand it to the frame parser.
                    final byte[] data = java.util.Arrays.copyOf(packet.getData(), len);
                    Main.runOnUiThread(() -> {
                        lastReceiveTime = System.currentTimeMillis();
                        _parseFrame(data, len);
                    });

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
     *
     * Called by: MainActivity (onPause) and internally by destroy().
     */
    public void stopReceiving() {
        Log.v("DATA_R", "Stop receiving called");
        if (receiveThread != null && receiveThread.isAlive()) {
            receiveThread.interrupt();
        }
        isReceiving = false;
        sReceivingActively = false;
    }

    /**
     * @return true if the app's own receive thread is actively consuming
     * DataSend.ARDUINO_PORT right now (foregrounded, between startReceiving()
     * and stopReceiving()). NOT the same as isUdpAvailable() - this says
     * nothing about whether the board is answering, only whether something
     * else binding the same local port right now would steal the app's own
     * replies out from under it.
     *
     * Called by: WidgetStatusFetcher._fetchLocalUdp(), to skip its own bind
     * while the live app would collide with it (see class doc there).
     */
    public static boolean isReceivingActively() {
        return sReceivingActively;
    }

    // ========================================================
    //  Frame parser  (runs on UI thread via runOnUiThread)
    // ========================================================

    /**
     * Feed a frame that arrived over the MQTT cloud link into the SAME
     * parser used for UDP. The wire format is byte-identical, so every
     * _recv* handler and the ACK path work unchanged. Marshalled onto the
     * UI thread to match the UDP receive loop.
     *
     * @param data  Raw frame bytes from the broker.
     * @param len   Byte length.
     *
     * Called by: MqttTransport.messageArrived() (cloud RX).
     */
    public void onCloudMessage(final byte[] data, final int len) {
        if (data == null || len == 0) return;
        Log.v("DATA_R", "MQTT : frame received, length: " + len);
        // Proof the Arduino itself is on the other end of the broker -
        // stamp this even if local ends up muting the payload below, so
        // isMqttArduinoAlive() stays accurate regardless of which
        // transport is currently primary.
        lastMqttRxTime = System.currentTimeMillis();
        // Local UDP is the primary link only while it's actually usable.
        // _UDP_Available() (unlike raw _IsWifiConn()) already respects the
        // CLOUD ONLY pin, so a forced-cloud session still applies cloud
        // messages even with WiFi physically connected -- only a genuinely
        // active local link (AUTO/LOCAL ONLY with WiFi + socket up) mutes it.
        if (Main._UDP_Available()) return;
        Main.runOnUiThread(() -> {
            lastReceiveTime = System.currentTimeMillis();
            _parseFrame(data, len);
        });
    }

    /**
     * Route an incoming binary v1 frame to the correct handler.
     *
     * Called by: internal only - the local receive loop in startReceiving()
     * and onCloudMessage().
     */
    private void _parseFrame(byte[] data, int len) {
        Frame.Parsed f = Frame.parse(data, len);
        if (!f.ok) {
            Log.i("DATA_R", "MALFORMED FRAME [" + f.reason + "] len [" + len + "]");
            Main._Console(true, "◄◄", "MALFORMED FRAME [{#R}" + f.reason + "{##}]");
            return;
        }

        boolean recognized = true;

        if ((f.flags & ProtocolOpcodes.Flag.IS_ACK) != 0) {
            _recvAck(f.seq, f.payload);
            _confirmLive();
            return;
        }

        byte opcode = (byte) f.opcode;
        if (opcode == ProtocolOpcodes.Opcode.LOG) {
            _recvLog(f.seq, f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_COLOR_SYNC) {
            _recvColorSync(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_DUAL_COLOR) {
            _recvDualColor(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_MAX_BRIGHTNESS) {
            _recvMaxBrightness(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_ENABLE) {
            _recvEnable(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_SETTINGS_FULL) {
            _recvSettingsFull(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_SETTINGS_ONE) {
            _recvSettingsOne(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_SAVE_RESULT) {
            _recvSaved(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_DEVICE_ID) {
            _recvDeviceId(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_STATUS) {
            _recvStatus(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_CLIMATE) {
            _recvClimate(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_LUX) {
            _recvLux(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_LINK) {
            _recvLink(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_FAULTS) {
            _recvFaults(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_TEST_MODE) {
            _recvTestMode(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_PARFUM_REMAINING) {
            _recvParfum(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_DIFFUSER_USAGE) {
            _recvDiffuserUsage(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_DIFFUSER_STATUS) {
            _recvDiffuserStatus(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.TELEM_DIFFUSER_HISTORY) {
            _recvDiffuserHistory(f.payload);
        } else if (opcode == ProtocolOpcodes.Opcode.KEEPALIVE) {
            _recvKeepAlive();
        } else {
            recognized = false;
        }

        if (recognized) {
            _confirmLive();
        } else {
            String opName = ProtocolOpcodes.opcodeName(opcode);
            Log.i("DATA_R", "OPCODE NOT HANDLED [" + opName + "]");
            Main._Console(true, "◄◄",
                    "OPCODE NOT HANDLED [{#Y}" + opName + "{##}]");
        }
    }

    // ========================================================
    //  Individual receive handlers
    // ========================================================

    /**
     * ACK frame - resolves the pending command in DATAs so the app learns
     * whether that specific command was applied.
     *
     * @param seq      Sequence id from the frame header.
     * @param payload  ACK payload (result code).
     *
     * Called by: internal only - _parseFrame(). Feeds DataSend.ackResolve()
     * to resolve the pending command.
     */
    private void _recvAck(int seq, byte[] payload) {
        if (payload.length < 1) {
            Log.i("DATA_R", "ACK PAYLOAD EMPTY seq [" + seq + "]");
            return;
        }
        ProtocolOpcodes.AckPayload ack = ProtocolOpcodes.AckPayload.unpack(payload, 0);
        Log.i("DATA_R", "ACK seq [" + seq + "] result [" + ack.result + "]");
        if (DATAs != null) DATAs.ackResolve(seq, ack.result);
    }

    // ========================================================
    //  Device state pushed by the Arduino
    //  Held here so the UI can read it; every packet is also
    //  echoed to the Term console, where [values] chip up.
    // ========================================================

    /** Fault bits, mirroring the firmware's fault mask layout. */
    public static final int FAULT_WIFI = 0x0001, FAULT_NTP = 0x0002, FAULT_EEPROM = 0x0004,
                            FAULT_BME  = 0x0008, FAULT_DIF_NORESP = 0x0010, FAULT_DIF_NOWATER = 0x0020;

    /** Active test-mode value, 0 = none. */
    public int testMode = 0;

    /** Link health. rssi is dBm (negative), 0 when down. */
    public int rssi = 0, wifiState = 0;

    /** Latest fault bitmask. */
    public int faults = 0;

    /** TELEM_TEST_MODE - the test mode currently forced, 0 = none. */
    private void _recvTestMode(byte[] payload) {
        if (payload.length < 1) return;
        testMode = payload[0] & 0xFF;
        Log.v("DATA_R", "Test mode set to: " + testMode);
        Main._Console(true, "◄◄", "TEST MODE ["
                + ((testMode == 0) ? "none" : SET_TestModeName(testMode)) + "]");
    }

    /** @return firmware test-mode name for a raw value. Called by: _recvTestMode() only. */
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

    /** TELEM_CLIMATE - temperature + humidity. */
    private void _recvClimate(byte[] payload) {
        if (payload.length < 2) return;
        ProtocolOpcodes.TelemClimatePayload c = ProtocolOpcodes.TelemClimatePayload.unpack(payload, 0);
        Log.v("DATA_R", "Climate - temp: " + c.temp_c + " humidity: " + c.humidity_pct);
        STS.applyClimate(c.temp_c, c.humidity_pct);
    }

    /** TELEM_ENABLE - global LED enable (1) / disable (0). */
    private void _recvEnable(byte[] payload) {
        if (payload.length < 1) return;
        ProtocolOpcodes.TelemEnablePayload e = ProtocolOpcodes.TelemEnablePayload.unpack(payload, 0);
        Log.v("DATA_R", "Enable state: " + (e.value != 0));
        STS.applyEnable(e.value != 0);
    }

    /**
     * TELEM_LINK - |rssi| dBm bucket + wifi state. The board buckets the
     * signal so this only arrives when the strength band or wifi state
     * actually changes. Feeds the top-left Arduino/WiFi indicator.
     */
    private void _recvLink(byte[] payload) {
        if (payload.length < 2) return;
        ProtocolOpcodes.TelemLinkPayload l = ProtocolOpcodes.TelemLinkPayload.unpack(payload, 0);
        rssi      = (l.rssi_bucket == 0) ? 0 : -l.rssi_bucket;
        wifiState = l.wifi_state;
        Log.v("DATA_R", "Link - rssi: " + rssi + " wifiState: " + wifiState);
        Main.updateWifiSignal(l.rssi_bucket, wifiState);
        Log.i("DATA_R", "LINK rssi [" + rssi + "] wifi [" + wifiState + "]");
    }

    /**
     * KEEPALIVE - ping from the board (every ~10 s). Reply in kind so it
     * knows the app is still here.
     *
     * Called by: _parseFrame() only. Calls DataSend.sendKeepAlive() in reply.
     */
    private void _recvKeepAlive() {
        Log.v("DATA_R", "Keep-alive received, sending response");
        if (DATAs != null) DATAs.sendKeepAlive();
    }

    /** TELEM_FAULTS - fault bitmask; only transitions are worth a console line. */
    private void _recvFaults(byte[] payload) {
        if (payload.length < 2) return;
        ProtocolOpcodes.TelemFaultsPayload fp = ProtocolOpcodes.TelemFaultsPayload.unpack(payload, 0);
        int now = fp.mask;
        int raised  = now & ~faults;
        int cleared = faults & ~now;
        faults = now;
        Log.v("DATA_R", "Faults - now: " + now + " raised: " + raised + " cleared: " + cleared);

        if (raised  != 0) Main._Console(true, "◄◄", "{#R}FAULT{##} [" + _faultNames(raised) + "]");
        if (cleared != 0) Main._Console(true, "◄◄", "{#G}FAULT CLEARED{##} [" + _faultNames(cleared) + "]");
    }

    /** @return comma-joined names for the set bits of a fault mask. Called by: _recvFaults() only. */
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
     * TELEM_SAVE_RESULT - a scheduled EEPROM write finished.
     *
     * A command ack only means "accepted"; settings live in RAM until the
     * delayed write runs, so this is the first point at which a change is
     * safe across a power cut.
     */
    private void _recvSaved(byte[] payload) {
        if (payload.length < 1) return;
        int result = payload[0] & 0xFF;
        Log.v("DATA_R", "EEPROM save result: " + result);
        Main._Console(true, "◄◄", (result == 0)
                ? "SETTINGS SAVED [OK]"
                : "SETTINGS SAVE [{#R}FAILED{##}] code [" + result + "]");
    }

    /**
     * TELEM_DEVICE_ID - the controller's own MQTT device id (12 ASCII hex
     * chars, its WiFi MAC), sent as part of the HELLO burst. Only ever
     * arrives over a working transport (local UDP in practice, since a
     * mismatched cloud topic can't deliver anything until this is learned),
     * so this is how the app bootstraps the cloud-fallback topic instead of
     * sitting on MqttTransport.DEFAULT_DEVICE_ID forever.
     */
    private void _recvDeviceId(byte[] payload) {
        if (payload.length < 12 || Main.MQTT == null) return;
        String deviceId = new String(payload, 0, 12, java.nio.charset.StandardCharsets.US_ASCII);
        Log.v("DATA_R", "Device id: " + deviceId);
        Main.MQTT.setDeviceId(deviceId);
    }

    /**
     * LOG - Arduino sent one Term-console log line.
     * Payload: level(u8) + source(u8) + text(rest, UTF-8) - the "str" field
     * type in protocol_table.json means the generator skips codegen for it
     * (same as any "raw" field), so this is hand-decoded, matching
     * DeviceLink.handleLog() on the _rmk_app side. The frame's own SEQ byte
     * covers what the old ASCII format carried as a separate seq field in
     * the text body.
     *
     * @param seq      Frame sequence id (passed through to Main._ConsoleLog).
     * @param payload  level(1) + source(1) + text(rest).
     *
     * Called by: _parseFrame() only.
     */
    private void _recvLog(int seq, byte[] payload) {
        if (payload.length < 2) return;
        int level = payload[0] & 0xFF;
        int src   = payload[1] & 0xFF;
        String text = new String(payload, 2, payload.length - 2, java.nio.charset.StandardCharsets.UTF_8);
        Log.v("DATA_R", "LOG level: " + level + " src: " + src + " text: " + text);
        Main._ConsoleLog(level, src, seq, text);
        Log.i("DATA_R", "LOG lvl [" + level + "] src [" + src + "]");
    }

    /**
     * TELEM_COLOR_SYNC - compressed binary colour delta. Body is a run of
     * records until payload end:
     *   0x01 FILL  start count r g b        – LEDs [start, start+count) = (r,g,b)
     *   0x02 SETN  count {idx r g b}…       – 'count' scattered pixels
     * Same record encoding as the old ASCII-era 'LK' packet - this is
     * nearly a line-for-line port of the old _recvLedColorBin().
     *
     * @param d  Payload bytes (record stream, no 'L''K' prefix anymore -
     *           the opcode itself identifies this).
     *
     * Called by: internal only - _parseFrame().
     */
    private void _recvColorSync(byte[] d) {
        int len = d.length;
        Log.v("DATA_R", "Processing color sync, length: " + len);
        int i     = 0;
        int total = 0;
        while (i < len) {
            int op = d[i++] & 0xFF;
            if (op == 0x01) {                         // FILL start count r g b
                if (i + 5 > len) break;
                int start = d[i++] & 0xFF;
                int count = d[i++] & 0xFF;
                int r = d[i++] & 0xFF, g = d[i++] & 0xFF, b = d[i++] & 0xFF;
                Log.v("DATA_R", "FILL op - start: " + start + " count: " + count + " RGB: " + r + "," + g + "," + b);
                LED.updateColorBulk(start, count, r, g, b);   // one shot, no per-pixel animator
                total += count;
            } else if (op == 0x02) {                  // SETN count {idx r g b}
                if (i >= len) break;
                int count = d[i++] & 0xFF;
                Log.v("DATA_R", "SETN op - count: " + count);
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
                Log.i("DATA_R", "COLOR SYNC BAD OPCODE [" + op + "] at [" + (i - 1) + "]");
                break;
            }
        }
        Log.i("DATA_R", "COLOR SYNC LEDS [" + total + "] BYTES [" + len + "]");
    }

    /**
     * TELEM_DUAL_COLOR - Arduino returns the saved dual colour.
     * Saves it to the LED dual-colour list and refreshes the UI.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvDualColor(byte[] payload) {
        if (payload.length < 6) {
            Log.i("DATA_R", "DUAL COLOR PAYLOAD TOO SHORT [" + payload.length + "]");
            return;
        }
        ProtocolOpcodes.TelemDualColorPayload d = ProtocolOpcodes.TelemDualColorPayload.unpack(payload, 0);
        Log.v("DATA_R", "Dual color - L: " + d.r1 + "," + d.g1 + "," + d.b1 + " R: " + d.r2 + "," + d.g2 + "," + d.b2);
        LED.addDualColor(d.r1, d.g1, d.b1, d.r2, d.g2, d.b2);
        Log.i("DATA_R", "DUAL COLOR [" + d.r1 + " " + d.g1 + " " + d.b1
                + "] <> [" + d.r2 + " " + d.g2 + " " + d.b2 + "]");
        Main._Console(true, "◄◄",
                "DUAL COLOR [" + d.r1 + "," + d.g1 + "," + d.b1
                + "] -- [" + d.r2 + "," + d.g2 + "," + d.b2 + "]");
    }

    /**
     * TELEM_MAX_BRIGHTNESS - Arduino reports the maximum allowed brightness.
     * Updates the brightness SeekBar maximum in LED.
     */
    private void _recvMaxBrightness(byte[] payload) {
        if (payload.length < 1) return;
        int brightness = payload[0] & 0xFF;
        Log.v("DATA_R", "Max brightness value: " + brightness);
        LED.setMaxBrightness(brightness);
        Log.i("DATA_R", "MAX BRIGHTNESS [" + brightness + "]");
        Main._Console(true, "◄◄", "MAX BRIGHTNESS [{#Y}" + brightness + "{##}]");
    }

    /**
     * TELEM_SETTINGS_FULL - all 50 settings in one push (id = array index,
     * no id byte needed - see protocol_table.json's note on this opcode).
     */
    private void _recvSettingsFull(byte[] payload) {
        Log.v("DATA_R", "Processing full settings, length: " + payload.length);
        StringBuilder validLog = new StringBuilder("SETTINGS ");
        for (int id = 0; id < payload.length; id++) {
            int value = payload[id] & 0xFF;
            _applyOneSetting(id, value, validLog);
        }
        Log.i("DATA_R", validLog.toString());
    }

    /** TELEM_SETTINGS_ONE - a single setting value (e.g. answering SETTINGS_READ_ONE). */
    private void _recvSettingsOne(byte[] payload) {
        if (payload.length < 2) return;
        ProtocolOpcodes.TelemSettingsOnePayload s = ProtocolOpcodes.TelemSettingsOnePayload.unpack(payload, 0);
        StringBuilder log = new StringBuilder("SETTING ");
        _applyOneSetting(s.id, s.value, log);
        Log.i("DATA_R", log.toString());
    }

    /**
     * Shared by _recvSettingsFull()/_recvSettingsOne(): validate, clamp,
     * apply one {id,value} pair to SET, and fire the same downstream LED
     * notifications the ASCII-era _recvSettings() did.
     */
    private void _applyOneSetting(int id, int value, StringBuilder log) {
        if (SET.isValidId(id)) {
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
            if (id == SET.SET_MotionRandomColor) LED.notifyMotionRandomSettingChanged();
            log.append("[").append(id).append(":").append(value).append("] ");
        } else {
            Log.i("DATA_R", "SETTING [" + id + "] NOT FOUND");
        }
    }

    /**
     * TELEM_STATUS - Arduino reports core system status: tv/motion/udpraw
     * (ambilight)/ambient/diffuser_summary, one byte each.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvStatus(byte[] payload) {
        if (payload.length < 5) {
            Log.i("DATA_R", "STATUS PAYLOAD TOO SHORT [" + payload.length + "]");
            return;
        }
        ProtocolOpcodes.TelemStatusPayload s = ProtocolOpcodes.TelemStatusPayload.unpack(payload, 0);
        Log.i("DATA_R", "STATUS [tv=" + s.tv + " motion=" + s.motion + " udpraw=" + s.udpraw
                + " ambient=" + s.ambient + " dif=" + s.diffuser_summary + "]");
        STS.applyStatus(s.tv, s.motion, s.udpraw, s.ambient, s.diffuser_summary);
        LED._refreshHBDualSplit(); // TV on/off can flip HB into/out of dual split
    }

    /**
     * TELEM_PARFUM_REMAINING - Arduino relays the diffuser's parfum-mode
     * remaining minutes (0 = parfum inactive). Stored in STS so the
     * long-press parfum popup opens with the live value.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvParfum(byte[] payload) {
        if (payload.length < 2) return;
        ProtocolOpcodes.TelemParfumRemainingPayload p = ProtocolOpcodes.TelemParfumRemainingPayload.unpack(payload, 0);
        Log.v("DATA_R", "Parfum minutes: " + p.minutes);
        Log.i("DATA_R", "PARFUM [" + p.minutes + " min]");
        STS.applyParfumRemaining(p.minutes);
    }

    /**
     * TELEM_DIFFUSER_USAGE - diffuser usage/refill stats, relayed by the
     * SmartTV: accumulated (mode-weighted) minutes since the last refill,
     * rolling average completed-cycle minutes, refill history count
     * (0-10), and lifetime refill count. Drives the top-left refill badge
     * (icon tint, percent, fill bar) via STS.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvDiffuserUsage(byte[] payload) {
        if (payload.length < 7) {
            Log.i("DATA_R", "DIFFUSER USAGE PAYLOAD TOO SHORT [" + payload.length + "]");
            return;
        }
        ProtocolOpcodes.TelemDiffuserUsagePayload u = ProtocolOpcodes.TelemDiffuserUsagePayload.unpack(payload, 0);
        Log.i("DATA_R", "DIFFUSER USAGE [accum=" + u.accum_min + "min avg=" + u.avg_min
                + "min refills=" + u.refill_count + "/10 total=" + u.lifetime_refills + "]");
        STS.applyDiffuserUsage(u.accum_min, u.avg_min, u.refill_count, u.lifetime_refills);
    }

    /**
     * TELEM_DIFFUSER_STATUS - broader status push (mode/strip/parfum/usage/
     * avg/refills all in one frame). Fans out to the same STS methods the
     * separate TELEM_PARFUM_REMAINING/TELEM_DIFFUSER_USAGE frames feed, so
     * a direct DIFFUSER_STATUS_QUERY reply (see DataSend.sendDiffuserStatus())
     * updates exactly the same UI state either way.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvDiffuserStatus(byte[] payload) {
        if (payload.length < 11) {
            Log.i("DATA_R", "DIFFUSER STATUS PAYLOAD TOO SHORT [" + payload.length + "]");
            return;
        }
        ProtocolOpcodes.TelemDiffuserStatusPayload d = ProtocolOpcodes.TelemDiffuserStatusPayload.unpack(payload, 0);
        Log.i("DATA_R", "DIFFUSER STATUS [mode=" + d.mode + " strip=" + d.strip + " parfum=" + d.parfum_min
                + "min usage=" + d.usage_min + "min avg=" + d.avg_min + "min refills=" + d.refill_count
                + "/10 total=" + d.lifetime_refills + "]");
        STS.applyParfumRemaining(d.parfum_min);
        STS.applyDiffuserUsage(d.usage_min, d.avg_min, d.refill_count, d.lifetime_refills);
    }

    /**
     * TELEM_DIFFUSER_HISTORY - full refill-cycle history, sent only on
     * demand (see DataSend.sendDiffuserHistory()) and never cached by
     * SmartTV, which just relays the diffuser's reply straight through.
     * Payload: count(u8) + up to 10x minutes(u16) - a "raw" field (variable
     * length), hand-decoded here same as every other raw opcode.
     *
     * Called by: _parseFrame() only.
     */
    private void _recvDiffuserHistory(byte[] payload) {
        if (payload.length < 1) return;
        int count = payload[0] & 0xFF;
        int[] minutes = new int[10];
        int pos = 1;
        for (int i = 0; i < 10 && pos + 2 <= payload.length; i++) {
            minutes[i] = ((payload[pos] & 0xFF) << 8) | (payload[pos + 1] & 0xFF);
            pos += 2;
        }
        Log.i("DATA_R", "DIFFUSER HISTORY [" + count + "/10] " + java.util.Arrays.toString(minutes));
        STS.applyDiffuserHistory(count, minutes);
    }

    /**
     * TELEM_LUX - Arduino reports the classified lux level (1..5). The
     * rolling average was dropped. Delegates the animated update to STS.
     */
    private void _recvLux(byte[] payload) {
        if (payload.length < 1) {
            Log.i("DATA_R", "LUX PAYLOAD TOO SHORT [" + payload.length + "]");
            return;
        }
        int lux = payload[0] & 0xFF;
        Log.v("DATA_R", "Lux level: " + lux);
        STS.applyLux(lux);
    }

    // ========================================================
    //  Connection status management
    // ========================================================

    /**
     * Live-status flavour: CLOUD MODE when the controller is reached over MQTT
     * (no local WiFi), otherwise the local Connected (IP:port) label. Call on
     * every liveness confirmation so the top label tracks the active transport.
     *
     * Called by: internal _parseFrame() (every recognized local-UDP or
     * cloud frame) and MqttTransport.connect()'s success callback (broker
     * session just came up - may still resolve to CloudNoReply below).
     */
    public void _confirmLive() {
        Log.v("DATA_R", "Confirming live status");
        Main._ClearBootLoading();   // first real confirmation - stop waiting on the 10s timeout
        Main._CancelTransportFallback(); // any transport answered - stop the UDP->MQTT fallback timer
        // NOTE: this also fires right after the MQTT SESSION connects (see
        // MqttTransport.connect()), before the board has said anything -
        // isMqttArduinoAlive() (not just _MqttConnected()) is what keeps
        // that case from claiming "CLOUD MODE" with nothing on the other end.
        if (Main._UDP_Available())         _setStatus(Status.Connected);
        else if (isMqttArduinoAlive())      _setStatus(Status.Cloud);
        else                                _setStatus(Status.CloudNoReply);
    }

    /**
     * Called by: _confirmLive() and the timeoutRunnable watchdog (internal),
     * MainActivity._refreshTransportStatus() (periodic supervisor tick and
     * onResume()), and MqttTransport's connectionLost() callback (drops the
     * CLOUD label if the broker session dies while off-WiFi).
     */
    public void _setStatus(Status status) {
        if (status == currentStatus) return;   // no change
        Log.v("DATA_R", "Status changing from " + currentStatus + " to " + status);
        currentStatus = status;

        switch (status) {
            case NoWifi:
                Main.TEXT_ConnectInfo.setText("~~ NO WIFI ~~");
                Main._Console(true, "--", "{#R}NO WIFI{##}");
                break;
            case Connected:
                Main.TEXT_ConnectInfo.setText(
                        "~~ " + DataSend.ARDUINO_IP + ":" + DataSend.ARDUINO_PORT + " ~~");
                Main._Console(true, "--",
                        "CONNECT {#G}" + DataSend.ARDUINO_IP
                        + "{##}:{#Y}" + DataSend.ARDUINO_PORT + "{##}");
                break;
            case Cloud:
                // Reached over the HiveMQ cloud link (local UDP isn't the active transport).
                Main.TEXT_ConnectInfo.setText("~~ CLOUD MODE ~~");
                Main._Console(true, "☁", "{#G}CLOUD MODE{##}");
                break;
            case CloudNoReply:
                // Broker session is up (phone reached HiveMQ), but the board
                // itself has not actually replied over it - distinct from a
                // genuine "CLOUD MODE" so this doesn't read as connected.
                Main.TEXT_ConnectInfo.setText("~~ CLOUD - NO REPLY ~~");
                Main._Console(true, "☁", "{#R}CLOUD - NO REPLY{##}");
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

        // Re-apply the right effect for the new status - neon flicker while
        // genuinely reachable (Connected/Cloud), sliding red scanline
        // otherwise. Always stop both first: switching straight from one
        // active effect to the other (e.g. Connected -> NoWifi) must not
        // leave the old animator running underneath the new one.
        Main._NeonFlickerOFF(Main.TEXT_ConnectInfo);
        Main._ScanlineOFF();
        boolean reachable = (status == Status.Connected || status == Status.Cloud);
        if (reachable) {
            Main._NeonFlickerON(Main.TEXT_ConnectInfo);
        } else {
            Main._ScanlineON();
        }
    }
}
