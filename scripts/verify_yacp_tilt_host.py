"""Compile the actual tilt HAL against a controllable IMU on Linux/WSL."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / "tilt-power"
    command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
               "-Itest/yacp_tilt/stubs", "-Ilib/hal", "test/yacp_tilt/TiltPowerTest.cpp",
               "lib/hal/HalTiltSensor.cpp", "-o", str(executable)]
    with (output / "run.log").open("x") as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = {"passed": True, "orientation_mode_combinations": 16,
              "power_policy": "enabled and in reader", "failures": ["initial sleep", "wake", "sleep", "absent IMU"],
              "sanitizers": ["address", "undefined"], "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    run(p.parse_args().output)
