# Drafted replies to open issues (not posted)

These are drafts for Mitchell to review. Nothing has been posted, closed or
labelled. They assume 1.9.0 ships as described in `CHANGELOG-1.9.0.md`.
Hold every "fixed in 1.9.0" reply until that release is out and verified on
a Vita.

---

## Crash reports: #10, #11, #13, #14, #16, #17, #19, #20, #21, #22, #23, #25, #26 ("end frame" / "tab body")

One reply for each. After 1.9.0 is out, close each one with it, or close them
all as duplicates of a single tracking issue.

> Thank you for sending this in. The crash reporter in 1.8.4 recorded what the
> *screen* was doing ("end frame" is where the screen waits for each frame), not
> the part of VitaOS that actually crashed, so these reports all looked the same.
>
> The crash files people attached in #12 showed two real bugs: the Play
> screensaver reading a game that a background library refresh had just
> replaced, and a startup race with the network module. Both are fixed in
> 1.9.0. That version also gives the threads that download things (Store,
> news, weather, box art) much more room. Too little room there can corrupt
> memory and crash VitaOS somewhere else later, which fits what you saw.
>
> Please update to 1.9.0 (Settings > VitaOS > Update). If it still crashes,
> the new report says which part crashed and where. Attaching the crash file
> from `ux0:data` (named in the report) helps the most.

For #21, which has a GPU crash file, add:

> Yours was a GPU crash while the Store was loading. If it happens again on
> 1.9.0, please attach the `psp2core-...GPUCRASH.psp2dmp` file.

For #19, which also mentions box art and dead buttons, add:

> The buttons going dead (only touch working) is fixed in 1.9.0. It happened
> when another controller plugin changed the timing data VitaOS reads. About
> box art never downloading: could you say which consoles and where the ROMs
> are? Then I can check that case.

## #12: three crashes on day one (core dumps attached)

> Thank you, the dumps made all the difference. I wrote a small reader for them
> (`tools/crashdump.py` in the repo) and mapped them to the 1.8.4 code:
>
> - **Dumps 2 and 3** (idle on Home, then the screensaver): the main thread
>   crashed in Play's screensaver reel. It kept an index into the game list,
>   and a background library refresh rebuilt that list underneath it, so it
>   read an empty slot. In 1.9.0 the reel checks its game every frame and
>   stops when the library reloads.
> - **Dump 1** (first launch): the thread that adds newly installed games asked
>   for the Wi-Fi state while the network module was still loading, and jumped
>   to address 0. In 1.9.0 networking is set up before that thread starts. So
>   this was a real bug, not a bad transfer, though it only shows up when the
>   timing is unlucky.
>
> The weather and store threads you saw in the dumps were bystanders. They
> now get much larger stacks anyway.
>
> Favourites not persisting and box art re-downloading: I couldn't reproduce
> those from the code alone. Could you say whether the favourites were games
> from the PS Vita, PSP or a retro console? And does box art download again
> every time even when the pictures are already showing?

## #15 and #8: m4a files won't play

> Found it, and I'm sorry: it wasn't your files. Every VitaOS package from
> 1.0.0 to 1.8.4 was published without the small player module VitaOS uses
> for music, films and previews. My own builds had it, which is why I never
> saw this. 1.9.0 includes it, and the build now refuses to publish without it.
>
> 1.9.0 also fixes the two usual problems with hand-copied m4a files (the
> index at the end of the file, and cover art stored as a picture track) the
> first time you play them, without re-encoding. Nothing is lost: chapters
> and text tracks (audiobooks) are kept, and if a cover-picture track has to
> be left out, the untouched original stays beside the song as `.orig`. AAC m4a at 44.1 kHz /
> 192 kbps like yours is exactly what it plays. Apple Lossless (ALAC) files
> need converting to AAC first; VitaOS now says so.

## #24: static music

> Thanks. The Home music (and the UI sounds) refilled a sound buffer while the
> Vita was still playing it, which comes out as crackle and static. 1.9.0 fixes
> that. If you still hear static after updating, please say whether it's the
> Home music or songs from the Music tab. Music-tab songs didn't play at all
> in 1.8.4 (see #15), so that would be a different problem.

## #4: Japanese game titles don't render

> Thanks for the screenshot. There were two problems in it. The empty boxes
> were missing Japanese characters: 1.8.0 added a Japanese font, and it covers
> most titles. The "?" on the icons was VitaOS taking the first *byte* of the
> name for the letter tile. 1.9.0 uses the whole first character, and it also
> loads icons that VitaOS couldn't read before (interlaced PNGs), so most of
> those tiles should now show the real icon.
>
> Some rarer kanji (for example 朧) and Korean or Chinese titles still aren't in
> the font. If you can, please post a screenshot from 1.9.0 with any that still
> look wrong.

## #9: app and homebrew organisation

> 1.9.0 lets you decide. In Apps, Triangle on an app > **Show in Play**. In
> Play, START on a game > **Move it to Apps**. Adrenaline bubbles now count as
> games by themselves.
>
> About PS1 games only opening Adrenaline: VitaOS can start a PSP or PS1 game
> directly through RetroFlow's launcher, and from 1.9.0 also through an
> Adrenaline bubble you made for that game. Without either, it can only open
> Adrenaline. Adrenaline 8 has no way to be told which game to start.

## #6: other emulators

> 1.9.0 adds **Nintendo DS (DSVita)** and **Dreamcast (Flycast)**. Put DS games
> in `ux0:data/dsvita` or `ux0:roms/nds` and they show up in Play.
>
> For GBA, VitaOS uses RetroArch's gpSP core. Standalone mGBA has no
> documented way for another app to hand it a game, so I can't launch it
> directly yet.
>
> About the Music and Movies tabs: hiding them in Settings > VitaOS hides the
> tabs and their tiles on Home (an older version didn't save settings, see
> #7). If something still shows after hiding them, what is it exactly?

## #5: VitaOS auto-update

> VitaOS checks for a new version once a day and tells you on Home. The
> update itself is one press: Settings > VitaOS > Update, or START anywhere in
> Settings. It downloads, installs and reopens by itself. I've kept the
> install step manual on purpose, so an update never interrupts you
> mid-game.

## #3: Emu4Vita / Singles cores

> Thanks! VitaOS reads RetroArch today. Emu4Vita++ takes a core name and a
> game from other apps, so it can be added. I want to check the core names on
> a real Vita first rather than guess. ROM folders under `ux0:roms/<console>`
> are found, but those games launch through RetroArch for now.

## #1: bug reports and suggestions (pellaaa93)

> Thank you, and thanks for the fork. In 1.9.0:
>
> 1. **Icons not recognised:** VitaOS reads retail icons from the system's own
>    copies (since 1.4.1). 1.9.0 also loads the icons it couldn't decode before.
> 2. **Buttons stop working, only touch works:** fixed. VitaOS ignored the
>    buttons after another controller plugin changed the timing data it reads.
> 3. **Weather not showing:** fixed with the settings-saving fix in #7.
> 4. **24-hour clock:** VitaOS now follows the Vita's own Time Format setting
>    (Settings > Date & Time).
>
> About merging: I'd like to take ideas from your fork (the weather details
> card, more subreddits) one at a time rather than merge it in one go,
> because the code has moved a lot since 1.4.3. Pull requests for single
> features are very welcome.

## #2: suggestions (MarcoGamerOficial)

> Thank you for the list. Status as of 1.9.0:
>
> - **Choose the emulator per game / standalone emulators:** DS (DSVita),
>   Dreamcast (Flycast), N64 (DaedalusX64) and ScummVM launch directly.
>   OpenBOR and Fake-08 are on the list; I need to confirm how they take a
>   game first.
> - **Ports inside a console (N64 ports under N64):** not yet. For now,
>   Apps > Triangle > Show in Play puts a port in Play.
> - **Hidden games:** START > Remove keeps them hidden across restarts. If
>   hidden games come back for you, please say when.
> - **Background music:** Settings > Display & sound > Home music.
> - **Layouts, themes in the Store, profiles, trophies:** noted. Those are
>   bigger projects for later.
>
> @Gjiroto: games that show under Apps can now be moved: Apps > Triangle >
> Show in Play.

## #18: feature request (search, update all)

> Both are in now: SELECT searches from anywhere (games and the Store), and
> the Store's updates tile on Home has **Update all**. Settings > VitaOS >
> Update apps automatically updates them while the Vita charges.
