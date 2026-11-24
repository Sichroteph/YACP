"""Exercise the production bounded manifest parser, optionally against a copied real EPUB."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile


def run(output, book=None):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    exe = output / 'reference-pages'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-Ilib/JsonParser',
                    'test/yacp_reference_pages/ReferencePages.cpp', 'lib/JsonParser/ReferencePageParser.cpp',
                    'lib/JsonParser/StreamingJsonParser.cpp', '-o', str(exe)], cwd=root, check=True)
    checks = 0

    def read(value, index=0, chunk=1):
        nonlocal checks
        checks += 1
        text = value if isinstance(value, str) else json.dumps(value)
        result = subprocess.run([str(exe), str(chunk), str(index)], input=text, text=True,
                                capture_output=True, check=True)
        assert not result.stderr, result.stderr
        return json.loads(result.stdout)

    base = dict(format='x-locations', version=1, totalLocations=10, totalWords=3000,
                spine=[dict(index=0, startLocation=1, endLocation=10, wordStart=0, wordCount=3000)])
    expected = [0, 3000, 3000, 1500, 2, 1, 1, 1, 2, 2, 2, 2]
    for chunk in (1, 7, 1024, 65536):
        assert read(base, chunk=chunk) == expected
    words = copy.deepcopy(base)
    words.update(format='crossink-locations', wordsPerReferencePage=500)
    assert read(words)[:5] == [0, 3000, 3000, 500, 6]
    chars = copy.deepcopy(words)
    chars.update(totalCharacters=15000, charactersPerReferencePage=1000, totalReferencePages=15)
    chars['spine'][0].update(characterStart=2000, characterCount=12000)
    assert read(dict(reversed(list(chars.items()))))[:5] == [2000, 12000, 15000, 1000, 15]
    chars['extra'] = {'spine': [{'index': 0}], 'numeric': -1.25e2, 'bool': True, 'nothing': None}
    assert read(chars)[:5] == [2000, 12000, 15000, 1000, 15]
    del words['spine'][0]['index']
    assert read(words)[:5] == [0, 3000, 3000, 500, 6]
    for key, value in [('version', 2), ('version', None), ('format', 'future'), ('totalWords', 0),
                       ('totalWords', 4294967296), ('totalWords', -1), ('totalWords', '3000'),
                       ('totalWords', 3.5), ('spine', [None]), ('spine', []), ('spine', {})]:
        bad = copy.deepcopy(base)
        bad[key] = value
        assert read(bad) is None, (key, value)
    for key, value in [('wordStart', 3000), ('wordCount', 3001), ('endLocation', 11),
                       ('startLocation', 0), ('index', 1), ('wordCount', None)]:
        bad = copy.deepcopy(base)
        bad['spine'][0][key] = value
        assert read(bad) is None, (key, value)
    text = json.dumps(base)
    for bad in (text[:-1], text + '{}', text + 'x', text.replace(', "version"', ' "version"'),
                text.replace('"version": 1', '"version": 01'), text.replace('"version": 1', '"version": 1,'),
                text.replace('"version": 1', '"version": 1, "version": 1'),
                text.replace('"wordStart": 0', '"wordStart": 0, "wordStart": 0'),
                text.replace('3000', '3e+'), text.replace('3000', '3.'),
                text[:-1] + ',}', text.replace('10,', '10],', 1), '[' + text + ']'):
        assert read(bad) is None, bad
    duplicate = copy.deepcopy(base)
    duplicate['spine'] *= 2
    assert read(duplicate) is None
    assert read(base, index=2) is None
    real_entries = 0
    if book:
        original = book.read_bytes()
        with ZipFile(book) as epub:
            manifest = epub.read('META-INF/x-locations.json')
        document = json.loads(manifest)
        use_chars = bool(document.get('totalCharacters') and document.get('charactersPerReferencePage'))
        total = document['totalCharacters' if use_chars else 'totalWords']
        units = document['charactersPerReferencePage'] if use_chars else document.get('wordsPerReferencePage', 1500)
        pages = document.get('totalReferencePages', (total + units - 1) // units)
        for ordinal, entry in enumerate(document['spine']):
            index = entry.get('index', ordinal)
            start = entry.get('characterStart' if use_chars else 'wordStart', 0)
            count = entry.get('characterCount' if use_chars else 'wordCount', 0)
            actual = read(manifest.decode(), index=index, chunk=1024)
            if count:
                assert actual[:5] == [start, count, total, units, pages], ordinal
                assert actual[5:] == [min((start + int(max(0, min(1, f)) * count)) // units + 1, pages)
                                      for f in (-1, 0, .25, .5, .75, 1, 2)]
            else:
                assert actual is None
            real_entries += 1
        assert book.read_bytes() == original
    result = dict(passed=True, checks=checks, real_chapters=real_entries,
                  sanitizers=['address', 'undefined'], source_unchanged=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--book', type=Path)
    args = p.parse_args()
    run(args.output, args.book)
