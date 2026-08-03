# Changelog

Each entry's heading is the exact `versionName` (matches the app's
"V: 1.XXX" label and the GitHub release tag `V1.XXX`). Newest first.

See AGENTS.md for the rule on keeping this updated.

## 1.007
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
