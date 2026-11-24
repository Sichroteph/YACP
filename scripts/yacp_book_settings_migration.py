"""Prepare per-book settings candidates; no runtime import or source write."""
from yacp_data_formats import book_reader_settings
from yacp_settings_migration import candidate


def book_candidate(data, global_settings, source_variant, sd_font_sizes=None):
    decoded = book_reader_settings(data)
    result = {"source_version": decoded["version"], "settings": {}, "decisions": [], "blocked_keys": [],
              "runtime_book_scope_implemented": True, "ready_to_activate": False,
              "profile_flags": 1 if decoded["custom_reader_settings"] else 0}
    if decoded["custom_reader_settings"]:
        values = dict(decoded["settings"])
        legacy_render_mode = values.pop("epubRenderMode")
        if values["sdFontFamilyName"]:
            values["sdFontSizeRange"] = global_settings.get("sdFontSizeRange")
        converted = candidate(values, source_variant, sd_font_sizes)
        result["settings"] = converted["settings"]
        result["settings"].pop("uiTheme")  # Home never belongs to a book-specific profile.
        result["decisions"] = converted["decisions"]
        result["blocked_keys"] = converted["blocked_keys"]
        result["legacy_snapshot_render_mode"] = legacy_render_mode
        if legacy_render_mode not in (0, 1, 2):
            result["blocked_keys"].append("epubRenderMode")
        result["sd_size_origin"] = "indexed captured family catalogue, as selected by the frozen reader"
    if decoded["flags"] & 2:
        seconds = decoded["auto_page_turn_seconds"]
        if 5 <= seconds <= 120:
            result["auto_page_turn_seconds"] = seconds
            result["settings"]["autoPageTurnSeconds"] = seconds
            result["profile_flags"] |= 2
        else:
            result["blocked_keys"].append("autoPageTurnSeconds")
    if decoded["render_mode_override"]:
        result["legacy_render_mode"] = decoded["render_mode"]
        if decoded["render_mode"] not in (0, 1, 2):
            result["blocked_keys"].append("renderMode")
    # The approved plan chooses CrossPoint's layout engine for every book.
    # Preserve the old experiment choices as evidence, never silently enable
    # a CrossInk/Balanced/Light parser or substitute them into font settings.
    result["layout_engine_decision"] = "CrossPoint 1.6.5, as requested; legacy render modes archived"
    return result
