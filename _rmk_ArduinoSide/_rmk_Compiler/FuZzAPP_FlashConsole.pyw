"""FuZzAPP_FlashConsole.pyw - Flash Console for SmartTV and Diffuser

Top device selection buttons for Smart TV R4 Wifi and Diffuser - WeMos D1 Mini.
Build/flash controls and docked console on the right.
Console source toggle: Serial ↔ Telnet.
"""

import atexit
import os
import subprocess
import sys
import time
import threading
import re
import tkinter as tk
from tkinter import ttk, scrolledtext, filedialog

# Add .res to path for imports
res_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '.res')
sys.path.insert(0, res_path)
from network import SerialConnection, TelnetConnection, get_serial_ports, ping_host
from compiler import ArduinoCompiler

_LOCK_FILE = os.path.join(res_path, '.flashconsole.lock')


def _load_ota_password():
    """Read OTA_PASSWORD_VALUE out of the shared, gitignored
    ../_rmk_Shared/WiFiCredentials.h (same file the _rmk_ Arduino sketches
    read their WiFi/OTA credentials from - see WiFiCredentials.h.example)
    so the OTA password lives in exactly one place. Returns None if that
    file hasn't been created yet (fresh clone before copying the example)."""
    creds_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '_rmk_Shared', 'WiFiCredentials.h')
    try:
        with open(creds_path, 'r', encoding='utf-8') as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r'OTA_PASSWORD_VALUE\s+"([^"]*)"', text)
    return m.group(1) if m else None

if sys.platform == 'win32':
    _TASKKILL_FLAGS = subprocess.CREATE_NO_WINDOW
else:
    _TASKKILL_FLAGS = 0


def _enforce_single_instance():
    """Kill any previously-running FlashConsole so only one instance is ever
    open. Two live instances fighting over the same COM port / device state
    is what caused a stale, pre-update window to be mistaken for a fresh one."""
    try:
        if os.path.exists(_LOCK_FILE):
            old_pid = open(_LOCK_FILE).read().strip()
            if old_pid.isdigit() and int(old_pid) != os.getpid():
                subprocess.run(['taskkill', '/PID', old_pid, '/F'],
                              capture_output=True, creationflags=_TASKKILL_FLAGS)
    except Exception:
        pass  # best-effort - a stale/unreadable lock file must never block startup

    try:
        with open(_LOCK_FILE, 'w') as f:
            f.write(str(os.getpid()))
    except Exception:
        pass

    atexit.register(_release_single_instance_lock)


def _release_single_instance_lock():
    try:
        if open(_LOCK_FILE).read().strip() == str(os.getpid()):
            os.remove(_LOCK_FILE)
    except Exception:
        pass

# Theme: dark graphite, professional & low-glare (matching TestMode)
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


class Device:
    """Device configuration"""
    def __init__(self, device_id, name, button_text, device_type, sketch_path,
                 serial_port=None, ip=None, port=None, telnet_port=None, fqbn="arduino:avr:uno",
                 baud=115200, ota_password=None):
        self.device_id = device_id
        self.name = name
        self.button_text = button_text
        self.device_type = device_type  # 'serial' or 'wifi'
        self.sketch_path = sketch_path
        self.serial_port = serial_port
        self.ip = ip
        self.port = port
        self.telnet_port = telnet_port
        self.fqbn = fqbn
        self.baud = baud
        self.ota_password = ota_password


class FlashConsole:
    def __init__(self, root):
        self.root = root
        self.root.title("FuZzAPP Flash Console (rmk)")
        self.root.geometry("1000x750")
        self.root.configure(bg=BG)
        self._set_window_icon()

        # Configure modern styling
        self._setup_styles()
        
        # Compiler instance
        self.arduino_cli_path = r"C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"  # Default Arduino IDE path
        self.compiler = ArduinoCompiler(self.arduino_cli_path)
        
        # Resolve sketch paths relative to script directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        
        # Devices configuration
        self.devices = [
            Device(
                device_id='smarttv',
                name='SmartTV R4 (rmk)',
                button_text='Smart TV R4 (rmk)',
                device_type='serial',
                sketch_path=os.path.join(script_dir, '../_rmk_SmartTV_R4/_rmk_SmartTV_R4.ino'),
                serial_port=None,  # Auto-detect
                telnet_port=23,
                fqbn='arduino:renesas_uno:unor4wifi'  # Correct FQBN for UNO R4 WiFi
            ),
            Device(
                device_id='diffuser',
                name='Diffuser (rmk)',
                button_text='Diffuser - WeMos D1 Mini (rmk)',
                device_type='wifi',
                sketch_path=os.path.join(script_dir, '../_rmk_Diffuser/_rmk_Diffuser.ino'),
                ip='192.168.1.203',
                # 8266 is ArduinoOTA's default listen port (the sketch never
                # calls ArduinoOTA.setPort(), so it stays at the default) - this
                # is the actual OTA upload port, NOT the diffuser's own 8439 UDP
                # command port (see Globals.h DIF_UDP_PORT on the SmartTV relay
                # side). Flashing to 8439 would silently target the wrong
                # listener and fail.
                port=8266,
                telnet_port=23,
                # d1_mini, not the frozen tool's plain "d1" - this is the FQBN
                # _rmk_Diffuser was actually built/flashed against and verified
                # working (00_PLAN.md Phase 3/7 notes).
                fqbn='esp8266:esp8266:d1_mini',
                ota_password=_load_ota_password()  # from ../_rmk_Shared/WiFiCredentials.h - must match OTA_PASSWORD_VALUE there
            )
        ]
        
        self.active_device = self.devices[0]
        
        # Connections
        self.serial_connection = None
        self.telnet_connection = None
        self._resume_connections = []  # [(kind, arg1, arg2), ...] to reopen after flash
        # Guards against a second Compile/Flash click spawning a concurrent,
        # overlapping operation while one is already running.
        self._operation_busy = False
        # Bumped whenever a telnet connect/disconnect happens by any means -
        # lets a background auto-reconnect chain notice it's been superseded
        # and stop itself instead of running on indefinitely. See
        # _reconnect_telnet_with_retry.
        self._telnet_reconnect_token = 0
        # Same idea as _telnet_reconnect_token, for the post-flash serial
        # reconnect chain. See _reconnect_serial_with_retry.
        self._serial_reconnect_token = 0
        
        # UI state
        self.serial_ports = []
        self.flash_port_var = tk.StringVar(value='')  # Configuration-panel port (source of truth for Flash)
        self.console_update_timer = None
        
        self._build_ui()
        self._update_button_styles()
        self._refresh_serial_ports()
        self._start_console_update()
        self._start_status_monitor()

        # Derive the minimum window size from what the layout actually needs
        # (button rows, the WiFi/Telnet config line, etc.) instead of a guessed
        # constant, so shrinking the window can never clip or hide a control.
        self.root.update_idletasks()
        min_w, min_h = self.root.winfo_reqwidth(), self.root.winfo_reqheight()
        self.root.minsize(min_w, min_h)
        if min_w > 1000 or min_h > 750:
            self.root.geometry(f"{max(1000, min_w)}x{max(750, min_h)}")

        # Check if arduino-cli is available (after UI is built)
        available, version = self.compiler.check_arduino_cli()
        if not available:
            self._log(self.build_output, "Warning: arduino-cli not found in PATH")
            self._log(self.build_output, "Please install arduino-cli or configure the path")
        else:
            self._log(self.build_output, f"arduino-cli found: {version.strip()}")
        
    def _set_window_icon(self):
        icon_path = os.path.join(res_path, 'fuzz_icon.png')
        try:
            self._icon_image = tk.PhotoImage(file=icon_path)  # kept alive on self
            self.root.iconphoto(True, self._icon_image)
        except Exception:
            pass  # icon is cosmetic only - never block startup over it

    def _setup_styles(self):
        """Configure styling to match TestMode design"""
        style = ttk.Style()
        style.theme_use('clam')
        
        # Configure frames
        style.configure('TFrame', background=BG)
        style.configure('TPanel', background=BG_PANEL, relief='flat')
        
        # Configure labels
        style.configure('TLabel', background=BG, foreground=TEXT, font=('Arial', 9))
        style.configure('Header.TLabel', background=BG, foreground=TEXT, font=('Arial', 11, 'bold'))
        style.configure('Title.TLabel', background=BG_PANEL, foreground=TEXT, font=('Arial', 10, 'bold'))
        style.configure('Muted.TLabel', background=BG, foreground=TEXT_SECONDARY, font=('Arial', 8))
        
        # Configure buttons
        # bordercolor/lightcolor/darkcolor pinned to the button's own background -
        # 'clam' still paints its default bevel colors even at borderwidth=0
        # otherwise, which showed up as a stray-colored sliver on one edge.
        style.configure('TButton',
                       background=BG_BTN,
                       foreground=TEXT,
                       borderwidth=0,
                       relief='flat',
                       bordercolor=BG_BTN,
                       lightcolor=BG_BTN,
                       darkcolor=BG_BTN,
                       focuscolor='none',
                       font=('Arial', 9, 'bold'),
                       padding=(15, 8))
        style.map('TButton',
                 background=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 bordercolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 lightcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 darkcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)])

        style.configure('Secondary.TButton',
                       background=BG_BTN,
                       foreground=TEXT_SECONDARY,
                       borderwidth=0,
                       relief='flat',
                       bordercolor=BG_BTN,
                       lightcolor=BG_BTN,
                       darkcolor=BG_BTN,
                       focuscolor='none',
                       font=('Arial', 9),
                       padding=(15, 8))
        style.map('Secondary.TButton',
                 background=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 bordercolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 lightcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 darkcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)])
        
        # Configure progress bar with teal color. Same 'clam' bevel issue as the
        # buttons/entries above - pin border/light/dark colors to the trough
        # color so no stray-colored stroke outlines the bar.
        style.configure('Horizontal.TProgressbar',
                       background='#008080',  # Teal color
                       troughcolor=BG_FIELD,
                       borderwidth=0,
                       bordercolor=BG_FIELD,
                       lightcolor='#008080',
                       darkcolor='#008080',
                       thickness=8)
        style.configure('TProgressbar',
                       background='#008080',  # Teal color
                       troughcolor=BG_FIELD,
                       borderwidth=0,
                       bordercolor=BG_FIELD,
                       lightcolor='#008080',
                       darkcolor='#008080',
                       thickness=8)
        
        style.configure('Device.TButton',
                       background=BG_BTN,
                       foreground=TEXT,
                       borderwidth=0,
                       relief='flat',
                       bordercolor=BG_BTN,
                       lightcolor=BG_BTN,
                       darkcolor=BG_BTN,
                       focuscolor='none',
                       font=('Arial', 10, 'bold'),
                       padding=(20, 12))
        style.map('Device.TButton',
                 background=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 bordercolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 lightcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)],
                 darkcolor=[('active', BG_BTN_HOVER), ('pressed', BG_BTN_HOVER)])

        style.configure('DeviceActive.TButton',
                       background=BG_ROW_SELECTED,
                       foreground=TEXT,
                       borderwidth=0,
                       relief='flat',
                       bordercolor=BG_ROW_SELECTED,
                       lightcolor=BG_ROW_SELECTED,
                       darkcolor=BG_ROW_SELECTED,
                       focuscolor='none',
                       font=('Arial', 10, 'bold'),
                       padding=(20, 12))
        
        # Configure entries
        # 'clam' paints a light bevel (lightcolor/darkcolor) around entries even
        # at borderwidth=0 unless it's pinned - that's the stray white stroke.
        # Give it a real, subtle dark border instead, with a blue ring on focus.
        style.configure('TEntry',
                       background=BG_FIELD,
                       foreground=TEXT,
                       fieldbackground=BG_FIELD,
                       borderwidth=1,
                       relief='flat',
                       bordercolor=BORDER,
                       lightcolor=BG_FIELD,
                       darkcolor=BG_FIELD,
                       insertcolor=TEXT,
                       padding=4,
                       font=('Arial', 9))
        style.map('TEntry',
                 bordercolor=[('focus', ACCENT_BLUE), ('!focus', BORDER)],
                 lightcolor=[('focus', ACCENT_BLUE)],
                 darkcolor=[('focus', ACCENT_BLUE)])

        # Configure combobox
        style.configure('TCombobox',
                       background=BG_FIELD,
                       foreground=TEXT,
                       fieldbackground=BG_FIELD,
                       arrowcolor=TEXT_SECONDARY,
                       borderwidth=1,
                       relief='flat',
                       bordercolor=BORDER,
                       lightcolor=BG_FIELD,
                       darkcolor=BG_FIELD,
                       padding=4,
                       font=('Arial', 9))
        # The 'clam' theme swaps in its own washed-out colors for the readonly
        # state (used by the Console's Serial/Baud dropdowns) unless overridden -
        # pin every state to the same dark-theme colors as the editable
        # Configuration-panel port dropdown so they look like one design.
        style.map('TCombobox',
                 fieldbackground=[('readonly', BG_FIELD), ('disabled', BG_FIELD)],
                 foreground=[('readonly', TEXT), ('disabled', TEXT_MUTED)],
                 background=[('readonly', BG_FIELD)],
                 bordercolor=[('focus', ACCENT_BLUE), ('!focus', BORDER)],
                 arrowcolor=[('readonly', TEXT_SECONDARY)])
        
        # Configure labelframe
        style.configure('TLabelframe',
                       background=BG_PANEL,
                       foreground=TEXT,
                       borderwidth=0,
                       relief='flat')
        style.configure('TLabelframe.Label',
                       background=BG_PANEL,
                       foreground=TEXT,
                       font=('Arial', 9, 'bold'))
        
        # Configure panedwindow
        style.configure('TPanedwindow', background=BG, sashwidth=4)
        
        # Configure notebook (tabs)
        style.configure('TNotebook', background=BG, borderwidth=0)
        style.configure('TNotebook.Tab', 
                       background=BG_BTN,
                       foreground=TEXT,
                       padding=(20, 10),
                       font=('Arial', 9))
        style.map('TNotebook.Tab',
                 background=[('selected', BG_PANEL)],
                 foreground=[('selected', TEXT)])
        
        # Configure scrollbar
        style.configure('TScrollbar',
                       background=BG_BTN,
                       troughcolor=BG,
                       borderwidth=0,
                       arrowsize=12)
        
    def _build_ui(self):
        # Main container
        main_frame = tk.Frame(self.root, bg=BG)
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Branded header bar
        self._build_header(main_frame)

        # Top device selection buttons
        self._build_device_buttons(main_frame)
        
        # Right panel - controls and console
        controls_frame = tk.Frame(main_frame, bg=BG)
        controls_frame.pack(fill=tk.BOTH, expand=True, padx=15, pady=(5, 15))
        
        # Vertical split for controls and console
        right_pane = tk.PanedWindow(controls_frame, orient=tk.VERTICAL, 
                                   bg=BG, sashwidth=4, sashrelief=tk.FLAT)
        right_pane.pack(fill=tk.BOTH, expand=True)
        
        # Top panel - build/flash controls
        self.build_frame = tk.Frame(right_pane, bg=BG)
        right_pane.add(self.build_frame, minsize=300)
        
        # Bottom panel - console
        self.console_frame = tk.Frame(right_pane, bg=BG)
        right_pane.add(self.console_frame, minsize=300)
        
        self._build_controls()
        self._build_console()
        
    def _build_header(self, parent):
        header = tk.Frame(parent, bg=BG_SIDEBAR, height=52)
        header.pack(fill=tk.X, side=tk.TOP)
        header.pack_propagate(False)

        # Thumbnail of the brand icon (512px source -> 32px via subsample)
        try:
            self._header_logo = self._icon_image.subsample(16, 16)
            tk.Label(header, image=self._header_logo, bg=BG_SIDEBAR).pack(
                side=tk.LEFT, padx=(18, 10))
        except Exception:
            pass

        title_box = tk.Frame(header, bg=BG_SIDEBAR)
        title_box.pack(side=tk.LEFT, pady=8)
        tk.Label(title_box, text="FuZzAPP Flash Console", bg=BG_SIDEBAR, fg=WHITE,
                font=('Arial', 13, 'bold')).pack(anchor='w')
        tk.Label(title_box, text="Arduino build • flash • device console", bg=BG_SIDEBAR,
                fg=TEXT_MUTED, font=('Arial', 8)).pack(anchor='w')

        # Thin accent underline to separate header from the rest of the window
        tk.Frame(parent, bg=ACCENT_BLUE, height=2).pack(fill=tk.X, side=tk.TOP)

    def _build_device_buttons(self, parent):
        # Top button bar with TestMode styling: device selection on the left,
        # Compile/Flash actions on the right of the same row.
        button_frame = tk.Frame(parent, bg=BG)
        button_frame.pack(fill=tk.X, padx=15, pady=(15, 5))

        # Device selection buttons
        self.device_buttons = []

        for i, device in enumerate(self.devices):
            btn_style = 'DeviceActive.TButton' if i == 0 else 'Device.TButton'
            btn = ttk.Button(
                button_frame,
                text=device.button_text,
                style=btn_style,
                command=lambda d=device: self._select_device(d)
            )
            btn.pack(side=tk.LEFT, padx=(0 if i == 0 else 10, 0))
            self.device_buttons.append(btn)

        # Compile/Flash actions, right-aligned on the same row. Progress bar and
        # status live inside this same frame (below the buttons) so the bar's
        # width is pinned exactly to the Compile-button-start/Flash-button-end
        # span, rather than the full window width.
        action_frame = tk.Frame(button_frame, bg=BG)
        action_frame.pack(side=tk.RIGHT)

        btn_row = tk.Frame(action_frame, bg=BG)
        btn_row.pack(fill=tk.X)

        # Fixed character width so the button doesn't resize when its label
        # toggles between "Compile" and "Stop" while a build is running.
        self.compile_btn = ttk.Button(btn_row, text="Compile", width=10,
                                     command=self._on_compile_button)
        self.compile_btn.pack(side=tk.LEFT, padx=(0, 8))

        self.flash_btn = ttk.Button(btn_row, text="Flash", width=10, command=self._flash)
        self.flash_btn.pack(side=tk.LEFT)

        self.progress_var = tk.DoubleVar(value=0)
        self.progress_bar = ttk.Progressbar(action_frame, variable=self.progress_var,
                                          maximum=100, mode='determinate')
        self.progress_bar.pack(fill=tk.X, pady=(8, 4))

        # status_var still gets updated (Ready/Compiling/Success/Error/elapsed
        # time) - kept around since other code reads/sets it - but no label is
        # shown for it anymore; Build Output already logs the same events.
        self.status_var = tk.StringVar(value="Ready")

    def _build_controls(self):
        # Configuration frame with TestMode card style
        config_frame = ttk.LabelFrame(self.build_frame, text="Configuration", style='TLabelframe')
        config_frame.pack(fill=tk.X, padx=15, pady=(5, 10))
        
        # Sketch path with TestMode styling
        path_frame = tk.Frame(config_frame, bg=BG_PANEL)
        path_frame.pack(fill=tk.X, padx=10, pady=10)
        
        ttk.Label(path_frame, text="Sketch:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.sketch_path_var = tk.StringVar(value=self.active_device.sketch_path)
        sketch_entry = ttk.Entry(path_frame, textvariable=self.sketch_path_var, style='TEntry')
        sketch_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(5, 0))
        
        browse_btn = ttk.Button(path_frame, text="Browse", command=self._browse_sketch, 
                               style='Secondary.TButton')
        browse_btn.pack(side=tk.LEFT, padx=(5, 0))
        
        # Device-specific configuration
        self.device_config_frame = tk.Frame(config_frame, bg=BG_PANEL)
        self.device_config_frame.pack(fill=tk.X, padx=10, pady=(0, 10))
        self._update_device_config()
        
        # Build output with TestMode card style
        output_frame = ttk.LabelFrame(self.build_frame, text="Build Output", style='TLabelframe')
        output_frame.pack(fill=tk.BOTH, expand=True, padx=15, pady=(0, 10))
        
        self.build_output = scrolledtext.ScrolledText(output_frame, bg=BG_FIELD, fg=TEXT,
                                                       insertbackground=TEXT, height=8,
                                                       font=('Consolas', 9),
                                                       relief='flat', borderwidth=0)
        self.build_output.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        # Timestamp column color (tag/message columns default to fg=TEXT)
        self.build_output.tag_config('ts', foreground=TEXT_MUTED)
        
    def _build_console(self):
        # Console header with source toggle - TestMode styling
        header_frame = tk.Frame(self.console_frame, bg=BG)
        header_frame.pack(fill=tk.X, padx=15, pady=(15, 10))
        
        ttk.Label(header_frame, text="Console", style='Header.TLabel').pack(side=tk.LEFT)
        
        # Source toggle with TestMode styling
        # Connection controls with TestMode styling - both Serial and Telnet can be active
        conn_frame = tk.Frame(self.console_frame, bg=BG)
        conn_frame.pack(fill=tk.X, padx=15, pady=(0, 10))
        
        # Serial controls on left
        self.serial_conn_frame = tk.Frame(conn_frame, bg=BG)
        self.serial_conn_frame.pack(side=tk.LEFT, padx=(0, 20))
        
        ttk.Label(self.serial_conn_frame, text="Serial:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.serial_port_var = tk.StringVar(value=self.active_device.serial_port or '')
        self.serial_port_combo = ttk.Combobox(self.serial_conn_frame, textvariable=self.serial_port_var,
                                             values=[], width=10, state="readonly")
        self.serial_port_combo.pack(side=tk.LEFT, padx=(5, 5))
        
        ttk.Label(self.serial_conn_frame, text="Baud:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.baud_var = tk.IntVar(value=self.active_device.baud)
        baud_combo = ttk.Combobox(self.serial_conn_frame, textvariable=self.baud_var,
                                 values=[9600, 19200, 38400, 57600, 115200], 
                                 width=6, state="readonly")
        baud_combo.pack(side=tk.LEFT, padx=(5, 5))
        
        self.serial_connect_btn = ttk.Button(self.serial_conn_frame, text="Connect",
                                            command=self._connect_serial,
                                            style='TButton')
        self.serial_connect_btn.pack(side=tk.LEFT, padx=(0, 5))
        
        self.serial_disconnect_btn = ttk.Button(self.serial_conn_frame, text="Disconnect",
                                               command=self._disconnect_serial,
                                               style='TButton',
                                               state=tk.DISABLED)
        self.serial_disconnect_btn.pack(side=tk.LEFT)
        
        # Telnet controls on right
        self.telnet_conn_frame = tk.Frame(conn_frame, bg=BG)
        self.telnet_conn_frame.pack(side=tk.LEFT)
        
        ttk.Label(self.telnet_conn_frame, text="Telnet:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.telnet_ip_var = tk.StringVar(value=self.active_device.ip or '192.168.1.203')
        ttk.Entry(self.telnet_conn_frame, textvariable=self.telnet_ip_var,
                 style='TEntry', width=12).pack(side=tk.LEFT, padx=(5, 5))
        
        ttk.Label(self.telnet_conn_frame, text="Port:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.telnet_port_var = tk.IntVar(value=self.active_device.telnet_port or 23)
        ttk.Entry(self.telnet_conn_frame, textvariable=self.telnet_port_var,
                 style='TEntry', width=5).pack(side=tk.LEFT, padx=(5, 5))
        
        self.telnet_connect_btn = ttk.Button(self.telnet_conn_frame, text="Connect",
                                            command=self._connect_telnet,
                                            style='TButton')
        self.telnet_connect_btn.pack(side=tk.LEFT, padx=(0, 5))
        
        self.telnet_disconnect_btn = ttk.Button(self.telnet_conn_frame, text="Disconnect",
                                               command=self._disconnect_telnet,
                                               style='TButton',
                                               state=tk.DISABLED)
        self.telnet_disconnect_btn.pack(side=tk.LEFT)
        
        # Clear console button
        clear_btn = ttk.Button(conn_frame, text="Clear", command=self._clear_console,
                              style='Secondary.TButton')
        clear_btn.pack(side=tk.RIGHT, padx=(10, 0))
        
        # Console output with TestMode card style
        console_output_frame = ttk.LabelFrame(self.console_frame, text="Output", style='TLabelframe')
        console_output_frame.pack(fill=tk.BOTH, expand=True, padx=15, pady=(0, 10))
        
        self.console_output = scrolledtext.ScrolledText(console_output_frame, bg=BG_FIELD, fg=TEXT,
                                                         insertbackground=TEXT, height=12,
                                                         font=('Consolas', 9),
                                                         relief='flat', borderwidth=0)
        self.console_output.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Configure tags for colored output
        self.console_output.tag_config('serial', foreground='#FF6600')  # Orange for serial
        self.console_output.tag_config('telnet', foreground='#00CCCC')   # Cyan for telnet
        self.console_output.tag_config('sent', foreground=ACCENT_AMBER)  # Amber for commands you sent
        self.console_output.tag_config('ts', foreground=TEXT_MUTED)      # Muted for timestamp column
        
        # Command input with TestMode styling
        cmd_frame = tk.Frame(self.console_frame, bg=BG)
        cmd_frame.pack(fill=tk.X, padx=15, pady=(0, 15))
        
        ttk.Label(cmd_frame, text="Command:", style='Muted.TLabel').pack(side=tk.LEFT)
        self.console_cmd_var = tk.StringVar()
        # Fixed, compact width - these are short device commands (M2, P30, D, ...),
        # not long text, so it doesn't need to expand and fill the row.
        cmd_entry = ttk.Entry(cmd_frame, textvariable=self.console_cmd_var, style='TEntry', width=30)
        cmd_entry.pack(side=tk.LEFT, padx=(5, 10))
        cmd_entry.bind('<Return>', lambda e: self._send_console_command())

        # Where to send the command: both / serial-only / telnet-only. Sits
        # directly to the left of Send.
        self.cmd_target_var = tk.StringVar(value='both')
        target_combo = ttk.Combobox(cmd_frame, textvariable=self.cmd_target_var,
                                   values=['both', 'serial', 'telnet'], width=7, state="readonly")
        target_combo.pack(side=tk.LEFT, padx=(0, 10))

        send_btn = ttk.Button(cmd_frame, text="Send", command=self._send_console_command)
        send_btn.pack(side=tk.LEFT)

    def _update_button_styles(self):
        """Update button styles to show active device"""
        for i, (btn, device) in enumerate(zip(self.device_buttons, self.devices)):
            if device == self.active_device:
                btn.configure(style='DeviceActive.TButton')
            else:
                btn.configure(style='Device.TButton')
                
    def _update_device_config(self):
        # Clear previous config
        for widget in self.device_config_frame.winfo_children():
            widget.destroy()
            
        device = self.active_device
        
        if device.device_type == 'serial':
            # Serial port config - same compact single-row layout as the WiFi
            # config below, so switching between device tabs doesn't jump size.
            port_frame = tk.Frame(self.device_config_frame, bg=BG_CARD)
            port_frame.pack(fill=tk.X, pady=(5, 10))

            ttk.Label(port_frame, text="Port:", style='Muted.TLabel').pack(side=tk.LEFT)
            # Auto-fill with the first detected Arduino port so the field is
            # never blank when a board is actually plugged in; still editable
            # and offers the rest of the detected ports as a dropdown.
            self.flash_port_var.set(device.serial_port or (self.serial_ports[0] if self.serial_ports else ''))
            self.flash_port_combo = ttk.Combobox(port_frame, textvariable=self.flash_port_var,
                                                values=self.serial_ports, width=13)
            self.flash_port_combo.pack(side=tk.LEFT, padx=(5, 10))

            ttk.Button(port_frame, text="Refresh", command=self._refresh_serial_ports,
                      style='Secondary.TButton').pack(side=tk.LEFT)

            # Online/Offline: is the configured port actually enumerated right
            # now (board plugged in over USB)? Kept live by _check_device_status.
            self.device_status_var = tk.StringVar(value="● Checking...")
            self.device_status_label = tk.Label(port_frame, textvariable=self.device_status_var,
                                                bg=BG_CARD, fg=TEXT_MUTED, font=('Arial', 9, 'bold'))
            self.device_status_label.pack(side=tk.LEFT, padx=(10, 0))

            if self.serial_ports:
                ttk.Label(port_frame, text=f"Available: {', '.join(self.serial_ports[:3])}",
                         style='Muted.TLabel').pack(side=tk.LEFT, padx=(10, 0))
        else:
            # WiFi config with modern styling - IP, Port, FQBN on same line, smaller
            wifi_frame = tk.Frame(self.device_config_frame, bg=BG_CARD)
            wifi_frame.pack(fill=tk.X, pady=(5, 10))
            
            # IP
            ttk.Label(wifi_frame, text="IP:", style='Muted.TLabel').pack(side=tk.LEFT)
            self.wifi_ip_var = tk.StringVar(value=device.ip or '192.168.1.203')
            ttk.Entry(wifi_frame, textvariable=self.wifi_ip_var, style='TEntry', width=15).pack(side=tk.LEFT, padx=(5, 10))
            
            # Port
            ttk.Label(wifi_frame, text="Port:", style='Muted.TLabel').pack(side=tk.LEFT)
            self.wifi_port_var = tk.IntVar(value=device.port or 8266)
            ttk.Entry(wifi_frame, textvariable=self.wifi_port_var, style='TEntry', width=6).pack(side=tk.LEFT, padx=(5, 10))
            
            # FQBN
            ttk.Label(wifi_frame, text="FQBN:", style='Muted.TLabel').pack(side=tk.LEFT)
            self.fqbn_var = tk.StringVar(value=device.fqbn)
            fqbn_entry = ttk.Entry(wifi_frame, textvariable=self.fqbn_var, style='TEntry', state='readonly', width=24)
            fqbn_entry.pack(side=tk.LEFT, padx=(5, 10))

            # Online/Offline: is the device actually reachable on the network
            # right now (necessary for OTA flashing to have any chance of
            # working)? Kept live by _check_device_status.
            self.device_status_var = tk.StringVar(value="● Checking...")
            self.device_status_label = tk.Label(wifi_frame, textvariable=self.device_status_var,
                                                bg=BG_CARD, fg=TEXT_MUTED, font=('Arial', 9, 'bold'))
            self.device_status_label.pack(side=tk.LEFT)

        # Check right away rather than waiting for the next periodic tick, so
        # switching tabs doesn't leave "Checking..." showing for seconds.
        self._check_device_status()

    def _select_device(self, device):
        self.active_device = device
        self._update_button_styles()
        self.sketch_path_var.set(self.active_device.sketch_path)
        self._update_device_config()
        
        # Update console connection defaults based on device type
        if self.active_device.device_type == 'serial':
            self.flash_port_var.set(
                self.active_device.serial_port or (self.serial_ports[0] if self.serial_ports else ''))
            self._auto_set_console_serial()
        else:
            self.telnet_ip_var.set(self.active_device.ip or '192.168.1.203')
            self.telnet_port_var.set(self.active_device.telnet_port or 23)
            
    def _refresh_serial_ports(self):
        ports = get_serial_ports()
        self.serial_ports = [p[0] for p in ports]
        self.serial_port_combo['values'] = self.serial_ports

        # Keep the Configuration-panel port field (only present while a serial
        # device is selected) in sync with what's actually detected.
        if self.active_device.device_type == 'serial' and hasattr(self, 'flash_port_combo'):
            self.flash_port_combo['values'] = self.serial_ports
            if not self.flash_port_var.get().strip() and self.serial_ports:
                self.flash_port_var.set(self.serial_ports[0])

        self._auto_set_console_serial()

    def _auto_set_console_serial(self):
        """Auto-fill console Port/Baud: configured device port > first detected port; device's baud."""
        device = self.active_device
        port = self.flash_port_var.get().strip() or device.serial_port or (self.serial_ports[0] if self.serial_ports else '')
        if port:
            self.serial_port_var.set(port)
        self.baud_var.set(device.baud)

    _STATUS_POLL_MS = 3000  # how often the Online/Offline indicator re-checks

    def _start_status_monitor(self):
        """Kick off the recurring Online/Offline check for whichever device tab
        is active - SmartTV (serial port presence) or Diffuser (network reachability)."""
        self._check_device_status()
        self.root.after(self._STATUS_POLL_MS, self._start_status_monitor)

    def _check_device_status(self):
        """Refresh the Online/Offline indicator for the currently active device.
        Serial: is the configured port actually enumerated right now (board
        plugged in over USB)? Cheap/local, checked synchronously.
        WiFi: is the IP actually reachable on the network right now (a
        necessary precondition for OTA to have any chance of working)? Uses a
        blocking socket connect, so it runs on a background thread and the
        active device is re-checked before applying the result, in case the
        user switched tabs while the check was still in flight."""
        if not hasattr(self, 'device_status_var'):
            return  # Configuration panel not built yet, or torn down mid-switch

        device = self.active_device

        if device.device_type == 'serial':
            ports = get_serial_ports()
            self.serial_ports = [p[0] for p in ports]
            configured_port = (self.flash_port_var.get().strip() if hasattr(self, 'flash_port_var') else '')
            online = bool(configured_port) and configured_port in self.serial_ports
            self._set_device_status(online, device)
        else:
            ip = self.wifi_ip_var.get().strip() if hasattr(self, 'wifi_ip_var') else device.ip

            def worker():
                # ICMP ping instead of a telnet-port probe: the ESP8266
                # answers ping at the OS/WiFi level as soon as it has an IP,
                # before the sketch has even reached setup() - so it reports
                # "online" sooner (e.g. right after an OTA reboot) than
                # waiting for the telnet server specifically to be listening.
                online = bool(ip) and ping_host(ip, timeout_ms=1000)
                self.root.after(0, lambda: self._set_device_status(online, device))

            threading.Thread(target=worker, daemon=True).start()

    def _set_device_status(self, online, checked_device):
        # The active tab may have changed while an async WiFi check was still
        # running - don't paint a stale result over the now-different panel.
        if self.active_device is not checked_device or not hasattr(self, 'device_status_var'):
            return
        try:
            if online:
                self.device_status_var.set("● Online")
                self.device_status_label.config(fg=ACCENT_GREEN)
            else:
                self.device_status_var.set("● Offline")
                self.device_status_label.config(fg=ACCENT_RED)
        except tk.TclError:
            pass  # widget was destroyed mid-update (rapid tab switching) - harmless

    def _browse_sketch(self):
        filename = filedialog.askopenfilename(
            title="Select Arduino Sketch",
            filetypes=[("Arduino Sketch", "*.ino"), ("All Files", "*.*")]
        )
        if filename:
            self.sketch_path_var.set(filename)
            self.active_device.sketch_path = filename
            
    # Fixed column widths so the console lines up like a table (Consolas is monospace)
    _TS_COL_WIDTH = 14   # "[HH:MM:SS.mmm]"
    _TAG_COL_WIDTH = 9   # "[SERIAL] " / "[TELNET] " / "[OUTPUT] "

    # tag -> (bracket label, Tk color-tag name). "serial_sent"/"telnet_sent" show
    # the same [SERIAL]/[TELNET] label as received data but color it differently
    # (see 'sent' tag config) so commands you typed stand out from device output.
    _TAG_INFO = {
        'serial': ('[SERIAL]', 'serial'),
        'telnet': ('[TELNET]', 'telnet'),
        'serial_sent': ('[SERIAL]', 'sent'),
        'telnet_sent': ('[TELNET]', 'sent'),
    }

    def _log(self, text_widget, message, timestamp=True, tag=None):
        """Log message to text widget in aligned columns: <timestamp> [<tag>] <message>

        The tag column is only shown when a source tag (serial/telnet) is given -
        untagged messages (e.g. build output) skip straight from timestamp to
        message, no "[OUTPUT]" filler.

        Every line of `message` gets its own timestamp + tag prefix (not just the
        first), so multi-line dumps like the telnet help banner stay in-column
        instead of trailing off with raw, unprefixed text.
        """
        info = self._TAG_INFO.get(tag)
        color_tag = info[1] if info else None
        tag_field = info[0].ljust(self._TAG_COL_WIDTH) + ' ' if info else ''

        if timestamp:
            now = time.time()
            # time.strftime has no %f (milliseconds) support - build it manually
            ts_field = time.strftime('[%H:%M:%S', time.localtime(now)) + f".{int(now % 1 * 1000):03d}]"
        else:
            ts_field = ''
        ts_field = ts_field.ljust(self._TS_COL_WIDTH)

        # Split on any line breaks so every line of the message gets the same
        # timestamp/tag prefix and stays aligned in its own columns.
        lines = message.splitlines() or ['']

        for line in lines:
            formatted_line = f"{ts_field} {tag_field}{line}"

            # Line number BEFORE inserting: at this point 'end-1c' points at the
            # (empty) line the new text is about to occupy. Computing it AFTER
            # the insert (as this used to) is off by one - Text always keeps a
            # trailing phantom empty line, so 'end-1c' post-insert lands on
            # *that* line instead of the one just written, and tag_add silently
            # no-ops on an empty range. That's why colors never showed up.
            line_num = int(text_widget.index('end-1c').split('.')[0])
            text_widget.insert(tk.END, formatted_line + "\n")

            try:
                if timestamp:
                    text_widget.tag_add('ts', f"{line_num}.0", f"{line_num}.{self._TS_COL_WIDTH}")
                if color_tag:
                    # Color the tag AND the message body (not just the tag word) so
                    # Serial vs Telnet lines are visually distinct at a glance while
                    # scrolling, not just when reading the small tag label closely.
                    tag_start_col = self._TS_COL_WIDTH + 1  # +1 for the space between columns
                    text_widget.tag_add(color_tag, f"{line_num}.{tag_start_col}", f"{line_num}.end")
            except Exception:
                # Tag coloring failed, but the line was already inserted
                pass

        text_widget.see(tk.END)
        
    def _make_build_callback(self):
        """Build a callback for ArduinoCompiler ops (compile/flash/flash_ota).
        These run on a worker thread, so every UI touch is marshaled onto the
        main thread via root.after - Tk widgets are not thread-safe."""
        def callback(status, message):
            def apply():
                if status in ("start", "success", "error"):
                    self._log(self.build_output, message)
                    if status == "start":
                        # The Compile button becomes a Stop button only while a
                        # stoppable phase (compile / core install) is running -
                        # flashing/uploading can't be safely interrupted.
                        if self.compiler.current_operation in ["compile", "core_install"]:
                            self.compile_btn.config(text="Stop")
                        else:
                            self.compile_btn.config(text="Compile")
                    elif status == "success":
                        self.status_var.set("Success")
                        self.progress_var.set(100)
                        self.compile_btn.config(text="Compile")
                    elif status == "error":
                        self.status_var.set("Error")
                        self.progress_var.set(0)
                        self.compile_btn.config(text="Compile")
                elif status == "output":
                    self._log(self.build_output, message, timestamp=False)
                elif status == "progress_only":
                    # Extract percentage without showing output
                    if '%' in message:
                        try:
                            # Look for patterns like "45%" or "45 %"
                            match = re.search(r'(\d+)\s*%', message)
                            if match:
                                self.progress_var.set(float(match.group(1)))
                        except:
                            pass
                elif status == "progress":
                    self.status_var.set(message)
                    # Estimate progress from elapsed time, since Flash/OTA give
                    # no real percentage the way Compile does. The reference
                    # duration must roughly match how long the operation
                    # actually takes - using the full 300s operation timeout as
                    # the denominator here made the bar crawl to ~5% over a
                    # typical 15s flash and look "stuck at 0" the whole time.
                    if "elapsed" in message:
                        try:
                            match = re.search(r'(\d+)s elapsed', message)
                            if match:
                                elapsed = int(match.group(1))
                                if message.startswith(("Flash:", "WiFi OTA:")):
                                    reference_seconds = 30   # typical upload: 10-25s
                                elif message.startswith("Compilation:"):
                                    reference_seconds = 60   # typical compile: 10-40s
                                else:
                                    reference_seconds = 300  # core/platform install: can be slow
                                progress = min(95, (elapsed / reference_seconds) * 100)  # Max 95% until success
                                self.progress_var.set(progress)
                        except:
                            pass
            self.root.after(0, apply)
        return callback
        
    def _on_compile_button(self):
        """Compile button doubles as Stop while a compile/core-install is running."""
        if self.compiler.running and self.compiler.current_operation in ("compile", "core_install"):
            self._stop_operation()
        elif self._operation_busy:
            # Something else (e.g. a Flash's upload phase) is already running -
            # Stop only applies to the stoppable compile phase, so just ignore
            # rather than starting a second, overlapping compile.
            return
        else:
            self._compile()

    def _compile(self):
        if self._operation_busy:
            return
        self._begin_operation()

        sketch_path = self.sketch_path_var.get()
        fqbn = self.active_device.fqbn
        self.progress_var.set(0)
        self._log(self.build_output, f"Compiling {os.path.basename(sketch_path)} for {fqbn}...")
        self.status_var.set("Compiling...")

        def worker():
            try:
                self.compiler.compile(sketch_path, fqbn, self._make_build_callback())
            finally:
                self.root.after(0, self._end_operation)

        thread = threading.Thread(target=worker)
        thread.daemon = True
        thread.start()

    def _begin_operation(self):
        """Mark a compile/flash cycle as in progress and disable Flash so a
        second click can't spawn a concurrent, overlapping operation - which
        previously raced two independent post-flash telnet reconnect
        sequences against each other, among other things. Compile stays
        enabled since it doubles as Stop during its own stoppable phase."""
        self._operation_busy = True
        self.flash_btn.state(['disabled'])

    def _end_operation(self):
        self._operation_busy = False
        self.flash_btn.state(['!disabled'])

    def _stop_operation(self):
        """Stop the current running operation"""
        if self.compiler.stop():
            self._log(self.build_output, "Operation stopped by user")
            self.status_var.set("Stopped")
            self.progress_var.set(0)
            self.compile_btn.config(text="Compile")

    def _flash(self):
        """Resolve target (port or ip:port) then run compile+flash on a worker thread."""
        if self._operation_busy:
            self._log(self.build_output, "A compile/flash is already running - please wait for it to finish")
            return
        self._begin_operation()

        device = self.active_device
        sketch_path = self.sketch_path_var.get()
        self.progress_var.set(0)

        if device.device_type == 'serial':
            # Prefer the port the user typed/selected in the Configuration panel
            port = self.flash_port_var.get().strip() or device.serial_port
            if not port:
                # Auto-detect if still not specified
                ports = get_serial_ports()
                if ports:
                    port = ports[0][0]
                    self._log(self.build_output, f"Auto-detected port: {port}")
                else:
                    self._log(self.build_output, "No serial ports found")
                    self.status_var.set("Error")
                    self._end_operation()
                    return
            target = ('serial', port)
        else:
            target = ('wifi', (self.wifi_ip_var.get(), self.wifi_port_var.get()))

        thread = threading.Thread(target=self._compile_then_flash,
                                 args=(sketch_path, device.fqbn, target, device.ota_password, device.telnet_port or 23))
        thread.daemon = True
        thread.start()

    def _compile_then_flash(self, sketch_path, fqbn, target, ota_password=None, telnet_port=23):
        """Worker thread: compile -> close console link -> flash -> reopen console link.
        target: ('serial', port) or ('wifi', (ip, port))"""
        try:
            self._compile_then_flash_body(sketch_path, fqbn, target, ota_password, telnet_port)
        finally:
            self.root.after(0, self._end_operation)

    def _compile_then_flash_body(self, sketch_path, fqbn, target, ota_password, telnet_port):
        callback = self._make_build_callback()
        mode, dest = target

        self.root.after(0, lambda: self.status_var.set("Compiling..."))
        ok, msg = self.compiler.compile(sketch_path, fqbn, callback)
        if not ok:
            self.root.after(0, lambda: self.status_var.set("Error"))
            self.root.after(0, lambda: self.progress_var.set(0))
            return

        # Reset progress for flash phase
        self.root.after(0, lambda: self.progress_var.set(0))

        # Close existing connections for flash
        self.root.after(0, lambda: self._suspend_console_connection(mode, dest))
        time.sleep(0.3)  # let COM/socket release before flash claims it

        # Perform flash
        if mode == 'serial':
            self._log(self.build_output, f"=== Flashing to {dest} ===")
            self.root.after(0, lambda: self.status_var.set("Flashing..."))
            self.compiler.flash(sketch_path, dest, fqbn, callback)
        else:
            ip, port = dest
            self._log(self.build_output, f"=== WiFi OTA to {ip}:{port} ===")
            self.root.after(0, lambda: self.status_var.set("Flashing..."))
            self.compiler.flash_ota(sketch_path, ip, port, fqbn, callback, ota_password)

        # Always auto-connect after flash to the target port
        if mode == 'serial':
            self._resume_connections.append(('serial', dest, 115200))  # Default baud rate
        else:
            # dest's port is the OTA/flash port (e.g. 8266) - NOT the telnet
            # port. Reusing it here meant every post-OTA-flash auto-reconnect
            # tried to open a telnet session against the OTA port and got
            # refused for the device's entire uptime, no matter how long it
            # retried, while a real telnet connect on the correct port (23)
            # worked instantly the whole time. Confirmed via the self-test
            # suite - "actively refused" never once cleared over 2+ minutes
            # of retries, despite ping and a direct telnet connect both
            # working fine throughout.
            ip, _ota_port = dest
            self._resume_connections.append(('telnet', ip, telnet_port))

        self.root.after(0, self._resume_console_connection)

    def _suspend_console_connection(self, mode=None, dest=None):
        """Close only whichever console link the CURRENT flash actually
        conflicts with - not any unrelated device's connection. A serial
        upload only needs its own COM port free (telnet runs over TCP,
        totally unrelated to that port, so it's never touched for a serial
        flash regardless of which device it's connected to - e.g. flashing
        the SmartTV over USB must not drop an unrelated telnet session to
        the Diffuser, or vice versa). A WiFi OTA flash only risks
        contending with a telnet session to the SAME device (shared RAM/
        WiFi buffers on that one target) - a session to some other device
        stays open too. mode/dest mirror _flash()'s target tuple: ('serial',
        port) or ('wifi', (ip, port))."""
        self._resume_connections = []
        if mode == 'serial':
            if (self.serial_connection and self.serial_connection.is_connected()
                    and self.serial_connection.port == dest):
                self._resume_connections.append(('serial', self.serial_connection.port, self.serial_connection.baudrate))
                self._log(self.build_output, "Closing serial connection before flash...")
                self._disconnect_serial()
        elif mode == 'wifi':
            ip, _port = dest
            if (self.telnet_connection and self.telnet_connection.is_connected()
                    and self.telnet_connection.host == ip):
                self._resume_connections.append(('telnet', self.telnet_connection.host, self.telnet_connection.port))
                self._log(self.console_output, "Closing telnet connection before flash...")
                self._disconnect_telnet()

    def _resume_console_connection(self):
        """Reopen whichever console link(s) were closed for the flash."""
        # Clear console before reconnecting
        self.console_output.delete(1.0, tk.END)

        for kind, a, b in self._resume_connections:
            if kind == 'serial':
                self.serial_port_var.set(a)
                self.baud_var.set(b)
                # Not a plain _connect_serial(): esptool has only just closed
                # the port, and Windows can briefly keep holding the OS-level
                # handle before it's actually free - an instant single
                # attempt here regularly lost that race with
                # PermissionError(13, 'Access is denied').
                self._reconnect_serial_with_retry()
            else:
                self.telnet_ip_var.set(a)
                self.telnet_port_var.set(b)
                # Not a plain _connect_telnet(): right after an OTA flash the
                # device is mid-reboot (espota reports success the instant the
                # device acks the upload and starts ESP.restart(), well before
                # WiFi/telnet are back up), so an instant single attempt is
                # near-certain to fail with "refused"/"timed out" - that's
                # exactly what was showing up here.
                self._reconnect_telnet_with_retry()
        self._resume_connections = []

    # esptool closes the port itself right after flashing, but Windows can
    # take a moment to actually release the underlying OS handle - a single
    # immediate reopen here regularly raced that and lost with
    # PermissionError(13, 'Access is denied'). This is a local resource, not
    # a network round-trip like the telnet reboot wait below, so the budget
    # and delay are both far shorter.
    _SERIAL_RECONNECT_BUDGET_S = 5
    _SERIAL_RECONNECT_DELAY_MS = 250

    def _reconnect_serial_with_retry(self, deadline=None, attempt=0, token=None):
        """Retry the post-flash serial reconnect for up to
        _SERIAL_RECONNECT_BUDGET_S seconds while Windows finishes releasing
        the COM port handle esptool just closed. pyserial's open() fails (or
        succeeds) essentially instantly, so unlike the telnet chain below
        this runs straight on the UI thread instead of a worker thread.

        token: mirrors _reconnect_telnet_with_retry's cancellation guard - a
        manual Connect/Disconnect click (or a newer chain) supersedes and
        silently stops this one. See that method's docstring for why a
        stale chain left running is worse than it sounds."""
        now = time.time()
        first_call = deadline is None
        if first_call:
            self._serial_reconnect_token += 1
            token = self._serial_reconnect_token
            deadline = now + self._SERIAL_RECONNECT_BUDGET_S
        elif token != self._serial_reconnect_token:
            return  # superseded - a manual connect/disconnect or a newer chain took over

        port = self.serial_port_var.get().strip()
        baud = self.baud_var.get()

        conn = SerialConnection(port, baud)
        success, message = conn.connect()

        if success:
            self.serial_connection = conn
            self._log(self.console_output, f"Connected to {port} @ {baud}", tag='serial')
            self.serial_connect_btn.state(['disabled'])
            self.serial_disconnect_btn.state(['!disabled'])
            return

        if now >= deadline:
            self._log(self.console_output, f"Connection failed: {message}", tag='serial')
            return

        self.root.after(self._SERIAL_RECONNECT_DELAY_MS,
                       lambda: self._reconnect_serial_with_retry(deadline, attempt + 1, token))

    # A device's WiFi-rejoin time after an OTA reboot varies a lot in
    # practice (observed anywhere from ~5s to 90s+ on the same device/network)
    # - a fixed attempt count either gives up too early on a slow reconnect or
    # wastes time on a fast one, so retry against a total time budget instead.
    _TELNET_RECONNECT_BUDGET_S = 120
    _TELNET_RECONNECT_DELAY_MS = 3000
    _TELNET_RECONNECT_LOG_EVERY_S = 15  # periodic debug status, not spammy
    # espota reports OTA "success" the instant the device acks the upload and
    # starts ESP.restart() - not once it's actually finished rebooting. A
    # first reconnect attempt right then can catch the device in some
    # not-actually-ready transient state: the TCP accept succeeds, but the
    # link then gets forcibly reset (WinError 10054) on first real use.
    # Confirmed live: a reconnect that succeeded in 9ms broke exactly this
    # way a few seconds later. Waiting before the very first attempt avoids
    # racing that window.
    _TELNET_RECONNECT_INITIAL_DELAY_MS = 5000

    def _reconnect_telnet_with_retry(self, deadline=None, attempt=0, last_log=None, token=None):
        """Retry the post-flash telnet reconnect for up to
        _TELNET_RECONNECT_BUDGET_S seconds while the device finishes rebooting
        and rejoining WiFi. Each attempt's blocking socket connect runs on a
        worker thread so the wait doesn't freeze the UI. Logs the start, a
        periodic status line with the last error seen (debug visibility
        without spamming every ~3s attempt), and the final outcome.

        token: this chain silently stops the moment it's no longer current -
        i.e. the user clicked Connect/Disconnect manually, or another chain
        was started. Without this, a stale chain kept running in the
        background indefinitely (nothing ever cancelled it), and its next
        tick could open a brand-new connection that - thanks to the device
        now always accepting a new client and dropping the old one - silently
        booted out whatever the user had just connected manually. That's
        what made "reconnect" seem broken only after a flash, and fine again
        after a full app restart (no stale chain left running)."""
        now = time.time()
        first_call = deadline is None
        if first_call:
            self._telnet_reconnect_token += 1
            token = self._telnet_reconnect_token
            deadline = now + self._TELNET_RECONNECT_BUDGET_S
            last_log = now
            self._log(self.console_output,
                     f"Waiting for device to come back online after reboot "
                     f"(retrying for up to {self._TELNET_RECONNECT_BUDGET_S}s)...", tag='telnet')
            # Cool-down before the first real attempt - see
            # _TELNET_RECONNECT_INITIAL_DELAY_MS above.
            self.root.after(self._TELNET_RECONNECT_INITIAL_DELAY_MS,
                           lambda: self._reconnect_telnet_with_retry(deadline, attempt, last_log, token))
            return
        elif token != self._telnet_reconnect_token:
            return  # superseded - a manual connect/disconnect or a newer chain took over

        ip = self.telnet_ip_var.get()
        port = self.telnet_port_var.get()

        def worker():
            # Must always reach root.after below, no matter what happens -
            # an uncaught exception here would kill the thread silently and
            # permanently stall the retry chain with no further log output,
            # looking exactly like "stuck after one message" even once the
            # device is back up and genuinely reachable.
            try:
                conn = TelnetConnection(ip, port, timeout=3)
                success, message = conn.connect()
            except Exception as e:
                conn, success, message = None, False, str(e)
            self.root.after(0, lambda: self._on_telnet_reconnect_result(
                success, message, conn, ip, port, deadline, attempt + 1, last_log, token))

        threading.Thread(target=worker, daemon=True).start()

    def _on_telnet_reconnect_result(self, success, message, conn, ip, port, deadline, attempt, last_log, token):
        if token != self._telnet_reconnect_token:
            # Superseded while this attempt's connect() was in flight - don't
            # apply its result (could otherwise silently replace a connection
            # the user just made some other way) and don't reschedule.
            if success and conn:
                conn.disconnect()
            return

        if success:
            self.telnet_connection = conn
            self._log(self.console_output,
                     f"Connected to {ip}:{port} (after {attempt} attempt{'s' if attempt != 1 else ''})", tag='telnet')
            self.telnet_connect_btn.state(['disabled'])
            self.telnet_disconnect_btn.state(['!disabled'])
            return

        now = time.time()
        if now >= deadline:
            self._log(self.console_output,
                     f"Connection failed after {attempt} attempts: {message}", tag='telnet')
            return

        if now - last_log >= self._TELNET_RECONNECT_LOG_EVERY_S:
            self._log(self.console_output,
                     f"Still retrying ({int(deadline - now)}s left, attempt {attempt}) - "
                     f"last error: {message}", tag='telnet')
            last_log = now

        self.root.after(self._TELNET_RECONNECT_DELAY_MS,
                       lambda: self._reconnect_telnet_with_retry(deadline, attempt, last_log, token))
            
    def _connect_serial(self):
        port = self.serial_port_var.get().strip()
        if not port:
            self._refresh_serial_ports()
            port = self.serial_port_var.get().strip()
        baud = self.baud_var.get()
        
        self.serial_connection = SerialConnection(port, baud)
        success, message = self.serial_connection.connect()

        if success:
            # Only a successful manual connect supersedes a still-running
            # post-flash retry chain - see _reconnect_serial_with_retry's
            # docstring (mirrors the same reasoning as the telnet token).
            self._serial_reconnect_token += 1
            self._log(self.console_output, f"Connected to {port} @ {baud}", tag='serial')
            self.serial_connect_btn.state(['disabled'])
            self.serial_disconnect_btn.state(['!disabled'])
        else:
            self._log(self.console_output, f"Connection failed: {message}", tag='serial')

    def _disconnect_serial(self):
        # Also supersede any pending auto-reconnect chain - an explicit
        # Disconnect means the user doesn't want to be reconnected out from
        # under them a moment later.
        self._serial_reconnect_token += 1

        if self.serial_connection:
            self.serial_connection.disconnect()
            self.serial_connection = None
            self._log(self.console_output, "Disconnected", tag='serial')
            self.serial_connect_btn.state(['!disabled'])
            self.serial_disconnect_btn.state(['disabled'])
            
    def _connect_telnet(self):
        ip = self.telnet_ip_var.get()
        port = self.telnet_port_var.get()

        self.telnet_connection = TelnetConnection(ip, port)
        success, message = self.telnet_connection.connect()

        if success:
            # Only a *successful* manual connect supersedes any auto-reconnect
            # chain still running from an earlier flash - otherwise its next
            # tick could open a second connection and (since the device now
            # always accepts a new client and drops the old one) silently
            # boot out the one just made here. Bumping the token on a FAILED
            # manual attempt too was tried first and made things worse: a
            # manual click that missed the device's boot window would cancel
            # the auto chain and then not retry itself, leaving nothing
            # retrying at all - confirmed by the self-test suite.
            self._telnet_reconnect_token += 1
            self._log(self.console_output, f"Connected to {ip}:{port}", tag='telnet')
            self.telnet_connect_btn.state(['disabled'])
            self.telnet_disconnect_btn.state(['!disabled'])
        else:
            self._log(self.console_output, f"Connection failed: {message}", tag='telnet')

    def _disconnect_telnet(self):
        # Also supersede any pending auto-reconnect chain - an explicit
        # Disconnect means the user doesn't want to be reconnected out from
        # under them a few seconds later.
        self._telnet_reconnect_token += 1

        if self.telnet_connection:
            self.telnet_connection.disconnect()
            self.telnet_connection = None
            self._log(self.console_output, "Disconnected", tag='telnet')
            self.telnet_connect_btn.state(['!disabled'])
            self.telnet_disconnect_btn.state(['disabled'])
            
    def _send_console_command(self):
        cmd = self.console_cmd_var.get()
        if not cmd:
            return
        
        target = self.cmd_target_var.get()
        sent = False
        
        if target in ['serial', 'both'] and self.serial_connection:
            success, message = self.serial_connection.send(cmd)
            if success:
                self._log(self.console_output, f"> {cmd}", tag='serial_sent')
                sent = True
            else:
                self._log(self.console_output, f"Send failed: {message}", tag='serial')

        if target in ['telnet', 'both'] and self.telnet_connection:
            success, message = self.telnet_connection.send(cmd)
            if success:
                self._log(self.console_output, f"> {cmd}", tag='telnet_sent')
                sent = True
            else:
                self._log(self.console_output, f"Send failed: {message}", tag='telnet')
        
        if sent:
            self.console_cmd_var.set("")
        elif not self.serial_connection and not self.telnet_connection:
            self._log(self.console_output, "Not connected to any interface")
            
    def _clear_console(self):
        """Clear the console output"""
        self.console_output.delete(1.0, tk.END)
            
    def _start_console_update(self):
        self._update_console()
        self.console_update_timer = self.root.after(100, self._start_console_update)
        
    def _update_console(self):
        # Get data from both connections and display in same console with colored tags
        if self.serial_connection:
            data = self.serial_connection.get_data()
            for d in data:
                self._log(self.console_output, d, timestamp=True, tag='serial')
        if self.telnet_connection:
            data = self.telnet_connection.get_data()
            for d in data:
                self._log(self.console_output, d, timestamp=True, tag='telnet')


def main():
    _enforce_single_instance()
    root = tk.Tk()
    app = FlashConsole(root)
    root.mainloop()


if __name__ == "__main__":
    main()
