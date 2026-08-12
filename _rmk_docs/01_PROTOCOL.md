# 01_PROTOCOL — FuZzAPP unified wire protocol v1

Single binary framing used on **both** Link A (SmartTV ⇄ Diffuser, UDP :8439) and Link B (App ⇄ SmartTV, UDP
:`APP_UDP_PORT` primary / MQTT `LEDs/cmd`-successor topics as automatic fallback). One opcode table is the single
source of truth; C++ (shared by both boards) and Java (app) codecs — and the Python `_rmk_TestMode` codec — are all
**generated from that one table**, not hand-mirrored. This is a **hard cut**: all three nodes ship together, no
dual old/new protocol support in the firmware or app (see 00_PLAN.md §8 for how old/new run side-by-side *as whole
builds*, e.g. two APKs, during bring-up).

This replaces: the ASCII/hex one-letter-command scheme, the `#SS<cmd>`/`#SSR` ACK envelope, the ad hoc binary `LK`
colour-sync exception, and the `'*'`-prefixed term-log envelope — with one consistent binary frame format and one
opcode space.

---

## 1. Frame format

```
 byte:   0        1        2        3        4         5..N-2         N-1
      +--------+--------+--------+--------+--------+~~~~~~~~~~~~~+--------+
      | MAGIC  | FLAGS  |  SEQ   | OPCODE |  ...PAYLOAD (0-247B)...| CRC8   |
      | 0xF2   | 8 bits | 0-255  | 0-255  |         raw bytes      |Dallas/ |
      +--------+--------+--------+--------+~~~~~~~~~~~~~~~~~~~~~~~~+ Maxim  |
                                                                    +--------+
```

- **MAGIC** (1B, `0xF2`) — cheap garbage-rejection; a datagram/message not starting with this is dropped before
  touching the dispatcher.
- **FLAGS** (1B bitfield):
  | Bit | Name | Meaning |
  |---|---|---|
  | 0 | `ACK_REQ` | Sender wants an explicit ACK frame back |
  | 1 | `IS_ACK` | This frame *is* an ACK (payload = 1-byte result code; `SEQ` = the sequence being acknowledged, not a new allocation) |
  | 2 | `IS_TELEMETRY` | Unsolicited push, never ACKed even if `ACK_REQ` happened to be set, never deduped (each push is new data) |
  | 3 | `FRAGMENTED` | Payload's first byte is a `FRAG_ID` (0-based); frame is one piece of a larger logical message |
  | 4 | `LAST_FRAG` | This is the final fragment (only meaningful with bit 3) |
  | 5-7 | reserved | must be 0 in v1 |
- **SEQ** (1B) — rolling 0-255 counter, allocated per sender per link (matches the current design's proven range;
  no observed real-world collision risk at today's UI-driven command rates — see 00_PLAN.md §9 for the one edge
  case worth a bounds check).
- **OPCODE** (1B) — index into the single opcode table (§3).
- **PAYLOAD** (0-247B) — opcode-specific binary layout, network byte order (big-endian) for any multi-byte field.
- **CRC8** (1B, Dallas/Maxim polynomial 0x31) — computed over every preceding byte. A CRC failure means the frame
  is silently dropped; no NACK is possible since `SEQ`/`OPCODE` can't be trusted either.

**No fixed `LEN` field** — UDP datagram size and MQTT message size already carry length for free, and this build
sends exactly one frame per transport message (no multi-frame packing), so re-deriving `LEN` would be redundant
overhead paid on every single packet. Total fixed overhead: **5 bytes** (down from the 8-byte design considered and
rejected during planning — see 00_PLAN.md §9).

**Version negotiation**: no per-frame version byte either, for the same overhead reason. Version is exchanged once,
inside the `HELLO` opcode's payload (§3), at the start of every session (welcome/reconnect on Link B, boot/reconnect
on Link A). If a peer receives a `HELLO` at a version it doesn't support, it replies `ACK` with result
`VERSION_MISMATCH` and its own highest supported version in the ACK payload's second byte, and does not proceed
past the handshake. Because this is a hard-cut rewrite, both ends start at `v1` on day one; the mechanism exists so
a staged rollout (e.g. app updated before a board is reflashed) fails safely and visibly instead of silently
misparsing frames.

**Max frame size**: 255 bytes total (fits inside the existing 256-byte buffers on both the Diffuser and the app,
comfortably under any real-world UDP MTU concern) → max payload 249B unfragmented, 248B first-fragment (1B eaten by
`FRAG_ID`).

---

## 2. Sequencing, ACK, retry, duplicates, fragmentation

- **Allocation**: sender-side rolling `SEQ` (0-255), incremented after each *new* command (not on a retry — retries
  reuse the same `SEQ`, matching today's proven behaviour).
- **ACK matching**: receiver replies with `OPCODE=ACK(0x00)`, `SEQ`=the acknowledged sequence, `IS_ACK` set,
  1-byte result payload. Sender resolves against a small pending-table keyed by `SEQ`; an ACK for an unknown/already-
  resolved `SEQ` is silently dropped (handles late/duplicate ACKs after a retry already gave up).
- **Retry tiers** (opcode-table-driven, §3 `ack` column, not hardcoded per command letter like today):
  | Tier | Timeout | Max retries | Used for |
  |---|---|---|---|
  | FAST | 300ms | 2 (3 total sends) | Same-hop commands the receiver can answer synchronously (LED colour, settings, ambient mode, enable toggle) |
  | RELAY | 3000ms | 1 (2 total sends) | Commands relayed through a second hop (App→SmartTV→Diffuser), or that trigger a sensor-path side effect (lux test) |
  | SLOW | 9000ms | 0 (single send) | MQTT credential provisioning — the far end does a real blocking broker connect before it can ACK |
- **Duplicate handling**: receiver keeps an 8-entry ring of `(SEQ)` accepted per remote peer, each entry expiring
  after `DEDUP_WINDOW_MS`=3000. A command frame whose `SEQ` is still in the ring is **not re-executed** — the cached
  ACK is simply resent. `IS_TELEMETRY` frames are exempt (never deduped; each one is new data by definition).
- **No fail-over-on-NACK coupling**: a failed/expired ACK is a per-command outcome only (surfaced to the console/UI
  and to nothing else). Transport-liveness (UDP-vs-MQTT selection) stays a fully separate, timestamp-driven state
  machine on the app side, exactly as it works today — deliberately kept as two decoupled subsystems (00_PLAN.md
  §9 explains why unifying them isn't recommended).
- **Fragmentation**: reserved for the rare payload that doesn't fit in 247/248 bytes. None of the payloads in the
  current opcode table need it (the largest, `SETTINGS_FULL` at 50 bytes and `DIFFUSER_HISTORY` at 21 bytes, both
  fit easily) — it exists for future-proofing, not because today's data requires it. When used: all fragments share
  the first fragment's `SEQ`; receiver buffers by `(peer, SEQ)` until `LAST_FRAG` arrives or a 2000ms reassembly
  timeout, whichever first (partial buffers are dropped silently on timeout).
- **Rate limits**: opcode-table `rate` column (§3) caps how often a *sender* may emit a given opcode, independent
  of ACK/retry traffic — mainly relevant to `TELEM_COLOR_SYNC` (capped at the LED-FPS period, same as today) and
  `KEEPALIVE` (fixed 10s cadence either direction, unaffected by burst command traffic).

---

## 3. Opcode table (single source of truth)

One flat 0x00-0xFF space. `Link` = which physical link(s) the opcode is valid on (`A`=SmartTV⇄Diffuser,
`B`=App⇄SmartTV, `both`=identical semantics/payload reused verbatim on both, notably the Diffuser-control family
which SmartTV mostly relays unmodified). `Dir` = `→` sender-initiated command, `⇐` telemetry/reply push.

### System (0x00-0x0F, both links)

| Code | Name | Link | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|---|
| 0x00 | `ACK` | both | ⇐ | result (1B, §4) [+ highest-supported-version (1B) iff result=VERSION_MISMATCH] | — | — |
| 0x01 | `HELLO` | both | → | proto-version (1B) | FAST | on connect/reconnect only |
| 0x02 | `KEEPALIVE` | both | → | — | none (`ACK_REQ` never set) | fixed 10s |
| 0x03 | `LOG` | both | ⇐ | level(1B) + source(1B) + text(N, UTF-8, no NUL) | — (telemetry) | on demand |
| 0x04 | `DIAG_HEALTH` | both | → | — | FAST | on demand |
| 0x05 | `DIAG_PARFUM_TRACE` | A only | → | — | FAST | on demand |

### LED / colour (0x10-0x1F, Link B)

| Code | Name | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|
| 0x10 | `LED_SET_COLOR` | → | r,g,b (3B) | FAST | interactive |
| 0x11 | `LED_GET_COLOR` | → | — | none (reply is `TELEM_COLOR_SYNC`) | on demand |
| 0x12 | `LED_SET_DUAL_COLOR` | → | shake-flag(1B) + r1,g1,b1,r2,g2,b2 (6B) | FAST | interactive |
| 0x13 | `LED_GET_DUAL_COLOR` | → | — | none (reply is `TELEM_DUAL_COLOR`) | on demand |
| 0x14 | `LED_SET_SELECTION` | → | bitmask, `ceil(LED_TOTAL/8)`=8B (61 LEDs) | FAST | interactive |
| 0x15 | `LED_SET_BRIGHTNESS` | → | value (1B, 0-120) | FAST | interactive |
| 0x16 | `LED_SET_ENABLE` | → | — (pure toggle) | FAST | interactive |
| 0x1A | `TELEM_COLOR_SYNC` | ⇐ | FILL(op=1,start,count,r,g,b: 6B) / SETN(op=2,count,N×{idx,r,g,b}) records, same run-length scheme as today's `LK` | — (telemetry) | ≤1×/LED-FPS-period, coalesced |
| 0x1B | `TELEM_DUAL_COLOR` | ⇐ | r1,g1,b1,r2,g2,b2 (6B) | — | on change or `LED_GET_DUAL_COLOR` |
| 0x1C | `TELEM_MAX_BRIGHTNESS` | ⇐ | value (1B) | — | welcome-only |
| 0x1D | `TELEM_ENABLE` | ⇐ | value (1B, 0/1) | — | dirty-gated |

### Settings (0x20-0x2F, Link B)

| Code | Name | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|
| 0x20 | `SETTINGS_READ_ALL` | → | — | none (reply is `TELEM_SETTINGS_FULL`) | on connect / on demand |
| 0x21 | `SETTINGS_READ_ONE` | → | id (1B) | none (reply is `TELEM_SETTINGS_ONE`) | on demand |
| 0x22 | `SETTINGS_WRITE` | → | N×{id(1B), val(1B)} | FAST | interactive |
| 0x23 | `TELEM_SETTINGS_FULL` | ⇐ | 50×val(1B), id = payload index (no id byte needed — ids are contiguous 0-49) | — | on `SETTINGS_READ_ALL` |
| 0x24 | `TELEM_SETTINGS_ONE` | ⇐ | id(1B) + val(1B) | — | on `SETTINGS_READ_ONE` |
| 0x25 | `TELEM_SAVE_RESULT` | ⇐ | result (1B, 0=ok) | — | on EEPROM write completion |

### Status / telemetry (0x30-0x3F, Link B unless noted)

| Code | Name | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|
| 0x30 | `TELEM_STATUS` | ⇐ | tv(1B) + motion(1B) + udpraw(1B) + ambient(1B) + diffuser-summary(1B) | — | dirty-gated |
| 0x31 | `TELEM_CLIMATE` | ⇐ | temp(1B signed) + humidity(1B) | — | dirty-gated |
| 0x32 | `TELEM_LUX` | ⇐ | level (1B, 1-5) | — | dirty-gated |
| 0x33 | `TELEM_LINK` | ⇐ | rssi-bucket(1B, 0-4) + wifi-state(1B) | — | dirty on bucket change |
| 0x34 | `TELEM_FAULTS` | ⇐ | bitmask (2B) | — | transition-only, never on welcome |
| 0x35 | `TELEM_TEST_MODE` | ⇐ | mode (1B) | — | on entry + self-cancel |
| 0x36 | `TELEM_DEVICE_ID` | ⇐ | device_id (12B ASCII hex, `NET::DeviceId()`) | — | on HELLO burst only |

`TELEM_DEVICE_ID` is how the app learns this board's MQTT device id (the
`fuzz/<device_id>/...` topic path) instead of sitting on a placeholder -
there's no other discovery channel, so this only ever gets through over a
transport that's already working (local UDP in practice; a cloud session
using the wrong topic can't receive it). See DataReceive._recvDeviceId()
and MqttTransport.setDeviceId() on the app side.

### Ambient / Test Mode (0x40-0x4F, Link B)

| Code | Name | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|
| 0x40 | `SET_AMBIENT_MODE` | → | on/off (1B) | FAST | interactive |
| 0x41 | `SET_TEST_MODE` | → | mode (1B, 0x00-0x05) | FAST | interactive |
| 0x42 | `SET_TEST_DIFFUSER` | → | value (1B: 00 off / 01-04 mode / FF re-roll) | RELAY | interactive |
| 0x43 | `SET_TEST_LUX` | → | level (1B, 1-4) | FAST | interactive |
| 0x50 | `SET_MQTT_CREDENTIALS` | → | userLen(1B)+user+passLen(1B)+pass (raw bytes — no base64; MQTT/UDP both carry binary natively in v1, unlike the old ASCII protocol) | SLOW | on demand |

### Diffuser control (0x60-0x6F, `both` — SmartTV relays these opcodes to/from the Diffuser largely unmodified on Link A; App issues them on Link B and SmartTV plays proxy)

| Code | Name | Dir | Payload | ACK tier | Rate |
|---|---|---|---|---|---|
| 0x60 | `DIFFUSER_STATUS_QUERY` | → | verbose-flag (1B: 0=silent/`Dc`-equivalent, 1=verbose/`Ds`-equivalent) | RELAY (B) / FAST (A) | on demand (B), 5s poll (A) |
| 0x61 | `DIFFUSER_HISTORY_QUERY` | → | — | RELAY (B) / FAST (A) | on demand |
| 0x62 | `DIFFUSER_HISTORY_REMOVE` | → | 1-based index (1B) | RELAY (B) / FAST (A) | on demand |
| 0x63 | `DIFFUSER_MANUAL_REFILL` | → | — | RELAY (B) / FAST (A) | on demand |
| 0x64 | `DIFFUSER_SHUTDOWN` | → | — | RELAY (B) / FAST (A) | on demand |
| 0x65 | `DIFFUSER_TURN_ON` | → | mode(1B) + dual-flag(1B) + r1,g1,b1[,r2,g2,b2] + brightness(1B) + effect(1B) + speedMs(1B) | RELAY (B) / FAST (A) | on demand |
| 0x66 | `DIFFUSER_PARFUM_START` | → | minutes (2B, 1-360) + mode (1B) | RELAY (B) / FAST (A) | on demand |
| 0x67 | `DIFFUSER_PARFUM_CANCEL` | → | — | RELAY (B) / FAST (A) | on demand |
| 0x69 | `TELEM_DIFFUSER_STATUS` | ⇐ | mode(1B) + strip(1B) + parfumMin(2B) + usageMin(2B) + avgMin(2B) + refillCount(1B) + lifetimeRefills(2B) — 11B, replaces the old `Ds` 24-char ASCII reply on both links | — | on query / dirty push |
| 0x6A | `TELEM_DIFFUSER_HISTORY` | ⇐ | count(1B) + up to 10×minutes(2B) | — | on `DIFFUSER_HISTORY_QUERY` |
| 0x6B | `TELEM_DIFFUSER_USAGE` | ⇐ | accumMin(2B)+avgMin(2B)+refillCount(1B)+lifetimeRefills(2B) | — | dirty-gated, diffuser-reachable only |
| 0x6C | `TELEM_PARFUM_REMAINING` | ⇐ | minutes (2B) | — | while active, or on drop-to-0 |

Result: **8 telemetry+control families instead of the old scheme's ~30 unrelated one-letter prefixes**, one codec
entry point per family instead of a length-sniffing `switch` on the first 1-2 characters.

---

## 4. ACK result codes

| Code | Name | Meaning | Notes vs today |
|---|---|---|---|
| 0 | `OK` | Applied as requested | |
| 1 | `CLAMPED` | Applied, but a value was range-clamped | Today's `APP_ACK_CLAMPED` is defined but never actually emitted (silent `constrain()`) — v1 fixes this: every clamp site sets the result explicitly. **Ruling needed, see 00_PLAN.md §9.** |
| 2 | `REJECTED` | Malformed or nothing to act on | |
| 3 | `BLOCKED` | Well-formed, refused by a preemption/ownership rule (e.g. brightness while Motion owns the LEDs) | |
| 4 | `LOCKED` | Queued behind an in-progress operation (Diffuser Parfum window), will apply later or on natural expiry | Today's `APP_ACK_LOCKED` is defined but never emitted — v1 fixes this too |
| 5 | `UNREACHABLE` | Relay target (Diffuser) did not respond in time | Replaces the old, confusingly-named `NOWATER`(5) — that code never meant "out of water," it meant "the Diffuser didn't answer"; real out-of-water state is telemetry (`TELEM_STATUS`'s diffuser-summary field), not an ack code |
| 6 | `UNSUPPORTED` | Unknown opcode, or a diagnostic sub-code not implemented | |
| 7 | `UNAUTHORIZED` | Credential rejected (`SET_MQTT_CREDENTIALS`) | |
| 8 | `VERSION_MISMATCH` | `HELLO` version not supported; ACK payload's 2nd byte carries the highest version the replier does support | New in v1 — today's `APP_PROTO_VER` has no live wire carrier at all |

A malformed frame (bad CRC, or a length too short for its declared opcode) is **silently dropped** — no NACK, since
`SEQ`/`OPCODE` can't be trusted enough to address a reply.

---

## 5. Behaviour across a UDP↔MQTT transport switch (Link B only)

- **Topics** (MQTT, replacing the single self-tagged duplex topic): two one-way topics per session,
  `fuzz/<deviceId>/b/c2d` (controller→device) and `fuzz/<deviceId>/b/d2c` (device→controller). This removes the
  self-echo problem structurally instead of by a sender-tag-and-filter convention — the convention worked, but a
  topic split is strictly simpler and removes a whole class of "did I just re-execute my own echo" bugs for free.
  QoS stays 0 (fire-and-forget matches the existing ACK/retry layer, which already handles loss).
- **Frame bytes are identical over UDP and MQTT** — no re-encoding, no base64 (unlike today's `$` command, which
  had to base64 its payload specifically because the *old* protocol was ASCII-oriented; v1's binary frames need no
  such wrapping on either transport).
- **Liveness proofs stay separate and asymmetric**, exactly as today (this is a "keep as-is" decision, not a gap):
  `UDP_LIVE` = a genuine frame received on the local socket within `CONN_LOST_TIMEOUT`=25000ms (or within a 4000ms
  post-`HELLO` grace window). `MQTT_LIVE` = a genuine non-echo frame received on `d2c` within the same window. UDP
  is preferred whenever `UDP_LIVE`; MQTT is used only when it isn't. The supervisor re-evaluates every 5000ms and
  triggers a full resync (`SETTINGS_READ_ALL` + `LED_GET_COLOR` + `LED_SET_SELECTION` re-push, since selection is
  app-local and can't be re-derived by the board) on every transition.
- **`SEQ` space is per-link, not per-transport** — a command retried after a mid-flight UDP→MQTT switch reuses the
  same `SEQ` on the new transport, so the receiver's dedup ring still recognizes it correctly if both copies
  eventually arrive.

---

## 6. Telemetry model & dirty-gating

Unchanged policy from today, carried forward deliberately (it works and there's no reason to redesign it): each
`TELEM_*` opcode has its own "changed since last push" flag on the sender, checked after every state-affecting
handler; a `HELLO` forces every group to resend once regardless of dirty state (full resync on reconnect). What
changes in v1 is only the *encoding* — one binary opcode per group instead of one ASCII-prefixed line per group,
and (for `TELEM_COLOR_SYNC`) the same proven run-length FILL/SETN scheme is kept nearly verbatim since it already
does its job well.

**Worst-case telemetry bytes/sec** (Link B, steady state, one board): dominated by `TELEM_COLOR_SYNC` during an
active full-strip animation at the default 120 FPS setting — worst case ~1 SETN record touching every one of the
61 addressable LEDs per frame (rare in practice; most effects touch a handful of pixels per tick) = `5 + 2 + 61×4`
≈ 251B every 8ms ≈ **~31 KB/s** peak, vs. the old ASCII `LK` binary framing's already-similar peak (this path was
already binary and already the bandwidth-dominant one — v1 doesn't change its shape, only wraps it in the new
5-byte header instead of the old bare `'L''K'` prefix). All other telemetry groups combined add well under 1 KB/s
even during a burst of state changes.

---

## 7. Bytes-on-the-wire: old vs new, 10 highest-frequency operations

| # | Operation | Old (ASCII/hex) | New (binary v1) | Δ |
|---|---|---|---|---|
| 1 | Keepalive | 1B (`k`, bare) | 5B (header only) | **+4B** — accepted regression, see below |
| 2 | Status push (`s`) | 11B | 5B hdr + 5B payload = 10B | −9% |
| 3 | Climate push (`H`) | 5B | 5+2 = 7B | +40% (tiny packet, fixed-overhead-dominated) |
| 4 | Lux push (`M`) | 5B | 5+1 = 6B | +20% (same reason) |
| 5 | RSSI/link push (`w`) | 5B | 5+2 = 7B | +40% (same reason) |
| 6 | Full settings dump (`S`+50×2hex) | 201B | 5+50 = 55B | **−73%** |
| 7 | Diffuser status reply (`Ds`) | 24B | 5+11 = 16B | −33% |
| 8 | Diffuser history (`Dh`) | 44B | 5+21 = 26B | −41% |
| 9 | LED selection mask (`LO`+61 chars) | 63B | 5+8 = 13B | **−79%** |
| 10 | Diffuser turn-on, dual (`Dn`) | 22B | 5+11 = 16B | −27% |

**Honest tradeoff**: the smallest, highest-frequency-but-lowest-bandwidth-impact packets (keepalive, single-field
telemetry) get a few bytes *larger* under the new fixed 5-byte header, because ASCII-hex's "2 bytes per byte of
real data" penalty barely applies when there's almost no real data to begin with. The packets that actually matter
for bandwidth and latency — anything carrying more than ~2-3 bytes of real payload — shrink substantially, and the
worst-case dominant cost (color-sync during animation, already binary today) is essentially unchanged. Net: total
steady-state bandwidth drops; the only "regression" is on packets so small the absolute byte delta is noise.

---

## 8. Keeping C++ and Java (and Python) codecs in lockstep

The opcode table in §3, plus every payload layout it references, is checked in as **one machine-readable source**:
`_rmk_ArduinoSide/_rmk_Shared/protocol_table.yaml` (or `.csv` — final format decided in the build-plan phase, not
here). A small generator script (`_rmk_ArduinoSide/_rmk_Shared/tools/gen_protocol.py`) reads that file and emits:

- `_rmk_ArduinoSide/_rmk_Shared/protocol_opcodes.h` — `enum class Opcode : uint8_t { ... }`, ACK-result enum,
  per-opcode struct-pack/unpack inline functions, retry-tier constants. Included by both `_rmk_SmartTV_R4` and
  `_rmk_Diffuser`.
- `_rmk_app/.../protocol/ProtocolOpcodes.java` — the same table as a Java class of `byte` constants + a matching
  encode/decode helper per opcode (mirrors the C++ struct-pack functions field-for-field).
- `_rmk_TestMode`'s own opcode module (language TBD in the build plan — see 00_PLAN.md §8) — generated the same
  way, so the test console can never silently drift from the real protocol the way `TestMode_APP`'s hand-maintained
  Python command tables theoretically could (in practice the audit found them still accurate today, but generation
  removes the risk going forward).

**The generator, not a human, is the enforcement mechanism** — a payload-layout change is made once in
`protocol_table.yaml` and regenerating is a required, checked step (a CI/pre-commit script diffs the generated
files against a fresh run and fails the build if they're stale) rather than a documentation convention three
people have to remember to keep in sync by hand, which is exactly the failure mode that produced several of the
comment/code mismatches this audit found in the current firmware (`02_REGRESSION.md`, items R3/R9).

`_rmk_TestMode` exercises the real codec directly (not a hand-rolled parallel implementation) — every command it
sends and every reply it decodes goes through the generated module, so using the test console to poke real hardware
is also a live, continuous correctness check on the codec itself.

---

## 9. What is explicitly *not* redesigned here

- The physical-layer facts (ports, IPs, hardware pin mapping) are unchanged — only the bytes on top of them.
- The Diffuser's own MODE-button physical state machine, buzzer envelope detection, and WS2812 effect set are
  firmware logic, not protocol — covered in 00_PLAN.md, not here.
- Settings *semantics* (which id means what, ranges, defaults) are a data-model concern — see 00_PLAN.md §5.4;
  this document only defines how a settings id/value pair travels on the wire.
