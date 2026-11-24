"""Check actual recent-store removal and failed-save rollback in memory."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / 'recent-removal'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_recents/stubs', '-Itest/yacp_opds/stubs', '-Itest/yacp_statistics/stubs',
               '-I.pio/libdeps/tiny/ArduinoJson/src', '-Isrc',
               'test/yacp_recents/RecentRemovalTest.cpp', 'src/RecentBooksStore.cpp', '-o', str(executable)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = dict(passed=True, positions=18, failed_save_rollback=True,
                  sanitizers=['address', 'undefined'], sd_atomicity_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args().output)
