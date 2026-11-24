"""Check device-name persistence and HTTP validation on an isolated simulator SD."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from verify_yacp_simulator_stats import create_fixture


def run(program, output):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    settings = sd / ".crosspoint/settings.json"
    values = json.loads(settings.read_text())
    values["deviceName"] = "Reader QA"
    settings.write_text(json.dumps(values), encoding="utf-8")
    (sd / ".crosspoint/wifi.json").write_text(json.dumps({"lastConnectedSsid": "LocalQA", "credentials": [
        {"ssid": "LocalQA", "password_obf": "", "password_len": 0, "password_crc32": 0}]}), encoding="utf-8")
    results = []
    for phase in ("api", "reload", "invalid-file"):
        if phase == "invalid-file":
            values = json.loads(settings.read_text())
            values["deviceName"] = "a" * 21
            settings.write_text(json.dumps(values), encoding="utf-8")
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        base = f"http://127.0.0.1:{port}"
        actions = ["1800:TAP:240,756", "2600:DOWN:100", "3000:DOWN:100", "3400:DOWN:100",
                   "4200:CONFIRM:100", "6000:CONFIRM:100", "19000:QUIT"]
        env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
                   CROSSPOINT_SIM_HTTP_PORT=str(port), CROSSPOINT_SIM_WIFI_NETWORKS="LocalQA:-40:open",
                   CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions))

        def status():
            with urlopen(base + "/api/status", timeout=2) as response:
                return json.load(response)

        def post(payload, expected):
            request = Request(base + "/api/settings", data=json.dumps(payload).encode(), method="POST",
                              headers={"Content-Type": "application/json"})
            try:
                response = urlopen(request, timeout=3)
            except HTTPError as error:
                response = error
            with response:
                assert response.status == expected, (payload, response.status, response.read())
            results.append({"phase": phase, "expected_http_status": expected})

        with (output / (phase + ".log")).open("x", encoding="utf-8") as log:
            process = subprocess.Popen([str(program)], cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 16
                while True:
                    try:
                        state = status()
                        break
                    except (URLError, TimeoutError):
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Simulator Web server did not start")
                        time.sleep(0.25)
                expected = {"api": "Reader QA", "reload": "Named reader", "invalid-file": "YACP X4 Pro"}[phase]
                assert state["deviceName"] == expected, state["deviceName"]
                if phase == "api":
                    before = settings.read_bytes()
                    for bad in (None, True, 12, [], "x", "a" * 21, "hello\0tail", "line\nbreak"):
                        post({"deviceName": bad, "trackReadingStats": 0}, 400)
                        assert settings.read_bytes() == before, "Invalid request partially saved settings"
                        assert status()["deviceName"] == "Reader QA"
                    for name in ("a" * 20, "\u00e9" * 10, "AB", ""):
                        post({"deviceName": name}, 200)
                        assert status()["deviceName"] == (name or "YACP X4 Pro")
                        assert json.loads(settings.read_text())["deviceName"] == name
                    post({"deviceName": "Named reader"}, 200)
                assert process.wait(timeout=24) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
    report = {"passed": True, "phases": 3, "http_cases": len(results), "source": "synthetic SD only",
              "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    run(args.program, args.output)
