#!/bin/sh
# Home music must start by itself. Builds src/sfx.c for this computer with
# stand-ins for the Vita's threads, semaphores and audio port, and checks
# that non-silent buffers reach the port without any UI sound being played:
#   A. Home asks for the music from the first frame, before it has been made;
#   B. Home asks only after it is ready (entering the tab later).
# The stand-in SignalSema hands the CPU to the woken thread first, as the
# Vita does for the mixer (it has the higher priority): that is what made the
# old order, signal first and set the flag after, lose the wake-up.
# Usage: tools/test_sfx.sh [path/to/sfx.c]   (default: src/sfx.c)
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
src=${1:-$here/src/sfx.c}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/psp2/io" "$work/psp2/kernel"
cp "$src" "$work/sfx.c"; cp "$here/src/sfx.h" "$work/sfx.h"
cat > "$work/ui.h" <<'H'
#include "vita.h"
static inline void ui_save(const char *p, const void *d, int n, int a) { (void)p; (void)d; (void)n; (void)a; }
H
for h in audioout.h io/fcntl.h kernel/threadmgr.h; do echo '#include "vita.h"' > "$work/psp2/$h"; done
cat > "$work/vita.h" <<'H'
#ifndef FAKE_VITA_H
#define FAKE_VITA_H
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int SceUID; typedef unsigned int SceSize; typedef unsigned int SceUInt;
#define SCE_O_RDONLY 0
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_MODE_MONO 0
typedef struct { pthread_mutex_t m; pthread_cond_t c; int n, max; } FakeSema;
extern FakeSema fake_semas[8]; extern int fake_nsema; extern volatile int fake_peak;
static inline SceUID sceKernelCreateSema(const char *name, int attr, int init, int max, void *opt) {
    (void)name; (void)attr; (void)opt;
    FakeSema *s = &fake_semas[fake_nsema]; pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->n = init; s->max = max;
    return fake_nsema++;
}
static inline int sceKernelWaitSema(SceUID id, int n, void *t) {
    (void)n; (void)t; FakeSema *s = &fake_semas[id];
    pthread_mutex_lock(&s->m); while (s->n < 1) pthread_cond_wait(&s->c, &s->m); s->n--; pthread_mutex_unlock(&s->m); return 0;
}
static inline int sceKernelSignalSema(SceUID id, int n) {
    (void)n; FakeSema *s = &fake_semas[id];
    pthread_mutex_lock(&s->m); if (s->n < s->max) s->n++; pthread_cond_signal(&s->c); pthread_mutex_unlock(&s->m);
    if (s->max == 1 && id == 0) usleep(3000);        /* the wake-up semaphore: the mixer runs first, as on the Vita */
    return 0;
}
typedef int (*FakeEntry)(SceSize, void *);
static inline void *fake_tramp(void *p) { ((FakeEntry)p)(0, NULL); return NULL; }
static inline SceUID sceKernelCreateThread(const char *n, FakeEntry e, int prio, int stack, int a, int b, void *o) {
    (void)n; (void)prio; (void)stack; (void)a; (void)b; (void)o; pthread_t t; pthread_create(&t, NULL, fake_tramp, (void *)e); pthread_detach(t); return 1;
}
static inline int sceKernelStartThread(SceUID t, int a, void *b) { (void)t; (void)a; (void)b; return 0; }
static inline int sceAudioOutOpenPort(int t, int len, int rate, int mode) { (void)t; (void)len; (void)rate; (void)mode; return 7; }
static inline int sceAudioOutOutput(int port, const void *buf) {
    (void)port; const short *s = buf; int pk = 0;
    for (int i = 0; s && i < 256; ++i) { int a = s[i] < 0 ? -s[i] : s[i]; if (a > pk) pk = a; }
    if (pk > fake_peak) fake_peak = pk;
    usleep(500); return 256;
}
extern int fake_cfg_pos;
static inline SceUID sceIoOpen(const char *p, int f, int m) { (void)p; (void)f; (void)m; fake_cfg_pos = 0; return 3; }
static inline int sceIoRead(SceUID fd, void *b, int n) { (void)fd; const char *c = "0 6\n"; int l = 4; if (fake_cfg_pos) return 0; memcpy(b, c, l < n ? l : n); fake_cfg_pos = 1; return l; }
static inline int sceIoClose(SceUID fd) { (void)fd; return 0; }
#endif
H
cat > "$work/main.c" <<'C'
#include <stdio.h>
#include "vita.h"
#include "sfx.h"
FakeSema fake_semas[8]; int fake_nsema; volatile int fake_peak; int fake_cfg_pos;
int main(int c, char **v) {
    int late = c > 1 && v[1][0] == 'B';
    sfx_init();                       /* UI sounds 0 (so nothing else can wake the mixer), Home music 6 */
    if (late) sleep(4);               /* B: the music is long since made when Home first asks */
    for (int f = 0; f < 60 * 8 && fake_peak == 0; ++f) { sfx_ambient(1); usleep(16000); }
    printf("%s  %s (peak %d)\n", late ? "B: asked once the music was ready " : "A: asked before the music was made", fake_peak ? "ok" : "FAIL: silent", fake_peak);
    return fake_peak == 0;
}
C
cc -O2 -w -I"$work" -o "$work/t" "$work/main.c" "$work/sfx.c" -lm -lpthread
fail=0
"$work/t" A || fail=1
"$work/t" B || fail=1
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
