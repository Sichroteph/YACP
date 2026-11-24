"""Write a restartable settings experiment from a verified YACP data snapshot.

Outputs are deliberately not named settings.json and cannot be activated by a
reader. No credentials or source values are printed. This is not full migration.
"""
import argparse
import json
from pathlib import Path
import re

from prepare_yacp_migration import append_matching_json, enumerate_files, sha256
from yacp_settings_defaults import global_candidate
from yacp_book_settings_migration import book_candidate
from yacp_font_catalog import verified_catalog
from yacp_recent_books_migration import candidate as recent_candidate
from yacp_state_migration import candidate as state_candidate


def prepare(snapshot, output, include_books=False, fonts_snapshot=None, include_recents=False, include_state=False):
    snapshot = snapshot.resolve(strict=True)
    output = output.absolute()
    for path in (output, *output.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError("Output must not traverse a linked directory")
    output = output.resolve()
    if ("nextcloud" in {part.casefold() for part in output.parts} or output.drive.upper() == "G:"
            or snapshot == output or snapshot in output.parents or output in snapshot.parents):
        raise ValueError("Use a separate local experiment directory outside the SD and Nextcloud")
    manifest = json.loads((snapshot / "source-manifest.json").read_text(encoding="utf-8"))
    verified = json.loads((snapshot / "COPY-VERIFIED.json").read_text(encoding="utf-8"))
    if verified.get("copy_verified") is not True or verified.get("source_variant") != manifest.get("source_variant"):
        raise ValueError("Expected a verified snapshot with an explicit matching variant")
    source = snapshot / "original"
    if enumerate_files(source) != manifest["files"]:
        raise ValueError("Snapshot no longer matches its manifest")
    legacy = source / "crossink-settings.json"
    values = json.loads(legacy.read_text(encoding="utf-8-sig"))
    fonts, fonts_digest = verified_catalog(fonts_snapshot) if fonts_snapshot else ({}, None)
    result = global_candidate(values, manifest["source_variant"], fonts)
    origin = {"schema": 1, "source_manifest_sha256": sha256(snapshot / "source-manifest.json"),
              "settings_sha256": sha256(legacy), "source_variant": manifest["source_variant"],
              "origin": "YACP crossink-settings.json", "ready_to_activate": False}
    names = {"SOURCE.json", "candidate-settings.json", "settings-decisions.json"}
    if fonts_digest:
        origin["font_catalogue_manifest_sha256"] = fonts_digest
    book_reports = []
    if include_books:
        origin["include_book_settings"] = True
        names.add("book-settings-candidates.json")
        for path in sorted(source.glob("epub_*/reader_settings.bin")):
            book_reports.append({"source_path": path.relative_to(source).as_posix(), "source_sha256": sha256(path),
                                 "candidate": book_candidate(path.read_bytes(), values, manifest["source_variant"], fonts)})
    recent_report = None
    if include_recents:
        # Verify the current loader's bound instead of assuming the desired 18.
        header = Path(__file__).resolve().parents[1] / "src/RecentBooksStore.h"
        match = re.search(r"MAX_RECENT_BOOKS\s*=\s*(\d+)\s*;", header.read_text(encoding="utf-8"))
        if not match:
            raise ValueError("Cannot identify the target recent-book loader capacity")
        recent_source = source / "recent.json"
        recent_report = recent_candidate(json.loads(recent_source.read_text(encoding="utf-8")), int(match[1]))
        origin["recent_sha256"] = sha256(recent_source)
        origin["target_recent_capacity"] = int(match[1])
        names.add("recent-books-candidate.json")
    state_report = None
    if include_state:
        state_source = source / "state.json"
        state_report = state_candidate(json.loads(state_source.read_text(encoding="utf-8")))
        origin["state_sha256"] = sha256(state_source)
        names.add("shared-state-candidate.json")
    if output.exists():
        if not output.is_dir() or any(p.name not in names or not p.is_file() for p in output.iterdir()):
            raise ValueError("Output contains unrelated files")
        if not (output / "SOURCE.json").is_file():
            raise ValueError("Existing output has no experiment origin")
    else:
        output.mkdir(parents=True)
    append_matching_json(output / "SOURCE.json", origin)
    append_matching_json(output / "candidate-settings.json", result.pop("settings"))
    append_matching_json(output / "settings-decisions.json", result)
    if include_books:
        append_matching_json(output / "book-settings-candidates.json", book_reports)
    if recent_report is not None:
        append_matching_json(output / "recent-books-candidate.json", recent_report)
    if state_report is not None:
        append_matching_json(output / "shared-state-candidate.json", state_report)
    if enumerate_files(source) != manifest["files"]:
        raise ValueError("Snapshot changed while converting")
    summary = {"source_keys": len(values), "blocked_keys": len(result["blocked_keys"]),
               "blocked_default_fields": len(result["blocked_default_fields"]),
               "source_preserved": True, "ready_to_activate": False}
    if include_books:
        summary["book_profiles"] = len(book_reports)
    if recent_report is not None:
        summary["recent_books"] = recent_report["source_count"]
        summary["recent_blockers"] = len(recent_report["blocked"])
    if state_report is not None:
        summary["state_blockers"] = len(state_report["blocked_keys"])
        summary["gallery_order_verified"] = state_report["gallery_order_verified"]
    print(json.dumps(summary))
    return summary


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("snapshot", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--include-books", action="store_true")
    p.add_argument("--fonts-snapshot", type=Path)
    p.add_argument("--include-recents", action="store_true")
    p.add_argument("--include-state", action="store_true")
    a = p.parse_args()
    prepare(a.snapshot, a.output, a.include_books, a.fonts_snapshot, a.include_recents, a.include_state)
