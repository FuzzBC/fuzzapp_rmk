/* controls.js - param-control builder for TestMode.hta. Ported from the
 * mockups' shared/controls.js with two IE11/Trident-safety changes:
 *   - classList.toggle(cls, force) -> explicit add()/remove() (the 2-arg
 *     form isn't reliable on IE11's classList)
 *   - hexcolor doesn't use <input type="color"> (no native colour picker
 *     in Trident) - instead: a hex text field, a preview swatch, and a
 *     canvas-based saturation/value square + hue strip (drag to pick,
 *     kept in sync both ways with the hex field), the closest thing to a
 *     real colour picker Trident's actually capable of rendering
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

  // --- HSV <-> RGB, for the hexcolor picker (canvas SV square + hue strip) ---
  function hsvToRgb(h, s, v) {
    var c = v * s, x = c * (1 - Math.abs((h / 60) % 2 - 1)), m = v - c, r, g, b;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return [Math.round((r + m) * 255), Math.round((g + m) * 255), Math.round((b + m) * 255)];
  }
  function rgbToHsv(r, g, b) {
    r /= 255; g /= 255; b /= 255;
    var max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min, h = 0;
    if (d !== 0) {
      if (max === r) h = 60 * (((g - b) / d) % 6);
      else if (max === g) h = 60 * ((b - r) / d + 2);
      else h = 60 * ((r - g) / d + 4);
      if (h < 0) h += 360;
    }
    return { h: h, s: max === 0 ? 0 : d / max, v: max };
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
      hw.appendChild(swatch); hw.appendChild(textInput);
      widget.appendChild(hw);

      // Canvas SV square + hue strip - the actual interactive way to pick
      // a color here (no native <input type="color"> in Trident, see file
      // header). Bidirectional sync with the hex field: typing hex moves
      // the markers, dragging on either canvas rewrites the hex text +
      // swatch.
      var SV_W = 180, SV_H = 110, HUE_W = 180, HUE_H = 16;
      var picker = el('div', 'ctl-picker');
      var svCanvas = document.createElement('canvas');
      svCanvas.className = 'ctl-sv'; svCanvas.width = SV_W; svCanvas.height = SV_H;
      var hueCanvas = document.createElement('canvas');
      hueCanvas.className = 'ctl-hue'; hueCanvas.width = HUE_W; hueCanvas.height = HUE_H;
      picker.appendChild(svCanvas); picker.appendChild(hueCanvas);
      widget.appendChild(picker);
      var svCtx = svCanvas.getContext('2d'), hueCtx = hueCanvas.getContext('2d');

      function hexToRgb(hex) {
        return { r: parseInt(hex.substr(0, 2), 16) || 0, g: parseInt(hex.substr(2, 2), 16) || 0, b: parseInt(hex.substr(4, 2), 16) || 0 };
      }
      function byte2(n) { var s = n.toString(16).toUpperCase(); return s.length < 2 ? '0' + s : s; }

      var hsv = { h: 0, s: 0, v: 1 };

      function drawHue() {
        var grad = hueCtx.createLinearGradient(0, 0, HUE_W, 0);
        ['#f00', '#ff0', '#0f0', '#0ff', '#00f', '#f0f', '#f00'].forEach(function (c, i) { grad.addColorStop(i / 6, c); });
        hueCtx.fillStyle = grad;
        hueCtx.fillRect(0, 0, HUE_W, HUE_H);
        var x = (hsv.h / 360) * HUE_W;
        hueCtx.strokeStyle = '#fff'; hueCtx.lineWidth = 2;
        hueCtx.strokeRect(x - 2, 0, 4, HUE_H);
      }
      function drawSv() {
        var rgb = hsvToRgb(hsv.h, 1, 1);
        svCtx.fillStyle = 'rgb(' + rgb[0] + ',' + rgb[1] + ',' + rgb[2] + ')';
        svCtx.fillRect(0, 0, SV_W, SV_H);
        var gw = svCtx.createLinearGradient(0, 0, SV_W, 0);
        gw.addColorStop(0, 'rgba(255,255,255,1)'); gw.addColorStop(1, 'rgba(255,255,255,0)');
        svCtx.fillStyle = gw; svCtx.fillRect(0, 0, SV_W, SV_H);
        var gb = svCtx.createLinearGradient(0, 0, 0, SV_H);
        gb.addColorStop(0, 'rgba(0,0,0,0)'); gb.addColorStop(1, 'rgba(0,0,0,1)');
        svCtx.fillStyle = gb; svCtx.fillRect(0, 0, SV_W, SV_H);
        var mx = hsv.s * SV_W, my = (1 - hsv.v) * SV_H;
        svCtx.beginPath(); svCtx.arc(mx, my, 6, 0, Math.PI * 2);
        svCtx.strokeStyle = '#fff'; svCtx.lineWidth = 2; svCtx.stroke();
        svCtx.beginPath(); svCtx.arc(mx, my, 7, 0, Math.PI * 2);
        svCtx.strokeStyle = 'rgba(0,0,0,.4)'; svCtx.lineWidth = 1; svCtx.stroke();
      }
      function applyHsv() {
        var rgb = hsvToRgb(hsv.h, hsv.s, hsv.v);
        var v = byte2(rgb[0]) + byte2(rgb[1]) + byte2(rgb[2]);
        textInput.value = v;
        swatch.style.background = '#' + v;
        drawSv(); drawHue();
      }
      function setFromHex(hex) {
        var c = hexToRgb(hex);
        hsv = rgbToHsv(c.r, c.g, c.b);
        drawSv(); drawHue();
      }
      function dragOn(canvas, onMove) {
        function move(ev) {
          var rect = canvas.getBoundingClientRect();
          onMove(ev.clientX - rect.left, ev.clientY - rect.top);
          onChange();
        }
        canvas.addEventListener('mousedown', function (ev) {
          move(ev);
          document.addEventListener('mousemove', move);
          document.addEventListener('mouseup', function up() {
            document.removeEventListener('mousemove', move);
            document.removeEventListener('mouseup', up);
          });
          ev.preventDefault();
        });
      }
      dragOn(svCanvas, function (x, y) {
        hsv.s = Math.max(0, Math.min(1, x / SV_W));
        hsv.v = Math.max(0, Math.min(1, 1 - y / SV_H));
        applyHsv();
      });
      dragOn(hueCanvas, function (x) {
        hsv.h = Math.max(0, Math.min(359, (x / HUE_W) * 360));
        applyHsv();
      });

      setFromHex(p['default'].toUpperCase());

      textInput.addEventListener('input', function () {
        var v = textInput.value.toUpperCase().replace(/[^0-9A-F]/g, '').slice(0, 6);
        while (v.length < 6) v += '0';
        textInput.value = v;
        swatch.style.background = '#' + v;
        setFromHex(v);
        onChange();
      });
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
