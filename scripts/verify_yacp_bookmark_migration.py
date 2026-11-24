"""Open converted synthetic YACP v2-v5 bookmarks through CrossPoint's reader UI."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
from zipfile import ZipFile
from PIL import Image

from verify_yacp_simulator_stats import create_fixture
from yacp_bookmark_migration import candidate, check_target_collisions
from yacp_data_formats import crosspoint48_page_offsets, progress_record

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'test'))
from test_yacp_bookmark_migration import legacy, section_v44


def scenario(program, output, version, points):
    output.mkdir()
    sd = create_fixture(output)
    settings_path = sd / '.crosspoint/settings.json'
    settings = json.loads(settings_path.read_text())
    settings.update(trackReadingStats=0, readerMenuStyle=0, fontSize=points)
    settings_path.write_text(json.dumps(settings))
    book = sd / 'completion.epub'
    with ZipFile(io.BytesIO(book.read_bytes())) as old:
        chapters = {name: old.read(name) for name in old.namelist()}
    paragraphs = ''.join(f'<p>Paragraph {i}. The reader should preserve the location of this passage when the font changes. '
                         'This is synthetic text for testing bookmark migration.</p>' for i in range(24))
    paragraphs += '<p>Target sentence. This is the saved bookmark and the expected destination.</p>'
    paragraphs += ''.join(f'<p>Following paragraph {i}. These lines provide enough text after the target to check the page boundary.</p>'
                          for i in range(12))
    chapters['first.xhtml'] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Bookmark migration</h1>' +
                                paragraphs + '</body></html>').encode()
    with ZipFile(book, 'w') as archive:
        for name, data in chapters.items(): archive.writestr(name, data)
    source = legacy(version, path='/completion.epub')
    (output / 'source-bookmarks.bin').write_bytes(source)
    check_target_collisions(['/completion.epub'])
    result = candidate(source, '/completion.epub', book, {0: section_v44()})
    assert result['blocked_entries'] == 0
    (output / 'candidate-review.json').write_text(json.dumps(result, indent=2))
    target = sd / result['target_path'].lstrip('/')
    target.parent.mkdir()
    target.write_text(json.dumps(result['candidate']))
    before = hashlib.sha256(target.read_bytes()).hexdigest()
    book_before = hashlib.sha256(book.read_bytes()).hexdigest()
    actions = ['1800:CONFIRM:200', '7000:CONFIRM:200', '9000:DOWN:250', '10000:CONFIRM:250',
               '13000:CONFIRM:250', '18500:BACK:250', '21500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'11500:{output}/bookmarks.bmp;16500:{output}/destination.bmp')
    (output / 'input-script.txt').write_text(';'.join(actions))
    with (output / 'run.log').open('x') as log:
        subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=35, check=True)
    trace = (output / 'run.log').read_text()
    assert 'Entering activity: EpubReaderBookmarks' in trace
    assert 'Exiting activity: EpubReaderBookmarks' in trace
    cache = list((sd / '.crosspoint').glob('epub_*/progress.bin'))
    assert len(cache) == 1
    progress = progress_record(cache[0].read_bytes())
    assert progress['spine'] == 0
    offsets = crosspoint48_page_offsets((cache[0].parent / 'sections/0.bin').read_bytes())
    page = progress['page']
    expected = result['candidate']['bookmarks'][0]['vo']
    assert page > 0 and page + 1 < len(offsets), 'Expected a nonterminal saved page'
    assert offsets[page] <= expected < offsets[page + 1], 'Bookmark opened another passage'
    assert progress['visible_text_offset'] == offsets[page]
    assert hashlib.sha256(target.read_bytes()).hexdigest() == before
    assert hashlib.sha256(book.read_bytes()).hexdigest() == book_before
    assert (output / 'source-bookmarks.bin').read_bytes() == source
    for path in output.glob('*.bmp'):
        with Image.open(path) as picture: picture.save(path.with_suffix('.png'))
    return dict(version=version, points=points, page=page, target_offset=expected,
                resumed_offset=offsets[page], passed=True)


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = [(version, 12) for version in (2, 3, 4, 5)] + [(5, 16)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(scenario, program, output / str(i), *case) for i, case in enumerate(cases)]
        results = [f.result() for f in futures]
    report = dict(passed=True, cases=results, source_and_candidate_unchanged=True,
                  visual_review_required=True, hardware_validated=False, migration_activated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    run(a.program, a.output)
