"""gui.py - Tkinter UI for TestMode.pyw (split command-builder layout).

Left sidebar: every command, grouped by category, one click away.
Right panel: the selected command's full editor - param controls, a live
payload preview, Send, and a response area that renders status/colour/
settings replies richly instead of as raw hex.

Direct sockets (net.py) mean sends are effectively instant; all network I/O
runs on background daemon threads and results are marshalled back to the Tk
main thread via a queue polled on a timer, so the UI never blocks.
"""

import os
import time
import queue
import threading
import tkinter as tk
from tkinter import ttk, colorchooser, messagebox

import net
from core import hexb, find_ack, describe_reply, parse_history_reply
from commands_diffuser import DIFFUSER_COMMANDS
from commands_smarttv import SMARTTV_COMMANDS, TV_SETTINGS

# ---- theme: dark graphite, professional & low-glare ----
BG = '#1b1e24'
BG_SIDEBAR = '#14171c'
BG_ROW_HOVER = '#252a32'
BG_ROW_SELECTED = '#20344a'
BG_PANEL = '#191c22'
BG_CARD = '#20242b'
BG_FIELD = '#262b33'
BG_BTN = '#282d36'
BG_BTN_HOVER = '#343a44'
CELL_BG = '#262b33'
BORDER = '#343841'
BORDER_STRONG = '#464c57'
TEXT = '#e6e4de'
TEXT_SECONDARY = '#b6b3ac'
TEXT_MUTED = '#82807a'
WHITE = '#f4f3ef'
ACCENT_BLUE = '#6fa3d8'
ACCENT_GREEN = '#7cb87c'
ACCENT_RED = '#d98a8a'
ACCENT_AMBER = '#d9b877'
ACCENT_PURPLE = '#a693d1'
BTN_GREEN_BG = '#203a28'
BTN_GREEN_FG = '#8fd39a'
BTN_GREEN_ACTIVE = '#2a4a34'

CHIP_COLORS = {
    'ok': ('#203a28', '#8fd39a'),
    'clamped': ('#3a331e', '#d9b877'),
    'rejected': ('#3a2020', '#d98a8a'),
    'blocked': ('#3a2020', '#d98a8a'),
    'locked': ('#3a331e', '#d9b877'),
    'nowater': ('#3a2020', '#d98a8a'),
    'unsupported': ('#282d36', '#82807a'),
    'timeout': ('#282d36', '#82807a'),
    'error': ('#3a2020', '#d98a8a'),
    'sent': ('#20344a', '#6fa3d8'),
    'none': ('#282d36', '#82807a'),
    'unknown': ('#282d36', '#82807a'),
}

# ack/result banner content: kind -> (icon glyph, headline, one-line meaning).
# Same kind strings as CHIP_COLORS above, so the banner and the small chip
# next to Send always agree on colour for a given result.
ACK_INFO = {
    'ok':          ('✓', 'OK',          'Command accepted.'),
    'clamped':     ('△', 'CLAMPED',     "Accepted, but the value was out of range and got clamped."),
    'rejected':    ('✕', 'REJECTED',    'Command was rejected - nothing changed.'),
    'blocked':     ('⛔', 'BLOCKED',     "Blocked by the device's current state."),
    'locked':      ('⚿', 'LOCKED',      'That setting is locked.'),
    'nowater':     ('⚠', 'NO WATER',    'Diffuser reservoir is empty.'),
    'unsupported': ('–', 'UNSUPPORTED', 'Not supported by this firmware build.'),
    'timeout':     ('…', 'TIMEOUT',     'No reply within the timeout window.'),
    'error':       ('✕', 'ERROR',       'The send itself failed.'),
    'sent':        ('➤', 'SENT',        'Sent - this command has no ack envelope.'),
    'unknown':     ('?', 'NO ACK',      'No matching ack packet came back.'),
    'none':        ('–', '-',           ''),
}

LED_ZONES = [
    ('TV', 0, 30, '#6fa3d8'),
    ('COM', 30, 10, '#7cb87c'),
    ('UCOM', 40, 2, '#a693d1'),
    ('BED', 42, 8, '#c9a06a'),
    ('LAMP', 50, 10, '#d18f68'),
    ('HB', 60, 1, '#cf85a8'),
]

SETTINGS_CAT_COLORS = {
    'TV': '#6fa3d8', 'MOTION': '#7cb87c', 'OTHER': '#82807a', 'HB': '#cf85a8',
    'DIF': '#c9a06a', 'UDPRAW': '#a693d1', 'RESERVED': '#5a5f68',
}

DIF_MODE_NAMES = ['OFF', 'CONT', '10 SEC', '2H AFTER SLEEP', '4H AFTER SLEEP']

# sec (raw spec['section'] string) -> (sidebar title, accent colour, 3-char badge, icon glyph)
# Icons are plain Unicode geometric shapes (same family as the existing chevrons)
# rather than an icon font or bundled images - renders everywhere Tkinter runs,
# no new assets to ship or load.
CATEGORY_META = {
    'UDP :8439': ('UDP session & control', ACCENT_BLUE, 'UDP', '◆'),
    'Console (Serial + Telnet :23)': ('Serial / Telnet console', ACCENT_PURPLE, 'TTY', '▥'),
    'UDP :8472 - session': ('Session', ACCENT_BLUE, 'SES', '●'),
    'UDP :8472 - test mode (@)': ('Test mode (@)', ACCENT_AMBER, 'TST', '▶'),
    'UDP :8472 - settings (S)': ('Settings (S)', ACCENT_GREEN, 'SET', '▣'),
    'UDP :8472 - ambient mode (A)': ('Ambient mode (A)', ACCENT_PURPLE, 'AMB', '◐'),
    'UDP :8472 - debug (K)': ('Debug (K)', TEXT_MUTED, 'DBG', '▧'),
    'UDP :8472 - diagnostics (!)': ('Diagnostics (!)', ACCENT_GREEN, 'DIAG', '✚'),
    'UDP :8472 - diffuser relay (D)': ('Diffuser relay (D)', ACCENT_AMBER, 'REL', '◆'),
    'UDP :8472 - LED zone (L)': ('LED zone (L)', ACCENT_BLUE, 'LED', '■'),
}


def category_meta(section):
    """Sidebar (title, colour, badge, icon) for a raw section string, with a safe fallback."""
    return CATEGORY_META.get(section, (section, TEXT_MUTED, section[:3].upper(), '▪'))


def describe_lk_packet(data):
    """Return a safe, readable summary of a binary LK LED colour-sync packet."""
    if not data.startswith(b'LK'):
        return None
    if len(data) == 2:
        return 'LK colour sync (empty packet)'

    pos, fills, pixels, records = 2, 0, 0, []
    while pos < len(data):
        op = data[pos]
        if op == 0x01:  # FILL: op, start, count, r, g, b
            if pos + 6 > len(data):
                records.append('truncated FILL')
                break
            start, count, red, green, blue = data[pos + 1:pos + 6]
            fills += 1
            pixels += count
            records.append('fill %d-%d #%02X%02X%02X' % (start, start + count - 1, red, green, blue))
            pos += 6
        elif op == 0x02:  # SETN: op, count, then count × idx/r/g/b
            if pos + 2 > len(data):
                records.append('truncated SETN')
                break
            count = data[pos + 1]
            end = pos + 2 + count * 4
            if end > len(data):
                records.append('truncated SETN (%d LEDs)' % count)
                break
            pixels += count
            records.append('set %d LED%s' % (count, '' if count == 1 else 's'))
            pos = end
        else:
            records.append('unknown op 0x%02X' % op)
            break

    detail = '; '.join(records[:3])
    if len(records) > 3:
        detail += '; +%d more' % (len(records) - 3)
    return 'LK colour sync · %d LED%s · %d fill record%s%s' % (
        pixels, '' if pixels == 1 else 's', fills, '' if fills == 1 else 's',
        (' · ' + detail) if detail else '')


def now_stamp():
    ms = int((time.time() % 1) * 1000)
    return time.strftime('%Y-%m-%d %H:%M:%S') + '.%03d' % ms


def fmt_duration_min(total_min):
    """Minute count -> 'Xh Ym' (or just 'Ym' under an hour) - same shape as
    the Android app's DiffuserUsagePopup._fmtDuration(), for consistency
    with what the phone shows."""
    if total_min < 60:
        return '%dm' % total_min
    h, m = divmod(total_min, 60)
    return '%dh' % h if m == 0 else '%dh %dm' % (h, m)


def file_stamp():
    return time.strftime('%Y-%m-%d_%H-%M-%S')


def parse_status_fields(text):
    """Ds/Dc (and the SmartTV's relayed Ds) share one format: Ds+MM+SS+TTTT+UUUU+VVVV+RR+LLLL."""
    if not text or text[:2] != 'Ds' or len(text) < 24:
        return None
    mm = int(text[2:4], 16)
    mode_name = 'OUT OF WATER' if mm == 5 else (DIF_MODE_NAMES[mm] if mm < len(DIF_MODE_NAMES) else 'M%d' % mm)
    return {
        'mode': mm, 'mode_name': mode_name,
        'strip': int(text[4:6], 16),
        'parfum_min': int(text[6:10], 16),
        'usage_min': int(text[10:14], 16),
        'avg_min': int(text[14:18], 16),
        'refill_count': int(text[18:20], 16),
        'total_refills': int(text[20:24], 16),
    }


def parse_settings_reply(text):
    print(f"DEBUG: parse_settings_reply called with text: {text[:50] if text else 'None'}...")
    if not text or text[:1] != 'S' or len(text) < 1 + 50 * 4:
        print(f"DEBUG: Parse failed - text start: {text[:1] if text else 'None'}, length: {len(text) if text else 0}")
        return None
    idx_to_name = dict(TV_SETTINGS)
    rows = []
    for i in range(50):
        base = 1 + i * 4
        try:
            idx = int(text[base:base + 2], 16)
            val = int(text[base + 2:base + 4], 16)
        except Exception as e:
            print(f"DEBUG: Exception parsing index {i}: {e}")
            continue
        rows.append((idx, idx_to_name.get(idx, 'idx %d' % idx), val))
    print(f"DEBUG: Parsed {len(rows)} settings successfully")
    return rows


class App(object):
    def __init__(self, root, base_dir):
        self.root = root
        self.base_dir = base_dir
        self.seq = 0
        self.result_queue = queue.Queue()
        self.devices = {
            'diffuser': {'name': 'Diffuser', 'ip': '192.168.1.203', 'udp_port': 8439,
                         'telnet_port': 23, 'timeout_ms': 1200, 'has_console': True},
            'smarttv': {'name': 'SmartTV', 'ip': '192.168.1.202', 'udp_port': 8472,
                        'telnet_port': 23, 'timeout_ms': 1200, 'has_console': False},
        }
        self.active_device = 'diffuser'
        self.selected_id = {'diffuser': None, 'smarttv': None}
        self.sidebar_rows = {}
        self.command_tables = {'diffuser': DIFFUSER_COMMANDS, 'smarttv': SMARTTV_COMMANDS}
        self.expanded_sections = {'diffuser': {}, 'smarttv': {}}
        self.keep_alive_on = False
        self.keep_alive_stop = threading.Event()
        self.keep_alive_thread = None
        self.last_heartbeat = {'diffuser': None, 'smarttv': None}

        self.log_path = None
        self.log_file = None
        self.log_buffer = []  # In-memory log buffer

        self._build_style()
        self._build_ui()
        self.switch_device('diffuser')
        self._log_sys('TestMode started. Log not saved to file yet.')
        d, t = self.devices['diffuser'], self.devices['smarttv']
        self._log_sys('Diffuser -> %s   SmartTV -> %s' % (d['ip'], t['ip']))

        self.root.protocol('WM_DELETE_WINDOW', self._on_close)
        self.root.after(50, self._poll_queue)

    # ------------------------------------------------------------- logging
    def _log_write(self, line):
        self.log_buffer.append(line + '\n')

    def _panel_log(self, tag, cls, text):
        ts = now_stamp()
        self._append_log_line(ts, tag, cls, text)
        self._log_write('%s [%s] %s' % (ts, tag, text))

    def _log_sys(self, text):
        ts = now_stamp()
        self._append_log_line(ts, 'SYS', 'sys', text)
        self._log_write('%s [SYS] %s' % (ts, text))

    def _append_log_line(self, ts, tag, cls, text):
        self.log_text.configure(state='normal')
        self.log_text.insert('end', ts + ' ', 'sys')
        tagcolor = 'tagdif' if tag == 'DIF' else ('tagtv' if tag == 'TV' else 'sys')
        self.log_text.insert('end', '[%s] ' % tag, tagcolor)
        self.log_text.insert('end', text + '\n', cls)
        self.log_text.see('end')
        self.log_text.configure(state='disabled')

    def _clear_log_panel(self):
        self.log_text.configure(state='normal')
        self.log_text.delete('1.0', 'end')
        self.log_text.configure(state='disabled')

    def _save_log_file(self):
        if not self.log_buffer:
            self._log_sys('No log entries to save')
            return
        
        # Generate log file path with timestamp
        log_path = os.path.join(self.base_dir, 'TestMode-%s.log' % file_stamp())
        
        try:
            with open(log_path, 'w', encoding='utf-8') as f:
                f.write('=' * 66 + '\n')
                f.write('FuZz TestMode log - saved ' + now_stamp() + '\n')
                d, t = self.devices['diffuser'], self.devices['smarttv']
                f.write('Diffuser default: %s:%d (udp) / :%d (telnet)\n' % (d['ip'], d['udp_port'], d['telnet_port']))
                f.write('SmartTV  default: %s:%d (udp) / :%d (telnet)\n' % (t['ip'], t['udp_port'], t['telnet_port']))
                f.write('=' * 66 + '\n')
                f.writelines(self.log_buffer)
            
            self.log_path = log_path
            self._log_sys('Log saved to: ' + log_path)
            
            # Try to open the log file
            try:
                os.startfile(log_path)
            except Exception:
                pass
                
        except Exception as e:
            self._log_sys('Failed to save log: ' + str(e))

    # ------------------------------------------------------------- style
    def _build_style(self):
        style = ttk.Style()
        try:
            style.theme_use('clam')
        except Exception:
            pass
        style.configure('TCombobox', fieldbackground=BG_FIELD, background=BG_FIELD, foreground=TEXT,
                        arrowcolor=TEXT_SECONDARY, bordercolor=BORDER_STRONG, lightcolor=BG_FIELD, darkcolor=BG_FIELD)
        style.map('TCombobox', fieldbackground=[('readonly', BG_FIELD)], foreground=[('readonly', TEXT)])
        self.root.option_add('*TCombobox*Listbox.background', BG_FIELD)
        self.root.option_add('*TCombobox*Listbox.foreground', TEXT)
        self.root.option_add('*TCombobox*Listbox.selectBackground', BTN_GREEN_BG)
        self.root.option_add('*TCombobox*Listbox.selectForeground', BTN_GREEN_FG)

    # ------------------------------------------------------------- top-level layout
    def _build_ui(self):
        self.root.configure(bg=BG)

        topbar = tk.Frame(self.root, bg=BG_SIDEBAR)
        topbar.pack(fill='x')
        tk.Label(topbar, text='FuZz TestMode', bg=BG_SIDEBAR, fg=TEXT,
                 font=('Segoe UI', 11, 'bold')).pack(side='left', padx=12, pady=8)
        tk.Label(topbar, text='every send is logged live in this window - click SaveLog to write it to disk',
                 bg=BG_SIDEBAR, fg=TEXT_MUTED, font=('Segoe UI', 9)).pack(side='left')

        self.keepalive_dot = tk.Label(topbar, text='●', bg=BG_SIDEBAR, fg=TEXT_MUTED, font=('Segoe UI', 10))
        self.keepalive_dot.pack(side='right', padx=(0, 4), pady=8)
        self.keepalive_label = tk.Label(topbar, text='Keep-alive: off', bg=BG_SIDEBAR, fg=TEXT_SECONDARY,
                                        font=('Segoe UI', 9))
        self.keepalive_label.pack(side='right', padx=(10, 4), pady=8)
        self.keepalive_btn = tk.Button(topbar, text='Start keep-alive', bg=BG_BTN, fg=TEXT,
                                       activebackground=BG_BTN_HOVER, relief='flat', bd=0, font=('Segoe UI', 9),
                                       padx=10, pady=3, command=self._toggle_keep_alive)
        self.keepalive_btn.pack(side='right', padx=10, pady=6)
        tk.Button(topbar, text='SaveLog', bg=BG_BTN, fg=TEXT, activebackground=BG_BTN_HOVER, relief='flat', bd=0,
                  font=('Segoe UI', 9), padx=9, pady=3, command=self._save_log_file).pack(side='right', padx=(10, 0), pady=6)

        tabsbar = tk.Frame(self.root, bg=BG_SIDEBAR)
        tabsbar.pack(fill='x')
        self.tab_buttons = {}
        for i, (key, label) in enumerate((('diffuser', 'Diffuser'), ('smarttv', 'SmartTV'))):
            b = tk.Label(tabsbar, text=label, bg=BG_SIDEBAR, fg=TEXT_SECONDARY, font=('Segoe UI', 10, 'bold'),
                         padx=16, pady=7, cursor='hand2')
            b.pack(side='left', padx=(12 if i == 0 else 4, 0))
            b.bind('<Button-1>', lambda e, k=key: self.switch_device(k))
            self.tab_buttons[key] = b

        self.connbar = tk.Frame(self.root, bg=BG_PANEL)
        self.connbar.pack(fill='x')
        self._build_connbar_fields()

        body = tk.Frame(self.root, bg=BG)

        self.log_frame = self._build_logpanel(self.root)
        self.log_frame.pack(side='bottom', fill='x')
        self.raw_frame = self._build_rawbox(self.root)
        self.raw_frame.pack(side='bottom', fill='x')
        body.pack(fill='both', expand=True)

        sidebar_outer = tk.Frame(body, bg=BG_SIDEBAR, width=300)
        sidebar_outer.pack(side='left', fill='y')
        sidebar_outer.pack_propagate(False)

        filter_row = tk.Frame(sidebar_outer, bg=BG_SIDEBAR)
        filter_row.pack(fill='x', padx=10, pady=(10, 6))
        tk.Label(filter_row, text='FILTER', bg=BG_SIDEBAR, fg=TEXT_MUTED, font=('Segoe UI', 7, 'bold')).pack(anchor='w')
        self.filter_var = tk.StringVar()
        filter_entry = tk.Entry(filter_row, textvariable=self.filter_var, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                                highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 10))
        filter_entry.pack(fill='x', pady=(3, 0))
        self.filter_var.trace_add('write', lambda *_a: self._render_sidebar(self.active_device, self.command_tables[self.active_device]))

        sb_canvas, self.sidebar_inner = self._build_scrollable(sidebar_outer, bg=BG_SIDEBAR)
        sb_canvas.pack(fill='both', expand=True)

        detail_outer = tk.Frame(body, bg=BG)
        detail_outer.pack(side='left', fill='both', expand=True)
        dt_canvas, self.detail_inner = self._build_scrollable(detail_outer, bg=BG)
        dt_canvas.pack(fill='both', expand=True)

    def _build_connbar_fields(self):
        self.conn_ip_var = tk.StringVar()
        self.conn_udp_var = tk.StringVar()
        self.conn_telnet_var = tk.StringVar()
        self.conn_timeout_var = tk.StringVar()

        def _field(col, label_text, var, width, cast, fallback, field_name):
            f = tk.Frame(self.connbar, bg=BG_PANEL)
            f.grid(row=0, column=col, padx=(12, 4), pady=6, sticky='w')
            tk.Label(f, text=label_text, bg=BG_PANEL, fg=TEXT_MUTED, font=('Segoe UI', 8)).pack(side='left', padx=(0, 4))
            ent = tk.Entry(f, textvariable=var, width=width, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                           highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 9))
            ent.pack(side='left')
            ent.bind('<Return>', lambda e: self._on_conn_change(field_name, var.get(), cast, fallback))
            ent.bind('<FocusOut>', lambda e: self._on_conn_change(field_name, var.get(), cast, fallback))
            return f

        _field(0, 'IP', self.conn_ip_var, 15, str, '', 'ip')
        _field(1, 'UDP port', self.conn_udp_var, 6, int, 8439, 'udp_port')
        self.conn_telnet_frame = _field(2, 'Telnet port', self.conn_telnet_var, 6, int, 23, 'telnet_port')
        _field(3, 'Timeout ms', self.conn_timeout_var, 6, int, 1200, 'timeout_ms')

        self.conn_note_label = tk.Label(self.connbar, text='', bg=BG_PANEL, fg=TEXT_MUTED, font=('Segoe UI', 8, 'italic'))
        self.conn_note_label.grid(row=0, column=4, padx=10, sticky='w')

    def _on_conn_change(self, field, raw_value, cast, fallback):
        device = self.devices[self.active_device]
        try:
            value = cast(raw_value)
        except Exception:
            value = fallback
        device[field] = value
        self._log_sys('connection: %s %s -> %s' % (self.active_device, field, value))

    def _build_scrollable(self, parent, bg=BG):
        canvas = tk.Canvas(parent, bg=bg, highlightthickness=0)
        vsb = tk.Scrollbar(parent, orient='vertical', command=canvas.yview, bg=BG_PANEL,
                           troughcolor=bg, activebackground=BORDER_STRONG, bd=0, highlightthickness=0)
        inner = tk.Frame(canvas, bg=bg)
        inner_id = canvas.create_window((0, 0), window=inner, anchor='nw')

        def _on_inner_configure(_e):
            canvas.configure(scrollregion=canvas.bbox('all'))
        inner.bind('<Configure>', _on_inner_configure)

        def _on_canvas_configure(e):
            canvas.itemconfigure(inner_id, width=e.width)
        canvas.bind('<Configure>', _on_canvas_configure)

        canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side='right', fill='y')

        def _on_enter(_e):
            canvas.bind_all('<MouseWheel>', lambda e: canvas.yview_scroll(int(-1 * (e.delta / 120)), 'units'))

        def _on_leave(_e):
            canvas.unbind_all('<MouseWheel>')
        canvas.bind('<Enter>', _on_enter)
        canvas.bind('<Leave>', _on_leave)

        return canvas, inner

    # ------------------------------------------------------------- sidebar
    def _render_sidebar(self, device_key, command_list):
        """Collapsible category tree. Categories with an active filter match
        force-expand; a typed filter narrows rows by id/label substring."""
        for w in self.sidebar_inner.winfo_children():
            w.destroy()
        self.sidebar_rows[device_key] = {}

        query = self.filter_var.get().strip().lower() if hasattr(self, 'filter_var') else ''
        expanded = self.expanded_sections.setdefault(device_key, {})

        sections = []
        by_section = {}
        for spec in command_list:
            sec = spec['section']
            if sec not in by_section:
                by_section[sec] = []
                sections.append(sec)
            by_section[sec].append(spec)

        first_id = None
        any_match = False
        for sec in sections:
            matches = [s for s in by_section[sec]
                       if not query or query in s['id'].lower() or query in s['label'].lower()
                       or query in s.get('name', '').lower()]
            if query and not matches:
                continue
            any_match = True
            is_open = bool(query) or expanded.get(sec, True)
            title, color, badge, icon = category_meta(sec)

            header = tk.Frame(self.sidebar_inner, bg=BG_SIDEBAR, cursor='hand2')
            header.pack(fill='x', pady=(10, 1))

            def _toggle(_e=None, dk=device_key, s=sec, exp=expanded):
                exp[s] = not exp.get(s, True)
                self._render_sidebar(dk, self.command_tables[dk])

            chevron = tk.Label(header, text=('\u25be' if is_open else '\u25b8'), bg=BG_SIDEBAR, fg=TEXT_MUTED,
                               font=('Segoe UI', 8), pady=4)
            chevron.pack(side='left', padx=(10, 2))
            tk.Label(header, text=icon, bg=BG_SIDEBAR, fg=color, font=('Segoe UI', 11),
                    padx=0, pady=1).pack(side='left', padx=(0, 6))
            tk.Label(header, text=title, bg=BG_SIDEBAR, fg=color, font=('Segoe UI', 8, 'bold'),
                     anchor='w').pack(side='left', fill='x', expand=True, pady=4)
            tk.Label(header, text=str(len(matches)), bg=BG_SIDEBAR, fg=TEXT_MUTED,
                     font=('Segoe UI', 8)).pack(side='right', padx=(0, 10))
            for w in (header,) + tuple(header.winfo_children()):
                w.bind('<Button-1>', _toggle)

            if not is_open:
                continue

            for spec in matches:
                if first_id is None:
                    first_id = spec['id']
                row = tk.Frame(self.sidebar_inner, bg=BG_SIDEBAR, cursor='hand2')
                row.pack(fill='x')
                name_lbl = tk.Label(row, text=spec.get('name', spec['label']), bg=BG_SIDEBAR, fg=TEXT_SECONDARY,
                                    font=('Segoe UI', 10), anchor='w', padx=24, pady=5)
                name_lbl.pack(side='left', fill='x', expand=True)
                code_lbl = tk.Label(row, text=spec['label'], bg=BG_SIDEBAR, fg=TEXT_MUTED,
                                    font=('Consolas', 8), anchor='e', padx=10, pady=5)
                code_lbl.pack(side='right')
                widgets = (row, name_lbl, code_lbl)
                for w in widgets:
                    w.bind('<Button-1>', lambda e, s=spec: self._select_command(device_key, s))
                    w.bind('<Enter>', lambda e, dk=device_key, sid=spec['id']: self._sidebar_hover(dk, sid, True))
                    w.bind('<Leave>', lambda e, dk=device_key, sid=spec['id']: self._sidebar_hover(dk, sid, False))
                self.sidebar_rows[device_key][spec['id']] = widgets

        if not any_match:
            tk.Label(self.sidebar_inner, text='no commands match "%s"' % query, bg=BG_SIDEBAR, fg=TEXT_MUTED,
                     font=('Segoe UI', 9), wraplength=250, justify='left', padx=14, pady=14).pack(fill='x')

        if self.selected_id[device_key] is None:
            self.selected_id[device_key] = first_id

        sel = self.selected_id.get(device_key)
        if sel and sel in self.sidebar_rows[device_key]:
            self._paint_row(device_key, sel, BG_ROW_SELECTED, ACCENT_BLUE)

    def _paint_row(self, device_key, spec_id, bg, name_fg):
        widgets = self.sidebar_rows.get(device_key, {}).get(spec_id)
        if not widgets:
            return
        row, name_lbl, code_lbl = widgets
        row.configure(bg=bg)
        name_lbl.configure(bg=bg, fg=name_fg)
        code_lbl.configure(bg=bg)

    def _sidebar_hover(self, device_key, spec_id, entering):
        if self.selected_id.get(device_key) == spec_id:
            return
        self._paint_row(device_key, spec_id, (BG_ROW_HOVER if entering else BG_SIDEBAR), TEXT_SECONDARY)

    def _select_command(self, device_key, spec):
        prev = self.selected_id.get(device_key)
        if prev and prev in self.sidebar_rows.get(device_key, {}):
            self._paint_row(device_key, prev, BG_SIDEBAR, TEXT_SECONDARY)
        self.selected_id[device_key] = spec['id']
        self._paint_row(device_key, spec['id'], BG_ROW_SELECTED, ACCENT_BLUE)
        if device_key == self.active_device:
            self._render_detail(spec, device_key)

    # ------------------------------------------------------------- detail panel
    def _build_param_controls(self, parent, spec, on_change):
        wrap = tk.Frame(parent, bg=BG)
        controls = {}
        params = spec.get('params') or []

        for p in params:
            row = tk.Frame(wrap, bg=BG)
            row.pack(fill='x', pady=5)
            tk.Label(row, text=p['label'], bg=BG, fg=TEXT_SECONDARY, font=('Segoe UI', 9), width=16,
                     anchor='w').pack(side='left')
            ptype = p['type']

            if ptype == 'enum':
                options = p['options']
                label_to_value = dict((lbl2, val) for val, lbl2 in options)
                value_to_label = dict((val, lbl2) for val, lbl2 in options)
                var = tk.StringVar(value=value_to_label.get(p['default'], p['default']))
                widths = [len(o[1]) for o in options] or [8]
                cb = ttk.Combobox(row, state='readonly', values=[o[1] for o in options], textvariable=var,
                                  width=max(10, min(34, max(widths))))
                cb.pack(side='left')
                cb.bind('<<ComboboxSelected>>', lambda e: on_change())
                controls[p['key']] = {'kind': 'enum', 'var': var, 'map': label_to_value, 'widget': cb}
            elif ptype == 'range':
                var = tk.IntVar(value=p['default'])
                slider = tk.Frame(row, bg=BG)
                slider.pack(side='left', fill='x')
                value_var = tk.StringVar(value=str(p['default']))
                tk.Label(slider, text=str(p['min']), bg=BG, fg=TEXT_MUTED,
                         font=('Consolas', 8), width=4, anchor='e').pack(side='left', padx=(0, 5))
                sc = tk.Scale(slider, from_=p['min'], to=p['max'], orient='horizontal', variable=var, length=265,
                              showvalue=0, sliderlength=20, width=14, bg=ACCENT_AMBER, activebackground='#e2c68a',
                              troughcolor=BORDER, highlightthickness=0, bd=0, sliderrelief='flat')
                sc.pack(side='left')
                tk.Label(slider, textvariable=value_var, bg=BTN_GREEN_BG, fg=BTN_GREEN_FG, font=('Consolas', 10, 'bold'),
                         width=5, padx=4, pady=2).pack(side='left', padx=6)
                tk.Label(slider, text=str(p['max']), bg=BG, fg=TEXT_MUTED,
                         font=('Consolas', 8), width=4, anchor='w').pack(side='left', padx=(0, 2))

                def _range_changed(_value, v=var, output=value_var):
                    output.set(str(v.get()))
                    on_change()

                sc.configure(command=_range_changed)
                controls[p['key']] = {'kind': 'range', 'var': var, 'widget': sc}
            elif ptype == 'number':
                var = tk.StringVar(value=str(p['default']))
                ent = tk.Entry(row, textvariable=var, width=8, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                              highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 10))
                ent.pack(side='left')
                var.trace_add('write', lambda *_a: on_change())
                controls[p['key']] = {'kind': 'number', 'var': var, 'widget': ent}
            elif ptype == 'checkbox':
                var = tk.BooleanVar(value=bool(p['default']))
                state_text = tk.StringVar()
                chk = tk.Checkbutton(row, variable=var, textvariable=state_text, indicatoron=0,
                                     width=5, padx=7, pady=3, relief='flat', bd=0,
                                     activebackground=ACCENT_GREEN, activeforeground=WHITE,
                                     highlightthickness=1, highlightbackground=BORDER,
                                     font=('Consolas', 9, 'bold'))

                def _sync_checkbox(*_a, v=var, button=chk, text=state_text):
                    enabled = bool(v.get())
                    text.set('ON' if enabled else 'OFF')
                    button.configure(bg=(ACCENT_GREEN if enabled else BG_BTN),
                                     fg=(WHITE if enabled else TEXT_MUTED))
                    on_change()

                var.trace_add('write', _sync_checkbox)
                _sync_checkbox()
                chk.pack(side='left')
                controls[p['key']] = {'kind': 'checkbox', 'var': var, 'widget': chk}
            elif ptype == 'hexcolor':
                var = tk.StringVar(value=p['default'].upper())
                swatch = tk.Label(row, bg='#' + p['default'], width=3, relief='flat', bd=0,
                                  highlightthickness=1, highlightbackground=BORDER_STRONG, cursor='hand2')
                swatch.pack(side='left', padx=(0, 6))
                ent = tk.Entry(row, textvariable=var, width=8, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                               highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 10))
                ent.pack(side='left')

                def _sync_swatch(*_a, sw=swatch, v=var):
                    val = ''.join(c for c in v.get().upper() if c in '0123456789ABCDEF')[:6]
                    val = (val + '000000')[:6]
                    if v.get() != val:
                        v.set(val)
                        return
                    try:
                        sw.configure(bg='#' + val)
                    except Exception:
                        pass
                    on_change()
                var.trace_add('write', _sync_swatch)

                def _pick(sw=swatch, v=var):
                    initial = '#' + (v.get() or 'FFFFFF')
                    res = colorchooser.askcolor(color=initial, title='Pick colour')
                    if res and res[1]:
                        v.set(res[1].lstrip('#').upper())
                swatch.bind('<Button-1>', lambda e, f=_pick: f())
                controls[p['key']] = {'kind': 'hexcolor', 'var': var, 'widget': ent}
            elif ptype == 'ledmask':
                grid_frame, get_mask = self._build_ledmask_control(row, p, on_change)
                grid_frame.pack(side='left')
                controls[p['key']] = {'kind': 'ledmask', 'get': get_mask}
            else:
                var = tk.StringVar(value=str(p.get('default', '')))
                ent = tk.Entry(row, textvariable=var, width=20, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                              highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 10))
                ent.pack(side='left')
                var.trace_add('write', lambda *_a: on_change())
                controls[p['key']] = {'kind': 'text', 'var': var, 'widget': ent}

        for p in params:
            dep_key = p.get('enable_when')
            if dep_key and dep_key in controls and p['key'] in controls:
                dep_var = controls[dep_key]['var']
                target_widget = controls[p['key']]['widget']

                def _sync_enable(*_a, dv=dep_var, tw=target_widget):
                    try:
                        tw.configure(state=('normal' if dv.get() else 'disabled'))
                    except Exception:
                        pass
                dep_var.trace_add('write', _sync_enable)
                _sync_enable()

        return wrap, controls

    def _build_ledmask_control(self, parent, p, on_change):
        wrap = tk.Frame(parent, bg=BG)
        state = {'bits': list((p.get('default') or '0' * 61))}
        squares = []

        def _redraw(pos):
            idx, widget, color = squares[pos]
            widget.configure(bg=(color if state['bits'][idx] == '1' else BG_BTN))

        def _toggle(pos):
            idx = squares[pos][0]
            state['bits'][idx] = '0' if state['bits'][idx] == '1' else '1'
            _redraw(pos)
            on_change()

        def _set_all(value_char):
            for pos in range(len(squares)):
                state['bits'][squares[pos][0]] = value_char
                _redraw(pos)
            on_change()

        for zone_name, start, count, color in LED_ZONES:
            zone = tk.Frame(wrap, bg=BG)
            zone.pack(side='left', padx=(0, 8))
            tk.Label(zone, text=zone_name, bg=BG, fg=TEXT_MUTED, font=('Segoe UI', 7)).pack(anchor='w')
            row = tk.Frame(zone, bg=BG)
            row.pack()
            for k in range(count):
                idx = start + k
                sq = tk.Label(row, bg=BG_BTN, width=1, height=1, font=('Segoe UI', 4), cursor='hand2',
                             highlightthickness=1, highlightbackground=BORDER)
                sq.pack(side='left', padx=1, pady=1)
                pos = len(squares)
                squares.append((idx, sq, color))
                sq.bind('<Button-1>', lambda e, i=pos: _toggle(i))

        btnrow = tk.Frame(wrap, bg=BG)
        btnrow.pack(side='left', padx=(6, 0))
        tk.Button(btnrow, text='All', bg=BG_BTN, fg=TEXT, relief='flat', bd=0, font=('Segoe UI', 8),
                 padx=6, pady=1, command=lambda: _set_all('1')).pack(pady=(0, 2))
        tk.Button(btnrow, text='None', bg=BG_BTN, fg=TEXT, relief='flat', bd=0, font=('Segoe UI', 8),
                 padx=6, pady=1, command=lambda: _set_all('0')).pack()

        def get_mask():
            return ''.join(state['bits'])

        return wrap, get_mask

    def _collect_param_values(self, spec, controls):
        vals = {}
        for p in spec.get('params') or []:
            c = controls[p['key']]
            kind = c['kind']
            if kind == 'checkbox':
                vals[p['key']] = bool(c['var'].get())
            elif kind == 'range':
                vals[p['key']] = int(c['var'].get())
            elif kind == 'number':
                try:
                    vals[p['key']] = int(c['var'].get())
                except Exception:
                    vals[p['key']] = p.get('min', 0)
            elif kind == 'enum':
                label = c['var'].get()
                vals[p['key']] = c['map'].get(label, label)
            elif kind == 'ledmask':
                vals[p['key']] = c['get']()
            else:
                vals[p['key']] = c['var'].get()
        return vals

    def _step_row(self, parent, number, title):
        """One numbered step: a small filled badge + title, matching the same
        numbered-flow pattern for every command - zero-parameter commands just
        end up with fewer steps, rather than a different layout entirely."""
        row = tk.Frame(parent, bg=BG)
        row.pack(fill='x', pady=(0 if number == 1 else 18, 8))
        badge = tk.Label(row, text=str(number), bg=ACCENT_BLUE, fg=BG_SIDEBAR, font=('Segoe UI', 9, 'bold'),
                         width=2, pady=1)
        badge.pack(side='left', padx=(0, 8))
        tk.Label(row, text=title, bg=BG, fg=TEXT, font=('Segoe UI', 10, 'bold')).pack(side='left')
        body = tk.Frame(parent, bg=BG)
        body.pack(fill='x', padx=(28, 0))
        return body

    def _confirm_ok(self, spec):
        """Gate a send behind a Yes/No dialog when the command spec carries a
        'confirm' message - for actions with real physical consequences
        (e.g. toggling every LED on the strip) where a stray click shouldn't
        fire immediately. Returns True to proceed, False to abort the send."""
        msg = spec.get('confirm')
        if not msg:
            return True
        return messagebox.askyesno(spec.get('name', spec['label']), msg)

    def _render_detail(self, spec, device_key):
        for w in self.detail_inner.winfo_children():
            w.destroy()

        # Fully bespoke panels (their own fetch/list/action flow, not the
        # generic params-or-direct_buttons + single Send/Result shape every
        # other command uses) opt out here instead of trying to force-fit.
        if spec.get('custom_panel') == 'diffuser_history':
            self._render_history_manager(spec, device_key)
            return

        pad = tk.Frame(self.detail_inner, bg=BG)
        pad.pack(fill='both', expand=True, padx=24, pady=20)

        _, cat_color, _, icon = category_meta(spec['section'])

        head = tk.Frame(pad, bg=BG)
        head.pack(fill='x')
        tk.Label(head, text=icon, bg=BG, fg=cat_color, font=('Segoe UI', 15)).pack(side='left', padx=(0, 8))
        tk.Label(head, text=spec.get('name', spec['label']), bg=BG, fg=TEXT, font=('Segoe UI', 15, 'bold')).pack(side='left')
        tk.Label(head, text=spec['label'], bg=BG_BTN, fg=TEXT_MUTED, font=('Consolas', 9), padx=7, pady=2).pack(side='left', padx=(10, 0))
        tag_text = 'UDP' if spec['transport'] == 'udp' else 'TCP:23'
        tk.Label(head, text=tag_text, bg=BG_BTN, fg=TEXT_SECONDARY, font=('Consolas', 9), padx=8, pady=2).pack(side='left', padx=(6, 0))

        tk.Label(pad, text=spec['desc'], bg=BG, fg=TEXT_SECONDARY, font=('Segoe UI', 9),
                 justify='left', anchor='w', wraplength=760).pack(fill='x', pady=(6, 4))

        preview_var = tk.StringVar(value='')
        controls = {}
        step_n = 1

        def _update_preview():
            if spec.get('direct_buttons'):
                return
            try:
                vals = self._collect_param_values(spec, controls)
                payload = spec['build'](vals)
                preview_var.set(payload)
            except Exception:
                preview_var.set('(invalid parameters)')

        if spec.get('params'):
            body = self._step_row(pad, step_n, 'Configure')
            step_n += 1
            pframe, controls = self._build_param_controls(body, spec, _update_preview)
            pframe.pack(fill='x')

        if spec.get('direct_buttons'):
            body = self._step_row(pad, step_n, 'Choose an option')
            step_n += 1
            btn_row = tk.Frame(body, bg=BG)
            btn_row.pack(fill='x')
            chip = tk.Label(btn_row, text='-', bg=BG_BTN, fg=TEXT_MUTED, font=('Segoe UI', 9, 'bold'), padx=10, pady=3)
            for val, lbl in spec['direct_buttons']:
                def _send_option(v=val):
                    if not self._confirm_ok(spec):
                        return
                    self._on_send(spec, device_key, v, chip, resp_area, {})
                b = tk.Button(btn_row, text=lbl, bg=BG_BTN, fg=TEXT, activebackground=BG_BTN_HOVER,
                             relief='flat', bd=0, font=('Segoe UI', 9), padx=12, pady=5,
                             command=_send_option)
                b.pack(side='left', padx=(0, 6))
            chip.pack(side='left', padx=(12, 0))
        else:
            body = self._step_row(pad, step_n, 'Review and send')
            step_n += 1
            preview_row = tk.Frame(body, bg=BG)
            preview_row.pack(fill='x', pady=(0, 10))
            tk.Label(preview_row, text='payload', bg=BG, fg=TEXT_MUTED, font=('Segoe UI', 8)).pack(side='left', padx=(0, 8))
            tk.Label(preview_row, textvariable=preview_var, bg=BG_CARD, fg=ACCENT_BLUE, font=('Consolas', 10),
                     padx=10, pady=4, anchor='w').pack(side='left', fill='x', expand=True)
            _update_preview()

            send_row = tk.Frame(body, bg=BG)
            send_row.pack(fill='x')
            chip = tk.Label(send_row, text='-', bg=BG_BTN, fg=TEXT_MUTED, font=('Segoe UI', 9, 'bold'), padx=10, pady=3)

            def _do_send():
                if not self._confirm_ok(spec):
                    return
                vals = self._collect_param_values(spec, controls)
                payload = spec['build'](vals)
                self._on_send(spec, device_key, payload, chip, resp_area, vals)
            send_btn = tk.Button(send_row, text='Send', bg=BTN_GREEN_BG, fg=BTN_GREEN_FG, activebackground=BTN_GREEN_ACTIVE,
                                 relief='flat', bd=0, font=('Segoe UI', 9, 'bold'), padx=16, pady=6, command=_do_send)
            send_btn.pack(side='left')
            chip.pack(side='left', padx=(12, 0))

        resp_body = self._step_row(pad, step_n, 'Result')
        resp_area = tk.Frame(resp_body, bg=BG_CARD)
        resp_area.pack(fill='x')
        tk.Label(resp_area, text='-', bg=BG_CARD, fg=TEXT_MUTED, font=('Consolas', 9), padx=10, pady=10,
                 anchor='w').pack(fill='x')

    # ------------------------------------------------------------- diffuser history manager
    def _render_history_manager(self, spec, device_key):
        """Bespoke panel (see _render_detail's custom_panel hook): live list
        of every stored refill-history cycle, each with its own X to remove
        just that one entry (device shifts the rest down - see DyII/
        removeHistoryEntryAt() in the diffuser .ino). Fetches Dh itself on
        open and again after every delete, entirely independent of the
        generic params/direct_buttons + single Send/Result flow every other
        command uses - a per-row list with per-row actions doesn't fit that
        shape."""
        pad = tk.Frame(self.detail_inner, bg=BG)
        pad.pack(fill='both', expand=True, padx=24, pady=20)

        _, cat_color, _, icon = category_meta(spec['section'])
        head = tk.Frame(pad, bg=BG)
        head.pack(fill='x')
        tk.Label(head, text=icon, bg=BG, fg=cat_color, font=('Segoe UI', 15)).pack(side='left', padx=(0, 8))
        tk.Label(head, text=spec.get('name', spec['label']), bg=BG, fg=TEXT, font=('Segoe UI', 15, 'bold')).pack(side='left')
        tk.Label(head, text=spec['label'], bg=BG_BTN, fg=TEXT_MUTED, font=('Consolas', 9), padx=7, pady=2).pack(side='left', padx=(10, 0))

        tk.Label(pad, text=spec['desc'], bg=BG, fg=TEXT_SECONDARY, font=('Segoe UI', 9),
                 justify='left', anchor='w', wraplength=760).pack(fill='x', pady=(6, 4))

        toolbar = tk.Frame(pad, bg=BG)
        toolbar.pack(fill='x', pady=(10, 4))
        status_var = tk.StringVar(value='Loading…')
        tk.Label(toolbar, textvariable=status_var, bg=BG, fg=TEXT_MUTED, font=('Segoe UI', 8)).pack(side='left')
        refresh_btn = tk.Button(toolbar, text='Refresh', bg=BG_BTN, fg=TEXT, activebackground=BG_BTN_HOVER,
                                relief='flat', bd=0, font=('Segoe UI', 9), padx=10, pady=4)
        refresh_btn.pack(side='right')

        list_frame = tk.Frame(pad, bg=BG)
        list_frame.pack(fill='x', pady=(6, 0))

        device = self.devices[device_key]

        def _fetch(on_reply):
            """Bare (un-enveloped) 'Dh' on a worker thread, result marshalled
            back onto the Tk thread via the existing result_queue poll -
            same pattern _worker_send uses for every other command."""
            def worker():
                result = net.send_udp(device['ip'], device['udp_port'], 'Dh', device['timeout_ms'])
                self.result_queue.put(lambda: on_reply(result))
            threading.Thread(target=worker, daemon=True).start()

        def _render_rows(minutes):
            for w in list_frame.winfo_children():
                w.destroy()
            if not minutes:
                tk.Label(list_frame, text='No refill cycles recorded yet.', bg=BG_CARD, fg=TEXT_MUTED,
                         font=('Consolas', 9), padx=10, pady=14, anchor='w').pack(fill='x')
                return
            for i, mins in enumerate(minutes, start=1):
                row = tk.Frame(list_frame, bg=CELL_BG, highlightthickness=1, highlightbackground=BORDER)
                row.pack(fill='x', pady=2)
                tk.Label(row, text='#%d' % i, bg=CELL_BG, fg=TEXT_MUTED, font=('Consolas', 9),
                         width=4, anchor='w', padx=8, pady=7).pack(side='left')
                tk.Label(row, text=fmt_duration_min(mins), bg=CELL_BG, fg=TEXT, font=('Segoe UI', 10, 'bold'),
                         anchor='w').pack(side='left', fill='x', expand=True)
                tk.Button(row, text='✕', bg=BG_BTN, fg=ACCENT_RED, activebackground=BG_BTN_HOVER,
                          relief='flat', bd=0, font=('Segoe UI', 10, 'bold'), padx=10, pady=4,
                          command=lambda idx=i, mins=mins: _delete(idx, mins)).pack(side='right', padx=8, pady=4)

        def _on_history_reply(result):
            if result['status'] != 'OK' or not result['replies']:
                status_var.set('Could not read history (%s)' % result.get('status', '?'))
                return
            minutes = parse_history_reply(result['replies'][0])
            if minutes is None:
                status_var.set('Unexpected reply: %s' % result['replies'][0])
                return
            status_var.set('%d of 10 cycles' % len(minutes))
            _render_rows(minutes)

        def _delete(idx, mins):
            if not messagebox.askyesno('Remove refill-history entry',
                    'Remove entry #%d (%s)?\n\nEverything after it shifts down to fill the gap. '
                    'Lifetime refill count and the current in-progress cycle are not affected.\n\n'
                    "This can't be undone." % (idx, fmt_duration_min(mins))):
                return
            seq = self.seq
            self.seq = (self.seq + 1) % 256
            wrapped = '#' + hexb(seq, 2) + 'Dy' + hexb(idx, 2)
            status_var.set('Removing #%d…' % idx)

            def worker():
                result = net.send_udp(device['ip'], device['udp_port'], wrapped, device['timeout_ms'])

                def _after():
                    if result['status'] != 'OK':
                        status_var.set('Send failed: %s' % result.get('message', result['status']))
                        return
                    ack = find_ack(result['replies'], seq)
                    if ack and ack['name'] != 'ok':
                        status_var.set('Device said: %s - list unchanged' % ack['name'])
                    _fetch(_on_history_reply)   # re-fetch either way - shows the true current state
                self.result_queue.put(_after)
            threading.Thread(target=worker, daemon=True).start()

        def _do_refresh():
            status_var.set('Loading…')
            _fetch(_on_history_reply)
        refresh_btn.configure(command=_do_refresh)
        _do_refresh()

    # ------------------------------------------------------------- response rendering
    def _render_ack_banner(self, resp_area, kind):
        """Big colour-coded strip at the top of the Result step, so whether a
        command actually succeeded reads at a glance instead of requiring the
        small chip next to Send to be spotted. Same CHIP_COLORS/kind vocabulary
        as that chip - this is a louder version of the same signal, not a
        second one."""
        bgc, fgc = CHIP_COLORS.get(kind, CHIP_COLORS['none'])
        icon, headline, meaning = ACK_INFO.get(kind, ACK_INFO['none'])
        banner = tk.Frame(resp_area, bg=bgc, highlightthickness=1, highlightbackground=fgc)
        banner.pack(fill='x', padx=10, pady=(10, 0))
        inner = tk.Frame(banner, bg=bgc)
        inner.pack(fill='x', padx=12, pady=9)
        tk.Label(inner, text=icon, bg=bgc, fg=fgc, font=('Segoe UI', 14, 'bold')).pack(side='left', padx=(0, 10))
        col = tk.Frame(inner, bg=bgc)
        col.pack(side='left', fill='x', expand=True)
        tk.Label(col, text=headline, bg=bgc, fg=fgc, font=('Segoe UI', 10, 'bold'), anchor='w').pack(fill='x')
        if meaning:
            tk.Label(col, text=meaning, bg=bgc, fg=fgc, font=('Segoe UI', 8), anchor='w').pack(fill='x', pady=(1, 0))

    def _render_response(self, resp_area, spec, raw_text, sent_vals, all_entries=None, ack_kind=None):
        for w in resp_area.winfo_children():
            w.destroy()

        if ack_kind:
            self._render_ack_banner(resp_area, ack_kind)

        # A single command can draw back several independent packets (the
        # connect handshake is the clearest example - s/H/E/M/w/f/p/LM/LK/@
        # can all arrive from one Z). List every one, plain-English, instead
        # of silently keeping only whichever got picked as "the" reply below.
        if all_entries and len(all_entries) > 1:
            self._render_reply_list(resp_area, all_entries)

        render = spec.get('render') or {}
        rtype = render.get('type')

        if rtype == 'status' and raw_text:
            fields = parse_status_fields(raw_text)
            if fields:
                self._render_status_grid(resp_area, fields, raw_text)
                return

        if rtype == 'color_params':
            keys = list(render.get('always') or [])
            cond = render.get('conditional')
            if cond and sent_vals.get(cond[0]):
                keys.append(cond[1])
            self._render_color_swatches(resp_area, [(k, sent_vals.get(k, 'FFFFFF')) for k in keys if k in sent_vals])
            self._render_raw_line(resp_area, raw_text)
            return

        if rtype == 'color_reply' and raw_text:
            hexval = ''.join(c for c in raw_text.upper() if c in '0123456789ABCDEF')
            if len(hexval) >= 6:
                self._render_color_swatches(resp_area, [('colour', hexval[:6])])
                self._render_hex_breakdown(resp_area, '', [
                    (hexval[0:2], ACCENT_RED), (hexval[2:4], ACCENT_GREEN), (hexval[4:6], ACCENT_BLUE)])
                return

        if rtype == 'color_reply_dual' and raw_text:
            hexval = ''.join(c for c in raw_text.upper() if c in '0123456789ABCDEF')
            if len(hexval) >= 12:
                self._render_color_swatches(resp_area, [('colour 1', hexval[:6]), ('colour 2', hexval[6:12])])
                self._render_hex_breakdown(resp_area, '', [
                    (hexval[0:2], ACCENT_RED), (hexval[2:4], ACCENT_GREEN), (hexval[4:6], ACCENT_BLUE),
                    (hexval[6:8], ACCENT_RED), (hexval[8:10], ACCENT_GREEN), (hexval[10:12], ACCENT_BLUE)])
                return
            if len(hexval) >= 6:
                self._render_color_swatches(resp_area, [('colour', hexval[:6])])
                self._render_raw_line(resp_area, raw_text, note='reply uses unpadded hex - widths may be off')
                return

        if rtype == 'settings_table' and raw_text:
            print(f"DEBUG: settings_table render called with raw_text: {raw_text[:50]}...")
            rows = parse_settings_reply(raw_text)
            print(f"DEBUG: parse_settings_reply returned: {rows}")
            if rows:
                self._render_settings_table(resp_area, rows)
                self._render_raw_line(resp_area, raw_text)
            else:
                # Show debugging info if parsing failed
                tk.Label(resp_area, text='Settings parse failed. Raw reply: %s' % raw_text, 
                        bg=BG_CARD, fg=ACCENT_RED, font=('Consolas', 9), padx=10, pady=10, anchor='w').pack(fill='x')
            return

        if all_entries and len(all_entries) > 1:
            return  # already listed every packet above - no redundant raw-only fallback needed

        # No specific renderer matched (e.g. 'k' keep-alive, or any command
        # with no 'render' entry) - still try the generic decoder before
        # falling back to raw text, so a single decodable packet (like an
        # async 'H' climate push arriving alongside a keep-alive reply)
        # doesn't show up unexplained just because it was the only packet.
        summary = describe_reply(raw_text) if raw_text else None
        if summary:
            tk.Label(resp_area, text=summary, bg=BG_CARD, fg=TEXT, font=('Segoe UI', 9),
                     padx=10, pady=10, anchor='w', justify='left', wraplength=700).pack(fill='x')
            self._render_raw_line(resp_area, raw_text)
            return

        tk.Label(resp_area, text=raw_text or '-', bg=BG_CARD, fg=TEXT_SECONDARY, font=('Consolas', 9),
                 padx=10, pady=10, anchor='w', justify='left', wraplength=700).pack(fill='x')

    def _render_reply_list(self, resp_area, entries):
        """One row per packet the device sent back this round: the raw code
        as a small tag, and its plain-English meaning as the primary text.
        The live log panel keeps the fully raw/coded form unchanged - this is
        purely the Result step's human-readable view."""
        wrap = tk.Frame(resp_area, bg=BG_CARD)
        wrap.pack(fill='x', padx=10, pady=(10, 4))
        tk.Label(wrap, text='%d PACKET%s RECEIVED' % (len(entries), '' if len(entries) == 1 else 'S'),
                 bg=BG_CARD, fg=ACCENT_BLUE, font=('Segoe UI', 9, 'bold'), anchor='w').pack(fill='x', pady=(0, 6))
        for badge, desc in entries:
            row = tk.Frame(wrap, bg=CELL_BG)
            row.pack(fill='x', pady=2)
            tk.Label(row, text=badge, bg=CELL_BG, fg=TEXT_MUTED, font=('Consolas', 8),
                    anchor='w', padx=8, pady=5, width=10).pack(side='left')
            tk.Label(row, text=desc or '(unrecognised packet)', bg=CELL_BG, fg=(TEXT if desc else TEXT_MUTED),
                    font=('Segoe UI', 9), anchor='w', justify='left', wraplength=520, padx=6, pady=5).pack(side='left', fill='x', expand=True)

    def _render_hex_breakdown(self, resp_area, prefix, segments):
        """`segments`: list of (hex_text, colour) rendered after `prefix`, one colour per decoded field."""
        box = tk.Text(resp_area, height=1, bg=BG_CARD, fg=TEXT_MUTED, font=('Consolas', 9), bd=0,
                      highlightthickness=0, wrap='none', padx=10, pady=6)
        box.insert('end', prefix)
        for text, color in segments:
            tag = 'hx_%s' % color.lstrip('#')
            box.tag_configure(tag, foreground=color)
            box.insert('end', ' ' + text, tag)
        box.configure(state='disabled')
        box.pack(fill='x')

    def _render_raw_line(self, resp_area, raw_text, note=None):
        line = 'raw: %s' % raw_text
        if note:
            line += '  (%s)' % note
        tk.Label(resp_area, text=line, bg=BG_CARD, fg=TEXT_MUTED, font=('Consolas', 8), padx=10, pady=(0, 8),
                 anchor='w', wraplength=700, justify='left').pack(fill='x')

    def _render_status_grid(self, resp_area, fields, raw_text=None):
        grid = tk.Frame(resp_area, bg=BG_CARD)
        grid.pack(fill='x', padx=10, pady=10)
        cells = [
            ('mode', 'M%d (%s)' % (fields['mode'], fields['mode_name']), ACCENT_BLUE),
            ('strip', str(fields['strip']), ACCENT_PURPLE),
            ('parfum', '%d min' % fields['parfum_min'] if fields['parfum_min'] else 'off', ACCENT_AMBER),
            ('usage', '%d min' % fields['usage_min'], ACCENT_AMBER),
            ('avg cycle', '%d min' % fields['avg_min'], ACCENT_AMBER),
            ('refills', '%d/10 (total %d)' % (fields['refill_count'], fields['total_refills']), ACCENT_GREEN),
        ]
        for i, (label, value, color) in enumerate(cells):
            cell = tk.Frame(grid, bg=CELL_BG, highlightthickness=1, highlightbackground=color)
            cell.grid(row=i // 3, column=i % 3, sticky='ew', padx=4, pady=4)
            grid.grid_columnconfigure(i % 3, weight=1)
            tk.Label(cell, text=label, bg=CELL_BG, fg=color, font=('Segoe UI', 8), anchor='w').pack(fill='x', padx=8, pady=(6, 0))
            tk.Label(cell, text=value, bg=CELL_BG, fg=TEXT, font=('Segoe UI', 10, 'bold'), anchor='w').pack(fill='x', padx=8, pady=(0, 6))

        legend = tk.Frame(resp_area, bg=BG_CARD)
        legend.pack(fill='x', padx=10, pady=(0, 4))
        for i, (name, color) in enumerate((('mode', ACCENT_BLUE), ('strip', ACCENT_PURPLE),
                                           ('timers', ACCENT_AMBER), ('counters', ACCENT_GREEN))):
            tk.Label(legend, text='\u25a0', bg=BG_CARD, fg=color, font=('Segoe UI', 9)).pack(side='left', padx=(0 if i == 0 else 10, 3))
            tk.Label(legend, text=name, bg=BG_CARD, fg=TEXT_MUTED, font=('Segoe UI', 8)).pack(side='left')

        if raw_text and len(raw_text) >= 24:
            self._render_hex_breakdown(resp_area, raw_text[:2], [
                (raw_text[2:4], ACCENT_BLUE), (raw_text[4:6], ACCENT_PURPLE),
                (raw_text[6:10], ACCENT_AMBER), (raw_text[10:14], ACCENT_AMBER), (raw_text[14:18], ACCENT_AMBER),
                (raw_text[18:20], ACCENT_GREEN), (raw_text[20:24], ACCENT_GREEN)])
        elif raw_text:
            self._render_raw_line(resp_area, raw_text)

    def _render_color_swatches(self, resp_area, hex_pairs):
        row = tk.Frame(resp_area, bg=BG_CARD)
        row.pack(fill='x', padx=10, pady=10)
        for label, hexval in hex_pairs:
            item = tk.Frame(row, bg=BG_CARD)
            item.pack(side='left', padx=(0, 16))
            hexval = (hexval or 'FFFFFF').upper()
            tk.Label(item, bg='#' + hexval, width=6, height=2, highlightthickness=1,
                    highlightbackground=BORDER_STRONG).pack()
            tk.Label(item, text='%s  #%s' % (label, hexval), bg=BG_CARD, fg=TEXT_SECONDARY,
                    font=('Consolas', 9)).pack(pady=(4, 0))

    def _render_settings_table(self, resp_area, rows):
        wrap = tk.Frame(resp_area, bg=BG_CARD)
        wrap.pack(fill='x', padx=10, pady=10)

        tk.Label(wrap, text='SMARTTV EEPROM SETTINGS  ·  %d / %d RECEIVED' % (len(rows), len(TV_SETTINGS)),
                 bg=BG_CARD, fg=ACCENT_BLUE, font=('Segoe UI', 9, 'bold'),
                 anchor='w').pack(fill='x', pady=(0, 4))

        # per-setting descriptions
        setting_descriptions = {
            'TV_ON_EFF': 'Effect shown when TV turns on (0-11)',
            'TV_OFF_EFF': 'Effect shown when TV turns off (0-7)',
            'TV_OFF_TIME': 'Time until auto power off (minutes)',
            'TV_BR_COM': 'Common area brightness (0-120)',
            'TV_BR_UCOM': 'Upper common area brightness (0-120)',
            'TV_BR_BED': 'Bed area brightness (0-120)',
            'TV_BR_LAMP': 'Lamp brightness (0-120)',
            'MOTION_BRIGHTNESS': 'Motion-triggered brightness level (0-120)',
            'MOTION_ON_TIME': 'How long motion keeps TV on (minutes)',
            'UDPRAW_AMBILIGHT_BR_MAX': 'Max ambilight brightness in UDPRAW mode (0-120)',
            'MOTION_RANDOM_COLOR': 'Enable random color during motion (0=off, 1=on)',
            'OTHER_BR_CL_DEL': 'Decrease all color channels (shift)',
            'OTHER_BR_CL_INC': 'Increase all color channels (shift)',
            'OTHER_BRIGHTNESS_AUTO': 'Auto-brightness adjustment (0=off, 1=on)',
            'OTHER_TO_OFF_TIME': 'Time to auto-off after no activity (minutes)',
            'TV_RANDOM_COLOR_START': 'Starting random color seed',
            'MOTION_DIVIDE_BRIGHTNESS': 'Divide brightness among active LEDs (0=off, 1=on)',
            'TV_BR_TV': 'TV display brightness (0-120)',
            'MOTION_RENEW_COLOR_TIME': 'Color refresh interval during motion (seconds)',
            'MOTION_AUTO_OFF_TIME': 'Auto-off timer after motion stops (minutes)',
            'MOTION_ON_EFFECT': 'Effect to show when motion detected (0-5)',
            'OTHER_AMBIENT_MODE_TIME': 'Ambient mode duration (minutes)',
            'OTHER_LED_FPS': 'LED frame rate (0-8, 8=fastest)',
            'TV_ON_BR_CL_DEL': 'Decrease color when TV turns on',
            'TV_ON_BR_CL_INC': 'Increase color when TV turns on',
            'TV_OFF_BR_CL_DEL': 'Decrease color when TV turns off',
            'TV_OFF_BR_CL_INC': 'Increase color when TV turns off',
            'MOTION_BR_CL_DEL': 'Decrease color during motion',
            'MOTION_BR_CL_INC': 'Increase color during motion',
            'UDPRAW_BR_CL_DEL': 'Decrease color in UDPRAW mode',
            'UDPRAW_BR_CL_INC': 'Increase color in UDPRAW mode',
            'HB_DUAL_COLOR': 'Heartbeat dual color mode (0=off, 1=on)',
            'HB_EFFECT': 'Heartbeat effect type (0-13)',
            'HB_EFFECT_SPEED': 'Heartbeat animation speed',
            'TV_ON_HB_EFFECT': 'Heartbeat effect when TV on (0-3)',
            'DIF_EFFECT': 'Diffuser LED effect (0-8)',
            'DIF_MODE_TV': 'Diffuser mode when TV on',
            'DIF_MODE_MOTION': 'Diffuser mode during motion',
            'DIF_MODE_UDPRAW': 'Diffuser mode in UDPRAW mode',
            'DIF_MODE_AMBIENT': 'Diffuser mode in ambient mode (0=off)',
            'DIF_IDLE_WAIT_MIN': 'Diffuser idle wait time (minutes)',
            'DIF_IDLE_ON_MIN': 'Diffuser idle on time (minutes)',
            'DIF_IDLE_MODE': 'Diffuser idle behavior mode',
            'DIF_BRIGHTNESS': 'Diffuser LED brightness (0-120)',
            'DIF_SPEED': 'Diffuser LED speed',
        }

        by_cat = {}
        order = []
        for idx, name, val in rows:
            cat = name.split('_')[0] if '_' in name else name.split(' ')[0]
            if cat not in by_cat:
                by_cat[cat] = []
                order.append(cat)
            by_cat[cat].append((idx, name, val))

        legend = tk.Frame(wrap, bg=BG_CARD)
        legend.pack(fill='x', pady=(0, 6))
        for i, cat in enumerate(order):
            color = SETTINGS_CAT_COLORS.get(cat, ACCENT_BLUE)
            tk.Label(legend, text='\u25a0', bg=BG_CARD, fg=color, font=('Segoe UI', 9)).pack(side='left', padx=(0 if i == 0 else 10, 3))
            tk.Label(legend, text=cat, bg=BG_CARD, fg=TEXT_MUTED, font=('Segoe UI', 8)).pack(side='left')

        for cat in order:
            color = SETTINGS_CAT_COLORS.get(cat, ACCENT_BLUE)
            tk.Label(wrap, text=cat, bg=BG_CARD, fg=color, font=('Segoe UI', 9, 'bold'),
                     anchor='w').pack(fill='x', pady=(8, 2))
            grid = tk.Frame(wrap, bg=BG_CARD)
            grid.pack(fill='x')
            grid.grid_columnconfigure(0, weight=1)
            for i, (idx, name, val) in enumerate(by_cat[cat]):
                cell = tk.Frame(grid, bg=CELL_BG, highlightthickness=1, highlightbackground=color)
                cell.grid(row=i, column=0, sticky='ew', padx=3, pady=2)

                setting_key = name.split(' (')[0]
                desc = setting_descriptions.get(setting_key, 'EEPROM setting')
                label = '%02X  %s\n%s' % (idx, name, desc)
                value_color = TEXT_MUTED if val == 0 else (ACCENT_GREEN if cat != 'RESERVED' else TEXT_MUTED)
                tk.Label(cell, text=label, bg=CELL_BG, fg=TEXT, font=('Segoe UI', 8), anchor='w',
                        justify='left', wraplength=240).pack(side='left', fill='x', expand=True, padx=(6, 4), pady=4)
                tk.Label(cell, text='%d\n0x%02X' % (val, val), bg=CELL_BG, fg=value_color,
                        font=('Consolas', 9, 'bold'), justify='right').pack(side='right', padx=(0, 8))

    # ------------------------------------------------------------- sending
    def _on_send(self, spec, device_key, payload, chip, resp_area, sent_vals):
        chip.configure(bg='#20344a', fg=ACCENT_BLUE, text='sending')
        for w in resp_area.winfo_children():
            w.destroy()
        tk.Label(resp_area, text='...', bg=BG_CARD, fg=TEXT_MUTED, font=('Consolas', 9), padx=10, pady=10, anchor='w').pack(fill='x')

        device = self.devices[device_key]
        threading.Thread(target=self._worker_send, args=(spec, device_key, device, payload, chip, resp_area, sent_vals),
                          daemon=True).start()

    def _worker_send(self, spec, device_key, device, payload, chip, resp_area, sent_vals):
        tag = 'DIF' if device_key == 'diffuser' else 'TV'
        transport_label = 'UDP' if spec['transport'] == 'udp' else 'TCP'
        envelope = (spec['transport'] == 'udp') and spec.get('envelope', True)
        # Per-command override - some commands (the diffuser relay family) are
        # a genuine two-hop round trip (app -> SmartTV -> diffuser -> SmartTV
        # -> app), which routinely takes longer than the device's normal
        # single-hop timeout. Confirmed live: a relayed 'Dh' request took
        # several seconds to come back even though it succeeded every time -
        # the 1200ms device default was giving up before the reply ever
        # arrived, which is what "not received" actually was.
        timeout_ms = spec.get('timeout_ms', device['timeout_ms'])

        seq = -1
        wrapped = payload
        if spec['transport'] == 'udp':
            if envelope:
                seq = self.seq
                self.seq = (self.seq + 1) % 256
                wrapped = '#' + hexb(seq, 2) + payload
            result = net.send_udp(device['ip'], device['udp_port'], wrapped, timeout_ms)
        else:
            result = net.send_tcp(device['ip'], device['telnet_port'], payload, timeout_ms + 300)

        def _finish():
            self._panel_log(tag, 'log-send', '%s > %s %s' % (transport_label, device['ip'], wrapped))
            status = result['status']

            def set_chip(kind, text):
                bgc, fgc = CHIP_COLORS.get(kind, CHIP_COLORS['none'])
                chip.configure(bg=bgc, fg=fgc, text=text)

            def set_resp(text, ack_kind=None):
                for w in resp_area.winfo_children():
                    w.destroy()
                if ack_kind:
                    self._render_ack_banner(resp_area, ack_kind)
                tk.Label(resp_area, text=text, bg=BG_CARD, fg=TEXT_MUTED, font=('Consolas', 9), padx=10, pady=10,
                        anchor='w', wraplength=700, justify='left').pack(fill='x')

            if status == 'TIMEOUT':
                set_chip('timeout', 'timeout')
                set_resp('no reply', 'timeout')
                self._panel_log(tag, 'log-err', '%s < timeout (%dms)' % (transport_label, timeout_ms))
                return
            if status == 'ERROR':
                set_chip('error', 'error')
                set_resp(result.get('message', 'error'), 'error')
                self._panel_log(tag, 'log-err', '%s < ERROR: %s' % (transport_label, result.get('message')))
                return

            replies = result['replies']
            reply_bytes = result.get('reply_bytes') or []
            if spec['transport'] == 'udp':
                # Every packet the device sent back this round, raw text kept
                # for the live log (unchanged) alongside a plain-English
                # description for the Result step - a "connect" style command
                # can draw many independent pushes (s/H/E/M/w/f/p/LM/LK/@ ...),
                # not just one, and all of them used to get silently dropped
                # except whichever happened to be picked as "the" data reply.
                all_entries = []  # (raw reply text, is_lk, badge text, description)
                for i, r in enumerate(replies):
                    raw = reply_bytes[i] if i < len(reply_bytes) else r.encode('utf-8', errors='replace')
                    # Live log stays 100% raw/coded, exactly what came over the
                    # wire - the human-readable form only ever appears in the
                    # Result step below, never here.
                    self._panel_log(tag, 'log-recv', '%s < %s' % (transport_label, r))
                    lk_summary = describe_lk_packet(raw)
                    all_entries.append((r, bool(lk_summary), 'LK' if lk_summary else r, lk_summary or describe_reply(r)))

                ack = find_ack(replies, seq) if envelope else None
                data_reply = None
                for r, is_lk, _badge, _desc in all_entries:
                    if not is_lk and not r.startswith('#'):
                        data_reply = r
                        break

                if ack:
                    ack_kind = ack['name']
                    set_chip(ack_kind, ack_kind)
                elif envelope:
                    ack_kind = 'unknown'
                    set_chip(ack_kind, 'no ack')
                else:
                    ack_kind = 'sent'
                    set_chip(ack_kind, 'sent (no envelope)')

                fallback = ''
                if not data_reply and all_entries:
                    fallback = all_entries[0][3] or all_entries[0][2]
                display_entries = [(badge, desc) for _r, _is_lk, badge, desc in all_entries]
                self._render_response(resp_area, spec, data_reply or fallback, sent_vals, display_entries, ack_kind=ack_kind)
            else:
                text = ' / '.join(replies) if replies else ''
                self._panel_log(tag, 'log-recv', '%s < %s' % (transport_label, text or '(empty)'))
                ack_kind = 'ok' if replies else 'timeout'
                set_chip(ack_kind, 'replied' if replies else 'no reply')
                set_resp(text or '(empty)', ack_kind)

        self.result_queue.put(_finish)

    def _poll_queue(self):
        try:
            while True:
                job = self.result_queue.get_nowait()
                try:
                    job()
                except Exception:
                    pass
        except queue.Empty:
            pass
        self.root.after(50, self._poll_queue)

    # ------------------------------------------------------------- keep-alive
    def _toggle_keep_alive(self):
        if self.keep_alive_on:
            self.keep_alive_stop.set()
            self.keep_alive_on = False
            self.keepalive_btn.configure(text='Start keep-alive')
            self.keepalive_label.configure(text='Keep-alive: off')
            self.keepalive_dot.configure(fg=TEXT_MUTED)
            self._log_sys('keep-alive stopped')
        else:
            self.keep_alive_stop = threading.Event()
            self.keep_alive_on = True
            self.keepalive_btn.configure(text='Stop keep-alive')
            self.keepalive_label.configure(text='Keep-alive: on')
            self.keepalive_dot.configure(fg=ACCENT_GREEN)
            self._log_sys('keep-alive started (pings the active device every 5s)')
            self.keep_alive_thread = threading.Thread(target=self._keep_alive_loop, daemon=True)
            self.keep_alive_thread.start()

    def _keep_alive_loop(self):
        stop_event = self.keep_alive_stop
        while not stop_event.is_set():
            device_key = self.active_device
            device = self.devices[device_key]
            if device_key == 'smarttv':
                payload, envelope = 'k', False
            else:
                payload, envelope = 'Dc', True
            wrapped = payload
            if envelope:
                seq = self.seq
                self.seq = (self.seq + 1) % 256
                wrapped = '#' + hexb(seq, 2) + payload
            # Keep-alive doesn't expect a reply, use very short timeout (100ms)
            # TIMEOUT is considered success for keep-alive pings
            result = net.send_udp(device['ip'], device['udp_port'], wrapped, 100)

            def _finish(dk=device_key, res=result, wp=wrapped):
                tag = 'DIF' if dk == 'diffuser' else 'TV'
                # Keep-alive pings don't expect replies, so TIMEOUT is considered success
                ok = res['status'] == 'OK' or res['status'] == 'TIMEOUT'
                self.last_heartbeat[dk] = time.time()
                if dk == self.active_device:
                    when = 'just now' if ok else res['status'].lower()
                    self.keepalive_label.configure(text='Keep-alive: on (%s)' % when)
                self._panel_log(tag, 'log-send' if ok else 'log-err',
                                'keep-alive %s -> %s' % (wp, res['status']))
            self.result_queue.put(_finish)

            if stop_event.wait(5.0):
                break

    # ------------------------------------------------------------- raw box
    def _build_rawbox(self, parent):
        frame = tk.Frame(parent, bg=BG_PANEL)
        tk.Label(frame, text='Raw command', bg=BG_PANEL, fg=TEXT_MUTED,
                 font=('Segoe UI', 9, 'bold')).pack(side='left', padx=(12, 8), pady=8)
        self.raw_transport_var = tk.StringVar()
        self.raw_transport_cb = ttk.Combobox(frame, state='readonly', textvariable=self.raw_transport_var, width=14)
        self.raw_transport_cb.pack(side='left')
        self.raw_payload_var = tk.StringVar()
        ent = tk.Entry(frame, textvariable=self.raw_payload_var, bg=BG_FIELD, fg=TEXT, insertbackground=TEXT,
                       highlightthickness=1, highlightbackground=BORDER_STRONG, relief='flat', font=('Consolas', 10))
        ent.pack(side='left', fill='x', expand=True, padx=8)
        self.raw_envelope_var = tk.BooleanVar(value=True)
        tk.Checkbutton(frame, text='wrap with #SS ack envelope', variable=self.raw_envelope_var, bg=BG_PANEL,
                      fg=TEXT_MUTED, selectcolor=BG_FIELD, activebackground=BG_PANEL,
                      font=('Segoe UI', 8)).pack(side='left', padx=6)
        tk.Button(frame, text='Send raw', bg=BTN_GREEN_BG, fg=BTN_GREEN_FG, relief='flat', bd=0,
                 font=('Segoe UI', 9, 'bold'), padx=12, pady=4, command=self._send_raw).pack(side='left', padx=(6, 12))
        return frame

    def _send_raw(self):
        device_key = self.active_device
        device = self.devices[device_key]
        payload = self.raw_payload_var.get()
        if not payload:
            return
        transport = 'udp' if self.raw_transport_cb.get().startswith('UDP') else 'telnet'
        envelope = self.raw_envelope_var.get()
        tag = 'DIF' if device_key == 'diffuser' else 'TV'

        def _worker():
            if transport == 'udp':
                wrapped = payload
                seq = -1
                if envelope:
                    seq = self.seq
                    self.seq = (self.seq + 1) % 256
                    wrapped = '#' + hexb(seq, 2) + payload
                result = net.send_udp(device['ip'], device['udp_port'], wrapped, device['timeout_ms'])

                def _finish():
                    self._panel_log(tag, 'log-send', 'UDP(raw) > %s %s%s' %
                                    (device['ip'], wrapped, ' [enveloped]' if envelope else ''))
                    if result['status'] == 'OK':
                        for r in result['replies']:
                            self._panel_log(tag, 'log-recv', 'UDP(raw) < %s' % r)
                    else:
                        msg = result.get('message')
                        self._panel_log(tag, 'log-err', 'UDP(raw) < %s%s' % (result['status'], (': ' + msg) if msg else ''))
                self.result_queue.put(_finish)
            else:
                result = net.send_tcp(device['ip'], device['telnet_port'], payload, device['timeout_ms'] + 300)

                def _finish():
                    self._panel_log(tag, 'log-send', 'TCP(raw) > %s:%d %s' % (device['ip'], device['telnet_port'], payload))
                    if result['status'] == 'OK':
                        self._panel_log(tag, 'log-recv', 'TCP(raw) < %s' % ' / '.join(result['replies']))
                    else:
                        msg = result.get('message')
                        self._panel_log(tag, 'log-err', 'TCP(raw) < %s%s' % (result['status'], (': ' + msg) if msg else ''))
                self.result_queue.put(_finish)

        threading.Thread(target=_worker, daemon=True).start()

    # ------------------------------------------------------------- log panel
    def _build_logpanel(self, parent):
        frame = tk.Frame(parent, bg=BG_PANEL, height=170)
        frame.pack_propagate(False)
        head = tk.Frame(frame, bg=BG_PANEL)
        head.pack(fill='x')
        tk.Label(head, text='LIVE LOG', bg=BG_PANEL, fg=TEXT_MUTED, font=('Segoe UI', 8, 'bold')).pack(side='left', padx=14, pady=7)
        tk.Button(head, text='Clear panel', bg=BG_BTN, fg=TEXT_SECONDARY, relief='flat', bd=0, font=('Segoe UI', 8),
                 padx=8, pady=2, command=self._clear_log_panel).pack(side='right', padx=14, pady=6)
        divider = tk.Frame(frame, bg=BORDER, height=1)
        divider.pack(fill='x')

        self.log_text = tk.Text(frame, bg=BG_PANEL, fg=TEXT, insertbackground=TEXT, font=('Consolas', 9),
                                wrap='word', bd=0, highlightthickness=0, state='disabled',
                                padx=6, spacing1=3, spacing3=5)
        self.log_text.pack(fill='both', expand=True, padx=12, pady=(6, 8))
        self.log_text.tag_configure('tagdif', foreground=ACCENT_BLUE)
        self.log_text.tag_configure('tagtv', foreground=ACCENT_PURPLE)
        self.log_text.tag_configure('log-send', foreground=ACCENT_GREEN)
        self.log_text.tag_configure('log-recv', foreground=TEXT_SECONDARY)
        self.log_text.tag_configure('log-err', foreground=ACCENT_RED)
        self.log_text.tag_configure('sys', foreground=TEXT_MUTED)
        return frame

    # ------------------------------------------------------------- device switch
    def switch_device(self, key):
        self.active_device = key
        for k, btn in self.tab_buttons.items():
            if k == key:
                btn.configure(bg=BG, fg=ACCENT_BLUE)
            else:
                btn.configure(bg=BG_SIDEBAR, fg=TEXT_SECONDARY)

        table = self.command_tables[key]
        self.filter_var.set('')
        self._render_sidebar(key, table)

        device = self.devices[key]
        self.conn_ip_var.set(device['ip'])
        self.conn_udp_var.set(str(device['udp_port']))
        self.conn_telnet_var.set(str(device['telnet_port']))
        self.conn_timeout_var.set(str(device['timeout_ms']))
        if device['has_console']:
            self.conn_telnet_frame.grid()
            self.conn_note_label.configure(text='')
        else:
            self.conn_telnet_frame.grid_remove()
            self.conn_note_label.configure(
                text='UDP only on this firmware (port %d) - no Serial/Telnet console' % device['udp_port'])

        values = ['UDP :%d' % device['udp_port']]
        if device['has_console']:
            values.append('Telnet :%d' % device['telnet_port'])
        self.raw_transport_cb.configure(values=values)
        self.raw_transport_cb.current(0)

        sel_id = self.selected_id.get(key)
        if sel_id:
            spec = next((s for s in table if s['id'] == sel_id), table[0])
            self._select_command(key, spec)

    def _on_close(self):
        self.keep_alive_stop.set()
        try:
            self.log_file.close()
        except Exception:
            pass
        self.root.destroy()


def run(base_dir):
    root = tk.Tk()
    root.title('FuZz TestMode - Diffuser / SmartTV Command Console')
    root.geometry('1360x880')
    root.minsize(1000, 640)
    App(root, base_dir)
    root.mainloop()
