<picture>
  <source media="(prefers-color-scheme: dark)" srcset="logos/png/lockup-on-dark.png">
  <img src="logos/png/lockup.png" alt="Nightcap" width="374">
</picture>

**[sethryder.github.io/nightcap](https://sethryder.github.io/nightcap/)**

A nightcap for monitors that will not go to sleep: a tiny Windows tray app that turns your monitors off when you lock the screen, and keeps them off.

Windows leaves the monitors on after Win+L if a browser is playing video, because the browser holds a "display required" power request. Nightcap sends the monitor-off command shortly after the session locks (2 seconds by default), and re-sends it every 30 seconds while the machine stays locked and nobody is touching it. Nothing changes while the session is unlocked.

- One exe of about 75 kB, no admin rights, no dependencies. The installer is optional.
- Idle until a lock event; no polling while unlocked.

## Use

Download `Nightcap-setup.exe` from the [releases page](https://github.com/sethryder/nightcap/releases) and run it. It installs for the current user only (no admin prompt), adds a Start menu shortcut, starts the app at login and can be removed from Windows' installed-apps list.

Prefer no installer? Download the bare `nightcap.exe` instead, put it somewhere permanent and run it.

Right-click the tray icon for:

- **Enabled**: turn the behaviour on or off (resets to on at next start).
- **Delay after lock**: how long after locking the monitors go off, from 2 seconds to 5 minutes. Saved under `HKCU\Software\Nightcap`.
- **Start at login**: adds or removes an entry under `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Toggle it again if you move the exe.
- **Exit**

The installer and exe are unsigned, so SmartScreen will warn on first run, and some antivirus tools are suspicious of very small native executables. Build it yourself if you would rather not trust the binary.

## Build

With MinGW-w64 GCC and `make` (cross-compiling from Linux works):

```
make                          # nightcap.exe
make installer VERSION=0.1.0  # Nightcap-setup.exe, needs NSIS
```

The icons in `logos/ico` are generated from the artwork in `logos` with `make icons`.

The delay presets (`DELAYS`) and the re-send interval (`REASSERT_MS`) are at the top of `main.c`.

## Licence

MIT
