/* mockup-9-tile-board.js - macOS System Settings-style control panel.
 * Every command is a tile in a grid, grouped under section headers;
 * clicking a tile expands it in place (full-width accordion) with its
 * controls/send/result right there in the grid - no separate panel to
 * navigate to. A slim always-visible activity column replaces the
 * separate log-panel/quicktest split.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

  var state = { activeDevice: 'diffuser', keepAliveOn: false, filter: '' };

  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('savelog-btn').addEventListener('click', function () { Engine.saveLog(); });
    $('filter-input').addEventListener('input', function () { state.filter = this.value.toLowerCase().trim(); renderBoard(); });

    Engine.subscribeLog(function (line) { renderActivityLine(line); });

    switchDevice('diffuser');
    Engine.logSys('TestMode mockup started — simulated device, no real network I/O.');
  }

  function switchDevice(key) {
    state.activeDevice = key;
    $('dev-diffuser').classList.toggle('on', key === 'diffuser');
    $('dev-smarttv').classList.toggle('on', key === 'smarttv');
    $('mainhead-title').textContent = Engine.devices[key].name + ' commands';
    $('conn-ip').textContent = Engine.devices[key].ip + ':' + Engine.devices[key].udpPort;
    renderBoard();
  }
  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    $('keepalive-btn').classList.toggle('on', state.keepAliveOn);
    if (state.keepAliveOn) Engine.startKeepAlive(function () { return state.activeDevice; });
    else Engine.stopKeepAlive();
  }

  function renderActivityLine(line) {
    var host = $('activity-feed');
    var row = el('div', 'log-line ' + (line.cls || 'sys'));
    row.appendChild(el('span', 'log-ts', line.ts.slice(11)));
    row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
    host.appendChild(row);
    host.scrollTop = host.scrollHeight;
    while (host.children.length > 200) host.removeChild(host.firstChild);
  }

  function renderBoard() {
    var host = $('board'); clear(host);
    var list = COMMAND_TABLES[state.activeDevice];
    var bySection = {}, order = [];
    list.forEach(function (spec) {
      var q = state.filter;
      var matches = !q || spec.id.toLowerCase().indexOf(q) !== -1 || spec.label.toLowerCase().indexOf(q) !== -1 || (spec.name || '').toLowerCase().indexOf(q) !== -1;
      if (!matches) return;
      if (!bySection[spec.section]) { bySection[spec.section] = []; order.push(spec.section); }
      bySection[spec.section].push(spec);
    });
    if (!order.length) { host.appendChild(el('div', 'result-empty', 'No commands match "' + state.filter + '"')); return; }
    order.forEach(function (sec) {
      var meta = categoryMeta(sec);
      host.appendChild(el('div', 'board-section-title', meta.icon + ' ' + meta.title));
      var grid = el('div', 'grid');
      bySection[sec].forEach(function (spec) {
        var tile = el('div', 'gtile');
        var head = el('div', 'gtile-head');
        head.appendChild(el('span', 'icon', meta.icon));
        head.appendChild(el('span', 'name', spec.name || spec.label));
        head.appendChild(el('span', 'label', spec.label));
        var chev = el('span', 'chev', '▾');
        head.appendChild(chev);
        var body = el('div', 'gtile-body');
        var built = false;
        head.addEventListener('click', function () {
          var willOpen = !tile.classList.contains('open');
          tile.classList.toggle('open');
          chev.textContent = willOpen ? '▴' : '▾';
          if (willOpen && !built) { built = true; buildTileBody(body, spec); }
        });
        tile.appendChild(head); tile.appendChild(body);
        grid.appendChild(tile);
      });
      host.appendChild(grid);
    });
  }

  function buildTileBody(host, spec) {
    var deviceKey = state.activeDevice;
    host.appendChild(el('div', 'gtile-desc', spec.desc || ''));
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
      host.appendChild(row); host.appendChild(chip);
    } else {
      var pv = el('div', 'preview-box'); previewEl = el('span', ''); pv.appendChild(previewEl);
      host.appendChild(pv); updatePreview();
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
    });
  }

  function buildCustomPanel(host, spec, deviceKey) {
    if (spec.custom_panel === 'diffuser_history') {
      var list = el('div', '');
      function draw() {
        clear(list);
        list.appendChild(el('div', 'gtile-desc', Engine.DIF.history.length + ' of 10 cycles stored'));
        Engine.DIF.history.forEach(function (mins, i) {
          var row = el('div', 'ctl-row');
          row.appendChild(el('div', 'ctl-label', '#' + (i + 1)));
          var w = el('div', 'ctl-widget', Engine.fmtDurationMin(mins) + '  ');
          var del = el('button', 'chip-status', 'Remove ✕');
          del.addEventListener('click', function () { if (window.confirm('Remove #' + (i + 1) + '?')) { Engine.DIF.history.splice(i, 1); draw(); } });
          w.appendChild(del); row.appendChild(w);
          list.appendChild(row);
        });
      }
      draw(); host.appendChild(list);
      return;
    }
    if (spec.custom_panel === 'settings_write') {
      var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });
      var selRow = el('div', 'ctl-row'); selRow.appendChild(el('div', 'ctl-label', 'Setting'));
      var select = document.createElement('select'); select.className = 'ctl-select';
      EE_SETTINGS_TABLE.forEach(function (row) { select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[6], String(row[0]))); });
      var sw = el('div', 'ctl-widget'); sw.appendChild(select); selRow.appendChild(sw); host.appendChild(selRow);
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
        valueHost.appendChild(wrap); updatePv();
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

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
