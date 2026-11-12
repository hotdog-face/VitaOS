#!/bin/sh
# Builds the three experimental parts into experimental/out/, for a VitaOS
# configured with -DVITAOS_EXPERIMENTAL=ON. Needs VITASDK.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/experimental/out"
mkdir -p "$out"
for part in shell kernel menu-app; do
    rm -rf "$here/experimental/$part/build"
    mkdir "$here/experimental/$part/build"
    (cd "$here/experimental/$part/build" && cmake .. >/dev/null && make >/dev/null)
done
cp "$here/experimental/shell/build/vitaos_shell.suprx" "$out/"
cp "$here/experimental/kernel/build/vitaos_k.skprx" "$out/"
cp "$here/experimental/menu-app/build/VitaOSPause.vpk" "$out/menu.vpk"
ls -l "$out"
