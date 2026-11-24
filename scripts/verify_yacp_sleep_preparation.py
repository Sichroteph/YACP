"""Decode browser-prepared BMPs with the actual CrossPoint bitmap implementation."""
import argparse
import json
from pathlib import Path
import subprocess


def run(source, output):
    root = Path(__file__).resolve().parents[1]
    source = source.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    program = output / 'probe'
    command = ['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g',
               '-DYACP_ADAPTIVE_TEST', '-Itest/yacp_bitmap/stubs', '-Ilib/GfxRenderer',
               'test/yacp_bitmap/BitmapProbe.cpp', 'lib/GfxRenderer/Bitmap.cpp', 'lib/GfxRenderer/BitmapHelpers.cpp', '-o', str(program)]
    with (output / 'run.log').open('x') as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(['node', 'test/yacp_bitmap/sleep-preparation.cjs', str(source), str(output)], cwd=root,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        cases = json.loads((output / 'cases.json').read_text())
        for case in cases:
            width, height = case['width'], case['height']
            pixels = (output / (case['name'] + '.indexes')).read_bytes()
            expected = bytearray((width + 3) // 4 * height)
            for y in range(height):
                for x in range(width):
                    expected[y * ((width + 3) // 4) + x // 4] |= pixels[y * width + x] << (6 - 2 * (x % 4))
            result = subprocess.run([str(program), str(output / (case['name'] + '.bmp')), 'native'],
                                    stdout=subprocess.PIPE, stderr=log, text=True, check=True)
            assert bytes.fromhex(result.stdout.strip()) == expected, case['name']
    report = {'passed': True, 'cases': cases, 'actual_bitmap_decoder': True,
              'sanitizers': ['address', 'undefined'], 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.source, a.output)
