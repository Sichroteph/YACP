"""Create a verified, restartable data copy for migration tests. Never modifies the source.

This is preparation, not an installer. The copy must not be used on a reader until
all semantic conversions and their acceptance tests have passed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

CHUNK = 1024 * 1024


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def append_matching_json(path, value):
    encoded = json.dumps(value, indent=2, ensure_ascii=False).encode("utf-8")
    prefix = path.read_bytes() if path.exists() else b""
    if not encoded.startswith(prefix):
        raise ValueError(f"Unexpected report contents; nothing overwritten: {path}")
    with path.open("ab" if path.exists() else "xb") as output:
        output.write(encoded[len(prefix):])


def enumerate_files(root):
    result = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f"Linked path is not a migration source: {path}")
        if path.is_file():
            result.append({"path": path.relative_to(root).as_posix(), "size": path.stat().st_size,
                           "sha256": sha256(path)})
    return result


def copy_resumable(source, destination, expected):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_symlink() or destination.is_junction():
        raise ValueError(f"Linked destination: {destination}")
    offset = destination.stat().st_size if destination.exists() else 0
    if offset > expected["size"]:
        raise ValueError(f"Destination has unexpected data: {destination}")
    # Compare the existing prefix before appending after an interrupted copy.
    with source.open("rb") as src:
        if destination.exists():
            with destination.open("rb") as dst:
                remaining = offset
                while remaining:
                    count = min(CHUNK, remaining)
                    if src.read(count) != dst.read(count):
                        raise ValueError(f"Destination differs; nothing overwritten: {destination}")
                    remaining -= count
        with destination.open("ab" if offset else "xb" if not destination.exists() else "ab") as dst:
            shutil.copyfileobj(src, dst, CHUNK)
    if destination.stat().st_size != expected["size"] or sha256(destination) != expected["sha256"]:
        raise ValueError(f"Copy verification failed: {destination}")


def prepare(source, destination, variant):
    source, destination = source.resolve(), destination.resolve()
    if not source.is_dir():
        raise ValueError("Source must be a directory containing a copy of the SD data")
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("Source and destination must be separate trees")
    if "nextcloud" in {part.casefold() for part in destination.parts}:
        raise ValueError("Nextcloud is not an archive destination")
    files = enumerate_files(source)
    manifest = {"schema": 1, "source": str(source), "source_variant": variant, "files": files}
    marker = destination / "source-manifest.json"
    pending = destination / "source-manifest.pending"
    if destination.exists():
        for path in destination.rglob("*"):
            if path.is_symlink() or path.is_junction():
                raise ValueError(f"Linked destination: {path}")
        if not marker.is_file():
            if list(destination.iterdir()) != [pending] or not pending.is_file():
                raise ValueError("Destination does not belong to this snapshot")
            append_matching_json(pending, manifest)
            pending.rename(marker)
        if json.loads(marker.read_text(encoding="utf-8")) != manifest:
            raise ValueError("Destination does not belong to this exact snapshot and variant")
    else:
        destination.mkdir(parents=True)
        append_matching_json(pending, manifest)
        pending.rename(marker)
    for entry in files:
        copy_resumable(source / entry["path"], destination / "original" / entry["path"], entry)
    # Detect source changes during copying before marking the snapshot complete.
    if enumerate_files(source) != files:
        raise ValueError("Source changed while copying; snapshot is not complete")
    completion = destination / "COPY-VERIFIED.json"
    result = {"files": len(files), "bytes": sum(f["size"] for f in files),
              "copy_verified": True, "migration_validated": False,
              "source_variant": variant}
    append_matching_json(completion, result)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--source-variant", required=True, choices=("tiny", "xlarge"))
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.destination, args.source_variant), indent=2))
