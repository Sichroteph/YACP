"""Verify C3 physical navigation under each retained YACP orientation policy."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def run_one(program, root, case):
    name, orientation, front, side, next_key, prev_key, back_key = case
    output=root/name
    output.mkdir(parents=True,exist_ok=False)
    sd=create_fixture(output)
    settings=sd/'.crosspoint/settings.json'
    values=json.loads(settings.read_text())
    values.update(trackReadingStats=0,orientation=orientation,frontButtonOrientationAware=front,
                  sideButtonOrientationAware=side,frontButtonFollowOrientation=1,
                  longPressButtonBehavior=0,sideButtonLongPress=2)
    if name=='hold-side':
        values.update(sideButtonLongPress=3,frontButtonFollowOrientation=0)
    if name=='hold-front':
        values.update(longPressButtonBehavior=2,frontButtonFollowOrientation=0)
    if name=='custom-inverted':
        values.update(readerFrontButtonsEnabled=1,readerFrontButtonBack=3,readerFrontButtonConfirm=2,
                      readerFrontButtonLeft=1,readerFrontButtonRight=0)
    settings.write_text(json.dumps(values))
    if name.startswith('hold-'):
        key='UP' if name=='hold-side' else 'BACK'
        expected_orientation=3 if name=='hold-side' else 1
        actions=f'1800:CONFIRM:100;6000:{key}:2000;10000:{next_key}:100;13000:{prev_key}:100;16000:{back_key}:100;19000:QUIT'
        shots=f'7600:{output}/held.bmp;9400:{output}/released.bmp;12000:{output}/next.bmp;15000:{output}/previous.bmp'
    else:
        expected_orientation=orientation
        actions=f'1800:CONFIRM:100;6000:{next_key}:100;10000:{prev_key}:100;14000:{back_key}:100;17000:QUIT'
        shots=f'8500:{output}/next.bmp;12000:{output}/previous.bmp;16000:{output}/home.bmp'
    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',CROSSPOINT_SIM_SD=str(sd),
             CROSSPOINT_SIM_INPUT_SCRIPT=actions,CROSSPOINT_SIM_SCREENSHOTS=shots)
    with (output/'run.log').open('x') as log:
        proc=subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=30)
    assert proc.returncode==0,name
    logs=(output/'run.log').read_text()
    assert logs.count('Entering activity: EpubReader\n')==1,name
    assert logs.count('Entering activity: YacpHome')==2,name
    # Each chapter is a single page: this independently proves both directions.
    with Image.open(output/'next.bmp') as next_image, Image.open(output/'previous.bmp') as previous_image:
        assert next_image.tobytes()!=previous_image.tobytes(),(name,'next chapter was not reached')
    progress=list((sd/'.crosspoint').glob('epub_*/progress.bin'))
    assert len(progress)==1 and progress[0].read_bytes()[:4]==bytes(4),name
    assert json.loads(settings.read_text())['orientation']==orientation,name
    if name.startswith('hold-'):
        profiles=list((sd/'.crosspoint/book_profiles').glob('epub_*/reader_profile.*'))
        assert len(profiles)==1,name
        assert decode(profiles[0].read_bytes())['settings']['orientation']==expected_orientation,name
    for p in output.glob('*.bmp'):
        with Image.open(p) as im: im.save(p.with_suffix('.png'))
    if name.startswith('hold-'):
        with Image.open(output/'held.bmp') as a,Image.open(output/'released.bmp') as b:
            assert a.tobytes()==b.tobytes(),name
    report={'passed':True,'case':name,'orientation':expected_orientation,'hardware_validated':False}
    (output/'result.json').write_text(json.dumps(report,indent=2))
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('program',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--case',nargs='*')
    a=p.parse_args()
    cases=[]
    for orientation in (1,2,3):
        cases += [(f'front-off-{orientation}',orientation,0,0,'RIGHT','LEFT','BACK'),
                  (f'front-nav-{orientation}',orientation,1,0,'LEFT','RIGHT','BACK'),
                  (f'front-all-{orientation}',orientation,2,0,'BACK' if orientation==2 else 'LEFT','CONFIRM' if orientation==2 else 'RIGHT','RIGHT' if orientation==2 else 'BACK'),
                  (f'side-off-{orientation}',orientation,0,0,'DOWN','UP','BACK'),
                  (f'side-on-{orientation}',orientation,0,1,'UP','DOWN','BACK'),
                  (f'default-{orientation}',orientation,3,2,'DOWN' if orientation==1 else 'UP','UP' if orientation==1 else 'DOWN','BACK')]
        cases += [(f'default-front-{orientation}',orientation,3,2,'RIGHT' if orientation==1 else 'LEFT',
                   'LEFT' if orientation==1 else 'RIGHT','BACK')]
    cases += [('hold-side',0,0,1,'UP','DOWN','BACK'),('hold-front',2,2,0,'LEFT','RIGHT','BACK'),
              ('custom-inverted',2,2,0,'RIGHT','LEFT','BACK')]
    if a.case: cases=[case for case in cases if case[0] in a.case]
    with ThreadPoolExecutor(max_workers=4) as pool:
        print(json.dumps(list(pool.map(lambda case:run_one(a.program.resolve(strict=True),a.output.absolute(),case),cases))))
