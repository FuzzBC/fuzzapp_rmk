# Instructions for AI assistants working on this project

## Before publishing an Android app release

Whenever you make changes to the Android app (`app/`) that will ship in the
next release build (`assembleRelease` / the "FuZz (release)" run
configuration), **add an entry to [CHANGELOG.md](CHANGELOG.md)** describing
what changed, *before* the release is built and published.

- Add it at the **top** of the file, under a new `## <versionMajor>.XXX`
  heading matching the versionName the *next* build will get - that's
  `version.properties`'s `versionMajor` and `versionCode`, formatted as
  `<versionMajor>.<versionCode zero-padded to 3 digits>` (e.g.
  `versionMajor=1, versionCode=4` → `## 1.004`). versionMajor is its own
  property, not always "1" - check `version.properties` if unsure, don't
  guess either number.
- Write it for the person using the app, not for another engineer: plain
  language, what changed and why it matters to them - not file names,
  function names, or implementation details.
- A few bullet points is enough. Skip entries that are pure internal
  refactors with no user-visible effect.

This file is bundled into the app at build time (see the `copyChangelog`
Gradle task in `app/build.gradle`) and is what the in-app **"V: 1.XXX"**
label (bottom-left of the main screen) shows when tapped - it's the app's
"What's new" screen, and the same text becomes the GitHub release's
description via `publish_release.ps1`. If you skip this step, the release
still publishes, but the release notes and in-app "What's new" screen will
be a generic placeholder instead of a real changelog.

## Project-specific conventions

- `version.properties` is the source of truth for the app's `versionMajor`
  and `versionCode`; never hand-edit `versionCode`/`versionName`/the major
  version number in `app/build.gradle` directly (see the comment there).
- `github_release.properties` (the publish token) and any file matching it
  is gitignored and must never be committed - see
  `github_release.properties.example` for the template.
- **Never hardcode a WiFi password, OTA password, API token, or any other
  credential directly in a tracked file** - not in a `.ino`/`.h`, not in a
  `.py`/`.pyw`, not in a `.json` config. This repo is public.
  - WiFi SSID/password and the Diffuser's OTA password live in
    `_ArduinoSide/_Shared/WiFiCredentials.h` - gitignored, never committed.
    Both `_FuZzAPP_Diffuser.h` and `_FuZzAPP_SmartTV_R4_DEF.h` `#include
    <WiFiCredentials.h>` (angle brackets - a quoted relative path like
    `"../_Shared/WiFiCredentials.h"` does NOT reliably resolve under
    arduino-cli, confirmed by an actual "No such type or directory" compile
    failure despite the file existing on disk). `compiler.py`'s `compile()`
    instead adds `_ArduinoSide/_Shared` as an ad-hoc `--library` search path
    on every compile, computed from `sketch_path` (no hardcoded paths) - so
    there's still one file to edit for both devices.
    `FuZzAPP_FlashConsole.pyw` reads the same file at runtime
    (`_load_ota_password()`) for the OTA flash password - that's plain
    Python file I/O, unaffected by the arduino-cli include quirk above.
    `WiFiCredentials.h.example` is the tracked template - copy it to
    `WiFiCredentials.h` in the same folder and fill in real values.
  - `_ArduinoSide/.Compiler/flashconsole_config.json` is gitignored the same
    way - see `flashconsole_config.json.example`.
  - If a new sketch/tool needs a secret, add it as another `_VALUE` macro (or
    JSON key) in this same shared file/template pair rather than inlining it
    - don't create a second, differently-named credentials mechanism.
