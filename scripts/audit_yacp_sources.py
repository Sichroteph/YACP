"""Read-only comparison of the published/local YACP source against this upstream base."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def digest(data):
    return hashlib.sha256(data).hexdigest()


def comparison(local, reference):
    if local == reference:
        return "identical"
    if b"\0" not in local and b"\0" not in reference and local.replace(b"\r\n", b"\n") == reference.replace(b"\r\n", b"\n"):
        return "line_endings_only"
    return "different"


def audit(source, target, base, published):
    tracked = set(git(source, "ls-files", "-z").decode().rstrip("\0").split("\0"))
    untracked = set(git(source, "ls-files", "--others", "--exclude-standard", "-z")
                    .decode().rstrip("\0").split("\0")) - {""}
    upstream = set(git(target, "ls-tree", "-rz", "--name-only", base).decode().rstrip("\0").split("\0"))
    published_files = set(git(source, "ls-tree", "-rz", "--name-only", published).decode().rstrip("\0").split("\0"))
    changed = set(git(source, "diff", "HEAD", "--name-only", "-z").decode().rstrip("\0").split("\0"))
    rows = []
    for name in sorted(tracked | untracked | published_files):
        file = source / name
        if not file.is_file():
            rows.append({"path": name, "local": "missing_or_submodule"})
            continue
        data = file.read_bytes()
        row = {"path": name, "bytes": len(data), "sha256": digest(data),
               "local": "untracked" if name in untracked else "modified" if name in changed else "HEAD"}
        if name in published_files:
            published_data = git(source, "show", f"{published}:{name}")
            row["published"] = comparison(data, published_data)
            row["published_sha256"] = digest(published_data)
        else:
            row["published"] = "absent"
        if name in upstream:
            upstream_data = git(target, "show", f"{base}:{name}")
            row["crosspoint"] = comparison(data, upstream_data)
            row["crosspoint_sha256"] = digest(upstream_data)
        else:
            row["crosspoint"] = "absent"
        rows.append(row)
    return {"yacp_head": git(source, "rev-parse", "HEAD").decode().strip(),
            "yacp_published_tag": published,
            "yacp_published_commit": git(source, "rev-parse", f"{published}^{{commit}}").decode().strip(),
            "crosspoint_base": git(target, "rev-parse", base).decode().strip(),
            "notes": ["HEAD is the reference checkout commit, not necessarily the last published release.",
                      "File differences are evidence for review, not decisions to port.",
                      "Ignored font sources have a separate archive manifest."], "files": rows}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--base", default="crosspoint-1.6.5")
    parser.add_argument("--published", default="v1.7.1-yacp")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.source.resolve(), Path(__file__).resolve().parents[1], args.base, args.published)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Audited {len(result['files'])} source entries")
