"""Offline compatibility checks; never execute or launch the game.

Set HC_TEST_SNAPSHOT to the decrypted PE snapshot, then run unittest discovery.
Requires pefile and capstone. The proprietary snapshot is not committed.
"""
import os
import pathlib
import re
import unittest

import capstone
import pefile


@unittest.skipUnless(os.environ.get('HC_TEST_SNAPSHOT'), 'HC_TEST_SNAPSHOT is not set')
class SupportedExecutableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pe = pefile.PE(os.environ['HC_TEST_SNAPSHOT'], fast_load=True)
        cls.decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        cls.decoder.detail = True
        cls.source = (pathlib.Path(__file__).resolve().parents[1] /
                      'src/Localization/LocalizationSystem.cpp').read_text()

    @classmethod
    def instructions(cls, rva, length):
        return list(cls.decoder.disasm(cls.pe.get_data(rva, length), rva))

    def test_old_unlock_target_is_inside_a_branch(self):
        enclosing = [i for i in self.instructions(0xF0B40, 0x34)
                     if i.address < 0xF0B60 < i.address + i.size]
        self.assertEqual(len(enclosing), 1)
        self.assertEqual(enclosing[0].mnemonic, 'je')
        self.assertEqual(enclosing[0].address, 0xF0B5F)

    def test_fixed_unlock_target_is_called_by_the_game_dispatcher(self):
        target = int(re.search(r'kIsCharacterUnlockedRva\s*=\s*(0x[\da-fA-F]+)', self.source)[1], 16)
        calls = [i.operands[0].imm for i in self.instructions(0x104750, 0x1A00)
                 if i.mnemonic == 'call' and i.operands[0].type == capstone.x86.X86_OP_IMM]
        self.assertIn(target, calls)
        self.assertEqual(target, 0xF0BD0)
        first = self.instructions(target, 3)
        self.assertEqual([(i.mnemonic, i.op_str) for i in first],
                         [('push', 'ebp'), ('mov', 'ebp, esp')])

    def test_crash_address_is_the_null_destination_zero_fill(self):
        instruction = self.instructions(0x167B0C, 2)[0]
        self.assertIn('stosb', instruction.mnemonic)
        self.assertIn('edi', instruction.op_str)

    def test_runtime_safety_signatures_accept_the_supported_snapshot(self):
        def source_bytes(name):
            body = re.search(r'const std::uint8_t ' + name + r'\[\]\s*\{([^}]+)\}', self.source)[1]
            return bytes(int(value, 16) for value in re.findall(r'0x[\da-fA-F]+', body))
        prefix, suffix = source_bytes('unlockPrefix'), source_bytes('unlockSuffix')
        self.assertEqual(self.pe.get_data(0xF0BD0, len(prefix)), prefix)
        self.assertEqual(self.pe.get_data(0xF0BD0 + 9, len(suffix)), suffix)
        profile_global = int.from_bytes(self.pe.get_data(0xF0BD0 + 5, 4), 'little')
        self.assertEqual(profile_global, self.pe.OPTIONAL_HEADER.ImageBase + 0x391284)
        numeric = source_bytes('numericPrefix')
        self.assertEqual(self.pe.get_data(0x115E50, len(numeric)), numeric)

    def test_main_pool_has_a_fixed_576_mib_budget(self):
        immediates = [operand.imm for i in self.instructions(0xF6BA0, 0xA0)
                      for operand in i.operands if operand.type == capstone.x86.X86_OP_IMM]
        self.assertIn(0x24000000, immediates)
        self.assertIn(0x2400001F, immediates)


if __name__ == '__main__':
    unittest.main()
