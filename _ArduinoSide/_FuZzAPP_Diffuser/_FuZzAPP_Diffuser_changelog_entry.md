## Security — WiFi/OTA passwords no longer hardcoded in source

**Files changed:** `_FuZzAPP_Diffuser.h`

- `WIFI_SSID`, `WIFI_PASS`, and `OTA_PASSWORD` were previously `#define`d
  literals committed directly in this header. They now resolve to
  `WIFI_SSID_VALUE`/`WIFI_PASS_VALUE`/`OTA_PASSWORD_VALUE` from a shared,
  gitignored `_ArduinoSide/_Shared/WiFiCredentials.h`, pulled in via
  `#include <WiFiCredentials.h>` so the real values are never committed.
  Angle brackets on purpose — a quoted relative path
  (`"../_Shared/WiFiCredentials.h"`) does not reliably resolve under
  arduino-cli (confirmed by an actual compile failure); `compiler.py`
  instead adds `_Shared` as a `--library` search path on every compile. The
  same file is shared with the SmartTV sketch (WiFi) and read by
  `FuZzAPP_FlashConsole.pyw` at runtime (OTA password) — one file to edit
  for all three. See `WiFiCredentials.h.example` and AGENTS.md. No firmware
  behavioural change.

## Diffuser firmware — effect set cleanup + Fire repair

**Files changed:** `_FuZzAPP_Diffuser_1_1_icLed.ino`, `_FuZzAPP_Diffuser.h`

- **Removed from the user-selectable effect set:** Chase, Centre_split, Color_wipe.
  Chase is not deleted — it's moved to a dedicated internal-only path
  (`stripOowChaseFrame()` / `tickStripOowChase()`, new `STRIP_OOW_CHASE` strip
  mode) reserved exclusively for the out-of-water alert. It's no longer
  reachable via the numbered `Dn ... EE` effect selector.
- **Removed entirely:** Comet.
- **Repaired Fire (case 5, was case 7):** the old flicker walk had a positive
  average drift (`random(0,60)-25` ≈ +4.5/tick), so every LED climbed to the
  heat ceiling and stuck near white — visually static, not flame-like. Replaced
  with a cool-then-spark model: every LED cools a small random amount each
  frame, and one random LED is reheated with a random spark each frame. This
  keeps the flicker bouncing around instead of saturating.
- **Renumbered** the remaining effects to stay contiguous (EE 1-8): Fade,
  Pulse, Random, Rainbow, Sparkle, Fire, Bounce, Confetti.
- `STRIP_EFFECT_COUNT` dropped from 12 to 8; `EFFECT_NAMES[]` updated to match.
- Removed now-dead `STRIP_FRONT_LO` / `STRIP_FRONT_HI` defines (only consumer
  was Centre_split).
- Updated all protocol/doc comments (top-of-file header, `stripEffectFrame()`
  doc, `udpExec()` doc) to reflect the new EE 00-08 numbering.

## Fire effect — realistic climbing-flame model

**Files changed:** `_FuZzAPP_Diffuser_1_1_icLed.ino`

- Replaced the flat "every LED cools/sparks independently" Fire with a
  classic cool→diffuse→spark model (Kriegsman "Fire2012"), split across the
  ring's two symmetric arms running from the back seam (base) to the
  front-centre pair (tip).
- New `fireArmStep()` helper: cools each cell by a random amount, diffuses
  heat from base toward tip (averaged with the two cells behind it, so
  embers visibly climb instead of flickering in place), then occasionally
  ignites a fresh spark at the base.
- `case 5` now runs `fireArmStep()` on two 5-LED arrays (`heatL`, `heatR`)
  and maps arm position back to physical LED index per the ring layout
  documented in `_FuZzAPP_Diffuser.h` (LED0/LED9 = seam/base, LED4/LED5 =
  front-centre/tip).

## Fix — strip blanked on mode-target chains that pass through OFF

**Files changed:** `_FuZzAPP_Diffuser_1_1_icLed.ino`

- Bug: MODE only ever advances forward (hardware limitation), so reaching a
  lower-numbered target mode requires wrapping through OFF first. That
  wrap called `applyShutdown()` → `stripOff()`, which blanked the strip —
  and nothing turned it back on once the chain reached the real target, so
  the LEDs stayed off.
- Fix: `applyShutdown()` now only blanks the strip for a genuine final
  shutdown (out-of-water, or a direct `Df`/`M0` request). If a mode-target
  chain is still active and aiming at a real mode (not OFF), the pass-through
  is treated as transient and the strip keeps showing whatever it's already
  on.

## Feature — auto-restart on unsolicited shutdown before diagnosing out-of-water

**Files changed:** `_FuZzAPP_Diffuser_1_1_icLed.ino`, `_FuZzAPP_Diffuser.h`

- Bug: the out-of-water check in `buzzerFinalizeBurst()` didn't distinguish
  a shutdown WE commanded (`Df`/`M0` long-press) from the diffuser dropping
  out on its own — a deliberate quick off within `WATER_OUT_TIMEOUT_MS` of
  power-on could get wrongly latched as out-of-water.
- New `g_shutdownCmdPending` flag, set by `cmdModeTarget()` right before its
  long-press, lets `buzzerFinalizeBurst()` tell commanded shutdowns apart
  from unsolicited ones.
- Unsolicited shutdowns no longer diagnose out-of-water immediately: the
  first fast drop (within `WATER_OUT_TIMEOUT_MS` of power-on) triggers one
  auto-restart with the last Dn settings (`g_lastDnMode` / `g_lastDnPayload`,
  cached in `cmdDiffuserOn()`). Only a second fast unsolicited drop right
  after that restart is diagnosed as out of water.
- `g_shutdownRetried` gates the single retry per power-on cycle and is
  cleared again on a confirmed real power-on in `applyModeAdvance()`, so a
  later, unrelated drop still gets its own retry.
- An unsolicited drop that happens well after the timeout (i.e. it ran fine
  for a while) is just restored with the last settings — not treated as a
  water symptom.
- `cmdDebugInfo()` ("D") now reports `lastDn` / `retried` state for
  diagnostics.

---
**AI name tracking feature added - now captures which AI assistant was used for each prompt**

---
**Testing AI name detection from session ID**

---
**Debug test for AI name fields**

---
**AI name tracking test**
