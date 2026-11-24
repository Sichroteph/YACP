"""Compare cold/warm sleep planes, corrupt-cache fallback and same-size source replacement."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, baseline, output, board, small=False):
    program, baseline = program.resolve(strict=True), baseline.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    seed = output / 'seed'
    sd = create_fixture(seed)
    values = json.loads((sd / '.crosspoint/settings.json').read_text())
    values.update(sleepScreen=2, sleepScreenCoverFilter=3, quickResumeSleepScreen=0, autoBackupStats=0,
                  trackReadingStats=0, uiTheme=0, clockEnabled=0)
    (sd / '.crosspoint/settings.json').write_text(json.dumps(values))
    width, height = (17, 19) if small else (528, 792)
    image = Image.new('RGB', (width, height))
    image.putdata([(v, v, v) for y in range(height) for x in range(width)
                   for v in [25 + x * 205 // (width - 1)]])
    image.save(sd / 'sleep.bmp')
    original_hash = hashlib.sha256((sd / 'sleep.bmp').read_bytes()).hexdigest()

    def scenario(name, executable, source, mutate=None):
        destination = output / name
        destination.mkdir()
        target = destination / 'sd'
        shutil.copytree(source, target)
        (target / '.crosspoint/state.json').write_text(json.dumps({'openEpubPath': '/completion.epub',
            'lastSleepFromReader': True, 'showBootScreen': True, 'readerActivityLoadCount': 0}))
        if mutate:
            mutate(target)
        env = dict(os.environ, SDL_VIDEODRIVER='offscreen', CROSSPOINT_SIM_SD=str(target),
                   CROSSPOINT_SIM_INPUT_SCRIPT='8500:POWER:800;28000:QUIT',
                   CROSSPOINT_SIM_SCREENSHOTS=f'25000:{destination}/sleep.bmp')
        with (destination / 'run.log').open('x') as log:
            result = subprocess.run([str(executable)], cwd=destination, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=45)
        assert result.returncode == 0
        with Image.open(destination / 'sleep.bmp') as image:
            image.save(destination / 'sleep.png')
            pixels = image.tobytes()
        return target, pixels, (destination / 'run.log').read_text()

    _, old_pixels, _ = scenario('baseline', baseline, sd)
    cold, cold_pixels, trace = scenario('cold', program, sd)
    assert 'Stored three sleep planes' in trace
    assert old_pixels == cold_pixels, 'Caching must retain the previous renderer output'
    cache, = (cold / '.crosspoint/sleep_planes_v2').glob('*.bin')
    cached = cache.read_bytes()
    assert cached[:6] == b'YSC2\2\1'
    size, = struct.unpack_from('<I', cached, 16)
    assert len(cached) == 48 + 3 * size
    warm, warm_pixels, trace = scenario('warm', program, cold)
    assert 'Rendered verified sleep image cache' in trace and 'Adaptive tone enabled:' not in trace
    assert warm_pixels == cold_pixels
    assert (warm / cache.relative_to(cold)).read_bytes() == cached

    def corrupt(target):
        path = target / cache.relative_to(cold)
        data = bytearray(path.read_bytes())
        data[48 + size + 30] ^= 1
        path.write_bytes(data)
    repaired, pixels, trace = scenario('corrupt', program, cold, corrupt)
    assert 'Sleep cache rejected' in trace and 'Stored three sleep planes' in trace
    assert pixels == cold_pixels and (repaired / cache.relative_to(cold)).read_bytes() == cached

    def replace(target):
        path = target / 'sleep.bmp'
        size_before = path.stat().st_size
        with Image.open(path) as image:
            image.transpose(Image.Transpose.FLIP_LEFT_RIGHT).save(path)
        assert path.stat().st_size == size_before
    replaced, pixels, trace = scenario('replaced', program, cold, replace)
    assert 'Rendered verified sleep image cache' not in trace and 'Stored three sleep planes' in trace
    assert pixels != cold_pixels
    assert hashlib.sha256((sd / 'sleep.bmp').read_bytes()).hexdigest() == original_hash
    assert hashlib.sha256((cold / 'sleep.bmp').read_bytes()).hexdigest() == original_hash
    report = dict(passed=True, board=board, source_dimensions=[width, height], plane_bytes=size, baseline_pixel_identical=True,
                  warm_pixel_identical=True, corrupt_fallback_identical=True, same_size_replacement_detected=True,
                  source_unchanged=True, hardware_grayscale_validated=False)
    (output / 'result.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--baseline', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--board', choices=('x3', 'pro'), required=True)
    p.add_argument('--small', action='store_true')
    a = p.parse_args()
    run(a.program, a.baseline, a.output, a.board, a.small)
