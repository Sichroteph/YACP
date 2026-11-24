"""Verify an uncertain Smart Sync never uploads to a loopback-only test server."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import threading

from verify_yacp_simulator_stats import create_fixture


def run(program, output, equal=False, upload_name=None, buttons=False):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    requests = []
    uploads = []
    if upload_name is not None:
        settings = sd / ".crosspoint/settings.json"
        values = json.loads(settings.read_text())
        values["deviceName"] = upload_name
        settings.write_text(json.dumps(values), encoding="utf-8")

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            requests.append("GET")
            body = json.dumps({"progress": "/body/DocFragment[1]/body/h1[1]" if equal else "invalid-anchor",
                               "percentage": 0.0 if equal else 0.99,
                               "device": "Local QA", "device_id": "local-qa"}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_PUT(self):
            requests.append("PUT")
            uploads.append(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
            self.send_response(200 if upload_name is not None else 500)
            self.send_header("Content-Length", "0")
            self.end_headers()

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    root = sd / ".crosspoint"
    (root / "koreader.json").write_text(json.dumps({"cfgVersion": 2, "username": "local-qa",
        "password": "synthetic-only", "serverUrl": f"http://127.0.0.1:{server.server_port}",
        "matchMethod": 0, "sendMetadata": False, "syncBehavior": 1}), encoding="utf-8")
    (root / "wifi.json").write_text(json.dumps({"lastConnectedSsid": "LocalQA", "credentials": [
        {"ssid": "LocalQA", "password_obf": "", "password_len": 0, "password_crc32": 0}]}), encoding="utf-8")
    if buttons:
        settings = root / "settings.json"
        values = json.loads(settings.read_text(encoding="utf-8"))
        values.update(longPressMenuAction=7)
        settings.write_text(json.dumps(values), encoding="utf-8")
        actions = ["1800:CONFIRM:120", "6200:CONFIRM:1300"]
    else:
        actions = ["1800:TAP:240,350", "5000:TAP:240,350", "7000:TAP:385,750"]
        actions += [f"{8500+i*400}:DOWN:100" for i in range(11)]
        actions += ["14200:CONFIRM:100"]
    if upload_name is not None:
        actions += ["22000:DOWN:100", "23000:CONFIRM:100", "30000:QUIT"]
    else:
        actions += ["26000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_WIFI_NETWORKS="LocalQA:-40:open",
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"13700:{output}/menu.bmp;23000:{output}/comparison.bmp")
    try:
        with (output / "run.log").open("x", encoding="utf-8") as log:
            result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=40)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    assert result.returncode == 0
    log = (output / "run.log").read_text(encoding="utf-8")
    assert requests.count("GET") == 2, requests
    assert requests.count("PUT") == (1 if upload_name is not None else 0)
    assert "Smart sync comparison unknown; opening manual selection" in log
    if upload_name is not None:
        assert uploads[0]["device"] == upload_name, uploads[0]["device"]
    else:
        assert "Updating progress:" not in log
    report = {"passed": True, "scenario": "explicit_named_upload" if upload_name is not None else "equal" if equal else "unmapped_remote_ahead",
              "server": "loopback only", "uploads": len(uploads), "requests": requests,
              "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--equal", action="store_true")
    p.add_argument("--buttons", action="store_true", help="Use the YACP Menu hold shortcut on X3/X4")
    p.add_argument("--upload-name", help="Explicitly upload to the synthetic loopback server and check the device label")
    a = p.parse_args()
    run(a.program, a.output, a.equal, a.upload_name, a.buttons)
