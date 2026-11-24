"""Audit a verified .crosspoint snapshot without displaying credentials or rewriting data."""
import argparse
import json
from pathlib import Path

from yacp_data_formats import (bookmarks, clippings, book_stats, daily_history, finished_books,
                               fnv64_book_path, font_point_size, global_stats, progress_record, book_reader_settings)
from yacp_font_catalog import verified_catalog


def audit(root, variant, fonts=None):
    settings = json.loads((root / "crossink-settings.json").read_text(encoding="utf-8"))
    state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    recent_doc = json.loads((root / "recent.json").read_text(encoding="utf-8"))
    recents = recent_doc if isinstance(recent_doc, list) else recent_doc.get("books", [])
    paths = {b["path"] for b in recents if "path" in b}
    if state.get("openEpubPath"):
        paths.add(state["openEpubPath"])
    cache_names = {fnv64_book_path(path): path for path in paths}
    result = {"source_variant": variant,
              "font": {"family": settings.get("sdFontFamilyName") or settings.get("fontFamily"),
                       "point_size": None},
              "open_book": state.get("openEpubPath"), "books": [], "bookmarks": [], "clippings": [],
              "migration_validated": False}
    try:
        result["font"]["point_size"] = font_point_size(settings, variant, (fonts or {}).get(settings.get("sdFontFamilyName")))
    except ValueError as error:
        result["font"]["conversion_blocker"] = str(error)
    for directory in sorted(root.glob("epub_*")):
        if not directory.is_dir():
            continue
        entry = {"cache": directory.name, "known_path": cache_names.get(directory.name),
                 "stats_files": [p.name for p in sorted(directory.glob("stats*.bin"))]}
        entry["stats"] = {p.name: book_stats(p.read_bytes()) for p in sorted(directory.glob("stats*.bin"))}
        progress = directory / "progress.bin"
        if progress.is_file():
            entry["progress"] = progress_record(progress.read_bytes())
            entry["has_exact_text_anchor"] = "visible_text_offset" in entry["progress"]
        reader_settings = directory / "reader_settings.bin"
        if reader_settings.is_file():
            entry["reader_settings"] = book_reader_settings(reader_settings.read_bytes())
            entry["reader_settings_migration_validated"] = False
        result["books"].append(entry)
    for file in sorted((root / "bookmarks").glob("*.bin")):
        result["bookmarks"].append({"file": file.name, "decoded": bookmarks(file.read_bytes())})
    for file in sorted((root / "clippings").glob("*.bin")):
        result["clippings"].append({"file": file.name, "decoded": clippings(file.read_bytes())})
    daily = root / "daily_reading.bin"
    if daily.exists():
        data = daily.read_bytes()
        history = daily_history(data)
        result["daily_history"] = {"version": history["version"], "bytes": len(data),
                                   "anchor_day": history["anchor_day"], "minutes": sum(history["minutes"])}
    result["global_stats"] = {p.name: global_stats(p.read_bytes()) for p in sorted(root.glob("global_stats.bin*"))}
    result["synced_stats"] = {p.name: global_stats(p.read_bytes()) for p in sorted((root / "synced_stats").glob("*.bin"))}
    result["finished_books"] = {p.name: finished_books(p.read_bytes()) for p in sorted(root.glob("finished_books.bin*"))}
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--source-variant", required=True, choices=("tiny", "xlarge"))
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fonts-snapshot", type=Path)
    args = parser.parse_args()
    fonts, _ = verified_catalog(args.fonts_snapshot) if args.fonts_snapshot else ({}, None)
    result = audit(args.source, args.source_variant, fonts)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(result, output, indent=2, ensure_ascii=False)
    print(f"Audited {len(result['books'])} caches and {len(result['bookmarks'])} bookmark files")
