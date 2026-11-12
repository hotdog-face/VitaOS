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
static int shell_off(void) { return file_exists(USER "shell.off"); }

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
    int settle = sceClibStrncmp(armed, "RETROLNCR", 9) ? 6 : 24;
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

static void menu_step(int ps_down) {
    if (!menu_at) return;
    SceUInt64 age = sceKernelGetSystemTimeWide() - menu_at;
    if (ps_down) {
        if (age > 600 * 1000) { slog("PS held: the quick menu", menu_tid, 0); menu_at = 0; }
        return;
    }
    if (age < 400 * 1000) return;
    menu_at = 0;
    if (!still_alive(menu_tid, menu_pid) || !file_exists("ux0:app/" MENU_TID "/eboot.bin")) return;
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

static int watch(SceSize args, void *argp) {
    (void)args; (void)argp;
    unsigned int was = 0, loops = 0;
    SceUInt64 last_ts = 0;
    SceCtrlData pads[16];
    while (running) {
        sceKernelDelayThread(50 * 1000);
        if (++loops % (TICK_MS / 50) == 0) { return_tick(); home_req_tick(); }
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
        } else if (down && !was) menu_note_press();
        was = pads[n - 1].buttons & 0x10000;
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
    if (shell_off() || file_exists(USER "lockscreen.keep")) { boot_log("lock screen kept (switch)", 0, 0, 0); return; }
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
    boot_log("vitaos_shell 0.1 loaded", 0, 0, 1);
    lock_patch();
    /* Open VitaOS when the system's start-up app (NPXS10079) has come and
     * gone, as AutoBoot does; 15 s at most, 4 s at least. */
    int seen = 0;
    for (int i = 0; i < 60 && running; ++i) {
        sceKernelDelayThread(250 * 1000);
        int up = app_running("NPXS10079");
        if (up) seen = 1;
        if (i >= 16 && seen && !up) break;
    }
    boot_log(seen ? "start-up app done" : "start-up app never seen", 0, 0, 0);
    patch_guard_clear();
    if (shell_off()) { boot_log("shell.off", 0, 0, 0); return sceKernelExitDeleteThread(0); }
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
    for (int i = 0; i < 2; ++i) if (inj[i] >= 0) taiInjectRelease(inj[i]);
    if (thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(thread, NULL, &timeout); }
    if (boot_thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(boot_thread, NULL, &timeout); }
    return SCE_KERNEL_STOP_SUCCESS;
}
