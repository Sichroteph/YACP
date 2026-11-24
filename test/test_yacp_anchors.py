from pathlib import Path
import struct
import sys
import unittest
import io
from zipfile import ZipFile
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_legacy_anchor import chapter_html, first_cached_line, legacy_section_name, unique_text_offset


class LegacyAnchorTest(unittest.TestCase):
    def test_section_follows_explicit_book_engine(self):
        profile = bytearray(86)
        profile[0:5] = bytes([2, 4, 30, 0, 1])
        self.assertEqual(legacy_section_name(48, profile), '48_balanced.bin')
        profile[4] = 2
        self.assertEqual(legacy_section_name(48, profile), '48_light.bin')
        profile[4] = 0
        self.assertEqual(legacy_section_name(48, profile), '48.bin')

    def test_text_snapshot_does_not_choose_cached_engine(self):
        profile = bytearray(86)
        profile[0:5] = bytes([2, 1, 30, 0, 2])
        profile[21] = 1
        self.assertEqual(legacy_section_name(48, profile), '48.bin')
        self.assertEqual(legacy_section_name(48, bytes([1, 30, 0])), '48.bin')
        self.assertEqual(legacy_section_name(48), '48.bin')

    def test_unknown_or_corrupt_profile_does_not_guess_a_section(self):
        profile = bytearray(86)
        profile[0:5] = bytes([2, 4, 30, 0, 3])
        for data in (profile, profile[:-1], bytes([3, 0, 0])):
            with self.assertRaises(ValueError):
                legacy_section_name(48, data)

    def test_v27_separate_word_strings_and_invalid_lengths(self):
        header = bytearray(bytes([27]) + bytes(36))
        line = struct.pack('<HBhhH', 1, 1, 12, 34, 2)
        for word in (b'Target', b'sentence'):
            line += struct.pack('<I', len(word)) + word
        lut = len(header) + len(line)
        struct.pack_into('<HI', header, 19, 1, lut)
        data = header + line + struct.pack('<I', 37)
        self.assertEqual(first_cached_line(data, 0), 'Target sentence')
        bad_length = bytearray(data)
        struct.pack_into('<I', bad_length, 46, 65536)
        for invalid in (data[:-1], data[:10], bad_length):
            with self.assertRaises(ValueError):
                first_cached_line(invalid, 0)

    def test_spine_ignores_dangling_cover_like_both_firmwares(self):
        data = io.BytesIO()
        with ZipFile(data, 'w') as archive:
            archive.writestr('META-INF/container.xml', '<container><rootfiles><rootfile full-path="book.opf"/></rootfiles></container>')
            archive.writestr('book.opf', '<package><manifest><item id="chapter" href="chapter.xhtml"/></manifest><spine><itemref idref="missing-cover"/><itemref idref="chapter"/></spine></package>')
            archive.writestr('chapter.xhtml', '<body>Correct chapter</body>')
        self.assertEqual(chapter_html(data, 0), '<body>Correct chapter</body>')
        with self.assertRaises(ValueError):
            chapter_html(data, 1)

    def test_counts_codepoints_whitespace_and_entities_but_not_script(self):
        html = '<html><head><title>Hidden</title></head><body>\n A &amp; B<script>hidden</script><p>Target sentence</p></body></html>'
        self.assertEqual(unique_text_offset(html, 'Target sentence'), len('\n A & B'))

    def test_combining_accents_keep_original_offset(self):
        html = '<body>e\u0301 prefix\n<em>Target</em> sentence</body>'
        self.assertEqual(unique_text_offset(html, 'Target sentence'), len('e\u0301 prefix\n'))

    def test_duplicate_text_is_not_guessed(self):
        with self.assertRaises(ValueError):
            unique_text_offset('<body>Repeated sentence. Repeated sentence.</body>', 'Repeated sentence.')

    def test_v44_text_arena_and_truncation(self):
        header = bytearray(b'\xffCXS\x2c' + bytes(39))
        page = struct.pack('<HBhh', 1, 1, 12, 34)
        text = b'Target\0sentence\0'
        page += struct.pack('<HBBBH', 2, 0, 0, 0, len(text))
        page += struct.pack('<HHhhBB', 0, 7, 0, 40, 0, 0) + text
        lut = len(header) + len(page)
        struct.pack_into('<HI', header, 26, 1, lut)
        data = header + page + struct.pack('<I', 44)
        self.assertEqual(first_cached_line(data, 0), 'Target sentence')
        for invalid in (data[:-1], data[:10], bytes(50)):
            with self.assertRaises(ValueError):
                first_cached_line(invalid, 0)


if __name__ == '__main__':
    unittest.main()
