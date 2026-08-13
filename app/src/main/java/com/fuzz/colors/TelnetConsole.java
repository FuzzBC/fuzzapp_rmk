package com.fuzz.colors;

/*
 * ============================================================
 *  TELNET – SmartTV + Diffuser telnet console clients
 * ============================================================
 *  Owns the TelnetLayout bubble: two independent terminal panes,
 *  SmartTV on top, Diffuser on the bottom - each one only visible while
 *  ITS OWN telnet link is enabled (see MainActivity._UpdateTelnetPane
 *  Visibility()), so a single-enabled pane fills the whole window and
 *  both together split it evenly. This restores what the layout's own
 *  long-standing comment already called for ("Split top/bottom: APP
 *  (SmartTV R4) over DIFFUSER") but was never actually built - only the
 *  Diffuser pane existed, even though the "TV TELNET" debug-grid toggle
 *  has always been able to arm the SmartTV's own telnet server
 *  (SET_TELNET_ENABLE) with nowhere in the app to actually view it.
 *
 *  Lifecycle (per pane, independent):
 *      1.  Constructed once by Main._Init_Global() with each pane's
 *          RecyclerView + status label.
 *      2.  setDiffuserEnabled(true) / setSmartTvEnabled(true) - arms
 *          that pane's client. Target IPs are the same fixed addresses
 *          the rest of the app already uses (DataSend.ARDUINO_IP for
 *          the SmartTV, the Diffuser's own static IP).
 *      3.  setXxxEnabled(false) - closes that pane's socket, stops its
 *          thread. The other pane is unaffected either way.
 *
 *  Reader thread (one per pane): connect -> stream lines into pane ->
 *  on drop, retry every RECONNECT_MS while still enabled.
 *
 *  References used here (short aliases):
 *      Main = MainActivity
 * ============================================================
 */

import android.util.Log;
import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class TelnetConsole {

    private static final String TAG = "TELNET_CONSOLE";

    // --------------------------------------------------------
    // Constants
    // --------------------------------------------------------
    /** Telnet port on both devices - matches firmware TELNET_PORT. */
    private static final int TELNET_PORT   = 23;

    /** Socket connect timeout, ms. */
    private static final int CONNECT_MS    = 3000;

    /** Delay between reconnect attempts while enabled, ms. */
    private static final int RECONNECT_MS  = 3000;

    /**
     * Sent right before the socket is torn down so the device closes the
     * session on its own side. Both firmwares treat 'Q' as quit; the
     * trailing CRLF terminates the line for the diffuser's parser.
     */
    private static final String QUIT_CMD   = "Q\r\n";

    /** Grace period after QUIT_CMD before the hard close, ms. */
    private static final int QUIT_GRACE_MS = 120;

    /** Static diffuser IP address (its own DHCP reservation - unlike the
     * SmartTV, this one doesn't already have a shared constant elsewhere). */
    private static final String DIFFUSER_IP = "192.168.1.203";

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /** Back-reference to Main for runOnUiThread + console lists. */
    private final MainActivity Main;

    private final Session smarttv;
    private final Session diffuser;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main         The running MainActivity instance. Must not be null.
     * @param rvSmartTv    SmartTV pane RecyclerView.
     * @param statusSmartTv SmartTV pane status label.
     * @param rvDif        Diffuser pane RecyclerView.
     * @param statusDif    Diffuser pane status label.
     */
    public TelnetConsole(MainActivity main,
                          RecyclerView rvSmartTv, TextView statusSmartTv,
                          RecyclerView rvDif, TextView statusDif) {
        this.Main = main;
        smarttv  = new Session("SMARTTV", DataSend.ARDUINO_IP, rvSmartTv, statusSmartTv);
        diffuser = new Session("DIFFUSER", DIFFUSER_IP, rvDif, statusDif);
    }

    // --------------------------------------------------------
    // Public API
    // --------------------------------------------------------
    /** @return true while the Diffuser telnet pane is switched on. */
    public boolean isEnabled() {
        return diffuser.enabled;
    }

    /** @return true while the SmartTV telnet pane is switched on. */
    public boolean isSmartTvEnabled() {
        return smarttv.enabled;
    }

    /** Master on/off for the Diffuser pane. See Session#setEnabled(). */
    public void setEnabled(boolean on) {
        diffuser.setEnabled(on);
    }

    /** Master on/off for the SmartTV pane. See Session#setEnabled(). */
    public void setSmartTvEnabled(boolean on) {
        smarttv.setEnabled(on);
    }

    /** Wipes the Diffuser terminal pane. */
    public void clear() {
        diffuser.adapter.clear();
    }

    /** Wipes the SmartTV terminal pane. */
    public void clearSmartTv() {
        smarttv.adapter.clear();
    }

    // --------------------------------------------------------
    // Per-target connection state + reader loop
    // --------------------------------------------------------
    /**
     * One independent telnet client: its own socket, reader thread,
     * generation counter, and UI targets. Non-static inner class so it
     * can reach Main.runOnUiThread() directly, same as the single-pane
     * version this replaces.
     */
    private final class Session {
        final String tag;
        final String ip;
        final RecyclerView   rv;
        final ConsoleAdapter adapter;
        final TextView       status;

        /** Master switch - set by Main from persisted prefs. */
        volatile boolean enabled = false;

        /** Reader thread + live socket (socket kept for hard close). */
        Thread thread;
        Socket socket;

        /**
         * Connect generation. Bumped on every disable so a stale reader
         * thread that wakes from a blocking read knows to die instead of
         * touching the UI or reconnecting.
         */
        int generation;

        Session(String tag, String ip, RecyclerView rv, TextView status) {
            this.tag = tag;
            this.ip = ip;
            this.rv = rv;
            this.status = status;
            adapter = new ConsoleAdapter(R.layout.item_telnet_line);
            rv.setAdapter(adapter);
            rv.setItemAnimator(null); // high-frequency log lines - skip animations
            setStatus("OFF", ThemeManager.getColor(Main, R.color.telnet_status_idle));
        }

        void setEnabled(boolean on) {
            if (enabled == on) return;
            Log.d(TAG, tag + " setEnabled(" + on + ")");
            enabled = on;

            if (on) {
                setStatus("CONNECT…", ThemeManager.getColor(Main, R.color.telnet_status_connecting));
                startPane();
            } else {
                stopPane();
                setStatus("OFF", ThemeManager.getColor(Main, R.color.telnet_status_idle));
            }
        }

        /** Spawn the connect/read/retry loop for this pane. */
        void startPane() {
            final int myGen = ++generation;

            thread = new Thread(() -> {
                while (enabled && myGen == generation) {
                    Socket s = new Socket();
                    try {
                        Log.d(TAG, tag + " gen=" + myGen + " connecting to " + ip + ":" + TELNET_PORT);
                        postStatus(myGen, "CONNECT…", ThemeManager.getColor(Main, R.color.telnet_status_connecting));
                        s.connect(new InetSocketAddress(ip, TELNET_PORT), CONNECT_MS);
                        socket = s;
                        Log.d(TAG, tag + " gen=" + myGen + " connected");
                        postStatus(myGen, ip, ThemeManager.getColor(Main, R.color.telnet_status_connected));

                        // Both firmwares print UTF-8 - decoding as Latin-1 mangles it.
                        BufferedReader in = new BufferedReader(new InputStreamReader(
                                s.getInputStream(), StandardCharsets.UTF_8));

                        String line;
                        while (enabled && myGen == generation
                                && (line = in.readLine()) != null) {
                            final String msg = sanitize(line);
                            Log.v(TAG, tag + " gen=" + myGen + " rx: " + msg);
                            Main.runOnUiThread(() -> {
                                if (myGen != generation) return; // stale
                                appendLine(msg);
                            });
                        }
                        Log.d(TAG, tag + " gen=" + myGen + " stream ended (readLine returned null or disabled)");
                    } catch (Exception e) {
                        // connect refused / timeout / stream drop - fall to retry
                        Log.d(TAG, tag + " gen=" + myGen + " " + e.getClass().getSimpleName() + ": " + e.getMessage());
                    } finally {
                        try { s.close(); } catch (Exception ignored) { }
                        if (socket == s) socket = null;
                    }

                    if (!enabled || myGen != generation) break;
                    Log.d(TAG, tag + " gen=" + myGen + " retrying in " + RECONNECT_MS + "ms");
                    postStatus(myGen, "RETRY…", ThemeManager.getColor(Main, R.color.telnet_status_error));
                    try { Thread.sleep(RECONNECT_MS); } catch (InterruptedException e) { break; }
                }
            }, "Telnet-" + tag);
            thread.start();
        }

        /**
         * Invalidate the reader and close the session politely: a 'Q'
         * quit byte is written first so the firmware drops the client
         * itself (see closeAsync()), instead of leaving a half-open slot
         * behind until its next connected() poll.
         */
        void stopPane() {
            generation++;
            Socket s = socket;
            socket = null;
            if (s != null) closeAsync(s);
            if (thread != null) {
                thread.interrupt();
                thread = null;
            }
        }

        /**
         * Off-thread graceful close: write QUIT_CMD, give the device a
         * moment to act on it, then hard-close. Never runs on the UI
         * thread - socket I/O there throws NetworkOnMainThreadException,
         * and setEnabled(false) is called straight from a click listener.
         *
         * The socket is closed no matter what: if the write fails (link
         * already dropped) the finally block still tears it down, which
         * also unblocks the reader thread's pending readLine().
         *
         * @param s  Socket to shut down. May already be closed.
         */
        void closeAsync(final Socket s) {
            new Thread(() -> {
                try {
                    s.getOutputStream().write(QUIT_CMD.getBytes(StandardCharsets.US_ASCII));
                    s.getOutputStream().flush();
                    Thread.sleep(QUIT_GRACE_MS); // let the firmware run its own stop()
                } catch (Exception ignored) {
                    // link already gone - the close below is all that's left
                } finally {
                    try { s.close(); } catch (Exception ignored) { }
                }
            }, "Telnet-" + tag + "-close").start();
        }

        /** UI-thread append + bottom autoscroll for this pane. */
        void appendLine(String msg) {
            if (adapter.appendLine(msg)) {
                adapter.notifyItemRemoved(0); // dropped oldest line (MAX_LINES cap)
            }
            int lastPos = adapter.getItemCount() - 1;
            adapter.notifyItemInserted(lastPos);
            rv.scrollToPosition(lastPos);
        }

        /** Post a status-chip update from a worker thread (generation-guarded). */
        void postStatus(final int myGen, final String text, final int color) {
            Main.runOnUiThread(() -> {
                if (myGen != generation) return; // stale
                setStatus(text, color);
            });
        }

        /** UI-thread status-chip setter. */
        void setStatus(String text, int color) {
            if (status == null) return;
            status.setText(text);
            status.setTextColor(color);
        }
    }

    /**
     * Drop CR and every other control byte (telnet IAC negotiation, NUL
     * padding, stray escapes) so a malformed line can't inject boxes or
     * blank glyphs into the terminal. Tabs are kept for column alignment.
     *
     * @param line  One raw line straight off the socket.
     * @return Printable-only line.
     */
    private static String sanitize(String line) {
        StringBuilder out = new StringBuilder(line.length());
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c == '\t' || c >= 0x20) out.append(c);
        }
        return out.toString();
    }
}
