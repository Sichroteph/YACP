import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_settings_defaults import SOURCE_FIELDS, global_candidate
from yacp_settings_migration import candidate


class SettingsDefaultsTest(unittest.TestCase):
    def test_inventory_matches_frozen_source_audit(self):
        audit = json.loads((Path(__file__).resolve().parents[1] / "docs/yacp-settings-audit.json").read_text())
        self.assertEqual(SOURCE_FIELDS, {row["field"] for row in audit["fields"]})

    def test_global_defaults_do_not_leak_into_book_conversion(self):
        self.assertNotIn("trackReadingStats", candidate({}, "tiny")["settings"])
        result = global_candidate({}, "tiny")
        self.assertEqual(result["settings"]["trackReadingStats"], 1)
        self.assertEqual(result["settings"]["statusBarProgressBarPosition"], 0)
        self.assertIn("fontSize", result["blocked_default_fields"])
        self.assertIn("sleepScreen", result["blocked_default_fields"])
        self.assertFalse(result["omitted_fields_reviewed"])
        self.assertFalse(result["missing_keys_use_crosspoint_defaults"])

    def test_explicit_values_keep_priority(self):
        result = global_candidate({"trackReadingStats": 0, "statusBarProgressBarPosition": 1}, "tiny")
        self.assertEqual(result["settings"]["trackReadingStats"], 0)
        self.assertEqual(result["settings"]["statusBarProgressBarPosition"], 1)
        self.assertNotIn("trackReadingStats", {row["source_field"] for row in result["default_decisions"]})

    def test_alias_or_override_does_not_hide_invalid_data(self):
        source = {"fadingFix": 9, "lineHeightPercent": 1, "opdsPassword_obf": "invalid"}
        result = global_candidate(source, "tiny")
        self.assertEqual(set(result["blocked_keys"]), set(source))
        rows = {row["source_field"]: row for row in result["default_decisions"]}
        self.assertEqual(rows["displayPowerSaving"]["status"], "stored_alias")
        self.assertEqual(rows["opdsPassword"]["status"], "stored_alias")
        self.assertEqual(rows["statusBar"]["status"], "blocked")


if __name__ == "__main__":
    unittest.main()
