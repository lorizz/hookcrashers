"""Extract only the three isolated allocator routines for offline x86 tests.

No game startup code, assets, Steam API, or process launch is included.
The generated fixture is local test data and must not be committed/distributed.
"""
import argparse
import pathlib
import struct

import capstone
import pefile

parser = argparse.ArgumentParser()
parser.add_argument('snapshot', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
pe = pefile.PE(str(args.snapshot), fast_load=True)
decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
decoder.detail = True
blocks = []
fixups = []
for name, rva, size in [('allocate', 0xDFE70, 247), ('free', 0xDFF70, 238), ('reset', 0xDFDF0, 125)]:
    code = pe.get_data(rva, size)
    instructions = list(decoder.disasm(code, rva))
    assert sum(i.size for i in instructions) == size, name
    for instruction in instructions:
        if instruction.mnemonic == 'call':
            assert name == 'reset', 'Unexpected call in pure allocator routine'
            assert instruction.size == 5 and instruction.bytes[0] == 0xE8
            assert instruction.operands[0].imm == 0x167AD0, 'Expected memset only'
            fixups.append(instruction.address - rva + 1)
        elif instruction.group(capstone.CS_GRP_JUMP):
            assert instruction.operands[0].type == capstone.x86.X86_OP_IMM
            assert rva <= instruction.operands[0].imm < rva + size, 'External jump'
        for operand in instruction.operands:
            if operand.type == capstone.x86.X86_OP_MEM:
                assert operand.mem.base or operand.mem.index, 'Unexpected absolute memory dependency'
    blocks.append(code)
assert len(fixups) == 2
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_bytes(struct.pack('<8s4I', b'HCPOOL1\0', *(len(b) for b in blocks), len(fixups)) +
                        b''.join(blocks) + struct.pack('<2I', *fixups))
print('Isolated allocate/free/reset fixture written:', args.output)
