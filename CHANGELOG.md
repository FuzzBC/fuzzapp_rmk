# Changelog

Each entry's heading is the exact `versionName` (matches the app's
"V: 1.XXX" label and the GitHub release tag `V1.XXX`). Newest first.

See AGENTS.md for the rule on keeping this updated.

## 8.026
- Internal: this build actually ships the debug/verbose logging work from
  the last update, which never made it into a published version -
  StatusManager, LEDManager, the widget refresh path (fetch/worker/both
  home-screen providers), the Diffuser Telnet console, and the update
  installer all now log to adb (Log.d/Log.v - no UI or console impact),
  for easier diagnosis of future issues.

## 8.025
- Internal: removed the extra-verbose per-stage cloud connection logging
  added in 8.023 - it did its job (found the real bug, fixed in 8.024) and
  was too noisy to leave on permanently. Console still shows CLOUD ON/LOST/
  FAIL as before.

## 8.024
- Fixed the real cause of "Connection lost" (8.020-8.023 fixed other real
  bugs, but not this one): the controller's own device id is delivered in
  a fixed-size slot padded with filler bytes when the id is shorter than
  the slot, and those filler bytes were being kept as part of the id
  instead of trimmed off. That corrupted id got saved and reused for
  every cloud topic afterwards - accepted while logging in, but rejected
  the instant the app tried to actually listen for replies, which looked
  identical to a dropped connection. Fixed at the source, and any phone
  that already had the corrupted value saved self-repairs automatically -
  no need to clear app data or log in again.

## 8.023
- Added: much more detailed cloud-connection logging in the console (no PC
  needed) - every connect attempt now shows which internal attempt number
  it is, how long each stage (dial broker / subscribe) actually took in
  milliseconds, the exact underlying error (not just a summary), and how
  long a session had been alive before it dropped. Needed to keep chasing
  the "Connection lost" issue with real numbers instead of guessing.

## 8.022
- Fixed: cloud login could still fail with "Connection lost" even after the
  last two fixes (8.020, 8.021) - those closed one race but missed another.
  This app's MQTT library had its own built-in auto-recovery that could
  reconnect on its own the instant a connection dropped for any reason
  (a brief mobile-data hiccup, for example) - running completely outside
  the app's own connection-attempt sequencing, so it could still collide
  with a normal reconnect happening around the same time. That built-in
  auto-recovery is now off; reconnecting after a drop goes exclusively
  through the app's own already-sequenced path instead.

## 8.021
- No functional changes - rebuild of 8.020 (same fix, fresh version number).

## 8.020
- Fixed: cloud login could still fail with "Connection lost" after both of
  the last two fixes (8.018, 8.019). Root cause: two connection attempts
  happening close together (background retry + a manual Save, or even a
  fast double-tap) each opened their own connection to the broker using
  this device's identity - only the bookkeeping *afterward* was protected
  before, not the connection attempts themselves, so the broker could
  boot whichever one it saw second, sometimes moments after the app had
  already shown "CLOUD ON". Connection attempts are now fully queued end
  to end, so only one is ever actually talking to the broker at a time.

## 8.019
- Fixed: cloud login could still fail with "Connection lost" (the last fix
  covered a different cause). This app shares its cloud identity with the
  original app, so having both connected to the cloud at the same time
  made them repeatedly kick each other off. This build now uses its own
  separate identity, so that can't happen anymore.

## 8.018
- Fixed: cloud login could fail with "Connection lost" even with the
  correct username/password - two connection attempts happening close
  together (a background retry and a manual save, for example) could
  interfere with each other and tear down a connection that was actually
  working. Connection attempts are now properly sequenced so this can't
  happen.

## 8.017
- Fixed: the cloud login dialog showed "Rejected by broker" for any
  connection failure - a bad password, a network hiccup, or anything else,
  all looked identical. It now shows the real reason so it's actually
  possible to tell what went wrong.

## 8.016
- Fixed: the console could spam "CLOUD LOST"/"CLOUD FAIL" every few seconds
  during an extended cloud outage. It now logs the failure once per
  outage instead of on every retry.

## 8.015
- Added: a "MQTT CRED" button in the debug panel to reopen the cloud
  login dialog any time, pre-filled with whatever's currently saved - no
  more waiting for the app to prompt you automatically. Also added a
  SHOW/HIDE button next to the password field so you can double-check
  exactly what's typed or saved.

## 8.014
- Fixed: cloud mode required connecting once on your home WiFi before it
  would work, which wasn't always possible (e.g. away from home). The
  SmartTV's cloud address is now fixed from the start on both ends, so
  cloud mode works immediately - no local-WiFi bootstrap step needed
  anymore, on this or any additional phone.

## 8.013
- Fixed: the in-app "update available" check was still looking at the old
  project's releases, so it could never see a new version here. This build
  still won't detect itself (you're reading this because you already
  installed it by hand) - but from here on, future updates should show up
  automatically.

## 8.012
- Fixed: cloud mode (used automatically when your phone has no local WiFi)
  could get stuck showing "CLOUD - NO REPLY" forever, even though the
  broker connection itself was fine. The app now learns the SmartTV's real
  cloud address the first time it connects over local WiFi, instead of
  using a placeholder that never matched. Requires one local-WiFi
  connection after updating (both the app and the SmartTV firmware) for
  the fix to take effect before cloud-only mode works again.

## 8.011
- Added: a "TV TELNET" toggle in the debug panel to remotely enable or
  disable the SmartTV's diagnostic Telnet server.
- Internal: rebuilt how the app talks to the SmartTV and Diffuser (a more
  reliable binary format instead of plain text). No other visible changes,
  but this build only works with matching firmware - it will not talk to
  an older device.

## 8.008
- Security/repo hygiene (no effect on the app itself): the Arduino firmware's
  WiFi and OTA passwords are no longer hardcoded in source. They now live in
  a local, gitignored credentials file so the source tree — including this
  repository, now public — never contains a real password. See
  `_ArduinoSide/_Shared/WiFiCredentials.h.example` and the firmware
  changelogs under `_ArduinoSide/` for details.

## 8.007
- Fixed: the home-screen widgets could stop refreshing on their own after a
  while (stuck showing an old temperature/humidity/refill reading and an
  old "updated at" time) with no way to fix it short of removing and
  re-adding the widget. The background refresh job is now re-armed
  automatically whenever the widget redraws or the app is opened, so a
  refresh dropped by the phone's battery manager recovers on its own.

## 8.005
- Fixed: if a home-screen widget refreshed in the background at the exact
  moment the app was waiting for the board to confirm a command, the board's
  reply could be delivered to the widget instead of the app - making the app
  wrongly show "NO ACK" even though the command actually went through.
- Fixed: an active heartbeat LED animation could keep running in the
  background (and draining battery) after closing the app, instead of
  stopping when the app closes.
- Changed: the brightness/clear increment slider (a small 1-10 range) now
  snaps to each value with a visible dot, instead of a smooth drag where
  it was hard to tell exactly which value you'd landed on.

## 8.004
- Fixed: the home-screen widgets' background refresh never actually
  succeeded over local WiFi, even with the board on the same network -
  it silently timed out and fell back to MQTT cloud every single time.
  The board replies to a fixed port (it also uses that same channel for
  unsolicited pushes with nothing to reply to), but the widget's fetch
  was listening on a random port, so the reply always landed where
  nothing was listening. Now binds to the same fixed port the live app
  already uses for this reason.
- Release builds are signed with the debug key again (no separate
  release keystore) - simpler for now than maintaining a real signing
  key; a sideloaded APK from either build gets flagged by Play Protect
  regardless of signing, since that's tied to Play Store distribution,
  not the certificate itself.

## 8.003
- Fixed: long-press "RANDOM" on the Dual Color popup to hand-dial a pair on
  the colour wheel could crash or silently fail to open. Root cause was
  structural, not a timing fluke - the wheel was anchored to a button that
  lives inside the already-open Dual Color popup, and Android rejects a new
  popup window parented to another popup's window. Fixed by anchoring to the
  activity's own window instead; applied the same defensive guard against
  the underlying crash class to every other popup in the app.
- Fixed: home-screen widgets (strip and stack) could show a real "Updated"
  time with permanently blank temp/hum/diffuser values. The cloud (MQTT)
  refresh path returned as soon as any one board packet arrived instead of
  waiting for the readings themselves, so a fast status packet could
  short-circuit the fetch before climate/diffuser data showed up.
- Fixed: the Dual Color console log (SAVE DUAL COLOR / shake-triggered)
  showed three separately-tinted numbers instead of a proper colour swatch,
  unlike the matching incoming-echo line. Both now render as real L/R colour
  chips.
- Widget "Updated HH:MM" label shortened to "U:HH:MM".
- Cleared every Java deprecation warning across the app, library, and
  avloadingindicator modules (Handler, NetworkInfo, PackageInfo.versionCode,
  startActivityForResult migrated to the modern Activity Result API, etc.) -
  no functional changes.

## 8.000
- Version scheme rebrand: `versionMajor` bumped 1 → 8 and `versionCode`
  reset to 0 to mark the start of the 8.0 generation. No functional app
  changes in this entry - see 1.017 below for the last feature set carried
  forward.

## 1.017
- Added two home-screen widgets: a compact one-line strip and a taller
  stacked version, both showing temperature, humidity, diffuser refill
  percent, and whether the app last reached the controller over WiFi or
  Cloud Mode. Both refresh in the background roughly every 30 minutes,
  have a manual refresh button and a "last updated" time, and tap through
  to open the app.
- The diffuser icon on both widgets now turns blue if it's out of water,
  and shows "REF" instead of a percentage once it's time for a refill.
- Replaced the scrolling rainbow connection-status text with two clearer
  effects: a gentle flicker while connected, and a red scanning line while
  not connected.
- Added an INFO button next to RESET in Settings that explains what every
  button in the app does on tap versus press-and-hold.
- The Parfum panel (diffuser usage popup) now always opens collapsed,
  even while a session is already running - a small "active" indicator
  shows next to the header instead so you can still tell at a glance.
  The +/- buttons and minutes box are also a bit smaller.
- Release builds are now signed with a proper release key instead of the
  debug key (see RELEASE_SIGNING.md for one-time setup).

## 1.016
- Cloud Mode no longer uses one login shared by every install. The first
  time the app needs Cloud Mode and you haven't set one up yet, it now asks
  for your own broker username/password, checks them live with the
  controller, and only saves them once they're confirmed to work. A wrong
  password is rejected right away instead of failing silently later.
- Cloud Mode login is now checked directly against the broker from your
  phone first, so a typo or wrong password is caught in a couple of seconds
  instead of waiting on a slow round trip to the controller.
- Setting up Cloud Mode while away from your home WiFi now works immediately
  instead of silently waiting until you're back home.
- Added a way to force-resend (or set up in advance) your Cloud Mode login:
  press and hold the connection status label for 5 seconds.
- Declining the Cloud Mode login popup now actually stops it from popping up
  again - it no longer keeps re-asking every few seconds.
- Removed swipe-to-change-page - TERM / LEDS / SET now have their own
  buttons, bottom-right, so a scroll or drag never accidentally flips the
  page or gets blocked by the old swipe detection.
- Dual Color's swatches moved out of the always-visible bottom row into
  their own popup (tap the new DUAL COLOR button) - same random-pair and
  saved-pair behaviour as before, just out of the way until you need it.
- Fixed the diffuser's Parfum mode not actually starting when tapped from
  the app.
- Diffuser Telnet console now prints in the same clean two-column style as
  the SmartTV console.

## 1.010
- Added themed "Update available" and "Downloading update" popups (percent,
  size, speed, Cancel), replacing the plain system dialog and silent Toast.
- The "Update available" popup now shows what's actually new in that
  version (pulled from this changelog), instead of a generic message.
- Fixed: the system install prompt could silently never appear after a
  download finished (completion broadcast was registered
  `RECEIVER_NOT_EXPORTED`, which blocks the system's own broadcast).
- Version numbers shown in the app now consistently use the "1.XXX" format
  instead of the raw internal release-tag number.
- Added a "V: 1.XXX" label, bottom-left of the main screen above the
  dual-colour swatch row - tap it to see this changelog ("What's new").
- Release tags now read "V1.XXX" instead of a bare number.
- `assembleRelease` now auto-publishes to GitHub and auto-increments the
  version for the next build (only when the publish actually succeeds).
- Fixed: after a WiFi drop, the status label could read "CLOUD MODE" even
  though the Arduino had never actually replied over the cloud link (it
  only reflected the phone's own connection to the broker). Now shows
  "CLOUD - NO REPLY" until the board has genuinely answered over cloud.
- Long-pressing the connection status label now forces an immediate
  reconnect attempt (same sequence as a fresh app start), instead of
  waiting for the next automatic retry.
- Internal: renamed UDPSend/UDPReceive to DataSend/DataReceive (aliases
  DATAs/DATAr) since both classes are shared by local UDP and the MQTT
  cloud link, not UDP-only. No behavior change; every method now documents
  its actual caller(s), and log tags moved from UDP_S/UDP_R/UDP_ERR to
  DATA_S/DATA_R/DATA_ERR.

## 1.1
- First release published through GitHub Releases.
