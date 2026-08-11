/* engine.js - layout-agnostic simulation engine, shared by every "round 2"
 * UX mockup (command palette / dashboard / wizard / tile board / IDE).
 *
 * This is the same fake-device logic round 1's app.js had, pulled out from
 * the DOM-rendering code so five very different-looking UIs can all sit on
 * top of one identical, consistent brain: same fakeState, same simulated
 * acks, same protocol decode. Only the presentation differs between
 * mockups now - which is the point of this round. ES5 on purpose (see
 * app.js's header comment - mshta's JScript engine, not Chrome).
 *
 * Depends on data.js being loaded first (COMMAND_TABLES, hexb, categoryMeta,
 * EE_SETTINGS_TABLE, TV_SETTINGS, DIF_MODE_NAMES, DIF_EFFECT_NAMES, ...).
 */

var Engine = (function () {
  'use strict';

  function repeatChar(ch, n) { var s = ''; for (var i = 0; i < n; i++) s += ch; return s; }
  function rand(a, b) { return a + Math.random() * (b - a); }
  function pad2(n) { n = String(n); return n.length < 2 ? '0' + n : n; }
  function nowStamp() {
    var d = new Date();
    return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) + ' ' +
      pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
  }
  function fmtDurationMin(total) {
    if (total < 60) return total + 'm';
    var h = Math.floor(total / 60), m = total % 60;
    return m === 0 ? h + 'h' : h + 'h ' + m + 'm';
  }

  /* ============================================================ state */
  var devices = {
    diffuser: { name: 'Diffuser', ip: '192.168.1.203', udpPort: 8439, telnetPort: 23, timeoutMs: 1200, hasConsole: true },
    smarttv: { name: 'SmartTV', ip: '192.168.1.202', udpPort: 8472, telnetPort: 23, timeoutMs: 1200, hasConsole: false },
  };

  var fakeState = {
    diffuser: { mode: 1, strip: 1, parfumMin: 0, usageMin: 128, avgMin: 96, refillCount: 3, totalRefills: 37,
                history: [45, 52, 38, 60, 41, 30, 55], effect: 0, color1: 'FFFFFF', color2: '000000' },
    smarttv: { tvOn: false, motion: 2, udpraw: false, ambient: 0, difSummary: 1, testMode: 0, ledsOn: true,
               lux: 2, brightness: 80, color1: 'FF8040', color2: '2040FF', mask: repeatChar('0', 61), settings: {} },
  };
  EE_SETTINGS_TABLE.forEach(function (row) { fakeState.smarttv.settings[row[0]] = row[3]; });
  for (var _ri = 45; _ri <= 49; _ri++) if (!(_ri in fakeState.smarttv.settings)) fakeState.smarttv.settings[_ri] = 0;

  var DIF = fakeState.diffuser;
  var TV = fakeState.smarttv;

  var seq = 0;
  function nextSeq() { var s = seq; seq = (seq + 1) % 256; return s; }

  var TESTMODE_NAMES = ['none', 'TV on (forced)', 'TV off (forced)', 'UDPRAW stream (sim)',
    'motion, front (sim)', 'motion, bed (sim)', 'diffuser test', 'lux test'];
  var MOTION_STATUS_NAMES = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed'];
  var AMBIENT_SUMMARY_NAMES = { 0: 'off', 1: 'on', 2: 'ready' };
  var DIF_SUMMARY_NAMES = { 0: 'off', 1: 'on', 2: 'out of water', 3: 'no response', 4: 'parfum running' };

  /* ============================================================ log bus */
  var logLines = [];
  var logListeners = [];
  function subscribeLog(fn) { logListeners.push(fn); }
  function logAppend(tag, cls, text) {
    var ts = nowStamp();
    var line = { ts: ts, tag: tag, cls: cls, text: text };
    logLines.push('[' + ts + '] [' + tag + '] ' + text);
    logListeners.forEach(function (fn) { fn(line); });
  }
  function logSys(text) { logAppend('SYS', 'sys', text); }

  function saveLog() {
    if (!logLines.length) { logSys('No log entries to save'); return false; }
    var header = repeatChar('=', 66) + '\nFuZz TestMode (HTA mockup) log - saved ' + nowStamp() + '\n' +
      'Diffuser default: ' + devices.diffuser.ip + ':' + devices.diffuser.udpPort + ' (udp) / :' + devices.diffuser.telnetPort + ' (telnet)\n' +
      'SmartTV  default: ' + devices.smarttv.ip + ':' + devices.smarttv.udpPort + ' (udp)\n' +
      repeatChar('=', 66) + '\n';
    var text = header + logLines.join('\n') + '\n';
    try {
      var blob = new Blob([text], { type: 'text/plain' });
      var url = URL.createObjectURL(blob);
      var a = document.createElement('a');
      a.href = url; a.download = 'TestMode-mockup-' + Date.now() + '.log';
      document.body.appendChild(a); a.click(); document.body.removeChild(a);
      setTimeout(function () { URL.revokeObjectURL(url); }, 2000);
      logSys('Log downloaded (' + logLines.length + ' lines)');
      return true;
    } catch (e) { logSys('Save failed: ' + e.message); return false; }
  }

  /* ==================================================== protocol decode */
  function parseStatusFields(text) {
    if (!text || text.slice(0, 2) !== 'Ds' || text.length < 24) return null;
    var mm = parseInt(text.slice(2, 4), 16);
    var modeName = mm === 5 ? 'OUT OF WATER' : (DIF_MODE_NAMES[mm] || ('M' + mm));
    return {
      mode: mm, modeName: modeName, strip: parseInt(text.slice(4, 6), 16),
      parfumMin: parseInt(text.slice(6, 10), 16), usageMin: parseInt(text.slice(10, 14), 16),
      avgMin: parseInt(text.slice(14, 18), 16), refillCount: parseInt(text.slice(18, 20), 16),
      totalRefills: parseInt(text.slice(20, 24), 16),
    };
  }
  function parseSettingsReply(text) {
    if (!text || text.charAt(0) !== 'S' || text.length < 1 + 50 * 4) return null;
    var idxToName = {}; TV_SETTINGS.forEach(function (r) { idxToName[r[0]] = r[1]; });
    var rows = [];
    for (var i = 0; i < 50; i++) {
      var base = 1 + i * 4;
      var idx = parseInt(text.slice(base, base + 2), 16);
      var val = parseInt(text.slice(base + 2, base + 4), 16);
      if (isNaN(idx) || isNaN(val)) continue;
      rows.push([idx, idxToName[idx] || ('idx ' + idx), val]);
    }
    return rows;
  }
  function describeReply(text) {
    if (!text) return null;
    if (text.charAt(0) === '!') return text.slice(1);
    if (text.charAt(0) === '*' && text.length >= 6) {
      var level = parseInt(text.charAt(1), 16), body = text.slice(6).trim();
      var levelNames = { 0: 'ERROR', 1: 'WARN', 2: 'INFO', 3: 'DEBUG', 4: 'SECTION', 5: 'GAP' };
      return '[' + (levelNames[level] || 'L' + level) + '] ' + (body || '(empty)');
    }
    if (text.charAt(0) === 's' && text.length >= 11) {
      var tv = parseInt(text.slice(1, 3), 16), mo = parseInt(text.slice(3, 5), 16), ur = parseInt(text.slice(5, 7), 16),
        am = parseInt(text.slice(7, 9), 16), ds = parseInt(text.slice(9, 11), 16);
      return 'Status - TV ' + (tv ? 'on' : 'off') + ', motion ' + (MOTION_STATUS_NAMES[mo] || mo) + ', ambilight ' +
        (ur ? 'on' : 'off') + ', ambient ' + (AMBIENT_SUMMARY_NAMES[am] || am) + ', diffuser ' + (DIF_SUMMARY_NAMES[ds] || ds);
    }
    if (text.charAt(0) === 'H' && text.length >= 5) return 'Climate - ' + parseInt(text.slice(1, 3), 16) + ' C, ' + parseInt(text.slice(3, 5), 16) + '% humidity';
    if (text.charAt(0) === 'E' && text.length >= 2) return 'LEDs ' + (parseInt(text.charAt(1), 16) ? 'enabled' : 'disabled');
    if (text.charAt(0) === 'M' && text.length >= 5) return 'Ambient light level - ' + parseInt(text.slice(1, 5), 16);
    if (text.charAt(0) === 'w' && text.length >= 3) return 'WiFi signal - -' + parseInt(text.slice(1, 3), 16) + ' dBm';
    if (text.charAt(0) === 'f' && text.length >= 5) { var mask = parseInt(text.slice(1, 5), 16); return 'Fault flags - ' + (mask ? '0x' + mask.toString(16).toUpperCase() : 'none'); }
    if (text.slice(0, 2) === 'LM') return 'Max brightness supported - ' + parseInt(text.slice(2), 16);
    if (text.charAt(0) === '@' && text.length >= 3) { var tm = parseInt(text.slice(1, 3), 16); return 'Active test mode - ' + (TESTMODE_NAMES[tm] || tm); }
    if (text.slice(0, 2) === 'Dh') {
      var count = parseInt(text.slice(2, 4), 16), mins = [], pos = 4;
      while (pos + 4 <= text.length) { mins.push(parseInt(text.slice(pos, pos + 4), 16)); pos += 4; }
      mins = mins.slice(0, count);
      return mins.length ? ('Refill history - ' + count + ' cycles: ' + mins.join(', ') + ' min') : 'Refill history - no cycles recorded yet';
    }
    if (text === 'LK') return 'LK colour sync';
    return null;
  }

  /* ======================================================== fake sends */
  function buildDsRaw(fs) {
    return 'Ds' + hexb(fs.mode, 2) + hexb(fs.strip, 2) + hexb(fs.parfumMin, 4) + hexb(fs.usageMin, 4) +
      hexb(fs.avgMin, 4) + hexb(fs.refillCount, 2) + hexb(fs.totalRefills, 4);
  }
  function buildDhRaw(fs) {
    var hist = fs.history.slice(-10);
    var s = 'Dh' + hexb(hist.length, 2);
    hist.forEach(function (m) { s += hexb(m, 4); });
    return s;
  }
  function buildSettingsRaw(fs) {
    var s = 'S';
    for (var i = 0; i < 50; i++) { var val = fs.settings[i] != null ? fs.settings[i] : 0; s += hexb(i, 2) + hexb(Math.min(val, 255), 2); }
    return s;
  }
  function buildSStatusRaw() {
    return 's' + hexb(TV.tvOn ? 1 : 0, 2) + hexb(TV.motion, 2) + hexb(TV.udpraw ? 1 : 0, 2) + hexb(TV.ambient, 2) + hexb(TV.difSummary, 2);
  }
  function describeFakeLK() {
    var count = (TV.mask.match(/1/g) || []).length || 61;
    return 'LK colour sync · ' + count + ' LED' + (count === 1 ? '' : 's') + ' · 1 fill record · fill 0-' + (count - 1) + ' #' + TV.color1;
  }
  function buildHandshakeEntries() {
    return [
      { raw: buildSStatusRaw() }, { raw: 'H' + hexb(24, 2) + hexb(41, 2) }, { raw: 'E' + hexb(TV.ledsOn ? 1 : 0, 1) },
      { raw: 'M' + hexb(TV.lux * 287, 4) }, { raw: 'w' + hexb(58, 2) + hexb(0, 2) }, { raw: 'f0000' },
      { raw: 'LM' + hexb(120, 2) }, { raw: '@' + hexb(TV.testMode, 2) }, { raw: 'LK', desc: describeFakeLK() },
    ];
  }

  var DIFFUSER_SIM = {
    Ds: function () { return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    Dc: function () { return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    Df: function () { if (DIF.parfumMin > 0) return { ackKind: 'locked' }; DIF.mode = 0; DIF.strip = 0; return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    DpStart: function (v) { DIF.parfumMin = v.min; DIF.mode = v.e === '2' ? 2 : 1; DIF.strip = 1; return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    DpCancel: function () { if (DIF.parfumMin <= 0) return { ackKind: 'rejected' }; DIF.parfumMin = 0; return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    Dn: function (v) { DIF.mode = parseInt(v.mode, 10); DIF.strip = 1; DIF.color1 = v.rgb1; if (v.dual) DIF.color2 = v.rgb2; return { entries: [{ raw: buildDsRaw(DIF) }] }; },
    Dh: function () { return { entries: [{ raw: buildDhRaw(DIF) }] }; },
    DiagHealth: function () { return { entries: [{ raw: '!WiFi OK · Water OK · Free heap 24.6KB · EEPROM saved 2m ago · Buzzer OK · History ' + Math.min(DIF.history.length, 10) + '/10' }] }; },
    DiagParfum: function () { var active = DIF.parfumMin > 0; return { entries: [{ raw: '!active=' + (active ? 'YES' : 'NO') + ' remaining=' + DIF.parfumMin + 'min pending=NO snapshot mode=' + DIF.mode + ' strip=' + DIF.strip }] }; },
  };

  var SMARTTV_SIM = {
    Z: function () { return { entries: buildHandshakeEntries() }; },
    k: function () { return { ackKind: 'sent', entries: [] }; },
    X: function () { TV.ledsOn = !TV.ledsOn; return { entries: [{ raw: 'E' + hexb(TV.ledsOn ? 1 : 0, 1) }] }; },
    AtEnum: function (v) {
      var ii = parseInt(v.ii, 10); TV.testMode = ii;
      if (ii === 1) TV.tvOn = true; else if (ii === 2) TV.tvOn = false;
      else if (ii === 3) TV.udpraw = true; else if (ii === 4) TV.motion = 3; else if (ii === 5) TV.motion = 4;
      else { TV.udpraw = false; TV.motion = 2; }
      return { entries: [{ raw: '@' + hexb(TV.testMode, 2) }] };
    },
    AtDif: function (v) { TV.difSummary = v.vv === '00' ? 0 : 1; return { entries: [{ raw: buildSStatusRaw() }] }; },
    AtLux: function (v, payload) { var level = parseInt(payload.slice(2), 16); TV.lux = level; return { entries: [{ raw: 'M' + hexb(level * 287, 4) }] }; },
    SRead: function () { return { entries: [{ raw: buildSettingsRaw(TV) }] }; },
    Ambient: function (v, payload) {
      if (payload === 'A1') { if (TV.tvOn) return { ackKind: 'blocked' }; TV.ambient = 1; } else { TV.ambient = 0; }
      return { entries: [{ raw: buildSStatusRaw() }] };
    },
    Debug: function (v) {
      var name = TV_DEBUG_NAMES[parseInt(v.ii, 10)] || 'section';
      return { entries: [
        { raw: '*204001', desc: '[APP][DEBUG] ' + name + ' — dump line 1' },
        { raw: '*204002', desc: '[APP][DEBUG] ' + name + ' — dump line 2' },
        { raw: '*204003', desc: '[APP][DEBUG] ' + name + ' — dump line 3' },
      ] };
    },
    DiagHealth: function () { return { entries: [{ raw: '!WiFi good · RAM 18.2KB free · LEDs ' + (TV.ledsOn ? 'on' : 'off') + ' · TV ' + (TV.tvOn ? 'on' : 'off') + ' · Diffuser mirror M' + DIF.mode + ' · EEPROM saved 4m ago' }] }; },
    Ds_relay: function () { return { entries: [{ raw: buildDsRaw(DIF) }], delayMs: rand(900, 1900) }; },
    Dh_relay: function () { return { entries: [{ raw: buildDhRaw(DIF) }], delayMs: rand(900, 1900) }; },
    Df_relay: function () { if (DIF.parfumMin > 0) return { ackKind: 'locked', delayMs: rand(900, 1900) }; DIF.mode = 0; DIF.strip = 0; return { entries: [{ raw: buildDsRaw(DIF) }], delayMs: rand(900, 1900) }; },
    Dn_relay: function (v) { DIF.mode = parseInt(v.mode, 10); DIF.strip = 1; return { entries: [{ raw: buildDsRaw(DIF) }], delayMs: rand(900, 1900) }; },
    DpStart_relay: function (v) { DIF.parfumMin = v.min; DIF.mode = parseInt(v.e, 10); DIF.strip = 1; return { entries: [{ raw: buildDsRaw(DIF) }], delayMs: rand(900, 1900) }; },
    DpCancel_relay: function () { if (DIF.parfumMin <= 0) return { ackKind: 'rejected', delayMs: rand(900, 1900) }; DIF.parfumMin = 0; return { entries: [{ raw: buildDsRaw(DIF) }], delayMs: rand(900, 1900) }; },
    LB: function (v) { TV.brightness = v.vv; return { entries: [{ raw: 'LB' + hexb(v.vv, 2) }] }; },
    LC_get: function () { return { entries: [{ raw: 'LK', desc: describeFakeLK() }] }; },
    LC_set: function (v) { TV.color1 = v.rgb; return { entries: [{ raw: 'LC' + v.rgb }] }; },
    LD_get: function () { return { entries: [{ raw: 'LD' + TV.color1 + TV.color2 }] }; },
    LD_set: function (v) { TV.color1 = v.rgb1; TV.color2 = v.rgb2; return { entries: [{ raw: 'LD' + v.rgb1 + v.rgb2 }] }; },
    Ld_set: function (v) { TV.color1 = v.rgb1; TV.color2 = v.rgb2; return { entries: [{ raw: 'Ld' + v.rgb1 + v.rgb2 }] }; },
    LO: function (v) { TV.mask = v.mask; var count = (v.mask.match(/1/g) || []).length; return { entries: [{ raw: 'LO ack', desc: count + ' LED' + (count === 1 ? '' : 's') + ' selected' }] }; },
  };

  var CONSOLE_SIM = {
    Mode: function (v, payload) { var m = parseInt(payload.slice(1), 10); DIF.mode = m; if (m === 0) DIF.strip = 0; return 'OK - mode M' + m + ' set (beep confirmed)'; },
    Parfum: function (v) { DIF.parfumMin = v.min; return v.min > 0 ? ('Parfum timer set to ' + v.min + ' min') : 'Parfum cancelled'; },
    EffectNext: function () { DIF.effect = (DIF.effect + 1) % DIF_EFFECT_NAMES.length; return 'Effect -> ' + DIF_EFFECT_NAMES[DIF.effect]; },
    ColorTest: function (v) { DIF.color1 = v.rgb; return 'Color set #' + v.rgb; },
    Status: function () { return 'Mode=M' + DIF.mode + ' Strip=' + DIF.strip; },
    Debug: function () { return 'Mode=M' + DIF.mode + ' Strip=' + DIF.strip + '\nBuzzer=OK WiFi=-58dBm (strong)\nUsage=' + DIF.usageMin + 'min Avg=' + DIF.avgMin + 'min Refills=' + DIF.refillCount + '/10 (total ' + DIF.totalRefills + ')\nEEPROM checkpoint OK'; },
    Help: function () { return 'Commands: M0-M4  P<min>  E  C<rrggbb>  S  D  ?  Q'; },
    Quit: function () { return 'bye'; },
  };

  function simulateUdp(deviceKey, spec, sentVals, payload) {
    var table = deviceKey === 'diffuser' ? DIFFUSER_SIM : SMARTTV_SIM;
    var fn = table[spec.id];
    if (!fn) return { ackKind: 'ok', entries: [] };
    var result = fn(sentVals, payload) || {};
    if (!result.ackKind) result.ackKind = 'ok';
    return result;
  }
  function simulateConsole(spec, sentVals, payload) {
    var fn = CONSOLE_SIM[spec.id];
    return fn ? fn(sentVals, payload) : '';
  }

  var ACK_CODE = { ok: 0, clamped: 1, rejected: 2, blocked: 3, locked: 4, nowater: 5, unsupported: 6 };

  /* --------------------------------------------------------- full send */
  // One entry point every UI can call: builds the envelope, logs send/recv,
  // simulates, and hands the caller back everything needed to paint a
  // result - no DOM work happens in here.
  function send(deviceKey, spec, payload, sentVals, cb) {
    var device = devices[deviceKey];
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';

    if (spec.transport === 'console') {
      var replyText = simulateConsole(spec, sentVals, payload);
      setTimeout(function () {
        logAppend(tag, 'send', 'TCP > ' + device.ip + ' ' + payload);
        logAppend(tag, 'recv', 'TCP < ' + (replyText || '(empty)').replace(/\n/g, ' \\n '));
        cb({ transport: 'console', ackKind: replyText ? 'ok' : 'timeout', text: replyText });
      }, rand(120, 320));
      return;
    }

    var envelope = spec.envelope !== false;
    var s = -1, wrapped = payload;
    if (envelope) { s = nextSeq(); wrapped = '#' + hexb(s, 2) + payload; }
    var sim = simulateUdp(deviceKey, spec, sentVals, payload);
    var delay = sim.delayMs || rand(150, 420);

    setTimeout(function () {
      logAppend(tag, 'send', 'UDP > ' + device.ip + ' ' + wrapped);
      var ackKind = envelope ? (sim.ackKind || 'ok') : 'sent';
      if (envelope) {
        var code = ACK_CODE[ackKind]; if (code == null) code = 0;
        logAppend(tag, 'recv', 'UDP < #' + hexb(s, 2) + code.toString(16).toUpperCase());
      }
      (sim.entries || []).forEach(function (e) { logAppend(tag, 'recv', 'UDP < ' + e.raw); });
      cb({ transport: 'udp', ackKind: ackKind, entries: sim.entries || [] });
    }, delay);
  }

  /* --------------------------------------------------------- keep-alive */
  var keepAliveTimer = null;
  function startKeepAlive(activeDeviceKeyFn, onTick) {
    function tick() {
      var dk = activeDeviceKeyFn(), tag = dk === 'diffuser' ? 'DIF' : 'TV';
      var payload = dk === 'smarttv' ? 'k' : 'Dc';
      var wrapped = dk === 'smarttv' ? payload : ('#' + hexb(nextSeq(), 2) + payload);
      logAppend(tag, 'send', 'keep-alive ' + wrapped + ' -> OK');
      if (onTick) onTick();
    }
    tick();
    keepAliveTimer = setInterval(tick, 5000);
  }
  function stopKeepAlive() { clearInterval(keepAliveTimer); }

  return {
    devices: devices, fakeState: fakeState, DIF: DIF, TV: TV,
    nextSeq: nextSeq, rand: rand, fmtDurationMin: fmtDurationMin, repeatChar: repeatChar,
    logLines: logLines, subscribeLog: subscribeLog, logAppend: logAppend, logSys: logSys, saveLog: saveLog,
    parseStatusFields: parseStatusFields, parseSettingsReply: parseSettingsReply, describeReply: describeReply,
    send: send, simulateUdp: simulateUdp, simulateConsole: simulateConsole,
    startKeepAlive: startKeepAlive, stopKeepAlive: stopKeepAlive,
    ACK_CODE: ACK_CODE,
  };
})();
