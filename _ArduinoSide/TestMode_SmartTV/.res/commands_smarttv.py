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
TV_SETTINGS_OPTIONS = [(str(idx), '%s %s' % (hexb(idx, 2), name)) for idx, name in TV_SETTINGS]

TV_DEBUG_NAMES = [
    'led_info', 'led_selected', 'led_order', 'led_color', 'led_tempcolor', 'motion', 'tv', 'ee',
    'app', 'udpraw', 'bme280', 'ambientmode', 'wifi', 'arduino', 'lisens', 'heartbeat', 'dif',
    'task', 'rtc', 'testmode', 'all', 'mqtt',
]
TV_DEBUG_OPTIONS = [(str(i), '%s %s' % (hexb(i, 2), name)) for i, name in enumerate(TV_DEBUG_NAMES)]

TV_DIF_MODE_OPTIONS = [('1', '1 CONT'), ('2', '2 10 SEC'), ('3', '3 2H after sleep'), ('4', '4 4H after sleep')]

TV_DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI']
TV_DIF_EFFECT_OPTIONS = [(str(i), '%s %s' % (hexb(i, 2), name)) for i, name in enumerate(TV_DIF_EFFECT_NAMES)]


def _build_at_enum(v):
    return '@' + hexb(int(v['ii']), 2)


def _build_s_write(v):
    return 'S' + hexb(int(v['ii']), 2) + hexb(v['vv'], 2)


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
        'label': 'Z', 'desc': 'Welcome/connect handshake - send this first. Makes UDP the active transport and triggers a full status push (LM/S/s/H/E/@/LK...)',
        'params': [], 'build': lambda v: 'Z',
    },
    {
        'id': 'k', 'transport': 'udp', 'section': 'UDP :8472 - session', 'envelope': False,
        'label': 'k', 'desc': 'Keep-alive ping - bypasses the normal dispatcher entirely, no ack/reply expected. Board suspends TX if it hears nothing for 25s',
        'params': [], 'build': lambda v: 'k',
    },
    {
        'id': 'X', 'transport': 'udp', 'section': 'UDP :8472 - session',
        'label': 'X', 'desc': 'Toggle LED enable/disable (no explicit on/off arg - pure toggle; check the S/status reply for current state)',
        'params': [], 'build': lambda v: 'X',
    },

    # ---------------- UDP :8472 - test mode ----------------
    {
        'id': 'AtEnum', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'label': '@ii', 'desc': 'Select test mode by enum (00 none/cancel, 01 force TV on, 02 force TV off, 03 simulate UDPRAW stream, 04 simulate motion "common", 05 simulate motion "bed"). Auto-cancels after 120s',
        'params': [{'key': 'ii', 'type': 'enum', 'label': 'Mode', 'options': [
            ('0', '00 none (cancel)'), ('1', '01 force TV on'), ('2', '02 force TV off'),
            ('3', '03 UDPRAW stream sim'), ('4', '04 motion common sim'), ('5', '05 motion bed sim'),
        ], 'default': '0'}],
        'build': _build_at_enum,
    },
    {
        'id': 'AtDif', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'label': '@Dvv', 'desc': 'Diffuser test action - 00 off, 01-04 mode CONT/10SEC/2H/4H, FF = re-push current mode with a fresh random colour',
        'params': [{'key': 'vv', 'type': 'enum', 'label': 'Action', 'options': [
            ('00', '00 off'), ('01', '01 mode CONT'), ('02', '02 mode 10 SEC'),
            ('03', '03 mode 2H after sleep'), ('04', '04 mode 4H after sleep'), ('FF', 'FF random colour refresh'),
        ], 'default': '00'}],
        'build': lambda v: '@D' + v['vv'],
    },
    {
        'id': 'AtLux', 'transport': 'udp', 'section': 'UDP :8472 - test mode (@)',
        'label': '@Lvv', 'desc': 'Force a lux level, 1-4 (0 or >4 rejected)',
        'params': [{'key': 'vv', 'type': 'number', 'label': 'Level', 'min': 1, 'max': 4, 'default': 1}],
        'build': lambda v: '@L' + hexb(v['vv'], 2),
    },

    # ---------------- UDP :8472 - settings ----------------
    {
        'id': 'SRead', 'transport': 'udp', 'section': 'UDP :8472 - settings (S)',
        'label': 'S', 'desc': 'Read all settings - board replies with the full S + 50x(idx+val) dump',
        'params': [], 'build': lambda v: 'S', 'render': {'type': 'settings_table'},
    },
    {
        'id': 'SWrite', 'transport': 'udp', 'section': 'UDP :8472 - settings (S)',
        'label': 'S ii vv', 'desc': "Write one setting - Sii vv (hex idx + hex value 0-255). Any rejected pair fails the whole packet's ack - re-read with S after",
        'params': [
            {'key': 'ii', 'type': 'enum', 'label': 'Setting', 'options': TV_SETTINGS_OPTIONS, 'default': '0'},
            {'key': 'vv', 'type': 'range', 'label': 'Value', 'min': 0, 'max': 255, 'default': 0},
        ],
        'build': _build_s_write,
    },

    # ---------------- UDP :8472 - ambient mode ----------------
    {
        'id': 'Ambient', 'transport': 'udp', 'section': 'UDP :8472 - ambient mode (A)',
        'label': 'A0 / A1', 'desc': 'Ambient mode off/on. A1 is a soft no-op (still acks OK) if UDPRAW is active, motion (bed) is active, or the TV is on',
        'direct_buttons': [('A0', 'A0 off'), ('A1', 'A1 on')],
    },

    # ---------------- UDP :8472 - debug ----------------
    {
        'id': 'Debug', 'transport': 'udp', 'section': 'UDP :8472 - debug (K)',
        'label': 'K ii', 'desc': "Dump debug info to the device's own Serial output only - nothing comes back over UDP beyond the ack",
        'params': [{'key': 'ii', 'type': 'enum', 'label': 'Section', 'options': TV_DEBUG_OPTIONS, 'default': '20'}],
        'build': _build_k_debug,
    },

    # ---------------- UDP :8472 - diffuser relay (D) ----------------
    {
        'id': 'Ds_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Ds', 'desc': 'Request diffuser status via the relay - reply is relayed status push (s.../p...), plus p<min> if parfum active',
        'params': [], 'build': lambda v: 'Ds', 'render': {'type': 'status'},
    },
    {
        'id': 'Dh_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Dh', 'desc': 'Request full diffuser refill history via the relay (passed through verbatim)',
        'params': [], 'build': lambda v: 'Dh',
    },
    {
        'id': 'Df_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Df', 'desc': 'Shutdown diffuser via the relay. Ack is deferred until the diffuser itself answers - allow extra time',
        'params': [], 'build': lambda v: 'Df',
    },
    {
        'id': 'Dn_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Dn XX ee', 'desc': 'Turn diffuser on: mode + effect only. BLOCKED unless a source is currently active - fire @01 (force TV on) or A1 first',
        'params': [
            {'key': 'mode', 'type': 'enum', 'label': 'Mode', 'options': TV_DIF_MODE_OPTIONS, 'default': '1'},
            {'key': 'eff', 'type': 'enum', 'label': 'Effect', 'options': TV_DIF_EFFECT_OPTIONS, 'default': '0'},
        ],
        'build': _build_dn_relay,
    },
    {
        'id': 'DpStart_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Dp start', 'desc': 'Parfum via the relay - DpMMMME, 1-360 min. Not gated by an active source (unlike Dn)',
        'params': [
            {'key': 'min', 'type': 'number', 'label': 'Minutes', 'min': 1, 'max': 360, 'default': 30},
            {'key': 'e', 'type': 'enum', 'label': 'Mode E', 'options': TV_DIF_MODE_OPTIONS, 'default': '1'},
        ],
        'build': _build_dp_start_relay,
    },
    {
        'id': 'DpCancel_relay', 'transport': 'udp', 'section': 'UDP :8472 - diffuser relay (D)',
        'label': 'Dp cancel', 'desc': 'Cancel active parfum via the relay (mode digit ignored but still required as a placeholder)',
        'params': [], 'build': lambda v: 'Dp00000',
    },

    # ---------------- UDP :8472 - LED zone (L) ----------------
    {
        'id': 'LB', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LB vv', 'desc': 'Brightness 0-120 (0x78) - out-of-range values are clamped, not rejected. No-op while Motion is active',
        'params': [{'key': 'vv', 'type': 'range', 'label': 'Brightness', 'min': 0, 'max': 120, 'default': 120}],
        'build': lambda v: 'LB' + hexb(v['vv'], 2),
    },
    {
        'id': 'LC_get', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LC (get)', 'desc': 'Request current colour info',
        'params': [], 'build': lambda v: 'LC', 'render': {'type': 'color_reply'},
    },
    {
        'id': 'LC_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LC set', 'desc': 'Set solid colour on the selected LEDs (see LO). Disables random-colour and HB dual-colour',
        'params': [{'key': 'rgb', 'type': 'hexcolor', 'label': 'Colour', 'default': 'FFFFFF'}],
        'build': lambda v: 'LC' + v['rgb'],
        'render': {'type': 'color_params', 'always': ['rgb']},
    },
    {
        'id': 'LD_get', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LD (get)', 'desc': 'Request current dual-colour info (reply channel widths are unpadded hex)',
        'params': [], 'build': lambda v: 'LD', 'render': {'type': 'color_reply_dual'},
    },
    {
        'id': 'LD_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LD set', 'desc': 'Set dual colour (plain). Blocked while UDPRAW or Ambient mode is active',
        'params': [
            {'key': 'rgb1', 'type': 'hexcolor', 'label': 'Colour 1', 'default': 'FFFFFF'},
            {'key': 'rgb2', 'type': 'hexcolor', 'label': 'Colour 2', 'default': '000000'},
        ],
        'build': lambda v: 'LD' + v['rgb1'] + v['rgb2'],
        'render': {'type': 'color_params', 'always': ['rgb1', 'rgb2']},
    },
    {
        'id': 'Ld_set', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'Ld set (shake)', 'desc': 'Set dual colour, "shake" variant task. Same guards as LD set',
        'params': [
            {'key': 'rgb1', 'type': 'hexcolor', 'label': 'Colour 1', 'default': 'FFFFFF'},
            {'key': 'rgb2', 'type': 'hexcolor', 'label': 'Colour 2', 'default': '000000'},
        ],
        'build': lambda v: 'Ld' + v['rgb1'] + v['rgb2'],
        'render': {'type': 'color_params', 'always': ['rgb1', 'rgb2']},
    },
    {
        'id': 'LO', 'transport': 'udp', 'section': 'UDP :8472 - LED zone (L)',
        'label': 'LO <61 chars>', 'desc': "61-char selection mask, '1'=selected else deselected. Layout: 0-29 TV strip, 30-39 COM, 40-41 UCOM, 42-49 BED, 50-59 LAMP, 60 HB (fake aggregate slot)",
        'params': [{'key': 'mask', 'type': 'ledmask', 'label': 'LED selection', 'default': '0' * 61}],
        'build': _build_lo_mask,
    },
]
