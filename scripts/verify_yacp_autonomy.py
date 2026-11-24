"""Exercise autonomy with an isolated copy of only the real history object."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from verify_yacp_simulator_stats import create_fixture

def run(program, source_state, output, device="pro"):
    program = program.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    history = json.loads(source_state.read_text(encoding="utf-8"))["powerHistory"]
    state = sd / ".crosspoint/state.json"
    state.write_text(json.dumps({"openEpubPath": "", "powerHistory": history}), encoding="utf-8")
    actions = ["1800:BACK:100" if device == "x3" else "1800:TAP:240,756"]
    actions += [f"{2700+i*400}:DOWN:100" for i in range(6)]
    actions += ["5600:CONFIRM:100", "9000:BACK:100" if device == "x3" else "9000:TAP:100,766", "12000:QUIT"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=";".join(actions),
               CROSSPOINT_SIM_SCREENSHOTS=f"5100:{output}/menu.bmp;7500:{output}/autonomy.bmp;10500:{output}/return.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=25)
    assert result.returncode == 0
    assert json.loads(state.read_text(encoding="utf-8"))["powerHistory"] == history
    for name in ("menu", "autonomy", "return"):
        assert (output / (name + ".bmp")).is_file()
    log = (output / "run.log").read_text(encoding="utf-8")
    assert "PowerStats" in log, "Autonomy screen was not opened"
    (output / "result.json").write_text(json.dumps({"passed": True,
        "real_history_unchanged": True, "hardware_validated": False}), encoding="utf-8")
    print("Autonomy screen opened; real copied history unchanged")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--source-state", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--device", choices=["pro", "x3"], default="pro")
    a = p.parse_args()
    run(a.program, a.source_state, a.output, a.device)
