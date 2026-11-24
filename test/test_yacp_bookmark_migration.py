import io
from pathlib import Path
import struct
import sys
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from yacp_bookmark_migration import candidate, cached_bookmark_page, check_target_collisions, target_path


def legacy(version, progress=.5, snippet='Target sentence', paragraph=12, path='/Book.epub', count=1):
    def string(text):
        raw = text.encode()
        return struct.pack('<I', len(raw)) + raw
    data = bytes([version]) + struct.pack('<B' if version == 2 else '<H', count)
    data += string('Title') + string('Author') + string(path)
    for _ in range(count):
        data += struct.pack('<HfI', 0, progress, 12345) + b'Chapter'.ljust(48, b'\0')
        if version >= 4: data += struct.pack('<H', paragraph)
        if version >= 5: data += snippet.encode().ljust(64, b'\0')
    return data


def section_v44():
    header = bytearray(b'\xffCXS\x2c' + bytes(39))
    pages, positions = bytearray(), []
    for phrase in (('Earlier', 'sentence'), ('Target', 'sentence')):
        positions.append(len(header) + len(pages))
        text = b'\0'.join(w.encode() for w in phrase) + b'\0'
        pages += struct.pack('<HBhhHBBBH', 1, 1, 12, 34, 2, 0, 0, 0, len(text))
        pages += struct.pack('<HHhhBB', 0, len(phrase[0]) + 1, 0, 40, 0, 0) + text
    lut = len(header) + len(pages)
    struct.pack_into('<HI', header, 26, 2, lut)
    struct.pack_into('<I', header, 36, lut + 8)
    return header + pages + struct.pack('<2I3H', *positions, 2, 1, 12)


def epub(text='<p>Earlier sentence</p><p>Target sentence</p>'):
    stream = io.BytesIO()
    with ZipFile(stream, 'w') as archive:
        archive.writestr('META-INF/container.xml', '<container><rootfile full-path="book.opf"/></container>')
        archive.writestr('book.opf', '<package><manifest><item id="c" href="c.xhtml"/></manifest><spine><itemref idref="c"/></spine></package>')
        archive.writestr('c.xhtml', '<html><body>' + text + '</body></html>')
    return stream


class BookmarkMigrationTest(unittest.TestCase):
    def test_all_versions_preserve_original_metadata_and_resolve_content(self):
        for version in (2, 3, 4, 5):
            result = candidate(legacy(version), '/Book.epub', epub(), {0: section_v44()})
            self.assertEqual(result['blocked_entries'], 0)
            self.assertEqual(result['candidate']['bookmarks'][0]['vo'], len('Earlier sentence'))
            self.assertEqual(result['source']['bookmarks'][0]['timestamp'], 12345)
            self.assertFalse(result['ready_to_activate'])
            self.assertFalse(result['firmware_navigation_validated'])

    def test_unique_snippet_needs_no_old_cache_and_retains_unicode_offsets(self):
        result = candidate(legacy(5), '/Book.epub', epub('<p>e\u0301 prefix</p><p>Target sentence</p>'))
        self.assertEqual(result['candidate']['bookmarks'][0]['vo'], len('e\u0301 prefix'))
        self.assertEqual(result['decisions'][0]['method'], 'unique_saved_snippet')

    def test_missing_and_duplicate_text_block_the_whole_document(self):
        for book in (epub('Unrelated contents'), epub('Target sentence. Target sentence.')):
            result = candidate(legacy(5, count=2), '/Book.epub', book)
            self.assertEqual(result['blocked_entries'], 2)
            self.assertIsNone(result['candidate'])
            self.assertEqual(len(result['source']['bookmarks']), 2)
        self.assertIsNone(candidate(legacy(2), '/Book.epub', epub())['candidate'])

    def test_paragraph_precedes_fraction_and_bounds_are_checked(self):
        section = section_v44()
        self.assertEqual(cached_bookmark_page(section, dict(progress=0, paragraph_index=12)), 1)
        self.assertEqual(cached_bookmark_page(section, dict(progress=1, paragraph_index=1)), 0)
        self.assertEqual(cached_bookmark_page(section, dict(progress=1)), 1)
        for bad in (section[:-1], section[:20]):
            with self.assertRaises(ValueError):
                cached_bookmark_page(bad, dict(progress=0, paragraph_index=12))

    def test_legacy_v27_page_table_is_read_with_its_own_header(self):
        header = bytearray(bytes([27]) + bytes(36))
        pages, positions = bytearray(), []
        for words in ((b'Earlier', b'sentence'), (b'Target', b'sentence')):
            positions.append(len(header) + len(pages))
            pages += struct.pack('<HBhhH', 1, 1, 12, 34, 2)
            for word in words: pages += struct.pack('<I', len(word)) + word
        lut = len(header) + len(pages)
        struct.pack_into('<HI', header, 19, 2, lut)
        struct.pack_into('<I', header, 29, lut + 8)
        section = header + pages + struct.pack('<2I3H', *positions, 2, 1, 12)
        result = candidate(legacy(4), '/Book.epub', epub(), {0: section})
        self.assertEqual(result['candidate']['bookmarks'][0]['vo'], len('Earlier sentence'))

    def test_progress_uses_inflated_spine_bytes(self):
        book = epub()
        with ZipFile(book) as old:
            files = {name: old.read(name) for name in old.namelist()}
        files['book.opf'] = b'<package><manifest><item id="c" href="c.xhtml"/><item id="d" href="d.xhtml"/></manifest><spine><itemref idref="c"/><itemref idref="d"/></spine></package>'
        files['d.xhtml'] = b'<body>' + b'Extra words ' * 100 + b'</body>'
        stream = io.BytesIO()
        with ZipFile(stream, 'w') as archive:
            for name, data in files.items(): archive.writestr(name, data)
        result = candidate(legacy(5), '/Book.epub', stream)
        expected = .5 * len(files['c.xhtml']) / (len(files['c.xhtml']) + len(files['d.xhtml']))
        self.assertAlmostEqual(result['candidate']['bookmarks'][0]['percentage'], expected)

    def test_paths_cannot_alias_other_books_silently(self):
        self.assertEqual(target_path('/folder/Book.epub'), '/.crosspoint/bookmarks/folder_Book.json')
        for paths in (['/folder/Book.epub', '/folder_Book.epub'], ['/Book.epub', '/book.epub']):
            with self.assertRaises(ValueError): check_target_collisions(paths)
        for path in ('Book.epub', '/../Book.epub', '//Book.epub', '/a/./Book.epub'):
            with self.assertRaises(ValueError): target_path(path)
        with self.assertRaises(ValueError): candidate(legacy(5), '/Other.epub', epub())


if __name__ == '__main__':
    unittest.main()
