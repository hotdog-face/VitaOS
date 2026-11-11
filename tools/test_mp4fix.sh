#!/bin/sh
# Builds src/mp4fix.c for this computer and checks it against files made with
# ffmpeg: index last, iTunes cover art, cover art as a video track, already fine. The sound must decode
# to exactly the same samples afterwards, and ffprobe must find the index
# first and a single track. Needs ffmpeg/ffprobe and a C compiler.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/main.c" <<'C'
#include <stdio.h>
#include "mp4fix.h"
int main(int c, char **v) { int r = mp4fix_check(v[1], 1); int f = mp4fix_fix(v[1], 1); printf("check %d fix %d after %d\n", r, f, mp4fix_check(v[1], 1)); return f < 0; }
C
cc -Wall -O2 -I"$here/src" -o "$work/fix" "$work/main.c" "$here/src/mp4fix.c"
ffmpeg -v error -f lavfi -i "sine=frequency=440:duration=8" -f lavfi -i "color=c=red:s=300x300:d=1" -frames:v 1 "$work/cover.png"
ffmpeg -v error -f lavfi -i "sine=frequency=440:duration=8" -c:a aac -b:a 192k -ar 44100 "$work/last.m4a"
ffmpeg -v error -i "$work/last.m4a" -i "$work/cover.png" -map 0:a -map 1:v -c:a copy -c:v mjpeg -disposition:v attached_pic "$work/covr.m4a"
ffmpeg -v error -i "$work/last.m4a" -loop 1 -t 8 -i "$work/cover.png" -map 0:a -map 1:v -c:a copy -c:v mjpeg -r 1 -f mp4 "$work/track.m4a"
ffmpeg -v error -i "$work/last.m4a" -c copy -movflags +faststart "$work/good.m4a"
fail=0
for f in last covr track good; do
  before=$(ffmpeg -v error -i "$work/$f.m4a" -map 0:a -f s16le - | shasum | cut -c1-16)
  "$work/fix" "$work/$f.m4a"
  after=$(ffmpeg -v error -i "$work/$f.m4a" -map 0:a -f s16le - | shasum | cut -c1-16)
  traks=$(ffprobe -v trace "$work/$f.m4a" 2>&1 | grep -c "type:'trak'")
  first=$(ffprobe -v trace "$work/$f.m4a" 2>&1 | grep -o "type:'moov'\|type:'mdat'" | head -1)
  echo "$f: audio $before -> $after, tracks: $traks, first: $first"
  [ "$before" = "$after" ] && [ "$traks" = 1 ] && [ "$first" = "type:'moov'" ] || fail=1
done
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }
