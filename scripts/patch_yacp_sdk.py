"""Apply reviewed, idempotent YACP SDK deltas without replacing the SDK tree."""
from pathlib import Path
import subprocess
import json


def apply_patches(root):
    sdk = root / "freeink-sdk"
    policy = json.loads((root / ".yacp-source.json").read_text(encoding="utf-8"))
    for name in policy["sdk_patches"]:
        patch = root / "scripts/sdk_patches" / name
        if not patch.is_file():
            raise ValueError(f"Required YACP SDK patch is missing: {name}")
        args = ["git", "-C", str(sdk), "apply", "--ignore-whitespace"]
        reverse = subprocess.run(args + ["--reverse", "--check", str(patch)],
                                 capture_output=True)
        if reverse.returncode == 0:
            continue
        check = subprocess.run(args + ["--check", str(patch)], capture_output=True, text=True)
        if check.returncode:
            raise ValueError(f"SDK patch {patch.name} no longer applies: {check.stderr}")
        subprocess.run(args + [str(patch)], check=True)


if __name__ == "__main__":
    apply_patches(Path(__file__).resolve().parents[1])
