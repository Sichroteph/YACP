"""Retained, synthetic English UI checks for the unified four-reader project."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path

from PIL import Image, ImageChops
from verify_yacp_fixed_progress import xtc_fixture
from verify_yacp_home_layout import simulate
from verify_yacp_simulator_stats import create_fixture


def fixture(output, theme):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    path = sd / ".crosspoint/settings.json"
    settings = json.loads(path.read_text())
    settings.update(language="EN", uiTheme=theme, clockShowHeader=0, clockHasBeenSynced=1,
                    clockDateHasBeenSynced=1, trackReadingStats=0, sleepTimeoutMinutes=30)
    path.write_text(json.dumps(settings))
    return sd, path, settings


def selected_band(path, footer):
    with Image.open(path) as source:
        image = source.convert("L")
    width, height = image.size
    bands = []
    start = None
    for y in range(height // 2, height - footer):
        # The left inner padding has no label ink. Average the dither over
        # five rows so Lyra's gray selection is tested like a solid selection.
        dark = sum(image.getpixel((x, row)) < 180
                   for row in range(max(0, y - 2), min(height, y + 3)) for x in range(28, 35))
        filled = dark > 35 * 0.14
        if filled and start is None:
            start = y
        if not filled and start is not None:
            if y - start >= 12:
                bands.append((start, y))
            start = None
    if start is not None and height - footer - start >= 12:
        bands.append((start, height - footer))
    assert bands, f"No complete selected menu row found: {path}"
    return image.size, bands[-1]


def home(program, output, board, theme):
    sd, _, _ = fixture(output, theme)
    actions = [f"{1700 + index * 230}:DOWN:100" for index in range(6)]
    captures = [(3500, "last-menu-row")]
    if board != "pro":
        actions += ["4300:CONFIRM:100", "6200:QUIT"]
        captures.append((5600, "settings"))
    else:
        actions += ["4300:QUIT"]
    trace = simulate(program, output / "buttons", sd, actions, captures)
    assert "ButtonLayoutSetup" not in trace
    size, (top, bottom) = selected_band(output / "buttons/last-menu-row.png", 0 if board == "pro" else 40)
    assert bottom < size[1] - (0 if board == "pro" else 40)
    if board == "pro":
        # Replay from a fresh Home with a tap at the selected drawn row's centre.
        touch = actions[:-1] + [f"4300:TAP:{size[0] // 3},{(top + bottom) // 2}", "6200:QUIT"]
        trace = simulate(program, output / "touch", sd, touch, [(5600, "settings")])
    assert "Entering activity: Settings" in trace, output
    return {"case": "home", "board": board, "theme": theme, "selected_row": [top, bottom],
            "settings_opens": True, "touch_verified": board == "pro"}


def clock(program, output, board, kind):
    sd, path, settings = fixture(output, 5)
    settings.update(statusBarClock=3, statusBarProgressBar=2, clockFormat=0, screenMargin=5,
                    xtcStatusBarMode=1)
    path.write_text(json.dumps(settings))
    if kind == "epub":
        book = sd / "completion.epub"
    else:
        book = sd / ("clock." + kind)
        if kind == "txt":
            book.write_text("A synthetic reading page for the top clock.\n" * 80)
        else:
            xtc_fixture(book)
    (sd / ".crosspoint/recent.json").write_text(json.dumps({"books": [
        {"path": "/" + book.name, "title": "Clock Placement", "author": "YACP QA", "coverBmpPath": ""}]}))
    enter = "1800:TAP:240,350" if board == "pro" else "1800:CONFIRM:100"
    trace = simulate(program, output / "reader", sd, [enter, "5000:QUIT"], [(4000, "clock")])
    assert "ButtonLayoutSetup" not in trace
    with Image.open(output / "reader/clock.png") as source:
        image = source.convert("L")
    centre = image.width // 2
    ink = [(x, y) for y in range(32) for x in range(centre - 60, centre + 60)
           if image.getpixel((x, y)) < 128]
    if board == "x4":
        # Original X4 has no RTC. Keeping an imported top-clock preference must
        # neither paint a false clock nor consume a reader line.
        assert not ink or min(y for _, y in ink) >= 10
        saved = json.loads(path.read_text())
        saved["statusBarClock"] = 0
        path.write_text(json.dumps(saved))
        simulate(program, output / "clock-hidden", sd, [enter, "5000:QUIT"], [(4000, "clock")])
        with Image.open(output / "clock-hidden/clock.png") as hidden:
            assert ImageChops.difference(image, hidden.convert("L")).getbbox() is None
        return {"case": "clock", "board": board, "format": kind, "rtc_available": False,
                "no_clock_or_reserved_line": True}
    assert ink and min(y for _, y in ink) < 10, (board, kind, "clock not at top edge")
    # Ubuntu UI 10 digits have 15 pixel rows. A clipped clock can still start
    # at row zero, so checking only its first visible row misses the regression.
    rows = {y for _, y in ink}
    assert len(rows) >= 15, (board, kind, "clock digits clipped", sorted(rows))
    return {"case": "clock", "board": board, "format": kind, "first_ink_y": min(y for _, y in ink)}


def run(programs, output):
    output.mkdir(parents=True, exist_ok=False)
    jobs = []
    for board in ("x3", "pro", "classic", "x4"):
        program = Path(programs[board]).resolve(strict=True)
        for theme in (0, 2, 3):
            jobs.append((home, program, output / f"{board}-theme-{theme}", board, theme))
        for kind in ("epub", "txt", "xtc"):
            jobs.append((clock, program, output / f"{board}-clock-{kind}", board, kind))
    def execute(job):
        function, *args = job
        result = function(*args)
        print(json.dumps(result), flush=True)
        return result
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(execute, jobs))
    report = {"passed": True, "scenarios": results, "language": "English", "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({"passed": True, "scenarios": len(results)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--programs-json", type=Path, required=True,
                        help="JSON mapping x3/pro/classic/x4 to explicitly selected native executables")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(json.loads(args.programs_json.read_text()), args.output.resolve())
