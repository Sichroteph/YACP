"""Run one pre-created native reader fixture and check its persisted text anchor."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from verify_yacp_reader_fixture import verify
from yacp_book_profile import decode


def run(program, fixture, book_scope=False, input_script=None):
    program = program.resolve(strict=True)
    fixture = fixture.resolve(strict=True)
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(fixture / "sd"),
               CROSSPOINT_SIM_INPUT_SCRIPT=input_script or "2500:TAP:240,350;24000:HOME:100;28000:QUIT",
               CROSSPOINT_SIM_SCREENSHOTS=f"20000:{fixture}/resumed-page.bmp;27000:{fixture}/home.bmp")
    with (fixture / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=fixture, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=45)
    if result.returncode:
        raise RuntimeError(f"Reader fixture failed: exit {result.returncode}")
    if not (fixture / "resumed-page.bmp").is_file():
        raise AssertionError("Reader screenshot missing")
    verify(fixture)
    if book_scope:
        expected = json.loads((fixture / 'expected-global-settings.json').read_text())
        saved = json.loads((fixture / 'sd/.crosspoint/settings.json').read_text())
        differences = [key for key, value in expected.items() if saved.get(key) != value]
        assert not differences, differences
        metadata = json.loads((fixture / 'fixture.json').read_text())
        profiles = list((fixture / 'sd/.crosspoint/book_profiles' / metadata['native_cache']).glob('reader_profile.*'))
        expected_bytes = (fixture / 'expected-book-profile.bin').read_bytes()
        assert len(profiles) == 1 and profiles[0].read_bytes() == expected_bytes
        size = decode(expected_bytes)['settings']['fontSize']
        assert f'Opened profile: slot=0 size={size}' in (fixture / 'run.log').read_text()
        from PIL import Image
        with Image.open(fixture / 'resumed-page.bmp') as image:
            image.save(fixture / 'resumed-page.png')
        result = {'passed': True, 'global_keys_preserved': len(expected), 'global_font_size': saved['fontSize'],
                  'book_profile_unchanged': True, 'book_font_size': size, 'hardware_validated': False}
        with (fixture / 'scope-result.json').open('x') as output:
            json.dump(result, output, indent=2)
        print(json.dumps(result))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("fixture", type=Path)
    p.add_argument("--book-scope", action="store_true")
    p.add_argument("--input-script", type=Path)
    a = p.parse_args()
    run(a.program, a.fixture, a.book_scope, a.input_script.read_text() if a.input_script else None)
