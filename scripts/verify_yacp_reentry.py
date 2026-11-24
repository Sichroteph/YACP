"""Execute the actual firmware's completed-migration/legacy-return regression.

Only isolated cards are written. No device connection or firmware installation.
Runs on the Windows/WSL host through the existing native boot harness.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

from check_yacp_source import ROOT, source_manifest
from deploy_yacp import receipt
from verify_yacp_runtime_migration import boot, hashes, synthetic, unix
from yacp_credential_migration import fnv1a, xor_bytes
from yacp_data_formats import fnv64_book_path, progress_record
from yacp_legacy_anchor import first_cached_line, chapter_html, unique_text_offset

KEY = bytes.fromhex("deadbeef0001")
PASSWORD = b"qa-password-19chars"
assert len(PASSWORD) == 19


def encoded(value):
    return base64.b64encode(xor_bytes(value, KEY)).decode()


def legacy_wifi():
    envelope = b"CPV1" + struct.pack("<I", fnv1a(KEY + PASSWORD)) + PASSWORD
    return {"lastConnectedSsid": "QA", "credentials": [{"ssid": "QA", "password_obf": encoded(envelope)}]}


def rewritten_wifi():
    result = legacy_wifi()
    row = result["credentials"][0]
    raw = xor_bytes(base64.b64decode(row["password_obf"]), KEY)
    row.update(password_len=len(raw), password_crc32=zlib.crc32(raw))
    fresh = b"manually-entered"
    result["credentials"].append({"ssid": "Fresh", "password_obf": encoded(fresh),
        "password_len": len(fresh), "password_crc32": zlib.crc32(fresh)})
    return result


def run(program, output, real_snapshot=None):
    program = program.resolve(strict=True)
    build = json.loads(program.with_suffix(".json").read_text(encoding="utf-8"))
    fingerprint = source_manifest()["sha256"]
    assert build["source"]["sha256"] == fingerprint, "Native executable must match the current source"
    assert hashlib.sha256(program.read_bytes()).hexdigest() == build["sha256"]
    output.mkdir(parents=True, exist_ok=False)
    seed = output / "seed"
    synthetic(seed)
    cp = seed / "sd/.crosspoint"
    source = cp / fnv64_book_path("/Book2.epub")
    (source / "progress.bin").write_bytes(struct.pack("<HHH", 0, 0, 2))
    stats = bytearray(73)
    struct.pack_into("<BHII", stats, 0, 6, 4, 2000, 18)
    (source / "stats_v6.bin").write_bytes(stats)
    (cp / "wifi.json").write_text(json.dumps(legacy_wifi()))
    boot(program, seed)
    original = seed / "sd/.yacp-migration-v1/original"
    frozen = hashes(original)
    native = next(p.parent.name for p in cp.glob("epub_*/migration-source-path")
                  if p.read_text() == "/Book2.epub")
    legacy = fnv64_book_path("/Book2.epub")

    def fixture(name, mode=0):
        out = output / name
        shutil.copytree(seed / "sd", out / "sd")
        data = out / "sd/.crosspoint"
        old = data / legacy
        shutil.copytree(original / legacy, old)
        (old / "progress.bin").write_bytes(struct.pack("<HHH", 0, 1, 2))
        if mode:
            profile = bytearray(86)
            profile[0:2] = b"\2\4"
            profile[4] = mode
            (old / "reader_settings.bin").write_bytes(profile)
            shutil.copy2(old / "sections/0.bin", old / f"sections/0_{'balanced' if mode == 1 else 'light'}.bin")
        (data / "crossink-settings.json").write_text(json.dumps({"buttonLayoutPromptVersion": "1.7.2-yacp.501-tiny"}))
        (data / "wifi.json").write_text(json.dumps(rewritten_wifi()))
        return out

    def validate(out):
        data = out / "sd/.crosspoint"
        saved = progress_record((data / native / "progress.bin").read_bytes())
        assert saved["spine"] == 0 and saved["visible_text_offset"] == len("Earlier sentence"), saved
        assert (data / native / "stats_v6.bin").read_bytes() == bytes(stats)
        wifi = json.loads((data / "wifi.json").read_text())
        row = wifi["credentials"][0]
        assert xor_bytes(base64.b64decode(row["password_obf"]), KEY) == PASSWORD
        assert row["password_len"] == len(PASSWORD) and row["password_crc32"] == zlib.crc32(PASSWORD)
        assert wifi["credentials"][1] == rewritten_wifi()["credentials"][1]
        assert hashes(out / "sd/.yacp-migration-v1/original") == frozen
        assert not (out / "sd/.yacp-reentry-v2/pending").exists()
        assert (data / "legacy-receipts" / legacy).stat().st_size == 160
        assert (data / "legacy-receipts" / legacy).read_bytes() == receipt(data / legacy, "/Book2.epub")

    cases = []
    for mode in (0, 1, 2):
        out = fixture(f"return-mode-{mode}", mode)
        before = hashes(out / "sd/.crosspoint")
        boot(program, out)
        validate(out)
        assert hashes(out / "sd/.yacp-reentry-v2/history/1/original") == before
        # Native reading advances after recovery. Old legacy data must not rewind it.
        p = out / "sd/.crosspoint" / native / "progress.bin"
        advanced = struct.pack("<HHHI", 0, 1, 2, len("Earlier sentence") + 2)
        p.write_bytes(advanced)
        resume = boot(program, out, extra_env={"YACP_TEST_DEEP_RESUME": "1"})
        assert "Checked deep-sleep resume" in resume
        assert p.read_bytes() == advanced
        boot(program, out)
        assert p.read_bytes() == advanced
        assert len(list((out / "sd/.yacp-reentry-v2/history").iterdir())) == 1
        # A second legacy return changes the previously acknowledged source.
        (out / "sd/.crosspoint" / legacy / "progress.bin").write_bytes(struct.pack("<HHH", 0, 0, 2))
        boot(program, out)
        assert progress_record(p.read_bytes())["visible_text_offset"] == 0
        assert len(list((out / "sd/.yacp-reentry-v2/history").iterdir())) == 2
        cases.append(f"return-mode-{mode}-native-advance-second-return")

    for phase in ("reentry-original", "reentry-copied", "reentry-wifi", "reentry-book", "reentry-prepared", "reentry-activated"):
        out = fixture(phase)
        before = hashes(out / "sd/.crosspoint")
        boot(program, out, phase)
        boot(program, out)
        validate(out)
        assert hashes(out / "sd/.yacp-reentry-v2/history/1/original") == before
        cases.append(phase + "-resumed")

    for failure in ("missing-section", "conflicting-native", "invalid-wifi-envelope", "invalid-native-password", "corrupt-source-receipt"):
        out = fixture(failure)
        data = out / "sd/.crosspoint"
        if failure == "missing-section":
            # Rename preserves the fixture's missing file without deleting it.
            p = data / legacy / "sections/0.bin"
            p.rename(p.with_suffix(".preserved"))
        elif failure == "conflicting-native":
            (data / native / "progress.bin").write_bytes(struct.pack("<HHHI", 0, 1, 2, 7))
        elif failure == "invalid-wifi-envelope":
            document = rewritten_wifi()
            row = document["credentials"][0]
            raw = bytearray(xor_bytes(base64.b64decode(row["password_obf"]), KEY))
            raw[4] ^= 1
            row.update(password_obf=encoded(raw), password_crc32=zlib.crc32(raw))
            (data / "wifi.json").write_text(json.dumps(document))
        elif failure == "corrupt-source-receipt":
            folder = data / "legacy-receipts"
            folder.mkdir(exist_ok=True)
            broken = bytearray(receipt(data / legacy, "/Book2.epub"))
            broken[10] ^= 1
            (folder / legacy).write_bytes(broken)
        else:
            document = rewritten_wifi()
            value = b"invalid\0password"
            document["credentials"][1].update(password_obf=encoded(value), password_len=len(value),
                                               password_crc32=zlib.crc32(value))
            (data / "wifi.json").write_text(json.dumps(document))
        before = hashes(data)
        log = boot(program, out)
        assert "Migration paused" in log
        preserved = out / "sd/.yacp-reentry-v2/pending/original"
        assert hashes(preserved if preserved.exists() else data) == before
        assert hashes(out / "sd/.yacp-migration-v1/original") == frozen
        cases.append(failure + "-blocked-originals-preserved")

    if real_snapshot:
        real_snapshot = real_snapshot.resolve(strict=True)
        out = output / "real-congo-return"
        sd = out / "sd"
        cp = sd / ".crosspoint"
        cp.mkdir(parents=True)
        book = "/Congo - Michael Crichton.epub"
        shutil.copy2(real_snapshot / book[1:], sd / book[1:])
        old = cp / fnv64_book_path(book)
        shutil.copytree(real_snapshot / ".crosspoint" / old.name, old)
        (cp / "settings.json").write_text(json.dumps({"language": "EN", "trackReadingStats": 0}))
        (cp / "wifi.json").write_text(json.dumps(rewritten_wifi()))
        done = sd / ".yacp-migration-v1/complete"
        done.parent.mkdir()
        done.write_bytes(b"YACPMIG\1")
        current = progress_record((old / "progress.bin").read_bytes())
        line = first_cached_line((old / f"sections/{current['spine']}.bin").read_bytes(), current["page"])
        expected = unique_text_offset(chapter_html(sd / book[1:], current["spine"]), line)
        book_hash = hashlib.sha256((sd / book[1:]).read_bytes()).hexdigest()
        boot(program, out)
        converted = [progress_record(p.read_bytes()) for p in cp.glob("epub_*/progress.bin") if p.parent.name != old.name]
        assert len(converted) == 1 and converted[0]["spine"] == current["spine"] and converted[0]["visible_text_offset"] == expected
        assert expected == 949 and current["spine"] == 38
        assert hashlib.sha256((sd / book[1:]).read_bytes()).hexdigest() == book_hash
        cases.append("real-congo-spine-38-offset-949")

    assert source_manifest()["sha256"] == fingerprint, "Source changed while exercising the firmware"
    report = {"passed": True, "schema": 2, "source_sha256": fingerprint, "program_sha256": build["sha256"],
              "cases": cases, "case_count": len(cases), "deep_resume_no_conversion_scan": True, "hardware_validated": False, "installed_devices": 0}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    accepted = ROOT / "qa-artifacts/data-safety-acceptance" / (fingerprint + ".json")
    accepted.parent.mkdir(exist_ok=True)
    accepted.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--real-snapshot", type=Path)
    args = parser.parse_args()
    run(args.program, args.output, args.real_snapshot)
