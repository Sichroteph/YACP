"""Verify software sleep/wake and autonomy persistence on an isolated X3 SD."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import re
from verify_yacp_simulator_stats import create_fixture
from yacp_book_profile import DEFAULTS, encode

def run(program, output, reader_dark=False, expect_stats_backup=False, book_profile_cache=None):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / ".crosspoint"
    if reader_dark:
        settings = root / "settings.json"
        values = json.loads(settings.read_text(encoding="utf-8"))
        values["readerDarkMode"] = 1
        settings.write_text(json.dumps(values), encoding="utf-8")
    if book_profile_cache:
        if not re.fullmatch(r'epub_[0-9]+', book_profile_cache):
            raise ValueError('Invalid simulator cache identity')
        profile = root / 'book_profiles' / book_profile_cache
        profile.mkdir(parents=True)
        (profile / 'reader_profile.0').write_bytes(encode(dict(DEFAULTS, fontSize=14, orientation=2, readerDarkMode=1)))
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
        CROSSPOINT_SIM_INPUT_SCRIPT="2000:CONFIRM:100;7000:DOWN:100;11000:POWER:800;16500:POWER:100",
        CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE="7500:QUIT",
        CROSSPOINT_SIM_SCREENSHOTS=f"9500:{output}/before.bmp;14500:{output}/asleep.bmp",
        CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE=f"6000:{output}/resumed.bmp")
    sleep_state = None
    with (output / "run.log").open("x", encoding="utf-8") as log:
        proc = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
        start = time.monotonic()
        while proc.poll() is None and time.monotonic() - start < 34:
            frame = root / "sleep_frame.bin"
            if sleep_state is None and frame.is_file() and frame.stat().st_size > 0:
                (output / "retained-frame.bin").write_bytes(frame.read_bytes())
                sleep_state = json.loads((root / "state.json").read_text(encoding="utf-8"))
                assert frame.stat().st_size == 528 * 792 // 8
                (output / "sleep-state.json").write_text(json.dumps(sleep_state), encoding="utf-8")
            time.sleep(0.1)
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=5)
            raise RuntimeError("Simulator did not finish its wake scenario")
    assert proc.returncode == 0 and sleep_state is not None
    assert sleep_state["openEpubPath"] == "/completion.epub" and sleep_state["lastSleepFromReader"]
    assert sleep_state["powerHistory"]["sleepSamples"] == 1
    assert sleep_state.get("sleepFrameInverted", False) == (reader_dark or bool(book_profile_cache))
    assert sleep_state["powerHistory"]["activeSeconds"] == 0  # simulator is permanently USB-powered
    assert not (root / "sleep_frame.bin").exists(), "Wake did not consume retained frame"
    saved = json.loads((root / "state.json").read_text(encoding="utf-8"))
    assert saved["openEpubPath"] == "/completion.epub" and saved["showBootScreen"]
    assert saved["powerHistory"]["sleepSamples"] == 1
    for name in ("before", "resumed"):
        assert (output / (name + ".bmp")).exists()
    if book_profile_cache:
        trace = (output / 'run.log').read_text()
        assert trace.count('Opened profile: slot=0 size=14') == 2
        settings = json.loads((root / 'settings.json').read_text())
        assert settings['fontSize'] == 12 and settings.get('orientation', 0) == 0
        assert settings.get('readerDarkMode', 0) == int(reader_dark)
        from PIL import Image
        with Image.open(output / 'before.bmp') as before, Image.open(output / 'resumed.bmp') as after:
            assert before.tobytes() == after.tobytes(), 'Resumed page changed'
            after.save(output / 'resumed.png')
    if expect_stats_backup:
        backups = list((sd / ".crossink-stats-backup").glob("stats_*.bin"))
        assert len(backups) == 1, backups
        assert backups[0].read_bytes() == (root / "global_stats.bin").read_bytes()
    (output / "result.json").write_text(json.dumps({"passed": True,
        "scenario": "X3 Quick Resume and one autonomy sample", "reader_dark": reader_dark,
        "book_profile": bool(book_profile_cache),
        "stats_backup_verified": expect_stats_backup,
        "hardware_validated": False}), encoding="utf-8")
    print("Quick Resume: saved frame consumed, book resumed, one autonomy sample retained")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--reader-dark", action="store_true")
    p.add_argument("--expect-stats-backup", action="store_true")
    p.add_argument("--book-profile-cache", help="Observed native cache name for the synthetic /completion.epub")
    a = p.parse_args()
    run(a.program, a.output, a.reader_dark, a.expect_stats_backup, a.book_profile_cache)
