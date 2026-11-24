"""Compile and run OTA policy/orchestration tests with in-memory device services.

Run on Linux/WSL with g++, OpenSSL development headers and sanitizers installed.
No network request, SD mount access, partition erase or firmware install occurs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def run(output, release_json=None):
    root = Path(__file__).resolve().parents[1]
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    base = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
            "-fsanitize=address,undefined", "-g", "-I", "src/network", "-I", "lib/JsonParser"]
    common = ["lib/JsonParser/ReleaseJsonParser.cpp", "lib/JsonParser/StreamingJsonParser.cpp"]
    cases = {
        "policy": base + ["test/yacp_ota/ReleasePolicyTest.cpp"] + common,
        "orchestration": base + ['-DCROSSPOINT_VERSION="1.7.2-yacp.169-tiny"',
            "-I", "test/yacp_ota/stubs", "-I", "lib/Memory", "test/yacp_ota/OtaUpdaterTest.cpp",
            "src/network/OtaUpdater.cpp"] + common + ["-lcrypto"],
    }
    with (output / "run.log").open("x", encoding="utf-8") as log:
        for name, command in cases.items():
            executable = output / name
            subprocess.run(command + ["-o", str(executable)], cwd=root, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
            subprocess.run([str(executable)], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
        if release_json:
            release_json = release_json.resolve(strict=True)
            before = hashlib.sha256(release_json.read_bytes()).hexdigest()
            subprocess.run([str(output / "orchestration"), str(release_json)], cwd=root,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
            assert before == hashlib.sha256(release_json.read_bytes()).hexdigest()
    report = {"passed": True, "tests": list(cases), "sanitizers": ["address", "undefined"],
              "release_metadata_sha256": before if release_json else None,
              "device_services": "in-memory stubs", "hardware_validated": False, "flashed_devices": 0}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--release-json", type=Path)
    args = parser.parse_args()
    run(args.output, args.release_json)
