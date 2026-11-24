"""Exercise the cover-free Home with synthetic books on touch or button simulators."""
import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def simulate(program, directory, sd, actions, captures):
    directory.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_RENDER_DRIVER="software",
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=";".join(
                   f"{time}:{directory}/{name}.bmp" for time, name in captures))
    with (directory / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=directory, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=35)
    assert result.returncode == 0, directory
    for _, name in captures:
        with Image.open(directory / f"{name}.bmp") as image:
            image.save(directory / f"{name}.png")
    return (directory / "run.log").read_text(encoding="utf-8")


def run(program, output, touch):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / ".crosspoint"
    settings = json.loads((root / "settings.json").read_text())
    settings.update(trackReadingStats=0, clockHasBeenSynced=1, clockDateHasBeenSynced=1)
    (root / "settings.json").write_text(json.dumps(settings), encoding="utf-8")
    shutil.copyfile(sd / "completion.epub", sd / "other.epub")
    books = [
        dict(path="/completion.epub", title="The Art of Reading", author="Alex Morgan",
             coverBmpPath="/.crosspoint/forbidden-cover.bmp"),
        dict(path="/other.epub", title="A Very Long Book Title About Quiet Afternoons and the Pleasure of Reading Together",
             author="Alexandra Morgan and Benjamin Richardson", coverBmpPath="/.crosspoint/forbidden-cover.bmp")]
    # A visible, valid cover sentinel must never replace the typographic card.
    Image.new("1", (320, 480), 0).save(root / "forbidden-cover.bmp")
    caches = []
    for index, book in enumerate(books):
        existing = set(root.glob("epub_*"))
        (root / "recent.json").write_text(json.dumps({"books": [book]}), encoding="utf-8")
        (root / "state.json").write_text('{"openEpubPath":""}', encoding="utf-8")
        open_book = "1800:TAP:240,350" if touch else "1800:CONFIRM:100"
        simulate(program, output / f"prepare-{index}", sd,
                 [open_book, "5500:HOME:100", "7500:QUIT"], [])
        created = set(root.glob("epub_*")) - existing
        assert len(created) == 1, created
        caches.append(created.pop())
    assert len(caches) == 2, caches
    # Each bootstrap identifies its cache using the reader's own platform hash.
    for index, cache in enumerate(caches):
        stats = bytearray(73)
        struct.pack_into("<BHII", stats, 0, 6, 3, 1800 if index else 5400, 30)
        struct.pack_into("<I", stats, 69, 3600 if index else 9900)
        (cache / "stats_v6.bin").write_bytes(stats)
        (cache / "progress.bin").write_bytes(struct.pack("<HHH", 0, index, 4))
    (root / "recent.json").write_text(json.dumps({"books": books}), encoding="utf-8")
    (root / "state.json").write_text('{"openEpubPath":""}', encoding="utf-8")
    before = {p.relative_to(sd): p.read_bytes() for p in sd.rglob("*.bin")}
    before_images = {p.relative_to(sd): p.read_bytes() for p in sd.rglob("*.bmp")}
    if touch:
        actions = ["2300:SWIPE:380,350,100,350", "4100:TAP:100,686", "6500:BACK:100",
                   "8000:TAP:370,686", "10400:BACK:100", "12000:TAP:240,756",
                   "14500:BACK:100", "16500:QUIT"]
        captures = [(1600, "home"), (3500, "long-title"), (5700, "stats"),
                    (9400, "autonomy"), (13500, "menu"), (15700, "returned")]
    else:
        actions = ["2300:RIGHT:100", "4100:BACK:100"]
        actions += [f"{5200+i*400}:DOWN:100" for i in range(5)]
        actions += ["7600:CONFIRM:100", "10100:BACK:100", "11700:BACK:100", "14000:QUIT"]
        captures = [(1600, "home"), (3500, "long-title"), (4800, "menu"),
                    (9300, "stats"), (13000, "returned")]
    logs = simulate(program, output / "navigation", sd, actions, captures)
    assert "Entering activity: BookStats" in logs, "Statistics navigation failed"
    if touch:
        assert "Entering activity: PowerStats" in logs, "Autonomy touch shortcut failed"
    assert "Entering activity: YacpHomeMenu" in logs, "Home menu navigation failed"
    with Image.open(output / "navigation/long-title.png") as selected, Image.open(
            output / "navigation/returned.png") as returned:
        assert selected.tobytes() == returned.tobytes(), "Returning changed the selected Home card"
    for relative, data in before.items():
        assert (sd / relative).read_bytes() == data, f"Home changed {relative}"
    after_images = {p.relative_to(sd): p.read_bytes() for p in sd.rglob("*.bmp")}
    assert before_images == after_images, "Home generated or rewrote cover images"
    (root / "recent.json").write_text('{"books":[]}', encoding="utf-8")
    simulate(program, output / "empty", sd, ["3000:QUIT"], [(1800, "home")])
    shutil.copyfile(sd / "completion.epub", sd / "no-stats.epub")
    (root / "recent.json").write_text(json.dumps({"books": [
        dict(path="/no-stats.epub", title="", author="", coverBmpPath="")]}), encoding="utf-8")
    simulate(program, output / "unknown", sd, ["3000:QUIT"], [(1800, "home")])
    report = dict(passed=True, touch=touch, statistics_unchanged=True, covers_unchanged=True,
                  visual_review_required=True, hardware_validated=False)
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--touch", action="store_true")
    args = parser.parse_args()
    run(args.program, args.output, args.touch)
