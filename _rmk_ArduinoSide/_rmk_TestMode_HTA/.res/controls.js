/* controls.js - param-control builder for TestMode.hta. Ported from the
 * mockups' shared/controls.js with two IE11/Trident-safety changes:
 *   - classList.toggle(cls, force) -> explicit add()/remove() (the 2-arg
 *     form isn't reliable on IE11's classList)
 *   - hexcolor no longer uses <input type="color"> (no native colour
 *     picker in Trident) - just the hex text field plus a plain preview
 *     swatch
 * Same param TYPES as before (enum/range/number/checkbox/hexcolor/ledmask
 * /text), same theme-neutral .ctl-* class names app.css styles.
 */

var Controls = (function () {
  'use strict';

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.appendChild(document.createTextNode(text));
    return e;
  }
  function repeat(ch, n) { var s = ''; for (var i = 0; i < n; i++) s += ch; return s; }
  function setOn(elm, on) { if (on) elm.className = elm.className.replace(/\s*\bon\b/g, '') + ' on'; else elm.className = elm.className.replace(/\s*\bon\b/g, ''); }

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
      for (var oi = 0; oi < p.options.length; oi++) select.appendChild(new Option(p.options[oi][1], p.options[oi][0]));
      select.value = p['default'];
      select.addEventListener('change', onChange);
      widget.appendChild(select);
      out.get = function () { return select.value; };
    } else if (p.type === 'range') {
      var rw = el('div', 'ctl-range');
      rw.appendChild(el('span', 'ctl-range-min', String(p.min)));
      var input = document.createElement('input');
      input.type = 'range'; input.min = p.min; input.max = p.max; input.value = p['default'];
      var valueLabel = el('span', 'ctl-range-value', String(p['default']));
      input.addEventListener('input', function () { valueLabel.firstChild.nodeValue = input.value; onChange(); });
      rw.appendChild(input); rw.appendChild(valueLabel); rw.appendChild(el('span', 'ctl-range-max', String(p.max)));
      widget.appendChild(rw);
      out.get = function () { return parseInt(input.value, 10); };
    } else if (p.type === 'number') {
      var num = document.createElement('input');
      num.type = 'number'; num.className = 'ctl-number'; num.value = p['default']; num.min = p.min; num.max = p.max;
      num.addEventListener('input', onChange);
      widget.appendChild(num);
      out.get = function () { var n = parseInt(num.value, 10); return isNaN(n) ? (p.min || 0) : n; };
    } else if (p.type === 'checkbox') {
      var btn = el('button', 'ctl-toggle', p['default'] ? 'ON' : 'OFF');
      var checked = !!p['default'];
      btn.type = 'button';
      setOn(btn, checked);
      btn.addEventListener('click', function () {
        checked = !checked; setOn(btn, checked); btn.firstChild.nodeValue = checked ? 'ON' : 'OFF'; onChange();
      });
      widget.appendChild(btn);
      out.get = function () { return checked; };
      out.checkEl = btn;
    } else if (p.type === 'hexcolor') {
      var hw = el('div', 'ctl-color');
      var swatch = el('div', 'ctl-swatch');
      swatch.style.background = '#' + p['default'];
      var textInput = document.createElement('input');
      textInput.type = 'text'; textInput.className = 'ctl-hex'; textInput.value = p['default'].toUpperCase(); textInput.maxLength = 6;
      textInput.addEventListener('input', function () {
        var v = textInput.value.toUpperCase().replace(/[^0-9A-F]/g, '').slice(0, 6);
        while (v.length < 6) v += '0';
        textInput.value = v;
        swatch.style.background = '#' + v;
        onChange();
      });
      hw.appendChild(swatch); hw.appendChild(textInput);
      widget.appendChild(hw);
      out.get = function () { return textInput.value.toUpperCase(); };
    } else if (p.type === 'ledmask') {
      out = buildLedZones(widget, p, onChange);
      out.key = p.key;
    } else {
      var txt = document.createElement('input');
      txt.type = 'text'; txt.className = 'ctl-text'; txt.value = p['default'] != null ? String(p['default']) : '';
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

  // Compact zone-toggle version of the LED mask (whole zone at a time, not
  // per-pixel) - same 61-char bitstring wire format, far less DOM.
  function buildLedZones(container, p, onChange) {
    var bits = (p['default'] || repeat('0', 61)).split('');
    var wrap = el('div', 'ctl-zones');
    LED_ZONES.forEach(function (zone) {
      var name = zone[0], start = zone[1], count = zone[2];
      var chip = el('button', 'ctl-zone-chip', name);
      chip.type = 'button';
      function isOn() { for (var i = 0; i < count; i++) if (bits[start + i] !== '1') return false; return true; }
      function sync() { setOn(chip, isOn()); }
      chip.addEventListener('click', function () {
        var on = !isOn();
        for (var i = 0; i < count; i++) bits[start + i] = on ? '1' : '0';
        sync(); onChange();
      });
      sync();
      wrap.appendChild(chip);
    });
    container.appendChild(wrap);
    return { get: function () { return bits.join(''); } };
  }

  function buildForm(container, spec, onChange) {
    var controls = {};
    (spec.params || []).forEach(function (p) { controls[p.key] = buildField(container, p, onChange); });
    var deps = container.querySelectorAll('[data-enable-when]');
    if (deps.length) {
      window.setInterval(function () {
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
