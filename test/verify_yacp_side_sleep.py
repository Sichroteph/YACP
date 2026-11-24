"""Exercise side-key sleep through actual reader activities on synthetic cards."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile, ZIP_STORED

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from verify_yacp_simulator_stats import create_fixture
from verify_yacp_fixed_progress import xtc_fixture
from yacp_data_formats import book_stats, global_stats
from yacp_book_profile import decode


def run(program, output, mode):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    kind = mode.split('-')[0]
    root = sd / '.crosspoint'
    if kind == 'epub':
        # Keep a short forward press away from the completion dialog.
        path = sd / 'completion.epub'
        with ZipFile(path) as book:
            entries = {name: book.read(name) for name in book.namelist()}
        entries['book.opf'] = entries['book.opf'].replace(
            b'</manifest>', b'<item id="extra" href="extra.xhtml" media-type="application/xhtml+xml"/></manifest>').replace(
            b'</spine>', b'<itemref idref="extra"/></spine>')
        entries['extra.xhtml'] = entries['last.xhtml']
        with ZipFile(path, 'w', compression=ZIP_STORED) as book:
            for name, content in entries.items():
                book.writestr(name, content)
    if kind != 'epub':
        book = sd / ('sleep.' + kind)
        if kind == 'txt':
            book.write_text('Reading sleep test.\n' * 300)
        else:
            xtc_fixture(book)
        (root / 'recent.json').write_text(json.dumps({'books': [{
            'path': '/' + book.name, 'title': 'Sleep test', 'author': 'YACP QA', 'coverBmpPath': ''}]}))
    settings = root / 'settings.json'
    values = json.loads(settings.read_text())
    both = '-both-' in mode
    upper = mode.endswith('upper') or mode.endswith('up')
    layout = 7 if both else 6 if upper else 5
    rotation = mode.endswith('rotation')
    values.update(sideButtonLongPress=3 if rotation else 2, sideButtonLayout=layout,
                  longPressButtonBehavior=0, sleepScreen=6, shortPwrBtn=1, doubleClickPwrLight=0)
    settings.write_text(json.dumps(values))
    hold = mode.endswith('hold') or rotation
    key = 'UP' if upper else 'DOWN'
    actions = '2000:TAP:240,350;'
    if not both:
        actions += '6000:' + ('DOWN' if upper else 'UP') + ':100;'
    actions += f'14000:{key}:{1800 if hold else 100};'
    actions += '18000:HOME:100;20500:QUIT' if hold else '20000:POWER:1800'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=actions,
               CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='6500:QUIT')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0, mode
    trace = (output / 'run.log').read_text()
    assert trace.count('Entering deep sleep') == (0 if hold else 1), mode
    activity = 'XtcReader' if kind == 'xtc' else 'EpubReader'
    assert trace.count('Entering activity: ' + activity + '\n') == (1 if hold else 2), mode
    saved = json.loads(settings.read_text())
    assert saved['sideButtonLongPress'] == (3 if rotation else 2), (mode, saved['sideButtonLongPress'])
    assert saved['sideButtonLayout'] == layout, (mode, saved['sideButtonLayout'])
    cache_kind = 'xtc' if kind == 'xtc' else 'epub'
    progress = list(root.glob(cache_kind + '_*/progress.bin'))
    assert len(progress) == 1, (mode, progress)
    data = progress[0].read_bytes()
    turn_position = {'epub': data[:2], 'txt': data[2:4], 'xtc': data}[kind]
    page = int.from_bytes(turn_position, 'little')
    assert page == (0 if both else 1), (mode, page)
    if rotation:
        profiles = list(root.glob('book_profiles/epub_*/reader_profile.*'))
        assert len(profiles) == 1, (mode, profiles)
        assert decode(profiles[0].read_bytes())['settings']['orientation'] == 1, mode
    if kind == 'epub':
        stats = list(root.glob('epub_*/stats_v6.bin'))
        assert len(stats) == 1, mode
        seconds = book_stats(stats[0].read_bytes())['seconds']
        assert seconds > 0, (mode, seconds)
        assert global_stats((root / 'global_stats.bin').read_bytes())['seconds'] == seconds, mode
    report = dict(passed=True, mode=mode, position=page, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    modes = ['epub-lower', 'epub-upper', 'epub-both-up', 'epub-both-down', 'epub-hold',
             'epub-rotation', 'txt-lower', 'xtc-upper']
    with ThreadPoolExecutor(max_workers=3) as pool:
        reports = list(pool.map(lambda mode: run(args.program.resolve(strict=True),
                                                args.output.absolute() / mode, mode), modes))
    print(json.dumps(reports))
