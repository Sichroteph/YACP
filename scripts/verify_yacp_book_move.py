"""Exercise production book move/recovery against an in-memory SD; no real deletion."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    executable = output / 'book-move'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-Itest/yacp_book_move/stubs', '-Itest/yacp_recents/stubs', '-Itest/yacp_statistics/stubs',
               '-Ilib/Memory', '-Isrc', 'test/yacp_book_move/BookMoveTest.cpp',
               'src/util/BookPathMove.cpp', 'src/util/BookmarkUtil.cpp', '-o', str(executable)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    report = dict(passed=True, sanitizers=['address', 'undefined'], hardware_validated=False,
                  real_files_removed=0, metadata_stores='fault-injecting adapters; actual recents tested separately')
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args().output)
