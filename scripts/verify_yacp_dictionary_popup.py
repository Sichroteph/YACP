"""Exercise real touch lookup, popup isolation and lazy indexing on a synthetic Pro SD."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
from zipfile import ZipFile

from PIL import Image, ImageChops
from verify_yacp_simulator_stats import create_fixture


def same_image(first, second, area=None):
    with Image.open(first) as a, Image.open(second) as b:
        if a.size != b.size:
            return False
        if area is not None:
            a, b = a.crop(area), b.crop(area)
        return ImageChops.difference(a.convert("RGB"), b.convert("RGB")).getbbox() is None


def scenario(program, output, name, orientation=0):
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    config = sd / ".crosspoint/settings.json"
    settings = json.loads(config.read_text())
    top_status = name in ("top-status", "top-status-missing")
    settings.update(dictionaryName="" if name == "disabled" else "QA", fontFamily=1, fontSize=14,
                    touchReaderControls=int(name != "touch-off"), orientation=orientation,
                    readerDarkMode=int(name == "dark"), yacpReaderFontInitialized=True)
    if top_status:
        settings.update(screenMargin=4, statusBarClock=3, statusBarProgressBar=1,
                        statusBarProgressBarPosition=1, statusBarProgressBarThickness=2, textAntiAliasing=0)
    config.write_text(json.dumps(settings))
    with ZipFile(sd / "completion.epub") as book:
        contents = {n: book.read(n) for n in book.namelist()}
    body = (
        "<p>alpha alpha alpha alpha alpha</p><p>beta beta beta beta beta</p><p>gamma gamma gamma gamma gamma</p>"
        if top_status else
        "<p>alpha beta gamma delta alpha beta gamma delta alpha beta gamma delta</p>"
        "<p>Keep this book page unchanged while consulting the dictionary.</p>"
    )
    contents["first.xhtml"] = (
        '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Dictionary test</title></head><body>'
        + body + "</body></html>"
    ).encode()
    with ZipFile(sd / "completion.epub", "w") as book:
        for filename, data in contents.items():
            book.writestr(filename, data)
    root = sd / "dictionaries/QA"
    originals = {}
    if name not in ("missing", "top-status-missing"):
        root.mkdir(parents=True)
        data, index = bytearray(), bytearray()
        for word in ("alpha", "beta", "delta", "gamma"):
            key = "unrelated" if name == "not-found" else word
            paragraphs = [f"Definition block {i}: {word} has a useful meaning in this synthetic dictionary."
                          for i in range(1, 45)]
            text = "\n".join(paragraphs) if name == "plain" else "".join(f"<p>{p}</p>" for p in paragraphs)
            definition = text.encode()
            index += key.encode() + b"\0" + struct.pack(">II", len(data), len(definition))
            data += definition
        (root / "qa.dict").write_bytes(data)
        (root / "qa.idx").write_bytes(index)
        (root / "qa.ifo").write_text("StarDict's dict ifo file\nversion=2.4.2\nbookname=QA\nwordcount=4\n"
                                    f"idxfilesize={len(index)}\nsametypesequence={'m' if name == 'plain' else 'h'}\n")
        originals = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}

    width, height = (800, 480) if orientation in (1, 3) else (480, 800)
    opened = name in ("html", "plain", "repeat", "orientation", "dark")
    actions = ["1800:TAP:240,350"]
    shots = [(4000, "reader")]
    if top_status:
        # In portrait, this is on the first line beneath the progress and RTC lanes.
        actions += ["5000:TAP:100,63,900"]
        if name == "top-status":
            actions += ["8200:TAP:0.98,0.5"]
        shots += [(6500, "popup"), (9500, "returned")]
        end = 10500
    elif opened:
        # Scripts are parsed at boot in portrait, before the reader rotates.
        # Normalized coordinates remain correct in all reader orientations.
        actions += [f"5000:TAP:{100/width},{20/height},900",
                    "8500:SWIPE:0.5,0.6666667,0.5,0.3333333",
                    "11500:SWIPE:0.5,0.3333333,0.5,0.6666667",
                    "14500:TAP:0.98,0.5"]
        shots += [(7500, "popup"), (10500, "scrolled"), (13500, "back"), (16500, "returned")]
        if name == "repeat":
            actions += ["17500:TAP:100,20,1200", "21000:BACK:100"]
            shots += [(20000, "reopened"), (22500, "returned-again")]
        end = 23500 if name == "repeat" else 17500
    else:
        if name == "blank":
            actions += [f"5000:TAP:{width-15},{height-110},900"]
        elif name == "swipe":
            actions += [f"5000:SWIPE:{width-100},200,100,200"]
        elif name != "idle":
            actions += ["5000:TAP:100,20,900"]
        shots += [(9500, "returned")]
        end = 10500
    actions += [f"{end}:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=";".join(f"{at}:{output}/{label}.bmp" for at, label in shots))
    with (output / "run.log").open("x") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=end / 1000 + 20)
    assert result.returncode == 0, output
    log = (output / "run.log").read_text()
    if top_status:
        assert "Entering activity: DictionaryWordSelect" in log, output
        if name == "top-status":
            assert "Entering activity: DictionaryDefinition" in log, output
            history = list((sd / ".crosspoint/dictionary-history/QA").glob("*.bin"))
            assert len(history) == 1, output
            headword = history[0].read_bytes()[16:].rstrip(b"\0").decode()
            assert headword == "alpha", f"Expected alpha under the finger, selected {headword}"
        else:
            assert "Entering activity: DictionaryDefinition" not in log, output
        reader = output / "reader.bmp"
        progress_area = (0, 8, width, 16)
        clock_area = (190, 17, 290, 40)
        # The simulator uses wall time. Allow the digits to change at a minute boundary.
        for frame in (reader, output / "popup.bmp", output / "returned.bmp"):
            with Image.open(frame) as shot:
                ink = shot.convert("L")
                assert ink.crop(clock_area).getextrema()[0] < 128, "RTC must be visible"
                assert ink.crop(progress_area).getextrema()[0] < 128, "Progress bar must be visible"
            assert same_image(reader, frame, progress_area), output
        # The error toast starts below this strip. Compare the complete first line.
        assert same_image(reader, output / "popup.bmp", (0, 45, width, 90)), output
        assert same_image(reader, output / "returned.bmp", (0, 45, width, height)), output
    elif opened:
        assert "Entering activity: DictionaryDefinition" in log, output
        assert "Popup definition page 2/" in log, output
        assert not same_image(output / "popup.bmp", output / "scrolled.bmp"), output
        assert same_image(output / "popup.bmp", output / "back.bmp"), output
        assert same_image(output / "reader.bmp", output / "returned.bmp"), output
        assert log.count("Indexed 4 entries") == 1, output
        assert log.count("Entering activity: DictionaryDefinition") == (2 if name == "repeat" else 1), output
        if name == "repeat":
            assert same_image(output / "reader.bmp", output / "returned-again.bmp"), output
    else:
        assert "Entering activity: DictionaryDefinition" not in log, output
        if name not in ("not-found", "missing", "blank"):
            assert "Entering activity: DictionaryWordSelect" not in log, output
        if name not in ("not-found", "missing"):
            assert not (root / "qa.qidx").exists(), output
        if name in ("idle", "blank", "not-found", "missing", "disabled", "touch-off"):
            assert same_image(output / "reader.bmp", output / "returned.bmp"), output
    for filename, digest in originals.items():
        assert hashlib.sha256((root / filename).read_bytes()).hexdigest() == digest, output
    for _, label in shots:
        with Image.open(output / f"{label}.bmp") as shot:
            shot.save(output / f"{label}.png")
    return {"case": name, "orientation": orientation, "passed": True}


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    cases = [(name, 0) for name in ("html", "plain", "repeat", "dark", "idle", "blank", "not-found", "missing",
                                   "disabled", "touch-off", "swipe", "top-status", "top-status-missing")]
    cases += [("orientation", value) for value in (1, 2, 3)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(scenario, program, output / f"{name}-{orientation}", name, orientation)
                   for name, orientation in cases]
        results = [f.result() for f in futures]
    report = {"passed": True, "cases": results, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.program, args.output)
