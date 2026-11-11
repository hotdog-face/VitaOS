#ifndef HOME_SYSINT_H
#define HOME_SYSINT_H

/* VitaOS's PS plugin, set up from Settings (sysint.c). */
typedef struct {
    const char *config;   /* the tai config taiHEN reads, or NULL if there is none */
    int listed;           /* our line is in it */
    int file;             /* ur0:tai/vitaos_ps.suprx is there */
    int current;          /* and it is the one inside this VitaOS */
    int bundled;          /* this package carries the plugin */
} SysintState;

int sysint_state(SysintState *s);            /* 1 when set up (listed and present) */
const char *sysint_config_path(void);
int sysint_install(char *err, int max);      /* 0, or < 0 with err filled in; nothing half-done */
int sysint_update_file(char *err, int max);  /* copy the bundled plugin over the installed one */
int sysint_remove(char *err, int max);       /* takes our line out of the config */

#endif
