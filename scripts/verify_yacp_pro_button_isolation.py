"""Compare Pro touch and Home with the imported C3 reader profile on and off."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from verify_yacp_reader_buttons import KEYS


def run(program, output, firmware_version=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    images = ("next", "menu", "home")
    for enabled in (0, 1):
        case = output / str(enabled)
        case.mkdir()
        sd = create_fixture(case)
        settings = sd / ".crosspoint/settings.json"
        values = json.loads(settings.read_text())
        values.update(trackReadingStats=0, readerFrontButtonsEnabled=enabled,
                      longPressMenuAction=9 if enabled else 22, longPressBackAction=18 if enabled else 22,
                      longPwrBtn=3 if enabled else 23, readerShortPowerAction=10 if enabled else 23,
                      sideButtonLongPress=5,
                      frontButtonOrientationAware=2 if enabled else 3, sideButtonOrientationAware=enabled,
                      **dict(zip(KEYS, (3, 2, 1, 0))))
        if firmware_version:
            values['buttonLayoutPromptVersion'] = firmware_version
        settings.write_text(json.dumps(values), encoding="utf-8")
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
            CROSSPOINT_SIM_INPUT_SCRIPT="1800:TAP:240,350;6000:DOWN:1200;10000:HOME:1300;14000:TAP:22,66;15000:SWIPE:100,350,380,350;17500:HOME:100;20500:QUIT",
            CROSSPOINT_SIM_SCREENSHOTS=f"8500:{case}/next.bmp;12800:{case}/menu.bmp;19500:{case}/home.bmp")
        with (case / "run.log").open("x", encoding="utf-8") as log:
            result = subprocess.run([str(program)], cwd=case, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=30)
        assert result.returncode == 0
        logs = (case / "run.log").read_text()
        # Pro defaults to CrossPoint's toolbar overlay, not the C3 full-screen menu.
        assert "Stored BW buffer" in logs and "Entering activity: EpubReaderMenu" not in logs
        assert logs.count("Entering activity: YacpHome") == 2
        assert 'Reader action ' not in logs
        for name in images:
            with Image.open(case / (name + ".bmp")) as image:
                image.save(case / (name + ".png"))
    for name in images:
        with Image.open(output / "0" / (name + ".bmp")) as first, Image.open(output / "1" / (name + ".bmp")) as second:
            assert first.tobytes() == second.tobytes(), name
    report = {"passed": True, "pro_home_touch_menu_pixel_identical": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--firmware-version")
    a = p.parse_args()
    run(a.program, a.output, a.firmware_version)
