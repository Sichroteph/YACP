"""Check native reader positioning after a prepared migration experiment."""
import argparse
import json
from pathlib import Path

from yacp_data_formats import crosspoint48_page_offsets, progress_record


def verify(root):
    fixture = json.loads((root / 'fixture.json').read_text(encoding='utf-8'))
    cache = root / 'sd/.crosspoint' / fixture['native_cache']
    current = progress_record((cache / 'progress.bin').read_bytes())
    candidate = fixture['candidate']
    assert current['spine'] == candidate['progress']['spine'], 'Reader changed chapter'
    section = (cache / 'sections' / f"{current['spine']}.bin").read_bytes()
    offsets = crosspoint48_page_offsets(section)
    page = current['page']
    assert 0 <= page < len(offsets)
    assert current['visible_text_offset'] == offsets[page]
    assert offsets[page] <= candidate['candidate_offset']
    assert page + 1 == len(offsets) or candidate['candidate_offset'] < offsets[page + 1]
    if section[0] not in (48, 49) and page + 1 == len(offsets):
        assert candidate['candidate_offset'] == offsets[page], 'Partial last page has no verified upper boundary'
    result = {'native_page_contains_candidate_offset': True,
              'requested_offset': candidate['candidate_offset'], 'resumed_offset': offsets[page],
              'spine': current['spine'], 'page': page, 'chapter_pages': len(offsets),
              'complete_chapter_cache': section[0] in (48, 49),
              'visual_text_match_requires_review': True, 'hardware_validated': False,
              'migration_validated': False}
    with (root / 'position-result.json').open('x', encoding='utf-8') as output:
        json.dump(result, output, indent=2)
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    verify(parser.parse_args().fixture)
