/* vitaos_k: EXPERIMENTAL kernel half of vitaos_shell (docs/real-os-plan.md).
 * Not part of any VitaOS release.
 *
 * One job: while vitaos_shell says a game is in front ("armed"), a short PS
 * press is taken out of what SceShell reads from the controller, so the shell
 * does not switch to LiveArea; vitaos_shell then opens the VitaOS menu
 * straight over the game. A press held for half a second or more is let
 * through from then on: that is the system's quick menu.
 *
 * It is loaded at run time by vitaos_shell (never from tai's config.txt), so
 * a start that goes wrong cannot stop the console from booting, and
 * vitaos_shell does not load it again after a load that did not finish.
 *
 * How it works: taiHEN hooks on the two controller read calls SceShell and
 * ScePaf use (found on red, 3.65: sceCtrlPeekBufferPositive2 and
 * sceCtrlPeekBufferPositiveExt2), acting only on calls made by the shell's
 * process. The hook pattern (shell-pid check, checked user<->kernel copies)
 * is the dev bridge's vakern, which is ds4vita's. No threads, no file I/O,
 * no memory of its own beyond a few words.
 *
 * vitaos_shell talks to it through the same call: port VK_PORT is not a
 * controller, and such a call from the shell's process sets "armed" from
 * `count` and returns the number of short presses hidden so far. */
#include <psp2kern/ctrl.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysmem/data_transfers.h>
#include <psp2kern/kernel/sysroot.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/display.h>
#include <taihen.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "overlay_assets.h"
#pragma GCC diagnostic pop

#define VK_PORT 0x564B                 /* 'VK' */
#define PS 0x10000u
#define HOLD_US 500000ull              /* longer than this is the quick menu */

/* 0.2: all twelve controller read calls are hooked (vakern's table, NIDs from
 * vita-headers db/360), so that the game can be kept from seeing buttons
 * while the overlay is up without ksceCtrlSetButtonIntercept, which the dev
 * bridge's overlay crashed around. Index 1 and 3 are the two the shell uses. */
#define NCTRL 12
static const unsigned int ctrl_nid[NCTRL] = {
    0xA9C3CED6, 0x15F81E8C, 0xA59454D3, 0x860BF292,      /* PeekBufferPositive, 2, Ext, Ext2 */
    0x67E7AB83, 0xC4226A3E, 0xE2D99296, 0xA7178860,      /* ReadBufferPositive, 2, Ext, Ext2 */
    0x104ED1A7, 0x81A89660, 0x15F96FB0, 0x27A0C5FB,      /* Peek/ReadBufferNegative, 2 */
};
static SceUID uid[NCTRL] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static tai_hook_ref_t ref[NCTRL];

static volatile int armed;
static volatile unsigned int shorts;   /* short presses hidden so far */
static int was_down, latched;          /* the newest sample last time; was "armed" set when this press began */
static SceUInt64 press_ts, counted_ts;

static int from_shell(void) { return ksceKernelGetProcessId() == ksceKernelSysrootGetShellPid(); }

static void filter(int ret, SceCtrlData *u) {
    if (ret <= 0 || !u || !from_shell()) return;
    if (!armed && !was_down && !latched) return;         /* nothing to hide, nothing hidden that could be read again */
    if (ret > 64) ret = 64;
    SceCtrlData k;
    /* The newest sample says where the press is. */
    if (ksceKernelMemcpyUserToKernel(&k, u + ret - 1, sizeof(k)) < 0) return;
    int down = (k.buttons & PS) != 0;
    SceUInt64 newest = k.timeStamp;
    if (down && !was_down) {                             /* a press began: at the oldest "down" of this run of samples */
        SceUInt64 start = newest;
        for (int i = ret - 2; i >= 0; --i) {
            SceCtrlData s;
            if (ksceKernelMemcpyUserToKernel(&s, u + i, sizeof(s)) < 0 || !(s.buttons & PS)) break;
            start = s.timeStamp;
        }
        press_ts = start;
        latched = armed;
    } else if (!down && was_down) {                      /* released */
        if (latched && newest - press_ts < HOLD_US && counted_ts != press_ts) { counted_ts = press_ts; ++shorts; }
    }
    was_down = down;
    if (!latched) return;
    /* Hide the first half second of the press, in every sample that holds it
     * (the same samples come back on later reads). */
    for (int i = 0; i < ret; ++i) {
        if (ksceKernelMemcpyUserToKernel(&k, u + i, sizeof(k)) < 0) return;
        if (!(k.buttons & PS) || k.timeStamp < press_ts || k.timeStamp - press_ts >= HOLD_US) continue;
        k.buttons &= ~PS;
        if (ksceKernelMemcpyKernelToUser(u + i, &k, sizeof(k)) < 0) return;
    }
}

/* ---------- 0.2: the overlay ----------
 * A card (Resume, Volume, Brightness, Close game) drawn over the running
 * game, with no app switch and so none of the shell's animations. Method and
 * drawing code are the dev bridge's vaoverlay (i-ha/agent-service/kernel),
 * with what its notes say went wrong left out:
 *  - the display hook exists only while the card is up (a hook left in place
 *    made retail games crash at launch), and it draws only into frames the
 *    one game presents itself, by checked copies (ksceKernelCopyToUser);
 *  - the hook never sleeps, locks, allocates or does file I/O, and uses no
 *    floating point;
 *  - nothing here has a thread: vitaos_shell decides everything (which row,
 *    what levels, when to open and close) and tells this module through the
 *    VK ports; this module draws, hides buttons from the game and reads the
 *    pad for vitaos_shell. */
#define CW 440                             /* the card, in 960x544 pixels */
#define CH 250
#define ROWS 4
#define ROW_Y0 54
#define ROW_H 42
#define PX(r, g, b) ((unsigned int)(r) | (unsigned int)(g) << 8 | (unsigned int)(b) << 16 | 0xFF000000u)
#define C_BG PX(16, 18, 34)
#define C_ROW PX(26, 30, 58)
#define C_SEL PX(40, 46, 86)
#define C_RING PX(139, 162, 255)
#define C_TEXT PX(236, 239, 244)
#define C_DIM PX(170, 180, 210)
#define C_TRACK PX(70, 78, 120)

static unsigned int card[CW * CH];
static unsigned int rowbuf[1920];          /* a scaled row; only under card_lock */
static volatile int card_lock;             /* the card is being drawn or copied: one at a time, nobody waits */
static volatile int ov_open, card_ready;
static volatile SceUID draw_pid;
static volatile unsigned int drawn;        /* frames the card went into since it opened */
static volatile int in_hook;
static volatile unsigned int hook_calls;
static SceUID disp_uid = -1;
static tai_hook_ref_t disp_ref;

static void fill(int x, int y, int w, int h, unsigned int c) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) card[j * CW + i] = c;
}

static unsigned int mix(unsigned int dst, unsigned int src, unsigned int a) {
    unsigned int r = ((src & 0xFF) * a + (dst & 0xFF) * (255 - a)) / 255;
    unsigned int g = ((src >> 8 & 0xFF) * a + (dst >> 8 & 0xFF) * (255 - a)) / 255;
    unsigned int bl = ((src >> 16 & 0xFF) * a + (dst >> 16 & 0xFF) * (255 - a)) / 255;
    return r | g << 8 | bl << 16 | 0xFF000000u;
}

static void mask(int x, int y, const Mask *m, unsigned int c) {
    for (int j = 0; j < m->h; ++j) {
        if (y + j < 0 || y + j >= CH) continue;
        for (int i = 0; i < m->w; ++i) {
            unsigned int a = m->px[j * m->w + i];
            if (!a || x + i < 0 || x + i >= CW) continue;
            unsigned int *d = &card[(y + j) * CW + x + i];
            *d = mix(*d, c, a);
        }
    }
}

/* state: bit 0 open, bits 1-3 the selected row, bits 8-15 volume 0..30,
 * bits 16-23 brightness 0..100. Called from vitaos_shell's thread. */
static void render(unsigned int state) {
    int sel = state >> 1 & 7, vol = state >> 8 & 0xFF, bright = state >> 16 & 0xFF;
    static const Mask *const label[ROWS] = {&t_resume, &t_volume, &t_bright, &t_close};
    if (__atomic_exchange_n(&card_lock, 1, __ATOMIC_ACQUIRE)) return;   /* a frame is being copied: the next state call redraws */
    fill(0, 0, CW, CH, C_BG);
    mask(20, 12, &t_title, C_RING);
    for (int i = 0; i < ROWS; ++i) {
        int y = ROW_Y0 + i * ROW_H;
        if (i == sel) fill(12, y, CW - 24, ROW_H - 4, C_RING);
        fill(14, y + 2, CW - 28, ROW_H - 8, i == sel ? C_SEL : C_ROW);
        mask(28, y + 6, label[i], C_TEXT);
        if (i == 1 || i == 2) {
            int w = 170, x = CW - 28 - w, level = i == 1 ? vol * w / 30 : bright * w / 100;
            if (level > w) level = w;
            fill(x, y + 17, w, 6, C_TRACK);
            fill(x, y + 17, level, 6, C_RING);
        }
    }
    mask((CW - t_footer.w) / 2, CH - 26, &t_footer, C_DIM);
    card_ready = 1;
    __atomic_store_n(&card_lock, 0, __ATOMIC_RELEASE);
}

static int frame_ok(const SceDisplayFrameBuf *f) {
    return f->base && f->pixelformat == 0 /* A8B8G8R8 */ &&
           f->width >= 320 && f->width <= 1920 && f->height >= 180 && f->height <= 1088 &&
           f->pitch >= f->width && f->pitch <= 2048;
}

/* The card, scaled to the game's resolution, into the frame it is handing
 * over. Every row lands inside base + height * pitch * 4. */
static void blit(const SceDisplayFrameBuf *f) {
    int fw = f->width, fh = f->height;
    int tw = CW * fw / 960, th = CH * fh / 544;
    int x0 = (fw - tw) / 2, y0 = (fh - th) / 2;
    char *base = f->base;
    unsigned int xstep = ((unsigned int)CW << 16) / (unsigned int)tw;
    unsigned int ystep = ((unsigned int)CH << 16) / (unsigned int)th;
    for (int ty = 0; ty < th; ++ty) {
        const unsigned int *row = card + ((unsigned int)ty * ystep >> 16) * CW;
        if (tw != CW) {
            for (int tx = 0; tx < tw; ++tx) rowbuf[tx] = row[(unsigned int)tx * xstep >> 16];
            row = rowbuf;
        }
        if (ksceKernelCopyToUser(base + ((y0 + ty) * f->pitch + x0) * 4, row, tw * 4) < 0) return;
    }
    ++drawn;
}

static int set_fb_patched(int head, int index, const SceDisplayFrameBuf *p, int sync) {
    __atomic_add_fetch(&in_hook, 1, __ATOMIC_SEQ_CST);
    ++hook_calls;
    if (ov_open && card_ready && p && ksceKernelGetProcessId() == draw_pid &&
        !__atomic_exchange_n(&card_lock, 1, __ATOMIC_ACQUIRE)) {
        SceDisplayFrameBuf f;
        memcpy(&f, p, sizeof(f));                          /* one snapshot, checked once */
        if (frame_ok(&f)) blit(&f);
        __atomic_store_n(&card_lock, 0, __ATOMIC_RELEASE);
    }
    int ret = TAI_CONTINUE(int, disp_ref, head, index, p, sync);
    __atomic_sub_fetch(&in_hook, 1, __ATOMIC_SEQ_CST);
    return ret;
}

static int overlay_open(SceUID pid) {
    if (ov_open) return 0;
    drawn = 0;
    card_ready = 0;
    draw_pid = pid;
    if (disp_uid < 0)
        disp_uid = taiHookFunctionExportForKernel(KERNEL_PID, &disp_ref, "SceDisplay", 0x9FED47AC,
                                                  0x16466675, set_fb_patched);   /* ksceDisplaySetFrameBufInternal */
    if (disp_uid < 0) { draw_pid = 0; return disp_uid; }
    ov_open = 1;
    return 0;
}

/* The hook goes again as soon as the card is closed. It is released just
 * after a frame has gone through it (the next one is a frame time away), and
 * never while one is inside. */
static void overlay_close(void) {
    ov_open = 0;
    draw_pid = 0;
    if (disp_uid < 0) return;
    unsigned int seen = hook_calls;
    for (int i = 0; i < 100 && hook_calls == seen; ++i) ksceKernelDelayThread(1000);
    for (int i = 0; i < 200 && in_hook; ++i) ksceKernelDelayThread(500);
    taiHookReleaseForKernel(disp_uid, disp_ref);
    disp_uid = -1;
}

/* While the card is up, every other program's controller reads come back
 * with no buttons pressed. */
static void blind(int ret, SceCtrlData *u, int negative) {
    if (!ov_open || ret <= 0 || !u || from_shell()) return;
    if (ksceKernelGetProcessId() != draw_pid) return;
    if (ret > 64) ret = 64;
    for (int i = 0; i < ret; ++i) {
        SceCtrlData k;
        if (ksceKernelMemcpyUserToKernel(&k, u + i, sizeof(k)) < 0) return;
        k.buttons = negative ? 0xFFFFFFFFu : 0;
        if (ksceKernelMemcpyKernelToUser(u + i, &k, sizeof(k)) < 0) return;
    }
}

static int hook_peek2(int port, SceCtrlData *data, int count) {
    if (port == VK_PORT) {
        if (!from_shell()) return 0x80340001;            /* not a controller port */
        armed = count != 0;
        return (int)(shorts & 0x7FFFFFFF);
    }
    if (port >= VK_PORT + 1 && port <= VK_PORT + 3) {
        if (!from_shell()) return 0x80340001;
        if (port == VK_PORT + 1) {                       /* the card's state; returns frames drawn */
            if (ov_open) render((unsigned int)count);
            return (int)(drawn & 0xFFFF);
        }
        if (port == VK_PORT + 2) {                       /* open over this pid, or 0: close */
            if (count) return overlay_open((SceUID)count);
            overlay_close();
            return 0;
        }
        SceCtrlData pad;                                 /* VK_PORT + 3: the buttons, read here in the kernel */
        memset(&pad, 0, sizeof(pad));
        if (ksceCtrlPeekBufferPositive(0, &pad, 1) < 0) return 0;
        return (int)(pad.buttons & 0x7FFFFFFF);
    }
    int ret = TAI_CONTINUE(int, ref[1], port, data, count);
    filter(ret, data);
    blind(ret, data, 0);
    return ret;
}

#define CTRL_HOOK(i) \
    static int hook_##i(int port, SceCtrlData *data, int count) { \
        int ret = TAI_CONTINUE(int, ref[i], port, data, count); \
        if (i == 3) filter(ret, data); \
        blind(ret, data, i >= 8); \
        return ret; \
    }
CTRL_HOOK(0) CTRL_HOOK(2) CTRL_HOOK(3) CTRL_HOOK(4) CTRL_HOOK(5) CTRL_HOOK(6)
CTRL_HOOK(7) CTRL_HOOK(8) CTRL_HOOK(9) CTRL_HOOK(10) CTRL_HOOK(11)
static const void *const ctrl_fn[NCTRL] = {hook_0, hook_peek2, hook_2, hook_3, hook_4, hook_5,
                                           hook_6, hook_7, hook_8, hook_9, hook_10, hook_11};

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    for (int i = 0; i < NCTRL; ++i)
        uid[i] = taiHookFunctionExportForKernel(KERNEL_PID, &ref[i], "SceCtrl", TAI_ANY_LIBRARY, ctrl_nid[i], ctrl_fn[i]);
    if (uid[1] < 0 || uid[3] < 0) {                      /* the two the shell uses: both or nothing */
        for (int i = 0; i < NCTRL; ++i) if (uid[i] >= 0) { taiHookReleaseForKernel(uid[i], ref[i]); uid[i] = -1; }
        return SCE_KERNEL_START_FAILED;
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    armed = 0;
    overlay_close();
    for (int i = 0; i < NCTRL; ++i) if (uid[i] >= 0) { taiHookReleaseForKernel(uid[i], ref[i]); uid[i] = -1; }
    return SCE_KERNEL_STOP_SUCCESS;
}
