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
- Plugin 1.4: PS keeps working alongside other controller plugins.
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
- **Start at power-on, set-up and removal (with Mitchell's OK):**
  - Set up from Settings added exactly one line under `*main` in
    `ur0:tai/config.txt`. `config.txt.before-vitaos` was identical to the
    original, the plugin file matched the package, and no temp files were left.
  - Remove from the same row gave back a `config.txt` byte-identical to the
    original.
  - **The restart in between could not show VitaOS opening at power-on.**
    Red went dark (black screen, PS light on, off the network) until a
    button press, then came up in Codex's dev Home.
  - **A control restart without the plugin went dark the same way,** so the
    plugin is not the cause. After any restart red stops at the lock screen
    and its panel goes off within about a minute. Codex's bridge only clears
    the lock screen in agent mode, and Home waits behind the lock screen
    before it loads the bridge.
  - One hint that the plugin did act: with it installed, Codex's autostart
    needed three tries to get its own Home running; without it, one. That
    fits VitaOS already being open, but it isn't proof.
  - **The one test that would prove it:** set it up again on red, restart,
    and have someone swipe the lock screen away within about 20 seconds of
    the logo. The bridge log then names which Home opened first
    (`VITAOS001` or `MVZA00010`).
- **Not verified:** the Home-music static fix. It needs someone to listen;
  a screenshot can't show it, and red's volume is at 0.
- Also not checked on hardware: the crash-report card (red has crash reports
  turned off), icons that used to be blank, and DS/Dreamcast games.

## Before release (for Mitchell)

1. On red, with the volume up: turn on Settings > Display & sound > Home
   music and listen on the Home tab for static or crackle.
2. Start at power-on is **not proven at power-on**. Before release, either
   test it on a Vita without Codex's `agent_autostart.suprx`, or ship 1.9.0
   with the Settings row saying it is new and how to undo it (hold L).
3. Push the branch and let CI build it. CI now checks that the player module
   and the plugin are inside the package.
4. Tag `v1.9.0` to publish.
