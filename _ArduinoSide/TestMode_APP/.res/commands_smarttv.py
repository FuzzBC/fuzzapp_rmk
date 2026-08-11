"""commands_smarttv.py - full command table for _FuZzAPP_SmartTV_R4_8_0_MQTT.
Protocol source: _FuZzAPP_SmartTV_R4_8_0_MQTT.ino (Exec dispatch ~6435, cmd* handlers)
                  _FuZzAPP_SmartTV_R4_DEF.h (EE_SETTINGS_TABLE, enums)
IMPORTANT: this firmware listens on UDP port 8472 (APP_UDP_PORT), NOT 8439 -
8439 is only the internal board-to-board link it uses to reach the physically
separate Diffuser device. There is NO Serial/Telnet console on this firmware
at all - every command below is UDP only.
"""

from core import hexb

# EE_SETTINGS_TABLE (idx 0-49 hex 00-31) - id used verbatim as the wire index.
TV_SETTINGS = [
    (0, 'TV_ON_EFF (0-11)'), (1, 'TV_OFF_EFF (0-7)'), (2, 'TV_OFF_TIME'), (3, 'TV_BR_COM'),
    (4, 'TV_BR_UCOM'), (5, 'TV_BR_BED'), (6, 'TV_BR_LAMP'), (7, 'MOTION_BRIGHTNESS'),
    (8, 'MOTION_ON_TIME'), (9, 'UDPRAW_AMBILIGHT_BR_MAX'), (10, 'MOTION_RANDOM_COLOR (0/1)'),
    (11, 'OTHER_BR_CL_DEL'), (12, 'OTHER_BR_CL_INC'), (13, 'OTHER_BRIGHTNESS_AUTO (0/1)'),
    (14, 'OTHER_TO_OFF_TIME'), (15, 'TV_RANDOM_COLOR_START'), (16, 'MOTION_DIVIDE_BRIGHTNESS (0/1)'),
    (17, 'TV_BR_TV'), (18, 'MOTION_RENEW_COLOR_TIME'), (19, 'MOTION_AUTO_OFF_TIME'),
    (20, 'MOTION_ON_EFFECT (0-5)'), (21, 'OTHER_AMBIENT_MODE_TIME'), (22, 'OTHER_LED_FPS (0-8)'),
    (23, 'TV_ON_BR_CL_DEL'), (24, 'TV_ON_BR_CL_INC'), (25, 'TV_OFF_BR_CL_DEL'), (26, 'TV_OFF_BR_CL_INC'),
    (27, 'MOTION_BR_CL_DEL'), (28, 'MOTION_BR_CL_INC'), (29, 'UDPRAW_BR_CL_DEL'), (30, 'UDPRAW_BR_CL_INC'),
    (31, 'HB_DUAL_COLOR (0/1)'), (32, 'HB_EFFECT (0-13)'), (33, 'HB_EFFECT_SPEED'),
    (34, 'TV_ON_HB_EFFECT (0-3)'), (35, 'DIF_EFFECT (0-8)'), (36, 'DIF_MODE_TV'), (37, 'DIF_MODE_MOTION'),
    (38, 'DIF_MODE_UDPRAW'), (39, 'DIF_MODE_AMBIENT (0=off)'), (40, 'DIF_IDLE_WAIT_MIN'),
    (41, 'DIF_IDLE_ON_MIN'), (42, 'DIF_IDLE_MODE'), (43, 'DIF_BRIGHTNESS (0-120)'), (44, 'DIF_SPEED'),
    (45, 'RESERVED_45'), (46, 'RESERVED_46'), (47, 'RESERVED_47'), (48, 'RESERVED_48'), (49, 'RESERVED_49'),
]

TV_DEBUG_NAMES = [
    'led_info', 'led_selected', 'led_order', 'led_color', 'led_tempcolor', 'motion', 'tv', 'ee',
    'app', 'udpraw', 'bme280', 'ambientmode', 'wifi', 'arduino', 'lisens', 'heartbeat', 'dif',
    'task', 'rtc', 'testmode', 'all', 'mqtt', 'eeprom_backup',
]
TV_DEBUG_OPTIONS = [(str(i), '%s %s' % (hexb(i, 2), name)) for i, name in enumerate(TV_DEBUG_NAMES)]

TV_DIF_MODE_OPTIONS = [('1', '1 CONT'), ('2', '2 10 SEC'), ('3', '3 2H after sleep'), ('4', '4 4H after sleep')]

TV_DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI']
TV_DIF_EFFECT_OPTIONS = [(str(i), '%s %s' % (hexb(i, 2), name)) for i, name in enumerate(TV_DIF_EFFECT_NAMES)]

# Every real EE setting (0-44; 45-49 are reserved padding, see TV_SETTINGS
# above), ported 1:1 from the Android app's SettingsManager.java
# (_populateSettings() -> SET_Info, and the TYPE_SWITCH / TYPE_SELECTABLE /
# TYPE_TIME* / TYPE_BRIGHTNESS / TYPE_INCREMENTAL dispatch in
# _buildSettingsRows()) so each setting gets the same control here as on the
# phone - switch, named choices, or a ranged slider with the real min/max
# instead of a generic 0-255. (idx, category, kind, default, min, max, label,
# options-or-None). kind is 'switch' / 'select' / 'bar'.
DIF_MODE_CHOICES = ['off', 'continuous', '10 sec', '2h after sleep', '4h after sleep']
EE_SETTINGS_TABLE = [
    (0,  'TV', 'select', 0, 0, 0, 'ON EFFECT', ['default', 'random + static', 'mid to out (sep)',
        'mid to out (all)', 'half run', 'half run random', '4-anchor expand', '2-anchor expand',
        'COM scanner', 'TV center sweep', 'liquid fill', 'pixel boot']),
    (1,  'TV', 'select', 0, 0, 0, 'OFF EFFECT', ['default', 'delay (w tv off)', 'delay (all)',
        'Slow TV ~ C ~ UC ~ B ~ L', 'countdown', 'bomb countdown', 'random half', 'TV center sweep']),
    (2,  'TV', 'bar', 0, 0, 180, 'OFF EFFECT TIME (s)', None),
    (17, 'TV', 'bar', 7, 0, 120, 'BRIGHTNESS TV', None),
    (3,  'TV', 'bar', 7, 0, 120, 'BRIGHTNESS COM', None),
    (4,  'TV', 'bar', 40, 0, 120, 'BRIGHTNESS uCOM', None),
    (5,  'TV', 'bar', 40, 0, 120, 'BRIGHTNESS BED', None),
    (6,  'TV', 'bar', 5, 0, 120, 'BRIGHTNESS LAMP', None),
    (15, 'TV', 'select', 0, 0, 0, 'RANDOM COLOR START', ['off', 'random colors', 'random dual']),
    (23, 'TV', 'bar', 15, 1, 150, 'ON BR/CL DELAY (ms)', None),
    (24, 'TV', 'bar', 4, 1, 10, 'ON BR/CL INCREMENT', None),
    (25, 'TV', 'bar', 8, 1, 150, 'OFF BR/CL DELAY (ms)', None),
    (26, 'TV', 'bar', 3, 1, 10, 'OFF BR/CL INCREMENT', None),

    (20, 'MOTION', 'select', 0, 0, 0, 'ON EFFECT', ['default', 'from middle', 'line moving', 'random',
        'cascade', 'the collision', 'right slide']),
    (7,  'MOTION', 'bar', 40, 0, 120, 'BRIGHTNESS', None),
    (8,  'MOTION', 'bar', 120, 0, 180, 'ON TIME (s)', None),
    (10, 'MOTION', 'switch', 0, 0, 1, 'RANDOM COLOR', None),
    (16, 'MOTION', 'switch', 0, 0, 1, 'DIVIDE BRIGHTNESS', None),
    (18, 'MOTION', 'bar', 60, 1, 180, 'RENEW COLOR TIME (s)', None),
    (19, 'MOTION', 'bar', 30, 1, 180, 'AUTO OFF TIME (min)', None),
    (27, 'MOTION', 'bar', 2, 1, 150, 'BR/CL DELAY (ms)', None),
    (28, 'MOTION', 'bar', 4, 1, 10, 'BR/CL INCREMENT', None),

    (31, 'HB', 'switch', 0, 0, 1, 'DUAL COLOR', None),
    (32, 'HB', 'select', 0, 0, 0, 'EFFECT', ['static', 'white move', 'heartbeat', 'random fade',
        'traveling shadow', 'expanding raindrops', 'colors', 'shooting star random',
        'random sparkling pop', 'gliding aurora', 'the glitch matrix', 'stochastic plasma',
        'digital rain', 'dual pulse sinusoidal', 'rainbow wave pulse']),
    (33, 'HB', 'bar', 8, 1, 150, 'EFFECT SPEED (ms)', None),
    (34, 'HB', 'select', 0, 0, 0, 'TV ON HB EFFECT', ['fade on', 'center bloom', 'linear sweep', 'quad point']),

    (9,  'AMBILIGHT', 'bar', 60, 0, 120, 'BRIGHTNESS', None),
    (29, 'AMBILIGHT', 'bar', 60, 1, 150, 'BR/CL DELAY (ms)', None),
    (30, 'AMBILIGHT', 'bar', 1, 1, 10, 'BR/CL INCREMENT', None),

    (35, 'DIFFUSER', 'select', 0, 0, 0, 'EFFECT', ['Static', 'Fade', 'Pulse', 'Random', 'Rainbow',
        'Sparkle', 'Fire', 'Bounce', 'Confetti']),
    (43, 'DIFFUSER', 'bar', 40, 0, 120, 'BRIGHTNESS', None),
    (44, 'DIFFUSER', 'bar', 30, 1, 150, 'EFFECT SPEED (ms)', None),
    (36, 'DIFFUSER', 'select', 0, 0, 0, 'MODE TV', DIF_MODE_CHOICES),
    (37, 'DIFFUSER', 'select', 0, 0, 0, 'MODE MOTION', DIF_MODE_CHOICES),
    (38, 'DIFFUSER', 'select', 0, 0, 0, 'MODE UDPRAW', DIF_MODE_CHOICES),
    (39, 'DIFFUSER', 'select', 0, 0, 0, 'MODE AMBIENT', DIF_MODE_CHOICES),
    (40, 'DIFFUSER', 'bar', 30, 1, 180, 'IDLE WAIT (min)', None),
    (41, 'DIFFUSER', 'bar', 10, 1, 180, 'IDLE ON (min)', None),
    (42, 'DIFFUSER', 'select', 0, 0, 0, 'MODE IDLE', DIF_MODE_CHOICES),

    (11, 'OTHER', 'bar', 3, 1, 150, 'BR/CL DELAY (ms)', None),
    (12, 'OTHER', 'bar', 3, 1, 10, 'BR/CL INCREMENT', None),
    (13, 'OTHER', 'bar', 150, 1, 150, 'BRIGHTNESS LUX INCREASE', None),
    (14, 'OTHER', 'bar', 30, 0, 180, 'TO OFF TIME (s)', None),
    (21, 'OTHER', 'bar', 30, 1, 180, 'AMBIENT MODE TIME (min)', None),
    (22, 'OTHER', 'select', 5, 0, 0, 'LED FPS', ['15', '25', '30', '60', '90', '120', '150', '200', '240']),
]


def _build_at_enum(v):
    return '@' + hexb(int(v['ii']), 2)


def _build_k_debug(v):
    return 'K' + hexb(int(v['ii']), 2)


def _build_dn_relay(v):
    return 'Dn' + hexb(int(v['mode']), 2) + hexb(int(v['eff']), 2)


def _build_dp_start_relay(v):
    return 'Dp' + hexb(v['min'], 4) + v['e']


def _build_lo_mask(v):
    m = ''.join(ch if ch in '01' else '0' for ch in v['mask'])
    if len(m) < 61:
        m = m + '0' * (61 - len(m))
    elif len(m) > 61:
        m = m[:61]
    return 'LO' + m


SMARTTV_COMMANDS = [
    # ---------------- UDP :8472 - session ----------------
    {
        'id': 'Z', 'transport': 'udp', 'section': 'UDP :8472 - session',
        'name': 'Connect to device', 'label': 'Z', 'desc': 'Welcome/connect handshake - send this first. Makes UDP the active transport and triggers a full status push (LM/S/s/H/E/@/LK...)',
        'params': [], 'build': lambda v: 'Z',
    },
    {
        'id': 'k', 'transport': 'udp', 'section': 'UDP :8472 - session', 'envelope': False,
        'name': 'Send keep-alive ping', 'label': 'k', 'desc': 'Keep-alive ping - bypasses the normal dispatcher entirely, no ack/reply expected. Board suspends TX if it hears nothing for 25s',
        'params': [], 'build': lambda v: 'k',
    },
    {
        'id': 'X', 'transport': 'udp', 'section': 'UDP :8472 - session',
        'name': 'Toggle LEDs on/off', 'label': 'X', 'desc': 'Toggle LED enable/disable (no explicit on/off arg - pure toggle; check the S/status reply for current state)',
        'params': [], 'build': lambda v: 'X',
        'confirm': 'This flips every LED on the physical strip on or off. Send it?',
    },

    # ---------------- UDP :8472 - test mode ----------------
    {
        'id': 'AtEnum', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'name': 'Simulate a source', 'label': '@ii', 'desc': 'Select test mode by enum (00 none/cancel, 01 force TV on, 02 force TV off, 03 simulate UDPRAW stream, 04 simulate motion "common", 05 simulate motion "bed"). Auto-cancels after 120s',
        'params': [{'key': 'ii', 'type': 'enum', 'label': 'Mode', 'options': [
            ('0', '00 none (cancel)'), ('1', '01 force TV on'), ('2', '02 force TV off'),
            ('3', '03 UDPRAW stream sim'), ('4', '04 motion common sim'), ('5', '05 motion bed sim'),
        ], 'default': '0'}],
        'build': _build_at_enum,
    },
    {
        'id': 'AtDif', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'name': 'Simulate diffuser action', 'label': '@Dvv', 'desc': 'Diffuser test action - 00 off, 01-04 mode CONT/10SEC/2H/4H, FF = re-push current mode with a fresh random colour',
        'params': [{'key': 'vv', 'type': 'enum', 'label': 'Action', 'options': [
            ('00', '00 off'), ('01', '01 mode CONT'), ('02', '02 mode 10 SEC'),
            ('03', '03 mode 2H after sleep'), ('04', '04 mode 4H after sleep'), ('FF', 'FF random colour refresh'),
        ], 'default': '00'}],
        'build': lambda v: '@D' + v['vv'],
    },
    {
        'id': 'AtLux', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'name': 'Force ambient light level', 'label': '@Lvv', 'desc': 'Force a lux level, 1-4 (0 or >4 rejected)',
        'direct_buttons': [('@L' + hexb(i, 2), 'Lux %d' % i) for i in range(1, 5)],
    },

    # ---------------- UDP :8472 - settings ----------------
    {
        'id': 'SRead', 'transport': 'udp', 'section': 'UDP :8472 - settings (S)',
        'name': 'Read all settings', 'label': 'S', 'desc': 'Read all settings - board replies with the full S + 50x(idx+val) dump',
        'params': [], 'build': lambda v: 'S', 'render': {'type': 'settings_table'},
    },
    {
        'id': 'SWrite', 'transport': 'udp', 'section': 'UDP :8472 - settings (S)',
        'name': 'Write one setting', 'label': 'S ii vv',
        'desc': "Pick a setting, then the value control below switches to match it - "
                "a switch, a named dropdown, or a slider clamped to the real range - "
                "same as the Android app shows for that same setting. Sii vv on the wire either way.",
        'custom_panel': 'settings_write',
    },

    # ---------------- UDP :8472 - ambient mode ----------------
    {
        'id': 'Ambient', 'transport': 'udp', 'section': 'UDP :8472 - ambient mode (A)',
        'name': 'Toggle ambient mode', 'label': 'A0 / A1', 'desc': 'Ambient mode off/on. A1 returns ack BLOCKED (not OK) if UDPRAW is active, motion (bed) is active, or the TV is on',
        'direct_buttons': [('A0', 'A0 off'), ('A1', 'A1 on')],
    },

    # ---------------- UDP :8472 - debug ----------------
    {
        'id': 'Debug', 'transport': 'udp', 'section': 'UDP :8472 - debug (K)',
        'name': 'Dump debug info', 'label': 'K ii', 'desc': "Most sections reply over UDP as one '*'-prefixed term-log line per row (decoded in the Result step below). Exception: eeprom_backup (K16) is Serial-only - it bypasses the log envelope entirely, so nothing comes back over UDP beyond the ack",
        'params': [{'key': 'ii', 'type': 'enum', 'label': 'Section', 'options': TV_DEBUG_OPTIONS, 'default': '20'}],
        'build': _build_k_debug,
    },

    # ---------------- UDP :8472 - diagnostics (!) ----------------
    {
        'id': 'DiagHealth', 'transport': 'udp', 'section': 'UDP :8472 - diagnostics (!)',
        'name': 'Diagnostic - health summary', 'label': '!00',
        'desc': 'Read-only health check (WiFi, free RAM, LEDs, TV, diffuser mirror, EEPROM save state) - never changes device state',
        'params': [], 'build': lambda v: '!00',
    },

    # ---------------- UDP :8472 - diffuser relay (D) ----------------
    # All six of these are a genuine two-hop round trip (app -> SmartTV ->
    # diffuser -> SmartTV -> app), confirmed live to routinely take several
    # seconds even on success - well past the 1200ms single-hop device
    # default, which was silently timing these out before the real reply
    # ever arrived (that's what "not received" for Dh turned out to be).
    {
        'id': 'Ds_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Check diffuser status', 'label': 'Ds', 'desc': 'Request diffuser status via the relay - reply is relayed status push (s.../p...), plus p<min> if parfum active',
        'params': [], 'build': lambda v: 'Ds', 'render': {'type': 'status'}, 'timeout_ms': 5000,
    },
    {
        'id': 'Dh_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Get diffuser refill history', 'label': 'Dh', 'desc': 'Request full diffuser refill history via the relay (passed through verbatim)',
        'params': [], 'build': lambda v: 'Dh', 'timeout_ms': 5000,
    },
    {
        'id': 'Df_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Shut down diffuser', 'label': 'Df', 'desc': 'Shutdown diffuser via the relay. Ack is deferred until the diffuser itself answers - allow extra time',
        'params': [], 'build': lambda v: 'Df', 'timeout_ms': 5000,
    },
    {
        'id': 'Dn_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Turn on diffuser', 'label': 'Dn XX ee', 'desc': 'Turn diffuser on: mode + effect only. BLOCKED unless a source is currently active - fire @01 (force TV on) or A1 first',
        'params': [
            {'key': 'mode', 'type': 'enum', 'label': 'Mode', 'options': TV_DIF_MODE_OPTIONS, 'default': '1'},
            {'key': 'eff', 'type': 'enum', 'label': 'Effect', 'options': TV_DIF_EFFECT_OPTIONS, 'default': '0'},
        ],
        'build': _build_dn_relay, 'timeout_ms': 5000,
    },
    {
        'id': 'DpStart_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Start diffuser parfum timer', 'label': 'Dp start', 'desc': 'Parfum via the relay - DpMMMME, 1-360 min. Not gated by an active source (unlike Dn)',
        'params': [
            {'key': 'min', 'type': 'number', 'label': 'Minutes', 'min': 1, 'max': 360, 'default': 30},
            {'key': 'e', 'type': 'enum', 'label': 'Mode E', 'options': TV_DIF_MODE_OPTIONS, 'default': '1'},
        ],
        'build': _build_dp_start_relay, 'timeout_ms': 5000,
    },
    {
        'id': 'DpCancel_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'name': 'Cancel diffuser parfum timer', 'label': 'Dp cancel', 'desc': 'Cancel active parfum via the relay (mode digit ignored but still required as a placeholder). Ack is REJECTED, not OK, if parfum was not actually running',
        'params': [], 'build': lambda v: 'Dp00000', 'timeout_ms': 5000,
    },

    # ---------------- UDP :8472 - LED zone (L) ----------------
    {
        'id': 'LB', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Set LED brightness', 'label': 'LB vv', 'desc': 'Brightness 0-120 (0x78) - out-of-range values are clamped, not rejected. No-op while Motion is active',
        'params': [{'key': 'vv', 'type': 'range', 'label': 'Brightness', 'min': 0, 'max': 120, 'default': 120}],
        'build': lambda v: 'LB' + hexb(v['vv'], 2),
    },
    {
        'id': 'LC_get', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Get current LED color', 'label': 'LC (get)',
        'desc': "Request current colour info. Confirmed live: since v3.1.0 this always replies as a binary 'LK' colour-sync packet (decoded in the Result step below), never the old ASCII hex reply - there is no plain hex format to expect here anymore.",
        'params': [], 'build': lambda v: 'LC',
    },
    {
        'id': 'LC_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Set LED color', 'label': 'LC set', 'desc': 'Set solid colour on the selected LEDs (see LO). Disables random-colour and HB dual-colour',
        'params': [{'key': 'rgb', 'type': 'hexcolor', 'label': 'Colour', 'default': 'FFFFFF'}],
        'build': lambda v: 'LC' + v['rgb'],
        'render': {'type': 'color_params', 'always': ['rgb']},
    },
    {
        'id': 'LD_get', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Get dual color', 'label': 'LD (get)', 'desc': 'Request current dual-colour info (reply channel widths are unpadded hex)',
        'params': [], 'build': lambda v: 'LD', 'render': {'type': 'color_reply_dual'},
    },
    {
        'id': 'LD_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Set dual color', 'label': 'LD set', 'desc': 'Set dual colour (plain). Blocked while UDPRAW or Ambient mode is active',
        'params': [
            {'key': 'rgb1', 'type': 'hexcolor', 'label': 'Colour 1', 'default': 'FFFFFF'},
            {'key': 'rgb2', 'type': 'hexcolor', 'label': 'Colour 2', 'default': '000000'},
        ],
        'build': lambda v: 'LD' + v['rgb1'] + v['rgb2'],
        'render': {'type': 'color_params', 'always': ['rgb1', 'rgb2']},
    },
    {
        'id': 'Ld_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Set dual color (shake effect)', 'label': 'Ld set (shake)', 'desc': 'Set dual colour, "shake" variant task. Same guards as LD set',
        'params': [
            {'key': 'rgb1', 'type': 'hexcolor', 'label': 'Colour 1', 'default': 'FFFFFF'},
            {'key': 'rgb2', 'type': 'hexcolor', 'label': 'Colour 2', 'default': '000000'},
        ],
        'build': lambda v: 'Ld' + v['rgb1'] + v['rgb2'],
        'render': {'type': 'color_params', 'always': ['rgb1', 'rgb2']},
    },
    {
        'id': 'LO', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'name': 'Select LEDs', 'label': 'LO <61 chars>', 'desc': "61-char selection mask, '1'=selected else deselected. Layout: 0-29 TV strip, 30-39 COM, 40-41 UCOM, 42-49 BED, 50-59 LAMP, 60 HB (fake aggregate slot)",
        'params': [{'key': 'mask', 'type': 'ledmask', 'label': 'LED selection', 'default': '0' * 61}],
        'build': _build_lo_mask,
    },
]
