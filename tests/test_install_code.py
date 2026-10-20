import unittest
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from generate_install_code import install_crc,generate


class InstallCodeTests(unittest.TestCase):
    def test_silicon_labs_public_reference_vector(self):
        # Sample documented code: 83FE D340 7A93 9723 A5C6 39B2 6916 D505 C3B5
        vector=bytes.fromhex("83FED3407A939723A5C639B26916D505")
        self.assertEqual(install_crc(vector),0xB5C3)

    def test_strict_length_validation(self):
        with self.assertRaises(ValueError):
            install_crc(bytes(12))

    def test_random_payload_crc_and_format(self):
        code=generate()
        self.assertEqual(len(code),18)
        self.assertEqual(code[-2:],install_crc(code[:-2]).to_bytes(2,"little"))
        self.assertNotEqual(generate(),code)

if __name__=="__main__":
    unittest.main()
