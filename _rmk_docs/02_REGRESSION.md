# 02_REGRESSION — Complete externally-observable-behaviour checklist

Source: full-file audits of `_FuZzAPP_SmartTV_R4.ino`+`.h` (12,429+1,281 lines), `_FuZzAPP_Diffuser.ino`+`.h`
(2,758+277 lines), `DataSend/DataReceive/MqttTransport/SettingsManager/StatusManager/TelnetConsole/ConsoleAdapter.java`
(6,045 lines), `MainActivity/LEDManager/HBEffectSimulator/ThemeManager/*Popup/Widget*/Update*` (14,182 lines), and
`TestMode_APP/.res/{core,gui,commands_smarttv,commands_diffuser}.py` (used as a third, independent cross-check of the
wire protocol). This is the master list every `_rmk_` phase must be checked against — a rewritten node is not "done"
with a phase until every box it owns still behaves identically (or the deviation is called out and approved).

Legend: **[BUG]** = current behaviour looks unintentional (comment/code mismatch, dead code path, etc.) — preserve
unless §5.9 of `00_PLAN.md` gets an explicit ruling to fix it. **[GAP]** = not independently verified by this audit,
needs a targeted read before sign-off.

---

## A. Node 1 — SmartTV (Arduino UNO R4 WiFi)

### A.1 Console / Serial / Telnet

- [ ] **[BUG?]** No Telnet server (port 23) and no Serial-input command parser exist anywhere in the current
      `.ino` — `Serial` is write-only. This contradicts the system description ("Telnet console"). Either this
      capability is genuinely absent today (in which case `_rmk_SmartTV_R4` adding one is new work, not parity),
      or it lives somewhere not covered by this audit. **Needs your ruling — see 00_PLAN.md §9, item R1.**
- [ ] All 23 `K`-debug targets (`led_info, led_selected, led_order, led_color, led_tempcolor, motion, tv, ee, app,
      udpraw, bme280, ambientmode, wifi, arduino, lisens, heartbeat, dif, task, rtc, testmode, all, mqtt,
      eeprom_backup`) reachable only via the `Kii` UDP/MQTT command, mirrored to `Serial` and to the app's term log.

### A.2 UDP/MQTT commands accepted (Link B, port `APP_UDP_PORT`, mirrored on MQTT topic `LEDs/cmd`)

- [ ] `k` — keepalive, bare, no ACK, no dispatch.
- [ ] `Z` — welcome/reconnect: adopts transport, forces full resync (`LM`, full `S`, `@`, force `LK`), (re)arms 10s keepalive.
- [ ] `X` — toggle LED enable; disable blanks everything + kills tasks + shuts diffuser; enable re-arms motion.
- [ ] `S` (bare) — read all 50 settings.
- [ ] `Sii` — read one setting.
- [ ] `Sii vv [...]` — write 1..N settings, range-validated, live-pushes diffuser-relevant changes, debounced EEPROM write.
- [ ] `A0`/`A1` — Ambient Mode off/on; ON blocked unless UDPRAW off, TV off, BED motion inactive.
- [ ] `Kii` — trigger one of the 23 debug dumps (§A.1).
- [ ] `@ii` / `@Dvv` / `@Lvv` — Test Mode family (§A.6).
- [ ] `$<b64user>,<b64pass>` — MQTT credential provisioning, live-broker-verified before persisting to EEPROM.
- [ ] `!00` — read-only health diagnostic. (`!` + any other code → UNSUPPORTED.)
- [ ] `Ds` → `DIF::RequestStatus()` (also forces `s`/`u`/`p` resend).
- [ ] `Dh` → `DIF::RequestHistory()`, relayed verbatim.
- [ ] `Dr` → `DIF::ManualRefill()`.
- [ ] `Df` → `DIF::Shutdown()`.
- [ ] `DnXXee` → `cmdDiffuserTurnOn()`, **blocked unless a source (TV/UDPRAW/Motion/Ambient) is active**.
- [ ] `DpMMMME` → `cmdDiffuserParfum()`, 4-hex minutes + 1-hex mode, NOT gated on active source, 0 min = cancel.
- [ ] `LBvv` — brightness 0–120; UDPRAW active → applies to stream + diffuser; Motion active → **rejected**; else `T_SMOOTH_CHANGE`.
- [ ] `LC` (bare) — force full colour-sync reply.
- [ ] `LCrrggbb` — set colour; UDPRAW→stream+diffuser; Motion→sets motion colour directly (disables random-colour); else smooth-change + disables random-colour.
- [ ] `LD`/`LDrrggbbRRGGBB` — get/set dual colour; blocked if UDPRAW, Ambient, or Motion(COM/BED) active.
- [ ] `Ld...` — same as `LD` but launches the "shake" chaotic-strobe variant.
- [ ] `LO<61 chars of 0/1>` — set LED selection bitmask.
- [ ] Unrecognized command → UNSUPPORTED ack + logs sender IP.

### A.3 UDP/MQTT telemetry sent (Link B)

- [ ] `s` — TV/Motion/UDPRAW/Ambient/Diffuser-summary, dirty-gated.
- [ ] `H` — temp/humidity, dirty-gated.
- [ ] `E` — LED enabled flag, dirty-gated.
- [ ] `p` — parfum minutes remaining, only while active or on the drop-to-0 transition.
- [ ] `u` — diffuser usage-accum/avg/refill-count/lifetime-refills, dirty AND diffuser-reachable gated.
- [ ] `M` — lux level, dirty-gated.
- [ ] `w` — \|RSSI\| bucket (0–4) + wifi state, dirty on **bucket** change only.
- [ ] `f` — fault bitmask (WIFI/NTP/EEPROM/BME/DIF_NORESP/DIF_NOWATER), transition-only, never on welcome.
- [ ] `@` — active test-mode, on entry and on self-cancel.
- [ ] `e` — EEPROM write result, on completion.
- [ ] `k` — bare keepalive every 10s of silence.
- [ ] `LM` — max brightness (120), welcome-only.
- [ ] `S`+50×(idx,val) — full settings dump, on `Z`/`S` request.
- [ ] `LD...` — current dual colour, on get/info request.
- [ ] `LK` (binary FILL/SETN records) — coalesced colour-sync delta, ≤1×/LED-FPS-period.
- [ ] `#SSR` — ACK: seq + 1-hex result nibble, per enveloped command.
- [ ] `*`+level+source+seq+text — term-log line, on demand.
- [ ] **[BUG]** `APP_ACK_CLAMPED`(1) and `APP_ACK_LOCKED`(4) are defined but never actually emitted — clamping happens silently.
- [ ] **[GAP]** Legacy ASCII `'LC'` colour-sync "rollback path" mentioned in changelog 3.1.0 — no longer found in source; confirm it's truly gone.
- [ ] **[BUG]** `APP_PROTO_VER` has no live wire carrier since the 3.0.0 rework removed the `'V'` identity packet — no protocol-mismatch detection currently reaches the app.

### A.4 Automations & preemption

- [ ] TV ON/OFF triggers the selected TV-on/TV-off effect pair (§A.7), lux-adaptive delay/increment only.
- [ ] Motion triggers the selected motion-on effect + shared motion-off fade, lux-adaptive delay/increment + a separate lux "hold" nudge while lit.
- [ ] UDPRAW streaming (port 5568) structurally suppresses `MOTION::Status()` and periodic `LED::Refresh()` while active; `TV::Status()` keeps reading the pin but its `On()`/`Off()` calls are suppressed.
- [ ] Ambient Mode reachable only when UDPRAW off AND TV off AND BED-motion inactive.
- [ ] LED-ownership priority: **UDPRAW > Motion(active) > TV/idle**, Ambient only from full-idle.
- [ ] Diffuser auto-on/off source priority: **TV > UDPRAW > MOTION > Ambient** (note: differs from LED-ownership order above — confirmed real, not a transcription error).
- [ ] Diffuser idle-pulse: starts after `DIF_IDLE_WAIT_MIN` of nothing active, runs `DIF_IDLE_ON_MIN` with strip forced black, then reverts.
- [ ] Lux-adaptive speed (`getLuxAdaptFactor/Delay/Inc`) applies only to TV-on/TV-off/Motion event-animation delay & increment; `T_LUX_BR_CHANGE` and `T_UDPRAW_SET_COLOR` always run at raw EE speed.

### A.5 Effects registered (identical IDs, timing, and feel required)

- [ ] HB idle 1–14: WhiteMove, Heartbeat, RandomFade, TravelingShadow, ExpandingRaindrops, Colors, ShootingStarRandom, RandomSparklingPop, GlidingAurora, TheGlitchMatrix, StochasticPlasma, DigitalRain, DualPulse, RainbowWavePulse.
  - [ ] **No "ECG-trace" effect exists today** — confirmed absent. Building it is net-new work, not a migration item.
- [ ] TV-ON 0–11 (10 distinct impls, 4/5 and 6/7 are literal aliases): Default, RandomStatic, MidToOutSep, MidToOutAll, HalfRun(×2 offsets), MidToExt(×2 anchor counts), ComEffect, QuadPointHB, LiquidFill, PixelBoot.
- [ ] TV-ON HB helper 0–3 (runs in parallel with the TV-ON effect, own phase counter): FadeOn, CenterBloom, LinearSweep, QuadPoint.
- [ ] TV-OFF 0–7 (7 distinct impls, 4/5 alias): Default, DelayWTvOff, DelayAll, SlowTvSequential, Countdown(×2 flicker variants), RandomHalf, QuadPointHB.
- [ ] Motion-ON 0–5: Default, FromMiddle, LineMoving, Random, Cascade, TheCollision (has an explicit convergence-safety cap, `MOTION_COLLISION_BLOOM_MAX_TICKS`=150).
- [ ] Motion-OFF: single non-selectable HB→COM→BED fade-to-black.
- [ ] No independent per-zone (BED/COM/LAMP) effect table — baked into whichever TV/Motion effect currently drives them.

### A.6 Test Mode (`@` family)

- [ ] `@ii` 0x00–0x05 (none/tvOn/tvOff/udpraw/motionCom/motionBed).
- [ ] `@Dvv` — diffuser test: 00 off, 01–04 mode, FF re-push current/last with fresh random colour (bypasses active-source guard on purpose).
- [ ] `@Lvv` — force lux level 1–4 via the same `setLux()` path a real sensor reading takes.
- [ ] Auto-cancels after 120s (`TESTMODE_DURATION`); re-selecting the same mode restarts the countdown; leaving a mode runs its specific exit cleanup (UDPRAW teardown / diffuser auto-off / lux reset-to-live).
- [ ] Every transition pushes `@`+2-hex to the app, on entry and on self-cancel.

### A.7 Settings (50 EEPROM-backed, ids 0–44 registered, 45–49 reserved/unused)

- [ ] All 41 named settings from the SmartTV audit's full table (TV ×13, Motion ×9, HB ×4, Ambilight ×3, Diffuser ×10, Other ×6, reserved ×5) preserved with identical id, range, and default — cross-check against the Android `SettingsManager.java` table (§C.7) and `commands_smarttv.py`'s `EE_SETTINGS_TABLE`; all three sources agree.
- [ ] **No firmware-side default/factory-reset logic exists** — EEPROM defaults are (apparently) pushed by the Android app's RESET button as a batch `S` write, not baked into the board. **[GAP]** confirm from Android `SettingsManager.java` reset-button code before assuming this.

### A.8 EEPROM persistence

- [ ] 50-byte settings block, 244-byte LED colour block, 244-byte ambient-colour block, 4-byte UDPRAW colour, 3-byte motion colour — all in the low ~550-byte region.
- [ ] 6-byte cross-boot self-test block and 67-byte MQTT-credential block anchored at the high end of the ~8192-byte EEPROM, growing toward the low region.
- [ ] Chunked delta writer (1 byte/tick, 60s debounce from last change) — only dirty bytes rewritten.
- [ ] **[BUG?]** `Read()`'s and `Write()`'s 4-byte-per-LED-record addressing appear off-by-one relative to each other — verify against a live `K16`/eeprom_backup dump before the rewrite trusts the byte layout (00_PLAN.md §9, item R2).
- [ ] No schema-version byte, no CRC/checksum on any persisted block beyond per-byte write+readback.

### A.9 NTP / time

- [ ] `NTPClient`, UTC+3 fixed offset, resync every 6h, one-shot 10s retry on failure, `RTC_EpochUTC()` only consumed today for the `APP_FAULT_NTP` bit (no wire packet carries device time since the 3.0.0 rework removed `'t'`).

---

## B. Node 2 — Diffuser (WeMos D1 Mini / ESP8266)

### B.1 Console (Serial + Telnet, port 23, shared handler)

- [ ] `M0`–`M4` — force MODE target directly.
- [ ] `P<decimal min>` — start/cancel Parfum via **decimal** minutes (asymmetric with the UDP `Dp` hex encoding — preserve or unify, see 00_PLAN.md §4/§9).
- [ ] `E` — cycle to next of 8 WS2812 animated effects.
- [ ] `Crrggbb` — manual solid-colour strip test.
- [ ] `S` — quick status.
- [ ] `D` — full debug dump.
- [ ] `?`/`H` — help.
- [ ] `T` — self-test sweep (compiled only under `DIF_TEST_MODE`).
- [ ] `Q` (Telnet only) — closes the session cleanly.
- [ ] Only one Telnet client at a time; a new connection always evicts the previous one.

### B.2 UDP protocol (Link A, port 8439)

- [ ] `Ds`/`Dc` — status query (verbose/silent) → `Ds`+mode(2hex)+strip(2hex)+parfum-min(4hex)+usage-min(4hex)+avg-min(4hex)+refill-count(2hex)+lifetime(4hex), 24 chars.
- [ ] `Dh` — full 10-entry refill history on demand.
- [ ] `Dr` — manual refill: bank usage, reset accumulator, clear OOW.
- [ ] `DyII` — remove one 1-based history entry (no bulk-clear by design).
- [ ] `Df` — shutdown; **queued** (not rejected) if Parfum active, applied on natural expiry or resolved per the priority order (queued Dn > queued Df > pre-parfum restore > shutdown).
- [ ] `DpMMMME`/`Dp0000` — start/cancel Parfum, minutes clamped (not rejected) above 360, `E` mandatory whenever minutes≠0.
- [ ] `DnXXrrggbbBREESP`/dual variant — turn on with mode/colour/brightness/effect/speed; queued if Parfum active.
- [ ] `!00`/`!04` — read-only health / parfum-trace diagnostics, own `'!'`-prefixed reply convention (deliberately distinct from `Ds`/`#SSR` to avoid an `'E'` collision bug).
- [ ] Ack envelope `#SS<hex result>` — OK/CLAMPED/REJECTED/LOCKED/**NOWATER-never-emitted-here**/UNSUPPORTED. No retry/timeout logic on the Diffuser side by design (single-attempt send).
- [ ] Status-reply deferral: `Dn`/`Df` requests needing physical MODE stepping defer their `Ds` reply until the target is reached or a 3000ms (`MODE_CONFIRM_MS`) timeout force-flushes it.
- [ ] **[GAP]** "Locked 5s status poll" mentioned in the task's system description was not found on the Diffuser side — likely lives in the SmartTV's `DIF_STATUS_CHECK_S`=5s poll interval instead; verify cross-node.

### B.3 Diffuser control state machine

- [ ] Modes M0 OFF / M1 CONT / M2 "10 SEC" (self-pulsing, opaque to firmware) / M3 "2H after sleep" / M4 "4H after sleep"; hardware only steps forward, reaching a lower target requires wrapping through OFF.
- [ ] Parfum: 1–360 min, while active `Df`/`Dn` refused-but-queued (last one wins, applied only on natural expiry).
  - [ ] **[BUG]** Documented "always coerce to physical 10-SEC mode regardless of requested E" (`PARFUM_RUN_MODE`) is **not implemented** — `PARFUM_RUN_MODE` doesn't exist in code; the caller's mode passes through unmodified. **Needs your ruling** — implement the documented coercion, or update the docs to match current (uncoerced) behaviour (00_PLAN.md §9, item R3).
  - [ ] Violet `#B400FF` PULSE cue is only the seed colour on an unsolicited-shutdown mid-parfum auto-restart; normal parfum operation shows the random-hue "breathe" fill/drain animation instead.
- [ ] Out-of-water detection is a **pure behavioural/timing heuristic**, not a distinct buzzer tone: two unsolicited multi-beep shutdowns within 10s of power-on (one natural + one after a single auto-restart) ⇒ OOW. **[GAP]** whether the real hardware emits a genuinely distinguishable tone for this condition is unverified against physical hardware — flag for real-device testing before the rewrite commits to (or improves on) this heuristic (00_PLAN.md §9, item R4).
- [ ] WS2812 ring (confirmed **10 LEDs**, not 6 as two stale comments claim): OFF/STATIC/DUAL/8 animated effects (Fade/Pulse/Random/Rainbow/Sparkle/Fire/Bounce/Confetti)/WIFI_CUE (spinning rainbow while reconnecting)/OOW_CHASE (blue single-LED chase, internal-only)/PARFUM_BREATHE (random-hue fill/drain, internal-only).
- [ ] OTA update flow (`ArduinoOTA`, hostname `FuZz-Diffuser`, password from shared `WiFiCredentials.h`).
- [ ] Cold-boot-only WiFi resync sequence: single-steps MODE with real buzzer confirmation until ground truth is recovered, caps at 6 attempts then force-issues a long-press shutdown; deferred entirely if Parfum is active; never re-runs on a mid-session WiFi drop.

### B.4 EEPROM persistence

- [ ] 50-byte block: usage-accum(4B) + refill-count(1B) + refill-next-index(1B) + 10×refill-history(40B) + lifetime-total(4B).
- [ ] No schema version/magic byte; virgin-flash (`0xFF`) and out-of-range count both sanity-reset to 0 on load.

---

## C. Node 3 — Android app (`com.fuzz.colors`)

### C.1 Pages

- [ ] TERM (console) — default-adjacent page; console RecyclerView, Clear Console, Debug button.
- [ ] LEDS — default page; 60 per-LED buttons + HB view, select-all/deselect/off, colour picker, 3 RGB channel fields → `RgbChannelPopup`, Send Color, Save Dual Color, dual-colour grid (apply/long-press-delete), brightness bar+steppers.
- [ ] SET — ~40 settings across 6 categories, footer buttons THEME/BG/INFO/Default/Enable-Disable/TestMode trigger.
- [ ] Telnet — floating draggable window + FAB, not a swipe page.

### C.2 Popups

- [ ] ColorWheelPopup — hue-ring dual-marker picker, SPIN (animated reroll, ≥2 revolutions)/SET.
- [ ] RgbChannelPopup — single-channel 0–255 slider, live preview swatch.
- [ ] BackgroundPopup — scrollable GIF thumbnail list (first-frame decode, LRU cache 64), SAF folder picker.
- [ ] DiffuserUsagePopup — headline state (OOW/LEARNING/remaining-time), 4-stat box, 5s-hold FORCE REFILL, collapsible Parfum accordion (minutes stepper 1–360, mode chips, START/OFF). **No per-client history list — all numbers are caller-supplied aggregates.**
  - [ ] **[GAP]** "Per-entry refill-history removal" and "stale Parfum revert" (named in recent commits) not found in this popup — confirm they live in `StatusManager.java`/`SettingsManager.java` before assuming parity coverage (00_PLAN.md §9, item R5).
- [ ] TestModePopup — generic reusable option-picker chrome only; the actual Test Mode command set/timing lives in `SettingsManager.java`, **not independently verified by this audit** (00_PLAN.md §9, item R6).
- [ ] ButtonGuidePopup — static hand-maintained reference card, 6 sections, explicitly "no way to derive automatically" (guaranteed drift risk — note for rewrite, don't silently "fix" without a decision).
- [ ] UpdatePopup — showAvailable / showProgress / showChangelog, 3-in-1 card family.
- [ ] ThemePopup — radial sunburst (19 single-hue + 31 dual-hue rays), drag-scrub live-apply, no Apply/Cancel.
- [ ] MQTT-credentials dialog (inline in MainActivity) — Save / 5s-hold force-resend / decline (only clearable via the same 5s-hold).

### C.3 Widgets

- [ ] FuzzWidgetProvider (4×1 strip) and FuzzWidgetStackProvider (1×4 vertical) — both plain `AppWidgetProvider`s (no collection/RemoteViewsService despite the "Stack" name). Show temp/humidity/diffuser refill%/connection icon/last-update time.
- [ ] Tap body → open app; tap refresh glyph → immediate "Refreshing…" + one-off WorkManager job.
- [ ] WorkManager-only scheduling: 30-min periodic (`KEEP` policy) + on-demand one-time (`REPLACE` policy); `updatePeriodMillis=0` in both widget-info XMLs (deliberate, avoids double-fire with the OS's own 30-min floor); `ensureIfAnyPlaced()` re-armed defensively every `onResume()`.
- [ ] `WidgetUpdateWorker` always returns `Result.success()` even on total fetch failure (deliberate — avoids WorkManager backoff); always calls both providers' update to clear "Refreshing…".
- [ ] `WidgetStatusFetcher` — headless duplicate of the UDP/MQTT parsing logic (local UDP first with a `SO_REUSEADDR`-collision guard, then MQTT with an 8s timeout / `CountDownLatch` on temp+diffuser-% both arriving).

### C.4 Theming

- [ ] 50 themes (19 single-hue + 31 dual-hue), hue-rotation of the existing `colors.xml`, `AMBER` = literal unrotated default.
- [ ] `SharedPreferences "FuZz_Theme"`, process-lifetime in-memory cache (external pref edits need a process restart to take effect — note as current behaviour, not necessarily desired).
- [ ] Pull-based `ThemeManager.getColor()` + `SettingsManager.applyTheme()` walks every live view (no `recreate()`).

### C.5 Transport & ACK protocol (app side)

- [ ] Every non-bare send wrapped `#SS<cmd>` (2-hex seq 0–255, starts at 1), `ACK_TIMEOUT_MS`=350 generic / `EXT_ACK_TIMEOUT_MS`=3000 for relayed-through-diffuser or lux commands / `MQTTCRED_ACK_TIMEOUT_MS`=9000 for `$`. `ACK_MAX_RETRIES`=2 (3 total transmissions), same seq reused across retries.
- [ ] Bare/unsequenced commands (no retry, reply-is-confirmation): `LC`(get), `LD`(get), `S`(get-all), `K`, `Z`, `k`.
- [ ] Unknown/duplicate/late ACK → silently dropped, no crash.
- [ ] NO-ACK outcome → console line + spinner colour only; **no automatic fail-over to MQTT and no connection-dead marking** — that's a fully separate, timestamp-driven transport-liveness state machine.
- [ ] UDP↔MQTT failover: `isUdpAvailable()` (WiFi+socket+real-packet-within-25s or 4s-post-welcome-grace) vs `isMqttArduinoAlive()` (genuine non-echo cloud payload within 25s) — two independent liveness proofs, re-evaluated every 5s by the supervisor; on any transport transition, resync (`_ResyncDeviceState()`) re-pushes app-local state (LED selection, settings).
- [ ] MqttTransport mirrors UDP byte-for-byte over one duplex topic `LEDs/cmd`, 1-byte sender tag (`A`/`D`) for self-echo filtering, QoS 0, no LWT/retained presence.
- [ ] MQTT credential flow: two-stage verify (app-side direct connect, then board-side `$` push over UDP if available or held pending), `FuZz_MqttCred` prefs (plaintext), decline flag clearable only via 5s hold.

### C.6 Every incoming/outgoing packet (app side)

- [ ] All entries in the transport-layer audit's §2 (outgoing, 24 commands) and §3 (incoming, 21 packet types) — cross-reference 1:1 against Node 1's §A.2/§A.3 above; every packet must appear on both sides with matching format.
- [ ] Binary `LK` colour-sync (FILL/SETN) parsed directly from the raw receive buffer (not through the string dispatcher).

### C.7 Settings UI (SettingsManager.java, ~40 registered of 50 id slots)

- [ ] Full id/category/type/default/range table (§C-transport-audit) matches Node 1's §A.7 and `commands_smarttv.py`'s table — three-way cross-check already done, all agree.
- [ ] Control-type→widget mapping: SWITCH→SwitchCompat, SELECTABLE ≤5 opts→inline chips />5→modal popup, everything else→BubbleSeekBar+steppers.
- [ ] App-local (non-board) persisted prefs: `FuZz_Background`/`BG_FILE`, `FuZz_Theme`, `FuZz_MqttCred`, `FuZz_Telnet`, `FuZz_DualColor`, `FuZz_Parfum`, `FuZz_Widget`.

### C.8 StatusManager telemetry

- [ ] Every field in the transport audit's §7 table (TV/Motion/Ambilight/AmbientMode/Diffuser enums, temp/humidity, LED-enable, lux, parfum-remaining, diffuser-usage×4, diffuser-history, rssi/wifiState, fault bitmask).
- [ ] Push-only freshness model (no per-field staleness timer beyond the transport-level connection-lost timeout); `applyStatus()` bails on an identical-to-last packet to avoid animation/lock spam; diffuser refill% forces 0 during OOW regardless of the raw accum/avg math.

### C.9 In-app updater

- [ ] `UpdateChecker` polls GitHub Releases (unauthenticated REST), version-code comparison, unparseable tag silently reads as "up to date."
- [ ] `UpdateInstaller` uses `DownloadManager` (not raw HTTP), 400ms progress polling, `ACTION_VIEW` hand-off to the OS installer UI (no silent/session-API install), `REQUEST_INSTALL_PACKAGES` + `canRequestPackageInstalls()` gate.
- [ ] Changelog ("What's new") reads bundled `assets/CHANGELOG.md`, hand-rolled minimal Markdown (`## `/`- `/`* ` only).

### C.10 Telnet console (app side)

- [ ] Client-only, connects to the **Diffuser's** native Telnet (`192.168.1.203:23`) — read-only monitor pane, no command input UI in this class; reconnects every 3s while enabled; sends `Q\r\n`+120ms grace on disable.
- [ ] Shared `ConsoleAdapter` renders both Term and Telnet panes: `'*'`-envelope parsing, `[bracket]`/`{#X}` chip markup, 1000-line cap per instance.

---

## D. Cross-node contract sign-off

- [ ] Every Link A command/reply pair (App §C.6 ⇄ SmartTV §A.2/A.3 ⇄ Diffuser §B.2) matches byte-for-byte across all three independent sources audited (firmware source, app source, `TestMode_APP` command tables) — confirmed consistent in this audit; re-verify after any change during the rewrite.
- [ ] ACK result-code table (`0` OK … `6`/`7` UNSUPPORTED/UNAUTHORIZED) has the same meaning on both links; note `NOWATER`(5) is Diffuser-relay-timeout-synthesized on the SmartTV side, never emitted directly by the Diffuser.
- [ ] Diffuser-relay round trip (App→SmartTV→Diffuser→SmartTV→App) budgets ~5s client-side timeout (`TestMode`'s `timeout_ms: 5000` for all `*_relay` commands) — preserve this budget or document why it changed.
- [ ] Fixed-reply-port behaviour: neither firmware replies to the UDP request's source port — always to the port it itself listens on. A rewritten client/board that doesn't preserve this will see all replies silently dropped.

---

## E. Explicit known-bug inventory (carry into 00_PLAN.md §9 for a ruling on each)

| # | Node | Bug | Current effect | Options |
|---|---|---|---|---|
| R1 | SmartTV | No Telnet/Serial console exists despite being in the stated architecture | App's `TELNET` class only ever reaches the Diffuser | Confirm intentional / add as new work |
| R2 | SmartTV | EEPROM LED-record `Read()`/`Write()` addressing looks off-by-one | Unverified — could be silently fine or silently corrupting one byte/LED | Verify against a live EEPROM dump before trusting the layout in the rewrite |
| R3 | Diffuser | Documented Parfum mode-coercion (`PARFUM_RUN_MODE`) doesn't exist in code | `E=1` (CONT) genuinely runs continuously, not 10-SEC as docs claim | Implement the coercion / fix the docs — pick one |
| R4 | Diffuser | OOW detection is a 2-strike timing heuristic, not a tone signature | Works but empirically tuned from only 4 live trials | Verify against real hardware; keep or design better sensing in `_rmk_Diffuser` |
| R5 | App | "Per-entry refill-history removal" / "stale Parfum revert" (recent commits) not found in `DiffuserUsagePopup` | Feature location unconfirmed | Locate in `StatusManager`/`SettingsManager` before claiming parity |
| R6 | App | `TestModePopup`'s actual command set lives in unread `SettingsManager.java` code | Test Mode UI behaviour only partially documented here | Targeted follow-up read before finalizing regression coverage |
| R7 | SmartTV | `APP_ACK_CLAMPED`/`APP_ACK_LOCKED` defined, never emitted | Clamping happens silently, no ack signal | Decide whether the rewrite's protocol should actually surface these |
| R8 | SmartTV | `APP_PROTO_VER` has no wire carrier since 3.0.0 | No protocol-mismatch detection today | New protocol's version negotiation (01_PROTOCOL.md) replaces this outright |
| R9 | Diffuser | Two stale comments say "WS2812 x6"; real count is 10 | Cosmetic only | Fix comments in rewrite, not a behaviour change |
