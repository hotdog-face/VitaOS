/* vitaos_shell: EXPERIMENTAL. VitaOS as the system's shell (docs/real-os-plan.md).
 * Not part of any VitaOS release. Built on vitaos_ps 1.5 (plugin/vitaos_ps.c);
 * loaded into SceShell by taiHEN in its place:
 *
 *     *main
 *     ur0:tai/vitaos_shell.suprx
 *
 * What it adds to vitaos_ps:
 *  - Power-on: opens VitaOS as soon as the system's start-up app (NPXS10079)
 *    is done, not after a fixed 15 s.
 *  - No lock screen at power-on or wake (NoLockScreen's two patches, only on a
 *    SceShell whose NID is known, and never again after a boot that did not
 *    get past them: see patch_guard). user/lockscreen.keep keeps the lock screen.
 *  - PS in a game opens the VitaOS menu (experimental/menu-app, VTOSPAUSE)
 *    over the suspended game. Holding PS still opens the system's quick menu.
 *    The system's own PS lock, held here while a game is in front, keeps the
 *    shell from going to LiveArea on the press. User-mode only: an earlier
 *    kernel half and controller hooks are gone (experimental/overlay-wip
 *    has them and why).
 *  - When a game ends, or the menu closes it, VitaOS comes back at once,
 *    wherever the game was started from.
 *
 * Undo: delete the line from ur0:tai/config.txt and restart. Holding L at
 * power-on skips this plugin. Switches, all in ux0:data/arcadehub/user/:
 * shell.off (everything below off), lockscreen.keep, menu.off, boot.off. */
#include <taihen.h>
#include <psp2/ctrl.h>
#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/shellutil.h>

static volatile int running = 1;
static SceUID thread = -1;

#define MENU_TID "VTOSPAUSE"

static int app_running(const char *tid) {
    SceUID id = -1;
    return sceAppMgrGetIdByName(&id, tid) >= 0 && id > 0;
}

static int vitaos_running(void) {
    return app_running("VITAOS001") || app_running("MVZA00010");   /* the developer's build */
}

static int is_home(const char *t) {
    return !sceClibStrncmp(t, "VITAOS001", 9) || !sceClibStrncmp(t, "MVZA00010", 9);
}

static int file_exists(const char *p) {
    SceIoStat st;
    return sceIoGetstat(p, &st) >= 0;
}

/* ---------- return to VitaOS when a game ends ---------- */

#define TICK_MS 250
static SceUInt64 armed_since;
static char armed[16];                /* title ID of the game whose end brings VitaOS back */
static SceUID armed_pid;
static int gone_ticks;

/* The non-system app the shell lists as running: 1 with its title ID and pid.
 * *home is set if VitaOS is among them. */
static int running_game(char *tid, SceUID *pid, int *home) {
    SceInt32 ids[8];
    int n = sceAppMgrGetRunningAppIdListForShell(ids, 8), found = 0;
    *home = 0;
    for (int i = 0; i < n && i < 8; ++i) {
        SceUID p = sceAppMgrGetProcessIdByAppIdForShell(ids[i]);
        char name[32];
        sceClibMemset(name, 0, sizeof(name));
        if (p <= 0 || sceAppMgrGetNameById(p, name) < 0 || !name[0]) continue;
        if (!sceClibStrncmp(name, "NPXS", 4)) continue;               /* Settings, Music: not games */
        if (is_home(name)) { *home = 1; continue; }
        if (!sceClibStrncmp(name, MENU_TID, 9)) continue;             /* the in-game menu is not a game */
        sceClibStrncpy(tid, name, 15);
        tid[15] = 0;
        *pid = p;
        found = 1;
    }
    return found;
}

/* A game suspended behind LiveArea can be missing from the running list (seen
 * 2026-09-24), so an armed game has ended only when neither its title ID nor
 * its pid resolves. A false "alive" only keeps VitaOS away. */
static int still_alive(const char *tid, SceUID pid) {
    char name[32];
    if (app_running(tid)) return 1;
    return pid > 0 && sceAppMgrGetNameById(pid, name) >= 0;
}

/* The agent bridge (developer Vitas) can do these jobs too. This plugin does
 * not stand aside for it: on a test console the bridge's own kiosk and return
 * are switched off with its flag files (user/kiosk.on removed, return.off). */
static int bridge_loaded(void) { return 0; }

#define USER "ux0:data/arcadehub/user/"
/* Everything stands aside with user/shell.off, and while a developer's tools
 * are at work: deploy.lock (installs over the bridge) or Agent Recovery
 * running (it is not in the shell's running list, and 0.4 kept closing it). */
static int shell_off(void) {
    SceUID id = -1;
    return file_exists(USER "shell.off") || file_exists("ux0:data/arcadehub/deploy.lock")
           || (sceAppMgrGetIdByName(&id, "MVZA00003") >= 0 && id > 0);
}

static void slog(const char *what, const char *tid, int v);

static const char *home_tid(void) {
    if (file_exists("ux0:app/VITAOS001/eboot.bin")) return "VITAOS001";
    if (file_exists("ux0:app/MVZA00010/eboot.bin")) return "MVZA00010";
    return 0;
}

/* The first URI launch after a game exits often only opens the target's
 * LiveArea page; a second one starts it (learned on the agent bridge,
 * 2026-09-19). Stops as soon as VitaOS, or anything else, is running. */
static void launch_home(const char *home) {
    char uri[48];
    sceClibSnprintf(uri, sizeof(uri), "psgm:play?titleid=%s", home);
    for (int attempt = 0; attempt < 2 && running; ++attempt) {
        if (!app_running(home)) sceAppMgrLaunchAppByUri(0xFFFFF, uri);
        for (int i = 0; i < (attempt ? 20 : 6) && running; ++i) {
            sceKernelDelayThread(TICK_MS * 1000);
            char t[16];
            SceUID p;
            int h;
            if (app_running(home) || running_game(t, &p, &h)) return;
        }
    }
}

/* 1.2: back to wherever the game was started from. VitaOS marks its own
 * launches (confirm.req, written just before it launches a game); a game
 * started from its bubble ends on LiveArea as it always did. With no bridge
 * to consume the flag, the plugin takes it itself. */
#define LAUNCH_FLAG "ux0:data/arcadehub/confirm.req"
static SceUInt64 home_launch_at;
static int armed_from_home;

static void return_tick(void) {
    char tid[16];
    SceUID pid = -1;
    int home;
    if (!bridge_loaded() && file_exists(LAUNCH_FLAG)) {
        home_launch_at = sceKernelGetSystemTimeWide();
        sceIoRemove(LAUNCH_FLAG);
    }
    if (running_game(tid, &pid, &home)) {
        if (sceClibStrncmp(armed, tid, 16)) {
            sceClibMemcpy(armed, tid, sizeof(armed));
            armed_since = sceKernelGetSystemTimeWide();        /* for the menu: how long this game has been open */
            armed_from_home = home_launch_at && sceKernelGetSystemTimeWide() - home_launch_at < 90ull * 1000 * 1000;
        }
        armed_pid = pid;
        gone_ticks = 0;
        return;
    }
    if (home) { armed[0] = 0; gone_ticks = 0; return; }     /* VitaOS is up, or the player parked it */
    if (!armed[0]) return;
    if (still_alive(armed, armed_pid)) { gone_ticks = 0; return; }
    /* 1.5 s with nothing running; RetroFlow's PSP launcher hands over to
     * Adrenaline, so give that one longer. */
    int settle = sceClibStrncmp(armed, "RETROLNCR", 9) ? 3 : 24;   /* 0.5: 0.75 s, was 1.5 s */
    if (++gone_ticks < settle) return;
    armed[0] = 0;
    gone_ticks = 0;
    const char *h = home_tid();
    (void)armed_from_home;                      /* as the shell, VitaOS is where every game ends */
    if (!h || shell_off() || file_exists(USER "shell-return.off")) return;
    slog("a game ended, opening", h, 0);
    launch_home(h);
}

/* ---------- PS in a game: the VitaOS menu ----------
 * The shell still switches to LiveArea on the press. 0.4 s later, if PS is not
 * being held (that is the system's quick menu), the menu app is opened over
 * LiveArea with a launch that closes nothing; the game stays suspended behind
 * it. The menu reads which game it is from paused.txt. This is the dev
 * bridge's pause_step (i-ha/kiosk-wip/vabridge.c), without its kernel half:
 * "a game is running and the menu is not" stands in for "a game is in front". */
#define PAUSED_TXT "ux0:data/arcadehub/paused.txt"
#define HOME_REQ "ux0:data/arcadehub/home.req"
static SceUInt64 menu_at;
static char menu_tid[16];
static SceUID menu_pid;

static void menu_note_press(void) {
    int h;
    if (shell_off() || file_exists(USER "menu.off") || app_running(MENU_TID)) return;
    if (!running_game(menu_tid, &menu_pid, &h)) return;
    menu_at = sceKernelGetSystemTimeWide();
}

static void menu_open(void);

/* ---------- 0.8: the system's PS lock while a game is in front ----------
 * The shell learns of a PS press by more than the controller reads the
 * kernel half filters (seen on red, 2026-11-13: with nothing opened after a
 * hidden press, the shell still went to LiveArea). The dev bridge kept it
 * away with the system's own lock, taken from inside SceShell; this does the
 * same. Off with user/pslock.off. The lock is
 * dropped whenever no game is in front, the menu app is up (it holds its
 * own), or a hidden press failed to open anything. */
static int ps_lock_on, ps_lock_events;
static void ps_lock_set(int on) {
    if (on == ps_lock_on) return;
    if (!ps_lock_events) { sceShellUtilInitEvents(0); ps_lock_events = 1; }
    int rc = on ? sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2) : sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
    if (rc >= 0) ps_lock_on = on;
    if (file_exists(USER "shell-debug")) slog(on ? "ps lock on" : "ps lock off", 0, rc);
}

/* With the lock held and no kernel half, PS comes straight to this plugin's
 * own read: a press shorter than half a second opens the menu the moment it
 * is let go (the shell is not going anywhere, so there is nothing to wait
 * for); a longer one is the shell's quick menu. */
static SceUInt64 ps_lock_press;
static int qm_aside;                    /* the system's quick menu may be up: see below */
static void menu_open(void);
static void ps_lock_step(int ps_down) {
    if (!ps_lock_press) return;
    SceUInt64 held = sceKernelGetSystemTimeWide() - ps_lock_press;
    /* 0.9a: a held PS is the system's quick menu, which opens lock or no lock.
     * From then on this plugin stands aside (lock off, no menu of its own
     * straight over the game) until its menu has been up once more: red hung
     * when the menu was launched directly a few seconds after the quick menu
     * (2026-11-13, once in three runs of the same steps). The next short PS
     * goes to the shell, and the menu opens the older way, after LiveArea. */
    if (ps_down) {
        if (held >= 500 * 1000 && !qm_aside) { qm_aside = 1; slog("PS held: standing aside for the quick menu", 0, 0); }
        return;
    }
    ps_lock_press = 0;
    int h;
    if (qm_aside || held >= 500 * 1000 || app_running(MENU_TID) || !running_game(menu_tid, &menu_pid, &h)) return;
    slog("menu: opening straight over", menu_tid, 0);        /* a line before the launch: a hang after it says where */
    menu_open();
}

static void ps_lock_tick(void) {
    char t[16];
    SceUID p;
    int h;
    int menu = app_running(MENU_TID), game = running_game(t, &p, &h) && !h;
    if (qm_aside && (menu || !game)) qm_aside = 0;       /* the menu came up, or the game has gone: back to normal */
    ps_lock_set(!qm_aside && !file_exists(USER "pslock.off") && !shell_off() && !file_exists(USER "menu.off") && !menu && game);
}

static void menu_step(int ps_down) {
    if (!menu_at) return;
    SceUInt64 age = sceKernelGetSystemTimeWide() - menu_at;
    if (ps_down) {
        if (age > 600 * 1000) { slog("PS held: the quick menu", menu_tid, 0); menu_at = 0; }
        return;
    }
    if (age < 400 * 1000) return;
    menu_at = 0;
    menu_open();
}

static void menu_open(void) {
    if (!still_alive(menu_tid, menu_pid) || !file_exists("ux0:app/" MENU_TID "/eboot.bin")) {
        slog("menu not opened: the game is gone, or no menu app", menu_tid, (int)menu_pid);
        return;
    }
    char title[128];
    sceClibMemset(title, 0, sizeof(title));
    if (sceAppMgrAppParamGetString(menu_pid, 10, title, sizeof(title) - 1) < 0 || !title[0])
        if (sceAppMgrAppParamGetString(menu_pid, 9, title, sizeof(title) - 1) < 0 || !title[0])
            sceClibStrncpy(title, menu_tid, sizeof(title) - 1);
    for (char *c = title; *c; ++c) if (*c == '\n' || *c == '\t') *c = ' ';
    SceUID fd = sceIoOpen(PAUSED_TXT, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) {
        sceIoWrite(fd, menu_tid, sceClibStrnlen(menu_tid, 16));
        sceIoWrite(fd, "\t", 1);
        sceIoWrite(fd, title, sceClibStrnlen(title, sizeof(title)));
        /* 0.6: and the minutes this game has been open (seconds >> 6 is near enough
         * to minutes / 1.07; no 64-bit division here), when it is the game being followed. */
        if (armed_since && !sceClibStrncmp(armed, menu_tid, 16)) {
            char m[16];
            unsigned int secs = (unsigned int)((sceKernelGetSystemTimeWide() - armed_since) >> 20);
            int n = sceClibSnprintf(m, sizeof(m), "\t%u", secs / 57);   /* ">> 20" counts 1.048576 s as one */
            sceIoWrite(fd, m, n);
        }
        sceIoWrite(fd, "\n", 1);
        sceIoClose(fd);
    }
    int rc = sceAppMgrLaunchAppByUri(0x20000, "psgm:play?titleid=" MENU_TID);
    slog("menu over", menu_tid, rc);
}

/* The menu's "Close game" (and anything else that wants Home) leaves
 * home.req; a system-mode app cannot open Home itself without stopping on
 * its LiveArea page. Taken once nothing but the menu is left running. */
static void home_req_tick(void) {
    char t[16];
    SceUID p;
    int h;
    if (!file_exists(HOME_REQ)) return;
    if (app_running(MENU_TID) || running_game(t, &p, &h)) return;
    sceIoRemove(HOME_REQ);
    armed[0] = 0;
    const char *home = home_tid();
    if (!home || shell_off()) return;
    slog("home.req, opening", home, 0);
    launch_home(home);
}

/* LiveArea is never where the player is left. If nothing at all is running
 * (no game, no VitaOS, no system app such as Settings) for 5 s, open VitaOS.
 * VitaOS parked behind LiveArea by its own "system home" counts as running,
 * so that way to the bubbles stays. Off with user/shell-idle.off. */
static int idle_ticks;
static void idle_tick(void) {
    SceInt32 ids[8];
    if (sceKernelGetSystemTimeWide() < 60ull * 1000 * 1000) return;      /* power-on is boot_start's job */
    /* 0.5b: a menu whose game has gone (ended from outside while the menu was
     * up) would sit there for ever and keep VitaOS away: close it after 3 s. */
    static int orphan_ticks;
    if (app_running(MENU_TID) && !shell_off()) {
        char t[16];
        SceUID p;
        int h;
        if (running_game(t, &p, &h) || (menu_tid[0] && still_alive(menu_tid, menu_pid)) || (armed[0] && still_alive(armed, armed_pid))) orphan_ticks = 0;
        else if (++orphan_ticks == 8) {                  /* 2 s: ask it to go (it checks menu.quit twice a second) */
            SceUID fd = sceIoOpen("ux0:data/arcadehub/menu.quit", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
            if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
            slog("the menu's game is gone: asking the menu to close", menu_tid, 0);
        } else if (orphan_ticks >= 24) {                 /* 6 s and still there: end it */
            orphan_ticks = 8;
            slog("the menu did not close: ending it", menu_tid, sceAppMgrDestroyAppByName(MENU_TID));
        }
    } else orphan_ticks = 0;
    static int idle_fails;
    if (sceAppMgrGetRunningAppIdListForShell(ids, 8) > 0 || vitaos_running() || app_running(MENU_TID)) { idle_ticks = 0; idle_fails = 0; return; }
    /* 0.6a: VitaOS has just started a game (its launch flag is under a minute
     * old): a slow start is not "nothing running". */
    if (home_launch_at && sceKernelGetSystemTimeWide() - home_launch_at < 60ull * 1000 * 1000) { idle_ticks = 0; return; }
    /* A game suspended behind LiveArea is not in that list (it closed one
     * that way on red, 2026-11-12): the game return_tick is following counts. */
    if (armed[0] && still_alive(armed, armed_pid)) { idle_ticks = 0; return; }
    if (++idle_ticks < 40) return;                       /* 10 s (5 s raced slow launches) */
    idle_ticks = 0;
    /* Three tries that did not bring VitaOS up: something is in the way (a
     * system question on screen, such as "The following application will
     * close"). Asking again every few seconds only stacks more of them (red,
     * 2026-11-13: 25 tries in five minutes). Wait until something changes. */
    if (idle_fails >= 3) return;
    const char *home = home_tid();
    if (!home || shell_off() || file_exists(USER "shell-idle.off") || file_exists("ux0:data/arcadehub/deploy.lock")) return;
    slog("nothing running, opening", home, idle_fails);
    launch_home(home);
    if (vitaos_running()) idle_fails = 0;
    else if (++idle_fails == 3) slog("VitaOS did not open after three tries: waiting for something to change", home, 0);
}

static int watch(SceSize args, void *argp) {
    (void)args; (void)argp;
    unsigned int was = 0, loops = 0;
    SceUInt64 last_ts = 0;
    SceCtrlData pads[16];
    while (running) {
        sceKernelDelayThread(50 * 1000);
        if (++loops % (TICK_MS / 50) == 0) { return_tick(); home_req_tick(); idle_tick(); ps_lock_tick(); }
        int n = sceCtrlPeekBufferPositive2(0, pads, 16);
        if (n <= 0) continue;
        unsigned int down = 0;
        int fresh = 0;
        for (int i = 0; i < n; ++i) {
            if (pads[i].timeStamp <= last_ts) continue;
            last_ts = pads[i].timeStamp;
            down |= pads[i].buttons & 0x10000;
            fresh = 1;
        }
        /* 1.4: timestamps that stop moving forward (another controller plugin
         * rewriting them) must not make PS go dead: use the newest sample. */
        if (!fresh) {
            if (pads[n - 1].timeStamp < last_ts) last_ts = pads[n - 1].timeStamp;
            down = pads[n - 1].buttons & 0x10000;
        }
        if (down && !was && vitaos_running()) {
            SceUID fd = sceIoOpen("ux0:data/arcadehub/ps.tap", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
            if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
        } else if (down && !was) {
            if (app_running(MENU_TID)) {                    /* PS in the menu: it resumes the game */
                SceUID fd = sceIoOpen("ux0:data/arcadehub/menu.ps", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
                if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
            } else if (ps_lock_on) ps_lock_press = sceKernelGetSystemTimeWide();   /* see ps_lock_step */
            else menu_note_press();
        }
        if (down && !was && file_exists(USER "shell-debug")) {
            char t[16] = "-";
            SceUID p = 0;
            int h = 0, g = running_game(t, &p, &h);
            slog("PS edge: game/home/vitaos/menu bits, tid", t, g | h << 1 | vitaos_running() << 2 | app_running(MENU_TID) << 3 | (menu_at ? 16 : 0) | ps_lock_on << 5);
        }
        was = pads[n - 1].buttons & 0x10000;
        ps_lock_step(was != 0);
        menu_step(was != 0);
    }
    return sceKernelExitDeleteThread(0);
}

/* ---------- 1.3: start in VitaOS ----------
 * About 15 s after the Vita starts (the shell has settled), open VitaOS, so the
 * bubbles are only ever seen by choice. Retries for two minutes: a launch
 * behind the lock screen, or one that stops on VitaOS's LiveArea page, does
 * not always take the first time. Off with ux0:data/arcadehub/user/boot.off
 * (Settings > VitaOS > Start at boot); holding L at power-on skips every
 * plugin, this one included. Never over an app that is already running. */
#define BOOT_OFF "ux0:data/arcadehub/user/boot.off"
static SceUID boot_thread = -1;

/* 1.5: what happened at power-on, in ux0:data/arcadehub/boot.log (the last
 * boot only, a few short lines with roughly the seconds since the system started
 * (microseconds >> 20: no 64-bit division without the C runtime)). When
 * "it did not open at power-on" is reported, this says why: switched off,
 * something else already running, or the launches not taking. */
#define BOOT_LOG "ux0:data/arcadehub/boot.log"
static void boot_log(const char *what, const char *tid, int v, int fresh) {
    char line[96];
    int n = sceClibSnprintf(line, sizeof(line), "%3u s  %s %s %08X\n",
                            (unsigned int)(sceKernelGetSystemTimeWide() >> 20), what, tid ? tid : "-", v);
    SceUID fd = sceIoOpen(BOOT_LOG, SCE_O_WRONLY | SCE_O_CREAT | (fresh ? SCE_O_TRUNC : SCE_O_APPEND), 0666);
    if (fd >= 0) { sceIoWrite(fd, line, n); sceIoClose(fd); }
}

static void slog(const char *what, const char *tid, int v) { boot_log(what, tid, v, 0); }

/* ---------- no lock screen ----------
 * NoLockScreen's patches: at these two places SceShell decides whether to
 * show the lock screen; "movs r0, #1; nop" makes it skip it. Offsets are for
 * the 3.65-3.73 retail SceShell only, so the module NID is checked first.
 * NOTE: this also skips a passcode. user/lockscreen.keep keeps the lock screen.
 *
 * patch_guard: boot.patching is written before the patches and removed once
 * the system has come up. If it is still there at the next start, the last
 * boot did not get past the patches, and they are left out from then on
 * (until the file is deleted). So a bad patch costs one failed boot, not a
 * boot loop. */
#define GUARD "ux0:data/arcadehub/boot.patching"
#define SHELL_NID_365 0x5549BF1F
static SceUID inj[2] = {-1, -1};

static void patch_guard_clear(void) { sceIoRemove(GUARD); }

static void lock_patch(void) {
    if (file_exists(USER "shell.off") || file_exists(USER "lockscreen.keep")) { boot_log("lock screen kept (switch)", 0, 0, 0); return; }
    if (file_exists(GUARD)) { boot_log("lock screen kept: the last boot stopped at the patches", 0, 0, 0); return; }
    tai_module_info_t info;
    sceClibMemset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (taiGetModuleInfo("SceShell", &info) < 0) { boot_log("lock screen kept: no SceShell info", 0, 0, 0); return; }
    if (info.module_nid != SHELL_NID_365) { boot_log("lock screen kept: unknown SceShell", 0, (int)info.module_nid, 0); return; }
    /* And the code itself has to look right at both places (the checks are
     * from devnoname120's NoLockScreen, GPL-3.0): a Thumb BL, with the
     * compares it sits between. */
    SceKernelModuleInfo mi;
    sceClibMemset(&mi, 0, sizeof(mi));
    mi.size = sizeof(mi);
    if (sceKernelGetModuleInfo(info.modid, &mi) < 0 || !mi.segments[0].vaddr || mi.segments[0].memsz < 0x236280) {
        boot_log("lock screen kept: no SceShell segment", 0, 0, 0);
        return;
    }
    const unsigned char *t = (const unsigned char *)mi.segments[0].vaddr;
    const unsigned char *a = t + 0x23556E, *b = t + 0x23626E;
    int ok_a = a[-4] == 0x10 && a[-3] == 0xF0 && a[-2] == 0x7F && a[-1] == 0x05 && a[4] == 0x01 && a[5] == 0x28 && a[7] == 0xD0;
    int ok_b = b[-4] == 0x00 && b[-3] == 0x28 && b[-2] == 0x0B && b[-1] == 0xD1 && b[4] == 0x01 && b[5] == 0x28 && b[6] == 0x09 && b[7] == 0xD1;
    int bl_a = (a[1] & 0xF8) == 0xF0 && (a[3] & 0xD0) == 0xC0, bl_b = (b[1] & 0xF8) == 0xF0 && (b[3] & 0xD0) == 0xC0;
    if (!(ok_a && ok_b && bl_a && bl_b)) {
        boot_log("lock screen kept: the code there is not what was expected", 0, ok_a | ok_b << 1 | bl_a << 2 | bl_b << 3, 0);
        return;
    }
    SceUID fd = sceIoOpen(GUARD, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
    static const unsigned int skip = 0xBF002001;      /* movs r0, #1 ; nop */
    inj[0] = taiInjectData(info.modid, 0, 0x23556E, &skip, sizeof(skip));
    inj[1] = taiInjectData(info.modid, 0, 0x23626E, &skip, sizeof(skip));
    boot_log("lock screen patches", 0, inj[0] < 0 ? inj[0] : inj[1], 0);
}

static int boot_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    boot_log("vitaos_shell 0.9a loaded", 0, 0, 1);
    lock_patch();
    /* Open VitaOS when the system's start-up app (NPXS10079) has come and
     * gone, as AutoBoot does; 15 s at most, 4 s at least. */
    /* Not earlier than that: a launch at second 8 left red on a black screen
     * until it was restarted with L held (2026-11-12). The start-up app has
     * to be done first. */
    int seen = 0;
    for (int i = 0; i < 60 && running; ++i) {
        sceKernelDelayThread(250 * 1000);
        int up = app_running("NPXS10079");
        if (up) seen = 1;
        if (i >= 16 && seen && !up) break;
    }
    boot_log(seen ? "start-up app done" : "start-up app never seen", 0, 0, 0);
    patch_guard_clear();
    if (file_exists(USER "shell.off")) { boot_log("shell.off", 0, 0, 0); return sceKernelExitDeleteThread(0); }   /* the file only: a stale deploy.lock must not keep VitaOS from opening */
    for (int attempt = 0; attempt < 12 && running; ++attempt) {
        char t[16] = "";
        SceUID p = -1;
        int h_up = 0;
        if (file_exists(BOOT_OFF)) { boot_log("off in Settings", 0, attempt, 0); break; }
        if (vitaos_running()) { boot_log("VitaOS already running", 0, attempt, 0); break; }
        if (running_game(t, &p, &h_up)) { boot_log("another app is running:", t, attempt, 0); break; }
        if (bridge_loaded()) { boot_log("agent bridge loaded: it opens Home itself", 0, attempt, 0); break; }
        const char *h = home_tid();
        if (!h) { boot_log("VitaOS is not installed", 0, attempt, 0); break; }
        boot_log("launching", h, attempt, 0);
        launch_home(h);
        if (vitaos_running()) { boot_log("VitaOS is up:", app_running("VITAOS001") ? "VITAOS001" : "MVZA00010", attempt, 0); break; }
        if (running_game(t, &p, &h_up)) boot_log("launch did not take; now running:", t, attempt, 0);
        else boot_log("launch did not take (lock screen?)", 0, attempt, 0);
        sceKernelDelayThread(10 * 1000 * 1000);
    }
    return sceKernelExitDeleteThread(0);
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    thread = sceKernelCreateThread("vitaos_shell", watch, 0x10000100, 0x2000, 0, 0, NULL);
    if (thread >= 0) sceKernelStartThread(thread, 0, NULL);
    boot_thread = sceKernelCreateThread("vitaos_boot", boot_start, 0x10000100, 0x4000, 0, 0, NULL);
    if (boot_thread >= 0) sceKernelStartThread(boot_thread, 0, NULL);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    running = 0;
    ps_lock_set(0);
    for (int i = 0; i < 2; ++i) if (inj[i] >= 0) taiInjectRelease(inj[i]);
    if (thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(thread, NULL, &timeout); }
    if (boot_thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(boot_thread, NULL, &timeout); }
    return SCE_KERNEL_STOP_SUCCESS;
}
