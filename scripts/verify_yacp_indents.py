"""Render paragraph-indent edge cases on an isolated synthetic Pro SD."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED

from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import crosspoint48_page_offsets


def run(program, output, enabled, toggle):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    book_path = sd / "completion.epub"
    with ZipFile(book_path) as book:
        entries = {name: book.read(name) for name in book.namelist()}
    entries["first.xhtml"] = b'''<html xmlns="http://www.w3.org/1999/xhtml"><body>
<p>Plain paragraph.</p><p style="text-indent:0">Zero paragraph.</p>
<p style="text-indent:2em">Positive paragraph.</p>
<p style="text-indent:-0.5em;margin-left:1em">Hanging paragraph.</p>
<p style="text-align:center">Centered paragraph.</p>
<p style="text-align:right">Right paragraph.</p>
<ul><li>List paragraph.</li></ul></body></html>'''
    with ZipFile(book_path, "w", compression=ZIP_STORED) as book:
        for name, data in entries.items():
            book.writestr(name, data)
    settings_path = sd / ".crosspoint/settings.json"
    values = json.loads(settings_path.read_text(encoding="utf-8"))
    values.update(forceParagraphIndents=int(enabled), paragraphAlignment=4)
    settings_path.write_text(json.dumps(values), encoding="utf-8")
    actions = ["1800:TAP:240,350"]
    if toggle:
        actions += ["5000:TAP:240,350", "7000:TAP:240,750", "8500:CONFIRM:100", "10000:TAP:300,340"]
        actions += [f"{11500+i*600}:DOWN:100" for i in range(7)]
        actions += ["16500:CONFIRM:100", "18500:BACK:100", "21000:TAP:240,200", "22000:TAP:240,200"]
    actions += ["25000:HOME:100", "27500:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"4000:{output}/before.bmp;17500:{output}/settings.bmp;23500:{output}/after.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    if toggle:
        assert json.loads(settings_path.read_text(encoding="utf-8"))["forceParagraphIndents"] == int(not enabled)
    cache, = (sd / ".crosspoint").glob("epub_*")
    section = (cache / "sections/0.bin").read_bytes()
    crosspoint48_page_offsets(section)
    if section[0] in (49, 233):
        assert section[21] == (int(not enabled) if toggle else int(enabled))
    report = {"passed": True, "initial_indents": enabled, "toggled_in_ui": toggle,
              "section_version": section[0], "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--enabled", action="store_true")
    p.add_argument("--toggle", action="store_true")
    a = p.parse_args()
    run(a.program, a.output, a.enabled, a.toggle)
