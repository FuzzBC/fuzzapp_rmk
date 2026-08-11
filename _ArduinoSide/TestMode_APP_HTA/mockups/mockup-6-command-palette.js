/* mockup-6-command-palette.js - Raycast/Spotlight-style UX.
 * Home = live status dashboard + quick actions + recent activity.
 * ⌘K / "/" / clicking the search bar opens a full-screen fuzzy palette
 * over every command; picking one opens a slide-in inspector drawer on
 * the right where params/preview/send/result live. Activity log is a
 * separate bottom sheet, not a persistent panel.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

  var state = { activeDevice: 'diffuser', keepAliveOn: false, activityUnread: 0, paletteActiveIndex: 0, paletteRows: [] };

  /* ------------------------------------------------------------- boot */
  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('activity-btn').addEventListener('click', openActivity);
    $('activity-close').addEventListener('click', closeActivity);
    $('activity-savelog').addEventListener('click', function () { Engine.saveLog(); });
    $('search-trigger').addEventListener('click', openPalette);
    $('drawer-close').addEventListener('click', closeDrawer);
    $('drawer-scrim').addEventListener('click', closeDrawer);
    $('palette-input').addEventListener('input', function () { renderPaletteList(this.value); });
    $('palette-overlay').addEventListener('click', function (e) { if (e.target === this) closePalette(); });

    document.addEventListener('keydown', function (e) {
      var tag = (document.activeElement && document.activeElement.tagName) || '';
      var typing = tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
      if ((e.key === 'k' && (e.metaKey || e.ctrlKey)) || (e.key === '/' && !typing)) {
        e.preventDefault(); openPalette(); return;
      }
      if (e.key === 'Escape') {
        if (!$('palette-overlay').classList.contains('hidden')) closePalette();
        else if ($('inspector-drawer').classList.contains('open')) closeDrawer();
        else if ($('activity-drawer').classList.contains('open')) closeActivity();
        return;
      }
      if (!$('palette-overlay').classList.contains('hidden')) {
        if (e.key === 'ArrowDown') { e.preventDefault(); movePalette(1); }
        else if (e.key === 'ArrowUp') { e.preventDefault(); movePalette(-1); }
        else if (e.key === 'Enter') { e.preventDefault(); chooseActivePaletteRow(); }
      }
    });

    Engine.subscribeLog(function (line) {
      pushActivityLine(line);
      if ($('activity-drawer').classList.contains('open')) renderActivityLog();
      else { state.activityUnread++; updateActivityBadge(); }
      renderRecentActivity();
    });

    switchDevice('diffuser');
    Engine.logSys('TestMode mockup started — simulated device, no real network I/O.');
  }

  var activityLines = [];
  function pushActivityLine(line) { activityLines.push(line); if (activityLines.length > 300) activityLines.shift(); }
  function updateActivityBadge() {
    var b = $('activity-badge');
    if (state.activityUnread > 0) { b.textContent = String(state.activityUnread); b.classList.remove('hidden'); }
    else b.classList.add('hidden');
  }

  /* --------------------------------------------------------- top bar */
  function switchDevice(key) {
    state.activeDevice = key;
    $('dev-diffuser').classList.toggle('on', key === 'diffuser');
    $('dev-smarttv').classList.toggle('on', key === 'smarttv');
    var d = Engine.devices[key];
    $('conn-ip').textContent = d.ip + ':' + d.udpPort;
    renderStatTiles();
    renderQuickActions();
    renderRecentActivity();
  }

  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    $('keepalive-btn').style.color = state.keepAliveOn ? 'var(--green)' : '';
    $('keepalive-btn').style.borderColor = state.keepAliveOn ? 'var(--green)' : '';
    if (state.keepAliveOn) Engine.startKeepAlive(function () { return state.activeDevice; });
    else Engine.stopKeepAlive();
  }

  /* --------------------------------------------------------- dashboard */
  function renderStatTiles() {
    var host = $('stat-tiles'); clear(host);
    var tiles;
    if (state.activeDevice === 'diffuser') {
      var d = Engine.DIF;
      tiles = [
        ['Mode', 'M' + d.mode + ' (' + (DIF_MODE_NAMES[d.mode] || '?') + ')', 'mode'],
        ['Parfum timer', d.parfumMin ? d.parfumMin + ' min' : 'off', 'timer'],
        ['Usage this cycle', d.usageMin + ' min', 'timer'],
        ['Refills', d.refillCount + '/10 (total ' + d.totalRefills + ')', 'count'],
      ];
    } else {
      var t = Engine.TV;
      tiles = [
        ['TV', t.tvOn ? 'on' : 'off', 'flag'],
        ['Motion', MOTION_STATUS_NAMES_LOCAL[t.motion] || t.motion, 'mode'],
        ['LEDs', t.ledsOn ? 'enabled' : 'disabled', 'flag'],
        ['Ambient mode', t.ambient ? 'on' : 'off', 'count'],
      ];
    }
    tiles.forEach(function (tt) {
      var tile = el('div', 'stat-tile accent-' + tt[2]);
      tile.appendChild(el('div', 'lab', tt[0]));
      tile.appendChild(el('div', 'val', tt[1]));
      host.appendChild(tile);
    });
  }
  var MOTION_STATUS_NAMES_LOCAL = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed'];

  function renderQuickActions() {
    var host = $('quick-actions'); clear(host);
    var specs = COMMAND_TABLES[state.activeDevice].filter(function (s) {
      return s.id === 'Ds' || s.id === 'Dc' || s.id === 'Z' || s.id === 'X' || s.section.toLowerCase().indexOf('test mode') !== -1;
    });
    var seen = 0;
    specs.forEach(function (spec) {
      if (spec.direct_buttons) {
        spec.direct_buttons.forEach(function (opt) {
          if (seen++ > 9) return;
          var b = el('button', '', opt[1]);
          b.addEventListener('click', function () { fireQuick(spec, opt[0], {}); });
          host.appendChild(b);
        });
      } else if (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum') {
        var p = spec.params[0];
        p.options.forEach(function (opt) {
          if (seen++ > 9) return;
          var v = {}; v[p.key] = opt[0];
          var b = el('button', '', opt[1]);
          b.addEventListener('click', function () { fireQuick(spec, spec.build(v), v); });
          host.appendChild(b);
        });
      } else {
        if (seen++ > 9) return;
        var b2 = el('button', '', spec.name || spec.label);
        b2.addEventListener('click', function () { openDrawer(spec); });
        host.appendChild(b2);
      }
    });
  }

  function fireQuick(spec, payload, vals) {
    if (spec.confirm && !window.confirm(spec.confirm)) return;
    Engine.send(state.activeDevice, spec, payload, vals, function () { renderStatTiles(); });
  }

  function renderRecentActivity() {
    var host = $('recent-activity'); clear(host);
    var recent = Engine.logLines.slice(-8).reverse();
    if (!recent.length) { host.appendChild(el('div', 'activity-empty', 'Nothing sent yet.')); return; }
    recent.forEach(function (line) {
      var row = el('div', 'activity-row');
      var kind = line.indexOf('[SYS]') !== -1 ? 'sys' : (line.indexOf(' > ') !== -1 ? 'send' : 'recv');
      var dot = el('span', 'dot ' + kind); row.appendChild(dot);
      row.appendChild(el('span', 'txt', line));
      host.appendChild(row);
    });
  }

  /* --------------------------------------------------------- palette */
  function openPalette() {
    $('palette-overlay').classList.remove('hidden');
    var input = $('palette-input');
    input.value = ''; input.focus();
    renderPaletteList('');
  }
  function closePalette() { $('palette-overlay').classList.add('hidden'); }

  function renderPaletteList(query) {
    query = query.trim().toLowerCase();
    var host = $('palette-list'); clear(host);
    var list = COMMAND_TABLES[state.activeDevice];
    var bySection = {}, order = [];
    list.forEach(function (spec) {
      var matches = !query || spec.id.toLowerCase().indexOf(query) !== -1 || spec.label.toLowerCase().indexOf(query) !== -1 ||
        (spec.name || '').toLowerCase().indexOf(query) !== -1 || spec.section.toLowerCase().indexOf(query) !== -1;
      if (!matches) return;
      if (!bySection[spec.section]) { bySection[spec.section] = []; order.push(spec.section); }
      bySection[spec.section].push(spec);
    });
    state.paletteRows = [];
    if (!order.length) { host.appendChild(el('div', 'palette-empty', 'No commands match "' + query + '"')); return; }
    order.forEach(function (sec) {
      var meta = categoryMeta(sec);
      host.appendChild(el('div', 'palette-group', meta.title));
      bySection[sec].forEach(function (spec) {
        var row = el('div', 'palette-row');
        row.appendChild(el('span', 'icon', meta.icon));
        row.appendChild(el('span', 'name', spec.name || spec.label));
        row.appendChild(el('span', 'label', spec.label));
        row.addEventListener('click', function () { closePalette(); openDrawer(spec); });
        row.addEventListener('mousemove', function () { setPaletteActive(state.paletteRows.indexOf(row)); });
        host.appendChild(row);
        state.paletteRows.push(row);
      });
    });
    setPaletteActive(0);
  }
  function setPaletteActive(i) {
    state.paletteRows.forEach(function (r) { r.classList.remove('active'); });
    if (state.paletteRows[i]) { state.paletteRows[i].classList.add('active'); state.paletteActiveIndex = i; state.paletteRows[i].scrollIntoView({ block: 'nearest' }); }
  }
  function movePalette(delta) {
    var n = state.paletteRows.length; if (!n) return;
    setPaletteActive((state.paletteActiveIndex + delta + n) % n);
  }
  function chooseActivePaletteRow() {
    var row = state.paletteRows[state.paletteActiveIndex];
    if (row) row.dispatchEvent(new Event('click'));
  }

  /* --------------------------------------------------------- inspector drawer */
  function openDrawer(spec) {
    $('inspector-drawer').classList.add('open');
    $('drawer-scrim').classList.remove('hidden');
    $('drawer-title').textContent = spec.name || spec.label;
    renderDrawerContent(spec);
  }
  function closeDrawer() { $('inspector-drawer').classList.remove('open'); $('drawer-scrim').classList.add('hidden'); }

  function renderDrawerContent(spec) {
    var host = $('drawer-content'); clear(host);
    var deviceKey = state.activeDevice;
    var meta = categoryMeta(spec.section);

    var badges = el('div', '');
    badges.appendChild(el('span', 'd-badge', spec.label));
    badges.appendChild(el('span', 'd-badge', spec.transport === 'udp' ? 'UDP' : (spec.transport === 'console' ? 'TCP:23' : spec.transport)));
    badges.appendChild(el('span', 'd-badge', meta.title));
    host.appendChild(badges);
    host.appendChild(el('div', 'd-desc', spec.desc || ''));

    var controls = {}, previewEl, resultBox = el('div', 'd-result');
    resultBox.appendChild(el('div', 'd-empty', '—'));

    function updatePreview() {
      if (!previewEl || spec.direct_buttons) return;
      try { previewEl.textContent = spec.build(Controls.collect(spec, controls)); } catch (e) { previewEl.textContent = '(invalid)'; }
    }

    if (spec.custom_panel) {
      renderCustomPanel(host, spec, deviceKey);
      return;
    }

    if (spec.params && spec.params.length) {
      host.appendChild(el('div', 'd-step', 'Configure'));
      var form = el('div', '');
      controls = Controls.buildForm(form, spec, updatePreview);
      host.appendChild(form);
    }

    var chip = el('span', 'd-chip', '—');
    if (spec.direct_buttons) {
      host.appendChild(el('div', 'd-step', 'Choose an option'));
      var row = el('div', 'd-direct');
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
      host.appendChild(el('div', 'd-step', 'Review & send'));
      var pv = el('div', 'd-preview');
      previewEl = el('span', '', ''); pv.appendChild(previewEl);
      host.appendChild(pv);
      updatePreview();
      var sendRow = el('div', 'd-send-row');
      var sendBtn = el('button', 'd-send-btn', 'Send');
      sendBtn.addEventListener('click', function () {
        if (spec.confirm && !window.confirm(spec.confirm)) return;
        var vals = Controls.collect(spec, controls);
        doSend(spec, deviceKey, spec.build(vals), vals, chip, resultBox);
      });
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip);
      host.appendChild(sendRow);
    }

    host.appendChild(el('div', 'd-step', 'Result'));
    host.appendChild(resultBox);
  }

  function doSend(spec, deviceKey, payload, vals, chip, resultBox) {
    chip.textContent = 'sending…';
    Engine.send(deviceKey, spec, payload, vals, function (result) {
      chip.textContent = result.ackKind || (result.text ? 'replied' : 'no reply');
      ResultView.render(resultBox, spec, result, vals);
      renderStatTiles();
    });
  }

  /* ---------------------------------------------- simplified custom panels */
  function renderCustomPanel(host, spec, deviceKey) {
    if (spec.custom_panel === 'diffuser_history') {
      var list = el('div', '');
      function draw() {
        clear(list);
        var status = el('div', 'd-step', Engine.DIF.history.length + ' of 10 cycles stored');
        list.appendChild(status);
        Engine.DIF.history.forEach(function (mins, i) {
          var row = el('div', 'ctl-row');
          row.appendChild(el('div', 'ctl-label', '#' + (i + 1)));
          var w = el('div', 'ctl-widget', Engine.fmtDurationMin(mins));
          var del = el('button', 'chip-btn', 'Remove');
          del.style.marginLeft = '10px';
          del.addEventListener('click', function () {
            if (!window.confirm('Remove entry #' + (i + 1) + '?')) return;
            Engine.DIF.history.splice(i, 1); draw();
          });
          w.appendChild(del);
          row.appendChild(w);
          list.appendChild(row);
        });
      }
      draw();
      host.appendChild(list);
      return;
    }
    if (spec.custom_panel === 'settings_write') {
      var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });
      host.appendChild(el('div', 'd-step', 'Choose a setting'));
      var selWrap = el('div', 'ctl-row');
      var select = document.createElement('select'); select.className = 'ctl-select';
      EE_SETTINGS_TABLE.forEach(function (row) { select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[6], String(row[0]))); });
      selWrap.appendChild(select);
      host.appendChild(selWrap);
      var valueHost = el('div', ''); host.appendChild(valueHost);
      var previewEl2 = el('div', 'd-preview');
      host.appendChild(el('div', 'd-step', 'Review & send'));
      host.appendChild(previewEl2);
      var chip2 = el('span', 'd-chip', '—');
      var sendRow2 = el('div', 'd-send-row');
      var sendBtn2 = el('button', 'd-send-btn', 'Send');
      sendRow2.appendChild(sendBtn2); sendRow2.appendChild(chip2);
      host.appendChild(sendRow2);
      var resultBox2 = el('div', 'd-result'); resultBox2.appendChild(el('div', 'd-empty', '—'));
      host.appendChild(el('div', 'd-step', 'Result'));
      host.appendChild(resultBox2);

      var valueCtrl = null;
      function rebuild() {
        clear(valueHost);
        var idx = parseInt(select.value, 10), row = byIdx[idx];
        var cur = Engine.TV.settings[idx] != null ? Engine.TV.settings[idx] : row[3];
        var wrap = el('div', 'ctl-row');
        wrap.appendChild(el('div', 'ctl-label', row[6]));
        var widget = el('div', 'ctl-widget'); wrap.appendChild(widget);
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
          rw.appendChild(input); rw.appendChild(vl);
          widget.appendChild(rw); valueCtrl = { get: function () { return parseInt(input.value, 10); } };
        }
        valueHost.appendChild(wrap);
        updatePv();
      }
      function updatePv() {
        var idx = parseInt(select.value, 10);
        previewEl2.textContent = 'S' + hexb(idx, 2) + hexb(valueCtrl.get(), 2);
      }
      select.addEventListener('change', rebuild);
      rebuild();
      sendBtn2.addEventListener('click', function () {
        var idx = parseInt(select.value, 10), val = valueCtrl.get();
        chip2.textContent = 'sending…';
        var payload = 'S' + hexb(idx, 2) + hexb(val, 2);
        Engine.send(deviceKey, { id: '__setwrite', transport: 'udp', envelope: true }, payload, {}, function (result) {
          Engine.TV.settings[idx] = val;
          chip2.textContent = result.ackKind;
          ResultView.render(resultBox2, {}, result, {});
        });
      });
      return;
    }
  }

  /* --------------------------------------------------------- activity drawer */
  function openActivity() {
    $('activity-drawer').classList.add('open');
    state.activityUnread = 0; updateActivityBadge();
    renderActivityLog();
  }
  function closeActivity() { $('activity-drawer').classList.remove('open'); }
  function renderActivityLog() {
    var host = $('activity-log'); clear(host);
    activityLines.forEach(function (line) {
      var cls = line.tag === 'SYS' ? 'sys' : (line.cls || 'recv');
      var row = el('div', 'log-line ' + cls);
      row.appendChild(el('span', 'log-ts', line.ts));
      row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
      host.appendChild(row);
    });
    host.scrollTop = host.scrollHeight;
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
