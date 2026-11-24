"""Exercise numeric spacing through CrossPoint controls on a synthetic Pro SD."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED

from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import crosspoint48_page_offsets, progress_record


def run(program, output, scenario):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    book_path = sd / "completion.epub"
    with ZipFile(book_path) as book:
        entries = {name: book.read(name) for name in book.namelist()}
    paragraphs = "".join(f"<p>Paragraph {i}. This synthetic passage checks that a spacing change keeps the visible "
                         "content. Each numbered paragraph gives the reader an unambiguous anchor.</p>"
                         for i in range(150))
    entries["first.xhtml"] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Spacing test</h1>'
                              + paragraphs + '</body></html>').encode()
    with ZipFile(book_path, "w", compression=ZIP_STORED) as book:
        for name, data in entries.items():
            book.writestr(name, data)
    actions = ["1800:TAP:240,350", "3500:SWIPE:380,350,100,350"]
    if scenario != "baseline":
        actions += ["5000:TAP:240,350", "7000:TAP:240,750"]
        if scenario == "settings":
            actions += ["8500:CONFIRM:100", "10000:TAP:300,340", "12500:TAP:200,405",
                        "13500:DOWN:100", "14200:DOWN:100", "14900:DOWN:100",
                        "16000:CONFIRM:100", "17100:RIGHT:100", "18000:CONFIRM:100", "20000:BACK:100"]
        else:
            actions += ["8500:DOWN:100", "9200:DOWN:100", "10000:CONFIRM:100", "12000:DOWN:100",
                        "12700:DOWN:100", "13400:DOWN:100", "14200:CONFIRM:100", "15500:DOWN:100",
                        "16200:RIGHT:100", "17500:CONFIRM:100"]
            if scenario == "cancel":
                actions += ["19000:CONFIRM:100", "20000:CONFIRM:100", "21000:RIGHT:100", "22000:BACK:100"]
            elif scenario == "reset":
                actions += ["19000:CONFIRM:100", "20000:UP:100", "20700:UP:100", "21400:UP:100",
                            "22400:CONFIRM:100"]
    actions += ["25000:HOME:100", "28000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"4500:{output}/before.bmp;16500:{output}/slider.bmp;23500:{output}/after.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    settings = json.loads((sd / ".crosspoint/settings.json").read_text(encoding="utf-8"))
    expected = {"baseline": 0, "custom": 111, "cancel": 111, "reset": 0, "settings": 101}[scenario]
    assert settings.get("lineHeightPercent", 0) == expected, settings.get("lineHeightPercent")
    assert settings.get("lineSpacing", 1) == 1
    cache, = (sd / ".crosspoint").glob("epub_*")
    position = progress_record((cache / "progress.bin").read_bytes())
    assert position["spine"] == 0
    if scenario != "reset":
        assert position["page"] > 0
    offsets = crosspoint48_page_offsets((cache / "sections/0.bin").read_bytes())
    report = {"passed": True, "scenario": scenario, "line_height_percent": expected,
              "position": position, "page_offsets": offsets, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items() if key != "page_offsets"}))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--scenario", required=True, choices=("baseline", "custom", "cancel", "reset", "settings"))
    a = p.parse_args()
    run(a.program, a.output, a.scenario)
