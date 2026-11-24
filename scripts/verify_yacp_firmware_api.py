"""Exercise the remote firmware request contract against the non-flashing simulator."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from verify_yacp_simulator_stats import create_fixture


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    (sd / "probe.bin").write_bytes(b"Synthetic test, not a firmware image")
    (sd / "folder.bin").mkdir()
    (sd / ".crosspoint/wifi.json").write_text(json.dumps({"lastConnectedSsid": "LocalQA", "credentials": [
        {"ssid": "LocalQA", "password_obf": "", "password_len": 0, "password_crc32": 0}]}), encoding="utf-8")
    source_hash = hashlib.sha256((sd / "probe.bin").read_bytes()).hexdigest()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    base = f"http://127.0.0.1:{port}"
    actions = ["1800:TAP:240,756", "2600:DOWN:100", "3000:DOWN:100", "3400:DOWN:100",
               "4200:CONFIRM:100", "6000:CONFIRM:100", "26000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_WIFI_NETWORKS="LocalQA:-40:open",
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions))
    results = []
    with (output / "run.log").open("x", encoding="utf-8") as log:
        process = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 22
            while True:
                try:
                    with urlopen(base + "/api/status", timeout=1) as response:
                        assert response.status == 200
                    break
                except (URLError, TimeoutError):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("Simulator Web server did not start")
                    time.sleep(0.25)
            cases = [
                ({}, False, 400),
                ({"path": "/probe.bin"}, False, 400),
                ({"path": "/probe.bin", "confirm": "no"}, True, 400),
                ({"path": "probe.bin", "confirm": "install"}, True, 400),
                ({"path": "/../probe.bin", "confirm": "install"}, True, 400),
                ({"path": "/probe.bin\u0000other", "confirm": "install"}, True, 400),
                ({"path": "/.crosspoint/probe.bin", "confirm": "install"}, True, 403),
                ({"path": "/completion.epub", "confirm": "install"}, True, 400),
                ({"path": "/absent.bin", "confirm": "install"}, False, 404),
                ({"path": "/folder.bin", "confirm": "install"}, True, 400),
                ({"path": "/probe.bin", "confirm": "install"}, False, 503),
                ({"path": "/probe.bin", "confirm": "install"}, True, 503),
            ]
            for payload, as_json, expected in cases:
                data = (json.dumps(payload) if as_json else urlencode(payload)).encode()
                request = Request(base + "/api/firmware/install", data=data, method="POST", headers={
                    "Content-Type": "application/json" if as_json else "application/x-www-form-urlencoded"})
                try:
                    response = urlopen(request, timeout=3)
                except HTTPError as error:
                    response = error
                with response:
                    body = response.read().decode()
                    assert response.status == expected, (payload, expected, response.status, body)
                    results.append({"request": payload, "json": as_json, "status": response.status, "body": body})
            with urlopen(base + "/api/status", timeout=3) as response:
                assert response.status == 200
            assert hashlib.sha256((sd / "probe.bin").read_bytes()).hexdigest() == source_hash
            assert process.wait(timeout=32) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    report = {"passed": True, "requests": results, "source_unchanged": True,
              "flashing_disabled": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "requests"}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.program, args.output)
