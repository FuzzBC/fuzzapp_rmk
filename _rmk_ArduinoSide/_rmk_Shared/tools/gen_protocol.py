#!/usr/bin/env python3
"""gen_protocol.py - generates the C++, Java, and Python protocol codecs from
protocol_table.json, the single source of truth for the FuZzAPP _rmk_ wire
protocol (see _rmk_docs/01_PROTOCOL.md).

Regenerate after any edit to protocol_table.json:
    python gen_protocol.py

Outputs (all marked GENERATED - do not hand-edit):
    _rmk_ArduinoSide/_rmk_Shared/protocol_opcodes.h        (C++, shared by both boards)
    _rmk_app/.../core/transport/ProtocolOpcodes.java       (Java, app side)
    _rmk_ArduinoSide/_rmk_TestMode/protocol_opcodes.py     (Python, test console)
    _rmk_ArduinoSide/_rmk_TestMode_HTA/.res/protocol_opcodes.js  (ES5 JS, HTA test console)

A CI/pre-commit check should re-run this and diff against the committed
generated files, failing the build if they're stale - see 01_PROTOCOL.md SS8.
This script itself does not enforce that; it only generates.

Fixed-layout opcodes (every field a plain u8/i8/u16/fixed-length byte array)
get a full generated pack/unpack pair per language. Opcodes with a "raw"
field (variable-length or conditionally-shaped payloads - color-sync records,
settings writes, diffuser turn-on, MQTT credentials, diffuser history) only
get their opcode constant generated; their codec is hand-written in each
language's transport layer, next to a comment referencing this table entry's
"note" field for the exact layout.
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED_DIR = os.path.dirname(HERE)
TABLE_PATH = os.path.join(SHARED_DIR, "protocol_table.json")

CPP_OUT = os.path.join(SHARED_DIR, "protocol_opcodes.h")
PY_OUT = os.path.join(os.path.dirname(SHARED_DIR), "_rmk_TestMode", "protocol_opcodes.py")
REPO_ROOT = os.path.dirname(os.path.dirname(SHARED_DIR))  # .../_rmk_ArduinoSide/_rmk_Shared -> repo root
# Targets the real cloned app (com.fuzz.colors) directly under a new
# `protocol` subpackage now - the earlier standalone _rmk_app scaffold
# (com.fuzz.colors.rmk.core.transport) was removed when the project was
# reorganized; see 00_PLAN.md's protocol-adaptation notes.
JAVA_OUT = os.path.join(
    REPO_ROOT, "app", "src", "main", "java",
    "com", "fuzz", "colors", "protocol", "ProtocolOpcodes.java",
)
# ES5 JS for TestMode.hta - mshta.exe hosts JScript (Trident), not a modern
# browser: no let/const/arrow-fn/template-strings/String.repeat. Byte
# payloads are plain JS strings where charCodeAt(i) is wire byte i (matches
# net.js's Net.sendUdp() contract and udp_send.ps1's Latin-1 round trip).
JS_OUT = os.path.join(
    os.path.dirname(SHARED_DIR), "_rmk_TestMode_HTA", ".res", "protocol_opcodes.js",
)

TYPE_SIZES = {"u8": 1, "i8": 1, "u16": 2}

GENERATED_BANNER = (
    "GENERATED FILE - DO NOT EDIT.\n"
    "Source of truth: _rmk_ArduinoSide/_rmk_Shared/protocol_table.json\n"
    "Regenerate with: python _rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py\n"
    "See _rmk_docs/01_PROTOCOL.md for the full protocol specification."
)


def load_table():
    with open(TABLE_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def is_fixed(op):
    return all(f["type"] not in ("raw", "str") for f in op["fields"])


def pascal(name):
    return "".join(part.capitalize() for part in name.split("_"))


# ---------------------------------------------------------------------------
# C++
# ---------------------------------------------------------------------------

def gen_cpp(table):
    lines = []
    lines.append("#pragma once")
    lines.append("/*")
    for l in GENERATED_BANNER.splitlines():
        lines.append(" * " + l)
    lines.append(" */")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("namespace Proto {")
    lines.append("")
    f = table["frame"]
    lines.append("constexpr uint8_t  FRAME_MAGIC          = %s;" % f["magic"])
    lines.append("constexpr uint8_t  FRAME_OVERHEAD_BYTES = %d;" % f["overhead_bytes"])
    lines.append("constexpr uint16_t FRAME_MAX_BYTES      = %d;" % f["max_frame_bytes"])
    lines.append("")
    lines.append("enum class Flag : uint8_t {")
    for b in f["flags_bits"]:
        lines.append("    %-14s = 1 << %d," % (b["name"], b["bit"]))
    lines.append("};")
    lines.append("")
    lines.append("enum class AckResult : uint8_t {")
    for r in table["ack_results"]:
        lines.append("    %-18s = %d," % (r["name"], r["code"]))
    lines.append("};")
    lines.append("const char* AckResultName(AckResult r);")
    lines.append("")
    lines.append("enum class AckTier : uint8_t { FAST, RELAY, SLOW, NONE };")
    lines.append("struct AckTierSpec { uint16_t timeoutMs; uint8_t maxRetries; };")
    lines.append("const AckTierSpec& AckTierOf(AckTier t);")
    lines.append("")
    lines.append("enum class Opcode : uint8_t {")
    for op in table["opcodes"]:
        lines.append("    %-26s = %s,  // link=%s dir=%s ack=%s" % (
            op["name"], op["code"], op["link"], op["dir"], op["ack"]))
    lines.append("};")
    lines.append("const char* OpcodeName(Opcode op);")
    lines.append("")
    lines.append("uint8_t Crc8(const uint8_t* data, size_t len);  // Dallas/Maxim, poly 0x31 - see .cpp")
    lines.append("")
    lines.append("// --- Fixed-layout opcode payloads: generated struct + Pack()/Unpack() pair ---")
    lines.append("// Opcodes with a variable/conditional payload (\"raw\" fields in the table) are")
    lines.append("// NOT generated here - hand-write their codec next to AppLink/DifLink, using the")
    lines.append("// Opcode:: constant above and the matching table entry's \"note\" for the layout.")
    lines.append("")
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        struct_name = pascal(op["name"]) + "Payload"
        lines.append("struct %s {" % struct_name)
        for fld in op["fields"]:
            ctype = {"u8": "uint8_t", "i8": "int8_t", "u16": "uint16_t", "bytes": "uint8_t"}[fld["type"]]
            if fld["type"] == "bytes":
                lines.append("    %s %s[%d];" % (ctype, fld["name"], fld["len"]))
            else:
                lines.append("    %s %s;" % (ctype, fld["name"]))
        lines.append("};")
        size_expr = " + ".join(
            str(fld["len"]) if fld["type"] == "bytes" else str(TYPE_SIZES[fld["type"]])
            for fld in op["fields"]
        )
        lines.append("constexpr size_t %s_SIZE = %s;" % (op["name"], size_expr))
        lines.append("size_t Pack(const %s& p, uint8_t* out);" % struct_name)
        lines.append("bool Unpack(const uint8_t* in, size_t len, %s& out);" % struct_name)
        lines.append("")
    lines.append("} // namespace Proto")
    lines.append("")
    return "\n".join(lines)


def gen_cpp_impl(table):
    """Companion .cpp with the actual Pack/Unpack bodies + name tables + CRC8."""
    lines = []
    lines.append('#include "protocol_opcodes.h"')
    lines.append("/*")
    for l in GENERATED_BANNER.splitlines():
        lines.append(" * " + l)
    lines.append(" */")
    lines.append("")
    lines.append("namespace Proto {")
    lines.append("")
    lines.append("uint8_t Crc8(const uint8_t* data, size_t len) {")
    lines.append("    uint8_t crc = 0;")
    lines.append("    for (size_t i = 0; i < len; i++) {")
    lines.append("        crc ^= data[i];")
    lines.append("        for (uint8_t b = 0; b < 8; b++)")
    lines.append("            crc = (crc & 0x01) ? (uint8_t)((crc >> 1) ^ 0x8C) : (uint8_t)(crc >> 1);")
    lines.append("    }")
    lines.append("    return crc;")
    lines.append("}")
    lines.append("")
    lines.append("const AckTierSpec& AckTierOf(AckTier t) {")
    tiers = table["ack_tiers"]
    order = ["FAST", "RELAY", "SLOW", "NONE"]
    lines.append("    static const AckTierSpec specs[4] = {")
    for name in order:
        spec = tiers[name]
        lines.append("        {%d, %d},  // %s" % (spec["timeout_ms"], spec["max_retries"], name))
    lines.append("    };")
    lines.append("    return specs[(uint8_t)t];")
    lines.append("}")
    lines.append("")
    lines.append("const char* AckResultName(AckResult r) {")
    lines.append("    switch (r) {")
    for res in table["ack_results"]:
        lines.append('        case AckResult::%s: return "%s";' % (res["name"], res["name"]))
    lines.append('        default: return "UNKNOWN";')
    lines.append("    }")
    lines.append("}")
    lines.append("")
    lines.append("const char* OpcodeName(Opcode op) {")
    lines.append("    switch (op) {")
    for op in table["opcodes"]:
        lines.append('        case Opcode::%s: return "%s";' % (op["name"], op["name"]))
    lines.append('        default: return "UNKNOWN";')
    lines.append("    }")
    lines.append("}")
    lines.append("")
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        struct_name = pascal(op["name"]) + "Payload"
        lines.append("size_t Pack(const %s& p, uint8_t* out) {" % struct_name)
        lines.append("    size_t i = 0;")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] in ("u8", "i8"):
                lines.append("    out[i++] = (uint8_t)p.%s;" % n)
            elif fld["type"] == "u16":
                lines.append("    out[i++] = (uint8_t)(p.%s >> 8); out[i++] = (uint8_t)(p.%s & 0xFF);" % (n, n))
            elif fld["type"] == "bytes":
                lines.append("    for (size_t k = 0; k < %d; k++) out[i++] = p.%s[k];" % (fld["len"], n))
        lines.append("    return i;")
        lines.append("}")
        lines.append("bool Unpack(const uint8_t* in, size_t len, %s& out) {" % struct_name)
        lines.append("    if (len < %s_SIZE) return false;" % op["name"])
        lines.append("    size_t i = 0;")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] == "u8":
                lines.append("    out.%s = in[i++];" % n)
            elif fld["type"] == "i8":
                lines.append("    out.%s = (int8_t)in[i++];" % n)
            elif fld["type"] == "u16":
                lines.append("    out.%s = (uint16_t)((in[i] << 8) | in[i+1]); i += 2;" % n)
            elif fld["type"] == "bytes":
                lines.append("    for (size_t k = 0; k < %d; k++) out.%s[k] = in[i++];" % (fld["len"], n))
        lines.append("    return true;")
        lines.append("}")
        lines.append("")
    lines.append("} // namespace Proto")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Java
# ---------------------------------------------------------------------------

def gen_java(table):
    lines = []
    lines.append("package com.fuzz.colors.protocol;")
    lines.append("")
    lines.append("/*")
    for l in GENERATED_BANNER.splitlines():
        lines.append(" * " + l)
    lines.append(" */")
    lines.append("public final class ProtocolOpcodes {")
    lines.append("    private ProtocolOpcodes() {}")
    lines.append("")
    f = table["frame"]
    lines.append("    public static final byte FRAME_MAGIC = (byte) %s;" % f["magic"])
    lines.append("    public static final int FRAME_OVERHEAD_BYTES = %d;" % f["overhead_bytes"])
    lines.append("    public static final int FRAME_MAX_BYTES = %d;" % f["max_frame_bytes"])
    lines.append("")
    lines.append("    public static final class Flag {")
    lines.append("        private Flag() {}")
    for b in f["flags_bits"]:
        lines.append("        public static final int %s = 1 << %d;" % (b["name"], b["bit"]))
    lines.append("    }")
    lines.append("")
    lines.append("    public static final class AckResult {")
    lines.append("        private AckResult() {}")
    for r in table["ack_results"]:
        lines.append("        public static final int %s = %d;" % (r["name"], r["code"]))
    lines.append("    }")
    lines.append("")
    lines.append("    public enum AckTier { FAST(%d, %d), RELAY(%d, %d), SLOW(%d, %d), NONE(%d, %d);" % (
        table["ack_tiers"]["FAST"]["timeout_ms"], table["ack_tiers"]["FAST"]["max_retries"],
        table["ack_tiers"]["RELAY"]["timeout_ms"], table["ack_tiers"]["RELAY"]["max_retries"],
        table["ack_tiers"]["SLOW"]["timeout_ms"], table["ack_tiers"]["SLOW"]["max_retries"],
        table["ack_tiers"]["NONE"]["timeout_ms"], table["ack_tiers"]["NONE"]["max_retries"],
    ))
    lines.append("        public final int timeoutMs; public final int maxRetries;")
    lines.append("        AckTier(int t, int r) { timeoutMs = t; maxRetries = r; }")
    lines.append("    }")
    lines.append("")
    lines.append("    public static final class Opcode {")
    lines.append("        private Opcode() {}")
    for op in table["opcodes"]:
        lines.append("        public static final byte %s = (byte) %s;  // link=%s dir=%s ack=%s" % (
            op["name"], op["code"], op["link"], op["dir"], op["ack"]))
    lines.append("    }")
    lines.append("")
    lines.append("    public static String opcodeName(byte op) {")
    lines.append("        switch (op) {")
    for op in table["opcodes"]:
        lines.append('            case Opcode.%s: return "%s";' % (op["name"], op["name"]))
    lines.append('            default: return "UNKNOWN";')
    lines.append("        }")
    lines.append("    }")
    lines.append("")
    lines.append("    /** Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() in protocol_opcodes.cpp bit for bit. */")
    lines.append("    public static byte crc8(byte[] data, int offset, int len) {")
    lines.append("        int crc = 0;")
    lines.append("        for (int i = offset; i < offset + len; i++) {")
    lines.append("            crc ^= (data[i] & 0xFF);")
    lines.append("            for (int b = 0; b < 8; b++)")
    lines.append("                crc = ((crc & 0x01) != 0) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);")
    lines.append("        }")
    lines.append("        return (byte) crc;")
    lines.append("    }")
    lines.append("")
    lines.append("    // --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---")
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        cls = pascal(op["name"]) + "Payload"
        size_expr = " + ".join(
            str(fld["len"]) if fld["type"] == "bytes" else str(TYPE_SIZES[fld["type"]])
            for fld in op["fields"]
        )
        lines.append("    public static final class %s {" % cls)
        for fld in op["fields"]:
            jtype = {"u8": "int", "i8": "int", "u16": "int", "bytes": "byte[]"}[fld["type"]]
            lines.append("        public %s %s;" % (jtype, fld["name"]))
        lines.append("        public static final int SIZE = %s;" % size_expr)
        lines.append("        public byte[] pack() {")
        lines.append("            byte[] out = new byte[SIZE];")
        lines.append("            int i = 0;")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] in ("u8", "i8"):
                lines.append("            out[i++] = (byte) %s;" % n)
            elif fld["type"] == "u16":
                lines.append("            out[i++] = (byte) ((%s >> 8) & 0xFF); out[i++] = (byte) (%s & 0xFF);" % (n, n))
            elif fld["type"] == "bytes":
                lines.append("            System.arraycopy(%s, 0, out, i, %d); i += %d;" % (n, fld["len"], fld["len"]))
        lines.append("            return out;")
        lines.append("        }")
        lines.append("        public static %s unpack(byte[] in, int offset) {" % cls)
        lines.append("            %s p = new %s();" % (cls, cls))
        lines.append("            int i = offset;")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] in ("u8", "i8"):
                lines.append("            p.%s = in[i++] & 0xFF;" % n)
            elif fld["type"] == "u16":
                lines.append("            p.%s = ((in[i] & 0xFF) << 8) | (in[i+1] & 0xFF); i += 2;" % n)
            elif fld["type"] == "bytes":
                lines.append("            p.%s = new byte[%d]; System.arraycopy(in, i, p.%s, 0, %d); i += %d;" % (n, fld["len"], n, fld["len"], fld["len"]))
        lines.append("            return p;")
        lines.append("        }")
        lines.append("    }")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Python
# ---------------------------------------------------------------------------

def gen_python(table):
    lines = []
    lines.append('"""')
    lines.append(GENERATED_BANNER)
    lines.append('"""')
    lines.append("import struct")
    lines.append("")
    f = table["frame"]
    lines.append("FRAME_MAGIC = %s" % f["magic"])
    lines.append("FRAME_OVERHEAD_BYTES = %d" % f["overhead_bytes"])
    lines.append("FRAME_MAX_BYTES = %d" % f["max_frame_bytes"])
    lines.append("")
    lines.append("class Flag:")
    for b in f["flags_bits"]:
        lines.append("    %s = 1 << %d" % (b["name"], b["bit"]))
    lines.append("")
    lines.append("class AckResult:")
    for r in table["ack_results"]:
        lines.append("    %s = %d" % (r["name"], r["code"]))
    lines.append("")
    lines.append("ACK_RESULT_NAMES = {")
    for r in table["ack_results"]:
        lines.append('    %d: "%s",' % (r["code"], r["name"]))
    lines.append("}")
    lines.append("")
    lines.append("ACK_TIERS = {")
    for name, spec in table["ack_tiers"].items():
        lines.append('    "%s": {"timeout_ms": %d, "max_retries": %d},' % (name, spec["timeout_ms"], spec["max_retries"]))
    lines.append("}")
    lines.append("")
    lines.append("class Opcode:")
    for op in table["opcodes"]:
        lines.append("    %s = %s  # link=%s dir=%s ack=%s" % (op["name"], op["code"], op["link"], op["dir"], op["ack"]))
    lines.append("")
    lines.append("OPCODE_NAMES = {")
    for op in table["opcodes"]:
        lines.append('    %s: "%s",' % (op["code"], op["name"]))
    lines.append("}")
    lines.append("")
    lines.append("def crc8(data):")
    lines.append('    """Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() in protocol_opcodes.cpp bit for bit."""')
    lines.append("    crc = 0")
    lines.append("    for byte in data:")
    lines.append("        crc ^= byte")
    lines.append("        for _ in range(8):")
    lines.append("            crc = ((crc >> 1) ^ 0x8C) if (crc & 0x01) else (crc >> 1)")
    lines.append("    return crc & 0xFF")
    lines.append("")
    lines.append("# --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---")
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        fmt = ">"  # big-endian, no padding
        names = []
        for fld in op["fields"]:
            names.append(fld["name"])
            if fld["type"] in ("u8",):
                fmt += "B"
            elif fld["type"] == "i8":
                fmt += "b"
            elif fld["type"] == "u16":
                fmt += "H"
            elif fld["type"] == "bytes":
                fmt += "%ds" % fld["len"]
        lines.append('def pack_%s(%s):' % (op["name"].lower(), ", ".join(names)))
        lines.append('    return struct.pack("%s", %s)' % (fmt, ", ".join(names)))
        lines.append('def unpack_%s(data):' % op["name"].lower())
        lines.append('    return dict(zip(%r, struct.unpack("%s", data[:struct.calcsize("%s")])))' % (names, fmt, fmt))
        lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# JavaScript (ES5, for TestMode.hta / mshta.exe's JScript engine)
# ---------------------------------------------------------------------------

def gen_javascript(table):
    lines = []
    lines.append("/*")
    for l in GENERATED_BANNER.splitlines():
        lines.append(" * " + l)
    lines.append(" */")
    lines.append("var Proto = (function () {")
    lines.append("  'use strict';")
    lines.append("")
    f = table["frame"]
    lines.append("  var FRAME_MAGIC = %s;" % f["magic"])
    lines.append("  var FRAME_OVERHEAD_BYTES = %d;" % f["overhead_bytes"])
    lines.append("  var FRAME_MAX_BYTES = %d;" % f["max_frame_bytes"])
    lines.append("")
    # JScript (mshta.exe's engine) rejects a trailing comma before a
    # closing }/] - unlike every modern engine, so every object/array
    # literal below is built as entries joined with ",\n", never a
    # comma appended unconditionally to the last one.
    # entries are (code, trailing_comment_or_None) pairs - the comma goes
    # right after `code`, BEFORE any // comment, or it'd land inside the
    # comment text and silently stop being a real separator at all.
    def obj_literal(var_decl, entries, indent="    "):
        lines.append(var_decl + " {")
        for i, item in enumerate(entries):
            code, comment = item if isinstance(item, tuple) else (item, None)
            suffix = "," if i < len(entries) - 1 else ""
            lines.append(indent + code + suffix + (" " + comment if comment else ""))
        lines.append("  };")

    obj_literal("  var Flag =", ["%s: 1 << %d" % (b["name"], b["bit"]) for b in f["flags_bits"]])
    lines.append("")
    obj_literal("  var AckResult =", ["%s: %d" % (r["name"], r["code"]) for r in table["ack_results"]])
    obj_literal("  var ACK_RESULT_NAMES =", ['%d: "%s"' % (r["code"], r["name"]) for r in table["ack_results"]])
    lines.append("")
    obj_literal("  var ACK_TIERS =", ['"%s": { timeoutMs: %d, maxRetries: %d }' % (name, spec["timeout_ms"], spec["max_retries"])
                                       for name, spec in table["ack_tiers"].items()])
    lines.append("")
    obj_literal("  var Opcode =", [("%s: %s" % (op["name"], op["code"]),
                                     "// link=%s dir=%s ack=%s" % (op["link"], op["dir"], op["ack"]))
                                    for op in table["opcodes"]])
    obj_literal("  var OPCODE_NAMES =", ['%s: "%s"' % (op["code"], op["name"]) for op in table["opcodes"]])
    lines.append("")
    lines.append("  // Dallas/Maxim CRC8, poly 0x31 - must match Proto::Crc8() bit for bit.")
    lines.append("  // `bytes` is a JS string where charCodeAt(i) is wire byte i (0-255).")
    lines.append("  function crc8(bytes) {")
    lines.append("    var crc = 0;")
    lines.append("    for (var i = 0; i < bytes.length; i++) {")
    lines.append("      crc ^= bytes.charCodeAt(i) & 0xFF;")
    lines.append("      for (var b = 0; b < 8; b++)")
    lines.append("        crc = (crc & 0x01) ? ((crc >> 1) ^ 0x8C) : (crc >> 1);")
    lines.append("    }")
    lines.append("    return crc & 0xFF;")
    lines.append("  }")
    lines.append("")
    lines.append("  // --- Fixed-layout opcode payload codecs (see the C++ header's same comment) ---")
    lines.append("  // packX(...) returns a byte-string (String.fromCharCode per byte); unpackX(data,")
    lines.append("  // offset) reads from a byte-string starting at offset (default 0) and returns a")
    lines.append("  // plain object keyed by field name. u16/bytes fields are big-endian, matching")
    lines.append("  // every other generated language.")
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        fname = pascal(op["name"])
        names = [fld["name"] for fld in op["fields"]]
        lines.append("  function pack%s(%s) {" % (fname, ", ".join(names)))
        lines.append("    var s = '';")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] in ("u8", "i8"):
                lines.append("    s += String.fromCharCode(%s & 0xFF);" % n)
            elif fld["type"] == "u16":
                lines.append("    s += String.fromCharCode((%s >> 8) & 0xFF); s += String.fromCharCode(%s & 0xFF);" % (n, n))
            elif fld["type"] == "bytes":
                lines.append("    for (var i%s = 0; i%s < %d; i%s++) s += String.fromCharCode(%s.charCodeAt(i%s) & 0xFF);" % (n, n, fld["len"], n, n, n))
        lines.append("    return s;")
        lines.append("  }")
        lines.append("  function unpack%s(data, offset) {" % fname)
        lines.append("    offset = offset || 0;")
        lines.append("    var out = {}, i = offset;")
        for fld in op["fields"]:
            n = fld["name"]
            if fld["type"] == "u8":
                lines.append("    out.%s = data.charCodeAt(i) & 0xFF; i += 1;" % n)
            elif fld["type"] == "i8":
                lines.append("    out.%s = data.charCodeAt(i) & 0xFF; if (out.%s >= 128) out.%s -= 256; i += 1;" % (n, n, n))
            elif fld["type"] == "u16":
                lines.append("    out.%s = ((data.charCodeAt(i) & 0xFF) << 8) | (data.charCodeAt(i + 1) & 0xFF); i += 2;" % n)
            elif fld["type"] == "bytes":
                lines.append("    out.%s = data.substr(i, %d); i += %d;" % (n, fld["len"], fld["len"]))
        lines.append("    return out;")
        lines.append("  }")
        lines.append("")
    export_entries = ["FRAME_MAGIC: FRAME_MAGIC, FRAME_OVERHEAD_BYTES: FRAME_OVERHEAD_BYTES, FRAME_MAX_BYTES: FRAME_MAX_BYTES",
                       "Flag: Flag, AckResult: AckResult, ACK_RESULT_NAMES: ACK_RESULT_NAMES, ACK_TIERS: ACK_TIERS",
                       "Opcode: Opcode, OPCODE_NAMES: OPCODE_NAMES, crc8: crc8"]
    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        fname = pascal(op["name"])
        export_entries.append("pack%s: pack%s, unpack%s: unpack%s" % (fname, fname, fname, fname))
    obj_literal("  return", export_entries)
    lines.append("})();")
    lines.append("")
    return "\n".join(lines)


def write(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    print("wrote", path)


def main():
    table = load_table()
    write(CPP_OUT, gen_cpp(table))
    write(os.path.join(SHARED_DIR, "protocol_opcodes.cpp"), gen_cpp_impl(table))
    write(JAVA_OUT, gen_java(table))
    write(PY_OUT, gen_python(table))
    write(JS_OUT, gen_javascript(table))


if __name__ == "__main__":
    main()
