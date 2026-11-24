"""PlatformIO pre-hook. Reject retired roots and unreserved build identities."""
from pathlib import Path
import json
import sys

Import("env")

root = Path(env["PROJECT_DIR"]).resolve()
sys.path.insert(0, str(root / "scripts"))
from check_yacp_source import consume_ticket

try:
    path, ticket = consume_ticket(root, env["PIOENV"])
except Exception as exc:
    raise RuntimeError(f"YACP source guard: {exc}") from exc
ticket["board"] = env.get("BOARD")
ticket["build_flags"] = env.get("BUILD_FLAGS")
path.write_text(json.dumps(ticket, indent=2), encoding="utf-8")
print(f"YACP source verified: {ticket['version']} / {env['PIOENV']} / {ticket['source_sha256'][:12]}")
