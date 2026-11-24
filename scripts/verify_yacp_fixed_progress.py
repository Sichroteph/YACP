"""Verify TXT/XTC deferred progress through real reader activities on X3."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import time
from verify_yacp_simulator_stats import create_fixture

def xtc_fixture(path):
    count, width, height = 4, 64, 64
    data_offset = 56 + count * 16
    header = struct.pack("<IBBHBBBBIQQQQII", 0x00435458, 1, 0, count, 0, 0, 0, 0, 1, 0, 56, data_offset, 0, 0, 0)
    pages = []
    table = bytearray()
    for i in range(count):
        pixels = bytes([255 ^ ((1 << (i + 1)) - 1)]) * (width * height // 8)
        page = struct.pack("<IHHBBIQ", 0x00475458, width, height, 0, 0, len(pixels), 0) + pixels
        table.extend(struct.pack("<QIHH", data_offset, len(page), width, height))
        data_offset += len(page)
        pages.append(page)
    path.write_bytes(header + table + b"".join(pages))

def run_one(program, output, kind):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    path = sd / ("checkpoint." + kind)
    if kind == "txt":
        path.write_text("".join(f"Line {i:03d}. Reader progress checkpoint test.\n" for i in range(200)), encoding="utf-8")
    else:
        xtc_fixture(path)
    root = sd / ".crosspoint"
    (root / "recent.json").write_text(json.dumps({"books":[{"path":"/"+path.name,"title":"Checkpoint "+kind.upper(),"author":"YACP QA","coverBmpPath":""}]}), encoding="utf-8")
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_INPUT_SCRIPT="2000:CONFIRM:100;6000:DOWN:100;8000:DOWN:100;10000:UP:100;13000:BACK:100;16000:QUIT",
        CROSSPOINT_SIM_SCREENSHOTS=f"11500:{output}/before-exit.bmp;15000:{output}/home.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        proc = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
        time.sleep(12)
        files = list(root.glob("*/progress.bin"))
        assert len(files) == 1 and files[0].read_bytes() == bytes(6 if kind == 'txt' else 4), files
        proc.wait(timeout=15)
        assert proc.returncode == 0
    saved = files[0].read_bytes()
    assert int.from_bytes(saved[:2] if kind == 'txt' else saved,'little') == 1
    if kind == 'txt':
        assert len(saved)==6 and int.from_bytes(saved[2:],'little')>0
    log = (output / "run.log").read_text(encoding="utf-8")
    assert log.count("Checkpoint saved:") == 2, log
    env["CROSSPOINT_SIM_INPUT_SCRIPT"] = "2000:CONFIRM:100;6000:QUIT"
    env["CROSSPOINT_SIM_SCREENSHOTS"] = f"5000:{output}/reopened.bmp"
    with (output / "reopen.log").open("x", encoding="utf-8") as log:
        proc = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=15)
    assert proc.returncode == 0 and files[0].read_bytes() == saved
    if kind == 'txt':
        settings=json.loads((root/'settings.json').read_text())
        settings['fontSize']=10
        (root/'settings.json').write_text(json.dumps(settings))
        env['CROSSPOINT_SIM_SCREENSHOTS']=f'5000:{output}/repaginated.bmp'
        with (output/'reflow.log').open('x') as log:
            subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
        current=files[0].read_bytes()
        page,offset=struct.unpack('<HI',current)
        index=(files[0].parent/'index.bin').read_bytes()
        count=struct.unpack_from('<I',index,26)[0]
        offsets=struct.unpack_from(f'<{count}I',index,30)
        anchor=struct.unpack('<HI',saved)[1]
        assert offset==offsets[page] and offset<=anchor and (page+1==count or anchor<offsets[page+1])
        # Old CrossPoint page-only records remain readable on an unchanged index.
        files[0].write_bytes(struct.pack('<HH',page,0))
        with (output/'legacy.log').open('x') as log:
            subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=15)
        assert files[0].read_bytes()==current
    (output / "result.json").write_text(json.dumps({"passed": True,"format":kind,"saved_page":1,"progress_writes":2,"hardware_validated":False}), encoding="utf-8")
    print(f"{kind}: page 1 restored, two checkpoints for first display / exit")

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("program",type=Path)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    for kind in ("txt", "xtc"):
        run_one(a.program.resolve(strict=True),a.output.absolute()/kind,kind)
