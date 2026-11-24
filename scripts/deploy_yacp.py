"""Guarded YACP deployment. Default: validate and back up; --apply transfers/installs.

All installation checks and backups run on this PC using the existing HTTP APIs or a mounted card.
Backups contain private reading data and credentials. Never put them in Nextcloud.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
from urllib.parse import urlencode
from urllib.request import Request, urlopen
from urllib.error import HTTPError
import uuid
import zlib

from check_yacp_source import ROOT, assert_canonical, source_manifest
from migrate_yacp_copy import text_progress
from verify_yacp_image import verify
from yacp_credential_migration import xor_bytes, fnv1a
from yacp_data_formats import fnv64_book_path, esp32_book_path, progress_record
from yacp_legacy_anchor import legacy_section_name

MODELS = {"X3": {"tiny"}, "X4": {"tiny"}, "X4 Classic": {"x4c"}, "X4 Pro": {"x4pro"},
          "xteink_x4_pro": {"x4pro"}}
ACCEPTANCE_ROOT = ROOT / "qa-artifacts/data-safety-acceptance"


def require_acceptance(fingerprint):
    path = ACCEPTANCE_ROOT / (fingerprint + ".json")
    if not path.is_file():
        raise ValueError("Run the completed-migration firmware regression for these exact source inputs first")
    evidence = json.loads(path.read_text(encoding="utf-8"))
    required = {"return-mode-0-native-advance-second-return", "reentry-original-resumed",
                "reentry-activated-resumed", "conflicting-native-blocked-originals-preserved",
                "invalid-wifi-envelope-blocked-originals-preserved"}
    if (evidence.get("passed") is not True or evidence.get("schema") != 2
            or evidence.get("source_sha256") != fingerprint or not evidence.get("program_sha256")
            or not required.issubset(evidence.get("cases", []))):
        raise ValueError("Incomplete or stale firmware data-safety acceptance")
    text_path = ACCEPTANCE_ROOT / (fingerprint + "-txt.json")
    if not text_path.is_file():
        raise ValueError("Run the TXT/Markdown firmware migration regression for these exact source inputs first")
    text = json.loads(text_path.read_text(encoding="utf-8"))
    required_text = {"old-page-only-record-with-damaged-index-blocked"}
    for extension in (".TXT", ".md"):
        required_text.update(extension + suffix for suffix in (
            "-anchor-interruption-retained-original", "-unchanged-source-keeps-native-advance",
            "-cpp-native-txt-cache-recognized", "-unacknowledged-conflict-blocked"))
    if (text.get("passed") is not True or text.get("source_sha256") != fingerprint
            or text.get("program_sha256") != evidence["program_sha256"]
            or not required_text.issubset(text.get("cases", []))):
        raise ValueError("Incomplete or stale TXT/Markdown firmware migration acceptance")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def image_report(image):
    assert_canonical()
    report = json.loads(image.with_suffix(".json").read_text(encoding="utf-8"))
    ticket = report.get("build_ticket", {})
    if (not report.get("build_succeeded") or not ticket.get("consumed") or ticket.get("lease_active")
            or report.get("environment") not in set.union(*MODELS.values())
            or ticket.get("environment") != report["environment"]):
        raise ValueError("Expected a successful canonical build with a closed build session")
    if report["source"]["sha256"] != source_manifest()["sha256"]:
        raise ValueError("Image source does not match the current canonical inputs")
    require_acceptance(report["source"]["sha256"])
    identity = verify(image.read_bytes(), report["environment"], report["version"])
    if identity["sha256"] != report["sha256"] or identity["bytes"] != report["bytes"]:
        raise ValueError("Image differs from its paired build report")
    expected = f"YACP-{report['version']}-{report['environment']}.bin"
    if image.name != expected:
        raise ValueError("Use the descriptive YACP filename paired with the build report")
    return report


def private_output(path, source=None):
    path = path.absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction() or parent.name.lower() == "nextcloud":
            raise ValueError("Use an unlinked private directory outside Nextcloud")
    path = path.resolve()
    if source and (path == source or source in path.parents or path in source.parents):
        raise ValueError("The backup must be separate from the source card")
    path.mkdir(parents=True, exist_ok=False)
    return path


def receipt(cache, book):
    data = (cache / "progress.bin").read_bytes()
    if len(data) not in (4, 6, 10):
        raise ValueError("Unsupported legacy position")
    result = bytearray(160)
    result[:8] = b"YACPRCP\2"
    result[8] = len(data)
    result[9:9 + len(data)] = data
    profile = cache / "reader_settings.bin"
    raw = profile.read_bytes() if profile.exists() else None
    if raw is not None:
        if len(raw) not in (3, 86):
            raise ValueError("Unsupported legacy profile")
        result[19] = len(raw)
        result[20:20 + len(raw)] = raw
    section = cache / "sections" / legacy_section_name(progress_record(data)["spine"], raw)
    if section.exists():
        with section.open("rb") as stream:
            header = stream.read(44)
        if len(header) != 44:
            raise ValueError("Truncated legacy section")
        result[106] = 44
        result[107:151] = header
    struct.pack_into("<I", result, 151, fnv1a(result[:151]))
    return bytes(result)


def txt_receipt(cache, file_size):
    old = cache / "progress.bin"
    if old.stat().st_size not in (4, 6):
        raise ValueError("Invalid TXT/Markdown position requires review")
    raw = old.read_bytes()
    index = None
    if len(raw) == 4:
        index_file = cache / "index.bin"
        # 16-bit page numbers cannot reliably identify a wrapped index.
        if not index_file.is_file() or not 30 <= index_file.stat().st_size <= 30 + 4 * 65536:
            raise ValueError("TXT/Markdown page index requires review")
        index = index_file.read_bytes()
    if not 0 <= file_size < 0xFFFFFFFF:
        raise ValueError("TXT/Markdown source size requires review")
    try:
        _, offset = struct.unpack("<HI", text_progress(raw, file_size, index))
    except ValueError as exc:
        raise ValueError("TXT/Markdown byte offset requires review") from exc
    result = bytearray(struct.pack("<4sB6sxII", b"YTX\1", len(raw), raw, file_size, offset))
    result += struct.pack("<I", fnv1a(result))
    return bytes(result)


def audit_text_positions(cp, book, file_size):
    current = esp32_book_path(book)
    candidates = {
        current.replace("epub_", "txt_", 1),
        fnv64_book_path(book).replace("epub_", "txt_", 1),
    }
    new = cp / current / "progress.bin"
    pending_offsets = set()
    for key in candidates:
        cache = cp / key
        if not (cache / "progress.bin").is_file():
            continue
        pin = cache / "migration-source-path"
        if pin.exists() and pin.read_bytes() != book.encode("utf-8"):
            raise ValueError("TXT/Markdown cache identity requires review")
        expected = txt_receipt(cache, file_size)
        saved = cp / "txt-receipts" / key
        if saved.exists():
            if saved.stat().st_size != len(expected):
                raise ValueError("TXT/Markdown receipt requires review")
            data = saved.read_bytes()
            if data[:4] != b"YTX\1" or struct.unpack_from("<I", data, 20)[0] != fnv1a(data[:20]):
                raise ValueError("TXT/Markdown receipt requires review")
            if data == expected:
                continue
        pending_offsets.add(struct.unpack_from("<I", expected, 16)[0])
    if len(pending_offsets) > 1 or (pending_offsets and new.exists()):
        raise ValueError("Mixed TXT/Markdown positions require review before deployment")


def audit_card(card, factory_key, book_paths=None):
    if not isinstance(factory_key, bytes) or len(factory_key) != 6:
        raise ValueError("An explicit six-byte factory identity is required")
    if (card / ".yacp-reentry-v2/pending").exists():
        raise ValueError("An interrupted data repair must finish before installation")
    cp = card / ".crosspoint"
    if (cp / "book-move.bin").exists():
        raise ValueError("An interrupted book move must finish before installation")
    if not (cp / "settings.json").is_file():
        raise ValueError("Native YACP settings required; migrate the original card first")
    wifi = cp / "wifi.json"
    if wifi.exists():
        document = json.loads(wifi.read_text(encoding="utf-8"))
        if (not isinstance(document, dict) or not isinstance(document.get("credentials"), list)
                or len(document["credentials"]) > 8):
            raise ValueError("Wi-Fi credential schema requires review")
        for row in document["credentials"]:
            if (not isinstance(row, dict) or not isinstance(row.get("ssid"), str)
                    or not 1 <= len(row["ssid"].encode("utf-8")) <= 32 or "\0" in row["ssid"]
                    or not isinstance(row.get("password_obf"), str)
                    or type(row.get("password_len")) is not int or type(row.get("password_crc32")) is not int):
                raise ValueError("Wi-Fi credential schema requires review")
            value = xor_bytes(base64.b64decode(row["password_obf"], validate=True), factory_key)
            if (value.startswith(b"CPV1") or len(value) > 64 or b"\0" in value
                    or row.get("password_len") != len(value) or row.get("password_crc32") != zlib.crc32(value)):
                raise ValueError("Wi-Fi credential migration or integrity check required")
    if book_paths is None:
        book_paths = {}
        for directory, folders, names in os.walk(card, followlinks=False, onerror=walk_error):
            folders[:] = [n for n in folders if not n.startswith(".") and n != "System Volume Information"]
            for name in names:
                path = Path(directory, name)
                suffix = path.suffix.lower()
                if suffix in (".epub", ".txt", ".md", ".xtc", ".xtch"):
                    book_paths["/" + path.relative_to(card).as_posix()] = path.stat().st_size
    for book, file_size in book_paths.items():
        suffix = Path(book).suffix.lower()
        if suffix in (".txt", ".md"):
            audit_text_positions(cp, book, file_size)
            continue
        kind = {".epub": "epub", ".xtc": "xtc", ".xtch": "xtc"}.get(suffix)
        if not kind:
            continue
        old = cp / fnv64_book_path(book).replace("epub_", kind + "_", 1)
        native = cp / esp32_book_path(book).replace("epub_", kind + "_", 1)
        if old == native or not (old / "progress.bin").exists():
            continue
        if kind != "epub":
            raise ValueError("Mixed fixed-page positions require review before deployment")
        pin = cp / "legacy-receipts" / old.name
        if not pin.is_file() or pin.read_bytes() != receipt(old, book):
            raise ValueError("Unreconciled legacy/native reading position; deployment blocked")
        if not (native / "progress.bin").is_file():
            raise ValueError("Converted native reading position is missing")
    return True


def walk_error(error):
    raise error


def copy_card(card, output):
    rows = []
    for directory, folders, names in os.walk(card, followlinks=False, onerror=walk_error):
        (output / "card" / Path(directory).relative_to(card)).mkdir(parents=True, exist_ok=True)
        for name in folders + names:
            item = Path(directory, name)
            if item.is_symlink() or item.is_junction():
                raise ValueError("Linked card paths are not supported")
        for name in names:
            source = Path(directory, name)
            rel = source.relative_to(card)
            target = output / "card" / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            with source.open("rb") as src, target.open("xb") as dst:
                shutil.copyfileobj(src, dst)
            before = digest(source)
            if digest(target) != before:
                raise ValueError("Card backup verification failed")
            rows.append({"path": rel.as_posix(), "bytes": source.stat().st_size, "sha256": before})
    return rows


class Device:
    def __init__(self, address, timeout=180):
        if not re.fullmatch(r"(?:[0-9]{1,3}\.){3}[0-9]{1,3}", address):
            raise ValueError("Use the reader's explicit IPv4 address")
        self.base, self.timeout = "http://" + address, timeout

    def request(self, path, data=None, content_type=None):
        headers = {"Content-Type": content_type} if content_type else {}
        try:
            with urlopen(Request(self.base + path, data=data, headers=headers), timeout=self.timeout) as response:
                return response.read()
        except HTTPError as error:
            body = error.read(4096).decode("utf-8", errors="replace")
            raise ValueError(f"Reader HTTP {error.code}: {body}") from None

    def json(self, path, data=None):
        return json.loads(self.request(path, data))


def remote_entries(device, path):
    entries = device.json("/api/files?" + urlencode({"path": path}))
    if not isinstance(entries, list):
        raise ValueError("Invalid remote directory listing")
    names = set()
    for entry in entries:
        name = entry.get("name", "")
        if (not isinstance(name, str) or not name or name in names or name in (".", "..")
                or re.search(r"[/\\:\x00-\x1f]", name)):
            raise ValueError("Invalid remote snapshot filename")
        names.add(name)
    return entries


def require_visible_listing(device):
    settings = device.json("/api/settings")
    if not isinstance(settings, list) or not any(
            row.get("key") == "showHiddenFiles" and row.get("value") in (True, 1) for row in settings):
        raise ValueError("Enable the existing showHiddenFiles setting or use a mounted card for a complete backup")


def copy_snapshot(device, remote, output):
    """Copy the live store to this PC and compare it again. No device writes."""
    rows = []
    def visit(path, relative, verify=False, depth=0):
        if depth > 24:
            raise ValueError("Snapshot nesting exceeds the backup bound")
        found = set()
        target_dir = output / "card" / relative
        if not verify:
            target_dir.mkdir(parents=True, exist_ok=False)
        for entry in remote_entries(device, path):
            name = entry["name"]
            found.add(name)
            child, rel = path + "/" + name, relative / name
            if entry["isDirectory"]:
                visit(child, rel, verify, depth + 1)
                continue
            data = device.request("/download?" + urlencode({"path": child}))
            if len(data) != entry["size"]:
                raise ValueError("Truncated device data backup")
            target = output / "card" / rel
            if not verify:
                with target.open("xb") as stream:
                    stream.write(data)
            checksum = hashlib.sha256(data).hexdigest()
            if not target.is_file() or digest(target) != checksum:
                raise ValueError("Local backup readback failed or live data changed")
            if not verify:
                rows.append({"path": rel.as_posix(), "bytes": len(data), "sha256": checksum})
        if verify and {p.name for p in target_dir.iterdir()} != found:
            raise ValueError("Device data changed during the backup")
    visit(remote, PurePosixPath(".crosspoint"))
    visit(remote, PurePosixPath(".crosspoint"), verify=True)
    if not (output / "card/.crosspoint/settings.json").is_file():
        raise ValueError("The backup does not include the live reader settings")
    return rows


def remote_books(device, path="/", depth=0):
    if depth > 24:
        raise ValueError("Library nesting exceeds the audit bound")
    books = {}
    for entry in remote_entries(device, path):
        name = entry["name"]
        child = path.rstrip("/") + "/" + name
        if name == ".yacp-reentry-v2" and any(row["name"] == "pending" for row in remote_entries(device, child)):
            raise ValueError("An interrupted data repair must finish before installation")
        if name.startswith(".") or name in ("System Volume Information", "XTCache"):
            continue
        if entry["isDirectory"]:
            books.update(remote_books(device, child, depth + 1))
        elif Path(name).suffix.lower() in (".epub", ".txt", ".md", ".xtc", ".xtch"):
            books[child] = entry["size"]
    return books


def deploy(image, backup, *, device=None, card=None, model=None, factory_key=None, apply=False):
    image = image.resolve(strict=True)
    report = image_report(image)  # Before any network or card mutation.
    card = card.resolve(strict=True) if card else None
    backup = private_output(backup, card)
    status = device.json("/api/status") if device else {"device": model}
    if status.get("device") not in MODELS or report["environment"] not in MODELS[status["device"]]:
        raise ValueError("Reader model and firmware variant do not match")
    if device:
        if not isinstance(status.get("serial"), str) or status["serial"].strip().casefold() in ("", "not found", "unknown"):
            raise ValueError("The reader must identify itself before deployment")
        require_visible_listing(device)
        rows = copy_snapshot(device, "/.crosspoint", backup)
        audit_card(backup / "card", factory_key, remote_books(device))
        require_visible_listing(device)
        after = device.json("/api/status")
        if any(after.get(k) != status.get(k) for k in ("serial", "device", "version")):
            raise ValueError("Reader identity changed during the data backup")
    else:
        if model not in MODELS or factory_key is None:
            raise ValueError("Card deployment requires an explicit reader model and factory MAC")
        rows = copy_card(card, backup)
        audit_card(card, factory_key)
    (backup / "manifest.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    (backup / "BACKUP-VERIFIED.json").write_text(json.dumps({"verified": True, "files": len(rows),
        "image_sha256": report["sha256"], "device": status}, indent=2), encoding="utf-8")
    image_report(image)  # Refuse changed source or image after a long backup.
    result = {"backup_verified": True, "files": len(rows), "version": report["version"], "installed": False}
    def journal():
        (backup / "deployment.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    journal()
    if apply:
        if device:
            boundary = "yacp-" + uuid.uuid4().hex
            body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{image.name}"\r\n'
                    'Content-Type: application/octet-stream\r\n\r\n').encode() + image.read_bytes()
            body += f"\r\n--{boundary}--\r\n".encode()
            device.request("/upload?path=/", body, "multipart/form-data; boundary=" + boundary)
            result.update(phase="install_requested", installed=None)
            journal()
            response = device.request("/api/firmware/install", urlencode({"path": "/" + image.name,
                "confirm": "install"}).encode(), "application/x-www-form-urlencoded").decode()
            if response.strip() != "Firmware installed, restarting":
                raise ValueError("Reader did not acknowledge a successful install: " + response)
            result.update(installed=True, phase="acknowledged", response=response.strip())
            # No status request after success: firmware leaves File Transfer on reboot.
        else:
            for row in rows:
                if digest(card / row["path"]) != row["sha256"]:
                    raise ValueError("Card data changed after backup; refusing firmware copy")
            for source in (image, image.with_suffix(".json")):
                target = card / source.name
                with source.open("rb") as src, target.open("xb") as dst:
                    shutil.copyfileobj(src, dst)
                    dst.flush()
                    os.fsync(dst.fileno())
                if digest(source) != digest(target):
                    raise ValueError("Firmware/report card copy verification failed")
            result["copied_to_card"] = True
    journal()
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument("--device")
    destination.add_argument("--card", type=Path)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--model", choices=MODELS)
    parser.add_argument("--factory-mac")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    key = bytes.fromhex(args.factory_mac.replace(":", "").replace("-", "")) if args.factory_mac else None
    print(json.dumps(deploy(args.image, args.backup, device=Device(args.device) if args.device else None,
        card=args.card, model=args.model, factory_key=key, apply=args.apply)))
