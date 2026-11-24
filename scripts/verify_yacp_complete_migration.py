"""Boot a migrated native SD copy and verify the real current book and stores."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from yacp_data_formats import book_stats, daily_history, crosspoint48_page_offsets, progress_record


def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def run(program, migration, output, board, inspect_existing=False):
    program=program.resolve(strict=True)
    migration=migration.resolve(strict=True)
    output=output.absolute()
    if (output.exists() and not inspect_existing) or 'nextcloud' in {p.casefold() for p in output.parts}:
        raise ValueError('Use a new local verification directory')
    if migration in output.parents or output in migration.parents:
        raise ValueError('Keep experiment and prepared migration separate')
    completion=json.loads((migration/'COMPLETE.json').read_text(encoding='utf-8'))
    intent=json.loads((migration/'intent.json').read_text(encoding='utf-8'))
    assert completion['prepared_copy_verified'] and completion['intent_sha256']==digest(migration/'intent.json')
    card=migration/'card'
    report=json.loads((card/'.yacp-migration/report.json').read_text(encoding='utf-8'))
    assert report['target_cache']=='native-simulator'
    if not inspect_existing:output.mkdir(parents=True)
    sd=output/'sd'
    # Archives, dictionaries and old binaries are preserved in the verified
    # prepared card. Ordinary EPUB acceptance needs books, fonts, images and
    # all converted runtime stores, not a second copy of these inactive assets.
    def ignored(directory,names):
        if Path(directory)==card:
            return [n for n in names if n in ('.yacp-migration','dictionaries') or n.endswith('.bin') or n.startswith('._')]
        return []
    if not inspect_existing:shutil.copytree(card,sd,ignore=ignored)
    config=sd/'.crosspoint'
    original_config=card/'.crosspoint'
    state=json.loads((original_config/'state.json').read_text(encoding='utf-8'))
    book_path=state['openEpubPath']
    selected=next(b for b in report['books'] if b['path']==book_path)
    assert selected['progress_migrated']
    before_book=digest(card/book_path.lstrip('/'))
    original_settings=json.loads((original_config/'settings.json').read_text(encoding='utf-8'))
    settings=dict(original_settings,language='EN',trackReadingStats=0,clockEnabled=0,sleepTimeoutMinutes=30,moveFinishedToReadFolder=0)
    if not inspect_existing:(config/'settings.json').write_text(json.dumps(settings),encoding='utf-8')
    protected={str(p.relative_to(original_config)):digest(p) for p in original_config.rglob('*') if p.is_file() and
               (p.name.startswith(('stats','global_stats','daily_reading','finished_books','reader_profile')))}
    original_recents=json.loads((original_config/'recent.json').read_text(encoding='utf-8'))
    front=('BACK','CONFIRM','LEFT','RIGHT')
    confirm=front[settings.get('frontButtonConfirm',1)]
    back=front[settings.get('readerFrontButtonBack',0) if settings.get('readerFrontButtonsEnabled') else settings.get('frontButtonBack',0)]
    actions=(f'2000:{confirm}:120;26000:{back}:120;28500:{back}:120;32000:QUIT' if board=='x3' else
             f'2000:TAP:240,350;26000:HOME:120;28500:{back}:120;32000:QUIT')
    env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_RENDER_DRIVER='software',CROSSPOINT_SIM_SD=str(sd),
             CROSSPOINT_SIM_INPUT_SCRIPT=actions,CROSSPOINT_SIM_SCREENSHOTS=f'23000:{output}/reader.bmp;30500:{output}/home.bmp')
    if not inspect_existing:
        with (output/'run.log').open('x') as log:
            subprocess.run([str(program)],cwd=output,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=55)
    trace=(output/'run.log').read_text(encoding='utf-8')
    assert 'Entering activity: EpubReader' in trace and 'Exiting activity: EpubReader' in trace
    cache=config/selected['target_cache']
    progress=progress_record((cache/'progress.bin').read_bytes())
    assert progress['spine']==selected['spine']
    offsets=crosspoint48_page_offsets((cache/f'sections/{progress["spine"]}.bin').read_bytes())
    page=progress['page']; anchor=selected['text_offset']
    assert offsets[page]<=anchor and (page+1==len(offsets) or anchor<offsets[page+1])
    assert progress['visible_text_offset']==offsets[page]
    after_settings=json.loads((config/'settings.json').read_text(encoding='utf-8'))
    assert all(after_settings.get(k)==v for k,v in settings.items()), {k:(v,after_settings.get(k)) for k,v in settings.items() if after_settings.get(k)!=v}
    normalized=[]
    for name,sha in protected.items():
        if digest(config/name)==sha:continue
        if name=='daily_reading.bin':
            old=daily_history((original_config/name).read_bytes())
            new=daily_history((config/name).read_bytes())
            assert old['version']==1 and new['version']==2
            assert old['minutes']==new['minutes'] and old['anchor_day']==new['anchor_day']
        elif name=='daily_reading.bin.bak':
            assert daily_history((original_config/'daily_reading.bin').read_bytes())['version']==1
            assert digest(config/name)==digest(original_config/'daily_reading.bin')
        elif Path(name).name.startswith('stats'):
            old=book_stats((original_config/name).read_bytes())
            new=book_stats((config/name).read_bytes())
            # Remaining time is recalculated for the newly paginated book;
            # historical durations, counts, dates and flags must stay identical.
            assert {k:v for k,v in old.items() if k not in ('version','estimated_time_left_seconds')}=={k:v for k,v in new.items() if k not in ('version','estimated_time_left_seconds')}
        else:
            raise AssertionError(name)
        normalized.append(name)
    assert digest(sd/book_path.lstrip('/'))==before_book
    after_recents=json.loads((config/'recent.json').read_text(encoding='utf-8'))
    assert len(after_recents['books'])==len(original_recents['books'])
    assert {b['path'] for b in after_recents['books']}=={b['path'] for b in original_recents['books']}
    for entry in intent['files']:
        assert digest(card/entry['path'])==entry['sha256'], 'Prepared migration was modified'
    result=dict(passed=True,board=board,current_book_resumed=True,protected_stores=len(protected),
                converted_settings_verified=len(settings),recent_books=len(after_recents['books']),
                validated_format_or_derived_updates=normalized,
                prepared_copy_unchanged=True,simulator_overrides={k:settings[k] for k in settings if original_settings.get(k)!=settings[k]},
                hardware_validated=False,installed=False)
    (output/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program',type=Path)
    parser.add_argument('--migration',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--board',choices=('x3','pro'),required=True)
    parser.add_argument('--inspect-existing',action='store_true',help='Inspect an already executed fixture without running or changing it')
    args=parser.parse_args()
    run(args.program,args.migration,args.output,args.board,args.inspect_existing)
