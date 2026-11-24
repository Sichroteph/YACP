"""Pure OPDS proposal; no files, requests, key discovery or credential logging."""
import base64
from copy import deepcopy
from yacp_credential_migration import decode_yacp_password, hardware_key, xor_bytes


def text(value, maximum):
    if not isinstance(value, str) or '\0' in value or len(value.encode('utf-8')) > maximum:
        raise ValueError('Invalid or oversized OPDS field')
    return value


def candidate(document, legacy, source_key, target_key):
    """An existing store is authoritative, even when empty, as in frozen YACP.

    The 4096-byte per-field limit is an explicit experiment bound, not a claimed
    firmware limit. Larger inputs fail as a unit and remain in the original.
    """
    hardware_key(source_key)
    hardware_key(target_key)
    allowed = {'opdsServerUrl', 'opdsUsername', 'opdsPassword', 'opdsPassword_obf'}
    if not isinstance(legacy, dict) or set(legacy) - allowed:
        raise ValueError('Unreviewed legacy OPDS fields')
    if document is None:
        url = text(legacy.get('opdsServerUrl', ''), 127)
        username = text(legacy.get('opdsUsername', ''), 63)
        if not url:
            if username or legacy.get('opdsPassword') or legacy.get('opdsPassword_obf'):
                raise ValueError('Credentials have no legacy OPDS server URL')
            rows = []
        else:
            row = {'name': 'OPDS Server', 'url': url, 'username': username}
            if 'opdsPassword_obf' in legacy:
                row['password_obf'] = legacy['opdsPassword_obf']
            elif 'opdsPassword' in legacy:
                row['password'] = text(legacy['opdsPassword'], 63)
            rows = [row]
        source = 'legacy_settings'
    else:
        if not isinstance(document, dict) or set(document) != {'servers'} or not isinstance(document['servers'], list):
            raise ValueError('Unreviewed OPDS store schema')
        rows = deepcopy(document['servers'])
        source = 'opds_store'
    if len(rows) > 8:
        raise ValueError('OPDS server count exceeds target capacity')
    servers = []
    for row in rows:
        if not isinstance(row, dict) or set(row) - {'name', 'url', 'username', 'password', 'password_obf', 'filenameFormat'}:
            raise ValueError('Unreviewed OPDS server schema')
        target = {key: text(row.get(key, ''), 4096) for key in ('name', 'url', 'username')}
        if not target['url']:
            raise ValueError('OPDS server has no URL')
        if 'password_obf' in row:
            password = decode_yacp_password(row['password_obf'], source_key, maximum=4096)
            # Refuse conflicting copies instead of choosing a silent fallback.
            if 'password' in row and text(row['password'], 4096).encode() != password:
                raise ValueError('Conflicting OPDS credential representations')
        else:
            password = text(row.get('password', ''), 4096).encode()
        target['password_obf'] = base64.b64encode(xor_bytes(password, target_key)).decode('ascii')
        filename = row.get('filenameFormat', 'author_title')
        if filename not in ('author_title', 'title_author'):
            raise ValueError('Unreviewed OPDS filename choice')
        target['filenameFormat'] = int(filename == 'title_author')
        servers.append(target)
    return {'candidate': {'servers': servers}, 'source': source,
            'settings': {}, 'blocked': [],
            'source_filename_formats': [r.get('filenameFormat', 'author_title') for r in rows],
            'legacy_retained_in_original': bool(document is not None and legacy),
            'ready_to_activate': False}
