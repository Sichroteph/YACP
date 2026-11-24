"""Round-trip a reviewed shared-state proposal through the actual firmware codec."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(candidate, output):
    candidate = candidate.resolve(strict=True)
    before = hashlib.sha256(candidate.read_bytes()).hexdigest()
    proposal = json.loads(candidate.read_text(encoding="utf-8"))
    if proposal["blocked_keys"]:
        raise ValueError("Cannot validate a shared-state proposal with unresolved fields")
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    program = output / "state-round-trip"
    command = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
               "-Itest/yacp_power/stubs", "-I.pio/libdeps/tiny/ArduinoJson/src", "-Isrc",
               "test/yacp_power/StateCandidateRoundTrip.cpp", "src/CrossPointState.cpp", "-o", str(program)]
    with (output / "run.log").open("x") as log:
        subprocess.run(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run([str(program), str(candidate)], stdout=log, stderr=subprocess.STDOUT, check=True)
    assert hashlib.sha256(candidate.read_bytes()).hexdigest() == before
    result = {"passed": True, "fields": len(proposal["state"]), "candidate_unchanged": True,
              "sanitizers": ["address", "undefined"], "activation_validated": False,
              "gallery_order_verified": False, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("candidate", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.candidate, a.output)
