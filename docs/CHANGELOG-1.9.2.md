# VitaOS 1.9.2

**Update from Settings > VitaOS > Update, or install `VitaOS.vpk` with VitaShell over any earlier version. Your settings stay.**

## Fixed

- **Opening Settings > Language could close VitaOS.** The menu offers nine
  entries (Automatic and eight languages) but its list had room for eight,
  so it read one past the end. On some consoles that showed a garbled last
  row; on others VitaOS closed. This was in every version since 1.8.0.
  Found from a crash report sent from a Vita (#27). Thank you.

Everything from 1.9.1 is included: the Store finds new homebrew on GitHub
by itself and no longer drops entries past 1400, VitaOS starts faster, and
more than 1024 hidden games stay hidden. See `CHANGELOG-1.9.1.md`.

## Tested on hardware (red, PCH-1000, firmware 3.65)

A local build of this code on 2026-11-12: Settings > Language opens and shows
all nine rows (`docs/language-menu-1.9.2-red.png`). The crash itself was not
reproduced on this console beforehand; the cause was found by mapping the
report's addresses to the 1.9.0 source.
