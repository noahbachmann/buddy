# android/screener-dump – the screener's screen reader (#66, #76)

An `Instrumentation` that dumps the active window's accessibility tree in `uiautomator dump`'s exact format, without
`uiautomator`'s idle wait: an autoplaying video never lets the screen go idle, so `uiautomator dump` fails ("could
not get idle state") whenever one is on screen. Measured on the phone: byte-identical output on a still screen,
~1.5 s per read (`uiautomator dump`: ~1.9 s).

- `src/main/kotlin/buddy/screener/dump/Dump.kt`: the whole thing. Waits for idle best effort (500 ms quiet within
  2.5 s: settles a list after a swipe), then reads anyway. Result: `xml=<base64 gzip UTF-8>` (+ `idle=false`), or
  `error=<reason>`.
- Its own APK, targeting itself: `am instrument` force-stops the target package on every run, which must never be
  the app or the keyboard (`screener-ime`).
- `buddy_screener/device.py` uses it when installed and falls back to `uiautomator dump` otherwise.

## Build and install (on the VM; the Pi has no Android SDK)

```bash
ANDROID_HOME=$HOME/android-sdk ./gradlew assembleRelease     # JDK 17; R8 on: ~26 KB, debug-signed
scp build/outputs/apk/release/screener-dump-release.apk buddy:/tmp/ && ssh buddy 'adb install -r /tmp/screener-dump-release.apk'
ssh buddy 'adb shell am instrument -w buddy.screener.dump/.Dump | head -c 120'    # INSTRUMENTATION_RESULT: xml=H4sI…
```

## Gotchas

- Only one UiAutomation connection at a time on the phone: never run it in parallel with `uiautomator dump`.
- A new debug key (VM rebuilt) → `adb uninstall buddy.screener.dump` before installing.
