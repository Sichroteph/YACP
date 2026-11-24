"""Exercise each built-in family, size and face through the actual EPUB reader."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED

from PIL import Image, ImageDraw
from verify_yacp_guide import section_geometry
from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import progress_record


def scenario(program, output, family, size, board):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text(encoding='utf-8'))
    values.update(fontFamily=family, fontSize=size, sdFontFamilyName='',
                  guideReadingEnabled=0, focusReadingEnabled=0,
                  trackReadingStats=0, clockEnabled=0)
    settings.write_text(json.dumps(values), encoding='utf-8')
    book = sd / 'completion.epub'
    with ZipFile(book) as source:
        entries = {name: source.read(name) for name in source.namelist()}
    paragraph = ('Regular text. <b>Bold text.</b> <i>Italic text.</i> '
                 '<b><i>Bold italic text.</i></b> Accents: café, Noël, façade, '
                 'cœur, déjà, forêt. Punctuation: “reader”, apostrophe’s, '
                 'ellipsis… Numbers: 0123456789. ')
    title = ('Bitter' if family else 'Lexend Deca') + f' {size} pt'
    entries['first.xhtml'] = (
        '<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>' + title + '</h1>'
        + ('<p>' + paragraph * 2 + '</p>') * 12 + '</body></html>').encode('utf-8')
    with ZipFile(book, 'w', compression=ZIP_STORED) as target:
        for name, data in entries.items():
            target.writestr(name, data)
    open_action = 'TAP:240,350' if board == 'pro' else 'CONFIRM:100'
    home_action = 'HOME:100' if board == 'pro' else 'BACK:100'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=(f'2500:{open_action};8500:DOWN:100;'
                                           f'12000:{home_action};14000:QUIT'),
               CROSSPOINT_SIM_SCREENSHOTS=(f'7000:{output}/first.bmp;'
                                          f'11000:{output}/second.bmp'))
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=30)
    assert result.returncode == 0, output
    cache, = (sd / '.crosspoint').glob('epub_*')
    offsets, geometry, dots = section_geometry((cache / 'sections/0.bin').read_bytes())
    assert not dots and len(offsets) >= 2
    styles = {style & 3 for lines, _, _ in geometry for line in lines for style in line[4]}
    assert styles == {0, 1, 2, 3}, styles
    rendered_text = b''.join(line[6] for lines, _, _ in geometry for line in lines)
    for word in ('café', 'Noël', 'façade', 'cœur', 'déjà', 'forêt'):
        assert word.encode('utf-8') in rendered_text, word
    progress = progress_record((cache / 'progress.bin').read_bytes())
    assert progress['spine'] == 0 and progress['page'] == 1, progress
    saved = json.loads(settings.read_text(encoding='utf-8'))
    assert saved['fontFamily'] == family and saved['fontSize'] == size
    assert not saved.get('sdFontFamilyName')
    hashes = []
    for name in ('first', 'second'):
        with Image.open(output / (name + '.bmp')) as screenshot:
            screenshot.save(output / (name + '.png'))
            hashes.append(hashlib.sha256(screenshot.tobytes()).hexdigest())
    assert hashes[0] != hashes[1]
    return dict(family=title, pages=len(offsets), styles=sorted(styles),
                page_hashes=hashes, progress=progress, passed=True)


def run(program, output, board, variant, standard_output=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sizes = (10, 12, 14, 16) if variant == 'standard' else (16, 18, 20)
    cases = [(family, size) for family in (0, 1) for size in sizes]
    with ThreadPoolExecutor(max_workers=3) as pool:
        futures = [pool.submit(scenario, program, output / f'{family}-{size}', family, size, board)
                   for family, size in cases]
        results = [future.result() for future in futures]
    assert len({row['page_hashes'][0] for row in results}) == len(cases)
    if standard_output:
        assert variant == 'xlarge'
        baseline = json.loads((standard_output / 'result.json').read_text(encoding='utf-8'))
        assert baseline['passed'] and baseline['board'] == board and baseline['variant'] == 'standard'
        for row in results:
            if row['family'].endswith('16 pt'):
                previous, = [old for old in baseline['cases'] if old['family'] == row['family']]
                assert row['page_hashes'] == previous['page_hashes'], row['family']
                assert row['progress'] == previous['progress'], row['family']
    # This sheet is a review aid, not an automatically asserted visual result.
    sheet = Image.new('RGB', (len(sizes) * 260, 2 * 460), 'white')
    draw = ImageDraw.Draw(sheet)
    for (family, size), row in zip(cases, results):
        x, y = sizes.index(size) * 260, family * 460
        draw.text((x + 6, y + 4), row['family'], fill='black')
        with Image.open(output / f'{family}-{size}' / 'first.png') as screenshot:
            screenshot.thumbnail((252, 432))
            sheet.paste(screenshot.convert('RGB'), (x + 4, y + 24))
    sheet.save(output / 'review.png')
    report = dict(passed=True, board=board, variant=variant, cases=results,
                  program_sha256=hashlib.sha256(program.read_bytes()).hexdigest(),
                  shared_16pt_matches_standard=bool(standard_output),
                  visual_review_required=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(dict(passed=True, cases=len(cases), board=board, variant=variant)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--board', choices=('pro', 'x3'), default='pro')
    parser.add_argument('--variant', choices=('standard', 'xlarge'), default='standard')
    parser.add_argument('--standard-output', type=Path)
    args = parser.parse_args()
    run(args.program, args.output, args.board, args.variant, args.standard_output)
