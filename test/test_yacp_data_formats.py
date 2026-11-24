import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("formats", Path(__file__).resolve().parents[1] / "scripts/yacp_data_formats.py")
formats = importlib.util.module_from_spec(spec)
spec.loader.exec_module(formats)


class YacpFormatsTest(unittest.TestCase):
    def test_per_book_settings_packed_layout_and_flags(self):
        raw = bytearray(86)
        raw[:5] = bytes([2, 7, 30, 0, 1])
        raw[5:22] = bytes([1, 2, 95, 0, 5, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 1])
        raw[22:26] = b'Test'
        result = formats.book_reader_settings(raw)
        self.assertEqual(result['auto_page_turn_seconds'], 30)
        self.assertEqual(result['settings']['sdFontFamilyName'], 'Test')
        self.assertEqual(result['settings']['lineHeightPercent'], 95)
        self.assertTrue(result['custom_reader_settings'])
        self.assertTrue(result['render_mode_override'])
        self.assertNotIn('sdFontSizeRange', result['settings'])
        self.assertEqual(formats.book_reader_settings(bytes([1, 120, 0]))['auto_page_turn_seconds'], 120)
        for bad in (b'', bytes([3]) + bytes(85), raw[:-1], raw + b'0', bytes([2, 8]) + bytes(84),
                    raw[:22] + b'x' * 64):
            with self.assertRaises(ValueError):
                formats.book_reader_settings(bad)

    def test_esp32_hash_matches_preexisting_card_cache_names(self):
        self.assertEqual(formats.esp32_book_path('/Alice-in-Wonderland-Lewis-Carroll.epub'), 'epub_1509110002')
        self.assertEqual(formats.esp32_book_path('/nuit du renard, La - Mary Higgins Clark.epub'), 'epub_3942829618')
        self.assertNotEqual(formats.esp32_book_path('/book.epub'), formats.esp32_book_path('/Book.epub'))

    def test_crosspoint48_committed_page_offsets(self):
        data = bytearray(68)
        data[0] = 48
        struct.pack_into('<H', data, 21, 2)
        struct.pack_into('<I', data, 23, 50)
        struct.pack_into('<I', data, 39, 60)
        struct.pack_into('<II', data, 50, 43, 46)
        struct.pack_into('<II', data, 60, 0, 1047)
        self.assertEqual(formats.crosspoint48_page_offsets(data), [0, 1047])
        partial = bytearray(data)
        partial[0] = 0xFE - (48 - 28)
        partial += struct.pack('<II', 1000, 2000)
        self.assertEqual(formats.crosspoint48_page_offsets(partial), [0, 1047])
        for invalid in (data[:66], bytes([0]) + data[1:], partial[:-1]):
            with self.assertRaises(ValueError):
                formats.crosspoint48_page_offsets(invalid)

    def test_crosspoint48_rejects_invalid_locations(self):
        data = bytearray(68)
        data[0] = 48
        struct.pack_into('<H', data, 21, 2)
        struct.pack_into('<I', data, 23, 50)
        struct.pack_into('<I', data, 39, 60)
        struct.pack_into('<II', data, 50, 43, 46)
        struct.pack_into('<II', data, 60, 1047, 0)
        with self.assertRaises(ValueError):
            formats.crosspoint48_page_offsets(data)
        struct.pack_into('<II', data, 60, 0, 1047)
        struct.pack_into('<II', data, 50, 43, 51)
        with self.assertRaises(ValueError):
            formats.crosspoint48_page_offsets(data)

    def test_yacp49_indent_header_and_partial_watermark(self):
        data = bytearray(69)
        data[0] = 49
        data[21] = 1
        struct.pack_into('<H', data, 22, 2)
        struct.pack_into('<I', data, 24, 51)
        struct.pack_into('<I', data, 40, 61)
        struct.pack_into('<II', data, 51, 44, 47)
        struct.pack_into('<II', data, 61, 0, 100)
        self.assertEqual(formats.crosspoint48_page_offsets(data), [0, 100])
        data[0] = 233
        data += struct.pack('<II', 500, 1000)
        self.assertEqual(formats.crosspoint48_page_offsets(data), [0, 100])
        data[21] = 2
        with self.assertRaises(ValueError):
            formats.crosspoint48_page_offsets(data)
        data[21] = 0
        with self.assertRaises(ValueError):
            formats.crosspoint48_page_offsets(data[:-1])

    def test_book_statistics_preserve_dates_flags_and_buckets(self):
        data = bytes([6]) + struct.pack("<HII", 42, 123456, 789) + bytes([1])
        data += struct.pack("<HHB", 45, 17, 15)
        data += struct.pack("<HBBHBB", 2026, 9, 15, 2026, 9, 30)
        data += struct.pack("<11II", *range(11), 1234)
        result = formats.book_stats(data)
        self.assertEqual(result["seconds"], 123456)
        self.assertEqual(result["finished_date"], "2026-09-30")
        self.assertEqual(result["flags"], 15)
        self.assertEqual(result["day_of_week_seconds"], list(range(4, 11)))
        self.assertEqual(result["estimated_time_left_seconds"], 1234)
        for invalid in (data[:-1], data + b"x", bytes([7]) + data[1:]):
            with self.assertRaises(ValueError):
                formats.book_stats(invalid)

    def test_global_statistics_preserve_entire_reading_history(self):
        data = bytes([3]) + struct.pack("<15I", *range(15)) + struct.pack("<I", 9000)
        data += bytes(range(92)) + struct.pack("<H", 365)
        result = formats.global_stats(data)
        self.assertEqual(result["history_bits_hex"], bytes(range(92)).hex())
        self.assertEqual(result["longest_streak"], 365)
        with self.assertRaises(ValueError):
            formats.global_stats(data[:-1])

    def test_finished_index_preserves_key_and_truncated_utf8_bytes(self):
        data = b"CPFB\x03\x01\x00\x00" + struct.pack("<QI", 2**63 + 1, 12345)
        data += struct.pack("<HBBHBB", 2026, 1, 1, 2026, 2, 2)
        data += struct.pack("<H", 2) + b"A\xc3" + struct.pack("<H", 6) + b"Author"
        result = formats.finished_books(data)["books"][0]
        self.assertEqual(result["path_key"], str(2**63 + 1))
        self.assertEqual(result["title_utf8_hex"], "41c3")
        self.assertEqual(result["author"], "Author")
        for invalid in (data[:-1], data + b"x"):
            with self.assertRaises(ValueError):
                formats.finished_books(invalid)

    def test_daily_v1_upgrade_is_lossless_and_repeatable(self):
        old = b"CRHM\x01" + struct.pack("<I", 9000) + bytes([255, 20]) + bytes(728)
        upgraded = formats.upgrade_daily_history(old)
        self.assertEqual(len(upgraded), 1469)
        self.assertEqual(formats.daily_history(upgraded)["minutes"], formats.daily_history(old)["minutes"])
        self.assertEqual(formats.upgrade_daily_history(upgraded), upgraded)
        with self.assertRaises(ValueError):
            formats.daily_history(old[:-1])

    def test_standard_and_xlarge_indices_are_distinct(self):
        self.assertEqual(formats.font_point_size({"fontSize": 0}, "tiny"), 10)
        self.assertEqual(formats.font_point_size({"fontSize": 0}, "xlarge"), 16)
        self.assertEqual(formats.font_point_size({"fontSize": 2}, "xlarge"), 20)

    def test_sd_catalogue_index_precedes_nominal_range(self):
        settings = {"fontSize": 0, "sdFontSizeRange": 2, "sdFontFamilyName": "AtkinsonHyperlegibleNext"}
        for variant in ('tiny', 'xlarge'):
            self.assertEqual(formats.font_point_size(settings, variant, [12, 14, 16, 18]), 12)
            self.assertEqual(formats.font_point_size(dict(settings, fontSize=2), variant, [12, 14, 16, 18]), 16)
            self.assertEqual(formats.font_point_size(dict(settings, fontSize=7), variant, [12, 14, 16, 18]), 18)
        self.assertEqual(formats.font_point_size(settings, 'tiny', [14]), 14)

    def test_invalid_font_index_and_missing_sd_catalogue_fail(self):
        for settings in ({"fontSize": 4}, {"fontSize": -1},
                         {"fontSize": 0, "sdFontFamilyName": "Example"}):
            with self.assertRaises(ValueError):
                formats.font_point_size(settings, "tiny")

    def test_progress_preserves_offset(self):
        self.assertEqual(formats.progress_record(struct.pack("<HHHI", 2, 3, 20, 456)),
                         {"spine": 2, "page": 3, "chapter_pages": 20, "visible_text_offset": 456})
        with self.assertRaises(ValueError):
            formats.progress_record(b"\x00" * 5)

    def test_bookmark_preserves_anchor_snippet_and_timestamp(self):
        def string(value):
            raw = value.encode("utf-8")
            return struct.pack("<I", len(raw)) + raw
        data = bytes([5]) + struct.pack("<H", 1)
        data += string("Title") + string("Author") + string("/Book.epub")
        data += struct.pack("<HfI", 2, 0.5, 12345) + b"Chapter".ljust(48, b"\0")
        data += struct.pack("<H", 12) + b"Snippet".ljust(64, b"\0")
        record = formats.bookmarks(data)["bookmarks"][0]
        self.assertEqual(record["paragraph_index"], 12)
        self.assertEqual(record["snippet"], "Snippet")
        self.assertEqual(record["timestamp"], 12345)
        for truncated in (data[:2], data[:-1]):
            with self.assertRaises(ValueError):
                formats.bookmarks(truncated)


if __name__ == "__main__":
    unittest.main()
