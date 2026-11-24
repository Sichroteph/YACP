"""Exercise the firmware's TXT/Markdown migration on isolated simulator cards."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from check_yacp_source import source_manifest
from deploy_yacp import ACCEPTANCE_ROOT, txt_receipt
from verify_yacp_runtime_migration import boot, hashes
from yacp_data_formats import fnv64_book_path

PASSAGES = "\ufeff  é&<>  \r\n\t中  Passage\r\nNext"
SOURCE = PASSAGES.encode("utf-8")
PASSAGE = SOURCE.index(b"Passage")
NEXT = SOURCE.index(b"Next")
VISIBLE_PASSAGE = 11
VISIBLE_NEXT = 18


def native_record(offset):
    return struct.pack("<HHHI", 0, 0, 0, offset)


def make_card(folder, book):
    active = folder / "sd/.crosspoint"
    active.mkdir(parents=True, exist_ok=False)
    (active / "settings.json").write_text(json.dumps({
        "language": "EN", "yacpReaderFontInitialized": True, "trackReadingStats": 0,
    }))
    (active.parent / book.lstrip("/")).write_bytes(SOURCE)
    return active


def old(txt, anchor, indexed=False):
    txt.mkdir(parents=True, exist_ok=False)
    (txt / "progress.bin").write_bytes(struct.pack("<HH" if indexed else "<HI", 1, 0 if indexed else anchor))
    if indexed:
        header = struct.pack("<4sBIiiiiBI", b"ITXT", 3, len(SOURCE), 400, 25, 100, 10, 0, 2)
        (txt / "index.bin").write_bytes(header + struct.pack("<II", 0, anchor))


def run(program, output):
    program = program.resolve(strict=True)
    report = json.loads(program.with_suffix(".json").read_text(encoding="utf-8"))
    fingerprint = source_manifest()["sha256"]
    assert report["source"]["sha256"] == fingerprint
    assert hashlib.sha256(program.read_bytes()).hexdigest() == report["sha256"]
    output.mkdir(parents=True, exist_ok=False)
    cases = []

    for extension, indexed in ((".TXT", False), (".md", True)):
        out = output / ("markdown" if indexed else "txt")
        book = "/Guide" + extension
        cache = fnv64_book_path(book).replace("epub_", "txt_", 1)
        active = make_card(out, book)
        original = active / cache
        old(original, PASSAGE, indexed)
        snapshot = hashes(original)
        boot(program, out, "reentry-text")
        boot(program, out)
        native = next(active.glob("epub_*/progress.bin"))
        assert native.read_bytes() == native_record(VISIBLE_PASSAGE)
        history = active.parent / ".yacp-reentry-v2/history"
        assert not original.exists()
        assert hashes(history / "1/original" / cache) == snapshot
        assert hashes(history / "1/retired-txt" / cache) == snapshot
        assert (active / "txt-receipts" / cache).read_bytes() == txt_receipt(history / "1/original" / cache, len(SOURCE))
        cases.append(f"{extension}-anchor-interruption-retained-original")

        # A downgrade can recreate the legacy cache. An unchanged receipt
        # must leave a more recent native reading position intact.
        native.write_bytes(native_record(VISIBLE_NEXT))
        old(original, PASSAGE, indexed)
        boot(program, out)
        assert native.read_bytes() == native_record(VISIBLE_NEXT)
        assert not original.exists()
        cases.append(f"{extension}-unchanged-source-keeps-native-advance")

        # The different key is the old CrossPoint native TXT cache. Equal
        # anchors may be acknowledged without replacing the native record.
        native_old = active / native.parent.name.replace("epub_", "txt_", 1)
        old(native_old, NEXT, indexed)
        boot(program, out)
        assert native.read_bytes() == native_record(VISIBLE_NEXT)
        assert not native_old.exists()
        cases.append(f"{extension}-cpp-native-txt-cache-recognized")

        # A fresh card with both useful positions and no receipt is ambiguous.
        out = output / ("markdown-conflict" if indexed else "txt-conflict")
        conflict = make_card(out, book)
        old(conflict / cache, PASSAGE, indexed)
        (conflict / native.parent.name).mkdir()
        (conflict / native.parent.name / native.name).write_bytes(native_record(VISIBLE_NEXT))
        before = hashes(conflict)
        assert "Conflicting native and old plain text positions" in boot(program, out)
        assert hashes(conflict) == before
        cases.append(f"{extension}-unacknowledged-conflict-blocked")

    out = output / "damaged-index"
    book = "/Guide.txt"
    active = make_card(out, book)
    legacy = active / fnv64_book_path(book).replace("epub_", "txt_", 1)
    old(legacy, PASSAGE, True)
    index = legacy / "index.bin"
    index.write_bytes(index.read_bytes()[:-1])
    before = hashes(active)
    assert "Plain text position requires review" in boot(program, out)
    assert hashes(active) == before
    cases.append("old-page-only-record-with-damaged-index-blocked")

    assert source_manifest()["sha256"] == fingerprint
    result = json.dumps({"passed": True, "source_sha256": fingerprint, "program_sha256": report["sha256"],
                         "cases": cases}, indent=2)
    (output / "result.json").write_text(result)
    ACCEPTANCE_ROOT.mkdir(parents=True, exist_ok=True)
    (ACCEPTANCE_ROOT / (fingerprint + "-txt.json")).write_text(result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    run(args.program, args.output)
