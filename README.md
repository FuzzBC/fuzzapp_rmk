# FuZzAPP

An Android app + Arduino firmware ecosystem for a WiFi-connected LED
lighting rig (TV backlight, bed/lamp strips, a heartbeat strip) with a
companion scent diffuser — driven automatically by the TV turning on/off,
motion, ambient light level, or manually from the phone. The app and the
boards talk over a local UDP protocol on the home network, with an MQTT
(HiveMQ Cloud) link as an automatic fallback when the phone is off the LAN.

This repository is the full source: the Android app, both Arduino firmwares,
and the desktop tooling used to build/flash/debug them.

## Download

Prebuilt APKs are published on this repo's
**[Releases](https://github.com/FuzzBC/fuzzapp/releases)** page (tagged
`V<version>`, e.g. `V8.007`). Installing requires enabling "install from
unknown sources" for your browser/file manager, since this isn't
distributed through the Play Store — the app has its own in-app update
checker (`UpdateChecker`), so once installed it'll notify you here again
for future releases. Google Play Protect may flag the APK as unverified
(the release build isn't Play-Store-signed — see `CHANGELOG.md`).

## Repository layout

### Android app (`app/`)

The controller app (package `com.fuzz.colors`). Talks to the SmartTV board
over UDP (falling back to MQTT), drives LED color/effects, shows live
temperature/humidity/diffuser status, exposes home-screen widgets, and has
an in-app updater that checks GitHub Releases.

Key classes (`app/src/main/java/com/fuzz/colors/`):

| Class | Responsibility |
|---|---|
| `MainActivity` | Orchestrates every other subsystem; owns the Android lifecycle, global UI helpers, and the Debug dialog. |
| `DataSend` / `DataReceive` | Outgoing/incoming UDP packets — the wire protocol to the SmartTV board. |
| `MqttTransport` | Cloud fallback transport over HiveMQ Cloud (TLS) — mirrors the UDP link when the phone isn't on the same network as the board. |
| `LEDManager` | LED state and the color/effect UI. |
| `SettingsManager` | All persisted settings (EEPROM-backed on the board side) and their UI. |
| `StatusManager` | System status: TV/motion/climate sensors, connection health. |
| `FuzzWidgetProvider` / `FuzzWidgetStackProvider` / `WidgetUpdateWorker` / `WidgetStatusFetcher` / `WidgetScheduling` | Home-screen widgets and their background refresh. |
| `TelnetConsole` / `ConsoleAdapter` | The in-app Term console (telnet client to the boards, chip-based log rendering). |
| `UpdateChecker` / `UpdateInstaller` / `UpdatePopup` | Checks this repo's [GitHub Releases](https://github.com/FuzzBC/fuzzapp/releases) for a newer build and installs it. |
| `ColorWheelPopup`, `RgbChannelPopup`, `ThemePopup`, `TestModePopup`, `DiffuserUsagePopup`, `BackgroundPopup`, `ButtonGuidePopup` | Feature-specific popups. |

Two vendored/forked libraries live alongside it as their own Gradle modules
(`settings.gradle`): `library/` (a color-picker widget, based on
[QuadFlask/ColorPicker](https://github.com/QuadFlask/ColorPicker)) and
`avloadingindicator/` (loading-spinner widgets, based on
[czy1121/AVLoadingIndicatorView](https://github.com/czy1121/AVLoadingIndicatorView)).

### Arduino firmware (`_ArduinoSide/`)

**`_FuZzAPP_SmartTV_R4/`** — the main controller, an **Arduino UNO R4 WiFi**.
Owns every LED zone (TV backlight, com/uCom, bed, lamp, heartbeat), reads the
TV-on sensor / motion sensors / light sensor / BME280 climate sensor, drives
the diffuser over a local UDP link, keeps NTP time, and speaks the UDP+MQTT
protocol to the phone app. `_FuZzAPP_SmartTV_R4_DEF.h` is the single master
header (every define/enum/struct); `_FuZzAPP_SmartTV_R4_changelog.md` is its
detailed technical changelog.

**`_FuZzAPP_Diffuser/`** — the diffuser bridge, a **WeMos D1 Mini
(ESP8266)**. The diffuser itself is an unmodified standalone appliance with
no electrical interface; this board reads its buzzer confirmation tones on
an analog pin and drives its MODE button by simulating a physical button
press, turning it into a WiFi-controllable device. Also drives a WS2812 LED
ring around the diffuser. Config is in `_FuZzAPP_Diffuser.h`;
`_FuZzAPP_Diffuser_changelog_entry.md` is its changelog.

**`_Shared/WiFiCredentials.h`** *(gitignored — you create this)* — the WiFi
SSID/password and the diffuser's OTA password, shared by both sketches and
by the FlashConsole tool below. See [Credentials setup](#credentials-setup).

### Desktop tooling (`_ArduinoSide/.Compiler/`, `_ArduinoSide/TestMode_SmartTV/`)

**`FuZzAPP_FlashConsole.pyw`** — a Tkinter GUI that wraps `arduino-cli` to
compile and flash both boards without touching a command line: pick a
device (SmartTV over serial, Diffuser over WiFi/OTA), Compile, Flash, and
watch a live docked Serial/Telnet console with auto-reconnect. Its helper
modules live in `.res/`: `compiler.py` (arduino-cli process wrapper —
compile, serial upload, OTA upload via `espota.py`), `network.py` (serial +
telnet connection handling), `fuzz_icon.png` (window icon).

**`TestMode_SmartTV/TestMode.pyw`** — a standalone command console for
poking either board directly over raw UDP/TCP without the phone app, e.g.
to script or replay protocol commands during development. `.res/gui.py`
(Tkinter UI), `.res/core.py` (protocol encode/decode helpers shared by the
UDP/TCP command modules), `.res/commands_smarttv.py` /
`.res/commands_diffuser.py` (the actual command tables for each board),
`.res/net.py` (socket helpers), `.res/tcp_send.ps1` / `.res/udp_send.ps1`
(one-off PowerShell equivalents for sending a single raw packet from a
terminal).

### Release tooling (repo root)

- **`publish_release.ps1`** — after a release APK is built, publishes it as
  a [GitHub Release](https://github.com/FuzzBC/fuzzapp/releases) on this
  repo (tag `V<versionMajor>.<versionCode>`), pulling the release notes
  straight out of `CHANGELOG.md`. Needs `github_release.properties` (see
  `github_release.properties.example`) with a GitHub token scoped to this
  repo.
- **`version.properties`** — the single source of truth for `versionMajor`
  / `versionCode`; `app/build.gradle` reads it, and `bumpVersionForNextBuild`
  increments `versionCode` after a successful release build.
- **`CHANGELOG.md`** — the app's user-facing "What's new" text (see
  `AGENTS.md` for the convention); bundled into the APK at build time and
  shown in-app when the version label is tapped.
- **`DeleteBuild.bat`** — recursively deletes stray `build/` folders and
  `desktop.ini` files (a recurring artifact of this project living in a
  cloud-synced folder on Windows) from the whole tree.
- **`AGENTS.md`** — conventions for AI assistants (and humans) working on
  this repo: changelog discipline, versioning rules, and — most importantly
  for a public repo — the credentials rule described below.

## Credentials setup

**No WiFi password or OTA password is committed to this repository.** Both
Arduino sketches and the FlashConsole tool read them from one local,
gitignored file:

```bash
cp _ArduinoSide/_Shared/WiFiCredentials.h.example _ArduinoSide/_Shared/WiFiCredentials.h
```

Then edit `_ArduinoSide/_Shared/WiFiCredentials.h` and fill in your real
network name/password and a password for the diffuser's OTA updates:

```c
#define WIFI_SSID_VALUE      "YourNetworkName"
#define WIFI_PASS_VALUE      "YourNetworkPassword"
#define OTA_PASSWORD_VALUE   "YourOtaPassword"
```

`WiFiCredentials.h` is listed in `.gitignore` and will never be picked up by
`git add`/`git status` as a change to commit. The same pattern is used for
`_ArduinoSide/.Compiler/flashconsole_config.json` (per-machine device
config — COM port, board IP, etc.; see `flashconsole_config.json.example`)
and for `github_release.properties` (the GitHub publish token; see
`github_release.properties.example`).

## Building

### Android app

Open the repo root in Android Studio (Gradle modules: `app`, `library`,
`avloadingindicator` — see `settings.gradle`) and run/build the `app`
module normally. `minSdk 21`, `targetSdk`/`compileSdk 34`.

### Arduino firmware

1. Install [Arduino CLI](https://arduino.github.io/arduino-cli/) and the
   board cores: `arduino:renesas_uno` (UNO R4 WiFi) and `esp8266` (D1 Mini).
2. Set up `_ArduinoSide/_Shared/WiFiCredentials.h` as above.
3. Run `_ArduinoSide/.Compiler/FuZzAPP_FlashConsole.pyw` (`pythonw
   FuZzAPP_FlashConsole.pyw`), pick a device, and use Compile/Flash. SmartTV
   flashes over USB serial; the Diffuser flashes over WiFi (OTA), so it must
   already be running firmware that has WiFi/OTA available.

## Communication protocol (short version)

- **App ↔ SmartTV board:** UDP, port `APP_UDP_PORT` (see
  `_FuZzAPP_SmartTV_R4_DEF.h`), one-letter command codes with an optional
  `#SS<cmd>` sequence-numbered ACK envelope. Falls back to MQTT
  (`MQTT_HOST`/`MQTT_TOPIC` in the same header) when UDP is unreachable —
  MQTT credentials are supplied by the app at runtime and cached in EEPROM,
  never hardcoded.
- **SmartTV board ↔ Diffuser board:** UDP, port `DIF_UDP_PORT` (8439),
  short text commands (`Ds`/`Dc` status, `Dn…` turn on, `Df` shutdown,
  `Dp…` timed "parfum" run) with the same `#SS`/ack scheme.
- **Console access:** both boards expose a Telnet console (port 23) and a
  USB-serial console with the same human-readable command set, used by
  `FuZzAPP_FlashConsole.pyw`'s docked console and `TestMode.pyw`.

## License

No license file is currently included — all rights reserved by default.
Add a `LICENSE` file if you want to define terms for reuse.
