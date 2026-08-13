/* ui.js - Dashboard + Bottom Sheet UI for TestMode.hta (00_PLAN.md Phase 6
 * follow-up work). Layout/navigation code ported unchanged from the frozen
 * TestMode_APP_HTA; everything that touched the old ASCII protocol is
 * rewritten: `lastKnown` is filled from decoded TELEM_* frames instead of
 * ASCII status strings, hero actions send real opcodes (HELLO/
 * LED_SET_ENABLE/DIFFUSER_STATUS_QUERY/...) instead of 'Z'/'X'/'Ds' text
 * commands, and both custom panels (refill-history manager, write-one-
 * setting) now build binary frames (DIFFUSER_HISTORY_QUERY/_REMOVE,
 * SETTINGS_READ_ONE/_WRITE) instead of ASCII 'Dh'/'Dy'/'S' commands.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }
  function addClass(e, c) { if ((' ' + e.className + ' ').indexOf(' ' + c + ' ') === -1) e.className = (e.className + ' ' + c).replace(/^\s+/, ''); }
  function removeClass(e, c) { e.className = (' ' + e.className + ' ').replace(' ' + c + ' ', ' ').replace(/^\s+|\s+$/g, ''); }
  function hasClass(e, c) { return (' ' + e.className + ' ').indexOf(' ' + c + ' ') !== -1; }

  var state = { activeDevice: 'diffuser', keepAliveOn: false, unread: 0 };
  var activityLines = [];

  // What we actually know about each device right now, filled in only by
  // decoded TELEM_* frames as they arrive (nothing here is invented) - null
  // fields render as "—" until something tells us otherwise.
  var lastKnown = {
    diffuser: { mode: null, strip: null, parfumMin: null, usageMin: null, avgMin: null, refillCount: null, totalRefills: null },
    smarttv: { tvOn: null, motion: null, ledsOn: null, udpraw: null, ambient: null, dif: null }
  };

  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('sheet-close').addEventListener('click', closeSheet);
    $('sheet-scrim').addEventListener('click', closeSheet);
    $('quicktest-fab').addEventListener('click', openQuickCommands);
    $('quicktest-close').addEventListener('click', closeQuickCommands);
    $('activity-fab').addEventListener('click', openActivity);
    $('activity-close').addEventListener('click', closeActivity);
    $('activity-savelog').addEventListener('click', function () { Engine.saveLog(); });
    $('raw-send-btn').addEventListener('click', sendRaw);

    Engine.subscribeLog(function (line) {
      activityLines.push(line);
      if (hasClass($('activity-panel'), 'open')) appendActivityLine(line);
      else { state.unread++; updateBadge(); }
    });

    switchDevice('diffuser');
    Engine.logSys('TestMode (rmk) started - binary v1 protocol.');
    Engine.logSys('Diffuser -> ' + Engine.devices.diffuser.ip + ':' + Engine.devices.diffuser.udpPort +
      '   SmartTV -> ' + Engine.devices.smarttv.ip + ':' + Engine.devices.smarttv.udpPort);
  }

  function updateBadge() {
    var b = $('activity-badge');
    if (state.unread > 0) { b.textContent = String(state.unread); removeClass(b, 'hidden'); } else addClass(b, 'hidden');
  }

  function switchDevice(key) {
    state.activeDevice = key;
    if (key === 'diffuser') { addClass($('dev-diffuser'), 'on'); removeClass($('dev-smarttv'), 'on'); }
    else { addClass($('dev-smarttv'), 'on'); removeClass($('dev-diffuser'), 'on'); }
    $('conn-ip').textContent = Engine.devices[key].ip;
    renderHero();
    renderQuickTestRow();
    renderTileGrid();
    renderRawTransport();
    closeSheet();
    closeQuickCommands();
    autoConnect(key);
  }

  // Eagerly refreshes the hero on every tab switch instead of waiting for a
  // manual "Connect"/"Check status" press - SmartTV's HELLO happens to
  // trigger a full telemetry burst so this looked automatic there already,
  // but Diffuser's HELLO only ACKs (no telemetry), so its hero silently sat
  // on "no data yet" until you noticed the differently-labelled "Check
  // status" button was the equivalent action. Firing the real status query
  // here removes that asymmetry instead of just documenting it.
  function autoConnect(key) {
    if (key === 'diffuser') {
      fire(findSpec('DifStatus'), Proto.packDiffuserStatusQuery(0), {});
    } else {
      fire(findSpec('Hello'), Proto.packHello(1), {});
    }
  }

  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    if (state.keepAliveOn) addClass($('keepalive-btn'), 'on'); else removeClass($('keepalive-btn'), 'on');
    $('keepalive-btn').textContent = 'Keep-alive: ' + (state.keepAliveOn ? 'on' : 'off');
    if (state.keepAliveOn) Engine.startKeepAlive(function () { return state.activeDevice; });
    else Engine.stopKeepAlive();
  }

  /* ------------------------------------------------- absorb real state */
  // Scans whatever frames a send actually drew back and folds any
  // status-shaped TELEM_* ones into lastKnown, regardless of which command
  // triggered them - a HELLO handshake, a direct status query, or an async
  // push riding along with something else all update the same cache.
  function absorbEntries(deviceKey, entries) {
    var k = lastKnown[deviceKey];
    for (var i = 0; i < entries.length; i++) {
      var p = entries[i].parsed;
      if (!p || !p.ok) continue;
      var name = p.opcodeName;
      if (deviceKey === 'diffuser' && name === 'TELEM_DIFFUSER_STATUS') {
        var ds = Proto.unpackTelemDiffuserStatus(p.payload);
        k.mode = ds.mode; k.strip = ds.strip; k.parfumMin = ds.parfum_min; k.usageMin = ds.usage_min;
        k.avgMin = ds.avg_min; k.refillCount = ds.refill_count; k.totalRefills = ds.lifetime_refills;
      } else if (deviceKey === 'smarttv' && name === 'TELEM_STATUS') {
        var st = Proto.unpackTelemStatus(p.payload);
        k.tvOn = !!st.tv; k.motion = st.motion; k.udpraw = !!st.udpraw; k.ambient = st.ambient; k.dif = st.diffuser_summary;
      } else if (deviceKey === 'smarttv' && name === 'TELEM_ENABLE') {
        var en = Proto.unpackTelemEnable(p.payload);
        k.ledsOn = !!en.value;
      }
    }
  }

  /* ------------------------------------------------------------- hero */
  // MOTION_LABEL/AMBIENT_LABEL/DIF_SUMMARY_LABEL now live in data.js,
  // shared with result-view.js's generic entry cards.
  var DIF_MODE_LABEL = ['OFF', 'CONT', '10 SEC', '2H AFTER SLEEP', '4H AFTER SLEEP'];
  function dash(v) { return v == null ? '—' : v; }

  function renderHero() {
    var host = $('hero'); clear(host);
    var top = el('div', 'hero-top');
    var left = el('div', '');
    left.appendChild(el('div', 'hero-label', state.activeDevice.toUpperCase()));
    var stats = el('div', 'hero-stats');
    var actions = el('div', 'hero-actions');

    if (state.activeDevice === 'diffuser') {
      var d = lastKnown.diffuser;
      var modeText = d.mode == null ? 'no data yet' : ('M' + d.mode + ' · ' + (DIF_MODE_LABEL[d.mode] || '?'));
      left.appendChild(el('div', 'hero-title', modeText));
      left.appendChild(el('div', 'hero-sub', d.mode == null ? 'Press "Check status" to read the device' : (d.parfumMin ? ('Parfum running — ' + d.parfumMin + ' min left') : 'Parfum idle')));
      [['Usage', d.usageMin != null ? d.usageMin + 'm' : '—'], ['Avg cycle', d.avgMin != null ? d.avgMin + 'm' : '—'],
       ['Refills', d.refillCount != null ? d.refillCount + '/10' : '—'], ['Total', dash(d.totalRefills)]].forEach(function (s) {
        var st = el('div', 'hero-stat'); st.appendChild(el('div', 'lab', s[0])); st.appendChild(el('div', 'val', String(s[1]))); stats.appendChild(st);
      });
      addAction(actions, 'Check status', function () { fire(findSpec('DifStatus'), Proto.packDiffuserStatusQuery(0), {}); });
      addAction(actions, 'Cancel parfum', function () { fire(findSpec('DifParfumCancel'), '', {}); });
      addAction(actions, 'Shut down', function () { fire(findSpec('DifShutdown'), '', {}); });
    } else {
      var t = lastKnown.smarttv;
      left.appendChild(el('div', 'hero-title', t.tvOn == null ? 'no data yet' : (t.tvOn ? 'TV is on' : 'TV is off')));
      left.appendChild(el('div', 'hero-sub', t.tvOn == null ? 'Press "Connect" to read the device' :
        ('Motion: ' + (MOTION_LABEL[t.motion] || t.motion) + ' · LEDs ' + (t.ledsOn == null ? '—' : (t.ledsOn ? 'on' : 'off')))));
      [['Ambilight', t.udpraw == null ? '—' : (t.udpraw ? 'on' : 'off')],
       ['Ambient mode', t.ambient == null ? '—' : (AMBIENT_LABEL[t.ambient] || t.ambient)],
       ['Diffuser', t.dif == null ? '—' : (DIF_SUMMARY_LABEL[t.dif] || t.dif)],
       ['LEDs', t.ledsOn == null ? '—' : (t.ledsOn ? 'on' : 'off')]].forEach(function (s) {
        var st = el('div', 'hero-stat'); st.appendChild(el('div', 'lab', s[0])); st.appendChild(el('div', 'val', String(s[1]))); stats.appendChild(st);
      });
      addAction(actions, 'Connect (HELLO)', function () { fire(findSpec('Hello'), Proto.packHello(1), {}); });
      addAction(actions, 'Toggle LEDs', function () { fire(findSpec('LedSetEnable'), '', {}); });
    }
    top.appendChild(left);
    host.appendChild(top); host.appendChild(stats); host.appendChild(actions);
  }
  function addAction(container, label, fn) { var b = el('button', '', label); b.addEventListener('click', fn); container.appendChild(b); }
  function findSpec(id) { var t = COMMAND_TABLES[state.activeDevice]; for (var i = 0; i < t.length; i++) if (t[i].id === id) return t[i]; return null; }
  function fire(spec, payload, vals) {
    if (!spec) return;
    if (spec.confirm && !window.confirm(spec.confirm)) return;
    var dk = state.activeDevice;
    Engine.send(dk, spec, payload, vals, function (result) {
      absorbEntries(dk, result.entries || []);
      renderHero();
    });
  }

  /* -------------------------------------------------------- quick test row */
  function renderQuickTestRow() {
    var host = $('quicktest-row'); clear(host);
    var specs = COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.section.toLowerCase().indexOf('test mode') !== -1; });
    var any = false;
    specs.forEach(function (spec) {
      var options = spec.direct_buttons ? spec.direct_buttons :
        (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum' ?
          spec.params[0].options.map(function (opt) { var v = {}; v[spec.params[0].key] = opt[0]; return [spec.build(v), opt[1]]; }) : null);
      if (!options) return;
      options.forEach(function (opt) {
        any = true;
        var b = el('button', '', spec.label.replace(/[^A-Za-z0-9_]/g, '').slice(0, 18) + ' ' + opt[1]);
        b.addEventListener('click', function () { fire(spec, opt[0], {}); });
        host.appendChild(b);
      });
    });
    if (any) removeClass($('quicktest-fab'), 'hidden'); else { addClass($('quicktest-fab'), 'hidden'); closeQuickCommands(); }
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
      tile.type = 'button';
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
    var allItems = [];
    specs.forEach(function (spec) {
      var item = el('div', 'cmd-item');
      var head = el('div', 'cmd-item-head');
      head.appendChild(el('span', 'name', spec.name || spec.label));
      head.appendChild(el('span', 'label', spec.label));
      var body2 = el('div', 'cmd-item-body');
      var built = false;
      head.addEventListener('click', function () {
        var willOpen = !hasClass(item, 'open');
        allItems.forEach(function (other) { if (other !== item) removeClass(other, 'open'); });
        if (willOpen) addClass(item, 'open'); else removeClass(item, 'open');
        if (willOpen && !built) { built = true; buildCommandBody(body2, spec); }
      });
      item.appendChild(head); item.appendChild(body2);
      body.appendChild(item);
      allItems.push(item);
    });
    addClass($('sheet'), 'open');
    removeClass($('sheet-scrim'), 'hidden');
  }
  function closeSheet() { removeClass($('sheet'), 'open'); addClass($('sheet-scrim'), 'hidden'); }

  function openQuickCommands() { addClass($('quicktest-panel'), 'open'); }
  function closeQuickCommands() { removeClass($('quicktest-panel'), 'open'); }

  function buildCommandBody(host, spec) {
    var deviceKey = state.activeDevice;
    host.appendChild(el('div', 'cmd-item-desc', spec.desc || ''));

    if (spec.custom_panel === 'diffuser_history') { buildDiffuserHistory(host, spec, deviceKey); return; }
    if (spec.custom_panel === 'settings_write') { buildSettingsWrite(host, spec, deviceKey); return; }

    var controls = {}, previewEl;
    var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—'));

    function updatePreview() {
      if (!previewEl || spec.direct_buttons) return;
      try { previewEl.textContent = Engine.toHex(spec.build(Controls.collect(spec, controls))) || '(empty payload)'; } catch (e) { previewEl.textContent = '(invalid)'; }
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
      absorbEntries(deviceKey, result.entries || []);
      renderHero();
    });
  }

  /* ------------------------------------------------ refill history (real) */
  function buildDiffuserHistory(host, spec, deviceKey) {
    var status = el('div', 'cmd-item-desc', 'Loading…');
    host.appendChild(status);
    var list = el('div', '');
    host.appendChild(list);

    var readSpec = { id: '__dh_read', opcode: 'DIFFUSER_HISTORY_QUERY', transport: 'udp', timeout_ms: 4500 };
    var delSpec = { id: '__dh_del', opcode: 'DIFFUSER_HISTORY_REMOVE', transport: 'udp', timeout_ms: 4500 };

    function fetchAndRender() {
      status.textContent = 'Loading…';
      Engine.send(deviceKey, readSpec, '', {}, function (result) {
        if (result.ackKind === 'timeout' || result.ackKind === 'error' || !(result.entries || []).length) {
          status.textContent = 'Could not read history (' + result.ackKind + ')';
          clear(list);
          return;
        }
        var minutes = ResultView.parseDiffuserHistory(result.entries[0].parsed.payload);
        status.textContent = minutes.length + ' of 10 cycles';
        renderRows(minutes);
      });
    }
    function renderRows(minutes) {
      clear(list);
      if (!minutes.length) { list.appendChild(el('div', 'result-empty', 'No refill cycles recorded yet.')); return; }
      minutes.forEach(function (mins, i) {
        var row = el('div', 'dh-row');
        row.appendChild(el('div', 'dh-index', '#' + (i + 1)));
        row.appendChild(el('div', 'dh-value', Engine.fmtDurationMin(mins)));
        var del = el('button', 'dh-remove', 'Remove ✕');
        del.addEventListener('click', function () {
          if (!window.confirm("Remove entry #" + (i + 1) + ' (' + Engine.fmtDurationMin(mins) + ")? Everything after it shifts down to fill the gap. This can't be undone.")) return;
          status.textContent = 'Removing #' + (i + 1) + '…';
          Engine.send(deviceKey, delSpec, Proto.packDiffuserHistoryRemove(i), {}, function () { fetchAndRender(); });
        });
        row.appendChild(del);
        list.appendChild(row);
      });
    }
    fetchAndRender();
  }

  /* ------------------------------------------------- write setting (real) */
  function buildSettingsWrite(host, spec, deviceKey) {
    var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });
    var select = document.createElement('select'); select.className = 'ctl-select';
    EE_SETTINGS_TABLE.forEach(function (row) { select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[6], String(row[0]))); });
    var selRow = el('div', 'ctl-row'); selRow.appendChild(el('div', 'ctl-label', 'Setting'));
    var sw = el('div', 'ctl-widget'); sw.appendChild(select); selRow.appendChild(sw);
    host.appendChild(selRow);

    var curRow = el('div', 'ctl-row'); curRow.appendChild(el('div', 'ctl-label', 'Current on device'));
    var curWidget = el('div', 'ctl-widget');
    var curVal = el('span', 'chip-status', 'reading…');
    var refreshBtn = el('button', 'chip-status', 'Refresh ⟳');
    curWidget.appendChild(curVal); curWidget.appendChild(document.createTextNode(' ')); curWidget.appendChild(refreshBtn);
    curRow.appendChild(curWidget);
    host.appendChild(curRow);

    var valueHost = el('div', ''); host.appendChild(valueHost);
    var pv = el('div', 'preview-box'); var previewEl = el('span', ''); pv.appendChild(previewEl); host.appendChild(pv);
    var chip = el('span', 'chip-status', '—');
    var sendRow = el('div', 'send-row'); var sendBtn = el('button', 'send-btn', 'Send');
    sendRow.appendChild(sendBtn); sendRow.appendChild(chip); host.appendChild(sendRow);
    var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—')); host.appendChild(resultBox);

    function currentIdx() { return parseInt(select.value, 10); }
    function formatCur(idx, val) {
      var row = byIdx[idx];
      if (row[2] === 'switch') return val ? 'ON' : 'OFF';
      if (row[2] === 'select') return val + '  ' + (row[7][val] != null ? row[7][val] : val);
      return String(val);
    }

    var valueCtrl = null;
    function rebuildValueControl(initial) {
      clear(valueHost);
      var idx = currentIdx(), row = byIdx[idx];
      var cur = initial != null ? initial : row[3];
      var wrap = el('div', 'ctl-row'); wrap.appendChild(el('div', 'ctl-label', row[6]));
      var widget = el('div', 'ctl-widget'); wrap.appendChild(widget);
      if (row[2] === 'switch') {
        var btn = el('button', 'ctl-toggle', cur ? 'ON' : 'OFF'); var checked = !!cur;
        if (checked) addClass(btn, 'on');
        btn.addEventListener('click', function () { checked = !checked; if (checked) addClass(btn, 'on'); else removeClass(btn, 'on'); btn.textContent = checked ? 'ON' : 'OFF'; updatePv(); });
        widget.appendChild(btn);
        valueCtrl = { get: function () { return checked ? 1 : 0; }, setFromDevice: function (v) { checked = !!v; if (checked) addClass(btn, 'on'); else removeClass(btn, 'on'); btn.textContent = checked ? 'ON' : 'OFF'; } };
      } else if (row[2] === 'select') {
        var sel = document.createElement('select'); sel.className = 'ctl-select';
        row[7].forEach(function (name, i) { sel.appendChild(new Option(i + '  ' + name, String(i))); });
        sel.value = String(cur); sel.addEventListener('change', updatePv);
        widget.appendChild(sel);
        valueCtrl = { get: function () { return parseInt(sel.value, 10); }, setFromDevice: function (v) { sel.value = String(v); } };
      } else {
        var rw = el('div', 'ctl-range');
        var input = document.createElement('input'); input.type = 'range'; input.min = row[4]; input.max = row[5]; input.value = cur;
        var vl = el('span', 'ctl-range-value', String(cur));
        input.addEventListener('input', function () { vl.textContent = input.value; updatePv(); });
        rw.appendChild(input); rw.appendChild(vl); widget.appendChild(rw);
        valueCtrl = { get: function () { return parseInt(input.value, 10); }, setFromDevice: function (v) { input.value = v; vl.textContent = String(v); } };
      }
      valueHost.appendChild(wrap);
      updatePv();
    }
    function updatePv() { previewEl.textContent = 'SETTINGS_WRITE  id=' + currentIdx() + ' val=' + valueCtrl.get(); }

    var readSpec = { id: '__setread', opcode: 'SETTINGS_READ_ONE', transport: 'udp', ack: false };
    function fetchCurrent() {
      var idx = currentIdx();
      curVal.textContent = 'reading…';
      Engine.send(deviceKey, readSpec, Proto.packSettingsReadOne(idx), {}, function (result) {
        if (idx !== currentIdx()) return; // setting changed while this read was in flight
        var entry = (result.entries || []).filter(function (e) { return e.parsed.ok && e.parsed.opcodeName === 'TELEM_SETTINGS_ONE'; })[0];
        if (!entry) { curVal.textContent = 'read failed (' + result.ackKind + ')'; return; }
        var d = Proto.unpackTelemSettingsOne(entry.parsed.payload);
        if (d.id !== idx) { curVal.textContent = 'read failed (index mismatch)'; return; }
        curVal.textContent = formatCur(idx, d.value);
        if (valueCtrl) { valueCtrl.setFromDevice(d.value); updatePv(); }
      });
    }
    refreshBtn.addEventListener('click', fetchCurrent);
    select.addEventListener('change', function () { rebuildValueControl(null); fetchCurrent(); });
    rebuildValueControl(null);
    fetchCurrent();

    var writeSpec = { id: '__setwrite', opcode: 'SETTINGS_WRITE', transport: 'udp' };
    sendBtn.addEventListener('click', function () {
      var idx = currentIdx(), val = valueCtrl.get();
      chip.textContent = 'sending…';
      Engine.send(deviceKey, writeSpec, byteStr([idx, val]), {}, function (result) {
        chip.textContent = result.ackKind;
        ResultView.render(resultBox, {}, result, {});
        window.setTimeout(fetchCurrent, 800);
      });
    });
  }

  /* --------------------------------------------------------- activity panel */
  function openActivity() {
    addClass($('activity-panel'), 'open');
    state.unread = 0; updateBadge();
    renderActivityLog();
  }
  function closeActivity() { removeClass($('activity-panel'), 'open'); }
  function renderActivityLog() {
    var host = $('activity-log'); clear(host);
    activityLines.forEach(appendActivityLine);
  }
  function appendActivityLine(line) {
    var host = $('activity-log');
    var row = el('div', 'log-line ' + (line.cls || 'sys'));
    row.appendChild(el('span', 'log-ts', line.ts));
    row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
    host.appendChild(row);
    host.scrollTop = host.scrollHeight;
  }

  /* --------------------------------------------------------------- raw box */
  // Raw box now sends a raw HEX PAYLOAD under a chosen opcode name, not raw
  // ASCII text - the wire format is binary now, so a free-text field alone
  // can't express a frame. "envelope" here means "wrap with a real v1 frame
  // header/CRC" (checked) vs. "fire the hex bytes onto the wire completely
  // unwrapped" (unchecked) - useful for testing malformed-frame handling.
  function renderRawTransport() {
    var device = Engine.devices[state.activeDevice];
    var sel = $('raw-transport'); clear(sel);
    sel.appendChild(new Option('UDP :' + device.udpPort, 'udp'));
    if (device.hasConsole) sel.appendChild(new Option('Telnet :' + device.telnetPort, 'telnet'));
    var opSel = $('raw-opcode');
    if (opSel) {
      clear(opSel);
      var names = []; for (var k in Proto.Opcode) if (Proto.Opcode.hasOwnProperty(k)) names.push(k);
      names.sort();
      names.forEach(function (n) { opSel.appendChild(new Option(n + ' (0x' + hexb(Proto.Opcode[n], 2) + ')', n)); });
    }
  }
  function hexToByteStr(hex) {
    hex = (hex || '').replace(/[^0-9A-Fa-f]/g, '');
    var s = '';
    for (var i = 0; i + 1 < hex.length; i += 2) s += String.fromCharCode(parseInt(hex.substr(i, 2), 16));
    return s;
  }
  function sendRaw() {
    var deviceKey = state.activeDevice, device = Engine.devices[deviceKey];
    var payloadHex = $('raw-payload').value;
    var transport = $('raw-transport').value;
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';
    if (transport === 'udp') {
      var envelope = $('raw-envelope').checked;
      var opcodeName = $('raw-opcode') ? $('raw-opcode').value : 'HELLO';
      var payload = hexToByteStr(payloadHex);
      var wire;
      if (envelope) {
        var s = Engine.nextSeq();
        wire = Engine.buildFrame(Proto.Opcode[opcodeName], s, Proto.Flag.ACK_REQ, payload);
        Engine.logAppend(tag, 'send', 'UDP(raw) > ' + device.ip + ' [' + opcodeName + '] seq=' + s + ' ' + Engine.toHex(payload) + ' [framed]');
      } else {
        wire = payload;
        Engine.logAppend(tag, 'send', 'UDP(raw) > ' + device.ip + ' ' + Engine.toHex(payload) + ' [unframed bytes]');
      }
      Net.sendUdp(device.ip, device.udpPort, wire, device.timeoutMs, function (result) {
        if (result.status === 'OK') {
          for (var i = 0; i < result.replies.length; i++) Engine.logAppend(tag, 'recv', 'UDP(raw) < ' + Engine.describeFrame(Engine.parseFrame(result.replies[i])));
        } else {
          Engine.logAppend(tag, 'err', 'UDP(raw) < ' + result.status + (result.message ? (': ' + result.message) : ''));
        }
      });
    } else {
      var text = $('raw-payload').value;
      Engine.logAppend(tag, 'send', 'TCP(raw) > ' + device.ip + ':' + device.telnetPort + ' ' + text);
      Net.sendTcp(device.ip, device.telnetPort, text, device.timeoutMs + 300, function (result) {
        if (result.status === 'OK') Engine.logAppend(tag, 'recv', 'TCP(raw) < ' + (result.replies[0] || '(empty)'));
        else Engine.logAppend(tag, 'err', 'TCP(raw) < ' + result.status + (result.message ? (': ' + result.message) : ''));
      });
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
