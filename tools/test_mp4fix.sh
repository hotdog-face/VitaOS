#!/bin/sh
# Builds src/mp4fix.c's rewrite code (-DMP4FIX_REWRITE: not part of VitaOS,
# which never rewrites files) for this computer and checks it against files
# made with ffmpeg. After a fix the sound must decode to exactly the same samples, the
# index must come first, and nothing may be lost:
#   - index last: every track and tag kept, no .orig left behind;
#   - cover art as a JPEG "video" track: that track only is left out, and the
#     untouched original stays as <name>.orig;
#   - chapters (a text track, as in audiobooks): kept;
#   - a film (flags 0): only reordered, video kept;
#   - cancel: the original is untouched;
#   - journal and sweep: leftovers of an interrupted fix are tidied, originals put back.
# Needs ffmpeg/ffprobe and a C compiler.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/main.c" <<'C'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp4fix.h"
int main(int c, char **v) {
    if (!strcmp(v[1], "sweep")) { printf("%d\n", mp4fix_sweep(v[2], 3)); return 0; }
    mp4fix_set_journal("journal.txt");
    if (!strcmp(v[1], "recover")) { printf("%d\n", mp4fix_recover()); return 0; }
    int flags = atoi(v[1]);
    Mp4fixJob job = {0};
    if (c > 3) job.cancel = 1;
    int r = mp4fix_check(v[2], flags), f = mp4fix_fix(v[2], flags, &job);
    printf("check %d fix %d after %d\n", r, f, mp4fix_check(v[2], flags));
    return 0;
}
C
cc -Wall -O2 -DMP4FIX_REWRITE -I"$here/src" -o "$work/fix" "$work/main.c" "$here/src/mp4fix.c"
cd "$work"
q="ffmpeg -v error -y"
$q -f lavfi -i "sine=frequency=440:duration=8" -c:a aac -b:a 192k -ar 44100 last.m4a
$q -f lavfi -i "color=c=red:s=300x300:d=1" -frames:v 1 cover.png
$q -i last.m4a -i cover.png -map 0:a -map 1:v -c:a copy -c:v mjpeg -disposition:v attached_pic covr.m4a
$q -i last.m4a -loop 1 -t 8 -i cover.png -map 0:a -map 1:v -c:a copy -c:v mjpeg -r 1 -f mp4 track.m4a
$q -i last.m4a -c copy -movflags +faststart good.m4a
printf ';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=4000\ntitle=One\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=4000\nEND=8000\ntitle=Two\n' > ch.txt
$q -i last.m4a -i ch.txt -map_metadata 1 -map_chapters 1 -c copy -f mp4 chapters.m4a
$q -f lavfi -i "testsrc=s=320x180:d=4:r=10" -f lavfi -i "sine=frequency=440:duration=4" -c:v mpeg4 -c:a aac film.mp4
cp film.mp4 clip.m4a
fail=0
audio() { ffmpeg -v error -i "$1" -map 0:a -f s16le - | shasum | cut -c1-12; }
traks() { ffprobe -v trace "$1" 2>&1 | grep -c "type:'trak'"; }
first() { ffprobe -v trace "$1" 2>&1 | grep -o "type:'moov'\|type:'mdat'" | head -1; }
chapters() { ffprobe -v error -show_chapters "$1" | grep -c "^\[CHAPTER\]" || true; }
check() {   # name file flags want_traks want_orig(0/1)
  f=$2; cp "$f" "keep-$f"; a0=$(audio "$f"); t0=$(traks "$f")
  ./fix "$3" "$f" > out.txt; a1=$(audio "$f"); t1=$(traks "$f")
  ok=1
  [ "$a0" = "$a1" ] || ok=0
  [ "$(first "$f")" = "type:'moov'" ] || ok=0
  [ "$t1" = "$4" ] || ok=0
  if [ "$5" = 1 ]; then cmp -s "$f.orig" "keep-$f" || ok=0; else [ ! -e "$f.orig" ] || ok=0; fi
  [ ! -e "$f.part" ] || ok=0
  printf '%-34s %s   (%s; tracks %s -> %s)\n' "$1" "$([ $ok = 1 ] && echo ok || echo FAIL)" "$(cat out.txt)" "$t0" "$t1"
  [ $ok = 1 ] || fail=1
}
check "index last" last.m4a 1 1 0
check "iTunes cover (a tag)" covr.m4a 1 1 0
check "cover as JPEG track: left out" track.m4a 1 1 1
check "already fine" good.m4a 1 1 0
check "chapters: text track kept" chapters.m4a 1 2 0
[ "$(chapters chapters.m4a)" = 2 ] && echo "chapters still readable            ok" || { echo "chapters still readable            FAIL"; fail=1; }
check "song with real video: kept" clip.m4a 1 2 0
check "film: reordered only" film.mp4 0 2 0
# fragmented files are left exactly as they are (their tables live in the fragments)
$q -i last.m4a -loop 1 -t 8 -i cover.png -map 0:a -map 1:v -c:a copy -c:v mjpeg -r 1 -movflags frag_keyframe+empty_moov -f mp4 frag.m4a
cp frag.m4a frag0.m4a; ./fix 1 frag.m4a > out.txt
if cmp -s frag.m4a frag0.m4a && [ ! -e frag.m4a.orig ] && [ ! -e frag.m4a.part ] && [ "$(audio frag.m4a)" = "$(audio frag0.m4a)" ]; then echo "fragmented + cover: left alone      ok   ($(cat out.txt))"; else echo "fragmented + cover: left alone      FAIL ($(cat out.txt))"; fail=1; fi
# cancel: nothing changes
cp last.m4a x.m4a 2>/dev/null; $q -f lavfi -i "sine=frequency=440:duration=8" -c:a aac c.m4a; cp c.m4a c0.m4a
./fix 1 c.m4a cancel > out.txt
if cmp -s c.m4a c0.m4a && [ ! -e c.m4a.part ] && [ ! -e c.m4a.orig ] && grep -q "fix -3" out.txt; then echo "cancel: original untouched         ok"; else echo "cancel: original untouched         FAIL"; fail=1; fi
# sweep: an interrupted swap (file missing, .orig and .part there), a stray .part, and a complete pair
mkdir -p sw/album
cp c0.m4a sw/album/a.m4a.orig; echo junk > sw/album/a.m4a.part       # crashed between the renames
cp c0.m4a sw/b.m4a; echo junk > sw/b.m4a.part                        # crashed while writing
cp c0.m4a sw/c.m4a; cp c0.m4a sw/c.m4a.orig                          # complete pair: leave both
echo keep > sw/notes.part                                            # not media: not ours
n=$(./fix sweep sw)
if [ "$n" = 1 ] && cmp -s sw/album/a.m4a c0.m4a && [ ! -e sw/album/a.m4a.orig ] && [ ! -e sw/album/a.m4a.part ] \
   && [ ! -e sw/b.m4a.part ] && [ -e sw/c.m4a.orig ] && [ -e sw/notes.part ]; then echo "sweep after a crash                ok"; else echo "sweep after a crash                FAIL ($n)"; fail=1; fi
# journal: what start-up recovery does, for the one file named in it
cp c0.m4a j.m4a.orig; echo junk > j.m4a.part; printf '%s' "j.m4a" > journal.txt          # stopped between the renames
n=$(./fix recover)
if [ "$n" = 1 ] && cmp -s j.m4a c0.m4a && [ ! -e j.m4a.part ] && [ ! -e j.m4a.orig ] && [ ! -e journal.txt ]; then echo "journal: original put back         ok"; else echo "journal: original put back         FAIL ($n)"; fail=1; fi
cp c0.m4a k.m4a; echo junk > k.m4a.part; printf '%s' "k.m4a" > journal.txt             # stopped while writing
n=$(./fix recover)
if [ "$n" = 0 ] && cmp -s k.m4a c0.m4a && [ ! -e k.m4a.part ] && [ ! -e journal.txt ]; then echo "journal: half-written copy removed ok"; else echo "journal: half-written copy removed FAIL"; fail=1; fi
[ "$(./fix recover)" = 0 ] && [ ! -e journal.txt ] && echo "journal: nothing pending           ok" || { echo "journal: nothing pending           FAIL"; fail=1; }
ls *.txt | grep -q journal && { echo "journal left behind after fixes    FAIL"; fail=1; } || echo "journal cleared after every fix    ok"
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
