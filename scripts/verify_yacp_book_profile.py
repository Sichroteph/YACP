"""Check EPUB book scope through the X3 reader and Home, without touching a device."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    shutil.copyfile(sd / 'completion.epub', sd / 'other.epub')
    root = sd / '.crosspoint'
    settings_path = root / 'settings.json'
    settings = json.loads(settings_path.read_text())
    settings.update(longPressButtonBehavior=3, trackReadingStats=0)
    settings_path.write_text(json.dumps(settings))
    (root / 'recent.json').write_text(json.dumps({'books': [
        {'path': '/completion.epub', 'title': 'Book One', 'author': 'YACP QA', 'coverBmpPath': ''},
        {'path': '/other.epub', 'title': 'Book Two', 'author': 'YACP QA', 'coverBmpPath': ''}]}))
    actions = ['1800:CONFIRM:100', '5000:RIGHT:2000', '8500:BACK:100', '10500:RIGHT:100',
               '12000:CONFIRM:100', '15500:BACK:100', '17500:RIGHT:100', '19000:CONFIRM:100',
               '23000:BACK:100', '25000:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'7800:{output}/changed.bmp;14500:{output}/other.bmp;22000:{output}/reopened.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    sizes = list(map(int, re.findall(r'Opened profile: slot=-?\d+ size=(\d+)', trace)))
    assert sizes == [12, 12, 14], sizes
    saved = json.loads(settings_path.read_text())
    assert saved['fontSize'] == 12
    profiles = list(root.glob('book_profiles/epub_*/reader_profile.*'))
    assert len(profiles) == 1, profiles
    profile = decode(profiles[0].read_bytes())
    assert profile['settings']['fontSize'] == 14
    for name in ('changed', 'other', 'reopened'):
        with Image.open(output / (name + '.bmp')) as image:
            image.save(output / (name + '.png'))
    report = {'passed': True, 'opened_sizes': sizes, 'global_size': saved['fontSize'],
              'book_profile': profile, 'hardware_validated': False, 'migration_activated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
