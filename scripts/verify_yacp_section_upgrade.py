"""Check a section-format upgrade against an isolated prior simulator fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

from yacp_data_formats import crosspoint48_page_offsets, progress_record


def run(program, source_sd, output, top_progress_bar=False):
    program = program.resolve(strict=True)
    source_sd = source_sd.resolve(strict=True)
    output = output.absolute()
    if source_sd == output or source_sd in output.parents or output in source_sd.parents:
        raise ValueError("Expected separate source and output trees")
    cache, = (source_sd / ".crosspoint").glob("epub_*")
    old_position = progress_record((cache / "progress.bin").read_bytes())
    old_section = cache / "sections" / f"{old_position['spine']}.bin"
    assert old_section.read_bytes()[0] in (48, 234, 49, 233, 50, 232)
    original_hash = hashlib.sha256(old_section.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    sd = output / "sd"
    shutil.copytree(source_sd, sd)
    state = sd / ".crosspoint/state.json"
    state.write_text('{"openEpubPath":""}', encoding="utf-8")
    settings = sd / ".crosspoint/settings.json"
    values = json.loads(settings.read_text(encoding="utf-8"))
    values.update(language="EN", trackReadingStats=0)
    if top_progress_bar:
        values.update(statusBarProgressBar=0, statusBarProgressBarPosition=1)
    settings.write_text(json.dumps(values), encoding="utf-8")
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT="1800:TAP:240,350;15000:HOME:100;18500:QUIT",
               CROSSPOINT_SIM_SCREENSHOTS=f"10000:{output}/resumed.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=30)
    assert result.returncode == 0
    new_cache = sd / ".crosspoint" / cache.name
    position = progress_record((new_cache / "progress.bin").read_bytes())
    assert position["spine"] == old_position["spine"]
    section = (new_cache / "sections" / old_section.name).read_bytes()
    assert section[0] in (51, 231)
    offsets = crosspoint48_page_offsets(section)
    anchor, page = old_position["visible_text_offset"], position["page"]
    assert offsets[page] <= anchor and (page + 1 == len(offsets) or anchor < offsets[page + 1])
    assert hashlib.sha256(old_section.read_bytes()).hexdigest() == original_hash
    report = {"passed": True, "source_cache_unchanged": True, "new_section_version": section[0],
              "old_position": old_position, "new_position": position, "hardware_validated": False,
              "top_progress_bar": top_progress_bar}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--source-sd", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--top-progress-bar", action="store_true")
    a = p.parse_args()
    run(a.program, a.source_sd, a.output, a.top_progress_bar)
