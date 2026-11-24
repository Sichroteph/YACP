# File Formats

## Dictionary history, version 1

Each installed dictionary folder name has a separate history under `/.crosspoint/dictionary-history/<folder>/`. The firmware alternates between `a.bin` and `b.bin` so a partial write does not replace the previous valid snapshot.

The 16-byte header consists of ASCII magic `DIH1`, a 32-bit generation, a 32-bit payload length and a 32-bit checksum, all little-endian. The checksum is the XOR of the IEEE CRC-32 of the first 12 header bytes and the IEEE CRC-32 of the payload. The newest valid generation wins. The payload contains up to 128 NUL-terminated UTF-8 dictionary headwords, newest first, within 8192 bytes. An empty payload represents an empty history. The history contains no book paths, definitions or dictionary offsets.

## Legacy-source receipt, schema 2

`/.crosspoint/data-checked-v3` holds the eight-byte `YACPMIG` plus byte 1 marker after a successful boot inspection. It only permits skipping inspection on an ordinary same-firmware deep-sleep resume without a pending repair. The prior v2 marker does not attest to the TXT/Markdown conversion. Cold restart ignores this shortcut. External card changes require a restart.

`/.crosspoint/legacy-receipts/<legacy-cache-name>` contains 160 bytes. It identifies the active legacy inputs that have already been reconciled; it never replaces `progress.bin` or changes an EPUB section-cache version.

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 8 | ASCII `YACPRCP` followed by byte 2 |
| 8 | 1 | Legacy progress length: 4, 6 or 10 |
| 9 | 10 | Exact progress bytes, zero-padded |
| 19 | 1 | Legacy profile length: 0, 3 or 86 |
| 20 | 86 | Exact profile bytes, zero-padded |
| 106 | 1 | Section prefix length: 0 or 44 |
| 107 | 44 | Exact section-header prefix, zero-padded |
| 151 | 4 | Little-endian FNV-1a checksum of bytes 0 through 150 |
| 155 | 5 | Reserved zero bytes |

Missing receipts trigger review/conversion. A present receipt with a wrong schema, length or checksum blocks replay. Native position changes do not modify this source receipt. Repeat-repair directories under `/.yacp-reentry-v2` preserve the complete prior active store; only a prepared inactive clone becomes `/.crosspoint`.

## TXT and Markdown progress after CrossPoint 2.0

Older YACP stored TXT and Markdown progress under `/.crosspoint/txt_<path-hash>/progress.bin`. Its six-byte record contains a little-endian uint16 page and a uint32 source byte offset. The earlier four-byte record contains the page without a source offset. It needs a matching version 3 index (disk magic `ITXT` followed by byte 3), with a valid source size, page count and monotonic offsets. XTC and XTCH retain their distinct `xtc_` cache and four-byte page record.

CrossPoint 2.0 paginates TXT and Markdown through its EPUB engine. Conversion writes a ten-byte `epub_<path-hash>/progress.bin`, with spine, page and page count set to zero and the decoded text offset in bytes 6 through 9. The offset counts Unicode code points in the generated HTML body before the saved passage. CR and line break tags do not add a character. A leading UTF-8 BOM and non-emitted trailing spaces are excluded. A record past the end of the source, split across a UTF-8 character, or in conflict with an unacknowledged native position pauses migration.

`/.crosspoint/txt-receipts/<old-cache-name>` records a source already converted. Its 24 bytes hold `YTX` plus byte 1, a one-byte source progress length, six bytes for the original progress (zero-padded), a reserved zero byte, the file size, the checked source byte offset and an FNV-1a checksum of bytes 0 through 19. The final three fields are little-endian uint32 values. A downgrade that writes the same source record leaves later native progress intact. The inactive staging copy moves the old cache to `retired-txt` in the repair history, where the complete prior store also remains under `original`. The live reader uses only the new cache.

## Offline migration bundle, schema 1

The host-only bundle uses `intent.json` to pin every output path, byte length and SHA-256. Files assemble in `card.pending`; a rename publishes `card` after verification. `COMPLETE.json` confirms a prepared copy, with installation and hardware validation explicitly false. Original stores remain in `card/.yacp-migration/original-crosspoint`, beside a non-secret review report. This is an offline data format, not a new firmware boot importer. See `yacp-migration.md` for retry and credential handling.

## YACP decoded sleep-image planes, version 2

Custom full-screen BMPs and per-book gallery BMPs can use `/.crosspoint/sleep_planes_v2/<path-fnv32>.bin`. These are disposable render caches, separate from the original BMP, legacy YACP sleep caches and reading data. Covers and transparent overlays retain the ordinary upstream path.

The 48-byte header has ASCII `YSC2` at 0, version 2 at 4 and commit byte 1 at 5. Bytes 6 to 11 contain filter, cover mode, orientation, controller, grayscale mode and fading-fix setting. Little-endian uint16 width/height occupy 12/14. Little-endian uint32 values at 16/20/24/28 contain framebuffer bytes, source file bytes, source FNV-1a and source-path FNV-1a. Three plane checksums start at 32, 36 and 40. Offset 44 stores the FNV-1a of bytes 0 through 43. The payload is exactly three framebuffers: BW, LSB, MSB, in the current SDK encoding.

A provisional header has commit byte zero. The complete checksummed header is written only after all three frames. A hit requires matching rendering parameters, full source content fingerprint, exact cache length, header checksum and all plane checksums before display begins. Each subsequent plane read is checked again. Unknown, truncated, altered or mismatched data takes the ordinary BMP decoder. A transient adaptive-tone analysis failure is not cached. Increment this format and directory version if future code changes how stored plane bytes are interpreted.

The fixed cache state is 115 bytes, allocated fallibly once for an eligible sleep render. Preparation and validation borrow the existing framebuffer while its previous pixels are disposable, hashing the source in framebuffer-sized reads and validating one complete plane per read. The helper retains a bounded 192-byte stack fallback for callers without scratch storage. Validation also holds one 48-byte header. No second framebuffer is allocated. The source BMP is closed before each cache read/write and reopened only when another decode pass is needed. Warm reuse still reads the source for content identity and validates the cache, but skips adaptive histogram analysis and the three BMP rasterization passes. Energy, latency and physical grayscale quality require device measurement.

## YACP on CrossPoint 2.0.0, section 74

Section 74 extends the CrossPoint 2.0.0 header with three one-byte YACP options before the page count: forced paragraph indentation, guide dots and publisher annotations. The header occupies 47 bytes on the supported targets and the partial sentinel is 208. Text blocks carry the optional guide-dot positions in their arena, and each page ends with its bounded publisher-marker list. Earlier generated sections are rebuilt. Reading positions remain in a separate file and do not use this section version.

## YACP CrossPoint port: publisher annotations (section 51)

Section 51 adds the publisher-numbering toggle at byte 23 after the guide toggle. Its packed header is 46 bytes; the page count starts at byte 24. The matching partial sentinel is 231. Each page body ends with a one-byte marker count (0 to 8), followed by that many records: signed 16-bit line Y and a 16-byte, NUL-terminated UTF-8 label. Invalid counts, truncated records and unterminated labels are rejected. Older layout caches rebuild from the EPUB while retaining separately saved reading positions.

Book profile version 4 is 66 bytes. It appends `publisherPageNumbers` as text field 20, before the 32-byte font-family field and checksum. Version 1 (62 bytes), 2 (64 bytes) and 3 (65 bytes) remain readable; their absent fields inherit global values. New writes use the alternate verified slot, preserving the preceding record during interruption.

## YACP CrossPoint port: guide annotations (section 50)

Section version 50 adds `guideReadingEnabled` immediately after the version 49 forced-indent flag. Its packed header is 45 bytes: indent at byte 21, guide at byte 22, page count at byte 23. The corresponding partial sentinel is 232. Old sections are rebuilt from the EPUB; progress uses the separate visible-text anchor.

TextBlock's existing one-byte focus flag becomes flags: bit 0 retains focus annotations, bit 1 adds `int16_t guideDotX[wordCount]` after the optional focus suffix positions and before byte arrays. `INT16_MIN` means no dot for that word. Other values are line-relative dot positions. Words, line positions, link spans and visible offsets remain unchanged by this decoration. The extra array exists only on lines with dots, inside the same fallible arena allocation. Font-cache scanning includes the dot glyph. Dot placement performs no word measurement during page rendering.

Book profile version 3 is 65 bytes. It appends the guide switch at field index 18, after the version 2 interval at index 17. The family occupies bytes 29 through 60 and the checksum starts at byte 61. Versions 1 and 2 remain readable, retaining current global defaults for fields they did not store. Writes still alternate slots and preserve the preceding valid profile.

## YACP book-scoped reader profiles

The CrossPoint-based port stores EPUB text choices separately from global defaults in
`/.crosspoint/book_profiles/epub_<native-path-hash>/reader_profile.0` and `reader_profile.1`.
These are user data, not disposable pagination caches. Legacy `reader_settings.bin`
is preserved and requires explicit conversion; firmware does not guess its font indices.

The first text-only format is 62 bytes: ASCII `YBRS`, version byte 1, little-endian uint32 generation,
17 bytes of settings, a 32-byte NUL-terminated SD family, and little-endian uint32
FNV-1a checksum of the first 58 bytes. Field order is font family, point size,
line spacing preset, numeric line height, orientation, margin, alignment,
embedded styles, hyphenation, antialiasing, reader dark mode, images, paragraph
spacing, forced indents, focus reading, word spacing, character spacing.

Version 2 is 64 bytes: `YBRS`, version 2, uint32 generation, one flags
byte, 18 setting bytes, the 32-byte family and a uint32 FNV-1a checksum of the
first 60 bytes. The first 17 settings keep their previous order; byte 18 is the
custom automatic-turn interval in seconds (5 to 120). Flag bit 0 enables text
overrides and bit 1 enables the saved interval. An interval-only record continues
to follow global text defaults. Choosing an unchanged interval explicitly still
creates its override. The timer itself is never restored as active. Version 1
loads as text-only and upgrades on a later setting change, retaining the old slot.

Version 3 is 65 bytes and adds the guide-dot preference at field index 18.
Version 4 is 66 bytes and adds the publisher page-number preference at index 19.
The family follows the settings and the checksum covers all preceding bytes.

Version 5 keeps the 66-byte record and stores settings for the CrossPoint 2.0
layout engine. Field index 2 accepts the seven line-height presets (0 to 6).
Index 11 also accepts monochrome images (3). Index 13 is the paragraph indent
width in spaces (0 to 5). The flags, field count, family and checksum stay in
their version 4 positions. Earlier profiles use their original ranges when read.
When there is no explicit line-height percentage, the earlier YACP preset
is converted using the original table for the saved font family. The new
renderer derives line height from font size, so pagination can change after
the transition.

Version 6 keeps the same layout and adds value 6 in field index 13 for YACP's
forced paragraph indents. A version 1 to 4 forced-indent flag restores this
mode when enabled and maps to zero spaces otherwise. Saved numeric widths
from version 5 retain their value. The firmware writes version 6 when forced
indents are selected and version 5 for numeric widths.

The latest valid generation wins using unsigned wrap-aware comparison. A save
writes the other slot, closes it and verifies its contents before accepting it.
Truncation can therefore retain the preceding valid copy. Unknown magic or
version prevents writes to both slots. If neither damaged slot is recoverable,
global defaults are used without overwriting the damaged files. Unchanged
settings cause no profile write once their explicit overrides have been recorded.

## Upstream formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 10

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 10
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 49 (YACP)

Adds the one-byte `forceParagraphIndents` boolean after `wordSpacingPercent`,
before the page count. The header is 44 bytes. Page count starts at offset 22,
page LUT at 24, and visible-text LUT at 40. Both enabled and disabled values
participate in validation. The committed partial marker is 233; the complete
marker is 49. A different version or flag rebuilds the section using the saved
visible-text position. An incomplete build still uses zero.

This is a YACP format extension. Before integrating a later upstream section
format, reconcile its version and layout explicitly rather than treating an
upstream version numbered 49 as this format. Book progress remains ten bytes.

### Version 48

Version 48 keeps the version 47 serialized layout unchanged. It was bumped
because Hangul text no longer has implicit line-break opportunities between
syllables: Korean words wrap at spaces (like CSS `word-break: keep-all`), and
with hyphenation enabled a word may also split at the end of a line wherever the
CJK line-breaking rules allow, without an inserted hyphen. Justification stretches only word spaces. Cached line breaks and word
positions from version 47 no longer match.

### Version 47

The section header adds signed `characterSpacing` (pixels) and unsigned
`wordSpacingPercent` after `focusReadingEnabled`; both participate in cache
validation. Each TextBlock's BlockStyle stores only `characterSpacing` after
`directionDefined`. Word spacing is resolved into cached word positions during
layout. Sections from earlier versions are rebuilt.

### Version 46

Version 46 keeps the version 45 serialized layout unchanged. It was bumped
because ordered lists now number their items, `list-style-type: none`
suppresses list markers, and `<ul>`/`<ol>` containers contribute their own
margins and padding to child block insets, changing cached word contents and
page layout.

### Version 45

Version 45 keeps the version 44 serialized layout unchanged. It was bumped
because internal EPUB links now preserve CSS superscript and subscript styles,
changing their cached word-style flags and page layout.

### Version 44

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 44 appends the internal-link rectangles produced during text layout to
each serialized page. The reader uses these rectangles for touch navigation;
older caches are rebuilt because they contain no link geometry.

Version 43 keeps the version 42 serialized layout unchanged. It was bumped
because paragraph base direction now excludes direction changes from inline
elements.

Version 42 keeps the version 41 serialized layout unchanged. It was bumped
because closing a block now strips inherited vertical margins and padding.

Version 41 keeps the version 40 serialized layout unchanged. It was bumped
because simple HTML table rows are now laid out as positioned columns rather
than flattened paragraphs with synthetic row/cell labels.

Version 40 keeps the version 39 serialized layout unchanged. It was bumped
because ruby groups now remain intact when large text blocks are soft-flushed.

Version 39 keeps the version 38 serialized layout unchanged. It was bumped
because image top margins are now clamped to keep full-height images within the
page viewport.

Version 38 keeps the version 37 serialized layout unchanged. It was bumped
because Focus Reading now permits line breaks at visible hyphens and dashes
and hyphenates focus-split words as a whole, changing cached page layout.

Version 37 increases the fixed-size footnote href field from 96 to 256 bytes.
This changes each serialized footnote record from 128 to 288 bytes, so older
section caches must be discarded and rebuilt.

Version 36 keeps the version 35 serialized layout unchanged. It was bumped
because ruby and justified text positioning and CJK line breaking now use
corrected word measurements, so version 35 cached page layouts no longer match.

Version 35 adds a header offset and a `uint32_t` entry per page for the
visible-text offset LUT. The other section LUTs remain unchanged.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- per-page visible-text offset LUT (zero-based Unicode codepoints in `<body>`)
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs retained for navigation and legacy sync fallback
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 49
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 256

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3,
    TAG_PageDropCap = 4,
    TAG_PageBorderBox = 5
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32,
    DISCRETIONARY_HYPHEN = 128  // trailing hyphen inserted by line breaking
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
    s8 characterSpacing;
    s32 fontId;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
    u8 hasWordFonts;
    if (hasWordFonts != 0) {
        s32 wordFonts[3];
        u8 wordFontSlot[wordCount] [[comment("0 = block font, n = wordFonts[n - 1]; bit 0x80 = small caps")]];
    }
};

struct ImageBlock {
    String imagePath;
    String srcPath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageDropCap {
    s16 xPos;
    s16 yPos;  // baseline of the last spanned line
    s32 fontId;
    u16 scale256;
    WordStyle style;
    u8 length;
    char text[length];
};

struct BorderSide {
    u8 width;
    u8 style;  // 0 none, 1 solid, 2 double, 3 dotted, 4 dashed
};

struct PageBorderBox {
    s16 xPos;
    s16 yPos;
    u16 width;
    u16 height;
    BorderSide sides[4];  // top, right, bottom, left
    bool shaded;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else if (pageElementType == TAG_PageDropCap) {
        PageDropCap dropCap [[inline]];
    } else if (pageElementType == TAG_PageBorderBox) {
        PageBorderBox borderBox [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphIndentSpaces;
    u8 paragraphAlignment;
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;
    s8 characterSpacing;
    u8 wordSpacingPercent;
    bool forceParagraphIndents;

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;
    u32 visibleTextLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }

    if (visibleTextLutOffset != 0) {
	u32 visibleTextOffset[pageCount] @ visibleTextLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## CLX1 — library index (`.crosspoint/library.idx`)

Written by `lib/LibraryIndex/LibraryBuilder.cpp`, read by `LibraryIndexFile`. One
file describing every book on the card, so the shelf can sort and search
thousands of titles without opening any of them.

Format version 2. An index written by another version fails validation on open
and is rebuilt; that is the entire migration mechanism.

### Layout

| Section | Offset | Contents |
|---|---|---|
| Header | 0 | 64 bytes, `ClixHeader` |
| Folders | `folderStart` | length-prefixed paths, one per folder |
| Records | `recordStart` | `bookCount` × 128-byte `ClixRecord` |
| Permutations | `permStart` | `bookCount` u16 author order, then `bookCount` u16 arrival order |
| Name blob | `nameStart` | per record: path hash, name, canonical author, title, source author (see below) |

The arrival permutation runs oldest first, keyed by the record's FAT
modification time (when the file landed on the card); `firstSeen` — the
build-assigned discovery counter — breaks ties and carries books whose
filesystem reports no time. Fold version 3 introduced the timestamp key; a
fold bump rebuilds ranks while preserving `firstSeen`.
Fold version 4 preserves leading articles in title sort and search keys.

Sections are 512-byte aligned so each starts on an SD block boundary.

### Records are exactly 128 bytes

A fixed stride is what lets the reader seek straight to record *n* without an
offset table, and read a screenful in one 4 KB block. `static_assert` enforces it.

Each record carries `fold[96]`, the title normalised for search and sorting —
accents stripped, case dropped, leading articles preserved — and `authorKey[12]`,
the author's words folded and sorted so that "Victor Hugo" and "Hugo Victor" group as
one person. `authorKey` is a GROUPING key, not an ordering one: the shelf orders by
surname, derived separately from the display name.

The byte before the folded title records metadata extraction status: not
attempted, extracted, or failed. The final four bytes contain the packed FAT
modification date and time returned by SdFat. A zero timestamp is not trusted.
These fields occupy the alignment and reserved bytes from version 1, so the
record remains exactly 128 bytes.

The header records whether EPUB metadata extraction was enabled for the build.
This prevents a metadata-disabled rebuild from making filename fallbacks look
fresh to a later metadata-enabled build.

### The name blob

Per record, at `nameStart + nameOff`:

```text
[u64 pathHash]    FNV-1a fingerprint of the complete path
[nameLen bytes]  filename, without the directory
[u8][author]     display author, one spelling chosen per authorKey across the library
[u8][title]      the book's own title, or length 0 if it never gave one
[u8][source]     cleaned author spelling before the library-wide spelling vote
```

The filename must stay the first textual field and stay the filename: `readPath`
rebuilds a book's path from it, so writing the display title there makes the book
impossible to open. That was a real defect, and it is why title has its own field.

The source author is separate from the displayed canonical author so a later
rebuild can repeat the spelling vote after books are added or removed. Existing
display reads still stop at the author or title fields and retain their offsets.

### Freshness and unchanged rebuilds

Reconciliation treats the persisted 64-bit complete-path fingerprint as the
book identity. Metadata is reused only when the fingerprint, size, nonzero FAT
timestamp, fold version, metadata mode, and expected extraction status agree.
EPUBs with a zero timestamp or a previous extraction failure are parsed again.

If every current record reuses metadata, the old and new counts agree, and no
unreadable entry was seen, the staging files are discarded and the live index is
left byte-for-byte unchanged. A normal rebuild action is therefore a freshness
check, not a forced metadata reread.

### Header flags

`RANKS_DEGRADED` says one or more orders fell back to walk order because a
checked sort allocation failed. Title and author each use a phase-local
`SortKey[bookCount]` allocation (14 bytes per book, 57,344 bytes at the 4,096-book
format ceiling); the first array is released before the second is requested.
Sorting is therefore best effort through the full format limit rather than
being disabled at an arbitrary library size.

`DEDUP_DEGRADED` says a directory exceeded the fixed 1024-entry duplicate-key
buffer, or that its fallible 8 KiB allocation failed. The walk still indexes
every enumerated book; it only stops remembering additional identities for
duplicate-dirent detection, so a damaged FAT may expose duplicates but cannot
make a real book disappear.

`selfSize` is the expected file size. Comparing it against the real one is a free
truncation guard: a build cut short by a power failure cannot pass.

## YACP statistics retained by the CrossPoint port

- Per-book `stats_v6.bin` is 73 bytes. Versions 1 through 6 remain readable; the flags preserve manual dates and completion state. A save verifies its temporary bytes, retains the preceding valid file as `.bak`, and publishes by rename. Future or corrupt data blocks destructive saves.
- `global_stats.bin` version 3 is 159 bytes and includes 730 days of activity bits. Synchronized device files are snapshots, not deltas; the current device identifier is excluded from imported totals.
- `daily_reading.bin` begins with `CRHM`. Version 2 stores a four-byte anchor day and 730 little-endian 16-bit minute buckets, totaling 1,469 bytes. Version 1 remains readable without discarding its day values. The wider buckets fix the former 255-minute ceiling.
- `finished_books.bin` begins with `CPFB` and retains at most 32 records. Version 3 stores a 64-bit FNV-1a path key, reading seconds, start/finish dates and bounded UTF-8 title/author strings. Device cache-directory hashes remain CrossPoint's platform-dependent hashes and must not be confused with these stable index keys.

The independent host decoders live in `scripts/yacp_data_formats.py`. Existing caches are never considered proof of position preservation across layout engines; migration uses reviewed source-text anchors and fresh CrossPoint page tables.

## Legacy YACP per-book reader settings

`epub_<legacy-path-hash>/reader_settings.bin` is user data, not disposable pagination cache.
Version 1 contains the version byte and a little-endian 16-bit automatic page-turn interval (three bytes total).
Version 2 contains 86 bytes: version, flags, interval, rendering mode, 17 byte-sized reader settings, and a 64-byte terminated UTF-8 SD font-family field. Flags 0/1/2 respectively enable custom text settings, the interval and a rendering-mode override.

The 17 settings, in order, are family, size index, line height, orientation, margin, publisher numbers, alignment, embedded styles, hyphenation, antialiasing, reader dark mode, images, paragraph spacing, paragraph indents, bionic reading, guide dots and rendering mode. The font-size range is not stored per book. The frozen reader computes a nominal target from the global range, but selects the indexed available family size first. Migration must use the captured font catalogue, not just that nominal target. Built-in indices instead depend on Standard/XLarge. A record with custom-settings flag off must not override text choices, even when its unused snapshot contains values.

`book_reader_settings()` rejects unknown versions/flags, wrong lengths and unterminated family names. Conversion candidates retain old rendering-mode selections as evidence while selecting the CrossPoint layout engine, as requested. The port persists its converted book profiles in alternating `reader_profile.0` and `reader_profile.1` slots under `/.crosspoint/book_profiles/epub_<path-hash>/`. Version 3 is 65 bytes and includes the independent custom automatic-turn interval. Activation of a complete real-card migration remains pending.

## Nearby YACP statistics

The retained CISS protocol has a 14-byte header: four ASCII magic bytes, version 1, message type, one-byte payload size, a zero reserved byte and the sender's six-byte factory MAC. Types are Hello (1), Stats (2), Ack (3) and Name (4). Stats payloads are exactly 13, 17 or 159 bytes for global-statistics versions 1, 2 and 3. Names occupy 2 to 20 bytes. This protocol is separate from the SDK's CIFT file transfer.

Each peer has one canonical lowercase file `/.crosspoint/synced_stats/device_<12-hex-factory-MAC>.bin`. It is a complete local-device snapshot, never an aggregate or an increment. Saving verifies `.part` before rotating the canonical file to `.previous` and promoting the new snapshot. A failed promotion retains a recoverable previous snapshot. Identical snapshots perform no write. Unknown or corrupt existing snapshots block replacement.

Aggregation excludes this device, temporary files and noncanonical copies. It reads `.previous` only when the matching canonical snapshot is missing or invalid. A newer canonical format is skipped without falling back to an older snapshot. Files remain untouched during aggregation. These checks provide recoverable file-level behavior; they do not establish FAT/SD power-loss atomicity on physical hardware.

The YACP `recentBooksView` setting keeps its legacy meanings: `0` for the dedicated list, `1` for the nine-cover grid. The default is list. This does not change the `recent.json` record format or the eighteen-record bound.

## Book-profile reset

The YACP book-profile v3 format already permits flags zero. An explicit reset writes a newer, verified zero-flag record to the alternate slot. Both text and automatic-turn interval then inherit the current global settings, including later global changes. The preceding slot is preserved during the write. Unsupported formats block reset. Repeating an already complete reset does not write another record.

## YACP book move journal

`/.crosspoint/book-move.bin` is a 1,052-byte immutable intent: `YBM1`, version byte 1, five-bit source-presence mask (book/cache/bookmarks/profile/gallery), two-bit metadata-snapshot mask, reserved zero byte, two little-endian size/FNV-1a32 pairs for the recent/state snapshots, two zero-padded 512-byte absolute book paths, and a trailing little-endian FNV-1a32 checksum of the preceding bytes. Paths are validated before use. Version 2 keeps the record length and checksum and uses mask bits 5 and 6 for the protected book's `.key` and `.rights` files. It derives an `epub_` cache identity for TXT and Markdown. The v1 recovery path continues to resolve `txt_` identities. A pending journal must stay on the same MCU architecture because these identities use the device's `std::hash`.

Version 3 keeps the same record length and uses presence bits 7 through 11 for the clipping store and its `.bak`, `.tmp`, `.deleted` and `.migrate.bak` companions, in that order. The previously reserved byte at offset 7 stores the high byte of the presence mask. Recovery continues to accept versions 1 and 2.

`book-move-recent.json` and `book-move-state.json` preserve pre-operation metadata. They remain after successful completion, and may only be replaced for a new operation with no active intent. Recovery verifies snapshot sizes/checksums before restoring normal JSON paths. Existing destination data, missing source and destination, invalid records or corrupt snapshots block recovery. A truncated initial intent can require manual recovery, but no file renames occur until the intent has been reopened and verified. A successful operation retires only its intent, never book data.


## On-reader YACP migration transaction, schema 1

`/.yacp-migration-v1/intent`, `/.crosspoint/migration-ready` and `/.yacp-migration-v1/complete` contain the eight bytes `YACPMIG` followed by `0x01`. Intent is written and reopened before renaming the legacy `/.crosspoint` directory to `/.yacp-migration-v1/original`. Preparation writes under `/.yacp-migration-v1/staging`. The ready marker is written only after conversion; the staged directory is then renamed to `/.crosspoint`, followed by completion. Recovery accepts matching partial deterministic outputs, never conflicting prefixes. A partial completion marker is completed only when the activated ready marker is valid. Ambiguous active/original directory combinations block normal data loading.

Original files remain byte-identical, including unknown stores. Converted cache directories contain `migration-source-path` with the exact absolute SD book path. `migration-known-caches` records resolved legacy cache identities, including superseded hashes for the same path. `migration-report.json` gives schema, book/position/review counts and original location; `migration-review` contains individual source/reason records. Inactive bookmark scratch and owner files make streamed conversion repeatable and detect flattened target-name collisions.

Migration has a bounded 2 KiB copy/verification workspace, a 1 KiB cached-text buffer and a lazy matcher under 4 KiB, allocated once off the boot stack. JSON documents are individually capped at 32 KiB, with input/output buffers capped at 16 KiB; per-book profile conversion can hold two documents. A bookmark record's temporary serialized buffer is capped at 8 KiB and released before the next record. EPUB metadata and XML parsing reuse the current reader libraries. These allocations run before ordinary reader/font/network initialization and are released on completion; no migration work is added to page turns. Physical peak heap and FAT power-loss behavior still require device tests.
