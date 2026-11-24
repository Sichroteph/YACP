"""Exercise a long CJK paragraph and SD-backed metadata on a synthetic book."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
from zipfile import ZipFile, ZIP_STORED
from PIL import Image
from verify_yacp_simulator_stats import create_fixture


def run(program, source_family, output, x3=False):
    program = program.resolve(strict=True)
    source_family = source_family.resolve(strict=True)
    output = output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    sd = create_fixture(output)
    shutil.copytree(source_family, sd / ".fonts" / source_family.name)
    title = "\u4e2d\u6587\u9605\u8bfb CJK Test"
    author = "\u6d4b\u8bd5 Author"
    paragraph = "\u8fd9\u662f\u4e00\u6bb5\u7528\u4e8e\u6d4b\u8bd5\u7535\u5b50\u4e66\u6392\u7248\u7684\u4e2d\u6587\u3002\u8bf7\u7ffb\u9875\u7ee7\u7eed\u9605\u8bfb\uff0c\u5e76\u4fdd\u7559\u6b63\u786e\u7684\u6587\u5b57\u4f4d\u7f6e\u3002" * 300
    with ZipFile(sd / "cjk.epub", "w", compression=ZIP_STORED) as book:
        book.writestr("mimetype", "application/epub+zip")
        book.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        book.writestr("book.opf", f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">yacp-cjk</dc:identifier><dc:title>{title}</dc:title><dc:creator>{author}</dc:creator><dc:language>zh</dc:language></metadata><manifest><item id="body" href="body.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="body"/></spine></package>')
        book.writestr("body.xhtml", f'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head><body><h1>{title}</h1><p>{paragraph}</p></body></html>')
    root = sd / ".crosspoint"
    settings = root / "settings.json"
    values = json.loads(settings.read_text())
    values.update(sdFontFamilyName=source_family.name, fontSize=14, trackReadingStats=0)
    settings.write_text(json.dumps(values), encoding="utf-8")
    (root / "recent.json").write_text(json.dumps({"books": [{"path": "/cjk.epub", "title": title, "author": author, "coverBmpPath": ""}]}), encoding="utf-8")
    actions = ("2500:CONFIRM:100;12000:RIGHT:100;18000:BACK:100;22500:QUIT" if x3 else
               "2500:TAP:240,350;12000:SWIPE:380,350,100,350;18000:HOME:100;22500:QUIT")
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", CROSSPOINT_SIM_SD=str(sd),
               CROSSPOINT_SIM_INPUT_SCRIPT=actions,
               CROSSPOINT_SIM_SCREENSHOTS=f"1900:{output}/home.bmp;10500:{output}/first.bmp;16000:{output}/next.bmp;21000:{output}/returned.bmp")
    with (output / "run.log").open("x", encoding="utf-8") as log:
        result = subprocess.run([str(program)], cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=35)
    assert result.returncode == 0
    logs = (output / "run.log").read_text()
    assert "Loaded SD font family: " + source_family.name in logs
    assert logs.count("Rendered page in") >= 2
    assert "Text layout dropped content" not in logs
    assert json.loads(settings.read_text())["sdFontFamilyName"] == source_family.name
    for name in ("home", "first", "next", "returned"):
        with Image.open(output / (name + ".bmp")) as image:
            image.save(output / (name + ".png"))
    report = {"reader_scenario_passed": True, "model": "x3" if x3 else "x4_pro", "paragraph_codepoints": len(paragraph),
              "metadata_visual_review_required": True, "hardware_heap_validated": False}
    (output / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("program", type=Path)
    p.add_argument("--source-family", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--x3", action="store_true")
    a = p.parse_args()
    run(a.program, a.source_family, a.output, a.x3)
