"""Verify that leftover CrossInk settings cannot replace a native CrossPoint card."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib
from verify_yacp_runtime_migration import boot, hashes
from yacp_credential_migration import fnv1a, xor_bytes


SIMULATOR_KEY = bytes.fromhex('deadbeef0001')


def encoded(password):
    return base64.b64encode(xor_bytes(password, SIMULATOR_KEY)).decode('ascii')


def legacy_encoded(password):
    envelope = b'CPV1' + struct.pack('<I', fnv1a(SIMULATOR_KEY + password)) + password
    return encoded(envelope)


def verify_mixed_credentials(program, root):
    for interrupted in (False, True):
        output = root / ('credentials-interrupted' if interrupted else 'credentials-complete')
        cp = output / 'sd/.crosspoint'
        cp.mkdir(parents=True)
        (cp / 'settings.json').write_text(json.dumps({'language': 'EN'}), encoding='utf-8')
        (cp / 'crossink-settings.json').write_text(json.dumps({'buttonLayoutPromptVersion': '1.6.0-crossink'}),
                                                    encoding='utf-8')
        legacy_password = b'legacy-secret'
        wrapped_password = b'wrapped-secret'
        current_password = b'fresh-secret'
        unknown = encoded(b'unknown-old-format')
        wrapped = b'CPV1' + struct.pack('<I', fnv1a(SIMULATOR_KEY + wrapped_password)) + wrapped_password
        original = {'lastConnectedSsid': 'Legacy', 'credentials': [
            {'ssid': 'Legacy', 'password_obf': legacy_encoded(legacy_password)},
            {'ssid': 'Wrapped', 'password_obf': encoded(wrapped),
             'password_len': len(wrapped), 'password_crc32': zlib.crc32(wrapped)},
            {'ssid': 'Current', 'password_obf': encoded(current_password),
             'password_len': len(current_password), 'password_crc32': zlib.crc32(current_password)},
            {'ssid': 'Unknown', 'password_obf': unknown},
        ]}
        wifi = cp / 'wifi.json'
        original_bytes = json.dumps(original, separators=(',', ':')).encode()
        wifi.write_bytes(original_bytes)
        if interrupted:
            boot(program, output, 'reentry-original')
            assert (output/'sd/.yacp-reentry-v2/pending/original/wifi.json').read_bytes() == original_bytes
        log = boot(program, output)
        assert 'Converted validated YACP WiFi credentials' in log
        backup = next((output/'sd/.yacp-reentry-v2/history').glob('*/original/wifi.json'))
        assert backup.read_bytes() == original_bytes
        actual = json.loads(wifi.read_text(encoding='utf-8'))
        legacy = actual['credentials'][0]
        assert xor_bytes(base64.b64decode(legacy['password_obf']), SIMULATOR_KEY) == legacy_password
        assert legacy['password_len'] == len(legacy_password)
        assert legacy['password_crc32'] == zlib.crc32(legacy_password)
        fixed_wrapped = actual['credentials'][1]
        assert xor_bytes(base64.b64decode(fixed_wrapped['password_obf']), SIMULATOR_KEY) == wrapped_password
        assert fixed_wrapped['password_len'] == len(wrapped_password)
        assert fixed_wrapped['password_crc32'] == zlib.crc32(wrapped_password)
        assert actual['credentials'][2] == original['credentials'][2]
        assert actual['credentials'][3] == original['credentials'][3]
        converted_bytes = wifi.read_bytes()
        boot(program, output)
        assert wifi.read_bytes() == converted_bytes
        assert backup.read_bytes() == original_bytes


def verify(program, source, output):
    output.mkdir(parents=True, exist_ok=False)
    shutil.copytree(source, output / 'sd')
    before = hashes(output / 'sd')
    settings = json.loads((source / '.crosspoint/settings.json').read_text(encoding='utf-8'))
    legacy = json.loads((source / '.crosspoint/crossink-settings.json').read_text(encoding='utf-8'))
    assert '-yacp' not in legacy.get('buttonLayoutPromptVersion', '')
    log = boot(program, output, 'native-settings')
    assert 'Non-YACP legacy store retained' in log
    assert not (output / 'sd/.yacp-migration-v1').exists()
    archive = output / 'sd/.yacp-source-backup/original'
    expected_archive = {key.removeprefix('.crosspoint/'): value for key, value in before.items()
                        if key.startswith('.crosspoint/')}
    assert hashes(archive) == expected_archive
    assert {key: value for key, value in hashes(output / 'sd').items()
            if not key.startswith('.yacp-source-backup/')} == before
    # Ordinary settings loading must retain native point sizes and Home actions.
    boot(program, output, actions='1000:QUIT')
    actual = json.loads((output / 'sd/.crosspoint/settings.json').read_text(encoding='utf-8'))
    for key in ('fontSize', 'uiTheme', 'homeButtonTapAction', 'homeButtonDoubleTapAction', 'homeButtonLongPressAction'):
        assert actual[key] == settings[key], (key, actual[key], settings[key])
    assert (output / 'sd/.crosspoint/crossink-settings.json').read_bytes() == (source / '.crosspoint/crossink-settings.json').read_bytes()
    for p in (source / '.crosspoint').rglob('progress.bin'):
        assert (output / 'sd' / p.relative_to(source)).read_bytes() == p.read_bytes()
    verify_mixed_credentials(program, output / 'synthetic')
    result = dict(passed=True, source_files_unchanged_at_migration_gate=len(before),
                  native_font_size=actual['fontSize'], native_home_actions=[actual[k] for k in
                  ('homeButtonTapAction','homeButtonDoubleTapAction','homeButtonLongPressAction')],
                  legacy_store_preserved=True, hardware_progress_files_preserved=True,
                  mixed_wifi_credentials_converted=True, interrupted_wifi_conversion_resumed=True,
                  hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    verify(args.program, args.source, args.output)
