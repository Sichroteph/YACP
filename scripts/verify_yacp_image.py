"""Read-only identity and integrity check for a raw YACP ESP application image.

Layout follows the installed ESP-IDF esp_app_format.h / esp_app_desc.h and
esptool's segment checksum and appended SHA-256 format. Not a flasher.
"""
import argparse
from functools import reduce
import hashlib
import json
from operator import xor
from pathlib import Path
import re
import struct


PROFILES = {
    "tiny": (5, "x4", "tiny"),
    "xlarge": (5, "x4", "xlarge"),
    "x4pro": (9, "x4pro", "x4pro-tiny"),
    "x4pro-go": (9, "x4pro", "x4pro-go"),
    # The C3 firmware tag intentionally names the X3/X4 OTA compatibility
    # class "x4" even when this private image compiles only X3 hardware.
    "x3-go": (5, "x4", "x3-go"),
    "x4pro-xlarge": (9, "x4pro", "x4pro-xlarge"),
    "x4c": (9, "x4c", "x4c-tiny"),
    "x4c-xlarge": (9, "x4c", "x4c-xlarge"),
}
OTA_SLOT_BYTES = 0x640000  # Both application slots in partitions.csv.


def verify(data, environment, version):
    chip, board, suffix = PROFILES[environment]
    if not 288 <= len(data) <= OTA_SLOT_BYTES:
        raise ValueError("Image is truncated or exceeds the CrossPoint OTA slot")
    if data[0] != 0xE9 or not 1 <= data[1] <= 16 or data[23] != 1:
        raise ValueError("Expected an ESP application with appended SHA-256")
    if struct.unpack_from("<H", data, 12)[0] != chip:
        raise ValueError("Image chip does not match the requested target")
    offset, checksum = 24, 0xEF
    for index in range(data[1]):
        if offset + 8 > len(data):
            raise ValueError("Truncated segment header")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if offset + size > len(data) or (index == 0 and size < 256):
            raise ValueError("Truncated segment data or application descriptor")
        checksum = reduce(xor, memoryview(data)[offset:offset + size], checksum)
        offset += size
    checksum_offset = (offset // 16) * 16 + 15
    if checksum_offset + 33 != len(data):
        raise ValueError("Unexpected trailing data or missing image digest")
    if data[checksum_offset] != checksum:
        raise ValueError("Segment checksum mismatch")
    if hashlib.sha256(data[:checksum_offset + 1]).digest() != data[-32:]:
        raise ValueError("Appended SHA-256 mismatch")
    if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
        raise ValueError("Missing application descriptor at first segment start")

    def string_at(offset):
        value = data[offset:offset + 32]
        if b"\0" not in value:
            raise ValueError("Unterminated application descriptor field")
        return value.split(b"\0", 1)[0].decode("ascii")

    embedded_version, project = string_at(48), string_at(80)
    if project != "YACP" or embedded_version != version + "-" + suffix:
        raise ValueError(f"Unexpected application identity: {project} {embedded_version}")
    tags = {m.decode("ascii") for m in re.findall(rb"CROSSPOINT-BOARD-V1:([a-z0-9]+);", data)}
    if tags != {board}:
        raise ValueError(f"Missing or conflicting board tags: {sorted(tags)}")
    return {"project": project, "embedded_version": embedded_version, "board": board,
            "chip_id": chip, "environment": environment, "bytes": len(data),
            "ota_headroom_bytes": OTA_SLOT_BYTES - len(data),
            "sha256": hashlib.sha256(data).hexdigest(), "image_integrity_verified": True,
            "hardware_validated": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--environment", choices=PROFILES, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.image.read_bytes(), args.environment, args.version), indent=2))
