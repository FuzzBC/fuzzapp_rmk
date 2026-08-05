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
