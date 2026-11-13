#!/usr/bin/env python3
"""Prepare the PS2 data pack locally from the user's North American disc."""
import argparse
from contextlib import ExitStack
import hashlib
from pathlib import Path
import re
import struct
import sys
import tempfile
import unicodedata
import zlib

from fm.disc import DiscImage, GameFilesError
from fm import text_listing as text
from title import title_pixels
from elfpack import embed

FILES = ('SLUS_014.11', 'DATA/WA_MRG.MRG', 'DATA/SU.MRG')
DIGESTS = ('84a54ed74f3d0edd6d81380839f7e4ef5bfb21ecea18be9a062bd6bfa5a45c88',
           '2026d85d3f224e04afc74e6fde70938195048883192fe845ffcadb8b286cc3f4',
           'c34d1eef2445e15ed40d20ad325c9907c55123d03f35b8693e7031966ef52e51')
COUNT = 722
HEADER = struct.Struct('<8s8I')
CARD = struct.Struct('<IB3x48s')
DUELIST = struct.Struct('<32s1444s')
MAGIC = b'YFMPS2A1'
DELTA = 0x8000F800

def data_track(cue):
    name = None
    for row in cue.read_text(encoding='utf-8-sig', errors='replace').splitlines():
        file = re.match(r'\s*FILE\s+(?:"([^"]+)"|(.+?))\s+BINARY\s*$', row, re.I)
        if file:
            name = (file[1] or file[2]).replace('\\', '/')
        if name and re.match(r'\s*TRACK\s+\d+\s+MODE[12]/(2048|2352)\s*$', row, re.I):
            return cue.parent / name
    raise ValueError('O CUE precisa indicar uma faixa de dados MODE1 ou MODE2.')

def input_files(source):
    if source.suffix.lower() == '.cue':
        source = data_track(source)
    if source.name.upper() == FILES[0]:
        source = source.parent
    with ExitStack() as stack:
        image = None if source.is_dir() else stack.enter_context(DiscImage(source))
        sizes = (0x1D0800, 0x2400000)
        result = []
        for i, name in enumerate(FILES):
            if image:
                entry = image.find(name)
                if not entry:
                    raise ValueError(f'{name} ausente. Use a faixa de dados do disco americano SLUS-01411.')
                lba, size = entry
                if size > 64 * 1024 * 1024 or (i < len(sizes) and size != sizes[i]):
                    raise ValueError(f'Tamanho de {name} incompatível com a edição americana.')
                data = image.read(lba, size)
            else:
                path = source / name
                if not path.is_file():
                    by_name = {p.name.upper(): p for p in source.iterdir()}
                    if '/' in name:
                        folder = by_name.get('DATA')
                        if folder and folder.is_dir():
                            path = next((p for p in folder.iterdir() if p.name.upper() == Path(name).name), path)
                    else:
                        path = by_name.get(name, path)
                if path.stat().st_size > 64 * 1024 * 1024:
                    raise ValueError(f'Tamanho inesperado de {name}.')
                data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if digest != DIGESTS[i]:
                raise ValueError(f'{name} tem SHA-256 {digest}. Esta versão usa o disco americano SLUS-01411 sem modificações.')
            result.append(data)
        return result

class Image:
    def __init__(self, slus):
        self.data = slus
    def bytes(self, address, count):
        at = address - DELTA
        return self.data[at:at + count]
    def u16(self, address):
        return struct.unpack_from('<H', self.data, address - DELTA)[0]
    def u32(self, address):
        return struct.unpack_from('<I', self.data, address - DELTA)[0]

def fixed_name(name, length):
    substitutions = str.maketrans({'‘': "'", '’': "'", '·': '.', '«': '<', '»': '>'})
    encoded = unicodedata.normalize('NFKD', name.translate(substitutions)).encode('ascii', 'replace')
    return encoded[:length - 1]

def read_name(image, glyphs, index):
    pointer = 0x801D0000 + image.u16(text.NAME_TABLE + index * 2)
    result = []
    for code in image.bytes(pointer, 80):
        if code >= 0xF0:
            break
        result.append(glyphs.get(code, '?'))
    return ''.join(result)

def pack_sections(slus, wa, su):
    image = Image(slus)
    glyphs = text.glyph_characters(image)
    names = text.card_names(image, glyphs)
    cards = bytearray()
    for cid in range(1, COUNT + 1):
        stats = image.u32(0x801D4244 + (cid - 1) * 4)
        attribute = image.bytes(0x801D5332 + cid, 1)[0]
        cards.extend(CARD.pack(stats, attribute, fixed_name(names[cid], 48)))
    opponents = bytearray()
    for index in range(40):
        at = 0xE99800 + 0x1800 * index
        name = read_name(image, glyphs, 0x328 + index)
        opponents.extend(DUELIST.pack(fixed_name(name, 32), wa[at:at + COUNT * 2]))
    thumbs = bytearray()
    for i in range(COUNT):
        thumbs.extend(wa[i * 2048:i * 2048 + 0x580])
    return (cards, wa[0xB63000 + 0x22000:0xB63000 + 0x24800],
            wa[0xB63000 + 0x24800:0xB63000 + 0x34800], opponents,
            wa[0xF55000:0xF55000 + 40 * 0x980], thumbs, title_pixels(su))

def packed_data(sections):
    chunks = tuple(sections)
    crc = 0
    for chunk in chunks:
        crc = zlib.crc32(chunk, crc)
    return HEADER.pack(MAGIC, *(len(s) for s in chunks), crc) + b''.join(chunks)

def write_atomic(target, data):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
        temporary.replace(target)
    finally:
        if temporary:
            temporary.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description='Extrai cartas e imagens para o teste nativo de PS2.')
    parser.add_argument('disco', type=Path, help='BIN, CUE, ISO ou pasta extraída do disco dos EUA')
    parser.add_argument('-o', '--saida', type=Path, default=Path('YFM_DISCO.ELF'))
    parser.add_argument('--modelo', type=Path, default=Path(__file__).resolve().parents[1] / 'YFM.ELF')
    args = parser.parse_args()
    try:
        if args.saida.suffix.upper() != '.ELF':
            raise ValueError('Use um nome com extensão .ELF para a saída.')
        if args.saida.resolve() == args.modelo.resolve():
            raise ValueError('Escolha um arquivo de saída diferente do modelo.')
        print('Lendo e verificando os arquivos do disco...')
        data = packed_data(pack_sections(*input_files(args.disco)))
        write_atomic(args.saida, embed(args.modelo.read_bytes(), data))
    except (OSError, ValueError, struct.error, GameFilesError) as exc:
        print(f'Falha: {exc}', file=sys.stderr)
        return 1
    print(f'Pronto: {args.saida.resolve()}')
    return 0

if __name__ == '__main__':
    sys.exit(main())
