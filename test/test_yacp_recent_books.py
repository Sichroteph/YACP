from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_recent_books_migration import candidate


def document(count):
    return {"books": [{"path": f"/book-{i}.epub", "title": f"Title {i}", "author": "Author",
                       "coverBmpPath": f"/.crosspoint/old-{i}/cover.bmp"} for i in range(count)]}


class RecentBooksMigrationTest(unittest.TestCase):
    def test_upstream_capacity_would_lose_two_realistic_entries(self):
        source = document(12)
        original = deepcopy(source)
        blocked = candidate(source, 10)
        self.assertEqual(blocked["recent"], original)
        self.assertTrue(blocked["blocked"])
        converted = candidate(source, 18)
        self.assertEqual(converted["recent"], original)
        self.assertFalse(converted["blocked"])
        self.assertFalse(converted["book_existence_checked"])
        converted["recent"]["books"][0]["title"] = "Edited candidate"
        self.assertEqual(source, original)

    def test_overflow_and_unknown_fields_are_preserved_but_blocked(self):
        for source in (document(19), {**document(1), "future": 1}):
            result = candidate(source, 18)
            self.assertEqual(result["recent"], source)
            self.assertTrue(result["blocked"])
        self.assertFalse(candidate(document(18), 18)["blocked"])

    def test_no_guess_for_ambiguous_or_unsafe_paths(self):
        for path in ("../book.epub", "/a/../book.epub", "/book\0.epub", "//book.epub"):
            source = document(1)
            source["books"][0]["path"] = path
            self.assertTrue(candidate(source, 18)["blocked"])
        source = document(2)
        source["books"][1]["path"] = source["books"][0]["path"]
        self.assertTrue(candidate(source, 18)["blocked"])


if __name__ == "__main__":
    unittest.main()
