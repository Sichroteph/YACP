"""Verify a candidate with production C++ obfuscation and CRC code without logging secrets."""
import argparse
import json
from pathlib import Path
import subprocess
from prepare_yacp_migration import sha256


def run(candidate, output):
    root = Path(__file__).resolve().parents[1]
    candidate = candidate.resolve(strict=True)
    paths = [candidate / 'SOURCE.json', candidate / 'candidate-wifi.json']
    before = [sha256(p) for p in paths]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    program = output / 'credential-check'
    args = ['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
            '-Itest/yacp_credentials/stubs', '-Itest/yacp_statistics/stubs',
            '-I.pio/libdeps/tiny/ArduinoJson/src', '-Ilib/Serialization',
            'test/yacp_credentials/Candidate.cpp', 'lib/Serialization/ObfuscationUtils.cpp',
            '-l:libmbedcrypto.so.7', '-o', str(program)]
    with (output / 'compile.log').open('x') as log:
        subprocess.run(args, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    # Only assertion expressions can reach stderr, never credential values.
    result = subprocess.run([str(program), *map(str, paths)], capture_output=True, text=True, check=True)
    report = json.loads(result.stdout)
    assert before == [sha256(p) for p in paths]
    report.update(candidate_unchanged=True, hardware_validated=False, ready_to_activate=False,
                  sanitizers=['address', 'undefined'], platform_adapter='host libmbedcrypto and explicit factory MAC')
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('candidate', type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    run(a.candidate, a.output)
