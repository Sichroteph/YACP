"""Exercise legacy EPUB commands through actual buttons on disposable simulator cards."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import decode
from yacp_data_formats import book_stats


# name: input, saved field, legacy ID, normalized action ID, expected child
CASES = {
    'menu-stats': ('CONFIRM', 'longPressMenuAction', 9, 10, 'BookStats'),
    'back-join': ('BACK', 'longPressBackAction', 18, 19, 'CrossPointWebServer'),
    'power-refresh': ('POWER', 'longPwrBtn', 3, 3, None),
    'power-stats': ('POWER', 'longPwrBtn', 10, 10, 'BookStats'),
    'menu-font': ('CONFIRM', 'longPressMenuAction', 2, 4, None),
    'menu-guide': ('CONFIRM', 'longPressMenuAction', 3, 5, None),
    'menu-focus': ('CONFIRM', 'longPressMenuAction', 4, 6, None),
    'menu-bookmark': ('CONFIRM', 'longPressMenuAction', 5, 7, None),
    'menu-sync': ('CONFIRM', 'longPressMenuAction', 7, 8, 'KOReaderSettings'),
    'menu-completed': ('CONFIRM', 'longPressMenuAction', 8, 9, 'BookStats'),
    'menu-screenshot': ('CONFIRM', 'longPressMenuAction', 10, 11, None),
    'menu-interval': ('CONFIRM', 'longPressMenuAction', 11, 12, 'AutoTurnInterval'),
    'menu-transfer': ('CONFIRM', 'longPressMenuAction', 12, 13, 'CrossPointWebServer'),
    'menu-tilt': ('CONFIRM', 'longPressMenuAction', 13, 14, None),
    'menu-dark': ('CONFIRM', 'longPressMenuAction', 14, 15, None),
    'menu-footnotes': ('CONFIRM', 'longPressMenuAction', 15, 16, None),
    'menu-browser': ('CONFIRM', 'longPressMenuAction', 16, 17, 'FileBrowser'),
    'menu-calibre': ('CONFIRM', 'longPressMenuAction', 17, 18, 'CrossPointWebServer'),
    'menu-hotspot': ('CONFIRM', 'longPressMenuAction', 19, 20, 'CrossPointWebServer'),
    'menu-reinforce': ('CONFIRM', 'longPressMenuAction', 21, 22, None),
    'back-ignore': ('BACK', 'longPressBackAction', 0, 0, None),
    'short-power-stats': ('POWER', 'readerShortPowerAction', 10, 10, 'BookStats'),
    'short-power-next': ('POWER', 'readerShortPowerAction', 2, 2, None),
    'power-long-with-short-sleep': ('POWER', 'longPwrBtn', 3, 3, None),
}


def scenario(program, output, name):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / '.crosspoint'
    key, field, value, action, child = CASES[name]
    path = root / 'settings.json'
    settings = json.loads(path.read_text())
    settings.update(trackReadingStats=0, clockEnabled=0, shortPwrBtn=2,
                    longPressMenuFunction=2, **{field: value})
    if name == 'power-long-with-short-sleep': settings['shortPwrBtn'] = 1
    path.write_text(json.dumps(settings))
    duration = 150 if name.startswith('short-') else 1800
    # With a competing CrossPoint Menu bookmark binding, the YACP binding must
    # win, and a Power action must not leak a page turn to the same release.
    script = f'1800:CONFIRM:150;6500:{key}:{duration};11200:BACK:150'
    if action == 2: script += ';12200:BACK:150'  # Dismiss the final-page completion prompt.
    script += ';14000:QUIT'
    env = dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_RENDER_DRIVER='software',
               CROSSPOINT_SIM_SD=str(sd), CROSSPOINT_SIM_INPUT_SCRIPT=script,
               CROSSPOINT_SIM_SCREENSHOTS=f'5300:{output}/before.bmp;7900:{output}/held.bmp;9000:{output}/released.bmp;12800:{output}/returned.bmp')
    with (output/'run.log').open('x') as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0, name
    trace = (output/'run.log').read_text()
    assert re.findall(r'Reader action (\d+)', trace) == [str(action)], name
    assert 'Entering activity: Sleep' not in trace, name
    assert 'Entering activity: EpubReaderMenu' not in trace, name
    if child: assert f'Entering activity: {child}' in trace, (name, child)
    saved = json.loads(path.read_text())
    assert saved[field] == value, name
    progress = list(root.glob('epub_*/progress.bin'))
    assert len(progress) == 1, (name, progress)
    data = progress[0].read_bytes()
    assert int.from_bytes(data[:2], 'little') == (1 if action == 2 else 0), name
    assert int.from_bytes(data[2:4], 'little') == 0, name
    if action in (4, 5, 6, 15):
        profiles = list(root.glob('book_profiles/epub_*/reader_profile.*'))
        assert profiles, name
        profile = decode(profiles[0].read_bytes())['settings']
        changed = {4:'fontFamily', 5:'guideReadingEnabled', 6:'focusReadingEnabled', 15:'readerDarkMode'}[action]
        assert profile[changed] == 1, (name, profile)
        assert saved.get(changed, 0) == 0, name
    if action == 14: assert saved['tiltPageTurn'] == 1, name
    if action == 7: assert list(root.glob('bookmarks/*.json')), name
    if action == 9:
        stats = list(root.glob('epub_*/stats_v6.bin'))
        assert len(stats) == 1 and book_stats(stats[0].read_bytes())['completed'], name
    if action == 11: assert len(list((sd/'screenshots').rglob('*.bmp'))) == 1, name
    if action in (0, 3, 4, 5, 6, 9, 10, 12, 14, 15, 17, 22):
        with Image.open(output/'held.bmp') as a, Image.open(output/'released.bmp') as b:
            assert a.tobytes() == b.tobytes(), (name, 'release changed screen')
    for capture in ('released', 'returned'):
        with Image.open(output/(capture+'.bmp')) as im: im.save(output/(capture+'.png'))
    report = dict(passed=True, scenario=name, action=action, release_consumed=True, hardware_validated=False)
    (output/'result.json').write_text(json.dumps(report, indent=2))
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('program', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    a = p.parse_args()
    with ThreadPoolExecutor(max_workers=3) as pool:
        results = list(pool.map(lambda name: scenario(a.program.resolve(strict=True), a.output.absolute()/name, name), a.cases))
    print(json.dumps(results))
