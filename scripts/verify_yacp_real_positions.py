"""Validate reviewed real-book anchors on isolated cards, using converted settings/profiles."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import struct
import subprocess

from yacp_book_profile import DEFAULTS, encode
from yacp_data_formats import crosspoint48_page_offsets, progress_record
from yacp_font_catalog import verified_catalog


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(program, snapshot, output, board, indices=None, anchor_file=None):
    program = program.resolve(strict=True)
    snapshot = snapshot.resolve(strict=True)
    output = output.absolute()
    if output.exists() or 'nextcloud' in {part.casefold() for part in output.parts}:
        raise ValueError('Use a new local experiment directory')
    if output == snapshot or snapshot in output.parents or output in snapshot.parents:
        raise ValueError('Source and output must be separate trees')
    output.mkdir(parents=True)
    root = Path(__file__).resolve().parents[1]
    hasher = output/'cache-identity'
    subprocess.run(['g++','-std=c++17',str(root/'test/yacp_statistics/CacheIdentity.cpp'),'-o',str(hasher)],check=True)
    anchor_file = anchor_file or snapshot/'anchor-candidates-v4.json'
    anchors = json.loads(anchor_file.read_text(encoding='utf-8'))['entries']
    settings = json.loads((snapshot/'settings-candidate-v25/candidate-settings.json').read_text())
    profiles = json.loads((snapshot/'settings-candidate-v25/book-settings-candidates.json').read_text())
    verified_catalog(snapshot/'fonts')
    root_manifest = {e['path']:e['sha256'] for e in json.loads((snapshot/'reference-books-manifest.json').read_text())}
    read_manifest = {e['path']:e['sha256'] for e in json.loads((snapshot/'read-books/source-manifest.json').read_text())['files']}

    def scenario(index, entry):
        folder = output/str(index)
        folder.mkdir()
        path = PurePosixPath(entry['book_path'])
        if not path.is_absolute() or '..' in path.parts: raise ValueError('Unsafe book path')
        if len(path.parts) == 2:
            source = snapshot/'reference-books'/path.name
            expected = root_manifest[path.name]
        elif len(path.parts) == 3 and path.parts[1] == 'Read':
            source = snapshot/'read-books/original'/path.name
            expected = read_manifest[path.name]
        else: raise ValueError('Book source not resolved')
        assert digest(source) == expected, 'Book copy differs from its manifest'
        sd = folder/'sd'
        cache_name = subprocess.check_output([str(hasher),str(path)],text=True).strip()
        config = sd/'.crosspoint'
        cache = config/cache_name
        cache.mkdir(parents=True)
        target = sd/str(path).lstrip('/')
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
        # Language and clocks affect test interaction, not layout. No credentials
        # or networking stores are copied into these isolated rendering cards.
        chosen = dict(settings,language='EN',trackReadingStats=0,clockEnabled=0,
                      sleepTimeoutMinutes=30,moveFinishedToReadFolder=0)
        book_profiles = [p['candidate'] for p in profiles if p['source_path'].split('/')[0] == entry['cache']]
        families = {chosen.get('sdFontFamilyName','')}
        if book_profiles:
            assert len(book_profiles) == 1 and not book_profiles[0]['blocked_keys']
            bp = book_profiles[0]
            values = dict(DEFAULTS, **{k:v for k,v in chosen.items() if k in DEFAULTS})
            values.update(bp['settings'])
            directory = config/'book_profiles'/cache_name
            directory.mkdir(parents=True)
            (directory/'reader_profile.0').write_bytes(encode(values, flags=bp['profile_flags']))
            families.add(values.get('sdFontFamilyName',''))
        for family in families - {''}:
            if Path(family).name != family: raise ValueError('Unsafe font family')
            shutil.copytree(snapshot/'fonts/original'/family,sd/'.fonts'/family)
        (config/'settings.json').write_text(json.dumps(chosen))
        (config/'state.json').write_text('{"openEpubPath":""}')
        (config/'recent.json').write_text(json.dumps({'books':[{'path':str(path),'title':path.stem,'author':'','coverBmpPath':''}]}))
        spine = entry['progress']['spine']
        offset = entry['candidate_offset']
        (cache/'progress.bin').write_bytes(struct.pack('<HHHI',spine,0,0,offset))
        # The copied YACP uses a custom physical front-button order. Exercise
        # that actual mapping instead of assuming the simulator defaults.
        hardware = ('BACK', 'CONFIRM', 'LEFT', 'RIGHT')
        confirm = hardware[chosen.get('frontButtonConfirm', 1)]
        back = hardware[chosen.get('readerFrontButtonBack', 0) if chosen.get('readerFrontButtonsEnabled')
                        else chosen.get('frontButtonBack', 0)]
        actions = (f'1800:{confirm}:150;27000:{back}:150;29000:{back}:150;32000:QUIT' if board == 'x3' else
                   f'1800:TAP:240,350;27000:HOME:100;29000:{back}:100;32000:QUIT')
        env = dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_RENDER_DRIVER='software',
                   CROSSPOINT_SIM_SD=str(sd),CROSSPOINT_SIM_INPUT_SCRIPT=actions,
                   CROSSPOINT_SIM_SCREENSHOTS=f'24000:{folder}/passage.bmp;30500:{folder}/home.bmp')
        result = dict(book_path=str(path),source_cache=entry['cache'],target_offset=offset,
                      source_spine=spine,board=board,hardware_validated=False)
        try:
            with (folder/'run.log').open('x') as log:
                subprocess.run([str(program)],cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=50)
            trace = (folder/'run.log').read_text()
            assert 'Entering activity: EpubReader' in trace, 'Reader did not open through the configured input mapping'
            assert 'Exiting activity: EpubReader' in trace, 'Reader did not exit through the configured input mapping'
            progress = progress_record((cache/'progress.bin').read_bytes())
            assert progress['spine'] == spine, progress
            offsets = crosspoint48_page_offsets((cache/f'sections/{spine}.bin').read_bytes())
            page = progress['page']
            assert 0 <= page < len(offsets)
            assert offsets[page] <= offset and (page+1 == len(offsets) or offset < offsets[page+1]), (progress,offset,offsets)
            assert progress['visible_text_offset'] == offsets[page], progress
            assert digest(source) == expected == digest(target)
            result.update(passed=True,page=page,resumed_offset=offsets[page],book_unchanged=True)
        except (AssertionError, OSError, subprocess.SubprocessError, ValueError) as error:
            result.update(passed=False,error=str(error))
        (folder/'result.json').write_text(json.dumps(result,indent=2))
        return result

    candidates = [entry for entry in anchors if 'candidate_offset' in entry]
    selected = [(index, entry) for index, entry in enumerate(candidates) if indices is None or index in indices]
    if not selected: raise ValueError('No matching anchor candidates')
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda pair: scenario(*pair),selected))
    report = dict(passed=all(r['passed'] for r in results),cases=results,
                  anchor_file=str(anchor_file),anchor_sha256=digest(anchor_file),
                  unresolved_source_positions=len(anchors)-len(candidates),full_migration_validated=False)
    (output/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(passed=report['passed'],cases=len(results),failures=sum(not r['passed'] for r in results),
                          unresolved=report['unresolved_source_positions'])))
    return report['passed']


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program',type=Path)
    parser.add_argument('--snapshot',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--board',choices=('x3','pro'),default='x3')
    parser.add_argument('--indices',type=int,nargs='+')
    parser.add_argument('--anchor-file',type=Path)
    args=parser.parse_args()
    raise SystemExit(0 if run(args.program,args.snapshot,args.output,args.board,args.indices,args.anchor_file) else 1)
