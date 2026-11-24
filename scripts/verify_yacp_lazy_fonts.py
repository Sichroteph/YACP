"""Check built-in reading defers a populated SD font catalogue until settings."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from verify_yacp_simulator_stats import create_fixture


def run(program, font_copy, output, corrupt_family=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    shutil.copytree(font_copy, sd / ".fonts")
    actions = ["1800:TAP:240,350", "7000:HOME:100", "10000:TAP:240,756"]
    actions += [f"{11000+i*400}:DOWN:100" for i in range(4)]
    actions += ["13500:CONFIRM:100", "19000:QUIT"]
    if corrupt_family:
        if Path(corrupt_family).name != corrupt_family or corrupt_family in (".", ".."):
            raise ValueError("Expected a single copied font-family directory name")
        files = list((sd / ".fonts" / corrupt_family).glob("*_16.cpfont"))
        if len(files) != 1:
            raise ValueError("Expected exactly one copied 16-point font")
        files[0].write_bytes(b"Invalid copied font fixture")
        settings = sd / ".crosspoint/settings.json"
        values = json.loads(settings.read_text(encoding="utf-8"))
        values.update(sdFontFamilyName=corrupt_family, fontSize=16)
        settings.write_text(json.dumps(values), encoding="utf-8")
        actions = ["1800:TAP:240,350", "7000:HOME:100", "19000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"5000:{output}/reader.bmp;16500:{output}/settings.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=32)
    assert result.returncode == 0
    log = (output / "run.log").read_text(encoding="utf-8")
    if corrupt_family:
        assert json.loads(settings.read_text(encoding="utf-8"))["sdFontFamilyName"] == corrupt_family
        assert "selection preserved" in log and "Rendered page in" in log
        report = {"passed": True, "saved_sd_selection_preserved": True,
                  "builtin_fallback_rendered": True, "hardware_validated": False}
    else:
        assert "Built-in font selected; SD discovery deferred" in log
        discovery = log.index("Discovering requested SD font catalogue")
        assert log.index("Entering activity: Settings") < discovery
        assert log.index("Rendered page in") < discovery
        assert log.count("Discovering requested SD font catalogue") == 1
        report = {"passed": True, "catalogue_scans_before_settings": 0,
                  "catalogue_scans_after_settings": 1, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--font-copy", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--corrupt-family")
    a = p.parse_args()
    run(a.program, a.font_copy, a.output, a.corrupt_family)
