"""Compatibility entry point for the two-mode X3 settings scenario."""
import argparse
from pathlib import Path
from verify_yacp_refresh_hints import run

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    run(a.program, a.output)
