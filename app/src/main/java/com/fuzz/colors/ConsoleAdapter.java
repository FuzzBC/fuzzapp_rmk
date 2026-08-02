package com.fuzz.colors;

/*
 * ============================================================
 *  CONSOLEADAPTER – Term console + telnet pane backing store
 * ============================================================
 *  Renders the framed log envelope the Arduino now sends:
 *
 *      '*' + level(1 hex) + source(2 hex) + seq(2 hex) + text
 *
 *  Level decides the gutter colour and whether the line survives the
 *  filter row; source decides the tag; every [bracketed] token in the
 *  text becomes a ChipSpan value pill. The firmware no longer sends
 *  {#X} colour markup - that shorthand is still honoured, but only for
 *  app-side lines produced by Main._Console().
 *
 *  Each line is parsed and spanned ONCE, at append time, and cached as
 *  a Spannable; onBindViewHolder() only ever calls setText().
 *
 *  Two append paths, deliberately different:
 *      appendLog()   Term console. Self-notifies, honours the filter,
 *                    returns the row to scroll to (-1 when hidden).
 *      appendLine()  Telnet panes. No filter, no notify - TelnetConsole
 *                    keeps driving the notifications itself.
 * ============================================================
 */

import android.graphics.Typeface;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.text.SimpleDateFormat;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class ConsoleAdapter extends RecyclerView.Adapter<ConsoleAdapter.VH> {

    // --------------------------------------------------------
    // Wire envelope – mirrors DEF.h APP_LOG_* / APP_SRC_*
    // --------------------------------------------------------
    /** Envelope header chars after '*': level(1) + source(2) + seq(2). */
    static final int LOG_HDR = 5;

    /** Severity codes – first hex digit of the envelope. */
    static final int LVL_ERR = 0, LVL_WRN = 1, LVL_INF = 2, LVL_DBG = 3,
                     LVL_SEC = 4, LVL_GAP = 5;

    /** Synthetic level for app-side lines (never arrives over the wire). */
    static final int LVL_LOCAL = 6;

    /** Synthetic source for app-side lines – suppresses the tag. */
    static final int SRC_LOCAL = -1;

    /**
     * Source names, indexed by the envelope's 2-hex source id. Kept to four
     * characters so the tag column stays narrow on a phone - the widest tag
     * rendered is "[TASK]".
     */
    static final String[] SRC_NAMES = {
            "SYS", "NET", "RTC", "EE", "APP", "LED", "TV", "MOT",
            "LUX", "DIF", "RAW", "AMB", "BME", "TASK", "HB", "TEST"
    };

    // --------------------------------------------------------
    // Terminal palette – fixed, deliberately NOT theme-aware
    // --------------------------------------------------------
    /** Gutter bar colour per level (index = LVL_*). */
    private static final int[] LVL_GUTTER = {
            0xFFE24B4A, 0xFFEF9F27, 0xFF378ADD, 0xFF5F5E5A, 0xFF1D9E75, 0x00000000, 0xFF444441
    };

    /** Body text colour per level (index = LVL_*). */
    private static final int[] LVL_TEXT = {
            0xFFF09595, 0xFFFAC775, 0xFFB4B2A9, 0xFF888780, 0xFF5DCAA5, 0xFF000000, 0xFFB4B2A9
    };

    /** Dim colour for timestamps and structural punctuation. */
    private static final int C_MUTED = 0xFF5F5E5A;

    /** Tag colour per source id – neighbouring sources stay distinguishable. */
    private static final int[] SRC_TINT = {
            0xFF888780, 0xFF85B7EB, 0xFF9FE1CB, 0xFFC0DD97, 0xFF85B7EB, 0xFFF0997B,
            0xFF5DCAA5, 0xFFED93B1, 0xFFEF9F27, 0xFFAFA9EC, 0xFF85B7EB, 0xFFAFA9EC,
            0xFF9FE1CB, 0xFF888780, 0xFFF0997B, 0xFFEF9F27
    };

    /** Chip palette: {background, foreground} per value class. */
    private static final int[] CHIP_NUM  = {0xFF412402, 0xFFFAC775}; // 42, 3.5, -7
    private static final int[] CHIP_ON   = {0xFF173404, 0xFFC0DD97}; // ON, True, OK
    private static final int[] CHIP_OFF  = {0xFF501313, 0xFFF7C1C1}; // OFF, False, none
    private static final int[] CHIP_TEXT = {0xFF04342C, 0xFF9FE1CB}; // STATIC, IPs, times

    /** {#X} shorthand -> ARGB. Still used by app-side Main._Console() lines. */
    private static final Map<String, Integer> TAG_COLORS = new HashMap<>();
    static {
        TAG_COLORS.put("R", 0xFFFF0000);
        TAG_COLORS.put("G", 0xFF00FF00);
        TAG_COLORS.put("B", 0xFF057AFF);
        TAG_COLORS.put("Y", 0xFFFFFF00);
        TAG_COLORS.put("C", 0xFF00FFFF);
        TAG_COLORS.put("M", 0xFFFF00FF);
        TAG_COLORS.put("O", 0xFFFFA500);
        TAG_COLORS.put("P", 0xFF800080);
        TAG_COLORS.put("W", 0xFFFFFFFF);
        TAG_COLORS.put("K", 0xFF000000);
    }

    /**
     * Hard cap on retained lines – bounds memory for long sessions.
     *
     * Raised from 500: a single expanded debug report now runs to several
     * hundred lines on its own (the EEPROM dump and the per-LED colour dumps
     * are the worst), so the old cap could evict the start of a report before
     * you had finished reading the end of it. Applies per adapter, so each
     * telnet pane gets its own allowance.
     */
    private static final int MAX_LINES = 1000;

    /** Arrival-time stamp shown at the head of every log row. */
    private static final SimpleDateFormat CLOCK =
            new SimpleDateFormat("HH:mm:ss", Locale.US);

    /**
     * Column widths in characters. Both columns are monospace and both strings
     * are space-padded to these widths here, so the row layout can measure them
     * with wrap_content and still line up perfectly - no em arithmetic, and no
     * dead space between the clock, the tag and the message.
     */
    private static final int COL_TIME = 8;   // "22:00:01"
    private static final int COL_SRC  = 6;   // widest tag rendered is "[TASK]"

    // --------------------------------------------------------
    // Model
    // --------------------------------------------------------
    /**
     * One console row, already split into its three columns:
     * clock, [source] tag and the message body.
     */
    private static final class Entry {
        final String       time;     // "22:00:01", empty for SEC/GAP rows
        final String       src;      // "[LED]", empty when there is no source
        final int          srcColor; // tag tint, ARGB
        final CharSequence msg;      // body, spanned + chipped at append time
        final int          level;    // LVL_*, drives the gutter colour

        Entry(String time, String src, int srcColor, CharSequence msg, int level) {
            this.time = time; this.src = src; this.srcColor = srcColor;
            this.msg  = msg;  this.level = level;
        }
    }

    /** Every line held, oldest first. */
    private final ArrayList<Entry> lines = new ArrayList<>();

    /** Row layout inflated per line – item_console_line by default. */
    private final int lineLayoutRes;

    /** Display density, handed to every ChipSpan. Resolved on first inflate. */
    private float density = 1f;

    // --------------------------------------------------------
    // Constructors
    // --------------------------------------------------------
    /** Default console adapter (Term tab look). */
    ConsoleAdapter() {
        this(R.layout.item_console_line);
    }

    /**
     * Adapter with a custom row layout – must contain a TextView with id
     * console_line_text (e.g. item_telnet_line for the telnet panes).
     *
     * @param lineLayoutRes Row layout resource.
     */
    ConsoleAdapter(int lineLayoutRes) {
        this.lineLayoutRes = lineLayoutRes;
    }

    // --------------------------------------------------------
    // Public API – Term console
    // --------------------------------------------------------
    /**
     * Appends one framed log line and notifies the RecyclerView itself.
     *
     * @param level   APP_LogLevel from the envelope, or LVL_LOCAL.
     * @param source  APP_LogSource from the envelope, or SRC_LOCAL.
     * @param tag     Prefix for app-side lines (e.g. "\u25c4\u25c4"); null for wire lines.
     * @param raw     Message body. {#X} shorthand honoured; [tokens] become chips.
     * @return Row index to scroll to, or -1 when the line is filtered out.
     */
    int appendLog(int level, int source, String tag, String raw) {
        lines.add(_build(level, source, tag, raw));

        if (lines.size() > MAX_LINES) {
            lines.remove(0);
            notifyItemRemoved(0);
        }

        int pos = lines.size() - 1;
        notifyItemInserted(pos);
        return pos;
    }

    // --------------------------------------------------------
    // Public API – telnet panes (unchanged contract)
    // --------------------------------------------------------
    /**
     * Appends one raw stream line with no envelope, tag or filtering.
     * Bracketed tokens still become chips - telnet output uses the same
     * [value] convention as the app log.
     *
     * @param rawMessage One full line, already CR/LF-stripped.
     * @return true if the oldest line was dropped to stay under MAX_LINES
     *         (caller should notifyItemRemoved(0) in that case).
     */
    boolean appendLine(String rawMessage) {
        lines.add(_build(LVL_LOCAL, SRC_LOCAL, null, rawMessage));

        boolean trimmed = false;
        if (lines.size() > MAX_LINES) {
            lines.remove(0);
            trimmed = true;
        }
        return trimmed;
    }

    /** Wipes the console. */
    void clear() {
        lines.clear();
        notifyDataSetChanged();
    }

    // --------------------------------------------------------
    // RecyclerView plumbing
    // --------------------------------------------------------
    @Override
    public int getItemCount() {
        return lines.size();
    }

    @NonNull
    @Override
    public VH onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        density = parent.getResources().getDisplayMetrics().density;
        View v = LayoutInflater.from(parent.getContext())
                .inflate(lineLayoutRes, parent, false);
        return new VH(v);
    }

    @Override
    public void onBindViewHolder(@NonNull VH holder, int position) {
        Entry e = lines.get(position);
        holder.text.setText(e.msg);

        // The telnet row layout has none of these - it is one plain column.
        if (holder.time != null) holder.time.setText(e.time);
        if (holder.src  != null) {
            holder.src.setText(e.src);
            holder.src.setTextColor(e.srcColor);
        }
        if (holder.gutter != null) {
            holder.gutter.setBackgroundColor(LVL_GUTTER[_clampLevel(e.level)]);
        }
    }

    static final class VH extends RecyclerView.ViewHolder {
        final TextView time;   // null for item_telnet_line
        final TextView src;    // null for item_telnet_line
        final TextView text;
        final View     gutter; // null for item_telnet_line

        VH(@NonNull View itemView) {
            super(itemView);
            time   = itemView.findViewById(R.id.console_line_time);
            src    = itemView.findViewById(R.id.console_line_src);
            text   = itemView.findViewById(R.id.console_line_text);
            gutter = itemView.findViewById(R.id.console_line_gutter);
        }
    }

    // --------------------------------------------------------
    // Internals
    // --------------------------------------------------------
    /** Guards the palette lookups against a malformed envelope. */
    private static int _clampLevel(int level) {
        return (level < 0 || level >= LVL_GUTTER.length) ? LVL_LOCAL : level;
    }

    /**
     * Splits one incoming line into its three columns.
     *
     * Layout is  <clock> [SRC] <message> ; SEC rows carry only a title and
     * GAP rows carry nothing, so both leave the first two columns empty and
     * stay visually aligned with the body text above them.
     *
     * @param level   LVL_* code.
     * @param source  Source id, or SRC_LOCAL for an app-side line.
     * @param tag     Prefix used in the tag column when there is no source.
     * @param raw     Message body; {#X} honoured, [tokens] become chips.
     */
    private Entry _build(int level, int source, String tag, String raw) {
        // GAP: blank spacer row.
        if (level == LVL_GAP) return new Entry("", "", C_MUTED, " ", level);
        // (SEC below pads both columns with blanks so its title still starts
        //  on the message column, in line with the rows around it.)

        // SEC: divider heading - no clock, no tag, it labels what follows.
        if (level == LVL_SEC) {
            SpannableStringBuilder title = new SpannableStringBuilder();
            title.append("\u2500\u2500 ").append(_plain(raw));
            title.setSpan(new ForegroundColorSpan(LVL_TEXT[LVL_SEC]), 0, title.length(),
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            title.setSpan(new StyleSpan(Typeface.BOLD), 0, title.length(),
                    Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            return new Entry(_pad("", COL_TIME), _pad("", COL_SRC), C_MUTED, title, level);
        }

        // Tag column: firmware source, else the caller's prefix, else nothing.
        String tagText;
        int    tagColor;
        if (source >= 0 && source < SRC_NAMES.length) {
            tagText  = "[" + SRC_NAMES[source] + "]";
            tagColor = SRC_TINT[source];
        } else if (tag != null && !tag.isEmpty()) {
            tagText  = _plain(tag).trim();
            tagColor = LVL_TEXT[_clampLevel(level)];
        } else {
            tagText  = "";
            tagColor = C_MUTED;
        }

        SpannableStringBuilder body = new SpannableStringBuilder();
        _appendSpanned(body, raw, LVL_TEXT[_clampLevel(level)]);

        // Arrival clock - local time, the Arduino stamps nothing per line.
        return new Entry(_pad(CLOCK.format(new Date()), COL_TIME),
                         _pad(tagText, COL_SRC), tagColor, body, level);
    }

    /**
     * Fits a column string to an exact character count: truncated if it is too
     * long, right-padded with spaces if too short.
     *
     * @param s      Column text.
     * @param width  Target width in characters.
     */
    private static String _pad(String s, int width) {
        if (s.length() >= width) return s.substring(0, width);
        StringBuilder b = new StringBuilder(s);
        while (b.length() < width) b.append(' ');
        return b.toString();
    }

    /** Strips {#X} markup and returns the plain text, discarding the spans. */
    private static String _plain(String raw) {
        return _stripColorTags(raw, new ArrayList<>());
    }

    /**
     * Appends {@code raw} to {@code out}: {#X} shorthand becomes colour spans,
     * [bracketed] tokens become ChipSpans, everything else takes baseColor.
     */
    private void _appendSpanned(SpannableStringBuilder out, String raw, int baseColor) {
        List<int[]> nested = new ArrayList<>();          // {relStart, relEnd, colour}
        String plain = _stripColorTags(raw, nested);

        int start = out.length();
        out.append(plain);
        out.setSpan(new ForegroundColorSpan(baseColor), start, out.length(),
                Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);

        // Pass 1 - bracketed tokens. [x] is the firmware convention, {x} the
        // older app-side one; either way the brackets mark a value.
        List<int[]> chips = new ArrayList<>();          // {start, end} in plain
        int i = 0;
        while (i < plain.length()) {
            int open = _nextOpen(plain, i);
            if (open < 0) break;
            char want  = (plain.charAt(open) == '[') ? ']' : '}';
            int  close = plain.indexOf(want, open + 1);
            if (close < 0) break;
            chips.add(new int[]{open, close + 1});
            i = close + 1;
        }

        // Pass 2 - a {#X} run with no brackets around it is a value too: that
        // markup only ever wrapped the thing worth reading. Promote it to a
        // chip unless a bracket already claimed those characters.
        for (int[] span : nested) {
            if (_overlaps(chips, span[0], span[1])) {
                out.setSpan(new ForegroundColorSpan(span[2]), start + span[0], start + span[1],
                        Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
            } else {
                chips.add(new int[]{span[0], span[1], span[2]});
            }
        }

        for (int[] c : chips) {
            String value = plain.substring(c[0], c[1]);
            if (value.length() > 1 && (value.charAt(0) == '[' || value.charAt(0) == '{')) {
                value = value.substring(1, value.length() - 1);
            }
            value = value.trim();
            if (value.isEmpty()) continue;

            int[] pal = (c.length > 2) ? _chipPalette(c[2], value) : _chipColors(value);
            out.setSpan(new ChipSpan(value, pal[0], pal[1], density),
                    start + c[0], start + c[1], Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        }
    }

    /** @return true if [from,to) touches any range already claimed as a chip. */
    private static boolean _overlaps(List<int[]> ranges, int from, int to) {
        for (int[] r : ranges) if (from < r[1] && to > r[0]) return true;
        return false;
    }

    /**
     * Palette for a promoted {#X} run. The author's own colour choice says what
     * the value meant, so honour it and fall back to the classifier otherwise.
     *
     * @param tagColor  ARGB the run was tagged with.
     * @param value     Run text, already trimmed.
     */
    private static int[] _chipPalette(int tagColor, String value) {
        if (tagColor == 0xFFFF0000) return CHIP_OFF;   // {#R}
        if (tagColor == 0xFF00FF00) return CHIP_ON;    // {#G}
        if (tagColor == 0xFFFFFF00 || tagColor == 0xFFFFA500) return CHIP_NUM; // {#Y} {#O}
        return _chipColors(value);
    }

    /** @return index of the next '[' or '{' at or after from, else -1. */
    private static int _nextOpen(String s, int from) {
        int a = s.indexOf('[', from);
        int b = s.indexOf('{', from);
        if (a < 0) return b;
        if (b < 0) return a;
        return Math.min(a, b);
    }

    /**
     * Classifies a chip value and returns its {background, foreground} pair.
     * An "r,g,b" triplet paints itself - the chip is filled with the actual
     * colour and the label flips to black or white for contrast.
     */
    private static int[] _chipColors(String v) {
        int[] rgb = _parseRgb(v);
        if (rgb != null) {
            int bg   = 0xFF000000 | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
            int luma = (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000;
            return new int[]{bg, luma > 140 ? 0xFF111512 : 0xFFE8E8E4};
        }
        if (_isNumeric(v)) return CHIP_NUM;

        if (v.equalsIgnoreCase("ON")   || v.equalsIgnoreCase("True")
         || v.equalsIgnoreCase("OK")   || v.equalsIgnoreCase("ACTIVE")) return CHIP_ON;

        if (v.equalsIgnoreCase("OFF")   || v.equalsIgnoreCase("False")
         || v.equalsIgnoreCase("none")  || v.equalsIgnoreCase("UNKNOWN")
         || v.equalsIgnoreCase("NO_RESPONSE") || v.equalsIgnoreCase("NOT FOUND")
         || v.equalsIgnoreCase("ERROR") || v.equalsIgnoreCase("FAIL")
         || v.equalsIgnoreCase("LOST")  || v.equalsIgnoreCase("INVALID")) return CHIP_OFF;

        return CHIP_TEXT;
    }

    /** @return true for plain integers/decimals, with optional sign. */
    private static boolean _isNumeric(String v) {
        boolean digit = false;
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c >= '0' && c <= '9') { digit = true; continue; }
            if (c == '.' || c == '-' || (c == ' ' && i > 0)) continue;
            return false;
        }
        return digit;
    }

    /** @return {r,g,b} for an "r,g,b" triplet of 0-255 values, else null. */
    private static int[] _parseRgb(String v) {
        int c1 = v.indexOf(','), c2 = v.lastIndexOf(',');
        if (c1 <= 0 || c2 <= c1 || c2 >= v.length() - 1) return null;
        try {
            int r = Integer.parseInt(v.substring(0, c1).trim());
            int g = Integer.parseInt(v.substring(c1 + 1, c2).trim());
            int b = Integer.parseInt(v.substring(c2 + 1).trim());
            if ((r | g | b) < 0 || r > 255 || g > 255 || b > 255) return null;
            return new int[]{r, g, b};
        } catch (NumberFormatException e) {
            return null;
        }
    }

    /**
     * Strips {#X}/{##} shorthand out of raw, returning the plain text and
     * collecting the colour ranges (relative to that plain text) that were
     * tagged. Single left-to-right pass, no regex / HTML parsing.
     */
    private static String _stripColorTags(String raw, List<int[]> spansOut) {
        StringBuilder plain = new StringBuilder();
        ArrayDeque<int[]> openStack = new ArrayDeque<>(); // {plainStartIdx, colour}
        int i = 0, n = raw.length();

        while (i < n) {
            int brace = raw.indexOf('{', i);
            if (brace < 0) { plain.append(raw, i, n); break; }
            plain.append(raw, i, brace);

            int close = raw.indexOf('}', brace);
            if (close < 0) { plain.append(raw, brace, n); break; }

            String token = raw.substring(brace + 1, close); // e.g. "#R" or "##"
            if (token.equals("##")) {
                if (!openStack.isEmpty()) {
                    int[] open = openStack.pop();
                    spansOut.add(new int[]{open[0], plain.length(), open[1]});
                }
                i = close + 1;
            } else if (token.length() == 2 && token.charAt(0) == '#'
                    && TAG_COLORS.containsKey(token.substring(1))) {
                openStack.push(new int[]{plain.length(), TAG_COLORS.get(token.substring(1))});
                i = close + 1;
            } else {
                // Not a colour tag - this brace is literal text, e.g. the older
                // "{{#G}ON{##}}" form. Keep it and rescan from the next char, or
                // the '{' gets eaten and its partner '}' is left stranded.
                plain.append('{');
                i = brace + 1;
            }
        }
        // Close any unterminated tags at end-of-string rather than dropping colour.
        while (!openStack.isEmpty()) {
            int[] open = openStack.pop();
            spansOut.add(new int[]{open[0], plain.length(), open[1]});
        }
        return plain.toString();
    }
}
