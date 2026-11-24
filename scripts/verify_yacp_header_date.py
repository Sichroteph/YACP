"""Verify X3 Home date visibility and persistence with synthetic simulator data."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    results = []
    for name, synced, clock, offset in (("untrusted", 0, 0, 48), ("date", 1, 0, 48),
                                         ("clock-date", 1, 1, 0)):
        case = output / name
        case.mkdir()
        sd = create_fixture(case)
        settings = sd / ".crosspoint/settings.json"
        values = json.loads(settings.read_text())
        values.update(clockHasBeenSynced=1, clockDateHasBeenSynced=synced,
                      clockShowHeader=clock, clockUtcOffsetQ=offset, clockTimezone=255, clockDst=2,
                      trackReadingStats=0)
        settings.write_text(json.dumps(values), encoding="utf-8")
        actions = ["1800:BACK:100"] + [f"{2600+i*500}:DOWN:100" for i in range(4)]
        actions += ["5100:CONFIRM:100", "6500:BACK:100", "7800:BACK:100", "10400:QUIT"]
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
                   CROSSPOINT_SIM_SCREENSHOTS=f"1400:{case}/home.bmp;9500:{case}/returned.bmp")
        with (case / "run.log").open("x", encoding="utf-8") as log:
            result = subprocess.run([str(program)], cwd=case, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=22)
        assert result.returncode == 0
        logs = (case / "run.log").read_text()
        assert "Entering activity: Settings" in logs and "Exiting activity: Settings" in logs
        saved = json.loads(settings.read_text())
        assert saved["clockDateHasBeenSynced"] == synced and saved["clockHasBeenSynced"] == 1
        assert saved["clockShowHeader"] == clock
        for image in ("home", "returned"):
            with Image.open(case / (image + ".bmp")) as picture:
                picture.save(case / (image + ".png"))
        results.append({"case": name, "passed": True, "settings_saved": True})
    with Image.open(output / "date/home.bmp") as first, Image.open(output / "untrusted/home.bmp") as second:
        assert first.tobytes() != second.tobytes(), "Trust flag must affect the Home date"
    report = {"cases": results, "hardware_validated": False, "visual_review_required": True}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
