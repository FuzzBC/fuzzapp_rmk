/* result-view.js - "what came back" renderer for TestMode.hta (00_PLAN.md
 * Phase 6 follow-up). Rewritten against decoded binary TELEM_* frames
 * instead of ASCII reply strings - entries[i] is now
 * {raw, parsed:{opcode,opcodeName,flags,seq,payload}, desc}, decoded via
 * Engine.decodePayload()/Proto.unpackXxx(). Category colours are plain CSS
 * classes (mshta's Trident engine has zero support for CSS custom
 * properties, so a var() reference set from script would silently fail).
 * Call ResultView.render(container, spec, sendResult, sentVals).
 */

var ResultView = (function () {
  'use strict';

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }

  var ACK_INFO = {
    ok: ['✓', 'OK', 'Command accepted.'],
    clamped: ['△', 'CLAMPED', 'Accepted, but the value was out of range and got clamped.'],
    rejected: ['✕', 'REJECTED', 'Command was rejected - nothing changed.'],
    blocked: ['⛔', 'BLOCKED', "Blocked by the device's current state."],
    locked: ['⚿', 'LOCKED', 'That setting is locked.'],
    unreachable: ['⚠', 'UNREACHABLE', 'Target device (e.g. relayed Diffuser) did not answer.'],
    unsupported: ['–', 'UNSUPPORTED', 'Not supported by this firmware build.'],
    unauthorized: ['⚿', 'UNAUTHORIZED', 'Not authorized.'],
    version_mismatch: ['≠', 'VERSION MISMATCH', 'Protocol version not supported by this firmware.'],
    timeout: ['…', 'TIMEOUT', 'No reply within the timeout window.'],
    error: ['✕', 'ERROR', 'The send itself failed.'],
    sent: ['➤', 'SENT', 'Sent - this opcode has ack=NONE, no ack envelope expected.'],
    unknown: ['?', 'NO ACK', 'No matching ACK frame came back.']
  };

  function ackBanner(container, kind) {
    var info = ACK_INFO[kind] || ACK_INFO.unknown;
    var banner = el('div', 'res-ack'); banner.setAttribute('data-kind', kind);
    banner.appendChild(el('span', 'res-ack-icon', info[0]));
    var col = el('div', 'res-ack-col');
    col.appendChild(el('div', 'res-ack-head', info[1]));
    if (info[2]) col.appendChild(el('div', 'res-ack-meaning', info[2]));
    banner.appendChild(col);
    container.appendChild(banner);
    return kind;
  }

  function fieldGrid(container, fields, rawText) {
    var grid = el('div', 'res-grid');
    for (var k in fields) {
      if (!fields.hasOwnProperty(k)) continue;
      var cell = el('div', 'res-cell');
      cell.appendChild(el('div', 'res-cell-label', k));
      cell.appendChild(el('div', 'res-cell-value', String(fields[k])));
      grid.appendChild(cell);
    }
    container.appendChild(grid);
    container.appendChild(el('div', 'res-raw', 'raw: ' + Engine.toHex(rawText)));
  }

  function swatches(container, pairs) {
    var row = el('div', 'res-swatches');
    pairs.forEach(function (pair) {
      var item = el('div', 'res-swatch');
      var box = el('div', 'res-swatch-box'); box.style.background = '#' + (pair[1] || 'FFFFFF');
      item.appendChild(box);
      item.appendChild(el('div', 'res-swatch-cap', pair[0] + '  #' + (pair[1] || 'FFFFFF')));
      row.appendChild(item);
    });
    container.appendChild(row);
  }

  var _eeByIdx = null;
  function eeByIdx() {
    if (!_eeByIdx) { _eeByIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { _eeByIdx[row[0]] = row; }); }
    return _eeByIdx;
  }
  function formatSettingTag(idx, val) {
    var row = eeByIdx()[idx];
    if (!row) return null;
    var kind = row[2], label = row[6], options = row[7];
    if (kind === 'switch') return val ? 'ON' : 'OFF';
    if (kind === 'select') return (options && options[val] != null) ? options[val] : null;
    var m = /\(([a-zA-Z]+)\)\s*$/.exec(label || '');
    return m ? (val + ' ' + m[1]) : null;
  }

  var CAT_CLASS = { TV: 'cat-tv', MOTION: 'cat-motion', OTHER: 'cat-other', HB: 'cat-hb',
    DIFFUSER: 'cat-dif', AMBILIGHT: 'cat-udpraw' };

  // TELEM_SETTINGS_FULL.values is a 50-byte string, array index = setting id.
  function settingsTable(container, valuesByteStr) {
    var wrap = el('div', 'res-table');
    wrap.appendChild(el('div', 'res-table-head', 'SMARTTV EEPROM SETTINGS · ' + EE_SETTINGS_TABLE.length + ' RECEIVED'));
    var byCat = {}, order = [];
    EE_SETTINGS_TABLE.forEach(function (row) {
      var idx = row[0], cat = row[1], val = valuesByteStr.charCodeAt(idx) & 0xFF;
      if (!byCat[cat]) { byCat[cat] = []; order.push(cat); }
      byCat[cat].push([idx, row[6], val]);
    });
    order.forEach(function (cat) {
      var catClass = CAT_CLASS[cat] || 'cat-other';
      wrap.appendChild(el('div', 'res-table-cat ' + catClass, cat));
      byCat[cat].forEach(function (r) {
        var row = el('div', 'res-table-row ' + catClass);
        row.appendChild(el('div', 'res-table-name', hexb(r[0], 2) + '  ' + r[1]));
        var tag = formatSettingTag(r[0], r[2]);
        row.appendChild(el('div', 'res-table-val', tag ? (r[2] + '  [' + tag + ']') : String(r[2])));
        wrap.appendChild(row);
      });
    });
    container.appendChild(wrap);
  }

  // TELEM_COLOR_SYNC's "raw" payload: repeated FILL(0x01,start,count,r,g,b)
  // or SETN(0x02,count,{idx,r,g,b}xN) records - see 01_PROTOCOL.md SS3.
  function parseColorSync(payload) {
    var pos = 0, fills = 0, pixels = 0, records = [];
    function b(i) { return payload.charCodeAt(i) & 0xFF; }
    while (pos < payload.length) {
      var op = b(pos);
      if (op === 0x01) {
        if (pos + 6 > payload.length) { records.push({ kind: 'error', text: 'truncated FILL' }); break; }
        var start = b(pos + 1), count = b(pos + 2), rr = b(pos + 3), gg = b(pos + 4), bb = b(pos + 5);
        fills++; pixels += count;
        records.push({ kind: 'fill', start: start, end: start + count - 1, count: count, hex: hexb(rr, 2) + hexb(gg, 2) + hexb(bb, 2) });
        pos += 6;
      } else if (op === 0x02) {
        if (pos + 2 > payload.length) { records.push({ kind: 'error', text: 'truncated SETN' }); break; }
        var cnt = b(pos + 1), end = pos + 2 + cnt * 4;
        if (end > payload.length) { records.push({ kind: 'error', text: 'truncated SETN (' + cnt + ' LEDs)' }); break; }
        pixels += cnt;
        records.push({ kind: 'set', count: cnt });
        pos = end;
      } else {
        records.push({ kind: 'error', text: 'unknown op 0x' + hexb(op, 2) });
        break;
      }
    }
    return { pixels: pixels, fills: fills, records: records };
  }

  function colorSyncView(container, payload) {
    if (!payload.length) { container.appendChild(el('div', 'res-line', 'colour sync (empty packet)')); return; }
    var p = parseColorSync(payload);
    container.appendChild(el('div', 'res-list-head', p.pixels + ' LED' + (p.pixels === 1 ? '' : 's') + ' · ' + p.fills + ' fill record' + (p.fills === 1 ? '' : 's')));
    var row = el('div', 'res-swatches');
    p.records.forEach(function (r) {
      var item = el('div', 'res-swatch');
      if (r.kind === 'fill') {
        var box = el('div', 'res-swatch-box'); box.style.background = '#' + r.hex;
        item.appendChild(box);
        item.appendChild(el('div', 'res-swatch-cap', 'LED ' + r.start + '–' + r.end + '  #' + r.hex));
      } else if (r.kind === 'set') {
        item.appendChild(el('div', 'chip-status', r.count + ' LED' + (r.count === 1 ? '' : 's') + ' set individually'));
      } else {
        var chip = el('div', 'chip-status', r.text); chip.style.color = '#ff6b6b';
        item.appendChild(chip);
      }
      row.appendChild(item);
    });
    container.appendChild(row);
  }

  // TELEM_DIFFUSER_HISTORY's "raw" payload: count(u8) + up to 10x minutes(u16).
  function parseDiffuserHistory(payload) {
    if (!payload.length) return [];
    var count = payload.charCodeAt(0) & 0xFF, minutes = [], pos = 1;
    while (pos + 2 <= payload.length && minutes.length < count) {
      minutes.push(((payload.charCodeAt(pos) & 0xFF) << 8) | (payload.charCodeAt(pos + 1) & 0xFF));
      pos += 2;
    }
    return minutes;
  }

  function diffuserHistoryView(container, payload) {
    var minutes = parseDiffuserHistory(payload);
    if (!minutes.length) { container.appendChild(el('div', 'result-empty', 'No refill cycles recorded yet.')); return; }
    var wrap = el('div', 'res-list');
    wrap.appendChild(el('div', 'res-list-head', minutes.length + ' of 10 cycles'));
    var box = el('div', 'res-list-box');
    minutes.forEach(function (m, i) { box.appendChild(el('div', 'res-list-line', '#' + (i + 1) + '  ' + Engine.fmtDurationMin(m))); });
    wrap.appendChild(box);
    container.appendChild(wrap);
  }

  function diffuserStatusView(container, fields) {
    var modeName = DIF_MODE_NAMES[fields.mode] || ('M' + fields.mode);
    var stripName = DIF_STRIP_NAMES[fields.strip] || ('#' + fields.strip);
    var grid = el('div', 'res-grid');
    [['mode', 'M' + fields.mode + ' (' + modeName + ')'],
     ['strip', stripName],
     ['parfum', fields.parfum_min ? fields.parfum_min + ' min' : 'off'],
     ['usage', Engine.fmtDurationMin(fields.usage_min)],
     ['avg cycle', Engine.fmtDurationMin(fields.avg_min)],
     ['refills', fields.refill_count + '/10 (lifetime ' + fields.lifetime_refills + ')']].forEach(function (c) {
      var cell = el('div', 'res-cell');
      cell.appendChild(el('div', 'res-cell-label', c[0]));
      cell.appendChild(el('div', 'res-cell-value', c[1]));
      grid.appendChild(cell);
    });
    container.appendChild(grid);
  }

  // Raw Telnet console text (e.g. the connect banner - see Telnet.cpp's
  // dbgPrintf() calls) isn't a structured protocol reply, just free-form
  // lines - this gives it basic terminal-like structure instead of one
  // flat uncolored blob: box-drawing rule lines (----...) get a section-
  // header look, "%-10s: %s"-style "label     : value" lines (IP/MAC/
  // Signal/Uptime/Mode/Strip in the banner, "COMMANDS" entries) get their
  // label/value tinted separately, everything else prints as plain text.
  function consoleView(container, text) {
    var box = el('div', 'res-console-box');
    var lines = String(text || '(empty)').split('\n');
    lines.forEach(function (line) {
      if (/─{3,}/.test(line)) {
        box.appendChild(el('div', 'res-console-rule', line));
        return;
      }
      // "%-10s: %s"-style banner lines only (IP/MAC/Signal/Uptime/Mode/
      // Strip): a single leading token, then the FIRST colon. Deliberately
      // NOT a general "anything : anything" match - cmdHelp()'s command
      // list has lines like "P<min>     Parfum mode: P30 = insist..." where
      // the colon is mid-sentence, not a label separator; requiring the
      // part before it to be one token (no spaces) keeps those as plain text.
      var m = /^(\s*\S+)\s*:\s(.*)$/.exec(line);
      if (m) {
        var row = el('div', 'res-console-line');
        row.appendChild(el('span', 'res-console-key', m[1] + ' : '));
        row.appendChild(el('span', 'res-console-val', m[2]));
        box.appendChild(row);
        return;
      }
      box.appendChild(el('div', 'res-console-line', line));
    });
    container.appendChild(box);
  }

  // Curated "OPCODE.field" -> real on/off switch, not just a 0/1-valued
  // enum or count that happens to overlap. There's no bool type in
  // protocol_table.json (everything's u8/u16/i8/bytes/raw) to drive this
  // from schema, so this list is hand-picked from the fields actually
  // known to be plain switches - same reasoning as EE_SETTINGS_TABLE's
  // kind:'switch' rows (data.js) for settings. Deliberately short: an
  // enum like TELEM_STATUS.motion or TELEM_STATUS.ambient reads 0-3, and
  // mislabelling one of those "OFF" would be actively wrong, not just
  // unstyled.
  var ONOFF_FIELDS = {
    'TELEM_ENABLE.value': 1, 'TELEM_STATUS.tv': 1, 'TELEM_STATUS.udpraw': 1
  };

  function formatFieldValue(opcodeName, key, val) {
    if (ONOFF_FIELDS[opcodeName + '.' + key] && (val === 0 || val === 1)) {
      return { text: val ? 'ON' : 'OFF', cls: val ? 'res-entry-on' : 'res-entry-off' };
    }
    if (typeof val === 'string') {
      // Long or non-printable byte strings (TELEM_SETTINGS_FULL's 50-byte
      // blob, TELEM_COLOR_SYNC's packed frame, TELEM_DEVICE_ID's NUL-
      // padded id, ...) get a size summary instead of a wall of hex - the
      // opcode-specific views (settingsTable/colorSyncView) already exist
      // for actually reading those; this generic card just needs to not
      // be unreadable when one shows up inside a mixed burst.
      if (val.length > 16 || /[^\x20-\x7e]/.test(val)) return { text: val.length + ' bytes', cls: 'res-entry-val' };
      return { text: "'" + val + "'", cls: 'res-entry-val' };
    }
    return { text: String(val), cls: 'res-entry-val' };
  }

  function entryCard(entry) {
    var parsed = entry.parsed;
    // Malformed frames (bad CRC, truncated, ...) have parsed.ok === false
    // and none of the decoded fields (opcode/opcodeName/payload) - see
    // Engine.parseFrame()/describeFrame(). Must not reach decodePayload()
    // with those missing, or toHex(undefined) throws. entry.desc already
    // holds describeFrame()'s safe "MALFORMED (...)" text for this case.
    var ok = parsed && parsed.ok;
    var opcodeName = ok ? parsed.opcodeName : null;
    var d = ok ? Engine.decodePayload(parsed.opcode, parsed.payload) : null;
    var kind = opcodeName === 'ACK' ? 'ack' : (opcodeName === 'LOG' ? 'log' : 'telem');

    // A LOG mixed in among other opcodes (e.g. DIAG_HEALTH's 8 LOG lines +
    // 1 trailing TELEM_LINK, which fails isAllLog()'s "every entry is LOG"
    // check) still gets the same colored dbg-row treatment as the
    // dedicated all-LOG view, not this function's generic card - its own
    // level tag already carries the "what is this" context a card header
    // would just repeat.
    if (kind === 'log' && d && d.fields) return logEntryRow(d.fields);

    var isFailedAck = kind === 'ack' && d && d.resultCode != null && d.resultCode !== Proto.AckResult.OK;

    var card = el('div', 'res-entry cat-' + kind + (isFailedAck ? ' res-entry-fail' : ''));
    var head = el('div', 'res-entry-head');
    head.appendChild(el('span', 'res-entry-name', ok ? opcodeName : 'MALFORMED'));
    if (ok) head.appendChild(el('span', 'res-entry-seq', 'seq ' + parsed.seq));
    card.appendChild(head);

    if (kind === 'ack' && d) {
      var resultName = Proto.ACK_RESULT_NAMES[d.resultCode] || ('code ' + d.resultCode);
      var chip = el('div', 'res-entry-chip ' + (isFailedAck ? 'res-entry-off' : 'res-entry-on'), resultName);
      card.appendChild(chip);
    } else if (d && d.fields && typeof d.fields === 'object') {
      // Same box style as diffuserStatusView()'s "Check status" cells
      // (.res-grid/.res-cell: bordered box, small caps label on top, bold
      // value below) instead of a bespoke inline "key: value" row layout -
      // one consistent look for every field grid in the app.
      var grid = el('div', 'res-grid');
      for (var k in d.fields) {
        if (!d.fields.hasOwnProperty(k)) continue;
        var fv = formatFieldValue(opcodeName, k, d.fields[k]);
        var cell = el('div', 'res-cell');
        cell.appendChild(el('div', 'res-cell-label', k));
        cell.appendChild(el('div', 'res-cell-value ' + fv.cls, fv.text));
        grid.appendChild(cell);
      }
      card.appendChild(grid);
    } else {
      card.appendChild(el('div', 'res-entry-raw', (d && d.text) || entry.desc || entry.raw || '(unrecognised packet)'));
    }
    return card;
  }

  function entryCards(container, entries) {
    var wrap = el('div', 'res-entries');
    wrap.appendChild(el('div', 'res-list-head', entries.length + ' PACKET' + (entries.length === 1 ? '' : 'S') + ' RECEIVED'));
    entries.forEach(function (e) { wrap.appendChild(entryCard(e)); });
    container.appendChild(wrap);
  }

  // LOG_LEVEL_NAMES (Engine.decodePayload's `fields.level`) -> .dbg-* CSS
  // class/short tag - mirrors the firmware's APP_LogLevel enum (Globals.h):
  // ERROR/WARN/INFO/DEBUG are per-line rows, SECTION is a plain header line
  // (no level tag), GAP is just a blank spacer - matches app.css's .dbg-*
  // rules, which existed but were never wired to anything until now (a
  // multi-line LOG dump - e.g. DIAG_HEALTH - used to fall through to the
  // plain, uncolored entryCards() below instead).
  var DBG_LEVEL_INFO = {
    ERROR: ['dbg-error', 'ERR'], WARN: ['dbg-warn', 'WRN'],
    INFO: ['dbg-info', 'INF'], DEBUG: ['dbg-debug', 'DBG']
  };
  function isAllLog(entries) {
    for (var i = 0; i < entries.length; i++) {
      if (!entries[i].parsed || entries[i].parsed.opcodeName !== 'LOG') return false;
    }
    return entries.length > 0;
  }
  // One LOG entry's decoded {level,source,text} -> its dbg-row/-section/-gap
  // element. Shared by logDumpView() (the all-LOG fast path) and
  // entryCard() (a LOG mixed in with other opcodes, e.g. DIAG_HEALTH's 8
  // LOG lines + 1 trailing TELEM_LINK - isAllLog() correctly says "no",
  // but a LOG entry deserves this treatment either way, not the generic
  // field-grid a TELEM_* opcode gets).
  function logEntryRow(f) {
    if (f.level === 'SECTION') return el('div', 'dbg-section', f.text);
    if (f.level === 'GAP') return el('div', 'dbg-gap');
    var info = DBG_LEVEL_INFO[f.level] || DBG_LEVEL_INFO.INFO;
    var row = el('div', 'dbg-row ' + info[0]);
    row.appendChild(el('div', 'dbg-level', info[1]));
    row.appendChild(el('div', 'dbg-text', f.text));
    return row;
  }
  function logDumpView(container, entries) {
    var wrap = el('div', 'dbg-list');
    entries.forEach(function (e) {
      var d = Engine.decodePayload(e.parsed.opcode, e.parsed.payload);
      if (!d.fields) { wrap.appendChild(el('div', 'res-list-line', e.desc || '(malformed LOG)')); return; }
      wrap.appendChild(logEntryRow(d.fields));
    });
    container.appendChild(wrap);
  }

  // sendResult: whatever Engine.send()'s callback received.
  function render(container, spec, sendResult, sentVals) {
    clear(container);

    if (sendResult.transport === 'console') {
      ackBanner(container, sendResult.ackKind);
      consoleView(container, sendResult.text);
      return;
    }

    var ackKind = sendResult.ackKind;
    ackBanner(container, ackKind);
    var entries = sendResult.entries || [];
    var render_ = spec.render || {};

    if (entries.length > 1) {
      if (isAllLog(entries)) logDumpView(container, entries);
      else entryCards(container, entries);
    }

    var first = entries.length ? entries[0] : null;
    var payload = first ? first.parsed.payload : '';

    if (render_.type === 'color_params') {
      var keys = (render_.always || []).slice();
      if (render_.conditional && sentVals[render_.conditional[0]]) keys.push(render_.conditional[1]);
      swatches(container, keys.filter(function (k) { return sentVals[k] != null; }).map(function (k) { return [k, sentVals[k]]; }));
      return;
    }
    if (render_.type === 'settings_table' && first) {
      var d = Proto.unpackTelemSettingsFull(payload);
      settingsTable(container, d.values); return;
    }
    if (render_.type === 'color_sync' && first) { colorSyncView(container, payload); return; }
    if (render_.type === 'diffuser_status' && first) {
      var s = Proto.unpackTelemDiffuserStatus(payload);
      diffuserStatusView(container, s); return;
    }
    if (render_.type === 'diffuser_history' && first) { diffuserHistoryView(container, payload); return; }

    if (entries.length > 1) return;
    if (first) { container.appendChild(entryCard(first)); return; }
    container.appendChild(el('div', 'res-line', '-'));
  }

  return { render: render, ackBanner: ackBanner, ACK_INFO: ACK_INFO, parseColorSync: parseColorSync, parseDiffuserHistory: parseDiffuserHistory };
})();
