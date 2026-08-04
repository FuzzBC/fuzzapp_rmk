"""commands_diffuser.py - full command table for _FuZzAPP_Diffuser_1_2_newCom.
Protocol source: _FuZzAPP_Diffuser_1_2_newCom.ino / .h (UDP :8439, console Serial+Telnet :23)
"""

from core import hexb, decode_ds_reply

DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI']
DIF_EFFECT_OPTIONS = [(str(i), '%02d %s' % (i, DIF_EFFECT_NAMES[i])) for i in range(len(DIF_EFFECT_NAMES))]


def _build_dp_start(v):
    return 'Dp' + hexb(v['min'], 4) + v['e']


def _build_dn(v):
    s = 'Dn' + hexb(int(v['mode']), 2) + v['rgb1']
    if v['dual']:
        s += v['rgb2']
    s += hexb(v['br'], 2) + hexb(int(v['effect']), 2) + hexb(v['sp'], 2)
    return s


DIFFUSER_COMMANDS = [
    # ---------------- UDP :8439 ----------------
    {
        'id': 'Ds', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Ds', 'desc': 'Status query -> DsMMSSTTTTUUUUVVVVRRLLLL (mode/strip/parfum/usage/avg/refills)',
        'params': [], 'build': lambda v: 'Ds', 'decode': decode_ds_reply, 'render': {'type': 'status'},
    },
    {
        'id': 'Dc', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Dc', 'desc': 'Silent status poll - same reply as Ds, firmware just skips its own verbose log',
        'params': [], 'build': lambda v: 'Dc', 'decode': decode_ds_reply, 'render': {'type': 'status'},
    },
    {
        'id': 'Df', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Df', 'desc': 'Shutdown (long-press MODE). Refused (LOCKED) while a Parfum window is active',
        'params': [], 'build': lambda v: 'Df',
    },
    {
        'id': 'DpStart', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Dp start', 'desc': 'DpMMMME - timed insist run, 1-360 min. Always physically runs 10 SEC regardless of E',
        'params': [
            {'key': 'min', 'type': 'number', 'label': 'Minutes', 'min': 1, 'max': 360, 'default': 30},
            {'key': 'e', 'type': 'enum', 'label': 'Mode E', 'options': [('1', '1 - CONT'), ('2', '2 - 10 SEC')], 'default': '1'},
        ],
        'build': _build_dp_start,
    },
    {
        'id': 'DpCancel', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Dp0000', 'desc': 'Cancel an active parfum window (no mode digit)',
        'params': [], 'build': lambda v: 'Dp0000',
    },
    {
        'id': 'Dn', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Dn strip-on', 'desc': 'Drive to mode + apply strip colour/effect immediately - DnXXrrggbb[rrggbb]BREESP',
        'params': [
            {'key': 'mode', 'type': 'enum', 'label': 'Mode', 'options': [
                ('1', 'M1 CONT'), ('2', 'M2 10 SEC'), ('3', 'M3 2H after sleep'), ('4', 'M4 4H after sleep'),
            ], 'default': '1'},
            {'key': 'rgb1', 'type': 'hexcolor', 'label': 'Colour 1', 'default': 'FFFFFF'},
            {'key': 'dual', 'type': 'checkbox', 'label': 'Dual', 'default': False},
            {'key': 'rgb2', 'type': 'hexcolor', 'label': 'Colour 2', 'default': '000000', 'enable_when': 'dual'},
            {'key': 'br', 'type': 'range', 'label': 'Brightness', 'min': 0, 'max': 255, 'default': 255},
            {'key': 'effect', 'type': 'enum', 'label': 'Effect', 'options': DIF_EFFECT_OPTIONS, 'default': '0'},
            {'key': 'sp', 'type': 'range', 'label': 'Speed ms', 'min': 5, 'max': 255, 'default': 15},
        ],
        'build': _build_dn,
        'render': {'type': 'color_params', 'always': ['rgb1'], 'conditional': ('dual', 'rgb2')},
    },
    {
        'id': 'Dh', 'transport': 'udp', 'section': 'UDP :8439',
        'label': 'Dh', 'desc': 'Full refill-cycle history on demand -> Dh + count + up to 10 x 4-hex minute values',
        'params': [], 'build': lambda v: 'Dh',
    },

    # ---------------- Console (Serial + Telnet :23) ----------------
    {
        'id': 'Mode', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'M0-M4', 'desc': 'Drive to mode directly. Confirmation is async (real buzzer beep) - re-check with S shortly after',
        'direct_buttons': [('M0', 'M0 OFF'), ('M1', 'M1'), ('M2', 'M2'), ('M3', 'M3'), ('M4', 'M4')],
    },
    {
        'id': 'Parfum', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'P<min>', 'desc': 'Parfum: decimal minutes 1-360 (note: decimal here, unlike UDP Dp which is hex), 0 = cancel + off',
        'params': [{'key': 'min', 'type': 'number', 'label': 'Minutes', 'min': 0, 'max': 360, 'default': 30}],
        'build': lambda v: 'P' + str(v['min']),
    },
    {
        'id': 'EffectNext', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'E', 'desc': 'Cycle to next animated strip effect',
        'params': [], 'build': lambda v: 'E',
    },
    {
        'id': 'ColorTest', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'Crrggbb', 'desc': 'Manual solid strip colour test (full brightness, default speed)',
        'params': [{'key': 'rgb', 'type': 'hexcolor', 'label': 'Colour', 'default': 'FFFFFF'}],
        'build': lambda v: 'C' + v['rgb'],
        'render': {'type': 'color_params', 'always': ['rgb']},
    },
    {
        'id': 'Status', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'S', 'desc': 'Quick status (mode / strip)',
        'params': [], 'build': lambda v: 'S',
    },
    {
        'id': 'Debug', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'D', 'desc': 'Full debug dump: mode, strip, buzzer, WiFi, usage/refill history, EEPROM checkpoint',
        'params': [], 'build': lambda v: 'D',
    },
    {
        'id': 'Help', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': '?/H', 'desc': 'List console commands',
        'params': [], 'build': lambda v: '?',
    },
    {
        'id': 'Quit', 'transport': 'console', 'section': 'Console (Serial + Telnet :23)',
        'label': 'Q', 'desc': "Close this telnet session (telnet only - no-op over the tool's per-command reconnect model, but confirms the firmware accepts it)",
        'params': [], 'build': lambda v: 'Q',
    },
]
