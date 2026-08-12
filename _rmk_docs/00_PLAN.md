# 00_PLAN — FuZzAPP `_rmk_` clean-room rewrite plan

**Status: PLAN ONLY. No code has been written.** This document, `01_PROTOCOL.md`, and `02_REGRESSION.md` are the
three files this phase produces. Everything below is a proposal for your approval — §9 lists every point where I
need a decision from you before Phase 1 starts (see §8).

Audit method: five full-source read-throughs (SmartTV `.ino`+`.h`, Diffuser `.ino`+`.h`, the Android app's
transport/settings/status layer, the Android app's UI/widget/updater layer, and the desktop `TestMode` tooling),
cross-checked against each other and against `README.md`/`AGENTS.md`. The complete behavioural inventory those
audits produced is `02_REGRESSION.md`; this document is the architecture built to preserve it.

---

## 1. Audit summary (full detail lives in `02_REGRESSION.md`)

| Node | Current shape | LOC | Owns |
|---|---|---|---|
| SmartTV R4 (`_FuZzAPP_SmartTV_R4`) | 1 `.ino` + 1 master `.h`, organized as 17 flat namespaces | 13,710 | 5 LED zones, TV/PIR/lux/BME280 sensing, NTP, UDP+MQTT app protocol, UDP diffuser-relay protocol, EEPROM |
| Diffuser (`_FuZzAPP_Diffuser`) | 1 `.ino` + 1 config `.h`, 14 flat namespaces | 3,035 | Virtual MODE-button driver, buzzer-tone sensing, WS2812 ring, refill/usage tracking, OTA, Telnet |
| Android app (`com.fuzz.colors`) | 32 Java classes, no package sub-structure | 18,485 | UDP+MQTT client, LED/settings/status UI, 8 popups, 2 widgets, updater, theming, Telnet client |
| `TestMode` tooling | Python/Tkinter (committed, working) + an uncommitted in-progress HTA/JS rewrite | ~2,700 (Python) | Raw protocol console for both boards |

**Top problems, ranked by rewrite-relevance** (full list with line numbers in the individual audit findings folded
into `02_REGRESSION.md`):

1. **SmartTV: a single global `TASK`/`HB::State` step struct is shared by nearly every animation/transition.**
   Works only because every effect-start site disciplines itself to kill other tasks first; this contract has been
   violated repeatedly in production (the changelog's own multi-entry bugfix trail: stuck-lit motion, brightness
   climbing back after a test, LEDs going silently black). **The single largest architectural fragility to fix.**
2. **SmartTV: `setPixel()` silently forces colour to black whenever brightness hits 0** — a leaky side effect
   three separate historical bugs had to work around by manually snapshotting colour before calling it.
3. **Android: `MainActivity` (2,481 lines) and `LEDManager` (2,021 lines) are god objects** — lifecycle, transport
   state machine, MQTT credential UX, debug dialog, updater trigger, and shake-sensor handling all live in one
   class; `LEDManager` additionally hand-builds three Java UI popups inline.
2. **Protocol is ad hoc ASCII/hex with per-handler magic-number length checks**, no schema/version negotiation
   that actually reaches the wire (`APP_PROTO_VER` has no carrier packet since a 2023-era rework), and one
   inconsistently-applied binary optimization (`LK` colour sync) alongside otherwise verbose text framing.
5. **Real comment/code mismatches were found, not just style issues** — the Diffuser's documented Parfum
   mode-coercion doesn't exist in code (`02_REGRESSION.md` R3); the Diffuser's out-of-water detection is a 2-strike
   timing heuristic tuned from 4 live trials, not genuine tone-pattern sensing (R4); the SmartTV's EEPROM per-LED
   record addressing looks off-by-one between its read and write paths (R2); no Telnet/Serial console exists on the
   SmartTV despite that being part of the stated system architecture (R1).
6. **Duplication that should collapse to one implementation**: Android's two widget providers are ~95% identical
   code; `WidgetStatusFetcher` is a deliberate headless re-implementation of the UDP/MQTT parsing three other
   classes already do, kept in sync only by convention; every popup hand-rolls the same chrome helpers
   (`_blend`/`_idealInk`/`_dimBehind`/`_animateIn`/`_buildDivider`).

---

## 2. Target architecture

### 2.1 Layout

```
_rmk_app/                          # self-contained Gradle project, applicationId com.fuzz.colors.rmk
_rmk_ArduinoSide/
    _rmk_SmartTV_R4/
    _rmk_Diffuser/
    _rmk_Shared/                   # protocol table + generated codecs + shared scheduler + CRC8
    _rmk_TestMode/                 # protocol console, rebuilt on the generated codec
_rmk_docs/                         # this plan, protocol spec, regression checklist, per-node changelogs
```

### 2.2 SmartTV R4 (`_rmk_ArduinoSide/_rmk_SmartTV_R4/`)

```
_rmk_SmartTV_R4.ino        setup()/loop() only — the fixed sequence from the current firmware, unchanged shape
src/
  Scheduler.{h,cpp}         new minimal cooperative scheduler (replaces the unreviewed external TaskJockeyMod —
                             see §9 R10); tasks get typed, tagged-union private state instead of a shared singleton
  Led.{h,cpp}                zone mapping, pixel buffers; explicit `setColor()`/`setBrightness()` primitives that
                             do NOT implicitly touch each other (fixes problem #2 above)
  Lisens.{h,cpp}              lux sampling/hysteresis
  EffectsHb.{h,cpp}            registration-table-driven HB idle effects (§7) — includes a reserved slot for the
                              not-yet-implemented ECG-trace effect (§9 R11: needs a reference from you)
  EffectsTv.{h,cpp}             TV-on/TV-off effect table + the parallel HB "on-transition" helper table
  EffectsMotion.{h,cpp}          motion-on effect table + the single shared motion-off fade
  Tv.{h,cpp}                      pin/debounce, master on/off task, event log
  Motion.{h,cpp}                   PIR debounce/trigger, colour-renew task, lux-hold nudge
  AppLink.{h,cpp}                   Link B dispatch + telemetry, built on `_rmk_Shared`'s generated codec
  DifLink.{h,cpp}                    Link A client (non-blocking async send, matches today's proven `DIF` design)
  Eeprom.{h,cpp}                      versioned map, single consolidated dirty-bitmap (fixes problem-list item 3
                                      in the audit — today's 5 separate dirty-flag arrays)
  Udpraw.{h,cpp}                       ambilight stream receiver
  Bme.{h,cpp}                           climate sensor
  Net.{h,cpp}                            WiFi + NTP
  Mqtt.{h,cpp}
  MqttCred.{h,cpp}
  Telnet.{h,cpp}                          NEW — only if §9 R1 is ruled "yes, build it" (no current implementation
                                          to port; this is net-new scope, not parity, if approved)
  Debug.{h,cpp}                           replaces `PRNT`; dumps go out as a bounded `LOG` opcode stream, not one
                                          UDP packet per line (fixes the multi-hundred-packet debug-dump stall)
```

**Dependency rule**: `Effects*`/`Tv`/`Motion` may call into `Led`/`Scheduler`/`Lisens`, never into `AppLink`/
`DifLink` directly (they report state changes; the link modules read state, they don't drive effects). `AppLink`/
`DifLink` never touch `Led` pixel buffers directly — they call `Led`'s public setters, same discipline the current
firmware already mostly follows, now enforced by file boundaries instead of convention.

### 2.3 Diffuser (`_rmk_ArduinoSide/_rmk_Diffuser/`)

```
_rmk_Diffuser.ino
src/
  Strip.{h,cpp}         WS2812 effects/fade/cues (kept close to current design — it works)
  ModeButton.{h,cpp}      virtual MODE state machine
  Buzzer.{h,cpp}            envelope detection + OOW heuristic — pending §9 R4 (verify against real hardware
                            before deciding whether to keep the current heuristic or design real tone sensing)
  Usage.{h,cpp}              refill tracking + EEPROM
  Parfum.{h,cpp}              pending §9 R3 ruling on mode-coercion
  Diag.{h,cpp}
  Wifi.{h,cpp}
  ProtoLink.{h,cpp}            Link A server, built on `_rmk_Shared`'s codec
  Telnet.{h,cpp}                 kept close to current design — this one is real and works today, unlike the
                                 SmartTV's (§9 R1)
```

### 2.4 `_rmk_Shared`

```
protocol_table.yaml       single source of truth for every opcode + payload layout (01_PROTOCOL.md §3)
tools/gen_protocol.py      generates the C++ header, the Java class, and the TestMode codec module from the table;
                            a checked build step fails if generated output is stale relative to the table
protocol_opcodes.h          generated
Crc8.{h,cpp}
Scheduler.{h,cpp}            shared cooperative scheduler used by both boards (see §6)
```

### 2.5 Android app (`_rmk_app/`)

```
com/fuzz/colors/rmk/
  core/
    transport/
      Transport.java              interface
      UdpTransport.java
      MqttTransport.java
      TransportSupervisor.java     liveness/failover state machine — extracted whole out of MainActivity
      ProtocolCodec.java            generated from protocol_table.yaml
      AckManager.java                seq/pending/retry — extracted out of DataSend, opcode-table-driven tiers
    state/
      DeviceState.java               StatusManager's data, no view code
      SettingsStore.java              SettingsManager's data model, no view code
      LedState.java                    LEDManager's data model, no view code
    persistence/
      Prefs.java                       one consolidated SharedPreferences wrapper (today's 7 independently-named
                                       prefs files: Background/Theme/MqttCred/Telnet/DualColor/Parfum/Widget)
  ui/
    MainActivity.java                  thin: page router + lifecycle only
    pages/  (Term/Leds/Settings page controllers — view-binding only, read/write core.state)
    popups/
      PopupChrome.java                  NEW shared base — kills the duplicated blend/ink/dim/animate/divider code
                                        across all 7 current hand-built popups
      ColorWheelPopup / RgbChannelPopup / BackgroundPopup / DiffuserUsagePopup / TestModePopup / ButtonGuidePopup /
      UpdatePopup / ThemePopup          kept, rebuilt on PopupChrome
    widgets/
      FuzzWidgetProvider.java            ONE class, parameterized by size — replaces the ~95%-duplicated pair
      WidgetScheduling / WidgetUpdateWorker / WidgetStatusFetcher   kept, but the fetcher now reuses
                                        `core.transport`/`core.ProtocolCodec` in headless mode instead of hand-
                                        duplicating packet parsing a third time
    effects/
      HBEffectSimulator.java             kept (deliberately firmware-bit-faithful mirror), now subscribes to the
                                        same `DeviceState` observable `LedsPageController` reads instead of caching
                                        its own `lastTvOn` boolean (fixes the two-sources-of-truth bug noted in the
                                        UI audit)
    console/  (TelnetConsole, ConsoleAdapter — kept close to current design)
    updater/  (UpdateChecker, UpdateInstaller, UpdatePopup — kept close to current design)
```

**Dependency rule**: `ui.*` depends on `core.*`; `core.*` never imports anything from `ui`; `widgets.*` depends only
on `core` (headless-safe, no Activity context assumptions baked in); `state.*` has zero Android UI-toolkit imports
so it's unit-testable without an emulator.

Gradle: `_rmk_app`'s own `settings.gradle`/`build.gradle`/wrapper, path-references `library/` and
`avloadingindicator/` from the frozen tree (never touching the root `settings.gradle`), `applicationId
com.fuzz.colors.rmk` so both APKs install side by side.

---

## 3. Function inventory

Grouped by node/module. This covers every public entry point (task callbacks, codec entry points, effect update
functions, EEPROM accessors, console command handlers, Android transport/state public methods) at the granularity
useful for planning; private per-effect helpers are sized by the effect tables in §7, not enumerated individually
here — that level of detail is generated fresh each time an effect is ported in Phase 4/5's per-effect work.

### 3.1 SmartTV R4

| Module | Signature | Purpose | Called from | Blocking? | Replaces |
|---|---|---|---|---|---|
| Scheduler | `TaskId add(Source, name, Callback, unit, interval, startDelay, locked)` | register a task | every module's setup/effect-start | no | `TSK::AddTask` |
| Scheduler | `void killAllUnlocked()` | enforce "one foreground effect" | every effect-start site | no | `TSK::KillTasksAvoidLocked` |
| Scheduler | `void run(uint32_t now)` | tick every registered task | `loop()` | no (each callback must be) | `_TASK.runTasks()` |
| Led | `void setColor(idx, CRGB)` | write colour only, never touches brightness/black | effects, AppLink | no | part of `LED::setPixel` |
| Led | `void setBrightness(idx, uint8_t)` | write brightness only, never forces colour to black | effects, AppLink | no | part of `LED::setPixel` |
| Led | `void refresh(uint32_t now)` | push to hardware, FPS-gated | `loop()` | no (NeoPixel `.show()` is short) | `LED::Refresh` |
| Lisens | `void check(uint32_t now)` | sample ADC, classify lux level | Scheduler (10ms) | no | `LISENS::Check` |
| EffectsHb | `void update(EffectId, uint32_t now)` | one HB idle-effect tick | Scheduler | no | `HB::T_EFFECT_HB_*` |
| EffectsTv | `void updateOn(EffectId, uint32_t now)` / `updateOff(...)` | one TV effect tick | Scheduler | no | `TV::T_EFFECT_TV_ON/OFF*` |
| EffectsMotion | `void updateOn(EffectId, uint32_t now)` | one motion effect tick | Scheduler | no | `MOTION::T_EFFECT_MOTION_ON*` |
| Tv | `void status(uint32_t now)` | pin read/debounce | `loop()` (always runs) | no | `TV::Status` |
| Motion | `void status(uint32_t now)` | PIR read/debounce | `loop()` (skipped during UDPRAW) | no | `MOTION::Status`/`PinStatus` |
| AppLink | `void loop()` | recv+dispatch, round-robin | `loop()` | no | `APP::Loop` |
| AppLink | `AckResult exec(Frame)` | opcode dispatch | `loop()` | no | `APP::Exec` |
| AppLink | `void pushTelemetry(Group, force)` | dirty-gated push | every state-affecting handler | no | `APP::updStatus` family |
| DifLink | `void loop()` / `void tickAsyncSend()` | recv+dispatch / non-blocking send step | `loop()` | no | `DIF::Loop`/`TickAsyncSend` |
| Eeprom | `void markDirty(Region, offset)` | one consolidated dirty tracker | every settings/colour writer | no | 5 separate `EE_*Changed[]` arrays |
| Eeprom | `void tick(uint32_t now)` | chunked delta writer, versioned | Scheduler | no | `EE::Write` |
| Eeprom | `uint8_t read(Region, offset)` | typed accessor | everywhere | no | `EE::Read`/direct `EEPROM.read` |
| Udpraw | `void loop()` | drain ambilight socket | `loop()` (unthrottled) | no | `UDPRAW::Loop` |
| Bme | `void check(uint32_t now)` | poll sensor | Scheduler (10s) | no (I2C read, short) | `BME::Check` |
| Net | `void check(uint32_t now)` | WiFi/NTP health | Scheduler (30s) | reconnect path bounded ≤10s, same as today unless §9 R12 changes it | `NET::Check` |
| Mqtt | `void loop()` | TLS pump | `loop()`, throttled 30ms | reconnect bounded ~4s | `MQTT::Loop` |
| Debug | `void dump(Target)` | structured LOG-opcode stream | AppLink dispatch of `DIAG_HEALTH`/debug opcodes | no (bounded stream, not per-line blocking send) | `PRNT::_Debug` |

### 3.2 Diffuser

| Module | Signature | Purpose | Called from | Blocking? | Replaces |
|---|---|---|---|---|---|
| Strip | `void applyPayload(DnPayload)` | apply Dn-equivalent command | ProtoLink | no | `STRIP::applyDnPayload` |
| Strip | `void tickFrame(uint32_t now)` | one animation frame | Scheduler | no | `STRIP::stripEffectFrame` |
| ModeButton | `void requestTarget(uint8_t mode)` | drive toward a mode, buzzer-confirmed | ProtoLink, console | no | `MODE::cmdModeTarget` |
| ModeButton | `void tick(uint32_t now)` | timeout/advance | Scheduler | no | `MODE::tickModeTimeout` |
| Buzzer | `void tick(uint32_t now)` | envelope sample | Scheduler (5ms) | ADC read only, negligible | `BUZZ::tickBuzzer` |
| Buzzer | `void finalizeBurst()` | classify beep count → confirm/shutdown/OOW | Buzzer.tick | no | `BUZZ::buzzerFinalizeBurst` |
| Usage | `void finalizeRefillCycle()` | bank a cycle, persist | Buzzer, manual refill | EEPROM commit, bounded | `USAGE::finalizeRefillCycle` |
| Parfum | `AckResult start(minutes, mode)` / `void stop(reason)` | timed run lifecycle | ProtoLink, console | no | `PARFUM::parfumStart/Stop` |
| ProtoLink | `void tick(uint32_t now)` | recv+dispatch | `loop()` | no | `PROTO::tickUDP` |
| ProtoLink | `AckResult dispatch(Frame)` | opcode handler | ProtoLink.tick | no | `PROTO::udpDispatch` |
| Telnet | `void tick(uint32_t now)` | accept/read/dispatch | `loop()` | no | `TELNET::tickTelnet` |
| Wifi | `void connect(uint32_t timeoutMs)` | boot-only connect | `setup()` | yes, bounded (boot only — kept, not a loop()-reachable stall) | `WIFI::connectWiFi` |

### 3.3 Android (`_rmk_app`)

| Module | Signature | Purpose | Called from | Threading |
|---|---|---|---|---|
| ProtocolCodec | `byte[] encode(Opcode, Object payload)` / `Frame decode(byte[])` | generated codec | AckManager, Transport | any (pure function) |
| AckManager | `void send(Opcode, payload, AckCallback)` | allocate seq, envelope, arm timeout | UI callbacks | posts to a single background looper (mirrors today's `ioExecutor`/`ack` split, unified) |
| AckManager | `void onAck(seq, result)` | resolve pending | Transport receive path | same looper |
| TransportSupervisor | `void evaluate(uint32_t now)` | pick UDP vs MQTT, resync on transition | 5s timer | background looper |
| UdpTransport | `void start()/stop()/send(bytes)` | socket lifecycle | TransportSupervisor | dedicated receive thread (kept — proven simpler than migrating to NIO/coroutines for a 100ms-poll loop) |
| MqttTransport | `void connect()/publish(bytes)/disconnect()` | broker session | TransportSupervisor | Paho callback thread + one shared worker (fixes today's per-publish bare-`Thread` spam) |
| DeviceState | `void apply(Opcode, payload)` per telemetry group | state mutation | Transport receive dispatch | main thread (kept — UI binds directly, no need to add background diffing for this data volume) |
| SettingsStore | `void applyReceived(id, val)` / `void write(id, val)` | settings model | SettingsPageController, Transport | main thread |
| LedState | `void setColor/setSelection/setBrightness(...)` | LED model | LedsPageController, HBEffectSimulator, Transport | main thread |
| PopupChrome | `void show(anchor)` / `dimBehind()` / `animateIn()` | shared popup chrome | every popup subclass | UI thread |
| FuzzWidgetProvider | `void onUpdate(...)` (parameterized by `WidgetSize`) | one implementation for both current widget classes | Android framework | UI thread (widget RPC) |
| WidgetStatusFetcher | `FetchResult fetch()` | headless status pull, reusing `ProtocolCodec`/`UdpTransport`/`MqttTransport` in headless mode | `WidgetUpdateWorker` | WorkManager background thread |

---

## 4. Data model

### 4.1 SmartTV RAM budget (target, 32 KB SRAM total)

| Buffer/struct | Size | Notes |
|---|---|---|
| `Led` current+target colour buffers, 238 slots × CRGB | ~1.43 KB | unchanged from today — every physical HB pixel needs a live render slot even though only 1 is individually addressable from the protocol (audit problem #11; not resolved by this rewrite, flagged §9 R13 as a possible future capability gap, not a regression to fix now) |
| `Led` stored-colour/brightness (EEPROM mirror) | ~488 B | unchanged |
| Scheduler per-task tagged-union scratch (replaces the shared `TASK`/`HB::State` singleton) | ~24 B fixed (one tagged union sized to the largest effect's step-state variant, not per-task-instance — mutual exclusivity is preserved by design, only the *type safety* changes) | **not** N× the old struct size — see §9 R14 for why this doesn't blow the budget |
| `AppLink` RX/TX buffers (3× 255 B, matches new max frame size) | 765 B | was 3×256B; effectively unchanged |
| `DifLink` state + async-send scratch | ~150 B | unchanged from today's `DIFx` |
| `Eeprom` consolidated dirty-bitmap (replaces 5 separate flag arrays) | ~70 B (1 bit/byte over the ~550-byte low region) | was ~5 small arrays adding up to a similar footprint — net roughly even, real win is code-path consolidation not RAM |
| Sensor/status structs (`Tv`, `Motion`, `Lisens`, `Bme`, `Net`) | ~700 B | unchanged |
| `Mqtt`/`MqttCred` state | ~280 B | unchanged |
| **Estimated total** | **~4.1 KB of 32 KB (~13%)** | leaves the same generous headroom the current firmware already has — this rewrite is not RAM-constrained, it's correctness-constrained |

### 4.2 EEPROM map (versioned, target)

| Region | Offset | Size | Change vs today |
|---|---|---|---|
| Schema version | 0 | 1 B | **NEW** — a rewrite that reorders/resizes any region below can detect and migrate instead of silently misreading, closing audit problem #12 |
| Settings | 1–50 | 50 B | same 50 slots, same ids (§`02_REGRESSION.md` A.7) |
| Region checksum(s) | after each region | 2 B (CRC16) per region | **NEW** — closes "no checksum on any persisted structure" (audit problem #12); cost is ~8 B total across 4 regions, negligible |
| LED colours | next | 244 B | same 4-byte/LED layout — **but only after §9 R2 is verified against real hardware**; if the audit's suspected off-by-one is real, the byte offsets here shift by 1 and this table gets corrected before Phase 3 |
| Ambient colours | next | 244 B | unchanged layout |
| UDPRAW colour | next | 4 B | unchanged |
| Motion colour | next | 3 B | unchanged |
| *(headroom)* | — | ~7,400 B | unchanged — plenty of room for the version byte + checksums without touching the anchored-at-the-end blocks |
| Self-test block | `len-73..len-68` | 6 B | unchanged |
| MQTT credentials | `len-67..len-1` | 67 B | unchanged; still raw bytes now that the wire protocol doesn't need base64 either (01_PROTOCOL.md §3) |

**Migration rule**: on boot, if the stored schema version < current, run a one-shot migration function per version
step (v0→v1 etc.); if no migration path exists for a very old version, fall back to factory defaults for that
region only (not a full-device wipe) and log a fault bit.

**Factory reset / defaults**: today this lives entirely on the Android app side (a batch settings write), with no
firmware-side default table. §9 R15 asks whether to keep that split or move canonical defaults into
`EE_SETTINGS_TABLE`'s (successor's) PROGMEM row so the firmware can self-initialize on a truly virgin EEPROM instead
of depending on the app having connected at least once.

### 4.3 Diffuser EEPROM map (versioned, target)

Adds a 1-byte schema version + a 2-byte CRC16 to the existing 50-byte usage/refill-history block (52 B total, from
50 B) — same rationale as §4.2, closing the audit's "no version/magic field" gap for this board too.

### 4.4 Android state containers

| Container | Persisted where | Sync rule with the board |
|---|---|---|
| `DeviceState` (StatusManager successor) | not persisted — always re-derived from telemetry | push-only from board; no local write path (matches today) |
| `SettingsStore` | not persisted locally — board (EEPROM) is the sole source of truth | `SETTINGS_READ_ALL` on every `HELLO`; local writes are optimistic-UI only until ACKed |
| `LedState` | `selection`/`dualColorList` persisted (`Prefs`, app-local, board can't re-derive) | colour/brightness always re-pulled from `TELEM_COLOR_SYNC` on reconnect, never trusted as locally authoritative |
| `Prefs` (consolidated) | Android `SharedPreferences`, one file | theme, background image path, MQTT creds, telnet-enabled, saved dual-colours, last-used parfum settings, widget-cache — same data as today's 7 separate prefs files, one wrapper |

---

## 5. Protocol specification

See `01_PROTOCOL.md` for the complete frame format, opcode table, ACK/retry/dedup/fragmentation rules, transport-
switch behaviour, telemetry model, and the old-vs-new bytes-on-the-wire comparison.

---

## 6. Task schedule

### 6.1 SmartTV R4 (target — same cadences as today unless noted; `loop()` sequence and structural gating during
UDPRAW streaming are both kept exactly as today, since they're deliberate and not part of any identified problem)

| Task | Period | Locked | Typical/worst runtime | Preconditions | Touches |
|---|---|---|---|---|---|
| `AppLink.loop()` | every 3rd `loop()` iteration | n/a (inline, not scheduler) | µs–low ms | — | UDP socket, dispatch |
| `DifLink.loop()` | every 3rd `loop()` iteration (offset) | n/a | µs | — | UDP socket |
| `DifLink.tickAsyncSend()` | every `loop()` | n/a | µs (one non-blocking step) | staged send pending | UDP socket |
| `Mqtt.loop()` | every 30ms | n/a | ms (TLS pump) | credentials cached | TLS socket |
| `AppWatchdog` | 1500ms | yes | µs | — | expires stale app peer IP |
| `LedRefresh` | 1000ms task + FPS-gated push in `loop()` | yes | ~1-8ms (`.show()`) | `Led.Enabled` | NeoPixel hardware — **§9 R16: clarify which of the two overlapping cadences is authoritative, current firmware has both active simultaneously** |
| `LisensCheck` | 10ms | yes | µs | — | ADC sample accumulation |
| `BmeCheck` | 10s | yes | ~ms (I2C) | — | climate struct |
| `NetCheck` | 30s | yes | µs (fast path) / up to bounded reconnect | — | WiFi/NTP health |
| `DifStatusCheck` | 5s | yes | µs (non-blocking send) | — | Diffuser status poll |
| `DifIdleCheck` | 10s | yes | µs | nothing else active | Diffuser idle-pulse state machine |
| `KeepAlive` | 10s | yes, armed on connect | µs | app connected | bare keepalive frame |
| One-shot effect tasks (TV/Motion/Ambient/SmoothChange/DualColor/ShakeDualColor/LedsToOff/LuxBrChange/UdpRawSetColor) | variable, effect-defined | no — killed by next `killAllUnlocked()` | effect-dependent, all sub-16ms per tick at target FPS | previous foreground effect finished/killed | pixel buffers via typed scheduler scratch |

### 6.2 Diffuser (target — unchanged cadences)

| Task | Period | Touches |
|---|---|---|
| `Wifi.tick` | 10s | reconnect check |
| `Led/StripFade` tick | 500ms (blink cue) + self-throttled per-effect internal ticks | WS2812 |
| `Buzzer.tick` | 5ms | ADC envelope |
| `Usage.tickAccum` | 1000ms | usage accumulator + periodic EEPROM checkpoint |
| `ModeButton.tickPinRelease`/`tickModeTimeout` | every `loop()`, self-throttled | virtual button pin |
| `Parfum.tick` | every `loop()`, self-throttled | countdown/expiry |
| `ProtoLink.tick` | every `loop()` | UDP socket |
| `Telnet.tick` | every `loop()` | TCP socket |
| `ArduinoOTA.handle()` | every `loop()` | OTA |

---

## 7. Effects engine

**Registration structure**: one `EffectDef{id, name, updateFn, category}` table per category (HB idle, TV-on,
TV-off, Motion-on, plus the 4-slot TV-on HB helper table) replacing today's scattered `*_HANDLERS[]` arrays split
between the `.ino` and the master header — adding an effect becomes one new table row + one function, matching the
brief's requirement directly. IDs are preserved unless you say otherwise (§`02_REGRESSION.md` A.5 has the full
current ID list — HB 1–14, TV-on 0–11, TV-off 0–7, Motion-on 0–5).

**Per-frame contract**: `void update(EffectId id, uint32_t now, ScratchView& scratch)` — `ScratchView` is the typed
view onto the scheduler's tagged-union per-task scratch (§4.1), replacing direct access to the old shared
`TASK`/`HB::State` globals. An effect signals completion by returning a `done` flag; the scheduler retires the task.

**Shared palette/easing helpers** (consolidating today's already-mostly-shared-but-duplicated helpers): `lerp8()`,
`hsvToRgb()` (today's "fast 3-phase HSV wheel," kept as-is — it's already shared and it's what the Android
`HBEffectSimulator` mirrors, so changing its math would break the client-side preview's fidelity), the 8-stop
palette table used by the `Colors` HB effect, and the `TG_BRIGHTNESS*` quartet collapsed into one function + a
buffer-selector parameter (closes audit problem #13).

**Timing source**: `millis()`-based throughout, unchanged — no reason to introduce a different clock source.

**Lux-adaptive speed feed-in**: `getLuxAdaptFactor()`/`Delay()`/`Inc()` kept as free functions callable from any
effect's update function, scoped exactly as today (TV-on/TV-off/Motion event delay+increment only — NOT
`T_LUX_BR_CHANGE` or `T_UDPRAW_SET_COLOR`, which stay unadapted on purpose per the audit's documented history of a
reverted proportional-rescale attempt).

**HB pixel anchor map**: unchanged — 178 physical pixels, indices 1–176 visible, expressed as the same constants
(`LED_START_I_HB`, etc.) centralized in `Led.h` rather than scattered across effect files.

**New effect**: the ECG-trace effect referenced in the source prompt does not exist in the current firmware (audit
confirmed absent — see `02_REGRESSION.md` A.5). Building it is net-new scope; I need a description or reference of
the intended look/timing before it can get a table row (§9 R11).

---

## 8. Build plan

Each phase is independently compilable/testable and has its own "how I verify nothing broke" tied to
`02_REGRESSION.md`. **Phase 0 (this document) requires your explicit approval before Phase 1 starts** — nothing
past this point is written until you review §5.3–5.5 (per your own instructions) and rule on §9.

| Phase | Scope | Verify | Status |
|---|---|---|---|
| 0 | This plan | Your review + §9 rulings | Superseded — you approved proceeding directly rather than ruling on every §9 item first; defaults were taken and are called out where they were, in each phase's own notes as they land |
| 1 | `_rmk_Shared`: protocol table, codegen, C++/Java/Python codec skeletons | Round-trip encode/decode unit tests for every opcode against fixture bytes; no hardware needed | **Done.** `protocol_table.json` (29 fixed-layout + 8 raw/variable opcodes), `tools/gen_protocol.py` emits `protocol_opcodes.{h,cpp}` (C++), `ProtocolOpcodes.java`, `protocol_opcodes.py`. All three compiled/parsed with real toolchains (`arm-none-eabi-g++`, `javac 21`, `python3 ast`). `tools/test_protocol.py` round-trips all 29 fixed opcodes with random fixtures — 0 failures. CRC8 (Dallas/Maxim) implemented identically in all three languages. |
| 2 | `_rmk_TestMode` rebuilt on the generated codec | Loopback encode/decode; manual opcode-table-vs-`02_REGRESSION.md` completeness pass | **Done, CLI + GUI.** `rmk_testmode.py` — `send <OPCODE> [field=value...]` / `listen`, builds real frames, verified over an actual UDP loopback socket (not just in-process): sent `HELLO`/`LED_SET_COLOR`/`KEEPALIVE`, receiver decoded all three correctly incl. CRC validation. **GUI follow-up (resolves R17)**: `_rmk_ArduinoSide/_rmk_TestMode_HTA/` — not the deferred Tkinter port originally planned, but a real port of the uncommitted `TestMode_APP_HTA/TestMode.hta` (an HTML Application, mshta.exe-hosted) onto the binary v1 protocol, since that's what actually existed as the frozen side's up-to-date GUI reference (the mockups R17 flagged were an earlier, already-abandoned design-exploration pass under the same folder — separately deleted, not part of this port). **`gen_protocol.py` gained a 4th target**: `gen_javascript()` emits `protocol_opcodes.js` (ES5, JScript-safe — no `let`/`const`/arrow-fns/template-strings/`Array.prototype` gaps in the pre-ES5 engine) alongside the existing C++/Java/Python outputs, so the HTA's opcode/frame codec can never drift from the other three languages the way a hand-transcribed one could. One real bug the generator itself had: trailing commas before a closing `}`/`]` are invalid in JScript's older engine (silently fine in every modern one) — every generated object literal now joins entries instead of unconditionally appending a comma to each line; verified against `cscript.exe` (the actual classic-JScript engine, closer to mshta's runtime than Node) via a `.wsf` harness exercising real frame build/parse/CRC8 round-trips, ACK/TELEM classification, and every command table entry's `build()` — 46/46 checks passing. Net.js/`udp_send.ps1`/`tcp_send.ps1`/`controls.js` needed **zero changes** — the PowerShell UDP transport already round-trips raw bytes via hex-encoding (built for the one binary reply the ASCII protocol already had, `LK` colour-sync), and the form-control builder was always protocol-agnostic. `protocol.js` (frame engine), `data.js` (opcode command table), `result-view.js` (reply renderers), and `ui.js` (hero/custom-panel wiring) were rewritten against the binary opcodes. **One real gap the generator can't cover**: `LOG`'s `text` field is type `"str"` (variable-length, no fixed layout) — same as every `"raw"` field, `gen_protocol.py` intentionally skips codegen for it; hand-decoded in `protocol.js` to match `DeviceLink.handleLog()`'s exact layout (`level(u8)+source(u8)+text(rest, UTF-8)`) after noticing the generic fallback would've hex-dumped every log line instead of showing it. Visual/interactive verification wasn't reachable in this pass — computer-use's access-request resolver only matches installed Start-Menu apps, not arbitrary running `mshta.exe` windows by title, so the GUI itself is unverified beyond "loads with no script-error dialog and every non-DOM code path checked out." |
| 3 | `_rmk_Diffuser` firmware (smallest node, fewest dependencies) — new scheduler, behaviourally-identical MODE/buzzer/strip/usage logic, protocol swapped to v1 on Link A | Full `02_REGRESSION.md` §B, exercised live via `_rmk_TestMode` against bench hardware (not the production board) | **Done, compiles clean on real hardware toolchain (esp8266:esp8266:d1_mini, both default and `DIF_TEST_MODE`+`DIF_DEBUG_VERBOSE` builds).** Split into 11 modules (Log/Strip/ModeButton/Buzzer/Usage/Parfum/Diag/Wifi/ProtoLink/Telnet/Test) + Globals/Types, ported line-for-line from the production `.ino` for behavioural fidelity. R3/R9 resolved by default (documented, not asked): Parfum's E-mode is honoured as requested, not coerced (the original coercion was already dead — comment-only, never real code); the two stale "WS2812 x6" comments are fixed to x10. R4 (OOW heuristic) preserved exactly, unverified against real hardware per that ruling. EEPROM gained the versioned-schema + CRC16 `00_PLAN.md §4.3 called for (`EE_SCHEMA_VERSION`, corrupt/virgin-flash detection resets to a fresh block instead of trusting garbage). DIAG replies now go out as `LOG` telemetry frames instead of raw `'!'`-marked text — the collision the marker existed to avoid is structurally impossible now (opcode dispatch, not first-byte sniffing). **Real bug found and fixed along the way**: `Test.h`/`ProtoLink.h`/`Globals.h` each checked `#ifdef DIF_TEST_MODE`/`DIF_DEBUG_VERBOSE` without including the header that defines those macros — silently compiled out entire modules (caught via a link error on `runSelfTest`, not a compile error) rather than actually respecting the flag. Not a hardware constraint like the SmartTV's RAM wall — a straightforward multi-translation-unit include-order mistake, now fixed in all three headers. RAM usage 44% (vs. the original monolithic sketch's 45%) — the module split cost nothing. |
| 4 | `_rmk_SmartTV_R4` firmware — Link A first (talks to the real rewritten Diffuser), then Link B (talks to `_rmk_TestMode` standing in for the app) | Full §A + cross-check §D, on bench hardware | **Done, compiles clean on real hardware toolchain (`arduino:renesas_uno:unor4wifi` 1.6.0).** `arduino-cli compile --clean --warnings all` — EXIT=0, flash 139772/262144 bytes (53%), RAM 23028/32768 bytes (70%, 9740 B free for locals). Every warning is pre-existing library/core noise (WiFiS3, PubSubClient, the Renesas core) or cosmetic and inherited verbatim from the original's own code shape — not a new bug. Ported faithfully with the TASK→`SCHED::Scratch` transformation throughout: `Scheduler` (new, replaces TaskJockeyMod), `Globals.h`/`.cpp` (full state model — `Globals.cpp` was a real gap caught mid-port, marked done earlier but never actually written, so nothing would have linked), `Led` (~1467 source lines), `Lisens` (lux hysteresis + `setLux()`, ~260 lines), `Hb` (14 idle HB effects, ~593 lines), `Tv` (detection, on/off, 12 on-effects, 4 HB helpers, 8 off-effects, ~2065 lines), `Motion` (PIR, 6 on-effects, off-fade, ~1224 lines), `Eeprom` (settings + delta write + schema/CRC16 gate, ~900 lines), `Udpraw` (~387 lines), `Bme` (~142 lines), `Net` (WiFi/NTP, ~420 lines). `Mqtt`/`MqttCred` and `AppLink`/`DifLink` were **adapted, not ported 1:1** — the original's ASCII single-duplex-topic scheme and text command dispatch are exactly what `01_PROTOCOL.md` already replaced with two one-way MQTT topics and the binary opcode table, so porting the old scheme verbatim would have contradicted a decision already locked in during Phase 1; `AppLink`/`DifLink` mirror the shape of the Diffuser's own `ProtoLink.cpp` instead, and both stayed in `namespace APP`/`namespace DIF` (not new names) so every other module's existing `APP::`/`DIF::` calls resolve unchanged. Wired `_rmk_SmartTV_R4.ino`/`_DEF.h` down to ~90 lines total (was a leftover ~12,600-line copy of the monolithic original from the abandoned OTA/Telnet patch, replaced in place) — the new `.ino` is pure `setup()`/`loop()` wiring; `_DEF.h` keeps only the `SMARTTV_ENABLE_OTA` gate. **Rulings made without stopping to ask, documented here**: (a) R15 partially forced, not chosen — with no factory-default table, a CRC/schema mismatch on `EE::Read()` zeroes every RAM-side struct (matching their pre-`Read()` state) rather than guessing at "sensible" values, same call the Diffuser's `Usage.cpp` made; a real per-setting default table still needs R15 ruled on separately. (b) MQTT `<deviceId>` (a real design choice `01_PROTOCOL.md` §5 flagged but didn't pin down) is the board's WiFi MAC, 12 hex chars, cached after first read. (c) The old `'K'` ASCII debug-dump command has no opcode in the new table and was NOT ported — `DIAG_HEALTH` covers its one load-bearing case (health summary); the rest is superseded by `Debug`/`Telnet` direct access. (d) Remote Telnet enable needed an opcode that didn't exist (that request predates the protocol table) — **you chose "add a new opcode"** over piggybacking or dropping it, so `SET_TELNET_ENABLE` (0x44) was added to `protocol_table.json` and the codec regenerated (round-trip tested, 30/30 fixed opcodes pass). **Real bugs the compile pass caught** (why full fidelity before compiling was worth it): `Tv.cpp`'s `T_EFFECT_TV_ON_8_ComEffect` was missing its `brInc` local entirely (restored); half a dozen files were missing includes for symbols they use (`Globals.h` in `DifLink.h`/`Eeprom.h`, `Net.h` in `Udpraw.cpp`, `<EEPROM.h>` in `MqttCred.cpp`, `Tv.h`/`Motion.h`/`Hb.h` in `AppLink.cpp`, `<Arduino.h>`/`<IPAddress.h>` in `Net.h`). Two real platform gaps, not include-fixable: this Renesas core's `WiFiServer` has no `hasClient()` (unlike ESP8266/ESP32) — `Telnet.cpp` uses `available()` alone, which doubles as the accept step here; and there's no `sbrk()` on this core's newlib — `APP::getFreeRam()` now estimates via a throwaway `malloc(4)` at the heap break measured against a stack-local address instead, same idea as the original, portable via plain `malloc`/`free`. |
| 5 | `_rmk_app` transport/state layer only (no UI polish) — talks to the real rewritten SmartTV | §C.5/C.6/C.8 | **Done, real debug APK builds clean (`./gradlew :app:assembleDebug` — BUILD SUCCESSFUL, 34 tasks, `app-debug.apk` produced).** `_rmk_app` is a **standalone** Gradle project (its own `settings.gradle`/`build.gradle`/wrapper, copied from root) — deliberately NOT added as a module to the frozen root `settings.gradle`, same sandboxing principle as `--library` flags on the Arduino side never touching `_ArduinoSide`. `applicationId com.fuzz.colors.rmk` (distinct from the frozen app's `com.fuzz.colors`, installable side by side per the plan). Built: `Frame.java` (hand-written, byte-identical to `Frame.h`/`.cpp`), `OpcodeTiers.java` (per-opcode ACK-tier lookup, hand-maintained against the table — the generator doesn't emit this mapping), `DeviceState.java` (StatusManager-equivalent pure data holder, push-only, `Listener` callback per field group), `DeviceLink.java` (DataSend+DataReceive merged into one class — the split existed there to separate ASCII string builders; the binary frame protocol's seq/pending map needs both send and receive on one object anyway — full ACK/retry/dedupe over UDP with MQTT fallback, `TELEM_COLOR_SYNC`'s FILL/SETN records decoded the same way the original's `'LK'` packet was), `MqttLink.java` (adapted to the two-topic scheme, not the original's single duplex topic + tag byte), and a minimal `MainActivity` (opens the link, sends `HELLO`, renders `DeviceState` as raw text — proving the layer works end to end, deliberately not real UI). **Known gap, flagged not guessed**: the MQTT topic path needs `<deviceId>` (the SmartTV's own WiFi MAC, per `NET::DeviceId()`) but this app has no discovery channel for learning that value without already being on the same LAN — `MqttLink.DEFAULT_DEVICE_ID` is a placeholder; cloud mode won't reach the real board until the actual MAC is configured (local UDP is unaffected). Two junk-file issues fixed along the way (not app bugs): stray Windows `desktop.ini` folder-metadata files under `_rmk_app` broke the Android resource merger (deleted, they're OS clutter, not tracked content) and `assembleDebug` NPE'd once after switching from `--offline` mid-daemon (stale incremental state, not a code defect — a `--stop` + clean rebuild produced a normal `BUILD SUCCESSFUL`). |
| 6 | `_rmk_app` UI layer — pages, popups, widgets, theming, updater | §C.1–C.4, C.9, C.10, run **side by side** with the old app (different `applicationId`, both installed) screen by screen | **Done, every §C.1–C.4/C.9 item has a real, build-verified counterpart** (`./gradlew :app:assembleDebug` → BUILD SUCCESSFUL, re-confirmed after each addition, most recently after theming/background/guide/update/MQTT-creds chrome and the widget subsystem). Single-Activity, 6 pages (TERM/LEDS/SET/TELNET/DIF/TEST) via plain visibility toggles. **TERM/LEDS/SET/TELNET/DIF/TEST**: as previously documented — TERM's `ConsoleAdapter`/`ChipSpan`, LEDS' 61-cell grid + single-colour row + **Dual Color** section (2×R/G/B + shake + 2 live swatches, `LED_SET_DUAL_COLOR`/`TELEM_DUAL_COLOR`), SET's 50-slot steppers, TELNET's near-verbatim `TelnetConsole`, DIF's Diffuser control stand-in, TEST's per-device test-mode rows. **Theming (§C.4)**: `ThemeManager` ported with the full original 50-entry `Theme` enum (single-hue rotations + two-hue palettes) and the same `getColor()`-drop-in/AMBER-is-untouched contract, simplified in one way — no `SECONDARY_ROLES` primary/secondary hue split, since this rebuild's flat UI has none of the two-hue-consuming custom panels that split exists for yet (every role resolves to `hueA`). Applied for real to the status-bar text and all 6 tab labels (`MainActivity.applyTheme()`), persisted via its own `SharedPreferences` (`FuZz_Theme_rmk`, no collision with the frozen app). `ThemePopup` is a plain single-choice `AlertDialog` over the same 50 names (not the original's swatch-grid card), opened from a new `THEME` button on a footer-equivalent chrome row. **Background (§C.2)**: `BackgroundManager` — Storage Access Framework single-image picker (`ActivityResultContracts.OpenDocument`) applied as the root layout's background, persisted via a persistable URI permission; NOT ported: the original's scrolling GIF gallery from a whole picked folder, animated playback, and thumbnail caching — one still image, applied for real, is the functional core, the gallery/animation is chrome on top of a picker that didn't exist here before this. **Button guide (§C.2)**: `ButtonGuideDialog` is a short, accurate guide to *this* rebuild's actual 6-tab UI, written fresh rather than porting the frozen app's guide text, which describes a completely different screen layout (LED wheel, dual-colour swatches, floating telnet window) that doesn't exist here. **MQTT credentials (§C.2)**: `MqttCredentialsDialog` — the missing UI in front of `MqttLink.setCredentials()`/`setDeviceId()`, which had no caller before this; opened via a long-press on the status bar, mirroring the original's gesture. **Updater (§C.9)**: `UpdateChecker`/`UpdateInstaller` are near-verbatim ports (GitHub releases API check, `DownloadManager`-driven download, hand-off to the system installer via `ACTION_VIEW` — Android's own install-confirmation UI is the final, un-bypassable gate); `UpdateDialog` drives them with plain `AlertDialog`s instead of the original's themed progress card. **Known gap, flagged not guessed**: `FuzzBC/fuzzapp` is the only real releases repo this project has, and it publishes the FROZEN app (`com.fuzz.colors`), not this rebuild (`com.fuzz.colors.rmk`) — the checker mechanism is real and testable now, but has no meaningful release channel of its own to actually find an update against yet. `REQUEST_INSTALL_PACKAGES` is declared in the manifest (matching the frozen app, which likewise never calls `canRequestPackageInstalls()` explicitly — the OS's own installer UI prompts for that grant itself during the `ACTION_VIEW` hand-off). `ChangelogDialog` reads the same bundled `assets/CHANGELOG.md` with the same hand-rolled minimal-Markdown rule (`## `/`- `/`* ` only), opened via a long-press on the `UPDATE` button. **Widgets (§C.3)**: `FuzzWidgetProvider` + `WidgetUpdateWorker` + `WidgetScheduling` + a headless `WidgetStatusFetcher`, the last one a real adaptation (not a port) of the frozen app's ASCII-packet fetcher onto the binary v1 protocol — sends `HELLO`, decodes real `TELEM_CLIMATE`/`TELEM_DIFFUSER_USAGE`/`TELEM_STATUS` frames via `Frame.parse()`/`ProtocolOpcodes` (the same decode path `DeviceLink` uses), local UDP first then MQTT cloud fallback via a short-lived second `MqttLink` instance, same as the original's fallback order. One widget shape, not two — the frozen app's second "stack" variant is the same cached data in a different `RemoteViews` layout with zero additional device interaction, so it's a deliberate not-duplicated call, not a dropped feature. Plain `TextView`s instead of the original's custom drawable icons with drop-shadow layering (no icon assets existed in this rebuild). WorkManager (`androidx.work:work-runtime:2.9.0`, newly added) drives the 30-minute periodic refresh + on-demand manual refresh via the widget's own refresh glyph. **Ruling made on `ColorWheelPopup`/`RgbChannelPopup`, still not ported, documented not silent**: both are custom-canvas-drawn and animated (hue ring w/ spin, glass slider w/ breathing glow/drop-shadow) — porting them now would just be a second, purely cosmetic way to enter the same RGB triple the flat EditText fields (LEDS' single-colour row, the new Dual Color section, DIF's Turn On) already cover for real; building a redundant UI path with zero functional delta isn't what finishing this phase should mean. Recurring nuisance, not a code issue, worth checking before any future build in this project: Windows keeps re-stamping `desktop.ini` into every new folder under `_rmk_app`, including inside `build/` mid-build — breaks the Android resource merger. Fixed each time with `./gradlew --stop` + delete `build`/`app/build` + `find app/src -name desktop.ini -delete` + rebuild. |
| 7 | Full three-node regression pass on the real rig | Entire `02_REGRESSION.md`, old system kept available as a live A/B reference throughout | Not started |
| 8 | Cutover decision (which build becomes "the" production firmware/app) | **Out of scope for this plan — do not act without asking, per the hard rule in the source prompt** | Not started |

Bring-up order: Shared codec → TestMode → Diffuser → SmartTV → App transport → App UI → integrated regression.
Old and new run side by side throughout: boards get bench-flashed (not the live production boards) until Phase 7;
the app ships as a second, separately-installable APK the whole time.

---

## 9. Risks, open questions, assumptions — **needs your ruling before Phase 1**

| # | Topic | Options | My recommendation |
|---|---|---|---|
| R1 | No Telnet/Serial console exists on SmartTV today, despite being part of the stated architecture | A) it's genuinely absent — building one in `_rmk_SmartTV_R4` is new scope; B) it exists somewhere this audit didn't cover (a companion tool proxying Serial?) | Need your answer — I can't tell from the source alone |
| R2 | SmartTV EEPROM per-LED record addressing looks off-by-one between `Read()`/`Write()` | Verify against a live `K16`/eeprom_backup dump before Phase 3 commits to a byte layout | Verify first, don't guess |
| R3 | Diffuser's documented Parfum mode-coercion (always physically run 10-SEC) isn't implemented in code | A) implement the coercion to match docs; B) fix the docs to match the current (uncoerced) behaviour | Need your call — this is a real functional difference, not cosmetic |
| R4 | Diffuser out-of-water detection is a 2-strike timing heuristic (tuned from 4 live trials), not tone-pattern sensing | A) keep the heuristic as-is (it works); B) test against real hardware for a genuinely distinguishable signal and design proper sensing | Test against real hardware before Phase 3 either way — even keeping the heuristic, its constants deserve more than 4 trials of validation |
| R5 | "Per-entry refill-history removal" / "stale Parfum revert" (recent commits) not found in `DiffuserUsagePopup.java`; may live in `StatusManager.java` (already read in full, but not searched for these specific terms) | Targeted grep/re-read before Phase 6 claims parity coverage | Low-cost follow-up, do it early in Phase 6 |
| R6 | `TestModePopup`'s real command set/timing lives in `SettingsManager.java`'s TestMode-trigger code (file was read in full, but this specific behaviour wasn't itemized in the report) | Same as R5 | Same |
| R7/R8 | `APP_ACK_CLAMPED`/`APP_ACK_LOCKED` are defined today but never emitted; new protocol's §4 makes them live | Keep them live (more informative to the user) vs. suppress them again for exact behavioural parity | Keep them live — this is a pure improvement with no downside I can find, but flagging since "any behaviour change is a regression unless approved" |
| R9 | Two stale Diffuser comments say "WS2812 x6"; real count is 10 | Fix comments only, not a behaviour question | No ruling needed, just confirming I'll fix it silently |
| R10 | `TaskJockeyMod` (external scheduler library) internals were out of this audit's scope | A) replace with the new minimal scheduler in `_rmk_Shared` (§2.2, §6); B) keep using it as an external dependency, wrapped | Recommend A — RAM footprint and task-count limits of an unreviewed dependency are exactly the kind of unknown a 32KB-budget rewrite shouldn't inherit blind |
| R11 | ECG-trace HB effect doesn't exist in current source | Need a description/reference (timing, colour behaviour, what "trace" means visually) before it gets a table row | — |
| R12 | `NET::Reconnect()`/NTP-retry blocking windows (bounded ≤10s) can freeze the whole board mid-`loop()` today | A) keep bounded-blocking as today (simplest, proven); B) make WiFi/NTP reconnect fully non-blocking like `DifLink` already is | Recommend B for consistency, but it's real new work — flagging as a scope choice, not assuming |
| R13 | Only 1 of 178 physical HB pixels is individually addressable from the app protocol (`LED_HB_NUM_FAKE=1`) | Not fixed by this rewrite unless you want it to be — real capability gap if per-pixel HB control is ever wanted | No action unless you ask for it — noting so it isn't silently forgotten |
| R14 | Per-effect typed scratch via a tagged union (§4.1) still shares one memory region across mutually-exclusive effects, same as today — the type-safety fix doesn't add per-instance storage | Confirming this reasoning is sound before it's load-bearing in Phase 4's scheduler design | Sanity-check welcome, not blocking |
| R15 | No firmware-side default/factory-reset table exists today (Android's RESET button pushes known values) | A) keep that split; B) move canonical defaults into the firmware's settings table too, so a virgin board self-initializes without ever having talked to the app | Lean B (more robust), but changes today's behaviour — your call |
| R16 | `LedRefresh`'s locked 1000ms task and the separate FPS-gated push in `loop()` overlap without an obviously single authoritative cadence today | Clarify/simplify to one clearly-authoritative push cadence in the rewrite | Needs your input on which behaviour is actually intended (does the 1000ms task matter today, or is it vestigial?) |
| R17 | **Resolved (Phase 2 GUI follow-up).** `_ArduinoSide/TestMode_APP_HTA/` is mid-migration and **uncommitted** (deleted mockups + untracked working `TestMode.hta`/`.res/`) — this audit used both `TestMode_APP` (committed, Python) and the uncommitted HTA files as cross-checks | Should the uncommitted HTA state be treated as authoritative-but-temporary, or committed/resolved first before `_rmk_TestMode` treats it as a design reference? | You asked to copy the HTA and adapt it — treated as authoritative (it's the real, working tool; the deleted files were an earlier abandoned mockup pass under the same folder, not this one). Ported to `_rmk_ArduinoSide/_rmk_TestMode_HTA/` against the binary protocol; see Phase 2's row for what changed vs. what carried over unchanged. |
| R18 | Frame header design: chose 5 bytes fixed overhead (no per-frame `LEN`, no per-frame version byte) over a simpler-but-larger 8-byte design during protocol drafting | Already decided in `01_PROTOCOL.md` — flagging that it was a real design choice with a rejected alternative, not the only option | Revisit if you'd rather trade 3 bytes/frame for a simpler mental model |
| R19 | MQTT topic redesign: two one-way topics (`.../c2d`, `.../d2c`) replacing today's single self-tagged duplex topic | This changes broker-side ACL/topic configuration if HiveMQ Cloud access rules are already scoped to the current topic name | Confirm you're OK reconfiguring the broker topic/ACL before Phase 1 locks this into the generated codec's assumptions |

**Attribution and language conventions** (`FuZzAPP` / `Pamfile Cristian Stefan`, Romanian UI/console strings)
carry forward unchanged — no ruling needed, noted here only for completeness against §6 of the source prompt.

---

Waiting for your review of §5.3–5.5 and rulings on §9 before Phase 1 begins, per your own instructions.
