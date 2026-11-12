# experimental/: VitaOS as the system

Not part of any VitaOS release. The plan, the test log and what is still
missing are in [docs/real-os-plan.md](../docs/real-os-plan.md); the install
and recovery page is [docs/experimental-install.md](../docs/experimental-install.md).

| Folder | What it is |
|---|---|
| `shell/` | `vitaos_shell.suprx`, a user-mode taiHEN plugin loaded into SceShell (`*main`). Opens VitaOS at power-on, removes the system lock screen (3.65 only), holds the system's PS lock while a game is in front and opens the menu on a short PS, and brings VitaOS back when a game ends. |
| `menu-app/` | The in-game menu ("VitaOS Menu", title ID `VTOSPAUSE`): a 16 MB system-mode app that runs beside a suspended game. Resume, Volume, Brightness, Close game. |
| `overlay-wip/` | Things that were tried and are **not built**: a kernel module that hid PS from the shell (not needed since the PS lock), controller hooks inside SceShell, and a card drawn into the game's frames from the kernel. Its README says what happened. |
| `out/` | Build output (ignored by git). |

## Build

```sh
export VITASDK=/path/to/vitasdk
sh tools/build_experimental.sh          # shell plugin and menu app -> experimental/out/
mkdir build-exp && cd build-exp
cmake .. -DVITAOS_EXPERIMENTAL=ON       # VitaOS with the Settings row, carrying both parts
make
```

Without `-DVITAOS_EXPERIMENTAL=ON`, VitaOS has none of this: no Settings
row and nothing from `experimental/` in the package. CI checks both
(`.github/workflows/experimental.yml`), uploads nothing and releases nothing.

## How the parts talk

All through small files in `ux0:data/arcadehub/`:

| File | Written by | Read by | Meaning |
|---|---|---|---|
| `paused.txt` | plugin | menu app | title ID, name and minutes played of the game the menu is over |
| `menu.ps` | plugin | menu app | PS was pressed while the menu is up: resume |
| `menu.quit` | plugin | menu app | the game behind the menu has gone: close |
| `home.req` | menu app | plugin | Close game was chosen: open VitaOS once the game has ended |
| `boot.log` | plugin | people | what the plugin did since power-on |
| `boot.patching` | plugin | plugin | lock-screen patches in progress; found at a start, they are skipped |

Switches are empty files in `ux0:data/arcadehub/user/`: `shell.off`,
`menu.off`, `pslock.off`, `shell-idle.off`, `shell-return.off`,
`lockscreen.keep`, `boot.off`, and for diagnosis `shell-debug`,
`menu-debug`.
