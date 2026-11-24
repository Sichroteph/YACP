from copy import deepcopy
import json
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from yacp_settings_migration import candidate, LANGUAGES, BUTTON_KEYS, READER_BUTTON_KEYS


class SettingsMigrationTest(unittest.TestCase):
    def test_recent_list_grid_and_invalid_choice(self):
        for value in (0, 1):
            result = candidate({"recentBooksView": value}, "tiny")
            self.assertFalse(result["blocked_keys"])
            self.assertEqual(result["settings"]["recentBooksView"], value)
        for value in (-1, 2, 256, True, "1", None):
            self.assertEqual(candidate({"recentBooksView": value}, "tiny")["blocked_keys"], ["recentBooksView"])

    def test_device_name_utf8_bytes_and_invalid_controls(self):
        for name in ("", "AB", "a" * 20, "\u00e9" * 10):
            result = candidate({"deviceName": name}, "tiny")
            self.assertEqual(result["settings"]["deviceName"], name)
            self.assertFalse(result["blocked_keys"])
        for name in (None, True, 12, "x", "a" * 21, "\u00e9" * 11, "hello\0tail", "line\nbreak"):
            self.assertEqual(candidate({"deviceName": name}, "tiny")["blocked_keys"], ["deviceName"])

    def test_tilt_axes_and_disabled_direction(self):
        for schema, mapping in ((1, (1, 2, 3, 4)), (2, (2, 1, 3, 4))):
            for direction, target in enumerate(mapping):
                for enabled in (0, 1):
                    source = {"tiltPageTurn": enabled, "tiltPageTurnDirection": direction,
                              "tiltPageTurnDirectionSchema": schema}
                    result = candidate(source, "tiny")
                    self.assertFalse(result["blocked_keys"])
                    self.assertEqual(result["settings"]["tiltPageTurn"], target if enabled else 0)
                    self.assertEqual(result["settings"]["tiltPageTurnLastMode"], target)
                    self.assertEqual(len(result["decisions"]), 3)
        for legacy in (0, 1, 2):
            self.assertEqual(candidate({"tiltPageTurn": legacy}, "tiny")["settings"]["tiltPageTurn"], legacy)
        for source in ({"tiltPageTurnDirection": None}, {"tiltPageTurn": 256},
                       {"tiltPageTurnDirection": 0, "tiltPageTurnDirectionSchema": 3},
                       {"tiltPageTurn": 2, "tiltPageTurnDirection": 0},
                       {"tiltPageTurnDirectionSchema": 2}, {"tiltPageTurnDirection": True}):
            result = candidate(source, "tiny")
            self.assertEqual(set(result["blocked_keys"]), set(source))
            self.assertNotIn("tiltPageTurn", result["settings"])

    def test_orientation_profiles_are_independent(self):
        for front in range(3):
            for side in (0, 1):
                result = candidate({"frontButtonOrientationAware": front, "sideButtonOrientationAware": side}, "tiny")
                self.assertFalse(result["blocked_keys"])
                self.assertEqual(result["settings"]["frontButtonOrientationAware"], front)
                self.assertEqual(result["settings"]["sideButtonOrientationAware"], side)
                self.assertNotIn("frontButtonFollowOrientation", result["settings"])
        self.assertEqual(candidate({"frontButtonOrientationAware": 256}, "tiny")["blocked_keys"], ["frontButtonOrientationAware"])

    def test_independent_front_and_side_holds_keep_their_raw_meanings(self):
        for side in range(5):
            for front in range(4):
                source = {"sideButtonLongPress": side, "longPressButtonBehavior": front}
                converted = candidate(source, "tiny")
                self.assertFalse(converted["blocked_keys"])
                for key, value in source.items():
                    self.assertEqual(converted["settings"][key], value)
        for bad in (-1, 5, 256, True, "4"):
            self.assertEqual(candidate({"sideButtonLongPress": bad}, "tiny")["blocked_keys"], ["sideButtonLongPress"])

    def test_renumbered_sleep_modes(self):
        for old, new in ((4, 5), (5, 4), (9, 6)):
            self.assertEqual(candidate({"sleepScreen": old}, "tiny")["settings"]["sleepScreen"], new)
        self.assertEqual(candidate({"sleepScreen": 7}, "tiny")["blocked_keys"], ["sleepScreen"])

    def test_refresh_never_is_not_sixty_pages(self):
        self.assertEqual(candidate({"refreshFrequency": 6}, "tiny")["settings"]["refreshFrequency"], 5)
        self.assertEqual(candidate({"refreshFrequency": 5}, "tiny")["settings"]["refreshFrequency"], 6)
        self.assertEqual(candidate({"refreshAction": 1}, "tiny")["settings"]["refreshAction"], 1)
        self.assertEqual(candidate({"refreshAction": 2}, "tiny")["blocked_keys"], ["refreshAction"])

    def test_filters_do_not_substitute_adaptive(self):
        self.assertEqual(candidate({"sleepScreenCoverFilter": 3}, "tiny")["settings"]["sleepScreenCoverFilter"], 0)
        self.assertEqual(candidate({"sleepScreenCoverFilter": 0}, "tiny")["settings"]["sleepScreenCoverFilter"], 3)

    def test_numeric_line_height_precedes_legacy(self):
        result = candidate({"lineSpacing": 0, "lineHeightPercent": 137}, "tiny")
        self.assertEqual(result["settings"]["lineHeightPercent"], 137)
        self.assertNotIn("lineSpacing", result["settings"])

    def test_old_line_height_uses_yacp_family_scale(self):
        self.assertEqual(candidate({"fontFamily": 1, "lineSpacing": 1}, "tiny")["settings"]["lineHeightPercent"], 110)
        self.assertEqual(candidate({"sdFontFamilyName": "Example", "lineSpacing": 2}, "tiny")["settings"]["lineHeightPercent"], 110)

    def test_standard_xlarge_and_sd_indices(self):
        self.assertEqual(candidate({"fontSize": 0}, "tiny")["settings"]["fontSize"], 10)
        self.assertEqual(candidate({"fontSize": 0}, "xlarge")["settings"]["fontSize"], 16)
        self.assertEqual(candidate({"fontSize": 0, "sdFontSizeRange": 2, "sdFontFamilyName": "Example"}, "tiny",
                                   {"Example": [12, 14, 16, 18]})["settings"]["fontSize"], 12)
        self.assertIn("fontSize", candidate({"fontSize": 0, "sdFontFamilyName": "Example"}, "tiny")["blocked_keys"])

    def test_focus_reading_and_home_theme(self):
        values = candidate({"bionicReadingEnabled": 1, "uiTheme": 6}, "tiny")["settings"]
        self.assertEqual(values, {"focusReadingEnabled": 1, "uiTheme": 5})

    def test_power_shortcut_is_not_reinterpreted_as_crosspoint_footnotes(self):
        result = candidate({"shortPwrBtn": 4}, "tiny")
        self.assertFalse(result["blocked_keys"])
        self.assertEqual(result["settings"]["readerShortPowerAction"], 4)
        self.assertEqual(result["settings"]["shortPwrBtn"], 0)
        self.assertEqual(candidate({"shortPwrBtn": 16}, "tiny")["settings"]["shortPwrBtn"], 4)

    def test_reader_holds_keep_ids_including_clipping(self):
        for key, limit in (("longPressBackAction", 22), ("longPressMenuAction", 22), ("longPwrBtn", 23)):
            for value in range(limit):
                result = candidate({key: value}, "tiny")
                self.assertFalse(result["blocked_keys"])
                self.assertEqual(result["settings"][key], value)
            for value in (limit, -1, 256, True, "1", None):
                self.assertEqual(candidate({key: value}, "tiny")["blocked_keys"], [key])
        result = candidate({"shortPwrBtn": 21}, "tiny")
        self.assertFalse(result["blocked_keys"])
        self.assertEqual(result["settings"]["readerShortPowerAction"], 21)
        self.assertEqual(result["settings"]["shortPwrBtn"], 0)

    def test_reader_dark_does_not_enable_global_night_mode(self):
        values = candidate({"readerDarkMode": 1, "forceParagraphIndents": 1}, "tiny")
        self.assertFalse(values["blocked_keys"])
        self.assertEqual(values["settings"]["readerDarkMode"], 1)
        self.assertEqual(values["settings"]["forceParagraphIndents"], 1)
        self.assertNotIn("screenInverted", values["settings"])

    def test_file_browser_preview_alias(self):
        for value in (0, 1):
            result = candidate({"fileBrowserPreview": value}, "tiny")
            self.assertFalse(result["blocked_keys"])
            self.assertEqual(result["settings"]["fileBrowserDisplay"], value)
        result = candidate({"fileBrowserPreview": 1, "fileBrowserDisplay": 0}, "tiny")
        self.assertEqual(result["settings"]["fileBrowserDisplay"], 0)
        self.assertEqual(candidate({"fileBrowserPreview": 2}, "tiny")["blocked_keys"], ["fileBrowserPreview"])

    def test_display_power_uses_the_persisted_key(self):
        for value in (0, 1, False, True):
            result = candidate({"fadingFix": value}, "tiny")
            self.assertFalse(result["blocked_keys"])
            self.assertEqual(result["settings"]["fadingFix"], int(value))
        for value in (-1, 2, "1", None):
            self.assertEqual(candidate({"fadingFix": value}, "tiny")["blocked_keys"], ["fadingFix"])
        # An internal C++ member name is not evidence of a supported file format.
        self.assertIn("displayPowerSaving", candidate({"displayPowerSaving": 1}, "tiny")["blocked_keys"])

    def test_clock_visibility_and_fixed_offset(self):
        for old, header, reader in ((0, 1, 1), (1, 1, 0), (2, 0, 0)):
            values = candidate({"hideClock": old, "clockUtcOffsetQ": 56}, "tiny")["settings"]
            self.assertEqual((values["clockShowHeader"], values["statusBarClock"]), (header, reader))
            self.assertEqual((values["clockUtcOffsetQ"], values["clockTimezone"], values["clockDst"]), (56, 255, 2))

    def test_date_trust_is_not_inferred_from_time_sync(self):
        for synced in (0, 1):
            result = candidate({"clockHasBeenSynced": 1, "clockDateHasBeenSynced": synced}, "tiny")
            self.assertFalse(result["blocked_keys"])
            self.assertEqual(result["settings"]["clockDateHasBeenSynced"], synced)
        self.assertNotIn("clockDateHasBeenSynced", candidate({"clockHasBeenSynced": 1}, "tiny")["settings"])

    def test_front_buttons_are_validated_as_one_mapping(self):
        good = dict(zip(BUTTON_KEYS, (2, 3, 0, 1)))
        self.assertFalse(candidate(good, "tiny")["blocked_keys"])
        bad = dict(good, frontButtonBack=3)
        result = candidate(bad, "tiny")
        self.assertEqual(len(result["blocked_keys"]), 4)
        self.assertFalse(set(BUTTON_KEYS) & result["settings"].keys())
        legacy = candidate({"frontButtonLayout": 2}, "tiny")["settings"]
        self.assertEqual([legacy[k] for k in BUTTON_KEYS], [1, 2, 0, 3])

    def test_invalid_values_do_not_wrap_or_coerce(self):
        for value in (True, -1, 256, "100", 100.5):
            self.assertIn("lineHeightPercent", candidate({"lineHeightPercent": value}, "tiny")["blocked_keys"])
        self.assertIn("sdFontFamilyName", candidate({"sdFontFamilyName": "é" * 16}, "tiny")["blocked_keys"])
        self.assertIn("sdFontFamilyName", candidate({"sdFontFamilyName": "../font"}, "tiny")["blocked_keys"])

    def test_reader_profile_requires_complete_unique_map(self):
        source = dict(zip(READER_BUTTON_KEYS, (3, 2, 1, 0)), readerFrontButtonsEnabled=1)
        values = candidate(source, "tiny")
        self.assertFalse(values["blocked_keys"])
        self.assertEqual([values["settings"][k] for k in READER_BUTTON_KEYS], [3, 2, 1, 0])
        self.assertFalse(set(BUTTON_KEYS) & values["settings"].keys())
        del source[READER_BUTTON_KEYS[2]]
        self.assertIn("readerFrontButtonsEnabled", candidate(source, "tiny")["blocked_keys"])
        source[READER_BUTTON_KEYS[2]] = 0
        self.assertEqual(len(candidate(source, "tiny")["blocked_keys"]), 5)
        source[READER_BUTTON_KEYS[2]] = 256
        self.assertEqual(len(candidate(source, "tiny")["blocked_keys"]), 5)
        self.assertFalse(candidate({"readerFrontButtonsEnabled": 0}, "tiny")["blocked_keys"])

    def test_candidate_accounts_for_every_key_without_exposing_values(self):
        source = {"opdsPassword_obf": "SECRET", "futureKey": [1, 2], "buttonLayoutPromptVersion": "old",
                  "lineHeightPercent": 100, "fontSize": 1}
        before = deepcopy(source)
        first = candidate(source, "tiny")
        self.assertEqual(first, candidate(source, "tiny"))
        self.assertEqual(source, before)
        self.assertEqual({d["source_key"] for d in first["decisions"]}, set(source))
        self.assertNotIn("SECRET", json.dumps(first))
        self.assertFalse(first["ready_to_activate"])

    def test_language_codes_match_generated_firmware(self):
        root = Path(__file__).resolve().parents[1]
        source = (root / "lib/I18n/I18nStrings.cpp").read_text(encoding="utf-8")
        array = re.search(r'LANGUAGE_CODES\[\]\s*=\s*\{(.*?)\};', source, re.S)
        self.assertIsNotNone(array)
        self.assertEqual(set(re.findall(r'"([^"\n]+)"', array[1])), LANGUAGES)


if __name__ == "__main__":
    unittest.main()
