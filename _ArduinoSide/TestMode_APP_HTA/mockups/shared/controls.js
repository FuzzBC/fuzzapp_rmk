/* controls.js - generic param-control builder shared by the round-2 UX
 * mockups. Same param TYPES as round 1 (enum/range/number/checkbox/
 * hexcolor/ledmask/text), rendered into theme-neutral class names
 * (.ctl-*) so each mockup's own CSS can restyle them to fit its look
 * without every mockup re-implementing slider/toggle/colour-picker logic.
 * The *navigation/chrome* around these is what actually differs per
 * mockup (palette vs dashboard vs wizard vs board vs IDE) - that's the
 * real UX difference this round is about, not the leaf widgets.
 */

var Controls = (function () {
  'use strict';

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  // Builds one param row into `container`, returns {key, get}.
  function buildField(container, p, onChange) {
    var row = el('div', 'ctl-row');
    row.appendChild(el('div', 'ctl-label', p.label));
    var widget = el('div', 'ctl-widget');
    row.appendChild(widget);
    container.appendChild(row);

    var out = { key: p.key };

    if (p.type === 'enum') {
      var select = document.createElement('select');
      select.className = 'ctl-select';
      p.options.forEach(function (opt) { select.appendChild(new Option(opt[1], opt[0])); });
      select.value = p.default;
      select.addEventListener('change', onChange);
      widget.appendChild(select);
      out.get = function () { return select.value; };
    } else if (p.type === 'range') {
      var rw = el('div', 'ctl-range');
      rw.appendChild(el('span', 'ctl-range-min', String(p.min)));
      var input = document.createElement('input');
      input.type = 'range'; input.min = p.min; input.max = p.max; input.value = p.default;
      var valueLabel = el('span', 'ctl-range-value', String(p.default));
      input.addEventListener('input', function () { valueLabel.textContent = input.value; onChange(); });
      rw.appendChild(input); rw.appendChild(valueLabel); rw.appendChild(el('span', 'ctl-range-max', String(p.max)));
      widget.appendChild(rw);
      out.get = function () { return parseInt(input.value, 10); };
    } else if (p.type === 'number') {
      var num = document.createElement('input');
      num.type = 'number'; num.className = 'ctl-number'; num.value = p.default; num.min = p.min; num.max = p.max;
      num.addEventListener('input', onChange);
      widget.appendChild(num);
      out.get = function () { var n = parseInt(num.value, 10); return isNaN(n) ? (p.min || 0) : n; };
    } else if (p.type === 'checkbox') {
      var btn = el('button', 'ctl-toggle', p.default ? 'ON' : 'OFF');
      var checked = !!p.default;
      btn.type = 'button';
      if (checked) btn.classList.add('on');
      btn.addEventListener('click', function () {
        checked = !checked; btn.classList.toggle('on', checked); btn.textContent = checked ? 'ON' : 'OFF'; onChange();
      });
      widget.appendChild(btn);
      out.get = function () { return checked; };
      out.checkEl = btn;
    } else if (p.type === 'hexcolor') {
      var hw = el('div', 'ctl-color');
      var colorInput = document.createElement('input');
      colorInput.type = 'color'; colorInput.className = 'ctl-swatch'; colorInput.value = '#' + p.default;
      var textInput = document.createElement('input');
      textInput.type = 'text'; textInput.className = 'ctl-hex'; textInput.value = p.default.toUpperCase(); textInput.maxLength = 6;
      colorInput.addEventListener('input', function () { textInput.value = colorInput.value.slice(1).toUpperCase(); onChange(); });
      textInput.addEventListener('input', function () {
        var v = textInput.value.toUpperCase().replace(/[^0-9A-F]/g, '').slice(0, 6);
        while (v.length < 6) v += '0';
        textInput.value = v; colorInput.value = '#' + v; onChange();
      });
      hw.appendChild(colorInput); hw.appendChild(textInput);
      widget.appendChild(hw);
      out.get = function () { return textInput.value.toUpperCase(); };
    } else if (p.type === 'ledmask') {
      out = buildLedZones(widget, p, onChange);
      out.key = p.key;
    } else {
      var txt = document.createElement('input');
      txt.type = 'text'; txt.className = 'ctl-text'; txt.value = p.default != null ? String(p.default) : '';
      txt.addEventListener('input', onChange);
      widget.appendChild(txt);
      out.get = function () { return txt.value; };
    }

    if (p.enable_when) {
      row.setAttribute('data-enable-when', p.enable_when);
      widget.style.opacity = '.4'; widget.style.pointerEvents = 'none';
    }
    return out;
  }

  // Compact zone-toggle version of the LED mask (whole zone at a time,
  // not per-pixel) - same 61-char bitstring wire format, far less DOM.
  function buildLedZones(container, p, onChange) {
    var bits = (p.default || Engine.repeatChar('0', 61)).split('');
    var wrap = el('div', 'ctl-zones');
    var chips = {};
    LED_ZONES.forEach(function (zone) {
      var name = zone[0], start = zone[1], count = zone[2];
      var chip = el('button', 'ctl-zone-chip', name);
      chip.type = 'button';
      function isOn() { for (var i = 0; i < count; i++) if (bits[start + i] !== '1') return false; return true; }
      function sync() { chip.classList.toggle('on', isOn()); }
      chip.addEventListener('click', function () {
        var on = !isOn();
        for (var i = 0; i < count; i++) bits[start + i] = on ? '1' : '0';
        sync(); onChange();
      });
      sync();
      chips[name] = sync;
      wrap.appendChild(chip);
    });
    container.appendChild(wrap);
    return { get: function () { return bits.join(''); } };
  }

  function buildForm(container, spec, onChange) {
    var controls = {};
    (spec.params || []).forEach(function (p) { controls[p.key] = buildField(container, p, onChange); });
    // enable_when wiring: poll (cheap, ~3/sec, params lists are tiny)
    var deps = container.querySelectorAll('[data-enable-when]');
    if (deps.length) {
      setInterval(function () {
        for (var i = 0; i < deps.length; i++) {
          var depKey = deps[i].getAttribute('data-enable-when');
          var dep = controls[depKey];
          if (!dep) continue;
          var on = dep.get();
          var widget = deps[i].querySelector('.ctl-widget');
          if (widget) { widget.style.opacity = on ? '1' : '.4'; widget.style.pointerEvents = on ? '' : 'none'; }
        }
      }, 250);
    }
    return controls;
  }

  function collect(spec, controls) {
    var vals = {};
    (spec.params || []).forEach(function (p) { vals[p.key] = controls[p.key].get(); });
    return vals;
  }

  return { buildForm: buildForm, collect: collect, buildField: buildField };
})();
