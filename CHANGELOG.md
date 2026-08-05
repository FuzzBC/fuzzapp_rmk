# Changelog

Each entry's heading is the exact `versionName` (matches the app's
"V: 1.XXX" label and the GitHub release tag `V1.XXX`). Newest first.

See AGENTS.md for the rule on keeping this updated.

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
