"""Verify custom cadence through the Pro's existing toolbar, touch slider and Home."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_auto_interval import create_interval_fixture
from yacp_book_profile import decode


def run_one(program, root, cancel):
    output = root / ('cancel' if cancel else 'save')
    output.mkdir(parents=True, exist_ok=False)
    sd = create_interval_fixture(output)
    path = sd / '.crosspoint/settings.json'
    values = json.loads(path.read_text())
    values.update(autoPageTurnSeconds=30, trackReadingStats=0)
    path.write_text(json.dumps(values))
    # Coordinates were checked on the English Pro 208 toolbar and slider captures.
    actions = ['1800:TAP:240,350', '5000:HOME:1200', '7500:TAP:388,750',
               '9200:SWIPE:240,650,240,400', '11000:TAP:240,460',
               '14000:TAP:240,590', '16500:TAP:81,430']
    if cancel:
        actions += ['19000:TAP:240,180', '20500:TAP:240,180', '21800:TAP:240,180']
    else:
        actions += ['19000:TAP:285,495', '25500:TAP:240,350']
    actions += ['28500:HOME:100', '31500:TAP:240,350', '40000:HOME:100', '42500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'15000:{output}/slider.bmp;17900:{output}/value.bmp;23000:{output}/before.bmp;24900:{output}/after.bmp;39000:{output}/reopened.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=55)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    assert trace.count('Entering activity: AutoTurnInterval') == 1
    assert trace.count('Entering activity: EpubReader\n') == 2
    assert 'Entering activity: EpubReaderMenu\n' not in trace
    saved = json.loads(path.read_text())
    assert saved['autoPageTurnSeconds'] == 30 and saved['fontSize'] == 12
    files = list((sd / '.crosspoint/book_profiles').glob('epub_*/reader_profile.*'))
    if cancel:
        assert not files
    else:
        assert len(files) == 1
        profile = decode(files[0].read_bytes())
        assert profile['flags'] == 2 and profile['settings']['autoPageTurnSeconds'] == 5
    positions = list((sd / '.crosspoint').glob('epub_*/progress.bin'))
    assert len(positions) == 1
    spine = int.from_bytes(positions[0].read_bytes()[:2], 'little')
    assert spine == (0 if cancel else 1), spine
    for file in output.glob('*.bmp'):
        with Image.open(file) as image:
            image.save(file.with_suffix('.png'))
    with Image.open(output / 'before.bmp') as before, Image.open(output / 'after.bmp') as after:
        assert (before.tobytes() != after.tobytes()) == (not cancel)
    report = {'passed': True, 'cancel': cancel, 'spine': spine, 'global_seconds': 30,
              'home_returns_home': trace.count('Entering activity: YacpHome') == 3,
              'hardware_validated': False}
    assert report['home_returns_home']
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=2) as pool:
        print(json.dumps(list(pool.map(lambda cancel: run_one(a.program.resolve(strict=True), a.output.absolute(), cancel), (False, True)))))
