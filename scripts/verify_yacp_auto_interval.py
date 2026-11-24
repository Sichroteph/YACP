"""Exercise the custom automatic-turn slider and per-book persistence on X3."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def create_interval_fixture(output):
    sd = create_fixture(output)
    # A third chapter keeps the first automatic turn away from completion dialogs.
    with ZipFile(sd / 'completion.epub') as source, ZipFile(sd / 'interval.epub', 'w', compression=ZIP_STORED) as target:
        for name in source.namelist():
            data = source.read(name)
            if name == 'book.opf':
                data = data.replace(b'</manifest>', b'<item id="extra" href="extra.xhtml" media-type="application/xhtml+xml"/></manifest>')
                data = data.replace(b'</spine>', b'<itemref idref="extra"/></spine>')
            target.writestr(name, data)
        target.writestr('extra.xhtml', source.read('first.xhtml'))
    recent = sd / '.crosspoint/recent.json'
    books = json.loads(recent.read_text())
    books['books'][0]['path'] = '/interval.epub'
    recent.write_text(json.dumps(books))
    return sd


def run_one(program, root, mode):
    output = root / mode
    output.mkdir(parents=True, exist_ok=False)
    sd = create_interval_fixture(output)
    settings_path = sd / '.crosspoint/settings.json'
    settings = json.loads(settings_path.read_text())
    settings.update(trackReadingStats=0, autoPageTurnSeconds=5 if mode == 'same' else 30)
    settings_path.write_text(json.dumps(settings))
    actions = ['1800:CONFIRM:100', '5000:CONFIRM:100']
    actions += [f'{6500+i*500}:DOWN:100' for i in range(6)]
    actions += ['9800:CONFIRM:100']
    actions += [f'{11000+i*500}:DOWN:100' for i in range(5)]
    actions += ['14000:CONFIRM:100']
    if mode != 'same':
        actions += [f'{16000+i*600}:UP:100' for i in range(5)]
    actions += ['19300:BACK:100' if mode == 'cancel' else '19300:CONFIRM:100']
    actions += ['20500:BACK:100']
    if mode != 'cancel':
        actions += ['27400:BACK:100']  # CrossPoint consumes the first key to stop auto-turn.
    actions += ['28500:BACK:100', '31500:CONFIRM:100', '40000:BACK:100', '42500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'15000:{output}/slider.bmp;18900:{output}/value.bmp;21500:{output}/before.bmp;26700:{output}/after.bmp;39000:{output}/reopened.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=55)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    assert 'Entering activity: AutoTurnInterval' in trace
    assert trace.count('Entering activity: EpubReader\n') == 2
    saved = json.loads(settings_path.read_text())
    assert saved['fontSize'] == 12 and saved['autoPageTurnSeconds'] == settings['autoPageTurnSeconds']
    profiles = list((sd / '.crosspoint/book_profiles').glob('epub_*/reader_profile.*'))
    if mode == 'cancel':
        assert not profiles
    else:
        assert len(profiles) == 1
        profile = decode(profiles[0].read_bytes())
        assert profile['flags'] == 2 and profile['settings']['autoPageTurnSeconds'] == 5
    progress = list((sd / '.crosspoint').glob('epub_*/progress.bin'))
    assert len(progress) == 1
    spine = int.from_bytes(progress[0].read_bytes()[:2], 'little')
    assert spine == (0 if mode == 'cancel' else 1), spine
    # Reopening never activates the timer; a second timer would reach book end.
    assert 'End of book' not in trace
    for path in output.glob('*.bmp'):
        with Image.open(path) as image:
            image.save(path.with_suffix('.png'))
    with Image.open(output / 'before.bmp') as before, Image.open(output / 'after.bmp') as after:
        assert (before.tobytes() != after.tobytes()) == (mode != 'cancel')
    report = {'passed': True, 'mode': mode, 'profile_count': len(profiles), 'spine': spine,
              'global_seconds': saved['autoPageTurnSeconds'], 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mode', nargs='+', default=['save', 'cancel', 'same'])
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        print(json.dumps(list(pool.map(lambda mode: run_one(a.program.resolve(strict=True), a.output.absolute(), mode), a.mode))))
