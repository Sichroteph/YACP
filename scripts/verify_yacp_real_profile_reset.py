"""Validate candidate real-card profiles through the C++ loader and resetter in memory."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from yacp_book_profile import DEFAULTS, encode


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(candidate, output):
    root = Path(__file__).resolve().parents[1]
    candidate = candidate.resolve(strict=True)
    output = output.absolute()
    if output.exists() or candidate in output.parents or output in candidate.parents:
        raise ValueError('Use a new separate output directory')
    source = candidate / 'book-settings-candidates.json'
    global_source = candidate / 'candidate-settings.json'
    original = {str(p): digest(p) for p in (source, global_source)}
    defaults = dict(DEFAULTS, **json.loads(global_source.read_text()))
    entries = json.loads(source.read_text())
    assert entries
    output.mkdir(parents=True)
    profiles = []
    blocked = []
    for i, entry in enumerate(entries):
        converted = entry['candidate']
        missing = [d['source_key'] for d in converted['decisions'] if d['status'] == 'blocked']
        if missing:
            blocked.append(dict(source=entry['source_path'], keys=missing))
            continue
        settings = dict(defaults, **converted['settings'])
        profile = output / f'profile-{i}.bin'
        profile.write_bytes(encode(settings, flags=1))
        profiles.append(profile)
    assert profiles, 'No complete converted profiles are available'
    executable = output / 'real-profile-reset'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_book_profile/stubs', '-Itest/yacp_statistics/stubs',
               '-I.pio/libdeps/tiny/ArduinoJson/src', '-Isrc/activities/reader',
               'test/yacp_book_profile/RealProfileResetTest.cpp', 'src/activities/reader/BookReaderSettings.cpp',
               '-o', str(executable)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable), *map(str, profiles)], stdout=log, stderr=subprocess.STDOUT, check=True)
    assert original == {path: digest(Path(path)) for path in original}
    report = dict(passed=True, real_profiles=len(profiles), blocked_profiles=blocked, source_digests=original,
                  source_preserved=True, migration_activated=False, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(dict(passed=True, real_profiles=len(profiles), blocked_profiles=len(blocked), source_preserved=True)))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.candidate, a.output)
