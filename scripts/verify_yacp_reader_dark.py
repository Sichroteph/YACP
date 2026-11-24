"""Check reader-only and global output polarity on a synthetic Pro SD."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def run(program, output, mode):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings_path = sd / ".crosspoint/settings.json"
    values = json.loads(settings_path.read_text(encoding="utf-8"))
    values.update(readerDarkMode=int(mode == "reader"), screenInverted=int(mode == "global"))
    settings_path.write_text(json.dumps(values), encoding="utf-8")
    actions = ["1800:TAP:240,350", "5000:TAP:240,350", "7000:TAP:240,750", "8500:CONFIRM:100"]
    if mode == "toggle":
        actions += ["10000:TAP:410,340"]
        actions += [f"{11500+i*600}:DOWN:100" for i in range(5)]
        actions += ["15000:CONFIRM:100"]
    actions += ["17000:BACK:100", "19000:TAP:240,200", "20000:TAP:240,200", "25000:HOME:100", "28000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"4000:{output}/before.bmp;16000:{output}/settings.bmp;23000:{output}/reader.bmp;27000:{output}/home.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    saved = json.loads(settings_path.read_text(encoding="utf-8"))
    assert saved["readerDarkMode"] == int(mode == "reader")
    if mode == 'toggle':
        profiles = list((sd / '.crosspoint/book_profiles').glob('epub_*/reader_profile.*'))
        assert len(profiles) == 1
        assert decode(profiles[0].read_bytes())['settings']['readerDarkMode'] == 1
    assert saved["screenInverted"] == int(mode == "global")
    report = {"passed": True, "mode": mode, "reader_dark_mode": saved["readerDarkMode"],
              "global_night_mode": saved["screenInverted"], "visual_review_required": True,
              "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--mode", choices=("off", "reader", "global", "toggle"), required=True)
    a = p.parse_args()
    run(a.program, a.output, a.mode)
