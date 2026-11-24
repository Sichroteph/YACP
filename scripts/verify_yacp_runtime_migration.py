"""Exercise the firmware importer itself on private copies, including interrupted boots."""
import argparse, hashlib, json, os, shutil, shlex, struct, subprocess, sys
from pathlib import Path
from zipfile import ZipFile
from yacp_data_formats import fnv64_book_path, esp32_book_path, progress_record
from yacp_settings_defaults import global_candidate
from yacp_book_settings_migration import book_candidate
from yacp_book_profile import DEFAULTS, encode
from yacp_legacy_anchor import legacy_section_name, first_cached_line, unique_text_offset
from yacp_bookmark_migration import epub_chapters
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'test'))
from test_yacp_bookmark_migration import legacy, section_v44


def unix(path):
    path=path.resolve()
    return '/mnt/'+path.drive[0].lower()+path.as_posix()[2:]


def hashes(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}


def boot(program, output, phase=None, actions='3000:QUIT', shots='', extra_env=None):
    env=['env','SDL_VIDEODRIVER=offscreen','CROSSPOINT_SIM_SD='+unix(output/'sd'),
         'CROSSPOINT_SIM_INPUT_SCRIPT='+actions,'CROSSPOINT_SIM_SCREENSHOTS='+shots]
    if phase:env+=['YACP_MIGRATION_STOP_AFTER='+phase]
    env += [key+'='+value for key,value in (extra_env or {}).items()]
    index=len(list(output.glob('run-*.log')))
    log=output/f'run-{index}.log'
    with log.open('x',encoding='utf-8') as stream:
        result=subprocess.run(['wsl','-d',os.environ.get('YACP_WSL_DISTRO', 'Ubuntu'),'--','bash','-lc',shlex.join(env+[unix(program)])],stdout=stream,stderr=subprocess.STDOUT,timeout=100)
    assert result.returncode==(77 if phase else 0),(result.returncode,log)
    return log.read_text(encoding='utf-8')


def synthetic(output, large=False):
    sd=output/'sd';cp=sd/'.crosspoint';cp.mkdir(parents=True)
    (cp/'crossink-settings.json').write_text(json.dumps(dict(buttonLayoutPromptVersion='1.8.1-yacp.6-tiny',
        uiTheme=6,fontFamily=0,fontSize=1,refreshFrequency=0,refreshAction=1,language='EN')),encoding='utf-8')
    (cp/'state.json').write_text(json.dumps(dict(openEpubPath='/Book2.epub')))
    for v in (2,3,4,5):
        path=f'/Book{v}.epub'
        with ZipFile(sd/path[1:],'w') as z:
            z.writestr('mimetype','application/epub+zip')
            z.writestr('META-INF/container.xml','<?xml version="1.0"?><container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            z.writestr('book.opf','<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Migration test</dc:title><dc:language>en</dc:language><dc:identifier id="id">test</dc:identifier></metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="c"/></spine></package>')
            z.writestr('c.xhtml','<html xmlns="http://www.w3.org/1999/xhtml"><body><p>Earlier sentence</p><p>Target sentence</p></body></html>')
        cache=cp/fnv64_book_path(path);(cache/'sections').mkdir(parents=True)
        (cache/'sections/0.bin').write_bytes(section_v44());(cache/'progress.bin').write_bytes(struct.pack('<HHH',0,1,2))
        (cp/'bookmarks').mkdir(exist_ok=True)
        (cp/f'bookmarks/legacy{v}.bin').write_bytes(legacy(v,path=path,count=1024 if large and v==5 else 1))
    return hashes(cp)


def validate_synthetic(output, before):
    sd=output/'sd';assert hashes(sd/'.yacp-migration-v1/original')==before
    active=sd/'.crosspoint'
    for v in (2,3,4,5):
        data=json.loads((active/f'bookmarks/Book{v}.json').read_text())
        assert all(e['vo']==len('Earlier sentence') and e['si']==0 for e in data['bookmarks'])
    for p in active.glob('epub_*/progress.bin'):
        assert progress_record(p.read_bytes())['visible_text_offset']==len('Earlier sentence')
    assert json.loads((active/'settings.json').read_text())['fontSize']==12


def fault_suite(program, root):
    results=[]
    for phase in ('original','settings','prepared','activated'):
        out=root/phase; before=synthetic(out,large=phase=='prepared')
        boot(program,out,phase)
        sd=out/'sd';active=sd/'.crosspoint'
        if phase=='settings':
            partial=sd/'.yacp-migration-v1/staging/settings.json'
            partial.write_bytes(partial.read_bytes()[:41])
        if phase=='prepared':
            # Simulate a truncated deterministic bookmark fragment before activation.
            p=next((sd/'.yacp-migration-v1/staging/migration-bookmark-staging').iterdir())
            p.write_bytes(p.read_bytes()[:27])
        if phase=='activated':
            (sd/'.yacp-migration-v1/complete').write_bytes(b'YAC')
        boot(program,out)
        assert (sd/'.yacp-migration-v1/complete').read_bytes()==b'YACPMIG\1'
        validate_synthetic(out,before)
        saved=hashes(active)
        boot(program,out)
        # Startup can update runtime state; migration records and book positions must stay unchanged.
        after=hashes(active)
        assert all(after.get(k)==v for k,v in saved.items() if k.startswith(('migration-','bookmarks/','epub_')))
        results.append(phase)
    # Actual build-314 failure: the root exists, but intent was never written.
    out=root/'empty-existing-root';before=synthetic(out)
    (out/'sd/.yacp-migration-v1').mkdir()
    boot(program,out)
    validate_synthetic(out,before)
    assert (out/'sd/.yacp-migration-v1/complete').read_bytes()==b'YACPMIG\1'
    results.append('empty-existing-root')
    # An existing regular file must never be accepted as a directory.
    out=root/'directory-collision';before=synthetic(out)
    (out/'sd/.yacp-migration-v1').write_bytes(b'occupied')
    log=boot(program,out)
    assert 'Migration paused' in log
    assert hashes(out/'sd/.crosspoint')==before
    assert (out/'sd/.yacp-migration-v1').read_bytes()==b'occupied'
    results.append('directory-collision-preserved')
    verify_source_backups(program, root / 'source-backups')
    (root/'fault-result.json').write_text(json.dumps(dict(passed=True,interrupted_phases=results,bookmarks_versions=[2,3,4,5],large_bookmark_count=1024)))
    print('Firmware fault suite passed:',results)


def verify_source_backups(program, root):
    for filename, settings in (
        ('settings.json', {'language': 'EN', 'screenMargin': 12, 'fontSize': 16}),
        ('crossink-settings.json', {'buttonLayoutPromptVersion': '1.6.1-crossink',
                                    'language': 'EN', 'screenMargin': 12, 'fontSize': 16,
                                    'sleepScreen': 9, 'uiTheme': 7}),
    ):
        out = root / filename
        sd = out / 'sd'
        active = sd / '.crosspoint'
        nested = active / 'epub_fixture'
        nested.mkdir(parents=True)
        (nested / 'empty').mkdir()
        (nested / 'progress.bin').write_bytes(bytes(range(256)) * 9)
        (active / filename).write_text(json.dumps(settings), encoding='utf-8')
        before = hashes(active)
        pending = sd / '.yacp-source-backup/pending'
        archive = pending.with_name('original')

        boot(program, out, 'source-backup-copied')
        assert hashes(active) == hashes(pending) == before
        assert not archive.exists()
        partial = pending / 'epub_fixture/progress.bin'
        partial.write_bytes(partial.read_bytes()[:1051])
        boot(program, out, 'source-backup-published')
        assert hashes(active) == hashes(archive) == before
        assert (archive / 'epub_fixture/empty').is_dir() and not pending.exists()
        (nested / 'progress.bin').write_bytes(b'changed during trial')
        boot(program, out)
        assert hashes(archive) == before
        user_settings = json.loads((active / 'settings.json').read_bytes())
        assert user_settings['screenMargin'] == 12 and user_settings['fontSize'] == 16
        if filename == 'crossink-settings.json':
            assert user_settings['sleepScreen'] == 6 and user_settings['uiTheme'] == 4
            assert json.loads((active / filename).read_bytes()) == settings

    out = root / 'existing-yacp'
    sd = out / 'sd'
    active = sd / '.crosspoint'
    active.mkdir(parents=True)
    (active / 'settings.json').write_text('{"yacpReaderFontInitialized": true, "screenMargin": 12}', encoding='utf-8')
    (active / 'crossink-settings.json').write_text('{"screenMargin": 3}', encoding='utf-8')
    previous = sd / '.yacp-source-backup/original'
    previous.mkdir(parents=True)
    (previous / 'settings.json').write_text('{"screenMargin": 2}', encoding='utf-8')
    previous_hashes = hashes(previous)
    expected = hashes(active)
    boot(program, out, 'source-backup-copied')
    archive = previous.with_name('pre-crosspoint-2.0')
    assert hashes(archive.with_suffix('.0.pending')) == expected and not archive.exists()
    boot(program, out, 'source-backup-published')
    assert hashes(archive) == expected
    boot(program, out)
    assert json.loads((active / 'settings.json').read_bytes())['screenMargin'] == 12
    assert hashes(previous) == previous_hashes and hashes(archive) == expected

    out = root / 'native-fresh'
    (out / 'sd').mkdir(parents=True)
    boot(program, out)
    boot(program, out)
    assert not (out / 'sd/.yacp-source-backup/pre-crosspoint-2.0').exists()


def real(program, source, output):
    output.mkdir(parents=True)
    def ignored(directory,names):
        return [n for n in names if Path(directory)==source and (n=='dictionaries' or n.endswith('.bin') or n.startswith('._'))]
    shutil.copytree(source,output/'sd',ignore=ignored)
    sd=output/'sd';cp=sd/'.crosspoint'
    p=cp/'crossink-settings.json';settings=json.loads(p.read_text(encoding='utf-8'));settings['language']='EN'
    p.write_text(json.dumps(settings),encoding='utf-8')
    before=hashes(cp)
    boot(program,output,'activated')
    original=sd/'.yacp-migration-v1/original'; assert hashes(original)==before
    sizes={p.name:sorted({int(f.stem.rsplit('_',1)[1]) for f in p.glob('*.cpfont')}) for p in (sd/'.fonts').iterdir() if p.is_dir()}
    expected=global_candidate(settings,'tiny',sizes)
    assert not expected['blocked_keys'],expected['blocked_keys']
    actual=json.loads((cp/'settings.json').read_text())
    assert all(actual.get(k)==v for k,v in expected['settings'].items()),{k:(v,actual.get(k)) for k,v in expected['settings'].items() if actual.get(k)!=v}
    positions=profiles=0
    congo=None
    for pin in cp.glob('epub_*/migration-source-path'):
        book=pin.read_text(encoding='utf-8');src=original/fnv64_book_path(book)
        if not src.exists():src=original/esp32_book_path(book)
        profile=(src/'reader_settings.bin').read_bytes() if (src/'reader_settings.bin').exists() else None
        if profile:
            candidate=book_candidate(profile,settings,'tiny',sizes)
            assert not candidate['blocked_keys']
            values=dict(DEFAULTS,**{k:v for k,v in expected['settings'].items() if k in DEFAULTS});values['sdFontFamilyName']=expected['settings'].get('sdFontFamilyName','');values.update(candidate['settings'])
            blob=encode(values,flags=candidate['profile_flags'])
            assert (cp/'book_profiles'/pin.parent.name/'reader_profile.0').read_bytes()==blob,book
            profiles+=1
        if not (src/'progress.bin').exists():continue
        old=progress_record((src/'progress.bin').read_bytes());new=progress_record((pin.parent/'progress.bin').read_bytes())
        offset=old.get('visible_text_offset',0)
        if 'visible_text_offset' not in old and old['page']:
            cache=src/'sections'/legacy_section_name(old['spine'],profile)
            text=first_cached_line(cache.read_bytes(),old['page'])
            html=epub_chapters(sd/book[1:])[old['spine']][0]
            offset=unique_text_offset(html,text)
        assert new['spine']==old['spine'] and new['visible_text_offset']==offset,book
        if 'Congo' in book:congo=dict(path=book,spine=new['spine'],offset=offset,cache=pin.parent.name)
        positions+=1
        for stat in src.glob('stats*.bin*'):
            assert (pin.parent/stat.name).read_bytes()==stat.read_bytes()
    assert congo and positions==11 and profiles==5,(congo,positions,profiles)
    for name in ('global_stats.bin','daily_reading.bin','finished_books.bin'):
        if (original/name).exists():assert (cp/name).read_bytes()==(original/name).read_bytes()
    result=dict(passed=True,settings=len(expected['settings']),positions=positions,profiles=profiles,congo=congo,originals_unchanged=True,hardware_validated=False)
    (output/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('program',type=Path);p.add_argument('output',type=Path);p.add_argument('--source',type=Path)
    a=p.parse_args();assert not a.output.exists() and 'nextcloud' not in str(a.output).lower()
    if a.source:real(a.program,a.source,a.output)
    else:fault_suite(a.program,a.output)
