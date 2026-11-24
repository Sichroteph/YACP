"""Exercise the actual OPDS store with persistence faults and sanitizers."""
import argparse
import json
from pathlib import Path
import subprocess


def run(output):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    program = output / 'test'
    args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
            '-Itest/yacp_opds/stubs', '-Itest/yacp_ota/stubs', '-I.pio/libdeps/tiny/ArduinoJson/src',
            '-Ilib/HttpHeader', '-Isrc',
            'src/OpdsServerStore.cpp', 'test/yacp_opds/Store.cpp', '-o', str(program)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(args, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(program)], stdout=log, stderr=subprocess.STDOUT, check=True)
    result = {'passed': True, 'actual_store_codec': True, 'save_failure_rollback': True,
              'credential_codec_stubbed': True, 'sanitizers': ['address', 'undefined'], 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    run(p.parse_args().output)
