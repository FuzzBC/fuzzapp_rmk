package com.fuzz.colors;

/*
 * ============================================================
 *  TELNET – diffuser telnet console client only
 * ============================================================
 *  Owns the TelnetLayout page: single terminal pane for Diffuser.
 *      BOTTOM – Diffuser    (D1 Mini,           TELNET port 23)
 *
 *  Lifecycle:
 *      1.  Constructed once by Main._Init_Global() with the
 *          RecyclerView + status label of TelnetLayout.
 *      2.  setEnabled(true)  – arms the client. No connection is
 *          made yet: target IP is static (192.168.1.203).
 *      3.  setEnabled(false) – closes socket, stops thread.
 *
 *  Reader thread: connect → stream lines into pane →
 *  on drop, retry every RECONNECT_MS while still enabled.
 *
 *  References used here (short aliases):
 *      Main = MainActivity
 * ============================================================
 */

import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

public class TelnetConsole {

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

    /** Pane index – Diffuser only. */
    static final int PANE_DIF = 0;

    // --------------------------------------------------------
    // References
    // --------------------------------------------------------
    /** Back-reference to Main for runOnUiThread + console lists. */
    private final MainActivity Main;

    /** Diffuser pane line list, adapter, status label. */
    private final RecyclerView   rv;
    private final ConsoleAdapter adapter;
    private final TextView       status;

    // --------------------------------------------------------
    // State
    // --------------------------------------------------------
    /** Static diffuser IP address. */
    private static final String DIFFUSER_IP = "192.168.1.203";

    /** Master switch - saved/restored by Main (FuZz_Telnet prefs). */
    private volatile boolean enabled = false;

    /** Reader thread + live socket (socket kept for hard close). */
    private Thread thread;
    private Socket socket;

    /**
     * Connect generation. Bumped on every disable so a
     * stale reader thread that wakes from a blocking read knows to die
     * instead of touching the UI or reconnecting.
     */
    private int generation;

    // --------------------------------------------------------
    // Constructor
    // --------------------------------------------------------
    /**
     * @param main      The running MainActivity instance. Must not be null.
     * @param rvDif     Diffuser pane RecyclerView.
     * @param statusDif Diffuser pane status label.
     */
    public TelnetConsole(MainActivity main,
                         RecyclerView rvDif, TextView statusDif) {
        this.Main  = main;
        this.rv    = rvDif;
        this.status = statusDif;

        adapter = new ConsoleAdapter(R.layout.item_telnet_line);
        rv.setAdapter(adapter);
        rv.setItemAnimator(null); // high-frequency log lines - skip animations
        _setStatus("OFF", 0xFF666666);
    }

    // --------------------------------------------------------
    // Public API
    // --------------------------------------------------------
    /** @return true while the telnet console feature is switched on. */
    public boolean isEnabled() {
        return enabled;
    }

    /**
     * Master on/off. ON arms the diffuser pane (connects to static IP).
     * OFF closes socket and stops the reader thread.
     *
     * @param on  New state.
     */
    public void setEnabled(boolean on) {
        if (enabled == on) return;
        enabled = on;

        if (on) {
            _setStatus("CONNECT…", 0xFFB8B800);
            _startPane();
        } else {
            _stopPane();
            _setStatus("OFF", 0xFF666666);
        }
    }

    /** Wipes the diffuser terminal pane. */
    public void clear() {
        adapter.clear();
    }

    // --------------------------------------------------------
    // Internals
    // --------------------------------------------------------
    /** Spawn the connect/read/retry loop for diffuser pane. */
    private void _startPane() {
        final int myGen = ++generation;

        thread = new Thread(() -> {
            while (enabled && myGen == generation) {
                Socket s = new Socket();
                try {
                    _postStatus(myGen, "CONNECT…", 0xFFB8B800);
                    s.connect(new InetSocketAddress(DIFFUSER_IP, TELNET_PORT), CONNECT_MS);
                    socket = s;
                    _postStatus(myGen, DIFFUSER_IP, 0xFF39D353);

                    // Diffuser firmware prints UTF-8 - decoding as Latin-1 mangles it.
                    BufferedReader in = new BufferedReader(new InputStreamReader(
                            s.getInputStream(), StandardCharsets.UTF_8));

                    String line;
                    while (enabled && myGen == generation
                            && (line = in.readLine()) != null) {
                        final String msg = _sanitize(line);
                        Main.runOnUiThread(() -> {
                            if (myGen != generation) return; // stale
                            _appendLine(msg);
                        });
                    }
                } catch (Exception ignored) {
                    // connect refused / timeout / stream drop - fall to retry
                } finally {
                    try { s.close(); } catch (Exception ignored) { }
                    if (socket == s) socket = null;
                }

                if (!enabled || myGen != generation) break;
                _postStatus(myGen, "RETRY…", 0xFFFF5F56);
                try { Thread.sleep(RECONNECT_MS); } catch (InterruptedException e) { break; }
            }
        }, "Telnet-DIF");
        thread.start();
    }

    /**
     * Invalidate the reader and close the session politely: a 'Q'
     * quit byte is written first so the firmware drops the client itself
     * (see _closeAsync), instead of leaving a half-open slot behind until
     * its next connected() poll.
     */
    private void _stopPane() {
        generation++;
        Socket s = socket;
        socket = null;
        if (s != null) _closeAsync(s);
        if (thread != null) {
            thread.interrupt();
            thread = null;
        }
    }

    /**
     * Off-thread graceful close: write QUIT_CMD, give the device a moment
     * to act on it, then hard-close. Never runs on the UI thread - socket
     * I/O there throws NetworkOnMainThreadException, and setEnabled(false)
     * is called straight from a click listener.
     *
     * The socket is closed no matter what: if the write fails (link
     * already dropped) the finally block still tears it down, which also
     * unblocks the reader thread's pending readLine().
     *
     * @param s  Socket to shut down. May already be closed.
     */
    private void _closeAsync(final Socket s) {
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
        }, "Telnet-close").start();
    }

    /**
     * Drop CR and every other control byte (telnet IAC negotiation, NUL
     * padding, stray escapes) so a malformed line can't inject boxes or
     * blank glyphs into the terminal. Tabs are kept for column alignment.
     *
     * @param line  One raw line straight off the socket.
     * @return Printable-only line.
     */
    private static String _sanitize(String line) {
        StringBuilder out = new StringBuilder(line.length());
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c == '\t' || c >= 0x20) out.append(c);
        }
        return out.toString();
    }

    /** UI-thread append + bottom autoscroll for diffuser pane. */
    private void _appendLine(String msg) {
        if (adapter.appendLine(msg)) {
            adapter.notifyItemRemoved(0); // dropped oldest line (MAX_LINES cap)
        }
        int lastPos = adapter.getItemCount() - 1;
        adapter.notifyItemInserted(lastPos);
        rv.scrollToPosition(lastPos);
    }

    /** Post a status-chip update from a worker thread (generation-guarded). */
    private void _postStatus(final int myGen, final String text, final int color) {
        Main.runOnUiThread(() -> {
            if (myGen != generation) return; // stale
            _setStatus(text, color);
        });
    }

    /** UI-thread status-chip setter. */
    private void _setStatus(String text, int color) {
        if (status == null) return;
        status.setText(text);
        status.setTextColor(color);
    }
}
