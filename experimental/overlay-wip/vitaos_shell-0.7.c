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
#include <psp2/avconfig.h>
#include <psp2/registrymgr.h>

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

/* ---------- 0.3: PS in a game never shows LiveArea ----------
 * The shell reads the controller like any program. While a game is in front
 * (ps_armed), these hooks on the shell's own controller reads take the PS bit
 * out of a press for its first half second. A short press is then never seen
 * by the shell, so it does not switch to LiveArea, and the menu is opened
 * straight over the game. A press held longer is let through: that is the
 * system's quick menu. This plugin's own reads are not hooked.
 * If a hidden press does not get the menu up, ps_armed stays off until the
 * next start (ps_failed), so PS can never go dead.
 * NOT WORKING YET, and off by default: see ctrl_hooks. */
#define CTRL_LIB 0xD197E3C7
#define NCTRL 12
static const unsigned int ctrl_nid[NCTRL] = {
    0xA9C3CED6, 0x15F81E8C, 0xA59454D3, 0x860BF292,      /* PeekBufferPositive, 2, Ext, Ext2 */
    0x67E7AB83, 0xC4226A3E, 0xE2D99296, 0xA7178860,      /* ReadBufferPositive, 2, Ext, Ext2 */
    0x104ED1A7, 0x81A89660, 0x15F96FB0, 0x27A0C5FB,      /* Peek/ReadBufferNegative, 2 */
};
#define NHOOK (NCTRL * 2)                                /* SceShell's imports, then ScePaf's */
static SceUID ctrl_uid[NHOOK];
static tai_hook_ref_t ctrl_ref[NHOOK];
static volatile int ps_armed, ps_failed;
static volatile unsigned int ps_hidden;                  /* presses hidden so far (a counter the watcher follows) */
static volatile SceUInt64 ps_since;
static volatile unsigned int ctrl_seen;                  /* bit i: hook i saw PS at least once (for the log) */

static void ctrl_filter(int i, int ret, SceCtrlData *d, int negative) {
    if (ret <= 0 || !d) return;
    for (int k = 0; k < ret && k < 64; ++k) {
        int down = negative ? !(d[k].buttons & 0x10000) : (d[k].buttons & 0x10000) != 0;
        if (!down) continue;
        ctrl_seen |= 1u << i;
        if (!ps_armed) continue;
        SceUInt64 now = sceKernelGetSystemTimeWide();
        if (!ps_since) { ps_since = now; ++ps_hidden; }
        if (now - ps_since < 500 * 1000) { if (negative) d[k].buttons |= 0x10000; else d[k].buttons &= ~0x10000u; }
    }
}
#define CTRL_HOOK(i) \
    static int ctrl_hook_##i(int port, SceCtrlData *d, int count) { \
        int ret = TAI_CONTINUE(int, ctrl_ref[i], port, d, count); \
        ctrl_filter(i, ret, d, (i % NCTRL) >= 8); \
        return ret; \
    }
CTRL_HOOK(0) CTRL_HOOK(1) CTRL_HOOK(2) CTRL_HOOK(3) CTRL_HOOK(4) CTRL_HOOK(5) CTRL_HOOK(6) CTRL_HOOK(7)
CTRL_HOOK(8) CTRL_HOOK(9) CTRL_HOOK(10) CTRL_HOOK(11) CTRL_HOOK(12) CTRL_HOOK(13) CTRL_HOOK(14) CTRL_HOOK(15)
CTRL_HOOK(16) CTRL_HOOK(17) CTRL_HOOK(18) CTRL_HOOK(19) CTRL_HOOK(20) CTRL_HOOK(21) CTRL_HOOK(22) CTRL_HOOK(23)
static const void *const ctrl_fn[NHOOK] = {
    ctrl_hook_0, ctrl_hook_1, ctrl_hook_2, ctrl_hook_3, ctrl_hook_4, ctrl_hook_5, ctrl_hook_6, ctrl_hook_7,
    ctrl_hook_8, ctrl_hook_9, ctrl_hook_10, ctrl_hook_11, ctrl_hook_12, ctrl_hook_13, ctrl_hook_14, ctrl_hook_15,
    ctrl_hook_16, ctrl_hook_17, ctrl_hook_18, ctrl_hook_19, ctrl_hook_20, ctrl_hook_21, ctrl_hook_22, ctrl_hook_23,
};

static void ctrl_hooks(void) {
    unsigned int ok = 0;
    /* OFF unless user/ps-hide.on exists. On red (2026-11-12) these hooks, even
     * when they changed nothing, made a PS press end the game instead of
     * suspending it; why is not known yet. Kept for the next attempt. */
    if (!file_exists(USER "ps-hide.on")) { slog("controller hooks: off", 0, 0); return; }
    for (int i = 0; i < NHOOK; ++i) {
        ctrl_uid[i] = taiHookFunctionImport(&ctrl_ref[i], i < NCTRL ? "SceShell" : "ScePaf", CTRL_LIB, ctrl_nid[i % NCTRL], ctrl_fn[i]);
        if (ctrl_uid[i] >= 0) ok |= 1u << i;
    }
    slog("controller hooks (bits: 0-11 SceShell, 12-23 ScePaf)", 0, (int)ok);
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

/* Every 250 ms: is a game in front of a shell that should not see PS? */
static void ps_arm_tick(void) {
    char t[16];
    SceUID p;
    int h;
    ps_armed = !ps_failed && !shell_off() && !file_exists(USER "menu.off") && file_exists(USER "ps-hide.on")
               && file_exists("ux0:app/" MENU_TID "/eboot.bin") && !app_running(MENU_TID) && running_game(t, &p, &h) && !h;
}

/* Every 50 ms. A hidden short press opens the menu at once; a held one was
 * let through to the shell for its quick menu. */
static void ps_hide_step(int ps_down) {
    static unsigned int done;
    static SceUInt64 opened_at;
    static int saw_down;
    if (ps_down) saw_down = 1;
    else if (saw_down) {                                   /* a whole press has gone by: this thread saw it go down and up */
        saw_down = 0;
        SceUInt64 since = ps_since;
        ps_since = 0;
        if (ps_hidden != done) {
            done = ps_hidden;
            if (since && sceKernelGetSystemTimeWide() - since < 500 * 1000 && !app_running(MENU_TID)) {
                int h;
                menu_at = 0;                                   /* not the LiveArea way as well */
                if (running_game(menu_tid, &menu_pid, &h)) { menu_open(); opened_at = sceKernelGetSystemTimeWide(); }
            }
        }
    } else if (ps_since && sceKernelGetSystemTimeWide() - ps_since > 2000 * 1000) ps_since = 0;   /* a press this thread missed */
    if (opened_at && sceKernelGetSystemTimeWide() - opened_at > 3000 * 1000) {
        opened_at = 0;
        if (!app_running(MENU_TID)) {                          /* it did not come up: never hide PS again this boot */
            ps_failed = 1;
            ps_armed = 0;
            slog("hidden PS did not open the menu: PS is the shell's again", menu_tid, (int)ctrl_seen);
        } else slog("menu over the game, no LiveArea", menu_tid, (int)ctrl_seen);
    }
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

/* ---------- 0.4: the kernel half (experimental/kernel/vitaos_k.c) ----------
 * With vitaos_k loaded, a short PS press over a game never reaches the shell
 * (no LiveArea); it is counted there instead, and the menu is opened here as
 * soon as the count moves. Loaded at run time, 30 s after power-on, only with
 * user/kernel.on, and never again after a load that did not finish
 * (boot.kloading is still there at the next start): delete that file to try
 * again. The module is ux0:data/arcadehub/vitaos_k.skprx. */
#define VK_PORT 0x564B
#define VK_FILE "ux0:data/arcadehub/vitaos_k.skprx"
#define VK_GUARD "ux0:data/arcadehub/boot.kloading"
static int vk_state;                       /* 0 not tried, 1 loaded, -1 not available */
static SceUInt64 vk_loaded_at, vk_opened_at;
static unsigned int vk_seen;

static void kernel_load(void) {
    if (vk_state || sceKernelGetSystemTimeWide() < 30ull * 1000 * 1000) return;
    vk_state = -1;
    if (shell_off() || !file_exists(USER "kernel.on") || !file_exists(VK_FILE)) { slog("kernel half: off", 0, 0); return; }
    if (file_exists(VK_GUARD)) { slog("kernel half: not loaded, the last load did not finish", 0, 0); return; }
    SceUID fd = sceIoOpen(VK_GUARD, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
    int rc = taiLoadStartKernelModule(VK_FILE, 0, NULL, 0);
    slog("kernel half: load", 0, rc);
    if (rc < 0) { sceIoRemove(VK_GUARD); return; }
    int c = sceCtrlPeekBufferPositive2(VK_PORT, NULL, 0);
    if (c < 0) { slog("kernel half: loaded but not answering", 0, c); sceIoRemove(VK_GUARD); return; }
    vk_seen = (unsigned int)c;
    vk_loaded_at = sceKernelGetSystemTimeWide();
    vk_state = 1;
}

/* ---------- 0.7: the overlay (vitaos_k 0.2) ----------
 * With user/overlay.on, a short PS over a game does not open the menu app:
 * the kernel half draws a card straight into the game's picture, so there is
 * no app switch and none of the shell's animations. This side runs it: which
 * row, the levels, what X does. The game keeps running underneath, without
 * buttons. If the card is never drawn (a game whose picture the kernel half
 * will not write into), it closes and the menu app opens as before, and the
 * overlay stays off until the next start. boot.overlay marks "the card is
 * up"; found at a start, it means the console went down with it up, and the
 * overlay is then left off until that file is deleted. */
#define OV_GUARD "ux0:data/arcadehub/boot.overlay"
#define BTN_UP 0x10
#define BTN_RIGHT 0x20
#define BTN_DOWN 0x40
#define BTN_LEFT 0x80
#define BTN_CIRCLE 0x2000
#define BTN_CROSS 0x4000
#define BRIGHT_MIN 21
#define BRIGHT_MAX 65536
static int ov_up, ov_sel, ov_vol, ov_bright, ov_off = -1;
static unsigned int ov_prev;
static SceUInt64 ov_since;
static char ov_tid[16];
static SceUID ov_pid;

static int overlay_wanted(void) {
    if (ov_off < 0) {
        ov_off = file_exists(OV_GUARD);
        if (ov_off) slog("overlay: off, the console went down with it up (delete boot.overlay to try again)", 0, 0);
    }
    return !ov_off && file_exists(USER "overlay.on");
}

/* Brightness as 0..100 for the card (32-bit division only: no C runtime here). */
static int ov_pct(void) {
    int pct = (ov_bright - BRIGHT_MIN) / 655;
    return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}

static void overlay_send(void) {
    sceCtrlPeekBufferPositive2(VK_PORT + 1, NULL, 1 | ov_sel << 1 | ov_vol << 8 | ov_pct() << 16);
}

static void overlay_shut(const char *why) {
    if (!ov_up) return;
    ov_up = 0;
    /* Buttons up first (half a second at most), or the game sees the X or O
     * that closed the card. */
    for (int i = 0; i < 20; ++i) {
        int held = sceCtrlPeekBufferPositive2(VK_PORT + 3, NULL, 0);
        if (held < 0 || !(held & 0xFFFF)) break;
        sceKernelDelayThread(25 * 1000);
    }
    sceCtrlPeekBufferPositive2(VK_PORT + 2, NULL, 0);
    sceIoRemove(OV_GUARD);
    if (why) slog(why, ov_tid, 0);
}

static int overlay_show(const char *tid, SceUID pid) {
    SceUID fd = sceIoOpen(OV_GUARD, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }
    int rc = sceCtrlPeekBufferPositive2(VK_PORT + 2, NULL, (int)pid);
    if (rc < 0) { sceIoRemove(OV_GUARD); slog("overlay: the kernel half would not open it", tid, rc); return rc; }
    sceClibStrncpy(ov_tid, tid, 15);
    ov_tid[15] = 0;
    ov_pid = pid;
    ov_sel = 0;
    if (sceAVConfigGetSystemVol(&ov_vol) < 0) ov_vol = 15;
    if (sceRegMgrGetKeyInt("/CONFIG/DISPLAY", "brightness", &ov_bright) < 0) ov_bright = BRIGHT_MAX / 2;
    ov_prev = 0xFFFFFFFF;                        /* nothing counts as newly pressed on the first look */
    ov_since = sceKernelGetSystemTimeWide();
    ov_up = 1;
    overlay_send();
    slog("overlay over", tid, 0);
    return 0;
}

/* Every 50 ms while the card is up. Returns 1 when the menu app should open
 * in its place (the card could not be drawn). */
static int overlay_step(void) {
    if (!still_alive(ov_tid, ov_pid)) { overlay_shut("overlay: the game is gone"); return 0; }
    int drawn = sceCtrlPeekBufferPositive2(VK_PORT + 1, NULL, 1 | ov_sel << 1 | ov_vol << 8 |
                    ov_pct() << 16);
    if (drawn <= 0 && sceKernelGetSystemTimeWide() - ov_since > 1500 * 1000) {
        overlay_shut("overlay: never drawn over this game; the menu app from here on");
        ov_off = 1;
        return 1;
    }
    int b = sceCtrlPeekBufferPositive2(VK_PORT + 3, NULL, 0);
    if (b < 0) return 0;
    unsigned int now = (unsigned int)b, down = now & ~ov_prev;
    ov_prev = now;
    if (down & BTN_UP) ov_sel = ov_sel > 0 ? ov_sel - 1 : 0;
    if (down & BTN_DOWN) ov_sel = ov_sel < 3 ? ov_sel + 1 : 3;
    int dir = (down & BTN_RIGHT) ? 1 : (down & BTN_LEFT) ? -1 : 0;
    if (dir && ov_sel == 1) {
        ov_vol += dir;
        if (ov_vol < 0) ov_vol = 0;
        if (ov_vol > 30) ov_vol = 30;
        sceAVConfigSetSystemVol(ov_vol);
    } else if (dir && ov_sel == 2) {
        ov_bright += dir * (BRIGHT_MAX / 16);
        if (ov_bright < BRIGHT_MIN) ov_bright = BRIGHT_MIN;
        if (ov_bright > BRIGHT_MAX) ov_bright = BRIGHT_MAX;
        sceAVConfigSetDisplayBrightness(ov_bright);
        sceRegMgrSetKeyInt("/CONFIG/DISPLAY", "brightness", ov_bright);
    }
    if (down & BTN_CIRCLE) overlay_shut("overlay: closed");
    else if ((down & BTN_CROSS) && ov_sel == 0) overlay_shut("overlay: resume");
    else if ((down & BTN_CROSS) && ov_sel == 3) {
        overlay_shut("overlay: close game");
        slog("overlay: ending", ov_tid, sceAppMgrDestroyAppByName(ov_tid));
    } else if (down) overlay_send();
    return 0;
}

/* Every 50 ms. */
static void kernel_step(void) {
    kernel_load();
    if (vk_state != 1) return;
    /* It has run for 10 s: the load is good. (30 s at first: a restart 12 s
     * after the load then left the guard behind and the module was not
     * loaded again, 2026-11-13.) */
    if (vk_loaded_at && sceKernelGetSystemTimeWide() - vk_loaded_at > 10ull * 1000 * 1000) {
        vk_loaded_at = 0;
        sceIoRemove(VK_GUARD);
    }
    static char t[16];
    static SceUID p;
    static int arm, n;
    int h;
    if (n++ % 5 == 0)                                      /* every 250 ms: is a game in front? */
        arm = !ps_failed && !shell_off() && !file_exists(USER "menu.off") && file_exists("ux0:app/" MENU_TID "/eboot.bin")
              && !app_running(MENU_TID) && running_game(t, &p, &h) && !h;
    int c = sceCtrlPeekBufferPositive2(VK_PORT, NULL, arm);
    if (c < 0) return;
    int fallback = 0;
    if (ov_up) {
        if ((unsigned int)c != vk_seen) { vk_seen = (unsigned int)c; overlay_shut("overlay: closed with PS"); return; }
        fallback = overlay_step();
        if (!fallback) return;
        running_game(t, &p, &h);                           /* the menu app instead, over the same game */
    }
    if ((unsigned int)c != vk_seen || fallback) {          /* a short press was hidden from the shell */
        vk_seen = (unsigned int)c;
        if (!fallback && arm && !app_running(MENU_TID) && overlay_wanted() && overlay_show(t, p) >= 0) return;
        if ((arm || fallback) && !app_running(MENU_TID)) {
            sceClibStrncpy(menu_tid, t, 15);
            menu_tid[15] = 0;
            menu_pid = p;
            menu_at = 0;
            arm = 0;
            sceCtrlPeekBufferPositive2(VK_PORT, NULL, 0);  /* the menu holds PS from here */
            menu_open();
            vk_opened_at = sceKernelGetSystemTimeWide();
        }
    }
    if (vk_opened_at && sceKernelGetSystemTimeWide() - vk_opened_at > 3000 * 1000) {
        vk_opened_at = 0;
        if (!app_running(MENU_TID)) {                      /* the menu did not come up: PS is the shell's again */
            ps_failed = 1;
            sceCtrlPeekBufferPositive2(VK_PORT, NULL, 0);
            slog("hidden PS did not open the menu: PS is the shell's again", menu_tid, 0);
        } else slog("menu over the game, no LiveArea", menu_tid, 0);
    }
}

static int watch(SceSize args, void *argp) {
    (void)args; (void)argp;
    unsigned int was = 0, loops = 0;
    SceUInt64 last_ts = 0;
    SceCtrlData pads[16];
    while (running) {
        sceKernelDelayThread(50 * 1000);
        if (++loops % (TICK_MS / 50) == 0) { return_tick(); home_req_tick(); idle_tick(); ps_arm_tick(); }
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
            } else menu_note_press();
        }
        if (down && !was && file_exists(USER "shell-debug")) {
            char t[16] = "-";
            SceUID p = 0;
            int h = 0, g = running_game(t, &p, &h);
            slog("PS edge: game/home/vitaos/menu bits, tid", t, g | h << 1 | vitaos_running() << 2 | app_running(MENU_TID) << 3 | (menu_at ? 16 : 0) | ps_armed << 5);
        }
        was = pads[n - 1].buttons & 0x10000;
        kernel_step();
        ps_hide_step(was != 0);
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
    boot_log("vitaos_shell 0.7 loaded", 0, 0, 1);
    lock_patch();
    ctrl_hooks();
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
    ps_armed = 0;
    for (int i = 0; i < NHOOK; ++i) if (ctrl_uid[i] > 0) taiHookRelease(ctrl_uid[i], ctrl_ref[i]);
    for (int i = 0; i < 2; ++i) if (inj[i] >= 0) taiInjectRelease(inj[i]);
    if (thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(thread, NULL, &timeout); }
    if (boot_thread >= 0) { SceUInt timeout = 500 * 1000; sceKernelWaitThreadEnd(boot_thread, NULL, &timeout); }
    return SCE_KERNEL_STOP_SUCCESS;
}
