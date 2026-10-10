"""Check the i386 call relocations used by the freestanding harness.

ELF R_386_PC32 uses S+A-P; a direct CALL/JMP needs A=-4. MinGW COFF
DISP32 uses the end of the displacement instead. GNU objcopy changes the
relocation type but leaves the COFF addend at zero, producing a call to S+4.
"""
from pathlib import Path
import struct


class Elf32:
    def __init__(self, data):
        self.data = bytearray(data)
        if self.data[:7] != b'\x7fELF\x01\x01\x01':
            raise ValueError('expected little-endian ELF32')
        self.kind, machine = struct.unpack_from('<HH', self.data, 16)
        if machine != 3:
            raise ValueError('expected i386 ELF')
        offset = struct.unpack_from('<I', self.data, 32)[0]
        size, count = struct.unpack_from('<HH', self.data, 46)
        if size != 40:
            raise ValueError('unexpected ELF section header size')
        self.sections = [struct.unpack_from('<10I', self.data, offset + i * size)
                         for i in range(count)]

    def calls(self):
        for rel in self.sections:
            if rel[1] != 9:  # SHT_REL
                continue
            if rel[9] != 8:
                raise ValueError('unexpected relocation size')
            target = self.sections[rel[7]]
            symbols = self.sections[rel[6]]
            strings = self.sections[symbols[6]]
            for pos in range(rel[4], rel[4] + rel[5], 8):
                address, info = struct.unpack_from('<II', self.data, pos)
                if info & 255 != 2:  # R_386_PC32
                    continue
                sym = struct.unpack_from('<IIIBBH', self.data,
                                         symbols[4] + (info >> 8) * symbols[9])
                # Section-relative local branches may intentionally have an
                # offset. Check named global/weak direct calls and tail jumps.
                if sym[3] >> 4 not in (1, 2) or sym[3] & 15 == 3:
                    continue
                offset = address - target[3] if self.kind == 2 else address
                field = target[4] + offset
                if not (target[2] & 4) or offset < 1 or offset + 4 > target[5]:
                    raise ValueError('PC32 reference outside executable section')
                if self.data[field - 1] not in (0xe8, 0xe9):
                    raise ValueError('unsupported named PC32 reference (expected CALL/JMP)')
                start = strings[4] + sym[0]
                end = self.data.index(0, start)
                name = self.data[start:end].decode('ascii')
                yield field, address, sym[1], sym[5], name


def normalize_coff_calls(path):
    """Only for freshly objcopy-converted MinGW assembly, never native ELF.

    Reject unexpected addends/defined targets instead of guessing. Our assembly
    uses PC32 only for direct calls to undefined external functions.
    """
    path = Path(path)
    elf = Elf32(path.read_bytes())
    if elf.kind != 1:
        raise ValueError('expected relocatable object')
    calls = list(elf.calls())
    for field, _, value, section, name in calls:
        addend = struct.unpack_from('<i', elf.data, field)[0]
        if section != 0 or value != 0 or addend != 0:
            raise ValueError(f'{name}: unsupported COFF call or already normalized object')
        struct.pack_into('<i', elf.data, field, -4)
    path.write_bytes(elf.data)
    return len(calls)


def verify_linked_calls(path):
    elf = Elf32(Path(path).read_bytes())
    if elf.kind != 2:
        raise ValueError('expected linked executable')
    count = 0
    for field, address, value, section, name in elf.calls():
        displacement = struct.unpack_from('<i', elf.data, field)[0]
        actual = (address + 4 + displacement) & 0xffffffff
        if not section or actual != value:
            raise ValueError(f'call to {name} lands at 0x{actual:08x}, '
                             f'expected entry 0x{value:08x}')
        count += 1
    if not count:
        raise ValueError('no call relocations; link with --emit-relocs')
    return count
