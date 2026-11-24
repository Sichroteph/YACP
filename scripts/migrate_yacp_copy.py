"""Assemble a restartable migrated SD COPY from a verified complete YACP snapshot.

Never writes the source or a mounted card. A new local directory contains the
prepared card, its manifest and an explicit review report. Unsupported data is
retained byte-for-byte under .yacp-migration/original-crosspoint on that copy.
No first-boot importer, flash or installation is performed by this tool.
"""
import argparse
import base64
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
from xml.etree.ElementTree import ParseError
from zipfile import BadZipFile

from prepare_yacp_migration import append_matching_json, copy_resumable, enumerate_files, sha256
from prepare_yacp_wifi_candidate import mac_bytes
from yacp_book_profile import DEFAULTS, encode as encode_profile
from yacp_book_settings_migration import book_candidate
from yacp_bookmark_migration import candidate as bookmark_candidate, target_path as bookmark_path
from yacp_credential_migration import decode_yacp_password, wifi_candidate, xor_bytes
from yacp_data_formats import (book_stats, bookmarks, daily_history, esp32_book_path,
                              finished_books, fnv64_book_path, global_stats, progress_record)
from yacp_legacy_anchor import (VisibleText, chapter_html, first_cached_line,
                               legacy_section_name, unique_text_offset)
from yacp_opds_migration import candidate as opds_candidate
from yacp_recent_books_migration import candidate as recent_candidate
from yacp_settings_defaults import global_candidate
from yacp_state_migration import candidate as state_candidate


def json_bytes(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2).encode('utf-8')


def safe_relative(name):
    path = PurePosixPath(name)
    if (not name or path.is_absolute() or str(path) != name or '\\' in name
            or any(p in ('.', '..') or ':' in p or p.endswith((' ', '.')) for p in path.parts)
            or any(ord(c) < 32 for c in name)):
        raise ValueError('Unsafe or noncanonical migration path')
    return path


def local_output(path, source):
    path = path.absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError('Linked output path')
    path = path.resolve()
    if ('nextcloud' in {p.casefold() for p in path.parts} or len(path.parts) < 3
            or path == source or path in source.parents or source in path.parents):
        raise ValueError('Use a separate local directory outside the snapshot and Nextcloud')
    if os.name == 'nt':
        import ctypes
        if ctypes.windll.kernel32.GetDriveTypeW(str(path.anchor)) != 3:
            raise ValueError('Output must be on a local fixed disk, not a reader or network drive')
    elif re.match(r'^/mnt/[a-z]/', str(path)):
        # WSL experiments use C:; never write a mounted removable card here.
        if not str(path).startswith('/mnt/c/'):
            raise ValueError('Use the local C: workspace or a Linux-local output directory')
    return path


def verified_snapshot(snapshot):
    snapshot = snapshot.resolve(strict=True)
    for path in (snapshot, *snapshot.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Linked snapshot path')
    manifest = json.loads((snapshot/'source-manifest.json').read_text(encoding='utf-8'))
    marker = json.loads((snapshot/'COPY-VERIFIED.json').read_text(encoding='utf-8'))
    if marker.get('copy_verified') is not True or marker.get('source_variant') != manifest.get('source_variant'):
        raise ValueError('A verified snapshot with an explicit matching variant is required')
    expected = sorted(manifest['files'], key=lambda e: e['path'])
    names = set()
    for entry in expected:
        safe_relative(entry['path'])
        key = entry['path'].casefold()
        if key in names or key.startswith('.yacp-migration/'):
            raise ValueError('Duplicate FAT path or previously migrated snapshot')
        names.add(key)
    if sorted(enumerate_files(snapshot/'original'), key=lambda e: e['path']) != expected:
        raise ValueError('Snapshot no longer matches its manifest')
    return manifest


def text_progress(data, file_size, index=None):
    """Recover an exact TXT byte offset, never reuse a page across layouts."""
    if len(data) == 6:
        page, offset = struct.unpack('<HI', data)
    elif len(data) == 4 and index is not None:
        page = struct.unpack_from('<H', data)[0]
        if (len(index) < 30 or index[:5] != b'ITXT\x03'
                or struct.unpack_from('<I', index, 5)[0] != file_size):
            raise ValueError('Unsupported or stale TXT index')
        count = struct.unpack_from('<I', index, 26)[0]
        if not count or len(index) != 30+4*count or page >= count:
            raise ValueError('Invalid TXT page index')
        offsets = struct.unpack_from(f'<{count}I', index, 30)
        if offsets[0] != 0 or any(a > b for a,b in zip(offsets,offsets[1:])) or offsets[-1] > file_size:
            raise ValueError('Invalid TXT byte offsets')
        offset = offsets[page]
    else:
        raise ValueError('No exact TXT text anchor')
    if offset > file_size:
        raise ValueError('TXT anchor outside source')
    return struct.pack('<HI', page, offset)


class Plan:
    def __init__(self, snapshot, manifest, target):
        self.root = snapshot/'original'
        self.entries = {e['path']: e for e in manifest['files']}
        self.files = {}
        self.folded = set()
        self.generated = {}
        self.report = dict(target_cache=target, source_variant=manifest['source_variant'],
                           converted=[], review=[], books=[], original_data_preserved=True,
                           hardware_validated=False, installed=False)

    def reserve(self, destination):
        safe_relative(destination)
        if destination.casefold() in self.folded:
            raise ValueError('Conflicting migration destinations')
        self.folded.add(destination.casefold())

    def copy(self, source, destination):
        self.reserve(destination)
        entry = self.entries[source]
        self.files[destination] = dict(path=destination, size=entry['size'], sha256=entry['sha256'], source=source)

    def write(self, destination, value):
        self.reserve(destination)
        data = value if isinstance(value, bytes) else json_bytes(value)
        self.generated[destination] = data
        self.files[destination] = dict(path=destination, size=len(data), sha256=hashlib.sha256(data).hexdigest())

    def read_json(self, name):
        return json.loads((self.root/name).read_text(encoding='utf-8-sig'))

    def issue(self, source, reason):
        # Reasons are fixed codes, never exception messages or credential values.
        self.report['review'].append(dict(source=source, reason=reason,
                                         original='/.yacp-migration/original-crosspoint/'+source.removeprefix('.crosspoint/')))

    def converted(self, name):
        self.report['converted'].append(name)


def build_plan(snapshot, manifest, source_key=None, target_key=None,
               cache_identity=esp32_book_path, target='esp32', source_cache_policy='conservative'):
    if source_cache_policy not in ('conservative','yacp-fnv64'):
        raise ValueError('Unsupported source cache policy')
    plan = Plan(snapshot, manifest, target)
    plan.report['source_cache_policy']=source_cache_policy
    cp = '.crosspoint/'
    for name in sorted(plan.entries):
        if name.startswith(cp):
            plan.copy(name, '.yacp-migration/original-crosspoint/'+name[len(cp):])
        else:
            plan.copy(name, name)
    if cp+'crossink-settings.json' not in plan.entries:
        raise ValueError('Snapshot does not contain YACP crossink-settings.json')
    try:
        legacy = plan.read_json(cp+'crossink-settings.json')
        if not isinstance(legacy,dict):
            raise ValueError('Invalid settings object')
    except (ValueError,UnicodeError):
        legacy = {}
        plan.issue(cp+'crossink-settings.json','unreadable_settings_using_reviewed_target_defaults')
    families = defaultdict(set)
    for name in plan.entries:
        parts = name.split('/')
        if len(parts) != 3 or parts[0] != '.fonts' or parts[2].startswith(('.', '_')):
            continue
        match = re.fullmatch(r'.+_([0-9]+)\.cpfont', parts[2])
        if match and 1 <= int(match[1]) <= 255:
            families[parts[1]].add(int(match[1]))
    families = {k: sorted(v) for k, v in families.items()}
    settings_report = global_candidate(legacy, manifest['source_variant'], families)
    settings = settings_report['settings']
    plan.write(cp+'settings.json', settings)
    plan.converted('settings')
    for key in settings_report['blocked_keys'] + settings_report['blocked_default_fields']:
        plan.issue(cp+'crossink-settings.json', 'setting_requires_review:'+key)

    state = {}
    if cp+'state.json' in plan.entries:
        try:
            result = state_candidate(plan.read_json(cp+'state.json'))
            state = result['state']
            for key in result['blocked_keys']:
                plan.issue(cp+'state.json', 'state_field_requires_review:'+key)
        except (ValueError, TypeError, KeyError):
            plan.issue(cp+'state.json', 'unreadable_state')
    # Directory indices are disposable anti-repeat history, not image choices.
    state.update(recentSleepImages=[0]*16, recentSleepPos=0, recentSleepFill=0,
                 recentOverlaySleepImages=[0]*16, recentOverlaySleepPos=0, recentOverlaySleepFill=0,
                 lastSleepFromReader=False, showBootScreen=True, sleepFrameInverted=False)
    plan.report['sleep_rotation'] = 'Reset temporary directory indices; images and converted settings retained'

    book_paths = ['/'+name for name in plan.entries if name.lower().endswith('.epub') and not name.startswith(cp)]
    identities = defaultdict(set)
    for path in book_paths:
        for identity in {fnv64_book_path(path), esp32_book_path(path)}:
            identities[identity].add(path)
    caches = sorted({name.split('/')[1] for name in plan.entries if re.match(r'^\.crosspoint/epub_[0-9]+/', name)})
    destinations = defaultdict(list)
    for cache in caches:
        matches = identities.get(cache, set())
        if len(matches) == 1:
            destinations[cache_identity(next(iter(matches)))].append(cache)
    resolved = {}
    progress_paths = set()
    for cache in caches:
        prefix = cp+cache+'/'
        matches = identities.get(cache, set())
        if len(matches) != 1:
            plan.issue(prefix, 'missing_or_ambiguous_book_identity')
            continue
        path = next(iter(matches))
        target_cache = cache_identity(path)
        aliases=destinations[target_cache]
        preferred=fnv64_book_path(path)
        if len(aliases)>1 and source_cache_policy=='yacp-fnv64' and preferred in aliases:
            if cache!=preferred:
                plan.report.setdefault('archived_inactive_caches',[]).append(cache)
                continue
            aliases=[preferred]
        if not re.fullmatch(r'epub_[0-9]+', target_cache) or len(aliases) != 1:
            plan.issue(prefix, 'colliding_book_caches')
            continue
        resolved[path] = cache
        entry = dict(path=path, source_cache=cache, target_cache=target_cache, progress_migrated=False)
        plan.report['books'].append(entry)
        target_prefix = cp+target_cache+'/'
        profile_name = prefix+'reader_settings.bin'
        if profile_name in plan.entries:
            try:
                profile = book_candidate((plan.root/profile_name).read_bytes(), legacy, manifest['source_variant'], families)
                if profile['blocked_keys']:
                    raise ValueError('Unconverted profile')
                values = dict(DEFAULTS, **{k:v for k,v in settings.items() if k in DEFAULTS})
                values.update(profile['settings'])
                plan.write(cp+'book_profiles/'+target_cache+'/reader_profile.0',
                           encode_profile(values, flags=profile['profile_flags']))
            except (ValueError, TypeError, KeyError, UnicodeError):
                plan.issue(profile_name, 'book_profile_requires_review')
        for name in sorted(plan.entries):
            if name.startswith(prefix) and re.fullmatch(r'stats(?:_v[0-9]+)?\.bin(?:\.bak)?', name[len(prefix):]):
                try:
                    book_stats((plan.root/name).read_bytes())
                    plan.copy(name, target_prefix+name[len(prefix):])
                except (ValueError, TypeError, struct.error):
                    plan.issue(name, 'book_statistics_require_review')
        progress_name = prefix+'progress.bin'
        if progress_name in plan.entries:
            try:
                progress = progress_record((plan.root/progress_name).read_bytes())
                html = chapter_html(plan.root/path.lstrip('/'), progress['spine'])
                if 'visible_text_offset' in progress:
                    offset = progress['visible_text_offset']
                elif progress['page'] == 0:
                    offset = 0
                else:
                    profile = (plan.root/profile_name).read_bytes() if profile_name in plan.entries else None
                    section_name = prefix+'sections/'+legacy_section_name(progress['spine'], profile)
                    if section_name not in plan.entries:
                        raise ValueError('Missing section')
                    line = first_cached_line((plan.root/section_name).read_bytes(), progress['page'])
                    offset = unique_text_offset(html, line)
                visible = VisibleText()
                visible.feed(html.replace('\r\n','\n').replace('\r','\n'))
                if not 0 <= offset <= len(''.join(visible.parts)):
                    raise ValueError('Text offset outside chapter')
                plan.write(target_prefix+'progress.bin', struct.pack('<HHHI', progress['spine'], 0, 0, offset))
                progress_paths.add(path)
                entry.update(progress_migrated=True, spine=progress['spine'], text_offset=offset)
            except (ValueError, TypeError, KeyError, UnicodeError, struct.error, BadZipFile, ParseError):
                plan.issue(progress_name, 'exact_reading_position_requires_review')

    fixed_paths = defaultdict(set)
    for name in plan.entries:
        extension = PurePosixPath(name).suffix.lower()
        kind = 'txt' if extension in ('.txt','.md') else 'xtc' if extension in ('.xtc','.xtch') else None
        if kind and not name.startswith(cp):
            for hasher in (fnv64_book_path,esp32_book_path):
                fixed_paths[hasher('/'+name).replace('epub_',kind+'_',1)].add('/'+name)
    fixed_caches = sorted({name.split('/')[1] for name in plan.entries if re.match(r'^\.crosspoint/(?:txt|xtc)_[0-9]+/',name)})
    fixed_targets = defaultdict(list)
    for cache in fixed_caches:
        matches = fixed_paths[cache]
        if len(matches)==1:
            destination=cache_identity(next(iter(matches))).replace('epub_',cache.split('_')[0]+'_',1)
            fixed_targets[destination].append(cache)
    for cache in fixed_caches:
        prefix=cp+cache+'/'
        matches=fixed_paths[cache]
        if len(matches)!=1:
            plan.issue(prefix,'missing_or_ambiguous_book_identity')
            continue
        path=next(iter(matches))
        kind=cache.split('_')[0]
        destination=cache_identity(path).replace('epub_',kind+'_',1)
        if len(fixed_targets[destination])!=1:
            plan.issue(prefix,'colliding_book_caches')
            continue
        name=prefix+'progress.bin'
        if name not in plan.entries:
            continue
        try:
            data=(plan.root/name).read_bytes()
            source=plan.root/path.lstrip('/')
            if kind=='txt':
                index=prefix+'index.bin'
                data=text_progress(data,source.stat().st_size,
                                   (plan.root/index).read_bytes() if index in plan.entries else None)
            else:
                with source.open('rb') as stream:header=stream.read(56)
                if (len(data)!=4 or len(header)!=56 or header[:4] not in (b'XTC\0',b'XTCH')
                        or struct.unpack('<I',data)[0] >= struct.unpack_from('<H',header,6)[0]):
                    raise ValueError('Invalid fixed-page position')
            plan.write(cp+destination+'/progress.bin',data)
            progress_paths.add(path)
            plan.report['books'].append(dict(path=path,source_cache=cache,target_cache=destination,progress_migrated=True,format=kind))
        except (ValueError,TypeError,struct.error):
            plan.issue(name,'exact_reading_position_requires_review')

    # These codecs and path keys were retained in the new firmware. Keep bytes
    # and backup priorities rather than adding totals or normalizing old dates.
    validators = {'global_stats.bin':global_stats, 'daily_reading.bin':daily_history,
                  'finished_books.bin':finished_books}
    handled = {cp+'crossink-settings.json', cp+'settings.json', cp+'state.json', cp+'recent.json',
               cp+'wifi.json', cp+'opds.json', cp+'koreader.json', cp+'sleep_frame.bin', cp+'home_carousel_cache.bin'}
    bookmark_owners = defaultdict(list)
    for name in plan.entries:
        if name.startswith(cp+'bookmarks/') and name.endswith('.bin'):
            try:
                path = bookmarks((plan.root/name).read_bytes())['path']
                bookmark_owners[bookmark_path(path).casefold()].append(name)
            except (ValueError,KeyError,UnicodeError,struct.error):
                pass
    for name in sorted(plan.entries):
        if not name.startswith(cp):
            continue
        leaf = name[len(cp):]
        base = leaf.removesuffix('.bak')
        decoder = validators.get(base)
        if leaf.startswith('synced_stats/') and leaf.endswith('.bin'):
            decoder = global_stats
        if decoder:
            handled.add(name)
            try:
                decoder((plan.root/name).read_bytes())
                plan.copy(name, name)
            except (ValueError, TypeError, struct.error):
                plan.issue(name, 'statistics_store_requires_review')
        elif leaf.startswith('bookmarks/') and leaf.endswith('.bin'):
            handled.add(name)
            try:
                raw = (plan.root/name).read_bytes()
                path = bookmarks(raw)['path']
                if path not in book_paths or len(bookmark_owners[bookmark_path(path).casefold()]) != 1:
                    raise ValueError('Missing bookmarked book')
                sections = {}
                if path in resolved:
                    prefix = cp+resolved[path]+'/'
                    profile_name = prefix+'reader_settings.bin'
                    profile = (plan.root/profile_name).read_bytes() if profile_name in plan.entries else None
                    for old in bookmarks(raw)['bookmarks']:
                        spine = old['spine']
                        section = prefix+'sections/'+legacy_section_name(spine, profile)
                        if section in plan.entries:
                            sections[spine] = (plan.root/section).read_bytes()
                converted = bookmark_candidate(raw, path, plan.root/path.lstrip('/'), sections)
                if converted['blocked_entries']:
                    raise ValueError('Unresolved bookmarks')
                plan.write(bookmark_path(path).lstrip('/'), converted['candidate'])
            except (ValueError, TypeError, KeyError, UnicodeError, struct.error, BadZipFile, ParseError):
                plan.issue(name, 'bookmarks_require_review')
        elif leaf.startswith('sleep_images/'):
            handled.add(name)
            plan.copy(name,name)
        elif name not in handled and not re.match(r'(?:epub|txt|xtc)_[0-9]+/',leaf):
            plan.issue(name,'unrecognized_or_excluded_store_retained')

    for store in ('wifi','opds'):
        name = cp+store+'.json'
        try:
            if store == 'wifi' and name in plan.entries:
                if source_key is None or target_key is None:
                    raise ValueError('Missing device identities')
                result = wifi_candidate(plan.read_json(name),source_key,target_key)
                plan.write(name,result['candidate'])
                plan.converted('wifi')
            if store == 'opds' and (name in plan.entries or legacy.get('opdsServerUrl')):
                if source_key is None or target_key is None:
                    raise ValueError('Missing device identities')
                old = {k:v for k,v in legacy.items() if k in ('opdsServerUrl','opdsUsername','opdsPassword','opdsPassword_obf')}
                result = opds_candidate(plan.read_json(name) if name in plan.entries else None,old,source_key,target_key)
                plan.write(name,result['candidate'])
                plan.converted('opds')
        except (ValueError, TypeError, KeyError, UnicodeError):
            plan.issue(name,'credentials_require_review_or_reentry')
    name = cp+'koreader.json'
    if name in plan.entries:
        try:
            old = plan.read_json(name)
            if not isinstance(old,dict) or set(old)-{'username','password_obf','password','serverUrl','matchMethod','syncBehavior'}:
                raise ValueError('Unknown KOReader fields')
            if source_key is None or target_key is None:
                raise ValueError('Missing device identities')
            password = decode_yacp_password(old['password_obf'],source_key,maximum=4096) if 'password_obf' in old else old.get('password','').encode('utf-8')
            if ('password_obf' in old and 'password' in old and old['password'].encode('utf-8')!=password) or b'\0' in password:
                raise ValueError('Conflicting credentials')
            for key in ('username','serverUrl'):
                if not isinstance(old.get(key,''),str) or '\0' in old.get(key,'') or len(old.get(key,'').encode('utf-8'))>4096:
                    raise ValueError('Invalid KOReader string')
            for key in ('matchMethod','syncBehavior'):
                if type(old.get(key,0)) is not int or old.get(key,0) not in (0,1):
                    raise ValueError('Invalid KOReader choice')
            converted = dict(cfgVersion=2,username=old.get('username',''),
                             password_obf=base64.b64encode(xor_bytes(password,target_key)).decode('ascii'),
                             serverUrl=old.get('serverUrl') or 'https://sync.koreader.rocks:443',
                             matchMethod=old.get('matchMethod',0),syncBehavior=old.get('syncBehavior',0),sendMetadata=False)
            plan.write(name,converted)
            plan.converted('koreader')
        except (ValueError,TypeError,KeyError,UnicodeError,AttributeError):
            plan.issue(name,'credentials_require_review_or_reentry')
    if cp+'recent.json' in plan.entries:
        try:
            recent = recent_candidate(plan.read_json(cp+'recent.json'),18)
            if recent['blocked']:
                raise ValueError('Recent list requires review')
            for book in recent['recent']['books']:
                # Cover images are rebuildable cache; never point at old layout.
                book['coverBmpPath'] = ''
            plan.write(cp+'recent.json',recent['recent'])
            plan.converted('recents')
        except (ValueError, TypeError, KeyError):
            plan.issue(cp+'recent.json','recent_list_requires_review')
    if state.get('openEpubPath') and state['openEpubPath'] not in progress_paths:
        plan.issue(cp+'state.json','automatic_resume_disabled_for_unresolved_position')
        state['openEpubPath'] = ''
    plan.write(cp+'state.json',state)
    plan.converted('state_and_autonomy')
    plan.report.update(migrated_positions=len(progress_paths), review_required=bool(plan.report['review']),
                       migration_status='prepared_copy_with_review' if plan.report['review'] else 'prepared_copy')
    plan.write('.yacp-migration/report.json',plan.report)
    return plan


def materialize(snapshot, output, manifest, plan, fault=None):
    """Append-only staging and one directory rename publish a complete local copy.

    A divergent destination is never overwritten. The optional fault hook is
    used by host tests at each persistent transition; it does not delete files.
    """
    output = local_output(output,snapshot.resolve())
    intent = dict(schema=1, source_manifest_sha256=sha256(snapshot/'source-manifest.json'),
                  target_cache=plan.report['target_cache'], files=[plan.files[k] for k in sorted(plan.files)])
    output.mkdir(parents=True,exist_ok=True)
    allowed = {'intent.json','card.pending','card','COMPLETE.json'}
    if any(p.name not in allowed or p.is_symlink() or p.is_junction() for p in output.iterdir()):
        raise ValueError('Output contains unrelated or linked data')
    if not (output/'intent.json').exists() and any(output.iterdir()):
        raise ValueError('Output is not owned by this migration')
    append_matching_json(output/'intent.json',intent)
    if fault: fault('intent')
    pending, final = output/'card.pending', output/'card'
    if pending.exists() and final.exists():
        raise ValueError('Ambiguous staging and published directories')
    card = final if final.exists() else pending
    card.mkdir(exist_ok=True)
    expected = {e['path']:e for e in intent['files']}
    for path in card.rglob('*'):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Linked prepared-card path')
        if path.is_file() and path.relative_to(card).as_posix() not in expected:
            raise ValueError('Prepared card contains unrelated data; nothing overwritten')
    for name,entry in expected.items():
        target = card/name
        if final.exists():
            if not target.is_file() or target.stat().st_size!=entry['size'] or sha256(target)!=entry['sha256']:
                raise ValueError('Published copy changed; use a new migration directory')
        elif 'source' in entry:
            copy_resumable(plan.root/entry['source'],target,entry)
        else:
            target.parent.mkdir(parents=True,exist_ok=True)
            data = plan.generated[name]
            prefix = target.read_bytes() if target.exists() else b''
            if not data.startswith(prefix):
                raise ValueError('Staged generated file differs; nothing overwritten')
            with target.open('ab' if target.exists() else 'xb') as stream:
                stream.write(data[len(prefix):])
                stream.flush()
                os.fsync(stream.fileno())
        if fault: fault('file:'+name)
    if sorted(enumerate_files(plan.root),key=lambda e:e['path']) != sorted(manifest['files'],key=lambda e:e['path']):
        raise ValueError('Original snapshot changed during migration')
    for name,entry in expected.items():
        if sha256(card/name)!=entry['sha256']:
            raise ValueError('Prepared-card verification failed')
    if fault: fault('verified')
    if card==pending:
        pending.rename(final)
    if fault: fault('published')
    completion=dict(schema=1,prepared_copy_verified=True,files=len(expected),
                    intent_sha256=sha256(output/'intent.json'),review_required=plan.report['review_required'],
                    installed=False,hardware_validated=False)
    append_matching_json(output/'COMPLETE.json',completion)
    if fault: fault('complete')
    return completion


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--source-mac',type=mac_bytes)
    parser.add_argument('--target-mac',type=mac_bytes)
    parser.add_argument('--same-device',action='store_true')
    parser.add_argument('--native-hasher',type=Path,help='Explicit compiled CacheIdentity helper for simulator copies only')
    parser.add_argument('--source-cache-policy',choices=('conservative','yacp-fnv64'),default='conservative',
                        help='Use yacp-fnv64 only for a source YACP version verified to use FNV64 book caches')
    args=parser.parse_args()
    if args.same_device and args.target_mac:parser.error('Choose same-device or target-mac')
    manifest=verified_snapshot(args.snapshot)
    target_key=args.source_mac if args.same_device else args.target_mac
    identity=esp32_book_path
    target='esp32'
    if args.native_hasher:
        program=args.native_hasher.resolve(strict=True)
        identity=lambda path:subprocess.check_output([str(program),path],text=True).strip()
        target='native-simulator'
    plan=build_plan(args.snapshot,manifest,args.source_mac,target_key,identity,target,args.source_cache_policy)
    result=materialize(args.snapshot,args.output,manifest,plan)
    print(json.dumps(dict(result,migrated_positions=plan.report['migrated_positions'],review_items=len(plan.report['review']))))
