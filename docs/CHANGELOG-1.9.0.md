# VitaOS 1.9.0 (draft release notes)

Not released yet. Tested on red (PCH-2000) on 2026-11-11: see "Tested on hardware" and "Before release" below.

## Music, films and previews play again

- **Songs, films, game previews and the boot video now play on every Vita.**
  The patched player module VitaOS needs (`modules/reAvPlayer.suprx`) was
  left out of every published package from 1.0.0 to 1.8.4, so playback only
  ever worked on the developer's own consoles. It is now part of the package,
  and the build refuses to publish without it. (#8, #15)
- VitaOS never rewrites your songs or films. A test on a real Vita showed
  the player takes hand-copied files as they are: index at the end, cover
  art stored as a picture track, chapter tracks (audiobooks). So the missing
  module was the whole problem, and no file "fixing" ships in 1.9.0.
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
  - There is never a moment without a `config.txt`: the old one is renamed
    aside, the new one moved in, and an interrupted change is finished or
    undone at the next start.
  - Your first original is kept as `config.txt.before-vitaos` and never
    overwritten.
  - VitaOS refuses to edit a config saved as UTF-16 or containing a stray
    zero byte, and tells you to add the line by hand.
- Plugin 1.5: PS keeps working alongside other controller plugins, and it
  writes a short log of what happened at power-on
  (`ux0:data/arcadehub/boot.log`), so "it didn't open at power-on" can be
  answered.
- The clock follows the Vita's own 12/24-hour setting. (#1)

## Your games, organised your way

- Apps > Triangle > **Show in Play** moves a homebrew game into Play;
  START on a Play game > **Move it to Apps** moves it back, as often as you
  like. (#9)
- Adrenaline bubbles count as games, and PSP and PS1 games start through
  their own bubble when RetroFlow's launcher isn't installed. Before, they
  only opened Adrenaline. (#9)
- **Nintendo DS** (DSVita) and **Dreamcast** (Flycast) games show up in Play
  when those emulators are installed. (#6)

## Fixes

- **Settings, favourites and recent games are saved on a fresh install.**
  Nothing created the folder VitaOS keeps them in until you happened to turn
  on certain Settings switches. Until then favourites, recents, the weather
  town and sound levels were lost at every restart, and box art was fetched
  again each time. (#7, #12, #1)
- All the new text is translated into Español, Português, Français, Deutsch,
  Italiano, 日本語 and Русский. Dialog messages now show their translation
  (before, only titles and buttons did), and Japanese text wraps properly.

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

- Installed over the older VitaOS on red (an Oct 1 build; backup kept),
  rolled back and reinstalled several times with the MCP install tool.
- Home, Play (118 Vita and 7 PSP games), Music (2,106 songs), Apps and
  Settings all open and draw correctly. Japanese song titles render.
- **Songs play as they are.** Four hand-made test songs played with nothing
  rewritten (each was byte-identical on the card afterwards): index at the
  end; cover art as a JPEG track; the same track disabled; a chapter track.
- **A film plays:** an H.264 960x544 `.mp4` with its index at the end played
  full screen, and as the dimmed preview behind the posters.
- **Play's screensaver reel** started after 30 s idle and played a game's
  preview video. This is the code that crashed in two of the #12 dumps.
- **Move it to Apps and Show in Play, both ways:** a game moved to Apps left
  Play (118 games) without being hidden and appeared in Apps; Show in Play
  brought it back (119).
- The new Settings rows in Japanese (`10-settings-japanese.png`). The
  translated dialogs were checked by measuring every language with the real
  fonts (all fit); they weren't opened on the console.
- Music opens in about 3 s with 2,106 songs.
- **Start at power-on works (tested with Mitchell's OK):**
  - Set up from Settings added exactly one line under `*main` in
    `ur0:tai/config.txt`. `config.txt.before-vitaos` was identical to the
    original, the plugin file matched the package, and no temp files were left.
  - After a restart, the plugin's own log shows it loaded at 3 s, launched
    VitaOS at 18 s, and VitaOS was up at 23 s
    (`17-boot-log-start-at-power-on.txt`).
  - "Update the plugin" from the same row replaced plugin 1.4 with 1.5.
  - Remove gave back a `config.txt` byte-identical to the original, twice.
  - On red only: Codex's `agent_autostart.suprx` then opens its own dev Home
    over VitaOS a few seconds later, so that is what stays on screen there.
  - On red only: after any restart it sleeps at the lock screen within about
    a minute unless someone swipes it. A control restart without the plugin
    did the same, so this is red's own boot path, not VitaOS.
  - Not tested: PS going to VitaOS's Home through the plugin (on red the dev
    bridge handles PS), and the return to VitaOS after a game.
- **Not verified:** the Home-music static fix. It needs someone to listen;
  a screenshot can't show it, and red's volume is at 0.
- Also not checked on hardware: the crash-report card (red has crash reports
  turned off), icons that used to be blank, and DS/Dreamcast games.

## Before release (for Mitchell)

1. On red, with the volume up: turn on Settings > Display & sound > Home
   music and listen on the Home tab for static or crackle.
2. Optional: try Start at power-on on a Vita without Codex's autostart, to
   see VitaOS stay on screen after power-on.
3. Push the branch and let CI build it. CI now checks that the player module
   and the plugin are inside the package.
4. Tag `v1.9.0` to publish.
