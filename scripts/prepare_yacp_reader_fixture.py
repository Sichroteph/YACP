"""Prepare one isolated native-simulator reader fixture, not an SD migration."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess

from yacp_data_formats import font_point_size
from yacp_font_catalog import verified_catalog


def prepare(snapshot, anchors, book_path, output, hash_program, variant):
    snapshot = snapshot.resolve(strict=True)
    output = output.resolve()
    if output.exists() or 'nextcloud' in {part.casefold() for part in output.parts}:
        raise ValueError('Use a new local test directory outside Nextcloud')
    if output == snapshot or snapshot in output.parents or output in snapshot.parents:
        raise ValueError('Source and fixture must be separate')
    relative = PurePosixPath(book_path)
    if not relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Expected an absolute, normalized SD book path')
    records = json.loads(anchors.read_text(encoding='utf-8'))['entries']
    candidates = [e for e in records if e.get('book_path') == book_path and 'candidate_offset' in e]
    if len(candidates) != 1:
        raise ValueError('Exactly one reviewed position candidate is required')
    candidate = candidates[0]
    source_data = snapshot / 'crosspoint/original'
    source_settings = json.loads((source_data / 'crossink-settings.json').read_text(encoding='utf-8'))
    native_cache = subprocess.check_output([str(hash_program.resolve(strict=True)), book_path], text=True).strip()
    if not re.fullmatch(r'epub_[0-9]+', native_cache):
        raise ValueError('Invalid native cache identifier')
    if len(relative.parts) != 2:
        raise ValueError('This reader fixture currently accepts root-level reference books only')
    source_book = snapshot / 'reference-books' / relative.name
    if not source_book.is_file():
        raise ValueError('Source reference book is missing')
    family = source_settings.get('sdFontFamilyName', '')
    if family and (Path(family).name != family or '/' in family or '\\' in family or family in ('.', '..')):
        raise ValueError('Invalid font family path')
    font_root = snapshot / 'fonts/original' / family
    if family and not font_root.is_dir():
        raise ValueError('SD font family was not captured')
    catalog, _ = verified_catalog(snapshot / 'fonts') if family else ({}, None)
    settings = {'language': 'EN', 'uiTheme': 5, 'fontSize': font_point_size(source_settings, variant, catalog.get(family)),
                'fontFamily': source_settings.get('fontFamily', 0), 'sdFontFamilyName': family,
                'trackReadingStats': 0, 'sleepTimeoutMinutes': 30}
    # These enum/value meanings were compared in both source headers. This is
    # a rendering experiment, not the complete settings importer.
    for key in ('paragraphAlignment', 'embeddedStyle', 'screenMargin', 'textAntiAliasing',
                'extraParagraphSpacing', 'hyphenationEnabled'):
        if key in source_settings:
            settings[key] = source_settings[key]
    output.mkdir(parents=True)
    sd = output / 'sd'
    cache = sd / '.crosspoint' / native_cache
    cache.mkdir(parents=True)
    shutil.copy2(source_book, sd / relative.name)
    if family:
        shutil.copytree(font_root, sd / '.fonts' / family)
    config = sd / '.crosspoint'
    (config / 'settings.json').write_text(json.dumps(settings), encoding='utf-8')
    (config / 'state.json').write_text('{"openEpubPath":""}', encoding='utf-8')
    (config / 'recent.json').write_text(json.dumps({'books': [{'path': book_path, 'title': relative.stem,
            'author': '', 'coverBmpPath': ''}]}), encoding='utf-8')
    progress = candidate['progress']
    (cache / 'progress.bin').write_bytes(struct.pack('<HHHI', progress['spine'], 0, 0,
                                                    candidate['candidate_offset']))
    copied = []
    for file in sorted(sd.rglob('*')):
        if file.is_file():
            copied.append({'path': file.relative_to(sd).as_posix(),
                           'sha256': hashlib.sha256(file.read_bytes()).hexdigest()})
    report = {'purpose': 'isolated native-simulator experiment', 'source_book': book_path,
              'native_cache': native_cache, 'candidate': candidate, 'settings': settings, 'files': copied,
              'device_cache_id_validated': False, 'firmware_navigation_validated': False,
              'migration_validated': False}
    (output / 'fixture.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'prepared': True, 'font_size': settings['fontSize'], 'native_cache': native_cache,
                      'spine': progress['spine'], 'migration_validated': False}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--anchors', required=True, type=Path)
    parser.add_argument('--book-path', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--hash-program', required=True, type=Path)
    parser.add_argument('--source-variant', required=True, choices=('tiny', 'xlarge'))
    args = parser.parse_args()
    prepare(args.snapshot, args.anchors, args.book_path, args.output, args.hash_program, args.source_variant)
