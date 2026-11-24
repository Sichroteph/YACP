"""Check the X3 current option through physical-button settings navigation."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_yacp_simulator_stats import create_fixture


def run(program, output, device):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    actions = ["1800:BACK:100"] + [f"{2600+i*600}:DOWN:100" for i in range(4)]
    actions += ["5200:CONFIRM:100", "6400:CONFIRM:100", "7600:UP:100", "8500:CONFIRM:100",
                "9600:UP:100"]
    if device == "x3":
        actions += ["12000:CONFIRM:100"]
    actions += ["15000:BACK:100", "16500:BACK:100", "18000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"11000:{output}/shown.bmp;13500:{output}/hidden.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=30)
    assert result.returncode == 0
    assert "Entering activity: StatusBarSettings" in (output / "run.log").read_text(encoding="utf-8")
    saved = json.loads((sd / ".crosspoint/settings.json").read_text(encoding="utf-8"))
    assert saved["statusBarBatteryCurrent"] == int(device != "x3")
    report = {"passed": True, "device": device, "current_option_toggled_off": device == "x3",
              "gauge_telemetry_simulated": False, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", choices=("x3", "pro"), default="x3")
    a = p.parse_args()
    run(a.program, a.output, a.device)
