"""Toggle Publisher Pages through CrossPoint's Pro text controls, then reopen the book."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def run(program, output):
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(publisherPageNumbers=0, trackReadingStats=0)
    settings.write_text(json.dumps(values))
    actions = ['1800:TAP:240,350', '5000:TAP:240,350', '7000:TAP:240,750', '8500:CONFIRM:100',
               '10000:TAP:410,340']
    actions += [f'{11500+i*600}:DOWN:100' for i in range(7)]
    actions += ['16100:CONFIRM:100', '18000:BACK:100', '20000:TAP:240,200', '21000:TAP:240,200',
                '24000:HOME:100', '26500:TAP:240,350', '31000:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'17300:{output}/settings.bmp;29000:{output}/reopened.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program.resolve(strict=True))], cwd=output, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=42)
    assert result.returncode == 0
    assert json.loads(settings.read_text())['publisherPageNumbers'] == 0
    profiles = list((sd / '.crosspoint/book_profiles').glob('epub_*/reader_profile.*'))
    assert len(profiles) == 1
    profile = decode(profiles[0].read_bytes())
    assert profile['flags'] == 1 and profile['settings']['publisherPageNumbers'] == 1
    cache, = (sd / '.crosspoint').glob('epub_*')
    section = (cache / 'sections/0.bin').read_bytes()
    assert section[0] in (51, 231) and section[23] == 1
    assert (output / 'run.log').read_text().count('Opened profile:') == 2
    from PIL import Image
    with Image.open(output / 'settings.bmp') as image:
        image.save(output / 'settings.png')
    report = dict(passed=True, book_publisher=1, global_publisher=0, reopened=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
