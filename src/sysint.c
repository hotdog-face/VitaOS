/* System integration (2026-11): set up VitaOS's PS plugin from Settings.
 *
 * The plugin (plugin/vitaos_ps.c) is what makes VitaOS feel like the Vita's
 * own home screen: it opens VitaOS at power-on, brings it back when a game
 * ends, and lets PS go to VitaOS's Home. Until now it meant copying a file
 * and editing tai/config.txt by hand, and editing the wrong one of the two
 * config files was the usual way it went wrong. VitaOS now ships the plugin
 * inside its own package and does the steps itself:
 *
 *  - the config taiHEN actually reads: ux0:tai/config.txt when it exists,
 *    otherwise ur0:tai/config.txt (taiHEN's own order);
 *  - a copy of that config is kept first, as config.txt.before-vitaos;
 *  - the plugin file goes to ur0:tai/vitaos_ps.suprx, written beside it and
 *    checked (SHA-256) before it takes the real name;
 *  - one line is added under *main (a user plugin inside the shell; nothing
 *    is ever added under *KERNEL), and the new config is read back and
 *    compared before it replaces the old one.
 *
 * Remove takes the line out again (the file stays, harmless). If anything
 * ever goes wrong at power-on, holding L while the Vita starts skips every
 * plugin, and the backup sits next to the config. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include "sysint.h"
#include "shared/sha256.h"

#define BUNDLED "app0:assets/plugin/vitaos_ps.suprx"
#define INSTALLED "ur0:tai/vitaos_ps.suprx"
#define LINE "ur0:tai/vitaos_ps.suprx"
#define MAX_CFG (64 * 1024)

/* An error for the player: a fixed sentence (so it can be translated), then
 * the file it is about on a line of its own. */
static void fail(char *err, int max, const char *sentence, const char *path) { snprintf(err, max, "%s\n%s", sentence, path); }

static int exists(const char *p) { SceIoStat st; return sceIoGetstat(p, &st) >= 0; }

static char *slurp(const char *path, int *len) {
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return NULL;
    int size = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size < 0 || size > MAX_CFG * 32) { sceIoClose(fd); return NULL; }
    char *b = malloc(size + 1);
    int n = b ? sceIoRead(fd, b, size) : -1;
    sceIoClose(fd);
    if (n != size) { free(b); return NULL; }
    b[n] = 0;
    if (len) *len = n;
    return b;
}

static int write_all(const char *path, const void *data, int len) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return fd;
    int w = sceIoWrite(fd, data, len);
    sceIoClose(fd);
    return w == len ? 0 : -1;
}

static int same_file(const char *path, const char *data, int len) {
    int n = 0;
    char *b = slurp(path, &n);
    int same = b && n == len && !memcmp(b, data, len);
    free(b);
    return same;
}

static void hash_hex(const char *data, int len, char out[65]) {
    Sha256 c;
    unsigned char d[32];
    sha256_init(&c);
    sha256_update(&c, data, len);
    sha256_final(&c, d);
    sha256_hex(d, out);
}

const char *sysint_config_path(void) {
    if (exists("ux0:tai/config.txt")) return "ux0:tai/config.txt";
    if (exists("ur0:tai/config.txt")) return "ur0:tai/config.txt";
    return NULL;
}

/* A config line naming our plugin (not a comment). */
static int is_our_line(const char *s, int n) {
    while (n && (*s == ' ' || *s == '\t')) { ++s; --n; }
    if (!n || *s == '#') return 0;
    for (int i = 0; i + 15 <= n; ++i)
        if (!strncmp(s + i, "vitaos_ps.suprx", 15)) return 1;
    return 0;
}

/* The section a line opens ("*main" -> "main"), or NULL if it is not a section line. */
static int section_of(const char *p, int n, char *out, int max) {
    while (n && (*p == ' ' || *p == '\t')) { ++p; --n; }
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r')) --n;
    if (!n || *p != '*') return 0;
    snprintf(out, max, "%.*s", n - 1, p + 1);
    return 1;
}

/* Our line, under a *main section (the only place a shell plugin starts). */
static int listed(const char *cfg) {
    char sect[32] = "";
    for (const char *p = cfg; *p;) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (!section_of(p, n, sect, sizeof(sect)) && !strcmp(sect, "main") && is_our_line(p, n)) return 1;
        p += n + (e ? 1 : 0);
    }
    return 0;
}

/* Our line anywhere at all (for Remove: a line someone put under another
 * section is still ours to take out). */
static int anywhere(const char *cfg) {
    for (const char *p = cfg; *p;) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (is_our_line(p, n)) return 1;
        p += n + (e ? 1 : 0);
    }
    return 0;
}

/* A config this code can edit safely: plain 8-bit text, no NUL bytes (strlen
 * would stop there and the rest, *main included, would be lost) and no
 * UTF-16 (taiHEN reads bytes; a UTF-16 file would be rewritten as garbage). */
static int config_ok(const char *cfg, int len, const char *path, char *err, int max) {
    const unsigned char *u = (const unsigned char *)cfg;
    if (len >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF))) {
        fail(err, max, "This config file is saved as UTF-16, which VitaOS will not edit. Nothing was changed: add the plugin line by hand.", path);
        return 0;
    }
    if (memchr(cfg, 0, len)) {
        fail(err, max, "This config file contains a stray zero byte, so VitaOS will not edit it. Nothing was changed: add the plugin line by hand.", path);
        return 0;
    }
    return 1;
}

/* The new config: ours under *main (added after the first "*main" line, or
 * a new *main section at the end), or every line of ours taken out. Keeps
 * the file's own line endings. */
static char *edit_config(const char *cfg, int add) {
    int len = strlen(cfg);
    if (add && listed(cfg)) { char *same = malloc(len + 1); if (same) memcpy(same, cfg, len + 1); return same; }
    const char *nl = strstr(cfg, "\r\n") ? "\r\n" : "\n";
    char *out = malloc(len + 128);
    if (!out) return NULL;
    int o = 0, done = !add;
    for (const char *p = cfg; *p;) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p + 1) : (int)strlen(p);
        int body = n;
        while (body && (p[body - 1] == '\n' || p[body - 1] == '\r')) --body;
        if (!add && is_our_line(p, body)) { p += n; continue; }   /* removing */
        memcpy(out + o, p, n);
        o += n;
        if (add && !done) {
            const char *t = p;
            int tl = body;
            while (tl && (*t == ' ' || *t == '\t')) { ++t; --tl; }
            while (tl && (t[tl - 1] == ' ' || t[tl - 1] == '\t')) --tl;
            if (tl == 5 && !strncmp(t, "*main", 5)) {
                if (!e) o += sprintf(out + o, "%s", nl);           /* "*main" was the last line, unterminated */
                o += sprintf(out + o, "%s%s", LINE, nl);
                done = 1;
            }
        }
        p += n;
    }
    if (!done) {
        if (o && out[o - 1] != '\n') o += sprintf(out + o, "%s", nl);
        o += sprintf(out + o, "*main%s%s%s", nl, LINE, nl);
    }
    out[o] = 0;
    return out;
}

/* Replace the config with `text`. At no moment is there no config: the old
 * one is renamed aside (.vitaos-old) and the new one, already written and
 * read back (.vitaos-new), takes its name; only then is the old one deleted.
 * sysint_recover() finishes or undoes an interrupted swap at the next start.
 * The very first original is kept as .before-vitaos and never overwritten. */
static int swap_config(const char *cfg_path, const char *old, int old_len, const char *text, char *err, int max) {
    char bak[64], tmp[64], prev[64];
    snprintf(bak, sizeof(bak), "%s.before-vitaos", cfg_path);
    snprintf(tmp, sizeof(tmp), "%s.vitaos-new", cfg_path);
    snprintf(prev, sizeof(prev), "%s.vitaos-old", cfg_path);
    int len = strlen(text);
    if (!exists(bak) && (write_all(bak, old, old_len) < 0 || !same_file(bak, old, old_len))) {
        sceIoRemove(bak);
        fail(err, max, "Could not keep a backup of the config file, so nothing was changed.", cfg_path);
        return -1;
    }
    if (exists(prev) && sceIoRemove(prev) < 0) {             /* a finished swap's leftover */
        fail(err, max, "Could not write the new config file, so nothing was changed.", prev);
        return -1;
    }
    if (write_all(tmp, text, len) < 0 || !same_file(tmp, text, len)) {
        sceIoRemove(tmp);
        fail(err, max, "Could not write the new config file, so nothing was changed.", cfg_path);
        return -1;
    }
    if (sceIoRename(cfg_path, prev) < 0) {
        sceIoRemove(tmp);
        fail(err, max, "Could not write the new config file, so nothing was changed.", cfg_path);
        return -1;
    }
    if (sceIoRename(tmp, cfg_path) < 0 || !same_file(cfg_path, text, len)) {
        sceIoRemove(cfg_path);
        if (sceIoRename(prev, cfg_path) < 0) {
            fail(err, max, "Could not put the config file back. It is safe under the name below: rename it to config.txt in VitaShell before restarting.", prev);
            return -1;
        }
        sceIoRemove(tmp);
        fail(err, max, "Could not replace the config file; the old one is back in place.", cfg_path);
        return -1;
    }
    sceIoRemove(prev);                                        /* best effort: recover() tidies it otherwise */
    return 0;
}

/* At start: finish or undo a swap a crash or power loss interrupted. Only
 * ever acts when config.txt itself is missing. */
void sysint_recover(void) {
    static const char *const dirs[] = {"ux0:tai", "ur0:tai"};
    for (int i = 0; i < 2; ++i) {
        char cfg[48], prev[64], tmp[64];
        snprintf(cfg, sizeof(cfg), "%s/config.txt", dirs[i]);
        snprintf(prev, sizeof(prev), "%s.vitaos-old", cfg);
        snprintf(tmp, sizeof(tmp), "%s.vitaos-new", cfg);
        if (exists(cfg)) {
            if (exists(tmp)) sceIoRemove(tmp);                /* never got used */
            continue;
        }
        if (exists(prev)) sceIoRename(prev, cfg);             /* the config as it was before the change */
        else if (exists(tmp)) sceIoRename(tmp, cfg);
    }
}

static int copy_plugin(char *err, int max) {
    int len = 0;
    char *bin = slurp(BUNDLED, &len);
    if (!bin || len < 1024) { free(bin); snprintf(err, max, "This VitaOS package has no plugin inside."); return -1; }
    char want[65], got[65];
    hash_hex(bin, len, want);
    sceIoMkdir("ur0:tai", 0777);
    const char *part = INSTALLED ".part";
    int rc = write_all(part, bin, len);
    int n = 0;
    char *back = rc == 0 ? slurp(part, &n) : NULL;
    if (back && n == len) hash_hex(back, n, got); else got[0] = 0;
    free(back);
    free(bin);
    if (strcmp(want, got)) { sceIoRemove(part); snprintf(err, max, "Could not copy the plugin to ur0:tai."); return -1; }
    sceIoRemove(INSTALLED);
    if (sceIoRename(part, INSTALLED) < 0) { sceIoRemove(part); snprintf(err, max, "Could not copy the plugin to ur0:tai."); return -1; }
    return 0;
}

int sysint_state(SysintState *s) {
    memset(s, 0, sizeof(*s));
    s->config = sysint_config_path();
    s->bundled = exists(BUNDLED);
    s->file = exists(INSTALLED);
    if (s->config) {
        int len = 0;
        char err[8];
        char *cfg = slurp(s->config, &len);
        s->odd = cfg && !config_ok(cfg, len, s->config, err, sizeof(err));
        s->listed = cfg && !s->odd && listed(cfg);
        free(cfg);
    }
    if (s->file && s->bundled) {
        int a = 0, b = 0;
        char *x = slurp(INSTALLED, &a), *y = slurp(BUNDLED, &b);
        s->current = x && y && a == b && !memcmp(x, y, a);
        free(x);
        free(y);
    }
    return s->listed && s->file;
}

int sysint_install(char *err, int max) {
    const char *cfg_path = sysint_config_path();
    if (!cfg_path) {
        snprintf(err, max, "This Vita has no tai/config.txt, so plugins are not set up here. Install HENkaku/Enso with taiHEN (vita.hacks.guide), then try again.");
        return -1;
    }
    if (copy_plugin(err, max) < 0) return -1;
    int len = 0;
    char *cfg = slurp(cfg_path, &len);
    if (!cfg || len > MAX_CFG) { free(cfg); fail(err, max, "Could not read the config file.", cfg_path); return -1; }
    if (!config_ok(cfg, len, cfg_path, err, max)) { free(cfg); return -1; }
    if (listed(cfg)) { free(cfg); return 0; }            /* already there: only the file was refreshed */
    char *text = edit_config(cfg, 1);
    int rc = text ? swap_config(cfg_path, cfg, len, text, err, max) : -1;
    free(text);
    free(cfg);
    return rc;
}

int sysint_update_file(char *err, int max) { return copy_plugin(err, max); }

int sysint_remove(char *err, int max) {
    const char *cfg_path = sysint_config_path();
    if (!cfg_path) return 0;
    int len = 0;
    char *cfg = slurp(cfg_path, &len);
    if (!cfg) { fail(err, max, "Could not read the config file.", cfg_path); return -1; }
    if (!config_ok(cfg, len, cfg_path, err, max)) { free(cfg); return -1; }
    if (!anywhere(cfg)) { free(cfg); return 0; }
    char *text = edit_config(cfg, 0);
    int rc = text ? swap_config(cfg_path, cfg, len, text, err, max) : -1;
    free(text);
    free(cfg);
    return rc;
}

#ifdef SYSINT_TEST
/* tools/test_sysint.sh: the config editing, on a computer. */
char *sysint_test_edit(const char *cfg, int add) { return edit_config(cfg, add); }
int sysint_test_ok(const char *cfg, int len) { char e[200]; return config_ok(cfg, len, "config.txt", e, sizeof(e)); }
int sysint_test_listed(const char *cfg) { return listed(cfg); }
#endif
