"""Capture YACP default sleep branding on an isolated English simulator card."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(sleepScreen=1,quickResumeSleepScreen=0,autoBackupStats=0,trackReadingStats=0)
    settings.write_text(json.dumps(values))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT='6000:POWER:800;12000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'50:{output}/initial.bmp;10000:{output}/sleep.bmp')
    with (output/'run.log').open('x') as log:
        process = subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=20)
    assert process.returncode == 0
    trace = (output/'run.log').read_text()
    assert 'Entering activity: Sleep' in trace
    for image in output.glob('*.bmp'):
        with Image.open(image) as frame:
            frame.save(image.with_suffix('.png'))
    (output/'result.json').write_text(json.dumps({'passed':True,'visual_review_required':True,'hardware_validated':False}))
    print('Sleep branding captured for visual review.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    args = parser.parse_args()
    run(args.program,args.output)
