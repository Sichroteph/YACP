"""Generate public, firmware and portal YACP logos from the preserved geometry."""
from __future__ import annotations

import gzip
import hashlib
import json
import math
from pathlib import Path

try:
    Import("env")
except NameError:
    ROOT = Path(__file__).resolve().parents[1]
    PIO_BUILD = False
else:
    ROOT = Path(env.subst("$PROJECT_DIR"))
    PIO_BUILD = True
LOGO_SIZE = 120
SUPERSAMPLE = 8
BOOKMARK_LEFT = 75
BOOKMARK_RIGHT = 91
BOOKMARK_TOP = 90
BOOKMARK_BOTTOM = 108
BOOKMARK_NOTCH_X = 83
BOOKMARK_NOTCH_Y = 100


def _scaled(value: int, scale: int) -> int:
    return value * scale


def _arc_points(
    center_x: float,
    center_y: float,
    radius: float,
    start_degrees: float,
    end_degrees: float,
    steps: int = 24,
) -> list[tuple[float, float]]:
    points = []
    for step in range(1, steps + 1):
        angle = math.radians(start_degrees + (end_degrees - start_degrees) * step / steps)
        points.append((center_x + radius * math.cos(angle), center_y + radius * math.sin(angle)))
    return points


def _reading_path_points() -> list[tuple[float, float]]:
    points = [(30.0, 24.0), (82.0, 24.0)]
    points.extend(_arc_points(82.0, 35.0, 11.0, -90.0, 90.0))
    points.append((30.0, 46.0))
    points.extend(_arc_points(30.0, 57.0, 11.0, -90.0, -270.0))
    points.append((82.0, 68.0))
    points.extend(_arc_points(82.0, 79.0, 11.0, -90.0, 90.0))
    points.append((30.0, 90.0))
    points.extend(_arc_points(30.0, 96.0, 6.0, -90.0, -180.0, steps=12))
    return points


def _draw_logo(image: Image.Image, scale: int) -> None:
    from PIL import ImageDraw
    draw = ImageDraw.Draw(image)
    black = (0, 0, 0, 255)
    stroke_width = _scaled(11, scale)

    # Draw the bookmark under the path so its top edge is hidden by the lower
    # stroke. This keeps the attachment flush instead of creating a black bulge.
    draw.polygon(
        [
            (_scaled(BOOKMARK_LEFT, scale), _scaled(BOOKMARK_TOP, scale)),
            (_scaled(BOOKMARK_RIGHT, scale), _scaled(BOOKMARK_TOP, scale)),
            (_scaled(BOOKMARK_RIGHT, scale), _scaled(BOOKMARK_BOTTOM, scale)),
            (_scaled(BOOKMARK_NOTCH_X, scale), _scaled(BOOKMARK_NOTCH_Y, scale)),
            (_scaled(BOOKMARK_LEFT, scale), _scaled(BOOKMARK_BOTTOM, scale)),
        ],
        fill=black,
    )

    path_points = [
        (round(_scaled(x, scale)), round(_scaled(y, scale))) for x, y in _reading_path_points()
    ]
    draw.line(path_points, fill=black, width=stroke_width, joint="curve")
    cap_radius = stroke_width // 2
    for x, y in (path_points[0], path_points[-1]):
        draw.ellipse((x - cap_radius, y - cap_radius, x + cap_radius, y + cap_radius), fill=black)


def _render_rgba(size: int) -> Image.Image:
    from PIL import Image
    working_size = LOGO_SIZE * SUPERSAMPLE
    image = Image.new("RGBA", (working_size, working_size), (255, 255, 255, 0))
    _draw_logo(image, SUPERSAMPLE)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def _write_if_changed(path: Path, data: bytes) -> None:
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def _save_png(path: Path, size: int, *, opaque_white: bool = False) -> None:
    from PIL import Image
    image = _render_rgba(size)
    if opaque_white:
        background = Image.new("RGBA", image.size, (255, 255, 255, 255))
        background.alpha_composite(image)
        image = background
    from io import BytesIO

    output = BytesIO()
    image.save(output, format="PNG", optimize=True)
    _write_if_changed(path, output.getvalue())


def _svg() -> str:
    return """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 120 120" role="img">
  <title>YACP continuous reading path</title>
  <desc>A rounded reading path crossing four lines and ending as a bookmark.</desc>
  <path d="M75 90h16v18l-8-8-8 8z" fill="#000"/>
  <path d="M30 24h52a11 11 0 0 1 0 22H30a11 11 0 0 0 0 22h52a11 11 0 0 1 0 22H30a6 6 0 0 0-6 6"
        fill="none" stroke="#000" stroke-width="11" stroke-linecap="round" stroke-linejoin="round"/>
</svg>
"""


def _firmware_header() -> str:
    from PIL import Image
    image = Image.new("RGBA", (LOGO_SIZE * SUPERSAMPLE, LOGO_SIZE * SUPERSAMPLE), (255, 255, 255, 255))
    _draw_logo(image, SUPERSAMPLE)
    image = image.resize((LOGO_SIZE, LOGO_SIZE), Image.Resampling.LANCZOS).convert("L")
    image = image.point(lambda value: 255 if value >= 128 else 0)
    # The preserved YACP and CrossPoint icon pipelines store portrait logo bits pre-rotated.
    image = image.rotate(90, expand=True)
    pixels = image.load()
    packed = []
    for y in range(LOGO_SIZE):
        for x in range(0, LOGO_SIZE, 8):
            value = 0
            for bit in range(8):
                if pixels[x + bit, y] >= 128:
                    value |= 1 << (7 - bit)
            packed.append(value)
    rows = ["    " + ", ".join(f"0x{value:02x}" for value in packed[i:i+16]) + "," for i in range(0,len(packed),16)]
    return ("#pragma once\n#include <cstdint>\n\n"
            "// Generated by scripts/build_yacp_logo.py. Do not edit.\n"
            "inline constexpr uint8_t YacpLogo120[] = {\n" + "\n".join(rows) + "\n};\n"
            'static_assert(sizeof(YacpLogo120) == 1800, "Logo must be 120x120 / 8 bytes");\n')


def _web_header(svg: bytes) -> str:
    data = gzip.compress(svg, compresslevel=9, mtime=0)
    rows = ["    " + ", ".join(f"0x{value:02x}" for value in data[i:i+16]) + "," for i in range(0,len(data),16)]
    etag = hashlib.sha256(data).hexdigest()[:16]
    return ("#pragma once\n\n// Generated by scripts/build_yacp_logo.py. Do not edit.\n"
            "inline constexpr char YacpLogoSvg[] PROGMEM = {\n" + "\n".join(rows) + "\n};\n"
            f'inline constexpr const char* YacpLogoSvgETag = "\\\"{etag}\\\"";\n')


def generate_runtime() -> None:
    outputs = [ROOT / "src/images/YacpLogo120.generated.h", ROOT / "src/network/html/YacpLogoSvg.generated.h"]
    signature = hashlib.sha256((ROOT / "scripts/build_yacp_logo.py").read_bytes()).hexdigest()
    stamp = ROOT / "src/images/YacpLogo.generated.json"
    if stamp.exists() and all(path.is_file() for path in outputs):
        try:
            previous = json.loads(stamp.read_text())
            if previous == {'generator':signature, 'outputs':[hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs]}:
                return
        except (ValueError, OSError):
            pass
    _write_if_changed(outputs[0], _firmware_header().encode("utf-8"))
    _write_if_changed(outputs[1], _web_header(_svg().encode("utf-8")).encode("utf-8"))
    stamp.write_text(json.dumps({'generator':signature, 'outputs':[hashlib.sha256(p.read_bytes()).hexdigest() for p in outputs]}))


def main() -> None:
    _write_if_changed(ROOT / "assets" / "brand" / "yacp-logo.svg", _svg().encode("utf-8"))
    _save_png(ROOT / "assets" / "brand" / "yacp-logo.png", 512)
    generate_runtime()
    print("Generated YACP logo assets")


if PIO_BUILD:
    generate_runtime()
elif __name__ == "__main__":
    main()
