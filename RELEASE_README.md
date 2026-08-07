# FuZzAPP — Releases

This is intended as the `README.md` for the [`FuzzBC/fuzzapp-releases`](https://github.com/FuzzBC/fuzzapp-releases)
repository (copy it there) — the public distribution point for signed
builds of the FuZzAPP Android app. **Source code lives in a separate
repository:** [`FuzzBC/fuzzapp`](https://github.com/FuzzBC/fuzzapp).

## What is FuZzAPP?

FuZzAPP is the Android controller for a WiFi-connected home lighting rig
(TV backlight, bed/lamp LED strips, a heartbeat strip) and a scent
diffuser — automated off TV on/off, motion, and ambient light, or driven
manually from the phone. It talks to two Arduino-based boards over the
local network (with an MQTT cloud fallback when you're off-WiFi), and adds
home-screen widgets and a live device console. See the source repo's
[README](https://github.com/FuzzBC/fuzzapp#readme) for the full
architecture and both boards' firmware.

## Download

Grab the latest APK from this repo's **[Releases](../../releases)** page
(each release is tagged `V<version>`, e.g. `V8.007`). The app has its own
in-app update checker (`UpdateChecker`), so once installed it will notify
you here again next time.

Installing requires enabling "install from unknown sources" for your
browser/file manager, since this isn't distributed through the Play Store.
Google Play Protect may flag the APK as unverified — the release build
isn't Play-Store-signed; this is expected, see the source repo's changelog.

## What's new

Each release's notes are pulled from the source repo's
[`CHANGELOG.md`](https://github.com/FuzzBC/fuzzapp/blob/main/CHANGELOG.md)
at publish time and shown both here (on the release page) and in-app.

## Building it yourself

This repo only hosts built APKs. To build from source, set up the
firmware/credentials, or contribute, see the source repository:
**https://github.com/FuzzBC/fuzzapp**

## License

No license file is currently included — all rights reserved by default.
