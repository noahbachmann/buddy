# android/screener-ime – the screener's keyboard (#66)

Android input method on the screener phone. The app's comment field hides its text from the accessibility tree, so
`uiautomator` can't read it back; a keyboard owns the field's InputConnection and can. It also replaces the stock
keyboard, whose autocorrect and suggestion chips changed typed text.

- `src/main/kotlin/buddy/screener/ime/ScreenerIme.kt`: the whole thing. Receiver registered while the keyboard runs;
  actions `buddy.screener.ime.TYPE` (`--es b64 <base64 UTF-8>`), `.CLEAR`, `.READ`. Every answer = the field's full
  text after the command: `result=1`, `data=<base64>`; failure `result=0`, `data=no_field|bad_b64|unreadable`.
- Only senders with `WRITE_SECURE_SETTINGS` (adb shell, system) can drive it.
- Input view = a thin "Screener keyboard" label, no keys: covers little of the app. Dev typing by hand switches to
  the stock keyboard (nav bar keyboard icon / `ime set`).

## Build and install (on the VM; the Pi has no Android SDK)

```bash
ANDROID_HOME=$HOME/android-sdk ./gradlew assembleRelease     # JDK 17; R8 on: ~33 KB, debug-signed
scp build/outputs/apk/release/screener-ime-release.apk buddy:/tmp/ && ssh buddy 'adb install -r /tmp/screener-ime-release.apk'
ssh buddy 'adb shell ime enable buddy.screener.ime/.ScreenerIme && adb shell ime set buddy.screener.ime/.ScreenerIme'
```

Drive it: `adb shell am broadcast -p buddy.screener.ime -a buddy.screener.ime.TYPE --es b64 "$(printf %s "$t" | base64 -w0)"`
→ `Broadcast completed: result=1, data="<base64 of the field>"`.

## Gotchas

- `READ` answers ok/empty even with no field focused (Android's dummy connection): only a read-back after `TYPE`
  proves the text landed in the right field.
- `ime enable` right after `adb install` can fail with "Unknown id": the IME list refreshes a second later.
- A new debug key (VM rebuilt) → `adb uninstall buddy.screener.ime` before installing.
