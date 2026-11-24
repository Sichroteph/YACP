"""Compare the firmware C++ statistics readers with independent decoders on an SD copy."""
import argparse
import json
from pathlib import Path
import subprocess
from yacp_data_formats import book_stats, daily_history, global_stats


def verify(root, binary):
    subprocess.run([str(binary)], check=True)
    counts = {"book_files": 0, "global_files": 0, "daily_files": 0}

    def round_trip(mode, file):
        output = subprocess.check_output([str(binary), mode, str(file)], text=True)
        return bytes.fromhex(output.strip())

    for file in sorted(root.glob("epub_*/stats*.bin")):
        before = book_stats(file.read_bytes())
        after = book_stats(round_trip("book", file))
        for key, expected in before.items():
            if key == "version":
                continue
            if key == "flags" and before["version"] < 6:
                expected &= 3
            if after[key] != expected:
                raise ValueError(f"Book field differs: {file.name}: {key}")
        counts["book_files"] += 1
    for file in sorted(root.glob("global_stats.bin*")):
        before = global_stats(file.read_bytes())
        after = global_stats(round_trip("global", file))
        if any(after[key] != value for key, value in before.items() if key != "version"):
            raise ValueError(f"Global statistics differ: {file.name}")
        counts["global_files"] += 1
    for file in sorted(root.glob("daily_reading.bin*")):
        before = daily_history(file.read_bytes())
        after = daily_history(round_trip("daily", file))
        if before["anchor_day"] != after["anchor_day"] or before["minutes"] != after["minutes"]:
            raise ValueError(f"Daily history differs: {file.name}")
        counts["daily_files"] += 1
    return dict(counts, cpp_round_trips_verified=True, full_migration_validated=False)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--binary", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.source.resolve(), args.binary.resolve()), indent=2))
