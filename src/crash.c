/* Crash reports you choose to send (2026-10-02). While VitaOS runs, the
 * watchdog keeps "what it was doing" (STAGE) in the app's safe memory, which
 * survives a crash. On the next start a background thread looks for crash
 * files in ux0:data that it has not seen before and that are VitaOS's own
 * (the dump names VitaOS's title ID once unpacked). If there is one, a card
 * shows a QR code: scanned with a phone it opens a GitHub issue already
 * filled in with the version, the model, the firmware and what VitaOS was
 * doing. Nothing leaves the Vita unless the player submits that issue.
 * O on the card turns the question off for good (user/crash-report.off). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <zlib.h>
#include "crash.h"
#include "qrcodegen.h"
#include "version.h"

#define SEEN "ux0:data/arcadehub/user/crash-seen.txt"
#define OFF "ux0:data/arcadehub/user/crash-report.off"
#define ISSUES "https://github.com/hotdog-face/VitaOS/issues/new"

typedef struct { char magic[8]; char version[16]; char stage[64]; unsigned int frames; } Stage;
static Stage last;                       /* what the previous run was doing when it stopped */
static volatile int found, active;
static char dump_name[96], url[2000], fault_line[80];
static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
static int qr_ok;

void crash_note_stage(const char *stage, unsigned int frames) {
    static char prev[64];
    if (!stage || !strncmp(prev, stage, sizeof(prev) - 1)) return;   /* only when it changes */
    snprintf(prev, sizeof(prev), "%s", stage);
    Stage s;
    memset(&s, 0, sizeof(s));
    memcpy(s.magic, "VOSSTG1", 8);
    snprintf(s.version, sizeof(s.version), "%s", VITAOS_VERSION);
    snprintf(s.stage, sizeof(s.stage), "%s", stage);
    s.frames = frames;
    sceAppUtilSaveSafeMemory(&s, sizeof(s), 0);
}

/* Unpack a .psp2dmp (gzip) a piece at a time and look for VitaOS's title IDs. */
static int ours(const char *path) {
    gzFile g = gzopen(path, "rb");
    if (!g) return 0;
    static char buf[64 * 1024 + 16];
    int hit = 0, keep = 0;
    for (int total = 0; !hit && total < 24 * 1024 * 1024;) {
        int n = gzread(g, buf + keep, 64 * 1024);
        if (n <= 0) break;
        total += n;
        int len = keep + n;
        for (int i = 0; i + 9 <= len && !hit; ++i)
            if ((buf[i] == 'M' && !memcmp(buf + i, "MVZA00010", 9)) || (buf[i] == 'V' && !memcmp(buf + i, "VITAOS001", 9))) hit = 1;
        keep = len < 9 ? len : 9;                 /* a name split across two pieces */
        memmove(buf, buf + len - keep, keep);
    }
    gzclose(g);
    return hit;
}

/* What the dump itself says (2026-11): which thread stopped, why, and where in
 * VitaOS's code, as offsets that map straight to a line with addr2line on the
 * same build. The stage above is the main thread's, and most 1.8.4 reports
 * said "end frame" only because that is where the main thread waits: the
 * thread that actually crashed was often another one. The notes needed sit in
 * the first few KB of the unpacked dump. */
typedef struct { char thread[32], why[24]; unsigned int pc, lr; int in_app; } Fault;

static unsigned int rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned int)p[3] << 24; }

/* Bounds are checked by subtraction (x <= len - y, with y known <= len), so a
 * huge offset or size in a damaged dump cannot wrap around and pass. */
static const unsigned char *note(const unsigned char *d, int len, const char *want, unsigned int *size) {
    if (len < 0x34 || memcmp(d, "\x7f" "ELF", 4)) return NULL;
    unsigned int n = (unsigned int)len;
    unsigned int phoff = rd32(d + 0x1c), phent = d[0x2a] | d[0x2b] << 8, phnum = d[0x2c] | d[0x2d] << 8;
    if (phent < 20 || phoff > n) return NULL;
    for (unsigned int i = 0; i < phnum; ++i) {
        if ((n - phoff) / phent <= i) break;                          /* entry i ends past the data */
        const unsigned char *ph = d + phoff + i * phent;
        if (rd32(ph) != 4) continue;                                  /* PT_NOTE */
        unsigned int off = rd32(ph + 4), fs = rd32(ph + 16);
        if (off > n || n - off < 12) continue;
        unsigned int nsz = rd32(d + off), dsz = rd32(d + off + 4);
        if (nsz > 32) continue;
        unsigned int head = 12 + ((nsz + 3) & ~3u);                   /* at most 44 */
        if (n - off < head) continue;
        unsigned int room = n - off - head;                           /* bytes after the name */
        if (dsz > room || fs < head || dsz > fs - head + 12) continue;
        if (strncmp((const char *)d + off + 12, want, nsz)) continue;
        *size = dsz;
        return d + off + head;
    }
    return NULL;
}

static int read_fault(const char *path, Fault *f) {
    memset(f, 0, sizeof(*f));
    const int max = 96 * 1024;
    unsigned char *d = malloc(max);
    if (!d) return 0;
    gzFile g = gzopen(path, "rb");
    int len = g ? gzread(g, d, max) : -1;
    if (g) gzclose(g);
    unsigned int tsz = 0, rsz = 0, msz = 0;
    const unsigned char *th = len > 0 ? note(d, len, "THREAD_INFO", &tsz) : NULL;
    const unsigned char *rg = len > 0 ? note(d, len, "THREAD_REG_INFO", &rsz) : NULL;
    const unsigned char *md = len > 0 ? note(d, len, "MODULE_INFO", &msz) : NULL;
    unsigned int uid = 0, stop = 0;
    if (th && tsz >= 8) {
        unsigned int n = rd32(th + 4), off = 8;
        for (unsigned int i = 0; i < n && off <= tsz && tsz - off >= 0xa0; ++i) {
            unsigned int sz = rd32(th + off);
            if (sz < 0xa0 || sz > tsz - off) break;
            if (rd32(th + off + 0x74)) {
                uid = rd32(th + off + 4);
                stop = rd32(th + off + 0x74);
                f->pc = rd32(th + off + 0x9c);
                snprintf(f->thread, sizeof(f->thread), "%.31s", (const char *)th + off + 8);
                break;
            }
            off += sz;
        }
    }
    if (uid && rg && rsz >= 8) {
        unsigned int n = rd32(rg + 4), off = 8;
        for (unsigned int i = 0; i < n && off <= rsz && rsz - off >= 72; ++i) {
            unsigned int sz = rd32(rg + off);
            if (sz < 72 || sz > rsz - off) break;
            if (rd32(rg + off + 4) == uid) { f->lr = rd32(rg + off + 8 + 14 * 4); break; }
            off += sz;
        }
    }
    /* Offsets into VitaOS's own code segment, when the address is in it. */
    if (md && msz >= 8) {
        unsigned int n = rd32(md + 4), off = 8;
        for (unsigned int i = 0; i < n && off <= msz && msz - off >= 0x50; ++i) {
            unsigned int segs = rd32(md + off + 0x4c);
            const char *name = (const char *)md + off + 0x24;
            if (segs > 4 || msz - off < 0x50 + segs * 0x14 + 0x10) break;
            if (!strncmp(name, "vitaos", 6) && segs) {
                unsigned int start = rd32(md + off + 0x50 + 8), size = rd32(md + off + 0x50 + 12);
                if (f->pc >= start && f->pc < start + size) { f->pc -= start; f->in_app |= 1; }
                if (f->lr >= start && f->lr < start + size) { f->lr -= start; f->in_app |= 2; }
            }
            off += 0x50 + segs * 0x14 + 0x10;
        }
    }
    snprintf(f->why, sizeof(f->why), "%s", stop == 0x30002 ? "bad instruction" : stop == 0x30003 ? "bad jump" :
             stop == 0x30004 ? "bad memory read/write" : stop == 0x60080 ? "divide by zero" : stop ? "stopped" : "");
    free(d);
    return f->thread[0] != 0;
}

static void urlenc(const char *in, char *out, int max) {
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *in && n < max - 4; ++in) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("-_.~", c)) out[n++] = c;
        else { out[n++] = '%'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15]; }
    }
    out[n] = 0;
}

static int finder(SceSize args, void *argp) {
    (void)args; (void)argp;
    sceKernelDelayThread(15 * 1000 * 1000);       /* well after start-up */
    char seen[4096] = "";
    SceUID fd = sceIoOpen(SEEN, SCE_O_RDONLY, 0);
    if (fd >= 0) { int n = sceIoRead(fd, seen, sizeof(seen) - 1); seen[n > 0 ? n : 0] = 0; sceIoClose(fd); }
    int first_run = fd < 0;                       /* old dumps from before this feature: remember, do not ask */
    SceUID d = sceIoDopen("ux0:data");
    if (d < 0) return sceKernelExitDeleteThread(0);
    SceIoDirent e;
    char newest[96] = "";
    SceUID out = sceIoOpen(SEEN, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
    for (;;) {
        memset(&e, 0, sizeof(e));
        if (sceIoDread(d, &e) <= 0) break;
        if (strncmp(e.d_name, "psp2core-", 9) || !strstr(e.d_name, ".psp2dmp") || strstr(seen, e.d_name)) continue;
        if (out >= 0) { sceIoWrite(out, e.d_name, strlen(e.d_name)); sceIoWrite(out, "\n", 1); }
        if (first_run) continue;
        char path[160];
        snprintf(path, sizeof(path), "ux0:data/%s", e.d_name);
        if (ours(path)) snprintf(newest, sizeof(newest), "%s", e.d_name);
    }
    if (out >= 0) sceIoClose(out);
    sceIoDclose(d);
    if (!newest[0]) return sceKernelExitDeleteThread(0);

    snprintf(dump_name, sizeof(dump_name), "%s", newest);
    SceKernelFwInfo fw;
    memset(&fw, 0, sizeof(fw));
    fw.size = sizeof(fw);
    sceKernelGetSystemSwVersion(&fw);
    SceIoStat st;
    const char *model = sceIoGetstat("imc0:", &st) >= 0 ? "PCH-2000" : "PCH-1000";
    int known = !memcmp(last.magic, "VOSSTG1", 8);
    char newest_path[160], where[160] = "", title_where[64];
    snprintf(newest_path, sizeof(newest_path), "ux0:data/%s", newest);
    Fault ft;
    if (read_fault(newest_path, &ft)) {
        snprintf(where, sizeof(where), "- Crashed thread: %s, %s, pc %s0x%x, lr %s0x%x\n", ft.thread, ft.why,
                 ft.in_app & 1 ? "vitaos+" : "", ft.pc, ft.in_app & 2 ? "vitaos+" : "", ft.lr);
        snprintf(title_where, sizeof(title_where), "%s at %s0x%x", ft.thread, ft.in_app & 1 ? "+" : "", ft.pc);
        snprintf(fault_line, sizeof(fault_line), "Crashed in: %s (%s)", ft.thread, ft.why);
    } else snprintf(title_where, sizeof(title_where), "%s", known ? last.stage : "unknown");
    char title[160], body[900], et[400], eb[1400];
    snprintf(title, sizeof(title), "Crash: VitaOS %s (%s)", known ? last.version : VITAOS_VERSION, title_where);
    snprintf(body, sizeof(body),
             "VitaOS closed unexpectedly.\n\n- VitaOS: %s\n- Screen: %s\n%s- Console: %s, firmware %s\n- Crash file: %s\n\n"
             "What were you doing just before it closed?\n\n\n"
             "(If you can, attach the crash file from ux0:data: it helps a lot.)\n",
             known ? last.version : VITAOS_VERSION, known ? last.stage : "unknown", where, model, fw.versionString, newest);
    urlenc(title, et, sizeof(et));
    urlenc(body, eb, sizeof(eb));
    snprintf(url, sizeof(url), ISSUES "?title=%s&body=%s", et, eb);
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    qr_ok = qrcodegen_encodeText(url, tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                 qrcodegen_Mask_AUTO, true);
    if (qr_ok) found = 1;
    return sceKernelExitDeleteThread(0);
}

void crash_init(void) {
    SceIoStat st;
    sceAppUtilLoadSafeMemory(&last, sizeof(last), 0);     /* before this run overwrites it */
    if (sceIoGetstat(OFF, &st) >= 0) return;
    SceUID t = sceKernelCreateThread("crash_find", finder, 0x10000110, 0x8000, 0, 0, NULL);
    if (t >= 0) sceKernelStartThread(t, 0, NULL);
}

int crash_active(void) {
    if (found && !active) { found = 0; active = 1; }
    return active;
}

const char *crash_hint(void) { return "X close    O don't ask again"; }

void crash_update(const Input *in) {
    if (in->pressed & SCE_CTRL_CROSS) { active = 0; return; }
    if (in->pressed & SCE_CTRL_CIRCLE) {
        SceUID f = sceIoOpen(OFF, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
        if (f >= 0) sceIoClose(f);
        active = 0;
        ui_toast("Crash reports: off", C_ACCENT);
        return;
    }
    vita2d_draw_rectangle(0, 0, W, H, RGBA8(21, 24, 33, 255));
    int size = qrcodegen_getSize(qr), cell = size ? 400 / size : 4;
    if (cell < 3) cell = 3;
    int qx = W - 60 - size * cell - 20, qy = 90;
    draw_round_rect(qx - 16, qy - 16, size * cell + 32, size * cell + 32, 12, RGBA8(255, 255, 255, 255));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (qrcodegen_getModule(qr, x, y)) vita2d_draw_rectangle(qx + x * cell, qy + y * cell, cell, cell, RGBA8(0, 0, 0, 255));
    text(bold, 50, 130, C_TEXT, 24, "VitaOS closed unexpectedly");
    draw_wrapped_text("Help fix it: scan this code with your phone. It opens a GitHub issue already filled in "
                      "with the VitaOS version, your console model and firmware, and what VitaOS was doing.",
                      50, 168, qx - 90, 16, 6, C_DIM);
    draw_wrapped_text("Nothing is sent unless you submit that issue yourself.", 50, 330, qx - 90, 15, 2, C_FAINT);
    char was[96];
    snprintf(was, sizeof(was), "Was doing: %s", !memcmp(last.magic, "VOSSTG1", 8) ? last.stage : "unknown");
    text_fit(font, 50, 390, C_FAINT, 14, was, qx - 90);
    if (fault_line[0]) text_fit(font, 50, 412, C_FAINT, 14, fault_line, qx - 90);
}
