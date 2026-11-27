"""Exercise real X3 UI reading and persistence after successful and failed imports."""
import argparse
import json
import os
from pathlib import Path
import sys
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from verify_yacp_runtime_migration import boot, hashes, synthetic, unix
from yacp_data_formats import progress_record


def exercise(program, output, failure):
    synthetic(output)
    sd = output / 'sd'
    for book in sd.glob('*.epub'):
        with ZipFile(book) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        entries['c.xhtml'] = ('<html xmlns="http://www.w3.org/1999/xhtml"><body>'
                              '<p>Earlier sentence</p><p>Target sentence</p>' + ''.join(
                                  f'<p>Paragraph {i}. Reading must remain available after an import failure. '
                                  'This test checks that new reading positions survive a restart.</p>'
                                  for i in range(150)) + '</body></html>').encode()
        with ZipFile(book, 'w') as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
    if failure == 'backup':
        (sd / '.crosspoint/crossink-settings.json').write_text(
            '{"buttonLayoutPromptVersion":"1.6.1-crossink"}', encoding='utf-8')
        (sd / '.crosspoint/settings.json').write_text('{"language":"EN","fontSize":14}', encoding='utf-8')
        pending = sd / '.yacp-source-backup/pending'
        pending.mkdir(parents=True)
        (pending / 'settings.json').write_bytes(b'incompatible partial data')
    source = hashes(sd / '.crosspoint')
    preserved = sd / ('.crosspoint' if failure == 'backup' else '.yacp-migration-v1/original')
    if failure == 'settings':
        # A previous beta stopped after preserving the old store. A directory
        # collision now forces the staged settings write to fail.
        boot(program, output, phase='original')
        (sd / '.yacp-migration-v1/staging/settings.json').mkdir(parents=True)
    shots = ';'.join(f'{ms}:{unix(output)}/{name}.bmp' for ms, name in
                     ((4000, 'library'), (8500, 'reader'), (12500, 'advanced')))
    log = boot(program, output, actions='1800:CONFIRM:100;5500:CONFIRM:100;'
               '9500:RIGHT:100;11000:RIGHT:100;14000:BACK:100;17000:QUIT', shots=shots)
    assert 'Entering activity: FileBrowser' in log, log[-3000:]
    assert 'Entering activity: EpubReader' in log, log[-3000:]
    assert 'General failure' not in log
    assert hashes(preserved) == source
    active = sd / ('.yacp-reading-v1' if failure else '.crosspoint')
    if failure:
        assert 'using isolated reading data' in log
        assert not (sd / '.yacp-migration-v1/complete').exists()
        if failure == 'backup':
            assert not (sd / '.yacp-source-backup/original').exists()
            assert (pending / 'settings.json').read_bytes() == b'incompatible partial data'
    else:
        assert (sd / '.yacp-migration-v1/complete').read_bytes() == b'YACPMIG\1'
    positions = {str(p.relative_to(active)): p.read_bytes() for p in active.glob('epub_*/progress.bin')}
    assert positions and any(progress_record(data).get('visible_text_offset', 0) > 100
                             for data in positions.values()), positions
    settings_file = active / 'settings.json'
    settings = json.loads(settings_file.read_text())
    settings['showHiddenFiles'] = 1
    settings_file.write_text(json.dumps(settings), encoding='utf-8')
    log2 = boot(program, output, actions='1800:CONFIRM:100;6000:QUIT',
                shots=f'4500:{unix(output)}/reopened.bmp')
    assert 'Entering activity: EpubReader' in log2
    assert all((active / path).read_bytes() == data for path, data in positions.items())
    assert json.loads(settings_file.read_text())['showHiddenFiles'] == 1
    assert hashes(preserved) == source
    if failure:
        assert 'Using isolated reading data' in log2
    return dict(import_failed=failure, library_opened=True, book_opened=True,
                new_position_persisted=True, preferences_persisted=True, original_hashes_preserved=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    os.environ.setdefault('YACP_WSL_DISTRO', 'YACP-Simulator')
    results = [exercise(args.program, args.output / name, failed)
               for name, failed in (('success', False), ('failed-settings', 'settings'), ('failed-backup', 'backup'))]
    (args.output / 'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))
