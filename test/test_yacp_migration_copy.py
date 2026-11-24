"""Safety checks for SD copies. Test artifacts stay in qa-artifacts for review."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("migration", ROOT / "scripts/prepare_yacp_migration.py")
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)


class MigrationCopyTest(unittest.TestCase):
    def setUp(self):
        artifacts = ROOT / "qa-artifacts"
        artifacts.mkdir(exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="migration-copy-", dir=artifacts))
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "state.json").write_text('{"openEpubPath":"/test.epub"}', encoding="utf-8")
        self.destination = self.root / "snapshot"

    def test_repeat_does_not_change_copy_or_original(self):
        result = migration.prepare(self.source, self.destination, "tiny")
        before = migration.enumerate_files(self.destination)
        self.assertTrue(result["copy_verified"])
        self.assertFalse(result["migration_validated"])
        self.assertEqual(result, migration.prepare(self.source, self.destination, "tiny"))
        self.assertEqual(before, migration.enumerate_files(self.destination))
        self.assertEqual(migration.sha256(self.source / "state.json"),
                         migration.sha256(self.destination / "original/state.json"))

    def test_resume_interrupted_copy(self):
        files = migration.enumerate_files(self.source)
        self.destination.mkdir()
        (self.destination / "source-manifest.json").write_text(json.dumps({
            "schema": 1, "source": str(self.source.resolve()), "source_variant": "tiny", "files": files
        }), encoding="utf-8")
        partial = self.destination / "original/state.json"
        partial.parent.mkdir()
        partial.write_bytes((self.source / "state.json").read_bytes()[:7])
        migration.prepare(self.source, self.destination, "tiny")
        self.assertEqual(partial.read_bytes(), (self.source / "state.json").read_bytes())

    def test_changed_source_is_rejected(self):
        migration.prepare(self.source, self.destination, "tiny")
        before = migration.enumerate_files(self.destination)
        (self.source / "state.json").write_text("{}", encoding="utf-8")
        with self.assertRaises(ValueError):
            migration.prepare(self.source, self.destination, "tiny")
        self.assertEqual(before, migration.enumerate_files(self.destination))

    def test_resume_interrupted_manifest(self):
        self.destination.mkdir()
        manifest = {"schema": 1, "source": str(self.source.resolve()), "source_variant": "tiny",
                    "files": migration.enumerate_files(self.source)}
        encoded = json.dumps(manifest, indent=2, ensure_ascii=False).encode("utf-8")
        (self.destination / "source-manifest.pending").write_bytes(encoded[:37])
        self.assertTrue(migration.prepare(self.source, self.destination, "tiny")["copy_verified"])

    def test_resume_interrupted_completion_report(self):
        result = migration.prepare(self.source, self.destination, "tiny")
        report = self.destination / "COPY-VERIFIED.json"
        report.write_bytes(report.read_bytes()[:15])
        self.assertEqual(migration.prepare(self.source, self.destination, "tiny"), result)

    def test_foreign_destination_is_not_overwritten(self):
        self.destination.mkdir()
        precious = self.destination / "precious.txt"
        precious.write_text("preserve", encoding="utf-8")
        with self.assertRaises(ValueError):
            migration.prepare(self.source, self.destination, "tiny")
        self.assertEqual(precious.read_text(), "preserve")

    def test_source_overlap_rejected(self):
        with self.assertRaises(ValueError):
            migration.prepare(self.source, self.source / "copy", "tiny")

    def test_different_variant_rejected(self):
        migration.prepare(self.source, self.destination, "tiny")
        with self.assertRaises(ValueError):
            migration.prepare(self.source, self.destination, "xlarge")


if __name__ == "__main__":
    unittest.main()
