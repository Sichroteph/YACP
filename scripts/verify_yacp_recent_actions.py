"""Exercise book actions on isolated synthetic cards, leaving every fixture intact."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import DEFAULTS, encode, decode


def scenario(program, hash_program, output, mode, action, touch=False, home_stats=False):
    output.mkdir()
    sd = create_fixture(output)
    root = sd / '.crosspoint'
    settings_path = root / 'settings.json'
    settings = json.loads(settings_path.read_text())
    settings.update(recentBooksView=mode, trackReadingStats=0)
    settings_path.write_text(json.dumps(settings))
    book = (sd / 'completion.epub').read_bytes()
    entries = []
    for i in range(3):
        path = f'/action-{i}.epub'
        (sd / path[1:]).write_bytes(book)
        entries.append(dict(path=path, title=f'Action book {i+1}', author='YACP QA', coverBmpPath=''))
    recent = root / 'recent.json'
    recent.write_text(json.dumps(dict(books=entries)))
    cache = subprocess.check_output([str(hash_program), entries[1]['path']], text=True).strip()
    profile_dir = root / 'book_profiles' / cache
    profile_dir.mkdir(parents=True)
    seed = encode(dict(DEFAULTS, fontSize=16), flags=3)
    if action == 'future': seed = seed[:4] + bytes([4]) + seed[5:]
    (profile_dir / 'reader_profile.0').write_bytes(seed)
    book_hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sd.glob('*.epub')}
    actions = ['1800:DOWN:100', '2400:BACK:100', '3400:CONFIRM:100']
    # Grid cell two and list row two, using CrossPoint's native hit regions.
    actions.append('5000:TAP:240,200,1200' if touch and mode else
                   '5000:TAP:240,160,1200' if touch else '5000:CONFIRM:1400')
    captures = f'6800:{output}/menu.bmp'
    target = {'stats': 1, 'reset': 2, 'cancel': 2, 'cache_cancel': 3, 'remove': 4, 'touch': 0, 'future': 2}[action]
    time = 9500 if action == 'future' else 7400
    if action == 'touch':
        actions += [f'{time}:BACK:100', f'{time+1000}:QUIT']
    else:
        for _ in range(target):
            actions.append(f'{time}:DOWN:250' if action == 'future' else f'{time}:DOWN:100')
            time += 700 if action == 'future' else 400
        actions.append(f'{time}:CONFIRM:100')
        captures += f';{time+1000}:{output}/action.bmp'
        time += 3000 if action == 'future' else 1700
        if action in ('cancel', 'cache_cancel', 'stats'):
            actions.append(f'{time}:BACK:100')
            time += 800
            actions.append(f'{time}:BACK:100')  # book menu
        else:
            actions += [f'{time}:DOWN:250' if action == 'future' else f'{time}:DOWN:100',
                        f'{time+700}:CONFIRM:250' if action == 'future' else f'{time+400}:CONFIRM:100']
            time += 700
            if action == 'future':
                captures += f';{time+500}:{output}/error.bmp'
                actions.append(f'{time+1000}:BACK:100')
                time += 1700
        time += 1200
        captures += f';{time}:{output}/returned.bmp'
        actions.append(f'{time+500}:BACK:100')  # recents
        if home_stats:
            assert action == 'remove'
            actions += [f'{time+1600+i*650}:DOWN:250' for i in range(5)]
            actions += [f'{time+5500}:CONFIRM:100', f'{time+8500}:QUIT']
            captures += f';{time+7400}:{output}/home-stats.bmp'
        else:
            actions.append(f'{time+1400}:BACK:100')  # Home menu
            captures += f';{time+2400}:{output}/home.bmp'
            if action not in ('stats', 'future'):
                actions.append(f'{time+3000}:CONFIRM:100')
                captures += f';{time+6000}:{output}/reader.bmp'
            actions.append(f'{time+7200}:QUIT')
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=captures)
    (output / 'input-script.txt').write_text(';'.join(actions))
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    assert trace.count('Entering activity: RecentBookMenu') == 1, trace[-3000:]
    assert 'Entering activity: EpubReader' not in trace.split('Entering activity: RecentBookMenu')[0]
    assert json.loads(settings_path.read_text())['fontSize'] == 12
    assert (profile_dir / 'reader_profile.0').read_bytes() == seed
    slots = list(profile_dir.glob('reader_profile.*'))
    if action == 'reset':
        assert len(slots) == 2
        assert decode((profile_dir / 'reader_profile.1').read_bytes())['flags'] == 0
        assert 'Opened profile: slot=1 size=12' in trace
    else:
        assert len(slots) == 1
    saved = json.loads(recent.read_text())['books']
    if action == 'remove':
        if home_stats:
            assert saved == [entries[0], entries[2]]
            assert 'Entering activity: BookStats' in trace
            assert 'Entering activity: EpubReader' not in trace
        else:
            assert len(saved) == 2 and saved[0]['path'] == entries[2]['path']
            assert saved[1] == entries[0]
    elif action in ('cancel', 'cache_cancel', 'reset'):
        assert len(saved) == 3 and saved[0]['path'] == entries[1]['path']
        if action in ('cancel', 'cache_cancel'): assert 'Opened profile: slot=0 size=16' in trace
    else:
        assert saved == entries
        assert 'Entering activity: EpubReader' not in trace
        if action == 'stats': assert 'Entering activity: BookStats' in trace
        if action == 'future': assert 'Cannot reset unsupported or damaged profile' in trace
    assert book_hashes == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sd.glob('*.epub')}
    for path in output.glob('*.bmp'):
        with Image.open(path) as picture: picture.save(path.with_suffix('.png'))
    return dict(mode=mode, action=action, touch=touch, home_stats=home_stats, passed=True)


def run(program, output, board):
    root = Path(__file__).resolve().parents[1]
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    hash_program = output / 'cache-identity'
    subprocess.run(['g++', '-std=c++17', str(root / 'test/yacp_statistics/CacheIdentity.cpp'),
                    '-o', str(hash_program)], check=True)
    cases = [dict(mode=mode, action=action) for mode in (0, 1)
             for action in ('reset', 'cancel', 'cache_cancel', 'remove', 'stats', 'future')]
    cases += [dict(mode=mode, action='remove', home_stats=True) for mode in (0, 1)]
    if board == 'pro': cases += [dict(mode=mode, action='touch', touch=True) for mode in (0, 1)]
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(scenario, program, hash_program, output / str(i), **case)
                   for i, case in enumerate(cases)]
        results = [future.result() for future in futures]
    report = dict(passed=True, board=board, cases=results, visual_review_required=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(passed=True, board=board, cases=len(results))))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--board', choices=('x3', 'pro'), default='x3')
    a = p.parse_args()
    run(a.program, a.output, a.board)
