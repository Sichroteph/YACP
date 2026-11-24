"""Edit and cancel a server's filename override in the CrossPoint settings UI."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, cancel):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    store = sd / '.crosspoint/opds.json'
    original = {'servers': [{'name': 'QA catalogue', 'url': 'https://example.invalid/opds',
                            'username': '', 'password_obf': '', 'filenameFormat': 0}]}
    store.write_text(json.dumps(original))
    actions = ['1800:BACK:100'] + [f'{2600 + i * 500}:DOWN:100' for i in range(4)]
    actions += ['5100:CONFIRM:100', '6000:CONFIRM:100', '6800:CONFIRM:100', '7600:CONFIRM:100']
    actions += [f'{8400 + i * 450}:UP:100' for i in range(10)]
    actions += ['13300:CONFIRM:100', '14500:CONFIRM:100']
    actions += [f'{15600 + i * 600}:DOWN:100' for i in range(4)]
    actions += ['18800:CONFIRM:100', '20100:DOWN:100',
                '21500:BACK:100' if cancel else '21500:CONFIRM:100', '23500:BACK:100', '25500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'18300:{output}/editor.bmp;20800:{output}/picker.bmp;22800:{output}/saved.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=38)
    assert result.returncode == 0
    assert 'Entering activity: OpdsSettings' in (output / 'run.log').read_text()
    saved = json.loads(store.read_text())
    assert len(saved['servers']) == 1
    assert saved['servers'][0]['filenameFormat'] == (0 if cancel else 1), saved
    assert saved['servers'][0]['url'] == original['servers'][0]['url']
    for path in output.glob('*.bmp'):
        with Image.open(path) as image:
            image.save(path.with_suffix('.png'))
    return {'cancel': cancel, 'passed': True}


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda cancel: scenario(program, output / ('cancel' if cancel else 'save'), cancel), (False, True)))
    report = {'passed': True, 'cases': results, 'external_opds_contacted': False, 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
