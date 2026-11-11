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
a = s.index('/* A config line naming our plugin'); b = s.index('/* Replace the config with')
print('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#define LINE "ur0:tai/vitaos_ps.suprx"\n' + s[a:b])
print(r'''
static int fails;
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
    int ok = !strcmp(out, want) && listed(out) && same_but_main(back, in) && !strcmp(edit_config(out, 1), out);
    printf("%-26s %s\n", name, ok ? "ok" : "FAIL");
    if (!ok) { printf("--- got\n%s--- want\n%s--- back\n%s---\n", out, want, back); fails++; }
}
int main(void) {
    check("main present", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/b.suprx\n",
          "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\nur0:tai/b.suprx\n");
    check("CRLF", "*KERNEL\r\nur0:tai/a.skprx\r\n*main\r\n", "*KERNEL\r\nur0:tai/a.skprx\r\n*main\r\nur0:tai/vitaos_ps.suprx\r\n");
    check("no main", "*KERNEL\nur0:tai/a.skprx\n", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("no final newline", "*KERNEL\nur0:tai/a.skprx", "*KERNEL\nur0:tai/a.skprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("main last, unterminated", "*KERNEL\n*main", "*KERNEL\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("commented line", "# ur0:tai/vitaos_ps.suprx\n*main\n", "# ur0:tai/vitaos_ps.suprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    check("main with spaces", "  *main  \n", "  *main  \nur0:tai/vitaos_ps.suprx\n");
    check("NPXS section first", "*NPXS10015\nur0:tai/x.suprx\n*main\n", "*NPXS10015\nur0:tai/x.suprx\n*main\nur0:tai/vitaos_ps.suprx\n");
    /* already listed (ux0 path, say): adding changes nothing */
    const char *has = "*main\nux0:tai/vitaos_ps.suprx\n";
    int ok = listed(has) && !strcmp(edit_config(has, 1), has);
    printf("%-26s %s\n", "already listed", ok ? "ok" : "FAIL"); fails += !ok;
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails != 0;
}''')
PY
cc -Wall -o "$work/t" "$work/t.c" && "$work/t"
