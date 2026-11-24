"""Audit omitted global settings against the frozen YACP loader.

Do not use this for per-book overrides: an absent book field inherits the global
profile, rather than the firmware's startup defaults. Unreviewed omissions block
the global experiment instead of silently adopting a new CrossPoint default.
"""
from yacp_settings_migration import candidate

# Frozen CrossPointSettings.h scalar inventory, checked by the source audit.
SOURCE_FIELDS = frozenset("""
autoBackupStats bionicReadingEnabled buttonLayoutPromptHadPriorChoice buttonLayoutPromptVersion
clockDateHasBeenSynced clockFormat clockHasBeenSynced clockUtcOffsetQ deviceName displayPowerSaving
embeddedStyle epubRenderMode extraParagraphSpacing fileBrowserDisplay fontFamily fontSize
forceParagraphIndents frontButtonBack frontButtonConfirm frontButtonLayout frontButtonLeft
frontButtonOrientationAware frontButtonRight guideReadingEnabled hideBatteryPercentage hideClock
hideFileExtension hyphenationEnabled imageRendering language lineHeightPercent lineSpacing
longPressBackAction longPressButtonBehavior longPressMenuAction longPwrBtn moveFinishedToReadFolder
opdsPassword opdsServerUrl opdsUsername orientation paragraphAlignment publisherPageNumbers
pwrBtnFootnoteBack quickResumeSleepScreen readerDarkMode readerFrontButtonBack readerFrontButtonConfirm
readerFrontButtonLeft readerFrontButtonRight readerFrontButtonsEnabled readingIdleTimeThresholdUnits
recentBooksView refreshAction refreshFrequency removeReadBooksFromRecents screenMargin sdFontFamilyName
sdFontSizeRange shortPwrBtn showHiddenFiles sideButtonLayout sideButtonLongPress sideButtonOrientationAware
sleepScreen sleepScreenCoverFilter sleepScreenCoverMode sleepTimeoutMinutes stablePageNumbers statusBar
statusBarBattery statusBarBatteryCurrent statusBarBookProgressPercentage statusBarChapterPageCount
statusBarProgressBar statusBarProgressBarPosition statusBarProgressBarThickness statusBarTimeLeft
statusBarTitle textAntiAliasing tiltPageTurn tiltPageTurnDirection trackReadingStats uiTheme xtcStatusBarMode
""".split())

STATUS_FIELDS = ("statusBarChapterPageCount", "statusBarBookProgressPercentage", "statusBarProgressBar",
                 "statusBarTitle", "statusBarBattery")
FRONT_FIELDS = ("frontButtonBack", "frontButtonConfirm", "frontButtonLeft", "frontButtonRight")


def global_candidate(source, variant, sd_font_sizes=None):
    result = candidate(source, variant, sd_font_sizes)
    decisions = []
    for key in sorted(SOURCE_FIELDS - source.keys()):
        row = {"source_field": key, "status": "blocked",
               "reason": "Omitted legacy field needs an origin-aware default review"}
        if key == "buttonLayoutPromptHadPriorChoice":
            row.update(status="runtime_only", reason="Derived old setup hint; CrossPoint startup controls take precedence")
        elif key == "epubRenderMode":
            row.update(status="runtime_only", reason="Old per-book rendering engine choice is not a global persisted setting")
        elif key == "displayPowerSaving" and "fadingFix" in source:
            row.update(status="stored_alias", reason="Member is persisted under fadingFix", source_key="fadingFix")
        elif key == "frontButtonLayout" and all(k in source for k in FRONT_FIELDS):
            row.update(status="superseded", reason="Explicit front-button values are reviewed by the ordinary converter")
        elif key == "lineSpacing" and "lineHeightPercent" in source:
            row.update(status="superseded", reason="Explicit numeric line height is authoritative")
        elif key == "statusBar" and all(k in source for k in STATUS_FIELDS):
            row.update(status="superseded", reason="All five fields written by the legacy status-bar preset are explicit")
        elif key == "opdsPassword" and "opdsPassword_obf" in source:
            row.update(status="stored_alias", reason="Obfuscated credential needs separate-store conversion",
                       source_key="opdsPassword_obf")
        elif key in ("opdsPassword", "opdsUsername", "opdsServerUrl"):
            # Frozen header initializes these to empty strings. This says nothing
            # about the separately saved opds.json, which is preserved.
            row.update(status="empty_legacy_default", reason="Absent legacy string initializes empty; separate server store still needs audit")
        elif key in ("trackReadingStats", "statusBarProgressBarPosition"):
            # Frozen header: lines 334 and 476; JsonSettingsIO::loadSettings keeps
            # fieldDefault. Non-debug builds also unconditionally track stats.
            value = 1 if key == "trackReadingStats" else 0
            result["settings"][key] = value
            row.update(status="default_converted", reason="Verified frozen YACP initializer and load behavior",
                       target_key=key, value=value)
        decisions.append(row)
    result["default_decisions"] = decisions
    result["blocked_default_fields"] = [row["source_field"] for row in decisions if row["status"] == "blocked"]
    result["missing_keys_use_crosspoint_defaults"] = False
    result["omitted_fields_reviewed"] = not result["blocked_default_fields"]
    return result
