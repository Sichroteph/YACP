"""Pure decoders for the YACP formats, used before writing any migration output."""
import math
import struct
from datetime import date

BUILTIN_SIZES = {"tiny": (10, 12, 14, 16), "xlarge": (16, 18, 20)}
SD_RANGES = ((8, 9, 10, 12), (10, 12, 14, 16), (16, 18, 20),
             (10, 12, 14, 16, 18), (8, 9, 10, 12, 14, 16, 18, 20))

BOOK_READER_FIELDS = (
    "fontFamily", "fontSize", "lineHeightPercent", "orientation", "screenMargin", "publisherPageNumbers",
    "paragraphAlignment", "embeddedStyle", "hyphenationEnabled", "textAntiAliasing", "readerDarkMode",
    "imageRendering", "extraParagraphSpacing", "forceParagraphIndents", "bionicReadingEnabled",
    "guideReadingEnabled", "epubRenderMode",
)


def book_reader_settings(data):
    """Decode packed reader_settings.bin, without activating or normalizing it.

    Frozen EpubReaderActivity.cpp:952 reads v1 (3 bytes) and v2 (86 bytes).
    The v2 font family occupies 64 bytes, unlike the new settings' 32 bytes.
    The SD size range is absent. Actual raster selection uses the catalogue index;
    the global range supplies only the old fallback target.
    """
    if not data or data[0] not in (1, 2):
        raise ValueError("Unknown book reader-settings version")
    version = data[0]
    if len(data) != (3 if version == 1 else 86):
        raise ValueError("Truncated or extended book reader-settings record")
    if version == 1:
        seconds = int.from_bytes(data[1:3], "little")
        return {"version": 1, "flags": 2 if seconds else 0, "auto_page_turn_seconds": seconds,
                "custom_reader_settings": False, "render_mode_override": False, "settings": {}}
    flags = data[1]
    if flags & ~7:
        raise ValueError("Unknown book reader-settings flags")
    family = data[22:86]
    if b"\0" not in family:
        raise ValueError("Unterminated book font family")
    values = dict(zip(BOOK_READER_FIELDS, data[5:22]))
    values["sdFontFamilyName"] = family.split(b"\0", 1)[0].decode("utf-8")
    return {"version": 2, "flags": flags, "auto_page_turn_seconds": int.from_bytes(data[2:4], "little"),
            "custom_reader_settings": bool(flags & 1), "render_mode_override": bool(flags & 4),
            "render_mode": data[4], "settings": values}


def crosspoint48_page_offsets(data):
    """Read a CrossPoint 48 or YACP 49/50 visible-codepoint table."""
    # Section::writeSectionFileHeader writes packed POD fields, not a C struct.
    # It is deliberately a different format from YACP's CXS/version 44 cache.
    if len(data) < 43 or data[0] not in (48, 49, 50, 51, 234, 233, 232, 231):
        raise ValueError('Expected a CrossPoint 48 or YACP 49/50 section')
    delta = {48: 0, 234: 0, 49: 1, 233: 1, 50: 2, 232: 2, 51: 3, 231: 3}[data[0]]
    header_size = 43 + delta
    if len(data) < header_size or any(value not in (0, 1) for value in data[21:21 + delta]):
        raise ValueError('Invalid YACP section header')
    count = struct.unpack_from('<H', data, 21 + delta)[0]
    pages_lut = struct.unpack_from('<I', data, 23 + delta)[0]
    visible_lut = struct.unpack_from('<I', data, 39 + delta)[0]
    if not count or pages_lut < header_size or pages_lut + count * 4 > len(data):
        raise ValueError('Invalid CrossPoint page table')
    if visible_lut < pages_lut + count * 4 or visible_lut + count * 4 > len(data):
        raise ValueError('Invalid CrossPoint visible-text table')
    if data[0] not in (48, 49, 50, 51):
        end = visible_lut + count * 4
        if end + 8 != len(data):
            raise ValueError('Partial section has no valid watermark trailer')
        consumed, total = struct.unpack_from('<II', data, end)
        if not 0 < consumed <= total:
            raise ValueError('Invalid partial-section watermark')
    page_positions = struct.unpack_from(f'<{count}I', data, pages_lut)
    if any(p < header_size or p >= pages_lut for p in page_positions):
        raise ValueError('Page points outside the serialized page area')
    if any(a >= b for a, b in zip(page_positions, page_positions[1:])):
        raise ValueError('CrossPoint page locations are not increasing')
    offsets = list(struct.unpack_from(f'<{count}I', data, visible_lut))
    if offsets != sorted(offsets):
        raise ValueError('Visible-text offsets are not monotonic')
    return offsets


def stats_date(data, offset):
    year, month, day = struct.unpack_from("<HBB", data, offset)
    if (year, month, day) == (0, 0, 0):
        return None
    if not 2000 <= year <= 2099:
        raise ValueError("Statistics date outside the supported range")
    return date(year, month, day).isoformat()


def book_stats(data):
    sizes = {1: 11, 2: 12, 3: 16, 4: 69, 5: 73, 6: 73}
    if not data or sizes.get(data[0]) != len(data):
        raise ValueError("Unsupported or truncated book statistics")
    version = data[0]
    sessions, seconds, pages = struct.unpack_from("<HII", data, 1)
    result = {"version": version, "sessions": sessions, "seconds": seconds, "pages": pages}
    if version >= 2:
        if data[11] not in (0, 1):
            raise ValueError("Invalid book completion flag")
        result["completed"] = bool(data[11])
    if version >= 3:
        result["pace_seconds"], result["pace_samples"] = struct.unpack_from("<HH", data, 12)
    if version >= 4:
        result.update(flags=data[16], start_date=stats_date(data, 17), finished_date=stats_date(data, 21),
                      time_of_day_seconds=list(struct.unpack_from("<4I", data, 25)),
                      day_of_week_seconds=list(struct.unpack_from("<7I", data, 41)))
    if version >= 5:
        result["estimated_time_left_seconds"] = struct.unpack_from("<I", data, 69)[0]
    return result


def global_stats(data):
    sizes = {1: 13, 2: 17, 3: 159}
    if not data or sizes.get(data[0]) != len(data):
        raise ValueError("Unsupported or truncated global statistics")
    sessions, seconds, pages = struct.unpack_from("<III", data, 1)
    result = {"version": data[0], "sessions": sessions, "seconds": seconds, "pages": pages}
    if data[0] >= 2:
        result["completed_books"] = struct.unpack_from("<I", data, 13)[0]
    if data[0] >= 3:
        result.update(time_of_day_seconds=list(struct.unpack_from("<4I", data, 17)),
                      day_of_week_seconds=list(struct.unpack_from("<7I", data, 33)),
                      history_anchor_day=struct.unpack_from("<I", data, 61)[0],
                      history_bits_hex=data[65:157].hex(),
                      longest_streak=struct.unpack_from("<H", data, 157)[0])
    return result


def finished_books(data):
    if len(data) < 8 or data[:4] != b"CPFB" or data[4] not in (1, 2, 3) or data[5] > 32:
        raise ValueError("Invalid finished-book index header")
    version, count = data[4:6]
    position = 8

    def take(size):
        nonlocal position
        if position + size > len(data):
            raise ValueError("Truncated finished-book index")
        value = data[position:position + size]
        position += size
        return value

    def string(limit):
        size = struct.unpack("<H", take(2))[0]
        if size > limit:
            raise ValueError("Finished-book metadata exceeds its format limit")
        # Preserve raw bytes too: old firmware could truncate a multibyte UTF-8 title.
        raw = take(size)
        return raw.decode("utf-8", errors="replace"), raw.hex()

    entries = []
    for _ in range(count):
        path_key, seconds = struct.unpack("<QI", take(12))
        entry = {"path_key": str(path_key), "seconds": seconds}
        if version >= 2:
            entry["start_date"] = stats_date(take(4), 0)
        entry["finished_date"] = stats_date(take(4), 0)
        entry["title"], entry["title_utf8_hex"] = string(160)
        if version >= 3:
            entry["author"], entry["author_utf8_hex"] = string(120)
        entries.append(entry)
    if position != len(data):
        raise ValueError("Unexpected finished-book trailing data")
    return {"version": version, "reserved_hex": data[6:8].hex(), "books": entries}


def font_point_size(settings, source_variant, sd_sizes=None):
    """Resolve the actual local YACP selection, not its nominal range target.

    SdCardFontFamilyInfo::selectFile picks the indexed available size first.
    For captured v4 raster families every registry entry has regular style,
    so the range-based nearest fallback is not reached. Missing catalogues
    must block migration instead of silently inventing an effective size.
    """
    index = settings.get("fontSize")
    if type(index) is not int or index < 0:
        raise ValueError("Missing or invalid YACP font index")
    if settings.get("sdFontFamilyName"):
        if not sd_sizes or any(type(size) is not int or not 1 <= size <= 255 for size in sd_sizes):
            raise ValueError("Captured SD font family sizes are required")
        sizes = sorted(set(sd_sizes))
        return sizes[min(index, len(sizes) - 1)]
    else:
        sizes = BUILTIN_SIZES[source_variant]
    if index >= len(sizes):
        raise ValueError("Font index is outside the recorded range")
    return sizes[index]


def fnv64_book_path(path):
    value = 0xCBF29CE484222325
    for byte in path.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return f"epub_{value}"


def esp32_book_path(path):
    """GCC 14 libstdc++ string hash on little-endian 32-bit ESP32 targets.

    Implements the public-domain MurmurHash2 algorithm used by _Hash_bytes,
    with the seed from the installed toolchain's bits/functional_hash.h:205.
    Native 64-bit simulator identifiers deliberately use a different hash.
    """
    raw = path.encode("utf-8")
    mask, multiplier = 0xFFFFFFFF, 0x5BD1E995
    value = 0xC70F6907 ^ len(raw)
    full = len(raw) & ~3
    for offset in range(0, full, 4):
        block = (int.from_bytes(raw[offset:offset + 4], "little") * multiplier) & mask
        block = ((block ^ (block >> 24)) * multiplier) & mask
        value = ((value * multiplier) & mask) ^ block
    if full < len(raw):
        value = ((value ^ int.from_bytes(raw[full:], "little")) * multiplier) & mask
    value = ((value ^ (value >> 13)) * multiplier) & mask
    return f"epub_{value ^ (value >> 15)}"


def progress_record(data):
    if len(data) not in (4, 6, 10):
        raise ValueError(f"Unsupported progress record size: {len(data)}")
    spine, page = struct.unpack_from("<HH", data)
    result = {"spine": spine, "page": page}
    if len(data) >= 6:
        result["chapter_pages"] = struct.unpack_from("<H", data, 4)[0]
    if len(data) == 10:
        result["visible_text_offset"] = struct.unpack_from("<I", data, 6)[0]
    return result


def daily_history(data):
    if len(data) < 9 or data[:4] != b"CRHM":
        raise ValueError("Invalid daily-history header")
    version = data[4]
    if version not in (1, 2) or len(data) != 9 + 730 * version:
        raise ValueError("Unsupported or truncated daily-history format")
    minutes = list(data[9:]) if version == 1 else list(struct.unpack_from("<730H", data, 9))
    if any(value > 1440 for value in minutes):
        raise ValueError("Daily minutes exceed a complete day")
    return {"version": version, "anchor_day": struct.unpack_from("<I", data, 5)[0], "minutes": minutes}


def upgrade_daily_history(data):
    history = daily_history(data)
    return b"CRHM\x02" + struct.pack("<I730H", history["anchor_day"], *history["minutes"])


def clippings(data):
    """Decode frozen ClippingStore v1 without truncating stored text or anchors.

    C++ fixed-size title/text limits could split UTF-8. Keep exact bytes beside
    the display text so this audit never turns replacement glyphs into migrated
    originals. Reading a record does not validate a new CrossPoint text anchor.
    """
    position = 0

    def take(count):
        nonlocal position
        if count < 0 or position + count > len(data):
            raise ValueError("Truncated clipping data")
        value = data[position:position + count]
        position += count
        return value

    def number(fmt):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))[0]

    def string(limit):
        length = number("<I")
        if length > limit:
            raise ValueError("Clipping string exceeds its format limit")
        return take(length)

    def text_fields(record, key, raw):
        record[key] = raw.decode("utf-8", errors="replace")
        record[key + "_utf8_hex"] = raw.hex()

    version, count = number("<B"), number("<H")
    if version != 1 or count > 64:
        raise ValueError("Unsupported clipping version or count")
    result = {"version": version, "clippings": [], "navigation_validated": False}
    for key in ("title", "author", "path"):
        text_fields(result, key, string(65536))
    fields = ("spine", "start_page", "end_page", "page_count", "start_word", "end_word", "word_count", "paragraph_index")
    for _ in range(count):
        record = dict(zip(fields, struct.unpack("<8H", take(16))))
        record["timestamp"] = number("<I")
        title = take(48)
        record["chapter_title_field_hex"] = title.hex()
        text_fields(record, "chapter_title", title.split(b"\0", 1)[0])
        text_fields(record, "text", string(512))
        result["clippings"].append(record)
    if position != len(data):
        raise ValueError("Unexpected clipping trailing data")
    return result


def bookmarks(data):
    """Decode v2-v5 without dropping paragraph anchors, timestamps or snippets."""
    position = 0

    def take(count):
        nonlocal position
        if count < 0 or position + count > len(data):
            raise ValueError("Truncated bookmark data")
        result = data[position:position + count]
        position += count
        return result

    def number(fmt):
        return struct.unpack(fmt, take(struct.calcsize(fmt)))[0]

    def string():
        length = number("<I")
        if length > 65536:
            raise ValueError("Unexpected bookmark string length")
        return take(length).decode("utf-8")

    version = number("<B")
    if version not in (2, 3, 4, 5):
        raise ValueError(f"Unsupported bookmark version: {version}")
    count = number("<B" if version == 2 else "<H")
    if count > 1024:
        raise ValueError("Too many bookmarks")
    result = {"version": version, "title": string(), "author": string(), "path": string(), "bookmarks": []}
    for _ in range(count):
        entry = {"spine": number("<H"), "progress": number("<f"), "timestamp": number("<I"),
                 "chapter_title": take(48).split(b"\0", 1)[0].decode("utf-8")}
        if not math.isfinite(entry["progress"]) or not 0 <= entry["progress"] <= 1:
            raise ValueError("Invalid bookmark progress")
        if version >= 4:
            entry["paragraph_index"] = number("<H")
        if version >= 5:
            entry["snippet"] = take(64).split(b"\0", 1)[0].decode("utf-8")
        result["bookmarks"].append(entry)
    if position != len(data):
        raise ValueError("Unexpected bookmark trailing data")
    return result
