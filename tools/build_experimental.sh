#!/bin/sh
# Builds the two experimental parts (shell plugin, menu app) into experimental/out/, for a VitaOS
# configured with -DVITAOS_EXPERIMENTAL=ON. Needs VITASDK.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/experimental/out"
mkdir -p "$out"
for part in shell menu-app; do
    rm -rf "$here/experimental/$part/build"
    mkdir "$here/experimental/$part/build"
    (cd "$here/experimental/$part/build" && cmake .. >/dev/null && make >/dev/null)
done
cp "$here/experimental/shell/build/vitaos_shell.suprx" "$out/"
rm -f "$out/vitaos_k.skprx"
cp "$here/experimental/menu-app/build/VitaOSPause.vpk" "$out/menu.vpk"
ls -l "$out"
