"""Exercise independent side-button long presses on isolated simulator cards."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode


def run_one(program, output, mode, pro=False, firmware_version=None):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    # Three long chapters prevent accidental end-of-book prompts from masking
    # an extra release-triggered turn.
    with ZipFile(sd / 'long.epub', 'w', compression=ZIP_STORED) as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(3))
        spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(3))
        book.writestr('book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">yacp-hold</dc:identifier><dc:title>Long Press Test</dc:title><dc:language>en</dc:language></metadata><manifest>{items}</manifest><spine>{spine}</spine></package>')
        for i in range(3):
            text = ''.join(f'<p>Paragraph {j}. A repeatable reader test checks that holding a button performs exactly one action and its release does not turn another page.</p>' for j in range(75))
            book.writestr(f'c{i}.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter {i+1}</title></head><body><h1>Chapter {i+1}</h1>{text}</body></html>')
    root = sd / '.crosspoint'
    (root/'recent.json').write_text(json.dumps({'books': [{'path': '/long.epub', 'title': 'Long Press Test', 'author': 'YACP QA', 'coverBmpPath': ''}]}))
    settings = root / 'settings.json'
    values = json.loads(settings.read_text())
    side, front, expected_spine, expected_page, expected_size, expected_orientation = {
        'previous': (4, 1, 0, 1, 12, 0),
        'previous-both': (4, 0, 0, 1, 12, 0),
        'chapter': (0, 0, 1, 0, 12, 0),
        'font': (1, 0, 0, 0, 14, 0),
        'rotation': (3, 0, 0, 0, 12, 3),
        'front-font': (2, 3, 0, 0, 14, 0),
        'off': (2, 1, 0, 1, 12, 0),
        'inherit': (5, 0, 0, 1, 12, 0),
        'invalid': (256, 0, 0, 1, 12, 0),
    }[mode]
    values.update(trackReadingStats=0, sideButtonLongPress=side, longPressButtonBehavior=front,
                  sideButtonLayout=3 if mode == 'previous-both' else 0)
    if firmware_version:
        values['buttonLayoutPromptVersion'] = firmware_version
    settings.write_text(json.dumps(values))
    actions = ['1800:TAP:240,350' if pro else '1800:CONFIRM:100']
    if mode.startswith('previous'):
        actions += ['6000:DOWN:100', '8000:DOWN:100']
    key = 'UP' if mode in ('font', 'rotation', 'previous-both') else 'RIGHT' if mode == 'front-font' else 'DOWN'
    actions += [f'10500:{key}:2000', '16500:HOME:100' if pro else '16500:BACK:100', '19500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'9800:{output}/before.bmp;12000:{output}/held.bmp;15200:{output}/released.bmp')
    with (output/'run.log').open('x') as log:
        proc = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=35)
    assert proc.returncode == 0, mode
    progress = list(root.glob('epub_*/progress.bin'))
    assert len(progress) == 1, mode
    data = progress[0].read_bytes()
    actual = (int.from_bytes(data[:2], 'little'), int.from_bytes(data[2:4], 'little'))
    assert actual == (expected_spine, expected_page), (mode, actual)
    saved = json.loads(settings.read_text())
    assert saved['fontSize'] == 12, (mode, saved['fontSize'])
    assert saved.get('orientation', 0) == 0, (mode, saved.get('orientation'))
    profiles = list(root.glob('book_profiles/epub_*/reader_profile.*'))
    if expected_size != 12 or expected_orientation != 0:
        assert len(profiles) == 1, (mode, profiles)
        profile = decode(profiles[0].read_bytes())['settings']
        assert (profile['fontSize'], profile['orientation']) == (expected_size, expected_orientation)
    else:
        assert not profiles, (mode, profiles)
    assert saved['sideButtonLongPress'] == (5 if mode == 'invalid' else side)
    assert 'Discovering requested SD font catalogue' not in (output/'run.log').read_text()
    for name in ('before', 'held', 'released'):
        with Image.open(output/(name+'.bmp')) as im:
            im.save(output/(name+'.png'))
    # The release must not alter either text or position after a long hold.
    assert (output/'held.bmp').read_bytes() == (output/'released.bmp').read_bytes(), mode
    report = {'passed': True, 'mode': mode, 'position': actual, 'font_size': expected_size,
              'orientation': expected_orientation, 'release_unchanged': True, 'hardware_validated': False}
    (output/'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--pro', action='store_true', help='Use the X4 Pro touch and Home controls')
    p.add_argument('--firmware-version', help='Skip the unrelated first-boot button profile prompt')
    p.add_argument('--mode', nargs='+', default=['previous', 'previous-both', 'chapter', 'font', 'rotation', 'front-font', 'off', 'inherit', 'invalid'])
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        reports = list(pool.map(lambda mode: run_one(a.program.resolve(strict=True), a.output.absolute()/mode, mode,
                                                   a.pro, a.firmware_version), a.mode))
    print(json.dumps(reports))
