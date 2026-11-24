from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_state_migration import candidate, POWER_DEFAULTS, PENDING_DEFAULTS


class StateMigrationTest(unittest.TestCase):
    def test_counters_ring_open_book_and_power_are_preserved(self):
        power = {**deepcopy(POWER_DEFAULTS), "activeSeconds": 1200, "samples": [[0, 99], [20, 94]], "lastPercent": 94}
        source = {"openEpubPath": "/book.epub", "powerHistory": power, "recentSleepImages": list(range(16)),
                  "recentSleepPos": 6, "recentSleepFill": 16, "showBootScreen": False,
                  "lastSleepFromReader": False, "lastSleepRenderedQuickResume": True,
                  "favoriteSleepImagePath": "", "preferredSleepFolderPath": "", **PENDING_DEFAULTS}
        original = deepcopy(source)
        result = candidate(source)
        self.assertFalse(result["blocked_keys"])
        for key in ("openEpubPath", "powerHistory", "recentSleepImages", "recentSleepPos", "recentSleepFill", "showBootScreen"):
            self.assertEqual(result["state"][key], source[key])
        self.assertTrue(result["requires_cold_boot"])
        self.assertTrue(result["requires_unchanged_gallery_order"])
        self.assertFalse(result["ready_to_activate"])
        result["state"]["powerHistory"]["samples"].append([21, 93])
        self.assertEqual(source, original)

    def test_active_pending_navigation_and_gallery_choices_block(self):
        for key, value in (("pendingBookmarkSpine", 3), ("pendingBookmarkProgress", 0.5),
                           ("pendingClippingIndex", 0), ("favoriteSleepImagePath", "/sleep/a.bmp"),
                           ("preferredSleepFolderPath", "/gallery"), ("unknown", 1)):
            self.assertIn(key, candidate({key: value})["blocked_keys"])

    def test_no_silent_autonomy_clamping(self):
        for changes in ({"lastPercent": 101}, {"samples": [[20, 90], [1, 89]]},
                        {"samples": [[1, 101]]}, {"samples": [[0, 90]] * 22},
                        {"chargePending": True, "chargePendingPercent": 255}, {"future": 1}):
            self.assertEqual(candidate({"powerHistory": {**POWER_DEFAULTS, **changes}})["blocked_keys"], ["powerHistory"])

    def test_invalid_path_ring_and_counter_rejected(self):
        for source, key in (({"openEpubPath": "/../book"}, "openEpubPath"),
                            ({"readerActivityLoadCount": 256}, "readerActivityLoadCount"),
                            ({"recentSleepImages": [1], "recentSleepFill": 2}, "recentSleepFill")):
            self.assertIn(key, candidate(source)["blocked_keys"])


if __name__ == "__main__":
    unittest.main()
