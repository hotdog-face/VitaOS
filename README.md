# VitaOS

A modern home screen for the PS Vita. VitaOS puts your games, films, music,
apps and photos behind one fast, good-looking menu, with a PS5-style feel on
the handheld you already own.

**Case study and video:** [mitchellv.me/work/vitaos](https://www.mitchellv.me/work/vitaos) · by [Mitchell Vizensky](https://www.mitchellv.me), game UX designer

If it's useful to you, you can [buy me a coffee on Ko-fi](https://ko-fi.com/Y4G62AHO0A). It helps keep these projects going.

VitaOS is free and open source (GPLv3). It is not affiliated with or
endorsed by Sony Interactive Entertainment. PlayStation and PS Vita are
trademarks of Sony Interactive Entertainment.

![Home](screenshots/home.png)

| Play | Music |
| --- | --- |
| ![Play](screenshots/play.png) | ![Music](screenshots/music.png) |
| **Movies** | **Downloads** |
| ![Movies](screenshots/movies.png) | ![Downloads](screenshots/store.png) |

## What you get

- **Home:** jump back into what you played last, with Quick Resume
  snapshots, the time, battery and weather.
- **Play:** every game on your card, by console and by collection, with box
  art, gameplay previews, favourites and search. It covers RetroArch
  systems, PSP and PS Vita.
- **Movies:** a cover flow of your films with resume, plus swipe for
  brightness and volume.
- **Music:** albums, artists, songs, liked songs, shuffle and repeat, and it
  keeps playing while you browse.
- **Apps:** your installed apps as a tidy grid you can rearrange (hold X), plus
  Camera, Photos and Voice Memos built in.
- **Store:** homebrew from VitaHomebrewDB and CBPS-DB, sorted by what is most
  popular, with installs and updates on the Vita itself.
- **Files, Settings, search,** an on-screen keyboard, toasts, UI sounds and
  a live, tilt-aware background.

## In your language

VitaOS speaks English, Español, Português, Français, Deutsch, Italiano, 日本語 and Русский (Settings > Display & sound > Language; Automatic follows your Vita).

- **Español:** Un menú de inicio moderno para PS Vita: tus juegos, películas, música y apps en un solo lugar, con una tienda de homebrew integrada. Gratis y de código abierto. Instálalo con VitaShell; después se actualiza desde Ajustes con un botón.
- **Português:** Uma tela inicial moderna para o PS Vita: seus jogos, filmes, músicas e apps num só lugar, com uma loja de homebrew integrada. Gratuito e de código aberto. Instale com o VitaShell; depois ele se atualiza pelos Ajustes com um botão.
- **Français :** Un écran d'accueil moderne pour la PS Vita : vos jeux, films, musique et applis au même endroit, avec une boutique homebrew intégrée. Gratuit et open source. Installez-le avec VitaShell ; ensuite il se met à jour depuis les Réglages en un clic.
- **Deutsch:** Ein moderner Startbildschirm für die PS Vita: Spiele, Filme, Musik und Apps an einem Ort, mit eingebautem Homebrew-Store. Kostenlos und Open Source. Mit VitaShell installieren; danach aktualisiert es sich per Knopfdruck in den Optionen.
- **Italiano:** Una schermata Home moderna per PS Vita: giochi, film, musica e app in un unico posto, con uno store homebrew integrato. Gratuito e open source. Installalo con VitaShell; poi si aggiorna dalle Impostazioni con un tasto.
- **日本語:** PS Vita用のモダンなホーム画面。ゲーム・映画・音楽・アプリを一か所にまとめ、自作ソフトのストアも内蔵。無料のオープンソースです。VitaShellでインストールすれば、以降は設定からボタン一つでアップデートできます。
- **Русский:** Современный главный экран для PS Vita: игры, фильмы, музыка и приложения в одном месте, со встроенным магазином homebrew. Бесплатно и с открытым кодом. Установите через VitaShell, дальше обновления ставятся одной кнопкой в Настройках.

## Install

**What you need:** a PS Vita (or PS TV) that can run homebrew: HENkaku, h-encore
or Ensō, with VitaShell. If yours can't yet, follow
[vita.hacks.guide](https://vita.hacks.guide) first; it takes about 20 minutes.

1. On a computer or phone, download **`VitaOS.vpk`** from
   [the latest release](../../releases/latest).
2. Copy it to the Vita. The easiest way is VitaShell's USB mode (press SELECT
   in VitaShell, plug in the cable, copy the file to `ux0:`), or VitaShell's
   FTP mode over Wi-Fi.
3. In VitaShell, find `VitaOS.vpk`, press X and choose **Install**.
4. Open **VitaOS** from its bubble.

### Make VitaOS open when the Vita starts

In VitaOS go to **Settings > VitaOS > Start at power-on** and press X. VitaOS
sets up its small plugin by itself (it keeps a copy of your old plugin list),
then asks to restart. From then on:

- the Vita starts in VitaOS,
- a game you started from VitaOS comes back to VitaOS when it ends,
- PS inside VitaOS goes to its Home tab.

To undo it, use the same row: **VitaOS plugin > Remove the plugin**.

This needs plugins to be loaded when the Vita powers on, which is what
**Ensō** does. With h-encore or HENkaku without Ensō, plugins only load after
you run the exploit, so VitaOS opens by itself only after that.

If VitaOS doesn't open at power-on, `ux0:data/arcadehub/boot.log` says what
the plugin did at the last start.

**If the Vita ever starts badly,** hold **L** while turning it on: that skips
every plugin for that start. Your old plugin list is saved next to it as
`tai/config.txt.before-vitaos`.

<details><summary>Doing it by hand instead</summary>

Copy `vitaos_ps.suprx` (from the release) to `ur0:tai/`, then add it under
`*main` in the `config.txt` your Vita uses (`ux0:tai/config.txt` if it
exists, otherwise `ur0:tai/config.txt`):

```
*main
ur0:tai/vitaos_ps.suprx
```

If there is already a `*main` line, add only the second line under it, then restart.
</details>

### Updating

VitaOS checks for a new version once a day and shows a short notice when one
is out. Then go to
**Settings > VitaOS > Update** (or press START anywhere in Settings): it
downloads, installs and reopens by itself. Your games, settings and
favourites stay.

### If something goes wrong

- **VitaOS closed unexpectedly:** next time it opens it shows a QR code. Scan
  it with your phone to open a bug report that is already filled in. If you
  can, attach the crash file it names (in `ux0:data`).
- **A song or film won't play:** VitaOS plays AAC `.m4a` songs and H.264
  `.mp4` films up to 960x544, as they are (it never changes your files).
  Apple Lossless and other formats need converting to AAC first.
- **A game shows up in Apps instead of Play** (or the other way round):
  Triangle on it in Apps > **Show in Play**, or START on it in Play > **Move
  it to Apps**.

## Your content

VitaOS finds everything on the card by itself. It looks again every time it
starts (or choose Settings > VitaOS > Find games again).

| What | Put it here |
|---|---|
| PS Vita games | Installed games are found automatically |
| PSP and PS1 games | `ux0:pspemu/ISO` and `ux0:pspemu/PSP/GAME` (needs Adrenaline; RetroFlow's launcher or an Adrenaline bubble starts a game directly) |
| Retro games | `ux0:roms/<console>/`, e.g. `ux0:roms/snes`, or RetroArch playlists. Needs [RetroArch](https://www.retroarch.com/) and its cores |
| Nintendo 64 | `ux0:roms/n64` (needs DaedalusX64) |
| Nintendo DS | `ux0:data/dsvita` or `ux0:roms/nds` (needs DSVita) |
| Dreamcast | `ux0:roms/dreamcast` (needs Flycast) |
| Box art | Downloaded by itself; RetroFlow's and RetroArch's art is used too |
| Films | `.mp4` (H.264, up to 960x544) in `ux0:video/` |
| Music | `.m4a` (AAC) in `ux0:data/music/`, tagged with title, artist and album |
| Photos | Everything in `ux0:picture/` |

Most of these emulators are in VitaOS's Store tab.

## Build from source

With [VitaSDK](https://vitasdk.org) installed:

```
mkdir build && cd build
cmake ..
make
```

Build `plugin/` first and copy `vitaos_ps.suprx` to `assets/plugin/` (VitaOS
ships it), and `updater/` for `assets/updater.vpk`; `.github/workflows/build.yml`
does all three in order. Tests that run on a computer: `tools/test_sysint.sh`
(and `tools/test_mp4fix.sh` for the MP4 tool code, which VitaOS doesn't ship). Crash dumps: `tools/crashdump.py`.

## Credits

See [CREDITS.md](CREDITS.md).
