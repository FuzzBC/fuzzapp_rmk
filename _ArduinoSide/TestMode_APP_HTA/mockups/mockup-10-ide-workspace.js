/* mockup-10-ide-workspace.js - VS Code-style UX.
 * Explorer tree (folders = categories, files = commands) opens commands
 * as tabs in a multi-document editor - several can be open at once, each
 * keeping its own param state (the DOM node is cached, not rebuilt, so
 * switching tabs never loses what you typed). Bottom panel mirrors
 * VSCode's Terminal/Output split: LOG / QUICK TEST / RAW, one at a time.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

  var state = { activeDevice: 'diffuser', keepAliveOn: false, filter: '', panelOpen: true, bottomTab: 'log' };
  var openTabs = [];           // array of tabKey strings, in open order
  var tabMeta = {};            // tabKey -> {deviceKey, spec}
  var docNodes = {};           // tabKey -> cached DOM node
  var activeTabKey = null;
  var expanded = { diffuser: {}, smarttv: {} };

  function tabKeyFor(deviceKey, spec) { return deviceKey + '::' + spec.id; }

  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('panel-toggle').addEventListener('click', togglePanel);
    $('keepalive-btn').addEventListener('click', toggleKeepAlive);
    $('savelog-btn').addEventListener('click', function () { Engine.saveLog(); });
    $('filter-input').addEventListener('input', function () { state.filter = this.value.toLowerCase().trim(); renderExplorer(); });
    $('bt-log').addEventListener('click', function () { setBottomTab('log'); });
    $('bt-quicktest').addEventListener('click', function () { setBottomTab('quicktest'); });
    $('bt-raw').addEventListener('click', function () { setBottomTab('raw'); });
    $('raw-send-btn').addEventListener('click', sendRaw);

    Engine.subscribeLog(function (line) { appendLogLine(line); });

    switchDevice('diffuser');
    setBottomTab('log');
    Engine.logSys('TestMode mockup started — simulated device, no real network I/O.');
  }

  /* --------------------------------------------------------------- top */
  function switchDevice(key) {
    state.activeDevice = key;
    $('dev-diffuser').classList.toggle('on', key === 'diffuser');
    $('dev-smarttv').classList.toggle('on', key === 'smarttv');
    $('explorer-device').textContent = Engine.devices[key].name;
    $('sb-ip').textContent = Engine.devices[key].ip + ':' + Engine.devices[key].udpPort;
    renderExplorer();
    renderQuickTestPanel();
    var raw = $('raw-transport'); clear(raw);
    raw.appendChild(new Option('UDP :' + Engine.devices[key].udpPort, 'udp'));
    if (Engine.devices[key].hasConsole) raw.appendChild(new Option('Telnet :' + Engine.devices[key].telnetPort, 'telnet'));
  }
  function togglePanel() { state.panelOpen = !state.panelOpen; $('bottom-panel').classList.toggle('collapsed', !state.panelOpen); }
  function toggleKeepAlive() {
    state.keepAliveOn = !state.keepAliveOn;
    $('keepalive-btn').textContent = 'Keep-alive: ' + (state.keepAliveOn ? 'on' : 'off');
    if (state.keepAliveOn) Engine.startKeepAlive(function () { return state.activeDevice; });
    else Engine.stopKeepAlive();
  }

  /* ----------------------------------------------------------- explorer */
  function renderExplorer() {
    var host = $('explorer-tree'); clear(host);
    var list = COMMAND_TABLES[state.activeDevice];
    var bySection = {}, order = [];
    list.forEach(function (spec) {
      var q = state.filter;
      var matches = !q || spec.id.toLowerCase().indexOf(q) !== -1 || spec.label.toLowerCase().indexOf(q) !== -1 || (spec.name || '').toLowerCase().indexOf(q) !== -1;
      if (!matches) return;
      if (!bySection[spec.section]) { bySection[spec.section] = []; order.push(spec.section); }
      bySection[spec.section].push(spec);
    });
    var exp = expanded[state.activeDevice];
    order.forEach(function (sec) {
      var meta = categoryMeta(sec);
      var isOpen = !!state.filter || exp[sec] !== false;
      var folder = el('div', 'tree-folder');
      folder.appendChild(el('span', 'chev', isOpen ? '▾' : '▸'));
      folder.appendChild(el('span', '', meta.icon + ' ' + meta.title));
      folder.addEventListener('click', function () { exp[sec] = !isOpen; renderExplorer(); });
      host.appendChild(folder);
      if (!isOpen) return;
      bySection[sec].forEach(function (spec) {
        var tabKey = tabKeyFor(state.activeDevice, spec);
        var file = el('div', 'tree-file');
        if (tabKey === activeTabKey) file.classList.add('active');
        file.appendChild(el('span', '', spec.name || spec.label));
        file.appendChild(el('span', 'lbl', spec.label));
        file.addEventListener('click', function () { openTab(state.activeDevice, spec); });
        host.appendChild(file);
      });
    });
  }

  /* ------------------------------------------------------------- tabs */
  function openTab(deviceKey, spec) {
    var tabKey = tabKeyFor(deviceKey, spec);
    if (openTabs.indexOf(tabKey) === -1) { openTabs.push(tabKey); tabMeta[tabKey] = { deviceKey: deviceKey, spec: spec }; }
    activeTabKey = tabKey;
    renderTabstrip();
    renderEditorBody();
    renderExplorer();
  }
  function closeTab(tabKey) {
    var i = openTabs.indexOf(tabKey);
    if (i === -1) return;
    openTabs.splice(i, 1);
    delete docNodes[tabKey]; delete tabMeta[tabKey];
    if (activeTabKey === tabKey) activeTabKey = openTabs.length ? openTabs[openTabs.length - 1] : null;
    renderTabstrip(); renderEditorBody(); renderExplorer();
  }
  function renderTabstrip() {
    var host = $('tabstrip'); clear(host);
    if (!openTabs.length) { host.appendChild(el('div', 'tabstrip-empty', 'No commands open — pick one from the Explorer')); return; }
    openTabs.forEach(function (tabKey) {
      var meta = tabMeta[tabKey];
      var tab = el('div', 'dtab');
      if (tabKey === activeTabKey) tab.classList.add('active');
      tab.appendChild(el('span', '', meta.spec.name || meta.spec.label));
      var x = el('span', 'x', '✕');
      x.addEventListener('click', function (e) { e.stopPropagation(); closeTab(tabKey); });
      tab.appendChild(x);
      tab.addEventListener('click', function () { activeTabKey = tabKey; renderTabstrip(); renderEditorBody(); renderExplorer(); });
      host.appendChild(tab);
    });
  }

  function renderEditorBody() {
    var host = $('editor-body'); clear(host);
    if (!activeTabKey) {
      var empty = el('div', 'editor-empty', 'No command open.');
      empty.appendChild(el('div', 'hint', 'Pick a device on the left, then click a command in the Explorer to open it here — multiple can stay open as tabs.'));
      host.appendChild(empty);
      return;
    }
    if (!docNodes[activeTabKey]) docNodes[activeTabKey] = buildDoc(activeTabKey);
    host.appendChild(docNodes[activeTabKey]);
  }

  function buildDoc(tabKey) {
    var meta = tabMeta[tabKey], spec = meta.spec, deviceKey = meta.deviceKey;
    var catMeta = categoryMeta(spec.section);
    var doc = el('div', '');
    var head = el('div', 'doc-head');
    head.appendChild(el('span', 'doc-icon', catMeta.icon));
    head.appendChild(el('span', 'doc-name', spec.name || spec.label));
    head.appendChild(el('span', 'doc-badge', spec.label));
    head.appendChild(el('span', 'doc-badge', spec.transport === 'udp' ? 'UDP' : (spec.transport === 'console' ? 'TCP:23' : spec.transport)));
    doc.appendChild(head);
    doc.appendChild(el('div', 'doc-desc', spec.desc || ''));

    if (spec.custom_panel) { buildCustomPanel(doc, spec, deviceKey); return doc; }

    var controls = {}, previewEl;
    var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—'));
    function updatePreview() {
      if (!previewEl || spec.direct_buttons) return;
      try { previewEl.textContent = spec.build(Controls.collect(spec, controls)); } catch (e) { previewEl.textContent = '(invalid)'; }
    }
    if (spec.params && spec.params.length) {
      doc.appendChild(el('div', 'doc-step', 'Configure'));
      var form = el('div', ''); controls = Controls.buildForm(form, spec, updatePreview); doc.appendChild(form);
    }

    var chip = el('span', 'chip-status', '—');
    if (spec.direct_buttons) {
      doc.appendChild(el('div', 'doc-step', 'Choose an option'));
      var row = el('div', 'direct-row');
      spec.direct_buttons.forEach(function (opt) {
        var b = el('button', '', opt[1]);
        b.addEventListener('click', function () {
          if (spec.confirm && !window.confirm(spec.confirm)) return;
          doSend(spec, deviceKey, opt[0], {}, chip, resultBox);
        });
        row.appendChild(b);
      });
      doc.appendChild(row); doc.appendChild(chip);
    } else {
      doc.appendChild(el('div', 'doc-step', 'Review & send'));
      var pv = el('div', 'preview-box'); previewEl = el('span', ''); pv.appendChild(previewEl);
      doc.appendChild(pv); updatePreview();
      var sendRow = el('div', 'send-row');
      var sendBtn = el('button', 'send-btn', 'Send');
      sendBtn.addEventListener('click', function () {
        if (spec.confirm && !window.confirm(spec.confirm)) return;
        var vals = Controls.collect(spec, controls);
        doSend(spec, deviceKey, spec.build(vals), vals, chip, resultBox);
      });
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip);
      doc.appendChild(sendRow);
    }
    doc.appendChild(el('div', 'doc-step', 'Result'));
    doc.appendChild(resultBox);
    return doc;
  }

  function doSend(spec, deviceKey, payload, vals, chip, resultBox) {
    chip.textContent = 'sending…';
    Engine.send(deviceKey, spec, payload, vals, function (result) {
      chip.textContent = result.ackKind || (result.text ? 'replied' : 'no reply');
      ResultView.render(resultBox, spec, result, vals);
    });
  }

  function buildCustomPanel(doc, spec, deviceKey) {
    if (spec.custom_panel === 'diffuser_history') {
      var list = el('div', '');
      function draw() {
        clear(list);
        list.appendChild(el('div', 'doc-step', Engine.DIF.history.length + ' of 10 cycles stored'));
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
      draw(); doc.appendChild(list);
      return;
    }
    if (spec.custom_panel === 'settings_write') {
      var byIdx = {}; EE_SETTINGS_TABLE.forEach(function (row) { byIdx[row[0]] = row; });
      doc.appendChild(el('div', 'doc-step', 'Choose a setting'));
      var selRow = el('div', 'ctl-row'); selRow.appendChild(el('div', 'ctl-label', 'Setting'));
      var select = document.createElement('select'); select.className = 'ctl-select';
      EE_SETTINGS_TABLE.forEach(function (row) { select.appendChild(new Option(hexb(row[0], 2) + '  ' + row[6], String(row[0]))); });
      var sw = el('div', 'ctl-widget'); sw.appendChild(select); selRow.appendChild(sw); doc.appendChild(selRow);
      var valueHost = el('div', ''); doc.appendChild(valueHost);
      doc.appendChild(el('div', 'doc-step', 'Review & send'));
      var pv = el('div', 'preview-box'); var previewEl = el('span', ''); pv.appendChild(previewEl); doc.appendChild(pv);
      var chip = el('span', 'chip-status', '—');
      var sendRow = el('div', 'send-row'); var sendBtn = el('button', 'send-btn', 'Send');
      sendRow.appendChild(sendBtn); sendRow.appendChild(chip); doc.appendChild(sendRow);
      doc.appendChild(el('div', 'doc-step', 'Result'));
      var resultBox = el('div', 'result-box'); resultBox.appendChild(el('div', 'result-empty', '—')); doc.appendChild(resultBox);

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

  /* -------------------------------------------------------- bottom panel */
  function setBottomTab(name) {
    state.bottomTab = name;
    ['log', 'quicktest', 'raw'].forEach(function (t) {
      $('bt-' + t).classList.toggle('on', t === name);
      $('bp-' + t).classList.toggle('hidden', t !== name);
    });
    if (!state.panelOpen) togglePanel();
  }

  function appendLogLine(line) {
    var host = $('bp-log');
    var row = el('div', 'log-line ' + (line.cls || 'sys'));
    row.appendChild(el('span', 'log-ts', line.ts));
    row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
    host.appendChild(row);
    host.scrollTop = host.scrollHeight;
    while (host.children.length > 400) host.removeChild(host.firstChild);
  }

  function renderQuickTestPanel() {
    var host = $('bp-quicktest'); clear(host);
    var specs = COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.section.toLowerCase().indexOf('test mode') !== -1; });
    if (!specs.length) { host.appendChild(el('div', 'result-empty', '(no test-mode actions on this device)')); return; }
    specs.forEach(function (spec) {
      var options = spec.direct_buttons ? spec.direct_buttons :
        (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum' ?
          spec.params[0].options.map(function (opt) { var v = {}; v[spec.params[0].key] = opt[0]; return [spec.build(v), opt[1]]; }) : null);
      if (!options) return;
      host.appendChild(el('div', 'qt-group', spec.name || spec.label));
      var row = el('div', 'qt-row');
      options.forEach(function (opt) {
        var b = el('button', '', opt[1]);
        b.addEventListener('click', function () {
          if (spec.confirm && !window.confirm(spec.confirm)) return;
          Engine.send(state.activeDevice, spec, opt[0], {}, function () {});
        });
        row.appendChild(b);
      });
      host.appendChild(row);
    });
  }

  function sendRaw() {
    var deviceKey = state.activeDevice, device = Engine.devices[deviceKey];
    var payload = $('raw-payload').value;
    if (!payload) return;
    var transport = $('raw-transport').value, envelope = $('raw-envelope').checked;
    var tag = deviceKey === 'diffuser' ? 'DIF' : 'TV';
    if (transport === 'udp') {
      var seq = -1, wrapped = payload;
      if (envelope) { seq = Engine.nextSeq(); wrapped = '#' + hexb(seq, 2) + payload; }
      setTimeout(function () {
        Engine.logAppend(tag, 'send', 'UDP(raw) > ' + device.ip + ' ' + wrapped + (envelope ? ' [enveloped]' : ''));
        Engine.logAppend(tag, 'recv', 'UDP(raw) < ' + (envelope ? ('#' + hexb(seq, 2) + '0') : '(no reply expected)'));
      }, Engine.rand(100, 260));
    } else {
      setTimeout(function () {
        Engine.logAppend(tag, 'send', 'TCP(raw) > ' + device.ip + ':' + device.telnetPort + ' ' + payload);
        Engine.logAppend(tag, 'recv', 'TCP(raw) < OK');
      }, Engine.rand(100, 260));
    }
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
