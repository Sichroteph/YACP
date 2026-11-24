"""Verify the first system Back after a held reader Back across front-button profiles."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess

from PIL import Image, ImageChops
from verify_yacp_simulator_stats import create_fixture

KEYS = ('readerFrontButtonBack', 'readerFrontButtonConfirm', 'readerFrontButtonLeft', 'readerFrontButtonRight')
BUTTONS = ('BACK', 'CONFIRM', 'LEFT', 'RIGHT')


def same_image(first, second):
    with Image.open(first) as a, Image.open(second) as b:
        return a.size == b.size and ImageChops.difference(a.convert('RGB'), b.convert('RGB')).getbbox() is None


def scenario(program, output, raw_back, orientation):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    path = sd / '.crosspoint/settings.json'
    settings = json.loads(path.read_text())
    mapping = [raw_back] + [raw for raw in range(4) if raw != raw_back]
    settings.update(readerFrontButtonsEnabled=1, frontButtonOrientationAware=3,
                    frontButtonFollowOrientation=0, orientation=orientation, trackReadingStats=0,
                    yacpReaderFontInitialized=True, **dict(zip(KEYS, mapping)))
    path.write_text(json.dumps(settings))
    # The long hold opens the browser before release. Its release must not
    # dismiss that browser, but the very next system Back must dismiss it.
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=(f'1800:CONFIRM:100;6000:{BUTTONS[raw_back]}:1800;'
                                            '9500:BACK:100;12000:QUIT'),
               CROSSPOINT_SIM_SCREENSHOTS=(f'7300:{output}/browser-held.bmp;'
                                           f'8500:{output}/browser-released.bmp;'
                                           f'11000:{output}/after-first-back.bmp'))
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0, output
    trace = (output / 'run.log').read_text()
    assert trace.count('Entering activity: FileBrowser') == 1, output
    assert trace.count('Entering activity: YacpHome') == 2, 'First system Back did not return Home'
    assert same_image(output / 'browser-held.bmp', output / 'browser-released.bmp'), 'Hold release dismissed browser'
    assert not same_image(output / 'browser-released.bmp', output / 'after-first-back.bmp'), output
    progress = list((sd / '.crosspoint').glob('epub_*/progress.bin'))
    assert len(progress) == 1 and progress[0].read_bytes()[:4] == bytes(4), 'Position changed'
    saved = json.loads(path.read_text())
    assert tuple(saved.get(key, value) for key, value in zip(KEYS, mapping)) == tuple(mapping)
    system = ('frontButtonBack', 'frontButtonConfirm', 'frontButtonLeft', 'frontButtonRight')
    assert tuple(saved.get(key, value) for value, key in enumerate(system)) == (0, 1, 2, 3)
    for path in output.glob('*.bmp'):
        with Image.open(path) as picture:
            picture.save(path.with_suffix('.png'))
    return dict(passed=True, reader_back=BUTTONS[raw_back], orientation=orientation,
                release_stays_in_browser=True, first_back_returns_home=True, hardware_validated=False)


def run(program, output, cases=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = cases or [(raw, orientation) for raw in range(4) for orientation in (0, 2)]
    results = []
    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = {pool.submit(scenario, program, output / f'back-{raw}-orientation-{orientation}', raw, orientation):
                   (raw, orientation) for raw, orientation in cases}
        for future, case in futures.items():
            try:
                results.append(future.result())
            except Exception as error:
                results.append(dict(passed=False, case=case, error=repr(error)))
    report = dict(passed=all(row['passed'] for row in results), cases=results, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not run(args.program, args.output)['passed']:
        raise SystemExit(1)
