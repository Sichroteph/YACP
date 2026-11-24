import struct
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_data_formats import clippings


def string(raw):
    return struct.pack("<I", len(raw)) + raw


def fixture(count=1, text=b"A preserved passage", title=b"Chapter"):
    header = struct.pack("<BH", 1, count) + string(b"Book") + string(b"Author") + string(b"/book.epub")
    record = struct.pack("<8HI", 3, 12, 13, 20, 7, 2, 17, 42, 1234567890)
    record += title.ljust(48, b"\0") + string(text)
    return header + record * count


class ClippingsTest(unittest.TestCase):
    def test_all_fields_and_unresolved_navigation_survive(self):
        result = clippings(fixture())
        entry = result["clippings"][0]
        self.assertEqual([entry[k] for k in ("spine", "start_page", "end_page", "page_count", "start_word",
                                            "end_word", "word_count", "paragraph_index", "timestamp")],
                         [3, 12, 13, 20, 7, 2, 17, 42, 1234567890])
        self.assertEqual(entry["text"], "A preserved passage")
        self.assertEqual(bytes.fromhex(result["path_utf8_hex"]), b"/book.epub")
        self.assertFalse(result["navigation_validated"])

    def test_every_truncation_and_unknown_trailer_is_rejected(self):
        data = fixture()
        for size in range(len(data)):
            with self.assertRaises(ValueError):
                clippings(data[:size])
        for data in (b"\x02" + data[1:], data + b"\0", fixture(count=65), fixture(text=b"a" * 513)):
            with self.assertRaises(ValueError):
                clippings(data)

    def test_fixed_boundaries_and_split_unicode_keep_original_bytes(self):
        text = b"a" * 511 + b"\xc3"
        title = b"b" * 47 + b"\xe2"
        entries = clippings(fixture(count=64, text=text, title=title))["clippings"]
        self.assertEqual(len(entries), 64)
        self.assertEqual(bytes.fromhex(entries[-1]["text_utf8_hex"]), text)
        self.assertEqual(bytes.fromhex(entries[-1]["chapter_title_field_hex"]), title)
        self.assertEqual(clippings(fixture(count=0))["clippings"], [])


if __name__ == "__main__":
    unittest.main()
