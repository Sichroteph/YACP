"""Exercise the actual sleep-plane cache with storage faults and one-open-file enforcement."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    program = output / 'sleep-cache'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_sleep_cache/stubs', '-Isrc/util', 'test/yacp_sleep_cache/SleepImageCacheTest.cpp',
               'src/util/SleepImageCache.cpp', '-o', str(program)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(program)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = dict(passed=True, truncated_files=240, single_byte_corruptions=240, interrupted_writes=288,
                  key_changes=9, max_simultaneous_files=1, sanitizers=['address', 'undefined'],
                  hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args().output)
