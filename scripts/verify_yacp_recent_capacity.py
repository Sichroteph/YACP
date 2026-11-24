"""Open the last of 12/18 recent books, preserving all others through a resave."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def case(program, root, count, expected_limit):
    output = root / str(count)
    output.mkdir()
    sd = create_fixture(output)
    book = (sd / "completion.epub").read_bytes()
    entries = []
    for index in range(count):
        path = f"/recent-{index:02d}.epub"
        (sd / path[1:]).write_bytes(book)
        entries.append({"path": path, "title": f"Recent book {index + 1}", "author": "YACP QA",
                        "coverBmpPath": ""})
    recent = sd / ".crosspoint/recent.json"
    recent.write_text(json.dumps({"books": entries}))
    actions = [f"{1800 + i * 400}:DOWN:100" for i in range(count - 1)]
    actions += ["10100:CONFIRM:100", "17500:BACK:100", "20500:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"9500:{output}/last-recent.bmp;19000:{output}/returned.bmp")
    with (output / "run.log").open("x") as log:
        process = subprocess.run([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=35)
    assert process.returncode == 0
    log = (output / "run.log").read_text()
    expected_count = min(count, expected_limit)
    assert f"Recent books loaded from file ({expected_count} entries)" in log
    saved = json.loads(recent.read_text())["books"]
    assert len(saved) == expected_count
    expected = entries[:expected_count]
    assert [b["path"] for b in saved] == [expected[-1]["path"]] + [b["path"] for b in expected[:-1]]
    assert saved[1:] == expected[:-1], "Unopened recent entries must keep their metadata and order"
    assert saved[0]["title"] == "Completion Test", "The last loaded book must actually open"
    for image in output.glob("*.bmp"):
        with Image.open(image) as picture:
            picture.save(image.with_suffix(".png"))
    return {"count": count, "loaded": expected_count, "omitted": count - expected_count,
            "last_loaded_book_opened": True, "remaining_entries_unchanged": True}


def run(program, output, expected_limit=18):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda count: case(program, output, count, expected_limit), (12, 18)))
    report = {"passed": True, "cases": results, "all_entries_preserved": all(r["omitted"] == 0 for r in results),
              "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--expected-limit", type=int, default=18, help="Use 10 only to characterize the old upstream bound")
    a = p.parse_args()
    run(a.program, a.output, a.expected_limit)
