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
#include <taihen.h>

#define VK_PORT 0x564B                 /* 'VK' */
#define PS 0x10000u
#define HOLD_US 500000ull              /* longer than this is the quick menu */

static SceUID uid[2] = {-1, -1};
static tai_hook_ref_t ref[2];

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

static int hook_peek2(int port, SceCtrlData *data, int count) {
    if (port == VK_PORT) {
        if (!from_shell()) return 0x80340001;            /* not a controller port */
        armed = count != 0;
        return (int)(shorts & 0x7FFFFFFF);
    }
    int ret = TAI_CONTINUE(int, ref[0], port, data, count);
    filter(ret, data);
    return ret;
}

static int hook_peek_ext2(int port, SceCtrlData *data, int count) {
    int ret = TAI_CONTINUE(int, ref[1], port, data, count);
    filter(ret, data);
    return ret;
}

void _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
    (void)args; (void)argp;
    uid[0] = taiHookFunctionExportForKernel(KERNEL_PID, &ref[0], "SceCtrl", TAI_ANY_LIBRARY, 0x15F81E8C, hook_peek2);
    uid[1] = taiHookFunctionExportForKernel(KERNEL_PID, &ref[1], "SceCtrl", TAI_ANY_LIBRARY, 0x860BF292, hook_peek_ext2);
    if (uid[0] < 0 || uid[1] < 0) {                      /* both or neither */
        for (int i = 0; i < 2; ++i) if (uid[i] >= 0) { taiHookReleaseForKernel(uid[i], ref[i]); uid[i] = -1; }
        return SCE_KERNEL_START_FAILED;
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
    (void)args; (void)argp;
    armed = 0;
    for (int i = 0; i < 2; ++i) if (uid[i] >= 0) { taiHookReleaseForKernel(uid[i], ref[i]); uid[i] = -1; }
    return SCE_KERNEL_STOP_SUCCESS;
}
