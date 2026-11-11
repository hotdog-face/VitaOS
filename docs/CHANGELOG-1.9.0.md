# VitaOS 1.9.0 (draft release notes)

Not released yet. Tested on red (PCH-2000) on 2026-11-11: see "Tested on hardware" and "Before release" below.

## Music, films and previews play again

- **Songs, films, game previews and the boot video now play on every Vita.**
  The patched player module VitaOS needs (`modules/reAvPlayer.suprx`) was
  left out of every published package from 1.0.0 to 1.8.4, so playback only
  ever worked on the developer's own consoles. It is now part of the package,
  and the build refuses to publish without it. (#8, #15)
- Songs and films copied over by hand often have their index at the end of
  the file, or cover art stored as a second track. The Vita's player refuses
  those, so VitaOS now fixes them once, without re-encoding, the first time
  you play them.
- Apple Lossless and other formats VitaOS can't play now get a message
  saying what to convert them to, instead of "That file would not play".
- Home music and UI sounds no longer crackle or turn to static. (#24)

## Fewer crashes

- Play's screensaver (the gameplay reel after 30 s idle) could crash when the
  library refreshed in the background. Two of the three crash dumps sent in
  (#12) were this.
- VitaOS could crash at startup if it asked for the Wi-Fi state while the
  network module was still loading (the third dump in #12).
- Threads that download over HTTPS (Store, news, weather, box art, updates)
  get far more stack. A stack that was too small could corrupt memory and
  crash VitaOS somewhere unrelated later, a likely source of the "end frame"
  reports. (#10, #11, #13, #14, #16, #17, #19, #20, #21, #22, #23, #25, #26)
- Crash reports now say which part of VitaOS crashed and where. Each report
  can then be traced to a line of code, and they also ask for the crash file.

## Feels more like the Vita's own home screen

- **Start at power-on in one press:** Settings > VitaOS > Start at power-on
  installs VitaOS's plugin by itself. It uses the config file taiHEN actually
  reads, keeps a backup and checks every step. Update or remove it from the
  same place. No more copying files and editing `config.txt` by hand.
- Plugin 1.4: PS keeps working alongside other controller plugins.
- The clock follows the Vita's own 12/24-hour setting. (#1)

## Your games, organised your way

- Apps > Triangle > **Show in Play** moves a homebrew game into Play;
  START on a Play game > **Move it to Apps** moves it back. (#9)
- Adrenaline bubbles count as games, and PSP and PS1 games start through
  their own bubble when RetroFlow's launcher isn't installed. Before, they
  only opened Adrenaline. (#9)
- **Nintendo DS** (DSVita) and **Dreamcast** (Flycast) games show up in Play
  when those emulators are installed. (#6)

## Fixes

- Buttons no longer stop working after a while; before, only touch kept
  working until VitaOS restarted. (#1, #19)
- App icons that VitaOS couldn't read (interlaced and paletted PNGs) now
  load. (#1, #4)
- Tiles for Japanese and other non-Latin names show their first character
  instead of "?". (#4)
- When there's nothing to show yet, Play and Home explain where games go and
  how to get started.

## Tested on hardware (red, PCH-2000, 2026-11-11)

Screenshots are in `docs/screenshots-1.9.0-red/`.

- Installed over 1.7-era VitaOS (backup kept), then rolled back and
  reinstalled with the new MCP install tool.
- Home, Play (118 Vita and 7 PSP games), Music (2,106 songs), Apps and
  Settings all open and draw correctly. Japanese song titles render.
- **A hand-made m4a with its index at the end and a cover-art video track:**
  VitaOS said "Getting this song ready to play…", rearranged the file (index
  first, one track; checked by pulling it back to the Mac) and played it.
  The next song (index at the end only) went the same way.
- Settings > VitaOS shows "Start at power-on: Set up", which is correct:
  red's `ur0:tai/config.txt` has no VitaOS line. The set-up flow itself
  was **not** run, because it edits red's tai config and needs Mitchell's OK.
- The crash-report card was not triggered (red has crash reports turned off
  and had no new VitaOS crash). The dump reader was checked against the
  three dumps from #12 on the Mac.
- Not yet checked on hardware: an `.mp4` film, the static fix for Home music
  (red's volume was 0), icons that used to be blank, and DS/Dreamcast games.

## Before release (for Mitchell)

1. Install this build on a test Vita (red) and check:
   - an `.mp4` film plays;
   - Play previews play;
   - Settings > VitaOS > Start at power-on, then restart: VitaOS opens;
   - Remove the plugin, then restart: the Vita starts normally.
2. Push the branch and let CI build it. CI now checks that the player module
   and the plugin are inside the package.
3. Tag `v1.9.0` to publish.
