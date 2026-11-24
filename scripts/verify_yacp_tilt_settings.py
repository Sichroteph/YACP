"""Check tilt choices and saved disabled direction through the X3 settings UI.

The simulator does not generate physical gyro samples. Those are covered by
verify_yacp_tilt_host.py and still require a hardware acceptance run.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run_one(program, root, case):
    name, mode, remembered, expected_mode, expected_remembered = case
    output = root / name
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(tiltPageTurn=mode, tiltPageTurnLastMode=remembered)
    settings.write_text(json.dumps(values))
    actions = ['1800:BACK:100'] + [f'{2600+i*500}:DOWN:100' for i in range(4)]
    actions += ['5100:CONFIRM:100']
    if name == 'choose-disable':
        actions += ['6200:CONFIRM:100', '7000:CONFIRM:100']
        actions += [f'{7800+i*350}:DOWN:100' for i in range(11)]
        actions += ['12400:CONFIRM:100', '13700:DOWN:100', '14200:DOWN:100', '14700:DOWN:100',
                    '15500:CONFIRM:100', '16700:CONFIRM:100', '17500:DOWN:100', '18300:CONFIRM:100',
                    '19500:CONFIRM:100', '20300:DOWN:100', '21100:CONFIRM:100',
                    '22800:BACK:100', '23700:BACK:100', '26000:QUIT']
        shots = f'13100:{output}/choices.bmp;16300:{output}/forward.bmp;22100:{output}/disabled.bmp'
    else:
        actions += ['6600:BACK:100', '8000:QUIT']
        shots = ''
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions), CROSSPOINT_SIM_SCREENSHOTS=shots)
    with (output / 'run.log').open('x') as log:
        proc = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=40)
    assert proc.returncode == 0, name
    assert 'Entering activity: Settings' in (output / 'run.log').read_text(), name
    saved = json.loads(settings.read_text())
    assert (saved['tiltPageTurn'], saved['tiltPageTurnLastMode']) == (expected_mode, expected_remembered), (name, saved['tiltPageTurn'], saved['tiltPageTurnLastMode'])
    for path in output.glob('*.bmp'):
        with Image.open(path) as image:
            image.save(path.with_suffix('.png'))
    return {'scenario': name, 'mode': saved['tiltPageTurn'], 'remembered_mode': saved['tiltPageTurnLastMode'], 'passed': True}


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = [('choose-disable', 0, 1, 0, 4), ('off-keep', 0, 3, 0, 3),
             ('normal', 1, 4, 1, 1), ('inverted', 2, 1, 2, 2),
             ('forward', 3, 1, 3, 3), ('backward', 4, 1, 4, 4),
             ('invalid', 256, -1, 0, 1)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        reports = list(pool.map(lambda case: run_one(program, output, case), cases))
    result = {'scenarios': reports, 'passed': True, 'hardware_validated': False, 'gyro_simulated': False}
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    run(args.program, args.output)
