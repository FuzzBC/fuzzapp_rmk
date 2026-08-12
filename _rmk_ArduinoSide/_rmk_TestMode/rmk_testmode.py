#!/usr/bin/env python3
"""rmk_testmode.py - protocol console for the _rmk_ FuZzAPP rewrite.

Successor to TestMode_APP's Tkinter tool, rebuilt on the generated codec
(protocol_opcodes.py - see gen_protocol.py) instead of a hand-written command
table, so this tool can never silently drift from the real wire protocol the
way a hand-maintained table theoretically could (00_PLAN.md's build plan for
_rmk_TestMode, and 01_PROTOCOL.md SS8).

CLI first, not a GUI - a working bring-up tool for Phase 3/4 firmware testing
now; a GUI polish pass (mirroring TestMode_APP's Tkinter UI) is follow-up
work, not required to unblock the firmware phases.

Usage:
    python rmk_testmode.py --host 192.168.1.202 --port 8472 send HELLO proto_version=1
    python rmk_testmode.py --host 192.168.1.202 --port 8472 send LED_SET_COLOR r=255 g=0 b=0
    python rmk_testmode.py --host 192.168.1.202 --port 8472 send KEEPALIVE
    python rmk_testmode.py --host 192.168.1.202 --port 8472 listen

Frame format (see 01_PROTOCOL.md SS1): MAGIC(1) FLAGS(1) SEQ(1) OPCODE(1)
PAYLOAD(0-247) CRC8(1). No LEN field - UDP datagram size carries it.
"""
import argparse
import socket
import sys
import time

import protocol_opcodes as proto

FLAG_ACK_REQ = proto.Flag.ACK_REQ
FLAG_IS_ACK = proto.Flag.IS_ACK
FLAG_IS_TELEMETRY = proto.Flag.IS_TELEMETRY


def build_frame(opcode, seq, payload=b"", flags=0):
    """MAGIC + FLAGS + SEQ + OPCODE + payload + CRC8 (over every preceding byte)."""
    header = bytes([proto.FRAME_MAGIC, flags, seq & 0xFF, opcode])
    body = header + payload
    crc = proto.crc8(body)
    frame = body + bytes([crc])
    if len(frame) > proto.FRAME_MAX_BYTES:
        raise ValueError("frame too large: %d > %d" % (len(frame), proto.FRAME_MAX_BYTES))
    return frame


def parse_frame(data):
    """Returns dict{ok, flags, seq, opcode, opcode_name, payload} or {ok: False, reason}."""
    if len(data) < proto.FRAME_OVERHEAD_BYTES:
        return {"ok": False, "reason": "too short (%d bytes)" % len(data)}
    if data[0] != proto.FRAME_MAGIC:
        return {"ok": False, "reason": "bad magic 0x%02X" % data[0]}
    body, crc_byte = data[:-1], data[-1]
    if proto.crc8(body) != crc_byte:
        return {"ok": False, "reason": "CRC mismatch"}
    flags, seq, opcode = data[1], data[2], data[3]
    payload = data[4:-1]
    return {
        "ok": True,
        "flags": flags,
        "seq": seq,
        "opcode": opcode,
        "opcode_name": proto.OPCODE_NAMES.get(opcode, "0x%02X (unknown)" % opcode),
        "payload": payload,
    }


def describe_payload(opcode, payload):
    """Best-effort decode using the generated fixed-layout unpack_*() functions,
    falling back to raw hex for "raw"/variable opcodes (color-sync, settings
    write, diffuser turn-on, etc - see protocol_table.json's "note" fields)."""
    name = proto.OPCODE_NAMES.get(opcode)
    if name is None:
        return payload.hex()
    if opcode == proto.Opcode.ACK:
        if len(payload) < 1:
            return "(empty)"
        result = payload[0]
        extra = ""
        if result == proto.AckResult.VERSION_MISMATCH and len(payload) >= 2:
            extra = " max_supported_version=%d" % payload[1]
        return "result=%s(%d)%s" % (proto.ACK_RESULT_NAMES.get(result, "?"), result, extra)
    fn = getattr(proto, "unpack_%s" % name.lower(), None)
    if fn is None:
        return payload.hex() if payload else "(no payload / raw codec - see protocol_table.json)"
    try:
        return repr(fn(payload))
    except Exception as e:  # noqa: BLE001
        return "decode error (%s) raw=%s" % (e, payload.hex())


def cmd_send(args):
    opcode_name = args.opcode.upper()
    opcode = getattr(proto.Opcode, opcode_name, None)
    if opcode is None:
        print("Unknown opcode '%s'. Known opcodes:" % args.opcode)
        for n in sorted(proto.OPCODE_NAMES.values()):
            print("  " + n)
        sys.exit(1)

    fields = {}
    for kv in args.fields:
        if "=" not in kv:
            print("Bad field '%s', expected name=value" % kv)
            sys.exit(1)
        k, v = kv.split("=", 1)
        fields[k] = int(v, 0)

    pack_fn = getattr(proto, "pack_%s" % opcode_name.lower(), None)
    if pack_fn is None:
        if fields:
            print("Opcode %s has no generated fixed-layout codec (raw/variable payload) - "
                  "pass --raw-hex instead of name=value fields." % opcode_name)
            sys.exit(1)
        payload = bytes.fromhex(args.raw_hex) if args.raw_hex else b""
    else:
        try:
            import inspect
            sig_names = inspect.signature(pack_fn).parameters.keys()
            payload = pack_fn(*[fields[n] for n in sig_names])
        except KeyError as e:
            print("Missing field %s for %s" % (e, opcode_name))
            sys.exit(1)

    flags = FLAG_ACK_REQ if args.ack else 0
    frame = build_frame(opcode, args.seq, payload, flags)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(args.timeout)
    # Both firmwares reply to the FIXED port they themselves listen on (see
    # AppLink.cpp's rawSend(): APP_UDP.beginPacket(APP_RECV_IP, APP_UDP_PORT)),
    # not to our ephemeral source port - bind here or every ack/reply is sent
    # into a socket nobody's listening on and this silently "times out"
    # forever even though the device answered every single time. Matches
    # udp_send.ps1 (TestMode.hta's own sender) and the original net.py.
    sock.bind(("", args.port))
    sock.sendto(frame, (args.host, args.port))
    print("-> %s seq=%d flags=0x%02X payload=%s (%d bytes on wire)" % (
        opcode_name, args.seq, flags, payload.hex(), len(frame)))

    if args.ack or args.wait:
        deadline = time.time() + args.timeout
        while time.time() < deadline:
            try:
                data, addr = sock.recvfrom(proto.FRAME_MAX_BYTES)
            except socket.timeout:
                break
            parsed = parse_frame(data)
            _print_received(addr, parsed)
            if parsed["ok"] and (parsed["flags"] & FLAG_IS_ACK) and parsed["seq"] == args.seq:
                break


def cmd_listen(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    print("Listening on %s:%d (Ctrl+C to stop)..." % (args.bind, args.port))
    while True:
        data, addr = sock.recvfrom(proto.FRAME_MAX_BYTES)
        _print_received(addr, parse_frame(data))


def _print_received(addr, parsed):
    if not parsed["ok"]:
        print("<- %s:%d  MALFORMED (%s)" % (addr[0], addr[1], parsed["reason"]))
        return
    tag = "ACK" if (parsed["flags"] & FLAG_IS_ACK) else (
        "TELEM" if (parsed["flags"] & FLAG_IS_TELEMETRY) else "CMD")
    print("<- %s:%d  [%s] %s seq=%d  %s" % (
        addr[0], addr[1], tag, parsed["opcode_name"], parsed["seq"],
        describe_payload(parsed["opcode"], parsed["payload"])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.1.202", help="target device IP (default: SmartTV)")
    ap.add_argument("--port", type=int, default=8472, help="target UDP port (default: 8472, Link B)")
    ap.add_argument("--bind", default="0.0.0.0", help="listen bind address for the 'listen' command")
    ap.add_argument("--timeout", type=float, default=3.0, help="seconds to wait for a reply/ack")

    sub = ap.add_subparsers(dest="command", required=True)

    p_send = sub.add_parser("send", help="build+send one frame")
    p_send.add_argument("opcode", help="opcode name, e.g. HELLO, LED_SET_COLOR, KEEPALIVE")
    p_send.add_argument("fields", nargs="*", help="field=value pairs for fixed-layout opcodes")
    p_send.add_argument("--seq", type=int, default=1, help="sequence number (default: 1)")
    p_send.add_argument("--ack", action="store_true", help="set ACK_REQ flag and wait for the matching ack")
    p_send.add_argument("--wait", action="store_true", help="wait for any reply even without --ack")
    p_send.add_argument("--raw-hex", help="raw hex payload, for opcodes with no generated codec")
    p_send.set_defaults(func=cmd_send)

    p_listen = sub.add_parser("listen", help="print every frame received, decoded")
    p_listen.set_defaults(func=cmd_listen)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
