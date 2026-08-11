/* result-view.js - generic "what came back" renderer shared by the round-2
 * mockups. Same ack banner / status grid / colour swatches / settings
 * table / packet list logic as round 1, emitting theme-neutral classes
 * (.res-*) instead of inline styling, so each mockup's CSS controls the
 * actual look. Call ResultView.render(container, spec, sendResult, sentVals).
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
    nowater: ['⚠', 'NO WATER', 'Diffuser reservoir is empty.'],
    unsupported: ['–', 'UNSUPPORTED', 'Not supported by this firmware build.'],
    timeout: ['…', 'TIMEOUT', 'No reply within the timeout window.'],
    error: ['✕', 'ERROR', 'The send itself failed.'],
    sent: ['➤', 'SENT', 'Sent - this command has no ack envelope.'],
    unknown: ['?', 'NO ACK', 'No matching ack packet came back.'],
  };

  function ackBanner(container, kind) {
    var info = ACK_INFO[kind];
    if (!info) return;
    var banner = el('div', 'res-ack'); banner.setAttribute('data-kind', kind);
    banner.appendChild(el('span', 'res-ack-icon', info[0]));
    var col = el('div', 'res-ack-col');
    col.appendChild(el('div', 'res-ack-head', info[1]));
    if (info[2]) col.appendChild(el('div', 'res-ack-meaning', info[2]));
    banner.appendChild(col);
    container.appendChild(banner);
    return kind;
  }

  function statusGrid(container, fields, rawText) {
    var grid = el('div', 'res-grid');
    [['mode', 'M' + fields.mode + ' (' + fields.modeName + ')', 'mode'],
     ['strip', String(fields.strip), 'strip'],
     ['parfum', fields.parfumMin ? fields.parfumMin + ' min' : 'off', 'timer'],
     ['usage', fields.usageMin + ' min', 'timer'],
     ['avg cycle', fields.avgMin + ' min', 'timer'],
     ['refills', fields.refillCount + '/10 (total ' + fields.totalRefills + ')', 'count']].forEach(function (c) {
      var cell = el('div', 'res-cell'); cell.setAttribute('data-kind', c[2]);
      cell.appendChild(el('div', 'res-cell-label', c[0]));
      cell.appendChild(el('div', 'res-cell-value', c[1]));
      grid.appendChild(cell);
    });
    container.appendChild(grid);
    container.appendChild(el('div', 'res-raw', 'raw: ' + rawText));
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

  // idx -> EE_SETTINGS_TABLE row (kind/label/options), built once and
  // cached - lets a raw "idx=val" pair be shown as what it actually means
  // (ON/OFF, the effect's name, a value with its real unit) instead of a
  // bare number, same as the Android app's settings screen.
  var _eeByIdx = null;
  function eeByIdx() {
    if (!_eeByIdx) { _eeByIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { _eeByIdx[row[0]] = row; }); }
    return _eeByIdx;
  }
  // Returns a short bracketed tag for one setting's value - 'ON'/'OFF' for
  // a switch, the option's name for a select, 'N ms'/'N s'/... for a bar
  // whose label ends in a "(unit)" hint - or null when there's nothing
  // more useful to say than the number itself (unitless bar, or an idx
  // outside EE_SETTINGS_TABLE, e.g. the 45-49 reserved slots).
  function formatSettingTag(idx, val) {
    var row = eeByIdx()[idx];
    if (!row) return null;
    var kind = row[2], label = row[6], options = row[7];
    if (kind === 'switch') return val ? 'ON' : 'OFF';
    if (kind === 'select') return (options && options[val] != null) ? options[val] : null;
    var m = /\(([a-zA-Z]+)\)\s*$/.exec(label || '');
    return m ? (val + ' ' + m[1]) : null;
  }

  // Setting-name category -> the CSS variable each theme defines a colour
  // for (same grouping/keys as the Tk app's SETTINGS_CAT_COLORS), so at a
  // glance you can tell a TV setting from a MOTION one from a DIF one
  // instead of every row reading in the same flat colour. Set as an
  // inline style (not just a class) so it applies even if a theme's own
  // stylesheet doesn't happen to style the .cat-* class itself - only the
  // --cat-* variable needs to exist.
  var CAT_CLASS = { TV: 'cat-tv', MOTION: 'cat-motion', OTHER: 'cat-other', HB: 'cat-hb',
    DIF: 'cat-dif', UDPRAW: 'cat-udpraw', RESERVED: 'cat-reserved' };

  function settingsTable(container, rows) {
    var wrap = el('div', 'res-table');
    wrap.appendChild(el('div', 'res-table-head', 'SMARTTV EEPROM SETTINGS · ' + rows.length + ' / ' + TV_SETTINGS.length + ' RECEIVED'));
    var byCat = {}, order = [];
    rows.forEach(function (r) {
      var name = r[1];
      var cat = name.indexOf('_') !== -1 ? name.split('_')[0] : name.split(' ')[0];
      if (!byCat[cat]) { byCat[cat] = []; order.push(cat); }
      byCat[cat].push(r);
    });
    order.forEach(function (cat) {
      var catClass = CAT_CLASS[cat] || 'cat-other';
      var colorVar = 'var(--' + catClass + ')';
      var catHead = el('div', 'res-table-cat ' + catClass, cat);
      catHead.style.color = colorVar;
      wrap.appendChild(catHead);
      byCat[cat].forEach(function (r) {
        var row = el('div', 'res-table-row ' + catClass);
        row.style.borderLeftColor = colorVar;
        var nameEl = el('div', 'res-table-name', hexb(r[0], 2) + '  ' + r[1]);
        nameEl.style.color = colorVar;
        row.appendChild(nameEl);
        var tag = formatSettingTag(r[0], r[2]);
        row.appendChild(el('div', 'res-table-val', tag ? (r[2] + '  [' + tag + ']') : String(r[2])));
        wrap.appendChild(row);
      });
    });
    container.appendChild(wrap);
  }

  function replyList(container, entries) {
    var wrap = el('div', 'res-list');
    wrap.appendChild(el('div', 'res-list-head', entries.length + ' PACKET' + (entries.length === 1 ? '' : 'S') + ' RECEIVED'));
    var box = el('div', 'res-list-box');
    entries.forEach(function (e) { box.appendChild(el('div', 'res-list-line', e.desc || Engine.describeReply(e.raw) || e.raw || '(unrecognised packet)')); });
    wrap.appendChild(box);
    container.appendChild(wrap);
  }

  // sendResult: whatever Engine.send()'s callback received.
  function render(container, spec, sendResult, sentVals) {
    clear(container);

    if (sendResult.transport === 'console') {
      ackBanner(container, sendResult.ackKind);
      var box = el('div', 'res-line', sendResult.text || '(empty)');
      box.style.whiteSpace = 'pre-wrap';
      container.appendChild(box);
      return;
    }

    var ackKind = sendResult.ackKind;
    ackBanner(container, ackKind);
    var entries = sendResult.entries || [];
    if (entries.length > 1) replyList(container, entries);

    var render_ = spec.render || {};
    var rawText = entries.length ? entries[0].raw : '';

    if (render_.type === 'status' && rawText) {
      var fields = Engine.parseStatusFields(rawText);
      if (fields) { statusGrid(container, fields, rawText); return; }
    }
    if (render_.type === 'color_params') {
      var keys = (render_.always || []).slice();
      if (render_.conditional && sentVals[render_.conditional[0]]) keys.push(render_.conditional[1]);
      swatches(container, keys.filter(function (k) { return sentVals[k] != null; }).map(function (k) { return [k, sentVals[k]]; }));
      return;
    }
    if (render_.type === 'color_reply_dual' && rawText) {
      var hexval = rawText.replace(/[^0-9A-Fa-f]/g, '').toUpperCase();
      if (hexval.length >= 12) { swatches(container, [['colour 1', hexval.slice(0, 6)], ['colour 2', hexval.slice(6, 12)]]); return; }
    }
    if (render_.type === 'settings_table' && rawText) {
      var rows = Engine.parseSettingsReply(rawText);
      if (rows) { settingsTable(container, rows); return; }
    }

    if (entries.length > 1) return;
    if (entries.length === 1) { container.appendChild(el('div', 'res-line', entries[0].desc || Engine.describeReply(entries[0].raw) || entries[0].raw)); return; }
    container.appendChild(el('div', 'res-line', rawText || '-'));
  }

  return { render: render, ackBanner: ackBanner, ACK_INFO: ACK_INFO };
})();
