# VitaOS 1.9.1

**Update from Settings > VitaOS > Update, or install `VitaOS.vpk` with VitaShell over any earlier version. Your settings stay.**

## The Store finds new homebrew by itself

- **New projects show up without anyone submitting them.** The Store's list
  was built from two community databases (VitaHomebrewDB and CBPS-DB), so a
  new port or app only appeared once someone added it there. The daily
  catalogue now also searches GitHub for Vita projects and adds the ones
  with a real, installable package. The first run added about 80, among
  them ports, a Saturn emulator and several new utilities.
- A project is only added if its release carries a VPK that reads as a Vita
  app and it has some sign of use (2 stars or 20 downloads). The Store's
  existing rules still apply: no piracy tools, no packages that contain a
  commercial game. Firmware flashers are kept out too.
- **The Store was silently dropping apps.** It read at most 1400 entries and
  the catalogue had grown to 1545, so the last 145 never showed. It reads
  2400 now.
- The catalogue can no longer empty the Store if one of its sources has a
  bad day: a list much shorter than yesterday's is not published.

## A faster start

- **The first screen comes up whole.** VitaOS remembers which pictures the
  first screen asked for and loads them first at the next start. On the test
  console the first screen's pictures were all in at 4.4 s after VitaOS
  started, where they used to keep filling in until 6.6 s.
- **Something on screen sooner after a game.** Coming back from a game,
  VitaOS draws its background as soon as graphics are up, about 3.5 s before
  the first full frame. A start at power-on keeps its splash or boot video.
- **Large libraries start quicker.** Applying the hidden-games list is about
  four times faster.
- `ux0:data/arcadehub/start.log` records how long each start-up step took,
  for bug reports about slow starts.

## Fixed

- **Hidden games came back after 1024.** With more than 1024 hidden games
  (easy with a big ROM set), the rest were shown again and hiding another
  did nothing. The limit is 4096.

## Tested on hardware (red, PCH-1000, firmware 3.65, 2026-11-12)

A local build of this code, installed over 1.9.0's files:

- VitaOS starts; the first screen's pictures were all in at 4.4 s.
- A hidden list of 1804 games is applied in full (0.2 s).
- The Store, after a refresh, shows 1627 apps with the new catalogue
  (`docs/store-1.9.1-red.png`).

Not tested: installing one of the newly found apps from the Store, and the
package CI builds for this tag (it is the same source).
