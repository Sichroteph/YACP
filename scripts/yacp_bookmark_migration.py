"""Prepare CrossPoint bookmark records with reviewed text anchors, without activation.

The complete legacy record remains in each decision. A missing/ambiguous anchor
blocks the entire book document. No percentage-only approximation is emitted.
"""
import hashlib
import posixpath
import struct
from urllib.parse import unquote
import xml.etree.ElementTree as ET
from zipfile import ZipFile

from yacp_data_formats import bookmarks
from yacp_legacy_anchor import first_cached_line, unique_text_offset


def target_path(book_path):
    if (not isinstance(book_path, str) or not book_path.startswith('/') or '\\' in book_path
            or any(p in ('', '.', '..') for p in book_path[1:].split('/'))
            or '\0' in book_path or not book_path.lower().endswith('.epub')):
        raise ValueError('Expected a canonical absolute SD EPUB path')
    # Match BookmarkUtil.cpp exactly. The caller must check collisions between
    # flattened names, including books that already have CrossPoint bookmarks.
    return '/.crosspoint/bookmarks/' + book_path[1:].replace('/', '_').rsplit('.', 1)[0] + '.json'


def check_target_collisions(paths):
    owners = {}
    for path in paths:
        # FAT names are case-insensitive; source identity is still case-sensitive.
        key = target_path(path).casefold()
        if key in owners and owners[key] != path:
            raise ValueError('CrossPoint bookmark filenames collide; do not merge implicitly')
        owners[key] = path


def epub_chapters(epub):
    with ZipFile(epub) as archive:
        container = ET.fromstring(archive.read('META-INF/container.xml'))
        package_path = next(e.attrib['full-path'] for e in container.iter()
                            if e.tag.rsplit('}', 1)[-1] == 'rootfile')
        package = ET.fromstring(archive.read(package_path))
        items = {e.attrib['id']: e.attrib['href'] for e in package.iter()
                 if e.tag.rsplit('}', 1)[-1] == 'item'}
        result = []
        for element in package.iter():
            if element.tag.rsplit('}', 1)[-1] != 'itemref' or element.attrib.get('idref') not in items:
                continue
            href = unquote(items[element.attrib['idref']].split('#', 1)[0])
            path = posixpath.normpath(posixpath.join(posixpath.dirname(package_path), href))
            if path.startswith('/') or path == '..' or path.startswith('../'):
                raise ValueError('Chapter path leaves the EPUB archive')
            raw = archive.read(path)
            result.append((raw.decode('utf-8-sig'), len(raw)))
        if not result or sum(size for _, size in result) == 0:
            raise ValueError('EPUB has no readable spine')
        return result


def cached_bookmark_page(section, bookmark):
    if section[:5] == b'\xffCXS\x2c':
        header, count_at, paragraph_at = 44, 26, 36
    elif section[:1] == bytes([27]):
        header, count_at, paragraph_at = 37, 19, 29
    else:
        raise ValueError('Unsupported bookmark section cache')
    if len(section) < header:
        raise ValueError('Truncated bookmark section cache')
    count = struct.unpack_from('<H', section, count_at)[0]
    if not count:
        raise ValueError('Empty bookmark section cache')
    paragraph = bookmark.get('paragraph_index', 65535)
    if paragraph != 65535:
        lut = struct.unpack_from('<I', section, paragraph_at)[0]
        if lut < header or lut + 2 > len(section):
            raise ValueError('Missing bookmark paragraph table')
        table_count = struct.unpack_from('<H', section, lut)[0]
        if table_count != count or lut + 2 + 2 * count > len(section):
            raise ValueError('Invalid bookmark paragraph table')
        values = struct.unpack_from(f'<{count}H', section, lut + 2)
        if any(a > b for a, b in zip(values, values[1:])):
            raise ValueError('Unordered bookmark paragraph table')
        # Match the frozen reader: first page whose paragraph index reaches
        # the stored index, otherwise the final page.
        return next((i for i, value in enumerate(values) if value >= paragraph), count - 1)
    # Reproduce the old float multiplication before its integer truncation.
    fraction = struct.unpack('<f', struct.pack('<f', bookmark['progress'] * count))[0]
    return min(int(fraction), count - 1)


def candidate(data, book_path, epub, sections=None):
    decoded = bookmarks(data)
    destination = target_path(book_path)
    if decoded['path'] != book_path:
        raise ValueError('Bookmark document belongs to another book path')
    chapters = epub_chapters(epub)
    total_size = sum(size for _, size in chapters)
    entries, decisions = [], []
    for legacy in decoded['bookmarks']:
        decision = {'legacy': legacy}
        decisions.append(decision)
        try:
            spine = legacy['spine']
            if not 0 <= spine < len(chapters):
                raise ValueError('Bookmark chapter is absent')
            html = chapters[spine][0]
            text = legacy.get('snippet', '')
            offset = None
            if text:
                try:
                    offset = unique_text_offset(html, text)
                    decision['method'] = 'unique_saved_snippet'
                except ValueError:
                    pass
            if offset is None:
                if sections is None or spine not in sections:
                    raise ValueError('No unique saved snippet or corresponding section cache')
                section = sections[spine]
                page = cached_bookmark_page(section, legacy)
                text = first_cached_line(section, page)
                offset = unique_text_offset(html, text)
                decision.update(method='unique_cached_page_text', source_page=page,
                                source_section_sha256=hashlib.sha256(section).hexdigest())
            # CrossPoint weights progress by inflated spine size, not chapter count.
            progress = (sum(size for _, size in chapters[:spine]) +
                        legacy['progress'] * chapters[spine][1]) / total_size
            entry = dict(xpath='', percentage=progress, summary=text,
                         name='', si=spine, pc=0, pp=0, vo=offset)
            entries.append(entry)
            decision['candidate'] = entry
        except ValueError as error:
            decision['blocked'] = str(error)
    blocked = sum('blocked' in d for d in decisions)
    return dict(source_sha256=hashlib.sha256(data).hexdigest(), source=decoded,
                target_path=destination, decisions=decisions, blocked_entries=blocked,
                candidate=None if blocked else {'bookmarks': entries},
                target_collision_check_required=True, ready_to_activate=False,
                firmware_navigation_validated=False)
