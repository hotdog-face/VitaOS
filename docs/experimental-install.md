# VitaOS as the system: experimental build

**Status: draft. Not released. Tested on one console only** (a PCH-1000 on
firmware 3.65 with Ensō). Nothing here is part of the normal VitaOS download
or its updater.

This build makes VitaOS behave like the Vita's own system:

- The Vita starts in VitaOS, with no lock screen.
- PS in a game opens a VitaOS menu over it (Resume, Volume, Brightness,
  Close game). Holding PS still opens the system's quick menu.
- When a game ends, VitaOS comes back.
- Optional: a VitaOS lock screen (a clock; it is not a passcode).

LiveArea keeps running underneath and is still seen for about ten seconds at
power-on, and for a few seconds between a game ending and VitaOS drawing.

## Before you install

- It has only run on firmware **3.65** with Ensō. You need taiHEN and a
  `tai/config.txt`. On any other firmware the lock-screen part switches
  itself off (it only patches a system file it recognises), so the system
  lock screen stays; the rest is untested there.
- It loads a **kernel module**. Kernel modules can hang or crash a console.
  This one is loaded about 30 seconds after power-on, not from
  `config.txt`, and it leaves itself out after a load that did not finish.
- It **removes the system lock screen**. If you use a passcode, the passcode
  is skipped too. Do not install this if you rely on one.
- Know how to hold **L** at power-on: that skips every plugin, this one
  included.

## Install

1. Install the experimental `VitaOS.vpk` over your VitaOS (VitaShell, or
   VitaOS's own updater will not offer it).
2. Open VitaOS > Settings > VitaOS > **VitaOS as the system** > Set up.
   It adds one line under `*main` in your `tai/config.txt` (a copy of the
   old file is kept as `config.txt.before-vitaos`), copies the kernel
   module to `ux0:data/arcadehub/`, and installs the menu app ("VitaOS
   Menu", title ID `VTOSPAUSE`).
3. Restart when it asks.

## Uninstall

1. Settings > VitaOS > VitaOS as the system > **Remove**, then restart.
   The line is taken out of `config.txt`; the Vita starts as it did before.
2. The menu app stays installed and does nothing by itself. Delete its
   bubble if you want it gone.
3. To go back to the normal VitaOS, install the normal `VitaOS.vpk`.

## If something goes wrong

| What you see | What to do |
|---|---|
| Black screen or no home screen at power-on | Hold Power 30 s to switch off. Power on holding **L** until the home screen shows. Open VitaShell and delete the `ur0:tai/vitaos_shell.suprx` line from `ur0:tai/config.txt` (or `ux0:tai/config.txt`), or copy `config.txt.before-vitaos` back over it. |
| PS in a game shows LiveArea first | The kernel module was left out. Settings > VitaOS > VitaOS as the system > "Turn the PS part back on", then restart. |
| PS in a game does nothing useful | Hold PS for the system's quick menu; close the game from LiveArea. Then Remove, and report it. |
| VitaOS does not open at power-on | Check Settings > VitaOS > Start at power-on is on. |

Switches, all files in `ux0:data/arcadehub/user/` (create an empty file to
switch off): `shell.off` (everything), `menu.off` (the PS menu),
`shell-idle.off` (coming back to VitaOS when nothing is running),
`lockscreen.keep` (keep the system lock screen). Delete `kernel.on` to stop
the kernel module loading.

## What is known not to work yet

- Notifications while a game runs.
- Games that use "enlarged memory" may close the menu app; untested.
- Only Sound Shapes has been used for the PS menu tests.
