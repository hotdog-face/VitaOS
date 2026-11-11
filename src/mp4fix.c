/* mp4fix: make an .m4a/.mp4 playable by SceAvPlayer, losslessly (2026-11).
 *
 * VitaOS's own music import writes files with the index (the "moov" box)
 * first and only the audio track, and those play. Songs people copy over
 * themselves usually have the index at the end (most converters and phones
 * write it last), and files converted from MP3 often carry the cover picture
 * as a second, "video" track. SceAvPlayer refuses both at AddSource, which
 * was the "That file would not play" of issues 8 and 15.
 *
 * This does what `ffmpeg -i in -map 0:a -c copy -movflags +faststart out`
 * does: it moves the index to the front, optionally leaves out every track
 * that is not sound, and patches the chunk offsets. The sound itself is
 * copied byte for byte. The new file is written beside the old one as
 * "<name>.part" and only replaces it once it is complete.
 *
 * Plain stdio, so it builds and is tested on a computer too
 * (tools/test_mp4fix.sh). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* The handler of a trak ("soun", "vide", ...): trak > mdia > hdlr. */
static int find_handler(unsigned char *box, uint64_t size, void *ctx) {
    char *out = ctx;
    unsigned h = header_len(box);
    if (!memcmp(box + 4, "mdia", 4)) return each_child(box + h, size - h, find_handler, ctx);
    if (!memcmp(box + 4, "hdlr", 4) && size >= h + 12) { memcpy(out, box + h + 8, 4); return 1; }
    return 0;
}

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

/* What needs doing: MP4FIX_INDEX_LAST and/or MP4FIX_EXTRA_TRACKS, 0 if
 * nothing, < 0 if this is not an MP4 we understand. */
static int examine(FILE *f, Box *b, int *nb, unsigned char **moov_out, int audio_only) {
    int n = top_boxes(f, b, MAX_BOXES);
    if (n < 0) return MP4FIX_NOT_MP4;
    *nb = n;
    int moov = find(b, n, "moov"), mdat = find(b, n, "mdat");
    if (moov < 0 || mdat < 0 || b[moov].size > MAX_MOOV) return MP4FIX_NOT_MP4;
    int need = moov > mdat ? MP4FIX_INDEX_LAST : 0;
    unsigned char *m = malloc((size_t)b[moov].size);
    if (!m) return MP4FIX_NOT_MP4;
    if (fseek(f, (long)b[moov].at, SEEK_SET) || fread(m, 1, (size_t)b[moov].size, f) != b[moov].size) { free(m); return MP4FIX_NOT_MP4; }
    if (audio_only) {
        unsigned h = header_len(m);
        uint64_t at = h;
        int sound = 0, other = 0;
        while (at + 8 <= b[moov].size) {
            uint64_t size = be32(m + at);
            if (size < 8 || at + size > b[moov].size) break;
            if (!memcmp(m + at + 4, "trak", 4)) {
                char hd[5] = {0};
                each_child(m + at + header_len(m + at), size - header_len(m + at), find_handler, hd);
                if (!memcmp(hd, "soun", 4)) sound++; else other++;
            }
            at += size;
        }
        if (sound && other) need |= MP4FIX_EXTRA_TRACKS;
    }
    if (moov_out) *moov_out = m; else free(m);
    return need;
}

int mp4fix_check(const char *path, int audio_only) {
    FILE *f = fopen(path, "rb");
    if (!f) return MP4FIX_NOT_MP4;
    Box b[MAX_BOXES];
    int n = 0;
    int need = examine(f, b, &n, NULL, audio_only);
    fclose(f);
    return need;
}

static int copy_range(FILE *in, FILE *out, uint64_t at, uint64_t len) {
    static unsigned char buf[64 * 1024];
    if (fseek(in, (long)at, SEEK_SET)) return -1;
    while (len) {
        size_t want = len > sizeof(buf) ? sizeof(buf) : (size_t)len;
        if (fread(buf, 1, want, in) != want || fwrite(buf, 1, want, out) != want) return -1;
        len -= want;
    }
    return 0;
}

int mp4fix_fix(const char *path, int audio_only) {
    FILE *f = fopen(path, "rb");
    if (!f) return MP4FIX_NOT_MP4;
    Box b[MAX_BOXES];
    int n = 0;
    unsigned char *m = NULL;
    int need = examine(f, b, &n, &m, audio_only);
    if (need <= 0) { fclose(f); free(m); return need; }
    int moov = find(b, n, "moov");
    uint64_t moov_size = b[moov].size;

    /* The new index: the same, without the tracks that are not sound. */
    if (need & MP4FIX_EXTRA_TRACKS) {
        unsigned h = header_len(m);
        uint64_t r = h, w = h;
        while (r + 8 <= moov_size) {
            uint64_t size = be32(m + r);
            if (size < 8 || r + size > moov_size) break;
            int keep = 1;
            if (!memcmp(m + r + 4, "trak", 4)) {
                char hd[5] = {0};
                each_child(m + r + header_len(m + r), size - header_len(m + r), find_handler, hd);
                keep = !memcmp(hd, "soun", 4);
            }
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
    uint64_t moov_at = at;
    at += moov_size;
    for (int i = 0; i < n; ++i) {
        if (i == ftyp || i == moov) continue;
        b[i].new_at = at;
        at += b[i].size;
    }
    Patch p = {b, n, 0};
    unsigned h = header_len(m);
    if (each_child(m + h, moov_size - h, patch_offsets, &p) < 0 || p.overflow) { fclose(f); free(m); return MP4FIX_FAILED; }

    char part[512];
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *o = fopen(part, "wb");
    if (!o) { fclose(f); free(m); return MP4FIX_FAILED; }
    int bad = 0;
    if (ftyp >= 0) bad |= copy_range(f, o, b[ftyp].at, b[ftyp].size);
    if (!bad && fwrite(m, 1, (size_t)moov_size, o) != moov_size) bad = 1;
    for (int i = 0; i < n && !bad; ++i) {
        if (i == ftyp || i == moov) continue;
        bad |= copy_range(f, o, b[i].at, b[i].size);
    }
    if (fclose(o)) bad = 1;
    fclose(f);
    free(m);
    if (bad) { remove(part); return MP4FIX_FAILED; }
    (void)moov_at;
    /* Swap: keep the original until the new file is in place. */
    char old[520];
    snprintf(old, sizeof(old), "%s.orig", path);
    remove(old);
    if (rename(path, old)) { remove(part); return MP4FIX_FAILED; }
    if (rename(part, path)) { rename(old, path); remove(part); return MP4FIX_FAILED; }
    remove(old);
    return need;
}

/* The first sound track's codec, from its stsd entry ("mp4a", "alac", ...). */
static int find_stsd(unsigned char *box, uint64_t size, void *ctx) {
    char *out = ctx;
    unsigned h = header_len(box);
    if (!memcmp(box + 4, "mdia", 4) || !memcmp(box + 4, "minf", 4) || !memcmp(box + 4, "stbl", 4))
        return each_child(box + h, size - h, find_stsd, ctx);
    if (!memcmp(box + 4, "stsd", 4) && size >= h + 16) { memcpy(out, box + h + 12, 4); return 1; }
    return 0;
}

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
            char hd[5] = {0};
            unsigned h = header_len(m + at);
            each_child(m + at + h, s - h, find_handler, hd);
            if (!memcmp(hd, "soun", 4)) each_child(m + at + h, s - h, find_stsd, out);
        }
        at += s;
    }
    free(m);
    return out[0] ? 0 : -1;
}
