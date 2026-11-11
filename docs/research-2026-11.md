# VitaOS research notes, November 2026

Compiled on 2026-11-11 from web searches, fetched pages, GitHub API metadata and emulator source code. Dates come from GitHub release or push timestamps or from page infoboxes, as noted for each item. Anything not checked is marked **UNVERIFIED**.

Context: the VitaOS source on this branch already does several of these things.
- It launches RetroArch (`RETROVITA` with `&param=&param2=`), DaedalusX64, ScummVM, and PSP games through RetroFlow's `RETROLNCR` boot.bin, with Adrenaline as the fallback.
- The Store reads VitaHomebrewDB `apps.json`, and box art comes from libretro thumbnails.
- The `vitaos_ps.suprx` plugin autostarts VitaOS with a 15 s delay, up to 12 retries and a `BOOT_OFF` kill switch.

The findings below are measured against that.

---

## 1. Starting at boot and replacing the shell

### What exists
| Item | Status / date | Link |
|---|---|---|
| **HENlo** (browser exploit, `jailbreak.psp2.dev`). Credited to TheFloW, with the henlo_jb implementation by SKGleba and Yoti. | Covers FW 3.65–3.74; GameBrew notes 3.67 support was added. **It is gone after a reboot.** The henlo_jb repo was last pushed 2024-02-18 and calls itself beta. | https://vita.hacks.guide/using-henlo · https://github.com/SKGleba/henlo_jb · https://gamebrew.org/wiki/HENlo_JB_Vita |
| **h-encore / h-encore²** (older route) | h-encore covers 3.65–3.68; h-encore² covers 3.69–3.74. | https://vita.hacks.guide/installing-h-encore · https://github.com/TheOfficialFloW/h-encore |
| **VitaDeploy** (SKGleba). Downgrades to 3.60/3.65/3.68 and installs Ensō. | Repo pushed 2025-10-25 | https://github.com/SKGleba/VitaDeploy |
| **Ensō** (permanent CFW, 3.60/3.65 only) | Repo unchanged since 2018-08-26 | https://github.com/TheOfficialFloW/enso · https://vita.hacks.guide/installing-enso |
| **Current recommended route** (guide "last updated October 2026"): latest firmware, then HENlo, then VitaDeploy "Quick 3.65 Install", then Ensō. The guide warns against Enso EX unless you understand it. | Oct 2026 | https://retrohandheldhq.com/posts/ps-vita-homebrew-guide/ |
| **New 2025–26 exploit or CFW for 3.74+** | **None found.** Searches turned up nothing newer than HENlo. | — |
| **AutoBoot** (Rinnegatamante). A user plugin under `*main`. It polls `sceAppMgrGetRunningAppIdListForShell` until **NPXS10079** (started with LiveArea) is running, reads the title ID from `ux0:data/AutoBoot/boot.cfg`, then loops `sceAppMgrLaunchAppByUri(0xFFFFF,"psgm:play?titleid=…")` until it succeeds. | Repo pushed 2022-10-03. I read the source. | https://github.com/Rinnegatamante/AutoBoot · https://gamebrew.org/wiki/AutoBoot_Vita |
| **VitaXMB** (an XMB-style dashboard). One SEO-style comparison page says it still hands off to LiveArea when a game launches. | Its 2026 status is **UNVERIFIED** | https://www.gamebrew.org/wiki/VitaXMB |
| **RetroFlow Launcher** (jimbob4000), the most active launcher. v8.5.0 shipped 2026-10-11 and added the Azahar 3DS emulator. v8.4.0 (2026-09-13) added Adrenaline 8 support. v8.2.0 (2026-06-27) added Emu4Vita++. | Active | https://github.com/jimbob4000/RetroFlow-Launcher/releases |
| **HexFlow Launcher** (VitaHEX) | Last push 2024-07-28 | https://github.com/VitaHEX-Games/HexFlow-Launcher |
| **LiveArea NoLimits** (a SceShell plugin that raises the page limit), **Home Screen Restore** (backs up and restores the bubble layout) | Older | https://www.gamebrew.org/wiki/LiveArea_NoLimits_Vita · https://gamebrew.org/wiki/Home_Screen_Restore_Vita |

### Recovery and risks
- vita.hacks.guide troubleshooting says: "Hold `L` on boot to disable TaiHEN plugin loading." It then says to edit `ux0:tai/config.txt` and remove the plugins added most recently. If neither works, "your device may be bricked". https://vita.hacks.guide/troubleshooting
- taiHEN reads `ux0:tai/config.txt` first and falls back to `ur0:tai/config.txt`. Guides warn that both files can exist, so the one you edit may not be the one in use. This comes from secondary guides; I did **not** check it against the taiHEN source.
- Repairing the database from Safe Mode can wipe LiveArea bubbles. This comes from anecdotal Reddit reports.
- A `*main` user plugin running in SceShell, which is what VitaOS uses, is the lower-risk tier. `*KERNEL` plugins are the ones linked to boot loops. LinuxOnVita's README, for example, names USB and storage-remap kernel plugins.
- Security warning: an r/vitahacks thread reported a GitHub repo posing as the EQVita plugin. Install plugins only from the original author.

### Usable for VitaOS?
**Yes, and VitaOS is already on the safe path.** VitaOS's `boot_start` already has a delay, bounded retries, a kill-switch file, and checks that no app is running.

One improvement is worth copying from AutoBoot: wait until **NPXS10079 is running** instead of a fixed 15 s. That starts VitaOS faster on fast boots and avoids launching before the shell is ready on slow ones.

Do **not** replace or patch SceShell itself, and do not ship a `*KERNEL` component. Every true "shell replacement" option means modifying SceShell, which raises the risk of a boot loop for non-technical users.

Keep documenting the hold-L recovery in the installer and README. Put `ur0:tai` vs `ux0:tai` detection in VitaOS's own installer, so the line goes into the config file that is actually in use.

---

## 2. Plugins that make it feel like an OS: overlays, quick menus, suspend

| Item | Status / date | Link |
|---|---|---|
| **PSVshell** (Electry). Overclock overlay with fixed CPU steps from 41 to 500 MHz, toggled with Select+Up/Down. | Repo pushed 2023-03-22 | https://github.com/Electry/PSVshell |
| **PSV-Shell-Plus / PSVshellPlus** (darkelf44 fork). Adds a second page with extra info and current draw. | Fork | https://github.com/darkelf44/PSV-Shell-Plus · https://gamebrew.org/wiki/PSVshellPlus_Vita |
| **VitaGrafix** (sets resolution and FPS cap per game) | Repo pushed 2026-09-03 | https://github.com/Electry/VitaGrafix |
| **Quick Menu Plus**. The original Team CBPS v2.0.0 (2020) adds power-off, restart, standby and a volume slider to the PS quick menu. The GameBrew plugin list shows a v3.0.0 dated 2025-06-20 credited to GrayJack. | **I could not locate the v3 repo, so it is UNVERIFIED.** | https://gamebrew.org/wiki/Quick_Menu_Plus_Vita · https://github.com/psv-plugins-archive/QuickMenuPlus (archive, pushed 2024-06-08) |
| **PSV-VSH-Menu** (joel16). In-game reboot, shutdown, suspend and FPS menu. | Archived; does not work in SceShell | https://github.com/joel16/PSV-VSH-Menu |
| **LOLIcon** (kernel overclock) | 2018 | https://github.com/kylon/LOLIcon |
| **Vita JP Overlay** (Dartv) v0.7, GameBrew infobox 2026-10-12. A recent example of a working **in-game overlay**: a kernel `.skprx` plus a SceShell `.suprx`, credited to PSVshellPlus, reVita and the ScePaf-RE / vitasdk-paf-component hook patterns. | 2026 | https://github.com/Dartv/vita-jp-overlay · https://www.gamebrew.org/wiki/Vita_JP_Overlay |
| **Adrenaline 8.0** (isage) has its own GPU-rendered VSH menu, pauses the PSP game when that menu opens, and has a PS-button behaviour setting (via RetroFlow's boot.bin field at 0x14). | v8.0.0 2026-07-03, v8.0.2 2026-07-12 | https://github.com/isage/Adrenaline/releases |

**Suspend and resume:** I found no homebrew that keeps several games suspended or switches between them. The Vita runs one game app at a time. Launching another app while a game is suspended brings up the system "close the app?" dialog, which VitaOS already deals with through its `confirm.req` bridge. This is my own inference from the platform; no source states it.

### Usable for VitaOS?
- **Partly.** A real in-game "VitaOS quick menu" would need a SceShell or kernel overlay plugin in the style of PSVshell or Vita JP Overlay. That is a lot of work and adds brick and compatibility risk.
- A safer step: VitaOS can detect PSVshell or VitaGrafix and show it. The detection can grep the active `tai/config.txt`. It can then offer per-game tips such as "set 444/500 MHz for DSVita", without bundling either plugin.
- Do not bundle a kernel overlay.

---

## 3. vitasdk, audio decoding, rendering and fonts

### vitasdk packages (checked from the GitHub API on 2026-11-11)
The `vitasdk/packages` repo contains: `freetype`, `harfbuzz` (updated to 14.5.0 on 2026-09-24), `fribidi` 1.0.17, `ffmpeg` (updated to 9.0.2 on 2026-09-24), `libvita2d`, `libvita2d_ext`, `vitaGL`, `sdl2`, `sdl2_ttf`, `opus`/`opusfile`, `flac`, `mpg123`, `libmad`, `libvorbis`, `libsndfile`, `openal-soft`, `soloud`, `libwebp`, `curl`, `openssl`, `kubridge`, `imgui-vita2d`, and others.
- **No faad2 or fdk-aac package.** FFmpeg's built-in AAC decoder is the packaged software option.
- Recent work is maintenance: an OpenSSL ABI fix for curl (2026-10-15), CMake 4.x support (2026-10-17), and a kubridge update after vita-headers PR #889 (2026-10-14). https://github.com/vitasdk/packages
- `vita-headers` was pushed 2026-11-11 (active). `xerpi/libvita2d` was last pushed 2022-09-17 and is effectively frozen.

### Rendering alternatives
- **SDL3** officially supports Vita: a GXM renderer by default, optional PVR GL through gl4es4vita, and hints for 720/1080 output on PS TV. https://wiki.libsdl.org/SDL3/README-vita
- **vitaGL** (Rinnegatamante) was pushed 2026-11-08. https://github.com/Rinnegatamante/vitaGL

### AAC and m4a playback
| Item | Findings | Link |
|---|---|---|
| **libShellAudio** (GrapheneCt) | A reverse-engineered Sony library that plays MP3, **AAC (.m4a/.aac)**, ATRAC9 and WAV **through SceShell's resources**, decoded on the Codec Engine. It supports **background music** that keeps playing outside the app, which needs the "ENABLE BGM PROXY" attribute in param.sfo. It also has a music-player mode with EQ, repeat and shuffle, controlled by the quick menu's Music slider. MIT licence. GameBrew lists v1.2 (2021-09-04). The repo is archived (last pushed 2026-10-16). | https://github.com/GrapheneCt/libShellAudio · https://gamebrew.org/wiki/LibShellAudio_Vita |
| **ElevenMPV-A** (GrapheneCt) | GameBrew lists v7.10 (2022-11-19) and describes it as having "BG playback/hardware decoding". The repo is archived and includes a `ElevenMPV-A-ShellPlugin` and `libScePafPreload`. Its exact AAC decoder (SceAudiodec or something else) is **UNVERIFIED**: code search found no `sceAudiodec`. | https://github.com/GrapheneCt/ElevenMPV-A |
| **PSVITAMAN** (ObsoleteSony) v0.1.0 Alpha 3, 2026-10-12 | Plays FLAC, MP3, WAV and **AAC-LC in unencrypted M4A/MP4** through "the Vita native AAC service" (which module is unnamed). Supports Jellyfin over the LAN and up to 10,000 tracks. Closed source as far as I could see. | https://www.gamebrew.org/wiki/PSVITAMAN_by_ObsoleteSony |
| **FMOD-PSV** | Uses SceNgs and SceAudiodec hardware acceleration | https://gamebrew.org/wiki/FMOD-PSV_Vita |

### Usable for VitaOS?
- **libShellAudio is the most valuable find here.** VitaOS Music could keep playing after the player launches a game or goes back to LiveArea, like the stock Music app. It is MIT-licensed and uses hardware decoding.
- Risk: the library is archived and relies on reverse-engineered SceShell internals. Test it on the 3.60, 3.65 and 3.74 (HENlo) setups VitaOS supports.
- If that fails, FFmpeg 9.0.2 from vitasdk/packages can decode AAC in software.
- For CJK text, `freetype` plus `harfbuzz` are packaged, and the vita2d family loads TTF/OTF through FreeType. Loading a system font from `sa0:` instead is **UNVERIFIED** here.

---

## 4. Launching emulators with a ROM: the launch URIs

All of these use `sceAppMgrLaunchAppByUri(0xFFFFF, "psgm:play?titleid=<TID>&…")`. The target app reads the string back with `sceAppMgrGetAppParam`. Rows marked "source" come from that app's own code; rows marked RetroFlow come from RetroFlow's `src/index.lua` (lines ~6410–6612).

| Target | Title ID | URI parameters | How it parses them (and the catch) | Source |
|---|---|---|---|---|
| **RetroArch** | `RETROVITA` | `&param=<core path, e.g. app0:/<core>_libretro.self>&param2=<ROM path>` | Read only by the **Salamander** build (the default eboot). It needs both keys and cuts each value at the next `&`. `param` is the executable for `sceAppMgrLoadExec`; `param2` becomes argv[0]. **A ROM path containing `&` breaks.** RetroFlow builds core paths as `"app0:/" .. core_file`. | `frontend/drivers/platform_psp.c` (source) · RetroFlow |
| **Emu4Vita++** (noword) | `EMU4VPLUS` | `&core=<core name>&rom=<ROM path>` | Values are **URL-decoded**, and the core name is checked by `IsSafeCoreName`. The app then chain-loads the per-core eboot with `--rom <path>`. RetroFlow percent-encodes everything except `[A-Za-z0-9-_.~:/]` and strips the `eboot_` prefix from core files (e.g. `eboot_chimerasnes` becomes `chimerasnes`). Supported since v0.71; latest **v0.81 (2026-10-05)**. | `arch/source/main.cpp` (source) · https://github.com/noword/Emu4VitaPlus |
| **DaedalusX64** | `DEDALOX64` | `&param=<ROM path>` | Takes the rest of the string after `&param=` and boots it directly, skipping the ROM picker. The repo was pushed 2026-10-12. | `Source/SysVita/main.cpp` (source) |
| **DSVita** (Grarak) | `DSVITA000` | `&param=<.nds path>` | Takes the rest of the string after `&param=`. Saves and settings are keyed by file name under the cartridge path. ROM folder: `ux0:data/dsvita`. Latest **0.9.4 (2026-07-21)**, which added save states, cheats, a right-stick or rear-touch touchscreen and layouts. It needs `libshacccg.suprx` and a recent **kubridge** kernel plugin; 500 MHz is recommended. | `src/presenter/vita.rs` (source) · https://github.com/Grarak/DSVita · https://www.generationamiga.com/2026/07/23/nintendo-ds-emulator-dsvita-gets-a-major-ps-vita-update/ |
| **ScummVM** | `VSCU00001` | `&path=<game dir>&game_id=<id>` | Accepts either order and becomes `-p <path> <game_id>`. A `%` in game_id is unsafe because it is passed through as a format string. | `backends/platform/sdl/psp2/psp2-main.cpp` (source) |
| **Flycast** (Dreamcast) | `FLYCASTDC` | `&param=<ROM>` | RetroFlow only; I did not read the emulator's source. | RetroFlow |
| **Fake-08** (PICO-8) | `FAKE00008` | `&param=<cart>` | RetroFlow only | RetroFlow |
| **Azahar** (3DS, experimental) | `AZAHARPSV` | `&param=<ROM>`, ROMs in `ux0:/data/azahar/roms` | RetroFlow only. GameBrew calls it "an early, almost entirely vibecoded port" that needs a **.skprx plus CapUnlocker**. September 2026 coverage. | RetroFlow · https://www.gamebrew.org/wiki/Azahar_Vita · https://powerupgaming.co.uk/2026/09/07/the-ps-vita-is-somehow-emulating-3ds-games-now-and-it-already-runs-smash-bros/ |
| **EasyRPG** | `EASYRPG01` | `&project-path=<dir>` | RetroFlow only | RetroFlow |
| **Adrenaline (PSP/PS1)** | `RETROLNCR` (RetroFlow's Adrenaline launcher) | Write `ux0:app/RETROLNCR/data/boot.bin`. The game path goes at offset 0x40, null-padded to 256 bytes. Other fields: PS-button mode at 0x14, CPU speed, plugins at 0x20, NoDRM at 0x24, high memory at 0x28. Then launch `psgm:play?titleid=RETROLNCR`. | VitaOS already does this. RetroFlow v8.4.0 moved to **AdrBubbleBooter-oss** (shoui520, v1.3, 2026-08-25) and added Adrenaline **8.0.2** support plus the DAX/ZSO formats. Adrenaline itself has **no documented ISO launch argument**. | RetroFlow · https://github.com/shoui520/AdrBubbleBooter-oss · https://github.com/isage/Adrenaline |
| **mGBA standalone, PCSX-ReARMed standalone, OpenBOR** | — | **UNVERIFIED.** I found no launch-parameter convention. RetroFlow v8.3.0 uses the **mGBA core through RetroArch or Emu4Vita++** for GB and GBC. | https://github.com/jimbob4000/RetroFlow-Launcher/releases |

Adrenaline 8.0.0 (2026-07-03, isage, with the ARK developers) adds a choice of vPSP CFW ("Epinephrine"), Scale2x/3x filters, PS1 overlays and a GPU VSH menu. Docs: https://adrenaline.sarcasticat.com/docs/. One secondary blog post says Adrenaline 8 **may break Adrenaline Bubble Manager** compatibility; that is **UNVERIFIED**.

### Usable for VitaOS?
**Yes, and this is cheap.** VitaOS's `play.c` already has the right structure. Adding **DSVita, Emu4Vita++, Flycast, Fake-08 and EasyRPG** means one URI line each, plus a "this emulator isn't installed" check before launch, which RetroFlow does with `check_app_installed`.

Points to handle:
- Reject ROM paths containing `&`, or percent-encode them for Emu4Vita++ only. RetroArch and DaedalusX64 do no decoding.
- DSVita needs kubridge, which is a **kernel** plugin. Detect it rather than installing it.
- Treat Azahar as experimental: it needs a kernel plugin and CapUnlocker, so keep it off by default.

---

## 5. Homebrew databases and box art

| Source | Status | Link |
|---|---|---|
| **VitaDB (rinnegatamante.eu)** | **Offline as of 31.07.2026**, per the VitaHomebrewDB README. On 2026-11-11, `https://www.rinnegatamante.eu/vitadb/list_hbs_json.php` returns HTTP 200 with `[]`. **Do not use it.** | https://github.com/DrDecki/VitaHomebrewDB |
| **VitaHomebrewDB** (DrDecki; the same data appears as `DrDecki/VitaDBtoo-db`) | Static GitHub Pages site at `https://drdecki.github.io/VitaHomebrewDB/` (`apps.json`, `icons/`). Built from the last VitaDB client cache (2026-07-31) plus Internet Archive recovery. It lists 1,013 Vita homebrews, 123 plugins, 126 PSP homebrews and 27 PC tools, with 352 entries added since. Updated daily (pushed 2026-11-11). **VitaOS already uses it.** | https://drdecki.github.io/VitaHomebrewDB/ |
| **NeoVitaDB Catalog** (robin994) | Static site at `https://robin994.github.io/NeoVitaDB-Catalog/`, with one JSON file per app. A build job resolves each entry's **latest GitHub release** every 6 h and computes the hash, size and download count. A `trusted` flag is set when the repo has more than 50 stars. New entries go through a staging test catalog first. Its README notes the console-side parser is not a full JSON parser. Created 2026-08-01, pushed 2026-11-10. The client is NeoVitaDB Downloader v2.9.1 (2026-08-16). | https://github.com/robin994/NeoVitaDB-Catalog · https://github.com/robin994/NeoVitaDB-Downloader |
| **VitaForge** (josephinoo) v0.1.10, 2026-10-27 | A catalog browser on VitaDBtoo plus PKGj/NPS. It warns about missing data files or plugins. | https://github.com/josephinoo/vitaForge |
| **CBPS-DB** | `Team-CBPS/cbps-db` was pushed 2026-08-09, so it is still alive. I did not check its format. | https://github.com/Team-CBPS/cbps-db |
| **libretro-thumbnails** | Pushed 2026-09-06. `thumbnails.libretro.com/<System>/Named_Boxarts/` responds. VitaOS already uses it. **The licence terms are UNVERIFIED** (no SPDX licence on the repos). | https://github.com/libretro-thumbnails/libretro-thumbnails |
| **ScreenScraper API v2** | It needs **devid/devpassword obtained by presenting your software on their forum**. Quotas per thread, per minute and per day are mandatory, and must be handled. Use is allowed only in "applications entièrement gratuites et distribuées" (free, distributed apps) unless authorized. The API is in beta and can change without notice. | https://www.screenscraper.fr/webapi2.php |

### Usable for VitaOS?
- Keep VitaHomebrewDB as the main source.
- Consider NeoVitaDB as a **second source for checking updates**: it has per-release hashes and a `trusted` flag, which are good safety signals for non-technical users.
- Drop any leftover VitaDB endpoints.
- ScreenScraper is feasible but needs developer registration and quota handling. Treat it as optional; libretro thumbnails are enough.

---

## 6. Similar projects and community news (2025–26)

- **LinuxOnVita** (devwithzachary): v1.5.0 released 2026-10-23, running Linux 6.12 with an Alpine chroot, IceWM, Wi-Fi and ZRAM. It runs on 3.60/3.65 with HENkaku/Ensō (HENlo is linked).
  - It uses a **baremetal loader `.skprx`**.
  - It warns of **boot loops on return** when USB, storage-remap or other `*KERNEL` plugins are active.
  - It also warns that a 30 s forced power-off can corrupt microSD controllers ("all sectors return 0xFF").
  - https://github.com/devwithzachary/LinuxOnVita
  - Verdict: **link to it, never integrate it.** High risk for non-technical users.
- **GreenVita** (Day-OS): an Xbox Cloud Gaming client written in Rust, using the hardware H.264 decoder. Version 0.1.7 or 0.1.8 depending on the source; July 2026 coverage. https://www.purexbox.com/news/2026/07/xbox-cloud-gaming-now-works-on-the-ps-vita-and-heres-a-first-look · https://www.gamebrew.org/wiki/GreenVita. It is a candidate for the Play or Apps grid; its repo URL `Day-OS/green-vita` was not checked.
- **ReiHEN homebrew contest** (Zealous Chuck and 2Old4Gaming): entries ran 2025-07-21 to 2025-10-07, with a $700 prize pool. I did not check the results. https://wololo.net/2025/07/09/reihen-vita-homebrew-contest-announcement-700-in-cash-prizes/
- **RetroArch 1.22.2** (2025-11-20) is the current stable release with a Vita build. https://github.com/libretro/RetroArch/releases
- **GcToolKit v1.9** (July 2026) dumps game cartridges. Per retrohandheldhq; **UNVERIFIED** at the source.
- **Vita3K**, the PC emulator, is useful for testing VitaOS UI changes without hardware. Its homebrew compatibility list: https://github.com/Vita3K/homebrew-compatibility
- r/vitahacks: mirror snapshots show RetroFlow release posts and the fake-EQVita warning. I could not verify individual 2026 threads.

---

## Ranked next steps for VitaOS: high value, feasible, low brick risk

1. **Add launch URIs for DSVita (`DSVITA000 &param=`), Emu4Vita++ (`EMU4VPLUS &core=&rom=`, URL-encoded), Flycast, Fake-08 and EasyRPG** in `play.c` and `library.c`.
   - Check the app is installed before launching; the code is in the RetroFlow pattern above.
   - Reject `&` in ROM paths for the `&param=` targets.
   - Add NDS (`ux0:data/dsvita`) as a Play system.
   - Risk: none; this is app-level only.
2. **Switch the boot trigger to wait for LiveArea (NPXS10079)**, as AutoBoot does, keeping VitaOS's existing retry limit and `BOOT_OFF` kill switch. Risk: very low, since the plugin stays user-mode under `*main`.
3. **Make the installer safer**: detect which `tai/config.txt` is in use (ux0 vs ur0), back it up before editing, only append the `*main` line, and show "Hold L at power-on to skip plugins" in the installer and settings. Risk: low, and it lowers support load.
4. **Use NeoVitaDB as a second update and trust signal** in the Store (release hash, `trusted` flag), and remove any leftover VitaDB endpoints. VitaDB has been offline since 2026-07-31. Risk: none.
5. **Prototype background music with libShellAudio** (MIT, plays AAC through SceShell's Codec Engine, param.sfo BGM proxy flag), so VitaOS Music keeps playing like the stock player. Fall back to the current player if it fails to initialize. Risk: low for the device, medium for compatibility, because the library is archived reverse-engineering; test on 3.60, 3.65 and 3.74.
6. **Add per-emulator readiness checks** to the "Ready to play" checks: DSVita needs kubridge and libshacccg; Adrenaline needs its kernel line; recommend 444/500 MHz if PSVshell is present. Detect only and never install kernel plugins automatically. Risk: none.
7. **Support Adrenaline 8 (isage)**: keep the `RETROLNCR` boot.bin path, accept DAX and ZSO files, and point users to AdrBubbleBooter-oss. Risk: low.
8. **Optional:** ScreenScraper box art for systems that libretro thumbnails miss. It needs dev credentials from the ScreenScraper forum and quota handling, and is allowed only because VitaOS is free. Risk: none to the device; some project overhead.
9. **Do not do (brick or support risk too high for non-technical users):** replace or patch SceShell, ship any `*KERNEL` plugin (an overlay quick menu, Azahar's skprx, LinuxOnVita's baremetal loader), or run Enso or firmware steps from inside VitaOS. Link to vita.hacks.guide instead.

### Unverified or not found
- An exploit for 3.74 or later newer than HENlo, or any 2025–26 CFW.
- The Quick Menu Plus v3.0.0 (2025) repo.
- ElevenMPV-A's exact AAC decoder.
- Launch arguments for standalone mGBA, PCSX-ReARMed and OpenBOR.
- The emulator-side parsers for Flycast, Fake-08, Azahar and EasyRPG (taken from RetroFlow's usage only).
- libretro-thumbnails licence terms.
- Adrenaline 8 vs Bubble Manager compatibility.
- The VitaXMB 2026 status.
- The GreenVita repo URL and version.
- Loading system fonts from `sa0:`.
