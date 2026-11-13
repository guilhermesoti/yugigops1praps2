#!/bin/sh
# Compila o port: gera tmp/ps2/YFM_PS2.ELF e copia para release/.
# Requer PS2DEV apontando para o ps2dev (ee-gcc, PS2SDK, gsKit, libpng, zlib, freetype).
set -e
cd "$(dirname "$0")"
: "${PS2DEV:?defina PS2DEV (ex.: export PS2DEV=/usr/local/ps2dev)}"
python3 ps2/build_ps2.py -j "${JOBS:-4}"
mkdir -p release
cp tmp/ps2/YFM_PS2.ELF release/YFM_PS2.ELF
echo "pronto: release/YFM_PS2.ELF"
