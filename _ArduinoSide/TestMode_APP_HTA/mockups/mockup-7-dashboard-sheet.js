/* mockup-7-dashboard-sheet.js - smart-home dashboard UX.
 * Hero card = live device status + primary actions. Categories are tiles;
 * tapping one slides up a bottom sheet listing that category's commands,
 * each expanding inline (accordion) with its controls/send/result.
 * Activity log lives in a separate right-side slide-over, not inline.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

  var state = { activeDevice: 'diffuser', keepAliveOn: false, unread: 0 };
  var activityLines = [];

  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('sheet-close').addEventListener('click', closeSheet);
    $('sheet-scrim').addEventListener('click', closeSheet);
    $('activity-fab').addEventListener('click', openActivity);
    $('activity-close').addEventListener('click', closeActivity);
    $('activity-savelog').addEventListener('click', function () { Engine.saveLog(); });

    Engine.subscribeLog(function (line) {
      activityLines.push(line);
      if ($('activity-panel').classList.contains('open')) renderActivityLog();
      else { state.unread++; updateBadge(); }
    });

    switchDevice('diffuser');
    Engine.logSys('TestMode mockup started — simulated device, no real network I/O.');
  }

  function updateBadge() {
    var b = $('activity-badge');
    if (state.unread > 0) { b.textContent = String(state.unread); b.classList.remove('hidden'); } else b.classList.add('hidden');
  }

  function switchDevice(key) {
    state.activeDevice = key;
    $('dev-diffuser').classList.toggle('on', key === 'diffuser');
    $('dev-smarttv').classList.toggle('on', key === 'smarttv');
    $('conn-ip').textContent = Engine.devices[key].ip;
    renderHero();
    renderQuickTestRow();
    renderTileGrid();
    closeSheet();
  }

  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    $('keepalive-btn').classList.toggle('on', state.keepAliveOn);
    $('keepalive-btn').textContent = 'Keep-alive: ' + (state.keepAliveOn ? 'on' : 'off');
    if (state.keepAliveOn) Engine.startKeepAlive(function () { return state.activeDevice; });
    else Engine.stopKeepAlive();
  }

  /* ------------------------------------------------------------- hero */
  function renderHero() {
    var host = $('hero'); clear(host);
    var top = el('div', 'hero-top');
    var left = el('div', '');
    left.appendChild(el('div', 'hero-label', state.activeDevice.toUpperCase()));
    var stats = el('div', 'hero-stats');
    var actions = el('div', 'hero-actions');

    if (state.activeDevice === 'diffuser') {
      var d = Engine.DIF;
      left.appendChild(el('div', 'hero-title', 'M' + d.mode + ' · ' + (DIF_MODE_NAMES[d.mode] || '?')));
      left.appendChild(el('div', 'hero-sub', d.parfumMin ? ('Parfum running — ' + d.parfumMin + ' min left') : 'Parfum idle'));
      [['Usage', d.usageMin + 'm'], ['Avg cycle', d.avgMin + 'm'], ['Refills', d.refillCount + '/10'], ['Total', d.totalRefills]].forEach(function (s) {
        var st = el('div', 'hero-stat'); st.appendChild(el('div', 'lab', s[0])); st.appendChild(el('div', 'val', String(s[1]))); stats.appendChild(st);
      });
      addAction(actions, 'Check status', function () { fire(findSpec('Ds'), 'Ds', {}); });
      addAction(actions, 'Cancel parfum', function () { fire(findSpec('DpCancel'), 'Dp0000', {}); });
      addAction(actions, 'Shut down', function () { fire(findSpec('Df'), 'Df', {}); });
    } else {
      var t = Engine.TV;
      left.appendChild(el('div', 'hero-title', t.tvOn ? 'TV is on' : 'TV is off'));
      left.appendChild(el('div', 'hero-sub', 'Motion: ' + (MOTION_NAMES[t.motion] || t.motion) + ' · LEDs ' + (t.ledsOn ? 'on' : 'off')));
      [['Ambient', t.ambient ? 'on' : 'off'], ['UDPRAW', t.udpraw ? 'on' : 'off'], ['Lux', t.lux], ['Brightness', t.brightness]].forEach(function (s) {
        var st = el('div', 'hero-stat'); st.appendChild(el('div', 'lab', s[0])); st.appendChild(el('div', 'val', String(s[1]))); stats.appendChild(st);
      });
      addAction(actions, 'Connect (Z)', function () { fire(findSpec('Z'), 'Z', {}); });
      addAction(actions, 'Toggle LEDs', function () { fire(findSpec('X'), 'X', {}); });
    }
    top.appendChild(left);
    host.appendChild(top); host.appendChild(stats); host.appendChild(actions);
  }
  var MOTION_NAMES = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed'];
  function addAction(container, label, fn) { var b = el('button', '', label); b.addEventListener('click', fn); container.appendChild(b); }
  function findSpec(id) { return COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.id === id; })[0]; }
  function fire(spec, payload, vals) {
    if (!spec) return;
    if (spec.confirm && !window.confirm(spec.confirm)) return;
    Engine.send(state.activeDevice, spec, payload, vals, function () { renderHero(); });
  }

  /* -------------------------------------------------------- quick test row */
  function renderQuickTestRow() {
    var host = $('quicktest-row'); clear(host);
    var specs = COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.section.toLowerCase().indexOf('test mode') !== -1; });
    specs.forEach(function (spec) {
      var options = spec.direct_buttons ? spec.direct_buttons :
        (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum' ?
          spec.params[0].options.map(function (opt) { var v = {}; v[spec.params[0].key] = opt[0]; return [spec.build(v), opt[1]]; }) : null);
      if (!options) return;
      options.forEach(function (opt) {
        var b = el('button', '', spec.label.replace(/[^A-Za-z@!]/g, '') + ' ' + opt[1]);
        b.addEventListener('click', function () { fire(spec, opt[0], {}); });
        host.appendChild(b);
      });
    });
  }

  /* -------------------------------------------------------------- tiles */
  function renderTileGrid() {
    var host = $('tile-grid'); clear(host);
    var list = COMMAND_TABLES[state.activeDevice];
    var bySection = {}, order = [];
    list.forEach(function (spec) {
      if (spec.section.toLowerCase().indexOf('test mode') !== -1) return;
      if (!bySection[spec.section]) { bySection[spec.section] = []; order.push(spec.section); }
      bySection[spec.section].push(spec);
    });
    order.forEach(function (sec) {
      var meta = categoryMeta(sec);
      var tile = el('button', 'tile');
      tile.appendChild(el('div', 'icon', meta.icon));
      tile.appendChild(el('div', 'name', meta.title));
      tile.appendChild(el('div', 'count', bySection[sec].length + ' command' + (bySection[sec].length === 1 ? '' : 's')));
      tile.addEventListener('click', function () { openSheet(meta.title, bySection[sec]); });
      host.appendChild(tile);
    });
  }

  /* --------------------------------------------------------------- sheet */
  function openSheet(title, specs) {
    $('sheet-title').textContent = title;
    var body = $('sheet-body'); clear(body);
    var allItems = []; // accordion: opening one closes every other item in this sheet
    specs.forEach(function (spec) {
      var item = el('div', 'cmd-item');
      var head = el('div', 'cmd-item-head');
      head.appendChild(el('span', 'name', spec.name || spec.label));
      head.appendChild(el('span', 'label', spec.label));
      var body2 = el('div', 'cmd-item-body');
      var built = false;
      head.addEventListener('click', function () {
        var willOpen = !item.classList.contains('open');
        allItems.forEach(function (other) { if (other !== item) other.classList.remove('open'); });
        item.classList.toggle('open', willOpen);
        if (willOpen && !built) { built = true; buildCommandBody(body2, spec); }
      });
      item.appendChild(head); item.appendChild(body2);
      body.appendChild(item);
      allItems.push(item);
    });
    $('sheet').classList.add('open');
    $('sheet-scrim').classList.remove('hidden');
  }
  function closeSheet() { $('sheet').classList.remove('open'); $('sheet-scrim').classList.add('hidden'); }

  function buildCommandBody(host, spec) {
    var deviceKey = state.activeDevice;
    host.appendChild(el('div', 'cmd-item-desc', spec.desc || ''));

    if (spec.custom_panel) { buildCustomPanel(host, spec, deviceKey); return; }

    var controls = {}, previewEl;
    var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—'));

    function updatePreview() {
      if (!previewEl || spec.direct_buttons) return;
      try { previewEl.textContent = spec.build(Controls.collect(spec, controls)); } catch (e) { previewEl.textContent = '(invalid)'; }
    }

    if (spec.params && spec.params.length) controls = Controls.buildForm(host, spec, updatePreview);

    var chip = el('span', 'chip-status', '—');
    if (spec.direct_buttons) {
      var row = el('div', 'direct-row');
      spec.direct_buttons.forEach(function (opt) {
        var b = el('button', '', opt[1]);
        b.addEventListener('click', function () {
          if (spec.confirm && !window.confirm(spec.confirm)) return;
          doSend(spec, deviceKey, opt[0], {}, chip, resultBox);
        });
        row.appendChild(b);
      });
      host.appendChild(row);
      host.appendChild(chip);
    } else {
      var pv = el('div', 'preview-box');
      previewEl = el('span', '', ''); pv.appendChild(previewEl);
      host.appendChild(pv);
      updatePreview();
      var sendRow = el('div', 'send-row');
      var sendBtn = el('button', 'send-btn', 'Send');
      sendBtn.addEventListener('click', function () {
        if (spec.confirm && !window.confirm(spec.confirm)) return;
        var vals = Controls.collect(spec, controls);
        doSend(spec, deviceKey, spec.build(vals), vals, chip, resultBox);
      });
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip);
      host.appendChild(sendRow);
    }
    host.appendChild(resultBox);
  }

  function doSend(spec, deviceKey, payload, vals, chip, resultBox) {
    chip.textContent = 'sending…';
    Engine.send(deviceKey, spec, payload, vals, function (result) {
      chip.textContent = result.ackKind || (result.text ? 'replied' : 'no reply');
      ResultView.render(resultBox, spec, result, vals);
      renderHero();
    });
  }

  function buildCustomPanel(host, spec, deviceKey) {
    if (spec.custom_panel === 'diffuser_history') {
      var list = el('div', '');
      function draw() {
        clear(list);
        list.appendChild(el('div', 'cmd-item-desc', Engine.DIF.history.length + ' of 10 cycles stored'));
        Engine.DIF.history.forEach(function (mins, i) {
          var row = el('div', 'ctl-row');
          row.appendChild(el('div', 'ctl-label', '#' + (i + 1)));
          var w = el('div', 'ctl-widget', Engine.fmtDurationMin(mins) + '  ');
          var del = el('button', 'chip-status', 'Remove ✕');
          del.addEventListener('click', function () { if (window.confirm('Remove entry #' + (i + 1) + '?')) { Engine.DIF.history.splice(i, 1); draw(); } });
          w.appendChild(del);
          row.appendChild(w);
          list.appendChild(row);
        });
      }
      draw(); host.appendChild(list);
      return;
    }
    if (spec.custom_panel === 'settings_write') {
      var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });
      var select = document.createElement('select'); select.className = 'ctl-select';
      EE_SETTINGS_TABLE.forEach(function (row) { select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[6], String(row[0]))); });
      var selRow = el('div', 'ctl-row'); selRow.appendChild(el('div', 'ctl-label', 'Setting')); var sw = el('div', 'ctl-widget'); sw.appendChild(select); selRow.appendChild(sw);
      host.appendChild(selRow);
      var valueHost = el('div', ''); host.appendChild(valueHost);
      var pv = el('div', 'preview-box'); var previewEl = el('span', ''); pv.appendChild(previewEl); host.appendChild(pv);
      var chip = el('span', 'chip-status', '—');
      var sendRow = el('div', 'send-row'); var sendBtn = el('button', 'send-btn', 'Send');
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip); host.appendChild(sendRow);
      var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—')); host.appendChild(resultBox);

      var valueCtrl;
      function rebuild() {
        clear(valueHost);
        var idx = parseInt(select.value, 10), row = byIdx[idx];
        var cur = Engine.TV.settings[idx] != null ? Engine.TV.settings[idx] : row[3];
        var wrap = el('div', 'ctl-row'); wrap.appendChild(el('div', 'ctl-label', row[6])); var widget = el('div', 'ctl-widget'); wrap.appendChild(widget);
        if (row[2] === 'switch') {
          var btn = el('button', 'ctl-toggle', cur ? 'ON' : 'OFF'); var checked = !!cur;
          if (checked) btn.classList.add('on');
          btn.addEventListener('click', function () { checked = !checked; btn.classList.toggle('on', checked); btn.textContent = checked ? 'ON' : 'OFF'; updatePv(); });
          widget.appendChild(btn); valueCtrl = { get: function () { return checked ? 1 : 0; } };
        } else if (row[2] === 'select') {
          var sel = document.createElement('select'); sel.className = 'ctl-select';
          row[7].forEach(function (name, i) { sel.appendChild(new Option(i + '  ' + name, String(i))); });
          sel.value = String(cur); sel.addEventListener('change', updatePv);
          widget.appendChild(sel); valueCtrl = { get: function () { return parseInt(sel.value, 10); } };
        } else {
          var rw = el('div', 'ctl-range');
          var input = document.createElement('input'); input.type = 'range'; input.min = row[4]; input.max = row[5]; input.value = cur;
          var vl = el('span', 'ctl-range-value', String(cur));
          input.addEventListener('input', function () { vl.textContent = input.value; updatePv(); });
          rw.appendChild(input); rw.appendChild(vl); widget.appendChild(rw);
          valueCtrl = { get: function () { return parseInt(input.value, 10); } };
        }
        valueHost.appendChild(wrap);
        updatePv();
      }
      function updatePv() { previewEl.textContent = 'S' + hexb(parseInt(select.value, 10), 2) + hexb(valueCtrl.get(), 2); }
      select.addEventListener('change', rebuild);
      rebuild();
      sendBtn.addEventListener('click', function () {
        var idx = parseInt(select.value, 10), val = valueCtrl.get();
        chip.textContent = 'sending…';
        Engine.send(deviceKey, { id: '__setwrite', transport: 'udp', envelope: true }, 'S' + hexb(idx, 2) + hexb(val, 2), {}, function (result) {
          Engine.TV.settings[idx] = val;
          chip.textContent = result.ackKind;
          ResultView.render(resultBox, {}, result, {});
        });
      });
      return;
    }
  }

  /* --------------------------------------------------------- activity panel */
  function openActivity() { $('activity-panel').classList.add('open'); state.unread = 0; updateBadge(); renderActivityLog(); }
  function closeActivity() { $('activity-panel').classList.remove('open'); }
  function renderActivityLog() {
    var host = $('activity-log'); clear(host);
    activityLines.forEach(function (line) {
      var row = el('div', 'log-line ' + (line.cls || 'sys'));
      row.appendChild(el('span', 'log-ts', line.ts));
      row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
      host.appendChild(row);
    });
    host.scrollTop = host.scrollHeight;
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
