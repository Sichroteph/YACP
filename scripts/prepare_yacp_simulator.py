"""Apply YACP compatibility fixes to the pinned simulator dependency."""

from pathlib import Path
import sys

Import("env")

root = Path(env["PROJECT_DIR"]).resolve()
sys.path.insert(0, str(root / "scripts"))
from patch_yacp_simulator_storage import patch as patch_storage

simulator_src = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"] / "simulator" / "src"
patch_storage(simulator_src / "HalStorage.cpp")

storage = simulator_src / "HalStorage.cpp"
text = storage.read_text(encoding="utf-8")
if "#include <ReadingDataPath.h>" not in text:
    text = text.replace('#include "HalStorage.h"', '#include "HalStorage.h"\n#include <ReadingDataPath.h>', 1)
    anchor = "std::string resolveStoragePath(const char *path) {"
    if text.count(anchor) != 1:
        raise ValueError("Unknown simulator storage path resolver")
    text = text.replace(anchor, anchor + "\n  path = reading_data::resolve(path);", 1)
    storage.write_text(text, encoding="utf-8")

# Host rendering cannot exercise the retained physical display wake path.
gpio = simulator_src / "HalGPIO.h"
text = gpio.read_text(encoding="utf-8")
if "supportsReaderWakePreview()" not in text:
    anchor = "public:"
    if anchor not in text:
        raise ValueError("Unknown simulator GPIO interface")
    text = text.replace(anchor, anchor + "\n  bool supportsReaderWakePreview() const { return false; }", 1)
    gpio.write_text(text, encoding="utf-8")
if "samplePhysicalSideButtons()" not in text:
    text = text.replace("public:", "public:\n  uint8_t samplePhysicalSideButtons() { return 0; }", 1)
    gpio.write_text(text, encoding="utf-8")

freertos = simulator_src / "freertos/FreeRTOS.h"
text = freertos.read_text(encoding="utf-8")
if "#define pdPASS" not in text:
    text += "\n#define pdPASS pdTRUE\n"
    freertos.write_text(text, encoding="utf-8")

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
