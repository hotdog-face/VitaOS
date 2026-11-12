# VitaOS as the system: experimental plan

Branch `claude/vitaos-real-os`, started 2026-11-11 from 1.9.0 (`f7289fd`).
Status: plan only. Nothing here has been built or run on a console yet.

The goal is that VitaOS is the system a person uses: it is what appears at
power-on, what PS returns to from any game, and what owns the in-game menu,
notifications and the lock screen. LiveArea keeps running underneath (it is
`SceShell`, which the Vita can't run without) but is never seen.

## Where it stands (2026-11-12)

Working on red today, with Codex's dev bridge doing the shell-side part:

- **PS in a retail game opens a VitaOS menu over it**: Resume, Volume,
  Brightness, Close game (`experimental/menu-app`, title `VTOSPAUSE`, a
  16 MB system-mode app using vita2d). Resume goes back into the same game
  process. Screenshot: `docs/screenshots-real-os/01-...png`.
- A system-mode app runs beside a retail game with no hang (see "First run
  on red" below for what the earlier hang really was).

**Phase 2 passed on red (2026-11-12, about 21:30 PST), with VitaOS's own
plugin** `experimental/shell/vitaos_shell.suprx` 0.1 under `*main` in place
of Codex's `agent_autostart.suprx`. Its log (`ux0:data/arcadehub/boot.log`):

```
  4 s  vitaos_shell 0.1 loaded
  4 s  lock screen patches            (both applied; NID and code check passed)
 18 s  start-up app done
 18 s  launching VITAOS001
 23 s  VitaOS is up: VITAOS001
 90 s  menu over PCSA00003            (PS in Sound Shapes)
106 s  home.req, opening VITAOS001    (Close game in the menu)
165 s  a game ended, opening VITAOS001
```

- Power-on to VitaOS in 23 s, with the launch waiting for the start-up app
  (`NPXS10079`, done at 18 s) instead of a fixed delay.
- PS in a game opens the menu; Close game lands in VitaOS about 15 s later
  (VitaOS's own start-up, with its splash, is most of that); a game ended
  from outside also lands in VitaOS.
- **No lock screen at power-on: confirmed by Mitchell** on a second restart
  ("it goes straight to VitaOS"); the log was the same, VitaOS up at 23 s.
- Close game to VitaOS on screen, on a warm system (no splash): **9.4 s**.
  About 1 s of that is the menu's own exit delay and up to 1.5 s the
  plugin's first-launch wait; the rest is VitaOS starting.
- Not checked: wake from sleep.

**Later the same evening (plugin 0.2, menu update, lock screen):**

- Red restarts hands-free now: a remote restart came back with the bridge
  answering in 30 s (no lock screen in the way; VitaOS loads the bridge).
  Mitchell allowed remote restarts for this work.
- **PS toggles the menu.** The menu holds the PS lock, so PS no longer drops
  to LiveArea behind it; the plugin passes the press on (`menu.ps`) and the
  menu resumes the game (same pid before and after).
- **VitaOS comes back whenever nothing is running** (5 s on LiveArea with no
  game, no VitaOS, no system app): 9.8 s after VitaOS itself was closed.
- Close game to VitaOS on screen: 6.4 s (that run still included VitaOS's
  cold-start splash; the earlier warm run was 9.4 s before the trims).
- **VitaOS's own lock screen** (`src/main.c`, only with
  `user/lockscreen.on`): time, date, battery, any button opens it. Shown at
  power-on; seen on red (`docs/screenshots-real-os/03-...png`). The wake-from-
  sleep path is written (power callback) but not tested: nothing can wake red
  remotely. It is a cover, not security.
- Blind button presses sent too fast get dropped: one "Close game" test
  pressed Resume instead. Leave about a second between presses.

**Hiding LiveArea on PS: tried, not working (plugin 0.3, same night).**

- Idea: user-mode hooks on the shell's own controller reads take the PS bit
  out of a short press while a game is in front, so the shell never switches
  to LiveArea, and the plugin opens the menu straight over the game.
- What the hooks found: SceShell imports `sceCtrlPeekBufferPositive2`; ScePaf
  imports `sceCtrlPeekBufferPositive2` and `sceCtrlPeekBufferPositiveExt2`
  (hook bits `0x0000A002`). The other ten read functions are not imported.
- Result on red: with the hooks installed, **a PS press ended the game**
  instead of suspending it (page peeled away within 0.35 s), and the shell
  still went to LiveArea. This happened even with the hooks passing
  everything through unchanged. Without the hooks, the same plugin build
  works as 0.2 did. Why is not known.
- So PS does not reach the shell only through those reads (Codex's note in
  `vakern.c` says the same: the shell takes PS from the kernel's button
  emulation path). Hiding it properly is the kernel route in target 1
  (reVita's method), which means a `*KERNEL` line: phase 3b, with its own
  approval.
- Plugin 0.3c is on red: hooks off unless `user/ps-hide.on` exists.
- Found and fixed on the way: the "nothing running" return treated a game
  suspended behind LiveArea as nothing running and closed it after 5 s
  (0.2 had this; it showed when the menu did not open, and would have after
  holding PS for the quick menu). 0.3c counts the suspended game. Checked:
  hold PS, wait 10 s, the game is still there.
- `user/shell-debug` makes the plugin log every PS press with what was
  running.

**The kernel route works (plugin 0.4 + `vitaos_k.skprx` 0.1, same night,
with Mitchell's OK for a kernel plugin).**

- `experimental/kernel/vitaos_k.c`: taiHEN hooks on the kernel's
  `sceCtrlPeekBufferPositive2` and `...Ext2` exports, acting only on calls
  from the shell's process (vakern's and ds4vita's pattern). While
  vitaos_shell says a game is in front, the first half second of a PS press
  is taken out of what the shell reads; a longer hold is let through.
- **No `*KERNEL` line and no change to `config.txt`.** vitaos_shell loads the
  module itself 30 s after power-on (`taiLoadStartKernelModule` from inside
  SceShell works), from `ux0:data/arcadehub/vitaos_k.skprx`, only with
  `user/kernel.on`. It writes `boot.kloading` first and removes it after 30 s
  of running; if that file is still there at the next start, the module is
  not loaded again. So a bad module costs one restart, not a boot loop, and
  holding L is not needed to get out.
- The two halves talk through the hooked call itself: port `0x564B` is not a
  controller; from the shell's process it sets "armed" and returns the count
  of short presses hidden.
- On red: load at 28 s (`400309BD`, a module id). Short PS in Sound Shapes:
  the shell never saw it (the plugin's own debug line for a PS press did not
  fire), the menu opened, same game pid after Resume. PS in the menu
  resumes. Holding PS still opens Sony's quick menu (Wi-Fi, flight mode,
  Bluetooth, brightness, music), and the game survives it.
- What is still seen on a short PS: about a second of the shell's own
  app-launch animation into the menu (its wallpaper, then the menu app's
  LiveArea page zooming in: `docs/screenshots-real-os/04-...png`). The
  game's own page and the bubbles are no longer shown. Making the menu's
  page plain and dark would make that second look like a fade; it needs the
  menu app reinstalled through the promoter (LiveArea art is cached at
  install).

**Covering the gaps (plugin 0.5a, 2026-11-12):**

- **Launching earlier at power-on does not work, and cost a recovery.** A
  test switch made the plugin open VitaOS at second 8 instead of waiting for
  the start-up app (`NPXS10079`, which shows up at about second 18). Red
  stayed on a black screen and off the network; its log ends at
  "8 s launching VITAOS001". Mitchell had to restart it holding L and open
  VitaShell so the switch file could be deleted over FTP. The switch is
  removed from the code. So LiveArea's ten seconds or so at power-on cannot
  be shortened from the launch side; it would have to be covered (a boot
  animation that runs longer, or a full-screen layer drawn in SceShell).
  Lesson: a test of the boot path needs its own failed-boot guard before it
  runs, as the lock-screen patches and the kernel half have.
- **The plugin stands aside for the developer tools**: `deploy.lock`, or
  Agent Recovery (`MVZA00003`) running. Recovery is not in the shell's
  running list, so 0.4's "nothing running" and "a game ended" rules kept
  closing it, which is why installs failed with "connection refused". With
  0.5 the upload through Recovery worked first time. The boot launch and
  the lock-screen patch look only at `user/shell.off`, so a stale lock
  cannot keep VitaOS from opening.
- Wait before returning to VitaOS after a game ends: 0.75 s (was 1.5 s).
  A game ended from outside, warm system: VitaOS owns the screen 8.4 s
  later. Most of that is VitaOS starting; that is where the time is now.
- The menu app's LiveArea page is plain dark, so the PS transition is a
  dark page folding in (`docs/screenshots-real-os/05-...png`) with the
  system's "Please wait..." box for a moment. Reinstalling over an installed
  title failed in the promoter (`0x80870005`, `0x80101114`); uninstall, then
  install, works.

**VitaOS starts faster (2026-11-12/13).**

`src/main.c` now writes `ux0:data/arcadehub/start.log` on every start: one
line per start-up step, in milliseconds since the process began. Before
(warm start on red, 119 Vita games, about 1,000 hidden entries):

| Step | ms |
|---|---|
| process start to `main` | 310 |
| graphics (`vita2d_init`) | 910 |
| two UI fonts | 440 |
| Japanese font | 360 |
| crash check, sound | 270 |
| game catalog (reading the lists) | 950 |
| hidden games applied | 505 |
| shelves and lists | 200 |
| threads, tabs | 195 |
| first frame | 655 |
| **first thing on screen** | **4,900** |

Changed:

- **The background is drawn right after graphics start** on a warm start
  (`ui_init`), at 1,350 ms. So 3.5 s of the wait is now spent looking at
  VitaOS's own background, not LiveArea or black.
- The hidden-games pass compares a hash of each title first: 505 ms to
  130 ms.
- First real frame: 4,400 ms (was 4,900).

Measured end to end on red, a game ended from outside, warm:

| | before | now |
|---|---|---|
| VitaOS's background on screen | 8.4 s | **4.5 s** |
| full Home screen | 8.4 s | about 7.5 s |

Left to cut, in order of size: the catalog read (950 ms, file reads from the
card), graphics start (910 ms, mostly fixed), the fonts (800 ms; the
Japanese one could load after the first frame), and about 2.4 s before
`main` that belongs to the shell's own app launch.

Also (plugin 0.5b): a menu whose game ended while it was open is closed
after 3 s. One had been left open that way and kept VitaOS from coming back.

**Set up and removed from Settings (2026-11-13).**

- Build: `tools/build_experimental.sh` builds the three parts into
  `experimental/out/`; `cmake -DVITAOS_EXPERIMENTAL=ON` then compiles the
  Settings row and carries them in the package (`app0:assets/shell/`).
  **The option is off by default; a normal build has none of it** (checked:
  no `assets/shell/` in the normal package, and CI does not set the option).
- `src/sysint.c` knows both plugins now (`vitaos_ps`, `vitaos_shell`) and
  swaps one for the other in a single config write, with the same backup,
  atomic swap and read-back as before. `tools/test_sysint.sh` still passes.
- Settings > VitaOS > **VitaOS as the system**: Set up (plugin and config
  line, kernel half and its switch, the menu app through the system's
  installer), Update its files, Remove. When on, a **VitaOS lock screen**
  toggle sits under it and the old "Start at power-on" set-up row is hidden,
  so the two plugins can't both be listed.
- On red, from Settings only:
  - Remove took exactly one line out of `config.txt` and cleared the kernel
    and lock-screen switches.
  - Set up (with the menu app uninstalled first, so that path ran too) added
    the line under the first `*main`, copied the kernel half, and installed
    the menu app. No error message.
  - After a restart: plugin loaded at 3 s, VitaOS up at 21 s, kernel half
    loaded at 28 s, PS in Sound Shapes opened the menu, Resume gave the same
    game back.
- Not tested: "Update its files", Set up on a Vita with no dev bridge, and
  Set up on a config that still has `vitaos_ps` listed (red's has none).
- Seen on the way: with a developer background app running (`MVZA00005`),
  the "nothing running" return does not fire, so red stayed on LiveArea.
  That only concerns consoles with Codex's tools.

What the goal still lacks:

1. Wake from sleep, checked by eye (system lock screen stays away; VitaOS's
   shows).
2. LiveArea is still seen in two places: from about 8 s to 22 s after
   power-on, before VitaOS is up; and for a few seconds between a game
   ending and VitaOS drawing. (PS over a game now shows only the launch
   animation into the menu.) Its bubbles are also reachable from VitaOS's
   "system home", on purpose.
3. Notifications from VitaOS while a game runs (needs the background helper).
4. A second console: everything has only ever run on red. (The Settings row
   exists now; see above.)
5. The menu: Wi-Fi, screenshot.

Red is left in this state (plus `user/lockscreen.on`, VitaOS built from this
branch as `VITAOS001`, `vitaos_shell.suprx` 0.5b, and the kernel half with `user/kernel.on`; delete that file to stop it loading). Undo: restore `ur0:tai/config.txt` from
`config.txt.before-vitaos-shell` (Mac copy:
`backups/red-tai-before-vitaos-shell-20261111-2125`), rename
`user/kiosk.on.aside` and `user/pause.off.aside` back, delete
`ux0:data/arcadehub/return.off`, restart.

Next, in value order: make VitaOS reopen fast after a game (skip the splash
and boot video on a return, restore the tab and selection); more in the menu
(Wi-Fi, screenshot, battery, clock, "Home" that keeps the game); VitaOS's
own lock screen and notifications; then an installer and uninstaller in
Settings for people who want to try it.

What changed in the design because of what red showed:

- **VitaOS itself cannot stay resident beside a game.** A suspended game
  keeps the GPU memory (CDRAM) and the contiguous pool; an app beside it gets
  about 16 MB (ElevenMPV-A's real numbers, read from its executable on red:
  authority `0x2F00000000000001`, attribute 2, memory `0x4000` KB; `param.sfo`
  CATEGORY `gdc`, ATTRIBUTE `0x1089088`). VitaOS uses a 96 MB heap. So the
  split is: **a small menu app beside the game, and the full VitaOS reopened
  when the game ends.** "Fast switching" then means making VitaOS start
  fast, not keeping it alive.
- The menu app as a separate system-mode app works, so the kernel + PAF
  overlay (target 2, step 2) is no longer needed for a first version, and
  phase 3's kernel module can wait. The menu covers the whole screen; it is
  not drawn over the live game picture.
- A system-mode app cannot open Home itself: both launch flags stop on
  Home's LiveArea page with a Start button. The shell plugin does it
  (`home.req`).
- `sceAppMgrLaunchAppByName2ForShell` answers "Could not find the
  application" for the menu app; `sceAppMgrLaunchAppByUri(0x20000, ...)`
  from inside SceShell opens it directly.
- A first install of a new title ID works hands-free through Agent
  Recovery's installer (`i-ha/vita.py install`).

## Rules for this track

- **Red only.** Blue is never touched.
- **Nothing reaches mainline users.** No part of this goes into the 1.9.x
  updater or releases. If it ships, it is a separate, labeled experimental
  download with install and uninstall steps. Mitchell decides.
- **Recovery before risk.** Before every kernel or shell change on red: back
  up `ur0:tai`, write down the exact undo, and confirm the recovery path.
- **Every experimental module goes in `ur0:tai/config.txt`, never in
  `ur0:tai/boot_config.txt`.** Holding L at power-on skips `config.txt`; it
  does not skip `boot_config.txt`.
- **Mitchell's approval in the work session** for each restart of red, and
  for anything that writes `os0:`, `vs0:`, `sa0:`, the boot partitions or
  Ensō, or that could block hold-L recovery. This plan needs none of those
  writes in phases 0 to 5.

Marks used below: **[red]** proven on red, **[codex]** from Codex's notes on
red, **[web]** from published plugins or docs and not yet checked on red,
**[unverified]** a guess that a phase has to prove.

## Red today

| | |
|---|---|
| Model | PCH-1000 (OLED). No `imc0:`. 1.9.0's notes call it PCH-2000; that is wrong. |
| Firmware | 3.65, plain Ensō (no enso_ex; `os0:` dated 2017-03-17) |
| SceShell | module NID `0x5549BF1F` (3.65 retail) |
| Storage | SD2Vita 400 GB as `ux0:` through YAMT |
| `boot_config.txt` | custom, in `ur0:tai/`; loads `vbanim.skprx` and `yamt.skprx` |
| taiHEN config | `ur0:tai/config.txt` (no `ux0:tai/config.txt`); 1832 bytes, SHA-256 starts `5e98b1f39e53e2a0` |
| `*KERNEL` | itls, yamt_helper, 0syscall6, nonpdrm, repatch, kubridge, fd_fix, CapUnlocker, azaharnative, ioplus |
| `*main` | henkaku.suprx, shellbat, pngshot, agent_autostart.suprx |
| `*NPXS10015` | vbanim.suprx, yamt.suprx |
| `*ALL` | VitaGrafix |
| Dev stack (Codex) | `agent_autostart.suprx` opens the dev Home `MVZA00010` about 20 s after boot; Home loads `ux0:data/vita-agent/vakern.skprx` at runtime; `vabridge.suprx` 8.6d2 is injected into SceShell (TCP 1348) |
| VitaOS | 1.9.0 as `VITAOS001`. Leftovers in `ur0:tai`: `vitaos_ps.suprx` 1.5 (not referenced) and `config.txt.before-vitaos` |
| After a restart | stops at the lock screen and sleeps within about a minute unless someone swipes **[red]** |
| Backup | `backups/red-tai-before-start-at-power-on-20261111-1629/ur0-tai/` (34 files, the original config) |

### Recovery ladder for red

1. **Bad user plugin (`*main`, `*ALL`, a title ID):** hold L at power-on.
   taiHEN skips `config.txt`: `*KERNEL` when taiHEN starts, and `*main` only
   if L is still held when SceShell starts, so keep holding until the lock
   screen. Then fix the config over Agent Recovery (FTP 1345) or VitaShell.
   **[web]**; Codex used hold L on red to get out of the SceShell freeze
   **[codex]**.
2. **Bad kernel plugin in `config.txt`:** the same. Hold L.
3. **With L held, red keeps its SD2Vita `ux0:`** (YAMT loads from
   `boot_config.txt`, which L does not skip), so the tools on it stay
   reachable. **[red, 2026-11-12]** Retail games won't start in that mode
   (`nonpdrm` is skipped). Whether VitaShell opens in that mode was not
   tried.
4. **Bad module in `boot_config.txt`:** only Safe Mode helps (R + PS + Power).
   Safe Mode skips Ensō's patches. "Restart" is the only safe choice there;
   "Restore the PS Vita System" resets settings and leaves Ensō inactive
   (re-run the Ensō installer, Square); **never choose "Update"**. This plan
   never edits `boot_config.txt`. **[web]**
5. **Black screen with the PS light blue** (seen after Codex's Linux
   handoff): hold Power about 30 s, then Safe Mode > Restart. **[codex]**

`config.txt` edits follow Codex's rule: copy to
`config.txt.before-<change>` first, write, read back, compare.

One tooling limit: the bridge and the MCP refuse writes under `tai/`. So tai
changes on red go through VitaOS's own installer (`src/sysint.c`, as Start at
power-on does) or Agent Recovery's FTP.

## What already exists

Built by Codex on red (in `i-ha/`, mostly `kiosk-wip/`):

| Piece | What it does | State |
|---|---|---|
| Kiosk ("Hide the bubbles") | hides LiveArea's bubbles behind VitaOS | basic version works |
| Return to Home | reopens Home when a game closes | works with the dev bridge |
| PS lock | `sceShellUtilLock(0x800)` (`PS_BTN_2`) so PS doesn't leave VitaOS | works |
| `vaoverlay.skprx` | in-game quick menu drawn from the kernel: hooks `ksceDisplaySetFrameBufInternal` (lib `0x9FED47AC`, NID `0x16466675`), input through `ksceCtrlSetButtonIntercept` | worked over RetroArch; 4 console crashes; parked |
| `VTOSPAUSE` | system-mode pause app (`-a 0x2808000000000000`, CATEGORY `gdc`, ATTRIBUTE `0x1088088`) | failed; parked 09-28 |
| LowMemMode hook | SceShell offset `0x272526` (3.65) | written |
| One-bubble `app.db` | LiveArea with a single bubble | black-screened the shell; recovered by an L-held boot |
| AppMgr shell-event hooks, headless top menu, shellspawn probe | | never implemented |

In VitaOS 1.9.0: the user plugin `vitaos_ps.suprx` 1.5 (opens VitaOS at
power-on after a fixed delay, PS goes to VitaOS's Home, boot log), and the
tai installer in Settings with backup, atomic swap and recovery.

**The open blocker from Codex's work.** A system-mode app running beside a
game froze SceShell on red, even with ElevenMPV-A, which is known to work on
other consoles. Codex's Oct 2 bisect: the cause is its own bridge/vakern;
with the bridge off, coexistence works; hold L recovers. It was never
narrowed further. Everything in targets 2 and 4 depends on a second app
living beside a game, so this is the first thing to pin down.

**What a read of the bridge source found (2026-11-12, code only, nothing run
on red).** Files: `i-ha/kiosk-wip/vabridge.c` (8.6d2) and
`i-ha/agent-service/kernel/vakern.c`. No line provably deadlocks. The notes
disagree with each other (09-28 says it froze "even alone/with L held";
10-02 says hold L works), the freeze is intermittent, and nobody looked for
a SceShell core dump, so SceShell may have crashed rather than hung.
Suspects, most likely first:

1. **Return-to-Home launches Home over the second app.** The watcher's
   `front_app` (`vabridge.c:1249`) only looks at display plane 0; a system
   app draws on the shell plane, so the watcher can't see it and, after its
   10 s hold, calls `return_home()` (1295), which can launch twice and can
   race a `launch` on the server thread. Switch that already exists:
   `ux0:data/arcadehub/return.off`.
2. **The bridge's `launch` command** (201-226) closes every non-system app
   and may launch a second time. The "bridge on" rows of the bisect probably
   used `launch`, the "bridge off" rows the bubble, so the bisect may have
   compared launch methods. Test: bridge on, open the app from its bubble or
   with `launchkeep`.
3. **The LowMemMode hook** (1521-1543) returns 0 for every launch budget
   check. Switch: `user/lowmem.off` (read at load).
4. **The watcher's polling as a whole** (15 to 25 AppMgr calls per 100 ms).
   Switches: the seven `diag-skip-*` files.
5. **vakern alone** (16 ctrl/touch hooks, an 8 ms tick thread). No switch;
   test by moving `vabridge.suprx` aside so only vakern loads.

Also a logic bug for later: if a mini app ever owns plane 0, the watcher
re-arms on it and forgets the game, and its exit can close the suspended
game. VitaOS's own shell plugin must track both apps.

So phase 1 on red starts with these switches, one at a time, and a check
for a SceShell `psp2core` after any freeze. All are files under
`ux0:data`, not tai changes.

**First run on red (2026-11-12, about 20:00 UTC, with Mitchell's OK).**
State: bridge 8.6d2 and vakern loaded; `kiosk.on`, `lowmem.off` and
`pause.off` present in `user/`; the kiosk had switched itself off for this
boot ("4 relaunches in 10 min") and was standing aside; Home in front was
`VITAOS001` 1.9.0. ElevenMPV-A (`GRVA00008`) was the second app.

| Run | Beside | How Eleven was opened | Result |
|---|---|---|---|
| 1 | VitaOS Home | `launchkeep`, then tap Start on its page | ran; Home stayed suspended on its own page; bridge answered for 30 s |
| 2 | VitaOS Home | back to Home, 12 s wait, `launchkeep` again | fine |
| 3 | VitaOS Home | the closing `launch` command | fine for 60 s |
| 4 | Persona 4 Golden (`PCSE00120`), started from its page | `launchkeep`, then tap Start | **froze**: the bridge stopped answering within 6 s; ping still answers |

What this changes:

- Bridge plus vakern do not freeze SceShell by themselves: a system app ran
  beside a game-budget app (VitaOS) three times, including with `launch`.
  That weakens suspects 2, 4 and 5.
- The freeze came with a retail game. So the next question is memory, not
  the watcher: with `lowmem.off` set, the bridge's LowMemMode hook is off,
  and SceShell's own budget check then runs for a system app starting beside
  a game that uses much more memory than VitaOS does. Unverified.
- `launchkeep` opens the app's LiveArea page with a Start gate, not the app.
  The shell plugin will need the gate patched or tapped.
- `launch PCSE00120` timed out and left the game on its Start gate too.

After run 4 (red back up about 21:20 UTC):

- It was a whole-console hang, not only SceShell: the picture stayed, the
  PS light blinked blue, and a normal Power hold did nothing. Mitchell got
  it back with the long forced power-off. **So the recovery for this freeze
  is a 30 s Power hold, not 10 s.** Ping still answered while it hung.
- No new crash dump (still 32, newest unchanged). Nothing was logged at the
  moment of the hang; the last lines before it are from the `launch` of the
  game: "closed last game", "closing running app", "lowmem: applying 0 0".
- After the restart the kiosk is on again and Home in front is `MVZA00010`.

**Later the same day: the freeze was a crashing game, not the bridge.**

- By hand, Persona 4 Golden and two other titles (one was the Quake port)
  crashed on red on their own, with the bridge's two files moved aside and
  with them back. Persona 4 has no play time recorded on red at all. Quake's
  dump: data abort in `SceGxm` on its main thread. These titles are broken
  on red by themselves; why is a separate follow-up.
- Holding L keeps the SD2Vita card (the bubbles were there) but retail games
  refuse to start, because `nonpdrm` is skipped. **[red]**
- Control: Sound Shapes (`PCSA00003`, last played Oct 2) starts and gets
  past its title screen.
- **Run 5: ElevenMPV-A beside Sound Shapes, bridge and vakern loaded, kiosk
  on: works.** Eleven ran for 25 s, the bridge kept answering, PS showed
  both, and Continue went back into the same game process
  (pid `0x00222767` before and after). No hang.

So run 4's hang was Eleven being started while Persona 4 was crashing in
the background (the system writes a crash dump at that point). Rule for the
shell plugin: never launch anything while an app is ending or a dump is
being written; the bridge already logs "kiosk: crash dump, waiting".
Codex's "the bridge freezes SceShell beside a second app" is not reproduced:
five runs, the only hang explained by a crashing game. The blocker for
targets 2 and 4 is lifted as far as red shows today.

Also seen: the bridge's `launch` fails with `0x80AB0010` if sent while the
kiosk is reopening Home; and crash dumps on red stay `.tmp` (cut short).
The bridge refuses to rename its own two files; VitaShell FTP (port 1337)
does it.

Codex's lessons that this plan keeps:

- No TCP server in the kernel.
- Never draw from your own thread on a still screen; draw only inside the
  display hook.
- Never `ksceKernelCopyToUserProc` into another process's framebuffer.
- No file I/O on the input thread.
- Wait 10 s before launching a second app; a double launch around a reboot
  crashes.
- Custom boot plugins caused boot loops before. That is why nothing new goes
  in `boot_config.txt`.

## Architecture

Four parts. Each can be removed on its own.

```
ur0:tai/config.txt
  *KERNEL  vitaos_k.skprx     PS button, display hook, suspend/resume   (phase 3+)
  *main    vitaos_shell.suprx boot launch, lock screen, PAF overlay page (phase 2+)

ux0:app/VTOSX0001             VitaOS built to live beside a game         (phase 1+)
  eboot.bin   the home screen (mini-app budget)
  eboot2.bin  background helper: notifications, downloads                 (phase 4)
```

The experimental build uses its own title ID (`VTOSX0001`) and its own
plugin file names, so it never collides with a released VitaOS on the same
card, and the 1.9.x updater never sees it.

### Target 1: VitaOS is the shell

**Never see LiveArea at boot.**

- `vitaos_shell.suprx` runs inside SceShell (`*main`). It waits until the
  system's start-up app `NPXS10079` has finished, then launches VitaOS. This
  is AutoBoot's method **[web]**, and replaces plugin 1.5's fixed 15 s wait,
  which is the main reason LiveArea shows for a while today.
- Lock screen: NoLockScreen's two patches, `taiInjectData` of `0xBF002001`
  at SceShell segment 0 offsets `0x23556E` and `0x23626E` (3.65 to 3.73
  retail) **[web]**. Applied only when SceShell's NID is `0x5549BF1F`;
  otherwise the plugin does nothing and logs it. This also fixes red sleeping
  at the lock screen after every restart.
  - Risk: these patches skip the passcode too. The plugin checks whether a
    passcode is set and leaves the lock screen alone if so, until VitaOS has
    its own (target 3).
- What still shows: the Ensō/boot logo and the first SceShell frames before
  VitaOS's first frame. To cover them, VitaOS's kernel module can hold the
  last boot-logo frame until VitaOS draws (phase 3, in the display hook)
  **[unverified]**.

**PS returns to VitaOS from anywhere.**

- Inside VitaOS: `sceShellUtilLock(PS_BTN_2)` **[red, codex]**.
- Inside a game: SceShell handles PS itself, so a user plugin can't see it.
  `vitaos_k.skprx` sees PS for every process through the SceCtrl export
  hooks (lib `0xD197E3C7`) with reVita's method of telling the shell's reads
  from the game's (`ksceKernelGetProcessId` import, `0xE2C40624` /
  `0x9DCB4B7A`) **[web; the NIDs are unverified on 3.65]**. A short press
  opens VitaOS's overlay (target 2); "Home" in it brings VitaOS forward.
- `vshPowerSetPsButtonPushTime` (used by Quick Menu Plus) can make the long
  press instant or disable Sony's quick menu **[web]**.

**Closing a game lands in VitaOS.**

- First version: the plugin polls the foreground app and relaunches VitaOS
  when a game exits (what `vitaos_ps` and Codex's bridge do today).
- Better: hook the shell-event imports (`_sceAppMgrReceiveShellEvent`
  `0xE0DADF51`, SceAppMgrUser `0x8AF17416`) so the plugin is told when an app
  exits instead of polling **[codex listed these; never implemented]**.
- With coexistence (below), VitaOS is still running when the game exits, so
  "return" is only bringing it forward. No relaunch, no load time.

**Hiding LiveArea itself.** Codex's kiosk is kept as the last step (phase 6):
it only matters for the moments LiveArea could still flash. The
`livearea-no-confirmation` offsets (`0x27895E`, `0x2725E8`) remove the "close
this app?" page-peel prompt **[web]**. The one-bubble `app.db` idea is
dropped: it black-screened the shell.

**Keeping VitaOS resident.** Today VitaOS is built with the game budget
(`-a 0x2200000000000101`), so starting a game closes it. The experimental
build is a mini app: `vita-make-fself -m <KB>` (allowed range `0x1000` to
`0x12800`), CATEGORY `gdc`, ATTRIBUTE as ElevenMPV-A (`0x01089088`) **[web]**.
Costs and limits:

- Memory: at most about 74 MB (`0x12800` KB), and less is safer. VitaOS's
  textures (box art, video preview) have to be dropped when a game starts
  and reloaded when VitaOS comes forward.
- Games that use enlarged memory kill mini apps. VitaOS has to notice it was
  killed and fall back to relaunch-on-exit.
- Codex's `VTOSPAUSE` failed with a different attribute set
  (`-a 0x2808000000000000`, `0x1088088`); the plan starts from ElevenMPV-A's
  exact values, which are known to work elsewhere.

### Target 2: the in-game menu

Three steps, cheapest first. Each one is useful if the next fails.

1. **QuickMenuReborn client** (user-mode). A small `suprx` in
   `ur0:QuickMenuReborn/` adds VitaOS rows to Sony's own quick menu (long
   press PS): Home, Quit game, volume, brightness, Wi-Fi, screenshot. Uses
   `QuickMenuRebornRegisterWidget` and friends (MIT) **[web]**. Needs the
   QuickMenuReborn plugin under `*main`. No kernel code.
2. **Kernel + PAF overlay** (the vita-jp-overlay / PSVshellPlus pattern
   **[web]**). `vitaos_k.skprx` takes PS and, while the menu is open, the
   game's input (SceCtrl and touch export hooks); `vitaos_shell.suprx` draws
   the menu as a PAF page inside SceShell at `overwrite_draw_priority = 6`,
   which SceShell composites over the game. The game is paused with
   `ksceKernelSuspendProcess(pid, 0x1C)` while the menu is open (3.63+ NIDs
   `0x93B8E785` / `0x6894499C`). Drawing happens in SceShell's own render
   loop, which avoids every crash class Codex hit with kernel blitting.
3. **Kernel blit fallback** (Codex's `vaoverlay`): kept for reference only.
   It drew into the game's framebuffer from the display hook and crashed the
   console four times.

Menu contents: suspend / quit, volume, brightness, Wi-Fi, battery, clock,
screenshot. All of these are user-mode calls available inside SceShell
(`sceAVConfig*`, `sceRegMgr*`, `scePower*`, `sceAppMgrDestroyAppByName`).

### Target 3: notifications, lock screen, status bar

- **Notifications.** `sceNotificationUtilSendNotification` from a
  background app (`eboot2.bin`, `param2.sfo`, started with
  `sceBgAppUtilStartBgApp`: the BGFTP pattern **[web]**). Sony's own
  notification pop-up then carries VitaOS's text: download finished, update
  ready, battery low. Later, VitaOS's overlay page can draw its own.
- **Status bar.** Over a game: part of the PAF overlay page. In SceShell's
  own screens: shellbat's hook points (`0x183F6C`, `0x40E4FC`, and
  `scePafWidgetSetFontSize` at `0x45D2C8`) **[web]**. Red already runs
  shellbat, so those offsets are in use on this exact firmware.
- **Lock screen.** After the NoLockScreen patches, VitaOS draws its own lock
  screen as its first screen at boot and on wake (clock, notifications,
  swipe or button to open). A passcode in VitaOS is a later, separate
  decision: it would only be as strong as the app, so it must be described
  as a convenience lock and not as security.

### Target 4: app lifecycle

- **Suspend and resume a game from VitaOS.** With VitaOS as a mini app, the
  game stays in memory when VitaOS comes forward; SceShell suspends it as it
  does for the quick menu. Resume is `sceAppMgrLaunchAppByUri` with the
  game's `psgm:play` URI, which brings a running game forward instead of
  restarting it **[unverified]**.
- **Fast switching.** VitaOS shows the suspended game as a "Now playing"
  tile with its last frame (taken in the display hook when PS is pressed).
- **Quit from VitaOS.** `sceAppMgrDestroyAppByName`; the kernel module's
  `ksceAppMgrKillProcess` only as a fallback for a hung game.
- Limit, set by Sony's design: one game-budget app at a time. Two games
  can't be kept alive. Switching is between one game, VitaOS and mini apps.

### Target 5: a literal own OS (research verdict)

Not a path to a home screen.

- Linux on the Vita has no audio, no GPU acceleration and no access to
  Sony's memory card **[web]**. None of the Vita's games or apps run under
  it. A home screen there would be a home screen for nothing.
- Baremetal has the same problem, plus writing every driver.
- On red: Linux never booted. Codex's handoff attempts ended in a black
  screen with a blue PS light, recovered by a 30 s power hold and Safe Mode >
  Restart **[codex]**. Returning from Linux is known to boot-loop on consoles
  with YAMT **[web]**, and red uses YAMT.
- What is realistic: a "Boot Linux" tile in VitaOS for people who already
  set Linux up. Not planned; listed for completeness.

## Phases

Each phase ends with something seen on red. A phase only starts when the one
before it passed.

### Phase 0: desk work (no change to red)

- Read taiHEN's and Ensō's source to confirm the recovery ladder above:
  what L skips, when, and that `boot_config.txt` modules still load.
- Read Codex's `vabridge.c` / `vakern` to find what could freeze SceShell
  when a second app runs beside a game. Suspects, from the lessons list: the
  kiosk's foreground-app watcher relaunching Home as soon as another app is
  in front, the PS lock, and the SceShell-side TCP server thread.
- Check the unverified NIDs and offsets against a 3.65 SceShell / kernel
  dump read from red (read-only) and against the vitasdk NID database.
- Build `VTOSX0001` (mini-app budget) and the three modules. Unit-test the
  config installer for the new lines with `tools/test_sysint.sh`.

Proves: the recovery story, and that the build is possible. Risk: none.

### Phase 1: VitaOS lives beside a game (an app install, no tai change)

- Install `VTOSX0001` on red (a new title ID needs the system installer
  once, through Agent Recovery).
- Test A, bridge loaded: start a game, bring VitaOS forward. Expect Codex's
  freeze; this time with logging, to see where.
- Test B, bridge not loaded (red started with L held, so `agent_autostart`
  doesn't run; **needs a restart and Mitchell's hands**, since nothing
  remote works on red without the bridge).
- Measure memory at each budget and choose the smallest that keeps Home,
  Play and Settings usable.

Proves: coexistence on red, the real blocker of targets 2 and 4, and what
the bridge does to break it. Risk: a frozen SceShell (hold Power 10 s,
restart). Undo: delete the app.

### Phase 2: VitaOS at boot, no lock screen (first tai change, user-mode)

- Adds one line under `*main` in `ur0:tai/config.txt`:
  `ur0:tai/vitaos_shell.suprx`. **This is the first change in the plan that
  touches red's tai config.** It is user-mode, inside SceShell.
- Wait-for-`NPXS10079` launch, NoLockScreen patches behind the NID check,
  return-to-VitaOS on game exit, boot log as in plugin 1.5.
- On red, `agent_autostart.suprx` is commented out for this test (it opens
  the dev Home over VitaOS), and put back afterwards.

Proves: power-on to VitaOS with no swipe and no bubbles in between; red no
longer goes dark after a restart. Risk: SceShell crashes at start (black
screen or restart loop). Recovery: hold L until the lock screen, restore
`config.txt.before-vitaos-shell`. Needs: Mitchell's OK for the config change
and for each restart.

### Phase 3: PS and the menu (first kernel change)

- 3a: QuickMenuReborn client (user-mode; one more `*main` line).
- 3b: `ur0:tai/vitaos_k.skprx` under `*KERNEL` in `config.txt`: PS seen
  everywhere, input taken while the menu is open, game suspended; PAF
  overlay page in `vitaos_shell.suprx`.

Proves: PS in a game opens VitaOS's menu over it; Home in the menu lands in
VitaOS with the game still alive. Risk: a kernel module can stop the console
from starting or crash it. Recovery: hold L (skips `*KERNEL` in
`config.txt`), remove the line. The module loads from `config.txt` only, so
Safe Mode is not needed. Needs: Mitchell's OK, with this risk stated again
at the time.

### Phase 4: notifications, status bar, lock screen

Background helper (`eboot2.bin`), notifications, the status bar in the
overlay, VitaOS's lock screen. No new hook points beyond phase 3.

Proves: a finished download shows as a notification inside a game.

### Phase 5: lifecycle

"Now playing" tile, resume without restart, quit from VitaOS, falling back
cleanly when an enlarged-memory game kills VitaOS.

Proves: game > PS > VitaOS > Music > back to the same game, still running.

### Phase 6: hide what's left, and package

Codex's kiosk for any remaining LiveArea flashes, the boot-logo hold, and
the page-peel prompt patches. An installer and an uninstaller in VitaOS's
Settings that add and remove all experimental lines in one step, with the
same backup rules as Start at power-on. A separate download, if Mitchell
wants one.

## Risks, in one table

| Change | Worst case | Recovery | Needs approval |
|---|---|---|---|
| New app `VTOSX0001` | whole-console hang while testing (seen 2026-11-12) | hold Power 30 s; if it shows Safe Mode, "Restart" | install: no; restarts: yes |
| `*main` line (`vitaos_shell.suprx`) | SceShell crash at boot | hold L, restore config | yes |
| NoLockScreen patches | passcode skipped | plugin checks for a passcode; remove line | part of the above |
| `*KERNEL` line (`vitaos_k.skprx`) | no boot, or crash | hold L, restore config | yes, risk restated |
| Process suspend from the kernel | hung game or console | hold Power 10 s | part of the above |
| `boot_config.txt`, `os0:`, `vs0:`, `sa0:`, Ensō | brick-class | Safe Mode at best | **not in this plan** |

## Open questions

- Does holding L on red still give the SD2Vita `ux0:`? (Phase 0, then one
  check on red.)
- What in the bridge freezes SceShell beside a second app? (Phases 0 and 1.)
- Are the reVita and suspend/resume NIDs right on 3.65? (Phase 0.)
- How much of VitaOS fits in a mini-app budget without dropping features?
  (Phase 1.)
- Does `psgm:play` resume a suspended game or restart it? (Phase 5.)

## Android on the Vita (VitaProjectDroid), looked at 2026-11-12

Mitchell asked whether https://github.com/Yeetserama/VitaProjectDroid helps.
It does not serve this goal; it is the same road as target 5.

- It boots LinuxOnVita's kernel bare-metal and runs an Android 5.1.1
  userspace on it. By its own README it is "pre-boot": the boot package
  builds, nothing has been booted on a console yet.
- By its own account there is no GPU driver (software rendering only), no
  audio, no battery reading, no suspend, no Bluetooth, no camera.
- Android replaces Sony's system while it runs, so no Vita game, PSP/PS1
  game through Adrenaline, or Vita homebrew runs there. VitaOS's whole
  library would be gone.
- On red specifically: it targets a console with no SD2Vita and reads its
  files from the Sony card. Red uses SD2Vita through YAMT, and the same
  bare-metal handoff left red on a black screen with a blue light when
  Codex tried Linux.
- The most it could ever be here is a "Boot Android" tile inside VitaOS,
  once that project boots at all. Worth a look again when it does.
