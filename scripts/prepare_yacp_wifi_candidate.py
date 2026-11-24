"""Prepare a verified same-device Wi-Fi candidate outside the original snapshot.

Never prints names or passwords, activates a store, or writes to an SD card.
The user must explicitly supply both identities when targeting another reader.
"""
import argparse
import base64
import json
from pathlib import Path
import re
import zlib

from prepare_yacp_migration import append_matching_json, enumerate_files, sha256
from yacp_credential_migration import decode_yacp_password, wifi_candidate, xor_bytes


def mac_bytes(value):
    if not isinstance(value, str) or not re.fullmatch(
            r'(?:[0-9a-fA-F]{12}|(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}|(?:[0-9a-fA-F]{2}-){5}[0-9a-fA-F]{2})', value):
        raise ValueError('Expected an explicit six-byte MAC address')
    return bytes.fromhex(value.replace(':', '').replace('-', ''))


def prepare(snapshot, output, source_key, target_key):
    snapshot = snapshot.resolve(strict=True)
    output = output.absolute()
    for path in (output, *output.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Output must not traverse a linked directory')
    output = output.resolve()
    if ('nextcloud' in {p.casefold() for p in output.parts} or output.drive.upper() == 'G:'
            or output == snapshot or snapshot in output.parents or output in snapshot.parents):
        raise ValueError('Use a separate local experiment directory outside the SD and Nextcloud')
    manifest = json.loads((snapshot / 'source-manifest.json').read_text(encoding='utf-8'))
    verified = json.loads((snapshot / 'COPY-VERIFIED.json').read_text(encoding='utf-8'))
    source = snapshot / 'original'
    if (verified.get('copy_verified') is not True
            or verified.get('source_variant') != manifest.get('source_variant')
            or enumerate_files(source) != manifest['files']):
        raise ValueError('Expected an unchanged verified snapshot')
    wifi = source / 'wifi.json'
    document = json.loads(wifi.read_text(encoding='utf-8-sig'))
    result = wifi_candidate(document, source_key, target_key)
    # Independent target round-trip before writing any candidate.
    for old, new in zip(document['credentials'], result['candidate']['credentials']):
        password = xor_bytes(base64.b64decode(new['password_obf'], validate=True), target_key)
        if (password != decode_yacp_password(old['password_obf'], source_key)
                or new['ssid'] != old['ssid'] or new['password_len'] != len(password)
                or new['password_crc32'] != zlib.crc32(password)):
            raise ValueError('Credential conversion failed verification')
    origin = dict(schema=1, source_manifest_sha256=sha256(snapshot / 'source-manifest.json'),
                  source_wifi_sha256=sha256(wifi), source_factory_mac=source_key.hex(),
                  target_factory_mac=target_key.hex(), ready_to_activate=False)
    report = dict(networks=len(result['candidate']['credentials']), source_integrity_verified=True,
                  target_round_trip_verified=True, source_unchanged=True,
                  same_device_target=source_key == target_key, ready_to_activate=False,
                  hardware_validated=False)
    names = {'SOURCE.json', 'candidate-wifi.json', 'verification.json'}
    if output.exists():
        if (not output.is_dir() or any(p.name not in names or not p.is_file() for p in output.iterdir())
                or not (output / 'SOURCE.json').is_file()):
            raise ValueError('Output contains unrelated files or lacks its experiment origin')
    else:
        output.mkdir(parents=True)
    # Prefix matching permits an interrupted write to resume, never an overwrite.
    append_matching_json(output / 'SOURCE.json', origin)
    append_matching_json(output / 'candidate-wifi.json', result['candidate'])
    if enumerate_files(source) != manifest['files']:
        raise ValueError('Snapshot changed while converting')
    append_matching_json(output / 'verification.json', report)
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('snapshot', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--source-mac', required=True, type=mac_bytes)
    target = p.add_mutually_exclusive_group(required=True)
    target.add_argument('--same-device', action='store_true')
    target.add_argument('--target-mac', type=mac_bytes)
    a = p.parse_args()
    print(json.dumps(prepare(a.snapshot, a.output, a.source_mac,
                            a.source_mac if a.same_device else a.target_mac)))
