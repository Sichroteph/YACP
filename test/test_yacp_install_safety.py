"""Exercise deployment failures: no upload/install may precede verified backups."""
import base64
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
from urllib.parse import parse_qs, urlsplit
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import deploy_yacp as safety


def image_bytes():
    header = bytearray(24)
    header[0:2] = b"\xe9\1"
    header[12:14] = struct.pack("<H", 5)
    header[23] = 1
    payload = bytearray(256)
    struct.pack_into("<I", payload, 0, 0xABCD5432)
    version = b"1.7.2-yacp.999-tiny"
    payload[16:16 + len(version)] = version
    payload[48:52] = b"YACP"
    payload.extend(b"CROSSPOINT-BOARD-V1:x4;")
    data = header + struct.pack("<II", 0x3c000020, len(payload)) + payload
    checksum = 0xef
    for value in payload:
        checksum ^= value
    data += bytes((len(data) // 16) * 16 + 15 - len(data)) + bytes([checksum])
    return bytes(data) + hashlib.sha256(data).digest()


class Device:
    def __init__(self, backup, fail=None):
        self.backup, self.fail, self.calls = backup, fail, []
        self.status_calls = 0
        self.settings = b'{"language":"EN"}'

    def json(self, path, data=None):
        self.calls.append(path)
        if path == "/api/status":
            self.status_calls += 1
            return {"device": "X4 Pro" if self.fail == "wrong-board" else "X3",
                    "serial": ("Not found" if self.fail == "missing-serial" else
                               "CHANGED" if self.fail == "identity" and self.status_calls > 1 else "QA"),
                    "version": "test"}
        if path == "/api/settings":
            if self.fail == "old-server":
                raise ValueError("404: no existing settings API")
            return [{"key": "showHiddenFiles", "value": int(self.fail != "hidden-files")}]
        if path.startswith("/api/files?"):
            directory = parse_qs(urlsplit(path).query)["path"][0]
            if directory == "/":
                return []
            if directory == "/.crosspoint":
                if self.fail == "missing-settings":
                    return []
                return [{"name": ".." if self.fail == "unsafe-listing" else "settings.json",
                         "size": len(self.settings), "isDirectory": False}]
        raise AssertionError(path)

    def request(self, path, data=None, content_type=None):
        self.calls.append(path)
        if path.startswith("/download?"):
            name = parse_qs(urlsplit(path).query)["path"][0]
            return b"partial" if self.fail == "truncated" else self.settings
        assert (self.backup / "BACKUP-VERIFIED.json").exists(), "No transfer before backup"
        if path.startswith("/upload"):
            return b"Uploaded"
        if path == "/api/firmware/install":
            if self.fail == "install":
                raise ValueError("500: image rejected")
            return b"Firmware installed, restarting"
        raise AssertionError(path)


class InstallSafety(unittest.TestCase):
    def setUp(self):
        # Retain failure evidence, without deleting files or using a real card.
        self.root = ROOT / "qa-artifacts/install-safety-unit" / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        self.image = self.root / "YACP-1.7.2-yacp.999-tiny.bin"
        self.backup = self.root / "backup"
        self.write_image()
        self.authority = patch.object(safety, "assert_canonical")
        self.source = patch.object(safety, "source_manifest", return_value={"sha256": "qa-source"})
        self.acceptance = patch.object(safety, "require_acceptance")
        self.authority.start()
        self.source.start()
        self.acceptance.start()
        self.addCleanup(self.authority.stop)
        self.addCleanup(self.source.stop)
        self.addCleanup(self.acceptance.stop)

    def write_image(self):
        data = image_bytes()
        self.image.write_bytes(data)
        self.image.with_suffix(".json").write_text(json.dumps({"version": "1.7.2-yacp.999", "environment": "tiny",
            "build_succeeded": True, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "source": {"sha256": "qa-source"},
            "build_ticket": {"consumed": True, "lease_active": False, "environment": "tiny"}}))

    def run_device(self, fail=None):
        device = Device(self.backup, fail)
        result = safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        return result, device

    def test_complete_backups_precede_transfer_and_no_poll_after_reboot(self):
        result, device = self.run_device()
        self.assertTrue(result["installed"])
        self.assertEqual(device.status_calls, 2)
        self.assertEqual(device.calls[-1], "/api/firmware/install")
        self.assertEqual((self.backup / "card/.crosspoint/settings.json").read_bytes(), device.settings)

    def test_stale_source_image_rejected_before_contact(self):
        report = json.loads(self.image.with_suffix(".json").read_text())
        report["source"]["sha256"] = "older-source"
        self.image.with_suffix(".json").write_text(json.dumps(report))
        device = Device(self.backup)
        with self.assertRaisesRegex(ValueError, "current canonical"):
            safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        self.assertFalse(device.calls)

    def test_bad_transport_data_or_identity_never_uploads(self):
        for failure in ("wrong-board", "old-server", "unsafe-listing", "missing-settings", "truncated", "identity", "hidden-files", "missing-serial"):
            with self.subTest(failure=failure):
                target = self.root / failure
                device = Device(target, failure)
                with self.assertRaises(ValueError):
                    safety.deploy(self.image, target, device=device, factory_key=bytes(6), apply=True)
                self.assertFalse(any(p.startswith("/upload") or p == "/api/firmware/install" for p in device.calls))

    def test_image_changed_since_compile_never_contacts_device(self):
        data = bytearray(self.image.read_bytes())
        data[100] ^= 1
        self.image.write_bytes(data)
        device = Device(self.backup)
        with self.assertRaises(ValueError):
            safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        self.assertFalse(device.calls)

    def test_source_changed_during_backup_never_uploads(self):
        device = Device(self.backup)
        with patch.object(safety, "source_manifest", side_effect=[{"sha256": "qa-source"}, {"sha256": "changed"}]):
            with self.assertRaisesRegex(ValueError, "current canonical"):
                safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        self.assertFalse(any(p.startswith("/upload") for p in device.calls))

    def test_live_or_unconsumed_build_ticket_refused(self):
        for bad in ({"lease_active": True}, {"consumed": False}):
            self.write_image()
            report = json.loads(self.image.with_suffix(".json").read_text())
            report["build_ticket"].update(bad)
            self.image.with_suffix(".json").write_text(json.dumps(report))
            with self.assertRaises(ValueError):
                safety.image_report(self.image)

    def test_no_shared_backup_destination(self):
        with self.assertRaisesRegex(ValueError, "Nextcloud"):
            safety.private_output(self.root / "Nextcloud" / "backup")

    def test_missing_firmware_regression_acceptance_blocks_transfer(self):
        self.acceptance.stop()
        with patch.object(safety, "ACCEPTANCE_ROOT", self.root / "no-acceptance"):
            device = Device(self.backup)
            with self.assertRaisesRegex(ValueError, "regression"):
                safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
            self.assertFalse(device.calls)

    def test_failed_install_never_claimed_success_or_polled(self):
        device = Device(self.backup, "install")
        with self.assertRaisesRegex(ValueError, "500"):
            safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        self.assertIsNone(json.loads((self.backup / "deployment.json").read_text())["installed"])
        self.assertEqual(device.calls[-1], "/api/firmware/install")

    def test_wrapped_credentials_block_card_copy_after_verified_preservation(self):
        card = self.root / "card"
        cp = card / ".crosspoint"
        cp.mkdir(parents=True)
        (cp / "settings.json").write_text('{"language":"EN"}')
        key = bytes.fromhex("deadbeef0001")
        envelope = b"CPV1" + bytes(4) + b"test-password"
        (cp / "wifi.json").write_text(json.dumps({"credentials": [{"ssid": "QA",
            "password_obf": base64.b64encode(safety.xor_bytes(envelope, key)).decode(),
            "password_len": len(envelope), "password_crc32": zlib.crc32(envelope)}]}))
        with self.assertRaisesRegex(ValueError, "Wi-Fi"):
            safety.deploy(self.image, self.backup, card=card, model="X3", factory_key=key, apply=True)
        self.assertEqual((cp / "wifi.json").read_bytes(), (self.backup / "card/.crosspoint/wifi.json").read_bytes())
        self.assertFalse((card / self.image.name).exists())

    def test_valid_native_card_preserves_data_empty_directories_and_verified_image(self):
        card = self.root / "card"
        cp = card / ".crosspoint"
        cp.mkdir(parents=True)
        (card / "empty-user-directory").mkdir()
        (cp / "settings.json").write_text('{"language":"EN"}')
        key, password = bytes.fromhex("deadbeef0001"), b"native-password"
        document = {"credentials": [{"ssid": "QA", "password_obf": base64.b64encode(safety.xor_bytes(password, key)).decode(),
                                   "password_len": len(password), "password_crc32": zlib.crc32(password)}]}
        (cp / "wifi.json").write_text(json.dumps(document))
        before = (cp / "wifi.json").read_bytes()
        result = safety.deploy(self.image, self.backup, card=card, model="X3", factory_key=key, apply=True)
        self.assertTrue(result["copied_to_card"])
        self.assertFalse(result["installed"])
        self.assertEqual((cp / "wifi.json").read_bytes(), before)
        self.assertTrue((self.backup / "card/empty-user-directory").is_dir())
        self.assertEqual(safety.digest(card / self.image.name), safety.digest(self.image))

    def test_unacknowledged_legacy_position_blocks_card_transfer(self):
        card = self.root / "card"
        cp = card / ".crosspoint"
        cp.mkdir(parents=True)
        (cp / "settings.json").write_text('{"language":"EN"}')
        (card / "Book.epub").write_bytes(b"unchanged book sentinel")
        legacy = cp / safety.fnv64_book_path("/Book.epub")
        legacy.mkdir()
        (legacy / "progress.bin").write_bytes(struct.pack("<HHH", 38, 2, 33))
        with self.assertRaisesRegex(ValueError, "Unreconciled"):
            safety.deploy(self.image, self.backup, card=card, model="X3", factory_key=bytes(6), apply=True)
        self.assertFalse((card / self.image.name).exists())
        self.assertEqual((self.backup / "card/Book.epub").read_bytes(), (card / "Book.epub").read_bytes())

    def test_interrupted_repair_blocks_card_transfer(self):
        card = self.root / "card"
        (card / ".crosspoint").mkdir(parents=True)
        (card / ".crosspoint/settings.json").write_text('{"language":"EN"}')
        (card / ".yacp-reentry-v2/pending").mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, "interrupted"):
            safety.deploy(self.image, self.backup, card=card, model="X3", factory_key=bytes(6), apply=True)
        self.assertFalse((card / self.image.name).exists())

    def test_mixed_fixed_page_formats_block_card_transfer(self):
        for extension, kind in (("txt", "txt"), ("md", "txt"), ("xtc", "xtc"), ("xtch", "xtc")):
            with self.subTest(extension=extension):
                card = self.root / extension
                cp = card / ".crosspoint"
                cp.mkdir(parents=True)
                (cp / "settings.json").write_text('{"language":"EN"}')
                book = "/Book." + extension
                (card / book[1:]).write_bytes(b"unchanged book sentinel")
                legacy = cp / safety.fnv64_book_path(book).replace("epub_", kind + "_", 1)
                legacy.mkdir()
                (legacy / "progress.bin").write_bytes(struct.pack("<HHH", 1, 2, 33))
                backup = self.root / (extension + "-backup")
                with self.assertRaisesRegex(ValueError, "review"):
                    safety.deploy(self.image, backup, card=card, model="X3", factory_key=bytes(6), apply=True)
                self.assertFalse((card / self.image.name).exists())
                self.assertEqual((backup / "card" / book[1:]).read_bytes(), (card / book[1:]).read_bytes())

    def test_unacknowledged_txt_upgrade_position_blocks_mixed_store(self):
        card = self.root / "text-upgrade"
        cp = card / ".crosspoint"
        cp.mkdir(parents=True)
        (cp / "settings.json").write_text('{"language":"EN"}')
        book = "/Book.txt"
        txt = card / book[1:]
        txt.write_bytes(b"source passage")
        current = cp / safety.esp32_book_path(book)
        old = cp / current.name.replace("epub_", "txt_", 1)
        old.mkdir()
        progress = old / "progress.bin"
        progress.write_bytes(struct.pack("<HI", 1, 7))
        self.assertTrue(safety.audit_card(card, bytes(6)))
        current.mkdir()
        (current / "progress.bin").write_bytes(struct.pack("<HHHI", 0, 5, 10, 12))
        with self.assertRaisesRegex(ValueError, "Mixed TXT/Markdown"):
            safety.audit_card(card, bytes(6))
        receipt = cp / "txt-receipts" / old.name
        receipt.parent.mkdir()
        receipt.write_bytes(safety.txt_receipt(old, txt.stat().st_size))
        self.assertTrue(safety.audit_card(card, bytes(6)))
        progress.write_bytes(struct.pack("<HI", 1, 14))
        with self.assertRaisesRegex(ValueError, "Mixed TXT/Markdown"):
            safety.audit_card(card, bytes(6))

    def test_invalid_factory_identity_blocks_card_transfer(self):
        for length in (0, 5, 7):
            card = self.root / ("identity-" + str(length))
            (card / ".crosspoint").mkdir(parents=True)
            (card / ".crosspoint/settings.json").write_text('{"language":"EN"}')
            with self.assertRaisesRegex(ValueError, "factory"):
                safety.deploy(self.image, self.root / ("backup-identity-" + str(length)),
                              card=card, model="X3", factory_key=bytes(length), apply=True)
            self.assertFalse((card / self.image.name).exists())

    def test_invalid_native_network_names_block_card_transfer(self):
        for index, ssid in enumerate(("", "a" * 33, "a\0b", 42, "é" * 17)):
            card = self.root / ("network-" + str(index))
            cp = card / ".crosspoint"
            cp.mkdir(parents=True)
            (cp / "settings.json").write_text('{"language":"EN"}')
            row = {"ssid": ssid, "password_obf": "", "password_len": 0, "password_crc32": 0}
            (cp / "wifi.json").write_text(json.dumps({"credentials": [row]}))
            before = (cp / "wifi.json").read_bytes()
            with self.assertRaisesRegex(ValueError, "Wi-Fi"):
                safety.deploy(self.image, self.root / ("backup-network-" + str(index)),
                              card=card, model="X3", factory_key=bytes(6), apply=True)
            self.assertEqual((cp / "wifi.json").read_bytes(), before)
            self.assertFalse((card / self.image.name).exists())

    def test_live_data_changes_during_backup_prevent_upload(self):
        class ChangingDevice(Device):
            downloads = 0
            def request(self, path, data=None, content_type=None):
                result = super().request(path, data, content_type)
                if path.startswith("/download?"):
                    self.downloads += 1
                    if self.downloads > 1:
                        return b"x" * len(result)
                return result
        device = ChangingDevice(self.backup)
        with self.assertRaisesRegex(ValueError, "changed"):
            safety.deploy(self.image, self.backup, device=device, factory_key=bytes(6), apply=True)
        self.assertFalse(any(p.startswith("/upload") for p in device.calls))

    def test_no_added_firmware_installation_checks_or_size_tuning(self):
        for name in ("src/network/FirmwareFlasher.cpp", "src/network/FirmwareFlasher.h",
                     "src/network/FirmwareBoardTag.cpp", "src/network/FirmwareBoardTag.h",
                     "src/network/CrossPointWebServer.cpp", "src/network/CrossPointWebServer.h"):
            text = (ROOT / name).read_text(encoding="utf-8")
            for forbidden in ("prepareFirmwareInstall", "UNSAFE_DATA", "YACP-DATA-SCHEMA", "firmware/preflight"):
                self.assertNotIn(forbidden, text, name)
        for name in ("LegacyMigration.cpp", "LegacyMigration.h", "LegacyMigrationIo.h", "LegacySettings.h"):
            text = (ROOT / "src/util" / name).read_text(encoding="utf-8")
            for forbidden in ("noipa", "#pragma GCC optimize", "preinstall", "withSuffix", "jsonMember", "pathAt("):
                self.assertNotIn(forbidden, text, name)


if __name__ == "__main__":
    unittest.main()
