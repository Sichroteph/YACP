import base64
from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from yacp_credential_migration import fnv1a, xor_bytes
from yacp_opds_migration import candidate

SOURCE = bytes.fromhex('102030405060')
TARGET = bytes.fromhex('708090a0b0c0')


def row(password=b'synthetic', filename='author_title'):
    payload = b'CPV1' + struct.pack('<I', fnv1a(SOURCE + password)) + password
    return {'name': 'QA', 'url': 'https://example.invalid/opds', 'username': 'qa',
            'password_obf': base64.b64encode(xor_bytes(payload, SOURCE)).decode(), 'filenameFormat': filename}


class OpdsMigrationTest(unittest.TestCase):
    def test_rekey_preserves_order_name_and_long_password(self):
        source = {'servers': [row(b'a' * 100), row(b'b', 'author_title')]}
        before = deepcopy(source)
        result = candidate(source, {}, SOURCE, TARGET)
        self.assertEqual(source, before)
        self.assertEqual(result, candidate(source, {}, SOURCE, TARGET))
        self.assertFalse(result['blocked'])
        for item, expected in zip(result['candidate']['servers'], (b'a' * 100, b'b')):
            self.assertEqual(xor_bytes(base64.b64decode(item['password_obf']), TARGET), expected)
        self.assertFalse(result['ready_to_activate'])

    def test_legacy_and_authoritative_empty_store(self):
        legacy = {'opdsServerUrl': 'https://example.invalid', 'opdsUsername': 'qa', 'opdsPassword': 'test'}
        self.assertEqual(len(candidate(None, legacy, SOURCE, TARGET)['candidate']['servers']), 1)
        self.assertFalse(candidate({'servers': []}, legacy, SOURCE, TARGET)['candidate']['servers'])
        self.assertFalse(candidate(None, {}, SOURCE, TARGET)['candidate']['servers'])

    def test_per_server_format_is_not_flattened(self):
        result = candidate({'servers': [row(), row(filename='title_author')]}, {}, SOURCE, TARGET)
        self.assertEqual(result['blocked'], [])
        self.assertEqual([r['filenameFormat'] for r in result['candidate']['servers']], [0, 1])
        self.assertEqual(result['settings'], {})
        result = candidate({'servers': [row(filename='title_author')]}, {}, SOURCE, TARGET)
        self.assertEqual(result['settings'], {})
        self.assertEqual(result['candidate']['servers'][0]['filenameFormat'], 1)

    def test_corruption_unknown_fields_and_capacity_stop_conversion(self):
        with self.assertRaises(ValueError):
            candidate({'servers': [row()]}, {}, TARGET, TARGET)
        for bad in ({'servers': [row()] * 9}, {'servers': [dict(row(), extra=1)]},
                    {'servers': [dict(row(), password='conflicting')]},
                    {'servers': [dict(row(), filenameFormat='unknown')]},
                    {'servers': [dict(row(), password_obf='corrupt')]}):
            with self.assertRaises(ValueError):
                candidate(bad, {}, SOURCE, TARGET)


if __name__ == '__main__':
    unittest.main()
