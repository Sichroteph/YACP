"""A missing SD family must remain selected and load again after being restored."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from verify_yacp_simulator_stats import create_fixture


def digests(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def run(program, source_family, output):
    program = program.resolve(strict=True)
    source_family = source_family.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    before = digests(source_family)
    sd = create_fixture(output)
    settings = sd / ".crosspoint/settings.json"
    values = json.loads(settings.read_text())
    values.update(sdFontFamilyName=source_family.name, fontSize=16, trackReadingStats=0)
    settings.write_text(json.dumps(values), encoding="utf-8")
    for phase in ("missing", "restored"):
        if phase == "restored":
            shutil.copytree(source_family, sd / ".fonts" / source_family.name)
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT="1800:TAP:240,350;6000:HOME:100;8000:QUIT",
                   CROSSPOINT_SIM_SCREENSHOTS=f"4500:{output}/{phase}.bmp")
        with (output / (phase + ".log")).open("x", encoding="utf-8") as log:
            result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=20)
        assert result.returncode == 0
        saved = json.loads(settings.read_text())
        assert saved["sdFontFamilyName"] == source_family.name and saved["fontSize"] == 16
        logs = (output / (phase + ".log")).read_text()
        assert "Rendered page in" in logs
        if phase == "missing":
            assert "SD font family not found:" in logs and "selection preserved" in logs
        else:
            assert "Loaded SD font family: " + source_family.name in logs
    assert digests(source_family) == before
    report = {"passed": True, "selection_and_size_preserved": True, "restored_family_loaded": True,
              "source_fonts_unchanged": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--source-family", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.source_family, a.output)
