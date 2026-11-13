"""Place a verified data pack inside an existing R5900 ELF load segment."""
import struct

MARKER = b'RGFM_DATA_SLOT_1'

def embed(elf, payload):
    if elf[:6] != b'\x7fELF\x01\x01' or len(elf) < 52:
        raise ValueError('O modelo precisa ser um ELF MIPS de PS2.')
    kind, arch = struct.unpack_from('<HH', elf, 16)
    if (kind, arch) != (2, 8) or struct.unpack_from('<I',elf,36)[0] & 0x00ff0000 != 0x00920000:
        raise ValueError('Modelo incompatível com o Emotion Engine.')
    table = struct.unpack_from('<I', elf, 28)[0]
    step, count = struct.unpack_from('<HH', elf, 42)
    pos = elf.find(MARKER)
    if pos < 0 or elf.find(MARKER, pos + 1) >= 0 or pos + 20 > len(elf):
        raise ValueError('O modelo não contém um único espaço de dados YFM.')
    size = struct.unpack_from('<I', elf, pos + 16)[0]
    start = pos + 20
    if len(payload) > size or start + size > len(elf):
        raise ValueError('Os dados excedem o espaço reservado no modelo.')
    if step < 32 or table + count * step > len(elf):
        raise ValueError('Tabela de segmentos ELF danificada.')
    for i in range(count):
        tag, begin, _, _, length = struct.unpack_from('<5I', elf, table + i * step)
        if tag == 1 and begin <= pos and start + size <= begin + length:
            break
    else:
        raise ValueError('O espaço de dados está fora do segmento carregável.')
    return elf[:start] + payload + bytes(size-len(payload)) + elf[start + size:]
