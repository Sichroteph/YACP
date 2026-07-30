#!/usr/bin/env python3
"""Build resize-friendly documentation previews from 1-bit e-ink captures.

The firmware and raw screenshots remain 1-bit. This script only changes the
copies published in documentation: known ordered-dither tiles are replaced by
flat grayscale pixels before a high-quality 2x resize.
"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageSequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE_DIR = ROOT / "docs" / "images" / "yacp"
DEFAULT_OUTPUT_DIR = DEFAULT_SOURCE_DIR / "media"
DEFAULT_ASSETS = (
    "home.png",
    "home-switcher.gif",
    "reading-achievement.png",
    "finished-books.png",
    "reading-stats.png",
    "reading-rhythm.png",
    "autonomy.png",
)

LIGHT_GRAY = 191  # One black pixel per 2x2 tile.
DARK_GRAY = 127  # Checkerboard: two black pixels per 2x2 tile.
PALE_GRAY = 223  # Two black pixels per 4x4 tile, used by the Home mark.


def _black_positions(image: Image.Image, x: int, y: int) -> set[tuple[int, int]]:
    return {
        (dx, dy)
        for dy in range(4)
        for dx in range(4)
        if image.getpixel((x + dx, y + dy)) < 128
    }


def _ordered_gray(
    positions: set[tuple[int, int]], x: int, y: int
) -> tuple[int, object] | None:
    if len(positions) == 4:
        for phase_x in (0, 1):
            for phase_y in (0, 1):
                expected = {
                    (phase_x, phase_y),
                    (phase_x + 2, phase_y),
                    (phase_x, phase_y + 2),
                    (phase_x + 2, phase_y + 2),
                }
                if positions == expected:
                    return LIGHT_GRAY, ((x + phase_x) % 2, (y + phase_y) % 2)

    if len(positions) == 8:
        for phase in (0, 1):
            expected = {
                (dx, dy)
                for dy in range(4)
                for dx in range(4)
                if (dx + dy) % 2 == phase
            }
            if positions == expected:
                return DARK_GRAY, (x + y + phase) % 2

    if len(positions) == 2:
        first, second = positions
        if abs(first[0] - second[0]) == 2 and abs(first[1] - second[1]) == 2:
            residues = frozenset(
                {
                    ((x + first[0]) % 4, (y + first[1]) % 4),
                    ((x + second[0]) % 4, (y + second[1]) % 4),
                }
            )
            return PALE_GRAY, residues

    return None


def _expected_mask(size: tuple[int, int], gray: int, key: object) -> Image.Image:
    width, height = size
    pixels = bytearray(width * height)

    if gray == LIGHT_GRAY:
        phase_x, phase_y = key
        for y in range(phase_y, height, 2):
            row = y * width
            for x in range(phase_x, width, 2):
                pixels[row + x] = 255
    elif gray == DARK_GRAY:
        phase = int(key)
        for y in range(height):
            row = y * width
            first_x = (phase - y) % 2
            for x in range(first_x, width, 2):
                pixels[row + x] = 255
    else:
        residues = key
        for y in range(height):
            row = y * width
            for x in range(width):
                if (x % 4, y % 4) in residues:
                    pixels[row + x] = 255

    return Image.frombytes("L", size, bytes(pixels))


def _flatten_pattern(
    source: Image.Image,
    output: Image.Image,
    gray: int,
    key: object,
    tiles: list[tuple[int, int, int, object]],
) -> None:
    seed = Image.new("L", source.size, 0)
    draw = ImageDraw.Draw(seed)
    for x, y, tile_gray, tile_key in tiles:
        if tile_gray == gray and tile_key == key:
            draw.rectangle((x, y, x + 3, y + 3), fill=255)

    # Exact tiles seed the region. Expansion reaches beneath text and rounded
    # edges, where complete 4x4 tiles naturally cannot occur.
    if gray == PALE_GRAY:
        # The pale Home mark contains no overlaid UI ink. A tighter solid
        # expansion removes its sparse 4x4 dots without carrying the card's
        # surrounding dither into the resized outline.
        region = seed.filter(ImageFilter.MaxFilter(7))
        output.paste((gray, gray, gray), mask=region)
        return

    region = seed.filter(ImageFilter.MaxFilter(17))
    expected_black = _expected_mask(source.size, gray, key)
    actual_black = source.point(lambda value: 255 if value < 128 else 0)
    actual_white = ImageChops.invert(actual_black)

    # Deviations from the periodic pattern are real UI ink or cut-outs. Grow
    # them by one pixel so strokes that happen to land on the dither phase are
    # retained as well.
    extra_black = ImageChops.subtract(actual_black, expected_black).filter(
        ImageFilter.MaxFilter(3)
    )
    extra_white = ImageChops.subtract(expected_black, actual_black).filter(
        ImageFilter.MaxFilter(3)
    )
    preserve_black = ImageChops.multiply(actual_black, extra_black)
    preserve_white = ImageChops.multiply(actual_white, extra_white)
    preserve = ImageChops.lighter(preserve_black, preserve_white)
    flatten = ImageChops.subtract(region, preserve)
    output.paste((gray, gray, gray), mask=flatten)


def dedither(image: Image.Image) -> Image.Image:
    """Replace complete renderer dither tiles while preserving UI ink."""
    source = image.convert("L")
    output = source.convert("RGB")
    width, height = source.size
    tiles: list[tuple[int, int, int, object]] = []
    counts: dict[int, Counter[object]] = {
        LIGHT_GRAY: Counter(),
        DARK_GRAY: Counter(),
        PALE_GRAY: Counter(),
    }

    for y in range(0, height - 3, 4):
        for x in range(0, width - 3, 4):
            match = _ordered_gray(_black_positions(source, x, y), x, y)
            if match is None:
                continue
            gray, key = match
            tiles.append((x, y, gray, key))
            counts[gray][key] += 1

    for gray in (LIGHT_GRAY, DARK_GRAY, PALE_GRAY):
        if counts[gray]:
            key, _ = counts[gray].most_common(1)[0]
            _flatten_pattern(source, output, gray, key, tiles)

    return output


def resize_for_media(image: Image.Image, scale: int) -> Image.Image:
    width, height = image.size
    return image.resize(
        (width * scale, height * scale),
        Image.Resampling.LANCZOS,
    )


def build_png(source: Path, destination: Path, scale: int) -> None:
    with Image.open(source) as image:
        preview = resize_for_media(dedither(image), scale)
        preview.save(destination, format="PNG", optimize=True)


def build_gif(source: Path, destination: Path, scale: int) -> None:
    with Image.open(source) as image:
        frames = [
            resize_for_media(dedither(frame.convert("RGB")), scale)
            for frame in ImageSequence.Iterator(image)
        ]
        durations = [
            frame.info.get("duration", image.info.get("duration", 1800))
            for frame in ImageSequence.Iterator(image)
        ]
        loop = image.info.get("loop", 0)

    if not frames:
        raise ValueError(f"No frame found in {source}")

    frames[0].save(
        destination,
        format="GIF",
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=loop,
        disposal=1,
        optimize=False,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate smooth grayscale documentation previews from e-ink captures."
    )
    parser.add_argument(
        "--source-dir",
        type=Path,
        default=DEFAULT_SOURCE_DIR,
        help="Directory containing the original 1-bit PNG/GIF captures.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="Destination for media-only previews.",
    )
    parser.add_argument(
        "--scale",
        type=int,
        default=2,
        help="Integer output scale. Default: 2.",
    )
    parser.add_argument(
        "assets",
        nargs="*",
        default=DEFAULT_ASSETS,
        help="Source filenames to process.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.scale < 1:
        raise ValueError("--scale must be at least 1")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for asset in args.assets:
        source = args.source_dir / asset
        destination = args.output_dir / asset
        if not source.is_file():
            raise FileNotFoundError(source)
        if source.suffix.lower() == ".gif":
            build_gif(source, destination, args.scale)
        else:
            build_png(source, destination, args.scale)
        print(f"{source.relative_to(ROOT)} -> {destination.relative_to(ROOT)}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
