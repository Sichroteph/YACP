"""Exercise Pro statistics navigation and date editing on synthetic data only."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import book_stats, global_stats, finished_books


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    actions = ';'.join((
        '1800:TAP:240,350', '8500:TAP:240,350', '10500:TAP:385,750',
        '12500:TAP:170,604', '16500:TAP:180,766',
        '18500:TAP:180,766', '19500:TAP:180,766', '20500:TAP:180,766',
        '22500:TAP:405,766', '25500:TAP:40,766', '28500:TAP:40,766',
        '42000:HOME:100', '44200:TAP:40,766', '47000:QUIT'))
    shots = ';'.join(f'{time}:{output}/{name}.bmp' for time, name in (
        (14500, 'book-stats'), (24000, 'edit-date'), (27000, 'edited-stats'),
        (31000, 'reader'), (43500, 'achievement'), (46000, 'home')))
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=actions, CROSSPOINT_SIM_SCREENSHOTS=shots)
    with (output / 'run.log').open('x', encoding='utf-8') as log:
        result = subprocess.run([str(program)], cwd=output, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=58)
    if result.returncode:
        raise RuntimeError(f'Simulator exited with {result.returncode}')
    root = sd / '.crosspoint'
    files = list(root.glob('epub_*/stats_v6.bin'))
    assert len(files) == 1
    book = book_stats(files[0].read_bytes())
    total = global_stats((root / 'global_stats.bin').read_bytes())
    index = finished_books((root / 'finished_books.bin').read_bytes())
    assert book['completed'] and book['finished_date'] and book['flags'] & 2
    assert total['completed_books'] == 1
    assert book['sessions'] == 0 and total['sessions'] == 0
    assert 15 <= book['seconds'] <= 24, book
    assert book['seconds'] == total['seconds']
    assert book['pages'] == 0 and total['pages'] == 0
    assert len(index['books']) == 1 and index['books'][0]['seconds'] == book['seconds']
    report = {'passed': True, 'book': book, 'global': total, 'finished': index,
              'scenario': 'reader, stats preview, edit completion date, resume, Home',
              'hardware_validated': False, 'migration_validated': False}
    (output / 'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'passed': True, 'reading_seconds': book['seconds'],
                      'completed_books': total['completed_books']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('program', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    run(args.program, args.output)
