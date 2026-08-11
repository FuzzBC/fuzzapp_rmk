/* app.js - shared UI behaviour for the TestMode HTA mockups.
 *
 * This is a MOCKUP: there is no real UDP/TCP socket here (a browser/HTA
 * can't open one directly - see the README). Every "send" instead runs
 * against an in-memory fake device (fakeState below) that remembers mode,
 * colour, parfum timer, settings, etc, so reading a value back after
 * writing it behaves the same way the real console does - close enough to
 * click around and evaluate the *interaction design*, which is the point
 * of this round. Written in plain ES5 (var/function, no arrow fns/const/
 * template strings) on purpose: the real .hta build runs its JScript
 * inside Trident/MSHTML (mshta.exe), not Chrome, so keeping this dialect
 * now means far less rewriting later.
 */

(function () {
  'use strict';

  /* ============================================================ state */
  var state = {
    activeDevice: 'diffuser',
    seq: 0,
    selectedId: { diffuser: null, smarttv: null },
    expanded: { diffuser: {}, smarttv: {} },
    filter: '',
    keepAliveOn: false,
    keepAliveTimer: null,
    logLines: [],       // {ts, tag, cls, text} - full buffer, what SaveLog writes
    liveLogVisible: false,
    devices: {
      diffuser: { name: 'Diffuser', ip: '192.168.1.203', udpPort: 8439, telnetPort: 23, timeoutMs: 1200, hasConsole: true },
      smarttv: { name: 'SmartTV', ip: '192.168.1.202', udpPort: 8472, telnetPort: 23, timeoutMs: 1200, hasConsole: false },
    },
  };

  var fakeState = {
    diffuser: { mode: 1, strip: 1, parfumMin: 0, usageMin: 128, avgMin: 96, refillCount: 3, totalRefills: 37,
                history: [45, 52, 38, 60, 41, 30, 55], effect: 0, color1: 'FFFFFF', color2: '000000' },
    smarttv: { tvOn: false, motion: 2, udpraw: false, ambient: 0, difSummary: 1, testMode: 0, ledsOn: true,
               lux: 2, brightness: 80, color1: 'FF8040', color2: '2040FF', mask: repeatChar('0', 61), settings: {} },
  };
  EE_SETTINGS_TABLE.forEach(function (row) { fakeState.smarttv.settings[row[0]] = row[3]; });
  for (var _ri = 45; _ri <= 49; _ri++) if (!(_ri in fakeState.smarttv.settings)) fakeState.smarttv.settings[_ri] = 0;

  function repeatChar(ch, n) { var s = ''; for (var i = 0; i < n; i++) s += ch; return s; }

  var DIF = fakeState.diffuser;
  var TV = fakeState.smarttv;

  var TESTMODE_NAMES = ['none', 'TV on (forced)', 'TV off (forced)', 'UDPRAW stream (sim)',
    'motion, front (sim)', 'motion, bed (sim)', 'diffuser test', 'lux test'];
  var MOTION_STATUS_NAMES = ['auto-off', 'off', 'idle (armed)', 'triggered, front', 'triggered, bed'];
  var AMBIENT_SUMMARY_NAMES = { 0: 'off', 1: 'on', 2: 'ready' };
  var DIF_SUMMARY_NAMES = { 0: 'off', 1: 'on', 2: 'out of water', 3: 'no response', 4: 'parfum running' };

  /* ======================================================= tiny utils */
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }
  function clear(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function pad2(n) { n = String(n); return n.length < 2 ? '0' + n : n; }
  function nowStamp() {
    var d = new Date();
    return d.getFullYear() + '-' + pad2(d.getMonth() + 1) + '-' + pad2(d.getDate()) + ' ' +
      pad2(d.getHours()) + ':' + pad2(d.getMinutes()) + ':' + pad2(d.getSeconds()) + '.' +
      (d.getMilliseconds() < 100 ? (d.getMilliseconds() < 10 ? '00' : '0') : '') + d.getMilliseconds();
  }
  function fmtDurationMin(total) {
    if (total < 60) return total + 'm';
    var h = Math.floor(total / 60), m = total % 60;
    return m === 0 ? h + 'h' : h + 'h ' + m + 'm';
  }
  function rand(a, b) { return a + Math.random() * (b - a); }

  /* ========================================================== logging */
  function logAppend(tag, cls, text) {
    var ts = nowStamp();
    state.logLines.push('[' + ts + '] [' + tag + '] ' + text);
    if (state.liveLogVisible) appendLogDom(ts, tag, cls, text);
  }
  function logSys(text) { logAppend('SYS', 'log-tag-sys', text); }
  function panelLog(tag, cls, text) { logAppend(tag, cls === 'log-send' ? 'log-send' : cls, text); }

  function appendLogDom(ts, tag, cls, text) {
    var box = $('logbox');
    if (!box) return;
    var line = el('div', 'log-line');
    var tsSpan = el('span', 'log-ts', ts + '  ');
    var tagCls = tag === 'DIF' ? 'log-tag-dif' : (tag === 'TV' ? 'log-tag-tv' : 'log-tag-sys');
    var tagSpan = el('span', tagCls, '[' + tag + '] ');
    var bodySpan = el('span', cls, text);
    line.appendChild(tsSpan); line.appendChild(tagSpan); line.appendChild(bodySpan);
    box.appendChild(line);
    box.scrollTop = box.scrollHeight;
  }

  function rebuildLogDom() {
    var box = $('logbox');
    if (!box) return;
    clear(box);
    // Re-render from the buffer in a simplified single-span-per-line form
    // (colour classification of stored lines is lossy on purpose - buffer
    // stays the source of truth for SaveLog, this is just a redraw).
    for (var i = 0; i < state.logLines.length; i++) {
      var line = el('div', 'log-line', state.logLines[i]);
      box.appendChild(line);
    }
    box.scrollTop = box.scrollHeight;
  }

  function toggleLiveLog() {
    state.liveLogVisible = !state.liveLogVisible;
    var panel = $('logpanel'), btn = $('livelog-btn');
    if (state.liveLogVisible) {
      panel.classList.add('visible');
      btn.textContent = 'Live log: on';
      rebuildLogDom();
    } else {
      panel.classList.remove('visible');
      btn.textContent = 'Live log: off';
    }
  }

  function clearLogPanel() {
    var box = $('logbox');
    if (box) clear(box);
  }

  function saveLog() {
    if (!state.logLines.length) { logSys('No log entries to save'); return; }
    var header = repeatChar('=', 66) + '\nFuZz TestMode (HTA mockup) log - saved ' + nowStamp() + '\n' +
      'Diffuser default: ' + state.devices.diffuser.ip + ':' + state.devices.diffuser.udpPort + ' (udp) / :' + state.devices.diffuser.telnetPort + ' (telnet)\n' +
      'SmartTV  default: ' + state.devices.smarttv.ip + ':' + state.devices.smarttv.udpPort + ' (udp)\n' +
      repeatChar('=', 66) + '\n';
    var text = header + state.logLines.join('\n') + '\n';
    try {
      var blob = new Blob([text], { type: 'text/plain' });
      var url = URL.createObjectURL(blob);
      var a = document.createElement('a');
      a.href = url;
      a.download = 'TestMode-mockup-' + Date.now() + '.log';
      document.body.appendChild(a); a.click(); document.body.removeChild(a);
      setTimeout(function () { URL.revokeObjectURL(url); }, 2000);
      logSys('Log downloaded (' + state.logLines.length + ' lines)');
    } catch (e) { logSys('Save failed: ' + e.message); }
  }

  /* ---------------------------------------------------------- keep-alive */
  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    var dot = $('keepalive-dot'), label = $('keepalive-label'), btn = $('keepalive-btn');
    if (state.keepAliveOn) {
      btn.textContent = 'Stop keep-alive';
      label.textContent = 'Keep-alive: on';
      dot.classList.add('on');
      logSys('keep-alive started (pings the active device every 5s)');
      keepAliveTick();
      state.keepAliveTimer = setInterval(keepAliveTick, 5000);
    } else {
      btn.textContent = 'Start keep-alive';
      label.textContent = 'Keep-alive: off';
      dot.classList.remove('on');
      clearInterval(state.keepAliveTimer);
      logSys('keep-alive stopped');
    }
  }
  function keepAliveTick() {
    var dk = state.activeDevice, tag = dk === 'diffuser' ? 'DIF' : 'TV';
    var payload = dk === 'smarttv' ? 'k' : 'Dc';
    var wrapped = dk === 'smarttv' ? payload : ('#' + hexb(nextSeq(), 2) + payload);
    panelLog(tag, 'log-send', 'keep-alive ' + wrapped + ' -> OK');
    if (state.activeDevice === dk) $('keepalive-label').textContent = 'Keep-alive: on (just now)';
  }

  function nextSeq() { var s = state.seq; state.seq = (state.seq + 1) % 256; return s; }

  /* ================================================================ */
  /* ---------------------------------------------------------- topbar */
  function initTopbar() {
    $('savelog-btn').addEventListener('click', saveLog);
    $('livelog-btn').addEventListener('click', toggleLiveLog);
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('logclear-btn').addEventListener('click', clearLogPanel);
    $('tab-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('tab-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
  }

  /* -------------------------------------------------------- conn bar */
  function initConnBar() {
    ['ip', 'udp', 'telnet', 'timeout'].forEach(function (f) {
      var input = $('conn-' + f);
      input.addEventListener('change', function () { onConnChange(f, input.value); });
    });
  }
  function onConnChange(field, raw) {
    var device = state.devices[state.activeDevice];
    var map = { ip: 'ip', udp: 'udpPort', telnet: 'telnetPort', timeout: 'timeoutMs' };
    var key = map[field];
    var value = (field === 'ip') ? raw : (parseInt(raw, 10) || device[key]);
    device[key] = value;
    logSys('connection: ' + state.activeDevice + ' ' + key + ' -> ' + value);
  }
  function renderConnBar() {
    var d = state.devices[state.activeDevice];
    $('conn-ip').value = d.ip;
    $('conn-udp').value = d.udpPort;
    $('conn-telnet').value = d.telnetPort;
    $('conn-timeout').value = d.timeoutMs;
    if (d.hasConsole) {
      $('conn-telnet-wrap').classList.remove('hidden');
      $('conn-note').textContent = '';
    } else {
      $('conn-telnet-wrap').classList.add('hidden');
      $('conn-note').textContent = 'UDP only on this firmware (port ' + d.udpPort + ') - no Serial/Telnet console';
    }
    var rawTransport = $('raw-transport');
    clear(rawTransport);
    rawTransport.appendChild(new Option('UDP :' + d.udpPort, 'udp'));
    if (d.hasConsole) rawTransport.appendChild(new Option('Telnet :' + d.telnetPort, 'telnet'));
  }

  /* ------------------------------------------------------- sidebar */
  function initSidebarFilter() {
    $('sidebar-filter-input').addEventListener('input', function (e) {
      state.filter = e.target.value.trim().toLowerCase();
      renderSidebar();
    });
  }

  function renderSidebar() {
    var host = $('sidebar-scroll');
    clear(host);
    var deviceKey = state.activeDevice;
    var list = COMMAND_TABLES[deviceKey];
    var query = state.filter;
    var expanded = state.expanded[deviceKey];

    var sections = [], bySection = {};
    list.forEach(function (spec) {
      var sec = spec.section;
      if (sec.toLowerCase().indexOf('test mode') !== -1) return; // lives in Quick Test panel
      if (!bySection[sec]) { bySection[sec] = []; sections.push(sec); }
      bySection[sec].push(spec);
    });

    var firstId = null, anyMatch = false;
    sections.forEach(function (sec) {
      var matches = bySection[sec].filter(function (s) {
        return !query || s.id.toLowerCase().indexOf(query) !== -1 || s.label.toLowerCase().indexOf(query) !== -1 ||
          (s.name || '').toLowerCase().indexOf(query) !== -1;
      });
      if (query && !matches.length) return;
      anyMatch = true;
      var isOpen = !!query || (expanded[sec] !== false);
      var meta = categoryMeta(sec);

      var header = el('div', 'sec-header');
      header.appendChild(el('span', 'sec-chevron', isOpen ? '▾' : '▸'));
      var iconSpan = el('span', 'sec-icon', meta.icon); iconSpan.style.color = 'var(--accent-' + meta.color + ')';
      header.appendChild(iconSpan);
      var titleSpan = el('span', 'sec-title', meta.title); titleSpan.style.color = 'var(--accent-' + meta.color + ')';
      header.appendChild(titleSpan);
      header.appendChild(el('span', 'sec-count', String(matches.length)));
      header.addEventListener('click', function () {
        expanded[sec] = !(expanded[sec] !== false);
        renderSidebar();
      });
      host.appendChild(header);

      if (!isOpen) return;
      matches.forEach(function (spec) {
        if (firstId === null) firstId = spec.id;
        var row = el('div', 'cmd-row');
        if (state.selectedId[deviceKey] === spec.id) row.classList.add('selected');
        row.appendChild(el('span', 'cmd-name', spec.name || spec.label));
        row.appendChild(el('span', 'cmd-label', spec.label));
        row.addEventListener('click', function () { selectCommand(deviceKey, spec); });
        host.appendChild(row);
      });
    });

    if (!anyMatch) {
      host.appendChild(el('div', 'sidebar-empty', 'no commands match "' + query + '"'));
    }
    if (!state.selectedId[deviceKey]) state.selectedId[deviceKey] = firstId;
  }

  function selectCommand(deviceKey, spec) {
    state.selectedId[deviceKey] = spec.id;
    renderSidebar();
    if (deviceKey === state.activeDevice) renderDetail(spec, deviceKey);
  }

  /* -------------------------------------------------------- switching */
  function switchDevice(key) {
    state.activeDevice = key;
    $('tab-diffuser').classList.toggle('active', key === 'diffuser');
    $('tab-smarttv').classList.toggle('active', key === 'smarttv');
    state.filter = ''; $('sidebar-filter-input').value = '';
    renderSidebar();
    renderConnBar();

    var qt = $('quicktest');
    if (key === 'diffuser') {
      qt.classList.add('hidden');
    } else {
      qt.classList.remove('hidden');
      renderQuickTest(key);
    }

    var sel = state.selectedId[key];
    var table = COMMAND_TABLES[key];
    var spec = table.filter(function (s) { return s.id === sel; })[0] || table[0];
    if (spec) { state.selectedId[key] = spec.id; renderDetail(spec, key); renderSidebar(); }
  }

  /* ============================================================ param controls */
  var CHIP_ON_WORDS = { on: 1, true: 1, ok: 1, active: 1 };
  var CHIP_OFF_WORDS = { off: 1, false: 1, none: 1, unknown: 1, error: 1, fail: 1 };

  function buildParamControls(container, spec, onChange) {
    var controls = {};
    var params = spec.params || [];
    params.forEach(function (p) {
      var row = el('div', 'param-row');
      row.appendChild(el('div', 'param-label', p.label));
      var wrap = el('div', 'param-control');
      row.appendChild(wrap);
      container.appendChild(row);

      if (p.type === 'enum') {
        var select = document.createElement('select');
        p.options.forEach(function (opt) { select.appendChild(new Option(opt[1], opt[0])); });
        select.value = p.default;
        select.addEventListener('change', onChange);
        wrap.appendChild(select);
        controls[p.key] = { kind: 'enum', get: function () { return select.value; } };
      } else if (p.type === 'range') {
        var rw = el('div', 'range-wrap');
        rw.appendChild(el('span', 'range-min', String(p.min)));
        var input = document.createElement('input');
        input.type = 'range'; input.min = p.min; input.max = p.max; input.value = p.default;
        var valueLabel = el('span', 'range-value', String(p.default));
        input.addEventListener('input', function () { valueLabel.textContent = input.value; onChange(); });
        rw.appendChild(input);
        rw.appendChild(valueLabel);
        rw.appendChild(el('span', 'range-max', String(p.max)));
        wrap.appendChild(rw);
        controls[p.key] = { kind: 'range', get: function () { return parseInt(input.value, 10); } };
      } else if (p.type === 'number') {
        var num = document.createElement('input');
        num.type = 'number'; num.value = p.default; num.min = p.min; num.max = p.max;
        num.addEventListener('input', onChange);
        wrap.appendChild(num);
        controls[p.key] = { kind: 'number', get: function () { var n = parseInt(num.value, 10); return isNaN(n) ? (p.min || 0) : n; } };
      } else if (p.type === 'checkbox') {
        var btn = el('button', 'toggle-btn', p.default ? 'ON' : 'OFF');
        var checked = !!p.default;
        if (checked) btn.classList.add('on');
        btn.type = 'button';
        btn.addEventListener('click', function () {
          checked = !checked;
          btn.classList.toggle('on', checked);
          btn.textContent = checked ? 'ON' : 'OFF';
          onChange();
        });
        wrap.appendChild(btn);
        controls[p.key] = { kind: 'checkbox', get: function () { return checked; } };
      } else if (p.type === 'hexcolor') {
        var hw = el('div', 'hexcolor-wrap');
        var colorInput = document.createElement('input');
        colorInput.type = 'color'; colorInput.className = 'swatch';
        colorInput.value = '#' + p.default;
        var textInput = document.createElement('input');
        textInput.type = 'text'; textInput.value = p.default.toUpperCase(); textInput.maxLength = 6;
        colorInput.addEventListener('input', function () { textInput.value = colorInput.value.slice(1).toUpperCase(); onChange(); });
        textInput.addEventListener('input', function () {
          var v = textInput.value.toUpperCase().replace(/[^0-9A-F]/g, '').slice(0, 6);
          while (v.length < 6) v += '0';
          textInput.value = v;
          colorInput.value = '#' + v;
          onChange();
        });
        hw.appendChild(colorInput); hw.appendChild(textInput);
        wrap.appendChild(hw);
        controls[p.key] = { kind: 'hexcolor', get: function () { return textInput.value.toUpperCase(); }, widget: wrap };
      } else if (p.type === 'ledmask') {
        var control = buildLedmaskControl(wrap, p, onChange);
        controls[p.key] = { kind: 'ledmask', get: control.get };
      } else {
        var txt = document.createElement('input');
        txt.type = 'text'; txt.value = p.default != null ? String(p.default) : '';
        txt.addEventListener('input', onChange);
        wrap.appendChild(txt);
        controls[p.key] = { kind: 'text', get: function () { return txt.value; } };
      }
    });

    // enable_when dependency wiring
    params.forEach(function (p) {
      if (!p.enable_when || !controls[p.enable_when]) return;
      var dep = controls[p.enable_when];
      var target = controls[p.key];
      if (!target || !target.widget) return;
      var sync = function () {
        var on = dep.get();
        target.widget.style.opacity = on ? '1' : '.4';
        target.widget.style.pointerEvents = on ? '' : 'none';
      };
      var row = target.widget.closest ? target.widget.closest('.param-row') : null;
      if (row) row.addEventListener('click', sync, true);
      sync();
      // also re-check whenever the dependency control changes (checkbox click above already calls onChange -> caller re-renders preview; hook a light poll)
      setInterval(sync, 300);
    });

    return controls;
  }

  function buildLedmaskControl(wrap, p, onChange) {
    var bits = (p.default || repeatChar('0', 61)).split('');
    var cells = [];
    var ledWrap = el('div', 'ledmask-wrap');
    LED_ZONES.forEach(function (zone) {
      var name = zone[0], start = zone[1], count = zone[2], color = zone[3];
      var zoneEl = el('div', 'ledmask-zone');
      zoneEl.appendChild(el('div', 'ledmask-zone-label', name));
      var grid = el('div', 'ledmask-grid');
      for (var k = 0; k < count; k++) {
        (function (idx) {
          var cell = document.createElement('button');
          cell.type = 'button'; cell.className = 'ledmask-cell';
          cell.style.background = bits[idx] === '1' ? 'var(--accent-' + color + ')' : '';
          cell.addEventListener('click', function () {
            bits[idx] = bits[idx] === '1' ? '0' : '1';
            cell.style.background = bits[idx] === '1' ? 'var(--accent-' + color + ')' : '';
            onChange();
          });
          cells.push(cell);
          grid.appendChild(cell);
        })(start + k);
      }
      zoneEl.appendChild(grid);
      ledWrap.appendChild(zoneEl);
    });
    var btns = el('div', 'ledmask-btns');
    var allBtn = el('button', 'btn btn-sm', 'All'); allBtn.type = 'button';
    var noneBtn = el('button', 'btn btn-sm', 'None'); noneBtn.type = 'button';
    allBtn.addEventListener('click', function () { setAllBits('1'); });
    noneBtn.addEventListener('click', function () { setAllBits('0'); });
    function setAllBits(ch) {
      for (var i = 0; i < bits.length; i++) bits[i] = ch;
      redrawAll();
      onChange();
    }
    function redrawAll() {
      var i = 0;
      LED_ZONES.forEach(function (zone) {
        var start = zone[1], count = zone[2], color = zone[3];
        for (var k = 0; k < count; k++, i++) {
          cells[i].style.background = bits[start + k] === '1' ? 'var(--accent-' + color + ')' : '';
        }
      });
    }
    btns.appendChild(allBtn); btns.appendChild(noneBtn);
    ledWrap.appendChild(btns);
    wrap.appendChild(ledWrap);
    return { get: function () { return bits.join(''); } };
  }

  function collectParamValues(spec, controls) {
    var vals = {};
    (spec.params || []).forEach(function (p) { vals[p.key] = controls[p.key].get(); });
    return vals;
  }

  function stepRow(parent, number, title) {
    var step = el('div', 'step');
    var head = el('div', 'step-head');
    head.appendChild(el('div', 'step-badge', String(number)));
    head.appendChild(el('div', 'step-title', title));
    step.appendChild(head);
    var body = el('div', 'step-body');
    step.appendChild(body);
    parent.appendChild(step);
    return body;
  }

  function confirmOk(spec) {
    if (!spec.confirm) return true;
    return window.confirm(spec.confirm);
  }

  /* ================================================================ detail panel */
  function renderDetail(spec, deviceKey) {
    var host = $('detail');
    clear(host);
    host.scrollTop = 0;

    if (spec.custom_panel === 'diffuser_history') { renderHistoryManager(spec, deviceKey, host); return; }
    if (spec.custom_panel === 'settings_write') { renderSettingsWrite(spec, deviceKey, host); return; }

    var pad = el('div', 'detail-pad');
    host.appendChild(pad);

    var meta = categoryMeta(spec.section);
    var head = el('div', 'detail-head');
    var iconSpan = el('span', 'detail-icon', meta.icon); iconSpan.style.color = 'var(--accent-' + meta.color + ')';
    head.appendChild(iconSpan);
    head.appendChild(el('span', 'detail-name', spec.name || spec.label));
    head.appendChild(el('span', 'badge', spec.label));
    head.appendChild(el('span', 'badge badge-transport', spec.transport === 'udp' ? 'UDP' : 'TCP:23'));
    pad.appendChild(head);
    pad.appendChild(el('div', 'detail-desc', spec.desc || ''));

    var controls = {}, stepN = 1;
    var previewEl;

    function updatePreview() {
      if (spec.direct_buttons || !previewEl) return;
      try {
        var vals = collectParamValues(spec, controls);
        previewEl.textContent = spec.build(vals);
      } catch (e) { previewEl.textContent = '(invalid parameters)'; }
    }

    if (spec.params && spec.params.length) {
      var body1 = stepRow(pad, stepN++, 'Configure');
      buildParamControls_intoDom(body1, spec, controls, updatePreview);
    }

    var respArea, chip;

    if (spec.direct_buttons) {
      var body2 = stepRow(pad, stepN++, 'Choose an option');
      var btnRow = el('div', 'direct-btn-row');
      chip = el('span', 'chip', '-');
      spec.direct_buttons.forEach(function (opt) {
        var b = el('button', 'btn', opt[1]);
        b.addEventListener('click', function () {
          if (!confirmOk(spec)) return;
          onSend(spec, deviceKey, opt[0], chip, respArea, {});
        });
        btnRow.appendChild(b);
      });
      btnRow.appendChild(chip);
      body2.appendChild(btnRow);
    } else {
      var body3 = stepRow(pad, stepN++, 'Review and send');
      var previewRow = el('div', 'preview-row');
      previewRow.appendChild(el('span', 'preview-label', 'PAYLOAD'));
      previewEl = el('span', 'preview-payload', '');
      previewRow.appendChild(previewEl);
      body3.appendChild(previewRow);
      updatePreview();

      var sendRow = el('div', 'send-row');
      var sendBtn = el('button', 'btn btn-green', 'Send');
      chip = el('span', 'chip', '-');
      sendBtn.addEventListener('click', function () {
        if (!confirmOk(spec)) return;
        var vals = collectParamValues(spec, controls);
        var payload = spec.build(vals);
        onSend(spec, deviceKey, payload, chip, respArea, vals);
      });
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip);
      body3.appendChild(sendRow);
    }

    var body4 = stepRow(pad, stepN++, 'Result');
    respArea = el('div', 'result-card');
    respArea.appendChild(el('div', 'result-empty', '-'));
    body4.appendChild(respArea);
  }

  // buildParamControls() appends rows straight to a container; small
  // adapter so renderDetail's local `controls` object gets filled in place.
  function buildParamControls_intoDom(container, spec, controlsOut, onChange) {
    var built = buildParamControls(container, spec, onChange);
    Object.keys(built).forEach(function (k) { controlsOut[k] = built[k]; });
  }

  /* ---------------------------------------------------- sending / simulation */
  function onSend(spec, deviceKey, payload, chip, respArea, sentVals) {
    chip.className = 'chip'; chip.style.background = 'var(--accent-blue-bg)'; chip.style.color = 'var(--accent-blue)'; chip.textContent = 'sending';
    clear(respArea);
    respArea.appendChild(el('div', 'result-empty', '...'));
    var device = state.devices[deviceKey];
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';

    if (spec.transport === 'console') {
      var replyText = simulateConsole(spec, sentVals, payload);
      var delay = rand(120, 320);
      setTimeout(function () {
        panelLog(tag, 'log-send', 'TCP > ' + device.ip + ' ' + payload);
        panelLog(tag, 'log-recv', 'TCP < ' + replyText.replace(/\n/g, ' \\n '));
        var kind = replyText ? 'ok' : 'timeout';
        setChip(chip, kind, replyText ? 'replied' : 'no reply');
        clear(respArea);
        renderAckBanner(respArea, kind);
        var box = el('div', 'result-empty', replyText || '(empty)');
        box.style.whiteSpace = 'pre-wrap';
        respArea.appendChild(box);
      }, delay);
      return;
    }

    var envelope = spec.envelope !== false;
    var seq = -1, wrapped = payload;
    if (envelope) { seq = nextSeq(); wrapped = '#' + hexb(seq, 2) + payload; }

    var sim = simulateUdp(spec, sentVals, payload);
    var delay = sim.delayMs || rand(150, 420);

    setTimeout(function () {
      panelLog(tag, 'log-send', 'UDP > ' + device.ip + ' ' + wrapped);

      var ackKind = envelope ? (sim.ackKind || 'ok') : 'sent';
      if (envelope) {
        var ackCode = { ok: 0, clamped: 1, rejected: 2, blocked: 3, locked: 4, nowater: 5, unsupported: 6 }[ackKind];
        panelLog(tag, 'log-recv', 'UDP < #' + hexb(seq, 2) + (ackCode != null ? ackCode.toString(16).toUpperCase() : '0'));
      }
      var entries = sim.entries || [];
      entries.forEach(function (e) { panelLog(tag, 'log-recv', 'UDP < ' + e.raw); });

      setChip(chip, ackKind, envelope ? ackKind : 'sent (no envelope)');

      var displayEntries = entries.map(function (e) { return [e.raw, e.desc || describeReply(e.raw), e.raw]; });
      var dataRaw = entries.length ? entries[0].raw : '';
      renderResponse(respArea, spec, dataRaw, sentVals, displayEntries, ackKind);
    }, delay);
  }

  function setChip(chip, kind, text) {
    var pair = chipColors(kind);
    chip.style.background = pair[0]; chip.style.color = pair[1];
    chip.textContent = text;
  }
  function chipColors(kind) {
    var map = {
      ok: ['var(--chip-on-bg)', 'var(--chip-on-fg)'],
      sent: ['var(--accent-blue-bg)', 'var(--accent-blue)'],
      clamped: ['var(--chip-warn-bg)', 'var(--chip-warn-fg)'],
      locked: ['var(--chip-warn-bg)', 'var(--chip-warn-fg)'],
      rejected: ['var(--chip-off-bg)', 'var(--chip-off-fg)'],
      blocked: ['var(--chip-off-bg)', 'var(--chip-off-fg)'],
      nowater: ['var(--chip-off-bg)', 'var(--chip-off-fg)'],
      error: ['var(--chip-off-bg)', 'var(--chip-off-fg)'],
      timeout: ['var(--bg-btn)', 'var(--text-muted)'],
      unsupported: ['var(--bg-btn)', 'var(--text-muted)'],
      unknown: ['var(--bg-btn)', 'var(--text-muted)'],
      none: ['var(--bg-btn)', 'var(--text-muted)'],
    };
    return map[kind] || map.none;
  }

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
  function renderAckBanner(container, kind) {
    var info = ACK_INFO[kind];
    if (!info) return;
    var pair = chipColors(kind);
    var banner = el('div', 'ack-banner');
    banner.style.background = pair[0]; banner.style.color = pair[1]; banner.style.borderColor = pair[1];
    banner.appendChild(el('span', 'ack-icon', info[0]));
    var col = el('div', '');
    col.appendChild(el('div', 'ack-head', info[1]));
    if (info[2]) col.appendChild(el('div', 'ack-meaning', info[2]));
    banner.appendChild(col);
    container.appendChild(banner);
  }

  function renderResponse(respArea, spec, rawText, sentVals, allEntries, ackKind) {
    clear(respArea);
    if (ackKind) renderAckBanner(respArea, ackKind);
    if (allEntries && allEntries.length > 1) renderReplyList(respArea, allEntries);

    var render = spec.render || {};
    if (render.type === 'status' && rawText) {
      var fields = parseStatusFields(rawText);
      if (fields) { renderStatusGrid(respArea, fields, rawText); return; }
    }
    if (render.type === 'color_params') {
      var keys = (render.always || []).slice();
      if (render.conditional && sentVals[render.conditional[0]]) keys.push(render.conditional[1]);
      renderColorSwatches(respArea, keys.filter(function (k) { return sentVals[k] != null; }).map(function (k) { return [k, sentVals[k]]; }));
      return;
    }
    if (render.type === 'color_reply_dual' && rawText) {
      var hexval = rawText.replace(/[^0-9A-Fa-f]/g, '').toUpperCase();
      if (hexval.length >= 12) {
        renderColorSwatches(respArea, [['colour 1', hexval.slice(0, 6)], ['colour 2', hexval.slice(6, 12)]]);
        return;
      }
    }
    if (render.type === 'settings_table' && rawText) {
      var rows = parseSettingsReply(rawText);
      if (rows) { renderSettingsTable(respArea, rows); return; }
    }

    if (allEntries && allEntries.length > 1) return; // already listed above
    if (allEntries && allEntries.length === 1) {
      var text = allEntries[0][1] || allEntries[0][0];
      respArea.appendChild(el('div', 'result-empty', text));
      return;
    }
    respArea.appendChild(el('div', 'result-empty', rawText || '-'));
  }

  function renderReplyList(respArea, entries) {
    var wrap = el('div', 'reply-list-wrap');
    var head = el('div', 'reply-list-head');
    head.appendChild(el('span', 'reply-count', entries.length + ' PACKET' + (entries.length === 1 ? '' : 'S') + ' RECEIVED'));
    wrap.appendChild(head);
    var box = el('div', 'reply-box');
    entries.forEach(function (e) {
      var line = el('div', 'reply-line', e[1] || e[0] || '(unrecognised packet)');
      box.appendChild(line);
    });
    wrap.appendChild(box);
    respArea.appendChild(wrap);
  }

  function renderStatusGrid(respArea, fields, rawText) {
    var grid = el('div', 'status-grid');
    var cellsSpec = [
      ['mode', 'M' + fields.mode + ' (' + fields.modeName + ')', 'blue'],
      ['strip', String(fields.strip), 'purple'],
      ['parfum', fields.parfumMin ? fields.parfumMin + ' min' : 'off', 'amber'],
      ['usage', fields.usageMin + ' min', 'amber'],
      ['avg cycle', fields.avgMin + ' min', 'amber'],
      ['refills', fields.refillCount + '/10 (total ' + fields.totalRefills + ')', 'green'],
    ];
    cellsSpec.forEach(function (c) {
      var cell = el('div', 'status-cell');
      cell.style.borderColor = 'var(--accent-' + c[2] + ')';
      var lab = el('div', 'status-cell-label', c[0]); lab.style.color = 'var(--accent-' + c[2] + ')';
      cell.appendChild(lab);
      cell.appendChild(el('div', 'status-cell-value', c[1]));
      grid.appendChild(cell);
    });
    respArea.appendChild(grid);

    var legend = el('div', 'legend');
    [['mode', 'blue'], ['strip', 'purple'], ['timers', 'amber'], ['counters', 'green']].forEach(function (l) {
      var item = el('div', 'legend-item');
      var sq = el('span', 'legend-sq', '■'); sq.style.color = 'var(--accent-' + l[1] + ')';
      item.appendChild(sq); item.appendChild(el('span', '', l[0]));
      legend.appendChild(item);
    });
    respArea.appendChild(legend);
    respArea.appendChild(el('div', 'raw-line', 'raw: ' + rawText));
  }

  function renderColorSwatches(respArea, pairs) {
    var row = el('div', 'swatch-row');
    pairs.forEach(function (pair) {
      var item = el('div', 'swatch-item');
      var box = el('div', 'box'); box.style.background = '#' + (pair[1] || 'FFFFFF');
      item.appendChild(box);
      item.appendChild(el('div', 'cap', pair[0] + '  #' + (pair[1] || 'FFFFFF')));
      row.appendChild(item);
    });
    respArea.appendChild(row);
  }

  function renderSettingsTable(respArea, rows) {
    var wrap = el('div', 'settings-wrap');
    var head = el('div', 'reply-count', 'SMARTTV EEPROM SETTINGS · ' + rows.length + ' / ' + TV_SETTINGS.length + ' RECEIVED');
    wrap.appendChild(head);
    var byCat = {}, order = [];
    var catColors = { TV: 'blue', MOTION: 'green', HB: 'pink', AMBILIGHT: 'purple', DIFFUSER: 'amber2', OTHER: 'muted', RESERVED: 'muted' };
    rows.forEach(function (r) {
      var name = r[1];
      var cat = name.indexOf('_') !== -1 ? name.split('_')[0] : name.split(' ')[0];
      if (!byCat[cat]) { byCat[cat] = []; order.push(cat); }
      byCat[cat].push(r);
    });
    order.forEach(function (cat) {
      var color = catColors[cat] || 'blue';
      var title = el('div', 'settings-cat-title', cat); title.style.color = 'var(--accent-' + color + ')';
      wrap.appendChild(title);
      byCat[cat].forEach(function (r) {
        var row = el('div', 'settings-row'); row.style.borderColor = 'var(--accent-' + color + ')';
        var txt = el('div', 'txt');
        txt.appendChild(document.createTextNode(hexb(r[0], 2) + '  ' + r[1]));
        var val = el('div', 'val', String(r[2]));
        row.appendChild(txt); row.appendChild(val);
        wrap.appendChild(row);
      });
    });
    respArea.appendChild(wrap);
  }

  /* -------------------------------------------------------- protocol decode */
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
    if (text.charAt(0) === '!' ) return text.slice(1);
    if (text.charAt(0) === '*' && text.length >= 6) {
      try {
        var level = parseInt(text.charAt(1), 16), source = parseInt(text.slice(2, 4), 16), body = text.slice(6).trim();
        var levelNames = { 0: 'ERROR', 1: 'WARN', 2: 'INFO', 3: 'DEBUG', 4: 'SECTION', 5: 'GAP' };
        return '[' + (levelNames[level] || 'L' + level) + '] ' + (body || '(empty)');
      } catch (e) { return null; }
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
    if (text.slice(0, 5) === 'LO ac') return text.slice(3);
    return null;
  }

  /* --------------------------------------------------------- fake sends */
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
      { raw: buildSStatusRaw() },
      { raw: 'H' + hexb(24, 2) + hexb(41, 2) },
      { raw: 'E' + hexb(TV.ledsOn ? 1 : 0, 1) },
      { raw: 'M' + hexb(TV.lux * 287, 4) },
      { raw: 'w' + hexb(58, 2) + hexb(0, 2) },
      { raw: 'f0000' },
      { raw: 'LM' + hexb(120, 2) },
      { raw: '@' + hexb(TV.testMode, 2) },
      { raw: 'LK', desc: describeFakeLK() },
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

  function simulateUdp(spec, sentVals, payload) {
    var table = state.activeDevice === 'diffuser' ? DIFFUSER_SIM : SMARTTV_SIM;
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

  /* ---------------------------------------------------- history manager */
  function renderHistoryManager(spec, deviceKey, host) {
    var pad = el('div', 'detail-pad');
    host.appendChild(pad);
    var meta = categoryMeta(spec.section);
    var head = el('div', 'detail-head');
    var iconSpan = el('span', 'detail-icon', meta.icon); iconSpan.style.color = 'var(--accent-' + meta.color + ')';
    head.appendChild(iconSpan);
    head.appendChild(el('span', 'detail-name', spec.name));
    head.appendChild(el('span', 'badge', spec.label));
    pad.appendChild(head);
    pad.appendChild(el('div', 'detail-desc', spec.desc));

    var toolbar = el('div', 'history-toolbar');
    var status = el('span', 'history-status', DIF.history.length + ' of 10 cycles');
    var refreshBtn = el('button', 'btn btn-sm', 'Refresh');
    toolbar.appendChild(status); toolbar.appendChild(refreshBtn);
    pad.appendChild(toolbar);

    var list = el('div', '');
    pad.appendChild(list);

    function renderRows() {
      clear(list);
      status.textContent = DIF.history.length + ' of 10 cycles';
      if (!DIF.history.length) { list.appendChild(el('div', 'result-empty', 'No refill cycles recorded yet.')); return; }
      DIF.history.forEach(function (mins, i) {
        var row = el('div', 'history-row');
        row.appendChild(el('span', 'num', '#' + (i + 1)));
        row.appendChild(el('span', 'dur', fmtDurationMin(mins)));
        var del = el('button', '', '✕');
        del.addEventListener('click', function () {
          if (!window.confirm('Remove entry #' + (i + 1) + ' (' + fmtDurationMin(mins) + ')? Everything after it shifts down. This can\'t be undone.')) return;
          DIF.history.splice(i, 1);
          logSys('history: removed entry, ' + DIF.history.length + ' remain');
          renderRows();
        });
        row.appendChild(del);
        list.appendChild(row);
      });
    }
    refreshBtn.addEventListener('click', renderRows);
    renderRows();
  }

  /* ---------------------------------------------------- settings write */
  function renderSettingsWrite(spec, deviceKey, host) {
    var pad = el('div', 'detail-pad');
    host.appendChild(pad);
    var meta = categoryMeta(spec.section);
    var head = el('div', 'detail-head');
    var iconSpan = el('span', 'detail-icon', meta.icon); iconSpan.style.color = 'var(--accent-' + meta.color + ')';
    head.appendChild(iconSpan);
    head.appendChild(el('span', 'detail-name', spec.name));
    head.appendChild(el('span', 'badge', spec.label));
    pad.appendChild(head);
    pad.appendChild(el('div', 'detail-desc', spec.desc));

    var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });

    var current = el('div', 'current-card');
    current.appendChild(el('span', 'lab', 'CURRENT ON DEVICE'));
    var currentVal = el('span', 'val', '–');
    current.appendChild(currentVal);
    current.appendChild(el('span', 'sp', ''));
    pad.appendChild(current);

    var body1 = stepRow(pad, 1, 'Choose a setting');
    var row1 = el('div', 'param-row');
    row1.appendChild(el('div', 'param-label', 'Setting'));
    var select = document.createElement('select');
    EE_SETTINGS_TABLE.forEach(function (row) {
      select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[1] + ': ' + row[6], String(row[0])));
    });
    row1.appendChild(select);
    body1.appendChild(row1);

    var valueHolder = el('div', '');
    pad.appendChild(valueHolder);

    var previewEl;
    var currentIdx = function () { return parseInt(select.value, 10); };

    function formatValue(idx, val) {
      var row = byIdx[idx];
      if (row[2] === 'switch') return val ? 'ON' : 'OFF';
      if (row[2] === 'select') return val + '  ' + (row[7][val] || val);
      return String(val);
    }

    var valueControl = null;

    function rebuildValue() {
      clear(valueHolder);
      var idx = currentIdx();
      var row = byIdx[idx];
      currentVal.textContent = formatValue(idx, TV.settings[idx] != null ? TV.settings[idx] : row[3]);
      var body2 = stepRow(valueHolder, 2, 'Value – ' + row[6]);
      var wrap = el('div', 'param-row');
      wrap.appendChild(el('div', 'param-label', 'Value'));
      var ctrlWrap = el('div', 'param-control');
      wrap.appendChild(ctrlWrap);
      body2.appendChild(wrap);

      var curVal = TV.settings[idx] != null ? TV.settings[idx] : row[3];
      if (row[2] === 'switch') {
        var btn = el('button', 'toggle-btn', curVal ? 'ON' : 'OFF');
        var checked = !!curVal;
        if (checked) btn.classList.add('on');
        btn.addEventListener('click', function () { checked = !checked; btn.classList.toggle('on', checked); btn.textContent = checked ? 'ON' : 'OFF'; updatePreview(); });
        ctrlWrap.appendChild(btn);
        valueControl = { get: function () { return checked ? 1 : 0; } };
      } else if (row[2] === 'select') {
        var sel = document.createElement('select');
        row[7].forEach(function (name, i) { sel.appendChild(new Option(i + '  ' + name, String(i))); });
        sel.value = String(curVal);
        sel.addEventListener('change', updatePreview);
        ctrlWrap.appendChild(sel);
        valueControl = { get: function () { return parseInt(sel.value, 10); } };
      } else {
        var rw = el('div', 'range-wrap');
        rw.appendChild(el('span', 'range-min', String(row[4])));
        var input = document.createElement('input');
        input.type = 'range'; input.min = row[4]; input.max = row[5]; input.value = curVal;
        var valLabel = el('span', 'range-value', String(curVal));
        input.addEventListener('input', function () { valLabel.textContent = input.value; updatePreview(); });
        rw.appendChild(input); rw.appendChild(valLabel); rw.appendChild(el('span', 'range-max', String(row[5])));
        ctrlWrap.appendChild(rw);
        valueControl = { get: function () { return parseInt(input.value, 10); } };
      }
      updatePreview();
    }

    var body3 = stepRow(pad, 3, 'Review and send');
    var previewRow = el('div', 'preview-row');
    previewRow.appendChild(el('span', 'preview-label', 'PAYLOAD'));
    previewEl = el('span', 'preview-payload', '');
    previewRow.appendChild(previewEl);
    body3.appendChild(previewRow);

    function updatePreview() {
      try {
        var idx = currentIdx();
        previewEl.textContent = 'S' + hexb(idx, 2) + hexb(valueControl.get(), 2);
      } catch (e) { previewEl.textContent = '(invalid)'; }
    }

    var sendRow = el('div', 'send-row');
    var sendBtn = el('button', 'btn btn-green', 'Send');
    var chip = el('span', 'chip', '-');
    sendRow.appendChild(sendBtn); sendRow.appendChild(chip);
    body3.appendChild(sendRow);

    var body4 = stepRow(pad, 4, 'Result');
    var respArea = el('div', 'result-card');
    respArea.appendChild(el('div', 'result-empty', '-'));
    body4.appendChild(respArea);

    select.addEventListener('change', rebuildValue);
    rebuildValue();

    sendBtn.addEventListener('click', function () {
      var idx = currentIdx(), val = valueControl.get();
      var payload = 'S' + hexb(idx, 2) + hexb(val, 2);
      chip.textContent = 'sending';
      var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';
      var seq = nextSeq(); var wrapped = '#' + hexb(seq, 2) + payload;
      setTimeout(function () {
        TV.settings[idx] = val;
        panelLog(tag, 'log-send', 'UDP > ' + state.devices[deviceKey].ip + ' ' + wrapped);
        panelLog(tag, 'log-recv', 'UDP < #' + hexb(seq, 2) + '0');
        setChip(chip, 'ok', 'ok');
        clear(respArea);
        renderAckBanner(respArea, 'ok');
        respArea.appendChild(el('div', 'result-empty', 'Setting ' + hexb(idx, 2) + ' (' + byIdx[idx][6] + ') -> ' + formatValue(idx, val)));
        currentVal.textContent = formatValue(idx, val);
      }, rand(150, 350));
    });
  }

  /* ================================================================ quick test */
  function renderQuickTest(deviceKey) {
    var host = $('quicktest-scroll');
    clear(host);
    var statusChip = $('quicktest-chip'); statusChip.textContent = '-'; statusChip.style.background = ''; statusChip.style.color = '';
    var statusLine = $('quicktest-line'); clear(statusLine);

    var specs = COMMAND_TABLES[deviceKey].filter(function (s) { return s.section.toLowerCase().indexOf('test mode') !== -1; });
    if (!specs.length) { host.appendChild(el('div', 'quicktest-empty', '(no test-mode actions on this device)')); return; }

    specs.forEach(function (spec) {
      var options;
      if (spec.direct_buttons) options = spec.direct_buttons;
      else if (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum') {
        var p = spec.params[0];
        options = p.options.map(function (opt) { var v = {}; v[p.key] = opt[0]; return [spec.build(v), opt[1]]; });
      } else return;

      var meta = categoryMeta(spec.section);
      var title = el('div', 'quicktest-group-title', spec.name || spec.label);
      title.style.color = 'var(--accent-' + meta.color + ')';
      host.appendChild(title);
      var grid = el('div', 'quicktest-grid');
      options.forEach(function (opt) {
        var btn = el('button', '', opt[1]);
        btn.addEventListener('click', function () {
          if (!confirmOk(spec)) return;
          quicktestFire(spec, deviceKey, opt[0]);
        });
        grid.appendChild(btn);
      });
      host.appendChild(grid);
    });
  }

  function quicktestFire(spec, deviceKey, payload) {
    var chip = $('quicktest-chip'), line = $('quicktest-line');
    chip.style.background = 'var(--accent-blue-bg)'; chip.style.color = 'var(--accent-blue)'; chip.textContent = 'sending';
    clear(line); line.appendChild(document.createTextNode('...'));
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';
    var envelope = spec.envelope !== false;
    var seq = -1, wrapped = payload;
    if (envelope) { seq = nextSeq(); wrapped = '#' + hexb(seq, 2) + payload; }
    var sim = simulateUdp(spec, {}, payload);
    setTimeout(function () {
      panelLog(tag, 'log-send', 'UDP > ' + state.devices[deviceKey].ip + ' ' + wrapped);
      var ackKind = envelope ? (sim.ackKind || 'ok') : 'sent';
      (sim.entries || []).forEach(function (e) { panelLog(tag, 'log-recv', 'UDP < ' + e.raw); });
      setChip(chip, ackKind, ackKind);
      clear(line);
      var firstDesc = sim.entries && sim.entries[0] ? (sim.entries[0].desc || describeReply(sim.entries[0].raw)) : null;
      line.appendChild(document.createTextNode(firstDesc || (sim.entries && sim.entries[0] ? sim.entries[0].raw : '(no reply body)')));
    }, sim.delayMs || rand(120, 320));
  }

  /* ================================================================ raw box */
  function initRawBox() {
    $('raw-send-btn').addEventListener('click', function () {
      var deviceKey = state.activeDevice, device = state.devices[deviceKey];
      var payload = $('raw-payload').value;
      if (!payload) return;
      var transport = $('raw-transport').value;
      var envelope = $('raw-envelope').checked;
      var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';
      if (transport === 'udp') {
        var seq = -1, wrapped = payload;
        if (envelope) { seq = nextSeq(); wrapped = '#' + hexb(seq, 2) + payload; }
        setTimeout(function () {
          panelLog(tag, 'log-send', 'UDP(raw) > ' + device.ip + ' ' + wrapped + (envelope ? ' [enveloped]' : ''));
          panelLog(tag, 'log-recv', 'UDP(raw) < ' + (envelope ? ('#' + hexb(seq, 2) + '0') : '(no reply expected)'));
        }, rand(100, 260));
      } else {
        setTimeout(function () {
          panelLog(tag, 'log-send', 'TCP(raw) > ' + device.ip + ':' + device.telnetPort + ' ' + payload);
          panelLog(tag, 'log-recv', 'TCP(raw) < OK');
        }, rand(100, 260));
      }
    });
  }

  /* ================================================================ boot */
  function boot() {
    initTopbar();
    initConnBar();
    initSidebarFilter();
    initRawBox();
    switchDevice('diffuser');
    logSys('TestMode mockup started - simulated device, no real network I/O.');
    logSys('Diffuser -> ' + state.devices.diffuser.ip + '   SmartTV -> ' + state.devices.smarttv.ip);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
