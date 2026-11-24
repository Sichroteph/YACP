"""Toggle recent list/grid through the actual settings screen on synthetic cards."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, initial):
    output.mkdir()
    sd = create_fixture(output)
    path = sd / '.crosspoint/settings.json'
    values = json.loads(path.read_text())
    values.update(recentBooksView=initial, trackReadingStats=0)
    path.write_text(json.dumps(values))
    actions = ['1800:BACK:100'] + [f'{2600+i*500}:DOWN:100' for i in range(4)]
    actions += ['5100:CONFIRM:100'] + [f'{6200+i*500}:DOWN:100' for i in range(8)]
    actions += ['11200:CONFIRM:100', '13500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'10700:{output}/before.bmp;12500:{output}/after.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0
    assert json.loads(path.read_text())['recentBooksView'] == 1 - initial
    for path in output.glob('*.bmp'):
        with Image.open(path) as frame: frame.save(path.with_suffix('.png'))
    return dict(initial=initial, saved=1-initial, passed=True)


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(scenario, program, output / str(value), value) for value in (0, 1)]
        cases = [future.result() for future in futures]
    report = dict(passed=True, cases=cases, visual_review_required=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    run(a.program, a.output)
