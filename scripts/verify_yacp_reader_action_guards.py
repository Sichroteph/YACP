"""Check sleep/wake, chord priority and CrossPoint menu inheritance on X3."""
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
    path = sd / '.crosspoint/settings.json'
    settings = json.loads(path.read_text())
    settings.update(trackReadingStats=0, longPwrBtn=3, shortPwrBtn=2)
    actions = '1800:CONFIRM:150;6500:CONFIRM:1800;11500:QUIT'
    after_wake = ''
    if mode == 'sleep':
        settings.update(longPressMenuAction=1)
        actions = '1800:CONFIRM:150;6500:CONFIRM:1800;12500:POWER:1800'
        after_wake = '6500:QUIT'
    elif mode == 'short-sleep':
        settings.update(shortPwrBtn=1)
        actions = '1800:CONFIRM:150;6500:POWER:150;12500:POWER:1800'
        after_wake = '6500:QUIT'
    elif mode.startswith('chord'):
        down_duration = 1900 if mode == 'chord' else 1500
        actions = f'1800:CONFIRM:150;6500:POWER:1800;6510:DOWN:{down_duration};11500:BACK:150;13500:QUIT'
    elif mode == 'menu-off':
        settings.update(longPressMenuAction=0, longPressMenuFunction=2)
    elif mode == 'menu-upstream':
        settings.update(longPressMenuAction=22, longPressMenuFunction=2)
    else:
        raise ValueError(mode)
    path.write_text(json.dumps(settings))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=actions,
               CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE=after_wake,
               CROSSPOINT_SIM_SCREENSHOTS=f'5300:{output}/before.bmp;10000:{output}/after.bmp',
               CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE=f'5500:{output}/resumed.bmp')
    with (output/'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=30)
    assert result.returncode == 0, mode
    trace = (output/'run.log').read_text()
    if mode in ('sleep', 'short-sleep'):
        assert trace.count('Entering activity: Sleep') == 1, mode
        assert 'Reader action 3' not in trace, 'Wake hold became a refresh'
        assert trace.count('Entering activity: EpubReader') == 2, mode
        state = json.loads((sd/'.crosspoint/state.json').read_text())
        assert state['powerHistory']['sleepSamples'] == 1, mode
        with Image.open(output/'before.bmp') as a, Image.open(output/'resumed.bmp') as b:
            assert a.tobytes() == b.tobytes(), 'Wake changed the page'
    elif mode.startswith('chord'):
        assert 'Reader action ' not in trace, mode
        assert 'Entering activity: Sleep' not in trace, mode
        assert len(list((sd/'screenshots').rglob('*.bmp'))) == 1, mode
        # CrossPoint's screenshot feedback restores its B/W snapshot, not the
        # previous gray planes. Validate position, not anti-aliasing pixels.
        progress = list((sd/'.crosspoint').glob('epub_*/progress.bin'))
        assert len(progress) == 1 and progress[0].read_bytes()[:4] == bytes(4)
        assert trace.count('Rendered page in ') == 1, 'Chord release rerendered or turned a page'
    elif mode == 'menu-off':
        assert trace.count('Entering activity: EpubReaderMenu') == 1
        assert not list((sd/'.crosspoint/bookmarks').glob('*.json'))
    else:
        assert 'Entering activity: EpubReaderMenu' not in trace
        assert len(list((sd/'.crosspoint/bookmarks').glob('*.json'))) == 1
    for path in output.glob('*.bmp'):
        with Image.open(path) as im: im.save(path.with_suffix('.png'))
    report = dict(passed=True, scenario=mode, hardware_validated=False)
    (output/'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cases', nargs='+', default=['sleep', 'short-sleep', 'chord', 'chord-down-first', 'menu-off', 'menu-upstream'])
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        reports = list(pool.map(lambda mode: scenario(a.program.resolve(strict=True), a.output.absolute()/mode, mode), a.cases))
    print(json.dumps(reports))
