"""Compare displayed totals with and without duplicate peer backup/temp files."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import struct
import subprocess
from PIL import Image,ImageChops
from verify_yacp_simulator_stats import create_fixture


def scenario(program,output,board,copies):
    output.mkdir(parents=True,exist_ok=False)
    sd=create_fixture(output)
    root=sd/'.crosspoint'
    def stats(seconds): return bytes([3])+struct.pack('<4I',4,seconds,120,2)+bytes(142)
    (root/'global_stats.bin').write_bytes(stats(3600))
    peers=root/'synced_stats'
    peers.mkdir()
    (peers/'device_020304050607.bin').write_bytes(stats(7200))
    if copies:
        for suffix in ('.part','.previous','.bak','.tmp'):
            (peers/('device_020304050607.bin'+suffix)).write_bytes(stats(7200))
        (peers/'copy.bin').write_bytes(stats(7200))
    before={p:p.read_bytes() for p in root.rglob('*.bin*') if p.is_file()}
    enter='2500:BACK:100' if board=='x3' else '2500:TAP:240,756'
    actions=[enter]+[f'{3500+i*500}:DOWN:100' for i in range(5)]+['6500:CONFIRM:100']
    # Both current simulator profiles expose their clock; three steps reach All Devices.
    actions += ['8500:DOWN:100','9200:DOWN:100','9900:DOWN:100']
    actions += ['13500:BACK:100','15000:QUIT']
    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_RENDER_DRIVER='software',CROSSPOINT_SIM_SD=str(sd),
             CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),CROSSPOINT_SIM_SCREENSHOTS=f'11500:{output}/totals.bmp')
    with (output/'run.log').open('x') as log:
        result=subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=25)
    assert result.returncode==0
    trace=(output/'run.log').read_text()
    assert 'Entering activity: BookStats' in trace
    assert 'Aggregated 1 synced stats file(s)' in trace
    assert all(p.read_bytes()==data for p,data in before.items())
    with Image.open(output/'totals.bmp') as frame: frame.save(output/'totals.png')
    return output/'totals.png'


def run(program,output,board):
    program=program.resolve(strict=True)
    output=output.absolute()
    output.mkdir(parents=True,exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        images=list(pool.map(lambda copies:scenario(program,output/str(copies),board,copies),(False,True)))
    with Image.open(images[0]) as first,Image.open(images[1]) as second:
        assert ImageChops.difference(first.convert('RGB'),second.convert('RGB')).getbbox() is None
    result={'passed':True,'board':board,'duplicate_copies_do_not_change_display':True,
            'originals_unchanged':True,'expected_total_seconds':10800,'hardware_validated':False}
    (output/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('program',type=Path)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--board',choices=('x3','pro'),required=True)
    a=p.parse_args()
    run(a.program,a.output,a.board)
