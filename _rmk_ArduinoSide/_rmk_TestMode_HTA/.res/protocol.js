/* protocol.js - binary v1 frame engine for TestMode.hta (00_PLAN.md Phase 6
 * follow-up work). Adapted from the frozen TestMode_APP_HTA's protocol.js:
 * same device registry / log bus / keep-alive / send() call shape (still
 * {transport, ackKind, entries:[{raw,desc}]}, so ui.js and the sheet/result
 * wiring barely change), but the actual wire format is now the binary v1
 * frame (MAGIC/FLAGS/SEQ/OPCODE/payload/CRC8, see 01_PROTOCOL.md) instead
 * of the old ASCII "#SS<cmd>" envelope. Byte payloads are plain JS strings
 * where charCodeAt(i) is wire byte i - matches Proto's own pack/unpack
 * contract (protocol_opcodes.js, generated) and Net.sendUdp()'s.
 *
 * ACK vs TELEM classification mirrors rmk_testmode.py's _print_received()
 * exactly: by the frame's own FLAGS bits (IS_ACK first, then IS_TELEMETRY,
 * else CMD/reply) - not by opcode number ranges or anything guessed.
 */

var Engine = (function () {
  'use strict';

  function repeatChar(ch, n) { var s = ''; for (var i = 0; i < n; i++) s += ch; return s; }
  function pad2(n) { n = String(n); return n.length < 2 ? '0' + n : n; }
  function nowStamp() {
    var d = new Date();
    return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) + ' ' +
      pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds());
  }
  function fmtDurationMin(total) {
    var h = Math.floor(total / 60), m = total % 60;
    return h + 'h ' + m + 'm';
  }
  function hex2(b) { var s = (b & 0xFF).toString(16).toUpperCase(); return s.length < 2 ? '0' + s : s; }
  function toHex(byteStr) {
    var out = '';
    for (var i = 0; i < byteStr.length; i++) out += hex2(byteStr.charCodeAt(i));
    return out;
  }
  // GENERATED_NAME -> PascalCase, matches gen_protocol.py's pascal() exactly -
  // used to look up Proto['pack'+name]/Proto['unpack'+name] for any opcode.
  function pascal(name) {
    var parts = name.split('_'), out = '';
    for (var i = 0; i < parts.length; i++) {
      var p = parts[i];
      out += p.length ? (p.charAt(0).toUpperCase() + p.slice(1).toLowerCase()) : '';
    }
    return out;
  }

  /* ============================================================ state */
  var devices = {
    diffuser: { name: 'Diffuser', ip: '192.168.1.203', udpPort: 8439, telnetPort: 23, timeoutMs: 1500, hasConsole: false },
    smarttv: { name: 'SmartTV', ip: '192.168.1.202', udpPort: 8472, telnetPort: 23, timeoutMs: 1500, hasConsole: true }
  };

  var seq = 0;
  function nextSeq() { seq = (seq + 1) % 256; return seq; }

  /* ============================================================ log bus */
  var logLines = [];
  var logListeners = [];
  function subscribeLog(fn) { logListeners.push(fn); }
  function logAppend(tag, cls, text) {
    var ts = nowStamp();
    var line = { ts: ts, tag: tag, cls: cls, text: text };
    logLines.push('[' + ts + '] [' + tag + '] ' + text);
    for (var i = 0; i < logListeners.length; i++) logListeners[i](line);
  }
  function logSys(text) { logAppend('SYS', 'sys', text); }

  function saveLog() {
    if (!logLines.length) { logSys('No log entries to save'); return null; }
    var header = repeatChar('=', 66) + '\r\nFuZz TestMode (rmk) log - saved ' + nowStamp() + '\r\n' +
      'Diffuser default: ' + devices.diffuser.ip + ':' + devices.diffuser.udpPort + ' (udp, v1 binary) / :' + devices.diffuser.telnetPort + ' (telnet)\r\n' +
      'SmartTV  default: ' + devices.smarttv.ip + ':' + devices.smarttv.udpPort + ' (udp, v1 binary) / :' + devices.smarttv.telnetPort + ' (telnet)\r\n' +
      repeatChar('=', 66) + '\r\n';
    var text = header + logLines.join('\r\n') + '\r\n';
    var fname = 'TestMode-rmk-' + nowStamp().replace(/[: ]/g, '-') + '.log';
    var path = Net.baseDir + '\\' + fname;
    try {
      Net.writeTextFile(path, text);
      logSys('Log saved to: ' + path);
      Net.openInNotepad(path);
      return path;
    } catch (e) {
      logSys('Save failed: ' + e.message);
      return null;
    }
  }

  /* ---------------------------------------------------------- keep-alive */
  var keepAliveTimer = null;
  function startKeepAlive(activeDeviceKeyFn, onTick) {
    function tick() {
      var dk = activeDeviceKeyFn(), tag = dk === 'diffuser' ? 'DIF' : 'TV';
      var device = devices[dk];
      var s = nextSeq();
      var frame = buildFrame(Proto.Opcode.KEEPALIVE, s, 0, '');
      Net.sendUdp(device.ip, device.udpPort, frame, 300, function (result) {
        logAppend(tag, 'send', 'keep-alive KEEPALIVE seq=' + s + ' -> ' + (result.status === 'ERROR' ? 'ERROR: ' + result.message : result.status));
      });
      if (onTick) onTick();
    }
    tick();
    keepAliveTimer = window.setInterval(tick, 5000);
  }
  function stopKeepAlive() { window.clearInterval(keepAliveTimer); }

  /* ============================================================ framing */
  // See 01_PROTOCOL.md SS1: MAGIC(1) FLAGS(1) SEQ(1) OPCODE(1) payload(0-247) CRC8(1).
  function buildFrame(opcode, seqNum, flags, payload) {
    payload = payload || '';
    var body = String.fromCharCode(Proto.FRAME_MAGIC) + String.fromCharCode(flags & 0xFF) +
      String.fromCharCode(seqNum & 0xFF) + String.fromCharCode(opcode & 0xFF) + payload;
    return body + String.fromCharCode(Proto.crc8(body));
  }

  function parseFrame(data) {
    if (!data || data.length < Proto.FRAME_OVERHEAD_BYTES) return { ok: false, reason: 'too short (' + (data ? data.length : 0) + ' bytes)' };
    if ((data.charCodeAt(0) & 0xFF) !== Proto.FRAME_MAGIC) return { ok: false, reason: 'bad magic 0x' + hex2(data.charCodeAt(0)) };
    var bodyLen = data.length - 1;
    var crcByte = data.charCodeAt(data.length - 1) & 0xFF;
    var crcCalc = Proto.crc8(data.substr(0, bodyLen));
    if (crcCalc !== crcByte) return { ok: false, reason: 'CRC mismatch' };
    var flags = data.charCodeAt(1) & 0xFF, sq = data.charCodeAt(2) & 0xFF, opcode = data.charCodeAt(3) & 0xFF;
    var payload = data.substr(4, bodyLen - 4);
    return { ok: true, flags: flags, seq: sq, opcode: opcode, opcodeName: Proto.OPCODE_NAMES[opcode] || ('0x' + hex2(opcode) + ' (unknown)'), payload: payload };
  }

  /* ==================================================== payload decode */
  // LOG's `text` field is type "str" in protocol_table.json (variable-
  // length, consumes the rest of the payload) - same as "raw" fields, the
  // generator deliberately skips codegen for it (see gen_protocol.py's
  // is_fixed()). Hand-decoded here the same way DeviceLink.handleLog()
  // does on the Android side: level(u8) + source(u8) + text(rest, UTF-8-
  // compatible byte-for-byte since payload is a Latin-1 byte-string).
  var LOG_LEVEL_NAMES = { 0: 'ERROR', 1: 'WARN', 2: 'INFO', 3: 'DEBUG', 4: 'SECTION', 5: 'GAP' };
  var LOG_SOURCE_NAMES = { 0: 'SYS', 1: 'NET', 2: 'RTC', 3: 'EE', 4: 'APP', 5: 'LED', 6: 'TV', 7: 'MOTION',
    8: 'LUX', 9: 'DIF', 10: 'UDPRAW', 11: 'AMBIENT', 12: 'BME', 13: 'TASK', 14: 'HB', 15: 'TEST' };

  // Best-effort decode using the generated fixed-layout unpackXxx()
  // functions, falling back to raw hex for "raw"/variable opcodes - exact
  // same dispatch rmk_testmode.py's describe_payload() uses.
  function decodePayload(opcode, payload) {
    var name = Proto.OPCODE_NAMES[opcode];
    if (name == null) return { text: toHex(payload) || '(empty)', fields: null };
    if (opcode === Proto.Opcode.ACK) {
      if (!payload.length) return { text: '(empty)', fields: null };
      var result = payload.charCodeAt(0) & 0xFF;
      var extra = '';
      if (result === Proto.AckResult.VERSION_MISMATCH && payload.length >= 2) extra = ' max_supported_version=' + (payload.charCodeAt(1) & 0xFF);
      return { text: 'result=' + (Proto.ACK_RESULT_NAMES[result] || '?') + '(' + result + ')' + extra, fields: { result: result }, resultCode: result };
    }
    if (opcode === Proto.Opcode.LOG) {
      if (payload.length < 2) return { text: '(malformed - too short)', fields: null };
      var level = payload.charCodeAt(0) & 0xFF, source = payload.charCodeAt(1) & 0xFF;
      var text = payload.substr(2);
      var fields = { level: LOG_LEVEL_NAMES[level] || level, source: LOG_SOURCE_NAMES[source] || source, text: text };
      return { text: '[' + fields.level + '] [' + fields.source + '] ' + text, fields: fields };
    }
    var fn = Proto['unpack' + pascal(name)];
    if (!fn) return { text: payload.length ? toHex(payload) : '(no payload / raw codec - see protocol_table.json)', fields: null };
    try {
      var fields = fn(payload);
      var parts = [];
      for (var k in fields) if (fields.hasOwnProperty(k)) parts.push(k + '=' + fieldToStr(fields[k]));
      return { text: '{' + parts.join(', ') + '}', fields: fields };
    } catch (e) {
      return { text: 'decode error (' + e.message + ') raw=' + toHex(payload), fields: null };
    }
  }
  function fieldToStr(v) {
    if (typeof v === 'string') return v.length <= 16 && !/[^\x20-\x7e]/.test(v) ? "'" + v + "'" : '<' + toHex(v) + '>';
    return String(v);
  }

  function describeFrame(parsed) {
    if (!parsed.ok) return 'MALFORMED (' + parsed.reason + ')';
    var tag = (parsed.flags & Proto.Flag.IS_ACK) ? 'ACK' : ((parsed.flags & Proto.Flag.IS_TELEMETRY) ? 'TELEM' : 'CMD');
    var d = decodePayload(parsed.opcode, parsed.payload);
    return '[' + tag + '] ' + parsed.opcodeName + ' seq=' + parsed.seq + '  ' + d.text;
  }

  /* ---------------------------------------------------------------- send */
  // The one entry point every UI action goes through. spec.opcode is an
  // OPCODE NAME (string, e.g. 'LED_SET_COLOR'); spec.build(vals) returns a
  // byte-string payload (or '' for no-payload opcodes). Callback shape kept
  // identical to the ASCII-era engine - {transport:'udp', ackKind,
  // entries:[{raw,desc}]} or {transport:'console', ackKind, text} - so
  // ui.js/result-view.js's wiring barely needed to change.
  function send(deviceKey, spec, payload, sentVals, cb) {
    var device = devices[deviceKey];
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';

    if (spec.transport === 'console') {
      logAppend(tag, 'send', 'TCP > ' + device.ip + ' ' + payload);
      Net.sendTcp(device.ip, device.telnetPort, payload, (spec.timeout_ms || device.timeoutMs) + 300, function (result) {
        if (result.status === 'OK') {
          var text = result.replies[0] || '';
          logAppend(tag, 'recv', 'TCP < ' + (text || '(empty)'));
          cb({ transport: 'console', ackKind: text ? 'ok' : 'timeout', text: text });
        } else {
          logAppend(tag, 'err', 'TCP < ' + result.status + (result.message ? (': ' + result.message) : ''));
          cb({ transport: 'console', ackKind: result.status === 'TIMEOUT' ? 'timeout' : 'error', text: result.message || '' });
        }
      });
      return;
    }

    var opcode = Proto.Opcode[spec.opcode];
    var wantAck = spec.ack !== false;
    var s = nextSeq();
    var flags = wantAck ? Proto.Flag.ACK_REQ : 0;
    var frame = buildFrame(opcode, s, flags, payload || '');
    var timeoutMs = spec.timeout_ms || device.timeoutMs;

    logAppend(tag, 'send', 'UDP > ' + device.ip + ' [' + spec.opcode + '] seq=' + s + (payload ? ('  ' + toHex(payload)) : ''));
    Net.sendUdp(device.ip, device.udpPort, frame, timeoutMs, function (result) {
      if (result.status === 'TIMEOUT') {
        logAppend(tag, 'err', 'UDP < timeout (' + timeoutMs + 'ms)');
        cb({ transport: 'udp', ackKind: 'timeout', entries: [] });
        return;
      }
      if (result.status === 'ERROR') {
        logAppend(tag, 'err', 'UDP < ERROR: ' + result.message);
        cb({ transport: 'udp', ackKind: 'error', entries: [] });
        return;
      }
      var replies = result.replies;
      var entries = [], ackKind = wantAck ? 'unknown' : 'sent';
      for (var i = 0; i < replies.length; i++) {
        var parsed = parseFrame(replies[i]);
        logAppend(tag, 'recv', 'UDP < ' + describeFrame(parsed));
        if (!parsed.ok) continue;
        if ((parsed.flags & Proto.Flag.IS_ACK) && parsed.seq === s) {
          var d = decodePayload(parsed.opcode, parsed.payload);
          ackKind = d.resultCode != null ? (Proto.ACK_RESULT_NAMES[d.resultCode] || 'unknown').toLowerCase() : 'ok';
          continue; // the ack itself isn't a result entry, same as the ASCII-era engine
        }
        entries.push({ raw: replies[i], parsed: parsed, desc: describeFrame(parsed) });
      }
      cb({ transport: 'udp', ackKind: ackKind, entries: entries });
    });
  }

  return {
    devices: devices, nextSeq: nextSeq,
    logLines: logLines, subscribeLog: subscribeLog, logAppend: logAppend, logSys: logSys, saveLog: saveLog,
    startKeepAlive: startKeepAlive, stopKeepAlive: stopKeepAlive,
    buildFrame: buildFrame, parseFrame: parseFrame, decodePayload: decodePayload, describeFrame: describeFrame,
    toHex: toHex, pascal: pascal, fmtDurationMin: fmtDurationMin, send: send
  };
})();
