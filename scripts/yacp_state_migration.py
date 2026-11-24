"""Review shared state.json without activating a new firmware or old frame cache."""
from copy import deepcopy

SCALAR_DEFAULTS = {"readerActivityLoadCount": 0, "lastSleepFromReader": False, "showBootScreen": True}
PENDING_DEFAULTS = {"pendingBookmarkSpine": 65535, "pendingBookmarkProgress": -1,
                    "pendingBookmarkParagraphIndex": 65535, "pendingClippingIndex": 65535}
POWER_DEFAULTS = {"activeSeconds": 0, "cycleStartDay": 0, "readerPageDisplays": 0, "sleepSamples": 0,
                  "lastPercent": 255, "chargePendingPercent": 255, "hasCycleStartDay": False,
                  "cycleConfirmed": False, "chargePending": False, "samples": []}


def uint(value, maximum):
    return type(value) is int and 0 <= value <= maximum


def sd_path(value, allow_empty=True):
    return (isinstance(value, str) and ((not value and allow_empty) or
            (value.startswith("/") and not value.startswith("//") and "\\" not in value
             and not any(part in (".", "..") for part in value.split("/"))
             and not any(ord(char) < 32 for char in value))))


def candidate(source):
    if not isinstance(source, dict):
        raise ValueError("Expected frozen YACP state.json object")
    output, decisions, blocked = {}, [], []

    def record(key, status, reason):
        decisions.append({"source_key": key, "status": status, "reason": reason})
        if status == "blocked":
            blocked.append(key)

    for key, value in source.items():
        if key == "openEpubPath":
            if sd_path(value):
                output[key] = value
                record(key, "converted", "Open-book path retained independently of its progress migration")
            else:
                record(key, "blocked", "Unsafe or invalid book path")
        elif key in ("favoriteSleepImagePath", "preferredSleepFolderPath"):
            record(key, "neutral_archived" if value == "" else "blocked",
                   "Empty legacy gallery preference" if value == "" else "Gallery preference requires its runtime port")
        elif key in PENDING_DEFAULTS:
            neutral = type(value) in (int, float) and value == PENDING_DEFAULTS[key]
            record(key, "neutral_archived" if neutral else "blocked",
                   "No pending legacy navigation" if neutral else "Pending bookmark or clipping must be resolved before activation")
        elif key == "lastSleepRenderedQuickResume":
            record(key, "runtime_archived" if type(value) is bool else "blocked",
                   "Old framebuffer cache flag is not a new CrossPoint frame; first activation must be a cold boot")
        elif key in SCALAR_DEFAULTS:
            valid = uint(value, 255) if key == "readerActivityLoadCount" else type(value) is bool
            if valid:
                output[key] = value
            record(key, "converted" if valid else "blocked", "Same shared-state field and unit")
        elif key in ("recentSleepImages", "recentSleepPos", "recentSleepFill", "powerHistory"):
            continue
        else:
            record(key, "blocked", "Unknown state field; retain original before reviewing its meaning")

    images = source.get("recentSleepImages", [])
    pos, fill = source.get("recentSleepPos", 0), source.get("recentSleepFill", 0)
    valid = (isinstance(images, list) and len(images) <= 16 and all(uint(v, 65535) for v in images)
             and uint(pos, 15) and uint(fill, len(images)))
    for key in ("recentSleepImages", "recentSleepPos", "recentSleepFill"):
        if key in source:
            record(key, "converted" if valid else "blocked", "Same 16-entry ring; source directory enumeration must remain unchanged")
    if valid:
        output.update(recentSleepImages=images + [0] * (16 - len(images)), recentSleepPos=pos, recentSleepFill=fill)

    power = source.get("powerHistory", {})
    valid = isinstance(power, dict) and not (power.keys() - POWER_DEFAULTS.keys())
    if valid:
        power = {**deepcopy(POWER_DEFAULTS), **deepcopy(power)}
        for key in ("activeSeconds", "cycleStartDay", "readerPageDisplays"):
            valid &= uint(power[key], 0xffffffff)
        valid &= uint(power["sleepSamples"], 65535)
        for key in ("lastPercent", "chargePendingPercent"):
            valid &= uint(power[key], 100) or (type(power[key]) is int and power[key] == 255)
        for key in ("hasCycleStartDay", "cycleConfirmed", "chargePending"):
            valid &= type(power[key]) is bool
        valid &= isinstance(power["samples"], list) and len(power["samples"]) <= 21
        if valid:
            previous = -1
            for sample in power["samples"]:
                if (not isinstance(sample, list) or len(sample) != 2 or not uint(sample[0], 65535)
                        or not uint(sample[1], 100) or sample[0] < previous):
                    valid = False
                    break
                previous = sample[0]
            if power["chargePending"] and power["chargePendingPercent"] > 100:
                valid = False
    if "powerHistory" in source:
        record("powerHistory", "converted" if valid else "blocked", "Bounded autonomy object; no counters or samples discarded")
    if valid:
        output["powerHistory"] = power
    output.setdefault("openEpubPath", "")
    for key, value in SCALAR_DEFAULTS.items():
        output.setdefault(key, value)
    # These upstream fields have no legacy counterparts. They describe a new
    # overlay/frame cache, not old reading progress or device settings.
    output.update(recentOverlaySleepImages=[0] * 16, recentOverlaySleepPos=0, recentOverlaySleepFill=0,
                  sleepFrameInverted=False)
    assert {r["source_key"] for r in decisions} == set(source)
    return {"state": output, "decisions": decisions, "blocked_keys": sorted(blocked),
            "requires_cold_boot": True, "requires_unchanged_gallery_order": bool(fill),
            "gallery_order_verified": False, "ready_to_activate": False}
