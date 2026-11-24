"""Check optional reference counters without changing serialized pages or text pixels."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image, ImageChops
from verify_yacp_simulator_stats import create_fixture


def scenario(program, output, board, mode):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / '.crosspoint/settings.json'
    values = json.loads(settings.read_text())
    values.update(stablePageNumbers=int(mode != 'off'), statusBarChapterPageCount=1,
                  trackReadingStats=0, clockEnabled=0)
    settings.write_text(json.dumps(values))
    book = sd / 'completion.epub'
    with ZipFile(book) as source:
        entries = {name: source.read(name) for name in source.namelist()}
    if mode != 'missing':
        manifest = dict(format='x-locations', version=1, totalLocations=20, totalWords=30000,
                        spine=[dict(index=i, startLocation=1+i*10, endLocation=10+i*10,
                                    wordStart=i*15000, wordCount=15000) for i in range(2)])
        text = json.dumps(manifest)
        entries['META-INF/x-locations.json'] = (text[:-1] if mode == 'invalid' else text).encode()
    with ZipFile(book, 'w', compression=ZIP_STORED) as target:
        for name, data in entries.items(): target.writestr(name, data)
    actions = '1800:TAP:240,350;6500:SWIPE:380,350,100,350;10500:HOME:150' if board == 'pro' else \
              '1800:CONFIRM:150;6500:RIGHT:150;10500:BACK:150'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software', CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=actions+';12000:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'5000:{output}/first.bmp;9000:{output}/last.bmp')
    with (output / 'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=24)
    assert result.returncode == 0
    trace = (output / 'run.log').read_text()
    if mode == 'on':
        assert trace.count('reference pages') == 2, trace[-3000:]
        assert 'Loaded chapter 0: 20 reference pages' in trace
        assert 'Loaded chapter 1: 20 reference pages' in trace
    elif mode == 'invalid':
        assert trace.count('Invalid or unsupported reference numbering') == 2
    else:
        assert 'REFPAGE' not in trace
    cache, = (sd / '.crosspoint').glob('epub_*')
    sections = [(cache / 'sections' / (str(i)+'.bin')).read_bytes() for i in range(2)]
    for path in output.glob('*.bmp'):
        with Image.open(path) as image: image.save(path.with_suffix('.png'))
    return sections, (cache / 'progress.bin').read_bytes()


def run(program, output, board):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    modes = ('off', 'on', 'missing', 'invalid')
    with ThreadPoolExecutor(max_workers=2) as pool:
        jobs = [pool.submit(scenario, program, output / mode, board, mode) for mode in modes]
        results = [job.result() for job in jobs]
    assert all(result == results[0] for result in results)
    for page in ('first', 'last'):
        with Image.open(output / 'off' / (page+'.bmp')) as original:
            for mode in modes[1:]:
                with Image.open(output / mode / (page+'.bmp')) as candidate:
                    bbox = ImageChops.difference(original.convert('RGB'), candidate.convert('RGB')).getbbox()
                    if mode == 'on':
                        assert bbox and bbox[1] > original.height - 60, bbox
                    else:
                        assert bbox is None, (mode, bbox)
    report = dict(passed=True, board=board, identical_sections_and_progress=True,
                  changes_limited_to_status_counter=True, missing_and_invalid_fallback=True,
                  hardware_validated=False, visual_review_required=True)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--board', choices=('x3', 'pro'), required=True)
    args = p.parse_args()
    run(args.program, args.output, args.board)
