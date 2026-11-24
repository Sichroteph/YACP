"""Check the cache-maintenance confirmation and cancellation on a synthetic card."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, output, cancellation='back'):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    cache = sd / '.crosspoint/epub_7'
    cache.mkdir()
    for name in ('progress.bin', 'stats_v6.bin', 'stats_v99.bin', 'book.bin'):
        (cache / name).write_bytes(b'QA preserved ' + name.encode())
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in cache.iterdir()}
    actions = ['1800:BACK:100'] + [f'{2600+i*500}:DOWN:100' for i in range(4)]
    actions += ['5100:CONFIRM:100', '6000:CONFIRM:100', '6800:CONFIRM:100', '7600:CONFIRM:100']
    actions += [f'{8600+i*500}:UP:150' for i in range(9)]
    cancel = {'back': 'BACK:250', 'select': 'CONFIRM:250', 'outside': 'TAP:5,5,200'}[cancellation]
    actions += ['13900:CONFIRM:150', f'17000:{cancel}', '19500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'13300:{output}/setting.bmp;15400:{output}/warning.bmp;18500:{output}/returned.bmp')
    (output / 'input-script.txt').write_text(';'.join(actions))
    with (output / 'run.log').open('x') as log:
        subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=35, check=True)
    trace = (output / 'run.log').read_text()
    assert 'Entering activity: ClearCache' in trace
    assert 'Exiting activity: ClearCache' in trace
    assert 'User confirmed, starting cache clear' not in trace
    assert before == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in cache.iterdir()}
    for path in output.glob('*.bmp'):
        with Image.open(path) as frame: frame.save(path.with_suffix('.png'))
    report = dict(passed=True, cancellation=cancellation, cancellation_preserves_all_entries=True, visual_review_required=True,
                  hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--cancellation', choices=('back', 'select', 'outside'), default='back')
    a = p.parse_args()
    run(a.program, a.output, a.cancellation)
