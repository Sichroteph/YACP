"""Compare actual serialized reader geometry and pixels with Guide Dots off/on."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
from zipfile import ZipFile, ZIP_STORED

from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import crosspoint48_page_offsets, progress_record


def section_geometry(data, marker_records=None):
    offsets = crosspoint48_page_offsets(data)
    delta = {48: 0, 234: 0, 49: 1, 233: 1, 50: 2, 232: 2, 51: 3, 231: 3}[data[0]]
    lut = struct.unpack_from('<I', data, 23 + delta)[0]
    pages = struct.unpack_from(f'<{len(offsets)}I', data, lut)
    geometry, dots = [], []
    for page in pages:
        pos = page

        def take(n):
            nonlocal pos
            assert pos + n <= len(data)
            result = data[pos:pos+n]
            pos += n
            return result

        def read(fmt):
            return struct.unpack(fmt, take(struct.calcsize(fmt)))

        count, = read('<H')
        lines = []
        for _ in range(count):
            tag, = read('<B')
            assert tag == 1, 'Fixture must contain only text'
            origin = read('<hh')
            wc, flags, size = read('<HBH')
            assert flags & ~3 == 0
            text_offsets = take(wc * 2)
            xpos = read(f'<{wc}h')
            focus_x = take(wc * 2) if flags & 1 else b''
            dot_x = read(f'<{wc}h') if flags & 2 else (-32768,) * wc
            styles = take(wc)
            focus_boundary = take(wc) if flags & 1 else b''
            text = take(size)
            rubies = []
            for _ in range(wc):
                length, = read('<I')
                rubies.append(take(length))
            block_style = take(24)
            lines.append((origin, text_offsets, xpos, focus_x, styles, focus_boundary, text, rubies, block_style))
            for i, dot in enumerate(dot_x):
                if dot == -32768:
                    continue
                assert i + 1 < wc
                assert min(xpos[i], xpos[i+1]) < dot < max(xpos[i], xpos[i+1])
                dots.append((len(geometry), origin, i, dot))
        footnotes, = read('<H')
        footnote_records = [(take(32).split(b'\0', 1)[0], take(256).split(b'\0', 1)[0]) for _ in range(footnotes)]
        links, = read('<H')
        link_records = [(take(256).split(b'\0', 1)[0], read('<hhhh')) for _ in range(links)]
        if data[0] in (51, 231):
            markers, = read('<B')
            assert markers <= 8
            records = [(read('<h')[0], take(16).split(b'\0', 1)[0].decode('utf8')) for _ in range(markers)]
            if marker_records is not None: marker_records.append(records)
        geometry.append((lines, footnote_records, link_records))
    return offsets, geometry, dots


def scenario(program, output, enabled, board, font_dir=None):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(guideReadingEnabled=int(enabled), trackReadingStats=0, focusReadingEnabled=1,
                  hyphenationEnabled=1, clockEnabled=0)
    if font_dir:
        import shutil
        family = 'AtkinsonHyperlegibleNext'
        shutil.copytree(font_dir / family, sd / '.fonts' / family)
        values.update(sdFontFamilyName=family, fontSize=12)
    settings.write_text(json.dumps(values))
    book = sd / 'completion.epub'
    with ZipFile(book) as source:
        entries = {name: source.read(name) for name in source.namelist()}
    paragraph = ('This <b>carefully composed</b> paragraph keeps its original spacing, '
                 'punctuation and <i>emphasized words</i>. Guide dots must decorate gaps '
                 'without moving words or changing the next line. An extraordinary '
                 'intercontinental journey also exercises hyphenation. ')
    html = '<html xmlns="http://www.w3.org/1999/xhtml"><body><h1>Guide Dots</h1>'
    html += '<p>' + paragraph * 3 + '</p>'
    html += '<p style="text-align:left">Left aligned text keeps ordinary word gaps.</p>'
    html += '<p style="text-align:center">A centered line.</p>'
    html += '<p style="text-align:right">A right aligned line.</p>'
    html += '<p>A footnote <a href="last.xhtml#note">reference</a> retains its touch target.</p>'
    html += ('<p>' + paragraph * 3 + '</p>') * 3 + '</body></html>'
    entries['first.xhtml'] = html.encode()
    with ZipFile(book, 'w', compression=ZIP_STORED) as target:
        for name, data in entries.items():
            target.writestr(name, data)
    open_action = 'TAP:240,350' if board == 'pro' else 'CONFIRM:100'
    home_action = 'HOME:100' if board == 'pro' else 'BACK:100'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=f'1800:{open_action};11000:{home_action};13000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'8500:{output}/page.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0
    if font_dir:
        assert json.loads(settings.read_text())['sdFontFamilyName'] == family
        assert 'AtkinsonHyperlegibleNext' in (output / 'run.log').read_text()
    cache, = (sd / '.crosspoint').glob('epub_*')
    data = (cache / 'sections/0.bin').read_bytes()
    assert len(crosspoint48_page_offsets(data)) >= 2
    return data, progress_record((cache / 'progress.bin').read_bytes())


def run(program, output, board, baseline=None, font_dir=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    off, off_progress = scenario(program, output / 'off', False, board, font_dir)
    on, on_progress = scenario(program, output / 'on', True, board, font_dir)
    off_offsets, off_geometry, off_dots = section_geometry(off)
    on_offsets, on_geometry, on_dots = section_geometry(on)
    assert off_offsets == on_offsets and off_geometry == on_geometry
    assert off_progress == on_progress
    assert not off_dots and len(on_dots) > 20
    from PIL import Image, ImageChops
    with Image.open(output / 'off/page.bmp') as before, Image.open(output / 'on/page.bmp') as after:
        before, after = before.convert('L'), after.convert('L')
        # Guide rendering adds ink only. Every existing text pixel stays in place.
        assert ImageChops.subtract(after, before).getbbox() is None
        assert ImageChops.difference(before, after).getbbox() is not None
        after.save(output / 'on/page.png')
    if baseline:
        old, old_progress = scenario(baseline.resolve(strict=True), output / 'baseline', False, board, font_dir)
        old_offsets, old_geometry, old_dots = section_geometry(old)
        assert old_offsets == off_offsets and old_geometry == off_geometry and not old_dots
        assert old_progress == off_progress
        with Image.open(output / 'baseline/page.bmp') as old_image, Image.open(output / 'off/page.bmp') as current:
            assert old_image.tobytes() == current.tobytes()
    report = dict(passed=True, pages=len(on_offsets), dots=len(on_dots), geometry_identical=True,
                  progress_identical=True, only_added_ink=True, prior_build_compared=bool(baseline),
                  board=board, sd_font=bool(font_dir), hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--board', choices=('pro', 'x3'), default='pro')
    p.add_argument('--baseline', type=Path)
    p.add_argument('--font-dir', type=Path)
    a = p.parse_args()
    run(a.program, a.output, a.board, a.baseline, a.font_dir)
