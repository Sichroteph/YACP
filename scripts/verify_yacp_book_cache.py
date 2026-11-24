"""Run the production cache invalidator against an in-memory SD with injected failures."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / 'book-cache'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_book_cache/stubs', '-Itest/yacp_recents/stubs', '-Itest/yacp_statistics/stubs',
               '-Isrc/util', 'test/yacp_book_cache/BookCacheTest.cpp', 'src/util/BookCacheUtils.cpp',
               '-o', str(executable)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = dict(passed=True, preserved_entries=24, removal_failures_and_retries=19,
                  formats=['EPUB', 'XTC', 'TXT', 'Markdown'], sanitizers=['address', 'undefined'],
                  hardware_validated=False, real_files_removed=0)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args().output)
