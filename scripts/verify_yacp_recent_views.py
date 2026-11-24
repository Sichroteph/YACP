"""Exercise isolated recent lists/grids, selection, missing files and lazy covers."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, mode, board, missing=False, initial=0, home_only=False, empty=False, touch=False):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(recentBooksView=mode, trackReadingStats=0)
    settings.write_text(json.dumps(values))
    book = (sd / 'completion.epub').read_bytes()
    entries = []
    (sd / 'covers').mkdir()
    for index in range(0 if empty else 18):
        path = f'/recent-{index:02d}.epub'
        if not (missing and index == 7):
            (sd / path[1:]).write_bytes(book)
        cover = f'/covers/{index:02d}.bmp'
        picture = Image.new('1', (100, 150), 1)
        draw = ImageDraw.Draw(picture)
        draw.rectangle((5, 5, 94, 144), outline=0, width=3)
        draw.rectangle((12, 12 + index * 5, 87, 25 + index * 5), fill=0)
        draw.text((20, 125), str(index + 1), fill=0)
        picture.save(sd / cover[1:])
        entries.append(dict(path=path, title=f'Recent book {index + 1:02d}', author='YACP QA',
                            coverBmpPath='' if home_only else cover))
    recent = sd / '.crosspoint/recent.json'
    recent.write_text(json.dumps(dict(books=entries)))
    valid = [book for i, book in enumerate(entries) if not (missing and i == 7)]
    actions = []
    timestamp = 1800
    for _ in range(initial):
        actions.append(f'{timestamp}:DOWN:100')
        timestamp += 350
    if home_only:
        actions.append(f'{timestamp+3500}:QUIT')
        captures = f'{timestamp+2500}:{output}/home.bmp'
    else:
        actions += [f'{timestamp}:TAP:240,756' if touch else f'{timestamp}:BACK:100',
                    f'{timestamp+1000}:TAP:240,100' if touch else f'{timestamp+1000}:CONFIRM:100']
        timestamp += 2600
        captures = f'{timestamp}:{output}/first.bmp'
        timestamp += 700
        if touch:
            assert board == 'pro' and mode == 1 and not missing and not initial and not empty
            actions.append(f'{timestamp}:SWIPE:400,350,80,350')
            timestamp += 1700
        elif not initial and valid:
            button = 'RIGHT' if mode else 'DOWN'
            for _ in range(len(valid) - 1):
                actions.append(f'{timestamp}:{button}:100')
                timestamp += 350
        captures += f';{timestamp+900}:{output}/selected.bmp'
        actions += [f'{timestamp+1700}:TAP:400,600' if touch else
                    f'{timestamp+1700}:CONFIRM:100' if valid else f'{timestamp+1700}:BACK:100',
                    f'{timestamp+7200}:QUIT']
        captures += f';{timestamp+6000}:{output}/opened.bmp'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=captures)
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    assert json.loads(settings.read_text())['recentBooksView'] == mode
    saved = json.loads(recent.read_text())['books']
    if home_only:
        assert not list((sd / '.crosspoint').glob('epub_*'))
        assert 'Grid page start=' not in trace
        assert saved == entries
    else:
        assert 'Entering activity: RecentBooks' + ('Grid' if mode else 'List') in trace
        if valid:
            wanted = valid[initial] if initial else valid[-1]
            assert saved[0]['path'] == wanted['path'], (saved[0]['path'], wanted['path'])
            assert saved[0]['title'] == 'Completion Test'
            # CrossPoint already prunes missing entries when addBook remembers a reader.
            assert len(saved) == len(valid)
            assert saved[1:] == [book for book in valid if book['path'] != wanted['path']]
            assert 'Entering activity: EpubReader' in trace
        else:
            assert 'Exiting activity: RecentBooks' in trace
        if mode and len(valid) > 9 and not initial:
            assert 'Grid page start=0' in trace and 'Grid page start=9' in trace
    for path in output.glob('*.bmp'):
        with Image.open(path) as picture: picture.save(path.with_suffix('.png'))
    return dict(mode=mode, missing=missing, initial=initial, home_only=home_only,
                empty=empty, touch=touch, passed=True)


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = [dict(mode=0), dict(mode=1), dict(mode=1, missing=True),
             dict(mode=0, initial=5), dict(mode=1, initial=10),
             dict(mode=0, empty=True), dict(mode=1, empty=True), dict(mode=1, home_only=True)]
    if board == 'pro': cases.append(dict(mode=1, touch=True))
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(scenario, program, output / str(i), board=board, **case)
                   for i, case in enumerate(cases)]
        results = [f.result() for f in futures]
    report = dict(passed=True, board=board, cases=results, visual_review_required=True,
                  hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(passed=True, board=board, cases=len(results))))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--board', choices=('x3', 'pro'), default='x3')
    a = p.parse_args()
    run(a.program, a.output, a.board)
