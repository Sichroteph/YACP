"""Recover reviewable text-position candidates from v27/v44 caches. Never installs them."""
import argparse
from html.parser import HTMLParser
import json
from pathlib import Path
import posixpath
import struct
import unicodedata
from urllib.parse import unquote
import xml.etree.ElementTree as ET
from zipfile import ZipFile
from yacp_data_formats import book_reader_settings, esp32_book_path, fnv64_book_path, progress_record


def legacy_section_name(spine, profile=None):
    # Frozen EpubReaderActivity::applyBookReaderSettings chooses the explicit
    # per-book mode, or CrossInkDefault even if the text snapshot has another
    # mode. Never try a different engine's cached page as a fallback.
    mode = 0
    if profile is not None:
        decoded = book_reader_settings(profile)
        if decoded['render_mode_override']:
            mode = decoded['render_mode']
    if mode not in (0, 1, 2):
        raise ValueError('Unknown legacy render mode; no section cache selected')
    return f'{spine}{("", "_balanced", "_light")[mode]}.bin'


def first_cached_line(data, page):
    modern = data[:5] == b"\xffCXS\x2c"
    if modern:
        header_size, count_at, lut_at = 44, 26, 28
    elif data[:1] == bytes([27]):
        # CrossPoint tag 1.4.1: Section.cpp header and TextBlock::serialize.
        header_size, count_at, lut_at = 37, 19, 21
    else:
        raise ValueError("Unsupported section cache; expected version 27 or YACP version 44")
    if len(data) < header_size:
        raise ValueError("Truncated section header")
    count = struct.unpack_from("<H", data, count_at)[0]
    lut = struct.unpack_from("<I", data, lut_at)[0]
    if not 0 <= page < count or lut < header_size or lut + 4 * count > len(data):
        raise ValueError("Invalid page table")
    position = struct.unpack_from("<I", data, lut + page * 4)[0]
    end_of_page = struct.unpack_from("<I", data, lut + (page + 1) * 4)[0] if page + 1 < count else lut
    if position < header_size or position + 14 > end_of_page or end_of_page > lut:
        raise ValueError("Invalid page offset")
    elements = struct.unpack_from("<H", data, position)[0]
    if not 1 <= elements <= 1024 or data[position + 2] != 1:
        raise ValueError("Page does not begin with a text line")
    if not modern:
        words = struct.unpack_from("<H", data, position + 7)[0]
        if not 1 <= words <= 512:
            raise ValueError("Invalid word count")
        cursor, decoded = position + 9, []
        for _ in range(words):
            if cursor + 4 > end_of_page:
                raise ValueError("Truncated word size")
            length = struct.unpack_from("<I", data, cursor)[0]
            cursor += 4
            if not 0 < length <= 65536 or cursor + length > end_of_page:
                raise ValueError("Truncated word")
            decoded.append(data[cursor:cursor + length].decode("utf-8"))
            cursor += length
        return " ".join(decoded)
    words, bionic, guide, flags, text_size = struct.unpack_from("<HBBBH", data, position + 7)
    if not 1 <= words <= 512 or any(v not in (0, 1) for v in (bionic, guide, flags)):
        raise ValueError("Invalid text-block header")
    arena = position + 14
    metadata = words * (5 + 3 * bionic + 2 * guide + flags)
    text = data[arena + metadata:arena + metadata + text_size]
    if len(text) != text_size or arena + metadata + text_size > end_of_page:
        raise ValueError("Truncated text arena")
    offsets = struct.unpack_from(f"<{words}H", data, arena)
    flag_start = arena + words * (5 + 3 * bionic + 2 * guide)
    decoded = []
    for index, start in enumerate(offsets):
        end = offsets[index + 1] if index + 1 < words else text_size
        if start >= end or end > text_size or text[end - 1] != 0:
            raise ValueError("Invalid word boundary")
        word = text[start:end - 1].decode("utf-8")
        if flags and data[flag_start + index] & 2:
            if word.endswith(("-", "\u2010")):
                word = word[:-1]
        decoded.append(word)
    return " ".join(decoded)


class VisibleText(HTMLParser):
    # CrossPoint VisibleTextUtils.h defines this exact exclusion list.
    NON_VISIBLE = {"head", "style", "script", "title", "rp"}
    VOID = {"br", "hr", "img", "meta", "link", "input", "source", "wbr", "area", "base", "embed", "param", "track", "col"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.stack = []
        self.parts = []

    def handle_starttag(self, tag, attrs):
        name = tag.rsplit(":", 1)[-1]
        if name not in self.VOID:
            self.stack.append(name)

    def handle_endtag(self, tag):
        name = tag.rsplit(":", 1)[-1]
        if name in self.stack:
            self.stack = self.stack[:len(self.stack) - 1 - self.stack[::-1].index(name)]

    def handle_data(self, text):
        if "body" in self.stack and not self.NON_VISIBLE.intersection(self.stack):
            self.parts.append(text)


def comparable_text(text):
    """NFC and whitespace-free search, with original codepoint positions retained."""
    normalized, positions = [], []
    index = 0
    while index < len(text):
        end = index + 1
        while end < len(text) and unicodedata.combining(text[end]):
            end += 1
        for char in unicodedata.normalize("NFC", text[index:end]):
            if not char.isspace() and char not in ("\u00ad", "\u200b", "\ufeff"):
                normalized.append(char)
                positions.append(index)
        index = end
    return "".join(normalized), positions


def unique_text_offset(html, line):
    parser = VisibleText()
    parser.feed(html.replace("\r\n", "\n").replace("\r", "\n"))
    original = "".join(parser.parts)
    normalized, positions = comparable_text(original)
    needle, _ = comparable_text(line)
    if len(needle) < 8:
        raise ValueError("Text anchor is too short for an automatic candidate")
    position = normalized.find(needle)
    if position < 0:
        raise ValueError("Cached text is not present in the current chapter")
    if normalized.find(needle, position + 1) >= 0:
        raise ValueError("Cached text occurs more than once; no position guessed")
    return positions[position]


def chapter_html(epub, spine):
    with ZipFile(epub) as archive:
        container = ET.fromstring(archive.read("META-INF/container.xml"))
        package_path = next(element.attrib["full-path"] for element in container.iter()
                            if element.tag.rsplit("}", 1)[-1] == "rootfile")
        package = ET.fromstring(archive.read(package_path))
        items = {element.attrib["id"]: element.attrib["href"] for element in package.iter()
                 if element.tag.rsplit("}", 1)[-1] == "item"}
        # Both firmware parsers omit unresolved idrefs. Counting a dangling
        # cover reference would shift every subsequent chapter by one.
        spines = [element.attrib["idref"] for element in package.iter()
                  if element.tag.rsplit("}", 1)[-1] == "itemref" and element.attrib.get("idref") in items]
        if not 0 <= spine < len(spines):
            raise ValueError("Progress does not identify a chapter")
        href = unquote(items[spines[spine]].split("#", 1)[0])
        path = posixpath.normpath(posixpath.join(posixpath.dirname(package_path), href))
        return archive.read(path).decode("utf-8-sig")


def audit_anchors(source, book_roots):
    books = {}
    for root, prefix in book_roots:
        for file in root.rglob("*.epub"):
            sd_path = prefix.rstrip("/") + "/" + file.relative_to(root).as_posix()
            for identity in (fnv64_book_path(sd_path), esp32_book_path(sd_path)):
                # A hash is an index, not proof that two paths are the same book.
                books.setdefault(identity, set()).add((sd_path, file.resolve()))
    entries = []
    for progress in sorted(source.glob("epub_*/progress.bin")):
        saved = progress_record(progress.read_bytes())
        entry = dict(cache=progress.parent.name, progress=saved, firmware_navigation_validated=False)
        try:
            if progress.parent.name not in books:
                raise ValueError("Book path is not resolved from the supplied book copies")
            matches = books[progress.parent.name]
            if len(matches) != 1:
                raise ValueError("Cache identity matches multiple book paths; no book selected")
            sd_path, epub = next(iter(matches))
            entry["book_path"] = sd_path
            entry["source_cache_hash"] = "fnv64" if fnv64_book_path(sd_path) == progress.parent.name else "gcc32"
            entry["esp32_target_cache"] = esp32_book_path(sd_path)
            html = chapter_html(epub, saved["spine"])
            if "visible_text_offset" in saved:
                entry.update(candidate_offset=saved["visible_text_offset"], method="existing_offset")
            elif saved["page"] == 0:
                entry.update(candidate_offset=0, method="chapter_start")
            else:
                profile = progress.parent / "reader_settings.bin"
                section = progress.parent / "sections" / legacy_section_name(
                    saved['spine'], profile.read_bytes() if profile.is_file() else None)
                if not section.is_file():
                    raise ValueError("Saved page has no corresponding section cache")
                line = first_cached_line(section.read_bytes(), saved["page"])
                entry.update(candidate_offset=unique_text_offset(html, line), method="unique_visible_text_match",
                             source_line=line, source_section=section.relative_to(progress.parent).as_posix())
        except (ValueError, KeyError, UnicodeError, ET.ParseError) as error:
            entry["unresolved"] = str(error)
        entries.append(entry)
    return {"entries": entries, "candidates": sum("candidate_offset" in e for e in entries),
            "unresolved": sum("unresolved" in e for e in entries), "migration_validated": False}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--book-root", required=True, type=Path)
    parser.add_argument("--finished-book-root", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    roots = [(args.book_root, "/")]
    if args.finished_book_root:
        roots += [(args.finished_book_root, "/Read"), (args.finished_book_root, "/read")]
    result = audit_anchors(args.source, roots)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(result, output, indent=2, ensure_ascii=False)
    print(json.dumps({key: value for key, value in result.items() if key != "entries"}))
