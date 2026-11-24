"""Verify the final-page prompt, cancel/confirm and one-time achievement on Pro."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import book_stats, global_stats

def run(program, output, confirm):
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    actions = "2000:TAP:240,350;9000:SWIPE:380,350,100,350;14000:HOME:100;"
    if confirm:
        actions += "17000:DOWN:100;19000:CONFIRM:100;24500:TAP:45,766;28000:QUIT"
    else:
        actions += "17000:BACK:100;20000:TAP:240,350;25000:HOME:100;29000:QUIT"
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_INPUT_SCRIPT=actions,
        CROSSPOINT_SIM_SCREENSHOTS=f"15500:{output}/prompt.bmp;22000:{output}/result.bmp;27000:{output}/home.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        proc = subprocess.run([str(program.resolve(strict=True))], cwd=output, env=env,
            stdout=log, stderr=subprocess.STDOUT, timeout=40)
    assert proc.returncode == 0
    root = sd / ".crosspoint"
    book = book_stats(next(root.glob("epub_*/stats_v6.bin")).read_bytes())
    totals = global_stats((root / "global_stats.bin").read_bytes())
    log = (output / "run.log").read_text(encoding="utf-8")
    assert log.count("Entering activity: Confirmation") == 1
    assert book["completed"] == confirm and totals["completed_books"] == int(confirm)
    assert not book["flags"] & 4
    if confirm:
        assert "Entering activity: BookStats" in log
    else:
        assert book["flags"] & 8 and "Entering activity: BookStats" not in log
    assert log.count("Entering activity: YacpHome") >= 2
    (output / "result.json").write_text(json.dumps({"passed": True,"confirm":confirm,
        "book":book,"global":totals,"hardware_validated":False},indent=2),encoding="utf-8")
    print(json.dumps({"passed":True,"confirm":confirm,"completed_books":totals["completed_books"],"seconds":book["seconds"]}))

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("program",type=Path)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--confirm",action="store_true")
    a=p.parse_args()
    run(a.program,a.output,a.confirm)
