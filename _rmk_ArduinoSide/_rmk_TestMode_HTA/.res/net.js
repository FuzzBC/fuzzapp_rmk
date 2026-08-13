/* net.js - real UDP/TCP sends for TestMode.hta.
 *
 * mshta.exe hosts JScript inside Trident/MSHTML - there is no socket API
 * available to script at all (no WebSocket, no raw UDP/TCP, nothing). The
 * only way to actually put a packet on the wire from here is to shell out
 * to a real process that can - so every send launches udp_send.ps1 or
 * tcp_send.ps1 (in this same .res folder).
 *
 * Launched via WshShell.Run(cmd, 0, false) - windowStyle 0 = hidden, the
 * documented way to keep the child window from ever flashing on screen -
 * NOT WshShell.Exec(), which always briefly shows a console window
 * because it needs one to attach StdOut/StdErr pipes to. Since Run()
 * doesn't hand back a way to read the child's output, each .ps1 script
 * instead writes its result to a file (-OutFile) and this side polls the
 * filesystem for it to appear. To avoid ever reading a half-written file,
 * the script writes to "<OutFile>.part" and renames it to "<OutFile>"
 * only once it's completely done - so "the file exists" and "the file is
 * complete" are the same moment.
 *
 * The result file holds a small, deliberately hex-encoded protocol
 * instead of raw text:
 *   OK\r\n<replyCount>\r\n<hex bytes of reply 1>\r\n<hex bytes of reply 2>\r\n...
 *   TIMEOUT\r\n<message>
 *   ERROR\r\n<message>
 * Hex-encoding every reply (rather than writing it as text, or joining
 * replies with a delimiter character) means the file only ever holds
 * plain ASCII, regardless of what's actually in the reply - which matters
 * because the LED colour-sync 'LK' reply is a raw BINARY packet (arbitrary
 * byte values, not text), and any text-based encoding/delimiter risks
 * corrupting or mis-splitting it.
 */

var Net = (function () {
  'use strict';

  var shell = new ActiveXObject('WScript.Shell');
  var fso = new ActiveXObject('Scripting.FileSystemObject');

  // This .hta's own folder, so script paths work no matter what the
  // process's current directory happens to be (double-click, shortcut,
  // "Run as administrator", ...).
  function computeBaseDir() {
    var p = location.href;
    p = p.replace(/^file:\/\/\//, '');
    try { p = decodeURIComponent(p); } catch (e) { /* leave as-is */ }
    p = p.replace(/\//g, '\\');
    var i = p.lastIndexOf('\\');
    return i === -1 ? p : p.substring(0, i);
  }
  var BASE_DIR = computeBaseDir();
  var RES_DIR = BASE_DIR + '\\.res';

  function q(s) {
    // Win32/CreateProcess argument quoting (what WshShell.Exec and
    // powershell.exe's own argv both expect) - wrap in quotes, escape any
    // embedded quote as \" . Our payloads are protocol strings (hex,
    // letters, #/@/!) that never legitimately contain a quote, but the
    // raw-command box is free text, so this is a real (if unlikely) input.
    return '"' + String(s).replace(/"/g, '\\"') + '"';
  }

  // UTF-8 decode, not a 1:1 byte->char map - the Diffuser's Telnet console
  // banner uses real UTF-8 box-drawing characters (e.g. U+2500 '-', 3 bytes
  // E2 94 80), and treating each byte as its own Latin-1 code point turned
  // every one of those into 3 garbled characters ("a" + two control chars)
  // instead of one. Handles the 1/2/3-byte UTF-8 forms actually seen on the
  // wire (ASCII console text plus a handful of box-drawing/status glyphs);
  // any byte that doesn't decode cleanly falls back to its raw code point
  // rather than throwing, since this is diagnostic console text, not a
  // strict protocol field.
  function hexToStr(hex) {
    var bytes = [];
    for (var i = 0; i + 1 < hex.length; i += 2) bytes.push(parseInt(hex.substr(i, 2), 16));
    var out = '', n = bytes.length;
    for (i = 0; i < n; i++) {
      var b0 = bytes[i];
      if (b0 < 0x80) {
        out += String.fromCharCode(b0);
      } else if ((b0 & 0xE0) === 0xC0 && i + 1 < n && (bytes[i + 1] & 0xC0) === 0x80) {
        out += String.fromCharCode(((b0 & 0x1F) << 6) | (bytes[i + 1] & 0x3F));
        i += 1;
      } else if ((b0 & 0xF0) === 0xE0 && i + 2 < n && (bytes[i + 1] & 0xC0) === 0x80 && (bytes[i + 2] & 0xC0) === 0x80) {
        out += String.fromCharCode(((b0 & 0x0F) << 12) | ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F));
        i += 2;
      } else if ((b0 & 0xF8) === 0xF0 && i + 3 < n && (bytes[i + 1] & 0xC0) === 0x80 && (bytes[i + 2] & 0xC0) === 0x80 && (bytes[i + 3] & 0xC0) === 0x80) {
        // Surrogate pair - codepoint > 0xFFFF, never actually emitted by
        // this console's text but handled for completeness.
        var cp = ((b0 & 0x07) << 18) | ((bytes[i + 1] & 0x3F) << 12) | ((bytes[i + 2] & 0x3F) << 6) | (bytes[i + 3] & 0x3F);
        cp -= 0x10000;
        out += String.fromCharCode(0xD800 + (cp >> 10), 0xDC00 + (cp & 0x3FF));
        i += 3;
      } else {
        out += String.fromCharCode(b0);
      }
    }
    return out;
  }
  function hex2(b) { var s = (b & 0xFF).toString(16); return s.length < 2 ? '0' + s : s; }
  // Payload bytes go on the command line hex-encoded, never raw - a raw
  // binary payload (any byte 0-255) embedded in a Win32 command line gets
  // silently truncated at the first embedded NUL byte (confirmed live:
  // WshShell.Run's child process invocation fails outright, no output
  // file, no visible error). Any opcode whose payload happens to contain
  // a zero byte anywhere - extremely common: a colour channel of 0, an
  // id/value of 0, a disabled flag, mode 0, etc. - silently never reached
  // the wire under the old raw-string param. Hex is command-line-safe by
  // construction (only 0-9a-f), so this sidesteps the whole bug class
  // instead of trying to escape around it (see udp_send.ps1/tcp_send.ps1's
  // matching -PayloadHex decode side).
  function strToHex(s) {
    var out = '';
    for (var i = 0; i < s.length; i++) out += hex2(s.charCodeAt(i));
    return out;
  }

  var seqCounter = 0;
  function uniqueName() {
    seqCounter++;
    return 'out_' + new Date().getTime().toString(36) + '_' + seqCounter + '_' + Math.floor(Math.random() * 46656).toString(36);
  }

  function readAndDelete(path) {
    var text = '';
    try {
      var stream = fso.OpenTextFile(path, 1, false, 0); // ForReading, ASCII
      text = stream.ReadAll();
      stream.Close();
    } catch (e) { /* best effort */ }
    try { fso.DeleteFile(path, true); } catch (e2) { /* best effort */ }
    return text;
  }

  // Runs one command line completely hidden (no console flash - see this
  // file's header) and polls the filesystem for its result file instead of
  // reading a stdout pipe. hardTimeoutMs is a last-resort ceiling
  // independent of whatever timeout the .ps1 script itself was told to
  // use, in case PowerShell never starts (missing install, AV
  // interference, policy prompt, ...) - Run() gives no process handle to
  // kill in that case, so this just stops waiting and reports an error;
  // the .ps1's own -TimeoutMs/-MaxTimeoutMs bound is what actually keeps
  // that from happening in practice.
  function runHiddenAsync(cmdLine, outFile, hardTimeoutMs, onDone) {
    try {
      shell.Run(cmdLine, 0, false);
    } catch (e) {
      onDone({ ok: false, message: 'launch failed: ' + e.message });
      return;
    }
    var startedAt = new Date().getTime();
    var timer = window.setInterval(function () {
      if (fso.FileExists(outFile)) {
        window.clearInterval(timer);
        onDone({ ok: true, stdout: readAndDelete(outFile) });
        return;
      }
      if (new Date().getTime() - startedAt > hardTimeoutMs) {
        window.clearInterval(timer);
        onDone({ ok: false, message: 'powershell did not finish within ' + hardTimeoutMs + 'ms (hard ceiling)' });
      }
    }, 50);
  }

  function parseEnvelope(stdout) {
    var lines = (stdout || '').replace(/\r\n/g, '\n').split('\n');
    while (lines.length && lines[lines.length - 1] === '') lines.pop();
    var status = lines[0] || '';
    if (status === 'OK') {
      var count = parseInt(lines[1], 10);
      if (isNaN(count)) count = 0;
      var replies = [];
      for (var i = 0; i < count; i++) replies.push(hexToStr(lines[2 + i] || ''));
      return { status: 'OK', replies: replies };
    }
    if (status === 'TIMEOUT' || status === 'ERROR') {
      return { status: status, message: lines[1] || '', replies: [] };
    }
    return { status: 'ERROR', message: 'unrecognised script output: ' + (stdout || '').slice(0, 200), replies: [] };
  }

  function sendUdp(ip, port, payload, timeoutMs, cb) {
    var script = RES_DIR + '\\udp_send.ps1';
    var outFile = RES_DIR + '\\' + uniqueName() + '.txt';
    var maxTimeout = Math.max(timeoutMs * 4, 6000);
    var cmd = 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File ' + q(script) +
      ' -IP ' + q(ip) + ' -Port ' + parseInt(port, 10) + ' -PayloadHex ' + q(strToHex(payload)) +
      ' -TimeoutMs ' + parseInt(timeoutMs, 10) + ' -MaxTimeoutMs ' + maxTimeout + ' -OutFile ' + q(outFile);
    runHiddenAsync(cmd, outFile, maxTimeout + 4000, function (res) {
      if (!res.ok) { cb({ status: 'ERROR', message: res.message, replies: [] }); return; }
      cb(parseEnvelope(res.stdout));
    });
  }

  function sendTcp(ip, port, payload, timeoutMs, cb) {
    var script = RES_DIR + '\\tcp_send.ps1';
    var outFile = RES_DIR + '\\' + uniqueName() + '.txt';
    // Same idle-timeout/hard-cap split as sendUdp() - see tcp_send.ps1's
    // header. maxTimeout must reach this watchdog too, or a genuinely long
    // multi-chunk console reply that the .ps1 is correctly still waiting
    // on would get reported as an error here before it ever finishes.
    var maxTimeout = Math.max(timeoutMs * 4, 6000);
    var cmd = 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -WindowStyle Hidden -File ' + q(script) +
      ' -IP ' + q(ip) + ' -Port ' + parseInt(port, 10) + ' -PayloadHex ' + q(strToHex(payload)) +
      ' -TimeoutMs ' + parseInt(timeoutMs, 10) + ' -MaxTimeoutMs ' + maxTimeout + ' -OutFile ' + q(outFile);
    runHiddenAsync(cmd, outFile, maxTimeout + 4000, function (res) {
      if (!res.ok) { cb({ status: 'ERROR', message: res.message, replies: [] }); return; }
      cb(parseEnvelope(res.stdout));
    });
  }

  // ------------------------------------------------------- log file I/O
  function writeTextFile(path, text) {
    // TristateTrue (-1) = Unicode - matches what Notepad/most editors
    // expect so the saved log opens cleanly if double-clicked.
    var stream = fso.CreateTextFile(path, true, -1);
    stream.Write(text);
    stream.Close();
  }
  function ensureFolder(path) {
    if (!fso.FolderExists(path)) fso.CreateFolder(path);
  }
  function openInNotepad(path) {
    try { shell.Run('notepad.exe ' + q(path), 1, false); } catch (e) { /* best effort, non-fatal */ }
  }

  return {
    baseDir: BASE_DIR, resDir: RES_DIR,
    sendUdp: sendUdp, sendTcp: sendTcp,
    writeTextFile: writeTextFile, ensureFolder: ensureFolder, openInNotepad: openInNotepad
  };
})();
