"""Extract isolated, execution-free VM helpers for CurrentCharacterDataTests.

The generated fixture is local proprietary test data: never commit/distribute it.
Usage: script.py decrypted_snapshot.exe output.bin
"""
from pathlib import Path
import struct
import sys
import capstone
import pefile

pe = pefile.PE(sys.argv[1], fast_load=True)
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
decoder.detail = True
specs = [(0x11DB60, 248), (0x1156D0, 167), (0xA7CA0, 53), (0xFD7A0, 64), (0x11E990, 45)]
blocks = []
fixups = []
for index, (rva, size) in enumerate(specs):
    code = pe.get_data(rva, size)
    instructions = list(decoder.disasm(code, rva))
    assert sum(i.size for i in instructions) == size
    for instruction in instructions:
        if instruction.mnemonic == 'call':
            target = instruction.operands[0].imm
            matches = [i for i, spec in enumerate(specs) if spec[0] == target]
            assert instruction.bytes[0] == 0xE8 and len(matches) == 1
            fixups.append((index, instruction.address - rva + 1, matches[0]))
        elif instruction.group(capstone.CS_GRP_JUMP):
            assert rva <= instruction.operands[0].imm < rva + size
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM:
                assert operand.mem.base or operand.mem.index, 'Unexpected absolute dependency'
    blocks.append(code)
Path(sys.argv[2]).write_bytes(struct.pack('<8s6I', b'HCCDAT1\0', *(len(b) for b in blocks), len(fixups)) +
                             b''.join(blocks) + b''.join(struct.pack('<3I', *f) for f in fixups))
frame = pe.get_data(0x1082D0, 503)
assert frame[:10] == bytes.fromhex('558bec81ec24010000a1')
Path(sys.argv[2]).with_suffix('.frame.bin').write_bytes(frame)
print('Local property-get/map-find/reference/hash fixture extracted; no game launched.')
