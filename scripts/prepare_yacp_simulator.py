"""Apply YACP compatibility fixes to the pinned simulator dependency."""

from pathlib import Path
import sys

Import("env")

root = Path(env["PROJECT_DIR"]).resolve()
sys.path.insert(0, str(root / "scripts"))
from patch_yacp_simulator_storage import patch as patch_storage

simulator_src = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / "simulator" / "src"
patch_storage(simulator_src / "HalStorage.cpp")

# The device's verified OTA metadata client configures this ESP-IDF field.
# The host shim should honor it for both GET entry points.
http_client = simulator_src / "esp_http_client.h"
text = http_client.read_text(encoding="utf-8")
field = "  bool disable_auto_redirect = false;"
old_call = "          response, true, timeouts(handle->config)))"
new_call = "          response, !handle->config.disable_auto_redirect, timeouts(handle->config)))"
if field not in text:
    anchor = "  bool keep_alive_enable = false;"
    if text.count(anchor) != 1 or text.count(old_call) != 2:
        raise ValueError("Unknown pinned simulator HTTP client layout")
    text = text.replace(anchor, anchor + "\n" + field).replace(old_call, new_call)
    http_client.write_text(text, encoding="utf-8")
elif text.count(new_call) != 2:
    raise ValueError("Simulator HTTP client contains an incomplete compatibility patch")
