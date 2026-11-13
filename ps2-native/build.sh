#!/bin/sh
# Builds YFM_PS2.ELF: the decompiled game (Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled,
# a fixed commit) with this port's patch and PS2 sources, compiled for the EE.
# Needs: git, python3, and the PS2 toolchain + PS2SDK (ps2dev) with PS2DEV set,
# plus gsKit, libpng, zlib and freetype from ps2dev's ports.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
UPSTREAM=https://github.com/Unchiga/Yu-Gi-Oh-Forbidden-Memories-Recompiled.git
COMMIT=36d499c13522402a83c5b4772714f66046f80357
WORK=${WORK:-$HERE/work}
: "${PS2DEV:?set PS2DEV to your ps2dev install (e.g. /usr/local/ps2dev)}"
if [ ! -d "$WORK/.git" ]; then
    git clone "$UPSTREAM" "$WORK"
fi
cd "$WORK"
git checkout -q -f "$COMMIT"
git clean -qfd -e tmp
git apply "$HERE/patches/upstream.patch"
rm -rf src/ps2 ps2
cp -r "$HERE/src-ps2" src/ps2
cp -r "$HERE/ps2" ps2
python3 ps2/build_ps2.py -j "${JOBS:-4}"
cp tmp/ps2/YFM_PS2.ELF "$HERE/YFM_PS2.ELF"
echo "pronto: $HERE/YFM_PS2.ELF"
