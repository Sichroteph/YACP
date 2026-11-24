"""Verify the retained CrossPoint dictionary is indexed only on explicit lookup."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from verify_yacp_simulator_stats import create_fixture


def run(program, output, lookup):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    root = sd / "dictionaries/QA"
    root.mkdir(parents=True)
    definition = b"The first item in a sequence. Synthetic dictionary acceptance test."
    index = b"first\0" + struct.pack(">II", 0, len(definition))
    (root / "qa.dict").write_bytes(definition)
    (root / "qa.idx").write_bytes(index)
    (root / "qa.ifo").write_text("StarDict's dict ifo file\nversion=2.4.2\nbookname=QA\nwordcount=1\n"
                                 f"idxfilesize={len(index)}\nsametypesequence=m\n", encoding="utf-8")
    originals = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    config = sd / ".crosspoint/settings.json"
    settings = json.loads(config.read_text(encoding="utf-8"))
    settings["dictionaryName"] = "QA"
    config.write_text(json.dumps(settings), encoding="utf-8")
    actions = ["1800:TAP:240,350"]
    if lookup:
        actions += ["5000:TAP:240,350", "7000:TAP:385,750"]
        actions += [f"{8500+i*500}:DOWN:100" for i in range(4)]
        actions += ["11500:CONFIRM:100", "13500:TAP:185,30", "20000:BACK:100", "21500:BACK:100"]
    actions += ["24000:HOME:100", "27000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"4000:{output}/reader.bmp;18000:{output}/definition.bmp;23000:{output}/returned.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=40)
    assert result.returncode == 0
    log = (output / "run.log").read_text(encoding="utf-8")
    if lookup:
        assert "Entering activity: DictionaryDefinition" in log
        assert log.index("Rendered page in") < log.index("Entering activity: DictionaryWordSelect")
        assert log.index("Entering activity: DictionaryWordSelect") < log.index("Indexed 1 entries")
        assert (root / "qa.qidx").is_file()
    else:
        assert not (root / "qa.qidx").exists()
        assert "[DICT]" not in log and "Entering activity: Dictionary" not in log
    for name, digest in originals.items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest
    report = {"passed": True, "lookup_requested": lookup, "dictionary_sources_unchanged": True,
              "indexed_only_on_demand": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--lookup", action="store_true")
    a = p.parse_args()
    run(a.program, a.output, a.lookup)
