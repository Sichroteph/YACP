"""CrossPoint-based YACP per-book profiles v1 to v6. Pure codec, never installs data."""
import struct

FIELDS = (
    'fontFamily', 'fontSize', 'lineSpacing', 'lineHeightPercent', 'orientation', 'screenMargin',
    'paragraphAlignment', 'embeddedStyle', 'hyphenationEnabled', 'textAntiAliasing', 'readerDarkMode',
    'imageRendering', 'extraParagraphSpacing', 'forceParagraphIndents', 'focusReadingEnabled',
    'wordSpacing', 'characterSpacing', 'autoPageTurnSeconds', 'guideReadingEnabled', 'publisherPageNumbers',
)
RANGES = ((0, 1), (4, 255), (0, 3), (0, 200), (0, 3), (5, 40), (0, 4), (0, 1), (0, 1),
          (0, 1), (0, 1), (0, 2), (0, 1), (0, 1), (0, 1), (50, 200), (0, 4), (5, 120), (0, 1), (0, 1))
DEFAULTS = dict(zip(FIELDS, (0, 14, 1, 0, 0, 5, 0, 1, 0, 1, 0, 0, 1, 0, 0, 100, 2, 30, 0, 0)))
FIELDS_V5 = tuple({"lineSpacing": "lineHeight", "forceParagraphIndents": "paragraphIndentSpaces"}.get(key, key)
                  for key in FIELDS)
RANGES_V5 = tuple({2: (0, 6), 11: (0, 3), 13: (0, 5)}.get(i, limits) for i, limits in enumerate(RANGES))
RANGES_V6 = RANGES_V5[:13] + ((0, 6),) + RANGES_V5[14:]
FIELD_COUNTS = {1: 17, 2: 18, 3: 19, 4: len(FIELDS), 5: len(FIELDS_V5), 6: len(FIELDS_V5)}
RECORD_SIZES = {1: 62, 2: 64, 3: 65, 4: 66, 5: 66, 6: 66}



def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def encode(settings, generation=1, flags=1, version=4):
    if version not in FIELD_COUNTS or type(flags) is not int or flags & ~3 or (version == 1 and flags != 1):
        raise ValueError('Unsupported profile version or flags')
    fields = FIELDS_V5 if version >= 5 else FIELDS
    ranges = {5: RANGES_V5, 6: RANGES_V6}.get(version, RANGES)
    values = [settings[key] for key in fields[:FIELD_COUNTS[version]]]
    for key, value, (low, high) in zip(fields, values, ranges):
        if type(value) is not int or not low <= value <= high:
            raise ValueError('Invalid ' + key)
    if 0 < values[3] < 70:
        raise ValueError('Invalid line height')
    family = settings.get('sdFontFamilyName', '').encode('utf-8')
    if len(family) > 31 or b'\0' in family:
        raise ValueError('Invalid font family')
    data = b'YBRS' + bytes([version]) + struct.pack('<I', generation)
    if version >= 2:
        data += bytes([flags])
    data += bytes(values) + family.ljust(32, b'\0')
    return data + struct.pack('<I', checksum(data))


def decode(data):
    if len(data) < 5 or data[:4] != b'YBRS' or data[4] not in FIELD_COUNTS:
        raise ValueError('Unsupported book profile')
    version = data[4]
    expected_size = RECORD_SIZES[version]
    if len(data) != expected_size or checksum(data[:-4]) != struct.unpack_from('<I', data, len(data) - 4)[0]:
        raise ValueError('Invalid book profile')
    flags = 1 if version == 1 else data[9]
    start = 9 if version == 1 else 10
    field_end = start + FIELD_COUNTS[version]
    family = data[field_end:field_end + 32]
    if b'\0' not in family:
        raise ValueError('Unterminated family')
    fields = FIELDS_V5 if version >= 5 else FIELDS
    values = dict(zip(fields, data[start:field_end]))
    values['sdFontFamilyName'] = family.split(b'\0', 1)[0].decode('utf-8')
    generation = struct.unpack_from('<I', data, 5)[0]
    encode(values, generation, flags, version)  # Validate ranges without normalizing them.
    return {'version': version, 'flags': flags, 'generation': generation, 'settings': values}
