"""Select or cancel the C3 EPUB commands through CrossPoint's actual settings popup."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, mode):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    field, row, option, expected = {
        'menu': ('longPressMenuAction', 6, 10, 9),
        'back': ('longPressBackAction', 7, 19, 18),
        'power': ('longPwrBtn', 8, 4, 3),
        'short': ('readerShortPowerAction', 9, 11, 10),
        'cancel': ('longPressMenuAction', 6, 10, 22),
    }[mode]
    actions = ['1800:BACK:150'] + [f'{2600+i*500}:DOWN:150' for i in range(4)]
    actions += ['5100:CONFIRM:150', '6200:CONFIRM:150', '7000:CONFIRM:150']
    t = 7800
    for _ in range(row): actions.append(f'{t}:DOWN:150'); t += 400
    shots = [(t, 'controls')]
    t += 600; actions.append(f'{t}:CONFIRM:150'); t += 650
    for _ in range(option): actions.append(f'{t}:DOWN:150'); t += 400
    shots.append((t, 'choice'))
    t += 650; actions.append(f'{t}:'+('BACK' if mode == 'cancel' else 'CONFIRM')+':150')
    t += 700; shots.append((t, 'saved'))
    t += 600; actions.append(f'{t}:BACK:150')
    t += 700; actions.append(f'{t}:BACK:150')
    t += 700; actions.append(f'{t}:QUIT')
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=';'.join(f'{ms}:{output}/{name}.bmp' for ms,name in shots))
    with (output/'run.log').open('x') as log:
        proc = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=40)
    assert proc.returncode == 0, mode
    saved = json.loads((sd/'.crosspoint/settings.json').read_text())
    assert saved[field] == expected, (mode, saved[field], expected)
    assert saved['longPressMenuFunction'] == 1, 'CrossPoint Menu binding changed'
    for _, name in shots:
        with Image.open(output/(name+'.bmp')) as im: im.save(output/(name+'.png'))
    report = dict(passed=True, mode=mode, saved_id=expected, hardware_validated=False)
    (output/'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        result = list(pool.map(lambda mode: scenario(a.program.resolve(strict=True), a.output.absolute()/mode, mode),
                               ['menu', 'back', 'power', 'short', 'cancel']))
    print(json.dumps(result))
