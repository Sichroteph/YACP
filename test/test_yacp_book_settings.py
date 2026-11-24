from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from yacp_book_settings_migration import book_candidate


class BookSettingsTest(unittest.TestCase):
    def record(self, family=b'', index=2, flags=5):
        data = bytearray(86)
        data[:5] = bytes([2, flags, 30, 0, 0])
        data[5:22] = bytes([0, index, 100, 0, 5, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0])
        data[22:22 + len(family)] = family
        return data

    def test_sd_catalogue_index_and_builtin_variant(self):
        standard = book_candidate(self.record(), {}, 'tiny')
        self.assertEqual(standard['settings']['fontSize'], 14)
        xlarge = book_candidate(self.record(), {}, 'xlarge')
        self.assertEqual(xlarge['settings']['fontSize'], 20)
        for variant in ('tiny', 'xlarge'):
            result = book_candidate(self.record(b'Test'), {'sdFontSizeRange': 2}, variant, {'Test': [12, 14, 16, 18]})
            self.assertEqual(result['settings']['fontSize'], 16)
            self.assertNotIn('uiTheme', result['settings'])
            self.assertFalse(result['ready_to_activate'])
            self.assertIn('fontSize', book_candidate(self.record(b'Test'), {}, variant)['blocked_keys'])

    def test_inactive_profile_is_not_applied(self):
        result = book_candidate(self.record(b'Unused', flags=4), {}, 'tiny')
        self.assertEqual(result['settings'], {})
        self.assertNotIn('auto_page_turn_seconds', result)
        self.assertEqual(book_candidate(bytes([1, 30, 0]), {}, 'tiny')['auto_page_turn_seconds'], 30)
        self.assertIn('autoPageTurnSeconds', book_candidate(bytes([1, 255, 0]), {}, 'tiny')['blocked_keys'])


if __name__ == '__main__':
    unittest.main()
