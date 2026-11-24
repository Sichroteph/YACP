"""Read the captured raster-font catalogue using frozen YACP filename rules."""
import json
import re
from prepare_yacp_migration import enumerate_files, sha256


def verified_catalog(snapshot):
    snapshot = snapshot.resolve(strict=True)
    manifest = json.loads((snapshot / 'source-manifest.json').read_text(encoding='utf-8'))
    verified = json.loads((snapshot / 'COPY-VERIFIED.json').read_text(encoding='utf-8'))
    # WindowsPath and PosixPath sort mixed-case names differently. This is a
    # content manifest, not an on-card enumeration-order record.
    expected = sorted(manifest['files'], key=lambda entry: entry['path'])
    actual = sorted(enumerate_files(snapshot / 'original'), key=lambda entry: entry['path'])
    if verified.get('copy_verified') is not True or actual != expected:
        raise ValueError('Font snapshot does not match its verified manifest')
    families = {}
    for entry in manifest['files']:
        parts = entry['path'].split('/')
        if len(parts) != 2 or parts[1].startswith(('.', '_')):
            continue
        match = re.fullmatch(r'(.+)_([0-9]+)\.cpfont', parts[1])
        if not match or len(parts[1][:-7].encode('utf-8')) > 127:
            continue
        size = int(match[2])
        if 1 <= size <= 255:
            families.setdefault(parts[0], set()).add(size)
    return {family: sorted(sizes) for family, sizes in families.items()}, sha256(snapshot / 'source-manifest.json')
