"""Check the nearby entry and unavailable-radio screen using synthetic simulator data."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program,output,board):
    program=program.resolve(strict=True)
    output=output.absolute()
    output.mkdir(parents=True,exist_ok=False)
    sd=create_fixture(output)
    source=sd/'.crosspoint/global_stats.bin'
    source.write_bytes(bytes([3])+bytes(158))
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    enter='2500:BACK:100' if board=='x3' else '2500:TAP:240,756'
    actions=[enter,'3500:DOWN:100','4000:DOWN:100','4500:DOWN:100','5200:CONFIRM:100']
    if board=='x3':
        actions += ['6500:UP:100','7500:CONFIRM:100','10500:BACK:100']
    else:
        actions += ['6500:UP:100','7500:CONFIRM:100','10500:TAP:100,758']
    actions += ['12500:QUIT']
    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_RENDER_DRIVER='software',CROSSPOINT_SIM_SD=str(sd),
             CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
             CROSSPOINT_SIM_SCREENSHOTS=f'7000:{output}/menu.bmp;9400:{output}/nearby.bmp;11800:{output}/back.bmp')
    with (output/'run.log').open('x') as log:
        result=subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=25)
    assert result.returncode==0
    trace=(output/'run.log').read_text()
    assert 'Entering activity: NearbyStatsSync' in trace
    assert 'Exiting activity: NearbyStatsSync' in trace
    assert trace.rfind('Entering activity: YacpHome') > trace.find('Exiting activity: NearbyStatsSync')
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
    assert not (sd/'.crosspoint/synced_stats').exists()
    for file in output.glob('*.bmp'):
        with Image.open(file) as frame: frame.save(file.with_suffix('.png'))
    report={'passed':True,'board':board,'source_preserved':True,'radio_available':False,'hardware_validated':False}
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('program',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--board',choices=('x3','pro'),required=True)
    a=p.parse_args()
    run(a.program,a.output,a.board)
