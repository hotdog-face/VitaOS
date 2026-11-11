#ifndef HOME_LIBRARY_H
#define HOME_LIBRARY_H

/* Finds the games on the card and writes Play's systems.tsv and games/*.tsv.
 * Leaves a Mac-built catalog alone (returns 0). Returns the number of games
 * found; `found` (may be NULL) counts up while it works. Slow: run it on a
 * thread or behind a progress screen. */
int library_scan(volatile int *found);

/* 1: the catalog on the card was written by library_scan; 0: Mac-built; -1: none. */
int library_is_ours(void);
int library_topup_vita(void);
int library_wiki_art(void);     /* Wikipedia box art for Vita games with no readable picture; blocking, network */   /* Mac-built catalog: append retail Vita games it lacks; returns how many */
int library_scan_into(const char *root, volatile int *found);   /* testing: root like "ux0:data/vitaos-test/" */

/* Box art for games that have none, from libretro's thumbnail server (what
 * RetroArch's Thumbnails Updater uses). Slow: run on a thread; rescan after. */
int library_fetch_art(volatile int *done, volatile int *got);

/* Test: box art for the catalog under ROOT, at most CAP tries per system. */
int library_art_into(const char *root, int cap, volatile int *done, volatile int *got);

/* Play or Apps, by the player's own choice: 1 Play, -1 Apps, 0 not chosen. */
int library_user_choice(const char *tid);
void library_set_user_choice(const char *tid, int where);
/* An Adrenaline bubble's game (from its data/boot.bin); 0 if tid is not one. */
extern volatile int library_rescan_wanted;   /* set by Apps; the main loop rescans */
int library_bubble_target(const char *tid, char *out, int max);

int library_bubble_for(const char *game_path, char *tid_out);   /* tid_out: 10 bytes */

#endif
