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

static int listed(const char *cfg) {
    for (const char *p = cfg; *p;) {
        const char *e = strchr(p, '\n');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (is_our_line(p, n)) return 1;
        p += n + (e ? 1 : 0);
    }
    return 0;
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

/* Replace the config with `text`, keeping a backup and checking the result. */
static int swap_config(const char *cfg_path, const char *old, int old_len, const char *text, char *err, int max) {
    char bak[64], tmp[64];
    snprintf(bak, sizeof(bak), "%s.before-vitaos", cfg_path);
    snprintf(tmp, sizeof(tmp), "%s.vitaos-new", cfg_path);
    int len = strlen(text);
    if (write_all(bak, old, old_len) < 0 || !same_file(bak, old, old_len)) {
        snprintf(err, max, "Could not keep a backup of %s, so nothing was changed.", cfg_path);
        return -1;
    }
    if (write_all(tmp, text, len) < 0 || !same_file(tmp, text, len)) {
        sceIoRemove(tmp);
        snprintf(err, max, "Could not write the new %s, so nothing was changed.", cfg_path);
        return -1;
    }
    sceIoRemove(cfg_path);
    if (sceIoRename(tmp, cfg_path) < 0) {
        write_all(cfg_path, old, old_len);              /* put the old one straight back */
        sceIoRemove(tmp);
        snprintf(err, max, "Could not replace %s; the old one is back.", cfg_path);
        return -1;
    }
    if (!same_file(cfg_path, text, len)) {
        write_all(cfg_path, old, old_len);
        snprintf(err, max, "%s did not read back as written; the old one is back.", cfg_path);
        return -1;
    }
    return 0;
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
        char *cfg = slurp(s->config, NULL);
        s->listed = cfg && listed(cfg);
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
        snprintf(err, max, "This Vita has no tai/config.txt, so taiHEN plugins are not set up here. "
                           "Follow vita.hacks.guide first, then try again.");
        return -1;
    }
    if (copy_plugin(err, max) < 0) return -1;
    int len = 0;
    char *cfg = slurp(cfg_path, &len);
    if (!cfg || len > MAX_CFG) { free(cfg); snprintf(err, max, "Could not read %s.", cfg_path); return -1; }
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
    if (!cfg) { snprintf(err, max, "Could not read %s.", cfg_path); return -1; }
    if (!listed(cfg)) { free(cfg); return 0; }
    char *text = edit_config(cfg, 0);
    int rc = text ? swap_config(cfg_path, cfg, len, text, err, max) : -1;
    free(text);
    free(cfg);
    return rc;
}

#ifdef SYSINT_TEST
/* tools/test_sysint.sh: the config editing, on a computer. */
char *sysint_test_edit(const char *cfg, int add) { return edit_config(cfg, add); }
int sysint_test_listed(const char *cfg) { return listed(cfg); }
#endif
