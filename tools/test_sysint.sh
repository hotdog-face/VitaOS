#!/bin/sh
# Checks sysint.c's tai/config.txt editing on a computer: the line goes under
# the first *main (a new *main section when there is none), nothing else
# changes, line endings are kept, a commented-out line does not count, adding
# twice changes nothing, and removing gives back the original (at most an
# empty *main section VitaOS had added stays).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
python3 - "$here/src/sysint.c" > "$work/t.c" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = s.index('/* A config line naming our plugin'); b = s.index('static int copy_plugin(')
print(r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int kind;   /* 0 vitaos_ps, 1 vitaos_shell (experimental) */
static const char *const K_NAME[2] = {"vitaos_ps.suprx", "vitaos_shell.suprx"};
static const char *const K_INSTALLED[2] = {"ur0:tai/vitaos_ps.suprx", "ur0:tai/vitaos_shell.suprx"};
#define LINE K_INSTALLED[kind]
/* the Vita's file calls, on this computer; fail_rename makes the Nth rename fail */
static void fail(char *err, int max, const char *sentence, const char *path) { snprintf(err, max, "%s\n%s", sentence, path); }
static int fail_rename;
static int exists(const char *p) { return access(p, F_OK) == 0; }
static int sceIoRemove(const char *p) { return remove(p) ? -1 : 0; }
static int sceIoRename(const char *a, const char *b) { if (fail_rename && --fail_rename == 0) return -1; return rename(a, b) ? -1 : 0; }
static char *slurp(const char *path, int *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1); if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); b[n] = 0; if (len) *len = (int)n; return b;
}
static int write_all(const char *path, const void *d, int len) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    int ok = fwrite(d, 1, len, f) == (size_t)len; return fclose(f) || !ok ? -1 : 0;
}
static int same_file(const char *path, const char *d, int len) {
    int n = 0; char *b = slurp(path, &n); int same = b && n == len && !memcmp(b, d, len); free(b); return same;
}
''' + s[a:b].replace('"ux0:tai", "ur0:tai"', '"ux0tai", "ur0tai"'))
print(r'''
static int fails;
static void ok(const char *name, int cond) { printf("%-34s %s\n", name, cond ? "ok" : "FAIL"); fails += !cond; }
/* Removing may leave the "*main" line VitaOS added (an empty section, which
 * taiHEN accepts): compare without it and without a final newline. */
static int same_but_main(const char *back, const char *in) {
    if (!strcmp(back, in)) return 1;
    char a[512], b[512];
    snprintf(a, sizeof(a), "%s", back);
    snprintf(b, sizeof(b), "%s", in);
    size_t la = strlen(a), lb = strlen(b);
    while (la && a[la - 1] == '\n') a[--la] = 0;
    while (lb && b[lb - 1] == '\n') b[--lb] = 0;
    if (!strcmp(a, b)) return 1;
    if (la >= 6 && !strcmp(a + la - 6, "\n*main")) a[la - 6] = 0;
    return !strcmp(a, b);
}
static void check(const char *name, const char *in, const char *want) {
    char *out = edit_config(in, 1);
    char *back = edit_config(out, 0);
    int good = !strcmp(out, want) && listed(out) && same_but_main(back, in) && !strcmp(edit_config(out, 1), out);
    ok(name, good);
    if (!good) printf("--- got\n%s--- want\n%s--- back\n%s---\n", out, want, back);
}
static char *rd(const char *p) { return slurp(p, NULL); }
static int is(const char *p, const char *want) { char *b = rd(p); int r = b && !strcmp(b, want); free(b); return r; }
int main(void) {
    char err[300];
    check("main present", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/b.suprx\n",
          "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\nur0:tai/b.suprx\n");
    check("CRLF", "*KERNEL\r\nur0:tai/a.skprx\r\n*main\r\n", "*KERNEL\r\nur0:tai/a.skprx\r\n*main\r\nur0:tai/vitaos_ps.suprx\r\n");
    check("no main", "*KERNEL\nur0:tai/a.skprx\n", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("no final newline", "*KERNEL\nur0:tai/a.skprx", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("main last, unterminated", "*KERNEL\n*main", "*KERNEL\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("commented line", "# ur0:tai/vitaos_ps.suprx\n*main\n", "# ur0:tai/vitaos_ps.suprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("main with spaces", "  *main  \n", "  *main  \nur0:tai/vitaos_ps.suprx\n");
    check("NPXS section first", "*NPXS10015\nur0:tai/x.suprx\n*main\n", "*NPXS10015\nur0:tai/x.suprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    const char *has = "*main\nux0:tai/vitaos_ps.suprx\n";
    ok("already listed", listed(has) && !strcmp(edit_config(has, 1), has));
    /* under another section it does not count as installed, and gets added under *main */
    const char *wrong = "*KERNEL\nur0:tai/vitaos_ps.suprx\n*main\nur0:tai/b.suprx\n";
    ok("under *KERNEL: not installed", !listed(wrong) && anywhere(wrong) && listed(edit_config(wrong, 1)));
    ok("under *KERNEL: remove takes it out", !anywhere(edit_config(wrong, 0)));
    /* files VitaOS must refuse to edit */
    const char nul[] = "*KERNEL\nur0:tai/a.skprx\n\0*main\nur0:tai/henkaku.suprx\n";
    ok("stray NUL refused", !config_ok(nul, sizeof(nul) - 1, "config.txt", err, sizeof(err)) && strstr(err, "zero byte") && strstr(err, "\nconfig.txt"));
    ok("UTF-16 LE refused", !config_ok("\xFF\xFE*\0m\0", 6, "config.txt", err, sizeof(err)) && strstr(err, "UTF-16"));
    ok("UTF-16 BE refused", !config_ok("\xFE\xFF\0*\0m", 6, "config.txt", err, sizeof(err)));
    ok("UTF-8 BOM and plain accepted", config_ok("\xEF\xBB\xBF*main\n", 9, "c", err, sizeof(err)) && config_ok("*main\n", 6, "c", err, sizeof(err)));

    /* the swap, on real files */
    const char *C = "ur0tai/config.txt", *OLD = "*KERNEL\na\n*main\nb\n", *NEW = "*KERNEL\na\n*main\nur0:tai/vitaos_ps.suprx\nb\n";
    system("rm -rf ur0tai ux0tai; mkdir ur0tai");
    write_all(C, OLD, strlen(OLD));
    ok("swap", swap_config(C, OLD, strlen(OLD), NEW, err, sizeof(err)) == 0 && is(C, NEW) && is("ur0tai/config.txt.before-vitaos", OLD)
       && !exists("ur0tai/config.txt.vitaos-old") && !exists("ur0tai/config.txt.vitaos-new"));
    ok("first backup is kept", swap_config(C, NEW, strlen(NEW), OLD, err, sizeof(err)) == 0 && is(C, OLD) && is("ur0tai/config.txt.before-vitaos", OLD)
       && swap_config(C, OLD, strlen(OLD), NEW, err, sizeof(err)) == 0 && is("ur0tai/config.txt.before-vitaos", OLD));
    write_all(C, OLD, strlen(OLD));
    fail_rename = 1;                                   /* cannot set the old one aside */
    ok("rename 1 fails: untouched", swap_config(C, OLD, strlen(OLD), NEW, err, sizeof(err)) < 0 && is(C, OLD) && !exists("ur0tai/config.txt.vitaos-new"));
    fail_rename = 2;                                   /* new one will not move in */
    ok("rename 2 fails: old back", swap_config(C, OLD, strlen(OLD), NEW, err, sizeof(err)) < 0 && is(C, OLD) && !exists("ur0tai/config.txt.vitaos-old"));
    fail_rename = 0;
    /* power lost between the two renames: no config.txt, .vitaos-old and .vitaos-new both there */
    rename(C, "ur0tai/config.txt.vitaos-old"); write_all("ur0tai/config.txt.vitaos-new", NEW, strlen(NEW));
    sysint_recover();
    ok("recover: old config back", is(C, OLD) && !exists("ur0tai/config.txt.vitaos-old"));
    sysint_recover();
    ok("recover: leftover .new cleared", is(C, OLD) && !exists("ur0tai/config.txt.vitaos-new"));
    remove(C); write_all("ur0tai/config.txt.vitaos-new", NEW, strlen(NEW));
    sysint_recover();
    ok("recover: only .new left", is(C, NEW));
    system("rm -rf ur0tai ux0tai");
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails != 0;
}''')
PY
cc -Wall -Wno-unused-function -o "$work/t" "$work/t.c" && (cd "$work" && ./t)
