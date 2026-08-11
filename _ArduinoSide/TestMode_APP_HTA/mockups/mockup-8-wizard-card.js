/* mockup-8-wizard-card.js - mobile-first wizard UX.
 * Category pills up top, one focused command-card in the middle with
 * prev/next stepper arrows to move through that category, bottom tab bar
 * (Home / Commands / Log) swaps the whole screen like a native app.
 */
(function () {
  'use strict';
  function $(id) { return document.getElementById(id); }
  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; }
  function clear(n) { while (n.firstChild) n.removeChild(n.firstChild); }

  var state = { activeDevice: 'diffuser', section: null, sectionSpecs: [], index: 0 };
  var activityLines = [];

  function boot() {
    $('dev-diffuser').addEventListener('click', function () { switchDevice('diffuser'); });
    $('dev-smarttv').addEventListener('click', function () { switchDevice('smarttv'); });
    $('nav-home').addEventListener('click', function () { showView('home'); });
    $('nav-commands').addEventListener('click', function () { showView('commands'); });
    $('nav-log').addEventListener('click', function () { showView('log'); });
    $('prev-btn').addEventListener('click', function () { step(-1); });
    $('next-btn').addEventListener('click', function () { step(1); });
    $('jump-btn').addEventListener('click', openJump);
    $('jump-close').addEventListener('click', closeJump);
    $('jump-input').addEventListener('input', function () { renderJumpList(this.value); });
    $('savelog-btn').addEventListener('click', function () { Engine.saveLog(); });

    Engine.subscribeLog(function (line) { activityLines.push(line); if (!$('view-log').classList.contains('hidden')) renderLog(); });

    switchDevice('diffuser');
    Engine.logSys('TestMode mockup started — simulated device, no real network I/O.');
  }

  function switchDevice(key) {
    state.activeDevice = key;
    $('dev-diffuser').classList.toggle('on', key === 'diffuser');
    $('dev-smarttv').classList.toggle('on', key === 'smarttv');
    renderCatTabs();
    renderHome();
  }

  /* ------------------------------------------------------------ tabs */
  function sectionsFor(deviceKey) {
    var order = [], seen = {};
    COMMAND_TABLES[deviceKey].forEach(function (s) {
      if (s.section.toLowerCase().indexOf('test mode') !== -1) return;
      if (!seen[s.section]) { seen[s.section] = true; order.push(s.section); }
    });
    return order;
  }
  function renderCatTabs() {
    var host = $('cat-tabs'); clear(host);
    var sections = sectionsFor(state.activeDevice);
    sections.forEach(function (sec) {
      var meta = categoryMeta(sec);
      var b = el('button', 'cat-tab', meta.icon + ' ' + meta.title);
      b.addEventListener('click', function () { selectSection(sec); });
      host.appendChild(b);
    });
    selectSection(sections[0]);
  }
  function selectSection(sec) {
    state.section = sec;
    state.sectionSpecs = COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.section === sec; });
    state.index = 0;
    Array.prototype.forEach.call($('cat-tabs').children, function (btn, i) { btn.classList.toggle('on', sectionsFor(state.activeDevice)[i] === sec); });
    showView('commands');
    renderCard();
  }
  function step(delta) {
    var n = state.sectionSpecs.length; if (!n) return;
    state.index = (state.index + delta + n) % n;
    renderCard();
  }

  /* --------------------------------------------------------------- views */
  function showView(name) {
    ['home', 'commands', 'log'].forEach(function (v) { $('view-' + v).classList.toggle('hidden', v !== name); });
    $('nav-home').classList.toggle('on', name === 'home');
    $('nav-commands').classList.toggle('on', name === 'commands');
    $('nav-log').classList.toggle('on', name === 'log');
    if (name === 'log') renderLog();
    if (name === 'home') renderHome();
  }

  function renderHome() {
    var host = $('home-stats'); clear(host);
    var tiles;
    if (state.activeDevice === 'diffuser') {
      var d = Engine.DIF;
      tiles = [['Mode', 'M' + d.mode], ['Parfum', d.parfumMin ? d.parfumMin + 'm' : 'off'], ['Usage', d.usageMin + 'm'], ['Refills', d.refillCount + '/10']];
    } else {
      var t = Engine.TV;
      tiles = [['TV', t.tvOn ? 'on' : 'off'], ['LEDs', t.ledsOn ? 'on' : 'off'], ['Ambient', t.ambient ? 'on' : 'off'], ['Lux', t.lux]];
    }
    tiles.forEach(function (tt) {
      var s = el('div', 'home-stat'); s.appendChild(el('div', 'lab', tt[0])); s.appendChild(el('div', 'val', String(tt[1]))); host.appendChild(s);
    });

    var qt = $('home-quicktest'); clear(qt);
    var specs = COMMAND_TABLES[state.activeDevice].filter(function (s) { return s.section.toLowerCase().indexOf('test mode') !== -1; });
    var any = false;
    specs.forEach(function (spec) {
      var options = spec.direct_buttons ? spec.direct_buttons :
        (spec.params && spec.params.length === 1 && spec.params[0].type === 'enum' ?
          spec.params[0].options.map(function (opt) { var v = {}; v[spec.params[0].key] = opt[0]; return [spec.build(v), opt[1]]; }) : null);
      if (!options) return;
      options.forEach(function (opt) {
        any = true;
        var b = el('button', '', opt[1]);
        b.addEventListener('click', function () {
          if (spec.confirm && !window.confirm(spec.confirm)) return;
          Engine.send(state.activeDevice, spec, opt[0], {}, function () { renderHome(); });
        });
        qt.appendChild(b);
      });
    });
    if (!any) qt.appendChild(el('div', '', '(none for this device)'));
  }

  function renderLog() {
    var host = $('log-list'); clear(host);
    activityLines.forEach(function (line) {
      var row = el('div', 'log-line ' + (line.cls || 'sys'));
      row.appendChild(el('span', 'log-ts', line.ts));
      row.appendChild(document.createTextNode('[' + line.tag + '] ' + line.text));
      host.appendChild(row);
    });
    host.scrollTop = host.scrollHeight;
  }

  /* ------------------------------------------------------------- card */
  function renderCard() {
    var host = $('card-host'); clear(host);
    var specs = state.sectionSpecs, spec = specs[state.index];
    $('stepper-label').textContent = specs.length ? ((state.index + 1) + ' / ' + specs.length) : '';
    $('prev-btn').disabled = specs.length < 2;
    $('next-btn').disabled = specs.length < 2;
    if (!spec) { host.appendChild(el('div', 'card-desc', 'No commands in this category.')); return; }

    var meta = categoryMeta(spec.section);
    host.appendChild(el('div', 'card-icon', meta.icon));
    host.appendChild(el('div', 'card-name', spec.name || spec.label));
    var badges = el('div', 'card-badges');
    badges.appendChild(el('span', 'card-badge', spec.label));
    badges.appendChild(el('span', 'card-badge', spec.transport === 'udp' ? 'UDP' : (spec.transport === 'console' ? 'TCP:23' : spec.transport)));
    host.appendChild(badges);
    host.appendChild(el('div', 'card-desc', spec.desc || ''));

    var deviceKey = state.activeDevice;
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
      var sendBtn = el('button', 'send-btn', 'Send');
      sendBtn.addEventListener('click', function () {
        if (spec.confirm && !window.confirm(spec.confirm)) return;
        var vals = Controls.collect(spec, controls);
        doSend(spec, deviceKey, spec.build(vals), vals, chip, resultBox);
      });
      host.appendChild(sendBtn); host.appendChild(chip);
    }
    host.appendChild(resultBox);
  }

  function doSend(spec, deviceKey, payload, vals, chip, resultBox) {
    chip.textContent = 'sending…';
    Engine.send(deviceKey, spec, payload, vals, function (result) {
      chip.textContent = result.ackKind || (result.text ? 'replied' : 'no reply');
      ResultView.render(resultBox, spec, result, vals);
      if (!$('view-home').classList.contains('hidden')) renderHome();
    });
  }

  function buildCustomPanel(host, spec, deviceKey) {
    if (spec.custom_panel === 'diffuser_history') {
      var list = el('div', '');
      function draw() {
        clear(list);
        list.appendChild(el('div', 'card-desc', Engine.DIF.history.length + ' of 10 cycles stored'));
        Engine.DIF.history.forEach(function (mins, i) {
          var row = el('div', 'ctl-row');
          row.appendChild(el('div', 'ctl-label', '#' + (i + 1) + ' — ' + Engine.fmtDurationMin(mins)));
          var del = el('button', 'chip-status', 'Remove ✕');
          del.addEventListener('click', function () { if (window.confirm('Remove #' + (i + 1) + '?')) { Engine.DIF.history.splice(i, 1); draw(); } });
          row.appendChild(del);
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
      var sendBtn = el('button', 'send-btn', 'Send'); host.appendChild(sendBtn); host.appendChild(chip);
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

  /* -------------------------------------------------------------- jump */
  function openJump() { $('jump-overlay').classList.remove('hidden'); $('jump-input').value = ''; $('jump-input').focus(); renderJumpList(''); }
  function closeJump() { $('jump-overlay').classList.add('hidden'); }
  function renderJumpList(query) {
    query = query.trim().toLowerCase();
    var host = $('jump-list'); clear(host);
    COMMAND_TABLES[state.activeDevice].forEach(function (spec) {
      if (spec.section.toLowerCase().indexOf('test mode') !== -1) return;
      var matches = !query || spec.id.toLowerCase().indexOf(query) !== -1 || spec.label.toLowerCase().indexOf(query) !== -1 || (spec.name || '').toLowerCase().indexOf(query) !== -1;
      if (!matches) return;
      var row = el('div', 'jump-row');
      row.appendChild(document.createTextNode(spec.name || spec.label));
      row.appendChild(el('span', 'label', spec.label));
      row.addEventListener('click', function () {
        closeJump();
        selectSection(spec.section);
        state.index = state.sectionSpecs.indexOf(spec);
        renderCard();
        Array.prototype.forEach.call($('cat-tabs').children, function (btn, i) { btn.classList.toggle('on', sectionsFor(state.activeDevice)[i] === spec.section); });
      });
      host.appendChild(row);
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot();
})();
