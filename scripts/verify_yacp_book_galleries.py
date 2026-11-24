"""Verify per-book sleep BMPs, persistent non-repeat and global fallback on X3."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageDraw

from verify_yacp_simulator_stats import create_fixture


def gallery_name(book):
    value = 14695981039346656037
    for byte in book.encode("utf-8"):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}-{Path(book).stem}"


def wallpaper(path, name, index):
    image = Image.new("RGB", (528, 792), "white")
    draw = ImageDraw.Draw(image)
    draw.text((40, 40), name, fill="black")
    draw.rectangle((40 + index * 90, 130, 100 + index * 90, 650), fill="black")
    image.save(path)


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    for name in ("other.epub", "fallback.epub"):
        shutil.copyfile(sd / "completion.epub", sd / name)
    root = sd / ".crosspoint"
    settings = json.loads((root / "settings.json").read_text())
    settings.update(sleepScreen=2, quickResumeSleepScreen=0, trackReadingStats=0,
                    autoBackupStats=0, sleepScreenCoverFilter=1)
    (root / "settings.json").write_text(json.dumps(settings), encoding="utf-8")
    galleries = sd / ".book-galleries"
    first = galleries / gallery_name("/completion.epub")
    second = galleries / gallery_name("/other.epub")
    first.mkdir(parents=True)
    second.mkdir()
    wallpaper(first / "first.bmp", "Book A: first image", 0)
    wallpaper(first / "second.bmp", "Book A: second image", 1)
    wallpaper(second / "other.bmp", "Book B: its own image", 2)
    wallpaper(sd / "sleep.bmp", "Global fallback image", 3)
    (first / "invalid.bmp").write_bytes(b"Invalid BMP, must not be selected")
    hashes = {str(p.relative_to(sd)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sd.rglob("*.bmp")}
    selections = []
    cases = [("a1", "/completion.epub", first), ("a2", "/completion.epub", first),
             ("b", "/other.epub", second), ("fallback", "/fallback.epub", None)]
    for label, book, gallery in cases:
        (root / "state.json").write_text('{"openEpubPath":""}', encoding="utf-8")
        (root / "recent.json").write_text(json.dumps({"books": [{"path": book, "title": "Gallery Test",
            "author": "YACP QA", "coverBmpPath": ""}]}), encoding="utf-8")
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
            CROSSPOINT_SIM_INPUT_SCRIPT="2000:CONFIRM:100;8500:POWER:800;14500:QUIT",
            CROSSPOINT_SIM_SCREENSHOTS=f"12500:{output}/{label}.bmp")
        with (output / (label + ".log")).open("x", encoding="utf-8") as log:
            result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=24)
        assert result.returncode == 0
        log = (output / (label + ".log")).read_text()
        assert (output / (label + ".bmp")).is_file()
        if gallery:
            selected = (gallery / ".last-sleep-bmp").read_text()
            assert selected in ("first.bmp", "second.bmp") if label.startswith("a") else selected == "other.bmp"
            assert "Rendering book gallery sleep image: /.book-galleries/" + gallery.name + "/" + selected in log
            selections.append(selected)
        else:
            assert "Loading: /sleep.bmp" in log
            assert "Rendering book gallery sleep image" not in log
        print(json.dumps({"case": label, "passed": True}), flush=True)
    assert selections[0] != selections[1]
    assert all(hashlib.sha256((sd / name).read_bytes()).hexdigest() == digest for name, digest in hashes.items())
    report = {"passed": True, "selected_images": selections, "global_fallback": True,
              "source_images_unchanged": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.program, args.output)
