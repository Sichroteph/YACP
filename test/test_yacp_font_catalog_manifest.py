import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from prepare_yacp_migration import enumerate_files
from yacp_font_catalog import verified_catalog


class FontManifestTest(unittest.TestCase):
    def setUp(self):
        parent = Path(__file__).resolve().parents[1] / 'qa-artifacts/font-manifest-tests'
        parent.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(dir=parent))
        for name in ('alpha/a_12.cpfont', 'Beta/b_16.cpfont'):
            file = self.root / 'original' / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(name.encode())
        self.entries = enumerate_files(self.root / 'original')
        self.manifest = self.root / 'source-manifest.json'
        self.manifest.write_text(json.dumps({'files': list(reversed(self.entries))}))
        (self.root / 'COPY-VERIFIED.json').write_text('{"copy_verified":true}')

    def test_file_order_is_not_font_size_order(self):
        families, _ = verified_catalog(self.root)
        self.assertEqual(families, {'alpha': [12], 'Beta': [16]})

    def test_changed_content_is_rejected(self):
        (self.root / 'original/alpha/a_12.cpfont').write_bytes(b'changed')
        with self.assertRaises(ValueError): verified_catalog(self.root)

    def test_duplicate_manifest_entry_is_rejected(self):
        self.manifest.write_text(json.dumps({'files': self.entries + self.entries[:1]}))
        with self.assertRaises(ValueError): verified_catalog(self.root)


if __name__ == '__main__':
    unittest.main()
