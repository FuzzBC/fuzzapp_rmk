# Release signing setup

`app/build.gradle` now has a real `release` signing config instead of
reusing the debug key, and `release` builds are no longer `debuggable`.
Neither the keystore file nor its passwords are ever committed - they're
read from `local.properties`, which is already gitignored (see its own
header comment and the repo's `.gitignore`).

Until you do this one-time setup, release builds still work - they just
fall back to the debug key and print a warning in the Gradle console.

## Option A - Android Studio's wizard (easiest)

1. **Build → Generate Signed Bundle / APK…**
2. Pick **APK**, then **Create new…** under Key store path.
3. Fill in a keystore location (anywhere outside the repo folder is
   simplest, e.g. your user home directory), a store password, a key
   alias (e.g. `fuzz-release`), a key password, and the certificate info
   (name/org - can be anything, it's not displayed to users).
4. Finish the wizard once to actually generate the file - you can cancel
   out of actually building the APK at the end, the keystore is already
   saved to disk at that point.
5. Add the four properties below to `local.properties` by hand (the
   wizard doesn't write these itself).

## Option B - command line (`keytool`, ships with the JDK)

```bash
keytool -genkeypair -v -keystore fuzz-release.jks -keyalg RSA -keysize 2048 -validity 10000 -alias fuzz-release
```

It'll prompt you for a store password, a key password (can be the same
as the store password), and some certificate questions (name, org,
etc. - answer with anything, none of it is user-facing). Move the
resulting `fuzz-release.jks` somewhere outside the repo.

## Wiring it up

Add these four lines to `local.properties` (create the file if you
don't already have one - Android Studio normally generates it for you
with just `sdk.dir` in it):

```properties
RELEASE_STORE_FILE=C:\\Users\\il3ga\\fuzz-release.jks
RELEASE_STORE_PASSWORD=your store password
RELEASE_KEY_ALIAS=fuzz-release
RELEASE_KEY_PASSWORD=your key password
```

`RELEASE_STORE_FILE` can be an absolute path (as above) or relative to
the project's root folder. Windows paths need doubled backslashes
(`\\`) same as `sdk.dir` already does in that file.

Sync Gradle, then **Build → Generate Signed Bundle / APK…** (or just a
normal `release` build) will sign with your real key instead of the
debug one.

## What this does and doesn't fix

A properly release-signed APK is the correct baseline and a better
trust signal than a debug-signed, debuggable one - but Google Play
Protect still flags **any** sideloaded APK from outside the Play Store
on first install of each new build, regardless of signing key. The
only way to make that warning disappear entirely is distributing
through the Play Store (even just an internal-testing track). This
setup is a real improvement, not a full fix for that.

**Keep `fuzz-release.jks` backed up somewhere safe.** If you lose it,
every future update will need a *different* key, and Android refuses
to install an update signed by a different key than the one already on
a device - anyone with the app installed would need to uninstall the
old one first to get the new one.
