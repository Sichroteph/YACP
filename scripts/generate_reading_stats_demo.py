#!/usr/bin/env python3
"""Generate deterministic simulator data for the reading statistics screens."""

from __future__ import annotations

import argparse
import struct
from datetime import date, timedelta
from pathlib import Path


HISTORY_DAYS = 730
HISTORY_BYTES = 92
GLOBAL_STATS_SIZE = 159
EPOCH = date(2000, 1, 1)
FINISHED_BOOKS = (
    ("Dune", 8 * 3600 + 42 * 60, date(2026, 6, 12), date(2026, 8, 1)),
    ("The Hobbit", 8 * 3600 + 21 * 60, date(2026, 7, 1), date(2026, 7, 19)),
    ("Project Hail Mary", 13 * 3600 + 7 * 60, date(2026, 6, 16), date(2026, 7, 8)),
    ("1984", 6 * 3600 + 42 * 60, date(2026, 6, 10), date(2026, 6, 24)),
    ("Pride and Prejudice", 9 * 3600 + 16 * 60, date(2026, 5, 18), date(2026, 6, 11)),
    ("The Left Hand of Darkness", 7 * 3600 + 35 * 60, date(2026, 5, 7), date(2026, 5, 29)),
    ("The Count of Monte Cristo", 18 * 3600 + 52 * 60, date(2026, 3, 20), date(2026, 5, 12)),
    ("The Little Prince", 2 * 3600 + 4 * 60, date(2026, 4, 29), date(2026, 4, 30)),
)


def day_index(value: date) -> int:
    return (value - EPOCH).days


def demo_minutes(anchor: date) -> list[int]:
    """Return minutes indexed by age, where index 0 is the anchor date."""
    values = [0] * HISTORY_DAYS

    for age in range(420):
        current = anchor - timedelta(days=age)
        signal = (current.toordinal() * 37 + current.month * 19 + current.day * 11) % 100
        threshold = 68 if current.weekday() in (1, 3, 5, 6) else 48
        if signal < threshold:
            values[age] = 18 + ((current.toordinal() * 29 + age * 7) % 108)

    # Make the current streak and an earlier long streak easy to read.
    for age in range(8):
        values[age] = 35 + age * 7
    for age in range(92, 107):
        values[age] = 42 + (age % 6) * 12

    # Leave two deliberate gaps so the heatmap does not look synthetic.
    for age in range(38, 44):
        values[age] = 0
    for age in range(176, 185):
        values[age] = 0

    return values


def longest_streak(minutes: list[int]) -> int:
    longest = 0
    current = 0
    for value in minutes:
        if value:
            current += 1
            longest = max(longest, current)
        else:
            current = 0
    return longest


def split_total(total: int, weights: list[int]) -> list[int]:
    values = [(total * weight) // sum(weights) for weight in weights]
    values[-1] += total - sum(values)
    return values


def build_global_stats(anchor: date, minutes: list[int]) -> bytes:
    total_seconds = sum(minutes) * 60
    reading_bits = bytearray(HISTORY_BYTES)
    for age, value in enumerate(minutes):
        if value:
            reading_bits[age // 8] |= 1 << (age % 8)

    day_of_week_minutes = [0] * 7
    for age, value in enumerate(minutes):
        current = anchor - timedelta(days=age)
        day_of_week_minutes[current.weekday()] += value

    payload = bytearray(GLOBAL_STATS_SIZE)
    payload[0] = 3
    struct.pack_into("<I", payload, 1, 268)
    struct.pack_into("<I", payload, 5, total_seconds)
    struct.pack_into("<I", payload, 9, 7634)
    struct.pack_into("<I", payload, 13, 14)

    for index, value in enumerate(split_total(total_seconds, [12, 19, 31, 38])):
        struct.pack_into("<I", payload, 17 + index * 4, value)
    for index, value in enumerate(day_of_week_minutes):
        struct.pack_into("<I", payload, 33 + index * 4, value * 60)

    struct.pack_into("<I", payload, 61, day_index(anchor))
    payload[65:157] = reading_bits
    struct.pack_into("<H", payload, 157, longest_streak(minutes))
    return bytes(payload)


def build_finished_books() -> bytes:
    payload = bytearray(b"CPFB")
    payload.extend(struct.pack("<BBH", 2, len(FINISHED_BOOKS), 0))
    for path_key, (title, total_seconds, start_date, finished_date) in enumerate(
        FINISHED_BOOKS, start=1
    ):
        title_bytes = title.encode("utf-8")
        payload.extend(
            struct.pack(
                "<QI HBB HBB H",
                path_key,
                total_seconds,
                start_date.year,
                start_date.month,
                start_date.day,
                finished_date.year,
                finished_date.month,
                finished_date.day,
                len(title_bytes),
            )
        )
        payload.extend(title_bytes)
    return bytes(payload)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Create plausible reading statistics for simulator screenshots."
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path("fs_"),
        help="Simulator SD-card root. Default: ./fs_",
    )
    parser.add_argument(
        "--anchor-date",
        type=date.fromisoformat,
        default=date.today(),
        help="Last day in the generated history, in YYYY-MM-DD form.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Replace existing demo target files.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stats_dir = args.output_root / ".crosspoint"
    global_path = stats_dir / "global_stats.bin"
    daily_path = stats_dir / "daily_reading.bin"
    finished_books_path = stats_dir / "finished_books.bin"

    existing = [
        path
        for path in (global_path, daily_path, finished_books_path)
        if path.exists()
    ]
    if existing and not args.force:
        joined = ", ".join(str(path) for path in existing)
        raise SystemExit(f"Refusing to replace existing files without --force: {joined}")

    stats_dir.mkdir(parents=True, exist_ok=True)
    minutes = demo_minutes(args.anchor_date)
    global_path.write_bytes(build_global_stats(args.anchor_date, minutes))

    daily_header = b"CRHM" + bytes([1]) + struct.pack("<I", day_index(args.anchor_date))
    daily_path.write_bytes(daily_header + bytes(minutes))
    finished_books_path.write_bytes(build_finished_books())

    print(f"Generated Reading Rhythm demo for {args.anchor_date.isoformat()}")
    print(f"Reading days: {sum(1 for value in minutes if value)}")
    print(f"Total reading time: {sum(minutes) // 60} h {sum(minutes) % 60} min")
    print(f"Longest streak: {longest_streak(minutes)} days")
    print(f"Finished books: {len(FINISHED_BOOKS)}")
    print(f"Output: {stats_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
