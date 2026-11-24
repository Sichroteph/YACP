"""Compare publisher-page margin annotations with CrossPoint text geometry on copied fixtures."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image, ImageChops
from verify_yacp_simulator_stats import create_fixture
from verify_yacp_guide import section_geometry


def scenario(program, output, board, enabled, stress=False):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    config = sd / '.crosspoint/settings.json'
    settings = json.loads(config.read_text())
    settings.update(publisherPageNumbers=int(enabled), screenMargin=20, trackReadingStats=0, clockEnabled=0)
    config.write_text(json.dumps(settings))
    book = sd / 'completion.epub'
    with ZipFile(book) as source:
        entries = {name: source.read(name) for name in source.namelist()}
    html = '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body>'
    html += '<h1>Publisher Pages</h1><span epub:type="pagebreak" title="12"/>'
    html += '<p>The publisher marker belongs beside this paragraph. ' * 1
    html += 'A word con<span role="doc-pagebreak" aria-label="13"/>tinues across a hidden marker without a new gap.</p>'
    html += '<p><span epub:type="pagebreak" title="xiv"/>Another paragraph keeps the original typesetting.</p>'
    html += '<p>' + ('A longer paragraph allows reference markers on successive lines. ' * 20) + '</p>'
    html += '<p><span role="doc-pagebreak" title="15"/>The final paragraph is still part of the same chapter.</p>'
    html += '</body></html>'
    if stress:
        html = '<html xmlns="http://www.w3.org/1999/xhtml"><body><p>'
        html += ''.join(f'<span role="doc-pagebreak" title="{i+1}"/>word ' for i in range(80))
        html += '</p></body></html>'
    entries['first.xhtml'] = html.encode()
    with ZipFile(book, 'w', compression=ZIP_STORED) as target:
        for name, data in entries.items(): target.writestr(name, data)
    action = 'TAP:240,350' if board == 'pro' else 'CONFIRM:150'
    home = 'HOME:150' if board == 'pro' else 'BACK:150'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=f'1800:{action};10000:{home};12000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'8000:{output}/page.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0
    cache, = (sd / '.crosspoint').glob('epub_*')
    markers = []
    geometry = section_geometry((cache / 'sections/0.bin').read_bytes(), markers)
    labels = [label for page in markers for _, label in page]
    if enabled:
        assert labels == ([str(i+1) for i in range(8)] if stress else ['12', '13', 'xiv', '15']), labels
    else:
        assert not labels
    for page, records in zip(geometry[1], markers):
        ys = {line[0][1] for line in page[0]}
        assert all(y in ys for y, _ in records)
    with Image.open(output / 'page.bmp') as picture: picture.save(output / 'page.png')
    return geometry, (cache / 'progress.bin').read_bytes()


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(scenario, program, output / mode, board, enabled)
                for mode, enabled in [('off', False), ('on', True)]]
        before, after = [job.result() for job in jobs]
    assert before == after
    stress = scenario(program, output / 'bounded', board, True, stress=True)
    with Image.open(output / 'off/page.bmp') as original, Image.open(output / 'on/page.bmp') as annotated:
        bbox = ImageChops.difference(original.convert('RGB'), annotated.convert('RGB')).getbbox()
        assert bbox and bbox[2] <= 20, bbox
    report = dict(passed=True, board=board, layout_and_progress_identical=True,
                  labels_on_text_lines=True, pixel_changes_only_in_margin=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--board', choices=('x3', 'pro'), required=True)
    args = p.parse_args()
    run(args.program, args.output, args.board)
