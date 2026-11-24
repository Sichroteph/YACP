"""Exercise top/bottom progress bars using isolated English simulator cards."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image

from verify_yacp_simulator_stats import create_fixture
from verify_yacp_fixed_progress import xtc_fixture
from yacp_data_formats import progress_record


def run(program, output, kind, position, orientation, ui, board='pro', software_renderer=False):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / '.crosspoint'
    settings_path = root / 'settings.json'
    settings = json.loads(settings_path.read_text(encoding='utf-8'))
    settings.update(statusBarProgressBar=0, statusBarProgressBarPosition=0 if ui else position,
                    statusBarProgressBarThickness=2, orientation=orientation, screenMargin=5,
                    xtcStatusBarMode=1, statusBarBatteryCurrent=0)
    settings_path.write_text(json.dumps(settings), encoding='utf-8')
    if kind == 'epub':
        path = sd / 'completion.epub'
        with ZipFile(path) as book:
            entries = {name: book.read(name) for name in book.namelist()}
        paragraphs = ''.join(f'<p>Paragraph {i}. Reading text stays clear of the progress bar. '
                             'The CrossPoint layout and touch navigation remain in use.</p>' for i in range(120))
        entries['first.xhtml'] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Progress test</h1>'
                                  + paragraphs + '</body></html>').encode()
        with ZipFile(path, 'w', compression=ZIP_STORED) as book:
            for name, data in entries.items():
                book.writestr(name, data)
    else:
        path = sd / ('progress.' + kind)
        if kind == 'txt':
            path.write_text(''.join(f'Line {i:03d}. Reading text stays clear of the progress bar.\n'
                                    for i in range(200)), encoding='utf-8')
        else:
            xtc_fixture(path)
        (root / 'recent.json').write_text(json.dumps({'books': [{'path': '/' + path.name,
            'title': 'Progress test', 'author': 'YACP QA', 'coverBmpPath': ''}]}), encoding='utf-8')
    if ui:
        actions = ['1800:BACK:100'] + [f'{2600+i*600}:DOWN:100' for i in range(4)]
        actions += ['5200:CONFIRM:100', '6400:CONFIRM:100', '7600:UP:100', '8500:CONFIRM:100']
        actions += [f'{9600+i*500}:DOWN:100' for i in range(4)]
        actions += ['12000:CONFIRM:100', '13500:DOWN:100', '14500:CONFIRM:100',
                    '17000:BACK:100', '18500:BACK:100', '20000:QUIT']
        shots = f'13000:{output}/popup.bmp;16000:{output}/preview.bmp'
    else:
        # CrossPoint intentionally keeps fixed-layout XTC in portrait.
        forward = 'UP' if orientation == 3 and kind != 'xtc' else 'DOWN'
        leave = 'BACK' if board == 'x3' else 'HOME'
        actions = ['1800:CONFIRM:100', f'4500:{forward}:100', f'8000:{leave}:100', '10000:QUIT']
        shots = f'3500:{output}/first.bmp;7000:{output}/reader.bmp'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions), CROSSPOINT_SIM_SCREENSHOTS=shots)
    if software_renderer:
        env['SDL_RENDER_DRIVER'] = 'software'
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=35)
    assert result.returncode == 0
    for bitmap in output.glob('*.bmp'):
        with Image.open(bitmap) as picture:
            picture.save(bitmap.with_suffix('.png'))
    saved = json.loads(settings_path.read_text(encoding='utf-8'))
    assert saved['statusBarProgressBarPosition'] == position
    report = {'passed': True, 'format': kind, 'position': position, 'orientation': orientation,
              'ui_toggle': ui, 'board': board, 'effective_orientation': 0 if kind == 'xtc' else orientation,
              'software_renderer': software_renderer,
              'visual_review_required': True, 'hardware_validated': False}
    if not ui:
        files = list(root.glob('*/progress.bin'))
        assert len(files) == 1, files
        data = files[0].read_bytes()
        if kind == 'epub':
            report['progress'] = progress_record(data)
            assert report['progress']['spine'] == 0
            assert report['progress']['page'] == 1
        else:
            assert int.from_bytes(data, 'little') == 1
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--kind', choices=('epub', 'txt', 'xtc'), default='epub')
    p.add_argument('--position', required=True, type=int, choices=(0, 1))
    p.add_argument('--orientation', type=int, choices=(0, 1, 2, 3), default=0)
    p.add_argument('--ui', action='store_true')
    p.add_argument('--board', choices=('x3','pro'), default='pro')
    p.add_argument('--software-renderer', action='store_true')
    a = p.parse_args()
    run(a.program, a.output, a.kind, a.position, a.orientation, a.ui, a.board, a.software_renderer)
