#!/usr/bin/env python3
"""test_protocol.py - round-trip check for every fixed-layout opcode in
protocol_table.json, using the generated Python codec (protocol_opcodes.py).
Not a substitute for cross-language fixture testing (see gen_protocol.py's
docstring - a CI step should also diff freshly-generated files against the
committed ones), but catches table/generator mistakes cheaply, without
hardware or a C++/Java toolchain.

Run: python test_protocol.py
"""
import json
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SHARED_DIR = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(SHARED_DIR, "..", "_rmk_TestMode"))

import protocol_opcodes as proto  # noqa: E402

TABLE_PATH = os.path.join(SHARED_DIR, "protocol_table.json")


def load_table():
    with open(TABLE_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def is_fixed(op):
    return all(f["type"] not in ("raw", "str") for f in op["fields"])


def random_value(fld, rng):
    if fld["type"] == "u8":
        return rng.randint(0, 255)
    if fld["type"] == "i8":
        return rng.randint(-128, 127)
    if fld["type"] == "u16":
        return rng.randint(0, 65535)
    if fld["type"] == "bytes":
        return bytes(rng.randint(0, 255) for _ in range(fld["len"]))
    raise ValueError(fld["type"])


def main():
    table = load_table()
    rng = random.Random(1234)
    tested = 0
    failed = 0

    for op in table["opcodes"]:
        if not is_fixed(op) or not op["fields"]:
            continue
        name = op["name"].lower()
        pack_fn = getattr(proto, "pack_%s" % name, None)
        unpack_fn = getattr(proto, "unpack_%s" % name, None)
        if pack_fn is None or unpack_fn is None:
            print("MISSING codec functions for %s" % op["name"])
            failed += 1
            continue

        values = {fld["name"]: random_value(fld, rng) for fld in op["fields"]}
        packed = pack_fn(*[values[f["name"]] for f in op["fields"]])
        unpacked = unpack_fn(packed)

        ok = True
        for fld in op["fields"]:
            got = unpacked[fld["name"]]
            want = values[fld["name"]]
            if got != want:
                ok = False
        tested += 1
        if not ok:
            failed += 1
            print("FAIL %-26s want=%r got=%r" % (op["name"], values, unpacked))

    # Opcode uniqueness check - the single most important table invariant.
    codes = [int(op["code"], 16) for op in table["opcodes"]]
    if len(codes) != len(set(codes)):
        print("FAIL duplicate opcode values found in protocol_table.json")
        failed += 1

    print("")
    print("%d fixed-layout opcodes round-tripped, %d failed" % (tested, failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
