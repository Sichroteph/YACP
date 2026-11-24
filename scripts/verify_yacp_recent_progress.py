"""Capture recent-grid progress on a copy of a completed recent-action fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image


def digests(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def run(program, fixture, output):
    program = program.resolve(strict=True)
    source = (fixture / 'sd').resolve(strict=True)
    output = output.absolute()
    if output.exists() or source in output.parents:
        raise ValueError('Use a new separate output directory')
    before = digests(source)
    output.mkdir(parents=True)
    sd = output / 'sd'
    shutil.copytree(source, sd)
    root = sd / '.crosspoint'
    state = root / 'state.json'
    values = json.loads(state.read_text())
    values['openEpubPath'] = ''
    state.write_text(json.dumps(values))
    settings = root / 'settings.json'
    values = json.loads(settings.read_text())
    values['recentBooksView'] = 1
    settings.write_text(json.dumps(values))
    progress = {str(p): p.read_bytes().hex() for p in root.glob('epub_*/progress.bin')}
    assert len(progress) == 1
    recent = (root / 'recent.json').read_bytes()
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT='1800:BACK:100;2800:CONFIRM:100;6500:QUIT',
               CROSSPOINT_SIM_SCREENSHOTS=f'5200:{output}/progress.bmp')
    with (output / 'run.log').open('x') as log:
        subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT,
                       timeout=20, check=True)
    assert 'Entering activity: RecentBooksGrid' in (output / 'run.log').read_text()
    assert progress == {str(p): p.read_bytes().hex() for p in root.glob('epub_*/progress.bin')}
    assert (root / 'recent.json').read_bytes() == recent
    assert digests(source) == before
    with Image.open(output / 'progress.bmp') as picture: picture.save(output / 'progress.png')
    report = dict(passed=True, source_unchanged=True, progress_unchanged=True,
                  visual_review_required=True, hardware_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('fixture', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.fixture, a.output)
