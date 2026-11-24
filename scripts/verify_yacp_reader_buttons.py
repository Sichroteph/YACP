"""Exercise C3 reader button remapping through the CrossPoint settings UI."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture

KEYS = ("readerFrontButtonBack", "readerFrontButtonConfirm", "readerFrontButtonLeft", "readerFrontButtonRight")


def run(program, output, mode):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / ".crosspoint/settings.json"
    values = json.loads(settings.read_text())
    values.update(trackReadingStats=0, readerFrontButtonsEnabled=int(mode != "assign"),
                  **dict(zip(KEYS, (3, 2, 1, 0))))
    if mode == "invalid":
        values[KEYS[0]] = 256
    elif mode == "partial":
        del values[KEYS[0]]
    settings.write_text(json.dumps(values), encoding="utf-8")
    actions = ["1800:BACK:100"] + [f"{2600+i*500}:DOWN:100" for i in range(4)]
    actions += ["5100:CONFIRM:100", "6200:CONFIRM:100", "7000:CONFIRM:100",
                "7800:DOWN:100", "8400:DOWN:100", "9300:CONFIRM:100"]
    shots = [(8900, "controls"), (10400, "remap")]
    if mode == "assign":
        actions += ["11400:RIGHT:100", "12300:LEFT:100", "13200:LEFT:100",
                    "15500:CONFIRM:100", "16600:BACK:100", "18000:BACK:100",
                    "19000:BACK:100", "20200:CONFIRM:100", "24500:BACK:100",
                    "28000:CONFIRM:100", "31000:LEFT:100", "34000:RIGHT:100",
                    "36200:RIGHT:1200", "40000:QUIT"]
        shots += [(14400, "duplicate"), (26900, "next"), (30100, "previous"),
                  (32800, "reader-menu"), (39000, "home")]
        expected, enabled = (3, 2, 1, 0), 1
    else:
        actions += ["11500:UP:100" if mode == "reset" else "11500:DOWN:100",
                    "13200:BACK:100", "14500:BACK:100", "16000:QUIT"]
        expected, enabled = ((0, 1, 2, 3), 0) if mode in ("reset", "invalid", "partial") else ((3, 2, 1, 0), 1)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{t}:{output}/{name}.bmp" for t, name in shots))
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=55)
    assert result.returncode == 0
    saved = json.loads(settings.read_text())
    assert saved["readerFrontButtonsEnabled"] == enabled
    assert tuple(saved[k] for k in KEYS) == expected
    assert tuple(saved[k] for k in ("frontButtonBack", "frontButtonConfirm", "frontButtonLeft", "frontButtonRight")) == (0, 1, 2, 3)
    logs = (output / "run.log").read_text()
    assert "Entering activity: ButtonRemap" in logs
    if mode == "assign":
        assert "Entering activity: EpubReaderMenu" in logs
        progress = list((sd / ".crosspoint").glob("epub_*/progress.bin"))
        assert len(progress) == 1
        data = progress[0].read_bytes()
        assert int.from_bytes(data[:2], "little") == 0, "Previous-page button did not return to the first chapter"
        assert logs.count("Entering activity: YacpHome") >= 2
    for _, name in shots:
        with Image.open(output / (name + ".bmp")) as picture:
            picture.save(output / (name + ".png"))
    report = {"passed": True, "mode": mode, "system_mapping_unchanged": True,
              "hardware_validated": False, "visual_review_required": True}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--mode", choices=("assign", "reset", "cancel", "invalid", "partial"), default="assign")
    a = p.parse_args()
    run(a.program, a.output, a.mode)
