from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from yacp_book_profile import DEFAULTS, decode, encode


class ProfileTest(unittest.TestCase):
    def test_target_profile_is_strict_and_uses_points(self):
        settings = dict(DEFAULTS, fontSize=16, lineHeightPercent=90, sdFontFamilyName='AtkinsonHyperlegible')
        data = encode(settings)
        self.assertEqual(len(data), 66)
        self.assertEqual(decode(data), {'version': 4, 'flags': 1, 'generation': 1, 'settings': settings})
        for size in range(66):
            with self.assertRaises(ValueError):
                decode(data[:size])
        for offset in range(66):
            corrupt = bytearray(data)
            corrupt[offset] ^= 1
            with self.assertRaises(ValueError):
                decode(corrupt)

    def test_crosspoint_2_profile_uses_new_ranges_without_reinterpreting_v4(self):
        settings = dict(DEFAULTS, lineHeight=6, paragraphIndentSpaces=5, imageRendering=3, sdFontFamilyName="")
        del settings["lineSpacing"], settings["forceParagraphIndents"]
        data = encode(settings, version=5)
        self.assertEqual(len(data), 66)
        self.assertEqual(decode(data), {"version": 5, "flags": 1, "generation": 1, "settings": settings})
        self.assertEqual(decode(encode(DEFAULTS))["settings"]["lineSpacing"], 1)
        with self.assertRaises(ValueError):
            encode(dict(settings, lineHeight=7), version=5)
        with self.assertRaises(ValueError):
            encode(dict(DEFAULTS, lineSpacing=6), version=4)

    def test_forced_indent_requires_v6(self):
        settings = dict(DEFAULTS, lineHeight=2, paragraphIndentSpaces=6)
        del settings["lineSpacing"], settings["forceParagraphIndents"]
        with self.assertRaises(ValueError):
            encode(settings, version=5)
        self.assertEqual(decode(encode(settings, version=6))["settings"], dict(settings, sdFontFamilyName=""))

    def test_no_silent_truncation(self):
        for bad in ({'fontSize': 2}, {'fontSize': True}, {'lineHeightPercent': 1},
                    {'sdFontFamilyName': 'x' * 32}, {'sdFontFamilyName': 'x\0y'}, {'orientation': 4},
                    {'autoPageTurnSeconds': 0}, {'autoPageTurnSeconds': 121}, {'guideReadingEnabled': 2}):
            with self.assertRaises(ValueError):
                encode(dict(DEFAULTS, **bad))

    def test_legacy_and_interval_only(self):
        legacy = decode(encode(DEFAULTS, version=1))
        self.assertEqual(legacy['flags'], 1)
        self.assertNotIn('autoPageTurnSeconds', legacy['settings'])
        v2 = decode(encode(DEFAULTS, version=2))
        self.assertIn('autoPageTurnSeconds', v2['settings'])
        self.assertNotIn('guideReadingEnabled', v2['settings'])
        v3 = decode(encode(DEFAULTS, version=3))
        self.assertNotIn('publisherPageNumbers', v3['settings'])
        self.assertEqual(decode(encode(dict(DEFAULTS, publisherPageNumbers=1)))['settings']['publisherPageNumbers'], 1)
        interval = decode(encode(dict(DEFAULTS, autoPageTurnSeconds=7), flags=2))
        self.assertEqual(interval['flags'], 2)
        self.assertEqual(interval['settings']['autoPageTurnSeconds'], 7)
        for bad in (-1, 4, True):
            with self.assertRaises(ValueError):
                encode(DEFAULTS, flags=bad)


if __name__ == '__main__':
    unittest.main()
