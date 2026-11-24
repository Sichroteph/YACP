import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from prepare_yacp_migration import prepare as snapshot_copy
from prepare_yacp_settings_candidate import prepare


class SettingsCandidateTest(unittest.TestCase):
    def setUp(self):
        # Retained local fixtures: no automatic host-file deletion.
        parent = Path(__file__).resolve().parents[1] / "qa-artifacts/settings-candidate-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=parent))
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "crossink-settings.json").write_text(json.dumps({"fontSize": 0, "sleepScreen": 9,
                                                                       "uiTheme": 6}), encoding="utf-8")
        (self.source / "settings.json").write_text('{"fontSize":20,"sleepScreen":0}', encoding="utf-8")
        self.snapshot = self.root / "snapshot"
        snapshot_copy(self.source, self.snapshot, "tiny")

    def test_chooses_yacp_document_and_repeat_is_identical(self):
        output = self.root / "output"
        prepare(self.snapshot, output)
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        values = json.loads(before["candidate-settings.json"])
        self.assertEqual(values["fontSize"], 10)
        self.assertEqual(values["sleepScreen"], 6)
        prepare(self.snapshot, output)
        self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})
        self.assertFalse((output / "settings.json").exists())

    def test_interrupted_output_resumes_without_replacing_prefix(self):
        complete = self.root / "complete"
        prepare(self.snapshot, complete)
        resumed = self.root / "resumed"
        resumed.mkdir()
        (resumed / "SOURCE.json").write_bytes((complete / "SOURCE.json").read_bytes()[:40])
        prepare(self.snapshot, resumed)
        self.assertEqual({p.name: p.read_bytes() for p in complete.iterdir()},
                         {p.name: p.read_bytes() for p in resumed.iterdir()})

    def test_conflicting_output_is_not_overwritten(self):
        output = self.root / "output"
        prepare(self.snapshot, output)
        corrupt = output / "candidate-settings.json"
        corrupt.write_bytes(b"unrelated data")
        with self.assertRaises(ValueError):
            prepare(self.snapshot, output)
        self.assertEqual(corrupt.read_bytes(), b"unrelated data")

    def test_recent_capacity_is_read_from_the_actual_target_loader(self):
        # Build another complete snapshot; do not change the verified fixture.
        source = self.root / "with-recents-source"
        source.mkdir()
        (source / "crossink-settings.json").write_bytes((self.source / "crossink-settings.json").read_bytes())
        recent = {"books": [{"path": f"/{i}.epub", "title": str(i), "author": "", "coverBmpPath": ""}
                            for i in range(12)]}
        (source / "recent.json").write_text(json.dumps(recent))
        snapshot = self.root / "with-recents-snapshot"
        snapshot_copy(source, snapshot, "tiny")
        output = self.root / "with-recents-output"
        result = prepare(snapshot, output, include_recents=True)
        self.assertEqual(result["recent_books"], 12)
        report = json.loads((output / "recent-books-candidate.json").read_text())
        self.assertEqual(report["recent"], recent)
        self.assertEqual(bool(report["blocked"]), report["target_capacity"] < 12)
        before = {p.name: p.read_bytes() for p in output.iterdir()}
        prepare(snapshot, output, include_recents=True)
        self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})

    def test_interrupted_shared_state_proposal_is_repeatable(self):
        source = self.root / "with-state-source"
        source.mkdir()
        (source / "crossink-settings.json").write_bytes((self.source / "crossink-settings.json").read_bytes())
        (source / "state.json").write_text(json.dumps({"openEpubPath": "/book.epub", "powerHistory": {}}))
        snapshot = self.root / "with-state-snapshot"
        snapshot_copy(source, snapshot, "tiny")
        output = self.root / "with-state-output"
        prepare(snapshot, output, include_state=True)
        expected = {p.name: p.read_bytes() for p in output.iterdir()}
        resumed = self.root / "with-state-resumed"
        resumed.mkdir()
        for name, content in expected.items():
            (resumed / name).write_bytes(content[:90] if name == "shared-state-candidate.json" else content)
        prepare(snapshot, resumed, include_state=True)
        self.assertEqual(expected, {p.name: p.read_bytes() for p in resumed.iterdir()})
        report = json.loads(expected["shared-state-candidate.json"])
        self.assertEqual(report["state"]["openEpubPath"], "/book.epub")
        self.assertFalse(report["ready_to_activate"])

    def test_changed_snapshot_and_overlap_are_rejected(self):
        with self.assertRaises(ValueError):
            prepare(self.snapshot, self.snapshot / "candidate")
        file = self.snapshot / "original/settings.json"
        file.write_text("{}", encoding="utf-8")
        with self.assertRaises(ValueError):
            prepare(self.snapshot, self.root / "output")
        self.assertFalse((self.root / "output").exists())


if __name__ == "__main__":
    unittest.main()
