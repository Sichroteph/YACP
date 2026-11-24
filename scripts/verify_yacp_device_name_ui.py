"""Exercise the existing CrossPoint keyboard from the appended device-name setting."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run_one(program, output, board, cancel):
    case = output / ("cancel" if cancel else "save")
    case.mkdir(parents=True)
    sd = create_fixture(case)
    settings = sd / ".crosspoint/settings.json"
    values = json.loads(settings.read_text())
    values["deviceName"] = "QA Reader"
    settings.write_text(json.dumps(values), encoding="utf-8")
    actions = ["1800:TAP:240,756" if board == "pro" else "1800:BACK:100"]
    actions += [f"{2600 + i * 500}:DOWN:100" for i in range(4)]
    actions += ["5100:CONFIRM:100", "6000:CONFIRM:100", "6800:CONFIRM:100", "7600:CONFIRM:100",
                "8400:UP:100"]
    actions += (["9300:TAP:240,746", "11000:TAP:30,560"] if board == "pro" else
                ["9300:CONFIRM:100", "11000:CONFIRM:100"])
    if cancel:
        actions += ["12500:TAP:22,63" if board == "pro" else "12500:BACK:100"]
    else:
        # From the keyboard's number-row first key, wrap to bottom then its OK.
        actions += (["13600:TAP:419,756"] if board == "pro" else
                    ["12000:UP:100", "12800:LEFT:100", "13600:CONFIRM:100"])
    actions += ["16000:BACK:100", "17000:BACK:100", "19500:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"9000:{case}/setting.bmp;11600:{case}/keyboard.bmp;15100:{case}/returned.bmp")
    with (case / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=case, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=32)
    assert result.returncode == 0
    log = (case / "run.log").read_text()
    assert "Entering activity: KeyboardEntry" in log and "Exiting activity: KeyboardEntry" in log
    saved = json.loads(settings.read_text())
    assert saved["deviceName"] == ("QA Reader" if cancel else "QA Reader1"), saved["deviceName"]
    for path in case.glob("*.bmp"):
        with Image.open(path) as picture:
            picture.save(path.with_suffix(".png"))
    return {"case": case.name, "passed": True}


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        cases = list(pool.map(lambda cancel: run_one(program, output, board, cancel), (False, True)))
    report = {"board": board, "cases": cases, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--board", choices=("x3", "pro"), required=True)
    a = p.parse_args()
    run(a.program, a.output, a.board)
