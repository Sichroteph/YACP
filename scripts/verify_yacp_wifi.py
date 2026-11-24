"""Exercise CrossPoint saved-network fallback and Show on synthetic simulator data."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from verify_yacp_simulator_stats import create_fixture


def run(program, output, interrupt=False):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    version_match = re.fullmatch(r'YACP-(.+)-simulator_x4_pro\.elf', program.name)
    if version_match:
        settings_file = sd / '.crosspoint/settings.json'
        settings = json.loads(settings_file.read_text(encoding='utf-8'))
        settings['buttonLayoutPromptVersion'] = f'{version_match.group(1)}-simulator'
        settings_file.write_text(json.dumps(settings), encoding='utf-8')
    credentials = {"lastConnectedSsid": "Missing", "credentials": [
        {"ssid": ssid, "password_obf": "", "password_len": 0, "password_crc32": 0}
        for ssid in ("Missing", "Fallback")]}
    wifi = sd / ".crosspoint/wifi.json"
    wifi.write_text(json.dumps(credentials), encoding="utf-8")
    actions = ["1800:TAP:240,756", "2600:DOWN:100", "3000:DOWN:100", "3400:DOWN:100",
               "4200:CONFIRM:100", "6000:CONFIRM:100"]
    if interrupt:
        actions += ["8000:CONFIRM:100"]
    actions += ["15000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_WIFI_NETWORKS="Fallback:-40:open;Other:-60:open",
               CROSSPOINT_SIM_WIFI_CONNECT="timeout" if interrupt else "success",
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"5500:{output}/mode.bmp;12000:{output}/result.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=28)
    assert result.returncode == 0
    log = (output / "run.log").read_text(encoding="utf-8")
    state = json.loads(wifi.read_text(encoding="utf-8"))
    assert "Attempting saved network: Missing" in log
    if interrupt:
        assert "User requested manual network list" in log
        assert state["lastConnectedSsid"] == "Missing"
    else:
        assert "Saved network failed: Missing" in log
        assert "Attempting saved network: Fallback" in log
        assert state["lastConnectedSsid"] == "Fallback"
    report = {"passed": True, "scenario": "show_interrupt" if interrupt else "saved_network_fallback",
              "radio_and_access_point_hints_validated": False, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--interrupt", action="store_true")
    a = p.parse_args()
    run(a.program, a.output, a.interrupt)
