/* VitaOS pause menu: a small system-mode app (runs beside a suspended game,
 * like the system Music app) that the bridge opens when PS is pressed in a
 * game. Resume brings the game back; Close game ends it and opens VitaOS.
 *
 * The bridge writes ux0:data/arcadehub/paused.txt ("TITLEID\tTitle") before
 * launching it. System-mode apps get ~74 MB (MEMSIZE 0x12800), so this stays
 * small: no catalog, no players, two fonts and two pictures. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/io/fcntl.h>
#include <psp2/rtc.h>
#include <psp2/power.h>
#include <psp2/avconfig.h>
#include <psp2/shellutil.h>
#include <psp2/io/stat.h>
#include <psp2/registrymgr.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Debug build (2026-09-27): every start-up step goes to this log, so a start
 * that fails in system mode says where. Opened per line: a crash loses nothing. */
#define DLOG "ux0:data/vitaos-pause.log"
static void dlog(const char *fmt, ...) {
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
    line[n++] = '\n';
    SceUID fd = sceIoOpen(DLOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    if (fd >= 0) { sceIoWrite(fd, line, n); sceIoClose(fd); }
}

int _newlib_heap_size_user = 3 * 1024 * 1024;   /* 16 MiB budget in all: vita2d needs ~2 MiB of the rest */

#define W 960
#define H 544
#define PAUSED "ux0:data/arcadehub/paused.txt"
#define HOME_TID "MVZA00010"

/* CDRAM belongs to the suspended game: with one paused, vita2d's CDRAM
 * requests fail and the menu never starts (baseline 2026-09-27: alone, it took
 * all 16 MiB). Send them to our own PHYCONT pool (26 MiB), as VitaShell-sysapp
 * does. Linked with -Wl,--wrap=sceKernelAllocMemBlock. */
SceUID __real_sceKernelAllocMemBlock(const char *, SceKernelMemBlockType, SceSize, SceKernelAllocMemBlockOpt *);
SceUID __wrap_sceKernelAllocMemBlock(const char *name, SceKernelMemBlockType type, SceSize size, SceKernelAllocMemBlockOpt *opt) {
    SceKernelMemBlockType want = type;
    if (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) {
        type = SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW;
        size = (size + 0xFFFFF) & ~0xFFFFFu;             /* PHYCONT comes in 1 MiB pages */
    }
    SceUID uid = __real_sceKernelAllocMemBlock(name, type, size, opt);
    dlog("alloc %s type %08X->%08X size %u rc %08X", name ? name : "?", want, type, size, uid);
    return uid;
}

static vita2d_font *font, *bold;
static char tid[16], title[128];

static int minutes = -1;
static void read_paused(void) {
    char buf[256] = {0};
    SceUID fd = sceIoOpen(PAUSED, SCE_O_RDONLY, 0);
    if (fd >= 0) { sceIoRead(fd, buf, sizeof(buf) - 1); sceIoClose(fd); }
    char *tab = strchr(buf, '\t');
    if (tab) {
        *tab = 0;
        snprintf(tid, sizeof(tid), "%s", buf);
        snprintf(title, sizeof(title), "%s", tab + 1);
        char *nl = strpbrk(title, "\r\n");
        if (nl) *nl = 0;
        char *t2 = strchr(title, '\t');               /* a third field: minutes this game has been open */
        if (t2) { *t2 = 0; minutes = atoi(t2 + 1); }
    }
    if (!title[0]) snprintf(title, sizeof(title), "Your game");
}

static vita2d_texture *art(const char *file) {
    if (!tid[0]) return NULL;
    char p[96];
    snprintf(p, sizeof(p), "ur0:appmeta/%s/%s", tid, file);
    return vita2d_load_PNG_file(p);
}

static void launch_uri(const char *id) {
    char uri[64];
    snprintf(uri, sizeof(uri), "psgm:play?titleid=%s", id);
    sceAppMgrLaunchAppByUri(0x20000, uri);
}

static void rect_round(float x, float y, float w, float h, float r, unsigned int c) {
    vita2d_draw_rectangle(x + r, y, w - 2 * r, h, c);
    vita2d_draw_rectangle(x, y + r, r, h - 2 * r, c);
    vita2d_draw_rectangle(x + w - r, y + r, r, h - 2 * r, c);
    vita2d_draw_fill_circle(x + r, y + r, r, c);
    vita2d_draw_fill_circle(x + w - r, y + r, r, c);
    vita2d_draw_fill_circle(x + r, y + h - r, r, c);
    vita2d_draw_fill_circle(x + w - r, y + h - r, r, c);
}

enum { B_RESUME, B_VOLUME, B_BRIGHT, B_CLOSE, NB };
static const char *const LABEL[NB] = {"Resume", "Volume", "Brightness", "Close game"};
static const char *const SUB[NB] = {"Back to where you were", NULL, NULL, "Quit and go to VitaOS"};
#define BX 520
#define BY0 120
#define BW 380
#define BH 74
#define BGAP 14

/* The same values the system's own sliders use (see src/settings.c). */
#define BRIGHT_MIN 21
#define BRIGHT_MAX 65536
#define VOL_MAX 30
static int volume = VOL_MAX / 2, brightness = BRIGHT_MAX / 2;

static void levels_read(void) {
    if (sceAVConfigGetSystemVol(&volume) < 0) volume = VOL_MAX / 2;
    if (sceRegMgrGetKeyInt("/CONFIG/DISPLAY", "brightness", &brightness) < 0) brightness = BRIGHT_MAX / 2;
}
static void set_volume(int v) {
    volume = v < 0 ? 0 : v > VOL_MAX ? VOL_MAX : v;
    sceAVConfigSetSystemVol(volume);
}
static void set_brightness(int v) {
    brightness = v < BRIGHT_MIN ? BRIGHT_MIN : v > BRIGHT_MAX ? BRIGHT_MAX : v;
    sceAVConfigSetDisplayBrightness(brightness);
    sceRegMgrSetKeyInt("/CONFIG/DISPLAY", "brightness", brightness);
}
static float level(int i) {
    return i == B_VOLUME ? (float)volume / VOL_MAX : (float)(brightness - BRIGHT_MIN) / (BRIGHT_MAX - BRIGHT_MIN);
}
static void nudge(int i, int dir) {
    if (i == B_VOLUME) set_volume(volume + dir);
    else if (i == B_BRIGHT) set_brightness(brightness + dir * (BRIGHT_MAX / 16));
}

int main(void) {
    dlog("--- start, thread cpu mask %08X prio %08X", sceKernelGetThreadCpuAffinityMask(0), sceKernelGetThreadCurrentPriority());
    SceAppMgrBudgetInfo b;
    memset(&b, 0, sizeof(b));
    b.size = sizeof(b);
    int brc = sceAppMgrGetBudgetInfo(&b);
    dlog("budget rc %08X mode %d total_rw %u free_rw %u phycont %u free_phycont %u cdram %u free_cdram %u", brc,
         b.app_mode, b.total_user_rw_mem, b.free_user_rw, b.total_phycont_mem, b.free_phycont_mem, b.total_cdram_mem, b.free_cdram_mem);
    read_paused();
    levels_read();
    dlog("paused '%s' '%s'", tid, title);
    int vrc = vita2d_init();
    dlog("vita2d_init %d", vrc);
    vita2d_set_clear_color(RGBA8(10, 12, 30, 255));
    font = vita2d_load_font_file("app0:assets/Inter-Regular.ttf");
    bold = vita2d_load_font_file("app0:assets/Inter-Bold.ttf");
    dlog("fonts %p %p", (void *)font, (void *)bold);
    /* No full-screen pic0: a 960x544 texture is 2 MiB, more than the 16 MiB budget spares. */
    vita2d_texture *bg = NULL, *icon = art("icon0.png");
    dlog("art %p %p", (void *)bg, (void *)icon);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

    /* PS belongs to the menu while it is open: the shell leaves the press
     * alone (no LiveArea behind the menu), and vitaos_shell, which does see
     * it, leaves menu.ps for us. PS then means Resume, so PS opens the menu
     * and PS closes it. */
    #define MENU_PS "ux0:data/arcadehub/menu.ps"
    #define MENU_QUIT "ux0:data/arcadehub/menu.quit"     /* vitaos_shell: the game behind this menu has gone */
    sceIoRemove(MENU_PS);
    sceIoRemove(MENU_QUIT);
    int quit = 0;
    sceShellUtilInitEvents(0);
    int lrc = sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
    dlog("ps lock %08X", lrc);

    int sel = 0, act = -1, frame = 0;
    unsigned int prev = 0;
    int was_touch = 0, tx = 0, ty = 0;
    while (act < 0) {
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int down = pad.buttons & ~prev;
        prev = pad.buttons;
        if (frame > 10) {                                   /* ignore the PS press that opened us */
            if (down & SCE_CTRL_UP) sel = sel > 0 ? sel - 1 : 0;
            if (down & SCE_CTRL_DOWN) sel = sel < NB - 1 ? sel + 1 : NB - 1;
            if ((down & SCE_CTRL_CROSS) && SUB[sel]) act = sel;
            if (down & SCE_CTRL_LEFT) nudge(sel, -1);
            if (down & SCE_CTRL_RIGHT) nudge(sel, 1);
            if (down & SCE_CTRL_CIRCLE) act = B_RESUME;
        }
        if (frame > 20 && frame % 6 == 0) {
            SceIoStat st;
            if (sceIoGetstat(MENU_PS, &st) >= 0) { sceIoRemove(MENU_PS); act = B_RESUME; }
            if (frame % 30 == 0 && sceIoGetstat(MENU_QUIT, &st) >= 0) { sceIoRemove(MENU_QUIT); quit = 1; act = B_RESUME; }
        }
        SceTouchData t;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1);
        if (t.reportNum) { was_touch = 1; tx = t.report[0].x / 2; ty = t.report[0].y / 2; }
        else if (was_touch) {                               /* on release: a tap */
            was_touch = 0;
            for (int i = 0; i < NB; ++i)
                if (tx >= BX && tx < BX + BW && ty >= BY0 + i * (BH + BGAP) && ty < BY0 + i * (BH + BGAP) + BH) {
                    if (SUB[i]) act = i;
                    else {                                  /* a slider: tap where the level should be */
                        float f = (float)(tx - BX - 24) / (BW - 48);
                        f = f < 0 ? 0 : f > 1 ? 1 : f;
                        sel = i;
                        if (i == B_VOLUME) set_volume((int)(f * VOL_MAX + 0.5f));
                        else set_brightness(BRIGHT_MIN + (int)(f * (BRIGHT_MAX - BRIGHT_MIN)));
                    }
                }
        }

        vita2d_start_drawing();
        vita2d_clear_screen();
        if (bg) {
            float sx = (float)W / vita2d_texture_get_width(bg), sy = (float)H / vita2d_texture_get_height(bg);
            vita2d_draw_texture_scale(bg, 0, 0, sx, sy);
        }
        vita2d_draw_rectangle(0, 0, W, H, RGBA8(10, 12, 30, 200));   /* dim the game behind */

        SceDateTime now;
        sceRtcGetCurrentClockLocalTime(&now);
        char clock[32];
        snprintf(clock, sizeof(clock), "%d:%02d   %d%%", now.hour, now.minute, scePowerGetBatteryLifePercent());
        (void)clock;                                        /* the system's own bar is drawn over the top 32 px */
        vita2d_font_draw_text(bold, 60, 96, RGBA8(139, 162, 255, 255), 18, "VitaOS");

        if (icon) vita2d_draw_texture_scale(icon, 60, 200, 1.5f, 1.5f);
        vita2d_font_draw_text(bold, 60, 150, RGBA8(170, 180, 210, 255), 16, "PAUSED");
        vita2d_font_draw_text(bold, 60, 420, RGBA8(255, 255, 255, 255), 28, title);
        if (minutes >= 0) {
            char played[48];
            if (minutes < 1) snprintf(played, sizeof(played), "Just started");
            else if (minutes < 60) snprintf(played, sizeof(played), "Playing for %d min", minutes);
            else snprintf(played, sizeof(played), "Playing for %d h %d min", minutes / 60, minutes % 60);
            vita2d_font_draw_text(font, 60, 450, RGBA8(170, 180, 210, 255), 16, played);
        }

        for (int i = 0; i < NB; ++i) {
            float y = BY0 + i * (BH + BGAP);
            int on = i == sel;
            if (on) rect_round(BX - 4, y - 4, BW + 8, BH + 8, 18, RGBA8(139, 162, 255, 255));
            rect_round(BX, y, BW, BH, 14, on ? RGBA8(40, 46, 86, 255) : RGBA8(26, 30, 58, 235));
            vita2d_font_draw_text(bold, BX + 24, (int)y + 34, RGBA8(255, 255, 255, 255), 22, LABEL[i]);
            if (SUB[i]) vita2d_font_draw_text(font, BX + 24, (int)y + 58, RGBA8(170, 180, 210, 255), 15, SUB[i]);
            else {
                vita2d_draw_rectangle(BX + 24, y + 50, BW - 48, 6, RGBA8(70, 78, 120, 255));
                vita2d_draw_rectangle(BX + 24, y + 50, (BW - 48) * level(i), 6, RGBA8(139, 162, 255, 255));
            }
        }
        vita2d_font_draw_text(font, 48, H - 24, RGBA8(140, 150, 180, 255), 15, "X select    left/right change    O or PS resume");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        if (frame == 0 || frame == 60) dlog("frame %d drawn", frame);
        ++frame;
    }

    dlog("action %d", act);
    if (quit || act == B_CLOSE) {
        /* Nothing is launched from here on these two ways out, so nothing makes
         * the shell redraw: leave an empty, see-through picture behind, not the
         * menu's last frame (it stayed in the shell's layer on red, 2026-11-13). */
        vita2d_set_clear_color(RGBA8(0, 0, 0, 0));
        for (int i = 0; i < 3; ++i) { vita2d_start_drawing(); vita2d_clear_screen(); vita2d_end_drawing(); vita2d_swap_buffers(); }
    }
    sceShellUtilUnlock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
    if (act == B_CLOSE) {
        sceAppMgrDestroyOtherApp();                 /* the suspended game */
        SceUID fd = sceIoOpen("ux0:data/arcadehub/home.req", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (fd >= 0) { sceIoWrite(fd, "1", 1); sceIoClose(fd); }   /* vitaos_shell opens Home once we are gone */
        sceKernelDelayThread(250 * 1000);
        /* No launch from here: from a system-mode app both 0x20000 and
         * 0xFFFFF stop on Home's LiveArea page with a Start button (red,
         * 2026-11-12). The shell side (VitaOS's plugin; the dev bridge's kiosk
         * on red) sees that nothing is running and opens Home itself. */
    } else if (tid[0] && !quit) {
        launch_uri(tid);                            /* brings the suspended game back */
    }
    sceKernelDelayThread(300 * 1000);
    dlog("exit");
    sceKernelExitProcess(0);                    /* no vita2d_fini: it crashed on exit (dump 2026-09-27) */
    return 0;
}
