#ifndef HOME_MP4FIX_H
#define HOME_MP4FIX_H

/* Results: a mask of what was (or needs to be) changed, 0 for nothing, or < 0. */
#define MP4FIX_INDEX_LAST   1     /* the moov box sits after the media data */
#define MP4FIX_EXTRA_TRACKS 2     /* a song with cover art stored as a still-picture track */
#define MP4FIX_NOT_MP4     (-1)   /* not an MP4 this understands: leave it alone */
#define MP4FIX_FAILED      (-2)   /* tried (no space, card error); the original is untouched */
#define MP4FIX_CANCELLED   (-3)   /* stopped by job->cancel; the original is untouched */

/* Flags. */
#define MP4FIX_SONG 1             /* a song: also leave out still-picture tracks (the original is kept as .orig) */

/* Progress and cancel for a long rewrite (a film); may be NULL. */
typedef struct { volatile int cancel; volatile unsigned long long done, total; } Mp4fixJob;

int mp4fix_check(const char *path, int flags);
int mp4fix_fix(const char *path, int flags, Mp4fixJob *job);   /* blocking: call off the main thread */
long long mp4fix_size(const char *path);
int mp4fix_sound_codec(const char *path, char out[5]);   /* "mp4a", "alac", ... ; < 0 if unknown */
int mp4fix_sweep(const char *dir, int depth);            /* after a crash mid-fix; returns files put back */

#endif
