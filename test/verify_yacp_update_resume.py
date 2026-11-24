"""Verify reading positions when a pre-update SD card copy first boots, sleeps and wakes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from xml.sax.saxutils import escape
from zipfile import ZipFile, ZIP_STORED

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_yacp_source import source_manifest
from verify_yacp_fixed_progress import xtc_fixture
from verify_yacp_runtime_migration import hashes
from verify_yacp_simulator_stats import create_fixture
from yacp_data_formats import crosspoint48_page_offsets, progress_record


FORMATS = ("epub", "txt", "xtc")
SOURCE_FINGERPRINT = "168048a21974a6579beee3626617a9adb9e6a8a10566d0ff0b311d725686dcef"
PRO_SOURCE_FINGERPRINT = "9da4884b3734670eaaa4f6927e4f29621cdffdf3c57aa27799e3b932008c4e08"
CHAPTER_LINES = tuple(f"Marker {i:03d}. Café & ink. The north window is open." for i in range(1, 142))
ACTIVITIES = {"epub": "EpubReader", "txt": "EpubReader", "xtc": "XtcReader"}


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def file_digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_program(path, fingerprint, environment="simulator_x3"):
    path = path.resolve(strict=True)
    report = json.loads(path.with_suffix(".json").read_text(encoding="utf-8"))
    if not (
        report["build_succeeded"]
        and report["environment"] == environment
        and report["source"]["sha256"] == fingerprint
        and report["bytes"] == path.stat().st_size
        and report["sha256"] == file_digest(path)
    ):
        raise ValueError(f"Unexpected simulator identity: {path}")
    return path, {"version": report["version"], "source_sha256": fingerprint, "sha256": report["sha256"]}


def runtime_path(path):
    path = path.resolve()
    if os.name != "nt":
        return str(path)
    return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]


def launch(program, distro, folder, sd, label, inputs, shots, wake=False):
    env = {
        "SDL_VIDEODRIVER": "offscreen",
        "SDL_RENDER_DRIVER": "software",
        "CROSSPOINT_SIM_SD": runtime_path(sd),
        "CROSSPOINT_SIM_INPUT_SCRIPT": inputs,
        "CROSSPOINT_SIM_SCREENSHOTS": ";".join(
            f"{time}:{runtime_path(folder / (name + '.bmp'))}" for time, name in shots
        ),
        "CROSSPOINT_SIM_INPUT_SCRIPT_AFTER_WAKE": "7500:QUIT" if wake else "",
        "CROSSPOINT_SIM_SCREENSHOTS_AFTER_WAKE": (
            f"6000:{runtime_path(folder / 'after-wake.bmp')}" if wake else ""
        ),
    }
    log = folder / (label + ".log")
    if os.name == "nt":
        arguments = [key + "=" + value for key, value in env.items()]
        command = ["wsl", "-d", distro, "--", "sh", "-c",
                   shlex.join(["cd", runtime_path(folder)]) + " && exec " +
                   shlex.join(["env", *arguments, runtime_path(program)])]
        env = {}
    else:
        command = [str(program)]
    with log.open("x", encoding="utf-8") as stream:
        subprocess.run(command, cwd=folder, env=dict(os.environ, **env), stdout=stream,
                       stderr=subprocess.STDOUT, check=True, timeout=80)
    return log.read_text(encoding="utf-8", errors="replace")


def seed(folder, kind):
    sd = create_fixture(folder)
    cp = sd / ".crosspoint"
    if kind == "epub":
        book = sd / "completion.epub"
        with ZipFile(book) as archive:
            entries = {filename: archive.read(filename) for filename in archive.namelist()}
        body = "".join("<p>" + escape(line) + "</p>" for line in CHAPTER_LINES)
        entries["first.xhtml"] = (
            '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Position Test</title></head>'
            f"<body>{body}</body></html>"
        ).encode("utf-8")
        with ZipFile(book, "w", compression=ZIP_STORED) as archive:
            for filename, data in entries.items():
                archive.writestr(filename, data)
    else:
        book = sd / f"position.{kind}"
        if kind == "txt":
            book.write_bytes(("\r\n".join(CHAPTER_LINES) + "\r\n").encode("utf-8"))
        else:
            xtc_fixture(book)
    values = json.loads((cp / "settings.json").read_text(encoding="utf-8"))
    values.update(
        language="EN", yacpReaderFontInitialized=True, trackReadingStats=0,
        sleepScreen=6, shortPwrBtn=1, doubleClickPwrLight=0, statusBarChapterPageCount=0,
        statusBarBookProgressPercentage=0, statusBarProgressBar=2, statusBarTitle=2,
        statusBarBattery=0, statusBarBatteryCurrent=0, statusBarClock=0, clockEnabled=0,
        xtcStatusBarMode=0, moveFinishedToReadFolder=0
    )
    write_json(cp / "settings.json", values)
    write_json(cp / "recent.json", {"books": [
        {"path": "/" + book.name, "title": "Position Test", "author": "YACP QA", "coverBmpPath": ""}
    ]})
    return sd, book.name


def progress_file(sd, kind):
    matches = list((sd / ".crosspoint").glob(f"{kind}_*/progress.bin"))
    if len(matches) != 1:
        raise ValueError(f"Expected one {kind} progress file on the fixture card, found {matches}")
    return matches[0]


def frame(path):
    with Image.open(path) as image:
        normalized = image.convert("RGB")
        return normalized.size, normalized.tobytes()


def prepare(program, distro, case, kind, baseline_identity, *, pro=False):
    case.mkdir()
    source = case / "baseline"
    source.mkdir()
    sd, name = seed(source, kind)
    enter = "TAP:240,350" if pro else "CONFIRM:120"
    leave = "HOME:120" if pro else "BACK:120"
    log = launch(program, distro, source, sd, "before-update",
                 f"2000:{enter};7000:DOWN:120;9700:DOWN:120;13700:{leave};16000:QUIT",
                 [(5900, "opening"), (12500, "before-update")])
    baseline_activity = "TxtReader" if kind == "txt" else ACTIVITIES[kind]
    assert "Entering activity: " + baseline_activity in log
    assert "Exiting activity: " + baseline_activity in log
    assert frame(source / "opening.bmp") != frame(source / "before-update.bmp"), "The baseline never advanced"
    path = progress_file(sd, kind)
    raw = path.read_bytes()
    if kind == "xtc":
        (page,) = struct.unpack("<I", raw)
        assert page == 2, raw
        position = {"page": page}
    elif kind == "txt":
        page, byte_offset = struct.unpack("<HI", raw)
        assert page == 2 and byte_offset > 0, raw
        # This controlled TXT uses CRLF with no trailing spaces. The generated
        # HTML has one initial body newline, and a <br> inserts no text code point.
        prefix = (sd / name).read_bytes()[:byte_offset].decode("utf-8")
        anchor = 1 + len(prefix.replace("\r", "").replace("\n", ""))
        position = {"page": page, "byte_offset": byte_offset, "anchor": anchor, "spine": 0}
    else:
        old = progress_record(raw)
        assert old["spine"] == 0 and old["page"] == 2 and old["visible_text_offset"] > 0, old
        old_pages = crosspoint48_page_offsets((path.parent / "sections/0.bin").read_bytes())
        assert old_pages[old["page"]] == old["visible_text_offset"], old
        position = {"page": old["page"], "anchor": old["visible_text_offset"], "spine": old["spine"]}
    meta = {
        "build": baseline_identity, "book": name, "format": kind, "position": position,
        "progress_path": path.relative_to(sd).as_posix(), "progress_hex": raw.hex(),
        "sd_sha256": hashes(sd), "before_update_sha256": file_digest(source / "before-update.bmp"),
    }
    write_json(case / "baseline.json", meta)
    return meta


def read_section74(path):
    data = path.read_bytes()
    # See lib/Epub/Epub/Section.cpp and the section 74 format in docs/file-formats.md.
    header_size = 47
    partial = 0xFE - (74 - 28)
    if len(data) < header_size or data[0] not in (74, partial):
        raise ValueError("Expected a CrossPoint 2.0.0 + YACP section 74")
    count, pages_lut = struct.unpack_from("<HI", data, header_size - 22)
    (visible_lut,) = struct.unpack_from("<I", data, header_size - 4)
    if not count or not header_size <= pages_lut <= visible_lut - count * 4 or visible_lut + count * 4 > len(data):
        raise ValueError("Invalid section 74 lookup tables")
    if data[0] == partial:
        end = visible_lut + count * 4
        if end + 8 != len(data):
            raise ValueError("Invalid section 74 partial trailer")
        consumed, total = struct.unpack_from("<II", data, end)
        if not 0 < consumed <= total:
            raise ValueError("Invalid section 74 partial watermark")
    offsets = struct.unpack_from(f"<{count}I", data, visible_lut)
    if any(a > b for a, b in zip(offsets, offsets[1:])):
        raise ValueError("Unordered visible-text offsets")
    return offsets


def check_position(sd, meta):
    kind = meta["format"]
    path = progress_file(sd, "xtc" if kind == "xtc" else "epub")
    data = path.read_bytes()
    if kind == "xtc":
        assert data.hex() == meta["progress_hex"], (data, meta["progress_hex"])
        return {"page": meta["position"]["page"], "progress_hex": data.hex()}
    position = progress_record(data)
    target = meta["position"]
    assert position["spine"] == target["spine"], (target, position)
    offsets = read_section74(path.parent / "sections" / f"{position['spine']}.bin")
    page = position["page"]
    assert 0 <= page < len(offsets), position
    lower = offsets[page]
    # The fixtures deliberately leave more pages after the target. One more
    # persisted page is required to prove the upper bound of this passage.
    assert page + 1 < len(offsets), position
    upper = offsets[page + 1]
    assert position["visible_text_offset"] == lower, position
    assert lower <= target["anchor"] < upper, (target, lower, upper)
    return {"spine": position["spine"], "page": page, "first_offset": lower, "next_offset": upper,
            "progress_hex": data.hex()}


def acceptance(program, distro, case, meta, identity, run_label, *, pro=False):
    before = case / "baseline"
    sd = case / run_label / "sd"
    folder = sd.parent
    original = meta["sd_sha256"]
    assert hashes(before / "sd") == original, "The baseline card has changed"
    assert file_digest(before / "before-update.bmp") == meta["before_update_sha256"]
    shutil.copytree(before / "sd", sd)
    assert hashes(sd) == original, "The test card differs before the update"
    activity = ACTIVITIES[meta["format"]]

    enter = "TAP:240,350" if pro else "CONFIRM:120"
    leave = "HOME:120" if pro else "BACK:120"
    log = launch(program, distro, folder, sd, "after-update",
                 f"2400:{enter};15500:{leave};18000:QUIT", [(14300, "after-update")])
    assert "Entering activity: " + activity in log and "Exiting activity: " + activity in log
    upgraded = check_position(sd, meta)
    if meta["format"] == "txt":
        # The migrated slot must retain the exact old progress and TXT index.
        cache_prefix = meta["progress_path"].split("/")[1] if meta["progress_path"].startswith(".crosspoint/") else ""
        retained = sd / ".yacp-reentry-v2/history/1/original" / cache_prefix
        assert cache_prefix.startswith("txt_") and retained.is_dir()
        previous = original[f".crosspoint/{cache_prefix}/progress.bin"]
        assert file_digest(retained / "progress.bin") == previous
        index = f".crosspoint/{cache_prefix}/index.bin"
        if index in original:
            assert file_digest(retained / "index.bin") == original[index]
        assert not list((sd / ".crosspoint").glob("txt_*/progress.bin"))
    elif meta["format"] == "xtc":
        assert frame(before / "before-update.bmp") == frame(folder / "after-update.bmp"), "XTC page pixels changed"

    log = launch(program, distro, folder, sd, "sleep-wake",
                 f"2200:{enter};11500:POWER:140;17500:POWER:1200",
                 [(9800, "before-sleep")], wake=True)
    assert log.count("Entering activity: Sleep") == 1 and log.count("Entering deep sleep") == 1
    assert log.count("Entering activity: " + activity) == 2, "Wake did not reopen the reader"
    assert frame(folder / "after-update.bmp") == frame(folder / "before-sleep.bmp"), "Cold reopen changed pixels"
    assert frame(folder / "before-sleep.bmp") == frame(folder / "after-wake.bmp"), "Wake changed the visible page"
    resumed = check_position(sd, meta)
    # These records include the estimated total chapter pages. Background
    # pagination can change that estimate without changing the reading position.
    semantic = ("page",) if meta["format"] == "xtc" else ("spine", "page", "first_offset", "next_offset")
    assert all(upgraded[key] == resumed[key] for key in semantic), (upgraded, resumed)
    assert file_digest(sd / meta["book"]) == original[meta["book"]]
    assert hashes(before / "sd") == original
    result = {
        "passed": True, "format": meta["format"], "baseline": meta["position"], "after_update": upgraded,
        "after_wake": resumed, "baseline_sd_unchanged": True, "display_equal_after_wake": True,
        "old_txt_archive_verified": meta["format"] == "txt", "build": identity,
        "hardware_validated": False,
    }
    write_json(folder / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", type=Path, help="Omit to only generate and record the pre-update cards")
    parser.add_argument("--reuse-baseline", action="store_true")
    parser.add_argument("--run-label", default="candidate", help="A fresh output folder name for each candidate run")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--distro", default="YACP-Simulator")
    parser.add_argument("--profile", choices=("x3", "x4-pro"), default="x3")
    args = parser.parse_args()
    output = args.output.resolve()
    if not args.run_label.isascii() or not args.run_label.replace("-", "").isalnum():
        parser.error("--run-label must contain only ASCII letters, numbers or hyphens")
    if "nextcloud" in {part.casefold() for part in output.parts}:
        raise ValueError("Use a private local test directory")
    pro = args.profile == "x4-pro"
    baseline_environment = "simulator_x4_pro" if pro else "simulator_x3"
    candidate_environment = "simulator_x4_pro_uc8279" if pro else "simulator_x3"
    baseline, baseline_identity = verify_program(
        args.baseline, PRO_SOURCE_FINGERPRINT if pro else SOURCE_FINGERPRINT, baseline_environment
    )
    if args.reuse_baseline:
        if not args.candidate:
            parser.error("--reuse-baseline requires --candidate")
    else:
        output.mkdir(parents=True, exist_ok=False)

    if args.candidate:
        candidate, identity = verify_program(args.candidate, source_manifest()["sha256"], candidate_environment)

    def run(kind):
        case = output / kind
        if args.reuse_baseline:
            meta = json.loads((case / "baseline.json").read_text(encoding="utf-8"))
            assert meta["build"] == baseline_identity and meta["format"] == kind
        else:
            meta = prepare(baseline, args.distro, case, kind, baseline_identity, pro=pro)
        if not args.candidate:
            return {"format": kind, "baseline": meta["position"]}
        return acceptance(candidate, args.distro, case, meta, identity, args.run_label, pro=pro)

    # WSL 1 can fail to create a session when several wsl.exe processes start together.
    cases = [run(kind) for kind in FORMATS]
    if args.candidate and source_manifest()["sha256"] != identity["source_sha256"]:
        raise RuntimeError("The canonical source changed during validation")
    report = {"passed": bool(args.candidate), "profile": args.profile, "prepared": True,
              "hardware_validated": False, "cases": cases}
    report_name = f"result-{args.run_label}.json" if args.candidate else "baseline-result.json"
    write_json(output / report_name, report)
    print(json.dumps(report))


if __name__ == "__main__":
    main()
