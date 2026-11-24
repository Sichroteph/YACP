"""Exercise CrossPoint's file rows with the optional YACP display preferences."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from verify_yacp_simulator_stats import create_fixture


def run(program, output, lines, hide_extension):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / ".crosspoint"
    settings_path = root / "settings.json"
    settings = json.loads(settings_path.read_text(encoding="utf-8"))
    settings.update(fileBrowserDisplay=lines - 1, hideFileExtension=int(hide_extension), trackReadingStats=0)
    settings_path.write_text(json.dumps(settings), encoding="utf-8")
    (root / "recent.json").write_text('{"books":[]}', encoding="utf-8")
    names = [f"{chr(65+i)} Long book title with accents éàü and enough words to wrap across two file browser rows {i}.epub"
             for i in range(8)]
    for name in names:
        shutil.copyfile(sd / "completion.epub", sd / name)
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sd.glob("*.epub")}
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT="1800:CONFIRM:100;7000:TAP:200,135;11000:HOME:100;14000:QUIT",
               CROSSPOINT_SIM_SCREENSHOTS=f"5000:{output}/browser.bmp;10000:{output}/reader.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0
    state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    assert state["openEpubPath"] == "/" + names[0], state["openEpubPath"]
    assert before == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sd.glob("*.epub")}
    report = {"passed": True, "max_lines": lines, "hidden_extension": hide_extension,
              "tapped_first_book": True, "source_books_unchanged": True,
              "visual_review_required": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--lines", type=int, choices=(1, 2), required=True)
    p.add_argument("--hide-extension", action="store_true")
    a = p.parse_args()
    run(a.program, a.output, a.lines, a.hide_extension)
