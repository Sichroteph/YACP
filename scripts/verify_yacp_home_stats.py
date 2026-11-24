"""Verify selected-book Home statistics on a copied synthetic completion fixture."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def run(program, source_sd, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = output / "sd"
    shutil.copytree(source_sd, sd)
    root = sd / ".crosspoint"
    state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    state["openEpubPath"] = ""
    (root / "state.json").write_text(json.dumps(state), encoding="utf-8")
    shutil.copyfile(sd / "completion.epub", sd / "other.epub")
    (root / "recent.json").write_text(json.dumps({"books": [
        {"path": "/other.epub", "title": "Other book", "author": "YACP QA", "coverBmpPath": ""},
        {"path": "/completion.epub", "title": "Completion Test", "author": "YACP QA", "coverBmpPath": ""},
    ]}), encoding="utf-8")
    before = {p.relative_to(root): p.read_bytes() for p in root.rglob("*.bin")}
    actions = ["1800:SWIPE:380,350,100,350", "3500:TAP:240,756"]
    actions += [f"{4500+i*400}:DOWN:100" for i in range(6)]
    actions += ["7500:CONFIRM:100", "11500:TAP:40,766", "13500:BACK:100", "16000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{time}:{output}/{name}.bmp" for time, name in (
                   (2800, "selected"), (7100, "menu"), (10000, "selected-stats"), (15000, "home"))))
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=28)
    assert result.returncode == 0
    assert "Entering activity: BookStats" in (output / "run.log").read_text(encoding="utf-8")
    for relative, data in before.items():
        assert (root / relative).read_bytes() == data, f"Read-only statistics changed {relative}"
    for name in ("selected", "menu", "selected-stats", "home"):
        assert (output / (name + ".bmp")).is_file()
    report = {"passed": True, "existing_binary_data_unchanged": True,
              "scenario": "select second book, open its statistics, return to same Home card",
              "visual_review_required": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--source-sd", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.source_sd, a.output)
