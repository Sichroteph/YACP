"""Exercise X3 periodic reinforcement and explicit cleanup on isolated cards."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image, ImageChops
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, board, aa, action, kind, power_saving=False):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    paragraph = 'Reader refresh verification keeps the same text, justification and position. ' * 12
    if kind == 'epub':
        with ZipFile(sd / 'completion.epub') as source:
            entries = {name: source.read(name) for name in source.namelist()}
        for name in ('first.xhtml', 'last.xhtml'):
            entries[name] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body>' +
                             ('<p>' + paragraph + '</p>') * 15 + '</body></html>').encode()
        with ZipFile(sd / 'completion.epub', 'w', compression=ZIP_STORED) as target:
            for name, contents in entries.items():
                target.writestr(name, contents)
    else:
        (sd / 'refresh.txt').write_text((paragraph + '\n\n') * 25)
        (sd / '.crosspoint/recent.json').write_text(json.dumps({'books': [{
            'path': '/refresh.txt', 'title': 'Refresh verification', 'author': 'YACP QA', 'coverBmpPath': ''}]}))
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(refreshFrequency=4 if board == 'x3' and action else 0, refreshAction=action, shortPwrBtn=3, textAntiAliasing=aa,
                  trackReadingStats=0, clockEnabled=0, clockDateHasBeenSynced=0, fadingFix=int(power_saving))
    settings.write_text(json.dumps(values))
    enter = 'TAP:240,350' if board == 'pro' else 'CONFIRM:100'
    turn = 'SWIPE:380,350,100,350' if board == 'pro' else 'DOWN:100'
    leave = 'HOME:100' if board == 'pro' else 'BACK:100'
    actions = f'1800:{enter};7000:{turn};10000:{turn};13000:POWER:100;16000:{turn};19000:{leave};21000:QUIT'
    captures = ';'.join(f'{at}:{output}/{name}.bmp' for at, name in ((11500, 'before'), (14500, 'manual'), (18000, 'next')))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=actions, CROSSPOINT_SIM_SCREENSHOTS=captures)
    with (output / 'run.log').open('x') as log:
        process = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=35)
    assert process.returncode == 0
    log = (output / 'run.log').read_text()
    periodic = log.count('Periodic X3 reinforcement')
    assert (periodic >= 3) if board == 'x3' and action else periodic == 0, (output, periodic)
    double_passes = log.count('Magic Flash passes=2')
    assert (double_passes >= 3) if board == 'x3' and action else double_passes == 0, (output, double_passes)
    if board == 'x3' and action:
        assert 'Magic Flash passes=2 grayscale=1' not in log, 'Magic Flash must not add text grays'
    assert log.count('Reader cleanup (remaining=0)') >= 2, 'Initial and explicitly requested cleanup required'
    with Image.open(output / 'before.bmp') as before, Image.open(output / 'manual.bmp') as after:
        assert ImageChops.difference(before.convert('RGB'), after.convert('RGB')).getbbox() is None
    saved = json.loads(settings.read_text())
    assert saved['refreshAction'] == action
    assert saved['textAntiAliasing'] == aa, 'Classic smoothing preference was overwritten'
    assert saved['refreshFrequency'] == (4 if board == 'x3' and action else 0), 'Classic cadence was overwritten'
    for path in output.glob('*.bmp'):
        with Image.open(path) as picture:
            picture.save(path.with_suffix('.png'))
    return {'kind': kind, 'antialiasing': aa, 'action': action, 'reinforcements': periodic,
            'double_passes': double_passes,
            'explicit_cleanup_unchanged_pixels': True, 'passed': True}


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = [(aa, action, kind) for kind in ('epub', 'txt') for aa in (0, 1) for action in (0, 1, 2)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(lambda c: scenario(program, output / f'{c[2]}-{c[0]}-{c[1]}', board, *c), cases))
    for kind in ('epub', 'txt'):
        for aa in (0, 1):
            for action in (1, 2):
                for name in ('before', 'manual', 'next'):
                    reference_aa = 0 if board == 'x3' else aa
                    with Image.open(output / f'{kind}-{reference_aa}-0/{name}.bmp') as first, Image.open(output / f'{kind}-{aa}-{action}/{name}.bmp') as second:
                        assert ImageChops.difference(first.convert('RGB'), second.convert('RGB')).getbbox() is None
    result = {'passed': True, 'board': board, 'cases': results, 'magic_uses_black_white_text': board == 'x3',
              'classic_preferences_preserved': True,
              'physical_waveforms_validated': False}
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--board', choices=('x3', 'pro'), required=True)
    a = p.parse_args()
    run(a.program, a.output, a.board)
