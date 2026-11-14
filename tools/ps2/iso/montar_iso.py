#!/usr/bin/env python3
"""Monta a ISO de PS2 do port (para o OPL ou para gravar).

    python montar_iso.py YFM_PS2.ELF "Yu-Gi-Oh! Forbidden Memories (USA).bin" [pt-br.txt] [saida.iso]

A ISO leva o ELF como RGPM_001.01 (o ID que o OPL usa para as capas), o
SYSTEM.CNF, a imagem do SEU disco de PS1 como YFM.BIN e o portugues em
LANG/PTBR.TXT. Precisa do pycdlib (pip install pycdlib).
"""
import io, os, sys
import pycdlib

GAME_ID = "RGPM_001.01"


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    elf, disc = argv[1], argv[2]
    lang = argv[3] if len(argv) > 3 and argv[3].lower().endswith(".txt") else None
    out = next((a for a in argv[3:] if a.lower().endswith(".iso")), GAME_ID + ".Yu-Gi-Oh FM PS2.iso")
    cnf = ("BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n" % GAME_ID).encode()
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=1, sys_ident="PLAYSTATION", vol_ident="YFM_PS2_PORT")
    iso.add_fp(io.BytesIO(cnf), len(cnf), "/SYSTEM.CNF;1")
    iso.add_file(elf, "/%s;1" % GAME_ID)
    if lang:
        iso.add_directory("/LANG")
        iso.add_file(lang, "/LANG/PTBR.TXT;1")
    iso.add_file(disc, "/YFM.BIN;1")
    iso.write(out)
    iso.close()
    print("pronto:", out, "(%d MB)" % (os.path.getsize(out) >> 20))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
