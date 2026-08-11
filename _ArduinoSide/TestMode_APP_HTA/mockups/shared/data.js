/* data.js - command tables + protocol helpers for the TestMode HTA mockups.
 * Ported 1:1 (structure + real command set) from the Python reference app:
 *   TestMode_APP/.res/commands_diffuser.py
 *   TestMode_APP/.res/commands_smarttv.py
 *   TestMode_APP/.res/core.py
 * This file is pure data + tiny pure helpers - no DOM, no rendering. Shared
 * unchanged across every mockup theme so all 5 stay behaviourally identical;
 * only the CSS differs between them.
 */

/* ---------------------------------------------------------- core helpers */

function hexb(n, width) {
  n = parseInt(n, 10);
  if (isNaN(n) || n < 0) n = 0;
  var s = n.toString(16).toUpperCase();
  while (s.length < width) s = '0' + s;
  return s;
}

var ACK_NAMES = { 0: 'ok', 1: 'clamped', 2: 'rejected', 3: 'blocked', 4: 'locked', 5: 'nowater', 6: 'unsupported' };

var DIF_MODE_NAMES = ['OFF', 'CONT', '10 SEC', '2H AFTER SLEEP', '4H AFTER SLEEP'];

/* ------------------------------------------------------- category lookup */
// section string -> { title, color, badge, icon } ; color is a key into the
// theme's --accent-* CSS variables (see shared/app-common.css).
var CATEGORY_META = {
  'UDP :8439':                          { title: 'UDP session & control',   color: 'blue',   badge: 'UDP',  icon: '◆' },
  'Console (Serial + Telnet :23)':      { title: 'Serial / Telnet console', color: 'purple', badge: 'TTY',  icon: '▥' },
  'UDP :8472 - session':                { title: 'Session',                 color: 'blue',   badge: 'SES',  icon: '●' },
  'UDP :8472 - test mode (@)':          { title: 'Test mode (@)',           color: 'amber',  badge: 'TST',  icon: '▶' },
  'UDP :8472 - settings (S)':           { title: 'Settings (S)',            color: 'green',  badge: 'SET',  icon: '▣' },
  'UDP :8472 - ambient mode (A)':       { title: 'Ambient mode (A)',        color: 'purple', badge: 'AMB',  icon: '◐' },
  'UDP :8472 - debug (K)':              { title: 'Debug (K)',               color: 'muted',  badge: 'DBG',  icon: '▧' },
  'UDP :8472 - diagnostics (!)':        { title: 'Diagnostics (!)',         color: 'green',  badge: 'DIAG', icon: '✚' },
  'UDP :8472 - diffuser relay (D)':     { title: 'Diffuser relay (D)',      color: 'amber',  badge: 'REL',  icon: '◆' },
  'UDP :8472 - LED zone (L)':           { title: 'LED zone (L)',            color: 'blue',   badge: 'LED',  icon: '■' },
};
function categoryMeta(section) {
  return CATEGORY_META[section] || { title: section, color: 'muted', badge: section.slice(0, 3).toUpperCase(), icon: '▪' };
}

/* --------------------------------------------------------- LED zone map */
var LED_ZONES = [
  ['TV', 0, 30, 'blue'], ['COM', 30, 10, 'green'], ['UCOM', 40, 2, 'purple'],
  ['BED', 42, 8, 'amber2'], ['LAMP', 50, 10, 'amber'], ['HB', 60, 1, 'pink'],
];

/* ------------------------------------------------------- diffuser table */
var DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI'];
var DIF_EFFECT_OPTIONS = DIF_EFFECT_NAMES.map(function (n, i) { return [String(i), hexb(i, 2).slice(-2) === hexb(i,2) ? (i < 10 ? '0' + i : '' + i) + ' ' + n : i + ' ' + n]; });
// (kept simple/decimal like the Python "%02d %s" - not hex - to match the app)
DIF_EFFECT_OPTIONS = DIF_EFFECT_NAMES.map(function (n, i) { return [String(i), (i < 10 ? '0' + i : '' + i) + ' ' + n]; });

var DIFFUSER_COMMANDS = [
  { id: 'Ds', transport: 'udp', section: 'UDP :8439', name: 'Check status', label: 'Ds',
    desc: 'Status query -> DsMMSSTTTTUUUUVVVVRRLLLL (mode/strip/parfum/usage/avg/refills)',
    params: [], build: function () { return 'Ds'; }, render: { type: 'status' } },
  { id: 'Dc', transport: 'udp', section: 'UDP :8439', name: 'Poll status (silent)', label: 'Dc',
    desc: 'Silent status poll - same reply as Ds, firmware just skips its own verbose log',
    params: [], build: function () { return 'Dc'; }, render: { type: 'status' } },
  { id: 'Df', transport: 'udp', section: 'UDP :8439', name: 'Shut down', label: 'Df',
    desc: 'Shutdown (long-press MODE). Refused (LOCKED) while a Parfum window is active',
    params: [], build: function () { return 'Df'; } },
  { id: 'DpStart', transport: 'udp', section: 'UDP :8439', name: 'Start parfum timer', label: 'Dp start',
    desc: 'DpMMMME - timed insist run, 1-360 min. Always physically runs 10 SEC regardless of E',
    params: [
      { key: 'min', type: 'number', label: 'Minutes', min: 1, max: 360, default: 30 },
      { key: 'e', type: 'enum', label: 'Mode E', options: [['1', '1 - CONT'], ['2', '2 - 10 SEC']], default: '1' },
    ],
    build: function (v) { return 'Dp' + hexb(v.min, 4) + v.e; } },
  { id: 'DpCancel', transport: 'udp', section: 'UDP :8439', name: 'Cancel parfum timer', label: 'Dp0000',
    desc: 'Cancel an active parfum window (no mode digit)',
    params: [], build: function () { return 'Dp0000'; } },
  { id: 'Dn', transport: 'udp', section: 'UDP :8439', name: 'Turn on with color', label: 'Dn strip-on',
    desc: 'Drive to mode + apply strip colour/effect immediately - DnXXrrggbb[rrggbb]BREESP',
    params: [
      { key: 'mode', type: 'enum', label: 'Mode', options: [['1', 'M1 CONT'], ['2', 'M2 10 SEC'], ['3', 'M3 2H after sleep'], ['4', 'M4 4H after sleep']], default: '1' },
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', default: 'FFFFFF' },
      { key: 'dual', type: 'checkbox', label: 'Dual', default: false },
      { key: 'rgb2', type: 'hexcolor', label: 'Colour 2', default: '000000', enable_when: 'dual' },
      { key: 'br', type: 'range', label: 'Brightness', min: 0, max: 255, default: 255 },
      { key: 'effect', type: 'enum', label: 'Effect', options: DIF_EFFECT_OPTIONS, default: '0' },
      { key: 'sp', type: 'range', label: 'Speed ms', min: 5, max: 255, default: 15 },
    ],
    build: function (v) {
      var s = 'Dn' + hexb(v.mode, 2) + v.rgb1;
      if (v.dual) s += v.rgb2;
      s += hexb(v.br, 2) + hexb(v.effect, 2) + hexb(v.sp, 2);
      return s;
    },
    render: { type: 'color_params', always: ['rgb1'], conditional: ['dual', 'rgb2'] } },
  { id: 'Dh', transport: 'udp', section: 'UDP :8439', name: 'Get refill history', label: 'Dh',
    desc: 'Full refill-cycle history on demand -> Dh + count + up to 10 x 4-hex minute values',
    params: [], build: function () { return 'Dh'; } },
  { id: 'DiffuserHistoryManager', transport: 'udp', section: 'UDP :8439', name: 'Manage refill history', label: 'Dh / DyII',
    desc: "Lists every stored refill cycle with its duration. Press the ✕ next to any entry to remove just that one - everything after it shifts down to fill the gap. Lifetime refill count and the live in-progress cycle are never touched.",
    custom_panel: 'diffuser_history' },
  { id: 'DiagHealth', transport: 'udp', section: 'UDP :8439', name: 'Diagnostic - health summary', label: '!00',
    desc: 'Read-only health check (WiFi, power/water, free heap, EEPROM save age, buzzer, history) - never changes device state',
    params: [], build: function () { return '!00'; } },
  { id: 'DiagParfum', transport: 'udp', section: 'UDP :8439', name: 'Diagnostic - parfum trace', label: '!04',
    desc: 'Every parfum-related field in one shot (active/remaining/pending/pre-window snapshot) - read-only',
    params: [], build: function () { return '!04'; } },

  { id: 'Mode', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Force mode directly', label: 'M0-M4',
    desc: 'Drive to mode directly. Confirmation is async (real buzzer beep) - re-check with S shortly after',
    direct_buttons: [['M0', 'M0 OFF'], ['M1', 'M1'], ['M2', 'M2'], ['M3', 'M3'], ['M4', 'M4']] },
  { id: 'Parfum', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Set parfum timer (console)', label: 'P<min>',
    desc: 'Parfum: decimal minutes 1-360 (note: decimal here, unlike UDP Dp which is hex), 0 = cancel + off',
    params: [{ key: 'min', type: 'number', label: 'Minutes', min: 0, max: 360, default: 30 }],
    build: function (v) { return 'P' + v.min; } },
  { id: 'EffectNext', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Next strip effect', label: 'E',
    desc: 'Cycle to next animated strip effect', params: [], build: function () { return 'E'; } },
  { id: 'ColorTest', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Test solid color', label: 'Crrggbb',
    desc: 'Manual solid strip colour test (full brightness, default speed)',
    params: [{ key: 'rgb', type: 'hexcolor', label: 'Colour', default: 'FFFFFF' }],
    build: function (v) { return 'C' + v.rgb; }, render: { type: 'color_params', always: ['rgb'] } },
  { id: 'Status', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Quick status (console)', label: 'S',
    desc: 'Quick status (mode / strip)', params: [], build: function () { return 'S'; } },
  { id: 'Debug', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Full debug dump (console)', label: 'D',
    desc: 'Full debug dump: mode, strip, buzzer, WiFi, usage/refill history, EEPROM checkpoint',
    params: [], build: function () { return 'D'; } },
  { id: 'Help', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'List console commands', label: '?/H',
    desc: 'List console commands', params: [], build: function () { return '?'; } },
  { id: 'Quit', transport: 'console', section: 'Console (Serial + Telnet :23)', name: 'Close telnet session', label: 'Q',
    desc: "Close this telnet session (telnet only - no-op over the tool's per-command reconnect model, but confirms the firmware accepts it)",
    params: [], build: function () { return 'Q'; } },
];

/* ------------------------------------------------------------ TV tables */
var TV_SETTINGS = [
  [0, 'TV_ON_EFF (0-11)'], [1, 'TV_OFF_EFF (0-7)'], [2, 'TV_OFF_TIME'], [3, 'TV_BR_COM'],
  [4, 'TV_BR_UCOM'], [5, 'TV_BR_BED'], [6, 'TV_BR_LAMP'], [7, 'MOTION_BRIGHTNESS'],
  [8, 'MOTION_ON_TIME'], [9, 'UDPRAW_AMBILIGHT_BR_MAX'], [10, 'MOTION_RANDOM_COLOR (0/1)'],
  [11, 'OTHER_BR_CL_DEL'], [12, 'OTHER_BR_CL_INC'], [13, 'OTHER_BRIGHTNESS_AUTO (0/1)'],
  [14, 'OTHER_TO_OFF_TIME'], [15, 'TV_RANDOM_COLOR_START'], [16, 'MOTION_DIVIDE_BRIGHTNESS (0/1)'],
  [17, 'TV_BR_TV'], [18, 'MOTION_RENEW_COLOR_TIME'], [19, 'MOTION_AUTO_OFF_TIME'],
  [20, 'MOTION_ON_EFFECT (0-5)'], [21, 'OTHER_AMBIENT_MODE_TIME'], [22, 'OTHER_LED_FPS (0-8)'],
  [23, 'TV_ON_BR_CL_DEL'], [24, 'TV_ON_BR_CL_INC'], [25, 'TV_OFF_BR_CL_DEL'], [26, 'TV_OFF_BR_CL_INC'],
  [27, 'MOTION_BR_CL_DEL'], [28, 'MOTION_BR_CL_INC'], [29, 'UDPRAW_BR_CL_DEL'], [30, 'UDPRAW_BR_CL_INC'],
  [31, 'HB_DUAL_COLOR (0/1)'], [32, 'HB_EFFECT (0-13)'], [33, 'HB_EFFECT_SPEED'],
  [34, 'TV_ON_HB_EFFECT (0-3)'], [35, 'DIF_EFFECT (0-8)'], [36, 'DIF_MODE_TV'], [37, 'DIF_MODE_MOTION'],
  [38, 'DIF_MODE_UDPRAW'], [39, 'DIF_MODE_AMBIENT (0=off)'], [40, 'DIF_IDLE_WAIT_MIN'],
  [41, 'DIF_IDLE_ON_MIN'], [42, 'DIF_IDLE_MODE'], [43, 'DIF_BRIGHTNESS (0-120)'], [44, 'DIF_SPEED'],
  [45, 'RESERVED_45'], [46, 'RESERVED_46'], [47, 'RESERVED_47'], [48, 'RESERVED_48'], [49, 'RESERVED_49'],
];

var TV_DEBUG_NAMES = ['led_info', 'led_selected', 'led_order', 'led_color', 'led_tempcolor', 'motion', 'tv', 'ee',
  'app', 'udpraw', 'bme280', 'ambientmode', 'wifi', 'arduino', 'lisens', 'heartbeat', 'dif',
  'task', 'rtc', 'testmode', 'all', 'mqtt', 'eeprom_backup'];
var TV_DEBUG_OPTIONS = TV_DEBUG_NAMES.map(function (n, i) { return [String(i), hexb(i, 2) + ' ' + n]; });

var TV_DIF_MODE_OPTIONS = [['1', '1 CONT'], ['2', '2 10 SEC'], ['3', '3 2H after sleep'], ['4', '4 4H after sleep']];
var TV_DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI'];
var TV_DIF_EFFECT_OPTIONS = TV_DIF_EFFECT_NAMES.map(function (n, i) { return [String(i), hexb(i, 2) + ' ' + n]; });

var DIF_MODE_CHOICES = ['off', 'continuous', '10 sec', '2h after sleep', '4h after sleep'];
// (idx, category, kind, default, min, max, label, options|null)
var EE_SETTINGS_TABLE = [
  [0, 'TV', 'select', 0, 0, 0, 'ON EFFECT', ['default', 'random + static', 'mid to out (sep)', 'mid to out (all)', 'half run', 'half run random', '4-anchor expand', '2-anchor expand', 'COM scanner', 'TV center sweep', 'liquid fill', 'pixel boot']],
  [1, 'TV', 'select', 0, 0, 0, 'OFF EFFECT', ['default', 'delay (w tv off)', 'delay (all)', 'Slow TV ~ C ~ UC ~ B ~ L', 'countdown', 'bomb countdown', 'random half', 'TV center sweep']],
  [2, 'TV', 'bar', 0, 0, 180, 'OFF EFFECT TIME (s)', null],
  [17, 'TV', 'bar', 7, 0, 120, 'BRIGHTNESS TV', null],
  [3, 'TV', 'bar', 7, 0, 120, 'BRIGHTNESS COM', null],
  [4, 'TV', 'bar', 40, 0, 120, 'BRIGHTNESS uCOM', null],
  [5, 'TV', 'bar', 40, 0, 120, 'BRIGHTNESS BED', null],
  [6, 'TV', 'bar', 5, 0, 120, 'BRIGHTNESS LAMP', null],
  [15, 'TV', 'select', 0, 0, 0, 'RANDOM COLOR START', ['off', 'random colors', 'random dual']],
  [23, 'TV', 'bar', 15, 1, 150, 'ON BR/CL DELAY (ms)', null],
  [24, 'TV', 'bar', 4, 1, 10, 'ON BR/CL INCREMENT', null],
  [25, 'TV', 'bar', 8, 1, 150, 'OFF BR/CL DELAY (ms)', null],
  [26, 'TV', 'bar', 3, 1, 10, 'OFF BR/CL INCREMENT', null],
  [20, 'MOTION', 'select', 0, 0, 0, 'ON EFFECT', ['default', 'from middle', 'line moving', 'random', 'cascade', 'the collision', 'right slide']],
  [7, 'MOTION', 'bar', 40, 0, 120, 'BRIGHTNESS', null],
  [8, 'MOTION', 'bar', 120, 0, 180, 'ON TIME (s)', null],
  [10, 'MOTION', 'switch', 0, 0, 1, 'RANDOM COLOR', null],
  [16, 'MOTION', 'switch', 0, 0, 1, 'DIVIDE BRIGHTNESS', null],
  [18, 'MOTION', 'bar', 60, 1, 180, 'RENEW COLOR TIME (s)', null],
  [19, 'MOTION', 'bar', 30, 1, 180, 'AUTO OFF TIME (min)', null],
  [27, 'MOTION', 'bar', 2, 1, 150, 'BR/CL DELAY (ms)', null],
  [28, 'MOTION', 'bar', 4, 1, 10, 'BR/CL INCREMENT', null],
  [31, 'HB', 'switch', 0, 0, 1, 'DUAL COLOR', null],
  [32, 'HB', 'select', 0, 0, 0, 'EFFECT', ['static', 'white move', 'heartbeat', 'random fade', 'traveling shadow', 'expanding raindrops', 'colors', 'shooting star random', 'random sparkling pop', 'gliding aurora', 'the glitch matrix', 'stochastic plasma', 'digital rain', 'dual pulse sinusoidal', 'rainbow wave pulse']],
  [33, 'HB', 'bar', 8, 1, 150, 'EFFECT SPEED (ms)', null],
  [34, 'HB', 'select', 0, 0, 0, 'TV ON HB EFFECT', ['fade on', 'center bloom', 'linear sweep', 'quad point']],
  [9, 'AMBILIGHT', 'bar', 60, 0, 120, 'BRIGHTNESS', null],
  [29, 'AMBILIGHT', 'bar', 60, 1, 150, 'BR/CL DELAY (ms)', null],
  [30, 'AMBILIGHT', 'bar', 1, 1, 10, 'BR/CL INCREMENT', null],
  [35, 'DIFFUSER', 'select', 0, 0, 0, 'EFFECT', ['Static', 'Fade', 'Pulse', 'Random', 'Rainbow', 'Sparkle', 'Fire', 'Bounce', 'Confetti']],
  [43, 'DIFFUSER', 'bar', 40, 0, 120, 'BRIGHTNESS', null],
  [44, 'DIFFUSER', 'bar', 30, 1, 150, 'EFFECT SPEED (ms)', null],
  [36, 'DIFFUSER', 'select', 0, 0, 0, 'MODE TV', DIF_MODE_CHOICES],
  [37, 'DIFFUSER', 'select', 0, 0, 0, 'MODE MOTION', DIF_MODE_CHOICES],
  [38, 'DIFFUSER', 'select', 0, 0, 0, 'MODE UDPRAW', DIF_MODE_CHOICES],
  [39, 'DIFFUSER', 'select', 0, 0, 0, 'MODE AMBIENT', DIF_MODE_CHOICES],
  [40, 'DIFFUSER', 'bar', 30, 1, 180, 'IDLE WAIT (min)', null],
  [41, 'DIFFUSER', 'bar', 10, 1, 180, 'IDLE ON (min)', null],
  [42, 'DIFFUSER', 'select', 0, 0, 0, 'MODE IDLE', DIF_MODE_CHOICES],
  [11, 'OTHER', 'bar', 3, 1, 150, 'BR/CL DELAY (ms)', null],
  [12, 'OTHER', 'bar', 3, 1, 10, 'BR/CL INCREMENT', null],
  [13, 'OTHER', 'bar', 150, 1, 150, 'BRIGHTNESS LUX INCREASE', null],
  [14, 'OTHER', 'bar', 30, 0, 180, 'TO OFF TIME (s)', null],
  [21, 'OTHER', 'bar', 30, 1, 180, 'AMBIENT MODE TIME (min)', null],
  [22, 'OTHER', 'select', 5, 0, 0, 'LED FPS', ['15', '25', '30', '60', '90', '120', '150', '200', '240']],
];

var SMARTTV_COMMANDS = [
  { id: 'Z', transport: 'udp', section: 'UDP :8472 - session', name: 'Connect to device', label: 'Z',
    desc: 'Welcome/connect handshake - send this first. Makes UDP the active transport and triggers a full status push (LM/S/s/H/E/@/LK...)',
    params: [], build: function () { return 'Z'; } },
  { id: 'k', transport: 'udp', section: 'UDP :8472 - session', envelope: false, name: 'Send keep-alive ping', label: 'k',
    desc: 'Keep-alive ping - bypasses the normal dispatcher entirely, no ack/reply expected. Board suspends TX if it hears nothing for 25s',
    params: [], build: function () { return 'k'; } },
  { id: 'X', transport: 'udp', section: 'UDP :8472 - session', name: 'Toggle LEDs on/off', label: 'X',
    desc: 'Toggle LED enable/disable (no explicit on/off arg - pure toggle; check the S/status reply for current state)',
    params: [], build: function () { return 'X'; },
    confirm: 'This flips every LED on the physical strip on or off. Send it?' },

  { id: 'AtEnum', transport: 'udp', section: 'UDP :8472 - test mode (@)', name: 'Simulate a source', label: '@ii',
    desc: 'Select test mode by enum (00 none/cancel, 01 force TV on, 02 force TV off, 03 simulate UDPRAW stream, 04 simulate motion "common", 05 simulate motion "bed"). Auto-cancels after 120s',
    params: [{ key: 'ii', type: 'enum', label: 'Mode', options: [['0', '00 none (cancel)'], ['1', '01 force TV on'], ['2', '02 force TV off'], ['3', '03 UDPRAW stream sim'], ['4', '04 motion common sim'], ['5', '05 motion bed sim']], default: '0' }],
    build: function (v) { return '@' + hexb(v.ii, 2); } },
  { id: 'AtDif', transport: 'udp', section: 'UDP :8472 - test mode (@)', name: 'Simulate diffuser action', label: '@Dvv',
    desc: 'Diffuser test action - 00 off, 01-04 mode CONT/10SEC/2H/4H, FF = re-push current mode with a fresh random colour',
    params: [{ key: 'vv', type: 'enum', label: 'Action', options: [['00', '00 off'], ['01', '01 mode CONT'], ['02', '02 mode 10 SEC'], ['03', '03 mode 2H after sleep'], ['04', '04 mode 4H after sleep'], ['FF', 'FF random colour refresh']], default: '00' }],
    build: function (v) { return '@D' + v.vv; } },
  { id: 'AtLux', transport: 'udp', section: 'UDP :8472 - test mode (@)', name: 'Force ambient light level', label: '@Lvv',
    desc: 'Force a lux level, 1-4 (0 or >4 rejected)',
    direct_buttons: [1, 2, 3, 4].map(function (i) { return ['@L' + hexb(i, 2), 'Lux ' + i]; }) },

  { id: 'SRead', transport: 'udp', section: 'UDP :8472 - settings (S)', name: 'Read all settings', label: 'S',
    desc: 'Read all settings - board replies with the full S + 50x(idx+val) dump',
    params: [], build: function () { return 'S'; }, render: { type: 'settings_table' } },
  { id: 'SWrite', transport: 'udp', section: 'UDP :8472 - settings (S)', name: 'Write one setting', label: 'S ii vv',
    desc: 'Pick a setting, then the value control below switches to match it - a switch, a named dropdown, or a slider clamped to the real range - same as the Android app shows for that same setting. Sii vv on the wire either way.',
    custom_panel: 'settings_write' },

  { id: 'Ambient', transport: 'udp', section: 'UDP :8472 - ambient mode (A)', name: 'Toggle ambient mode', label: 'A0 / A1',
    desc: 'Ambient mode off/on. A1 returns ack BLOCKED (not OK) if UDPRAW is active, motion (bed) is active, or the TV is on',
    direct_buttons: [['A0', 'A0 off'], ['A1', 'A1 on']] },

  { id: 'Debug', transport: 'udp', section: 'UDP :8472 - debug (K)', name: 'Dump debug info', label: 'K ii',
    desc: "Most sections reply over UDP as one '*'-prefixed term-log line per row (decoded in the Result step below). Exception: eeprom_backup (K16) is Serial-only.",
    params: [{ key: 'ii', type: 'enum', label: 'Section', options: TV_DEBUG_OPTIONS, default: '20' }],
    build: function (v) { return 'K' + hexb(v.ii, 2); } },

  { id: 'DiagHealth', transport: 'udp', section: 'UDP :8472 - diagnostics (!)', name: 'Diagnostic - health summary', label: '!00',
    desc: 'Read-only health check (WiFi, free RAM, LEDs, TV, diffuser mirror, EEPROM save state) - never changes device state',
    params: [], build: function () { return '!00'; } },

  { id: 'Ds_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Check diffuser status', label: 'Ds',
    desc: 'Request diffuser status via the relay - reply is relayed status push (s.../p...), plus p<min> if parfum active',
    params: [], build: function () { return 'Ds'; }, render: { type: 'status' }, timeout_ms: 5000 },
  { id: 'Dh_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Get diffuser refill history', label: 'Dh',
    desc: 'Request full diffuser refill history via the relay (passed through verbatim)',
    params: [], build: function () { return 'Dh'; }, timeout_ms: 5000 },
  { id: 'Df_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Shut down diffuser', label: 'Df',
    desc: 'Shutdown diffuser via the relay. Ack is deferred until the diffuser itself answers - allow extra time',
    params: [], build: function () { return 'Df'; }, timeout_ms: 5000 },
  { id: 'Dn_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Turn on diffuser', label: 'Dn XX ee',
    desc: 'Turn diffuser on: mode + effect only. BLOCKED unless a source is currently active - fire @01 (force TV on) or A1 first',
    params: [
      { key: 'mode', type: 'enum', label: 'Mode', options: TV_DIF_MODE_OPTIONS, default: '1' },
      { key: 'eff', type: 'enum', label: 'Effect', options: TV_DIF_EFFECT_OPTIONS, default: '0' },
    ], build: function (v) { return 'Dn' + hexb(v.mode, 2) + hexb(v.eff, 2); }, timeout_ms: 5000 },
  { id: 'DpStart_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Start diffuser parfum timer', label: 'Dp start',
    desc: 'Parfum via the relay - DpMMMME, 1-360 min. Not gated by an active source (unlike Dn)',
    params: [
      { key: 'min', type: 'number', label: 'Minutes', min: 1, max: 360, default: 30 },
      { key: 'e', type: 'enum', label: 'Mode E', options: TV_DIF_MODE_OPTIONS, default: '1' },
    ], build: function (v) { return 'Dp' + hexb(v.min, 4) + v.e; }, timeout_ms: 5000 },
  { id: 'DpCancel_relay', transport: 'udp', section: 'UDP :8472 - diffuser relay (D)', name: 'Cancel diffuser parfum timer', label: 'Dp cancel',
    desc: 'Cancel active parfum via the relay (mode digit ignored but still required as a placeholder). Ack is REJECTED, not OK, if parfum was not actually running',
    params: [], build: function () { return 'Dp00000'; }, timeout_ms: 5000 },

  { id: 'LB', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Set LED brightness', label: 'LB vv',
    desc: 'Brightness 0-120 (0x78) - out-of-range values are clamped, not rejected. No-op while Motion is active',
    params: [{ key: 'vv', type: 'range', label: 'Brightness', min: 0, max: 120, default: 120 }],
    build: function (v) { return 'LB' + hexb(v.vv, 2); } },
  { id: 'LC_get', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Get current LED color', label: 'LC (get)',
    desc: "Request current colour info. Since v3.1.0 this always replies as a binary 'LK' colour-sync packet, never the old ASCII hex reply.",
    params: [], build: function () { return 'LC'; } },
  { id: 'LC_set', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Set LED color', label: 'LC set',
    desc: 'Set solid colour on the selected LEDs (see LO). Disables random-colour and HB dual-colour',
    params: [{ key: 'rgb', type: 'hexcolor', label: 'Colour', default: 'FFFFFF' }],
    build: function (v) { return 'LC' + v.rgb; }, render: { type: 'color_params', always: ['rgb'] } },
  { id: 'LD_get', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Get dual color', label: 'LD (get)',
    desc: 'Request current dual-colour info (reply channel widths are unpadded hex)',
    params: [], build: function () { return 'LD'; }, render: { type: 'color_reply_dual' } },
  { id: 'LD_set', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Set dual color', label: 'LD set',
    desc: 'Set dual colour (plain). Blocked while UDPRAW or Ambient mode is active',
    params: [
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', default: 'FFFFFF' },
      { key: 'rgb2', type: 'hexcolor', label: 'Colour 2', default: '000000' },
    ], build: function (v) { return 'LD' + v.rgb1 + v.rgb2; }, render: { type: 'color_params', always: ['rgb1', 'rgb2'] } },
  { id: 'Ld_set', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Set dual color (shake effect)', label: 'Ld set (shake)',
    desc: 'Set dual colour, "shake" variant task. Same guards as LD set',
    params: [
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', default: 'FFFFFF' },
      { key: 'rgb2', type: 'hexcolor', label: 'Colour 2', default: '000000' },
    ], build: function (v) { return 'Ld' + v.rgb1 + v.rgb2; }, render: { type: 'color_params', always: ['rgb1', 'rgb2'] } },
  { id: 'LO', transport: 'udp', section: 'UDP :8472 - LED zone (L)', name: 'Select LEDs', label: 'LO <61 chars>',
    desc: "61-char selection mask, '1'=selected else deselected. Layout: 0-29 TV strip, 30-39 COM, 40-41 UCOM, 42-49 BED, 50-59 LAMP, 60 HB (fake aggregate slot)",
    params: [{ key: 'mask', type: 'ledmask', label: 'LED selection', default: '0'.repeat(61) }],
    build: function (v) {
      var m = (v.mask || '').split('').map(function (c) { return c === '1' ? '1' : '0'; }).join('');
      if (m.length < 61) m += '0'.repeat(61 - m.length); else if (m.length > 61) m = m.slice(0, 61);
      return 'LO' + m;
    } },
];

var COMMAND_TABLES = { diffuser: DIFFUSER_COMMANDS, smarttv: SMARTTV_COMMANDS };
