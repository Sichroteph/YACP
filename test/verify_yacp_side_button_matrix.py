"""Exercise side-button short/hold combinations through the real Pro readers."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from zipfile import ZipFile, ZIP_STORED

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode

SHORT = [(-1, 1), (1, -1), (0, 0), (1, 1), (-1, -1), (1, 0), (0, 1), (0, 0)]
LONG = [(0, 0), (1, 0), (2, 0), (3, 0), (4, 0), (5, 0), (5, 1), (5, 2), (5, 3)]


def execute(program, output, actions, *, shots=False, wake=False):
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(output / 'sd'), CROSSPOINT_SIM_INPUT_SCRIPT=actions,
               CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE='1000:QUIT' if wake else '',
               CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE='',
               CROSSPOINT_SIM_SCREENSHOTS=(f'3300:{output}/held.bmp;4200:{output}/released.bmp' if shots else ''))
    with (output / 'run.log').open('x') as log:
        process = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                 stderr=subprocess.STDOUT, timeout=30)
    assert process.returncode == 0, (output, process.returncode)
    return (output / 'run.log').read_text()


def position(root, kind):
    paths = list(root.glob(kind + '_*/progress.bin'))
    assert len(paths) == 1, (kind, paths)
    data = paths[0].read_bytes()
    return (int.from_bytes(data[:2], 'little'), int.from_bytes(data[2:4], 'little')) if kind == 'epub' else (
        int.from_bytes(data[:2] if kind == 'txt' else data, 'little'),)


def seed(program, output, kind):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    book = sd / ('matrix.' + kind)
    if kind == 'epub':
        with ZipFile(book, 'w', compression=ZIP_STORED) as archive:
            archive.writestr('mimetype', 'application/epub+zip')
            archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(3))
            spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(3))
            archive.writestr('book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">side-matrix</dc:identifier><dc:title>Side Button Matrix</dc:title><dc:language>en</dc:language></metadata><manifest>{items}</manifest><spine>{spine}</spine></package>')
            for i in range(3):
                paragraphs = ''.join(f'<p>Chapter {i} paragraph {n}. This repeatable text checks reading position and independent physical button actions without an extra action on release.</p>' for n in range(75))
                archive.writestr(f'c{i}.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter {i}</title></head><body>{paragraphs}</body></html>')
    elif kind == 'txt':
        book.write_text(''.join(f'Line {n:04d}. Reader position and independent side-button test.\n' for n in range(2000)))
    else:
        count, width, height = 40, 64, 64
        offset = 56 + count * 16
        header = struct.pack('<IBBHBBBBIQQQQII', 0x00435458, 1, 0, count, 0, 0, 0, 0, 1, 0, 56, offset, 0, 0, 0)
        table, pages = bytearray(), []
        for i in range(count):
            pixels = bytes([255 ^ (i + 1)]) * (width * height // 8)
            page = struct.pack('<IHHBBIQ', 0x00475458, width, height, 0, 0, len(pixels), 0) + pixels
            table.extend(struct.pack('<QIHH', offset, len(page), width, height))
            offset += len(page)
            pages.append(page)
        book.write_bytes(header + table + b''.join(pages))
    root = sd / '.crosspoint'
    (root / 'recent.json').write_text(json.dumps({'books': [{'path': '/' + book.name,
        'title': 'Side Button Matrix', 'author': 'YACP QA', 'coverBmpPath': ''}]}))
    settings = root / 'settings.json'
    values = json.loads(settings.read_text())
    values.update(fontSize=14, trackReadingStats=0, statusBarTimeLeft=0, sideButtonLayout=0,
                  sideButtonLongPress=2, longPressButtonBehavior=0, autoBackupStats=0,
                  orientation=0, sleepScreen=6, shortPwrBtn=1, doubleClickPwrLight=0)
    settings.write_text(json.dumps(values))
    execute(program, output, '800:TAP:240,350;2500:DOWN:100;3200:DOWN:100;4000:HOME:100;5000:QUIT')
    assert position(root, kind) == ((0, 2) if kind == 'epub' else (2,)), (kind, position(root, kind))
    return sd


def run_case(program, seeds, output, case):
    kind, layout, long_mode, inherited, button, short = case
    name = f'{kind}-s{layout}-l{long_mode}-i{inherited}-b{button}-' + ('tap' if short else 'hold')
    folder = output / name
    folder.mkdir()
    shutil.copytree(seeds[kind], folder / 'sd')
    root = folder / 'sd/.crosspoint'
    settings = root / 'settings.json'
    values = json.loads(settings.read_text())
    values.update(sideButtonLayout=layout, sideButtonLongPress=long_mode, longPressButtonBehavior=inherited)
    settings.write_text(json.dumps(values))
    direction = SHORT[layout][button]
    sleeps = short and ((layout == 5 and button == 1) or (layout == 6 and button == 0) or layout == 7)
    key = 'UP' if button == 0 else 'DOWN'
    actions = f'700:TAP:240,350;2000:{key}:{100 if short else 1500};'
    actions += '3500:POWER:1800' if sleeps else '4800:HOME:100;6000:QUIT'
    trace = execute(program, folder, actions, shots=not short, wake=sleeps)
    assert trace.count('Entering deep sleep') == int(sleeps), (name, 'unexpected sleep')
    actual = position(root, kind)
    saved = json.loads(settings.read_text())
    profile = list(root.glob('book_profiles/epub_*/reader_profile.*')) if kind == 'epub' else []
    actual_text = decode(profile[0].read_bytes())['settings'] if profile else saved
    font, orientation = 14, 0
    action, step = 'page', 0
    if short:
        step = direction
    elif long_mode == 4:
        step = -1
    elif long_mode == 2 or (long_mode == 5 and inherited == 0):
        step = direction
    elif long_mode == 1:
        action = 'font'
        font = 16 if button == 0 else 12
    elif long_mode == 3:
        action = 'orientation'
        orientation = 3 if button == 0 else 1
    else:
        step = direction or (-1 if button == 0 else 1)
        behavior = 1 if long_mode == 0 else inherited
        if behavior == 1:
            action = 'chapter'
        elif behavior == 2:
            action, orientation = 'orientation', 3 if step > 0 else 1
        elif behavior == 3:
            action, font = 'font', 16 if step > 0 else 12
    if kind == 'xtc' and action in ('font', 'orientation'):
        font, orientation, step, action = 14, 0, 0, 'page'
    assert actual_text.get('fontSize', 14) == font, (name, 'font', actual_text.get('fontSize'), font)
    assert actual_text.get('orientation', 0) == orientation, (name, 'orientation', actual_text.get('orientation'), orientation)
    if action == 'page':
        expected = (0, 2 + step) if kind == 'epub' else (2 + step,)
        assert actual == expected, (name, actual, expected)
    elif action == 'chapter':
        expected = (1, 0) if step > 0 else (0, 0)
        if kind != 'epub':
            expected = (max(0, 2 + step * 10),)
        assert actual == expected, (name, actual, expected)
    if not short:
        assert (folder / 'held.bmp').read_bytes() == (folder / 'released.bmp').read_bytes(), (name, 'release changed frame')
    result = dict(case=name, passed=True, position=actual, font=font, orientation=orientation, sleep=sleeps)
    (folder / 'result.json').write_text(json.dumps(result, indent=2))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=8)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--inherited-taps', action='store_true')
    args = parser.parse_args()
    program, output = args.program.resolve(strict=True), args.output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    seeds = {kind: seed(program, output / ('seed-' + kind), kind) for kind in ('epub', 'txt', 'xtc')}
    cases = [(kind, layout, long_mode, inherited, button, False)
             for kind in seeds for layout in (6, 5, 7, 2, 0, 1, 3, 4)
             for long_mode, inherited in LONG for button in (0, 1)]
    cases += [(kind, layout, long_mode, inherited, button, True)
              for kind in seeds for layout in range(8) for long_mode, inherited in LONG for button in (0, 1)]
    if args.smoke:
        cases = [case for case in cases if not case[-1] and case[1] in (6, 7, 2) and case[2] in (0, 1, 4)]
    if args.inherited_taps:
        cases = [case for case in cases if case[-1] and case[2] == 5 and case[3] > 0]
    results, failures = [], []
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(run_case, program, seeds, output, case): case for case in cases}
        for future in as_completed(futures):
            try:
                results.append(future.result())
            except Exception as error:
                failures.append({'case': futures[future], 'error': repr(error)})
            if (len(results) + len(failures)) % 24 == 0:
                print(json.dumps({'completed': len(results) + len(failures), 'total': len(cases), 'failures': len(failures)}), flush=True)
    report = dict(passed=not failures, count=len(cases), successes=len(results), failures=failures,
                  hardware_validated=False, results=results)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({key: value for key, value in report.items() if key != 'results'}), flush=True)
    if failures:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
