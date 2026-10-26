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

    def test_unlock_native_callee_cleans_both_arguments(self):
        returns = [i for i in self.instructions(0xF0BD0, 0x7A) if i.mnemonic == 'ret']
        self.assertEqual(len(returns), 3)
        self.assertTrue(all(i.op_str == '8' for i in returns))
        self.assertRegex(self.source, r'using IsCharacterUnlockedFn\s*=\s*std::uint8_t\(__stdcall\*\)')

    def test_sync_insert_matches_the_native_roster_receiver(self):
        source = (pathlib.Path(__file__).resolve().parents[1] /
                  'src/Network/CharacterSyncExpansion.cpp').read_text()
        target = int(re.search(r'kInsertCharacterSyncEntryRva\s*=\s*(0x[\da-fA-F]+)', source)[1], 16)
        calls = [i.operands[0].imm for i in self.instructions(0x876F0, 0x440)
                 if i.mnemonic == 'call' and i.operands[0].type == capstone.x86.X86_OP_IMM]
        self.assertEqual(target, 0x2B030)
        self.assertIn(target, calls)
        self.assertEqual(self.pe.get_data(target, 6), bytes.fromhex('558bec6aff68'))
        self.assertEqual(self.pe.get_data(target + 0xE9, 3), bytes.fromhex('c21000'))
        enclosing = [i for i in self.instructions(0x12B070, 0x11C)
                     if i.address < 0x12B0E0 < i.address + i.size]
        self.assertEqual(len(enclosing), 1)
        self.assertEqual(enclosing[0].address, 0x12B0DC)

    def test_lobby_crash_is_argument_cleanup_after_dispatch(self):
        instruction = self.instructions(0x111EE5, 4)[0]
        self.assertEqual((instruction.mnemonic, instruction.op_str),
                         ('mov', 'byte ptr [eax + 8], 0'))

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

    def test_native_sender_serializes_the_actual_fresh_setting(self):
        code = self.pe.get_data(0xF216F, 0x41)
        self.assertEqual(code[:2], bytes.fromhex('8b0d'))
        self.assertEqual(int.from_bytes(code[2:6], 'little'), self.pe.OPTIONAL_HEADER.ImageBase + 0x391294)
        self.assertEqual(code[0xC:0x12], bytes.fromhex('81c114080000'))
        self.assertEqual(self.pe.get_data(0xF2187, 10), bytes.fromhex('c785c2fbffff20010000'))
        self.assertEqual(self.pe.get_data(0xF21AA, 6), bytes.fromhex('888513ebffff'))

    def test_native_receiver_calls_stdcall_host_mode_setter(self):
        calls = [i.operands[0].imm for i in self.instructions(0xF2390, 0x90)
                 if i.mnemonic == 'call' and i.operands[0].type == capstone.x86.X86_OP_IMM]
        self.assertIn(0x10A540, calls)
        self.assertEqual(self.pe.get_data(0x10A540, 8), bytes.fromhex('558bec83ec0c8b0d'))
        self.assertEqual(self.pe.get_data(0x10A55B, 6), bytes.fromhex('8d8914080000'))
        self.assertEqual(self.pe.get_data(0x10A571, 9), bytes.fromhex('8948148be55dc20400'))
        native_map = (pathlib.Path(__file__).resolve().parents[1] / 'src/NativeDB/Natives.h').read_text()
        self.assertRegex(native_map, r'CC_SetFreshModeEnabled\s*=\s*0x10A540')

    def test_native_menu_table_expansion_sites_are_instruction_immediates(self):
        self.assertEqual(self.pe.get_data(0x1443E0, 5), bytes.fromhex('682c5e0000'))
        self.assertEqual(self.pe.get_data(0x144413, 8), bytes.fromhex('6a2ec7072e000000'))
        self.assertEqual(self.pe.get_data(0x14441E, 5), bytes.fromhex('680c020000'))

    def test_main_pool_has_a_fixed_576_mib_budget(self):
        immediates = [operand.imm for i in self.instructions(0xF6BA0, 0xA0)
                      for operand in i.operands if operand.type == capstone.x86.X86_OP_IMM]
        self.assertIn(0x24000000, immediates)
        self.assertIn(0x2400001F, immediates)

    def test_character_capture_preserves_the_private_frame_update_abi(self):
        self.assertEqual(self.pe.get_data(0x1082D0, 10), bytes.fromhex('558bec81ec24010000a1'))
        self.assertEqual(self.pe.get_data(0x1082DE, 13), bytes.fromhex('33c58945fc5356578bf90f28c1'))
        returns = [i for i in self.instructions(0x1082D0, 503) if i.mnemonic == 'ret']
        self.assertTrue(returns)
        self.assertTrue(all(not i.op_str for i in returns))
        source = (pathlib.Path(__file__).resolve().parents[1] /
                  'src/Characters/CurrentCharacterData.cpp').read_text()
        self.assertIn('fxsave [esp]', source)
        self.assertIn('fxrstor [esp]', source)

    def test_native_property_and_array_storage_have_different_value_widths(self):
        # Numeric own-property getter reads the compact scalar at +8.
        self.assertEqual(self.pe.get_data(0x11D046, 5), bytes.fromhex('f30f2c4108'))
        # Array getter's numeric indexing multiplies by 16 and reads array+0x20.
        instructions = self.instructions(0x10E8A0, 0x8A)
        self.assertTrue(any(i.mnemonic == 'shl' and i.op_str == 'eax, 4' for i in instructions))
        # Array vtable+0xC returns its u16 length at +0x24 for property ID 1.
        self.assertEqual(int.from_bytes(self.pe.get_data(0x1CEC3C,4),'little'),
                         self.pe.OPTIONAL_HEADER.ImageBase + 0x11E990)


if __name__ == '__main__':
    unittest.main()
