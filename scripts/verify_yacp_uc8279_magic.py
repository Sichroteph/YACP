"""Record real Pro-driver command streams on the host, retaining all test files."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def unix(path):
    path = path.resolve()
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]


def run(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    sdk = ROOT / 'freeink-sdk/libs/display/FreeInkDisplay'
    host = sdk / 'test/host'
    for folder in ('src/driver', 'src/bus', 'src/lut', 'include'):
        (output / folder).mkdir(parents=True, exist_ok=True)
    for name in ('Ssd1677', 'Uc8179', 'Uc8279X4'):
        for suffix in ('h', 'cpp'):
            relative = f'src/driver/{name}Driver.{suffix}'
            shutil.copy2(sdk / relative, output / relative)
    for relative in ('src/driver/PanelDriver.h', 'src/FreeInkDisplay.cpp',
                     'include/FreeInkDisplay.h', 'include/GrayscaleCapabilities.h',
                     'src/lut/Ssd1677Luts.h', 'src/lut/Uc8279X3Luts.h', 'src/lut/UltraChipDirectGrayLuts.h'):
        shutil.copy2(sdk / relative, output / relative)
    for name in ('Arduino.h', 'BoardConfig.h', 'SPI.h', 'esp_heap_caps.h', 'sdkconfig.h'):
        shutil.copy2(host / 'pro_stubs' / name, output / name)
    # Extend the local host fixture with the real SDK's Classic enum for exclusion tests.
    board_stub = output / 'BoardConfig.h'
    board_stub.write_text(board_stub.read_text(encoding='utf-8').replace(
        'XteinkX4Pro, Sticky', 'XteinkX4Pro, XteinkX4Classic, Sticky'), encoding='utf-8')
    shutil.copy2(host / 'pro_stubs/EpdBus.h', output / 'src/bus/EpdBus.h')
    programs = [('existing', host / 'test_pro.cpp'),
                ('magic', ROOT / 'test/yacp_refresh/test_uc8279_magic.cpp')]
    for single in (False, True):
        for name, source in programs:
            executable = output / (name + ('-single' if single else '-dual'))
            args = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-unused-function',
                    '-DBOARD_HAS_PSRAM=1', '-DARDUINO=1', '-I' + unix(output), '-I' + unix(output / 'include')]
            if single:
                args.append('-DEINK_DISPLAY_SINGLE_BUFFER_MODE=1')
            args += [unix(source), unix(output / 'src/FreeInkDisplay.cpp')]
            args += [unix(output / f'src/driver/{driver}Driver.cpp') for driver in ('Ssd1677', 'Uc8179', 'Uc8279X4')]
            commands = [args + ['-o', unix(executable)], [unix(executable)]]
            if name == 'existing':
                commands.append([unix(executable), 'sticky'])
            with (output / (executable.name + '.log')).open('x', encoding='utf-8') as log:
                for command in commands:
                    subprocess.run(['wsl', '-d', 'Ubuntu', '--', 'bash', '-lc', shlex.join(command)],
                                   stdout=log, stderr=subprocess.STDOUT, check=True)
            print(executable.name + ': passed', flush=True)
    (output / 'result.json').write_text(json.dumps({'passed': True, 'physical_waveform_validated': False}), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    run(parser.parse_args().output)
