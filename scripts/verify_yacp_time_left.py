"""Check the EPUB time-left estimate and its cached Home value in the Pro simulator."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import book_stats


def run(program, hash_program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / '.crosspoint'
    settings = json.loads((root / 'settings.json').read_text(encoding='utf-8'))
    settings['statusBarTimeLeft'] = 2
    (root / 'settings.json').write_text(json.dumps(settings), encoding='utf-8')
    native_cache = subprocess.check_output([str(hash_program.resolve(strict=True)), '/completion.epub'], text=True).strip()
    cache = root / native_cache
    cache.mkdir()
    stats = bytes([6]) + struct.pack('<HII', 1, 1000, 20) + bytes([0])
    stats += struct.pack('<HHB', 30, 3, 0) + bytes(8 + 44) + struct.pack('<I', 0)
    assert len(stats) == 73
    (cache / 'stats_v6.bin').write_bytes(stats)
    (root / 'global_stats.bin').write_bytes(bytes([3]) + struct.pack('<4I', 1, 1000, 20, 0) + bytes(44 + 4 + 92 + 2))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT='2000:TAP:240,350;7000:TAP:240,350;9000:TAP:240,750;13500:HOME:100;17000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'5000:{output}/reader.bmp;11000:{output}/text-menu.bmp;16000:{output}/home.bmp')
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=30)
    if result.returncode:
        raise RuntimeError(f'Simulator exited with {result.returncode}')
    saved = book_stats((cache / 'stats_v6.bin').read_bytes())
    assert 50 <= saved['estimated_time_left_seconds'] <= 70, saved
    assert saved['sessions'] == 1 and saved['seconds'] == 1000
    assert saved['pace_samples'] == 3 and saved['pace_seconds'] == 30
    report = {'passed': True, 'book': saved, 'hardware_validated': False, 'migration_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'cached_time_left_seconds': saved['estimated_time_left_seconds']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--hash-program', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    run(args.program, args.hash_program, args.output)
