#ifndef HOME_MP4FIX_H
#define HOME_MP4FIX_H

/* Results: a mask of what was (or needs to be) changed, 0 for nothing, or < 0. */
#define MP4FIX_INDEX_LAST   1     /* the moov box sits after the media data */
#define MP4FIX_EXTRA_TRACKS 2     /* a sound file with other tracks (cover art as video) */
#define MP4FIX_NOT_MP4     (-1)   /* not an MP4 this understands: leave it alone */
#define MP4FIX_FAILED      (-2)   /* tried; the original is untouched */

int mp4fix_check(const char *path, int audio_only);
int mp4fix_fix(const char *path, int audio_only);   /* blocking: call off the main thread */
int mp4fix_sound_codec(const char *path, char out[5]);   /* "mp4a", "alac", ... ; < 0 if unknown */

#endif
