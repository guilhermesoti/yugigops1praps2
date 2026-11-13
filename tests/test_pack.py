"""End-to-end checks with generated ISO9660 sectors and invented card tables."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import preparar as prep
from elfpack import embed, MARKER

def isorecord(name, extent, length, directory=False):
    n = len(name)
    rec = bytearray(33 + n + (n % 2 == 0))
    rec[0] = len(rec)
    for position, value in ((2,extent), (10,length)):
        struct.pack_into('<I',rec,position,value)
        struct.pack_into('>I',rec,position+4,value)
    rec[25] = 2 if directory else 0
    rec[28:32] = b'\x01\x00\x00\x01'
    rec[32] = n
    rec[33:33+n] = name
    return rec

def test_image(raw):
    files = (b'slus', b'wa', b'su')
    cells = [bytearray(2048) for _ in range(25)]
    cells[16][:7] = b'\x01CD001\x01'
    cells[16][156:190] = isorecord(b'\0',18,2048,True)
    valid = ((18, (isorecord(b'SLUS_014.11;1',20,4),isorecord(b'DATA',19,2048,True))),
             (19, (isorecord(b'WA_MRG.MRG;1',21,2),isorecord(b'SU.MRG;1',22,2))))
    for lba, children in valid:
        data = b''.join(children)
        cells[lba][:len(data)] = data
    for i, data in enumerate(files):
        cells[20+i][:len(data)] = data
    if raw:
        return b''.join(bytes(24)+data+bytes(280) for data in cells)
    return b''.join(cells)

def inputs_fixture():
    slus = bytearray(0x1D0800)
    wa = bytearray(0x2400000)
    su = bytearray(0x31000)
    for cid in range(1,723):
        at=0x801D4244-prep.DELTA+(cid-1)*4
        struct.pack_into('<I',slus,at,110 | (130<<9) | (1<<22) | (8<<18))
    struct.pack_into('<H',slus,0x801D5800-prep.DELTA+2,0x800)
    struct.pack_into('<I',slus,0x801D9000-prep.DELTA+4,0x8260) # fullwidth A
    slus[0x801D0800-prep.DELTA:0x801D0800-prep.DELTA+3] = bytes([1,1,255])
    slus[0x801D0000-prep.DELTA] = 255
    table=0xB63000+0x24800
    struct.pack_into('<H',wa,table+2,1500)
    wa[table+1500:table+1506]=bytes([1,0,2,3,0,0])
    equip=0xB63000+0x22000
    struct.pack_into('<4H',wa,equip,4,1,3,0)
    for index in range(1,40):
        at=0xE99800+0x1800*index
        struct.pack_into('<16H',wa,at,*([128]*16))
    wa[0]=1
    struct.pack_into('<H',wa,0x502,0x03e0)
    su[64*2048+16*2048:64*2048+16*2048+128] = bytes([0x11]*128)
    struct.pack_into('<H',su,0x30000+4*512+2,0x001f)
    return bytes(slus), bytes(wa), bytes(su)

class PackTests(unittest.TestCase):
    def test_iso_and_raw_paths(self):
        with tempfile.TemporaryDirectory() as d:
            for raw in (True,False):
                p=Path(d)/('Track 01.bin' if raw else 'data.iso')
                p.write_bytes(test_image(raw))
                with prep.DiscImage(p) as image:
                    self.assertEqual(image.read(*image.find('DATA/SU.MRG')),b'su')
                    self.assertEqual(image.read(*image.find('SLUS_014.11')),b'slus')
                with self.assertRaisesRegex(ValueError,'Tamanho'):
                    prep.input_files(p)
            cue=Path(d)/'game.cue'
            cue.write_text('FILE "Track 01.bin" BINARY\n TRACK 01 MODE2/2352\n INDEX 01 00:00:00\n')
            self.assertEqual(prep.data_track(cue),Path(d)/'Track 01.bin')

    def test_pack_and_elf(self):
        with tempfile.TemporaryDirectory() as d:
            base=Path(d); inp=inputs_fixture()
            for file,data in zip(prep.FILES,inp):
                target=base/file;target.parent.mkdir(exist_ok=True);target.write_bytes(data)
            with self.assertRaisesRegex(ValueError,'SHA-256'):
                prep.input_files(base)
            with patch.object(prep, 'DIGESTS', tuple(hashlib.sha256(v).hexdigest() for v in inp)):
                chunks=prep.pack_sections(*prep.input_files(base))
            data=prep.packed_data(chunks)
            self.assertEqual(prep.CARD.unpack_from(chunks[0])[2].rstrip(b'\x00'),b'AA')
            self.assertEqual(struct.unpack_from('<H',chunks[6],0)[0],31)
            self.assertEqual(chunks[5][0],1)
            self.assertEqual(chunks[2][1500:1506],bytes([1,0,2,3,0,0]))
            self.assertEqual(prep.HEADER.unpack_from(data)[-1],zlib.crc32(data[40:]))
            elfpath=ROOT/'YFM.ELF'
            if not elfpath.is_file(): elfpath=ROOT/'build/YFM.ELF'
            elf=elfpath.read_bytes(); out=embed(elf,data); at=elf.index(MARKER)+20
            self.assertEqual(len(out),len(elf)); self.assertEqual(out[at:at+len(data)],data)
            self.assertEqual(elf[:at],out[:at]); self.assertEqual(elf[at+len(data):],out[at+len(data):])
            with self.assertRaises(ValueError): embed(elf,bytes(0x180001))
            with self.assertRaises(ValueError): embed(b'bad',data)
            dest=base/'pack.elf';prep.write_atomic(dest,out);self.assertEqual(dest.read_bytes(),out)

if __name__ == '__main__':
    unittest.main()
