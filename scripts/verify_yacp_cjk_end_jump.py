"""Exercise a cold backward chapter crossing, CJK end-page layout and cached reopen."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
from zipfile import ZipFile, ZIP_STORED

from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from verify_yacp_guide import section_geometry
from yacp_data_formats import progress_record


def run(program, source_family, output, cache_name, board):
    program = program.resolve(strict=True)
    source_family = source_family.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    shutil.copytree(source_family, sd / '.fonts' / source_family.name)
    book_path = sd / 'completion.epub'
    with ZipFile(book_path) as book:
        entries = {name: book.read(name) for name in book.namelist()}
    title = 'Long CJK chapter'
    paragraph = ('\u8fd9\u662f\u4e00\u6bb5\u7528\u4e8e\u6d4b\u8bd5\u7535\u5b50\u4e66\u6392\u7248\u7684\u4e2d\u6587\u3002'
                 '\u8bf7\u7ffb\u9875\u7ee7\u7eed\u9605\u8bfb\uff0c\u5e76\u4fdd\u7559\u6b63\u786e\u7684\u6587\u5b57\u4f4d\u7f6e\u3002') * 300
    entries['first.xhtml'] = (f'<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>{title}</h1>'
                               f'<p>{paragraph}</p><p>END OF LONG CHAPTER</p></body></html>').encode()
    with ZipFile(book_path, 'w', compression=ZIP_STORED) as book:
        for name, data in entries.items():
            book.writestr(name, data)
    root = sd / '.crosspoint'
    settings = json.loads((root / 'settings.json').read_text())
    settings.update(uiTheme=0, sdFontFamilyName=source_family.name, fontSize=14, trackReadingStats=0,
                    statusBarTimeLeft=0, guideReadingEnabled=0, forceParagraphIndents=0, clockEnabled=0)
    (root / 'settings.json').write_text(json.dumps(settings))
    cache = root / cache_name
    cache.mkdir()
    (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', 1, 0, 1, 0))

    def invoke(label, actions, screenshot_at):
        # Resume directly, avoiding different upstream and YACP Home designs.
        (root / 'state.json').write_text(json.dumps({'openEpubPath': '/completion.epub',
            'lastSleepFromReader': True, 'showBootScreen': True, 'readerActivityLoadCount': 0}))
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_INPUT_SCRIPT=actions,
                   CROSSPOINT_SIM_SCREENSHOTS=f'{screenshot_at}:{output}/{label}.bmp')
        with (output / f'{label}.log').open('x') as log:
            result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=60)
        assert result.returncode == 0
        text = (output / f'{label}.log').read_text()
        assert any(label + source_family.name in text for label in
                   ('Loaded SD font family: ', 'Loaded SD card font family: '))
        for failure in ('Text layout dropped content', 'Failed during incremental', 'Page out of bounds',
                        'Failed to load page', 'Failed to start section', 'Failed to persist page'):
            assert failure not in text, failure
        with Image.open(output / f'{label}.bmp') as image:
            image.save(output / f'{label}.png')
        return text

    previous = 'LEFT:100' if board == 'x3' else 'SWIPE:100,350,380,350'
    home = 'BACK:100' if board == 'x3' else 'HOME:100'
    trace = invoke('cold', f'7500:{previous};42000:{home};45000:QUIT', 40000)
    assert 'Loaded cache: 1, 0' in trace
    assert len(list(root.glob('epub_*'))) == 1, 'Wrong supplied native path hash'
    data = (cache / 'sections/0.bin').read_bytes()
    assert data[0] in (48, 50, 51), 'A direct end-page jump must finish the section'
    offsets, geometry, dots = section_geometry(data)
    assert len(offsets) > 10 and not dots
    # Compare the actual final page text, not only a percentage or page counter.
    final_text = b' '.join(line[6].replace(b'\0', b' ') for line in geometry[-1][0])
    assert b'END OF LONG CHAPTER' in final_text
    position = progress_record((cache / 'progress.bin').read_bytes())
    assert position['spine'] == 0 and position['page'] == len(offsets) - 1
    assert position['visible_text_offset'] == offsets[-1]
    before = hashlib.sha256(data).hexdigest()
    trace = invoke('reopened', f'11000:{home};14000:QUIT', 9500)
    assert 'Cache found, skipping build' in trace
    assert hashlib.sha256((cache / 'sections/0.bin').read_bytes()).hexdigest() == before
    assert progress_record((cache / 'progress.bin').read_bytes()) == position
    with Image.open(output / 'cold.bmp') as cold, Image.open(output / 'reopened.bmp') as reopened:
        assert cold.tobytes() == reopened.tobytes()
    report = dict(passed=True, board=board, paragraph_codepoints=len(paragraph), pages=len(offsets),
                  position=position, end_marker_present=True, cache_unchanged_on_reopen=True,
                  reopened_image_identical=True, hardware_heap_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--source-family', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cache-name', required=True, help='Native cache identity for /completion.epub')
    p.add_argument('--board', choices=('x3', 'pro'), required=True)
    a = p.parse_args()
    run(a.program, a.source_family, a.output, a.cache_name, a.board)
