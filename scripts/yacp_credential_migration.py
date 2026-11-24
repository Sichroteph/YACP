"""Pure credential conversion for explicit, known source and target devices.

No discovery, key guessing, logging, file writes or firmware activation. YACP's
CPV1 envelope and CrossPoint's length/CRC fields are different formats. These
checks detect accidental corruption; neither XOR scheme is encryption.
"""
import base64
import binascii
from copy import deepcopy
import struct
import zlib


def hardware_key(value):
    if not isinstance(value, bytes) or len(value) != 6:
        raise ValueError("An explicit six-byte device key is required")
    return value


def xor_bytes(data, key):
    hardware_key(key)
    return bytes(value ^ key[i % 6] for i, value in enumerate(data))


def fnv1a(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result


def decode_yacp_password(encoded, source_key, source_format="cpv1", maximum=64):
    hardware_key(source_key)
    if source_format not in ("cpv1", "reviewed_legacy"):
        raise ValueError("Unknown source credential format")
    if type(maximum) is not int or not 0 <= maximum <= 4096:
        raise ValueError("Invalid credential conversion bound")
    if not isinstance(encoded, str) or len(encoded) > 4 * ((maximum + 10) // 3):
        raise ValueError("Invalid or oversized encoded credential")
    if not encoded:
        return b""
    try:
        payload = xor_bytes(base64.b64decode(encoded, validate=True), source_key)
    except (ValueError, binascii.Error):
        raise ValueError("Invalid credential encoding") from None
    if source_format == "cpv1":
        if len(payload) < 8 or payload[:4] != b"CPV1":
            raise ValueError("Wrong source key or unsupported credential envelope")
        password = payload[8:]
        if struct.unpack_from("<I", payload, 4)[0] != fnv1a(source_key + password):
            raise ValueError("Credential integrity check failed")
    else:
        # This explicit mode is only for a separately reviewed pre-CPV1 store.
        # It cannot establish that the supplied source hardware key is correct.
        if payload.startswith(b"CPV1"):
            raise ValueError("Validated envelopes must use their checked decoder")
        password = payload
    if len(password) > maximum or b"\0" in password:
        raise ValueError("Credential exceeds the reviewed conversion bound")
    return password


def wifi_candidate(document, source_key, target_key, source_format="cpv1"):
    """Convert every network or fail as a unit. Preserve unknown fields only in originals."""
    hardware_key(source_key)
    hardware_key(target_key)
    if not isinstance(document, dict) or set(document) != {"credentials", "lastConnectedSsid"}:
        raise ValueError("Unreviewed Wi-Fi store schema")
    rows = document["credentials"]
    if not isinstance(rows, list) or len(rows) > 8:
        raise ValueError("Wi-Fi network count exceeds target capacity")
    last = document["lastConnectedSsid"]
    if not isinstance(last, str) or len(last.encode("utf-8")) > 32 or "\0" in last:
        raise ValueError("Invalid last-connected network")
    result = {"credentials": [], "lastConnectedSsid": last}
    seen = set()
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"ssid", "password_obf"}:
            raise ValueError("Unreviewed Wi-Fi credential schema")
        ssid = row["ssid"]
        if (not isinstance(ssid, str) or not 0 < len(ssid.encode("utf-8")) <= 32
                or "\0" in ssid or ssid in seen):
            raise ValueError("Invalid or duplicate network name")
        seen.add(ssid)
        password = decode_yacp_password(row["password_obf"], source_key, source_format)
        result["credentials"].append({"ssid": ssid,
            "password_obf": base64.b64encode(xor_bytes(password, target_key)).decode("ascii"),
            "password_len": len(password), "password_crc32": zlib.crc32(password)})
    return {"candidate": deepcopy(result), "source_format": source_format,
            "source_integrity_verified": source_format == "cpv1",
            "ready_to_activate": False}
