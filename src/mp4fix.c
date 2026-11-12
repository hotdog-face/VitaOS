/* mp4fix: reading MP4 files, and (for tools only) rearranging them.
 *
 * In VitaOS itself only mp4fix_sound_codec() is used: when a song or film
 * will not play, it says whether the sound is in a format the Vita has no
 * decoder for (Apple Lossless, say), so the message can say what to convert.
 *
 * The rest is compiled only with -DMP4FIX_REWRITE and is NOT part of VitaOS.
 * It was written for 1.9.0 on the theory that the Vita's player refuses
 * files with the index ("moov") at the end or cover art as a picture track.
 * A test on a PCH-2000 with the rewrite switched off (2026-11-11) showed the
 * player takes all of those as they are, so VitaOS never rewrites anyone's
 * files. The code stays, tested (tools/test_mp4fix.sh), in case a real file
 * turns up that needs it. What it does then:
 *  - Moving the index to the front reorders the file and patches the chunk
 *    offsets. Every track, tag and byte of media is kept.
 *  - For songs (MP4FIX_SONG), a track is left out only when it is a still
 *    picture (a video track with a JPEG or PNG codec); the untouched
 *    original then stays as "<name>.orig". Chapter, text and subtitle tracks
 *    and real video are kept.
 *  - The new file is written as "<name>.part" and takes the real name only
 *    when complete; mp4fix_recover() tidies up after an interruption.
 *
 * Plain stdio, so it builds and is tested on a computer. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "mp4fix.h"

#define MAX_MOOV (16 * 1024 * 1024)
#define MAX_BOXES 64

typedef struct { char type[5]; uint64_t at, size, new_at; } Box;

static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static void put32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void put64(unsigned char *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

static int64_t file_size(FILE *f) {
    if (fseek(f, 0, SEEK_END)) return -1;
    long n = ftell(f);
    return n < 0 ? -1 : n;
}

/* The top-level boxes, in file order. */
static int top_boxes(FILE *f, Box *b, int max) {
    int64_t end = file_size(f);
    if (end < 8 || end > 0x7FFFFFFF) return -1;   /* fseek takes a long: files up to 2 GB */
    uint64_t at = 0;
    int n = 0;
    while (at + 8 <= (uint64_t)end) {
        unsigned char h[16];
        if (fseek(f, (long)at, SEEK_SET) || fread(h, 1, 8, f) != 8) return -1;
        uint64_t size = be32(h);
        if (size == 1) {
            if (fread(h + 8, 1, 8, f) != 8) return -1;
            size = be64(h + 8);
        } else if (size == 0) size = (uint64_t)end - at;
        if (size < 8 || at + size > (uint64_t)end) {
            if (at + size > (uint64_t)end && n && !memcmp(h + 4, "mdat", 4)) size = (uint64_t)end - at;   /* cut short: keep what is there */
            else return -1;
        }
        if (n == max) return -1;
        memcpy(b[n].type, h + 4, 4);
        b[n].type[4] = 0;
        b[n].at = at;
        b[n].size = size;
        n++;
        at += size;
    }
    return n;
}

static int find(const Box *b, int n, const char *type) {
    for (int i = 0; i < n; ++i) if (!memcmp(b[i].type, type, 4)) return i;
    return -1;
}

/* Child boxes of a container held in memory: calls back for each. */
typedef int (*Visit)(unsigned char *box, uint64_t size, void *ctx);
static int each_child(unsigned char *p, uint64_t len, Visit fn, void *ctx) {
    uint64_t at = 0;
    while (at + 8 <= len) {
        uint64_t size = be32(p + at);
        if (size == 1 && at + 16 <= len) size = be64(p + at + 8);
        if (size < 8 || at + size > len) return -1;
        int rc = fn(p + at, size, ctx);
        if (rc) return rc;
        at += size;
    }
    return 0;
}

static unsigned header_len(const unsigned char *box) { return be32(box) == 1 ? 16 : 8; }

/* The handler of a trak ("soun", "vide", "text", ...): trak > mdia > hdlr. */
static int find_handler(unsigned char *box, uint64_t size, void *ctx) {
    char *out = ctx;
    unsigned h = header_len(box);
    if (!memcmp(box + 4, "mdia", 4)) return each_child(box + h, size - h, find_handler, ctx);
    if (!memcmp(box + 4, "hdlr", 4) && size >= h + 12) { memcpy(out, box + h + 8, 4); return 1; }
    return 0;
}

/* The codec of a trak's first sample entry ("mp4a", "jpeg", "avc1", ...). */
static int find_stsd(unsigned char *box, uint64_t size, void *ctx) {
    char *out = ctx;
    unsigned h = header_len(box);
    if (!memcmp(box + 4, "mdia", 4) || !memcmp(box + 4, "minf", 4) || !memcmp(box + 4, "stbl", 4))
        return each_child(box + h, size - h, find_stsd, ctx);
    if (!memcmp(box + 4, "stsd", 4) && size >= h + 16) { memcpy(out, box + h + 12, 4); return 1; }
    return 0;
}

#ifdef MP4FIX_REWRITE
/* For an "mp4v" entry, the kind of picture inside (its esds object type):
 * 0x6C JPEG, 0x6D PNG, 0x20 real MPEG-4 video. 0 if it cannot be read. */
typedef struct { unsigned char *entry; uint64_t len; } Entry;
static int find_entry(unsigned char *box, uint64_t size, void *ctx) {
    Entry *out = ctx;
    unsigned h = header_len(box);
    if (!memcmp(box + 4, "mdia", 4) || !memcmp(box + 4, "minf", 4) || !memcmp(box + 4, "stbl", 4))
        return each_child(box + h, size - h, find_entry, ctx);
    if (!memcmp(box + 4, "stsd", 4) && size >= h + 16) {
        uint64_t n = be32(box + h + 8);
        if (n >= 8 && n <= size - h - 8) { out->entry = box + h + 8; out->len = n; }
        return 1;
    }
    return 0;
}

static int esds_object_type(const unsigned char *e, uint64_t len) {
    if (len < 86 + 12) return 0;
    for (uint64_t at = 86; at + 8 <= len;) {                 /* boxes after the visual sample entry's fixed part */
        uint64_t size = be32(e + at);
        if (size < 8 || size > len - at) return 0;
        if (!memcmp(e + at + 4, "esds", 4)) {
            const unsigned char *p = e + at + 12, *end = e + at + size;
            for (int level = 0; level < 2; ++level) {         /* ES_Descr (3), then DecoderConfig (4) */
                if (end - p < 2 || *p != (level ? 0x04 : 0x03)) return 0;
                ++p;
                for (int k = 0; k < 4 && p < end && (*p & 0x80); ++k) ++p;   /* the length's leading bytes */
                if (p >= end) return 0;
                ++p;
                if (!level) {
                    if (end - p < 3) return 0;
                    int fl = p[2];
                    p += 3;
                    if (fl & 0x80) p += 2;
                    if (fl & 0x40) { if (p >= end) return 0; p += 1 + *p; }
                    if (fl & 0x20) p += 2;
                    if (p >= end) return 0;
                }
            }
            return p < end ? *p : 0;
        }
        at += size;
    }
    return 0;
}

/* 1 for a trak that is only a still picture (cover art stored as a track):
 * a video handler with a JPEG or PNG codec. Everything else is kept. */
static int is_picture_track(unsigned char *trak, uint64_t size) {
    char hd[5] = {0}, codec[5] = {0};
    unsigned h = header_len(trak);
    each_child(trak + h, size - h, find_handler, hd);
    if (memcmp(hd, "vide", 4)) return 0;
    each_child(trak + h, size - h, find_stsd, codec);
    if (!memcmp(codec, "mp4v", 4)) {                         /* how ffmpeg and many taggers store a JPEG cover */
        Entry en = {0, 0};
        each_child(trak + h, size - h, find_entry, &en);
        int ot = en.entry ? esds_object_type(en.entry, en.len) : 0;
        return ot == 0x6C || ot == 0x6D;
    }
    return !memcmp(codec, "jpeg", 4) || !memcmp(codec, "mjpa", 4) || !memcmp(codec, "mjpb", 4) ||
           !memcmp(codec, "png ", 4) || !memcmp(codec, "MJPG", 4);
}

#endif

static int is_sound_track(unsigned char *trak, uint64_t size) {
    char hd[5] = {0};
    unsigned h = header_len(trak);
    each_child(trak + h, size - h, find_handler, hd);
    return !memcmp(hd, "soun", 4);
}

#ifdef MP4FIX_REWRITE
typedef struct { const Box *b; int n; int overflow; } Patch;

static uint64_t moved(const Patch *p, uint64_t off) {
    for (int i = 0; i < p->n; ++i)
        if (off >= p->b[i].at && off < p->b[i].at + p->b[i].size) return off - p->b[i].at + p->b[i].new_at;
    return off;                                     /* outside every box: leave it */
}

/* stco/co64 anywhere under moov: point every chunk at its new place. */
static int patch_offsets(unsigned char *box, uint64_t size, void *ctx) {
    Patch *p = ctx;
    unsigned h = header_len(box);
    unsigned char *d = box + h;
    uint64_t len = size - h;
    if (!memcmp(box + 4, "trak", 4) || !memcmp(box + 4, "mdia", 4) || !memcmp(box + 4, "minf", 4) ||
        !memcmp(box + 4, "stbl", 4))
        return each_child(d, len, patch_offsets, ctx) < 0 ? -1 : 0;
    if (!memcmp(box + 4, "stco", 4) && len >= 8) {
        uint32_t count = be32(d + 4);
        if (8 + (uint64_t)count * 4 > len) return -1;
        for (uint32_t i = 0; i < count; ++i) {
            uint64_t v = moved(p, be32(d + 8 + i * 4));
            if (v > 0xFFFFFFFFu) { p->overflow = 1; return -1; }
            put32(d + 8 + i * 4, (uint32_t)v);
        }
    } else if (!memcmp(box + 4, "co64", 4) && len >= 8) {
        uint32_t count = be32(d + 4);
        if (8 + (uint64_t)count * 8 > len) return -1;
        for (uint32_t i = 0; i < count; ++i) put64(d + 8 + i * 8, moved(p, be64(d + 8 + i * 8)));
    }
    return 0;
}

#endif

/* What needs doing: MP4FIX_INDEX_LAST and/or MP4FIX_EXTRA_TRACKS, 0 if
 * nothing, < 0 if this is not an MP4 we understand. The index is only read
 * into memory when tracks have to be looked at (songs) or changed. */
static int examine(FILE *f, Box *b, int *nb, unsigned char **moov_out, int song) {
    int n = top_boxes(f, b, MAX_BOXES);
    if (n < 0) return MP4FIX_NOT_MP4;
    *nb = n;
    int moov = find(b, n, "moov"), mdat = find(b, n, "mdat");
    if (moov < 0 || mdat < 0 || b[moov].size > MAX_MOOV) return MP4FIX_NOT_MP4;
    /* Fragmented files (moof/sidx boxes, or an mvex in the index) keep their
     * sample tables in the fragments, which this code does not rewrite:
     * taking a track out or moving the index would break them. Left alone. */
    if (find(b, n, "moof") >= 0 || find(b, n, "sidx") >= 0) return MP4FIX_NOT_MP4;
    int need = moov > mdat ? MP4FIX_INDEX_LAST : 0;
    unsigned char *m = malloc((size_t)b[moov].size);
    if (!m) return MP4FIX_NOT_MP4;
    if (fseek(f, (long)b[moov].at, SEEK_SET) || fread(m, 1, (size_t)b[moov].size, f) != b[moov].size) { free(m); return MP4FIX_NOT_MP4; }
    for (uint64_t at = header_len(m); at + 8 <= b[moov].size;) {
        uint64_t size = be32(m + at);
        if (size < 8 || at + size > b[moov].size) break;
        if (!memcmp(m + at + 4, "mvex", 4)) { free(m); return MP4FIX_NOT_MP4; }
        at += size;
    }
    if (song) {
        uint64_t at = header_len(m);
        int sound = 0, pictures = 0;
        while (at + 8 <= b[moov].size) {
            uint64_t size = be32(m + at);
            if (size < 8 || at + size > b[moov].size) break;
            if (!memcmp(m + at + 4, "trak", 4)) {
                if (is_sound_track(m + at, size)) sound++;
#ifdef MP4FIX_REWRITE
                else if (is_picture_track(m + at, size)) pictures++;
#endif
            }
            at += size;
        }
        if (sound && pictures) need |= MP4FIX_EXTRA_TRACKS;
    }
    if (moov_out) *moov_out = m; else free(m);
    return need;
}

#ifdef MP4FIX_REWRITE
int mp4fix_check(const char *path, int flags) {
    FILE *f = fopen(path, "rb");
    if (!f) return MP4FIX_NOT_MP4;
    Box b[MAX_BOXES];
    int n = 0;
    int need = examine(f, b, &n, NULL, flags & MP4FIX_SONG);
    fclose(f);
    return need;
}

long long mp4fix_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    long long n = file_size(f);
    fclose(f);
    return n;
}

static int copy_range(FILE *in, FILE *out, uint64_t at, uint64_t len, Mp4fixJob *job) {
    enum { CHUNK = 256 * 1024 };
    unsigned char *buf = malloc(CHUNK);
    if (!buf) return -1;
    int rc = fseek(in, (long)at, SEEK_SET) ? -1 : 0;
    while (len && !rc) {
        size_t want = len > CHUNK ? CHUNK : (size_t)len;
        if (job && job->cancel) { rc = -2; break; }
        if (fread(buf, 1, want, in) != want || fwrite(buf, 1, want, out) != want) { rc = -1; break; }
        len -= want;
        if (job) job->done += want;
    }
    free(buf);
    return rc;
}

/* The journal: one small file naming the file being fixed, written before
 * anything is created and removed when the fix has finished either way.
 * mp4fix_recover() reads it at the next start, so recovery after a crash or
 * power loss looks at exactly one file instead of walking the whole card
 * (a walk of 900 album folders held Music on "Loading" for 20 s). */
static char journal[200];
void mp4fix_set_journal(const char *file) { snprintf(journal, sizeof(journal), "%s", file ? file : ""); }

static void journal_write(const char *path) {
    if (!journal[0]) return;
    FILE *j = fopen(journal, "wb");
    if (j) { fputs(path, j); fclose(j); }
}

static void journal_clear(void) { if (journal[0]) remove(journal); }

static int recover_one(const char *path) {
    char part[600], old[600];
    struct stat st;
    snprintf(part, sizeof(part), "%s.part", path);
    snprintf(old, sizeof(old), "%s.orig", path);
    int back = 0;
    if (stat(path, &st) && !stat(old, &st)) back = rename(old, path) == 0;   /* stopped between the two renames */
    remove(part);                                                            /* never complete if we are here */
    return back;
}

int mp4fix_recover(void) {
    if (!journal[0]) return 0;
    FILE *j = fopen(journal, "rb");
    if (!j) return 0;
    char path[520] = "";
    size_t n = fread(path, 1, sizeof(path) - 1, j);
    fclose(j);
    path[n] = 0;
    int back = path[0] ? recover_one(path) : 0;
    journal_clear();
    return back;
}

static int fix_impl(const char *path, int flags, Mp4fixJob *job);

int mp4fix_fix(const char *path, int flags, Mp4fixJob *job) {
    int need = mp4fix_check(path, flags);
    if (need <= 0) return need;
    journal_write(path);
    int rc = fix_impl(path, flags, job);
    journal_clear();
    return rc;
}

static int fix_impl(const char *path, int flags, Mp4fixJob *job) {
    FILE *f = fopen(path, "rb");
    if (!f) return MP4FIX_NOT_MP4;
    Box b[MAX_BOXES];
    int n = 0;
    unsigned char *m = NULL;
    int need = examine(f, b, &n, &m, flags & MP4FIX_SONG);
    if (need <= 0) { fclose(f); free(m); return need; }
    int moov = find(b, n, "moov");
    uint64_t moov_size = b[moov].size;

    /* The new index: the same, without the still-picture tracks. */
    if (need & MP4FIX_EXTRA_TRACKS) {
        unsigned h = header_len(m);
        uint64_t r = h, w = h;
        while (r + 8 <= moov_size) {
            uint64_t size = be32(m + r);
            if (size < 8 || r + size > moov_size) break;
            int keep = memcmp(m + r + 4, "trak", 4) || !is_picture_track(m + r, size);
            if (keep) { memmove(m + w, m + r, (size_t)size); w += size; }
            r += size;
        }
        moov_size = w;
        if (h == 16) put64(m + 8, moov_size); else put32(m, (uint32_t)moov_size);
    }

    /* Where everything lands: ftyp (if any) first, then moov, then the rest
     * in their old order. */
    int ftyp = find(b, n, "ftyp");
    uint64_t at = 0;
    if (ftyp >= 0) { b[ftyp].new_at = at; at += b[ftyp].size; }
    at += moov_size;
    for (int i = 0; i < n; ++i) {
        if (i == ftyp || i == moov) continue;
        b[i].new_at = at;
        at += b[i].size;
    }
    if (job) { job->total = at; job->done = 0; }
    Patch p = {b, n, 0};
    unsigned h = header_len(m);
    if (each_child(m + h, moov_size - h, patch_offsets, &p) < 0 || p.overflow) { fclose(f); free(m); return MP4FIX_FAILED; }

    char part[512], old[520];
    snprintf(part, sizeof(part), "%s.part", path);
    snprintf(old, sizeof(old), "%s.orig", path);
    FILE *o = fopen(part, "wb");
    if (!o) { fclose(f); free(m); return MP4FIX_FAILED; }
    int bad = 0;
    if (ftyp >= 0) bad = copy_range(f, o, b[ftyp].at, b[ftyp].size, job);
    if (!bad && fwrite(m, 1, (size_t)moov_size, o) != moov_size) bad = -1;
    if (!bad && job) job->done += moov_size;
    for (int i = 0; i < n && !bad; ++i) {
        if (i == ftyp || i == moov) continue;
        bad = copy_range(f, o, b[i].at, b[i].size, job);
    }
    if (fclose(o) && !bad) bad = -1;
    fclose(f);
    free(m);
    if (bad) { remove(part); return bad == -2 ? MP4FIX_CANCELLED : MP4FIX_FAILED; }
    /* Swap. The original keeps existing under one name or the other at every
     * step; mp4fix_recover() puts it back if this is interrupted. */
    struct stat st;
    int have_orig = stat(old, &st) == 0;                 /* an earlier original: that one is the true one */
    if (have_orig) {
        if (remove(path)) { remove(part); return MP4FIX_FAILED; }
    } else if (rename(path, old)) { remove(part); return MP4FIX_FAILED; }
    if (rename(part, path)) {
        if (!have_orig) rename(old, path);
        remove(part);
        return MP4FIX_FAILED;
    }
    /* Only reordered: nothing was left out, so the old copy is not needed.
     * A track was left out: the original stays, for good. */
    if (!(need & MP4FIX_EXTRA_TRACKS) && !have_orig) remove(old);
    return need;
}

/* The same tidy-up for a whole folder (not used at start: too slow on a big
 * library; kept for tools and tests). Under `dir` (and its folders, a few
 * levels down), delete half-written "<media>.part" files and give back the
 * name of any "<media>.orig" whose file is missing. Returns how many files
 * it put back. Never touches a pair that is complete. */
static int media_name(const char *name, size_t len) {
    static const char *const ext[] = {".m4a", ".mp4", ".m4b", ".aac", ".m4v", ".mov"};
    for (int i = 0; i < 6; ++i) {
        size_t el = strlen(ext[i]);
        if (len > el && !strncasecmp(name + len - el, ext[i], el)) return 1;
    }
    return 0;
}

int mp4fix_sweep(const char *dir, int depth) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    int restored = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[600], real[600];
        size_t len = strlen(e->d_name);
        struct stat st;
        /* By name first: a stat for each of a few thousand songs held Music
         * on "Loading" for 20 s. The Vita's readdir already says what is a
         * folder; elsewhere, only names without a dot are worth a stat. */
        int tail = len > 5 && (!strcmp(e->d_name + len - 5, ".part") || !strcmp(e->d_name + len - 5, ".orig"));
#ifdef __vita__
        int is_dir = SCE_S_ISDIR(e->d_stat.st_mode);
#else
        int is_dir = 0;
        if (!tail) { snprintf(path, sizeof(path), "%s/%s", dir, e->d_name); is_dir = stat(path, &st) == 0 && S_ISDIR(st.st_mode); }
#endif
        if (!is_dir && !tail) continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (is_dir) { if (depth > 0) restored += mp4fix_sweep(path, depth - 1); continue; }
        if (len > 5 && !strcmp(e->d_name + len - 5, ".part") && media_name(e->d_name, len - 5)) {
            remove(path);
        } else if (len > 5 && !strcmp(e->d_name + len - 5, ".orig") && media_name(e->d_name, len - 5)) {
            snprintf(real, sizeof(real), "%.*s", (int)(strlen(path) - 5), path);
            if (stat(real, &st) && rename(path, real) == 0) restored++;
        }
    }
    closedir(d);
    return restored;
}

#endif   /* MP4FIX_REWRITE */

/* The first sound track's codec, from its stsd entry ("mp4a", "alac", ...). */
int mp4fix_sound_codec(const char *path, char out[5]) {
    memset(out, 0, 5);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    Box b[MAX_BOXES];
    int n = 0;
    unsigned char *m = NULL;
    int rc = examine(f, b, &n, &m, 0);
    fclose(f);
    if (rc < 0 || !m) { free(m); return -1; }
    uint64_t size = b[find(b, n, "moov")].size, at = header_len(m);
    while (at + 8 <= size && !out[0]) {
        uint64_t s = be32(m + at);
        if (s < 8 || at + s > size) break;
        if (!memcmp(m + at + 4, "trak", 4)) {
            unsigned h = header_len(m + at);
            if (is_sound_track(m + at, s)) each_child(m + at + h, s - h, find_stsd, out);
        }
        at += s;
    }
    free(m);
    return out[0] ? 0 : -1;
}
