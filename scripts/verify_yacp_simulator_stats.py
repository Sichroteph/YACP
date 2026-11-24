"""Run a repeatable completion/reopen scenario on an isolated, synthetic simulator SD."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED

from yacp_data_formats import book_stats, global_stats, finished_books


def create_fixture(output):
    sd = output / 'sd'
    settings = sd / '.crosspoint'
    settings.mkdir(parents=True)
    (settings / 'settings.json').write_text(json.dumps({
        'language': 'EN', 'uiTheme': 5, 'fontFamily': 0, 'fontSize': 12,
        'trackReadingStats': 1, 'readingIdleTimeThresholdUnits': 30, 'sleepTimeoutMinutes': 10}), encoding='utf-8')
    (settings / 'state.json').write_text('{"openEpubPath":""}', encoding='utf-8')
    (settings / 'recent.json').write_text(json.dumps({'books': [{
        'path': '/completion.epub', 'title': 'Completion Test', 'author': 'YACP QA', 'coverBmpPath': ''}]}), encoding='utf-8')
    with ZipFile(sd / 'completion.epub', 'w', compression=ZIP_STORED) as book:
        book.writestr('mimetype', 'application/epub+zip')
        book.writestr('META-INF/container.xml', '<?xml version="1.0"?><container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr('book.opf', '''<?xml version="1.0"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">yacp-completion-test</dc:identifier><dc:title>Completion Test</dc:title><dc:creator>YACP QA</dc:creator><dc:language>en</dc:language></metadata>
<manifest><item id="first" href="first.xhtml" media-type="application/xhtml+xml"/><item id="last" href="last.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine><itemref idref="first"/><itemref idref="last"/></spine></package>''')
        for name, title in (('first', 'First chapter'), ('last', 'Last chapter')):
            book.writestr(name + '.xhtml', '<?xml version="1.0"?><html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + title + '</title></head><body><h1>' + title + '</h1><p>This short chapter is a synthetic test. Turn the page to continue.</p></body></html>')
    return sd


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / ".crosspoint"
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_INPUT_SCRIPT='2000:TAP:240,350;9000:SWIPE:380,350,100,350;18000:SWIPE:380,350,100,350;23000:HOME:100;25500:TAP:45,766;27000:TAP:240,350;33000:SWIPE:380,350,100,350;38000:HOME:100;41000:QUIT',
        CROSSPOINT_SIM_SCREENSHOTS=f'15000:{output}/last-page.bmp;20000:{output}/end.bmp;24500:{output}/achievement.bmp;40000:{output}/home.bmp')
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT,
                                timeout=55)
    if result.returncode:
        raise RuntimeError(f'Simulator exited with {result.returncode}; see {output / "run.log"}')
    books = list(settings.glob('epub_*/stats_v6.bin'))
    if len(books) != 1:
        raise AssertionError(f'Expected one statistics file, found {len(books)}')
    stats = book_stats(books[0].read_bytes())
    total = global_stats((settings / 'global_stats.bin').read_bytes())
    index = finished_books((settings / 'finished_books.bin').read_bytes())
    assert stats['completed'] and total['completed_books'] == 1
    assert not stats['flags'] & 4, 'Achievement should be acknowledged once'
    assert stats['pages'] == 3 and total['pages'] == 3
    assert stats['sessions'] == 0 and 10 <= stats['seconds'] <= 19
    assert total['seconds'] == stats['seconds']
    assert len(index['books']) == 1 and index['books'][0]['seconds'] == stats['seconds']
    report = {'scenario': 'complete, return Home, reopen, complete again', 'book': stats, 'global': total,
              'finished': index, 'passed': True, 'hardware_validated': False, 'migration_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'completed_books': total['completed_books'], 'forward_pages': stats['pages'],
                      'reading_seconds': stats['seconds']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    run(args.program, args.output)
