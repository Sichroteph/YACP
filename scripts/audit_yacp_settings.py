"""Inventory scalar settings declarations without inferring semantic equivalence."""
import argparse
import json
from pathlib import Path
import re
import subprocess

HEADER = "src/CrossPointSettings.h"
PATTERN = re.compile(r'^\s*(uint8_t|uint16_t|bool|char)\s+(\w+)(\[[^\]]+\])?\s*=\s*([^;]+);', re.M)


def fields(text):
    result = {}
    for match in PATTERN.finditer(text):
        result.setdefault(match[2], []).append({"type": match[1], "extent": match[3],
            "initializer": match[4], "line": text[:match.start()].count('\n') + 1})
    return result


def revision(repo, revision):
    return subprocess.check_output(['git', '-C', str(repo), 'show', revision + ':' + HEADER]).decode('utf-8')


def audit(snapshot, original, target):
    local = fields((snapshot / HEADER).read_text(encoding='utf-8'))
    published = fields(revision(original, 'v1.7.1-yacp'))
    upstream = fields(revision(target, 'crosspoint-1.6.5'))
    port = fields((target / HEADER).read_text(encoding='utf-8'))
    rows = []
    for name in sorted(local):
        row = {"field": name, "local": local[name], "published": published.get(name),
               "crosspoint_1_6_5": upstream.get(name), "current_port": port.get(name),
               "decision": "semantic_review_required"}
        comparable = lambda declarations: [(d['type'], d['extent'], d['initializer']) for d in declarations or []]
        row['same_declaration_as_published'] = comparable(local[name]) == comparable(published.get(name))
        row['same_declaration_as_crosspoint'] = comparable(local[name]) == comparable(upstream.get(name))
        rows.append(row)
    return {"header": HEADER, "notes": [
        "This lists scalar member declarations, not every firmware behavior or every persisted key.",
        "Repeated declarations retain conditional font-variant branches; their last value is not a universal default.",
        "Matching names or declarations do not prove matching enum meanings, lifecycle, rendering or migration.",
        "A field absent upstream may have been renamed or moved to another store, rather than lost."],
        "fields": rows}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--original-repo', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.snapshot, args.original_repo, Path(__file__).resolve().parents[1])
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"Inventoried {len(result['fields'])} YACP scalar settings")
