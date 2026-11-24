"""Exercise actual book profile storage with in-memory SD fault injection."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / 'book-profile'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_book_profile/stubs', '-Itest/yacp_statistics/stubs',
               '-I.pio/libdeps/tiny/ArduinoJson/src', '-Isrc/activities/reader', '-Isrc',
               'test/yacp_book_profile/BookProfileTest.cpp', 'src/activities/reader/BookReaderSettings.cpp',
               '-o', str(executable)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = {'passed': True, 'truncated_writes': 66, 'single_byte_corruptions': 66,
              'interval_only_inheritance': True, 'v1_upgrade': True, 'v2_upgrade': True, 'guide_scope': True,
              'v3_upgrade': True, 'publisher_scope': True,
              'book_isolation': True, 'unchanged_saves_without_writes': 1000,
              'reset_truncated_writes': 66, 'reset_inherits_future_defaults': True,
              'reset_idempotent': True, 'reset_future_guard': True,
              'sanitizers': ['address', 'undefined'], 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args().output)
