"""Select Adaptive Grayscale through CrossPoint settings and render a synthetic sleep BMP."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, output, ui):
    program=program.resolve(strict=True);output=output.absolute();output.mkdir(parents=True,exist_ok=False)
    sd=create_fixture(output);settings=sd/".crosspoint/settings.json"
    values=json.loads(settings.read_text());values.update(sleepScreen=2,sleepScreenCoverFilter=0,
        quickResumeSleepScreen=0,autoBackupStats=0,trackReadingStats=0)
    settings.write_text(json.dumps(values),encoding="utf-8")
    image=Image.new("RGB",(528,792));image.putdata([(v,v,v) for y in range(792)
        for x in range(528) for v in [25 + (x*205//527)]])
    image.save(sd/"sleep.bmp")
    before=hashlib.sha256((sd/"sleep.bmp").read_bytes()).hexdigest()
    if ui:
        actions=["1800:BACK:100"]+[f"{2600+i*600}:DOWN:100" for i in range(4)]
        actions += ["5200:CONFIRM:100","6400:DOWN:100","7200:DOWN:100","7800:DOWN:100",
            "8600:CONFIRM:100","10300:UP:100","11500:CONFIRM:100","13500:BACK:100",
            "15000:BACK:100","17500:CONFIRM:100","23000:POWER:800","29000:QUIT"]
        shots=f"9600:{output}/options.bmp;12600:{output}/selected.bmp;26500:{output}/sleep.bmp"
    else:
        actions=["2000:CONFIRM:100","8500:POWER:800","14500:QUIT"]
        shots=f"12500:{output}/sleep.bmp"
    env=dict(os.environ,SDL_VIDEODRIVER="offscreen",CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),CROSSPOINT_SIM_SCREENSHOTS=shots)
    with (output/"run.log").open("x",encoding="utf-8") as log:
        result=subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=42)
    assert result.returncode==0
    saved=json.loads(settings.read_text());assert saved["sleepScreenCoverFilter"]==(3 if ui else 0),saved["sleepScreenCoverFilter"]
    log=(output/"run.log").read_text();assert "Loading: /sleep.bmp" in log
    assert ("Adaptive tone enabled:" in log)==ui
    assert before==hashlib.sha256((sd/"sleep.bmp").read_bytes()).hexdigest()
    assert (output/"sleep.bmp").exists()
    report={"passed":True,"ui_selection":ui,"source_unchanged":True,"hardware_validated":False}
    (output/"result.json").write_text(json.dumps(report,indent=2),encoding="utf-8");print(json.dumps(report))


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("program",type=Path)
    p.add_argument("--output",type=Path,required=True);p.add_argument("--ui",action="store_true")
    a=p.parse_args();run(a.program,a.output,a.ui)
