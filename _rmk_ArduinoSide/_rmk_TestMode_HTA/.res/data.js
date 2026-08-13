/* data.js - command tables for TestMode.hta (00_PLAN.md Phase 6 follow-up).
 * Rewritten against the binary v1 opcode table (protocol_table.json) instead
 * of the frozen app's ASCII command set - every entry's `build(v)` returns a
 * byte-string payload (String.fromCharCode per byte) instead of an ASCII
 * command string, using the GENERATED Proto.packXxx()/unpackXxx() codec
 * (protocol_opcodes.js) for every fixed-layout opcode, so this table can
 * never silently drift from the real wire format the way a hand-rolled byte
 * layout could - same reasoning as rmk_testmode.py's own header comment.
 * Opcodes with a "raw" field (protocol_table.json's own term for
 * variable/conditional payloads: colour-sync, settings write, diffuser
 * turn-on, MQTT credentials) get a small hand-written builder next to their
 * entry, same as every other language's transport layer does for those.
 *
 * Diffuser opcodes are "link":"both" in the table - they work identically
 * framed whether sent directly to the Diffuser (192.168.1.203:8439) or
 * relayed through the SmartTV (192.168.1.202:8472), so they appear in BOTH
 * command tables below, same structure the frozen ASCII tool used.
 */

/* ---------------------------------------------------------- core helpers */

function hexb(n, width) {
  n = parseInt(n, 10);
  if (isNaN(n) || n < 0) n = 0;
  var s = n.toString(16).toUpperCase();
  while (s.length < width) s = '0' + s;
  return s;
}
function repeatChar(ch, n) { var s = ''; for (var i = 0; i < n; i++) s += ch; return s; }
function byteStr(bytes) { var s = ''; for (var i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i] & 0xFF); return s; }
function hexColorToRgb(hex) {
  hex = (hex || '000000').toUpperCase();
  return [parseInt(hex.substr(0, 2), 16) || 0, parseInt(hex.substr(2, 2), 16) || 0, parseInt(hex.substr(4, 2), 16) || 0];
}
// UI's 61-char '1'/'0' selection string -> the 8-byte bitmask
// LedSetSelectionPayload.mask expects (bit i of byte i/8 = LED i selected) -
// same packing MainActivity.pushSelection() uses on the Android side.
function ledMaskToBytes(maskStr) {
  var bytes = [0, 0, 0, 0, 0, 0, 0, 0];
  for (var i = 0; i < 61 && i < maskStr.length; i++) {
    if (maskStr.charAt(i) === '1') bytes[Math.floor(i / 8)] |= (1 << (i % 8));
  }
  return byteStr(bytes);
}

/* ------------------------------------------------------- category lookup */
var CATEGORY_META = {
  'UDP :8439':                     { title: 'Session & diagnostics',    color: 'blue',   badge: 'SES',  icon: '●' },
  'Console (Telnet :23)':          { title: 'Telnet console',           color: 'purple', badge: 'TTY',  icon: '▥' },
  'UDP :8472 - session':           { title: 'Session',                  color: 'blue',   badge: 'SES',  icon: '●' },
  'UDP :8472 - LED':               { title: 'LED (Link B)',             color: 'blue',   badge: 'LED',  icon: '■' },
  'UDP :8472 - settings':          { title: 'Settings',                 color: 'green',  badge: 'SET',  icon: '▣' },
  'UDP :8472 - test mode':         { title: 'Test mode',                color: 'amber',  badge: 'TST',  icon: '▶' },
  'UDP :8472 - ambient/telnet':    { title: 'Ambient / remote telnet',  color: 'purple', badge: 'AMB',  icon: '◐' },
  'UDP :8472 - mqtt':              { title: 'MQTT cloud credentials',   color: 'muted',  badge: 'MQTT', icon: '☁' },
  'UDP :8472 - diffuser relay':    { title: 'Diffuser relay',           color: 'amber',  badge: 'REL',  icon: '◆' }
};
function categoryMeta(section) {
  return CATEGORY_META[section] || { title: section, color: 'muted', badge: section.slice(0, 3).toUpperCase(), icon: '▪' };
}

/* --------------------------------------------------------- LED zone map */
var LED_ZONES = [
  ['TV', 0, 30, 'blue'], ['COM', 30, 10, 'green'], ['UCOM', 40, 2, 'purple'],
  ['BED', 42, 8, 'amber2'], ['LAMP', 50, 10, 'amber'], ['HB', 60, 1, 'pink']
];

/* ------------------------------------------------------- diffuser table */
var DIF_MODE_NAMES = ['OFF', 'CONT', '10 SEC', '2H AFTER SLEEP', '4H AFTER SLEEP'];
var DIF_EFFECT_NAMES = ['STATIC', 'FADE', 'PULSE', 'RANDOM', 'RAINBOW', 'SPARKLE', 'FIRE', 'BOUNCE', 'CONFETTI'];
// TELEM_DIFFUSER_STATUS.strip - matches the firmware's Strip.cpp::stripStatusName()
// exactly: 0=off, 1=static, 2=dual, 3+ = effectName(code-2) (DIF_EFFECT_NAMES[0]
// "STATIC" is skipped there since code 1 already covers plain static).
var DIF_STRIP_NAMES = ['OFF', 'STATIC', 'DUAL'].concat(DIF_EFFECT_NAMES.slice(1));
var DIF_EFFECT_OPTIONS = DIF_EFFECT_NAMES.map(function (n, i) { return [String(i), (i < 10 ? '0' + i : '' + i) + ' ' + n]; });
var DIF_MODE_OPTIONS = [['1', '1 CONT'], ['2', '2 10 SEC'], ['3', '3 2H after sleep'], ['4', '4 4H after sleep']];

/* ---------------------------------------------------- SmartTV status table */
// TELEM_STATUS's fields - shared by ui.js's hero card and result-view.js's
// generic entry cards, so a status reply reads the same way everywhere it
// shows up. Matches AppLink.cpp::updStatus() exactly - see its comments for
// the u8 -> meaning mapping (motion mirrors MOTION::Status's Globals.h enum
// directly; ambient/diffuser_summary are each a small hand-built code, not
// a raw firmware enum).
var MOTION_LABEL = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed'];
var AMBIENT_LABEL = { 0: 'off', 1: 'on', 2: 'ready' };
var DIF_SUMMARY_LABEL = { 0: 'off', 1: 'on', 2: 'out of water', 3: 'no response', 4: 'parfum running' };

/* ---------------------------------------------------- shared builders */
function buildNoPayload() { return ''; }

var DIFFUSER_COMMANDS = [
  { id: 'Hello', opcode: 'HELLO', transport: 'udp', section: 'UDP :8439', name: 'Connect / handshake', label: 'HELLO',
    desc: 'Send first - proto_version=1. Firmware answers with an ACK', params: [], build: function () { return Proto.packHello(1); } },
  { id: 'Keepalive', opcode: 'KEEPALIVE', transport: 'udp', section: 'UDP :8439', ack: false, name: 'Keep-alive ping', label: 'KEEPALIVE',
    desc: 'No ack expected - board suspends TX if it hears nothing for a while', params: [], build: buildNoPayload },
  { id: 'DiagHealth', opcode: 'DIAG_HEALTH', transport: 'udp', section: 'UDP :8439', name: 'Diagnostic - health summary', label: 'DIAG_HEALTH',
    desc: 'Read-only health check - never changes device state', params: [], build: buildNoPayload },
  { id: 'DiagParfum', opcode: 'DIAG_PARFUM_TRACE', transport: 'udp', section: 'UDP :8439', name: 'Diagnostic - parfum trace', label: 'DIAG_PARFUM_TRACE',
    desc: 'Every parfum-related field in one shot - read-only, Diffuser-only opcode (link=A)', params: [], build: buildNoPayload },

  { id: 'DifStatus', opcode: 'DIFFUSER_STATUS_QUERY', transport: 'udp', section: 'UDP :8439', name: 'Check status', label: 'DIFFUSER_STATUS_QUERY',
    desc: 'Status query -> TELEM_DIFFUSER_STATUS push',
    params: [{ key: 'verbose', type: 'checkbox', label: 'Verbose', 'default': false }],
    build: function (v) { return Proto.packDiffuserStatusQuery(v.verbose ? 1 : 0); }, render: { type: 'diffuser_status' } },
  { id: 'DifHistory', opcode: 'DIFFUSER_HISTORY_QUERY', transport: 'udp', section: 'UDP :8439', name: 'Get refill history', label: 'DIFFUSER_HISTORY_QUERY',
    desc: 'Full refill-cycle history on demand -> TELEM_DIFFUSER_HISTORY', params: [], build: buildNoPayload, render: { type: 'diffuser_history' }, custom_panel: 'diffuser_history' },
  { id: 'DifHistoryRemove', opcode: 'DIFFUSER_HISTORY_REMOVE', transport: 'udp', section: 'UDP :8439', name: 'Remove one history entry', label: 'DIFFUSER_HISTORY_REMOVE',
    desc: 'Removes one stored refill cycle by index; everything after it shifts down',
    params: [{ key: 'index', type: 'number', label: 'Index', min: 0, max: 9, 'default': 0 }],
    build: function (v) { return Proto.packDiffuserHistoryRemove(v.index); } },
  { id: 'DifManualRefill', opcode: 'DIFFUSER_MANUAL_REFILL', transport: 'udp', section: 'UDP :8439', name: 'Log a manual refill', label: 'DIFFUSER_MANUAL_REFILL',
    desc: 'Records a refill cycle manually (no physical mode change)', params: [], build: buildNoPayload },
  { id: 'DifShutdown', opcode: 'DIFFUSER_SHUTDOWN', transport: 'udp', section: 'UDP :8439', name: 'Shut down', label: 'DIFFUSER_SHUTDOWN',
    desc: 'Shutdown (same as long-press MODE). Refused (LOCKED) while a Parfum window is active', params: [], build: buildNoPayload,
    confirm: 'Shut down the diffuser now?' },
  { id: 'DifTurnOn', opcode: 'DIFFUSER_TURN_ON', transport: 'udp', section: 'UDP :8439', name: 'Turn on with color', label: 'DIFFUSER_TURN_ON',
    desc: 'mode(u8)+dual(u8)+rgb1[+rgb2]+brightness(u8)+effect(u8)+speed_ms(u8) - conditional length, hand-built (raw field in protocol_table.json)',
    params: [
      { key: 'mode', type: 'enum', label: 'Mode', options: DIF_MODE_OPTIONS, 'default': '1' },
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', 'default': 'FFFFFF' },
      { key: 'dual', type: 'checkbox', label: 'Dual', 'default': false },
      { key: 'rgb2', type: 'hexcolor', label: 'Colour 2', 'default': '000000', enable_when: 'dual' },
      { key: 'br', type: 'range', label: 'Brightness', min: 0, max: 255, 'default': 200 },
      { key: 'effect', type: 'enum', label: 'Effect', options: DIF_EFFECT_OPTIONS, 'default': '0' },
      { key: 'sp', type: 'range', label: 'Speed ms', min: 5, max: 255, 'default': 50 }
    ],
    build: function (v) {
      var c1 = hexColorToRgb(v.rgb1);
      var bytes = [parseInt(v.mode, 10), v.dual ? 1 : 0, c1[0], c1[1], c1[2]];
      if (v.dual) { var c2 = hexColorToRgb(v.rgb2); bytes = bytes.concat([c2[0], c2[1], c2[2]]); }
      bytes = bytes.concat([v.br, parseInt(v.effect, 10), v.sp]);
      return byteStr(bytes);
    },
    render: { type: 'color_params', always: ['rgb1'], conditional: ['dual', 'rgb2'] } },
  { id: 'DifParfumStart', opcode: 'DIFFUSER_PARFUM_START', transport: 'udp', section: 'UDP :8439', name: 'Start parfum timer', label: 'DIFFUSER_PARFUM_START',
    desc: 'minutes(u16) + mode(u8), 1-360 min',
    params: [
      { key: 'min', type: 'number', label: 'Minutes', min: 1, max: 360, 'default': 30 },
      { key: 'mode', type: 'enum', label: 'Mode', options: [['1', '1 CONT'], ['2', '2 10 SEC']], 'default': '1' }
    ], build: function (v) { return Proto.packDiffuserParfumStart(v.min, parseInt(v.mode, 10)); } },
  { id: 'DifParfumCancel', opcode: 'DIFFUSER_PARFUM_CANCEL', transport: 'udp', section: 'UDP :8439', name: 'Cancel parfum timer', label: 'DIFFUSER_PARFUM_CANCEL',
    desc: 'Cancel an active parfum window', params: [], build: buildNoPayload },

  { id: 'Mode', transport: 'console', section: 'Console (Telnet :23)', name: 'Force mode directly', label: 'M0-M4',
    desc: 'Drive to mode directly. Confirmation is async (real buzzer beep) - re-check with S shortly after',
    direct_buttons: [['M0', 'M0 OFF'], ['M1', 'M1'], ['M2', 'M2'], ['M3', 'M3'], ['M4', 'M4']] },
  { id: 'Parfum', transport: 'console', section: 'Console (Telnet :23)', name: 'Set parfum timer (console)', label: 'P<min>',
    desc: 'Parfum: decimal minutes 1-360, 0 = cancel + off',
    params: [{ key: 'min', type: 'number', label: 'Minutes', min: 0, max: 360, 'default': 30 }],
    build: function (v) { return 'P' + v.min; } },
  { id: 'EffectNext', transport: 'console', section: 'Console (Telnet :23)', name: 'Next strip effect', label: 'E',
    desc: 'Cycle to next animated strip effect', params: [], build: function () { return 'E'; } },
  { id: 'ColorTest', transport: 'console', section: 'Console (Telnet :23)', name: 'Test solid color', label: 'Crrggbb',
    desc: 'Manual solid strip colour test (full brightness, default speed)',
    params: [{ key: 'rgb', type: 'hexcolor', label: 'Colour', 'default': 'FFFFFF' }],
    build: function (v) { return 'C' + v.rgb; }, render: { type: 'color_params', always: ['rgb'] } },
  { id: 'Status', transport: 'console', section: 'Console (Telnet :23)', name: 'Quick status (console)', label: 'S',
    desc: 'Quick status (mode / strip)', params: [], build: function () { return 'S'; } },
  { id: 'Debug', transport: 'console', section: 'Console (Telnet :23)', name: 'Full debug dump (console)', label: 'D',
    desc: 'Full debug dump: mode, strip, buzzer, WiFi, usage/refill history, EEPROM checkpoint',
    params: [], build: function () { return 'D'; } },
  { id: 'Help', transport: 'console', section: 'Console (Telnet :23)', name: 'List console commands', label: '?/H',
    desc: 'List console commands', params: [], build: function () { return '?'; } },
  { id: 'Quit', transport: 'console', section: 'Console (Telnet :23)', name: 'Close telnet session', label: 'Q',
    desc: 'Close this telnet session', params: [], build: function () { return 'Q'; } }
];

/* ------------------------------------------------------------ TV tables */
// Same 50-slot semantics as the frozen app (EE_SETTINGS_TABLE) - the
// rebuild didn't change what any setting id means, only how it's shipped
// on the wire, so this table is unchanged in content from the ASCII tool.
var DIF_MODE_CHOICES = ['off', 'continuous', '10 sec', '2h after sleep', '4h after sleep'];
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
  [22, 'OTHER', 'select', 5, 0, 0, 'LED FPS', ['15', '25', '30', '60', '90', '120', '150', '200', '240']]
];
var EE_SETTINGS_OPTIONS = EE_SETTINGS_TABLE.map(function (r) { return [String(r[0]), hexb(r[0], 2) + ' ' + r[1] + ' (' + r[6] + ')']; });

var SMARTTV_COMMANDS = [
  { id: 'Hello', opcode: 'HELLO', transport: 'udp', section: 'UDP :8472 - session', name: 'Connect / handshake', label: 'HELLO',
    desc: 'Send first - triggers a full telemetry push (status/climate/lux/link/settings/colours/...)', params: [], build: function () { return Proto.packHello(1); } },
  { id: 'Keepalive', opcode: 'KEEPALIVE', transport: 'udp', section: 'UDP :8472 - session', ack: false, name: 'Keep-alive ping', label: 'KEEPALIVE',
    desc: 'No ack expected - board suspends TX if it hears nothing for a while', params: [], build: buildNoPayload },
  { id: 'DiagHealth', opcode: 'DIAG_HEALTH', transport: 'udp', section: 'UDP :8472 - session', name: 'Diagnostic - health summary', label: 'DIAG_HEALTH',
    desc: 'Read-only health check (WiFi, free RAM, LEDs, TV, diffuser mirror, EEPROM save state)', params: [], build: buildNoPayload },

  { id: 'LedSetColor', opcode: 'LED_SET_COLOR', transport: 'udp', section: 'UDP :8472 - LED', name: 'Set LED color', label: 'LED_SET_COLOR',
    desc: 'Set solid colour on the selected LEDs',
    params: [{ key: 'rgb', type: 'hexcolor', label: 'Colour', 'default': 'FFFFFF' }],
    build: function (v) { var c = hexColorToRgb(v.rgb); return Proto.packLedSetColor(c[0], c[1], c[2]); },
    render: { type: 'color_params', always: ['rgb'] } },
  { id: 'LedGetColor', opcode: 'LED_GET_COLOR', transport: 'udp', section: 'UDP :8472 - LED', ack: false, name: 'Get current LED color', label: 'LED_GET_COLOR',
    desc: 'Request current colour info -> TELEM_COLOR_SYNC (FILL/SETN records)', params: [], build: buildNoPayload, render: { type: 'color_sync' } },
  { id: 'LedSetDual', opcode: 'LED_SET_DUAL_COLOR', transport: 'udp', section: 'UDP :8472 - LED', name: 'Set dual color', label: 'LED_SET_DUAL_COLOR',
    desc: 'Set dual colour (optionally the "shake" variant)',
    params: [
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', 'default': 'FFFFFF' },
      { key: 'rgb2', type: 'hexcolor', label: 'Colour 2', 'default': '000000' },
      { key: 'shake', type: 'checkbox', label: 'Shake', 'default': false }
    ], build: function (v) { var c1 = hexColorToRgb(v.rgb1), c2 = hexColorToRgb(v.rgb2); return Proto.packLedSetDualColor(v.shake ? 1 : 0, c1[0], c1[1], c1[2], c2[0], c2[1], c2[2]); },
    render: { type: 'color_params', always: ['rgb1', 'rgb2'] } },
  { id: 'LedGetDual', opcode: 'LED_GET_DUAL_COLOR', transport: 'udp', section: 'UDP :8472 - LED', ack: false, name: 'Get dual color', label: 'LED_GET_DUAL_COLOR',
    desc: 'Request current dual-colour info -> TELEM_DUAL_COLOR', params: [], build: buildNoPayload },
  { id: 'LedSetSelection', opcode: 'LED_SET_SELECTION', transport: 'udp', section: 'UDP :8472 - LED', name: 'Select LEDs', label: 'LED_SET_SELECTION',
    desc: '61-LED zone selection, packed into an 8-byte bitmask. Layout: 0-29 TV strip, 30-39 COM, 40-41 UCOM, 42-49 BED, 50-59 LAMP, 60 HB (aggregate slot)',
    params: [{ key: 'mask', type: 'ledmask', label: 'LED selection', 'default': repeatChar('0', 61) }],
    build: function (v) { return Proto.packLedSetSelection(ledMaskToBytes(v.mask || repeatChar('0', 61))); } },
  { id: 'LedSetBrightness', opcode: 'LED_SET_BRIGHTNESS', transport: 'udp', section: 'UDP :8472 - LED', name: 'Set LED brightness', label: 'LED_SET_BRIGHTNESS',
    desc: 'Brightness 0-255 (clamped by TELEM_MAX_BRIGHTNESS on the firmware side)',
    params: [{ key: 'value', type: 'range', label: 'Brightness', min: 0, max: 255, 'default': 120 }],
    build: function (v) { return Proto.packLedSetBrightness(v.value); } },
  { id: 'LedSetEnable', opcode: 'LED_SET_ENABLE', transport: 'udp', section: 'UDP :8472 - LED', name: 'Toggle LEDs on/off', label: 'LED_SET_ENABLE',
    desc: 'Toggle LED enable/disable (pure toggle - check TELEM_ENABLE for the new state)', params: [], build: buildNoPayload,
    confirm: 'This flips every LED on the physical strip on or off. Send it?' },

  { id: 'SettingsReadAll', opcode: 'SETTINGS_READ_ALL', transport: 'udp', section: 'UDP :8472 - settings', ack: false, name: 'Read all settings', label: 'SETTINGS_READ_ALL',
    desc: 'Board replies with TELEM_SETTINGS_FULL (all 50 values)', params: [], build: buildNoPayload, render: { type: 'settings_table' } },
  { id: 'SettingsReadOne', opcode: 'SETTINGS_READ_ONE', transport: 'udp', section: 'UDP :8472 - settings', ack: false, name: 'Read one setting', label: 'SETTINGS_READ_ONE',
    desc: '-> TELEM_SETTINGS_ONE',
    params: [{ key: 'id', type: 'enum', label: 'Setting', options: EE_SETTINGS_OPTIONS, 'default': '0' }],
    build: function (v) { return Proto.packSettingsReadOne(parseInt(v.id, 10)); } },
  { id: 'SettingsWrite', opcode: 'SETTINGS_WRITE', transport: 'udp', section: 'UDP :8472 - settings', name: 'Write one setting', label: 'SETTINGS_WRITE',
    desc: 'id(u8)+val(u8) pair - raw field in protocol_table.json (variable count; this tool always sends exactly one pair)',
    params: [
      { key: 'id', type: 'enum', label: 'Setting', options: EE_SETTINGS_OPTIONS, 'default': '0' },
      { key: 'val', type: 'number', label: 'Value', min: 0, max: 255, 'default': 0 }
    ], build: function (v) { return byteStr([parseInt(v.id, 10), v.val]); }, custom_panel: 'settings_write' },

  { id: 'SetTestMode', opcode: 'SET_TEST_MODE', transport: 'udp', section: 'UDP :8472 - test mode', name: 'Force a test mode', label: 'SET_TEST_MODE',
    desc: '0 none/cancel, 1 force TV on, 2 force TV off, 3 UDPRAW stream sim, 4 motion common sim, 5 motion bed sim',
    params: [{ key: 'mode', type: 'enum', label: 'Mode', options: [['0', '0 none (cancel)'], ['1', '1 force TV on'], ['2', '2 force TV off'], ['3', '3 UDPRAW stream sim'], ['4', '4 motion common sim'], ['5', '5 motion bed sim']], 'default': '0' }],
    build: function (v) { return Proto.packSetTestMode(parseInt(v.mode, 10)); } },
  { id: 'SetTestDiffuser', opcode: 'SET_TEST_DIFFUSER', transport: 'udp', section: 'UDP :8472 - test mode', name: 'Simulate diffuser action', label: 'SET_TEST_DIFFUSER',
    desc: '0 off, 1-4 mode CONT/10SEC/2H/4H, 255 = re-push current mode with a fresh random colour',
    params: [{ key: 'value', type: 'enum', label: 'Action', options: [['0', '0 off'], ['1', '1 mode CONT'], ['2', '2 mode 10 SEC'], ['3', '3 mode 2H after sleep'], ['4', '4 mode 4H after sleep'], ['255', '255 random colour refresh']], 'default': '0' }],
    build: function (v) { return Proto.packSetTestDiffuser(parseInt(v.value, 10)); } },
  { id: 'SetTestLux', opcode: 'SET_TEST_LUX', transport: 'udp', section: 'UDP :8472 - test mode', name: 'Force ambient light level', label: 'SET_TEST_LUX',
    desc: 'Force a lux level, 1-4',
    params: [{ key: 'level', type: 'enum', label: 'Level', options: [['1', '1'], ['2', '2'], ['3', '3'], ['4', '4']], 'default': '1' }],
    build: function (v) { return Proto.packSetTestLux(parseInt(v.level, 10)); } },

  { id: 'SetAmbientMode', opcode: 'SET_AMBIENT_MODE', transport: 'udp', section: 'UDP :8472 - ambient/telnet', name: 'Toggle ambient mode', label: 'SET_AMBIENT_MODE',
    desc: 'On returns ack BLOCKED (not OK) if UDPRAW is active, motion (bed) is active, or the TV is on',
    // direct_buttons bypasses build() entirely (ui.js sends opt[0] as the
    // raw payload directly) - so these must already be byte-strings, not
    // ASCII digits like the old ASCII-era table used.
    direct_buttons: [[Proto.packSetAmbientMode(0), 'Off'], [Proto.packSetAmbientMode(1), 'On']] },
  { id: 'SetTelnetEnable', opcode: 'SET_TELNET_ENABLE', transport: 'udp', section: 'UDP :8472 - ambient/telnet', name: 'Enable/disable remote Telnet', label: 'SET_TELNET_ENABLE',
    desc: 'Flashed OFF, no EEPROM persistence - only ever armed by this command, never survives a reboot unattended',
    direct_buttons: [[Proto.packSetTelnetEnable(0), 'Disable'], [Proto.packSetTelnetEnable(1), 'Enable']] },
  { id: 'SetTelnetVerbosity', opcode: 'SET_TELNET_VERBOSITY', transport: 'udp', section: 'UDP :8472 - ambient/telnet', name: 'Set Telnet verbosity', label: 'SET_TELNET_VERBOSITY',
    desc: 'Filters termMsgLog()\'s Telnet mirror only (not the UDP/MQTT LOG frame). Not persisted - resets to normal on reboot.',
    direct_buttons: [[Proto.packSetTelnetVerbosity(0), 'Normal'], [Proto.packSetTelnetVerbosity(1), 'Debug'], [Proto.packSetTelnetVerbosity(2), 'Verbose']] },

  { id: 'SetMqttCreds', opcode: 'SET_MQTT_CREDENTIALS', transport: 'udp', section: 'UDP :8472 - mqtt', name: 'Set MQTT cloud credentials', label: 'SET_MQTT_CREDENTIALS',
    desc: 'userLen(u8)+user+passLen(u8)+pass, raw bytes - hand-built (raw field in protocol_table.json)',
    params: [
      { key: 'user', type: 'text', label: 'Username', 'default': '' },
      { key: 'pass', type: 'text', label: 'Password', 'default': '' }
    ], build: function (v) {
      var u = v.user || '', p = v.pass || '';
      return String.fromCharCode(u.length & 0xFF) + u + String.fromCharCode(p.length & 0xFF) + p;
    } },

  { id: 'DifStatus_relay', opcode: 'DIFFUSER_STATUS_QUERY', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Check diffuser status (relay)', label: 'DIFFUSER_STATUS_QUERY',
    desc: 'Request diffuser status via the relay -> TELEM_DIFFUSER_STATUS',
    params: [{ key: 'verbose', type: 'checkbox', label: 'Verbose', 'default': false }],
    build: function (v) { return Proto.packDiffuserStatusQuery(v.verbose ? 1 : 0); }, timeout_ms: 4500, render: { type: 'diffuser_status' } },
  { id: 'DifHistory_relay', opcode: 'DIFFUSER_HISTORY_QUERY', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Get diffuser refill history (relay)', label: 'DIFFUSER_HISTORY_QUERY',
    desc: 'Request full diffuser refill history via the relay', params: [], build: buildNoPayload, timeout_ms: 4500, render: { type: 'diffuser_history' }, custom_panel: 'diffuser_history' },
  { id: 'DifShutdown_relay', opcode: 'DIFFUSER_SHUTDOWN', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Shut down diffuser (relay)', label: 'DIFFUSER_SHUTDOWN',
    desc: 'Shutdown diffuser via the relay. Ack is deferred until the diffuser itself answers - allow extra time',
    params: [], build: buildNoPayload, timeout_ms: 4500, confirm: 'Shut down the diffuser now?' },
  { id: 'DifTurnOn_relay', opcode: 'DIFFUSER_TURN_ON', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Turn on diffuser (relay)', label: 'DIFFUSER_TURN_ON',
    desc: 'Turn diffuser on via the relay: mode + colour + effect. BLOCKED unless a source is currently active (fire SET_TEST_MODE=1 or SET_AMBIENT_MODE=1 first)',
    params: [
      { key: 'mode', type: 'enum', label: 'Mode', options: DIF_MODE_OPTIONS, 'default': '1' },
      { key: 'rgb1', type: 'hexcolor', label: 'Colour 1', 'default': 'FFFFFF' },
      { key: 'br', type: 'range', label: 'Brightness', min: 0, max: 255, 'default': 200 },
      { key: 'effect', type: 'enum', label: 'Effect', options: DIF_EFFECT_OPTIONS, 'default': '0' },
      { key: 'sp', type: 'range', label: 'Speed ms', min: 5, max: 255, 'default': 50 }
    ], build: function (v) {
      var c1 = hexColorToRgb(v.rgb1);
      return byteStr([parseInt(v.mode, 10), 0, c1[0], c1[1], c1[2], v.br, parseInt(v.effect, 10), v.sp]);
    }, timeout_ms: 4500, render: { type: 'color_params', always: ['rgb1'] } },
  { id: 'DifParfumStart_relay', opcode: 'DIFFUSER_PARFUM_START', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Start diffuser parfum timer (relay)', label: 'DIFFUSER_PARFUM_START',
    desc: 'Parfum via the relay - not gated by an active source (unlike Turn On)',
    params: [
      { key: 'min', type: 'number', label: 'Minutes', min: 1, max: 360, 'default': 30 },
      { key: 'mode', type: 'enum', label: 'Mode', options: [['1', '1 CONT'], ['2', '2 10 SEC']], 'default': '1' }
    ], build: function (v) { return Proto.packDiffuserParfumStart(v.min, parseInt(v.mode, 10)); }, timeout_ms: 4500 },
  { id: 'DifParfumCancel_relay', opcode: 'DIFFUSER_PARFUM_CANCEL', transport: 'udp', section: 'UDP :8472 - diffuser relay', name: 'Cancel diffuser parfum timer (relay)', label: 'DIFFUSER_PARFUM_CANCEL',
    desc: 'Cancel active parfum via the relay. Ack is REJECTED, not OK, if parfum was not actually running',
    params: [], build: buildNoPayload, timeout_ms: 4500 }
];

var COMMAND_TABLES = { diffuser: DIFFUSER_COMMANDS, smarttv: SMARTTV_COMMANDS };
