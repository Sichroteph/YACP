"""Failure-path checks for source identity and single-use build tickets.

Fixtures are retained under qa-artifacts; no cleanup deletes user files.
"""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import check_yacp_source as source


class AuthorityTests(unittest.TestCase):
    def setUp(self):
        retained = source.ROOT / "qa-artifacts/source-authority-tests"
        retained.mkdir(parents=True, exist_ok=True)
        self.root = Path(tempfile.mkdtemp(prefix="case-", dir=retained)).resolve()
        (self.root / "src").mkdir()
        (self.root / "src/reader.cpp").write_text("int page = 1;\n")
        (self.root / "platformio.ini").write_text("crossink_version = 1.7.2-yacp.508\n")
        self.policy = {"schema": 1, "product": "YACP-CrossPoint", "hardware": {"tiny": ["X3", "X4"]}}
        (self.root / ".yacp-source.json").write_text(json.dumps(self.policy))
        self.ticket = self.root / "ticket.json"

    def reserve(self, **overrides):
        ticket = {"product": "YACP-CrossPoint", "environment": "tiny", "source_root": str(self.root),
                  "version": "1.7.2-yacp.508", "source_sha256": "reviewed", "consumed": False,
                  "lease_active": True, "session_id": "test-session", "owner_pid": os.getpid(), "owner_os": os.name}
        ticket.update(overrides)
        self.ticket.write_text(json.dumps(ticket))

    def consume(self, environment="tiny"):
        with patch.dict(os.environ, YACP_BUILD_TICKET=str(self.ticket), YACP_BUILD_SESSION="test-session"), \
             patch.object(source, "assert_canonical", return_value=(self.policy, self.root)), \
             patch.object(source, "source_manifest", return_value={"sha256": "reviewed"}):
            return source.consume_ticket(self.root, environment)

    def test_ticket_cannot_compile_twice(self):
        self.reserve()
        self.consume()
        data = json.loads(self.ticket.read_text())
        data['lease_active'] = False
        self.ticket.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, "already compiled"):
            self.consume()

    def test_active_toolchain_session_may_reenter(self):
        self.reserve()
        self.consume()
        _, ticket = self.consume()
        self.assertEqual(ticket["guard_visits"], 2)

    def test_dead_wrapper_cannot_reenter(self):
        self.reserve(owner_pid=0)
        with self.assertRaisesRegex(ValueError, "no longer running"):
            self.consume()

    def test_windows_owner_is_verified_from_the_expected_wsl_distribution(self):
        from types import SimpleNamespace

        ticket = {"owner_os": "nt", "owner_pid": 47, "wsl_distro": "YACP-Simulator"}
        matching = SimpleNamespace(returncode=0, stdout='"python.exe","47","Console","1","35 Ko"')
        missing = SimpleNamespace(returncode=0, stdout='INFO: No tasks are running')
        with patch.object(source.os, "name", "posix"), \
             patch.dict(os.environ, WSL_DISTRO_NAME="YACP-Simulator"), \
             patch.object(source.shutil, "which", return_value="/mnt/c/Windows/System32/tasklist.exe"), \
             patch.object(source.subprocess, "run", side_effect=[matching, missing]):
            self.assertTrue(source.ticket_owner_alive(ticket))
            self.assertFalse(source.ticket_owner_alive(ticket))
            self.assertFalse(source.ticket_owner_alive(dict(ticket, wsl_distro="Ubuntu")))

    def test_wrong_environment_is_rejected(self):
        self.reserve()
        with self.assertRaisesRegex(ValueError, "profile"):
            self.consume("x4pro")

    def test_wrong_source_is_rejected(self):
        self.reserve(source_root=str(self.root / "other"))
        with self.assertRaisesRegex(ValueError, "another source"):
            self.consume()

    def test_changed_input_is_rejected(self):
        self.reserve(source_sha256="old-source")
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            self.consume()

    def test_version_mismatch_is_rejected(self):
        self.reserve(version="1.7.2-yacp.507")
        with self.assertRaisesRegex(ValueError, "old/already"):
            self.consume()

    def test_sibling_worktree_cannot_replace_authority(self):
        (self.root / source.AUTHORITY).write_text(json.dumps({"product": self.policy["product"],
                                                            "root": str(self.root / "canonical")}))
        with patch.object(source, "identity", return_value=(self.policy, self.root)):
            with self.assertRaisesRegex(ValueError, "not canonical"):
                source.register(self.root)

    def test_missing_project_marker_is_rejected(self):
        retired = self.root / "retired"
        retired.mkdir()
        with self.assertRaisesRegex(ValueError, "retired/reference"):
            source.identity(retired)

    def test_snapshot_tracks_code_and_local_config(self):
        with patch.object(source, "git", return_value="fixture"):
            before = source.source_manifest(self.root)["sha256"]
            (self.root / "src/reader.cpp").write_text("int page = 2;\n")
            changed = source.source_manifest(self.root)["sha256"]
            self.assertNotEqual(before, changed)
            (self.root / "platformio.local.ini").write_text("build_flags = -DNEW_BEHAVIOR\n")
            self.assertNotEqual(changed, source.source_manifest(self.root)["sha256"])

    def test_static_font_catalogue_and_headers_are_attested(self):
        fonts = self.root / "lib/EpdFont/builtinFonts"
        fonts.mkdir(parents=True)
        catalogue = fonts / "all.h"
        native = fonts / "ubuntu_10_regular.h"
        catalogue.write_text("#include <ubuntu_10_regular.h>\n")
        native.write_text("static const int glyph = 1;\n")
        with patch.object(source, "git", return_value="fixture"):
            before = source.source_manifest(self.root)["sha256"]
            catalogue.write_text("#include <ubuntu_12_regular.h>\n")
            changed = source.source_manifest(self.root)["sha256"]
            self.assertNotEqual(before, changed)
            native.write_text("static const int glyph = 2;\n")
            changed_again = source.source_manifest(self.root)["sha256"]
            self.assertNotEqual(changed, changed_again)
            (fonts / "yacp").mkdir()
            (fonts / "yacp/generated_font.h").write_text("regenerated font output\n")
            self.assertEqual(changed_again, source.source_manifest(self.root)["sha256"])

    def test_unknown_generated_suffix_and_binary_asset_are_build_inputs(self):
        header = self.root / "src/vendor.generated.h"
        with patch.object(source, "git", return_value="fixture"):
            before = source.source_manifest(self.root)["sha256"]
            header.write_text("static const int lookup = 1;\n")
            changed = source.source_manifest(self.root)["sha256"]
            self.assertNotEqual(before, changed)
            (self.root / "assets").mkdir()
            (self.root / "assets/waveform.bin").write_bytes(bytes([1, 2, 3, 4]))
            self.assertNotEqual(changed, source.source_manifest(self.root)["sha256"])

    def test_version_and_generated_outputs_do_not_change_source_identity(self):
        with patch.object(source, "git", return_value="fixture"):
            (self.root / "src/images").mkdir()
            (self.root / "src/components/icons").mkdir(parents=True)
            checkbox = self.root / "src/components/icons/checkbox_on.svg"
            checkbox.write_text("<svg/>\n")
            (self.root / "src/images/logo.svg").write_text("<svg/>\n")
            before = source.source_manifest(self.root)["sha256"]
            (self.root / "platformio.ini").write_text("crossink_version = 1.7.2-yacp.999\n")
            for name in ("src/images/YacpLogo120.generated.h", "src/images/logoSvg.generated.h",
                         "src/components/icons/checkbox_onSvg.generated.h"):
                (self.root / name).write_text("generated data\n")
            self.assertEqual(before, source.source_manifest(self.root)["sha256"])
            checkbox.write_text("<svg><!-- changed source --></svg>\n")
            self.assertNotEqual(before, source.source_manifest(self.root)["sha256"])

    def test_direct_compile_without_ticket_is_rejected(self):
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "direct compilation"):
                source.consume_ticket(self.root, "tiny")

    def test_stale_simulator_source_is_rejected(self):
        self.reserve(environment="simulator_x3", execution_root=str(self.root), mirror_src={})
        with self.assertRaisesRegex(ValueError, "stale/different"):
            self.consume("simulator_x3")

    def test_exact_simulator_source_can_consume_ticket(self):
        import hashlib
        expected = {"src/reader.cpp": hashlib.sha256((self.root / "src/reader.cpp").read_bytes()).hexdigest()}
        self.reserve(environment="simulator_x3", execution_root=str(self.root), mirror_src=expected)
        _, ticket = self.consume("simulator_x3")
        self.assertTrue(ticket["consumed"])


if __name__ == "__main__":
    unittest.main()
