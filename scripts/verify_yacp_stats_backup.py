"""Verify explicit statistics backup and cancellation with a synthetic Pro SD."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
from verify_yacp_simulator_stats import create_fixture


def run(program, output, scenario):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    original = bytes([4 if scenario == 'future' else 3]) + struct.pack('<4I', 4, 9000, 123, 2) + bytes(142)
    source = sd / '.crosspoint/global_stats.bin'
    source.write_bytes(original)
    actions = ['1800:BACK:100'] + [f'{2600+i*600}:DOWN:100' for i in range(4)]
    actions += ['5200:CONFIRM:100', '6400:CONFIRM:100', '7100:CONFIRM:100', '7800:CONFIRM:100',
                '9000:UP:100', '9700:UP:100', '10400:UP:100', '11500:CONFIRM:100']
    if scenario != 'cancel':
        actions += ['13500:DOWN:100']
    actions += ['14500:CONFIRM:100', '17500:BACK:100', '19500:QUIT']
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=';'.join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f'12500:{output}/confirm.bmp;16000:{output}/result.bmp')
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=30)
    assert result.returncode == 0
    assert 'Entering activity: BackupStats' in (output / 'run.log').read_text(encoding='utf-8')
    assert source.read_bytes() == original
    backups = list((sd / '.crossink-stats-backup').glob('*.bin'))
    assert len(backups) == (1 if scenario == 'save' else 0), backups
    for path in backups:
        assert path.read_bytes() == original
    report = {'passed': True, 'scenario': scenario, 'source_unchanged': True,
              'backups': len(backups), 'hardware_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--scenario', choices=('save', 'cancel', 'future'), required=True)
    a = p.parse_args()
    run(a.program, a.output, a.scenario)
