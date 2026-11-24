import base64
from copy import deepcopy
from pathlib import Path
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_credential_migration import decode_yacp_password, fnv1a, wifi_candidate, xor_bytes


SOURCE = bytes.fromhex("102030405060")
TARGET = bytes.fromhex("708090a0b0c0")


def envelope(password):
    if not password:
        return ""
    data = b"CPV1" + struct.pack("<I", fnv1a(SOURCE + password)) + password
    return base64.b64encode(xor_bytes(data, SOURCE)).decode("ascii")


def document(password=b"synthetic-password"):
    return {"lastConnectedSsid": "QA network", "credentials": [
        {"ssid": "QA network", "password_obf": envelope(password)}]}


class CredentialMigrationTest(unittest.TestCase):
    def test_known_fnv_vector(self):
        self.assertEqual(fnv1a(b"hello"), 0x4f9f2cab)

    def test_rekey_and_target_integrity_without_mutating_original(self):
        source = document("synthetic é password".encode())
        before = deepcopy(source)
        result = wifi_candidate(source, SOURCE, TARGET)
        item = result["candidate"]["credentials"][0]
        decoded = xor_bytes(base64.b64decode(item["password_obf"]), TARGET)
        self.assertEqual(decoded, "synthetic é password".encode())
        self.assertEqual(item["password_len"], len(decoded))
        self.assertEqual(item["password_crc32"], zlib.crc32(decoded))
        self.assertEqual(source, before)
        self.assertFalse(result["ready_to_activate"])
        self.assertTrue(result["source_integrity_verified"])
        self.assertEqual(result, wifi_candidate(source, SOURCE, TARGET))

    def test_wrong_key_and_corruption_are_not_legacy_fallbacks(self):
        with self.assertRaises(ValueError):
            wifi_candidate(document(), TARGET, TARGET)
        encoded = envelope(b"synthetic-password")
        raw = bytearray(base64.b64decode(encoded))
        raw[-1] ^= 1
        with self.assertRaises(ValueError):
            decode_yacp_password(base64.b64encode(raw).decode(), SOURCE)

    def test_empty_and_max_length(self):
        for password in (b"", b"a" * 64):
            item = wifi_candidate(document(password), SOURCE, SOURCE)["candidate"]["credentials"][0]
            self.assertEqual(item["password_len"], len(password))
        with self.assertRaises(ValueError):
            wifi_candidate(document(b"a" * 65), SOURCE, SOURCE)

    def test_legacy_requires_explicit_review_and_stays_unverified(self):
        source = document()
        source["credentials"][0]["password_obf"] = base64.b64encode(xor_bytes(b"legacy-test", SOURCE)).decode()
        with self.assertRaises(ValueError):
            wifi_candidate(source, SOURCE, TARGET)
        result = wifi_candidate(source, SOURCE, TARGET, "reviewed_legacy")
        self.assertFalse(result["source_integrity_verified"])
        with self.assertRaises(ValueError):
            wifi_candidate(document(), SOURCE, TARGET, "reviewed_legacy")

    def test_capacity_schema_and_duplicate_rows_are_not_silently_dropped(self):
        for mutation in ("extra", "duplicate", "capacity"):
            source = document()
            if mutation == "extra": source["future"] = 1
            if mutation == "duplicate": source["credentials"] *= 2
            if mutation == "capacity": source["credentials"] *= 9
            with self.assertRaises(ValueError):
                wifi_candidate(source, SOURCE, TARGET)

    def test_invalid_encoding_key_and_nul(self):
        for value in ("!invalid!", "a" * 97, 23):
            with self.assertRaises(ValueError): decode_yacp_password(value, SOURCE)
        with self.assertRaises(ValueError): wifi_candidate(document(), b"", TARGET)
        with self.assertRaises(ValueError): wifi_candidate(document(b"has\0nul"), SOURCE, TARGET)


if __name__ == "__main__":
    unittest.main()
