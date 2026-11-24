"""Origin-aware settings conversion candidates, never an installer.

Rules compare the frozen YACP CrossPointSettings/JsonSettingsIO/SettingsList
with the CrossPoint 1.6.5 port. Unknown or unsupported values stay explicit
blockers. Original documents are retained byte-for-byte by the snapshot tool.
"""
from yacp_data_formats import font_point_size


# Identical persisted meanings, not merely matching C++ member names.
RANGES = {
    "fontFamily": (0, 1), "paragraphAlignment": (0, 4), "orientation": (0, 3),
    "sleepScreenCoverMode": (0, 1), "hideBatteryPercentage": (0, 2),
    "sideButtonLayout": (0, 3), "imageRendering": (0, 2), "xtcStatusBarMode": (0, 2),
    "statusBarProgressBar": (0, 2), "statusBarProgressBarThickness": (0, 2),
    "statusBarTitle": (0, 2), "statusBarTimeLeft": (0, 2),
    "screenMargin": (5, 40), "sleepTimeoutMinutes": (1, 31),
    "readingIdleTimeThresholdUnits": (3, 60), "clockUtcOffsetQ": (0, 104),
    "lineHeightPercent": (70, 200), "fileBrowserDisplay": (0, 1),
    "statusBarProgressBarPosition": (0, 1),
    "sideButtonLongPress": (0, 4),
    "frontButtonOrientationAware": (0, 2),
    "recentBooksView": (0, 1),
}
TOGGLES = {
    "extraParagraphSpacing", "textAntiAliasing", "embeddedStyle", "hyphenationEnabled",
    "trackReadingStats", "autoBackupStats", "statusBarChapterPageCount", "statusBarBookProgressPercentage",
    "statusBarBattery", "quickResumeSleepScreen", "pwrBtnFootnoteBack", "showHiddenFiles",
    "removeReadBooksFromRecents", "moveFinishedToReadFolder", "clockFormat", "clockHasBeenSynced",
    "forceParagraphIndents", "readerDarkMode", "hideFileExtension", "statusBarBatteryCurrent",
    # YACP's displayPowerSaving member was persisted as fadingFix. Both renderers
    # pass this value as the SDK's turnOffScreen argument after a refresh.
    "fadingFix", "clockDateHasBeenSynced",
    "sideButtonOrientationAware", "guideReadingEnabled", "stablePageNumbers", "publisherPageNumbers",
}
# Missing values intentionally have no fallback mapping.
ENUM_MAPS = {
    "sleepScreen": {0: 0, 1: 1, 2: 2, 3: 3, 4: 5, 5: 4, 9: 6},
    "sleepScreenCoverFilter": {0: 3, 1: 1, 2: 2, 3: 0},
    "refreshFrequency": {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 6, 6: 5},
    "refreshAction": {0: 0, 1: 1},
    "uiTheme": {0: 0, 1: 1, 2: 2, 3: 3, 6: 5},
    "shortPwrBtn": {0: 0, 1: 1, 2: 2, 3: 3, 16: 4},
    "longPressButtonBehavior": {0: 0, 1: 1, 2: 2, 3: 3},
    # These bindings preserve the original EPUB command IDs, including clipping.
    "longPressBackAction": {i: i for i in range(22)},
    "longPressMenuAction": {i: i for i in range(22)},
    "longPwrBtn": {i: i for i in range(23)},
}
PENDING = {
    "displayPowerSaving", "epubRenderMode",
    "opdsPassword", "opdsPassword_obf", "opdsServerUrl", "opdsUsername",
    "statusBar",
}
SETUP_KEYS = {"buttonLayoutPromptVersion", "buttonLayoutPromptHadPriorChoice", "buttonLayoutPromptSeen"}
BUTTON_KEYS = ("frontButtonBack", "frontButtonConfirm", "frontButtonLeft", "frontButtonRight")
READER_BUTTON_KEYS = ("readerFrontButtonBack", "readerFrontButtonConfirm", "readerFrontButtonLeft", "readerFrontButtonRight")
LANGUAGES = set("AR AZ BE BS BG CA CAV CS DA DE EN ES FA FR FI HE HU ID IT KK LT NL NB OOK PL PT P2 RO RU SK SI SV TR UK VI".split())


def integer(value, minimum, maximum):
    return type(value) is int and minimum <= value <= maximum


def candidate(source, variant, sd_font_sizes=None):
    if not isinstance(source, dict) or variant not in ("tiny", "xlarge"):
        raise ValueError("Expected a YACP settings object and an explicit source variant")
    settings = {"uiTheme": 5}
    decisions = {}

    def record(key, status, reason, target=None):
        decisions[key] = {"source_key": key, "status": status, "reason": reason}
        if target:
            decisions[key]["target_keys"] = target

    for key, value in source.items():
        if key in RANGES:
            if integer(value, *RANGES[key]):
                settings[key] = value
                record(key, "converted", "Same persisted unit and meaning", [key])
            else:
                record(key, "blocked", "Invalid or unsupported numeric range")
        elif key in TOGGLES or key == "bionicReadingEnabled":
            if isinstance(value, bool) or integer(value, 0, 1):
                target = "focusReadingEnabled" if key == "bionicReadingEnabled" else key
                settings[target] = int(value)
                record(key, "converted", "Equivalent switch", [target])
            else:
                record(key, "blocked", "Invalid switch value")
        elif key == "fileBrowserPreview":
            if "fileBrowserDisplay" in source:
                record(key, "archived_only", "Explicit fileBrowserDisplay takes precedence")
            elif integer(value, 0, 1):
                settings["fileBrowserDisplay"] = value
                record(key, "converted", "Legacy key for the one-line or two-line layout", ["fileBrowserDisplay"])
            else:
                record(key, "blocked", "Invalid legacy file browser layout")
        elif key == "shortPwrBtn" and integer(value, 4, 22) and value != 16:
            settings["readerShortPowerAction"] = value
            settings["shortPwrBtn"] = 0
            record(key, "converted", "Distinct EPUB action on C3; ordinary CrossPoint controls elsewhere",
                   ["readerShortPowerAction", "shortPwrBtn"])
        elif key in ENUM_MAPS:
            mapping = ENUM_MAPS[key]
            if type(value) is int and value in mapping:
                settings[key] = mapping[value]
                record(key, "converted", "Explicit semantic enum mapping", [key])
            else:
                record(key, "blocked", "Behavior not ported or invalid enum value")
        elif key == "deviceName":
            if (isinstance(value, str) and (not value or 2 <= len(value.encode("utf-8")) <= 20)
                    and not any(ord(char) < 32 or ord(char) == 127 for char in value)):
                settings[key] = value
                record(key, "converted", "Device display name preserved; empty selects hardware default", [key])
            else:
                record(key, "blocked", "Invalid or oversized device display name")
        elif key == "sdFontFamilyName":
            if (isinstance(value, str) and len(value.encode("utf-8")) < 32
                    and not any(char in value for char in ("/", "\\", "\0")) and value not in (".", "..")):
                settings[key] = value
                record(key, "converted", "SD family name preserved", [key])
            else:
                record(key, "blocked", "Unsafe or oversized SD font family")
        elif key == "language":
            if isinstance(value, str) and value in LANGUAGES:
                settings[key] = value
                record(key, "converted", "Stable language code", [key])
            else:
                record(key, "blocked", "Unknown language code")
        elif key == "hideClock":
            if integer(value, 0, 2):
                settings["clockShowHeader"] = int(value != 2)
                settings["statusBarClock"] = int(value == 0)
                record(key, "converted", "Header and reader visibility split", ["clockShowHeader", "statusBarClock"])
            else:
                record(key, "blocked", "Invalid clock visibility")
        elif key in SETUP_KEYS:
            record(key, "archived_only", "Old setup marker must not override CrossPoint Home or touch defaults")
        elif key in PENDING:
            record(key, "blocked", "Retained in original; behavior or separate-store migration remains pending")
        elif key not in {"fontSize", "sdFontSizeRange", "lineSpacing", "frontButtonLayout", "sleepTimeout",
                        "readerFrontButtonsEnabled", "tiltPageTurn", "tiltPageTurnDirection",
                        "tiltPageTurnDirectionSchema", *BUTTON_KEYS, *READER_BUTTON_KEYS}:
            record(key, "blocked", "Unknown key; review before activation")

    tilt_keys = {key for key in ("tiltPageTurn", "tiltPageTurnDirection", "tiltPageTurnDirectionSchema") if key in source}
    if tilt_keys:
        mode = source.get("tiltPageTurn", 0)
        direction = source.get("tiltPageTurnDirection")
        schema = source.get("tiltPageTurnDirectionSchema", 1)
        if direction is None:
            valid = integer(mode, 0, 2) and "tiltPageTurnDirectionSchema" not in source and "tiltPageTurnDirection" not in source
            active_mode, remembered_mode = mode, mode or 1
        else:
            valid = integer(mode, 0, 1) and integer(direction, 0, 3) and integer(schema, 1, 2)
            if valid:
                # Frozen JsonSettingsIO swaps directions 0/1 before schema 2.
                if schema == 1 and direction < 2:
                    direction = 1 - direction
                remembered_mode = (2, 1, 3, 4)[direction]
                active_mode = remembered_mode if mode else 0
        if valid:
            settings.update(tiltPageTurn=active_mode, tiltPageTurnLastMode=remembered_mode)
        for key in tilt_keys:
            record(key, "consumed" if valid and key.endswith("Schema") else "converted" if valid else "blocked",
                   "Origin-aware gyro axis and sign; disabled gesture retained" if valid else "Invalid or unknown tilt schema",
                   ["tiltPageTurn", "tiltPageTurnLastMode"] if valid else None)

    if "fontSize" in source:
        try:
            sizes = (sd_font_sizes or {}).get(source.get("sdFontFamilyName"))
            settings["fontSize"] = font_point_size(source, variant, sizes)
            record("fontSize", "converted", "Legacy index resolved from the captured SD catalogue or built-in variant", ["fontSize"])
        except (ValueError, KeyError):
            record("fontSize", "blocked", "Invalid font index or missing captured SD family catalogue")
    if "sdFontSizeRange" in source:
        status = "consumed" if decisions.get("fontSize", {}).get("status") == "converted" else "blocked"
        record("sdFontSizeRange", status, "Indexed available SD size takes precedence over the old nominal range target", ["fontSize"])

    if "lineSpacing" in source:
        if "lineHeightPercent" in source:
            record("lineSpacing", "superseded", "Numeric line height is authoritative")
        elif integer(source["lineSpacing"], 0, 2):
            if source.get("sdFontFamilyName"):
                scale = (95, 100, 110)
            elif source.get("fontFamily", 0) == 1:
                scale = (95, 110, 130)
            else:
                scale = (90, 100, 120)
            settings["lineHeightPercent"] = scale[source["lineSpacing"]]
            record("lineSpacing", "converted", "Old YACP family-specific coefficient, not CrossPoint's preset index",
                   ["lineHeightPercent"])
        else:
            record("lineSpacing", "blocked", "Invalid legacy line spacing")
    if "sleepTimeout" in source:
        if "sleepTimeoutMinutes" in source:
            record("sleepTimeout", "superseded", "Explicit timeout in minutes is authoritative")
        elif integer(source["sleepTimeout"], 0, 4):
            settings["sleepTimeoutMinutes"] = (1, 5, 10, 15, 30)[source["sleepTimeout"]]
            record("sleepTimeout", "converted", "Legacy enum resolved to minutes", ["sleepTimeoutMinutes"])
        else:
            record("sleepTimeout", "blocked", "Invalid legacy sleep timeout")

    present_buttons = [key for key in BUTTON_KEYS if key in source]
    if present_buttons:
        values = [source[key] for key in present_buttons]
        valid = len(values) == 4 and all(integer(v, 0, 3) for v in values) and len(set(values)) == 4
        for key in present_buttons:
            if valid:
                settings[key] = source[key]
            record(key, "converted" if valid else "blocked", "Complete unique front-button mapping required", [key])
    if "frontButtonLayout" in source:
        if present_buttons:
            record("frontButtonLayout", "superseded", "Explicit button map is authoritative")
        elif integer(source["frontButtonLayout"], 0, 3):
            values = ((0, 1, 2, 3), (2, 3, 0, 1), (1, 2, 0, 3), (0, 1, 3, 2))[source["frontButtonLayout"]]
            settings.update(zip(BUTTON_KEYS, values))
            record("frontButtonLayout", "converted", "Legacy hardware layout resolved to four logical buttons", list(BUTTON_KEYS))
        else:
            record("frontButtonLayout", "blocked", "Invalid legacy front layout")
    present_reader = [key for key in READER_BUTTON_KEYS if key in source]
    values = [source[key] for key in present_reader]
    valid_reader = len(values) == 4 and all(integer(v, 0, 3) for v in values) and len(set(values)) == 4
    for key in present_reader:
        if valid_reader:
            settings[key] = source[key]
        record(key, "converted" if valid_reader else "blocked", "Complete unique C3 reader mapping required", [key])
    if "readerFrontButtonsEnabled" in source:
        enabled = source["readerFrontButtonsEnabled"]
        if (isinstance(enabled, bool) or integer(enabled, 0, 1)) and (not enabled or valid_reader):
            settings["readerFrontButtonsEnabled"] = int(enabled)
            record("readerFrontButtonsEnabled", "converted", "C3-only reader profile; S3 Home and touch remain CrossPoint",
                   ["readerFrontButtonsEnabled"])
        else:
            record("readerFrontButtonsEnabled", "blocked", "Enabled profile requires a complete unique mapping")
    if "clockUtcOffsetQ" in settings:
        # CrossPoint Timezones::activeIndex maps legacy offsets to fixed UTC entries.
        settings["clockTimezone"] = 255
        settings["clockDst"] = 2
    assert set(decisions) == set(source), "Every source key must be accounted for"
    return {"settings": settings, "decisions": [decisions[key] for key in sorted(decisions)],
            "blocked_keys": sorted(key for key, value in decisions.items() if value["status"] == "blocked"),
            "source_variant": variant, "missing_keys_use_crosspoint_defaults": True,
            "ready_to_activate": False, "migration_validated": False}
