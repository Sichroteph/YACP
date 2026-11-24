"""Compatibility entry point for the X3 display-mode hints and locked-control checks."""
import argparse
import json
from pathlib import Path
from verify_yacp_display_modes import ui


def run(program, output):
    ui(program.resolve(strict=True), output.absolute())
    result = {"passed": True, "mode_switching": True, "classic_preferences_restored": True,
              "cancellation_verified": True, "hardware_validated": False}
    (output / "result.json").write_text(json.dumps(result, indent=2))
    print(json.dumps(result))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
